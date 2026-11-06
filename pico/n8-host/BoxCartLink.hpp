#pragma once

// The box's cart link (docs/modular-graph-phase-e3-design.md section 5): rp::n8::CartLink (n8/CartLink.hpp) over the
// main loop's one Edio, with no thread of its own. rp.n8.nes pushes into a fixed ring during the block; after the
// block the main loop drains the ring to the cart's FIFO in writes of up to 192 bytes (drain()), so a push never waits
// on the N8, and block work and N8 work never overlap, now and when an audio driver sets the cadence.
//
// - push() copies the message into the ring and answers false, counted, when no game is running (the box's forwarding
//   state, which its N8 part sets each pass through setConnected(): a game is running and the box forwards to it) or
//   when the ring is full.
// - drain() writes whole messages back to back, as many as fit in 192 bytes (a write costs the same ~5 ms whatever it
//   carries, so batching is what lets a burst through), and splits only a message longer than that (a SysEx). A
//   message is counted forwarded, or failed, in the write that carries its last byte.
// - releaseNotes() queues a note-off for each note the same way, behind what was already pushed.
// - setExpansionVolume() records the value (clamped to the register's 0..255, or -1 to leave the register to the
//   cart), and drain() writes it to the cart's config register while connected: at every connect and on a change, as
//   the desktop's N8Host does (and a failed write again a second later). Its -1 (N8Host::EXP_VOL_AUTO) writes nothing
//   here: the desktop's rule for it (unity for an expansion mapper, else the N8 OS's value) is for a link no node
//   holds. The bridge plan's rp.n8.nes never leaves it at -1, as it gives its value at its first block.
// - acquire() and release() are the hooks the box installs the link with (rp.host.devices/2), counting the nodes that
//   hold it; the last release puts the volume back to -1, as N8CartLink does.
// - No device jobs: the box offers no rp.host.jobs/1, so rp.n8.nes refuses a job before it reaches the link, and the
//   link's own answer is CartLink's ("not connected").
//
// What it counts is the plan's edge into the cart, which the box's stats read without reaching into the plan: the
// pushes it was handed, and of them the ones rp.n8.nes made itself, the note-offs it owes for notes whose note-off a
// disconnect dropped and pays at the next connected block. The link keeps the node's books on its own answers to tell
// those apart (sounding: a note-on taken, no note-off taken since; owed: a sounding note whose note-off it refused),
// the rules N8Node.cpp's pushMidi keeps, so the box's `filtered` (staged minus what the link was handed from the
// plan's input) stays what the plan dropped. That holds for MIDI pushed through the node's `midi` input, the only input
// the box's plan wires.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does. One thread: the block and the drain both
// run on the box's main loop, which also sets the connected state.

#include <cstddef>
#include <cstdint>

#include "graph/rp_device_midi_in.h"
#include "n8/CartLink.hpp"

namespace rp::box {

class BoxCartLink final : public rp::n8::CartLink {
public:
    // Room for one block's MIDI (rp.device.midi-in's queue, which rp.core.midi-filter's matches) twice over, so the
    // note-offs the node owes after a reconnect fit beside a full block: the ring is emptied after every block.
    static constexpr std::uint32_t kMessages = 2 * RP_DEVICE_MIDI_IN_MESSAGES;
    static constexpr std::uint32_t kBytes = 2 * RP_DEVICE_MIDI_IN_BYTES;
    // The most one FIFO write carries.
    static constexpr std::uint32_t kBatch = 192;
    // How long a failed volume write waits before it is tried again.
    static constexpr std::uint32_t kVolumeRetryMs = 1000;

