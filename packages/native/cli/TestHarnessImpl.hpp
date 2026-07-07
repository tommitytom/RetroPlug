#pragma once

// TestHarness::Impl — the emulator/Project state the rpcpp `emu` surface drives.
//
// Shared between TestHarness.cpp (the txiki/QuickJS host) and
// HarnessRpcService.cpp (the rpcpp method bodies, which dereference Impl), so
// the service can be compiled and linked WITHOUT the JS runtime. The qrt/ctx
// members are therefore opaque forward-declared pointers here; only
// TestHarness.cpp (which includes tjs.h/private.h) ever dereferences them.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HostPath.hpp"
#include "Screenshot.hpp"
#include "Wav.hpp"
#include "TestHarness.hpp"
#include "project/Project.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/BlockRunner.hpp"
#include "system/OfflineRender.hpp"
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
#include "lsdj/ProjectKitRecompile.hpp"

#include "HarnessRpcService.hpp"
#include "TypedRpcServer.h"
#include "codecs/QuickJSCodec.h"
#include "transports/QuickJSTransport.h"

// In-process QuickJS object codec (matches the plugin bridge): the generated
// HarnessService TS client passes a request object to __rpcSend and gets a
// response object back — no serialization. Binary fields (rfl::Bytestring) ride
// JS Uint8Arrays. Both need a live JSContext, so the server is built after the
// host's runtime exists (see TestHarness ctor).
using HarnessRpcTransport = rpcpp::QuickJSTransport;
using HarnessRpcServer    = rpcpp::TypedRpcServer<HarnessRpcService, rpcpp::QuickJSCodec>;

