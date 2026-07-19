// Guards NES N8-FIFO intra-block timing: NesN8FifoRole QUEUES each byte tagged with its sample offset
// (ev.frame for MIDI, or an explicit offset for the raw pushBytes stream) and releases it into the N8 FIFO
// only once the block's sample progress reaches that offset (MesenNesSystem::stepIfBelowTarget drives
// pumpUntil). Previously every byte was pushed at the block start (frame-0 collapse) — the NES equivalent
// of the SameBoy serial fix.
//
// Two feeds share the one offset-scheduled queue: host MIDI (onMidi, ≤4-byte framed) and a RAW byte stream
// (pushBytes, no cap — a tracker's host-sync protocol). pumpUntil stable-sorts by offset so the two
// interleave in true sample order regardless of enqueue order.
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

TEST_CASE("NES N8 raw pushBytes releases a >4-byte payload whole at its offset, in order", "[audio][nes]") {
    NesN8FifoRole role;

    // A 6-byte run — past the MIDI 4-byte frame ceiling — at offset 300 (e.g. an arm+start+clock burst).
    const std::uint8_t payload[] = { 0xF9, 0x52, 0x03, 0x07, 0xFA, 0xF8 };
    role.pushBytes(300, payload, sizeof(payload));
    CHECK(role.pendingCount() == 6);
    CHECK(role.fifoRxCount() == 0);

    role.pumpUntil(299);                 // not yet due
    CHECK(role.fifoRxCount() == 0);

    role.pumpUntil(300);                 // due → all 6 released, in order
    CHECK(role.pendingCount() == 0);
    CHECK(role.fifoRxCount() == 6);
    for (std::uint8_t b : payload) CHECK(role.readFifoByte() == static_cast<int>(b));
    CHECK(role.readFifoByte() == -1);
}

TEST_CASE("NES N8 raw pushBytes + onMidi interleave in true sample order (sorted), not enqueue order", "[audio][nes]") {
    NesN8FifoRole role;

    // Enqueue a raw sync byte at offset 100 FIRST, then a host note at offset 40 — mirroring the engine
    // fanning the coreBytes sink after coreMidi. The stable sort must still release the note (earlier
    // offset) before the sync byte.
    const std::uint8_t clock = 0xF8;
    role.pushBytes(100, &clock, 1);
    const ::MidiEvent note = noteOn(40, 60);
    role.onMidi(&note, 1);
    CHECK(role.pendingCount() == 4);     // 1 raw + 3 note

    role.pumpUntil(40);                  // only the note is due
    CHECK(role.fifoRxCount() == 3);
    CHECK(role.readFifoByte() == 0x90);  // note first, despite being enqueued second
    CHECK(role.readFifoByte() == 60);
    CHECK(role.readFifoByte() == 100);   // velocity
    CHECK(role.readFifoByte() == -1);    // the raw clock is not due yet

    role.pumpUntil(100);                 // now the raw clock releases
    CHECK(role.readFifoByte() == 0xF8);
    CHECK(role.readFifoByte() == -1);
}

TEST_CASE("NES N8 raw pushBytes past the block end rebases into the next block", "[audio][nes]") {
    NesN8FifoRole role;
    constexpr std::uint32_t kFrames = 512;

    const std::uint8_t byte = 0xFA;
    role.pushBytes(kFrames + 100, &byte, 1);  // 100 samples into the next block
    role.pumpUntil(kFrames);                   // never reached this block
    CHECK(role.pendingCount() == 1);
    role.rebase(kFrames);                      // → offset 100
    role.pumpUntil(50);
    CHECK(role.pendingCount() == 1);
    role.pumpUntil(100);
    CHECK(role.pendingCount() == 0);
    CHECK(role.fifoRxCount() == 1);
    CHECK(role.readFifoByte() == 0xFA);
}
