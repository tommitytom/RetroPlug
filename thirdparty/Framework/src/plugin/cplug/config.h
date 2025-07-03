#ifndef PLUGIN_CONFIG_H
#define PLUGIN_CONFIG_H

#define CPLUG_COMPANY_NAME "tommitytom"
#define CPLUG_COMPANY_EMAIL ""
#define CPLUG_PLUGIN_NAME "RetroPlug"
#define CPLUG_PLUGIN_URI "http://retroplug.io"
#define CPLUG_PLUGIN_VERSION "1.0.0"

#define CPLUG_IS_INSTRUMENT 1

#define CPLUG_NUM_INPUT_BUSSES 0
#define CPLUG_NUM_OUTPUT_BUSSES 1
#define CPLUG_WANT_MIDI_INPUT 1
#define CPLUG_WANT_MIDI_OUTPUT 1

#define CPLUG_WANT_GUI 1
#define CPLUG_GUI_RESIZABLE 1

// See list of categories here: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst_1_1PlugType.html
#define CPLUG_VST3_CATEGORIES "Instrument|Stereo"

#define CPLUG_VST3_TUID_COMPONENT 'cplg', 'comp', 'rplg', 0
#define CPLUG_VST3_TUID_CONTROLLER 'cplg', 'edit', 'rplg', 0

#define CPLUG_AUV2_VIEW_CLASS CPLUGExampleView
#define CPLUG_AUV2_VIEW_CLASS_STR "CPLUGExampleView"
static const unsigned kAudioUnitProperty_UserPlugin = 'plug';

#define CPLUG_CLAP_ID "com.cplug.example"
#define CPLUG_CLAP_DESCRIPTION "Example plugin"
#define CPLUG_CLAP_FEATURES CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_STEREO

#define CPLUG_NUM_PARAMS 0

#endif // PLUGIN_CONFIG_H
