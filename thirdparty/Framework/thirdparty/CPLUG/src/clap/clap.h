/*
 * CLAP - CLever Audio Plugin
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (c) 2014...2022 Alexandre BIQUE <bique.alexandre@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

// private/macros.h
// Define CLAP_EXPORT
#if !defined(CLAP_EXPORT)
#   if defined _WIN32 || defined __CYGWIN__
#      ifdef __GNUC__
#         define CLAP_EXPORT __attribute__((dllexport))
#      else
#         define CLAP_EXPORT __declspec(dllexport)
#      endif
#   else
#      if __GNUC__ >= 4 || defined(__clang__)
#         define CLAP_EXPORT __attribute__((visibility("default")))
#      else
#         define CLAP_EXPORT
#      endif
#   endif
#endif

#if !defined(CLAP_ABI)
#   if defined _WIN32 || defined __CYGWIN__
#      define CLAP_ABI __cdecl
#   else
#      define CLAP_ABI
#   endif
#endif

#if defined(_MSVC_LANG)
#   define CLAP_CPLUSPLUS _MSVC_LANG
#elif defined(__cplusplus)
#   define CLAP_CPLUSPLUS __cplusplus
#endif

#if defined(CLAP_CPLUSPLUS) && CLAP_CPLUSPLUS >= 201103L
#   define CLAP_HAS_CXX11
#   define CLAP_CONSTEXPR constexpr
#else
#   define CLAP_CONSTEXPR
#endif

#if defined(CLAP_CPLUSPLUS) && CLAP_CPLUSPLUS >= 201703L
#   define CLAP_HAS_CXX17
#   define CLAP_NODISCARD [[nodiscard]]
#else
#   define CLAP_NODISCARD
#endif

#if defined(CLAP_CPLUSPLUS) && CLAP_CPLUSPLUS >= 202002L
#   define CLAP_HAS_CXX20
#endif

// private/std.h
#ifdef CLAP_HAS_CXX11
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#ifdef __cplusplus
#   include <cstddef>
#else
#   include <stddef.h>
#   include <stdbool.h>
#endif

// version.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_version {
   // This is the major ABI and API design
   // Version 0.X.Y correspond to the development stage, API and ABI are not stable
   // Version 1.X.Y correspond to the release stage, API and ABI are stable
   uint32_t major;
   uint32_t minor;
   uint32_t revision;
} clap_version_t;

#ifdef __cplusplus
}
#endif

#define CLAP_VERSION_MAJOR 1
#define CLAP_VERSION_MINOR 1
#define CLAP_VERSION_REVISION 10

#define CLAP_VERSION_INIT                                                                          \
   { (uint32_t)CLAP_VERSION_MAJOR, (uint32_t)CLAP_VERSION_MINOR, (uint32_t)CLAP_VERSION_REVISION }

#define CLAP_VERSION_LT(maj,min,rev) ((CLAP_VERSION_MAJOR < (maj)) || \
                    ((maj) == CLAP_VERSION_MAJOR && CLAP_VERSION_MINOR < (min)) || \
                    ((maj) == CLAP_VERSION_MAJOR && (min) == CLAP_VERSION_MINOR && CLAP_VERSION_REVISION < (rev)))
#define CLAP_VERSION_EQ(maj,min,rev) (((maj) == CLAP_VERSION_MAJOR) && ((min) == CLAP_VERSION_MINOR) && ((rev) == CLAP_VERSION_REVISION))
#define CLAP_VERSION_GE(maj,min,rev) (!CLAP_VERSION_LT(maj,min,rev))

static const CLAP_CONSTEXPR clap_version_t CLAP_VERSION = CLAP_VERSION_INIT;

CLAP_NODISCARD static inline CLAP_CONSTEXPR bool
clap_version_is_compatible(const clap_version_t v) {
   // versions 0.x.y were used during development stage and aren't compatible
   return v.major >= 1;
}

// entry.h
#ifdef __cplusplus
extern "C" {
#endif

// This interface is the entry point of the dynamic library.
//
// CLAP plugins standard search path:
//
// Linux
//   - ~/.clap
//   - /usr/lib/clap
//
// Windows
//   - %COMMONPROGRAMFILES%\CLAP
//   - %LOCALAPPDATA%\Programs\Common\CLAP
//
// MacOS
//   - /Library/Audio/Plug-Ins/CLAP
//   - ~/Library/Audio/Plug-Ins/CLAP
//
// In addition to the OS-specific default locations above, a CLAP host must query the environment
// for a CLAP_PATH variable, which is a list of directories formatted in the same manner as the host
// OS binary search path (PATH on Unix, separated by `:` and Path on Windows, separated by ';', as
// of this writing).
//
// Each directory should be recursively searched for files and/or bundles as appropriate in your OS
// ending with the extension `.clap`.
//
// Every method must be thread-safe.
typedef struct clap_plugin_entry {
   clap_version_t clap_version; // initialized to CLAP_VERSION

   // This function must be called first, and can only be called once.
   //
   // It should be as fast as possible, in order to perform a very quick scan of the plugin
   // descriptors.
   //
   // It is forbidden to display graphical user interface in this call.
   // It is forbidden to perform user interaction in this call.
   //
   // If the initialization depends upon expensive computation, maybe try to do them ahead of time
   // and cache the result.
   //
   // If init() returns false, then the host must not call deinit() nor any other clap
   // related symbols from the DSO.
   //
   // plugin_path is the path to the DSO (Linux, Windows), or the bundle (macOS).
   bool(CLAP_ABI *init)(const char *plugin_path);

   // No more calls into the DSO must be made after calling deinit().
   void(CLAP_ABI *deinit)(void);

   // Get the pointer to a factory. See factory/plugin-factory.h for an example.
   //
   // Returns null if the factory is not provided.
   // The returned pointer must *not* be freed by the caller.
   const void *(CLAP_ABI *get_factory)(const char *factory_id);
} clap_plugin_entry_t;

/* Entry point */
CLAP_EXPORT extern const clap_plugin_entry_t clap_entry;

#ifdef __cplusplus
}
#endif

// host.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_host {
   clap_version_t clap_version; // initialized to CLAP_VERSION

   void *host_data; // reserved pointer for the host

   // name and version are mandatory.
   const char *name;    // eg: "Bitwig Studio"
   const char *vendor;  // eg: "Bitwig GmbH"
   const char *url;     // eg: "https://bitwig.com"
   const char *version; // eg: "4.3", see plugin.h for advice on how to format the version

   // Query an extension.
   // The returned pointer is owned by the host.
   // It is forbidden to call it before plugin->init().
   // You can call it within plugin->init() call, and after.
   // [thread-safe]
   const void *(CLAP_ABI *get_extension)(const struct clap_host *host, const char *extension_id);

   // Request the host to deactivate and then reactivate the plugin.
   // The operation may be delayed by the host.
   // [thread-safe]
   void(CLAP_ABI *request_restart)(const struct clap_host *host);

   // Request the host to activate and start processing the plugin.
   // This is useful if you have external IO and need to wake up the plugin from "sleep".
   // [thread-safe]
   void(CLAP_ABI *request_process)(const struct clap_host *host);

   // Request the host to schedule a call to plugin->on_main_thread(plugin) on the main thread.
   // [thread-safe]
   void(CLAP_ABI *request_callback)(const struct clap_host *host);
} clap_host_t;

#ifdef __cplusplus
}
#endif

// fixedpoint.h
/// We use fixed point representation of beat time and seconds time
/// Usage:
///   double x = ...; // in beats
///   clap_beattime y = round(CLAP_BEATTIME_FACTOR * x);

// This will never change
static const CLAP_CONSTEXPR int64_t CLAP_BEATTIME_FACTOR = 1LL << 31;
static const CLAP_CONSTEXPR int64_t CLAP_SECTIME_FACTOR = 1LL << 31;

typedef int64_t clap_beattime;
typedef int64_t clap_sectime;

// id.h
typedef uint32_t clap_id;

static const CLAP_CONSTEXPR clap_id CLAP_INVALID_ID = UINT32_MAX;