    // Where the main loop's writes go: the cart's FIFO (`messages` of them end in `data`, and `firstUs` is when the
    // first message it carries arrived), and the cart's expansion-volume register (`again` on a retry of a write that
    // failed). Each answers whether the write reached the cart: a volume written whose read-back differs is written,
    // not retried (the cart's register is its own business from there; N8Host reads nothing back).
    struct Writer {
        void* ctx;
        bool (*fifo)(void* ctx, const std::uint8_t* data, std::size_t size, std::uint32_t messages,
                     std::uint32_t firstUs);
        bool (*volume)(void* ctx, int value, bool again);
    };
    // When what the plan pushes arrived: `arrival` answers it for a message the block handed the plan at `frame` with
    // these bytes (MidiStaging::arrival), `now` for one the plan made itself. Microseconds, the low 32 bits.
    struct Clock {
        void* ctx;
        std::uint32_t (*now)(void* ctx);
        bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us);
    };

    struct Counts {
        std::uint32_t received = 0;  // pushes, taken or refused
        std::uint32_t made = 0;      // of them, the note-offs rp.n8.nes owed (see above)
        std::uint32_t notReady = 0;  // pushes refused: no game running
        std::uint32_t full = 0;      // pushes refused: the ring full
        std::uint32_t released = 0;  // note-offs releaseNotes queued
        std::uint32_t forwarded = 0; // messages in writes the cart took
        std::uint32_t failed = 0;    // messages in writes that failed
        std::uint32_t sysex = 0;     // of the forwarded, SysEx
        std::uint32_t writes = 0;    // FIFO writes
        std::uint32_t maxBatch = 0;  // the most messages one write carried

        // What the plan handed the link from its input: its pushes, less the node's own note-offs.
        std::uint32_t fromPlanInput() const { return received - made; }
    };

    // The ring and the books, kept apart from the link in a plain struct the box holds in .bss: a polymorphic object
    // is initialised data (its vtable pointer), and what it held inline would be copied out of flash at boot with it.
    struct Message {
        std::uint32_t at;  // arrival, microseconds
        std::uint16_t size;
        bool          sysex;
    };
    struct Storage {
        std::uint8_t      bytes[kBytes];
        Message           messages[kMessages];
        std::uint8_t      batch[kBatch];
        rp::n8::CartNotes sounding, owed;
    };

    constexpr explicit BoxCartLink(Storage& storage) : s_(storage) {}
    void setClock(const Clock& clock) { clock_ = clock; }

    // rp::n8::CartLink.
    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
              double sampleRate) override;
    bool          connected() const override { return connected_; }
    std::uint64_t dropped() const override { return std::uint64_t{counts_.notReady} + counts_.full; }
    void          setExpansionVolume(int value) override;
    void          releaseNotes(const rp::n8::CartNotes& notes) override;

    // The box's side. Whether a game is running and the box forwards to it: a connect has the volume written again,
    // and a disconnect drops what is queued (counted not ready), as nothing may reach a cart that went away.
    void setConnected(bool connected);
    // The volume while connected, then the ring, in writes of up to kBatch bytes, until it is empty.
    void drain(const Writer& writer, std::uint32_t nowMs);
    void acquire() { ++holders_; }
    void release();

    const Counts& counts() const { return counts_; }
    int           expansionVolume() const { return volume_; }
    std::uint32_t queued() const { return messageCount_; }
    int           holders() const { return holders_; }

private:
    bool enqueue(const std::uint8_t* data, std::uint32_t size, std::uint32_t at);
    void book(const std::uint8_t* data, std::size_t size, bool taken);
    void discard();

    Storage& s_;
    Clock    clock_{};
    bool     connected_ = false;

    // The ring (s_.bytes and s_.messages): messages and their bytes, each in a circle of its own.
    std::uint32_t byteHead_ = 0, byteCount_ = 0;
    std::uint32_t messageHead_ = 0, messageCount_ = 0;
    std::uint32_t frontSent_ = 0;  // the bytes of the oldest message already written (a split one)

    int           volume_ = -1;
    bool          volumePending_ = false;
    bool          volumeWaiting_ = false;  // a failed write waits until retryAtMs_
    std::uint32_t retryAtMs_ = 0;
    int           holders_ = 0;

    Counts counts_{};
};

}  // namespace rp::box
