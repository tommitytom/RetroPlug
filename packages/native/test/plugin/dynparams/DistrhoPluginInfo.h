// Minimal DPF plugin identity for retroplug-dynparams-test. DPF requires this header at a fixed
// include path; the test's own include dir is ordered first so it shadows plugin/DistrhoPluginInfo.h.
// Nothing here is shipped - the test builds no plugin format, only the format-neutral PluginExporter,
// which is where the dynamic-parameter diff classifier lives.
#pragma once

#define DISTRHO_PLUGIN_NAME        "DynParamsTest"
#define DISTRHO_PLUGIN_URI         "urn:retroplug:dynparams-test"

#define DISTRHO_PLUGIN_NUM_INPUTS  0
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2

#define DISTRHO_PLUGIN_HAS_UI      0
#define DISTRHO_PLUGIN_IS_RT_SAFE  1

// the feature under test
#define DISTRHO_PLUGIN_WANT_DYNAMIC_PARAMETERS 1