// events.h
#ifdef __cplusplus
extern "C" {
#endif

// event header
// must be the first attribute of the event
typedef struct clap_event_header {
   uint32_t size;     // event size including this header, eg: sizeof (clap_event_note)
   uint32_t time;     // sample offset within the buffer for this event
   uint16_t space_id; // event space, see clap_host_event_registry
   uint16_t type;     // event type
   uint32_t flags;    // see clap_event_flags
} clap_event_header_t;

// The clap core event space
static const CLAP_CONSTEXPR uint16_t CLAP_CORE_EVENT_SPACE_ID = 0;

enum clap_event_flags {
   // Indicate a live user event, for example a user turning a physical knob
   // or playing a physical key.
   CLAP_EVENT_IS_LIVE = 1 << 0,

   // Indicate that the event should not be recorded.
   // For example this is useful when a parameter changes because of a MIDI CC,
   // because if the host records both the MIDI CC automation and the parameter
   // automation there will be a conflict.
   CLAP_EVENT_DONT_RECORD = 1 << 1,
};

// Some of the following events overlap, a note on can be expressed with:
// - CLAP_EVENT_NOTE_ON
// - CLAP_EVENT_MIDI
// - CLAP_EVENT_MIDI2
//
// The preferred way of sending a note event is to use CLAP_EVENT_NOTE_*.
//
// The same event must not be sent twice: it is forbidden to send a the same note on
// encoded with both CLAP_EVENT_NOTE_ON and CLAP_EVENT_MIDI.
//
// The plugins are encouraged to be able to handle note events encoded as raw midi or midi2,
// or implement clap_plugin_event_filter and reject raw midi and midi2 events.
enum {
   // NOTE_ON and NOTE_OFF represent a key pressed and key released event, respectively.
   // A NOTE_ON with a velocity of 0 is valid and should not be interpreted as a NOTE_OFF.
   //
   // NOTE_CHOKE is meant to choke the voice(s), like in a drum machine when a closed hihat
   // chokes an open hihat. This event can be sent by the host to the plugin. Here are two use
   // cases:
   // - a plugin is inside a drum pad in Bitwig Studio's drum machine, and this pad is choked by
   //   another one
   // - the user double-clicks the DAW's stop button in the transport which then stops the sound on
   //   every track
   //
   // NOTE_END is sent by the plugin to the host. The port, channel, key and note_id are those given
   // by the host in the NOTE_ON event. In other words, this event is matched against the
   // plugin's note input port.
   // NOTE_END is useful to help the host to match the plugin's voice life time.
   //
   // When using polyphonic modulations, the host has to allocate and release voices for its
   // polyphonic modulator. Yet only the plugin effectively knows when the host should terminate
   // a voice. NOTE_END solves that issue in a non-intrusive and cooperative way.
   //
   // CLAP assumes that the host will allocate a unique voice on NOTE_ON event for a given port,
   // channel and key. This voice will run until the plugin will instruct the host to terminate
   // it by sending a NOTE_END event.
   //
   // Consider the following sequence:
   // - process()
   //    Host->Plugin NoteOn(port:0, channel:0, key:16, time:t0)
   //    Host->Plugin NoteOn(port:0, channel:0, key:64, time:t0)
   //    Host->Plugin NoteOff(port:0, channel:0, key:16, t1)
   //    Host->Plugin NoteOff(port:0, channel:0, key:64, t1)
   //    # on t2, both notes did terminate
   //    Host->Plugin NoteOn(port:0, channel:0, key:64, t3)
   //    # Here the plugin finished processing all the frames and will tell the host
   //    # to terminate the voice on key 16 but not 64, because a note has been started at t3
   //    Plugin->Host NoteEnd(port:0, channel:0, key:16, time:ignored)
   //
   // These four events use clap_event_note.
   CLAP_EVENT_NOTE_ON = 0,
   CLAP_EVENT_NOTE_OFF = 1,
   CLAP_EVENT_NOTE_CHOKE = 2,
   CLAP_EVENT_NOTE_END = 3,

   // Represents a note expression.
   // Uses clap_event_note_expression.
   CLAP_EVENT_NOTE_EXPRESSION = 4,

   // PARAM_VALUE sets the parameter's value; uses clap_event_param_value.
   // PARAM_MOD sets the parameter's modulation amount; uses clap_event_param_mod.
   //
   // The value heard is: param_value + param_mod.
   //
   // In case of a concurrent global value/modulation versus a polyphonic one,
   // the voice should only use the polyphonic one and the polyphonic modulation
   // amount will already include the monophonic signal.
   CLAP_EVENT_PARAM_VALUE = 5,
   CLAP_EVENT_PARAM_MOD = 6,

   // Indicates that the user started or finished adjusting a knob.
   // This is not mandatory to wrap parameter changes with gesture events, but this improves
   // the user experience a lot when recording automation or overriding automation playback.
   // Uses clap_event_param_gesture.
   CLAP_EVENT_PARAM_GESTURE_BEGIN = 7,
   CLAP_EVENT_PARAM_GESTURE_END = 8,

   CLAP_EVENT_TRANSPORT = 9,   // update the transport info; clap_event_transport
   CLAP_EVENT_MIDI = 10,       // raw midi event; clap_event_midi
   CLAP_EVENT_MIDI_SYSEX = 11, // raw midi sysex event; clap_event_midi_sysex
   CLAP_EVENT_MIDI2 = 12,      // raw midi 2 event; clap_event_midi2
};

// Note on, off, end and choke events.
// In the case of note choke or end events:
// - the velocity is ignored.
// - key and channel are used to match active notes, a value of -1 matches all.
typedef struct clap_event_note {
   clap_event_header_t header;

   int32_t note_id; // -1 if unspecified, otherwise >=0
   int16_t port_index;
   int16_t channel;  // 0..15
   int16_t key;      // 0..127
   double  velocity; // 0..1
} clap_event_note_t;

enum {
   // with 0 < x <= 4, plain = 20 * log(x)
   CLAP_NOTE_EXPRESSION_VOLUME = 0,

   // pan, 0 left, 0.5 center, 1 right
   CLAP_NOTE_EXPRESSION_PAN = 1,

   // relative tuning in semitone, from -120 to +120
   CLAP_NOTE_EXPRESSION_TUNING = 2,

   // 0..1
   CLAP_NOTE_EXPRESSION_VIBRATO = 3,
   CLAP_NOTE_EXPRESSION_EXPRESSION = 4,
   CLAP_NOTE_EXPRESSION_BRIGHTNESS = 5,
   CLAP_NOTE_EXPRESSION_PRESSURE = 6,
};
typedef int32_t clap_note_expression;

typedef struct clap_event_note_expression {
   clap_event_header_t header;

   clap_note_expression expression_id;

   // target a specific note_id, port, key and channel, -1 for global
   int32_t note_id;
   int16_t port_index;
   int16_t channel;
   int16_t key;

   double value; // see expression for the range
} clap_event_note_expression_t;

typedef struct clap_event_param_value {
   clap_event_header_t header;

   // target parameter
   clap_id param_id; // @ref clap_param_info.id
   void   *cookie;   // @ref clap_param_info.cookie

   // target a specific note_id, port, key and channel, -1 for global
   int32_t note_id;
   int16_t port_index;
   int16_t channel;
   int16_t key;

   double value;
} clap_event_param_value_t;

typedef struct clap_event_param_mod {
   clap_event_header_t header;

   // target parameter
   clap_id param_id; // @ref clap_param_info.id
   void   *cookie;   // @ref clap_param_info.cookie

   // target a specific note_id, port, key and channel, -1 for global
   int32_t note_id;
   int16_t port_index;
   int16_t channel;
   int16_t key;

   double amount; // modulation amount
} clap_event_param_mod_t;

typedef struct clap_event_param_gesture {
   clap_event_header_t header;

   // target parameter
   clap_id param_id; // @ref clap_param_info.id
} clap_event_param_gesture_t;

enum clap_transport_flags {
   CLAP_TRANSPORT_HAS_TEMPO = 1 << 0,
   CLAP_TRANSPORT_HAS_BEATS_TIMELINE = 1 << 1,
   CLAP_TRANSPORT_HAS_SECONDS_TIMELINE = 1 << 2,
   CLAP_TRANSPORT_HAS_TIME_SIGNATURE = 1 << 3,
   CLAP_TRANSPORT_IS_PLAYING = 1 << 4,
   CLAP_TRANSPORT_IS_RECORDING = 1 << 5,
   CLAP_TRANSPORT_IS_LOOP_ACTIVE = 1 << 6,
   CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL = 1 << 7,
};

typedef struct clap_event_transport {
   clap_event_header_t header;

   uint32_t flags; // see clap_transport_flags

   clap_beattime song_pos_beats;   // position in beats
   clap_sectime  song_pos_seconds; // position in seconds

   double tempo;     // in bpm
   double tempo_inc; // tempo increment for each sample and until the next
                     // time info event

   clap_beattime loop_start_beats;
   clap_beattime loop_end_beats;
   clap_sectime  loop_start_seconds;
   clap_sectime  loop_end_seconds;

   clap_beattime bar_start;  // start pos of the current bar
   int32_t       bar_number; // bar at song pos 0 has the number 0

   uint16_t tsig_num;   // time signature numerator
   uint16_t tsig_denom; // time signature denominator
} clap_event_transport_t;

typedef struct clap_event_midi {
   clap_event_header_t header;

   uint16_t port_index;
   uint8_t  data[3];
} clap_event_midi_t;

typedef struct clap_event_midi_sysex {
   clap_event_header_t header;

   uint16_t       port_index;
   const uint8_t *buffer; // midi buffer
   uint32_t       size;
} clap_event_midi_sysex_t;

// While it is possible to use a series of midi2 event to send a sysex,
// prefer clap_event_midi_sysex if possible for efficiency.
typedef struct clap_event_midi2 {
   clap_event_header_t header;

   uint16_t port_index;
   uint32_t data[4];
} clap_event_midi2_t;

// Input event list. The host will deliver these sorted in sample order.
typedef struct clap_input_events {
   void *ctx; // reserved pointer for the list

   // returns the number of events in the list
   uint32_t(CLAP_ABI *size)(const struct clap_input_events *list);

   // Don't free the returned event, it belongs to the list
   const clap_event_header_t *(CLAP_ABI *get)(const struct clap_input_events *list, uint32_t index);
} clap_input_events_t;

// Output event list. The plugin must insert events in sample sorted order when inserting events
typedef struct clap_output_events {
   void *ctx; // reserved pointer for the list

   // Pushes a copy of the event
   // returns false if the event could not be pushed to the queue (out of memory?)
   bool(CLAP_ABI *try_push)(const struct clap_output_events *list,
                            const clap_event_header_t       *event);
} clap_output_events_t;

#ifdef __cplusplus
}
#endif

// audio-buffer.h
#ifdef __cplusplus
extern "C" {
#endif

// Sample code for reading a stereo buffer:
//
// bool isLeftConstant = (buffer->constant_mask & (1 << 0)) != 0;
// bool isRightConstant = (buffer->constant_mask & (1 << 1)) != 0;
//
// for (int i = 0; i < N; ++i) {
//    float l = data32[0][isLeftConstant ? 0 : i];
//    float r = data32[1][isRightConstant ? 0 : i];
// }
//
// Note: checking the constant mask is optional, and this implies that
// the buffer must be filled with the constant value.
// Rationale: if a buffer reader doesn't check the constant mask, then it may
// process garbage samples and in result, garbage samples may be transmitted
// to the audio interface with all the bad consequences it can have.
//
// The constant mask is a hint.
typedef struct clap_audio_buffer {
   // Either data32 or data64 pointer will be set.
   float  **data32;
   double **data64;
   uint32_t channel_count;
   uint32_t latency; // latency from/to the audio interface
   uint64_t constant_mask;
} clap_audio_buffer_t;

#ifdef __cplusplus
}
#endif

