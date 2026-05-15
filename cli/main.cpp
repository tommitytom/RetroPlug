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
//
// CLI overrides take precedence over fields in the script JSON.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <rfl/json.hpp>

#include "project/Project.hpp"
#include "system/SystemBase.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

#include "Script.hpp"
#include "Wav.hpp"

namespace {

struct CliArgs {
    std::string scriptPath;
    std::string romOverride;
    std::string outOverride;
    std::optional<std::uint32_t> durationOverride;
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s --script PATH [--rom ROM] [--out WAV] [--duration MS]\n"
        "\n"
        "  --script   JSON file describing rom + timed input events\n"
        "  --rom      override the script's `rom` path\n"
        "  --out      override the script's `out_wav` path\n"
        "  --duration override the script's `duration_ms`\n",
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
        if      (arg == "--script")   a.scriptPath  = need("--script");
        else if (arg == "--rom")      a.romOverride = need("--rom");
        else if (arg == "--out")      a.outOverride = need("--out");
        else if (arg == "--duration") a.durationOverride = static_cast<std::uint32_t>(std::atoi(need("--duration")));
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

} // namespace

int main(int argc, char** argv) try {
    const CliArgs args = parseArgs(argc, argv);

    // 1. Parse script JSON.
    const std::string json = slurpText(args.scriptPath);
    auto parsed = rfl::json::read<Script>(json);
    if (!parsed) {
        std::fprintf(stderr, "JSON parse error: %s\n", parsed.error().what());
        return 1;
    }
    Script script = std::move(parsed.value());

    if (!args.romOverride.empty())          script.rom         = args.romOverride;
    if (!args.outOverride.empty())          script.out_wav     = args.outOverride;
    if (args.durationOverride)              script.duration_ms = *args.durationOverride;

    if (script.rom.empty()) {
        std::fprintf(stderr, "script: 'rom' is required\n");
        return 1;
    }
    if (script.duration_ms == 0) {
        std::fprintf(stderr, "script: 'duration_ms' must be > 0\n");
        return 1;
    }

    // 2. Build the runtime: Project + activated SameBoySystem.
    Project project;
    project.reserve(1);

    SameBoyConfig cfg;
    cfg.romPath  = script.rom;
    cfg.model    = GameboyModel::CgbC;
    cfg.fastBoot = true;

    auto bytes = slurpBytes(script.rom);
    auto sys = std::make_unique<SameBoySystem>(
        project.nextSystemId(), cfg, std::move(bytes));
    sys->onActivate(static_cast<double>(script.sample_rate));
    SameBoySystem* sysRaw = sys.get();
    project.adoptSystem(sys.release());

    // 3. Flatten event lists to sorted (sample, ...) streams.
    const auto timed     = flattenEvents(script.events, script.sample_rate);
    const auto timedMidi = flattenMidi  (script.events, script.sample_rate);

    // 4. Render loop.
    const std::uint64_t totalSamples =
        (static_cast<std::uint64_t>(script.duration_ms) * script.sample_rate) / 1000ull;
    std::vector<float> outL(script.block_size), outR(script.block_size);
    float* outs[2] = { outL.data(), outR.data() };

    std::optional<WavWriter> wav;
    if (script.out_wav)
        wav.emplace(*script.out_wav, script.sample_rate, 2);

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t eventCursor = 0;
    std::size_t midiCursor  = 0;

    for (std::uint64_t s = 0; s < totalSamples; s += script.block_size) {
        const std::uint32_t frames =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(script.block_size, totalSamples - s));

        while (eventCursor < timed.size() && timed[eventCursor].sample <= s) {
            sysRaw->pressButton(timed[eventCursor].button, timed[eventCursor].down);
            ++eventCursor;
        }

        // MIDI events go straight to onMidi (single-system CLI; no routing
        // policy needed). Roles attached via the sniffer pick them up and
        // push bytes into the system's serial queue.
        while (midiCursor < timedMidi.size() && timedMidi[midiCursor].sample <= s) {
            sysRaw->onMidi(&timedMidi[midiCursor].event, 1);
            ++midiCursor;
        }

        std::fill_n(outL.data(), frames, 0.0f);
        std::fill_n(outR.data(), frames, 0.0f);
        AudioBlockInfo info{ frames, static_cast<double>(script.sample_rate) };
        sysRaw->onProcess(info, outs);

        if (wav) wav->writeBlockFloatPlanar(outs, frames);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double wallSec  = std::chrono::duration<double>(t1 - t0).count();
    const double audioSec = static_cast<double>(script.duration_ms) / 1000.0;
    const double xrt      = (wallSec > 0.0) ? (audioSec / wallSec) : 0.0;

    std::fprintf(stderr,
        "rendered %.2fs of audio in %.2fs wall (%.1fx realtime)%s%s\n",
        audioSec, wallSec, xrt,
        script.out_wav ? "; out=" : "",
        script.out_wav ? script.out_wav->c_str() : "");

    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
}
