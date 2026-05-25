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
#include <fstream>

#include "lsdj/SampleCache.hpp"
#include "project/ProjectSerialization.hpp"
#include "system/SystemTypes.hpp"
#include "util/Base64.hpp"
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

        // Headless test hook: load a .rplg file (raw PKZIP) at construction
        // when RETROPLUG_AUTOLOAD_PROJECT is set. Lets `reaper -renderproject`
        // (and friends) host the plugin with a preconfigured ROM without
        // requiring the .RPP to embed a DPF state chunk. If the host later
        // calls setState with non-empty data, that path replaces this one.
        if (const char* autoloadPath = std::getenv("RETROPLUG_AUTOLOAD_PROJECT")) {
            std::ifstream in(autoloadPath, std::ios::binary | std::ios::ate);
            if (in) {
                const std::streamsize size = in.tellg();
                if (size > 0) {
                    in.seekg(0, std::ios::beg);
                    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
                    if (in.read(reinterpret_cast<char*>(bytes.data()), size)) {
                        if (auto parsed = projectConfigFromZip(bytes)) {
                            applyProjectFromConfig(*parsed);
                            d_stderr("[PluginDSP] autoloaded project from %s", autoloadPath);
                        } else {
                            d_stderr("[PluginDSP] RETROPLUG_AUTOLOAD_PROJECT: failed to parse %s", autoloadPath);
                        }
                    }
                }
            } else {
                d_stderr("[PluginDSP] RETROPLUG_AUTOLOAD_PROJECT: cannot open %s", autoloadPath);
            }
        }
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

    const char* getLabel()       const noexcept override { return "RetroPlug"; }
    const char* getDescription() const          override { return "Multi-system retro emulator host (Game Boy / NES / GBA)"; }
    const char* getMaker()       const noexcept override { return "tommitytom"; }
    const char* getLicense()     const noexcept override { return "ISC"; }
    uint32_t    getVersion()     const noexcept override { return d_version(0, 6, 1); }
    int64_t     getUniqueId()    const noexcept override { return d_cconst('R', 'P', 'l', 'g'); }

    // ----------------------------------------------------------------------------------------------------------------
    // Init

    // Name the 8 outputs as four stereo pairs and tag each pair with a
    // sequential custom port group (IDs 0..3, per DPF's contract) so DAWs
    // that honour port grouping show them as Out 1..4 stereo pairs rather
    // than eight loose mono ports. Hosts that ignore groupId fall back to
    // the per-port name.
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input) {
            Plugin::initAudioPort(input, index, port);
            return;
        }
        const uint32_t pair = index / 2;
        const bool     left = (index % 2) == 0;
        char nameBuf[16];
        char symBuf [16];
        std::snprintf(nameBuf, sizeof(nameBuf), "Out %u%c", pair + 1, left ? 'L' : 'R');
        std::snprintf(symBuf,  sizeof(symBuf),  "out_%u_%c", pair + 1, left ? 'l' : 'r');
        port.hints   = 0;
        port.name    = nameBuf;
        port.symbol  = symBuf;
        port.groupId = pair;
    }

    void initPortGroup(uint32_t groupId, PortGroup& portGroup) override
    {
        constexpr uint32_t kPairCount = DISTRHO_PLUGIN_NUM_OUTPUTS / 2;
        if (groupId >= kPairCount) return;
        char nameBuf[16];
        char symBuf [16];
        std::snprintf(nameBuf, sizeof(nameBuf), "Out %u", groupId + 1);
        std::snprintf(symBuf,  sizeof(symBuf),  "out_%u", groupId + 1);
        portGroup.name   = nameBuf;
        portGroup.symbol = symBuf;
    }

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
            const auto zip = projectConfigToZip(project.snapshotConfig());
            // DPF state is a NUL-terminated UTF-8 string, so the binary zip
            // blob is wrapped in base64. The inner zip is already deflated,
            // so the base64 layer's 33% overhead applies to compressed bytes
            // — still a large net win over today's JSON+inline-base64.
            const std::string encoded = base64::encode(zip);
            return String(encoded.c_str());
        } catch (const std::exception& e) {
            d_stderr("[PluginDSP] getState serialization failed: %s", e.what());
            return String();
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "project") != 0) return;
        if (value == nullptr || value[0] == '\0') return;
        if (std::getenv("RETROPLUG_TRACE_LIFECYCLE"))
            d_stderr("[PluginDSP] setState (%zu chars)", std::strlen(value));
        const auto decoded = base64::decode(value);
        auto parsed = projectConfigFromZip(decoded);
        if (!parsed) {
            d_stderr("[PluginDSP] setState: failed to parse project zip");
            return;
        }
        applyProjectFromConfig(*parsed);
    }

    // Replace the running project with a fully-parsed config. Shared by DPF
    // setState (host-driven save/restore) and Command::LoadProject (the
    // user-driven "Load project" menu entry, fed in via the UI thread which
    // does the file IO and zip parse). Caller is responsible for the
    // threading context — setState runs DSP-side before activate;
    // Command::LoadProject runs DSP-side during the run-loop command drain,
    // same window AddSystem/etc. already mutate the project.
    void applyProjectFromConfig(const ProjectConfig& parsed)
    {
        // Tear down current systems. Non-RT (deletes GB instances) — same
        // category of work AddSystem/RemoveSystem already do during command
        // drain.
        project.clearSystems();
        project.config() = ProjectConfig{};

        SystemId firstAdded = 0;
        for (const auto& sysConfig : parsed.systems) {
            const SystemId id = project.addSystem(sysConfig);
            if (id == 0) {
                d_stderr("[PluginDSP] applyProjectFromConfig: addSystem failed for one entry");
                continue;
            }
            if (firstAdded == 0) firstAdded = id;
        }
        focusedSystemAtomic.store(firstAdded, std::memory_order_release);

        // Notify the UI (if attached) so it drops its cached project view.
        if (!events.tryPush(Event::makeConfigChanged()))
            d_stderr("[PluginDSP] applyProjectFromConfig: event queue full; dropping ConfigChanged");
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
        if (std::getenv("RETROPLUG_TRACE_LIFECYCLE"))
            d_stderr("[PluginDSP] activate (sr=%g)", fSampleRate);
        fSmoothGain.clearToTargetValue();
        project.onActivate(fSampleRate);
    }

    void deactivate() override
    {
        if (std::getenv("RETROPLUG_TRACE_LIFECYCLE"))
            d_stderr("[PluginDSP] deactivate");
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

                case Command::Kind::SetZoom: {
                    const std::uint8_t z = cmd.payload.setZoom.zoom;
                    if (z >= 1 && z <= 6 &&
                        project.config().settings.zoom != z) {
                        project.config().settings.zoom = z;
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::SetLayout: {
                    const SystemLayout l = cmd.payload.setLayout.layout;
                    if (project.config().settings.layout != l) {
                        project.config().settings.layout = l;
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::SetAudioRouting: {
                    const AudioRouting r = cmd.payload.setAudioRouting.routing;
                    if (project.config().settings.audioRouting != r) {
                        project.config().settings.audioRouting = r;
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::ResetSystem: {
                    if (SystemBase* sys = project.findSystem(cmd.payload.resetSystem.id))
                        sys->onReset();
                } break;

                case Command::Kind::NewSram: {
                    if (SystemBase* sys = project.findSystem(cmd.payload.newSram.id)) {
                        sys->clearSram();
                        projectMutated = true;
                    }
                } break;

                case Command::Kind::SetFastBoot: {
                    auto& fb = cmd.payload.setFastBoot;
                    if (SystemBase* sys = project.findSystem(fb.id)) {
                        const auto cur = sys->fastBoot();
                        if (cur && *cur != fb.enabled) {
                            sys->setFastBoot(fb.enabled);
                            projectMutated = true;
                        }
                    }
                } break;

                case Command::Kind::SetModel: {
                    auto& sm = cmd.payload.setModel;
                    if (auto* sb = dynamic_cast<SameBoySystem*>(project.findSystem(sm.id))) {
                        if (sb->config_.model != sm.model) {
                            sb->config_.model = sm.model;
                            sb->restartEmulator();
                            project.rebuildLinkGroups();
                            projectMutated = true;
                        }
                    }
                } break;

                case Command::Kind::SetHighpass: {
                    auto& sh = cmd.payload.setHighpass;
                    if (auto* sb = dynamic_cast<SameBoySystem*>(project.findSystem(sh.id))) {
                        if (sb->config_.highpass != sh.mode) {
                            sb->config_.highpass = sh.mode;
                            sb->applyHighpassMode();
                            projectMutated = true;
                        }
                    }
                } break;

                case Command::Kind::SetReloadOnRomChange: {
                    auto& sr = cmd.payload.setReloadOnRomChange;
                    if (SystemBase* sys = project.findSystem(sr.id)) {
                        if (sys->wantsRomReload() != sr.enabled) {
                            sys->setRomReload(sr.enabled);
                            projectMutated = true;
                        }
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
                        slot->compiledBytes = *owned;
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
                    ProjectConfig* config = cmd.payload.loadProject.config;
                    if (config) {
                        applyProjectFromConfig(*config);
                        // applyProjectFromConfig constructs SameBoySystems via
                        // Project::addSystem but doesn't activate them — the
                        // setState path relies on DPF calling activate() after
                        // setState, but Command::LoadProject runs mid-run()
                        // with no following activate. Activate now so the new
                        // systems actually start emulating; SameBoySystem's
                        // onActivate is idempotent for already-active systems.
                        project.onActivate(fSampleRate);
                        delete config;
                        // applyProjectFromConfig already pushes ConfigChanged.
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

        // Zero every output channel up front. Stereo mode only writes to
        // outs[0]/[1], so outs[2..7] stay silent; multi-out modes also
        // start from zero and only the systems mapped to each channel
        // contribute audio.
        for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
            std::memset(outputs[c], 0, frames * sizeof(float));

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

        // Audio routing — pick which output channels each system writes to.
        //   Stereo:         all systems sum into outs[0]/[1] (existing path).
        //   TwoPerInstance: system i writes to outs[(2i)%N]/[(2i+1)%N].
        //   OnePerInstance: system i writes its (L+R) mono mix to outs[i%N].
        //                   Passing the same buffer for both finishBlock
        //                   channels makes SameBoySystem sum L and R into
        //                   that single channel for free.
        const AudioRouting audioRouting = project.config().settings.audioRouting;
        if (audioRouting == AudioRouting::Stereo) {
            project.onProcess(info, outputs);
        } else {
            constexpr uint32_t kNumOuts = DISTRHO_PLUGIN_NUM_OUTPUTS;
            auto outsForIndex = [&](std::size_t i, float** dst) {
                if (audioRouting == AudioRouting::OnePerInstance) {
                    dst[0] = outputs[i % kNumOuts];
                    dst[1] = outputs[i % kNumOuts];
                } else {
                    const std::size_t p = (2 * i) % kNumOuts;
                    dst[0] = outputs[p];
                    dst[1] = outputs[(p + 1) % kNumOuts];
                }
            };

            const auto& systems = project.systems();

            // Index lookup for linked SameBoy members — they need their
            // outs slice computed from the system's slot in the project,
            // not their position within the link group.
            auto indexOf = [&systems](const SameBoySystem* needle) -> std::size_t {
                for (std::size_t i = 0; i < systems.size(); ++i) {
                    if (systems[i].get() == needle) return i;
                }
                return SIZE_MAX;
            };

            // Unlinked systems: SameBoySystem::onProcess bails when
            // linkPeers_ is non-empty, so this loop only drives the
            // standalone ones. Non-SameBoy systems (Mesen, GBA) flow
            // through here too — their onProcess writes to the per-system
            // outs slice with no further routing logic needed.
            for (std::size_t i = 0; i < systems.size(); ++i) {
                auto* sys = systems[i].get();
                if (!sys) continue;
                float* perSysOuts[2];
                outsForIndex(i, perSysOuts);
                sys->onProcess(info, perSysOuts);
            }

            // Linked SameBoy groups: round-robin step in lockstep (the
            // serial-bit ferrying inside SameBoySystem::serialStart/end
            // needs members to advance together), then finishBlock each
            // into its routed outs slice. LinkGroup::onProcess can't do
            // this directly because it hard-codes outs[0]/[1] for every
            // member.
            for (const auto& group : project.linkGroups()) {
                const auto& members = group.members();
                if (members.empty()) continue;

                for (auto* sb : members) {
                    if (sb) sb->prepareForBlock(info);
                }
                bool anyBelow = true;
                while (anyBelow) {
                    anyBelow = false;
                    for (auto* sb : members) {
                        if (sb && sb->stepIfBelowTarget(info.frames))
                            anyBelow = true;
                    }
                }
                for (auto* sb : members) {
                    if (!sb) continue;
                    const std::size_t idx = indexOf(sb);
                    if (idx == SIZE_MAX) continue;
                    float* perSysOuts[2];
                    outsForIndex(idx, perSysOuts);
                    sb->finishBlock(info, perSysOuts);
                }
            }
        }

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
        // against N>1 emulators summing past full scale. Applied uniformly
        // to all 8 channels — silent ones (Stereo mode's outs[2..7]) pass
        // through as zeros, the smoother advances at the same per-sample
        // rate as before.
        for (uint32_t i = 0; i < frames; ++i) {
            const float g = fSmoothGain.next();
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c) {
                float s = outputs[c][i] * g;
                outputs[c][i] = s / (1.0f + std::fabs(s));
            }
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
