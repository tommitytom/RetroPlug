#include "host/engine/Engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include "system/SystemBase.hpp"
#include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
#include "system/BlockRunner.hpp"   // runBlock + MultiOutRouter
#include "system/sameboy/SameBoySystem.hpp"  // live-apply cast target (model/highpass/gain/…)
#include "system/mesen/MesenNesSystem.hpp"    // live-apply cast target (NES region / sprite limit)

#include "transport/FrameBufferTriple.hpp"
#include "native/core/img/png/lodepng.h"

namespace {

// Read-only empties the kernel's per-block button/key inputs default to. The host
// doesn't yet feed the kernel per-block buttons/keys.
const std::vector<DspRuntime::ButtonIn> kNoButtons;
const std::vector<DspRuntime::KeyIn>    kNoKeys;

} // namespace

Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
    // Pre-reserve so adoptSystem/swapSystem never reallocate systems_ when the audio thread owns
    // the Project (production reserves the same way).
    project_.reserve(16);
}

void Engine::adoptSystem(std::unique_ptr<SystemBase> sys) {
    project_.adoptSystem(sys.release());  // Project takes ownership of the raw pointer
    project_.rebuildLinkGroups();
}

std::unique_ptr<SystemBase> Engine::removeSystem(SystemId id) {
    return std::unique_ptr<SystemBase>(project_.removeSystemAndRelease(id));  // does its own rebuild
}

std::unique_ptr<SystemBase> Engine::replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) {
    // swapSystem returns the displaced core, or the incoming one unchanged if id wasn't found
    // (then it was never adopted) — either way it's the leftover for the caller to dispose.
    SystemBase* old = project_.swapSystem(id, sys.release());
    project_.rebuildLinkGroups();
    return std::unique_ptr<SystemBase>(old);
}

std::size_t Engine::systemCount() const {
    return project_.systems().size();
}

SystemBase* Engine::findSystem(SystemId id) {
    return project_.findSystem(id);
}

bool Engine::loadKernel(const std::vector<std::uint8_t>& bytecode) {
    dspActive_ = dsp_.loadKernel(bytecode);
    return dspActive_;
}

bool Engine::setSystems(const std::vector<std::uint8_t>& json) {
    return dsp_.setSystems(json);
}

void Engine::stageMidi(std::vector<std::uint8_t> bytes) {
    pendingMidi_.push_back({ 0, std::move(bytes) });
}

void Engine::setBpm(double bpm) { bpm_ = bpm; }
void Engine::setTransport(bool playing) { transport_ = playing; }
void Engine::setAudioRouting(AudioRouting mode) { audioRouting_ = mode; }

// Run the kernel (if active) + fan its system-addressed sinks to the cores BEFORE onProcess
// (delivered this block); `dInfo`/`AudioBlockInfo` are both built at the block-start `ppq_`, and
// `ppq_` advances only after — so the kernel's walkTicks and the cores see the same block-start ppq.
void Engine::runBlockWithRouter(std::uint32_t frames, const AudioRouter& router) {
    if (dspActive_) {
#ifdef RETROPLUG_PROFILE
        dsp_.spanBegin(DSP_SPAN_KERNEL);  // the whole DSP-kernel stage (marshal + JS + sink fan-out)
#endif
        const DspRuntime::BlockInfo dInfo{ frames, sampleRate_, bpm_, ppq_, transport_ };
        dsp_.processBlock(pendingMidi_, kNoButtons, kNoKeys, pendingSerialOut_, dInfo);
        pendingMidi_.clear();       // staged host MIDI consumed this block
        pendingSerialOut_.clear();  // last block's serial-out consumed by the kernel this block
        // serial-in sink → the addressed system's serial FIFO.
        for (const auto& sv : dsp_.serialIn_)
            if (SystemBase* t = project_.findSystem(sv.system)) t->pushSerialIn(sv.byte);
        // core-MIDI sink → the addressed core's onMidi (e.g. the NES N8 FIFO). One ::MidiEvent per entry;
        // an oversized message (> the inline data[4]) is skipped, matching the DAW-drain guard.
        for (const auto& cm : dsp_.coreMidi_) {
            if (cm.data.empty() || cm.data.size() > ::MidiEvent::kDataSize) continue;
            if (SystemBase* t = project_.findSystem(cm.system)) {
                ::MidiEvent ev{};
                ev.frame = cm.frame;
                ev.size  = static_cast<std::uint32_t>(cm.data.size());
                for (std::size_t j = 0; j < cm.data.size(); ++j) ev.data[j] = cm.data[j];
                t->onMidi(&ev, 1);
            }
        }
        // role-generated button presses → the addressed core.
        for (const auto& bo : dsp_.buttonOut_)
            if (SystemBase* t = project_.findSystem(bo.system))
                t->pressButton(static_cast<std::uint8_t>(bo.button), bo.down);
#ifdef RETROPLUG_PROFILE
        dsp_.spanEnd();  // dsp-kernel
#endif
    }
    AudioBlockInfo info{ frames, sampleRate_, bpm_, ppq_, transport_ };
#ifdef RETROPLUG_PROFILE
    dsp_.spanBegin(DSP_SPAN_APU);  // the SameBoy core/APU render (dominates audio-thread wall-time)
#endif
    runBlock(info, project_, router);
#ifdef RETROPLUG_PROFILE
    dsp_.spanEnd();  // apu-render
#endif
    // Gather the raw serial-out bytes the SameBoy cores emitted THIS block (LSDj MI.OUT capture, armed
    // via ConfigField::SerialOutCapture) into pendingSerialOut_, for the kernel to decode next block.
    // Cheap no-op when nothing captured (an unarmed system's serialOutLog_ is empty). Only standalone
    // systems capture (the writeAudioSample gate is linkPeers_.empty()); a linked system's log stays empty.
    if (dspActive_) {
        for (const auto& sys : project_.systems()) {
            auto* sb = dynamic_cast<SameBoySystem*>(sys.get());
            if (!sb || sb->serialOutLog_.empty()) continue;
            for (const auto& entry : sb->serialOutLog_)
                pendingSerialOut_.push_back({ sb->id(), entry.second });
        }
    }
    // Copy each core's freshly-published frame/state/SRAM into the owned registry the control plane
    // reads through — the one place every driver funnels the block, so it covers all of them.
    registry_.publishAll(project_, frames, sampleRate_);
    if (transport_)
        ppq_ += (bpm_ / 60.0) * (static_cast<double>(frames) / sampleRate_);
}

