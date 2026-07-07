// DPF identity for the GREENFIELD plugin (retroplug-greenfield.{vst3,clap,…}). UI-less and
// hard-stereo: the DSP runs the greenfield Engine directly. Kept distinct from the legacy
// packages/native/src/DistrhoPluginInfo.h (name / URI / UNIQUE_ID / CLAP_ID all differ) so the two
// plugins coexist. DPF requires this header at a fixed include path — the plugin's own include dir is
// ordered BEFORE packages/native/src so this file wins over the legacy one.
#pragma once

#define DISTRHO_PLUGIN_NAME        "RetroPlug Greenfield"
#define DISTRHO_PLUGIN_URI         "urn:distrho:retroplug-greenfield"
#define DISTRHO_PLUGIN_CLAP_ID     "studio.kx.distrho.retroplug-greenfield"

#define DISTRHO_PLUGIN_NUM_INPUTS  0
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2   // greenfield Engine::processBlock is hard-stereo (outL/outR)

#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#define DISTRHO_PLUGIN_IS_SYNTH    1
#define DISTRHO_PLUGIN_HAS_UI      0   // DSP-first milestone: no editor (validate + reaper never open one)

#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_LATENCY     0
#define DISTRHO_PLUGIN_WANT_PROGRAMS    0

// A distinct 4-char id so the greenfield VST3/AU never collides with the legacy RPlg.
#define DISTRHO_PLUGIN_BRAND_ID  Dstr
#define DISTRHO_PLUGIN_UNIQUE_ID RPgf

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
