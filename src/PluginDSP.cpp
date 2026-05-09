/*
 * RetroPlug DSP — Step 1: SameBoy single-instance MVP.
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

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "transport/CommandQueue.hpp"
#include "transport/EventQueue.hpp"

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
    SharedDSPData       shared;
    Project             project;
    CommandQueue        commands;
    EventQueue          events;
    std::atomic<double> sampleRateAtomic{44100.0};

    LVGLPluginDSP()
        : Plugin(kPluginParameterCount, 0, 0)
    {
        fSampleRate = getSampleRate();
        sampleRateAtomic.store(fSampleRate, std::memory_order_release);

        fSmoothGain.setSampleRate(fSampleRate);
        fSmoothGain.setTargetValue(DB_CO(0.0f));
        fSmoothGain.setTimeConstant(0.020f);

        // Pre-reserve so adoptSystem in run() never reallocates. 16 instances
        // is well over what the multi-instance step targets.
        project.reserve(16);

        shared.project    = &project;
        shared.commands   = &commands;
        shared.events     = &events;
        shared.sampleRate = &sampleRateAtomic;

        // No bootstrap system — the UI loads a ROM via plugin.openRomBrowser
        // (Step 3). DPF setState (Step 4) will populate the project from a
        // saved host project where applicable.
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

    const char* getLabel()       const noexcept override { return "RetroPlug"; }
    const char* getDescription() const          override { return "Game Boy emulator host (Step 1: SameBoy MVP)"; }
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
             const MidiEvent*, uint32_t) override
    {
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
                    SystemBase* incoming = cmd.payload.loadRom.newSystem;
                    if (!incoming) break;
                    SystemBase* released = nullptr;
                    if (project.systems().empty()) {
                        // No system yet — adopt directly. project.reserve()
                        // in the ctor keeps this allocation-free.
                        project.adoptSystem(incoming);
                    } else {
                        // Replace slot 0 (single-instance MVP).
                        // swapSystem returns the displaced raw pointer.
                        SystemBase* slot0 = project.systems().front().get();
                        released = project.swapSystem(slot0->id(), incoming);
                    }
                    if (released) {
                        // Ship back to UI for off-thread free. If the event
                        // queue is full we'd rather leak than free here.
                        if (!events.tryPush(Event::makeSystemReleased(released)))
                            d_stderr("event queue full; leaking displaced system");
                    }
                } break;

                case Command::Kind::None:
                default:
                    break;
            }
        }

        float* const outL = outputs[0];
        float* const outR = outputs[1];
        std::memset(outL, 0, frames * sizeof(float));
        std::memset(outR, 0, frames * sizeof(float));

        AudioBlockInfo info{ frames, fSampleRate };
        for (auto& sys : project.systems()) {
            if (sys) sys->onProcess(info, outputs);
        }

        // Apply (smoothed) master gain to the mixed output.
        for (uint32_t i = 0; i < frames; ++i) {
            const float g = fSmoothGain.next();
            outL[i] *= g;
            outR[i] *= g;
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
