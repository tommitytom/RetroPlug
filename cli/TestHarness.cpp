#include "TestHarness.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
    #include "tjs.h"       // TJS_Initialize / TJS_NewRuntime / TJS_GetJSContext
                           // / TJS_FreeRuntime (+ <quickjs.h>)
    #include "private.h"   // TJS_EvalModuleContent / TJS_GetLoop /
                           // tjs__execute_jobs (+ <uv.h>)
}

#include "Screenshot.hpp"
#include "Wav.hpp"
#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/DebugTarget.hpp"
#include "system/InputTypes.hpp"
#include "system/RomFormat.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"
#include "system/MemoryType.hpp"
#include "transport/MidiTypes.hpp"
#include "system/mesen/MesenGbaConfig.hpp"
#include "system/mesen/MesenGbaSystem.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "lsdj/KitCompiler.hpp"
#include "lsdj/KitUtil.hpp"
#include "lsdj/SavSerialization.hpp"
#include "lsdj/codec/SavCodec.hpp"
#include "lsdj/codec/SongCodec.hpp"

// Guard the hand-mirrored TypeScript enums in test/harness/index.ts. The wire
// values are load-bearing; if a C++ renumber drifts from the TS Button/Mem
// objects, fail the build here with a pointed message rather than silently
// passing the wrong byte across the bridge.
static_assert(static_cast<int>(GameboyButton::Right)  == 0, "harness Button.Right out of sync");
static_assert(static_cast<int>(GameboyButton::Left)   == 1, "harness Button.Left out of sync");
static_assert(static_cast<int>(GameboyButton::Up)     == 2, "harness Button.Up out of sync");
static_assert(static_cast<int>(GameboyButton::Down)   == 3, "harness Button.Down out of sync");
static_assert(static_cast<int>(GameboyButton::A)      == 4, "harness Button.A out of sync");
static_assert(static_cast<int>(GameboyButton::B)      == 5, "harness Button.B out of sync");
static_assert(static_cast<int>(GameboyButton::Select) == 6, "harness Button.Select out of sync");
static_assert(static_cast<int>(GameboyButton::Start)  == 7, "harness Button.Start out of sync");
static_assert(static_cast<int>(rp::MemoryType::Ram)          == 0, "harness Mem.Ram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Rom)          == 1, "harness Mem.Rom out of sync");
static_assert(static_cast<int>(rp::MemoryType::Sram)         == 2, "harness Mem.Sram out of sync");
static_assert(static_cast<int>(rp::MemoryType::Vram)         == 3, "harness Mem.Vram out of sync");
static_assert(static_cast<int>(rp::MemoryType::IORegisters)  == 4, "harness Mem.IORegisters out of sync");
static_assert(static_cast<int>(rp::MemoryType::HRam)         == 5, "harness Mem.HRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::OAM)          == 6, "harness Mem.OAM out of sync");
static_assert(static_cast<int>(rp::MemoryType::NametableRam) == 7, "harness Mem.NametableRam out of sync");
static_assert(static_cast<int>(rp::MemoryType::ExtWorkRam)   == 8, "harness Mem.ExtWorkRam out of sync");

namespace {

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

// Flatten a TAP YAML diagnostic message onto one logical block, escaping
// newlines so a multi-line stack trace doesn't break the `1..N` plan.
std::string oneLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (c == '\n') ? ' ' : c;
    return out;
}

// Parse an LSDj sync-mode name for emu.loadRom's role option. Mirrors the
// --script runner's parseLsdjSyncMode (cli/main.cpp).
LsdjSyncMode parseLsdjSyncMode(const std::string& s) {
    if (s == "Off")                return LsdjSyncMode::Off;
    if (s == "MidiSync")           return LsdjSyncMode::MidiSync;
    if (s == "MidiSyncArduinoboy") return LsdjSyncMode::MidiSyncArduinoboy;
    if (s == "MidiMap")            return LsdjSyncMode::MidiMap;
    if (s == "Keyboard")           return LsdjSyncMode::Keyboard;
    if (s == "KeyboardMidi")       return LsdjSyncMode::KeyboardMidi;
    if (s == "MidiPassthrough")    return LsdjSyncMode::MidiPassthrough;
    if (s == "ArduinoboyMaster")   return LsdjSyncMode::ArduinoboyMaster;
    throw std::runtime_error("loadRom: unknown lsdj_sync_mode: " + s);
}

} // namespace

// ---------------------------------------------------------------------------
// Impl: owns the runtime + the Project the `emu` shims drive.
// ---------------------------------------------------------------------------

struct TestHarness::Impl {
    TJSRuntime* qrt = nullptr;
    JSContext*  ctx = nullptr;

    std::unique_ptr<Project> project;
    double      sampleRate = 44100.0;
    std::uint32_t blockSize = 1024;
    std::vector<float> scratchL, scratchR;

    // Simulated host transport, fed into AudioBlockInfo each block (mirrors
    // cli/main.cpp's cliBpm/cliTransport/cliPpq). LsdjSyncRole and friends read
    // these to generate MIDI-clock byte streams the same way as in the plugin.
    double        bpm              = 120.0;
    bool          transportPlaying = false;
    double        ppq              = 0.0;
    std::uint64_t sampleClock      = 0; // absolute sample pos across runMs calls

    // Per-system captures drained after each processed block. midiOut is what a
    // role emitted back to the host (e.g. Arduinoboy MI.OUT); serialOut is the
    // raw GB serial byte stream (master mode). Keyed by SystemId.
    struct MidiOutRec { std::uint64_t sample; std::vector<std::uint8_t> bytes; };
    struct SerialRec  { std::uint64_t sample; std::uint8_t byte; };
    std::vector<SystemBase*> sysList; // load order; pointers owned by `project`
    std::unordered_map<SystemId, std::vector<MidiOutRec>> midiOutLog;
    std::unordered_map<SystemId, std::vector<SerialRec>>  serialOutLog;
    std::unique_ptr<rp::lsdj::KitCompiler> kitCompiler_; // lazy; shared across patches

    // TAP state.
    int  testIndex   = 0;
    int  failures    = 0;
    bool donePrinted = false;

    Impl() : project(std::make_unique<Project>()),
             scratchL(blockSize), scratchR(blockSize) {}

    // -- emu surface (called from the static JS trampolines) ----------------

    std::uint32_t loadRom(const std::string& path,
                          const std::vector<std::uint8_t>* sram = nullptr,
                          const std::string& lsdjSyncMode = "",
                          std::uint8_t linkGroup = 0) {
        auto bytes = slurpBytes(path);
        const RomFormat fmt = detectRomFormat(bytes);

        std::unique_ptr<SystemBase> sys;
        switch (fmt) {
            case RomFormat::SameBoy: {
                SameBoyConfig cfg;
                cfg.romPath  = path;
                cfg.model    = SameBoyModel::CgbC;
                cfg.fastBoot = true;
                // Optional cartridge SRAM (a .sav image): loaded on activate so
                // a fixture can boot LSDj from a synthetic sav (skipping the
                // SRAM self-test) — mirrors the plugin's sibling-.sav load.
                if (sram) cfg.sram = *sram;
                // Optional LSDj sync-mode role (MidiSync / MidiMap / Passthrough
                // / ArduinoboyMaster / ...): pre-seeds the role so onActivate
                // skips the sniffer fallback. Mirrors the --script runner.
                if (!lsdjSyncMode.empty()) {
                    LsdjSyncConfig lsdj;
                    lsdj.mode = parseLsdjSyncMode(lsdjSyncMode);
                    cfg.roles.emplace_back(lsdj);
                }
                // Same nonzero linkGroup puts instances in a shared LinkGroup
                // for lockstep serial-bit ferrying (LSDj link-cable sync).
                cfg.linkGroupId = linkGroup;
                sys = std::make_unique<SameBoySystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::MesenNes: {
                MesenNesConfig cfg;
                cfg.romPath = path;
                sys = std::make_unique<MesenNesSystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::MesenGba: {
                MesenGbaConfig cfg;
                cfg.romPath = path; // no biosPath -> Mesen falls back to HLE BIOS
                sys = std::make_unique<MesenGbaSystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            default:
                throw std::runtime_error("loadRom: '" + path +
                    "' is not a recognised Game Boy, NES, or GBA ROM");
        }

        sys->onActivate(sampleRate);
        const SystemId id = sys->id();
        SystemBase* raw = sys.get();
        project->adoptSystem(sys.release());
        sysList.push_back(raw);
        project->rebuildLinkGroups();
        return static_cast<std::uint32_t>(id);
    }

    SystemBase* system(std::uint32_t id) {
        return project->findSystem(static_cast<SystemId>(id));
    }

    // Resolve a system + require it expose CPU state (non-empty register file).
    // No dynamic_cast: every backend answers the SystemBase CPU virtuals.
    SystemBase* cpuSystem(std::uint32_t id) {
        SystemBase* sys = system(id);
        if (!sys) throw std::runtime_error("unknown system id");
        if (sys->getCpuRegisters().empty())
            throw std::runtime_error("CPU state is not available for this system");
        return sys;
    }

    // Resolve a system's debugger/profiler (Mesen NES). Throws if unsupported.
    rp::IDebugTarget* debugTarget(std::uint32_t id) {
        SystemBase* sys = system(id);
        if (!sys) throw std::runtime_error("unknown system id");
        rp::IDebugTarget* d = sys->debugTarget();
        if (!d) throw std::runtime_error(
            "no debugger for this system (Mesen NES only)");
        return d;
    }

    // Drain each system's role outputs for the block just processed into the
    // per-system logs (absolute sample = sampleClock + event frame). Mirrors
    // the midiOut/serialOut drain in cli/main.cpp's render loop.
    void drainCaptures() {
        for (SystemBase* sys : sysList) {
            const SystemId id = sys->id();
            auto& mo = sys->midiOut();
            if (!mo.empty()) {
                auto& dst = midiOutLog[id];
                for (const auto& ev : mo) {
                    const std::uint32_t n =
                        std::min<std::uint32_t>(ev.size, ::MidiEvent::kDataSize);
                    dst.push_back(MidiOutRec{ sampleClock + ev.frame,
                        std::vector<std::uint8_t>(ev.data, ev.data + n) });
                }
                mo.clear();
            }
            if (auto* sb = dynamic_cast<SameBoySystem*>(sys)) {
                auto& raw = sb->serialOutLog_;
                if (!raw.empty()) {
                    auto& dst = serialOutLog[id];
                    for (const auto& [frame, byte] : raw)
                        dst.push_back(SerialRec{ sampleClock + frame, byte });
                    raw.clear();
                }
            }
        }
    }

    // One render block: build the AudioBlockInfo from the simulated transport,
    // process, optionally capture the mixed stereo output, drain role outputs,
    // then advance the transport clock.
    void stepBlock(std::uint32_t frames, std::vector<float>* capture) {
        float* outs[2] = { scratchL.data(), scratchR.data() };
        std::fill_n(scratchL.data(), frames, 0.0f);
        std::fill_n(scratchR.data(), frames, 0.0f);
        AudioBlockInfo info{ frames, sampleRate, bpm, ppq, transportPlaying };
        project->onProcess(info, outs);
        if (capture) {
            for (std::uint32_t f = 0; f < frames; ++f) {
                capture->push_back(scratchL[f]);
                capture->push_back(scratchR[f]);
            }
        }
        drainCaptures();
        sampleClock += frames;
        if (transportPlaying)
            ppq += (bpm / 60.0) * (static_cast<double>(frames) / sampleRate);
    }

    void runMs(double ms) {
        if (ms <= 0.0) return;
        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        for (std::uint64_t s = 0; s < total; s += blockSize)
            stepBlock(static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s)), nullptr);
    }

    // Like runMs but retains the mixed stereo output interleaved (L,R,L,R…).
    std::vector<float> runMsCapture(double ms) {
        std::vector<float> out;
        if (ms <= 0.0) return out;
        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        out.reserve(total * 2);
        for (std::uint64_t s = 0; s < total; s += blockSize)
            stepBlock(static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s)), &out);
        return out;
    }

    // Advance `ms` and return each system's audio in its own interleaved buffer
    // (out[i] = L,R,L,R… for system i). SameBoy-only — mirrors the manual
    // prepareForBlock → stepIfBelowTarget → finishBlock orchestration in
    // cli/main.cpp's --per-system-wav path, which interleaves linked systems the
    // same way LinkGroup does. Used to prove LSDj link-cable sync (the follower
    // produces audio only when actually synced to the leader).
    std::vector<std::vector<float>> runMsPerSystem(double ms) {
        std::vector<std::vector<float>> out(sysList.size());
        if (ms <= 0.0 || sysList.empty()) return out;
        std::vector<SameBoySystem*> sb;
        sb.reserve(sysList.size());
        for (SystemBase* s : sysList) {
            auto* p = dynamic_cast<SameBoySystem*>(s);
            if (!p) throw std::runtime_error("runMsPerSystem is SameBoy-only");
            sb.push_back(p);
        }
        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        std::vector<float> bl(blockSize), br(blockSize);
        for (std::uint64_t s = 0; s < total; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s));
            AudioBlockInfo info{ frames, sampleRate, bpm, ppq, transportPlaying };
            for (auto* x : sb) x->prepareForBlock(info);
            bool any = true;
            while (any) {
                any = false;
                for (auto* x : sb) if (x->stepIfBelowTarget(frames)) any = true;
            }
            for (std::size_t i = 0; i < sb.size(); ++i) {
                std::fill_n(bl.data(), frames, 0.0f);
                std::fill_n(br.data(), frames, 0.0f);
                float* o[2] = { bl.data(), br.data() };
                sb[i]->finishBlock(info, o);
                for (std::uint32_t f = 0; f < frames; ++f) {
                    out[i].push_back(bl[f]);
                    out[i].push_back(br[f]);
                }
            }
            drainCaptures();
            sampleClock += frames;
            if (transportPlaying)
                ppq += (bpm / 60.0) * (static_cast<double>(frames) / sampleRate);
        }
        return out;
    }

    // Compile a kit from sample files and queue it into the system's
    // LsdjKitPatchRole (the sniffer auto-attaches it to LSDj ROMs). The role
    // applies the bank at the top of the next process block, so call runMs after.
    // Mirrors the patch_kit path in cli/main.cpp.
    void patchKit(std::uint32_t id, std::uint8_t slot, const std::string& name,
                  const std::vector<std::pair<std::string, std::string>>& samples) {
        auto* sb = dynamic_cast<SameBoySystem*>(system(id));
        if (!sb) throw std::runtime_error("patchKit: system is not SameBoy");
        if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::lsdj::KitCompiler>();
        std::vector<rp::lsdj::CompileSampleSpec> specs;
        specs.reserve(samples.size());
        for (const auto& s : samples) {
            rp::lsdj::CompileSampleSpec sp;
            sp.path = s.first;
            sp.name = s.second;
            specs.push_back(std::move(sp));
        }
        auto compiled = kitCompiler_->compileKit(name, specs);
        if (!compiled.ok || compiled.bytes.size() != rp::lsdj::Kit::kSize)
            throw std::runtime_error("patchKit: compile failed: " + compiled.error);
        LsdjKitPatchRole* role = nullptr;
        for (auto& r : sb->roles_) {
            if (r && r->kind() == "lsdj-kit-patch") {
                role = static_cast<LsdjKitPatchRole*>(r.get());
                break;
            }
        }
        if (!role) throw std::runtime_error("patchKit: system has no lsdj-kit-patch role");
        role->queuePatch(slot, std::move(compiled.bytes));
    }

    // Snapshot the project's current config + savestate into a .rplg (pure
    // PKZIP from projectConfigToZip). Used to author Reaper DAW fixtures: a TS
    // test builds the LSDj/mGB state, then writes the .rplg the plugin auto-loads
    // via RETROPLUG_AUTOLOAD_PROJECT. Mirrors cli/main.cpp's --save-rplg.
    void saveRplg(const std::string& path) {
        const auto zip = projectConfigToZip(project->snapshotConfig());
        if (zip.empty())
            throw std::runtime_error("saveRplg: projectConfigToZip returned empty");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("saveRplg: cannot open " + path);
        f.write(reinterpret_cast<const char*>(zip.data()),
                static_cast<std::streamsize>(zip.size()));
        if (!f) throw std::runtime_error("saveRplg: write failed: " + path);
    }

    // Inverse of saveRplg: parse a .rplg (config + per-system savestate) and
    // rebuild the project from it — the harness-side mirror of the plugin's
    // RETROPLUG_AUTOLOAD_PROJECT / setState path (projectConfigFromZip ->
    // addSystem -> onActivate, restoring each GB savestate). Lets a test
    // round-trip a fixture (saveRplg then loadRplg) to reproduce exactly what a
    // DAW sees when it reloads the project. Returns the first restored system id.
    std::uint32_t loadRplg(const std::string& path) {
        auto bytes = slurpBytes(path);
        auto parsed = projectConfigFromZip(bytes);
        if (!parsed)
            throw std::runtime_error("loadRplg: failed to parse " + path);
        // Fresh project, mirroring PluginDSP::applyProjectFromConfig's
        // clearSystems + rebuild.
        project = std::make_unique<Project>();
        sysList.clear();
        midiOutLog.clear();
        serialOutLog.clear();
        std::vector<SystemId> ids;
        for (const auto& sysConfig : parsed->systems) {
            const SystemId id = project->addSystem(sysConfig);
            if (id == 0) throw std::runtime_error("loadRplg: addSystem failed");
            ids.push_back(id);
        }
        project->onActivate(sampleRate); // restores each system's savestate
        for (SystemId id : ids)
            if (SystemBase* raw = project->findSystem(id)) sysList.push_back(raw);
        project->rebuildLinkGroups();
        return ids.empty() ? 0u : static_cast<std::uint32_t>(ids.front());
    }

    // Take + clear the accumulated role outputs for a system.
    std::vector<MidiOutRec> takeMidi(std::uint32_t id) {
        auto it = midiOutLog.find(static_cast<SystemId>(id));
        if (it == midiOutLog.end()) return {};
        std::vector<MidiOutRec> v = std::move(it->second);
        it->second.clear();
        return v;
    }
    std::vector<SerialRec> takeSerial(std::uint32_t id) {
        auto it = serialOutLog.find(static_cast<SystemId>(id));
        if (it == serialOutLog.end()) return {};
        std::vector<SerialRec> v = std::move(it->second);
        it->second.clear();
        return v;
    }

    // Fresh Project per test() case so cases can't bleed emulator state.
    void beginCase() {
        project = std::make_unique<Project>();
        sysList.clear();
        midiOutLog.clear();
        serialOutLog.clear();
        bpm = 120.0;
        transportPlaying = false;
        ppq = 0.0;
        sampleClock = 0;
    }

    void report(const std::string& name, bool ok, const std::string& msg) {
        ++testIndex;
        if (ok) {
            std::printf("ok %d - %s\n", testIndex, name.c_str());
        } else {
            ++failures;
            std::printf("not ok %d - %s\n", testIndex, name.c_str());
            if (!msg.empty()) {
                std::printf("  ---\n  message: \"%s\"\n  ...\n",
                            oneLine(msg).c_str());
            }
        }
        std::fflush(stdout);
    }

    void done() {
        if (donePrinted) return;
        donePrinted = true;
        std::printf("1..%d\n", testIndex);
        std::fflush(stdout);
    }
};