// process.h
#ifdef __cplusplus
extern "C" {
#endif

enum {
   // Processing failed. The output buffer must be discarded.
   CLAP_PROCESS_ERROR = 0,

   // Processing succeeded, keep processing.
   CLAP_PROCESS_CONTINUE = 1,

   // Processing succeeded, keep processing if the output is not quiet.
   CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,

   // Rely upon the plugin's tail to determine if the plugin should continue to process.
   // see clap_plugin_tail
   CLAP_PROCESS_TAIL = 3,

   // Processing succeeded, but no more processing is required,
   // until the next event or variation in audio input.
   CLAP_PROCESS_SLEEP = 4,
};
typedef int32_t clap_process_status;

typedef struct clap_process {
   // A steady sample time counter.
   // This field can be used to calculate the sleep duration between two process calls.
   // This value may be specific to this plugin instance and have no relation to what
   // other plugin instances may receive.
   //
   // Set to -1 if not available, otherwise the value must be greater or equal to 0,
   // and must be increased by at least `frames_count` for the next call to process.
   int64_t steady_time;

   // Number of frames to process
   uint32_t frames_count;

   // time info at sample 0
   // If null, then this is a free running host, no transport events will be provided
   const clap_event_transport_t *transport;

   // Audio buffers, they must have the same count as specified
   // by clap_plugin_audio_ports->count().
   // The index maps to clap_plugin_audio_ports->get().
   // Input buffer and its contents are read-only.
   const clap_audio_buffer_t *audio_inputs;
   clap_audio_buffer_t       *audio_outputs;
   uint32_t                   audio_inputs_count;
   uint32_t                   audio_outputs_count;

   // The input event list can't be modified.
   // Input read-only event list. The host will deliver these sorted in sample order.
   const clap_input_events_t  *in_events;

   // Output event list. The plugin must insert events in sample sorted order when inserting events
   const clap_output_events_t *out_events;
} clap_process_t;

#ifdef __cplusplus
}
#endif

// plugin-features.h
// This file provides a set of standard plugin features meant to be used
// within clap_plugin_descriptor.features.
//
// For practical reasons we'll avoid spaces and use `-` instead to facilitate
// scripts that generate the feature array.
//
// Non-standard features should be formatted as follow: "$namespace:$feature"

/////////////////////
// Plugin category //
/////////////////////

// Add this feature if your plugin can process note events and then produce audio
#define CLAP_PLUGIN_FEATURE_INSTRUMENT "instrument"

// Add this feature if your plugin is an audio effect
#define CLAP_PLUGIN_FEATURE_AUDIO_EFFECT "audio-effect"

// Add this feature if your plugin is a note effect or a note generator/sequencer
#define CLAP_PLUGIN_FEATURE_NOTE_EFFECT "note-effect"

// Add this feature if your plugin converts audio to notes
#define CLAP_PLUGIN_FEATURE_NOTE_DETECTOR "note-detector"

// Add this feature if your plugin is an analyzer
#define CLAP_PLUGIN_FEATURE_ANALYZER "analyzer"

/////////////////////////
// Plugin sub-category //
/////////////////////////

#define CLAP_PLUGIN_FEATURE_SYNTHESIZER "synthesizer"
#define CLAP_PLUGIN_FEATURE_SAMPLER "sampler"
#define CLAP_PLUGIN_FEATURE_DRUM "drum" // For single drum
#define CLAP_PLUGIN_FEATURE_DRUM_MACHINE "drum-machine"

#define CLAP_PLUGIN_FEATURE_FILTER "filter"
#define CLAP_PLUGIN_FEATURE_PHASER "phaser"
#define CLAP_PLUGIN_FEATURE_EQUALIZER "equalizer"
#define CLAP_PLUGIN_FEATURE_DEESSER "de-esser"
#define CLAP_PLUGIN_FEATURE_PHASE_VOCODER "phase-vocoder"
#define CLAP_PLUGIN_FEATURE_GRANULAR "granular"
#define CLAP_PLUGIN_FEATURE_FREQUENCY_SHIFTER "frequency-shifter"
#define CLAP_PLUGIN_FEATURE_PITCH_SHIFTER "pitch-shifter"

#define CLAP_PLUGIN_FEATURE_DISTORTION "distortion"
#define CLAP_PLUGIN_FEATURE_TRANSIENT_SHAPER "transient-shaper"
#define CLAP_PLUGIN_FEATURE_COMPRESSOR "compressor"
#define CLAP_PLUGIN_FEATURE_EXPANDER "expander"
#define CLAP_PLUGIN_FEATURE_GATE "gate"
#define CLAP_PLUGIN_FEATURE_LIMITER "limiter"

#define CLAP_PLUGIN_FEATURE_FLANGER "flanger"
#define CLAP_PLUGIN_FEATURE_CHORUS "chorus"
#define CLAP_PLUGIN_FEATURE_DELAY "delay"
#define CLAP_PLUGIN_FEATURE_REVERB "reverb"

#define CLAP_PLUGIN_FEATURE_TREMOLO "tremolo"
#define CLAP_PLUGIN_FEATURE_GLITCH "glitch"

#define CLAP_PLUGIN_FEATURE_UTILITY "utility"
#define CLAP_PLUGIN_FEATURE_PITCH_CORRECTION "pitch-correction"
#define CLAP_PLUGIN_FEATURE_RESTORATION "restoration" // repair the sound

#define CLAP_PLUGIN_FEATURE_MULTI_EFFECTS "multi-effects"

#define CLAP_PLUGIN_FEATURE_MIXING "mixing"
#define CLAP_PLUGIN_FEATURE_MASTERING "mastering"

////////////////////////
// Audio Capabilities //
////////////////////////

#define CLAP_PLUGIN_FEATURE_MONO "mono"
#define CLAP_PLUGIN_FEATURE_STEREO "stereo"
#define CLAP_PLUGIN_FEATURE_SURROUND "surround"
#define CLAP_PLUGIN_FEATURE_AMBISONIC "ambisonic"

// plugin.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_descriptor {
   clap_version_t clap_version; // initialized to CLAP_VERSION

   // Mandatory fields must be set and must not be blank.
   // Otherwise the fields can be null or blank, though it is safer to make them blank.
   //
   // Some indications regarding id and version
   // - id is an arbitrary string which should be unique to your plugin,
   //   we encourage you to use a reverse URI eg: "com.u-he.diva"
   // - version is an arbitrary string which describes a plugin,
   //   it is useful for the host to understand and be able to compare two different
   //   version strings, so here is a regex like expression which is likely to be
   //   understood by most hosts: MAJOR(.MINOR(.REVISION)?)?( (Alpha|Beta) XREV)?
   const char *id;          // eg: "com.u-he.diva", mandatory
   const char *name;        // eg: "Diva", mandatory
   const char *vendor;      // eg: "u-he"
   const char *url;         // eg: "https://u-he.com/products/diva/"
   const char *manual_url;  // eg: "https://dl.u-he.com/manuals/plugins/diva/Diva-user-guide.pdf"
   const char *support_url; // eg: "https://u-he.com/support/"
   const char *version;     // eg: "1.4.4"
   const char *description; // eg: "The spirit of analogue"

   // Arbitrary list of keywords.
   // They can be matched by the host indexer and used to classify the plugin.
   // The array of pointers must be null terminated.
   // For some standard features see plugin-features.h
   const char *const *features;
} clap_plugin_descriptor_t;

typedef struct clap_plugin {
   const clap_plugin_descriptor_t *desc;

   void *plugin_data; // reserved pointer for the plugin

   // Must be called after creating the plugin.
   // If init returns false, the host must destroy the plugin instance.
   // If init returns true, then the plugin is initialized and in the deactivated state.
   // [main-thread]
   bool(CLAP_ABI *init)(const struct clap_plugin *plugin);

   // Free the plugin and its resources.
   // It is required to deactivate the plugin prior to this call.
   // [main-thread & !active]
   void(CLAP_ABI *destroy)(const struct clap_plugin *plugin);

   // Activate and deactivate the plugin.
   // In this call the plugin may allocate memory and prepare everything needed for the process
   // call. The process's sample rate will be constant and process's frame count will included in
   // the [min, max] range, which is bounded by [1, INT32_MAX].
   // Once activated the latency and port configuration must remain constant, until deactivation.
   // Returns true on success.
   // [main-thread & !active_state]
   bool(CLAP_ABI *activate)(const struct clap_plugin *plugin,
                            double                    sample_rate,
                            uint32_t                  min_frames_count,
                            uint32_t                  max_frames_count);
   // [main-thread & active_state]
   void(CLAP_ABI *deactivate)(const struct clap_plugin *plugin);

   // Call start processing before processing.
   // Returns true on success.
   // [audio-thread & active_state & !processing_state]
   bool(CLAP_ABI *start_processing)(const struct clap_plugin *plugin);

   // Call stop processing before sending the plugin to sleep.
   // [audio-thread & active_state & processing_state]
   void(CLAP_ABI *stop_processing)(const struct clap_plugin *plugin);

   // - Clears all buffers, performs a full reset of the processing state (filters, oscillators,
   //   envelopes, lfo, ...) and kills all voices.
   // - The parameter's value remain unchanged.
   // - clap_process.steady_time may jump backward.
   //
   // [audio-thread & active_state]
   void(CLAP_ABI *reset)(const struct clap_plugin *plugin);

   // process audio, events, ...
   // All the pointers coming from clap_process_t and its nested attributes,
   // are valid until process() returns.
   // [audio-thread & active_state & processing_state]
   clap_process_status(CLAP_ABI *process)(const struct clap_plugin *plugin,
                                          const clap_process_t     *process);

   // Query an extension.
   // The returned pointer is owned by the plugin.
   // It is forbidden to call it before plugin->init().
   // You can call it within plugin->init() call, and after.
   // [thread-safe]
   const void *(CLAP_ABI *get_extension)(const struct clap_plugin *plugin, const char *id);

   // Called by the host on the main thread in response to a previous call to:
   //   host->request_callback(host);
   // [main-thread]
   void(CLAP_ABI *on_main_thread)(const struct clap_plugin *plugin);
} clap_plugin_t;

#ifdef __cplusplus
}
#endif

// factory/plugin-factory.h
// Use it to retrieve const clap_plugin_factory_t* from
// clap_plugin_entry.get_factory()
static const CLAP_CONSTEXPR char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";

