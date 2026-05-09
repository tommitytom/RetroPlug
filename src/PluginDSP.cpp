/*
 * LVGL plugin example
 * Copyright (C) 2021 Jean Pierre Cimalando <jp-dev@inbox.ru>
 * Copyright (C) 2021-2024 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoPlugin.hpp"
#include "extra/ValueSmoother.hpp"
#include "PluginShared.hpp"

START_NAMESPACE_DISTRHO

// --------------------------------------------------------------------------------------------------------------------

static constexpr const float CLAMP(float v, float min, float max)
{
    return std::min(max, std::max(min, v));
}

static constexpr const float DB_CO(float g)
{
    return g > -90.f ? std::pow(10.f, g * 0.05f) : 0.f;
}

// --------------------------------------------------------------------------------------------------------------------

class LVGLPluginDSP : public Plugin
{
    // Parameter indices match the order in kPluginParameters (PluginShared.hpp).
    enum Parameters {
        kParamGain = 0,
        kParamFreq = 1,
        kParamShape = 2, // 0 = sine, 1 = square
    };

    float fGainDB = -50.0f;
    float fFreqHz = 440.0f;
    int fShape = 0;
    double fPhase = 0.0;
    double fSampleRate = 44100.0;
    ExponentialValueSmoother fSmoothGain;
    ExponentialValueSmoother fSmoothFreq;

public:
    SharedDSPData shared;

    LVGLPluginDSP()
        : Plugin(kPluginParameterCount, 0, 0) // parameters, programs, states
    {
        fSampleRate = getSampleRate();
        fSmoothGain.setSampleRate(fSampleRate);
        fSmoothGain.setTargetValue(DB_CO(0.f));
        fSmoothGain.setTimeConstant(0.020f); // 20ms
        fSmoothFreq.setSampleRate(fSampleRate);
        fSmoothFreq.setTargetValue(440.0f);
        fSmoothFreq.setTimeConstant(0.020f);
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

   /**
      Get the plugin label.@n
      This label is a short restricted name consisting of only _, a-z, A-Z and 0-9 characters.
    */
    const char* getLabel() const noexcept override
    {
        return "RetroPlug";
    }

   /**
      Get an extensive comment/description about the plugin.@n
      Optional, returns nothing by default.
    */
    const char* getDescription() const override
    {
        return "Internal test-tone generator (sine/square) with LVGL GUI";
    }

   /**
      Get the plugin author/maker.
    */
    const char* getMaker() const noexcept override
    {
        return "DISTRHO";
    }

   /**
      Get the plugin license (a single line of text or a URL).@n
      For commercial plugins this should return some short copyright information.
    */
    const char* getLicense() const noexcept override
    {
        return "ISC";
    }

   /**
      Get the plugin version, in hexadecimal.
      @see d_version()
    */
    uint32_t getVersion() const noexcept override
    {
        return d_version(1, 0, 0);
    }

   /**
      Get the plugin unique Id.@n
      This value is used by LADSPA, DSSI and VST plugin formats.
      @see d_cconst()
    */
    int64_t getUniqueId() const noexcept override
    {
        return d_cconst('R', 'P', 'l', 'g');
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Init

   /**
      Initialize the parameter @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kPluginParameterCount)
            return;
        const ParamSpec& spec = kPluginParameters[index];
        parameter.symbol = spec.symbol;
        parameter.name = spec.name;
        parameter.shortName = spec.shortName;
        parameter.unit = spec.unit;
        parameter.ranges.min = spec.min;
        parameter.ranges.max = spec.max;
        parameter.ranges.def = spec.def;
        parameter.hints = spec.hints;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Internal data

   /**
      Get the current value of a parameter.@n
      The host may call this function from any context, including realtime processing.
    */
    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamGain:  return fGainDB;
        case kParamFreq:  return fFreqHz;
        case kParamShape: return static_cast<float>(fShape);
        }
        return 0.0f;
    }

   /**
      Change a parameter value.@n
      The host may call this function from any context, including realtime processing.@n
      When a parameter is marked as automatable, you must ensure no non-realtime operations are performed.
      @note This function will only be called for parameter inputs.
    */
    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGain:
            fGainDB = value;
            fSmoothGain.setTargetValue(DB_CO(CLAMP(value, -90.0f, 30.0f)));
            break;
        case kParamFreq:
            fFreqHz = CLAMP(value, 20.0f, 20000.0f);
            fSmoothFreq.setTargetValue(fFreqHz);
            break;
        case kParamShape:
            fShape = (value > 0.5f) ? 1 : 0;
            break;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Audio/MIDI Processing

   /**
      Activate this plugin.
    */
    void activate() override
    {
        fSmoothGain.clearToTargetValue();
        fSmoothFreq.clearToTargetValue();
        fPhase = 0.0;
    }

   /**
      Run/process function for plugins with MIDI input.
      MIDI events are accepted but currently unused — the oscillator is driven
      entirely by parameters. They're plumbed through so the plugin presents a
      MIDI input port to the host.
    */
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent*, uint32_t) override
    {
        float* const outL = outputs[0];
        float* const outR = outputs[1];

        const int shape = fShape;
        const double phaseStepBase = 1.0 / fSampleRate;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float gain = fSmoothGain.next();
            const float freq = fSmoothFreq.next();

            float sample;
            if (shape == 0)
                sample = std::sin(fPhase * 2.0 * M_PI);
            else
                sample = (fPhase < 0.5) ? 1.0f : -1.0f;

            sample *= gain;
            outL[i] = sample;
            outR[i] = sample;

            fPhase += freq * phaseStepBase;
            if (fPhase >= 1.0)
                fPhase -= std::floor(fPhase);
        }

        // Downsample output and send to UI via ring buffer
        constexpr uint32_t kPts = SharedDSPData::kWaveformPoints;
        float waveform[kPts];
        const uint32_t chunkSize = frames > kPts ? frames / kPts : 1;
        const uint32_t actualPoints = frames > kPts ? kPts : frames;

        for (uint32_t i = 0; i < actualPoints; ++i)
        {
            float peakSigned = 0.0f;
            float peakAbs = 0.0f;
            const uint32_t start = i * chunkSize;
            const uint32_t end = (i + 1 < actualPoints) ? (i + 1) * chunkSize : frames;
            for (uint32_t j = start; j < end; ++j)
            {
                const float mono = (outL[j] + outR[j]) * 0.5f;
                const float abs = mono < 0.0f ? -mono : mono;
                if (abs > peakAbs) { peakAbs = abs; peakSigned = mono; }
            }
            waveform[i] = peakSigned;
        }

        // Write to ring buffer (drop if full — UI will catch up)
        const uint32_t dataSize = actualPoints * sizeof(float);
        if (shared.waveformRing.getWritableDataSize() >= dataSize + sizeof(uint32_t))
        {
            shared.waveformRing.writeUInt(actualPoints);
            shared.waveformRing.writeCustomData(waveform, dataSize);
            shared.waveformRing.commitWrite();
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Callbacks (optional)

   /**
      Optional callback to inform the plugin about a sample rate change.@n
      This function will only be called when the plugin is deactivated.
      @see getSampleRate()
    */
    void sampleRateChanged(double newSampleRate) override
    {
        fSampleRate = newSampleRate;
        fSmoothGain.setSampleRate(newSampleRate);
        fSmoothFreq.setSampleRate(newSampleRate);
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