// The active harness for the current process. txiki occupies BOTH the QuickJS
// context- and runtime-opaque slots (vm.c stores its TJSRuntime* in each), so
// we cannot stash our Impl there. One runtime per process + single-threaded
// means a translation-unit pointer is the correct recovery mechanism for the
// static JS trampolines.
namespace { TestHarness::Impl* g_activeImpl = nullptr; }

// ---------------------------------------------------------------------------
// JS trampolines. Every body is wrapped so a C++ throw never crosses into
// QuickJS (which does not catch C++ exceptions).
// ---------------------------------------------------------------------------

namespace {

JSValue jsLoadRom(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 1) return JS_ThrowTypeError(ctx, "loadRom(path) requires a path");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    // Optional 2nd arg: an ArrayBuffer of cartridge SRAM (a .sav image).
    std::vector<std::uint8_t> sram;
    bool hasSram = false;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        std::size_t len = 0;
        std::uint8_t* buf = JS_GetArrayBuffer(ctx, &len, argv[1]);
        if (buf) { sram.assign(buf, buf + len); hasSram = true; }
    }
    // Optional 3rd arg: an LSDj sync-mode name (sets the role on the system).
    std::string syncMode;
    if (argc >= 3 && JS_IsString(argv[2])) {
        const char* sm = JS_ToCString(ctx, argv[2]);
        if (sm) { syncMode = sm; JS_FreeCString(ctx, sm); }
    }
    // Optional 4th arg: link-group id (same nonzero value = lockstep serial).
    int32_t linkGroup = 0;
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
        JS_ToInt32(ctx, &linkGroup, argv[3]);
    try {
        const std::uint32_t id = h->loadRom(path, hasSram ? &sram : nullptr,
            syncMode, static_cast<std::uint8_t>(linkGroup));
        JS_FreeCString(ctx, path);
        return JS_NewInt32(ctx, static_cast<int32_t>(id));
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "%s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

// savFromJson(json) -> ArrayBuffer: build a 128 KiB sav image from a (possibly
// partial) Sav-model JSON fixture. Missing fields take model defaults, so a
// fixture can specify only what it cares about.
JSValue jsSavFromJson(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "savFromJson(json) requires a json string");
    const char* json = JS_ToCString(ctx, argv[0]);
    if (!json) return JS_EXCEPTION;
    auto sav = rp::lsdj::savFromJsonFixture(json);
    JS_FreeCString(ctx, json);
    if (!sav) return JS_ThrowTypeError(ctx, "savFromJson: %s", sav.error().what().c_str());
    const auto bytes = rp::lsdj::codec::encodeSav(sav.value());
    return JS_NewArrayBufferCopy(ctx, bytes.data(), bytes.size());
}

