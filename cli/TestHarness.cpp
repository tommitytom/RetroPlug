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

#include "HarnessRpcService.hpp"
#include "HarnessRpcRegistration.hpp"
#include "TypedRpcServer.h"
#include "codecs/MsgpackCodec.h"
#include "transports/QueueTransport.h"

#include <span>

using HarnessRpcTransport = rpcpp::QueueTransport<rpcpp::MsgpackCodec>;
using HarnessRpcServer    = rpcpp::TypedRpcServer<HarnessRpcService, rpcpp::MsgpackCodec>;

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

    // rpcpp server stack (restructure-04): the generated TS client dispatches
    // here via Symbol.for("retroplug").__rpcSend. Declaration order matters —
    // the server references the service + transport, so it must be destroyed
    // first (members destruct in reverse order).
    std::unique_ptr<HarnessRpcService>    rpcService_;
    std::unique_ptr<HarnessRpcTransport>  rpcTransport_;
    std::unique_ptr<HarnessRpcServer>     rpcServer_;

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
// HarnessRpcService — the rpcpp-exposed emulator surface (restructure-04).
// A thin wrapper over Impl; bodies live here where Impl is complete. Only the
// Stage-0 proof subset is implemented (covering every wire shape: optional +
// Bytestring param, scalar/void, buffer return, new-DTO-vector, DTO-with-buffer,
// reused-DTO). The rest are ported as the test/harness facade is flipped over.
// ---------------------------------------------------------------------------

std::uint32_t HarnessRpcService::loadRom(std::string path,
        std::vector<std::uint8_t> sram,
        std::string lsdjSyncMode,
        std::uint32_t linkGroup) {
    const std::vector<std::uint8_t>* sramPtr = sram.empty() ? nullptr : &sram;
    return h_->loadRom(path, sramPtr, lsdjSyncMode,
                       static_cast<std::uint8_t>(linkGroup));
}

void HarnessRpcService::runMs(double ms) { h_->runMs(ms); }

void HarnessRpcService::press(std::uint32_t systemId, std::int32_t button, bool down) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    sys->pressButton(static_cast<std::uint8_t>(button), down);
}

std::vector<HarnessMidiEvent> HarnessRpcService::drainMidi(std::uint32_t systemId) {
    std::vector<HarnessMidiEvent> out;
    for (const auto& rec : h_->takeMidi(systemId))
        out.push_back(HarnessMidiEvent{ rec.sample, rec.bytes });
    return out;
}

rfl::Bytestring HarnessRpcService::readMemory(std::uint32_t systemId, std::uint32_t type) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    rp::MemoryAccessor acc =
        sys->getMemory(static_cast<rp::MemoryType>(type), rp::AccessType::Read);
    rfl::Bytestring out;
    if (acc.valid()) {
        const auto* p = reinterpret_cast<const std::byte*>(acc.data());
        out.assign(p, p + acc.size());
    }
    return out;
}

std::vector<rp::CpuRegister> HarnessRpcService::getRegisters(std::uint32_t systemId) {
    return h_->cpuSystem(systemId)->getCpuRegisters();
}

HarnessFrame HarnessRpcService::getFrame(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("unknown system id");
    FrameBufferTriple* fb = sys->framebuffer();
    if (!fb) throw std::runtime_error("system has no framebuffer");
    const std::uint32_t fbW = fb->width();
    const std::uint32_t fbH = fb->height();
    const std::size_t pixels = static_cast<std::size_t>(fbW) * fbH;
    std::vector<std::uint32_t> xrgb(pixels);
    const bool published =
        fb->readInto(xrgb.data(), static_cast<std::uint32_t>(pixels));
    HarnessFrame out;
    out.width = fbW;
    out.height = fbH;
    out.published = published;
    if (published) {
        const auto* p = reinterpret_cast<const std::byte*>(xrgb.data());
        out.data.assign(p, p + pixels * 4);
    }
    return out;
}

rfl::Bytestring HarnessRpcService::getAudio(double ms) {
    const std::vector<float> samples = h_->runMsCapture(ms);
    const auto* p = reinterpret_cast<const std::byte*>(samples.data());
    return rfl::Bytestring(p, p + samples.size() * sizeof(float));
}

rp::BreakInfo HarnessRpcService::runUntilBreak(std::uint32_t systemId, std::uint64_t maxCycles) {
    return h_->debugTarget(systemId)->runUntilBreak(maxCycles);
}

rfl::Bytestring HarnessRpcService::savFromJson(std::string json) {
    auto sav = rp::lsdj::savFromJsonFixture(json);
    if (!sav) throw std::runtime_error("savFromJson: " + sav.error().what());
    const auto bytes = rp::lsdj::codec::encodeSav(sav.value());
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

bool HarnessRpcService::loadSram(std::uint32_t systemId, std::vector<std::uint8_t> sram) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("loadSram: unknown system id");
    return sys->loadSramBytes(std::move(sram));
}

void HarnessRpcService::reset(std::uint32_t systemId) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("reset: unknown system id");
    sys->onReset();
}

