// Guards the command ring's payload handling at its edges: a control-surface message must fit the
// command's 4-byte inline slot (drainInto reads `len` bytes out of it), and a queued per-system MIDI
// payload must be freed when teardown discards it unapplied. The leak itself only shows under
// LeakSanitizer: build this binary in build-asan/ (tools/run-sanitizer.sh's tree) and run "[invoker]".
//
// Run via `pnpm test:plugin`.

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"

TEST_CASE("stageControllerMidi refuses what the inline slot cannot hold", "[invoker]") {
    Engine        eng(44100.0);
    QueuedInvoker inv(eng);
    CHECK_FALSE(inv.stageControllerMidi({}));
    CHECK_FALSE(inv.stageControllerMidi({0xF0, 0x00, 0x20, 0x29, 0xF7}));
    CHECK(inv.stageControllerMidi({0x90, 60, 127}));
    CHECK(inv.stageControllerMidi({0xB0, 1, 2, 3}));
}

TEST_CASE("freePending frees a queued per-system MIDI payload", "[invoker]") {
    Engine        eng(44100.0);
    QueuedInvoker inv(eng);
    inv.setAudioThreadOwns(true);  // push-only: the command stays queued
    inv.stageSystemMidi(1, std::vector<std::uint8_t>(10, 0x42));  // past 4 bytes: an owning payload
    inv.freePending();             // teardown: discards it, and must free it
    inv.setAudioThreadOwns(false);
    SUCCEED();
}