// readFile(path) -> ArrayBuffer: slurp a file's raw bytes (e.g. a source .sav).
JSValue jsReadFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "readFile(path) requires a path");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const auto bytes = slurpBytes(path);
        JS_FreeCString(ctx, path);
        return JS_NewArrayBufferCopy(ctx, bytes.data(), bytes.size());
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "readFile: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

// writeFile(path, bytes): dump an ArrayBuffer's raw bytes to a file.
JSValue jsWriteFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "writeFile(path, bytes) requires a path and ArrayBuffer");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::size_t len = 0;
    std::uint8_t* buf = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (!buf) { JS_FreeCString(ctx, path); return JS_ThrowTypeError(ctx, "writeFile: bytes must be an ArrayBuffer"); }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const bool ok = f.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len)).good();
    JS_FreeCString(ctx, path);
    if (!ok) return JS_ThrowTypeError(ctx, "writeFile: write failed");
    return JS_UNDEFINED;
}

// savRoundtripDiff(savBytes) -> number: decode the working song, re-encode it
// from the model with the input as template, and return the first non-volatile
// byte offset that differs (or -1 if byte-identical). Skips the volatile clock +
// fileChanged bytes. Mirrors the C++ content round-trip so a TS test can byte-
// check a captured sav (e.g. one LSDj upgraded in SRAM) without a fixtures dir.
JSValue jsSavRoundtripDiff(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "savRoundtripDiff(savBytes) requires an ArrayBuffer");
    std::size_t len = 0;
    std::uint8_t* buf = JS_GetArrayBuffer(ctx, &len, argv[0]);
    if (!buf) return JS_ThrowTypeError(ctx, "savRoundtripDiff: arg must be an ArrayBuffer");
    constexpr std::size_t kSong = 0x8000;
    if (len < kSong) return JS_ThrowTypeError(ctx, "savRoundtripDiff: need >= 0x8000 bytes");
    std::span<const std::uint8_t> orig(buf, kSong);
    auto res = rp::lsdj::codec::decodeSong(orig);
    if (!res) return JS_ThrowTypeError(ctx, "savRoundtripDiff: decode: %s", res.error().what().c_str());
    const auto out = rp::lsdj::codec::encodeSong(res.value(), orig);
    const auto isVolatile = [](std::size_t off) {
        return off == 0x3FB2 || off == 0x3FB3 || (off >= 0x3FB6 && off <= 0x3FB9) || off == 0x3FC1;
    };
    for (std::size_t i = 0; i < kSong; ++i)
        if (orig[i] != out[i] && !isVolatile(i))
            return JS_NewInt32(ctx, static_cast<int32_t>(i));
    return JS_NewInt32(ctx, -1);
}