rfl::Bytestring HarnessRpcService::readFile(std::string path) {
    const auto bytes = slurpBytes(path);
    const auto* p = reinterpret_cast<const std::byte*>(bytes.data());
    return rfl::Bytestring(p, p + bytes.size());
}

void HarnessRpcService::writeFile(std::string path, std::vector<std::uint8_t> bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size())).good())
        throw std::runtime_error("writeFile: write failed: " + path);
}

std::int32_t HarnessRpcService::savRoundtripDiff(std::vector<std::uint8_t> sav) {
    constexpr std::size_t kSong = 0x8000;
    if (sav.size() < kSong) throw std::runtime_error("savRoundtripDiff: need >= 0x8000 bytes");
    std::span<const std::uint8_t> orig(sav.data(), kSong);
    auto res = rp::lsdj::codec::decodeSong(orig);
    if (!res) throw std::runtime_error("savRoundtripDiff: decode: " + res.error().what());
    const auto out = rp::lsdj::codec::encodeSong(res.value(), orig);
    const auto isVolatile = [](std::size_t off) {
        return off == 0x3FB2 || off == 0x3FB3 || (off >= 0x3FB6 && off <= 0x3FB9) || off == 0x3FC1;
    };
    for (std::size_t i = 0; i < kSong; ++i)
        if (orig[i] != out[i] && !isVolatile(i)) return static_cast<std::int32_t>(i);
    return -1;
}

void HarnessRpcService::sendMidi(std::uint32_t systemId, std::vector<std::uint8_t> bytes) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("sendMidi: unknown system id");
    if (bytes.empty() || bytes.size() > ::MidiEvent::kDataSize)
        throw std::runtime_error("sendMidi: expected 1.." +
            std::to_string(::MidiEvent::kDataSize) + " bytes");
    ::MidiEvent ev{};
    ev.frame = 0;
    ev.size  = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) ev.data[i] = bytes[i];
    sys->onMidi(&ev, 1);
}

void HarnessRpcService::setTransport(bool running) { h_->transportPlaying = running; }

void HarnessRpcService::setBpm(double bpm) {
    if (bpm <= 0.0) throw std::runtime_error("setBpm: bpm must be > 0");
    h_->bpm = bpm;
}

std::vector<HarnessSerialByte> HarnessRpcService::drainSerial(std::uint32_t systemId) {
    std::vector<HarnessSerialByte> out;
    for (const auto& rec : h_->takeSerial(systemId))
        out.push_back(HarnessSerialByte{ rec.sample, rec.byte });
    return out;
}

void HarnessRpcService::setRegister(std::uint32_t systemId, std::string name, std::int64_t value) {
    const bool ok = h_->cpuSystem(systemId)->setCpuRegister(name, static_cast<std::uint32_t>(value));
    if (!ok) throw std::runtime_error("setRegister: unknown or read-only register '" + name + "'");
}

std::int32_t HarnessRpcService::readCpu(std::uint32_t systemId, std::uint32_t addr) {
    const std::optional<std::uint8_t> b = h_->cpuSystem(systemId)->readCpuByte(addr);
    if (!b) throw std::runtime_error(
        "side-effect-free CPU peek is not supported for this system (use readMemory)");
    return *b;
}

std::uint64_t HarnessRpcService::step(std::uint32_t systemId) {
    return h_->cpuSystem(systemId)->stepInstruction();
}

bool HarnessRpcService::runUntilPc(std::uint32_t systemId, std::uint32_t pc, std::uint64_t maxCycles) {
    if (maxCycles == 0) throw std::runtime_error("runUntilPc: maxCycles must be > 0");
    return h_->cpuSystem(systemId)->runUntilPc(pc, maxCycles);
}

bool HarnessRpcService::screenshot(std::uint32_t systemId, std::string path) {
    SystemBase* sys = h_->system(systemId);
    if (!sys) throw std::runtime_error("screenshot: unknown system id");
    return rpcli::writeFramebufferPng(*sys, path);
}

HarnessPerSystemAudio HarnessRpcService::runMsPerSystem(double ms) {
    HarnessPerSystemAudio out;
    for (const auto& buf : h_->runMsPerSystem(ms)) {
        const auto* p = reinterpret_cast<const std::byte*>(buf.data());
        out.systems.emplace_back(p, p + buf.size() * sizeof(float));
    }
    return out;
}

void HarnessRpcService::writeWav(std::string path, std::vector<std::uint8_t> samples,
                                 std::uint32_t sampleRate) {
    if (sampleRate == 0) sampleRate = 44100;
    const float* data = reinterpret_cast<const float*>(samples.data());
    const std::size_t frames = (samples.size() / sizeof(float)) / 2; // interleaved stereo
    std::vector<float> l(frames), r(frames);
    for (std::size_t i = 0; i < frames; ++i) { l[i] = data[2 * i]; r[i] = data[2 * i + 1]; }
    float* outs[2] = { l.data(), r.data() };
    WavWriter w(path, sampleRate, 2);
    w.writeBlockFloatPlanar(outs, static_cast<std::uint32_t>(frames));
}

