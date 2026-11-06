#pragma once

// A box's MIDI input (docs/modular-graph-phase-e3-design.md section 5): the bytes off its MIDI UART, each with the
// moment it arrived, parsed into whole messages by pico/midi-in's parser (midi.c) and staged for rp.device.midi-in,
// which takes them through the host's rp.device.midi-in/1 (graph/rp_device_midi_in.h) at the frame of the block each
// arrival falls on. The host filters nothing: every message is staged as it came, with its status byte (a message the
// sender sent under running status gets its status back), system common and real time among them, save a complete
// SysEx the product's hook takes (the box's own F0 7D 50 commands). What one cart wants dropped is its plan's business.
//
// A SysEx is staged whole, once its F7 (or the status byte that cut it short, for which the parser supplies the F7)
// has arrived, so it is one message of one kind and a filter that drops `sysex` drops all of it. One past the 4 KB a
// message may be (RP_DEVICE_MIDI_IN_BYTES) is dropped whole and counted.
//
// What a block takes is bounded (RP_DEVICE_MIDI_IN_MESSAGES and _BYTES, rp.device.midi-in's queue), and feed() is
// bounded the same way: it refuses a byte when the staging could not take the message that byte might complete, taking
// nothing, and the caller leaves the byte in its ring for the next pass, after the block has emptied the staging. So
// the staging loses nothing to its own size, and a burst past one block's worth waits a pass rather than being dropped.
//
// The host ends each block itself (endBlock(), after runBlock): what the block did not take is dropped and counted,
// whatever the plan is. A plan with no rp.device.midi-in, or one whose midi-in the executor has silenced for a fault,
// takes nothing, and the staging must still empty, or feed() would refuse every byte from then on and the parse (the
// box's own commands among it) would stop. A host with no plan at all drops what is staged each pass
// (clearStaged()).
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does. One thread: the main loop feeds it and runs
// the block whose rp.device.midi-in drains it.

#include <cstddef>
#include <cstdint>

#include "graph/rp_device_midi_in.h"
#include "midi.h"

namespace rp::box {

class MidiStaging {
public:
    // What one block takes: rp.device.midi-in's queue, which the host stages no more than.
    static constexpr std::uint32_t kMessages = RP_DEVICE_MIDI_IN_MESSAGES;
    static constexpr std::uint32_t kBytes = RP_DEVICE_MIDI_IN_BYTES;
    static_assert(kMessages == 64 && kBytes == 4096, "the box stages what rp.device.midi-in takes in a block");
    // The longest message, a SysEx with its F0 and F7: the most one message may carry in rp.device.midi-in's queue.
    static constexpr std::uint32_t kSysexBytes = RP_DEVICE_MIDI_IN_BYTES;

    // A complete SysEx, F0 to F7, before it is staged: true takes it (it is not staged). The box's own commands.
    using SysexHook = bool (*)(void* ctx, const std::uint8_t* data, std::uint32_t size);
    // The frame a moment falls on within the block being run (ElapsedDriver::frameOf): what next() stamps each message
    // with, read when the block takes it.
    using FrameOf = std::uint32_t (*)(void* ctx, std::uint32_t us);

    struct Counts {
        std::uint32_t channelMessages = 0;  // complete channel-voice messages parsed (the box's `msgs`)
        std::uint32_t handed = 0;           // messages handed to the plan (rp.device.midi-in took them)
        std::uint32_t consumed = 0;         // SysEx messages the hook took
        std::uint32_t tooLong = 0;          // SysEx messages past kSysexBytes, dropped whole
        std::uint32_t untaken = 0;          // staged messages no block took (endBlock, clearStaged), dropped
    };

    // Zero until init(), which runs before the first byte: a box's one staging is in static storage, in .bss, with
    // nothing copied out of flash for it.
    constexpr MidiStaging() = default;
    // init() hands out this object's address (the interface struct), so a copy would point back at the original.
    MidiStaging(const MidiStaging&) = delete;
    MidiStaging& operator=(const MidiStaging&) = delete;
    void init(SysexHook hook, void* hookCtx, FrameOf frameOf, void* frameCtx);