void Engine::processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs) {
    // Caller owns the buffers; systems SUM into their router-assigned bus, so zero every channel
    // first. MultiOutRouter places each system per audioRouting_ (Stereo = all → pair 0; with 2
    // channels every mode collapses to that one pair).
    for (std::size_t c = 0; c < numOutputs; ++c)
        std::fill_n(outputs[c], frames, 0.0f);

    // ChannelSplit: one system fans its per-channel streams across the output pairs (a Game Boy's 4
    // channels → outs 0/1,2/3,4/5,6/7). Gated to a single system — a lone system has no link peers, so
    // this IS the "unlinked" condition; any other project falls through to MultiOutRouter and the wide
    // layout stays inert (the load-bearing correctness rule: the split is Engine/router-driven, never an
    // always-on system trait). A non-GB single system reports 1 stream → collapses to Stereo (pair 0).
    if (audioRouting_ == AudioRouting::ChannelSplit && systemCount() == 1) {
        const auto n = static_cast<std::uint32_t>(project_.systems().front()->channelLayout().size());
        if (n >= 1 && 2u * n <= numOutputs) {
            ChannelSplitRouter router(outputs, numOutputs, n);
            runBlockWithRouter(frames, router);
            return;
        }
    }

    MultiOutRouter router(outputs, numOutputs, audioRouting_);
    runBlockWithRouter(frames, router);
}

// Stereo convenience (the CLI render + the test-host audio loop): one pair, everyone mixed.
void Engine::processBlock(std::uint32_t frames, float* outL, float* outR) {
    float* outs[2] = { outL, outR };
    processBlock(frames, outs, 2);
}

void Engine::processBlockPerSystem(std::uint32_t frames, float* const* ls, float* const* rs, std::size_t nSystems) {
    // Each slot writes into its own L/R pair; zero them first (systems SUM in, like the mixed path).
    for (std::size_t i = 0; i < nSystems; ++i) {
        std::fill_n(ls[i], frames, 0.0f);
        std::fill_n(rs[i], frames, 0.0f);
    }
    PerSystemRouter router(ls, rs);
    runBlockWithRouter(frames, router);
}

void Engine::processBlockPerChannel(std::uint32_t frames, float* const* ls, float* const* rs, std::size_t nStreams) {
    // Each stream writes into its own L/R pair; zero them first (systems SUM in, like the mixed path).
    // PerChannelRouter keys off streamIndex, not slot — correct only for a single-system project (the
    // RPC enforces that); runBlockWithRouter still drives the kernel/serial pipeline so MIDI reaches the core.
    for (std::size_t i = 0; i < nStreams; ++i) {
        std::fill_n(ls[i], frames, 0.0f);
        std::fill_n(rs[i], frames, 0.0f);
    }
    PerChannelRouter router(ls, rs, static_cast<std::uint32_t>(nStreams));
    runBlockWithRouter(frames, router);
}

std::optional<std::vector<std::uint8_t>> Engine::readState(SystemId id) {
    return registry_.readState(id);   // the owned published copy — never walks Project / the live core
}

std::optional<std::vector<std::uint8_t>> Engine::readSram(SystemId id) {
    return registry_.readSram(id);    // SRAM sliced from the published savestate, not a live read
}

