// retroplug-cli
//
// Headless renderer that drives the SameBoy DSP path at full speed from a
// JSON command script. Useful for fast LSDJ renders, deterministic regression
// tests, and agent-driven experiments — no DPF, no UI thread, no real-time
// scheduling.
//
// Usage:
//   retroplug-cli --script path/to/script.json
//   retroplug-cli --script s.json --rom other.gb --out out.wav --duration 10000
//   retroplug-cli --script sync.json --screenshot-dir /tmp --final-screenshot
//
// CLI overrides take precedence over fields in the script JSON.
//
// Note: SameBoy plays a short boot sequence (white screen + beep) for ~1.5 s
// after reset before the cartridge actually runs. Schedule screenshots at
// `at_ms` >= 2000 to capture LSDJ rather than the boot logo.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <rfl/json.hpp>

#include "native/core/img/png/lodepng.h"

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoyConstants.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "transport/FrameBufferTriple.hpp"

#include "Script.hpp"
#include "Wav.hpp"

namespace {

struct CliArgs {
    std::string scriptPath;
    std::string romOverride;
    std::string outOverride;
    std::optional<std::uint32_t> durationOverride;
    std::optional<std::string>   screenshotDir;
    bool                         finalScreenshot = false;
    bool                         perSystemWav    = false;
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s --script PATH [options]\n"
        "\n"
        "  --script           JSON file describing rom(s) + timed events\n"
        "  --rom PATH         override the script's `rom` (single-system only)\n"
        "  --out PATH         override the script's `out_wav`\n"
        "  --duration MS      override the script's `duration_ms`\n"
        "  --screenshot-dir D directory for screenshot PNGs (default: out_wav's dir, then cwd)\n"
        "  --final-screenshot capture every system once at script end as `<stem>_final_sys<N>.png`\n"
        "  --per-system-wav   also write per-system WAVs as `<out_wav stem>_sys<N>.wav`\n"
        "                     (the mix WAV at `out_wav` is still produced). Required when you\n"
        "                     want to verify audible sync between linked instances.\n",
        argv0);
}

CliArgs parseArgs(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires an argument\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if      (arg == "--script")            a.scriptPath  = need("--script");
        else if (arg == "--rom")               a.romOverride = need("--rom");
        else if (arg == "--out")               a.outOverride = need("--out");
        else if (arg == "--duration")          a.durationOverride = static_cast<std::uint32_t>(std::atoi(need("--duration")));
        else if (arg == "--screenshot-dir")    a.screenshotDir    = std::string(need("--screenshot-dir"));
        else if (arg == "--final-screenshot")  a.finalScreenshot  = true;
        else if (arg == "--per-system-wav")    a.perSystemWav     = true;
        else if (arg == "-h" || arg == "--help") { printUsage(argv[0]); std::exit(0); }
        else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            printUsage(argv[0]);
            std::exit(2);
        }
    }
    if (a.scriptPath.empty()) {
        std::fprintf(stderr, "--script is required\n");
        printUsage(argv[0]);
        std::exit(2);
    }
    return a;
}

std::string slurpText(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::uint8_t> slurpBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    const std::streamsize size = in.tellg();
    if (size <= 0) throw std::runtime_error("empty file: " + path);
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("read failed: " + path);
    return buf;
}

