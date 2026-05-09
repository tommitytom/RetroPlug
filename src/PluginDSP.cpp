/*
 * RetroPlug DSP — Step 1: SameBoy single-instance MVP.
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoPlugin.hpp"
#include "extra/ValueSmoother.hpp"
#include "PluginShared.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "system/SystemTypes.hpp"
#include "system/sameboy/SameBoyConfig.hpp"

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

// Default ROM path for Step 1 dev convenience. RETROPLUG_ROM_PATH env-var
// override wins. Replaced by a UI picker in Step 3.
constexpr const char* kDefaultRomPath = "/home/tommitytom/retro/LSDj-v5.0.3.gb";

std::string resolveRomPath() {
    if (const char* env = std::getenv("RETROPLUG_ROM_PATH")) {
        if (env[0] != '\0') return env;
    }
    return kDefaultRomPath;
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
    SharedDSPData shared;
    Project       project;

    LVGLPluginDSP()
        : Plugin(kPluginParameterCount, 0, 0)
    {
        fSampleRate = getSampleRate();
        fSmoothGain.setSampleRate(fSampleRate);
        fSmoothGain.setTargetValue(DB_CO(0.0f));
        fSmoothGain.setTimeConstant(0.020f);

        shared.project = &project;

        // Bootstrap one SameBoy slot from the dev ROM path. setState (called
        // by hosts that have saved a project) will replace this on load.
        SameBoyConfig sb;
        sb.romPath = resolveRomPath();
        const SystemConfig cfg = sb;
        const SystemId id = project.addSystem(cfg);
        if (id == 0) {
            std::fprintf(stderr,
                "[RetroPlug] could not bootstrap SameBoy from '%s'; running silent\n",
                sb.romPath.c_str());
        } else {
            std::fprintf(stderr,
                "[RetroPlug] bootstrap SameBoy id=%u rom='%s'\n",
                id, sb.romPath.c_str());
        }
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
