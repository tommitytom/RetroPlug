// Reproduces "the SGB SameBoy models do not work": drive the real mGB ROM under
// each selectable SameBoyModel and assert the emulator actually boots and runs —
// the framebuffer must become non-uniform (a booted GB renders content) and the
// APU must produce signal. The DMG/CGB models pass; the SGB variants
// (Sgb / SgbPal / Sgb2) are expected to fail here, pinning the bug.
//
// Run via `pnpm test:plugin` (target retroplug-audio-test).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoyConstants.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/FrameBufferTriple.hpp"

#ifndef RP_MGB_ROM_PATH
#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
#endif

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr std::uint32_t kFrames     = 800;
// ~4 s of emulation: well past any boot ROM into the running game.
constexpr int           kBlocks     = 240;

std::vector<std::uint8_t> readRom() {
    std::FILE* f = std::fopen(RP_MGB_ROM_PATH, "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(n > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
    return bytes;
}

struct RunResult {
    float  audioPeak    = 0.0f;  // peak over the whole run
    int    peakBlock    = -1;    // block index where audioPeak occurred
    bool   frameVaries  = false; // framebuffer became non-uniform (game rendered)
};

RunResult run(SameBoyModel model, bool fastBoot) {
    SameBoyConfig cfg;
    cfg.model    = model;
    cfg.fastBoot = fastBoot;
    auto sys = std::make_unique<SameBoySystem>(1, cfg, readRom());
    sys->onActivate(kSampleRate);

    AudioBlockInfo info{};
    info.frames     = kFrames;
    info.sampleRate = kSampleRate;

    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    float* outs[2] = {l.data(), r.data()};

    RunResult res;
    for (int blk = 0; blk < kBlocks; ++blk) {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        sys->onProcess(info, outs);
        for (std::uint32_t i = 0; i < kFrames; ++i) {
            const float peak = std::max(std::abs(l[i]), std::abs(r[i]));
            if (peak > res.audioPeak) { res.audioPeak = peak; res.peakBlock = blk; }
        }
    }

    // Final framebuffer non-uniformity: a booted GB draws content, so at least two
    // pixels differ. A hung / never-rendering core leaves the buffer uniform.
    FrameBufferTriple* fb = sys->framebuffer();
    const std::uint32_t px = sameboy::kPixelCount;
    std::vector<std::uint32_t> pixels(px, 0u);
    if (fb->readInto(pixels.data(), px)) {
        for (std::uint32_t i = 1; i < px; ++i) {
            if (pixels[i] != pixels[0]) { res.frameVaries = true; break; }
        }
    }

    sys->onDeactivate();
    return res;
}

} // namespace

TEST_CASE("SameBoy models boot and render", "[audio][sameboy][model]") {
    struct Case { SameBoyModel model; const char* name; };
    const Case cases[] = {
        { SameBoyModel::DmgB,   "DmgB"   },
        { SameBoyModel::Mgb,    "Mgb"    },
        { SameBoyModel::CgbC,   "CgbC"   },
        { SameBoyModel::Sgb,    "Sgb"    },
        { SameBoyModel::SgbPal, "SgbPal" },
        { SameBoyModel::Sgb2,   "Sgb2"   },
    };

    for (const auto& c : cases) {
        // fastBoot=false: the stock boot ROM runs, so the chime supplies the audio
        // this test checks for.
        RunResult res = run(c.model, /*fastBoot=*/false);
        std::fprintf(stderr, "[model %-6s] audioPeak=%.5f frameVaries=%d\n",
                     c.name, res.audioPeak, res.frameVaries ? 1 : 0);
        INFO("model " << c.name);
        CHECK(res.frameVaries);        // the game booted and drew something
        CHECK(res.audioPeak > 0.001f); // the APU produced signal
    }
}

TEST_CASE("SameBoy fast boot removes the startup chime and still boots", "[audio][sameboy][fastboot]") {
    // RetroPlug's fast boot (findBootRom's *_fast ROMs) strips the boot chime and
    // the LCD-on white flash while still handing off to a game that boots + renders.
    // The flashlessness is by construction (the fast boot ROMs never turn on the LCD;
    // the game does), so this test targets the chime, which IS observable: the DMG /
    // CGB boot chime is the loudest event in a normal boot (~0.25-0.27); fast boot
    // leaves only mGB's own quiet running audio (~0.15). SGB has no boot chime (its
    // boot writes NR14 with bit7 clear), so only the boot+render is checked there.
    struct Case { SameBoyModel model; const char* name; bool chimes; };
    const Case cases[] = {
        { SameBoyModel::DmgB,   "DmgB",   true  },
        { SameBoyModel::Mgb,    "Mgb",    true  },
        { SameBoyModel::CgbC,   "CgbC",   true  },
        { SameBoyModel::Sgb,    "Sgb",    false },
        { SameBoyModel::SgbPal, "SgbPal", false },
        { SameBoyModel::Sgb2,   "Sgb2",   false },
    };

    for (const auto& c : cases) {
        RunResult slow = run(c.model, /*fastBoot=*/false);
        RunResult fast = run(c.model, /*fastBoot=*/true);
        std::fprintf(stderr, "[fastboot %-6s] slowPeak=%.5f@blk%d fastPeak=%.5f@blk%d fastFrameVaries=%d\n",
                     c.name, slow.audioPeak, slow.peakBlock, fast.audioPeak, fast.peakBlock,
                     fast.frameVaries ? 1 : 0);
        INFO("model " << c.name);
        // The game still boots and renders under fast boot.
        CHECK(fast.frameVaries);
        // mGB is actually running (guards against a dead/hung fast boot passing silently).
        CHECK(fast.audioPeak > 0.001f);
        if (c.chimes) {
            // Sanity: the normal boot really does chime (the loudest event).
            CHECK(slow.audioPeak > 0.22f);
            // Fast boot removes it: the loud chime is gone, leaving only mGB's audio,
            // which is well under the chime level (a chime would put this back near slow).
            CHECK(fast.audioPeak < 0.20f);
            CHECK(fast.audioPeak < slow.audioPeak * 0.75f);
        }
    }
}