// Snapshot a system's framebuffer to <dir>/<scriptStem>_<name>_sys<idx>.png.
// FrameBufferTriple stores XRGB8888 (little-endian B,G,R,X bytes); we
// transcode to RGB24 in-place to match lodepng_encode24_file's expected
// layout — same pattern as src/PluginUI.cpp:91-101.
bool dumpFramebuffer(SameBoySystem& sys,
                     const std::string& dir,
                     const std::string& scriptStem,
                     const std::string& name,
                     std::uint32_t      systemIndex) {
    FrameBufferTriple* fb = sys.framebuffer();
    if (!fb) return false;
    const std::uint32_t w = fb->width();
    const std::uint32_t h = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(w) * h;

    std::vector<std::uint32_t> xrgb(pixels);
    if (!fb->readInto(xrgb.data(), static_cast<std::uint32_t>(pixels))) {
        std::fprintf(stderr,
            "[screenshot] no frame published yet for system %u (name=%s) — skipping\n",
            systemIndex, name.c_str());
        return false;
    }

    std::vector<unsigned char> rgb(pixels * 3);
    const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(xrgb.data());
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = src[i * 4 + 2]; // R
        rgb[i * 3 + 1] = src[i * 4 + 1]; // G
        rgb[i * 3 + 2] = src[i * 4 + 0]; // B
    }

    std::filesystem::path out = std::filesystem::path(dir) /
        (scriptStem + "_" + name + "_sys" + std::to_string(systemIndex) + ".png");
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);

    const unsigned err = lodepng_encode24_file(out.string().c_str(),
                                               rgb.data(), w, h);
    if (err) {
        std::fprintf(stderr, "[screenshot] lodepng error %u writing %s: %s\n",
                     err, out.string().c_str(), lodepng_error_text(err));
        return false;
    }
    std::fprintf(stderr, "[screenshot] wrote %s\n", out.string().c_str());
    return true;
}

} // namespace

