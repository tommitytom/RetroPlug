// DPF identity for the RetroPlug plugin. The DSP runs the Engine directly. These are the canonical
// name / URI / UNIQUE_ID / CLAP_ID, so existing DAW projects that reference RetroPlug resolve. DPF requires
// this header at a fixed include path (the plugin's own include dir is ordered first).
#pragma once

#define DISTRHO_PLUGIN_NAME        "RetroPlug"
#define DISTRHO_PLUGIN_URI         "https://retroplug.io"
#define DISTRHO_PLUGIN_CLAP_ID     "net.tommitytom.retroplug"

#define DISTRHO_PLUGIN_NUM_INPUTS  0
#define DISTRHO_PLUGIN_NUM_OUTPUTS 8   // four stereo pairs (out_1..4); each system routes to one per audioRouting

#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#define DISTRHO_PLUGIN_IS_SYNTH    1
#define DISTRHO_PLUGIN_HAS_UI      1   // the React UI, on the shared LVGL editor widget

// Custom OpenGL toolkit: the generic dpf.js LVGL top-level widget (GL flush + keypad/pointer indevs +
// DPF→LVGL input translation). The editor (PluginUI.cpp) subclasses it.
#define DISTRHO_UI_USE_CUSTOM          1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH "LVGL.hpp"   // relative to dpf/distrho/DistrhoUI.hpp
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE  DGL_NAMESPACE::LVGLTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH       480
#define DISTRHO_UI_DEFAULT_HEIGHT      432
// User-resizable: setGeometryConstraints(480,432) becomes the min floor (not a fixed size), so the editor
// can grow to fit a multi-instance grid, and a Wayland tiling WM tiles it instead of floating a fixed-size
// window. PluginUI drives setSize to fit the grid + detects a tiling clamp in onResize.
#define DISTRHO_UI_USER_RESIZABLE      1

// Kept on primarily because it auto-enables DGL_USE_FILE_DROP (drag-and-drop, DISTRHO_UI_FILE_DROP below).
// The OS file *dialog* itself is now portable-file-dialogs (NativeFileDialog), NOT DPF's UI::openFileBrowser
// — that proved unreliable when hosted. Requires USE_FILE_BROWSER on the dpf_add_plugin call in CMakeLists.txt.
#define DISTRHO_UI_FILE_BROWSER        1

// OS file drag-and-drop: enables the UI::uiFileDropped callback (the drag-and-drop ROM/SAV/project load).
// The DGL side (DGL_USE_FILE_DROP) is auto-enabled by USE_FILE_BROWSER (see dgl/Base.hpp), so no extra
// dpf_add_plugin flag is needed.
#define DISTRHO_UI_FILE_DROP           1

#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  1
#define DISTRHO_PLUGIN_WANT_LATENCY     1
#define DISTRHO_PLUGIN_WANT_PROGRAMS    0

// In-process editor↔DSP access: enables UI::getPluginInstancePointer() so the editor reaches the shared
// host. All plugin formats (clap/vst3/jack) link DSP+UI in one binary.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

// The canonical RetroPlug 4-char ids (VST3/AU); DAW projects match on UNIQUE_ID.
// BRAND_ID is the manufacturer code (tommitytom); UNIQUE_ID is the product.
#define DISTRHO_PLUGIN_BRAND_ID  Tmtt
#define DISTRHO_PLUGIN_UNIQUE_ID RPlg

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