namespace rpcli {

inline std::string slurpText(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::vector<std::uint8_t> slurpBytes(const std::string& path) {
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
inline std::string oneLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (c == '\n') ? ' ' : c;
    return out;
}

// Parse an LSDj sync-mode name for emu.loadRom's role option.
inline LsdjSyncMode parseLsdjSyncMode(const std::string& s) {
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

} // namespace rpcli

// ---------------------------------------------------------------------------
// Impl: owns the runtime + the Project the `emu` shims drive.
// ---------------------------------------------------------------------------

struct TestHarness::Impl {
    std::unique_ptr<Project> project;
    double      sampleRate = 44100.0;
    std::uint32_t blockSize = 1024;
    std::vector<float> scratchL, scratchR;

    // Simulated host transport, fed into AudioBlockInfo each block. LsdjSyncRole
    // and friends read these to generate MIDI-clock byte streams the same way as
    // in the plugin.
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

    // rpcpp server stack: the generated TS client dispatches here via
    // Symbol.for("retroplug").__rpcSend. Declaration order matters —
    // the server references the service + transport, so it must be destroyed
    // first (members destruct in reverse order).
    std::unique_ptr<HarnessRpcService>    rpcService_;
    std::unique_ptr<HarnessRpcTransport>  rpcTransport_;
    std::unique_ptr<HarnessRpcServer>     rpcServer_;

    // TAP state.
    int  testIndex   = 0;
    int  failures    = 0;
    bool donePrinted = false;

    // End-user CLI bundle plumbing: argv is exposed to JS via getArgv(); the
    // bundle reports its process exit code via exit(code).
    std::vector<std::string> cliArgs;
    int                      cliExitCode = 0;

    Impl() : project(std::make_unique<Project>()),
             scratchL(blockSize), scratchR(blockSize) {}

    // -- emu surface (called from the rpcpp service in HarnessRpcService.cpp) --

    std::uint32_t loadRom(const std::string& path,
                          const std::vector<std::uint8_t>* sram = nullptr,
                          const std::string& lsdjSyncMode = "",
                          std::uint8_t linkGroup = 0) {
        auto bytes = rpcli::slurpBytes(path);
        const RomFormat fmt = detectRomFormat(bytes);

        std::unique_ptr<SystemBase> sys;
        switch (fmt) {
            case RomFormat::Gb: {
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
                // skips the sniffer fallback.
                if (!lsdjSyncMode.empty()) {
                    LsdjSyncConfig lsdj;
                    lsdj.mode = rpcli::parseLsdjSyncMode(lsdjSyncMode);
                    cfg.roles.emplace_back(lsdj);
                }
                // Same nonzero linkGroup puts instances in a shared LinkGroup
                // for lockstep serial-bit ferrying (LSDj link-cable sync).
                cfg.linkGroupId = linkGroup;
                sys = std::make_unique<SameBoySystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::Nes: {
                MesenNesConfig cfg;
                cfg.romPath = path;
                sys = std::make_unique<MesenNesSystem>(
                    project->nextSystemId(), cfg, std::move(bytes));
                break;
            }
            case RomFormat::Gba: {
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
    // per-system logs (absolute sample = sampleClock + event frame).
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
    // (out[i] = L,R,L,R… for system i). SameBoy-only — the manual
    // prepareForBlock → stepIfBelowTarget → finishBlock orchestration interleaves
    // linked systems the same way LinkGroup does. Used to prove LSDj link-cable
    // sync (the follower produces audio only when actually synced to the leader).
    std::vector<std::vector<float>> runMsPerSystem(double ms) {
        const std::size_t n = sysList.size();
        std::vector<std::vector<float>> out(n);
        if (ms <= 0.0 || n == 0) return out;
        // Per-system isolation is SameBoy-only (the per-system reaper fixtures
        // load only SameBoys). runBlock handles other kinds, but keep the guard
        // so this increment adds no new surface.
        for (SystemBase* s : sysList)
            if (!dynamic_cast<SameBoySystem*>(s))
                throw std::runtime_error("runMsPerSystem is SameBoy-only");

        // One persistent L/R block buffer per slot; runBlock routes each system
        // (linked or not) into its own buffer via PerSystemRouter.
        std::vector<std::vector<float>> bl(n, std::vector<float>(blockSize));
        std::vector<std::vector<float>> br(n, std::vector<float>(blockSize));
        std::vector<float*> ls(n), rs(n);
        for (std::size_t i = 0; i < n; ++i) { ls[i] = bl[i].data(); rs[i] = br[i].data(); }
        PerSystemRouter router(ls.data(), rs.data());

        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        for (std::uint64_t s = 0; s < total; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s));
            AudioBlockInfo info{ frames, sampleRate, bpm, ppq, transportPlaying };
            for (std::size_t i = 0; i < n; ++i) {
                std::fill_n(ls[i], frames, 0.0f);
                std::fill_n(rs[i], frames, 0.0f);
            }
            runBlock(info, *project, router);
            for (std::size_t i = 0; i < n; ++i)
                for (std::uint32_t f = 0; f < frames; ++f) {
                    out[i].push_back(ls[i][f]);
                    out[i].push_back(rs[i][f]);
                }
            drainCaptures();
            sampleClock += frames;
            if (transportPlaying)
                ppq += (bpm / 60.0) * (static_cast<double>(frames) / sampleRate);
        }
        return out;
    }

    // Route a MIDI message to the loaded systems per `routing` (the channel
    // nibble decides the target system), unlike a single-system onMidi.
    void dispatchMidi(const std::vector<std::uint8_t>& bytes, std::uint32_t routing) {
        if (bytes.empty() || bytes.size() > ::MidiEvent::kDataSize)
            throw std::runtime_error("dispatchMidi: expected 1.." +
                std::to_string(::MidiEvent::kDataSize) + " bytes");
        ::MidiEvent ev{};
        ev.frame = 0;
        ev.size  = static_cast<std::uint32_t>(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) ev.data[i] = bytes[i];
        project->dispatchMidi(&ev, 1, static_cast<MidiRouting>(routing));
    }

    // -- streaming render (single-shot + session) ---------------------------
    //
    // A render writes the mixed stereo output (and, in per-system mode, each
    // system's stereo to its own WAV plus their sum to the mix) straight to
    // disk block by block — the whole song never sits in one buffer (the
    // getAudio + writeWav path would, and Array.from-boxing it in JS is fatal).
    // The session form (renderBegin/renderChunk/renderEnd) lets the CLI apply
    // scripted input between chunks, so events interleave with a contiguous WAV.

    std::optional<WavWriter>     renderMix_;   // open during a render session
    std::vector<WavWriter>       renderPer_;   // per-system writers (per-system mode)
    std::vector<SameBoySystem*>  renderSb_;    // per-system targets (per-system mode)

    // Render `total` samples into the currently-open render writers. mix-only
    // uses stepBlock (the Stereo Project::onProcess path); per-system routes each
    // system to its own buffer via runBlock + PerSystemRouter, writes each
    // system's WAV, and sums into the mix (mix = sum of per-system).
    void renderInto(std::uint64_t total) {
        if (renderPer_.empty()) {
            for (std::uint64_t s = 0; s < total; s += blockSize) {
                const std::uint32_t frames = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(blockSize, total - s));
                stepBlock(frames, nullptr); // fills scratchL/scratchR
                if (renderMix_) {
                    float* outs[2] = { scratchL.data(), scratchR.data() };
                    renderMix_->writeBlockFloatPlanar(outs, frames);
                }
            }
            return;
        }
        // One persistent L/R block buffer per slot (renderSb_[i] == systems()[i]).
        const std::size_t n = renderSb_.size();
        std::vector<std::vector<float>> bl(n, std::vector<float>(blockSize));
        std::vector<std::vector<float>> br(n, std::vector<float>(blockSize));
        std::vector<float*> ls(n), rs(n);
        for (std::size_t i = 0; i < n; ++i) { ls[i] = bl[i].data(); rs[i] = br[i].data(); }
        PerSystemRouter router(ls.data(), rs.data());
        std::vector<float> ml(blockSize), mr(blockSize);
        for (std::uint64_t s = 0; s < total; s += blockSize) {
            const std::uint32_t frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(blockSize, total - s));
            AudioBlockInfo info{ frames, sampleRate, bpm, ppq, transportPlaying };
            for (std::size_t i = 0; i < n; ++i) {
                std::fill_n(ls[i], frames, 0.0f);
                std::fill_n(rs[i], frames, 0.0f);
            }
            runBlock(info, *project, router);
            if (renderMix_) { std::fill_n(ml.data(), frames, 0.0f);
                              std::fill_n(mr.data(), frames, 0.0f); }
            for (std::size_t i = 0; i < n; ++i) {
                float* o[2] = { ls[i], rs[i] };
                renderPer_[i].writeBlockFloatPlanar(o, frames);
                if (renderMix_)
                    for (std::uint32_t f = 0; f < frames; ++f) {
                        ml[f] += ls[i][f];
                        mr[f] += rs[i][f];
                    }
            }
            if (renderMix_) {
                float* mo[2] = { ml.data(), mr.data() };
                renderMix_->writeBlockFloatPlanar(mo, frames);
            }
            drainCaptures();
            sampleClock += frames;
            if (transportPlaying)
                ppq += (bpm / 60.0) * (static_cast<double>(frames) / sampleRate);
        }
    }

    // Open the render writers. Empty mixPath = no mix; non-empty perSystemPaths
    // = per-system mode (one path per loaded system, in load order; SameBoy-only).
    void renderBegin(const std::string& mixPath,
                     const std::vector<std::string>& perSystemPaths,
                     std::uint32_t wavRate) {
        if (wavRate == 0) wavRate = static_cast<std::uint32_t>(sampleRate);
        renderEnd();
        if (!perSystemPaths.empty()) {
            if (perSystemPaths.size() != sysList.size())
                throw std::runtime_error("renderBegin: expected one path per system");
            for (SystemBase* s : sysList) {
                auto* p = dynamic_cast<SameBoySystem*>(s);
                if (!p) throw std::runtime_error("renderBegin per-system is SameBoy-only");
                renderSb_.push_back(p);
            }
            for (const auto& p : perSystemPaths) renderPer_.emplace_back(p, wavRate, 2);
        }
        if (!mixPath.empty()) renderMix_.emplace(mixPath, wavRate, 2);
    }

    void renderChunk(double ms) {
        if (ms <= 0.0) return;
        renderInto(static_cast<std::uint64_t>(ms * sampleRate / 1000.0));
    }

    void renderEnd() {
        renderMix_.reset();
        renderPer_.clear();
        renderSb_.clear();
    }

    // Single-shot convenience wrappers (no scripted input).
    void renderWav(const std::string& path, double ms, std::uint32_t wavRate) {
        renderBegin(path, {}, wavRate);
        renderChunk(ms);
        renderEnd();
    }
    void renderWavPerSystem(const std::string& mixPath,
                            const std::vector<std::string>& perSystemPaths,
                            double ms, std::uint32_t wavRate) {
        renderBegin(mixPath, perSystemPaths, wavRate);
        renderChunk(ms);
        renderEnd();
    }

    // Parallel per-system render: farms each render unit (a system, or a SameBoy
    // link group) onto its own worker thread (system/OfflineRender.cpp), then
    // single-threaded sums the mix. Byte-identical to renderWavPerSystem's audio
    // but faster on multi-unit projects, and not SameBoy-only (covers Mesen too).
    // In-RAM per-system buffers (same memory profile as runMsPerSystem); the
    // streaming renderWavPerSystem stays the path for hour-scale renders. Pure
    // audio — any role MIDI/serial output this block is discarded, not captured.
    void renderWavPerSystemParallel(const std::string& mixPath,
                                    const std::vector<std::string>& perSystemPaths,
                                    double ms, std::uint32_t wavRate) {
        if (wavRate == 0) wavRate = static_cast<std::uint32_t>(sampleRate);
        if (ms <= 0.0) return;
        const std::size_t n = sysList.size();
        if (perSystemPaths.size() != n)
            throw std::runtime_error("renderWavPerSystemParallel: expected one path per system");

        const std::uint64_t total =
            static_cast<std::uint64_t>(ms * sampleRate / 1000.0);
        if (total == 0) return;
        // WavWriter sizes (and writeBlockFloatPlanar's frame count) are 32-bit;
        // a single WAV caps at 2^32 frames. The in-RAM buffers would OOM long
        // before this, but fail loudly rather than truncate the RIFF size.
        if (total > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("renderWavPerSystemParallel: render too long for one WAV");

        OfflineRenderParams params{};
        params.totalSamples     = total;
        params.blockSize        = blockSize;
        params.sampleRate       = sampleRate;
        params.bpm              = bpm;
        params.transportPlaying = transportPlaying;
        params.startPpq         = ppq;

        // per[slot] is interleaved L,R; slot index == sysList / systems() order.
        std::vector<std::vector<float>> per = renderUnitsParallel(*project, params);

        std::vector<float> mixL, mixR;
        if (!mixPath.empty()) { mixL.assign(total, 0.0f); mixR.assign(total, 0.0f); }
        std::vector<float> l(total), r(total);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& buf = per[i];
            for (std::uint64_t f = 0; f < total; ++f) {
                const float lv = buf[f * 2 + 0];
                const float rv = buf[f * 2 + 1];
                l[f] = lv; r[f] = rv;
                if (!mixPath.empty()) { mixL[f] += lv; mixR[f] += rv; }
            }
            WavWriter w(perSystemPaths[i], wavRate, 2);
            float* o[2] = { l.data(), r.data() };
            w.writeBlockFloatPlanar(o, static_cast<std::uint32_t>(total));
        }
        if (!mixPath.empty()) {
            WavWriter w(mixPath, wavRate, 2);
            float* mo[2] = { mixL.data(), mixR.data() };
            w.writeBlockFloatPlanar(mo, static_cast<std::uint32_t>(total));
        }

        // Pure-audio render: discard any role MIDI/serial accumulated this pass
        // (not drained per block), then advance the simulated transport.
        for (SystemBase* s : sysList) {
            s->midiOut().clear();
            if (auto* sb = dynamic_cast<SameBoySystem*>(s)) sb->serialOutLog_.clear();
        }
        sampleClock += total;
        if (transportPlaying)
            ppq += (bpm / 60.0) * (static_cast<double>(total) / sampleRate);
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

        // Mirror compileAndPatchKit + the DSP PatchKit handler: persist the kit
        // (sample metadata + compiled bytes) onto the system config so saves
        // round-trip — saveRplg keeps the bytes, saveProjectFile keeps just the
        // samples and recompiles on load.
        for (auto& rc : sb->config_.roles) {
            auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
            if (!kitCfg) continue;
            rp::lsdj::LsdjKitConfig* dst = nullptr;
            for (auto& k : kitCfg->kits) {
                if (k.slot == slot) { dst = &k; break; }
            }
            if (!dst) {
                rp::lsdj::LsdjKitConfig fresh;
                fresh.slot = slot;
                kitCfg->kits.push_back(std::move(fresh));
                dst = &kitCfg->kits.back();
            }
            dst->name = name;
            dst->samples.clear();
            for (const auto& s : samples) {
                rp::lsdj::LsdjSampleConfig sc;
                sc.path = s.first;
                sc.name = s.second;
                dst->samples.push_back(std::move(sc));
            }
            dst->compiledBytes = compiled.bytes;
            dst->compiledHash  = compiled.hash;
            break;
        }

        role->queuePatch(slot, std::move(compiled.bytes));
    }

    // .rplg save/load orchestration now lives in shared TS (@retroplug/retroplug
    // projectSerialization.ts) over these two byte-mover primitives + the generic
    // zip/unzip in HarnessRpcService. The primitives keep the irreducibly-native
    // work: walking the live instances for the config + blobs (snapshot) and
    // building/activating the emulator from a config (apply). TS owns the zip
    // structure, the entry-key contract, and the schema check.

    // A blob entry (name = the zip key, e.g. "systems/0/rom") + its raw bytes.
    struct RplgBlob { std::string name; std::vector<std::uint8_t> bytes; };
    struct RplgSnapshot { std::string config; std::vector<RplgBlob> blobs; };

    // Live project -> { thin config JSON (blobs emptied, schema stamped), each
    // blob keyed by the project_binaries entry contract }. The thin-snapshotConfig.
    RplgSnapshot snapshotProjectConfig() {
        ProjectConfig cfg = project->snapshotConfig();
        cfg.schemaVersion = std::to_string(rp::schema::kProject); // stamp current
        // A sink that collects the stripped blobs (name/key + bytes) instead of
        // writing them into a zip — the templated project_binaries walk drives it.
        struct Collector {
            std::vector<RplgBlob> blobs;
            bool add(std::string_view name, std::span<const std::uint8_t> bytes) {
                blobs.push_back({std::string(name), {bytes.begin(), bytes.end()}});
                return true;
            }
        } coll;
        project_binaries::strip(coll, cfg); // empties cfg's blobs into coll
        return { projectConfigToJson(cfg), std::move(coll.blobs) };
    }

    // { thin config JSON, blob entries } -> rebuild the project. Restores the
    // blobs into the config, recompiles any kits missing compiled bytes, then the
    // shared Project::loadFromConfig -> onActivate -> rebuildLinkGroups (the same
    // path a DAW reload takes). Returns the first restored system id.
    std::uint32_t applyProjectConfig(const std::string& config,
                                     const std::vector<RplgBlob>& blobs) {
        auto parsed = projectConfigFromJson(config);
        if (!parsed)
            throw std::runtime_error("applyProjectConfig: failed to parse config JSON");
        if (!blobs.empty()) {
            // A source that reads blobs from the entry list — mirror of Collector.
            struct MapSource {
                const std::vector<RplgBlob>* entries;
                bool has(std::string_view name) const {
                    for (const auto& e : *entries) if (e.name == name) return true;
                    return false;
                }
                std::vector<std::uint8_t> read(std::string_view name) const {
                    for (const auto& e : *entries) if (e.name == name) return e.bytes;
                    return {};
                }
            } src{&blobs};
            project_binaries::restore(src, *parsed);
        }
        if (rp::lsdj::projectHasKitsNeedingRecompile(*parsed)) {
            if (!kitCompiler_) kitCompiler_ = std::make_unique<rp::lsdj::KitCompiler>();
            rp::lsdj::recompileMissingKits(*parsed, *kitCompiler_);
        }
        project = std::make_unique<Project>();
        sysList.clear();
        midiOutLog.clear();
        serialOutLog.clear();
        const SystemId first = project->loadFromConfig(*parsed);
        if (!parsed->systems.empty() && first == 0)
            throw std::runtime_error("applyProjectConfig: loadFromConfig failed");
        project->onActivate(sampleRate); // restores each system's savestate
        for (const auto& s : project->systems())
            if (s) sysList.push_back(s.get());
        project->rebuildLinkGroups();
        return static_cast<std::uint32_t>(first);
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
                            rpcli::oneLine(msg).c_str());
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
