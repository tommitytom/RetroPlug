/*
 * RetroPlug DSP — SameBoy / Mesen / GBA host.
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoPlugin.hpp"
#include "extra/ValueSmoother.hpp"
#include "PluginShared.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lsdj/SampleCache.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"
#include "transport/MidiTypes.hpp"

START_NAMESPACE_DISTRHO

// --------------------------------------------------------------------------------------------------------------------

namespace {

constexpr float CLAMP(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

// dB → linear gain (skip below -90 dB)
constexpr float DB_CO(float g) {
    return g > -90.0f ? std::pow(10.0f, g * 0.05f) : 0.0f;
}

} // namespace

// --------------------------------------------------------------------------------------------------------------------

class LVGLPluginDSP : public Plugin {
    enum Parameters {
        kParamGain = 0,
    };

    float fGainDB = 0.0f;
    double fSampleRate = 44100.0;
    ExponentialValueSmoother fSmoothGain;

public:
    // TODO: No public members
    SharedDSPData         shared;
    Project               project;
    CommandQueue          commands;
    EventQueue            events;
    std::atomic<double>   sampleRateAtomic{44100.0};
    std::atomic<SystemId> focusedSystemAtomic{0};

    LVGLPluginDSP()
        : Plugin(kPluginParameterCount, 0, /*states=*/1)
    {
        fSampleRate = getSampleRate();
        sampleRateAtomic.store(fSampleRate, std::memory_order_release);

        fSmoothGain.setSampleRate(fSampleRate);
        fSmoothGain.setTargetValue(DB_CO(0.0f));
        fSmoothGain.setTimeConstant(0.020f);

        // Pre-reserve so adoptSystem in run() never reallocates. 16 instances
        // is well over what the multi-instance step targets.
        project.reserve(16);

        shared.project         = &project;
        shared.commands        = &commands;
        shared.events          = &events;
        shared.sampleRate      = &sampleRateAtomic;
        shared.focusedSystemId = &focusedSystemAtomic;

        // No bootstrap system — the UI loads a ROM via plugin.openRomBrowser,
        // and DPF setState populates the project from a saved host project
        // where applicable.
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

    const char* getLabel()       const noexcept override { return "RetroPlug"; }
    const char* getDescription() const          override { return "Multi-system retro emulator host (Game Boy / NES / GBA)"; }
    const char* getMaker()       const noexcept override { return "tommitytom"; }
    const char* getLicense()     const noexcept override { return "ISC"; }
    uint32_t    getVersion()     const noexcept override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const noexcept override { return d_cconst('R', 'P', 'l', 'g'); }

    // ----------------------------------------------------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kPluginParameterCount) return;
        const ParamSpec& spec = kPluginParameters[index];
        parameter.symbol      = spec.symbol;
        parameter.name        = spec.name;
        parameter.shortName   = spec.shortName;
        parameter.unit        = spec.unit;
        parameter.ranges.min  = spec.min;
        parameter.ranges.max  = spec.max;
        parameter.ranges.def  = spec.def;
        parameter.hints       = spec.hints;
    }

    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.hints        = kStateIsHostReadable | kStateIsHostWritable;
        state.key          = "project";
        state.label        = "Project";
        state.description  = "Serialized RetroPlug project (JSON).";
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "project") != 0) return String();
        try {
            const std::string json = projectConfigToJson(project.snapshotConfig());
            return String(json.c_str());
        } catch (const std::exception& e) {
            d_stderr("[PluginDSP] getState serialization failed: %s", e.what());
            return String();
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "project") != 0) return;
        applyProjectFromJson(value);
    }

    // Replace the running project with one parsed from a JSON blob. Shared by
    // DPF setState (host-driven save/restore) and Command::LoadProject (the
    // user-driven "Load project" menu entry, fed in via the UI thread).
    // Caller is responsible for the threading context — setState runs DSP-side
    // before activate; Command::LoadProject runs DSP-side during the run-loop
    // command drain, same window AddSystem/etc. already mutate the project.
    void applyProjectFromJson(const char* json)
    {
        if (json == nullptr || json[0] == '\0') return;

        std::optional<ProjectConfig> parsed;
        try {
            parsed = projectConfigFromJson(std::string_view(json));
        } catch (const std::exception& e) {
            d_stderr("[PluginDSP] applyProjectFromJson parse exception: %s", e.what());
            return;
        }
        if (!parsed) {
            d_stderr("[PluginDSP] applyProjectFromJson: failed to parse project JSON");
            return;
        }

        // Tear down current systems. Non-RT (deletes GB instances) — same
        // category of work AddSystem/RemoveSystem already do during command
        // drain.
        project.clearSystems();
        project.config() = ProjectConfig{};

        SystemId firstAdded = 0;
        for (const auto& sysConfig : parsed->systems) {
            const SystemId id = project.addSystem(sysConfig);
            if (id == 0) {
                d_stderr("[PluginDSP] applyProjectFromJson: addSystem failed for one entry");
                continue;
            }
            if (firstAdded == 0) firstAdded = id;
        }
        focusedSystemAtomic.store(firstAdded, std::memory_order_release);

        // Notify the UI (if attached) so it drops its cached project view.
        if (!events.tryPush(Event::makeConfigChanged()))
            d_stderr("[PluginDSP] applyProjectFromJson: event queue full; dropping ConfigChanged");
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
            case kParamGain: return fGainDB;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
            case kParamGain:
                fGainDB = value;
                fSmoothGain.setTargetValue(DB_CO(CLAMP(value, -90.0f, 12.0f)));
                break;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Audio/MIDI Processing

    void activate() override
    {
        fSmoothGain.clearToTargetValue();
        project.onActivate(fSampleRate);
    }

    void deactivate() override
    {
        project.onDeactivate();
    }

    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        auto sendBack = [this](SystemBase* released) {
            if (!released) return;
            if (!events.tryPush(Event::makeSystemReleased(released)))
                d_stderr("event queue full; leaking displaced system");
        };

        bool projectMutated = false;

        // Drain UI commands before running emulators so any keypresses queued
        // since the last block land at the right place in this one. The loop
        // body MUST NOT allocate or free — heap ownership transfers happen
        // through raw pointers in the command/event queues.
        Command cmd;
        while (commands.tryPop(cmd)) {
            switch (cmd.kind) {
                case Command::Kind::ButtonPress: {
                    auto& bp = cmd.payload.buttonPress;
                    if (SystemBase* sys = project.findSystem(bp.systemId))
                        sys->pressButton(bp.button, bp.down);
                } break;

                case Command::Kind::LoadRom: {
                    // Step-5 semantics: empty project → adopt as first system;
                    // otherwise replace the focused tile (or fall back to slot
                    // 0 if no focus is set).
                    SystemBase* incoming = cmd.payload.loadRom.newSystem;
                    if (!incoming) break;
                    if (project.systems().empty()) {
                        project.adoptSystem(incoming);
                        focusedSystemAtomic.store(incoming->id(), std::memory_order_release);
                    } else {
                        SystemId target = focusedSystemAtomic.load(std::memory_order_acquire);
                        if (!project.findSystem(target))
                            target = project.systems().front()->id();
                        sendBack(project.swapSystem(target, incoming));
                        focusedSystemAtomic.store(incoming->id(), std::memory_order_release);
                    }
                    project.rebuildLinkGroups();
                    projectMutated = true;
                } break;

                case Command::Kind::AddSystem: {
                    SystemBase* incoming = cmd.payload.addSystem.newSystem;
                    if (!incoming) break;
                    project.adoptSystem(incoming);
                    project.rebuildLinkGroups();
                    // Auto-focus the first system we add to an empty project.
                    if (focusedSystemAtomic.load(std::memory_order_relaxed) == 0)
                        focusedSystemAtomic.store(incoming->id(), std::memory_order_release);
                    projectMutated = true;
                } break;

                case Command::Kind::ReplaceSystem: {
                    auto& rs = cmd.payload.replaceSystem;
                    if (!rs.newSystem) break;
                    sendBack(project.swapSystem(rs.id, rs.newSystem));
                    project.rebuildLinkGroups();
                    if (focusedSystemAtomic.load(std::memory_order_relaxed) == rs.id)
                        focusedSystemAtomic.store(rs.newSystem->id(), std::memory_order_release);
                    projectMutated = true;
                } break;

                case Command::Kind::RemoveSystem: {
                    const SystemId removedId = cmd.payload.removeSystem.id;
                    sendBack(project.removeSystemAndRelease(removedId));
                    if (focusedSystemAtomic.load(std::memory_order_relaxed) == removedId) {
                        // Move focus to whatever's left, or 0 if empty.
                        const SystemId next = project.systems().empty()
                            ? SystemId{0}
                            : project.systems().front()->id();
                        focusedSystemAtomic.store(next, std::memory_order_release);
                    }
                    projectMutated = true;
                } break;

                case Command::Kind::SetLinkGroup: {
                    auto& slg = cmd.payload.setLinkGroup;
                    if (SystemBase* sys = project.findSystem(slg.id)) {
                        if (auto* sb = dynamic_cast<SameBoySystem*>(sys)) {
                            sb->config_.linkGroupId = slg.groupId;
                            project.rebuildLinkGroups();
                            projectMutated = true;
                        }
                    }
                } break;

                case Command::Kind::SetMidiRouting: {
                    const MidiRouting r = cmd.payload.setMidiRouting.routing;
                    if (project.config().settings.midiRouting != r) {
                        project.config().settings.midiRouting = r;
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::SetLsdjSyncConfig: {
                    auto& sc = cmd.payload.setLsdjSyncConfig;
                    auto* sys = project.findSystem(sc.id);
                    auto* sb  = dynamic_cast<SameBoySystem*>(sys);
                    if (!sb) break;
                    // Locate the lsdj-sync RoleConfig in the system's config
                    // and mutate it in place. Then reinstantiate roles so the
                    // live LsdjSyncRole picks up the new cfg_ snapshot
                    // (transient state — arduinoboyPlaying_, lastRow_, etc. —
                    // resets, which is the desired behavior on a mode flip).
                    bool found = false;
                    for (auto& rc : sb->config_.roles) {
                        if (auto* lsdj = rfl::get_if<LsdjSyncConfig>(&rc.variant())) {
                            lsdj->mode         = static_cast<LsdjSyncMode>(sc.mode);
                            lsdj->tempoDivisor = sc.tempoDivisor > 0 ? sc.tempoDivisor : 1;
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        sb->instantiateRoles();
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::PatchKit: {
                    auto& pk = cmd.payload.patchKit;
                    // Ownership lands here; release at scope exit even on
                    // early bailout so we never leak the heap-allocated
                    // 16 KB vector the UI hands us.
                    std::unique_ptr<std::vector<std::uint8_t>> owned(pk.bytes);
                    if (!owned) break;

                    auto* sb = dynamic_cast<SameBoySystem*>(project.findSystem(pk.id));
                    if (!sb) break;

                    // Update both the runtime role (for live emulator
                    // patching) and the per-system config (so project
                    // saves round-trip the patched kit).
                    LsdjKitPatchRole* role = nullptr;
                    for (auto& r : sb->roles_) {
                        if (r && r->kind() == "lsdj-kit-patch") {
                            role = static_cast<LsdjKitPatchRole*>(r.get());
                            break;
                        }
                    }
                    if (role) role->queuePatch(pk.kitIndex, *owned);

                    for (auto& rc : sb->config_.roles) {
                        auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant());
                        if (!kitCfg) continue;
                        // Find-or-create the slot entry; the UI controls
                        // the per-sample metadata, but we own the bytes
                        // and hash on the DSP side.
                        rp::lsdj::LsdjKitConfig* slot = nullptr;
                        for (auto& k : kitCfg->kits) {
                            if (k.slot == pk.kitIndex) { slot = &k; break; }
                        }
                        if (!slot) {
                            rp::lsdj::LsdjKitConfig fresh;
                            fresh.slot = pk.kitIndex;
                            kitCfg->kits.push_back(std::move(fresh));
                            slot = &kitCfg->kits.back();
                        }
                        slot->compiledBytes = Base64Bytes(*owned);
                        slot->compiledHash  =
                            rp::lsdj::SampleCache::hashBytes(owned->data(), owned->size());
                        break;
                    }
                    projectMutated = true;
                } break;

                case Command::Kind::SubscribeMemory: {
                    auto& sm = cmd.payload.subscribeMemory;
                    if (SystemBase* sys = project.findSystem(sm.systemId))
                        sys->enableMemorySnapshot(sm.type);
                } break;

                case Command::Kind::UnsubscribeMemory: {
                    auto& um = cmd.payload.unsubscribeMemory;
                    if (SystemBase* sys = project.findSystem(um.systemId))
                        sys->disableMemorySnapshot(um.type);
                } break;

                case Command::Kind::LoadProject: {
                    std::string* json = cmd.payload.loadProject.json;
                    if (json) {
                        applyProjectFromJson(json->c_str());
                        // applyProjectFromJson constructs SameBoySystems via
                        // Project::addSystem but doesn't activate them — the
                        // setState path relies on DPF calling activate() after
                        // setState, but Command::LoadProject runs mid-run()
                        // with no following activate. Activate now so the new
                        // systems actually start emulating; SameBoySystem's
                        // onActivate is idempotent for already-active systems.
                        project.onActivate(fSampleRate);
                        delete json;
                        // applyProjectFromJson already pushes ConfigChanged.
                        // Don't double-emit by setting projectMutated.
                    }
                } break;

                case Command::Kind::None:
                default:
                    break;
            }
        }

        // Notify the UI exactly once per block when the project tree changed.
        // The UI listens for ConfigChanged and re-queries listSystems() —
        // without this, the bridge's "rom-loaded" event fires before the DSP
        // has drained the command, so React would see a stale (empty) project.
        if (projectMutated) {
            if (!events.tryPush(Event::makeConfigChanged()))
                d_stderr("event queue full; UI will miss a ConfigChanged tick");
        }

        // Translate DPF MidiEvents to the shell type and dispatch through the
        // current routing rule. Done one event at a time so we never need to
        // allocate a translation buffer; dispatchMidi is internally a simple
        // loop, so per-event calls cost the same as a batched array.
        const MidiRouting routing = project.config().settings.midiRouting;
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& src = midiEvents[i];
            ::MidiEvent shell;
            shell.frame = src.frame;
            shell.size  = src.size;
            std::memcpy(shell.data, src.data, ::MidiEvent::kDataSize);
            shell.dataExt = src.dataExt;
            project.dispatchMidi(&shell, 1, routing);
        }

        float* const outL = outputs[0];
        float* const outR = outputs[1];
        std::memset(outL, 0, frames * sizeof(float));
        std::memset(outR, 0, frames * sizeof(float));

        // Host timing from DPF. When bbt is valid, compute continuous PPQ
        // position from bar/beat/tick; otherwise (host without BBT support)
        // approximate from sample frame at the default tempo. A manual BPM
        // override for hosts without BBT could be wired here later.
        const TimePosition& tp = getTimePosition();
        double bpm = 120.0;
        double ppq = 0.0;
        if (tp.bbt.valid) {
            bpm = tp.bbt.beatsPerMinute;
            // bbt.beat is 1-based per DistrhoDetails.hpp.
            ppq = (tp.bbt.barStartTick
                 + (tp.bbt.beat - 1) * tp.bbt.ticksPerBeat
                 + tp.bbt.tick) / tp.bbt.ticksPerBeat;
        } else if (tp.playing) {
            ppq = (static_cast<double>(tp.frame) / fSampleRate) * (bpm / 60.0);
        }
        AudioBlockInfo info{ frames, fSampleRate, bpm, ppq, tp.playing };
        project.onProcess(info, outputs);

        // Drain per-system MIDI output back to the host. Populated by
        // MIDI-emitting roles (e.g. ArduinoboyMaster's MI.OUT decoder).
        for (auto& sys : project.systems()) {
            if (!sys) continue;
            auto& outBuf = sys->midiOut();
            for (const auto& ev : outBuf) {
                MidiEvent dpfEv{};
                dpfEv.frame = ev.frame;
                dpfEv.size  = ev.size;
                std::memcpy(dpfEv.data, ev.data, ::MidiEvent::kDataSize);
                dpfEv.dataExt = ev.dataExt;
                writeMidiEvent(dpfEv);
            }
            outBuf.clear();
        }

        // Master gain → soft-clip output limiter. The clipper is x/(1+|x|),
        // a simple unit-bounded saturator with no allocations and a smooth
        // transition near 1.0; it's not a creative effect, just a guard
        // against N>1 emulators summing past full scale.
        for (uint32_t i = 0; i < frames; ++i) {
            const float g = fSmoothGain.next();
            float l = outL[i] * g;
            float r = outR[i] * g;
            l = l / (1.0f + std::fabs(l));
            r = r / (1.0f + std::fabs(r));
            outL[i] = l;
            outR[i] = r;
        }
    }

    void sampleRateChanged(double newSampleRate) override
    {
        fSampleRate = newSampleRate;
        sampleRateAtomic.store(newSampleRate, std::memory_order_release);
        fSmoothGain.setSampleRate(newSampleRate);
        project.onSampleRateChanged(newSampleRate);
    }

    // ----------------------------------------------------------------------------------------------------------------

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LVGLPluginDSP)
};

// --------------------------------------------------------------------------------------------------------------------

SharedDSPData* getSharedDSPData(void* pluginPtr)
{
    return &static_cast<LVGLPluginDSP*>(pluginPtr)->shared;
}

Plugin* createPlugin()
{
    return new LVGLPluginDSP();
}

// --------------------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