#ifdef __cplusplus
extern "C" {
#endif

// Every method must be thread-safe.
// It is very important to be able to scan the plugin as quickly as possible.
//
// The host may use clap_plugin_invalidation_factory to detect filesystem changes
// which may change the factory's content.
typedef struct clap_plugin_factory {
   // Get the number of plugins available.
   // [thread-safe]
   uint32_t(CLAP_ABI *get_plugin_count)(const struct clap_plugin_factory *factory);

   // Retrieves a plugin descriptor by its index.
   // Returns null in case of error.
   // The descriptor must not be freed.
   // [thread-safe]
   const clap_plugin_descriptor_t *(CLAP_ABI *get_plugin_descriptor)(
      const struct clap_plugin_factory *factory, uint32_t index);

   // Create a clap_plugin by its plugin_id.
   // The returned pointer must be freed by calling plugin->destroy(plugin);
   // The plugin is not allowed to use the host callbacks in the create method.
   // Returns null in case of error.
   // [thread-safe]
   const clap_plugin_t *(CLAP_ABI *create_plugin)(const struct clap_plugin_factory *factory,
                                                  const clap_host_t                *host,
                                                  const char                       *plugin_id);
} clap_plugin_factory_t;

#ifdef __cplusplus
}
#endif

// string-sizes.h
#ifdef __cplusplus
extern "C" {
#endif

enum {
   // String capacity for names that can be displayed to the user.
   CLAP_NAME_SIZE = 256,

   // String capacity for describing a path, like a parameter in a module hierarchy or path within a
   // set of nested track groups.
   //
   // This is not suited for describing a file path on the disk, as NTFS allows up to 32K long
   // paths.
   CLAP_PATH_SIZE = 1024,
};

#ifdef __cplusplus
}
#endif

// color.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_color {
   uint8_t alpha;
   uint8_t red;
   uint8_t green;
   uint8_t blue;
} clap_color_t;

#ifdef __cplusplus
}
#endif


// ext/audio-ports.h
/// @page Audio Ports
///
/// This extension provides a way for the plugin to describe its current audio ports.
///
/// If the plugin does not implement this extension, it won't have audio ports.
///
/// 32 bits support is required for both host and plugins. 64 bits audio is optional.
///
/// The plugin is only allowed to change its ports configuration while it is deactivated.

static CLAP_CONSTEXPR const char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
static CLAP_CONSTEXPR const char CLAP_PORT_MONO[] = "mono";
static CLAP_CONSTEXPR const char CLAP_PORT_STEREO[] = "stereo";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   // This port is the main audio input or output.
   // There can be only one main input and main output.
   // Main port must be at index 0.
   CLAP_AUDIO_PORT_IS_MAIN = 1 << 0,

   // This port can be used with 64 bits audio
   CLAP_AUDIO_PORT_SUPPORTS_64BITS = 1 << 1,

   // 64 bits audio is preferred with this port
   CLAP_AUDIO_PORT_PREFERS_64BITS = 1 << 2,

   // This port must be used with the same sample size as all the other ports which have this flag.
   // In other words if all ports have this flag then the plugin may either be used entirely with
   // 64 bits audio or 32 bits audio, but it can't be mixed.
   CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE = 1 << 3,
};

typedef struct clap_audio_port_info {
   // id identifies a port and must be stable.
   // id may overlap between input and output ports.
   clap_id id;
   char    name[CLAP_NAME_SIZE]; // displayable name

   uint32_t flags;
   uint32_t channel_count;

   // If null or empty then it is unspecified (arbitrary audio).
   // This field can be compared against:
   // - CLAP_PORT_MONO
   // - CLAP_PORT_STEREO
   // - CLAP_PORT_SURROUND (defined in the surround extension)
   // - CLAP_PORT_AMBISONIC (defined in the ambisonic extension)
   // - CLAP_PORT_CV (defined in the cv extension)
   //
   // An extension can provide its own port type and way to inspect the channels.
   const char *port_type;

   // in-place processing: allow the host to use the same buffer for input and output
   // if supported set the pair port id.
   // if not supported set to CLAP_INVALID_ID
   clap_id in_place_pair;
} clap_audio_port_info_t;

// The audio ports scan has to be done while the plugin is deactivated.
typedef struct clap_plugin_audio_ports {
   // Number of ports, for either input or output
   // [main-thread]
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);

   // Get info about an audio port.
   // Returns true on success and stores the result into info.
   // [main-thread]
   bool(CLAP_ABI *get)(const clap_plugin_t    *plugin,
                       uint32_t                index,
                       bool                    is_input,
                       clap_audio_port_info_t *info);
} clap_plugin_audio_ports_t;

enum {
   // The ports name did change, the host can scan them right away.
   CLAP_AUDIO_PORTS_RESCAN_NAMES = 1 << 0,

   // [!active] The flags did change
   CLAP_AUDIO_PORTS_RESCAN_FLAGS = 1 << 1,

   // [!active] The channel_count did change
   CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT = 1 << 2,

   // [!active] The port type did change
   CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE = 1 << 3,

   // [!active] The in-place pair did change, this requires.
   CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR = 1 << 4,

   // [!active] The list of ports have changed: entries have been removed/added.
   CLAP_AUDIO_PORTS_RESCAN_LIST = 1 << 5,
};

typedef struct clap_host_audio_ports {
   // Checks if the host allows a plugin to change a given aspect of the audio ports definition.
   // [main-thread]
   bool(CLAP_ABI *is_rescan_flag_supported)(const clap_host_t *host, uint32_t flag);

   // Rescan the full list of audio ports according to the flags.
   // It is illegal to ask the host to rescan with a flag that is not supported.
   // Certain flags require the plugin to be de-activated.
   // [main-thread]
   void(CLAP_ABI *rescan)(const clap_host_t *host, uint32_t flags);
} clap_host_audio_ports_t;

#ifdef __cplusplus
}
#endif

// ext/audio-ports-config.h
/// @page Audio Ports Config
///
/// This extension let the plugin provide port configurations presets.
/// For example mono, stereo, surround, ambisonic, ...
///
/// After the plugin initialization, the host may scan the list of configurations and eventually
/// select one that fits the plugin context. The host can only select a configuration if the plugin
/// is deactivated.
///
/// A configuration is a very simple description of the audio ports:
/// - it describes the main input and output ports
/// - it has a name that can be displayed to the user
///
/// The idea behind the configurations, is to let the user choose one via a menu.
///
/// Plugins with very complex configuration possibilities should let the user configure the ports
/// from the plugin GUI, and call @ref clap_host_audio_ports.rescan(CLAP_AUDIO_PORTS_RESCAN_ALL).
///
/// To inquire the exact bus layout, the plugin implements the clap_plugin_audio_ports_config_info_t
/// extension where all busses can be retrieved in the same way as in the audio-port extension.

static CLAP_CONSTEXPR const char CLAP_EXT_AUDIO_PORTS_CONFIG[] = "clap.audio-ports-config";
static CLAP_CONSTEXPR const char CLAP_EXT_AUDIO_PORTS_CONFIG_INFO[] =
   "clap.audio-ports-config-info/draft-0";

#ifdef __cplusplus
extern "C" {
#endif

// Minimalistic description of ports configuration
typedef struct clap_audio_ports_config {
   clap_id id;
   char    name[CLAP_NAME_SIZE];

   uint32_t input_port_count;
   uint32_t output_port_count;

   // main input info
   bool        has_main_input;
   uint32_t    main_input_channel_count;
   const char *main_input_port_type;

   // main output info
   bool        has_main_output;
   uint32_t    main_output_channel_count;
   const char *main_output_port_type;
} clap_audio_ports_config_t;

// The audio ports config scan has to be done while the plugin is deactivated.
typedef struct clap_plugin_audio_ports_config {
   // Gets the number of available configurations
   // [main-thread]
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin);

   // Gets information about a configuration
   // Returns true on success and stores the result into config.
   // [main-thread]
   bool(CLAP_ABI *get)(const clap_plugin_t       *plugin,
                       uint32_t                   index,
                       clap_audio_ports_config_t *config);

   // Selects the configuration designated by id
   // Returns true if the configuration could be applied.
   // Once applied the host should scan again the audio ports.
   // [main-thread & plugin-deactivated]
   bool(CLAP_ABI *select)(const clap_plugin_t *plugin, clap_id config_id);
} clap_plugin_audio_ports_config_t;

// Extended config info
typedef struct clap_plugin_audio_ports_config_info {

   // Gets the id of the currently selected config, or CLAP_INVALID_ID if the current port
   // layout isn't part of the config list.
   //
   // [main-thread]
   clap_id(CLAP_ABI *current_config)(const clap_plugin_t *plugin);

   // Get info about an audio port, for a given config_id.
   // This is analogous to clap_plugin_audio_ports.get().
   // Returns true on success and stores the result into info.
   // [main-thread]
   bool(CLAP_ABI *get)(const clap_plugin_t    *plugin,
                       clap_id                 config_id,
                       uint32_t                port_index,
                       bool                    is_input,
                       clap_audio_port_info_t *info);
} clap_plugin_audio_ports_config_info_t;

typedef struct clap_host_audio_ports_config {
   // Rescan the full list of configs.
   // [main-thread]
   void(CLAP_ABI *rescan)(const clap_host_t *host);
} clap_host_audio_ports_config_t;

#ifdef __cplusplus
}
#endif

// ext/event-registry.h
static CLAP_CONSTEXPR const char CLAP_EXT_EVENT_REGISTRY[] = "clap.event-registry";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_host_event_registry {
   // Queries an event space id.
   // The space id 0 is reserved for CLAP's core events. See CLAP_CORE_EVENT_SPACE.
   //
   // Return false and sets *space_id to UINT16_MAX if the space name is unknown to the host.
   // [main-thread]
   bool(CLAP_ABI *query)(const clap_host_t *host, const char *space_name, uint16_t *space_id);
} clap_host_event_registry_t;

#ifdef __cplusplus
}
#endif