JSValue jsRunMs(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double ms = 0.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) return JS_EXCEPTION;
    try {
        h->runMs(ms);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runMs: %s", e.what());
    }
}

JSValue jsPress(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, button = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "press(id, button, down)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &button, argv[1]) < 0) return JS_EXCEPTION;
    const int down = JS_ToBool(ctx, argv[2]);
    if (down < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        sys->pressButton(static_cast<std::uint8_t>(button), down == 1);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "press: %s", e.what());
    }
}

JSValue jsReadMemory(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, type = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readMemory(id, type)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[1]) < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        // Always hand JS a COPY — the accessor's pointer is live emulator
        // memory and relocates on reset/cart-swap.
        rp::MemoryAccessor acc = sys->getMemory(
            static_cast<rp::MemoryType>(type), rp::AccessType::Read);
        if (!acc.valid()) {
            const std::uint8_t empty = 0;
            return JS_NewArrayBufferCopy(ctx, &empty, 0);
        }
        return JS_NewArrayBufferCopy(ctx, acc.data(), acc.size());
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readMemory: %s", e.what());
    }
}

JSValue jsGetRegisters(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getRegisters(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        // Name-keyed register file — each backend reports its own CPU's set.
        const std::vector<rp::CpuRegister> regs =
            h->cpuSystem(static_cast<std::uint32_t>(id))->getCpuRegisters();
        JSValue obj = JS_NewObject(ctx);
        for (const auto& r : regs) {
            JS_SetPropertyStr(ctx, obj, r.name.c_str(),
                              JS_NewInt64(ctx, static_cast<int64_t>(r.value)));
        }
        return obj;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getRegisters: %s", e.what());
    }
}

JSValue jsSetRegister(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    int64_t value = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "setRegister(id, name, value)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &value, argv[2]) < 0) { JS_FreeCString(ctx, name); return JS_EXCEPTION; }
    try {
        const bool ok = h->cpuSystem(static_cast<std::uint32_t>(id))
            ->setCpuRegister(name, static_cast<std::uint32_t>(value));
        if (!ok) {
            JSValue err = JS_ThrowTypeError(ctx,
                "setRegister: unknown or read-only register '%s'", name);
            JS_FreeCString(ctx, name);
            return err;
        }
        JS_FreeCString(ctx, name);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "setRegister: %s", e.what());
        JS_FreeCString(ctx, name);
        return err;
    }
}

