// The pinned C++ boundary, tried on a real package (docs/downloadable-packs-loading-design.md section 2): the Mesen
// package's native target built as a pack (retroplug_pack(mesen), packages/mesen/native/CMakeLists.txt), carrying its
// own static copies of retroplug-system, node-sdk, n8-wire, reflect-cpp and deps/mesen, loaded into an Engine whose
// product (packs-host) lists no mesen, beside an Engine given this binary's static build of the same package.
//
// What it proves, and no more: the module exports its entry alone and imports nothing of RetroPlug's; its node types
// describe themselves as the static build's do; and a BlipToaster NES built through the pack's build function, driven
// by host MIDI through rp.host.midi-in, renders the static build's PCM byte for byte, block for block, the two run
// interleaved, silent until the first note and sounding after it, and publishes the same work RAM through SystemBase's
// snapshot virtuals. That exercises the engine calling a pack's SystemBase through its vtable (the block, the snapshot
// publish, the deleting destructor in Engine::destroySystem) and a build request's ownership crossing into the pack.
//
// What it does not: the two copies share this process's pid-keyed Mesen scratch paths (MesenGlobalInit.cpp's
// /tmp/retroplug-mesen/<pid>, which each copy's first init clears, and NesEverdriveFifo.hpp's retroplug-sd-<pid>/<n>,
// whose slot counter each copy keeps for itself, so both NES systems get slot 0). A NES BlipToaster boot writes
// neither, which is why the case is the NES alone; the SMS and GBA stage files there and are left to M5, where Mesen is
// a pack and no static copy sits beside it. It runs on Windows too: no heap ownership crosses the boundary
// (docs/downloadable-packs-m5a-plan.md), so the static CRT's heap per module is sound, which retroplug-packs-heap-test
// proves. Nor does it cover retroplug-sdl, which links -static-libstdc++: a pack there would carry a second C++
// runtime across this boundary, and is now held to the same rule (Design questions 4).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-mesen]"`.

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ElfSymbols.hpp"
#include "GraphDocs.hpp"
#include "graph/Descriptor.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackDirs.hpp"
#include "host/packs/PackTable.hpp"

// The Mesen package's static build, which this binary links and no engine of its registers by itself.
extern "C" const rp_package* rp_mesen_package(void);

using rp::host::packs::PackTable;
using rp::host::test::DocExtras;
using rp::host::test::DocSystem;
using rp::host::test::docNode;
using rp::host::test::docWire;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::filesystem::path mesenPack() { return std::filesystem::path(RP_PACKS_BUILD_DIR) / "mesen" / "0.1.0"; }

// A lone BlipToaster NES through rp.host.midi-in, on the host's stereo pair, its ROM put in `e`.
Engine::GraphRequest nesDocument(Engine& e) {
    rp::host::test::Roms   roms;
    std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
    rp::host::test::withRoms(e, systems, roms);
    const DocExtras extras{{docNode("midi-in", "rp.host.midi-in")}, {docWire("midi-in.midi", "sys-1.midi")}};
    return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems, extras), {1});
}

// One block, interleaved, with the notes due in it staged first. BlipToaster ignores MIDI for its first second or so
// (parity/scenes.ts BOOT_MS), so the notes start at 1.5 s: a chord, a change at 2.5 s, and its release.
std::vector<float> block(Engine& e, std::uint64_t at) {
    const auto due = [at](double seconds) {
        const auto from = static_cast<std::uint64_t>(seconds * kRate);
        return at <= from && from < at + kFrames ? static_cast<std::uint32_t>(from - at) : UINT32_MAX;
    };
    if (const auto f = due(1.5); f != UINT32_MAX) {
        e.stageMidi(f, std::vector<std::uint8_t>{0x90, 60, 100});
        e.stageMidi(f, std::vector<std::uint8_t>{0x91, 64, 100});
        e.stageMidi(f, std::vector<std::uint8_t>{0x92, 48, 100});
    }
    if (const auto f = due(2.5); f != UINT32_MAX) e.stageMidi(f, std::vector<std::uint8_t>{0x90, 67, 100});
    if (const auto f = due(3.0); f != UINT32_MAX) e.stageMidi(f, std::vector<std::uint8_t>{0x80, 67, 0});
    std::vector<float> l(kFrames), r(kFrames), out;
    float*             outs[2] = {l.data(), r.data()};
    e.processBlock(kFrames, outs, 2);
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        out.push_back(l[i]);
        out.push_back(r[i]);
    }
    return out;
}

// The file the code behind a system's vtable was loaded from: a core built by the pack's own copy of the package has
// its vtable in the module, one built by the static build has it in this binary.
std::string vtableFile(SystemBase* core) {
    if (!core) return {};
    const void* vtable = *reinterpret_cast<void* const*>(core);
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(vtable), &module))
        return {};
    wchar_t     path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
    return n == 0 ? std::string() : std::filesystem::path(std::wstring(path, n)).string();