// ext/gui.h
/// @page GUI
///
/// This extension defines how the plugin will present its GUI.
///
/// There are two approaches:
/// 1. the plugin creates a window and embeds it into the host's window
/// 2. the plugin creates a floating window
///
/// Embedding the window gives more control to the host, and feels more integrated.
/// Floating window are sometimes the only option due to technical limitations.
///
/// Showing the GUI works as follow:
///  1. clap_plugin_gui->is_api_supported(), check what can work
///  2. clap_plugin_gui->create(), allocates gui resources
///  3. if the plugin window is floating
///  4.    -> clap_plugin_gui->set_transient()
///  5.    -> clap_plugin_gui->suggest_title()
///  6. else
///  7.    -> clap_plugin_gui->set_scale()
///  8.    -> clap_plugin_gui->can_resize()
///  9.    -> if resizable and has known size from previous session, clap_plugin_gui->set_size()
/// 10.    -> else clap_plugin_gui->get_size(), gets initial size
/// 11.    -> clap_plugin_gui->set_parent()
/// 12. clap_plugin_gui->show()
/// 13. clap_plugin_gui->hide()/show() ...
/// 14. clap_plugin_gui->destroy() when done with the gui
///
/// Resizing the window (initiated by the plugin, if embedded):
/// 1. Plugins calls clap_host_gui->request_resize()
/// 2. If the host returns true the new size is accepted,
///    the host doesn't have to call clap_plugin_gui->set_size().
///    If the host returns false, the new size is rejected.
///
/// Resizing the window (drag, if embedded)):
/// 1. Only possible if clap_plugin_gui->can_resize() returns true
/// 2. Mouse drag -> new_size
/// 3. clap_plugin_gui->adjust_size(new_size) -> working_size
/// 4. clap_plugin_gui->set_size(working_size)

static CLAP_CONSTEXPR const char CLAP_EXT_GUI[] = "clap.gui";

// If your windowing API is not listed here, please open an issue and we'll figure it out.
// https://github.com/free-audio/clap/issues/new

// uses physical size
// embed using https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent
static const CLAP_CONSTEXPR char CLAP_WINDOW_API_WIN32[] = "win32";

// uses logical size, don't call clap_plugin_gui->set_scale()
static const CLAP_CONSTEXPR char CLAP_WINDOW_API_COCOA[] = "cocoa";

// uses physical size
// embed using https://specifications.freedesktop.org/xembed-spec/xembed-spec-latest.html
static const CLAP_CONSTEXPR char CLAP_WINDOW_API_X11[] = "x11";

// uses physical size
// embed is currently not supported, use floating windows
static const CLAP_CONSTEXPR char CLAP_WINDOW_API_WAYLAND[] = "wayland";

#ifdef __cplusplus
extern "C" {
#endif

typedef void         *clap_hwnd;
typedef void         *clap_nsview;
typedef unsigned long clap_xwnd;

// Represent a window reference.
typedef struct clap_window {
   const char *api; // one of CLAP_WINDOW_API_XXX
   union {
      clap_nsview cocoa;
      clap_xwnd   x11;
      clap_hwnd   win32;
      void       *ptr; // for anything defined outside of clap
   };
} clap_window_t;

// Information to improve window resizing when initiated by the host or window manager.
typedef struct clap_gui_resize_hints {
   bool can_resize_horizontally;
   bool can_resize_vertically;

   // only if can resize horizontally and vertically
   bool     preserve_aspect_ratio;
   uint32_t aspect_ratio_width;
   uint32_t aspect_ratio_height;
} clap_gui_resize_hints_t;

// Size (width, height) is in pixels; the corresponding windowing system extension is
// responsible for defining if it is physical pixels or logical pixels.
typedef struct clap_plugin_gui {
   // Returns true if the requested gui api is supported
   // [main-thread]
   bool(CLAP_ABI *is_api_supported)(const clap_plugin_t *plugin, const char *api, bool is_floating);

   // Returns true if the plugin has a preferred api.
   // The host has no obligation to honor the plugin preference, this is just a hint.
   // The const char **api variable should be explicitly assigned as a pointer to
   // one of the CLAP_WINDOW_API_ constants defined above, not strcopied.
   // [main-thread]
   bool(CLAP_ABI *get_preferred_api)(const clap_plugin_t *plugin,
                                     const char         **api,
                                     bool                *is_floating);

   // Create and allocate all resources necessary for the gui.
   //
   // If is_floating is true, then the window will not be managed by the host. The plugin
   // can set its window to stays above the parent window, see set_transient().
   // api may be null or blank for floating window.
   //
   // If is_floating is false, then the plugin has to embed its window into the parent window, see
   // set_parent().
   //
   // After this call, the GUI may not be visible yet; don't forget to call show().
   //
   // Returns true if the GUI is successfully created.
   // [main-thread]
   bool(CLAP_ABI *create)(const clap_plugin_t *plugin, const char *api, bool is_floating);

   // Free all resources associated with the gui.
   // [main-thread]
   void(CLAP_ABI *destroy)(const clap_plugin_t *plugin);

   // Set the absolute GUI scaling factor, and override any OS info.
   // Should not be used if the windowing api relies upon logical pixels.
   //
   // If the plugin prefers to work out the scaling factor itself by querying the OS directly,
   // then ignore the call.
   //
   // scale = 2 means 200% scaling.
   //
   // Returns true if the scaling could be applied
   // Returns false if the call was ignored, or the scaling could not be applied.
   // [main-thread]
   bool(CLAP_ABI *set_scale)(const clap_plugin_t *plugin, double scale);

   // Get the current size of the plugin UI.
   // clap_plugin_gui->create() must have been called prior to asking the size.
   //
   // Returns true if the plugin could get the size.
   // [main-thread]
   bool(CLAP_ABI *get_size)(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);

   // Returns true if the window is resizeable (mouse drag).
   // [main-thread & !floating]
   bool(CLAP_ABI *can_resize)(const clap_plugin_t *plugin);

   // Returns true if the plugin can provide hints on how to resize the window.
   // [main-thread & !floating]
   bool(CLAP_ABI *get_resize_hints)(const clap_plugin_t *plugin, clap_gui_resize_hints_t *hints);

   // If the plugin gui is resizable, then the plugin will calculate the closest
   // usable size which fits in the given size.
   // This method does not change the size.
   //
   // Returns true if the plugin could adjust the given size.
   // [main-thread & !floating]
   bool(CLAP_ABI *adjust_size)(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height);

   // Sets the window size.
   //
   // Returns true if the plugin could resize its window to the given size.
   // [main-thread & !floating]
   bool(CLAP_ABI *set_size)(const clap_plugin_t *plugin, uint32_t width, uint32_t height);

   // Embeds the plugin window into the given window.
   //
   // Returns true on success.
   // [main-thread & !floating]
   bool(CLAP_ABI *set_parent)(const clap_plugin_t *plugin, const clap_window_t *window);

   // Set the plugin floating window to stay above the given window.
   //
   // Returns true on success.
   // [main-thread & floating]
   bool(CLAP_ABI *set_transient)(const clap_plugin_t *plugin, const clap_window_t *window);

   // Suggests a window title. Only for floating windows.
   //
   // [main-thread & floating]
   void(CLAP_ABI *suggest_title)(const clap_plugin_t *plugin, const char *title);

   // Show the window.
   //
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *show)(const clap_plugin_t *plugin);

   // Hide the window, this method does not free the resources, it just hides
   // the window content. Yet it may be a good idea to stop painting timers.
   //
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *hide)(const clap_plugin_t *plugin);
} clap_plugin_gui_t;

typedef struct clap_host_gui {
   // The host should call get_resize_hints() again.
   // [thread-safe & !floating]
   void(CLAP_ABI *resize_hints_changed)(const clap_host_t *host);

   // Request the host to resize the client area to width, height.
   // Return true if the new size is accepted, false otherwise.
   // The host doesn't have to call set_size().
   //
   // Note: if not called from the main thread, then a return value simply means that the host
   // acknowledged the request and will process it asynchronously. If the request then can't be
   // satisfied then the host will call set_size() to revert the operation.
   // [thread-safe & !floating]
   bool(CLAP_ABI *request_resize)(const clap_host_t *host, uint32_t width, uint32_t height);

   // Request the host to show the plugin gui.
   // Return true on success, false otherwise.
   // [thread-safe]
   bool(CLAP_ABI *request_show)(const clap_host_t *host);

   // Request the host to hide the plugin gui.
   // Return true on success, false otherwise.
   // [thread-safe]
   bool(CLAP_ABI *request_hide)(const clap_host_t *host);

   // The floating window has been closed, or the connection to the gui has been lost.
   //
   // If was_destroyed is true, then the host must call clap_plugin_gui->destroy() to acknowledge
   // the gui destruction.
   // [thread-safe]
   void(CLAP_ABI *closed)(const clap_host_t *host, bool was_destroyed);
} clap_host_gui_t;

#ifdef __cplusplus
}
#endif

// ext/latency.h
static CLAP_CONSTEXPR const char CLAP_EXT_LATENCY[] = "clap.latency";

#ifdef __cplusplus
extern "C" {
#endif

// The audio ports scan has to be done while the plugin is deactivated.
typedef struct clap_plugin_latency {
   // Returns the plugin latency in samples.
   // [main-thread]
   uint32_t(CLAP_ABI *get)(const clap_plugin_t *plugin);
} clap_plugin_latency_t;

typedef struct clap_host_latency {
   // Tell the host that the latency changed.
   // The latency is only allowed to change if the plugin is deactivated.
   // If the plugin is activated, call host->request_restart()
   // [main-thread]
   void(CLAP_ABI *changed)(const clap_host_t *host);
} clap_host_latency_t;

#ifdef __cplusplus
}
#endif

// ext/log.h
static CLAP_CONSTEXPR const char CLAP_EXT_LOG[] = "clap.log";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   CLAP_LOG_DEBUG = 0,
   CLAP_LOG_INFO = 1,
   CLAP_LOG_WARNING = 2,
   CLAP_LOG_ERROR = 3,
   CLAP_LOG_FATAL = 4,

   // These severities should be used to report misbehaviour.
   // The plugin one can be used by a layer between the plugin and the host.
   CLAP_LOG_HOST_MISBEHAVING = 5,
   CLAP_LOG_PLUGIN_MISBEHAVING = 6,
};
typedef int32_t clap_log_severity;