JSValue jsReadCpu(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, addr = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readCpu(id, addr)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &addr, argv[1]) < 0) return JS_EXCEPTION;
    try {
        const std::optional<std::uint8_t> b =
            h->cpuSystem(static_cast<std::uint32_t>(id))
                ->readCpuByte(static_cast<std::uint32_t>(addr));
        if (!b)
            throw std::runtime_error(
                "side-effect-free CPU peek is not supported for this system "
                "(use readMemory)");
        return JS_NewInt32(ctx, *b);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readCpu: %s", e.what());
    }
}

JSValue jsStep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "step(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        // 0 = backend can't instruction-step (e.g. GBA without the debugger).
        const std::uint64_t cycles =
            h->cpuSystem(static_cast<std::uint32_t>(id))->stepInstruction();
        return JS_NewInt64(ctx, static_cast<int64_t>(cycles));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "step: %s", e.what());
    }
}

JSValue jsRunUntilPc(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, target = 0;
    int64_t maxCycles = 0;
    if (argc < 3) return JS_ThrowTypeError(ctx, "runUntilPc(id, pc, maxCycles)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &target, argv[1]) < 0) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &maxCycles, argv[2]) < 0) return JS_EXCEPTION;
    if (maxCycles <= 0) return JS_ThrowRangeError(ctx, "runUntilPc: maxCycles must be > 0");
    try {
        const bool hit = h->cpuSystem(static_cast<std::uint32_t>(id))->runUntilPc(
            static_cast<std::uint32_t>(target),
            static_cast<std::uint64_t>(maxCycles));
        return JS_NewBool(ctx, hit);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runUntilPc: %s", e.what());
    }
}

JSValue jsSendMidi(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "sendMidi(id, bytes)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    JSValue lenv = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t len = 0; JS_ToInt32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
    if (len < 1 || len > (int32_t)::MidiEvent::kDataSize)
        return JS_ThrowRangeError(ctx, "sendMidi: expected 1..%u bytes",
                                  (unsigned)::MidiEvent::kDataSize);
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        ::MidiEvent ev{};
        ev.frame = 0;
        ev.size  = static_cast<std::uint32_t>(len);
        for (int32_t i = 0; i < len; ++i) {
            JSValue b = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
            int32_t v = 0; JS_ToInt32(ctx, &v, b); JS_FreeValue(ctx, b);
            ev.data[i] = static_cast<std::uint8_t>(v);
        }
        sys->onMidi(&ev, 1); // queued for the next runMs (NES: into the N8 FIFO)
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "sendMidi: %s", e.what());
    }
}

// setTransport(running): start/stop the simulated host transport. While running,
// ppq advances each block so LsdjSyncRole (SYNC=MIDI) emits clock like a DAW.
JSValue jsSetTransport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 1) return JS_ThrowTypeError(ctx, "setTransport(running)");
    const int running = JS_ToBool(ctx, argv[0]);
    if (running < 0) return JS_EXCEPTION;
    h->transportPlaying = (running == 1);
    return JS_UNDEFINED;
}

// setBpm(bpm): set the simulated host tempo (default 120).
JSValue jsSetBpm(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double bpm = 0.0;
    if (argc < 1 || JS_ToFloat64(ctx, &bpm, argv[0]) < 0) return JS_EXCEPTION;
    if (bpm <= 0.0) return JS_ThrowRangeError(ctx, "setBpm: bpm must be > 0");
    h->bpm = bpm;
    return JS_UNDEFINED;
}

// drainMidi(id) -> [{ sample, bytes: number[] }]: take + clear the MIDI a role
// emitted back to the host since the last drain (e.g. Arduinoboy MI.OUT clock).
JSValue jsDrainMidi(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "drainMidi(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const auto recs = h->takeMidi(static_cast<std::uint32_t>(id));
    JSValue arr = JS_NewArray(ctx);
    for (std::uint32_t i = 0; i < recs.size(); ++i) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "sample", JS_NewInt64(ctx, (int64_t)recs[i].sample));
        JSValue b = JS_NewArray(ctx);
        for (std::uint32_t j = 0; j < recs[i].bytes.size(); ++j)
            JS_SetPropertyUint32(ctx, b, j, JS_NewInt32(ctx, recs[i].bytes[j]));
        JS_SetPropertyStr(ctx, o, "bytes", b);
        JS_SetPropertyUint32(ctx, arr, i, o);
    }
    return arr;
}

// drainSerial(id) -> [{ sample, byte }]: take + clear the raw GB serial-out
// byte stream captured since the last drain (Arduinoboy master mode).
JSValue jsDrainSerial(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "drainSerial(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const auto recs = h->takeSerial(static_cast<std::uint32_t>(id));
    JSValue arr = JS_NewArray(ctx);
    for (std::uint32_t i = 0; i < recs.size(); ++i) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "sample", JS_NewInt64(ctx, (int64_t)recs[i].sample));
        JS_SetPropertyStr(ctx, o, "byte",   JS_NewInt32(ctx, recs[i].byte));
        JS_SetPropertyUint32(ctx, arr, i, o);
    }
    return arr;
}

JSValue jsBeginProfile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "beginProfile(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        h->debugTarget(static_cast<std::uint32_t>(id))->beginProfile();
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "beginProfile: %s", e.what());
    }
}

JSValue jsReadProfile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "readProfile(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const std::vector<rp::ProfiledFunction> fns =
            h->debugTarget(static_cast<std::uint32_t>(id))->readProfile();
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < fns.size(); ++i) {
            const rp::ProfiledFunction& f = fns[i];
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, f.address));
            JS_SetPropertyStr(ctx, o, "label",
                JS_NewStringLen(ctx, f.label.data(), f.label.size()));
            JS_SetPropertyStr(ctx, o, "exclusiveCycles", JS_NewInt64(ctx, (int64_t)f.exclusiveCycles));
            JS_SetPropertyStr(ctx, o, "inclusiveCycles", JS_NewInt64(ctx, (int64_t)f.inclusiveCycles));
            JS_SetPropertyStr(ctx, o, "callCount",       JS_NewInt64(ctx, (int64_t)f.callCount));
            JS_SetPropertyStr(ctx, o, "minCycles",       JS_NewInt64(ctx, (int64_t)f.minCycles));
            JS_SetPropertyStr(ctx, o, "maxCycles",       JS_NewInt64(ctx, (int64_t)f.maxCycles));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readProfile: %s", e.what());
    }
}

