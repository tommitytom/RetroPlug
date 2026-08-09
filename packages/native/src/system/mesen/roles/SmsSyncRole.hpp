#pragma once

#include <cstdint>
#include <deque>

class SmsControlManager;
class SmsMemoryManager;

// Host-transport sync for the Master System controller-port transport. The DAW-side role hands this
// a level byte tagged with an intra-block sample offset; this releases it onto the emulated
// controller port at that offset, so a tracker polling $DD sees the host's clock at its true sample
// position instead of at the block boundary.
//
// THE PAYLOAD IS A LEVEL, NOT A MESSAGE, and that difference drives every design choice below.
// smsggdj carries its clock as a 2-bit counter held on two controller lines and samples it once per
// video frame (/workspaces/smsggdj/src/engine.asm sync_read + sync_in_delta); nothing measures edges
// or pulse widths. So this is "set line L to level V at offset F and HOLD it", not a byte stream.
//
// The level byte, verified against the ROM (engine.asm:576-590) and against the external-input test
// in test/audio/SmsAudio.test.cpp. Counter bits are active HIGH at the port: a line reading high is
// a 1.
//
//     levels = 0xFF
//     if (!(counter & 1)) levels &= ~0x08   // TR low
//     if (!(counter & 2)) levels &= ~0x80   // TH low
//     // TL (bit 2) stays high, so the ROM's "TR AND TL" reduces to TR, which is what a straight
//     // 3-wire cable produces and the case the AND was written for.
//
// GAME GEAR CARRIES THE SAME COUNTER ON DIFFERENT PINS, so the level word differs and the sink does
// too (see onAttach). The GG build reads the EXT parallel port $01, whose bits are PC0-PC6, with
// counter bit 0 = PC4 AND PC5 and counter bit 1 = PC6 (smsggdj GGSYNC.md). This class is agnostic:
// the encoding is the TS role's job, and only the destination is decided here.
//
// The counter state machine itself (START -> 0, CLOCK -> +1 while running, STOP -> freeze, not
// reset) lives in the TS role, mirroring the reference hardware bridge at
// /workspaces/smsggdj/adapter/src/sync_protocol.c. This class only schedules and applies.
//
// Structurally a NesN8FifoRole (packages/native/src/system/mesen/roles/NesN8FifoRole.hpp) with the
// FIFO swapped for a control-port write, and with flushAll dropped: risa's arm is a barrier because
// a byte queued for an abandoned position corrupts the stream, whereas a held level has no such
// hazard - a DAW seek is just a counter that keeps counting.
class SmsSyncRole {
public:
    // Bind to the console's managers. Called once, on the audio thread, from
    // MesenSmsSystem::onActivate after the pointers have been cached.
    //
    // `gameGear` picks the SINK, because the two machines carry the same counter on different
    // hardware. Master System reads controller port 2 ($DD), so the level goes to the control
    // manager's external-input mask. Game Gear has dedicated link I/O and its build reads the EXT
    // parallel port ($01 / PC0-PC6) instead, whose bits map straight to the pins - so the level goes
    // to the memory manager. Which machine is not something the DAW-side role has to know for
    // ROUTING (only for the bit layout it encodes); the system already knows, so it decides here.
    void onAttach(SmsControlManager& controlManager, SmsMemoryManager* memoryManager, bool gameGear) {
        controlManager_ = &controlManager;
        memoryManager_ = memoryManager;
        gameGear_ = gameGear;
    }
    void onDetach() { controlManager_ = nullptr; memoryManager_ = nullptr; clear(); }

    // Audio-thread: QUEUE each byte in `data` as a level tagged with `offset` (samples from block
    // start). Released by pumpUntil once the block's emulated position reaches that offset.
    //
    // `flush` is accepted and IGNORED, unlike the N8 FIFO's. It exists on the SystemBase seam for
    // byte protocols that need a barrier; a level has no undelivered-stream hazard to clear, and
    // dropping a queued level on a seek would leave the line stale at whatever it happened to hold.
    void pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool flush = false);

    // Audio-thread: apply every queued level whose offset has been reached, front-first. Called from
    // MesenSmsSystem's step loop with intraBlockSamplePos() - the Z80 cycle position, NOT the audio
    // ring depth, which lags by up to a flush window.
    //
    // COALESCING IS CORRECT HERE, not a shortcut. If several levels come due between two of the
    // ROM's once-per-frame polls, only the last is visible to it - and the protocol is built for
    // exactly that: the slave reads (current - last) & 3, so up to 3 clocks between polls survive as
    // a delta. Applying each in order and letting the last stand IS the intended behaviour. Do not
    // "fix" this into something that forces every intermediate level to be observed; that would
    // break the timing it appears to protect.
    void pumpUntil(std::uint32_t sampleOffset);

    // Audio-thread: at block end, shift still-queued offsets back by `frames` so a level that did
    // not come due this block keeps its relative timing next block (already-due levels clamp to 0).
    void rebase(std::uint32_t frames);

    // Drop queued (not-yet-applied) levels. The line itself is left where it is: on a reset the core
    // rebuilds its devices anyway, and the external mask is manager state that survives that.
    void clear() { pending_.clear(); needsSort_ = false; }

    // Introspection / tests.
    std::size_t   pendingCount() const { return pending_.size(); }
    std::uint8_t  lastApplied()  const { return lastApplied_; }

private:
    struct PendingLevel {
        std::uint32_t offset;   // samples from block start
        std::uint8_t  levels;
    };
    std::deque<PendingLevel> pending_;
    bool                     needsSort_    = false;  // set on enqueue; pumpUntil stable-sorts once
    std::uint8_t             lastApplied_  = 0xFF;   // idle: every line released
    SmsControlManager*       controlManager_ = nullptr;
    SmsMemoryManager*        memoryManager_  = nullptr;  // Game Gear EXT port sink; null on SMS
    bool                     gameGear_       = false;
};