typedef struct clap_host_log {
   // Log a message through the host.
   // [thread-safe]
   void(CLAP_ABI *log)(const clap_host_t *host, clap_log_severity severity, const char *msg);
} clap_host_log_t;

#ifdef __cplusplus
}
#endif

// ext/note-name.h
#ifdef __cplusplus
extern "C" {
#endif

static CLAP_CONSTEXPR const char CLAP_EXT_NOTE_NAME[] = "clap.note-name";

typedef struct clap_note_name {
   char    name[CLAP_NAME_SIZE];
   int16_t port;    // -1 for every port
   int16_t key;     // -1 for every key
   int16_t channel; // -1 for every channel
} clap_note_name_t;

typedef struct clap_plugin_note_name {
   // Return the number of note names
   // [main-thread]
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin);

   // Returns true on success and stores the result into note_name
   // [main-thread]
   bool(CLAP_ABI *get)(const clap_plugin_t *plugin, uint32_t index, clap_note_name_t *note_name);
} clap_plugin_note_name_t;

typedef struct clap_host_note_name {
   // Informs the host that the note names have changed.
   // [main-thread]
   void(CLAP_ABI *changed)(const clap_host_t *host);
} clap_host_note_name_t;

#ifdef __cplusplus
}
#endif

// ext/note-ports.h
/// @page Note Ports
///
/// This extension provides a way for the plugin to describe its current note ports.
/// If the plugin does not implement this extension, it won't have note input or output.
/// The plugin is only allowed to change its note ports configuration while it is deactivated.

static CLAP_CONSTEXPR const char CLAP_EXT_NOTE_PORTS[] = "clap.note-ports";

#ifdef __cplusplus
extern "C" {
#endif

enum clap_note_dialect {
   // Uses clap_event_note and clap_event_note_expression.
   CLAP_NOTE_DIALECT_CLAP = 1 << 0,

   // Uses clap_event_midi, no polyphonic expression
   CLAP_NOTE_DIALECT_MIDI = 1 << 1,

   // Uses clap_event_midi, with polyphonic expression (MPE)
   CLAP_NOTE_DIALECT_MIDI_MPE = 1 << 2,

   // Uses clap_event_midi2
   CLAP_NOTE_DIALECT_MIDI2 = 1 << 3,
};

typedef struct clap_note_port_info {
   // id identifies a port and must be stable.
   // id may overlap between input and output ports.
   clap_id  id;
   uint32_t supported_dialects;   // bitfield, see clap_note_dialect
   uint32_t preferred_dialect;    // one value of clap_note_dialect
   char     name[CLAP_NAME_SIZE]; // displayable name, i18n?
} clap_note_port_info_t;

// The note ports scan has to be done while the plugin is deactivated.
typedef struct clap_plugin_note_ports {
   // Number of ports, for either input or output.
   // [main-thread]
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin, bool is_input);

   // Get info about a note port.
   // Returns true on success and stores the result into info.
   // [main-thread]
   bool(CLAP_ABI *get)(const clap_plugin_t   *plugin,
                       uint32_t               index,
                       bool                   is_input,
                       clap_note_port_info_t *info);
} clap_plugin_note_ports_t;

enum {
   // The ports have changed, the host shall perform a full scan of the ports.
   // This flag can only be used if the plugin is not active.
   // If the plugin active, call host->request_restart() and then call rescan()
   // when the host calls deactivate()
   CLAP_NOTE_PORTS_RESCAN_ALL = 1 << 0,

   // The ports name did change, the host can scan them right away.
   CLAP_NOTE_PORTS_RESCAN_NAMES = 1 << 1,
};

typedef struct clap_host_note_ports {
   // Query which dialects the host supports
   // [main-thread]
   uint32_t(CLAP_ABI *supported_dialects)(const clap_host_t *host);

   // Rescan the full list of note ports according to the flags.
   // [main-thread]
   void(CLAP_ABI *rescan)(const clap_host_t *host, uint32_t flags);
} clap_host_note_ports_t;

#ifdef __cplusplus
}
#endif

// ext/params.h
/// @page Parameters
/// @brief parameters management
///
/// Main idea:
///
/// The host sees the plugin as an atomic entity; and acts as a controller on top of its parameters.
/// The plugin is responsible for keeping its audio processor and its GUI in sync.
///
/// The host can at any time read parameters' value on the [main-thread] using
/// @ref clap_plugin_params.value().
///
/// There are two options to communicate parameter value changes, and they are not concurrent.
/// - send automation points during clap_plugin.process()
/// - send automation points during clap_plugin_params.flush(), for parameter changes
///   without processing audio
///
/// When the plugin changes a parameter value, it must inform the host.
/// It will send @ref CLAP_EVENT_PARAM_VALUE event during process() or flush().
/// If the user is adjusting the value, don't forget to mark the beginning and end
/// of the gesture by sending CLAP_EVENT_PARAM_GESTURE_BEGIN and CLAP_EVENT_PARAM_GESTURE_END
/// events.
///
/// @note MIDI CCs are tricky because you may not know when the parameter adjustment ends.
/// Also if the host records incoming MIDI CC and parameter change automation at the same time,
/// there will be a conflict at playback: MIDI CC vs Automation.
/// The parameter automation will always target the same parameter because the param_id is stable.
/// The MIDI CC may have a different mapping in the future and may result in a different playback.
///
/// When a MIDI CC changes a parameter's value, set the flag CLAP_EVENT_DONT_RECORD in
/// clap_event_param.header.flags. That way the host may record the MIDI CC automation, but not the
/// parameter change and there won't be conflict at playback.
///
/// Scenarios:
///
/// I. Loading a preset
/// - load the preset in a temporary state
/// - call @ref clap_host_params.rescan() if anything changed
/// - call @ref clap_host_latency.changed() if latency changed
/// - invalidate any other info that may be cached by the host
/// - if the plugin is activated and the preset will introduce breaking changes
///   (latency, audio ports, new parameters, ...) be sure to wait for the host
///   to deactivate the plugin to apply those changes.
///   If there are no breaking changes, the plugin can apply them them right away.
///   The plugin is responsible for updating both its audio processor and its gui.
///
/// II. Turning a knob on the DAW interface
/// - the host will send an automation event to the plugin via a process() or flush()
///
/// III. Turning a knob on the Plugin interface
/// - the plugin is responsible for sending the parameter value to its audio processor
/// - call clap_host_params->request_flush() or clap_host->request_process().
/// - when the host calls either clap_plugin->process() or clap_plugin_params->flush(),
///   send an automation event and don't forget to wrap the parameter change(s)
///   with CLAP_EVENT_PARAM_GESTURE_BEGIN and CLAP_EVENT_PARAM_GESTURE_END to define the
///   beginning and end of the gesture.
///
/// IV. Turning a knob via automation
/// - host sends an automation point during clap_plugin->process() or clap_plugin_params->flush().
/// - the plugin is responsible for updating its GUI
///
/// V. Turning a knob via plugin's internal MIDI mapping
/// - the plugin sends a CLAP_EVENT_PARAM_VALUE output event, set should_record to false
/// - the plugin is responsible for updating its GUI
///
/// VI. Adding or removing parameters
/// - if the plugin is activated call clap_host->restart()
/// - once the plugin isn't active:
///   - apply the new state
///   - if a parameter is gone or is created with an id that may have been used before,
///     call clap_host_params.clear(host, param_id, CLAP_PARAM_CLEAR_ALL)
///   - call clap_host_params->rescan(CLAP_PARAM_RESCAN_ALL)
///
/// CLAP allows the plugin to change the parameter range, yet the plugin developer
/// should be aware that doing so isn't without risk, especially if you made the
/// promise to never change the sound. If you want to be 100% certain that the
/// sound will not change with all host, then simply never change the range.
///
/// There are two approaches to automations, either you automate the plain value,
/// or you automate the knob position. The first option will be robust to a range
/// increase, while the second won't be.
///
/// If the host goes with the second approach (automating the knob position), it means
/// that the plugin is hosted in a relaxed environment regarding sound changes (they are
/// accepted, and not a concern as long as they are reasonable). Though, stepped parameters
/// should be stored as plain value in the document.
///
/// If the host goes with the first approach, there will still be situation where the
/// sound may inevitably change. For example, if the plugin increase the range, there
/// is an automation playing at the max value and on top of that an LFO is applied.
/// See the following curve:
///                                   .
///                                  . .
///          .....                  .   .
/// before: .     .     and after: .     .
///
/// Persisting parameter values:
///
/// Plugins are responsible for persisting their parameter's values between
/// sessions by implementing the state extension. Otherwise parameter value will
/// not be recalled when reloading a project. Hosts should _not_ try to save and
/// restore parameter values for plugins that don't implement the state
/// extension.
///
/// Advice for the host:
///
/// - store plain values in the document (automation)
/// - store modulation amount in plain value delta, not in percentage
/// - when you apply a CC mapping, remember the min/max plain values so you can adjust
/// - do not implement a parameter saving fall back for plugins that don't
///   implement the state extension
///
/// Advice for the plugin:
///
/// - think carefully about your parameter range when designing your DSP
/// - avoid shrinking parameter ranges, they are very likely to change the sound
/// - consider changing the parameter range as a tradeoff: what you improve vs what you break
/// - make sure to implement saving and loading the parameter values using the
///   state extension
/// - if you plan to use adapters for other plugin formats, then you need to pay extra
///   attention to the adapter requirements