JSValue jsLoadLabels(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "loadLabels(id, path)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* path = JS_ToCString(ctx, argv[1]);
    if (!path) return JS_EXCEPTION;
    try {
        const bool ok = h->debugTarget(static_cast<std::uint32_t>(id))->loadLabels(path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "loadLabels: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsDisassemble(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, addr = 0, count = 1;
    if (argc < 3) return JS_ThrowTypeError(ctx, "disassemble(id, addr, count)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &addr, argv[1]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[2]) < 0) return JS_EXCEPTION;
    try {
        const auto lines = h->debugTarget(static_cast<std::uint32_t>(id))
            ->disassemble(static_cast<std::uint32_t>(addr), static_cast<std::uint32_t>(count));
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < lines.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, lines[i].address));
            JS_SetPropertyStr(ctx, o, "text",  JS_NewStringLen(ctx, lines[i].text.data(), lines[i].text.size()));
            JS_SetPropertyStr(ctx, o, "bytes", JS_NewStringLen(ctx, lines[i].bytes.data(), lines[i].bytes.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "disassemble: %s", e.what());
    }
}

JSValue jsSetTrace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "setTrace(id, on)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const int on = JS_ToBool(ctx, argv[1]);
    if (on < 0) return JS_EXCEPTION;
    try {
        h->debugTarget(static_cast<std::uint32_t>(id))->setTraceEnabled(on == 1);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "setTrace: %s", e.what());
    }
}

JSValue jsReadTrace(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0, count = 1;
    if (argc < 2) return JS_ThrowTypeError(ctx, "readTrace(id, count)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[1]) < 0) return JS_EXCEPTION;
    try {
        const auto rows = h->debugTarget(static_cast<std::uint32_t>(id))
            ->readTrace(static_cast<std::uint32_t>(count));
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < rows.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "pc",   JS_NewInt32(ctx, (int32_t)rows[i].pc));
            JS_SetPropertyStr(ctx, o, "text", JS_NewStringLen(ctx, rows[i].text.data(), rows[i].text.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "readTrace: %s", e.what());
    }
}

JSValue jsGetCallStack(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getCallStack(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const auto frames = h->debugTarget(static_cast<std::uint32_t>(id))->getCallStack();
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < frames.size(); ++i) {
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "address", JS_NewInt32(ctx, frames[i].address));
            JS_SetPropertyStr(ctx, o, "label", JS_NewStringLen(ctx, frames[i].label.data(), frames[i].label.size()));
            JS_SetPropertyUint32(ctx, arr, i, o);
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getCallStack: %s", e.what());
    }
}

JSValue breakInfoToJs(JSContext* ctx, const rp::BreakInfo& bi) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "broke",        JS_NewBool(ctx, bi.broke));
    JS_SetPropertyStr(ctx, o, "pc",           JS_NewInt32(ctx, (int32_t)bi.pc));
    JS_SetPropertyStr(ctx, o, "breakpointId", JS_NewInt32(ctx, bi.breakpointId));
    return o;
}

JSValue jsSetBreakpoints(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "setBreakpoints(id, bps[])");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (!JS_IsArray(argv[1])) return JS_ThrowTypeError(ctx, "setBreakpoints: bps must be an array");
    try {
        JSValue lenv = JS_GetPropertyStr(ctx, argv[1], "length");
        int32_t len = 0; JS_ToInt32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
        std::vector<rp::BreakpointSpec> specs;
        for (int32_t i = 0; i < len; ++i) {
            JSValue o = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
            rp::BreakpointSpec s;
            JSValue tv = JS_GetPropertyStr(ctx, o, "type");
            if (const char* ts = JS_ToCString(ctx, tv)) { s.type = ts; JS_FreeCString(ctx, ts); }
            JS_FreeValue(ctx, tv);
            JSValue sv = JS_GetPropertyStr(ctx, o, "start");
            int32_t sa = 0; JS_ToInt32(ctx, &sa, sv); s.start = (uint32_t)sa; JS_FreeValue(ctx, sv);
            JSValue ev = JS_GetPropertyStr(ctx, o, "end");
            int32_t ea = 0; if (!JS_IsUndefined(ev)) JS_ToInt32(ctx, &ea, ev); s.end = (uint32_t)ea; JS_FreeValue(ctx, ev);
            JSValue cv = JS_GetPropertyStr(ctx, o, "condition");
            if (JS_IsString(cv)) { if (const char* cs = JS_ToCString(ctx, cv)) { s.condition = cs; JS_FreeCString(ctx, cs); } }
            JS_FreeValue(ctx, cv);
            JS_FreeValue(ctx, o);
            specs.push_back(std::move(s));
        }
        h->debugTarget(static_cast<std::uint32_t>(id))->setBreakpoints(specs);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "setBreakpoints: %s", e.what());
    }
}

JSValue jsRunUntilBreak(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0; int64_t maxCycles = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "runUntilBreak(id, maxCycles)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &maxCycles, argv[1]) < 0) return JS_EXCEPTION;
    if (maxCycles <= 0) return JS_ThrowRangeError(ctx, "runUntilBreak: maxCycles must be > 0");
    try {
        return breakInfoToJs(ctx, h->debugTarget(static_cast<std::uint32_t>(id))
            ->runUntilBreak(static_cast<std::uint64_t>(maxCycles)));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runUntilBreak: %s", e.what());
    }
}

// magic: 0=step, 1=stepOver, 2=stepOut
JSValue jsStepKind(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "step(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        rp::IDebugTarget* d = h->debugTarget(static_cast<std::uint32_t>(id));
        rp::BreakInfo bi = magic == 1 ? d->stepOver()
                         : magic == 2 ? d->stepOut()
                                      : d->step();
        return breakInfoToJs(ctx, bi);
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "step: %s", e.what());
    }
}

JSValue jsGetFrame(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 1) return JS_ThrowTypeError(ctx, "getFrame(id)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        FrameBufferTriple* fb = sys->framebuffer();
        if (!fb) throw std::runtime_error("system has no framebuffer");

        const std::uint32_t fbW = fb->width();
        const std::uint32_t fbH = fb->height();
        const std::size_t pixels = static_cast<std::size_t>(fbW) * fbH;
        std::vector<std::uint32_t> xrgb(pixels);
        const bool published = fb->readInto(xrgb.data(),
                                            static_cast<std::uint32_t>(pixels));

        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "width",  JS_NewInt32(ctx, fbW));
        JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, fbH));
        JS_SetPropertyStr(ctx, obj, "published", JS_NewBool(ctx, published));
        // XRGB8888 bytes (empty when no frame has been published yet).
        const std::uint8_t* bytes =
            reinterpret_cast<const std::uint8_t*>(xrgb.data());
        JS_SetPropertyStr(ctx, obj, "data",
            JS_NewArrayBufferCopy(ctx, bytes, published ? pixels * 4 : 0));
        return obj;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getFrame: %s", e.what());
    }
}