#else
    Dl_info info{};
    if (dladdr(vtable, &info) == 0 || !info.dli_fname) return {};
    return info.dli_fname;
#endif
}

}  // namespace

TEST_CASE("the Mesen package loads as a pack, its types described as its static build describes them",
          "[packs-mesen]") {
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(mesenPack());
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->packages.size() == 1);
    const rp_package* pack = load.pack->packages[0];
    const rp_package* builtIn = rp_mesen_package();
    CHECK(pack != builtIn);  // the module's own copy, not this binary's
    REQUIRE(pack->node_type_count == builtIn->node_type_count);
    for (std::uint32_t k = 0; k < pack->node_type_count; ++k) {
        INFO(builtIn->node_types[k]->id);
        CHECK(std::string(pack->node_types[k]->id) == builtIn->node_types[k]->id);
        CHECK(std::string(pack->node_types[k]->descriptor(pack->node_types[k])) ==
              builtIn->node_types[k]->descriptor(builtIn->node_types[k]));
    }
    // Everything the module links stays its own: the static build's entry is not among its exports.
    CHECK(rp::host::packs::librarySymbol(load.pack->library, "rp_mesen_package") == nullptr);
#if defined(__linux__)
    // And the file says so: one export, and no import of RetroPlug's (cmake/packs.cmake's --no-undefined made every
    // import one of the system libraries').
    const auto symbols = rp::host::packs::test::readDynamicSymbols(
        (mesenPack() / "native" / std::string(rp::host::packs::currentPlatform()) / "rp-mesen.so").string());
    REQUIRE(symbols);
    CHECK(symbols->exported == std::vector<std::string>{"rp_pack_entry"});
    for (const auto& name : symbols->imported) {
        INFO(name);
        CHECK_FALSE(rp::host::packs::test::isRetroPlugSymbol(name));
    }
#endif
}

TEST_CASE("a NES booted through the Mesen pack renders the static build's PCM and work RAM, bit for bit",
          "[packs-mesen]") {
    PackTable none(rp::graph::productPackages());
    Engine    builtIn(kRate, none);
    REQUIRE(builtIn.addNodePackage(rp_mesen_package()));
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(mesenPack()));
    Engine fromPack(kRate, table);
    REQUIRE(fromPack.packsRegistered() == 1);

    const auto placedBuiltIn = builtIn.setGraph(nesDocument(builtIn));
    const auto placedPack = fromPack.setGraph(nesDocument(fromPack));
    REQUIRE(placedBuiltIn.ok);
    REQUIRE(placedPack.ok);
    REQUIRE(fromPack.findSystem(1) != nullptr);
    REQUIRE(builtIn.findSystem(1) != nullptr);
    // The pack engine's core really comes from the module, and the static build's from this binary: without this a
    // pack that registered nothing and fell back on the static types could still agree with itself.
    CHECK(vtableFile(fromPack.findSystem(1)).find("rp-mesen") != std::string::npos);
    CHECK(vtableFile(builtIn.findSystem(1)).find("rp-mesen") == std::string::npos);

    // Interleaved, block by block: each block of the pack's core against the same block of the static build's. The
    // NES is exactly silent until the first note (at 1.5 s) and sounds in every block from the one that carries it,
    // so what agrees is the notes the MIDI drove, not two idle cores.
    const auto firstNote = static_cast<std::uint64_t>(1.5 * kRate);
    std::size_t firstDiff = SIZE_MAX, loudBeforeNote = 0, quietAfterNote = 0;
    for (std::uint64_t at = 0; at < static_cast<std::uint64_t>(3.5 * kRate); at += kFrames) {
        const auto a = block(builtIn, at);
        const auto b = block(fromPack, at);
        REQUIRE(a.size() == b.size());
        if (firstDiff == SIZE_MAX && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) != 0)
            firstDiff = at;
        double energy = 0;
        for (const float s : a) energy += static_cast<double>(s) * s;
        if (at + kFrames <= firstNote && energy != 0.0) ++loudBeforeNote;
        if (at + kFrames > firstNote && energy < 1.0) ++quietAfterNote;
    }
    INFO("first differing block at frame " << firstDiff);
    CHECK(firstDiff == SIZE_MAX);
    CHECK(loudBeforeNote == 0);
    CHECK(quietAfterNote == 0);
    // The same work RAM, read through the snapshot each core publishes (SystemBase's virtuals, called by the engine).
    const auto ramBuiltIn = builtIn.readRam(1);
    const auto ramPack = fromPack.readRam(1);
    REQUIRE(ramBuiltIn);
    REQUIRE(ramPack);
    CHECK_FALSE(ramBuiltIn->empty());
    CHECK(*ramBuiltIn == *ramPack);
    CHECK(builtIn.faultCount() == 0);
    CHECK(fromPack.faultCount() == 0);
}