static CLAP_CONSTEXPR const char CLAP_EXT_PARAMS[] = "clap.params";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   // Is this param stepped? (integer values only)
   // if so the double value is converted to integer using a cast (equivalent to trunc).
   CLAP_PARAM_IS_STEPPED = 1 << 0,

   // Useful for periodic parameters like a phase
   CLAP_PARAM_IS_PERIODIC = 1 << 1,

   // The parameter should not be shown to the user, because it is currently not used.
   // It is not necessary to process automation for this parameter.
   CLAP_PARAM_IS_HIDDEN = 1 << 2,

   // The parameter can't be changed by the host.
   CLAP_PARAM_IS_READONLY = 1 << 3,

   // This parameter is used to merge the plugin and host bypass button.
   // It implies that the parameter is stepped.
   // min: 0 -> bypass off
   // max: 1 -> bypass on
   CLAP_PARAM_IS_BYPASS = 1 << 4,

   // When set:
   // - automation can be recorded
   // - automation can be played back
   //
   // The host can send live user changes for this parameter regardless of this flag.
   //
   // If this parameter affects the internal processing structure of the plugin, ie: max delay, fft
   // size, ... and the plugins needs to re-allocate its working buffers, then it should call
   // host->request_restart(), and perform the change once the plugin is re-activated.
   CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,

   // Does this parameter support per note automations?
   CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID = 1 << 6,

   // Does this parameter support per key automations?
   CLAP_PARAM_IS_AUTOMATABLE_PER_KEY = 1 << 7,

   // Does this parameter support per channel automations?
   CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL = 1 << 8,

   // Does this parameter support per port automations?
   CLAP_PARAM_IS_AUTOMATABLE_PER_PORT = 1 << 9,

   // Does this parameter support the modulation signal?
   CLAP_PARAM_IS_MODULATABLE = 1 << 10,

   // Does this parameter support per note modulations?
   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID = 1 << 11,

   // Does this parameter support per key modulations?
   CLAP_PARAM_IS_MODULATABLE_PER_KEY = 1 << 12,

   // Does this parameter support per channel modulations?
   CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL = 1 << 13,

   // Does this parameter support per port modulations?
   CLAP_PARAM_IS_MODULATABLE_PER_PORT = 1 << 14,

   // Any change to this parameter will affect the plugin output and requires to be done via
   // process() if the plugin is active.
   //
   // A simple example would be a DC Offset, changing it will change the output signal and must be
   // processed.
   CLAP_PARAM_REQUIRES_PROCESS = 1 << 15,

   // This parameter represents an enumerated value.
   // If you set this flag, then you must set CLAP_PARAM_IS_STEPPED too.
   // All values from min to max must not have a blank value_to_text().
   CLAP_PARAM_IS_ENUM = 1 << 16,
};
typedef uint32_t clap_param_info_flags;

/* This describes a parameter */
typedef struct clap_param_info {
   // Stable parameter identifier, it must never change.
   clap_id id;

   clap_param_info_flags flags;

   // This value is optional and set by the plugin.
   // Its purpose is to provide fast access to the plugin parameter object by caching its pointer.
   // For instance:
   //
   // in clap_plugin_params.get_info():
   //    Parameter *p = findParameter(param_id);
   //    param_info->cookie = p;
   //
   // later, in clap_plugin.process():
   //
   //    Parameter *p = (Parameter *)event->cookie;
   //    if (!p) [[unlikely]]
   //       p = findParameter(event->param_id);
   //
   // where findParameter() is a function the plugin implements to map parameter ids to internal
   // objects.
   //
   // Important:
   //  - The cookie is invalidated by a call to clap_host_params->rescan(CLAP_PARAM_RESCAN_ALL) or
   //    when the plugin is destroyed.
   //  - The host will either provide the cookie as issued or nullptr in events addressing
   //    parameters.
   //  - The plugin must gracefully handle the case of a cookie which is nullptr.
   //  - Many plugins will process the parameter events more quickly if the host can provide the
   //    cookie in a faster time than a hashmap lookup per param per event.
   void *cookie;

   // The display name. eg: "Volume". This does not need to be unique. Do not include the module
   // text in this. The host should concatenate/format the module + name in the case where showing
   // the name alone would be too vague.
   char name[CLAP_NAME_SIZE];

   // The module path containing the param, eg: "Oscillators/Wavetable 1".
   // '/' will be used as a separator to show a tree-like structure.
   char module[CLAP_PATH_SIZE];

   double min_value;     // Minimum plain value
   double max_value;     // Maximum plain value
   double default_value; // Default plain value
} clap_param_info_t;

typedef struct clap_plugin_params {
   // Returns the number of parameters.
   // [main-thread]
   uint32_t(CLAP_ABI *count)(const clap_plugin_t *plugin);

   // Copies the parameter's info to param_info.
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *get_info)(const clap_plugin_t *plugin,
                            uint32_t             param_index,
                            clap_param_info_t   *param_info);

   // Writes the parameter's current value to out_value.
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *get_value)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);

   // Fills out_buffer with a null-terminated UTF-8 string that represents the parameter at the
   // given 'value' argument. eg: "2.3 kHz". The host should always use this to format parameter
   // values before displaying it to the user.
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *value_to_text)(const clap_plugin_t *plugin,
                                 clap_id              param_id,
                                 double               value,
                                 char                *out_buffer,
                                 uint32_t             out_buffer_capacity);

   // Converts the null-terminated UTF-8 param_value_text into a double and writes it to out_value.
   // The host can use this to convert user input into a parameter value.
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *text_to_value)(const clap_plugin_t *plugin,
                                 clap_id              param_id,
                                 const char          *param_value_text,
                                 double              *out_value);

   // Flushes a set of parameter changes.
   // This method must not be called concurrently to clap_plugin->process().
   //
   // Note: if the plugin is processing, then the process() call will already achieve the
   // parameter update (bi-directional), so a call to flush isn't required, also be aware
   // that the plugin may use the sample offset in process(), while this information would be
   // lost within flush().
   //
   // [active ? audio-thread : main-thread]
   void(CLAP_ABI *flush)(const clap_plugin_t        *plugin,
                         const clap_input_events_t  *in,
                         const clap_output_events_t *out);
} clap_plugin_params_t;

enum {
   // The parameter values did change, eg. after loading a preset.
   // The host will scan all the parameters value.
   // The host will not record those changes as automation points.
   // New values takes effect immediately.
   CLAP_PARAM_RESCAN_VALUES = 1 << 0,

   // The value to text conversion changed, and the text needs to be rendered again.
   CLAP_PARAM_RESCAN_TEXT = 1 << 1,

   // The parameter info did change, use this flag for:
   // - name change
   // - module change
   // - is_periodic (flag)
   // - is_hidden (flag)
   // New info takes effect immediately.
   CLAP_PARAM_RESCAN_INFO = 1 << 2,

   // Invalidates everything the host knows about parameters.
   // It can only be used while the plugin is deactivated.
   // If the plugin is activated use clap_host->restart() and delay any change until the host calls
   // clap_plugin->deactivate().
   //
   // You must use this flag if:
   // - some parameters were added or removed.
   // - some parameters had critical changes:
   //   - is_per_note (flag)
   //   - is_per_key (flag)
   //   - is_per_channel (flag)
   //   - is_per_port (flag)
   //   - is_readonly (flag)
   //   - is_bypass (flag)
   //   - is_stepped (flag)
   //   - is_modulatable (flag)
   //   - min_value
   //   - max_value
   //   - cookie
   CLAP_PARAM_RESCAN_ALL = 1 << 3,
};
typedef uint32_t clap_param_rescan_flags;

enum {
   // Clears all possible references to a parameter
   CLAP_PARAM_CLEAR_ALL = 1 << 0,

   // Clears all automations to a parameter
   CLAP_PARAM_CLEAR_AUTOMATIONS = 1 << 1,

   // Clears all modulations to a parameter
   CLAP_PARAM_CLEAR_MODULATIONS = 1 << 2,
};
typedef uint32_t clap_param_clear_flags;

typedef struct clap_host_params {
   // Rescan the full list of parameters according to the flags.
   // [main-thread]
   void(CLAP_ABI *rescan)(const clap_host_t *host, clap_param_rescan_flags flags);

   // Clears references to a parameter.
   // [main-thread]
   void(CLAP_ABI *clear)(const clap_host_t *host, clap_id param_id, clap_param_clear_flags flags);

   // Request a parameter flush.
   //
   // The host will then schedule a call to either:
   // - clap_plugin.process()
   // - clap_plugin_params.flush()
   //
   // This function is always safe to use and should not be called from an [audio-thread] as the
   // plugin would already be within process() or flush().
   //
   // [thread-safe,!audio-thread]
   void(CLAP_ABI *request_flush)(const clap_host_t *host);
} clap_host_params_t;

#ifdef __cplusplus
}
#endif

// ext/posix-fd-support.h
// This extension let your plugin hook itself into the host select/poll/epoll/kqueue reactor.
// This is useful to handle asynchronous I/O on the main thread.
static CLAP_CONSTEXPR const char CLAP_EXT_POSIX_FD_SUPPORT[] = "clap.posix-fd-support";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   // IO events flags, they can be used to form a mask which describes:
   // - which events you are interested in (register_fd/modify_fd)
   // - which events happened (on_fd)
   CLAP_POSIX_FD_READ = 1 << 0,
   CLAP_POSIX_FD_WRITE = 1 << 1,
   CLAP_POSIX_FD_ERROR = 1 << 2,
};
typedef uint32_t clap_posix_fd_flags_t;

typedef struct clap_plugin_posix_fd_support {
   // This callback is "level-triggered".
   // It means that a writable fd will continuously produce "on_fd()" events;
   // don't forget using modify_fd() to remove the write notification once you're
   // done writing.
   //
   // [main-thread]
   void(CLAP_ABI *on_fd)(const clap_plugin_t *plugin, int fd, clap_posix_fd_flags_t flags);
} clap_plugin_posix_fd_support_t;

typedef struct clap_host_posix_fd_support {
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *register_fd)(const clap_host_t *host, int fd, clap_posix_fd_flags_t flags);

   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *modify_fd)(const clap_host_t *host, int fd, clap_posix_fd_flags_t flags);

   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *unregister_fd)(const clap_host_t *host, int fd);
} clap_host_posix_fd_support_t;

#ifdef __cplusplus
}
#endif

// ext/render.h
static CLAP_CONSTEXPR const char CLAP_EXT_RENDER[] = "clap.render";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   // Default setting, for "realtime" processing
   CLAP_RENDER_REALTIME = 0,

   // For processing without realtime pressure
   // The plugin may use more expensive algorithms for higher sound quality.
   CLAP_RENDER_OFFLINE = 1,
};
typedef int32_t clap_plugin_render_mode;

