/* Released into the public domain by Tré Dudman - 2024
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#include <AudioToolbox/AudioUnit.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <CoreMIDI/MIDIServices.h>
#include <cplug.h>

// Audio Units have no way (to my knowldge) of calling a DLL load/unload function, so we have to make one
volatile int g_auv2InstanceCount = 0;

// These values are from the AU C++ SDK
static const UInt32 kAUDefaultMaxFramesPerSlice = 1156;
static const double kAUDefaultSampleRate        = 44100.0;

#ifndef ARRSIZE
#define ARRSIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

static const char* _cplugLookup2Str(SInt16 selector)
{
    static const struct
    {
        const char* str;
        SInt16      id;
    } _auv2selectorstrings[] = {
        {"kAudioUnitInitializeSelect", 0x0001},
        {"kAudioUnitUninitializeSelect", 0x0002},
        {"kAudioUnitGetPropertyInfoSelect", 0x0003},
        {"kAudioUnitGetPropertySelect", 0x0004},
        {"kAudioUnitSetPropertySelect", 0x0005},
        {"kAudioUnitAddPropertyListenerSelect", 0x000A},
        {"kAudioUnitRemovePropertyListenerSelect", 0x000B},
        {"kAudioUnitRemovePropertyListenerWithUserDataSelect", 0x0012},
        {"kAudioUnitAddRenderNotifySelect", 0x000F},
        {"kAudioUnitRemoveRenderNotifySelect", 0x0010},
        {"kAudioUnitGetParameterSelect", 0x0006},
        {"kAudioUnitSetParameterSelect", 0x0007},
        {"kAudioUnitScheduleParametersSelect", 0x0011},
        {"kAudioUnitRenderSelect", 0x000E},
        {"kAudioUnitResetSelect", 0x0009},
        {"kAudioUnitComplexRenderSelect", 0x0013},
        {"kAudioUnitProcessSelect", 0x0014},
        {"kAudioUnitProcessMultipleSelect", 0x0015},
        {"kMusicDeviceMIDIEventSelect", 0x0101},
        {"kMusicDeviceSysExSelect", 0x0102},
        {"kMusicDevicePrepareInstrumentSelect", 0x0103},
        {"kMusicDeviceReleaseInstrumentSelect", 0x0104},
        {"kMusicDeviceStartNoteSelect", 0x0105},
        {"kMusicDeviceStopNoteSelect", 0x0106},
        {"kMusicDeviceMIDIEventListSelect", 0x0107},
        {"kAudioOutputUnitStartSelect", 0x0201},
        {"kAudioOutputUnitStopSelect", 0x0202},
    };

    for (int i = 0; i < ARRSIZE(_auv2selectorstrings); i++)
        if (selector == _auv2selectorstrings[i].id)
            return _auv2selectorstrings[i].str;

    return "UNKNOWN_PROPERTY";
}
static const char* _cplugProperty2Str(AudioUnitPropertyID inID)
{
    // clang-format off
    static const struct
    {
        const char* str;
        UInt32      id;
    } _auv2propertystrings[] = {
        {"kAudioUnitProperty_ClassInfo", 0},
        {"kAudioUnitProperty_MakeConnection", 1},
        {"kAudioUnitProperty_SampleRate", 2},
        {"kAudioUnitProperty_ParameterList", 3},
        {"kAudioUnitProperty_ParameterInfo", 4},
        {"kAudioUnitProperty_FastDispatch", 5},
        {"kAudioUnitProperty_CPULoad", 6},
        {"kAudioUnitProperty_StreamFormat", 8},
        {"kAudioUnitProperty_ElementCount", 11},
        {"kAudioUnitProperty_Latency", 12},
        {"kAudioUnitProperty_SupportedNumChannels", 13},
        {"kAudioUnitProperty_MaximumFramesPerSlice", 14},
        {"kAudioUnitProperty_SetExternalBuffer", 15},
        {"kAudioUnitProperty_ParameterValueStrings", 16},
        {"kAudioUnitProperty_AudioChannelLayout", 19},
        {"kAudioUnitProperty_TailTime", 20},
        {"kAudioUnitProperty_BypassEffect", 21},
        {"kAudioUnitProperty_LastRenderError", 22},
        {"kAudioUnitProperty_SetRenderCallback", 23},
        {"kAudioUnitProperty_FactoryPresets", 24},
        {"kAudioUnitProperty_RenderQuality", 26},
        {"kAudioUnitProperty_HostCallbacks", 27},
        {"kAudioUnitProperty_CurrentPreset", 28},
        {"kAudioUnitProperty_InPlaceProcessing", 29},
        {"kAudioUnitProperty_ElementName", 30},
        {"kAudioUnitProperty_SupportedChannelLayoutTags", 32},
        {"kAudioUnitProperty_PresentPreset", 36},
        {"kAudioUnitProperty_DependentParameters", 45},
        {"kAudioUnitProperty_InputSamplesInOutput", 49},
        {"kAudioUnitProperty_ShouldAllocateBuffer", 51},
        {"kAudioUnitProperty_FrequencyResponse", 52},
        {"kAudioUnitProperty_ParameterHistoryInfo", 53},
        {"kAudioUnitProperty_NickName", 54},
        {"kAudioUnitProperty_OfflineRender", 37},
        {"kAudioUnitProperty_ParameterIDName", 34},
        {"kAudioUnitProperty_ParameterStringFromValue", 33},
        {"kAudioUnitProperty_ParameterClumpName", 35},
        {"kAudioUnitProperty_ParameterValueFromString", 38},
        {"kAudioUnitProperty_ContextName", 25},
        {"kAudioUnitProperty_PresentationLatency", 40},
        {"kAudioUnitProperty_ClassInfoFromDocument", 50},
        {"kAudioUnitProperty_RequestViewController", 56},
        {"kAudioUnitProperty_ParametersForOverview", 57},
        {"kAudioUnitProperty_SupportsMPE", 58},
        {"kAudioUnitProperty_RenderContextObserver", 60},
        {"kAudioUnitProperty_LastRenderSampleTime", 61},
        {"kAudioUnitProperty_LoadedOutOfProcess", 62},
        {"kAudioUnitProperty_GetUIComponentList", 18},
        {"kAudioUnitProperty_CocoaUI", 31},
        {"kAudioUnitProperty_IconLocation", 39},
        {"kAudioUnitProperty_AUHostIdentifier", 46},
        {"kAudioUnitProperty_MIDIOutputCallbackInfo", 47},
        {"kAudioUnitProperty_MIDIOutputCallback", 48},
        {"kAudioUnitProperty_MIDIOutputEventListCallback", 63},
        {"kAudioUnitProperty_AudioUnitMIDIProtocol", 64},
        {"kAudioUnitProperty_HostMIDIProtocol", 65},
        {"kAudioUnitProperty_MIDIOutputBufferSizeHint", 66},
        {"kMusicDeviceProperty_DualSchedulingMode", 1013},
        {"kAudioUnitProperty_UserPlugin", kAudioUnitProperty_UserPlugin}
    };
    // clang-format on

    for (int i = 0; i < ARRSIZE(_auv2propertystrings); i++)
        if (inID == _auv2propertystrings[i].id)
            return _auv2propertystrings[i].str;

    return "UNKNOWN_PROPERTY";
}

static const char* _cplugScope2Str(AudioUnitScope inScope)
{
    static const char* _auv2scopestrings[] = {
        "kAudioUnitScope_Global",
        "kAudioUnitScope_Input",
        "kAudioUnitScope_Output",
        "kAudioUnitScope_Group",
        "kAudioUnitScope_Part",
        "kAudioUnitScope_Note",
        "kAudioUnitScope_Layer",
        "kAudioUnitScope_LayerItem"};

    if (inScope < ARRSIZE(_auv2scopestrings))
        return _auv2scopestrings[inScope];

    return "UNKNOWN_SCOPE";
}

typedef struct AUv2Plugin
{
    // The AudioComponentPlugInInterface must remain first
    AudioComponentPlugInInterface mPlugInInterface;
    // Used for sending param updates to the host
    AudioComponentInstance compInstance;
    // Metadata from your bundles .plist. Hosts & auval will quary the plugin for this info.
    // This duplicate state, but required
    AudioComponentDescription desc;

    void* userPlugin;
    // Despite the name, this is actually used for getting transport state, position, and BPM.
    HostCallbackInfo mHostCallbackInfo;

    // AUv2 won't let you use C strings for bus names. It's also stated we are responsible for ownership of the string
    CFStringRef inputBusNames[CPLUG_NUM_INPUT_BUSSES];
    CFStringRef outputBusNames[CPLUG_NUM_OUTPUT_BUSSES];

    // auval make you retain this state. In theory it's to support remote I/O, which we don't, but auval test you on it
    // https://developer-mdn.apple.com/library/archive/qa/qa1777/_index.html
    UInt32                        mMaxFramesPerSlice;
    AudioUnitPropertyListenerProc maxFramesListenerProc;
    void*                         maxFramesListenerData;
    // auval doesn't ask for this property, but pluginval does, so we have to set it.
    double sampleRate;

    // Store events here because AUv2 won't simply pass us all events in a single process callback
    UInt32     numEvents;
    CplugEvent events[CPLUG_EVENT_QUEUE_SIZE];
} AUv2Plugin;

int64_t AUv2WriteProc(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    CFMutableDataRef* dataRef = (CFMutableDataRef*)stateCtx;

    if (*dataRef == NULL)
        *dataRef = CFDataCreateMutable(NULL, numBytesToWrite);

    CFDataAppendBytes(*dataRef, writePos, numBytesToWrite);
    return numBytesToWrite;
}

struct AUv2ReadStateContext
{
    uint8_t* readPos;
    size_t   bytesRemaining;
};

int64_t AUv2ReadProc(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    struct AUv2ReadStateContext* ctx = (struct AUv2ReadStateContext*)stateCtx;

    size_t bytesToActualyRead = maxBytesToRead > ctx->bytesRemaining ? ctx->bytesRemaining : maxBytesToRead;

    if (bytesToActualyRead)
    {
        memcpy(readPos, ctx->readPos, bytesToActualyRead);
        ctx->bytesRemaining -= bytesToActualyRead;
        ctx->readPos        += bytesToActualyRead;
    }

    return bytesToActualyRead;
}

// ------------------------------------------------------------------------------------------------

OSStatus AUMethodGetPropertyInfo(
    AUv2Plugin*         auv2,
    AudioUnitPropertyID inID,
    AudioUnitScope      inScope,
    AudioUnitElement    inElement,
    UInt32*             outDataSize,
    bool*               outWritable)
{
    cplug_log(
        "AUMethodGetPropertyInfo => %u (%s) %u (%s) %u %p %p",
        inID,
        _cplugProperty2Str(inID),
        inScope,
        _cplugScope2Str(inScope),
        inElement,
        outDataSize,
        outWritable);

    // NOTE: auval and some hosts may lazily pass either/both 'outDataSize' 'outDataSize' as NULL
#define CPLUG_SAFE_SET_PTR(ptr, val)                                                                                   \
    if (ptr != NULL)                                                                                                   \
        *ptr = val;

    OSStatus result = noErr;
    // default
    CPLUG_SAFE_SET_PTR(outWritable, false);

    switch (inID)
    {
    case kAudioUnitProperty_ClassInfo:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(CFPropertyListRef));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_StreamFormat:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AudioStreamBasicDescription));
        break;

    case kAudioUnitProperty_ElementCount:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(UInt32));
        break;

    case kAudioUnitProperty_SupportedNumChannels:
    {
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        UInt32 num = 0;
        if (inScope == kAudioUnitScope_Global)
            num = 1;
        else if (inScope == kAudioUnitScope_Input)
            num = CPLUG_NUM_INPUT_BUSSES;
        else if (inScope == kAudioUnitScope_Output)
            num = CPLUG_NUM_OUTPUT_BUSSES;

        CPLUG_LOG_ASSERT_RETURN(num != 0u, kAudioUnitErr_InvalidProperty);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AUChannelInfo) * num);
        break;
    }
    case kAudioUnitProperty_AudioChannelLayout:
        // Not supported.
        // auval strangely doesn't like the default case of returning 'kAudioUnitErr_InvalidProperty' here
        result = kAudioUnitErr_InvalidPropertyValue;
        break;

    case kAudioUnitProperty_SetRenderCallback:
        CPLUG_LOG_ASSERT_RETURN(inScope != kAudioUnitScope_Output, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AURenderCallbackStruct));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_FactoryPresets:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(CFArrayRef));
        break;

    case kAudioUnitProperty_PresentPreset:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AUPreset));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_ElementName:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(CFStringRef));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_ParameterList:
    {
        // Global params only, else auval starts asking for input and output parameter detials
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AudioUnitParameterID) * CPLUG_NUM_PARAMS);
        break;
    }

    case kAudioUnitProperty_ParameterInfo:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AudioUnitParameterInfo));
        break;

    case kAudioUnitProperty_Latency:
        // auval will ask for latency in the global and input scopes and then test you on it
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(Float64));
        break;

    case kAudioUnitProperty_TailTime:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(Float64));
        break;

    case kAudioUnitProperty_MaximumFramesPerSlice:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(auv2->mMaxFramesPerSlice));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_InPlaceProcessing:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(UInt32));
        CPLUG_SAFE_SET_PTR(outDataSize, true);
        break;

    case kAudioUnitProperty_SupportedChannelLayoutTags:
    {
        CPLUG_LOG_ASSERT_RETURN(inScope != kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        UInt32 num = 0;
        if (inScope == kAudioUnitScope_Input)
            num = CPLUG_NUM_INPUT_BUSSES;
        else if (inScope == kAudioUnitScope_Output)
            num = CPLUG_NUM_OUTPUT_BUSSES;

        CPLUG_LOG_ASSERT_RETURN(num != 0u, kAudioUnitErr_InvalidProperty);
        CPLUG_SAFE_SET_PTR(outDataSize, (UInt32)sizeof(AudioChannelLayoutTag) * num);
        break;
    }

    case kAudioUnitProperty_ShouldAllocateBuffer:
        CPLUG_LOG_ASSERT_RETURN(
            (inScope == kAudioUnitScope_Input || inScope == kAudioUnitScope_Output),
            kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(UInt32));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_HostCallbacks:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(HostCallbackInfo));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_CocoaUI:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AudioUnitCocoaViewInfo));
        CPLUG_SAFE_SET_PTR(outWritable, true);
        break;

    case kAudioUnitProperty_ParameterClumpName:
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(AudioUnitParameterNameInfo));
        break;

        // TODO: support MIDI out
        // case kAudioUnitProperty_MIDIOutputCallbackInfo:
        //     break;
        // case kAudioUnitProperty_MIDIOutputCallback:
        //     CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        //     *outDataSize = sizeof(AUMIDIOutputCallbackStruct);
        //     CPLUG_SAFE_SET_PTR(outWritable, true);
        //     break;

    case kAudioUnitProperty_NickName:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_SAFE_SET_PTR(outDataSize, sizeof(CFStringRef));
        break;

#if CPLUG_IS_INSTRUMENT
    case kMusicDeviceProperty_InstrumentCount:
        if (inScope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        *outDataSize = sizeof(UInt32);
        break;
#endif
    default:
        CPLUG_SAFE_SET_PTR(outDataSize, 0);
        result = kAudioUnitErr_InvalidProperty;
        break;
    }

    return result;
}

// ------------------------------------------------------------------------------------------------

/*
NOTE: auval may pass you more data then you requested. They want you to update this to the number of bytes written
You will fail auval REQUIRED PROPERTIES tests if you fail to do this.
Properties they test you on include:
  - Latency
  - Tail time
*/
static OSStatus AUMethodGetProperty(
    AUv2Plugin*         auv2,
    AudioUnitPropertyID inID,
    AudioUnitScope      inScope,
    AudioUnitElement    inElement,
    void*               outData,
    UInt32*             ioDataSize)
{
    // CPLUG_LOG_ASSERT_RETURN(outData != NULL, kAudio_ParamError)
    // CPLUG_LOG_ASSERT_RETURN(ioDataSize != NULL, kAudio_ParamError)
    cplug_log(
        "AUMethodGetProperty    => %u (%s) %u (%s) %u %p %u",
        inID,
        _cplugProperty2Str(inID),
        inScope,
        _cplugScope2Str(inScope),
        inElement,
        outData,
        *ioDataSize);

    CPLUG_LOG_ASSERT_RETURN(inScope < kAudioUnitScope_Group, kAudioUnitErr_InvalidScope);

    OSStatus result = noErr;

    switch (inID)
    {
    case kAudioUnitProperty_ClassInfo:
    {
        CFMutableDictionaryRef dict =
            CFDictionaryCreateMutable(0, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

        // Required class info
        CFStringRef versionKey      = CFStringCreateWithCString(0, kAUPresetVersionKey, 0);
        CFStringRef typeKey         = CFStringCreateWithCString(0, kAUPresetTypeKey, 0);
        CFStringRef subtypeKey      = CFStringCreateWithCString(0, kAUPresetSubtypeKey, 0);
        CFStringRef manufacturerKey = CFStringCreateWithCString(0, kAUPresetManufacturerKey, 0);
        CFStringRef presetNameKey   = CFStringCreateWithCString(0, kAUPresetNameKey, 0);
        CFStringRef presetDataKey   = CFStringCreateWithCString(0, kAUPresetDataKey, 0);

        int version      = CPLUG_AUV2_VERSION_INT;
        int type         = auv2->desc.componentType;
        int subtype      = auv2->desc.componentSubType;
        int manufacturer = auv2->desc.componentManufacturer;

        CFNumberRef      versionRef      = CFNumberCreate(0, kCFNumberSInt32Type, &version);
        CFNumberRef      typeRef         = CFNumberCreate(0, kCFNumberSInt32Type, &type);
        CFNumberRef      subtypeRef      = CFNumberCreate(0, kCFNumberSInt32Type, &subtype);
        CFNumberRef      manufacturerRef = CFNumberCreate(0, kCFNumberSInt32Type, &manufacturer);
        CFStringRef      presetNameRef   = CFStringCreateWithCString(0, "state", 0);
        CFMutableDataRef presetDataRef   = NULL;
        cplug_saveState(auv2->userPlugin, &presetDataRef, AUv2WriteProc);

        CFDictionarySetValue(dict, versionKey, versionRef);
        CFDictionarySetValue(dict, typeKey, typeRef);
        CFDictionarySetValue(dict, subtypeKey, subtypeRef);
        CFDictionarySetValue(dict, manufacturerKey, manufacturerRef);
        CFDictionarySetValue(dict, presetNameKey, presetNameRef);
        if (presetDataRef)
            CFDictionarySetValue(dict, presetDataKey, presetDataRef);

        CFRelease(versionKey);
        CFRelease(typeKey);
        CFRelease(subtypeKey);
        CFRelease(manufacturerKey);
        CFRelease(presetNameKey);
        CFRelease(presetDataKey);

        CFRelease(versionRef);
        CFRelease(typeRef);
        CFRelease(subtypeRef);
        CFRelease(manufacturerRef);
        CFRelease(presetNameRef);
        CFRelease(presetDataRef);

        *(CFPropertyListRef*)outData = dict;
        break;
    }

    case kAudioUnitProperty_SampleRate:
        *(Float64*)outData = auv2->sampleRate;
        break;

    case kAudioUnitProperty_ParameterList:
    {
        AudioUnitParameterID* paramList = (AudioUnitParameterID*)(outData);
        for (UInt32 i = 0; i < CPLUG_NUM_PARAMS; i++)
            paramList[i] = i;
        break;
    }

    case kAudioUnitProperty_ParameterInfo:
    {
        AudioUnitParameterInfo* paramInfo = outData;

        const char* name = cplug_getParameterName(auv2->userPlugin, inElement);
        snprintf(paramInfo->name, sizeof(paramInfo->name), "%s", name);

        // Support unit names? Nah. The less CFStrings the better
        // paramInfo->unitName

        double min, max;
        cplug_getParameterRange(auv2->userPlugin, inElement, &min, &max);
        const uint32_t hints      = cplug_getParameterFlags(auv2->userPlugin, inElement);
        const float    defaultVal = cplug_getDefaultParameterValue(auv2->userPlugin, inElement);

        paramInfo->unit = 0;
        if (hints & CPLUG_FLAG_PARAMETER_IS_BOOL)
            paramInfo->unit = kAudioUnitParameterUnit_Boolean;
        // Audio units appear not to support integer values.
        // They do have a unit type 'indexed', which is meant to be paired with an array of names.
        // We don't support that either because apple want US to retain a bunch of CFArrays and CFStrings
        // That's dumb, and a simpler alternative is to just use formatted strings instead.
        // https://developer.apple.com/documentation/audiotoolbox/1534199-general_audio_unit_properties/kaudiounitproperty_parametervaluestrings/

        paramInfo->minValue     = min;
        paramInfo->maxValue     = max;
        paramInfo->defaultValue = defaultVal;

        // Using HasName causes the host to repeatedly call for kAudioUnitProperty_ParameterStringFromValue.
        // The downside to this is that it allocates a CFString. The upside is we can add value suffixes and indexed
        // param labels in a single function
        paramInfo->flags = kAudioUnitParameterFlag_HasName | kAudioUnitParameterFlag_IsReadable;
        if ((hints & CPLUG_FLAG_PARAMETER_IS_READ_ONLY) == 0)
            paramInfo->flags |= kAudioUnitParameterFlag_IsWritable;

        break;
    }

    case kAudioUnitProperty_StreamFormat:
    {
        AudioStreamBasicDescription* desc = (AudioStreamBasicDescription*)outData;

        int nChannels = 2;
        if (inScope == kAudioUnitScope_Input)
            nChannels = cplug_getInputBusChannelCount(auv2->userPlugin, inElement);
        if (inScope == kAudioUnitScope_Output)
            nChannels = cplug_getOutputBusChannelCount(auv2->userPlugin, inElement);

        desc->mSampleRate       = auv2->sampleRate;
        desc->mFormatID         = kAudioFormatLinearPCM;
        desc->mFormatFlags      = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        desc->mBytesPerPacket   = sizeof(float);
        desc->mFramesPerPacket  = 1;
        desc->mBytesPerFrame    = sizeof(float);
        desc->mChannelsPerFrame = nChannels;
        desc->mBitsPerChannel   = 32;
        desc->mReserved         = 0;
        break;
    }

    case kAudioUnitProperty_ElementCount:
    {
        UInt32 numBusses = 0;

        if (inScope == kAudioUnitScope_Global)
            numBusses = 1;
        else if (inScope == kAudioUnitScope_Input)
            // In Logic Pro, every instrument must receive an input (eg. sidechain) whether you want to or not.
            // If you don't do this, Logic Pro will silently fail to load your plugin.
            // This is not a problem in other hosts such as Ableton, FL and even auval.
            numBusses = CPLUG_NUM_INPUT_BUSSES == 0 ? 1 : CPLUG_NUM_INPUT_BUSSES;
        else if (inScope == kAudioUnitScope_Output)
            numBusses = CPLUG_NUM_OUTPUT_BUSSES;

        *(UInt32*)outData = numBusses;
        break;
    }

    case kAudioUnitProperty_Latency:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        *(Float64*)outData = cplug_getLatencyInSamples(auv2->userPlugin);
        *ioDataSize        = sizeof(Float64);
        break;

    case kAudioUnitProperty_SupportedNumChannels:
    {
        AUChannelInfo* infoArr = outData;
        for (int i = 0; i < *ioDataSize / sizeof(*infoArr); i++)
        {
            int inChannels         = cplug_getInputBusChannelCount(auv2->userPlugin, i);
            int outChannels        = cplug_getOutputBusChannelCount(auv2->userPlugin, i);
            infoArr[i].inChannels  = inChannels;
            infoArr[i].outChannels = outChannels;
        }
        break;
    }

    case kAudioUnitProperty_MaximumFramesPerSlice:
        *(UInt32*)(outData) = auv2->mMaxFramesPerSlice;
        *ioDataSize         = sizeof(auv2->mMaxFramesPerSlice);
        break;

    case kAudioUnitProperty_TailTime:
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        *(Float64*)outData = cplug_getTailInSamples(auv2->userPlugin);
        *ioDataSize        = sizeof(Float64);
        break;

    case kAudioUnitProperty_InPlaceProcessing:
        *(UInt32*)outData = 1;
        *ioDataSize       = sizeof(UInt32);
        break;

    case kAudioUnitProperty_ElementName:
    {
        if (inScope == kAudioUnitScope_Input)
        {
            if (inElement < CPLUG_NUM_INPUT_BUSSES)
            {
                if (auv2->inputBusNames[inElement] == NULL)
                {
                    const char* name               = cplug_getInputBusName(auv2->userPlugin, inElement);
                    auv2->inputBusNames[inElement] = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
                    CFRetain(auv2->inputBusNames[inElement]);
                }
                *(CFStringRef*)(outData) = auv2->inputBusNames[inElement];
                return noErr;
            }
        }
        if (inScope == kAudioUnitScope_Output)
        {
            if (inElement < CPLUG_NUM_OUTPUT_BUSSES)
            {
                if (auv2->outputBusNames[inElement] == NULL)
                {
                    const char* name                = cplug_getOutputBusName(auv2->userPlugin, inElement);
                    auv2->outputBusNames[inElement] = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
                    CFRetain(auv2->outputBusNames[inElement]);
                }
                *(CFStringRef*)(outData) = auv2->outputBusNames[inElement];
                return noErr;
            }
        }
        result = kAudioUnitErr_PropertyNotInUse;
        break;
    }

    case kAudioUnitProperty_CocoaUI:
    {
        AudioUnitCocoaViewInfo* info = (AudioUnitCocoaViewInfo*)outData;
        // AUv2 docs tell you to bundle your Cocoa GUI as a seperate App bundle nested inside your .component bundle.
        // For most people, including CPLUG, this is s̶t̶u̶p̶i̶d̶ ̶a̶n̶d̶ ̶a̶n̶n̶o̶y̶i̶n̶g̶ intrusive to our build system.
        // Here we simply point back to our .component bundle, tricking the host. JUCE, iPlug2 & DPlug all do the same.
        CFStringRef bundleID             = CFStringCreateWithCString(0, CPLUG_AUV2_BUNDLE_ID, kCFStringEncodingUTF8);
        CFBundleRef bundle               = CFBundleGetBundleWithIdentifier(bundleID);
        info->mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(bundle);
        info->mCocoaAUViewClass[0] = CFStringCreateWithCString(0, CPLUG_AUV2_VIEW_CLASS_STR, kCFStringEncodingUTF8);
        CFRelease(bundleID);
        break;
    }

    case kAudioUnitProperty_ParameterStringFromValue:
    {
        AudioUnitParameterStringFromValue* sfv = (AudioUnitParameterStringFromValue*)outData;

        char   buf[64];
        double value = (double)*sfv->inValue;
        cplug_parameterValueToString(auv2->userPlugin, sfv->inParamID, buf, sizeof(buf), value);
        sfv->outString = CFStringCreateWithCString(0, buf, kCFStringEncodingUTF8);
        break;
    }

    case kAudioUnitProperty_ParameterValueFromString:
    {
        AudioUnitParameterValueFromString* vfs = (AudioUnitParameterValueFromString*)outData;

        const char* str = CFStringGetCStringPtr(vfs->inString, kCFStringEncodingUTF8);
        vfs->outValue   = cplug_parameterStringToValue(auv2->userPlugin, vfs->inParamID, str);
        break;
    }

    // support this?
    // case kAudioUnitProperty_CurrentPreset:
    // auval will fail you if you don't give them an allocated string
    case kAudioUnitProperty_PresentPreset:
    {
        AUPreset* preset     = (AUPreset*)outData;
        preset->presetNumber = 0;
        preset->presetName   = CFStringCreateWithCString(0, "", kCFStringEncodingUTF8);
        break;
    }

    // NOTE: Setting this may cause your MIDI to be sent using the method returned by kMusicDeviceMIDIEventListSelect
// #if CPLUG_WANT_MIDI_INPUT
//     case kAudioUnitProperty_AudioUnitMIDIProtocol:
//         *(SInt32*)outData = kMIDIProtocol_1_0;
//         *ioDataSize       = sizeof(SInt32);
//         break;
// #endif
#if CPLUG_IS_INSTRUMENT
    case kMusicDeviceProperty_InstrumentCount:
        if (inScope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        *(UInt32*)outData = 1;
        *ioDataSize       = sizeof(UInt32);
        break;
#endif
    case kAudioUnitProperty_UserPlugin:
        *(UInt64*)outData = (UInt64)auv2->userPlugin;
        break;

    default:
        result = kAudioUnitErr_InvalidProperty;
        break;
    }
    return result;
}

static OSStatus AUMethodSetProperty(
    AUv2Plugin*         auv2,
    AudioUnitPropertyID inID,
    AudioUnitScope      inScope,
    AudioUnitElement    inElement,
    const void*         inData,
    UInt32              inDataSize)
{
    cplug_log(
        "AUMethodSetProperty => %u (%s) %u (%s) %u %p %u",
        inID,
        _cplugProperty2Str(inID),
        inScope,
        _cplugScope2Str(inScope),
        inElement,
        inData,
        inDataSize);
    CPLUG_LOG_ASSERT_RETURN(inData != NULL, kAudioUnitErr_InvalidParameterValue)
    CPLUG_LOG_ASSERT_RETURN(inDataSize != 0, kAudioUnitErr_InvalidParameterValue)

    OSStatus result = noErr;
    switch (inID)
    {
    case kAudioUnitProperty_ClassInfo:
    {
        CPLUG_LOG_ASSERT_RETURN(inDataSize == sizeof(CFPropertyListRef*), kAudioUnitErr_InvalidPropertyValue);
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);

        CFPropertyListRef* propList      = (CFPropertyListRef*)inData;
        CFStringRef        presetDataKey = CFStringCreateWithCString(0, kAUPresetDataKey, 0);
        CFMutableDataRef   presetData    = (CFMutableDataRef)CFDictionaryGetValue(*propList, presetDataKey);
        if (presetData)
        {
            struct AUv2ReadStateContext readCtx;
            readCtx.readPos        = CFDataGetMutableBytePtr(presetData);
            readCtx.bytesRemaining = CFDataGetLength(presetData);

            cplug_loadState(auv2->userPlugin, &readCtx, AUv2ReadProc);
        }
        CFRelease(presetDataKey);
        break;
    }

    case kAudioUnitProperty_MakeConnection:
        // const AudioUnitConnection* conn = (const AudioUnitConnection*)inData;
        // pretend to set connection
        break;

    case kAudioUnitProperty_SampleRate:
    {
        auv2->sampleRate = *(Float64*)inData;
        cplug_setSampleRateAndBlockSize(auv2->userPlugin, auv2->sampleRate, auv2->mMaxFramesPerSlice);
        break;
    }

    case kAudioUnitProperty_StreamFormat:
    {
        AudioStreamBasicDescription* desc = (AudioStreamBasicDescription*)inData;

        int nChannels = 0;
        switch (inScope)
        {
        case kAudioUnitScope_Global:
            nChannels = 1;
            break;
        case kAudioUnitScope_Input:
            nChannels = cplug_getInputBusChannelCount(auv2->userPlugin, inElement);
            break;
        case kAudioUnitScope_Output:
            nChannels = cplug_getOutputBusChannelCount(auv2->userPlugin, inElement);
            break;
        default:
            break;
        }
        CPLUG_LOG_ASSERT_RETURN(desc->mChannelsPerFrame <= nChannels, kAudioUnitErr_FormatNotSupported);

        cplug_setSampleRateAndBlockSize(auv2->userPlugin, desc->mSampleRate, auv2->mMaxFramesPerSlice);
        break;
    }
    case kAudioUnitProperty_MaximumFramesPerSlice:
        CPLUG_LOG_ASSERT_RETURN(inDataSize == sizeof(UInt32), kAudioUnitErr_InvalidPropertyValue);
        auv2->mMaxFramesPerSlice = *(UInt32*)inData;
        if (auv2->maxFramesListenerProc)
            auv2->maxFramesListenerProc(auv2->maxFramesListenerData, (AudioUnit)auv2, inID, inScope, inElement);
        break;

    case kAudioUnitProperty_SetRenderCallback:
    {
        // Pretend to set this. auval only test that you set it, not that you use it
        break;
    }

    case kAudioUnitProperty_PresentPreset:
    {
        CPLUG_LOG_ASSERT_RETURN(inDataSize == sizeof(AUPreset), kAudioUnitErr_InvalidPropertyValue);
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        // Pretend to set preset
        break;
    }

    case kAudioUnitProperty_HostCallbacks:
    {
        CPLUG_LOG_ASSERT_RETURN(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
        CPLUG_LOG_ASSERT_RETURN(inDataSize >= sizeof(auv2->mHostCallbackInfo), kAudioUnitErr_InvalidParameterValue);
        memcpy(&auv2->mHostCallbackInfo, inData, sizeof(auv2->mHostCallbackInfo));
        break;
    }

    default:
        result = kAudioUnitErr_InvalidProperty;

        break;
    }

    return result;
}

static OSStatus AUMethodAddPropertyListener(
    AUv2Plugin*                   auv2,
    AudioUnitPropertyID           prop,
    AudioUnitPropertyListenerProc proc,
    void*                         userData)
{
    cplug_log("AUMethodAddPropertyListener => %u (%s) %p %p", prop, _cplugProperty2Str(prop), proc, userData);

    switch (prop)
    {
    case kAudioUnitProperty_MaximumFramesPerSlice:
        auv2->maxFramesListenerProc = proc;
        auv2->maxFramesListenerData = userData;
        break;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
}

static OSStatus
AUMethodRemovePropertyListener(AUv2Plugin* auv2, AudioUnitPropertyID prop, AudioUnitPropertyListenerProc proc)
{
    cplug_log("AUMethodRemovePropertyListener => %u (%s) %p %p", prop, _cplugProperty2Str(prop), proc);

    switch (prop)
    {
    case kAudioUnitProperty_MaximumFramesPerSlice:
        auv2->maxFramesListenerProc = NULL;
        auv2->maxFramesListenerData = NULL;
        break;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
}

static OSStatus AUMethodRemovePropertyListenerWithUserData(
    AUv2Plugin*                   auv2,
    AudioUnitPropertyID           prop,
    AudioUnitPropertyListenerProc proc,
    void*                         userData)
{
    cplug_log(
        "AUMethodRemovePropertyListenerWithUserData => %u (%s) %p %p",
        prop,
        _cplugProperty2Str(prop),
        proc,
        userData);

    switch (prop)
    {
    case kAudioUnitProperty_MaximumFramesPerSlice:
        auv2->maxFramesListenerProc = NULL;
        auv2->maxFramesListenerData = NULL;
        break;
    default:
        return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
}

static OSStatus AUMethodAddRenderNotify(AUv2Plugin* auv2, AURenderCallback proc, void* userData)
{
    cplug_log("AUMethodAddRenderNotify => %u %p %p", proc, userData);
    // Pretend to do something
    return noErr;
}

static OSStatus AUMethodRemoveRenderNotify(AUv2Plugin* auv2, AURenderCallback proc, void* userData)
{
    cplug_log("AUMethodRemoveRenderNotify => %u %p %p", proc, userData);
    // Pretend to do something
    return noErr;
}

static OSStatus AUMethodGetParameter(
    AUv2Plugin*              auv2,
    AudioUnitParameterID     param,
    AudioUnitScope           scope,
    AudioUnitElement         elem,
    AudioUnitParameterValue* value)
{
    // cplug_log("AUMethodGetParameter => %u %s %u %p", param, _cplugScope2Str(scope), elem, value);
    CPLUG_LOG_ASSERT_RETURN(elem < CPLUG_NUM_PARAMS, kAudioUnitErr_InvalidParameter);
    CPLUG_LOG_ASSERT_RETURN(auv2->userPlugin != NULL, kAudioUnitErr_Uninitialized);
    *value = (AudioUnitParameterValue)cplug_getParameterValue(auv2->userPlugin, param);
    return noErr;
}

// this is a (potentially) realtime method; no lock
static OSStatus AUMethodSetParameter(
    AUv2Plugin*             auv2,
    AudioUnitParameterID    param,
    AudioUnitScope          scope,
    AudioUnitElement        elem,
    AudioUnitParameterValue value,
    UInt32                  bufferOffset)
{
    // cplug_log("AUMethodSetParameter => %u %s %u %f %u", param, _cplugScope2Str(scope), elem, value, bufferOffset);
    CPLUG_LOG_ASSERT_RETURN(isfinite(value), kAudioUnitErr_InvalidParameter);
    CPLUG_LOG_ASSERT_RETURN(param < CPLUG_NUM_PARAMS, kAudioUnitErr_InvalidParameter);
    CPLUG_LOG_ASSERT_RETURN(auv2->userPlugin != NULL, kAudioUnitErr_Uninitialized);

    if (! isfinite(value))
        return kAudioUnitErr_InvalidParameterValue;

    cplug_setParameterValue(auv2->userPlugin, param, value);
    return noErr;
}

static OSStatus AUMethodScheduleParameters(AUv2Plugin* auv2, const AudioUnitParameterEvent* events, UInt32 numEvents)
{
    cplug_log("AUMethodScheduleParameters => %p %u", events, numEvents);
    CPLUG_LOG_ASSERT_RETURN(events != NULL, kAudioUnitErr_InvalidParameterValue);

    OSStatus status = noErr;

    for (UInt32 i = 0; i < numEvents; ++i)
    {
        const AudioUnitParameterEvent* event = &events[i];
        switch (event->eventType)
        {
        case kParameterEvent_Immediate:
            CPLUG_LOG_ASSERT(isfinite(event->eventValues.immediate.value));
            cplug_setParameterValue(auv2->userPlugin, event->parameter, event->eventValues.immediate.value);
            break;
        case kParameterEvent_Ramped:
            CPLUG_LOG_ASSERT(isfinite(event->eventValues.ramp.startValue));
            CPLUG_LOG_ASSERT(isfinite(event->eventValues.ramp.endValue));
            status = kAudioUnitErr_ExtensionNotFound;
            break;
        default:
            break;
        }
    }

    return status;
}

static OSStatus AUMethodInitializeProcessing(AUv2Plugin* auv2)
{
    cplug_log("AUMethodInitializeProcessing");
    // Despite this 'initialize' naming convention, the bahaviour of this method is more closely aligned with VST3
    // IComponent::setActive. We don't currently support this feature.
    // https://developer.apple.com/documentation/audiotoolbox/1439851-audiounitinitialize?language=objc
    return noErr;
}

static OSStatus AUMethodUninitializeProcessing(AUv2Plugin* auv2)
{
    cplug_log("AUMethodUninitializeProcessing");
    // Read comments in AUMethodInitialize
    return noErr;
}

typedef struct AUv2ProcessContextTranslator
{
    CplugProcessContext cplugContext;
    AUv2Plugin*         auv2;
    UInt32              midiIdx;
    float*              channels[2];
} AUv2ProcessContextTranslator;

bool AUv2ProcessContextTranslator_enqueueEvent(CplugProcessContext* ctx, const CplugEvent* event, uint32_t frameIdx)
{
    // cplug_log("AUv2ProcessContextTranslator_enqueueEvent => %u", event->type);
    AUv2ProcessContextTranslator* translator = (AUv2ProcessContextTranslator*)ctx;

    switch (event->type)
    {
    case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
    {
        AudioUnitEvent auevent;
        auevent.mEventType                        = kAudioUnitEvent_ParameterValueChange;
        auevent.mArgument.mParameter.mAudioUnit   = translator->auv2->compInstance;
        auevent.mArgument.mParameter.mParameterID = event->parameter.idx;
        auevent.mArgument.mParameter.mScope       = kAudioUnitScope_Global;
        auevent.mArgument.mParameter.mElement     = 0;
        OSStatus status                           = AUEventListenerNotify(NULL, NULL, &auevent);
        CPLUG_LOG_ASSERT(status == noErr);
        return true;
    }
    case CPLUG_EVENT_PARAM_CHANGE_BEGIN:
    {
        AudioUnitEvent auevent;
        auevent.mEventType                        = kAudioUnitEvent_BeginParameterChangeGesture;
        auevent.mArgument.mParameter.mAudioUnit   = translator->auv2->compInstance;
        auevent.mArgument.mParameter.mParameterID = event->parameter.idx;
        auevent.mArgument.mParameter.mScope       = kAudioUnitScope_Global;
        auevent.mArgument.mParameter.mElement     = 0;
        OSStatus status                           = AUEventListenerNotify(NULL, NULL, &auevent);
        CPLUG_LOG_ASSERT(status == noErr);
        return true;
    }
    case CPLUG_EVENT_PARAM_CHANGE_END:
    {
        AudioUnitEvent auevent;
        auevent.mEventType                        = kAudioUnitEvent_EndParameterChangeGesture;
        auevent.mArgument.mParameter.mAudioUnit   = translator->auv2->compInstance;
        auevent.mArgument.mParameter.mParameterID = event->parameter.idx;
        auevent.mArgument.mParameter.mScope       = kAudioUnitScope_Global;
        auevent.mArgument.mParameter.mElement     = 0;
        OSStatus status                           = AUEventListenerNotify(NULL, NULL, &auevent);
        CPLUG_LOG_ASSERT(status == noErr);
        return true;
    }
    default:
        return false;
    }
}

bool AUv2ProcessContextTranslator_dequeueEvent(CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    AUv2ProcessContextTranslator* translator = (AUv2ProcessContextTranslator*)ctx;

    if (frameIdx >= translator->cplugContext.numFrames)
        return false;

    if (translator->midiIdx == translator->auv2->numEvents)
    {
        event->type                  = CPLUG_EVENT_PROCESS_AUDIO;
        event->processAudio.endFrame = translator->cplugContext.numFrames;
        return true;
    }

    CPLUG_LOG_ASSERT(translator->midiIdx < ARRSIZE(translator->auv2->events));
    const CplugEvent* cachedEvent = &translator->auv2->events[translator->midiIdx];
    if (cachedEvent->midi.frame != frameIdx)
    {
        event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
        event->processAudio.endFrame = cachedEvent->midi.frame;
        return true;
    }

    // Send MIDI event
    *event = *cachedEvent;
    translator->midiIdx++;

    return true;
}

float** AUv2ProcessContextTranslator_getAudioInput(const CplugProcessContext* ctx, uint32_t busIdx)
{
    const AUv2ProcessContextTranslator* translator = (const AUv2ProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT(busIdx == 0); // TODO: support more busses
    return (float**)translator->channels;
}
float** AUv2ProcessContextTranslator_getAudioOutput(const CplugProcessContext* ctx, uint32_t busIdx)
{
    const AUv2ProcessContextTranslator* translator = (const AUv2ProcessContextTranslator*)ctx;
    CPLUG_LOG_ASSERT(busIdx == 0); // TODO: support more busses
    return (float**)translator->channels;
}

static OSStatus AUMethodProcessAudio(
    AUv2Plugin*                 auv2,
    AudioUnitRenderActionFlags* ioActionFlags,
    const AudioTimeStamp*       inTimeStamp,
    UInt32                      inOutputBusNumber,
    UInt32                      inNumberFrames,
    AudioBufferList*            ioData)
{
    // cplug_log(
    //     "AUMethodProcessAudio => %u %p %u %u %p",
    //     *ioActionFlags,
    //     inTimeStamp,
    //     inOutputBusNumber,
    //     inNumberFrames,
    //     ioData);
    // The very smart people at Apple test you on this
    CPLUG_LOG_ASSERT_RETURN(inNumberFrames <= auv2->mMaxFramesPerSlice, kAudioUnitErr_TooManyFramesToProcess);

    if (*ioActionFlags == 0 || (*ioActionFlags & kAudioUnitRenderAction_DoNotCheckRenderArgs))
    {
        AUv2ProcessContextTranslator translator = {0};

        CplugProcessContext* ctx    = &translator.cplugContext;
        HostCallbackInfo*    hostcb = &auv2->mHostCallbackInfo;

        ctx->numFrames = inNumberFrames;

        if (hostcb->beatAndTempoProc)
        {
            hostcb->beatAndTempoProc(hostcb->hostUserData, &ctx->playheadBeats, &ctx->bpm);

            if (ctx->playheadBeats != 0)
                ctx->flags |= CPLUG_FLAG_TRANSPORT_HAS_PLAYHEAD_BEATS;
            if (ctx->bpm != 0)
                ctx->flags |= CPLUG_FLAG_TRANSPORT_HAS_BPM;
        }
        if (hostcb->musicalTimeLocationProc)
        {
            Float32 timesigNum = 0;
            hostcb->musicalTimeLocationProc(hostcb->hostUserData, NULL, &timesigNum, &ctx->timeSigDenominator, NULL);
            if (timesigNum != 0)
            {
                ctx->flags            |= CPLUG_FLAG_TRANSPORT_HAS_TIME_SIGNATURE;
                ctx->timeSigNumerator  = timesigNum;
            }
        }
        // Ableton 10 doesn't support transportStateProc2, so this should be our first choice
        if (hostcb->transportStateProc)
        {
            Boolean isPlaying = false, isLooping = false;
            hostcb->transportStateProc(
                hostcb->hostUserData,
                &isPlaying,
                NULL,
                NULL,
                &isLooping,
                &ctx->loopStartBeats,
                &ctx->loopEndBeats);

            if (isPlaying)
                ctx->flags |= CPLUG_FLAG_TRANSPORT_IS_PLAYING;
            if (isLooping)
                ctx->flags |= CPLUG_FLAG_TRANSPORT_IS_LOOPING;
        }
        if (hostcb->transportStateProc2)
        {
            Boolean isRecording = false;
            hostcb->transportStateProc2(hostcb->hostUserData, NULL, &isRecording, NULL, NULL, NULL, NULL, NULL);
            if (isRecording)
                ctx->flags |= CPLUG_FLAG_TRANSPORT_IS_RECORDING;
        }

        ctx->enqueueEvent   = AUv2ProcessContextTranslator_enqueueEvent;
        ctx->dequeueEvent   = AUv2ProcessContextTranslator_dequeueEvent;
        ctx->getAudioInput  = AUv2ProcessContextTranslator_getAudioInput;
        ctx->getAudioOutput = AUv2ProcessContextTranslator_getAudioOutput;

        translator.auv2    = auv2;
        translator.midiIdx = 0;

        CPLUG_LOG_ASSERT(ioData->mNumberBuffers == 2);
        for (int i = 0; i < ioData->mNumberBuffers; i++)
        {
            int numChannels = ioData->mBuffers[i].mNumberChannels;
            CPLUG_LOG_ASSERT(numChannels == 1);
            // The very smart people at Apple test you on this. Yes you actually have to return noErr.
            CPLUG_LOG_ASSERT_RETURN(ioData->mBuffers[i].mData != NULL, noErr);
            translator.channels[i] = (float*)ioData->mBuffers[i].mData;
        }

        cplug_process(auv2->userPlugin, &translator.cplugContext);
        // Clear MIDI event list
        auv2->numEvents = 0;
    }

    return noErr;
}

static OSStatus AUMethodResetProcessing(AUv2Plugin* auv2, AudioUnitScope scope, AudioUnitElement elem)
{
    cplug_log("AUMethodResetProcessing => %u %u", scope, elem);
    // TODO: support this?
    // a less confusing name for this function would be "stop all audio"
    // https://developer.apple.com/documentation/audiotoolbox/1439607-audiounitreset?language=objc
    return noErr;
}

#if CPLUG_IS_INSTRUMENT
static OSStatus AUMethodMusicDeviceMIDIEventProc(
    AUv2Plugin* auv2,
    UInt32      inStatus,
    UInt32      inData1,
    UInt32      inData2,
    UInt32      inOffsetSampleFrame)
{
    cplug_log("AUMethodMusicDeviceMIDIEventProc => %u %u %u %u", inStatus, inData1, inData2, inOffsetSampleFrame);
    if (auv2->numEvents < ARRSIZE(auv2->events))
    {
        CplugEvent* event  = &auv2->events[auv2->numEvents];
        event->type        = CPLUG_EVENT_MIDI;
        event->midi.frame  = inOffsetSampleFrame;
        event->midi.status = inStatus;
        event->midi.data1  = inData1;
        event->midi.data2  = inData2;
        auv2->numEvents++;
    }
    return noErr;
}

static OSStatus AUMethodMusicDeviceSysExProc(AUv2Plugin* auv2, const UInt8* inData, UInt32 inLength)
{
    cplug_log("AUMethodMusicDeviceSysExProc => %p %u", inData, inLength);
    return noErr;
}
#endif

static AudioComponentMethod AULookup(SInt16 selector)
{
    cplug_log("AULookup => %hd (%s)", selector, _cplugLookup2Str(selector));
    // Logic Pro will ask for 32767/0x7fff?
    switch (selector)
    {
    case kAudioUnitInitializeSelect:
        return (AudioComponentMethod)AUMethodInitializeProcessing;
    case kAudioUnitUninitializeSelect:
        return (AudioComponentMethod)AUMethodUninitializeProcessing;
    case kAudioUnitGetPropertyInfoSelect:
        return (AudioComponentMethod)AUMethodGetPropertyInfo;
    case kAudioUnitGetPropertySelect:
        return (AudioComponentMethod)AUMethodGetProperty;
    case kAudioUnitSetPropertySelect:
        return (AudioComponentMethod)AUMethodSetProperty;
    case kAudioUnitAddPropertyListenerSelect:
        return (AudioComponentMethod)AUMethodAddPropertyListener;
    case kAudioUnitRemovePropertyListenerSelect:
        return (AudioComponentMethod)AUMethodRemovePropertyListener;
    case kAudioUnitRemovePropertyListenerWithUserDataSelect:
        return (AudioComponentMethod)AUMethodRemovePropertyListenerWithUserData;
    case kAudioUnitAddRenderNotifySelect:
        return (AudioComponentMethod)AUMethodAddRenderNotify;
    case kAudioUnitRemoveRenderNotifySelect:
        return (AudioComponentMethod)AUMethodRemoveRenderNotify;
    case kAudioUnitGetParameterSelect:
        return (AudioComponentMethod)AUMethodGetParameter;
    case kAudioUnitSetParameterSelect:
        return (AudioComponentMethod)AUMethodSetParameter;
    case kAudioUnitScheduleParametersSelect:
        return (AudioComponentMethod)AUMethodScheduleParameters;
    case kAudioUnitRenderSelect:
        return (AudioComponentMethod)AUMethodProcessAudio;
    case kAudioUnitResetSelect:
        return (AudioComponentMethod)AUMethodResetProcessing;
    case kMusicDevicePrepareInstrumentSelect:
    case kMusicDeviceReleaseInstrumentSelect:
        break; // these are long deprecated
#if CPLUG_IS_INSTRUMENT
    case kMusicDeviceMIDIEventSelect:
        return (AudioComponentMethod)AUMethodMusicDeviceMIDIEventProc;
    case kMusicDeviceSysExSelect:
        return (AudioComponentMethod)AUMethodMusicDeviceSysExProc;
#endif
    default:
        cplug_log("WARNING: NO PROC FOR %hd (%s)", selector, _cplugLookup2Str(selector));
        break;
    }
    return NULL;
}

OSStatus ComponentBase_AP_Open(AUv2Plugin* auv2, AudioComponentInstance compInstance)
{
    cplug_log("ComponentBase_AP_Open");
    auv2->compInstance = compInstance;
    auv2->userPlugin   = cplug_createPlugin();
    return auv2->userPlugin != NULL ? noErr : kAudioUnitErr_FailedInitialization;
}

OSStatus ComponentBase_AP_Close(AUv2Plugin* auv2)
{
    cplug_log("ComponentBase_AP_Close");
    cplug_destroyPlugin(auv2->userPlugin);

    for (int i = 0; i < CPLUG_NUM_INPUT_BUSSES; i++)
        if (auv2->inputBusNames[i] != NULL)
            CFRelease(auv2->inputBusNames[i]);
    for (int i = 0; i < CPLUG_NUM_OUTPUT_BUSSES; i++)
        if (auv2->outputBusNames[i] != NULL)
            CFRelease(auv2->outputBusNames[i]);

    free(auv2);

    int numInstances = __atomic_fetch_sub(&g_auv2InstanceCount, 1, __ATOMIC_SEQ_CST);
    if (numInstances == 1)
        cplug_libraryUnload();

    return noErr;
}

__attribute__((visibility("default"))) void* GetPluginFactory(const AudioComponentDescription* inDesc)
{
    cplug_log("GetPluginFactory");

    int numInstances = __atomic_fetch_add(&g_auv2InstanceCount, 1, __ATOMIC_SEQ_CST);
    if (numInstances == 0)
        cplug_libraryLoad();

    AUv2Plugin* auv2 = (AUv2Plugin*)(malloc(sizeof(AUv2Plugin)));
    memset(auv2, 0, sizeof(*auv2));

    auv2->mPlugInInterface.Open     = (OSStatus(*)(void*, AudioComponentInstance))ComponentBase_AP_Open;
    auv2->mPlugInInterface.Close    = (OSStatus(*)(void*))ComponentBase_AP_Close;
    auv2->mPlugInInterface.Lookup   = AULookup;
    auv2->mPlugInInterface.reserved = NULL;

    auv2->desc = *inDesc;

    auv2->mMaxFramesPerSlice = kAUDefaultMaxFramesPerSlice;
    auv2->sampleRate         = kAUDefaultSampleRate;
    return auv2;
}