    // One byte off the UART and when it arrived (the low 32 bits of the microsecond clock): false, taking nothing,
    // when the staging could not hold what it might complete.
    bool feed(std::uint8_t byte, std::uint32_t arrivedUs);
    // Drops what is staged and the parse in progress, as a stream starting over: the box's stale MIDI, discarded when
    // forwarding begins.
    void reset();
    // Drops what is staged and keeps the parse, counted: for a host with no plan to take it, so its parse (the box's
    // commands) goes on.
    void clearStaged();
    // After a block: drops what it did not take, counted, and stages what waited for its room (a SysEx held whole, and
    // the short message behind it) for the next block, with no byte to wait for.
    void endBlock();

    // The interface rp.device.midi-in reads, whose next() hands the staged messages out in order, each at its frame.
    const rp_device_midi_in* interface() const { return &interface_; }
    // [audio] When the message the block handed out at `frame` with these bytes arrived, for a link timing what it was
    // given: the first one this block handed out at that frame with the same bytes, not yet asked for, the messages
    // asked for in order (a node keeps its input's order). A message the plan dropped is passed over, since none with
    // its bytes is asked for in its place (a filter keeps or drops by kind, so equal bytes share a fate). False for a
    // message none was handed out as, which a node's own message (rp.n8.nes's note-offs) is, with one exception: a
    // node's own message asked for first, at a frame and with the bytes of one the block handed out (an owed note-off
    // paid at frame 0 when the input carries the same note-off at frame 0), takes that one's arrival, and the input's
    // own push, and any earlier one at that frame with other bytes, find none and are timed from now. Only the latency
    // figure moves, in the block after a reconnect.
    bool arrival(std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us);

    const Counts& counts() const { return counts_; }
    std::uint32_t staged() const { return count_ - read_; }

private:
    struct Entry {
        std::uint32_t at;      // arrival, microseconds
        std::uint32_t frame;   // the frame next() handed it out at
        std::uint16_t offset;  // into bytes_
        std::uint16_t size;
    };

    static void onMessage(const midi_message* m, void* user);
    static void onSysex(std::uint8_t b, void* user);
    static bool nextFn(void* ctx, std::uint32_t frames, rp_device_midi_message* out);

    bool fits(std::uint32_t size) const { return count_ < kMessages && size <= kBytes - used_; }
    void stage(const std::uint8_t* data, std::uint32_t size, std::uint32_t at);
    void place(const std::uint8_t* data, std::uint32_t size, std::uint32_t at);
    void sysexDone();
    bool flushPending();

    SysexHook hook_ = nullptr;
    void*     hookCtx_ = nullptr;
    FrameOf   frameOf_ = nullptr;
    void*     frameCtx_ = nullptr;
    rp_device_midi_in interface_{};  // init() points it here

    midi_parser   parser_{};
    std::uint32_t now_ = 0;  // the arrival of the byte being parsed

    // The staging: the messages of the next block, their bytes back to back.
    Entry         entries_[kMessages] = {};
    std::uint8_t  bytes_[kBytes] = {};
    std::uint32_t count_ = 0, used_ = 0, read_ = 0;

    // The SysEx being parsed, and once complete, held while the staging has no room for it (`sysexHeld_`), with at
    // most one short message the same byte completed behind it (`short_`), so the order is kept.
    std::uint8_t  sysex_[kSysexBytes] = {};
    std::uint32_t sysexSize_ = 0;
    std::uint32_t sysexAt_ = 0;
    bool          sysexOpen_ = false, sysexOver_ = false, sysexHeld_ = false;
    std::uint8_t  short_[3] = {};
    std::uint32_t shortSize_ = 0, shortAt_ = 0;

    std::uint32_t asked_ = 0;  // arrival()'s place among what this block handed out

    Counts counts_{};
};

}  // namespace rp::box