// The render extension is used to let the plugin know if it has "realtime"
// pressure to process.
//
// If this information does not influence your rendering code, then don't
// implement this extension.
typedef struct clap_plugin_render {
   // Returns true if the plugin has a hard requirement to process in real-time.
   // This is especially useful for plugin acting as a proxy to an hardware device.
   // [main-thread]
   bool(CLAP_ABI *has_hard_realtime_requirement)(const clap_plugin_t *plugin);

   // Returns true if the rendering mode could be applied.
   // [main-thread]
   bool(CLAP_ABI *set)(const clap_plugin_t *plugin, clap_plugin_render_mode mode);
} clap_plugin_render_t;

#ifdef __cplusplus
}
#endif

// stream.h
/// @page Streams
///
/// ## Notes on using streams
///
/// When working with `clap_istream` and `clap_ostream` objects to load and save
/// state, it is important to keep in mind that the host may limit the number of
/// bytes that can be read or written at a time. The return values for the
/// stream read and write functions indicate how many bytes were actually read
/// or written. You need to use a loop to ensure that you read or write the
/// entirety of your state. Don't forget to also consider the negative return
/// values for the end of file and IO error codes.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_istream {
   void *ctx; // reserved pointer for the stream

   // returns the number of bytes read; 0 indicates end of file and -1 a read error
   int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t size);
} clap_istream_t;

typedef struct clap_ostream {
   void *ctx; // reserved pointer for the stream

   // returns the number of bytes written; -1 on write error
   int64_t(CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer, uint64_t size);
} clap_ostream_t;

#ifdef __cplusplus
}
#endif

// ext/state.h
/// @page State
/// @brief state management
///
/// Plugins can implement this extension to save and restore both parameter
/// values and non-parameter state. This is used to persist a plugin's state
/// between project reloads, when duplicating and copying plugin instances, and
/// for host-side preset management.

static CLAP_CONSTEXPR const char CLAP_EXT_STATE[] = "clap.state";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_state {
   // Saves the plugin state into stream.
   // Returns true if the state was correctly saved.
   // [main-thread]
   bool(CLAP_ABI *save)(const clap_plugin_t *plugin, const clap_ostream_t *stream);

   // Loads the plugin state from stream.
   // Returns true if the state was correctly restored.
   // [main-thread]
   bool(CLAP_ABI *load)(const clap_plugin_t *plugin, const clap_istream_t *stream);
} clap_plugin_state_t;

typedef struct clap_host_state {
   // Tell the host that the plugin state has changed and should be saved again.
   // If a parameter value changes, then it is implicit that the state is dirty.
   // [main-thread]
   void(CLAP_ABI *mark_dirty)(const clap_host_t *host);
} clap_host_state_t;

#ifdef __cplusplus
}
#endif

// ext/tail.h
static CLAP_CONSTEXPR const char CLAP_EXT_TAIL[] = "clap.tail";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_tail {
   // Returns tail length in samples.
   // Any value greater or equal to INT32_MAX implies infinite tail.
   // [main-thread,audio-thread]
   uint32_t(CLAP_ABI *get)(const clap_plugin_t *plugin);
} clap_plugin_tail_t;

typedef struct clap_host_tail {
   // Tell the host that the tail has changed.
   // [audio-thread]
   void(CLAP_ABI *changed)(const clap_host_t *host);
} clap_host_tail_t;

#ifdef __cplusplus
}
#endif

// ext/thread-check.h
static CLAP_CONSTEXPR const char CLAP_EXT_THREAD_CHECK[] = "clap.thread-check";

#ifdef __cplusplus
extern "C" {
#endif

/// @page thread-check
///
/// CLAP defines two symbolic threads:
///
/// main-thread:
///    This is the thread in which most of the interaction between the plugin and host happens.
///    This will be the same OS thread throughout the lifetime of the plug-in.
///    On macOS and Windows, this must be the thread on which gui and timer events are received
///    (i.e., the main thread of the program).
///    It isn't a realtime thread, yet this thread needs to respond fast enough to user interaction,
///    so it is recommended to run long and expensive tasks such as preset indexing or asset loading
///    in dedicated background threads.
///
/// audio-thread:
///    This thread is used for realtime audio processing. Its execution should be as deterministic
///    as possible to meet the audio interface's deadline (can be <1ms). In other words, there is a
///    known set of operations that should be avoided: malloc() and free(), mutexes (spin mutexes
///    are worse), I/O, waiting, ...
///    The audio-thread is something symbolic, there isn't one OS thread that remains the
///    audio-thread for the plugin lifetime. As you may guess, the host is likely to have a
///    thread pool and the plugin.process() call may be scheduled on different OS threads over time.
///    The most important thing is that there can't be two audio-threads at the same time. All the
///    functions marked with [audio-thread] **ARE NOT CONCURRENT**. The host may mark any OS thread,
///    including the main-thread as the audio-thread, as long as it can guarantee that only one OS
///    thread is the audio-thread at a time. The audio-thread can be seen as a concurrency guard for
///    all functions marked with [audio-thread].

// This interface is useful to do runtime checks and make
// sure that the functions are called on the correct threads.
// It is highly recommended that hosts implement this extension.
typedef struct clap_host_thread_check {
   // Returns true if "this" thread is the main thread.
   // [thread-safe]
   bool(CLAP_ABI *is_main_thread)(const clap_host_t *host);

   // Returns true if "this" thread is one of the audio threads.
   // [thread-safe]
   bool(CLAP_ABI *is_audio_thread)(const clap_host_t *host);
} clap_host_thread_check_t;

#ifdef __cplusplus
}
#endif

// ext/thread-pool.h
/// @page
///
/// This extension lets the plugin use the host's thread pool.
///
/// The plugin must provide @ref clap_plugin_thread_pool, and the host may provide @ref
/// clap_host_thread_pool. If it doesn't, the plugin should process its data by its own means. In
/// the worst case, a single threaded for-loop.
///
/// Simple example with N voices to process
///
/// @code
/// void myplug_thread_pool_exec(const clap_plugin *plugin, uint32_t voice_index)
/// {
///    compute_voice(plugin, voice_index);
/// }
///
/// void myplug_process(const clap_plugin *plugin, const clap_process *process)
/// {
///    ...
///    bool didComputeVoices = false;
///    if (host_thread_pool && host_thread_pool.exec)
///       didComputeVoices = host_thread_pool.request_exec(host, plugin, N);
///
///    if (!didComputeVoices)
///       for (uint32_t i = 0; i < N; ++i)
///          myplug_thread_pool_exec(plugin, i);
///    ...
/// }
/// @endcode
///
/// Be aware that using a thread pool may break hard real-time rules due to the thread
/// synchronization involved.
///
/// If the host knows that it is running under hard real-time pressure it may decide to not
/// provide this interface.

static CLAP_CONSTEXPR const char CLAP_EXT_THREAD_POOL[] = "clap.thread-pool";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_thread_pool {
   // Called by the thread pool
   void(CLAP_ABI *exec)(const clap_plugin_t *plugin, uint32_t task_index);
} clap_plugin_thread_pool_t;

typedef struct clap_host_thread_pool {
   // Schedule num_tasks jobs in the host thread pool.
   // It can't be called concurrently or from the thread pool.
   // Will block until all the tasks are processed.
   // This must be used exclusively for realtime processing within the process call.
   // Returns true if the host did execute all the tasks, false if it rejected the request.
   // The host should check that the plugin is within the process call, and if not, reject the exec
   // request.
   // [audio-thread]
   bool(CLAP_ABI *request_exec)(const clap_host_t *host, uint32_t num_tasks);
} clap_host_thread_pool_t;

#ifdef __cplusplus
}
#endif

// ext/timer-support
static CLAP_CONSTEXPR const char CLAP_EXT_TIMER_SUPPORT[] = "clap.timer-support";

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clap_plugin_timer_support {
   // [main-thread]
   void(CLAP_ABI *on_timer)(const clap_plugin_t *plugin, clap_id timer_id);
} clap_plugin_timer_support_t;

typedef struct clap_host_timer_support {
   // Registers a periodic timer.
   // The host may adjust the period if it is under a certain threshold.
   // 30 Hz should be allowed.
   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *register_timer)(const clap_host_t *host, uint32_t period_ms, clap_id *timer_id);

   // Returns true on success.
   // [main-thread]
   bool(CLAP_ABI *unregister_timer)(const clap_host_t *host, clap_id timer_id);
} clap_host_timer_support_t;

#ifdef __cplusplus
}
#endif

// ext/voice-info.h
// This extension indicates the number of voices the synthesizer has.
// It is useful for the host when performing polyphonic modulations,
// because the host needs its own voice management and should try to follow
// what the plugin is doing:
// - make the host's voice pool coherent with what the plugin has
// - turn the host's voice management to mono when the plugin is mono

static const char CLAP_EXT_VOICE_INFO[] = "clap.voice-info";

#ifdef __cplusplus
extern "C" {
#endif

enum {
   // Allows the host to send overlapping NOTE_ON events.
   // The plugin will then rely upon the note_id to distinguish between them.
   CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES = 1 << 0,
};

typedef struct clap_voice_info {
   // voice_count is the current number of voices that the patch can use
   // voice_capacity is the number of voices allocated voices
   // voice_count should not be confused with the number of active voices.
   //
   // 1 <= voice_count <= voice_capacity
   //
   // For example, a synth can have a capacity of 8 voices, but be configured
   // to only use 4 voices: {count: 4, capacity: 8}.
   //
   // If the voice_count is 1, then the synth is working in mono and the host
   // can decide to only use global modulation mapping.
   uint32_t voice_count;
   uint32_t voice_capacity;

   uint64_t flags;
} clap_voice_info_t;

typedef struct clap_plugin_voice_info {
   // gets the voice info, returns true on success
   // [main-thread && active]
   bool(CLAP_ABI *get)(const clap_plugin_t *plugin, clap_voice_info_t *info);
} clap_plugin_voice_info_t;

typedef struct clap_host_voice_info {
   // informs the host that the voice info has changed
   // [main-thread]
   void(CLAP_ABI *changed)(const clap_host_t *host);
} clap_host_voice_info_t;

#ifdef __cplusplus
}
#endif