JSValue jsScreenshot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    int32_t id = 0;
    if (argc < 2) return JS_ThrowTypeError(ctx, "screenshot(id, path)");
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    const char* path = JS_ToCString(ctx, argv[1]);
    if (!path) return JS_EXCEPTION;
    try {
        SystemBase* sys = h->system(static_cast<std::uint32_t>(id));
        if (!sys) throw std::runtime_error("unknown system id");
        const bool ok = rpcli::writeFramebufferPng(*sys, path);
        JS_FreeCString(ctx, path);
        return JS_NewBool(ctx, ok);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "screenshot: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsGetAudio(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double ms = 0.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const std::vector<float> samples = h->runMsCapture(ms);
        return JS_NewArrayBufferCopy(ctx,
            reinterpret_cast<const std::uint8_t*>(samples.data()),
            samples.size() * sizeof(float));
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "getAudio: %s", e.what());
    }
}

// runMsPerSystem(ms) -> [ArrayBuffer]: per-system interleaved stereo float32.
// writeWav(path, interleavedStereoFloat32, sampleRate?): dump audio (e.g. from
// runMsPerSystem) to a 16-bit stereo WAV so external tools (the reaper MCP audio
// analysis) can inspect it. Input is interleaved L,R,L,R… float32.
// patchKit(sys, slot, name, samples): samples = [{path, name}]. Compiles + queues
// a custom LSDj drum kit into the slot; call runMs afterwards for the role to
// apply it to the cartridge ROM.
JSValue jsPatchKit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 4) return JS_ThrowTypeError(ctx, "patchKit(sys, slot, name, samples)");
    int32_t id = 0, slot = 0;
    if (JS_ToInt32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &slot, argv[1]) < 0) return JS_EXCEPTION;
    const char* name = JS_ToCString(ctx, argv[2]);
    if (!name) return JS_EXCEPTION;
    std::string kitName = name; JS_FreeCString(ctx, name);
    std::vector<std::pair<std::string, std::string>> samples;
    if (JS_IsArray(argv[3])) {
        JSValue lenv = JS_GetPropertyStr(ctx, argv[3], "length");
        int32_t len = 0; JS_ToInt32(ctx, &len, lenv); JS_FreeValue(ctx, lenv);
        for (int32_t i = 0; i < len; ++i) {
            JSValue o = JS_GetPropertyUint32(ctx, argv[3], (uint32_t)i);
            std::string p, n;
            JSValue pv = JS_GetPropertyStr(ctx, o, "path");
            if (const char* s = JS_ToCString(ctx, pv)) { p = s; JS_FreeCString(ctx, s); }
            JS_FreeValue(ctx, pv);
            JSValue nv = JS_GetPropertyStr(ctx, o, "name");
            if (const char* s = JS_ToCString(ctx, nv)) { n = s; JS_FreeCString(ctx, s); }
            JS_FreeValue(ctx, nv);
            JS_FreeValue(ctx, o);
            samples.emplace_back(std::move(p), std::move(n));
        }
    }
    try {
        h->patchKit(static_cast<std::uint32_t>(id), static_cast<std::uint8_t>(slot),
                    kitName, samples);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "patchKit: %s", e.what());
    }
}

// saveRplg(path): snapshot the project state into a .rplg fixture.
JSValue jsSaveRplg(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 1) return JS_ThrowTypeError(ctx, "saveRplg(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        h->saveRplg(path);
        JS_FreeCString(ctx, path);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "saveRplg: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

// loadRplg(path): rebuild the project from a .rplg (round-trip of saveRplg).
JSValue jsLoadRplg(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 1) return JS_ThrowTypeError(ctx, "loadRplg(path)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    try {
        const std::uint32_t id = h->loadRplg(path);
        JS_FreeCString(ctx, path);
        return JS_NewUint32(ctx, id);
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "loadRplg: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsWriteWav(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "writeWav(path, samples, sampleRate?)");
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::size_t len = 0;
    std::uint8_t* buf = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (!buf) { JS_FreeCString(ctx, path); return JS_ThrowTypeError(ctx, "writeWav: samples must be an ArrayBuffer"); }
    std::uint32_t sr = 44100;
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        int32_t v = 44100; JS_ToInt32(ctx, &v, argv[2]); if (v > 0) sr = static_cast<std::uint32_t>(v);
    }
    try {
        const float* data = reinterpret_cast<const float*>(buf);
        const std::size_t frames = (len / sizeof(float)) / 2; // interleaved stereo
        std::vector<float> l(frames), r(frames);
        for (std::size_t i = 0; i < frames; ++i) { l[i] = data[2 * i]; r[i] = data[2 * i + 1]; }
        float* outs[2] = { l.data(), r.data() };
        WavWriter w(path, sr, 2);
        w.writeBlockFloatPlanar(outs, static_cast<std::uint32_t>(frames));
        JS_FreeCString(ctx, path);
        return JS_UNDEFINED;
    } catch (const std::exception& e) {
        JSValue err = JS_ThrowTypeError(ctx, "writeWav: %s", e.what());
        JS_FreeCString(ctx, path);
        return err;
    }
}

JSValue jsRunMsPerSystem(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    double ms = 0.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &ms, argv[0]) < 0) return JS_EXCEPTION;
    try {
        const auto perSys = h->runMsPerSystem(ms);
        JSValue arr = JS_NewArray(ctx);
        for (std::uint32_t i = 0; i < perSys.size(); ++i) {
            JS_SetPropertyUint32(ctx, arr, i, JS_NewArrayBufferCopy(ctx,
                reinterpret_cast<const std::uint8_t*>(perSys[i].data()),
                perSys[i].size() * sizeof(float)));
        }
        return arr;
    } catch (const std::exception& e) {
        return JS_ThrowTypeError(ctx, "runMsPerSystem: %s", e.what());
    }
}

JSValue jsBeginCase(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->beginCase();
    return JS_UNDEFINED;
}

JSValue jsReport(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    if (argc < 2) return JS_ThrowTypeError(ctx, "report(name, ok, message?)");
    const char* name = JS_ToCString(ctx, argv[0]);
    const int   ok   = JS_ToBool(ctx, argv[1]);
    const char* msg  = (argc >= 3 && !JS_IsUndefined(argv[2]))
                           ? JS_ToCString(ctx, argv[2]) : nullptr;
    h->report(name ? name : "", ok == 1, msg ? msg : "");
    if (name) JS_FreeCString(ctx, name);
    if (msg)  JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

JSValue jsDone(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* h = g_activeImpl;
    if (!h) return JS_ThrowInternalError(ctx, "harness unavailable");
    h->done();
    return JS_UNDEFINED;
}

// Console shim. txiki's built-in console writes to stdout, which would corrupt
// the TAP stream; route everything to stderr with the project's [js:<level>]
// convention instead.
JSValue jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int level = 0;
    if (argc >= 1) JS_ToInt32(ctx, &level, argv[0]);
    const char* msg = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    const char* tag = level >= 2 ? "error" : level == 1 ? "warn" : "log";
    std::fprintf(stderr, "[js:%s] %s\n", tag, msg ? msg : "");
    if (msg) JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}

} // namespace

