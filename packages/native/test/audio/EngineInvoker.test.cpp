// Guards the command ring's payload handling at its edges: a queued staged-message payload must be
// freed when teardown discards it unapplied. The leak itself only shows under LeakSanitizer: build
// this binary in build-asan/ (tools/run-sanitizer.sh's tree) and run "[invoker]".
//
// Run via `pnpm test:plugin`.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"

TEST_CASE("freePending frees a queued staged-message payload", "[invoker]") {
    Engine        eng(44100.0);
    QueuedInvoker inv(eng);
    inv.setAudioThreadOwns(true);  // push-only: the command stays queued
    // A stage-to-a-node message always owns its payload (the id and the bytes), so this is the ring's owning arm.
    inv.stageNodeMidi("sys-1", std::vector<std::uint8_t>(10, 0x42));
    inv.freePending();             // teardown: discards it, and must free it
    inv.setAudioThreadOwns(false);
    SUCCEED();
}