int main(int argc, char** argv) try {
    const CliArgs args = parseArgs(argc, argv);

    // 1. Parse + normalize script JSON.
    const std::string json = slurpText(args.scriptPath);
    auto parsed = rfl::json::read<Script>(json);
    if (!parsed) {
        std::fprintf(stderr, "JSON parse error: %s\n", parsed.error().what().c_str());
        return 1;
    }
    Script script = std::move(parsed.value());

    if (!args.outOverride.empty())          script.out_wav     = args.outOverride;
    if (args.durationOverride)              script.duration_ms = *args.durationOverride;

    // Normalize into a single `systems` list. `--rom` overrides the legacy
    // top-level `rom` field; `systems: [...]` in the JSON wins over both if
    // present, but `--rom` is only valid when the script is single-system.
    if (!args.romOverride.empty()) {
        if (script.systems && script.systems->size() > 1) {
            std::fprintf(stderr,
                "--rom can only override a single-system script (got %zu systems)\n",
                script.systems->size());
            return 1;
        }
        if (script.systems && script.systems->size() == 1)
            script.systems->front().rom = args.romOverride;
        else
            script.rom = args.romOverride;
    }
    std::vector<ScriptSystem> systemsList;
    if (script.systems && !script.systems->empty()) {
        systemsList = std::move(*script.systems);
    } else if (script.rom && !script.rom->empty()) {
        systemsList.push_back({*script.rom, std::nullopt});
    } else {
        std::fprintf(stderr, "script: must set either 'rom' or 'systems[]'\n");
        return 1;
    }

    if (script.duration_ms == 0) {
        std::fprintf(stderr, "script: 'duration_ms' must be > 0\n");
        return 1;
    }

    const MidiRouting routing = script.midi_routing
        ? parseMidiRouting(*script.midi_routing)
        : MidiRouting::SendToAll;

    const std::uint32_t systemCount = static_cast<std::uint32_t>(systemsList.size());

    // Resolve screenshot directory. Prefer --screenshot-dir, else the dir of
    // out_wav, else the current directory.
    std::string screenshotDir = ".";
    if (args.screenshotDir) {
        screenshotDir = *args.screenshotDir;
    } else if (script.out_wav) {
        std::filesystem::path p(*script.out_wav);
        if (p.has_parent_path() && !p.parent_path().empty())
            screenshotDir = p.parent_path().string();
    }
    const std::string scriptStem = std::filesystem::path(args.scriptPath).stem().string();

    // 2. Build the runtime: Project + N activated SameBoySystems.
    Project project;
    project.reserve(systemCount);

    std::vector<SameBoySystem*> systems;
    systems.reserve(systemCount);
    for (std::uint32_t i = 0; i < systemCount; ++i) {
        const auto& s = systemsList[i];
        if (s.rom.empty()) {
            std::fprintf(stderr, "script: systems[%u].rom is required\n", i);
            return 1;
        }

        SameBoyConfig cfg;
        cfg.romPath     = s.rom;
        cfg.model       = GameboyModel::CgbC;
        cfg.fastBoot    = true;
        cfg.linkGroupId = s.link_group.value_or(0);

        auto bytes = slurpBytes(s.rom);
        auto sys = std::make_unique<SameBoySystem>(
            project.nextSystemId(), cfg, std::move(bytes));
        sys->onActivate(static_cast<double>(script.sample_rate));
        systems.push_back(sys.get());
        project.adoptSystem(sys.release());
    }
    project.rebuildLinkGroups();

    // 3. Flatten event lists. Validation runs once per event in each pass.
    const auto timedButtons     = flattenEvents     (script.events, script.sample_rate, systemCount);
    const auto timedMidi        = flattenMidi       (script.events, script.sample_rate);
    const auto timedScreenshots = flattenScreenshots(script.events, script.sample_rate, systemCount);
    const auto timedTransport   = flattenTransport  (script.events, script.sample_rate);

    // Simulated host transport state, fed into AudioBlockInfo each block.
    // Mirrors what DPF surfaces from a DAW so LsdjSyncRole and friends can
    // generate MIDI clock byte streams the same way in CLI as in the plugin.
    double cliBpm       = script.bpm.value_or(120.0);
    bool   cliTransport = script.transport_running.value_or(false);
    double cliPpq       = 0.0;

    // 4. Render loop.
    const std::uint64_t totalSamples =
        (static_cast<std::uint64_t>(script.duration_ms) * script.sample_rate) / 1000ull;
    std::vector<float> outL(script.block_size), outR(script.block_size);
    float* outs[2] = { outL.data(), outR.data() };

    std::optional<WavWriter> wav;
    if (script.out_wav)
        wav.emplace(*script.out_wav, script.sample_rate, 2);

    // Per-system WAV plumbing. Allocated only when --per-system-wav is set.
    // The mix WAV (above) is also produced so the audible result is
    // verifiable; the per-system files isolate each instance for sync
    // analysis (cross-correlation, peak-time comparison, etc.).
    std::vector<WavWriter>          perSysWav;
    std::vector<std::vector<float>> perSysL, perSysR;
    std::vector<std::array<float*, 2>> perSysOuts;
    if (args.perSystemWav) {
        if (!script.out_wav) {
            std::fprintf(stderr,
                "--per-system-wav requires an out_wav (set `out_wav` in the script or pass --out)\n");
            return 1;
        }
        std::filesystem::path base(*script.out_wav);
        const std::string stem = base.stem().string();
        const std::string ext  = base.extension().string().empty() ? ".wav" : base.extension().string();
        const std::filesystem::path dir =
            base.has_parent_path() && !base.parent_path().empty()
                ? base.parent_path() : std::filesystem::path(".");
        perSysWav.reserve(systemCount);
        perSysL .assign(systemCount, std::vector<float>(script.block_size));
        perSysR .assign(systemCount, std::vector<float>(script.block_size));
        perSysOuts.resize(systemCount);
        for (std::uint32_t i = 0; i < systemCount; ++i) {
            const auto p = (dir / (stem + "_sys" + std::to_string(i) + ext)).string();
            perSysWav.emplace_back(p, script.sample_rate, 2);
            perSysOuts[i] = { perSysL[i].data(), perSysR[i].data() };
            std::fprintf(stderr, "[per-system-wav] sys%u -> %s\n", i, p.c_str());
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t btnCursor  = 0;
    std::size_t midiCursor = 0;
    std::size_t shotCursor = 0;
    std::size_t xportCursor = 0;

    for (std::uint64_t s = 0; s < totalSamples; s += script.block_size) {
        const std::uint32_t frames =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(script.block_size, totalSamples - s));

        while (btnCursor < timedButtons.size() && timedButtons[btnCursor].sample <= s) {
            const auto& ev = timedButtons[btnCursor];
            systems[ev.systemIndex]->pressButton(ev.button, ev.down);
            ++btnCursor;
        }

        // MIDI is routed through Project::dispatchMidi so the CLI exercises
        // the same routing logic the plugin uses (system messages broadcast,
        // channel messages routed per `midi_routing`).
        while (midiCursor < timedMidi.size() && timedMidi[midiCursor].sample <= s) {
            project.dispatchMidi(&timedMidi[midiCursor].event, 1, routing);
            ++midiCursor;
        }

        // Apply transport / BPM edits whose target sample falls inside this
        // block. Done before the AudioBlockInfo is built so the new state
        // takes effect immediately for the block they were scheduled in.
        while (xportCursor < timedTransport.size() &&
               timedTransport[xportCursor].sample < s + frames) {
            const auto& tx = timedTransport[xportCursor];
            if (tx.setBpm)       cliBpm       = *tx.setBpm;
            if (tx.setTransport) cliTransport = *tx.setTransport;
            ++xportCursor;
        }

        std::fill_n(outL.data(), frames, 0.0f);
        std::fill_n(outR.data(), frames, 0.0f);
        AudioBlockInfo info{ frames, static_cast<double>(script.sample_rate),
                             cliBpm, cliPpq, cliTransport };

        if (args.perSystemWav) {
            // Manual orchestration mirroring Project::onProcess + LinkGroup
            // round-robin, but with per-system outs buffers so we can write
            // each instance's audio to its own WAV. Works for linked and
            // unlinked systems alike: the step loop interleaves them, which
            // is exactly what LinkGroup does internally.
            for (auto* sys : systems) sys->prepareForBlock(info);
            bool anyBelow = true;
            while (anyBelow) {
                anyBelow = false;
                for (auto* sys : systems) {
                    if (sys->stepIfBelowTarget(info.frames)) anyBelow = true;
                }
            }
            for (std::uint32_t i = 0; i < systemCount; ++i) {
                std::fill_n(perSysL[i].data(), frames, 0.0f);
                std::fill_n(perSysR[i].data(), frames, 0.0f);
                systems[i]->finishBlock(info, perSysOuts[i].data());
                // Sum into the mix WAV.
                for (std::uint32_t f = 0; f < frames; ++f) {
                    outL[f] += perSysL[i][f];
                    outR[f] += perSysR[i][f];
                }
            }
        } else {
            project.onProcess(info, outs);
        }

        // Drain screenshot events whose target sample is now in the past.
        // Done after onProcess so the screenshot reflects the just-completed
        // frame (the framebuffer publish happens on vblank during GB_run).
        while (shotCursor < timedScreenshots.size() &&
               timedScreenshots[shotCursor].sample <= s + frames) {
            const auto& shot = timedScreenshots[shotCursor];
            dumpFramebuffer(*systems[shot.systemIndex], screenshotDir,
                            scriptStem, shot.name, shot.systemIndex);
            ++shotCursor;
        }

        if (wav) wav->writeBlockFloatPlanar(outs, frames);
        if (args.perSystemWav) {
            for (std::uint32_t i = 0; i < systemCount; ++i)
                perSysWav[i].writeBlockFloatPlanar(perSysOuts[i].data(), frames);
        }

        // Advance simulated PPQ for the next block based on the frames we
        // actually rendered (the last block may be short).
        if (cliTransport) {
            cliPpq += (cliBpm / 60.0) * (static_cast<double>(frames) /
                                         static_cast<double>(script.sample_rate));
        }
    }

    if (args.finalScreenshot) {
        for (std::uint32_t i = 0; i < systemCount; ++i) {
            dumpFramebuffer(*systems[i], screenshotDir, scriptStem, "final", i);
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double wallSec  = std::chrono::duration<double>(t1 - t0).count();
    const double audioSec = static_cast<double>(script.duration_ms) / 1000.0;
    const double xrt      = (wallSec > 0.0) ? (audioSec / wallSec) : 0.0;

    std::fprintf(stderr,
        "rendered %.2fs of audio across %u system(s) in %.2fs wall (%.1fx realtime)%s%s\n",
        audioSec, systemCount, wallSec, xrt,
        script.out_wav ? "; out=" : "",
        script.out_wav ? script.out_wav->c_str() : "");

    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
