/* Released into the public domain by Tré Dudman - 2024
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#include <clap/clap.h>
#include <cplug.h>
#include <stdio.h>
#include <string.h>

typedef struct CLAPPlugin
{
    clap_plugin_t clapPlugin;
    void*         userPlugin;
#if CPLUG_WANT_GUI
    void* userGUI;
#endif
    const clap_host_t*         host;
    const clap_host_latency_t* host_latency;
    const clap_host_state_t*   host_state;
    const clap_host_params_t*  host_params;
} CLAPPlugin;

#if CPLUG_NUM_INPUT_BUSSES + CPLUG_NUM_OUTPUT_BUSSES > 0
/////////////////////////////
// clap_plugin_audio_ports //
/////////////////////////////

static uint32_t CLAPExtAudioPorts_count(const clap_plugin_t* plugin, bool is_input)
{
    cplug_log("CLAPExtAudioPorts_count => %u", (unsigned)is_input);
    return is_input ? CPLUG_NUM_INPUT_BUSSES : CPLUG_NUM_OUTPUT_BUSSES;
}

static bool
CLAPExtAudioPorts_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info)
{
    cplug_log("CLAPExtAudioPorts_get => %u %p", (unsigned)is_input, info);
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;

    if (is_input && index < CPLUG_NUM_INPUT_BUSSES)
    {
        info->id = index;
        snprintf(info->name, sizeof(info->name), "%s", cplug_getInputBusName(clap->userPlugin, index));
        info->channel_count = cplug_getInputBusChannelCount(clap->userPlugin, index);
        // Maybe we will support 64bit one day (probably not)
        info->flags = CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
        if (index == 0)
            info->flags |= CLAP_AUDIO_PORT_IS_MAIN;

        if (info->channel_count == 1)
            info->port_type = CLAP_PORT_MONO;
        else if (info->channel_count == 2)
            info->port_type = CLAP_PORT_STEREO;
        else
            info->port_type = NULL;

        if (index < CPLUG_NUM_OUTPUT_BUSSES)
            info->in_place_pair = CPLUG_NUM_INPUT_BUSSES + index;
        else
            info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    if (! is_input && index < CPLUG_NUM_OUTPUT_BUSSES)
    {
        info->id = CPLUG_NUM_INPUT_BUSSES + index;
        snprintf(info->name, sizeof(info->name), "%s", cplug_getOutputBusName(clap->userPlugin, index));
        info->channel_count = cplug_getOutputBusChannelCount(clap->userPlugin, index);
        // Maybe we will support 64bit one day (probably not)
        info->flags = CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
        if (index == 0)
            info->flags |= CLAP_AUDIO_PORT_IS_MAIN;

        if (info->channel_count == 1)
            info->port_type = CLAP_PORT_MONO;
        else if (info->channel_count == 2)
            info->port_type = CLAP_PORT_STEREO;
        else
            info->port_type = NULL;

        if (index < CPLUG_NUM_INPUT_BUSSES)
            info->in_place_pair = index;
        else
            info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }

    return false;
}

static const clap_plugin_audio_ports_t s_clap_audio_ports = {
    .count = CLAPExtAudioPorts_count,
    .get   = CLAPExtAudioPorts_get,
};
#endif // CPLUG_NUM_INPUT_BUSSES + CPLUG_NUM_OUTPUT_BUSSES

#if CPLUG_WANT_MIDI_INPUT
////////////////////////////
// clap_plugin_note_ports //
////////////////////////////

static uint32_t CLAPExtNotePorts_count(const clap_plugin_t* plugin, bool is_input)
{
    cplug_log("CLAPExtNotePorts_count => %u", (unsigned)is_input);
    return 1;
}

static bool
CLAPExtNotePorts_get(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info)
{
    cplug_log("CLAPExtNotePorts_get => %u %p", (unsigned)is_input, info);
    CPLUG_LOG_ASSERT_RETURN(index == 0, false);

    info->id = 0;
    snprintf(info->name, sizeof(info->name), "%s", "MIDI Input");
    // NOTE: Bitwig 5.0 doesn't support the plain MIDI dialect, only CLAP. Bitwig 5.1 supports them all
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI;
    return true;
}

static const clap_plugin_note_ports_t s_clap_note_ports = {
    .count = CLAPExtNotePorts_count,
    .get   = CLAPExtNotePorts_get,
};
#endif // CPLUG_WANT_MIDI_INPUT

//////////////////
// clap_latency //
//////////////////

uint32_t CLAPExtLatency_get(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtLatency_get");
    return cplug_getLatencyInSamples(((CLAPPlugin*)plugin->plugin_data)->userPlugin);
}

static const clap_plugin_latency_t s_clap_latency = {
    .get = CLAPExtLatency_get,
};

///////////////
// clap_tail //
///////////////

uint32_t CLAPExtTail_get(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtTail_get");
    return cplug_getTailInSamples(((CLAPPlugin*)plugin->plugin_data)->userPlugin);
}

static const clap_plugin_tail_t s_clap_tail = {
    .get = CLAPExtTail_get,
};

////////////////
// clap_state //
////////////////

bool CLAPExtState_save(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    cplug_log("CLAPExtState_save => %p", stream);
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    cplug_saveState(clap->userPlugin, stream, (cplug_writeProc)stream->write);
    return true;
}

bool CLAPExtState_load(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    cplug_log("CLAPExtState_load %p", stream);
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    cplug_loadState(clap->userPlugin, stream, (cplug_readProc)stream->read);
    return true;
}

static const clap_plugin_state_t s_clap_state = {
    .save = CLAPExtState_save,
    .load = CLAPExtState_load,
};

#if CPLUG_NUM_PARAMS
/////////////////
// clap_params //
/////////////////

uint32_t CLAPExtParams_count(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtParams_count");
    return CPLUG_NUM_PARAMS;
}

bool CLAPExtParams_get_info(const clap_plugin_t* plugin, uint32_t param_index, clap_param_info_t* param_info)
{
    cplug_log("CLAPExtParams_get_info => %u %p", param_index, param_info);
    CPLUG_LOG_ASSERT_RETURN(param_index < CPLUG_NUM_PARAMS, false);

    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;

    param_info->id = param_index;
    snprintf(param_info->name, sizeof(param_info->name), "%s", cplug_getParameterName(clap->userPlugin, param_index));
    param_info->module[0]     = 0;
    param_info->default_value = cplug_getDefaultParameterValue(clap->userPlugin, param_index);
    cplug_getParameterRange(clap->userPlugin, param_index, &param_info->min_value, &param_info->max_value);

    uint32_t flags    = cplug_getParameterFlags(clap->userPlugin, param_index);
    param_info->flags = 0;
    if (flags & CPLUG_FLAG_PARAMETER_IS_READ_ONLY)
        param_info->flags |= CLAP_PARAM_IS_READONLY;
    if (flags & (CPLUG_FLAG_PARAMETER_IS_BOOL | CPLUG_FLAG_PARAMETER_IS_INTEGER))
        param_info->flags |= CLAP_PARAM_IS_STEPPED;
    if (flags & CPLUG_FLAG_PARAMETER_IS_HIDDEN)
        param_info->flags |= CLAP_PARAM_IS_HIDDEN;
    if (flags & CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE)
        param_info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
    if (flags & CPLUG_FLAG_PARAMETER_IS_BYPASS)
        param_info->flags |= CLAP_PARAM_IS_BYPASS;
    // This is a really great feature and I'd love to support it, however, at the time of writing this CLAP still isn't
    // supported by many hosts and so it's not worth it yet
    // TODO: Support this
    param_info->cookie = NULL;
    return true;
}

bool CLAPExtParams_get_value(const clap_plugin_t* plugin, clap_id param_id, double* out_value)
{
    cplug_log("CLAPExtParams_get_value => %u %p", param_id, out_value);
    CPLUG_LOG_ASSERT_RETURN(param_id < CPLUG_NUM_PARAMS, false);
    *out_value = cplug_getParameterValue(((CLAPPlugin*)plugin->plugin_data)->userPlugin, param_id);
    return true;
}

bool CLAPExtParams_value_to_text(
    const clap_plugin_t* plugin,
    clap_id              param_id,
    double               value,
    char*                out_buffer,
    uint32_t             out_buffer_capacity)
{
    // cplug_log("CLAPExtParams_value_to_text => %u %f %p %u", param_id, value, out_buffer, out_buffer_capacity);
    CPLUG_LOG_ASSERT_RETURN(param_id < CPLUG_NUM_PARAMS, false);

    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    cplug_parameterValueToString(clap->userPlugin, param_id, out_buffer, out_buffer_capacity, value);
    return true;
}

bool CLAPExtParams_text_to_value(
    const clap_plugin_t* plugin,
    clap_id              param_id,
    const char*          param_value_text,
    double*              out_value)
{
    cplug_log("CLAPExtParams_text_to_value => %u %p %p", param_id, param_value_text, out_value);
    CPLUG_LOG_ASSERT_RETURN(param_id < CPLUG_NUM_PARAMS, false);
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    *out_value       = cplug_parameterStringToValue(clap->userPlugin, param_id, param_value_text);
    return true;
}

void CLAPExtParams_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
{
    cplug_log("[WARNING: NOT SUPPORTED] CLAPExtParams_flush => %p %p", in, out);
    // NOTE: Bitwig & Reaper won't actually call this method if you process all your events in the process callback
    // We include this method anyway to prevent any segfault that may occur in future from not having it.
}

static const clap_plugin_params_t s_clap_params = {
    .count         = CLAPExtParams_count,
    .get_info      = CLAPExtParams_get_info,
    .get_value     = CLAPExtParams_get_value,
    .value_to_text = CLAPExtParams_value_to_text,
    .text_to_value = CLAPExtParams_text_to_value,
    .flush         = CLAPExtParams_flush,
};
#endif // CPLUG_NUM_PARAMS

#if CPLUG_WANT_GUI
//////////////
// clap_gui //
//////////////

#if defined(_WIN32)
#define CPLUG_CLAP_GUI_API CLAP_WINDOW_API_WIN32
#elif defined(__APPLE__)
#define CPLUG_CLAP_GUI_API CLAP_WINDOW_API_COCOA
#else
#define CPLUG_CLAP_GUI_API CLAP_WINDOW_API_X11
#endif

bool CLAPExtGUI_is_api_supported(const clap_plugin_t* plugin, const char* api, bool is_floating)
{
    cplug_log("CLAPExtGUI_is_api_supported => %s %u", api, (unsigned)is_floating);
    return ! strcmp(api, CPLUG_CLAP_GUI_API) && ! is_floating;
}

bool CLAPExtGUI_get_preferred_api(const clap_plugin_t* plugin, const char** api, bool* is_floating)
{
    cplug_log("CLAPExtGUI_get_preferred_api => %p %p", api, is_floating);
    *api         = CPLUG_CLAP_GUI_API;
    *is_floating = false;
    return true;
}

bool CLAPExtGUI_create(const clap_plugin_t* plugin, const char* api, bool is_floating)
{
    cplug_log("CLAPExtGUI_create => %s %u", api, (unsigned)is_floating);
    CPLUG_LOG_ASSERT_RETURN(CLAPExtGUI_is_api_supported(plugin, api, is_floating), false);

    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    clap->userGUI    = cplug_createGUI(clap->userPlugin);
    CPLUG_LOG_ASSERT_RETURN(clap->userGUI != NULL, false);

    return true;
}

void CLAPExtGUI_destroy(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtGUI_destroy");
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    cplug_setParent(clap->userGUI, NULL);
    cplug_destroyGUI(clap->userGUI);
    clap->userGUI = NULL;
}

static bool CLAPExtGUI_set_scale(const clap_plugin_t* plugin, double scale)
{
    cplug_log("CLAPExtGUI_set_scale => %f", scale);
    cplug_setScaleFactor(((CLAPPlugin*)plugin->plugin_data)->userGUI, (float)scale);
    return true;
}

static bool CLAPExtGUI_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    cplug_log("CLAPExtGUI_get_size => %p %p", width, height);
    cplug_getSize(((CLAPPlugin*)plugin->plugin_data)->userGUI, width, height);
    return true;
}

static bool CLAPExtGUI_can_resize(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtGUI_can_resize");
    return CPLUG_GUI_RESIZABLE;
}

static bool CLAPExtGUI_get_resize_hints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints)
{
    cplug_log("CLAPExtGUI_resize_hints => %p", hints);
    return cplug_getResizeHints(
        ((CLAPPlugin*)plugin->plugin_data)->userGUI,
        &hints->can_resize_horizontally,
        &hints->can_resize_vertically,
        &hints->preserve_aspect_ratio,
        &hints->aspect_ratio_width,
        &hints->aspect_ratio_height);
}

static bool CLAPExtGUI_adjust_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    cplug_log("CLAPExtGUI_adjust_size => %u %u", *width, *height);
    cplug_checkSize(((CLAPPlugin*)plugin->plugin_data)->userGUI, width, height);
    return true;
}

static bool CLAPExtGUI_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    cplug_log("CLAPExtGUI_set_size => %u %u", width, height);
    return cplug_setSize(((CLAPPlugin*)plugin->plugin_data)->userGUI, width, height);
}

static bool CLAPExtGUI_set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    cplug_log("CLAPExtGUI_set_parent => %p", window);
    cplug_setParent(((CLAPPlugin*)plugin->plugin_data)->userGUI, window->ptr);
    return true;
}

static bool CLAPExtGUI_set_transient(const clap_plugin_t* plugin, const clap_window_t* window)
{
    cplug_log("CLAPExtGUI_set_transient => %p", window);
    return false;
}

static void CLAPExtGUI_suggest_title(const clap_plugin_t* plugin, const char* title)
{
    cplug_log("CLAPExtGUI_suggest_title => %s", title);
}

static bool CLAPExtGUI_show(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtGUI_show");
    cplug_setVisible(((CLAPPlugin*)plugin->plugin_data)->userGUI, true);
    return true;
}

static bool CLAPExtGUI_hide(const clap_plugin_t* plugin)
{
    cplug_log("CLAPExtGUI_hide");
    cplug_setVisible(((CLAPPlugin*)plugin->plugin_data)->userGUI, false);
    return true;
}

static const clap_plugin_gui_t s_clap_gui = {
    .is_api_supported  = CLAPExtGUI_is_api_supported,
    .get_preferred_api = CLAPExtGUI_get_preferred_api,
    .create            = CLAPExtGUI_create,
    .destroy           = CLAPExtGUI_destroy,
    .set_scale         = CLAPExtGUI_set_scale,
    .get_size          = CLAPExtGUI_get_size,
    .can_resize        = CLAPExtGUI_can_resize,
    .get_resize_hints  = CLAPExtGUI_get_resize_hints,
    .adjust_size       = CLAPExtGUI_adjust_size,
    .set_size          = CLAPExtGUI_set_size,
    .set_parent        = CLAPExtGUI_set_parent,
    .set_transient     = CLAPExtGUI_set_transient,
    .suggest_title     = CLAPExtGUI_suggest_title,
    .show              = CLAPExtGUI_show,
    .hide              = CLAPExtGUI_hide,
};
#endif // CPLUG_WANT_GUI

/////////////////
// clap_plugin //
/////////////////

static bool CLAPPlugin_init(const struct clap_plugin* plugin)
{
    cplug_log("CLAPPlugin_init");
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;

    clap->userPlugin = cplug_createPlugin();

    // Fetch host's extensions here
    // Make sure to check that the interface functions are not null pointers
    clap->host_latency = (const clap_host_latency_t*)clap->host->get_extension(clap->host, CLAP_EXT_LATENCY);
    clap->host_state   = (const clap_host_state_t*)clap->host->get_extension(clap->host, CLAP_EXT_STATE);
    clap->host_params  = (const clap_host_params_t*)clap->host->get_extension(clap->host, CLAP_EXT_PARAMS);

    assert(clap->host_latency != NULL);
    assert(clap->host_state != NULL);
    assert(clap->host_params != NULL);
    return true;
}

static void CLAPPlugin_destroy(const struct clap_plugin* plugin)
{
    cplug_log("CLAPPlugin_destroy");
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;
    cplug_destroyPlugin(clap->userPlugin);
    free(clap);
}

static bool CLAPPlugin_activate(
    const clap_plugin_t* plugin,
    double               sample_rate,
    uint32_t             min_frames_count,
    uint32_t             max_frames_count)
{
    cplug_log("CLAPPlugin_activate => %f %u %u", sample_rate, min_frames_count, max_frames_count);
    cplug_setSampleRateAndBlockSize(((CLAPPlugin*)plugin->plugin_data)->userPlugin, sample_rate, max_frames_count);
    return true;
}

static void CLAPPlugin_deactivate(const struct clap_plugin* plugin) { cplug_log("CLAPPlugin_deactivate"); }

static bool CLAPPlugin_start_processing(const struct clap_plugin* plugin)
{
    cplug_log("CLAPPlugin_start_processing");
    return true;
}

static void CLAPPlugin_stop_processing(const struct clap_plugin* plugin) { cplug_log("CLAPPlugin_stop_processing"); }

static void CLAPPlugin_reset(const struct clap_plugin* plugin) { cplug_log("CLAPPlugin_reset"); }

typedef struct ClapProcessContextTranslator
{
    CplugProcessContext cplugContext;

    const clap_process_t* process;
    uint32_t              eventIdx;
    uint32_t              numEvents;
} ClapProcessContextTranslator;

bool ClapProcessContext_enqueueEvent(struct CplugProcessContext* ctx, const CplugEvent* paramEvent, uint32_t frameIdx)
{
    ClapProcessContextTranslator* translator = (ClapProcessContextTranslator*)ctx;
    const clap_process_t*         process    = translator->process;

    switch (paramEvent->type)
    {
    case CPLUG_EVENT_PARAM_CHANGE_BEGIN:
    case CPLUG_EVENT_PARAM_CHANGE_END:
    {
        clap_event_param_gesture_t event;
        memset(&event, 0, sizeof(event));
        event.header.size = sizeof(event);
        event.header.type = paramEvent->type == CPLUG_EVENT_PARAM_CHANGE_BEGIN ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                               : CLAP_EVENT_PARAM_GESTURE_END;
        event.param_id    = paramEvent->parameter.idx;
        return process->out_events->try_push(process->out_events, &event.header);
    }
    case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
    {
        clap_event_param_value_t event;
        memset(&event, 0, sizeof(event));
        event.header.size = sizeof(event);
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id    = paramEvent->parameter.idx;
        event.value       = paramEvent->parameter.value;
        return process->out_events->try_push(process->out_events, &event.header);
    }
    default:
        break;
    }
    return false;
}

bool ClapProcessContext_dequeueEvent(struct CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    ClapProcessContextTranslator* translator = (ClapProcessContextTranslator*)ctx;
    const clap_process_t*         process    = translator->process;

    if (frameIdx >= translator->cplugContext.numFrames)
        return false;

    if (translator->eventIdx == translator->numEvents)
    {
        // we reached the end of the event list
        event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
        event->processAudio.endFrame = translator->cplugContext.numFrames;
        return true;
    }

    const clap_event_header_t* hdr = process->in_events->get(process->in_events, translator->eventIdx);
    if (hdr->time != frameIdx)
    {
        event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
        event->processAudio.endFrame = hdr->time;
        return true;
    }

    switch (hdr->type)
    {
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE:
    case CLAP_EVENT_NOTE_END:
        cplug_log("WARNING: Unsupported MIDI format. If you're using Bitwig v5.0, please update to >= v5.1");
        break;
    case CLAP_EVENT_PARAM_VALUE:
    {
        const clap_event_param_value_t* ev = (const clap_event_param_value_t*)hdr;

        event->parameter.type  = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
        event->parameter.idx   = ev->param_id;
        event->parameter.value = ev->value;
        break;
    }
    case CLAP_EVENT_MIDI:
    {
        const clap_event_midi_t* ev = (const clap_event_midi_t*)hdr;

        event->midi.type     = CPLUG_EVENT_MIDI;
        event->midi.bytes[0] = ev->data[0];
        event->midi.bytes[1] = ev->data[1];
        event->midi.bytes[2] = ev->data[2];
        event->midi.bytes[3] = 0;
        event->midi.frame    = ev->header.time;
        break;
    }
    default:
        cplug_log("ClapProcessContext_dequeueEvent: Unhandled event type: %hu", hdr->type);
        break;
    }

    translator->eventIdx++;

    return true;
}

float** ClapProcessContext_getAudioInput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const ClapProcessContextTranslator* translator = (const ClapProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT_RETURN(busIdx < translator->process->audio_inputs_count, NULL);
    return translator->process->audio_inputs[busIdx].data32;
}

float** ClapProcessContext_getAudioOutput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const ClapProcessContextTranslator* translator = (const ClapProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT_RETURN(busIdx < translator->process->audio_outputs_count, NULL);
    return translator->process->audio_outputs[busIdx].data32;
}

static clap_process_status CLAPPlugin_process(const struct clap_plugin* plugin, const clap_process_t* process)
{
    // cplug_log("CLAPPlugin_process => %p", process);
    CLAPPlugin* clap = (CLAPPlugin*)plugin->plugin_data;

    struct ClapProcessContextTranslator translator = {0};
    translator.cplugContext.numFrames              = process->frames_count;

    if (process->transport)
    {
        if (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING)
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_IS_PLAYING;
        if (process->transport->flags & CLAP_TRANSPORT_IS_RECORDING)
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_IS_RECORDING;

        if (process->transport->song_pos_beats != 0)
        {
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_HAS_PLAYHEAD_BEATS;
            double posBeats                = (double)process->transport->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
            translator.cplugContext.playheadBeats = posBeats;
        }
        if (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)
        {
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_HAS_BPM;
            translator.cplugContext.bpm    = process->transport->tempo;
        }
        if (process->transport->flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE)
        {
            translator.cplugContext.flags |= CPLUG_FLAG_TRANSPORT_IS_LOOPING;
            double loopStartBeats = (double)process->transport->loop_start_beats / (double)CLAP_BEATTIME_FACTOR;
            double loopEndBeats   = (double)process->transport->loop_end_beats / (double)CLAP_BEATTIME_FACTOR;
            translator.cplugContext.loopStartBeats = loopStartBeats;
            translator.cplugContext.loopStartBeats = loopEndBeats;
        }
        if (process->transport->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)
        {
            translator.cplugContext.flags              |= CPLUG_FLAG_TRANSPORT_HAS_TIME_SIGNATURE;
            translator.cplugContext.timeSigNumerator    = process->transport->tsig_num;
            translator.cplugContext.timeSigDenominator  = process->transport->tsig_denom;
        }
    }

    translator.cplugContext.enqueueEvent   = &ClapProcessContext_enqueueEvent;
    translator.cplugContext.dequeueEvent   = &ClapProcessContext_dequeueEvent;
    translator.cplugContext.getAudioInput  = &ClapProcessContext_getAudioInput;
    translator.cplugContext.getAudioOutput = &ClapProcessContext_getAudioOutput;

    translator.process   = process;
    translator.eventIdx  = 0;
    translator.numEvents = process->in_events->size(process->in_events);

    cplug_process(clap->userPlugin, &translator.cplugContext);

    return CLAP_PROCESS_CONTINUE;
}

static const void* CLAPPlugin_get_extension(const struct clap_plugin* plugin, const char* id)
{
    cplug_log("CLAPPlugin_get_extension => %s", id);
    if (! strcmp(id, CLAP_EXT_LATENCY))
        return &s_clap_latency;
    if (! strcmp(id, CLAP_EXT_TAIL))
        return &s_clap_tail;
#if (CPLUG_NUM_INPUT_BUSSES + CPLUG_NUM_OUTPUT_BUSSES) > 0
    if (! strcmp(id, CLAP_EXT_AUDIO_PORTS))
        return &s_clap_audio_ports;
#endif
#if CPLUG_WANT_MIDI_INPUT
    if (! strcmp(id, CLAP_EXT_NOTE_PORTS))
        return &s_clap_note_ports;
#endif
    if (! strcmp(id, CLAP_EXT_STATE))
        return &s_clap_state;
#if CPLUG_NUM_PARAMS
    if (! strcmp(id, CLAP_EXT_PARAMS))
        return &s_clap_params;
#endif
#if CPLUG_WANT_GUI
    if (! strcmp(id, CLAP_EXT_GUI))
        return &s_clap_gui;
#endif
    return NULL;
}

static void CLAPPlugin_on_main_thread(const struct clap_plugin* plugin) { cplug_log("CLAPPlugin_on_main_thread"); }

/////////////////////////
// clap_plugin_factory //
/////////////////////////

static const char* s_clap_desc_features[] = {CPLUG_CLAP_FEATURES, NULL};

static const clap_plugin_descriptor_t s_clap_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = CPLUG_CLAP_ID,
    .name         = CPLUG_PLUGIN_NAME,
    .vendor       = CPLUG_COMPANY_NAME,
    .url          = CPLUG_PLUGIN_URI,
    .manual_url   = CPLUG_PLUGIN_URI,
    .support_url  = CPLUG_PLUGIN_URI,
    .version      = CPLUG_PLUGIN_VERSION,
    .description  = CPLUG_CLAP_DESCRIPTION,
    .features     = s_clap_desc_features};

static uint32_t CLAPFactory_get_plugin_count(const struct clap_plugin_factory* factory)
{
    cplug_log("CLAPFactory_get_plugin_count");
    return 1;
}

static const clap_plugin_descriptor_t*
CLAPFactory_get_plugin_descriptor(const struct clap_plugin_factory* factory, uint32_t index)
{
    cplug_log("CLAPFactory_get_plugin_descriptor => %u", index);
    CPLUG_LOG_ASSERT_RETURN(index == 0, NULL);
    return &s_clap_desc;
}

static const clap_plugin_t*
CLAPFactory_create_plugin(const struct clap_plugin_factory* factory, const clap_host_t* host, const char* plugin_id)
{
    cplug_log("CLAPFactory_create_plugin => %p %s", host, plugin_id);
    CPLUG_LOG_ASSERT_RETURN(clap_version_is_compatible(host->clap_version), NULL);
    // clap-validator tests you on this
    CPLUG_LOG_ASSERT_RETURN(strcmp(plugin_id, CPLUG_CLAP_ID) == 0, NULL);

    CLAPPlugin* clap                  = (CLAPPlugin*)calloc(1, sizeof(CLAPPlugin));
    clap->clapPlugin.desc             = &s_clap_desc;
    clap->clapPlugin.plugin_data      = clap;
    clap->clapPlugin.init             = CLAPPlugin_init;
    clap->clapPlugin.destroy          = CLAPPlugin_destroy;
    clap->clapPlugin.activate         = CLAPPlugin_activate;
    clap->clapPlugin.deactivate       = CLAPPlugin_deactivate;
    clap->clapPlugin.start_processing = CLAPPlugin_start_processing;
    clap->clapPlugin.stop_processing  = CLAPPlugin_stop_processing;
    clap->clapPlugin.reset            = CLAPPlugin_reset;
    clap->clapPlugin.process          = CLAPPlugin_process;
    clap->clapPlugin.get_extension    = CLAPPlugin_get_extension;
    clap->clapPlugin.on_main_thread   = CLAPPlugin_on_main_thread;

    clap->host = host;

    return &clap->clapPlugin;
}

static const clap_plugin_factory_t s_plugin_factory = {
    .get_plugin_count      = CLAPFactory_get_plugin_count,
    .get_plugin_descriptor = CLAPFactory_get_plugin_descriptor,
    .create_plugin         = CLAPFactory_create_plugin,
};

////////////////
// clap_entry //
////////////////

static bool CLAPEntry_init(const char* plugin_path)
{
    cplug_log("CLAPEntry_init => %s", plugin_path);
    cplug_libraryLoad();
    return true;
}

static void CLAPEntry_deinit(void)
{
    cplug_log("CLAPEntry_deinit");
    cplug_libraryUnload();
}

static const void* CLAPEntry_get_factory(const char* factory_id)
{
    cplug_log("CLAPEntry_get_factory => %s", factory_id);
    if (! strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID))
        return &s_plugin_factory;
    return NULL;
}

// This symbol will be resolved by the host
const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = CLAPEntry_init,
    .deinit       = CLAPEntry_deinit,
    .get_factory  = CLAPEntry_get_factory,
};
