#pragma once

#include <cstdint>
#include <deque>

#include "system/mesen/NesEverdriveFifo.hpp"
#include "transport/MidiTypes.hpp"

class NesConsole;

// EverDrive N8 Pro FIFO emulator wrapper. Attaches to a NesConsole's memory
// manager so the ROM's reads/writes at $40F0/$40F1 reach the FIFO; sample-offset
// schedules bytes into the FIFO's RX queue so the ROM's `$40F0` polling loop
// sees them. Two feeds share the one queue: host MIDI (`onMidi`, MIDI-framed)
// and a RAW byte stream (`pushBytes`, no framing/cap — e.g. a tracker's own
// sync/locate protocol over the N8 transport).
//
// Always attached when MesenNesSystem activates with a NES ROM — the FIFO is
// benign if the ROM never touches $40F0/$40F1 (most NES homebrew). If non-N8
// NES ROMs become a real concern, gate attachment on an iNES mapper-byte sniffer.
class NesN8FifoRole {
public:
    NesN8FifoRole();
    ~NesN8FifoRole();

    // Register the FIFO with `console`'s memory manager so memory accesses at
    // $40F0/$40F1 are routed through it. Called once, on the audio thread,
    // from MesenNesSystem::onActivate after the ROM has loaded.
    void onAttach(NesConsole& console);

    // Audio-thread: QUEUE each event's bytes tagged with the event's intra-block sample offset
    // (ev.frame) instead of delivering immediately. The bytes are released into the FIFO by pumpUntil
    // as the block's sample progress reaches each offset — so host MIDI keeps its timing rather than
    // collapsing to the block start (mirrors SameBoy's serial gate). The ROM polls `$40F1` bit 7
    // (FIFO_MOS_RXF: set = no data); reading `$40F0` pops the next byte. Bytes of one event share its
    // offset; order is preserved (the FIFO is order-sensitive — status byte then data bytes).
    void onMidi(const ::MidiEvent* events, std::uint32_t count);

    // Audio-thread: QUEUE `count` RAW bytes tagged with `offset` (samples from block start), no MIDI
    // framing and no length cap — for a byte protocol carried over the N8 transport (e.g. a tracker's
    // host-sync locate/clock stream). Bytes stay contiguous + ordered; released by pumpUntil alongside
    // any MIDI bytes, so raw + MIDI interleave by offset. `onMidi` is a thin MIDI-framed adapter over this.
    void pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count);

    // Audio-thread: release every queued byte whose offset has been reached (offset <= sampleOffset)
    // into the FIFO, front-first. Called from MesenNesSystem's step loop with the current intra-block
    // sample offset. The queue is stable-sorted by offset on the first call after any enqueue (all
    // enqueuing happens before the step loop), so bytes released front-first are in true sample order
    // even when two feeds (MIDI + raw) interleave out of enqueue order — subsequent calls are a plain
    // front-drain.
    void pumpUntil(std::uint32_t sampleOffset);

    // Audio-thread: at block end, shift still-queued offsets back by `frames` so a byte that didn't
    // fire this block keeps its relative timing next block (already-due bytes clamp to 0).
    void rebase(std::uint32_t frames);

    // Drop all queued (not-yet-delivered) bytes — on reset, to avoid stale MIDI after a state change.
    void clear() { pending_.clear(); needsSort_ = false; }

    // Introspection / tests.
    std::size_t pendingCount() const { return pending_.size(); }
    std::size_t fifoRxCount() { return fifo_.rxCount(); }
    // Pop the next delivered byte from the FIFO (as the ROM would via $40F0), or -1 if empty.
    int readFifoByte() {
        if (fifo_.ReadRam(0x40F1) == 0x80) return -1;  // bit7 set = no data
        return fifo_.ReadRam(0x40F0);
    }

private:
    struct PendingByte {
        std::uint32_t offset;   // samples from block start
        std::uint8_t  byte;
    };
    std::deque<PendingByte> pending_;
    bool                    needsSort_ = false;  // set on enqueue; pumpUntil stable-sorts by offset once
    rp::NesEverdriveFifo    fifo_;
};