void HarnessRpcService::saveRplg(std::string path) { h_->saveRplg(path); }
std::uint32_t HarnessRpcService::loadRplg(std::string path) { return h_->loadRplg(path); }

void HarnessRpcService::patchKit(std::uint32_t systemId, std::uint32_t slot, std::string name,
                                 std::vector<HarnessKitSample> samples) {
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(samples.size());
    for (auto& s : samples) pairs.emplace_back(std::move(s.path), std::move(s.name));
    h_->patchKit(systemId, static_cast<std::uint8_t>(slot), name, pairs);
}

void HarnessRpcService::beginProfile(std::uint32_t systemId) {
    h_->debugTarget(systemId)->beginProfile();
}
std::vector<rp::ProfiledFunction> HarnessRpcService::readProfile(std::uint32_t systemId) {
    return h_->debugTarget(systemId)->readProfile();
}
bool HarnessRpcService::loadLabels(std::uint32_t systemId, std::string path) {
    return h_->debugTarget(systemId)->loadLabels(path);
}
std::vector<rp::DisasmLine> HarnessRpcService::disassemble(std::uint32_t systemId,
                                                          std::uint32_t addr, std::uint32_t count) {
    return h_->debugTarget(systemId)->disassemble(addr, count);
}
void HarnessRpcService::setTrace(std::uint32_t systemId, bool on) {
    h_->debugTarget(systemId)->setTraceEnabled(on);
}
std::vector<rp::TraceLine> HarnessRpcService::readTrace(std::uint32_t systemId, std::uint32_t count) {
    return h_->debugTarget(systemId)->readTrace(count);
}
std::vector<rp::CallFrame> HarnessRpcService::getCallStack(std::uint32_t systemId) {
    return h_->debugTarget(systemId)->getCallStack();
}
void HarnessRpcService::setBreakpoints(std::uint32_t systemId, std::vector<rp::BreakpointSpec> bps) {
    h_->debugTarget(systemId)->setBreakpoints(bps);
}
rp::BreakInfo HarnessRpcService::stepInto(std::uint32_t systemId) { return h_->debugTarget(systemId)->step(); }
rp::BreakInfo HarnessRpcService::stepOver(std::uint32_t systemId) { return h_->debugTarget(systemId)->stepOver(); }
rp::BreakInfo HarnessRpcService::stepOut(std::uint32_t systemId)  { return h_->debugTarget(systemId)->stepOut(); }

// ---------------------------------------------------------------------------
// JS trampolines. Every body is wrapped so a C++ throw never crosses into
// QuickJS (which does not catch C++ exceptions).
// ---------------------------------------------------------------------------

namespace {

// __rpcSend(bytes) -> ArrayBuffer | null: the single sync entry the generated
// HarnessService client dispatches through (mirrors PluginJsBridge::js_rpcSend).
// Accepts a Uint8Array view or a raw ArrayBuffer; returns the msgpack reply.
JSValue jsHarnessRpcSend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* h = g_activeImpl;
    if (!h || !h->rpcServer_)
        return JS_ThrowInternalError(ctx, "__rpcSend: harness unavailable");
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "__rpcSend: expected (bytes)");
    std::size_t byteOffset = 0, byteLength = 0, arrayLen = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &byteOffset, &byteLength, nullptr);
    std::uint8_t* data = nullptr;
    if (!JS_IsException(ab)) {
        data = JS_GetArrayBuffer(ctx, &arrayLen, ab);
    } else {
        JS_FreeValue(ctx, ab);
        data = JS_GetArrayBuffer(ctx, &arrayLen, argv[0]);
        byteOffset = 0; byteLength = arrayLen;
        ab = JS_DupValue(ctx, argv[0]);
    }
    if (!data) { JS_FreeValue(ctx, ab); return JS_ThrowTypeError(ctx, "__rpcSend: not bytes"); }
    std::span<const char> bytes(reinterpret_cast<const char*>(data + byteOffset), byteLength);
    auto reply = h->rpcServer_->processMessage(bytes);
    JS_FreeValue(ctx, ab);
    if (!reply) return JS_NULL;
    return JS_NewArrayBufferCopy(ctx,
        reinterpret_cast<const std::uint8_t*>(reply->data()), reply->size());
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

    // Stand up the rpcpp server stack (restructure-04). The generated
    // HarnessService client dispatches through __rpcSend -> processMessage.
    impl_->rpcService_   = std::make_unique<HarnessRpcService>(impl_.get());
    impl_->rpcTransport_ = std::make_unique<HarnessRpcTransport>();
    impl_->rpcServer_    = std::make_unique<HarnessRpcServer>(*impl_->rpcService_,
                                                              *impl_->rpcTransport_);
    registerHarnessRpcMethods(*impl_->rpcServer_);

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
    bind("beginCase",    jsBeginCase,    0);
    bind("report",       jsReport,       3);
    bind("done",         jsDone,         0);
    bind("log",          jsLog,          2);
    // The emulator surface: the generated HarnessService client dispatches
    // through this single sync RPC entry (the per-method trampolines are gone).
    bind("__rpcSend",    jsHarnessRpcSend, 1);

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