bool Engine::screenshot(SystemId id, const std::string& path) {
    // Encode the owned registry frame (a published copy) — no findSystem walk, no live-core read, so
    // it's safe while the audio thread plays. false until the core has rendered its first frame.
    const SnapshotRegistry::Frame f = registry_.readFrame(id);
    // Distinguish the two failure modes so a false is diagnosable: a caller wanting liveness should query
    // getFrame(id).published (no file I/O); screenshot() is a file-writing action whose bool means "wrote".
    if (!f.published) {
        std::fprintf(stderr, "[Engine] screenshot: system %u has not published a frame yet\n",
                     static_cast<unsigned>(id));
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(f.width) * f.height;
    // The frame data is XRGB8888 (little-endian B,G,R,X) → transcode to RGB24 for lodepng.
    std::vector<unsigned char> rgb(pixels * 3);
    const std::uint8_t* src = f.data.data();
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = src[i * 4 + 2]; // R
        rgb[i * 3 + 1] = src[i * 4 + 1]; // G
        rgb[i * 3 + 2] = src[i * 4 + 0]; // B
    }
    const unsigned err = lodepng_encode24_file(path.c_str(), rgb.data(), f.width, f.height);
    if (err) {
        std::fprintf(stderr, "[Engine] screenshot: failed to write '%s': %s\n",
                     path.c_str(), lodepng_error_text(err));
        return false;
    }
    return true;
}

EngineFrame Engine::getFrame(SystemId id) {
    // The owned frame copy, by id — no findSystem, no live-core deref. width/height are 0 (published
    // false) for an unknown system; set with published false for a claimed core that hasn't rendered.
    SnapshotRegistry::Frame f = registry_.readFrame(id);
    EngineFrame out;
    out.width = f.width;
    out.height = f.height;
    out.published = f.published;
    out.data = std::move(f.data);   // raw XRGB8888, the Canvas's native format
    return out;
}

bool Engine::pressButton(SystemId id, std::uint8_t button, bool down) {
    SystemBase* sys = project_.findSystem(id);
    if (!sys) return false;
    sys->pressButton(button, down);  // spread across the block in onProcess
    return true;
}

void Engine::applyConfigField(SystemId id, std::uint8_t field, double value) {
    SystemBase* sys = project_.findSystem(id);
    if (!sys) return;

    // The two UNIVERSAL settings apply to every backend through the base interface — they must run
    // before the SameBoy-only cast below, or they silently no-op on a Mesen (NES/GBA) system even
    // though its gain slider / reload toggle look live in the UI.
    switch (static_cast<ConfigField>(field)) {
        case ConfigField::Gain:
            sys->setGainDb(static_cast<float>(value));  // live, smoothed
            return;
        case ConfigField::ReloadOnRomChange:
            sys->setRomReload(value != 0.0);  // a UI-thread ROM-watch flag; no core effect
            return;
        default:
            break;
    }

    // The remaining fields are backend-specific emulator knobs, dispatched by concrete type (a system
    // is exactly one, so the order of the casts below doesn't matter). Mesen (NES) first.
    if (auto* mn = dynamic_cast<MesenNesSystem*>(sys)) {
        switch (static_cast<ConfigField>(field)) {
            case ConfigField::NesRegion:
                mn->setRegion(static_cast<std::uint32_t>(value));  // value-guarded; resets the core on change
                break;
            case ConfigField::NesRemoveSpriteLimit:
                mn->setRemoveSpriteLimit(value != 0.0);            // live — the PPU re-reads it per scanline
                break;
            default:
                break;
        }
        return;
    }

    // SameBoy emulator knobs (model / highpass / link group / fast boot).
    auto* sb = dynamic_cast<SameBoySystem*>(sys);
    if (!sb) return;
    switch (static_cast<ConfigField>(field)) {
        case ConfigField::Model: {
            const auto m = static_cast<SameBoyModel>(static_cast<std::uint32_t>(value));
            if (sb->config_.model != m) {
                sb->config_.model = m;
                sb->restartEmulator();  // rebuilds gb_ + clears the savestate (a new model can't restore an old one)
                project_.rebuildLinkGroups();
            }
            break;
        }
        case ConfigField::Highpass: {
            const auto hp = static_cast<SameBoyHighpass>(static_cast<std::uint32_t>(value));
            if (sb->config_.highpass != hp) {
                sb->config_.highpass = hp;
                sb->applyHighpassMode();  // live — the filter samples its mode every audio frame
            }
            break;
        }
        case ConfigField::LinkGroup: {
            const auto g = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
            if (sb->config_.linkGroupId != g) {
                sb->config_.linkGroupId = g;
                project_.rebuildLinkGroups();
            }
            break;
        }
        case ConfigField::FastBoot:
            sb->setFastBoot(value != 0.0);  // deferred to the next restart
            break;
        case ConfigField::SerialOutCapture:
            sb->setSerialOutCapture(value != 0.0);  // live — arms LSDj MI.OUT serial-out capture
            break;
        default:
            break;  // the universal fields (Gain / ReloadOnRomChange) were handled above
    }
}
