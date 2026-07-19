// Guards NES host-MIDI intra-block timing: NesN8FifoRole now QUEUES each event's bytes tagged with the
// event's sample offset (ev.frame) and releases them into the N8 FIFO only once the block's sample
// progress reaches that offset (MesenNesSystem::stepIfBelowTarget drives pumpUntil). Previously every byte
// was pushed at the block start (frame-0 collapse) — the NES equivalent of the SameBoy serial fix.
//
// These checks exercise the role's gate/rebase directly (no emulator needed — the queue/FIFO don't touch
// the CPU), the way SameBoySerialTiming does for the GB serial gate. Run via `pnpm test:plugin`.

#include <cstdint>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "system/mesen/roles/NesN8FifoRole.hpp"
#include "transport/MidiTypes.hpp"

namespace {

::MidiEvent noteOn(std::uint32_t frame, std::uint8_t pitch) {
    ::MidiEvent e{};
    e.frame = frame;
    e.size  = 3;
    e.data[0] = 0x90;  // NoteOn ch1
    e.data[1] = pitch;
    e.data[2] = 100;
    return e;
}

} // namespace

TEST_CASE("NES N8 MIDI bytes release at their intra-block offset, in order", "[audio][nes]") {
    NesN8FifoRole role;

    // Two events in one block: A near the start (offset 100), B near the end (offset 7000).
    const ::MidiEvent a = noteOn(100, 60);
    const ::MidiEvent b = noteOn(7000, 67);
    role.onMidi(&a, 1);
    role.onMidi(&b, 1);

    // Queued, nothing delivered yet.
    CHECK(role.pendingCount() == 6);   // 3 bytes each
    CHECK(role.fifoRxCount() == 0);

    // Before A's offset: still nothing.
    role.pumpUntil(50);
    CHECK(role.pendingCount() == 6);
    CHECK(role.fifoRxCount() == 0);

    // Reaching A's offset releases A's 3 bytes only; B stays queued.
    role.pumpUntil(150);
    CHECK(role.pendingCount() == 3);
    CHECK(role.fifoRxCount() == 3);
    // ...and they are A's bytes, in order (the FIFO the ROM reads).
    CHECK(role.readFifoByte() == 0x90);
    CHECK(role.readFifoByte() == 60);
    CHECK(role.readFifoByte() == 100);
    CHECK(role.readFifoByte() == -1);  // FIFO drained; B not delivered

    // Reaching B's offset releases B.
    role.pumpUntil(7000);
    CHECK(role.pendingCount() == 0);
    CHECK(role.fifoRxCount() == 3);
    CHECK(role.readFifoByte() == 0x90);
    CHECK(role.readFifoByte() == 67);
    CHECK(role.readFifoByte() == 100);
}

TEST_CASE("NES N8 MIDI offsets past the block end rebase into the next block", "[audio][nes]") {
    NesN8FifoRole role;
    constexpr std::uint32_t kFrames = 512;

    // An event scheduled 100 samples into the NEXT block (offset = frames + 100).
    const ::MidiEvent late = noteOn(kFrames + 100, 64);
    role.onMidi(&late, 1);

    // This block never reaches it.
    role.pumpUntil(kFrames);
    CHECK(role.pendingCount() == 3);
    CHECK(role.fifoRxCount() == 0);

    // finishBlock rebases by the block length → offset becomes 100.
    role.rebase(kFrames);

    // Next block: not yet due at 50, due at 100.
    role.pumpUntil(50);
    CHECK(role.pendingCount() == 3);
    role.pumpUntil(100);
    CHECK(role.pendingCount() == 0);
    CHECK(role.fifoRxCount() == 3);
}

TEST_CASE("NES N8 MIDI already-due offsets clamp to 0 on rebase and clear() drops the queue", "[audio][nes]") {
    NesN8FifoRole role;
    constexpr std::uint32_t kFrames = 512;

    // Offset within this block that didn't fire (e.g. the ROM/audio never advanced there): rebase clamps
    // it to 0 so it fires at the very start of the next block rather than going negative.
    const ::MidiEvent e = noteOn(300, 62);
    role.onMidi(&e, 1);
    role.rebase(kFrames);                 // 300 <= 512 → clamps to 0
    role.pumpUntil(0);
    CHECK(role.pendingCount() == 0);
    CHECK(role.fifoRxCount() == 3);

    // clear() drops still-queued bytes (used on reset).
    role.onMidi(&e, 1);
    CHECK(role.pendingCount() == 3);
    role.clear();
    CHECK(role.pendingCount() == 0);
}
