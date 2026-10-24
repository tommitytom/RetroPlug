// Guards the command ring's payload handling at its edges: a control-surface message must fit the
// command's 4-byte inline slot (drainInto reads `len` bytes out of it), and a queued per-system MIDI
// payload must be freed when teardown discards it unapplied. The leak itself only shows under
// LeakSanitizer: build this binary in build-asan/ (tools/run-sanitizer.sh's tree) and run "[invoker]".
// Also that a host shape the graph cannot hold is refused, and the render that asked for it with it.
//
// Run via `pnpm test:plugin`.

#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/graph/Translator.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "system/SystemFactory.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

namespace {

std::vector<std::uint8_t> readRom() {
    std::vector<std::uint8_t> bytes;
    if (std::FILE* f = std::fopen(RP_MGB_ROM_PATH, "rb")) {
        std::fseek(f, 0, SEEK_END);
        bytes.resize(static_cast<std::size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
        std::fclose(f);
    }
    return bytes;
}

} // namespace

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

TEST_CASE("a host shape that does not plan is refused, and so is the render that wanted it", "[invoker]") {
    // rp.host.audio-out takes at most 32 buses, and PerSystem gives each system one, so a 33rd system has
    // nowhere to go. Rendering anyway would fill the 33 pairs from the stereo wiring still running.
    Engine           eng(44100.0);
    SystemFactory    factory;
    QueuedInvoker    inv(eng);
    EngineRpcService svc(eng, factory, inv);
    const std::vector<std::uint8_t> rom = readRom();
    REQUIRE_FALSE(rom.empty());
    for (SystemId id = 1; id <= 33; ++id) {
        auto sys = std::make_unique<SameBoySystem>(id, SameBoyConfig{}, rom);
        sys->onActivate(eng.sampleRate());
        inv.adoptSystem(std::move(sys));
    }
    REQUIRE(eng.systemCount() == 33);
    const rp::host::HostShape stereo{rp::host::HostShape::Kind::Multi, 2};
    CHECK(inv.setHostShape(stereo));  // already current
    CHECK_FALSE(inv.setHostShape({rp::host::HostShape::Kind::PerSystem, 0}));
    CHECK(eng.translator().shape() == stereo);
    CHECK(svc.renderAudioPerSystem(10.0).empty());
    CHECK(eng.translator().shape() == stereo);

    inv.removeSystem(33);
    CHECK(svc.renderAudioPerSystem(10.0).size() == 32);
}