// ---------------------------------------------------------------------------
// TestHarness lifecycle.
// ---------------------------------------------------------------------------

TestHarness::TestHarness() : impl_(std::make_unique<Impl>()) {
    // Idempotent global init (mirrors src/LvglJsEngine.cpp:130-136).
    static bool tjsInitialized = false;
    if (!tjsInitialized) {
        static char arg0[] = "retroplug-cli";
        static char* argv[] = { arg0, nullptr };
        TJS_Initialize(1, argv);
        tjsInitialized = true;
    }

    impl_->qrt = TJS_NewRuntime();
    if (!impl_->qrt) throw std::runtime_error("TJS_NewRuntime() failed");
    impl_->ctx = TJS_GetJSContext(impl_->qrt);

    // Recover *this Impl inside the static C trampolines. NOT via the context/
    // runtime opaque slots — txiki owns both for its TJSRuntime*.
    g_activeImpl = impl_.get();

    JSContext* ctx = impl_->ctx;

    // Build the Symbol.for("retroplug") namespace and attach the native
    // functions before defining it (DefinePropertyValue consumes the ref).
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sym    = JS_NewSymbol(ctx, "retroplug", /*is_global*/ true);
    JSAtom atom    = JS_ValueToAtom(ctx, sym);
    JSValue ns     = JS_NewObjectProto(ctx, JS_NULL);

    auto bind = [&](const char* name, JSCFunction* fn, int argc) {
        JS_SetPropertyStr(ctx, ns, name, JS_NewCFunction(ctx, fn, name, argc));
    };
    bind("loadRom",      jsLoadRom,      1);
    bind("savFromJson",  jsSavFromJson,  1);
    bind("readFile",     jsReadFile,     1);
    bind("writeFile",    jsWriteFile,    2);
    bind("savRoundtripDiff", jsSavRoundtripDiff, 1);
    bind("runMs",        jsRunMs,        1);
    bind("press",        jsPress,        3);
    bind("sendMidi",     jsSendMidi,     2);
    bind("setTransport", jsSetTransport, 1);
    bind("setBpm",       jsSetBpm,       1);
    bind("drainMidi",    jsDrainMidi,    1);
    bind("drainSerial",  jsDrainSerial,  1);
    bind("readMemory",   jsReadMemory,   2);
    bind("getRegisters", jsGetRegisters, 1);
    bind("setRegister",  jsSetRegister,  3);
    bind("readCpu",      jsReadCpu,      2);
    bind("step",         jsStep,         1);
    bind("runUntilPc",   jsRunUntilPc,   3);
    bind("getFrame",      jsGetFrame,      1);
    bind("screenshot",    jsScreenshot,    2);
    bind("getAudio",      jsGetAudio,      1);
    bind("runMsPerSystem", jsRunMsPerSystem, 1);
    bind("writeWav",       jsWriteWav,       3);
    bind("saveRplg",       jsSaveRplg,       1);
    bind("loadRplg",       jsLoadRplg,       1);
    bind("patchKit",       jsPatchKit,       4);
    bind("beginProfile",  jsBeginProfile,  1);
    bind("readProfile",   jsReadProfile,   1);
    bind("loadLabels",    jsLoadLabels,    2);
    bind("disassemble",    jsDisassemble,    3);
    bind("setTrace",       jsSetTrace,       2);
    bind("readTrace",      jsReadTrace,      2);
    bind("getCallStack",   jsGetCallStack,   1);
    bind("setBreakpoints", jsSetBreakpoints, 2);
    bind("runUntilBreak",  jsRunUntilBreak,  2);
    JS_SetPropertyStr(ctx, ns, "stepInto",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepInto", 1, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, ns, "stepOver",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepOver", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(ctx, ns, "stepOut",
        JS_NewCFunctionMagic(ctx, jsStepKind, "stepOut", 1, JS_CFUNC_generic_magic, 2));
    bind("beginCase",    jsBeginCase,    0);
    bind("report",       jsReport,       3);
    bind("done",         jsDone,         0);
    bind("log",          jsLog,          2);

    JS_DefinePropertyValue(ctx, global, atom, ns, JS_PROP_C_W_E);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, sym);
    JS_FreeValue(ctx, global);

    // Redirect console.* to stderr (keep stdout TAP-clean).
    static const char kConsoleShim[] =
        "(() => {"
        "  const rp = globalThis[Symbol.for('retroplug')];"
        "  const mk = (lvl) => (...a) => rp.log(lvl, a.map("
        "    x => typeof x === 'string' ? x : "
        "         (() => { try { return JSON.stringify(x); }"
        "                  catch (e) { return String(x); } })()"
        "  ).join(' '));"
        "  globalThis.console = { log: mk(0), info: mk(0), debug: mk(0),"
        "                         warn: mk(1), error: mk(2) };"
        "})();";
    JSValue r = JS_Eval(ctx, kConsoleShim, std::strlen(kConsoleShim),
                        "<console-shim>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, r);
}

TestHarness::~TestHarness() {
    if (impl_ && impl_->qrt) {
        TJS_FreeRuntime(impl_->qrt);
        impl_->qrt = nullptr;
        impl_->ctx = nullptr;
    }
    g_activeImpl = nullptr;
}

int TestHarness::runFile(const std::string& jsPath) {
    std::printf("TAP version 13\n");
    std::fflush(stdout);

    std::string code;
    try {
        code = slurpText(jsPath);
    } catch (const std::exception& e) {
        std::printf("Bail out! %s\n", e.what());
        std::fflush(stdout);
        return 1;
    }

    JSContext* ctx = impl_->ctx;
    // is_main=true fires the synthetic window 'load' event the runner hooks.
    JSValue res = TJS_EvalModuleContent(ctx, jsPath.c_str(), /*is_main*/ true,
                                        /*use_realpath*/ false, code.data(),
                                        code.size());
    const bool threw = JS_IsException(res);
    if (threw) tjs_dump_error(ctx);
    JS_FreeValue(ctx, res);

    // Drain any async work the tests scheduled (timers / promises). v1 tests
    // are synchronous, so a bounded pump is sufficient.
    for (int i = 0; i < 64; ++i) {
        uv_run(TJS_GetLoop(impl_->qrt), UV_RUN_NOWAIT);
        tjs__execute_jobs(ctx);
    }

    if (threw) {
        std::printf("Bail out! test module evaluation failed\n");
        std::fflush(stdout);
        return 1;
    }

    impl_->done();  // ensure a plan line even if the test forgot
    return impl_->failures > 0 ? 1 : 0;
}
