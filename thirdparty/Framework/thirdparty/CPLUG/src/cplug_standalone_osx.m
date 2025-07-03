/* Released into the public domain by Tré Dudman - 2024
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#include <AppKit/AppKit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreServices/CoreServices.h>
#include <cplug.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <pthread.h>

#define CPLUG_MIDI_RINGBUFFER_SIZE 128
#define MAX_BLOCK_SIZE             2048

#define USER_SAMPLE_RATE  44100
#define USER_BLOCK_SIZE   512
#define USER_NUM_CHANNELS 2

#ifndef ARRSIZE
#define ARRSIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define unlikely(x)  __builtin_expect(x, 0)
#define SLEEP_MS(ms) usleep(ms * 1000)

#define cplug_assert(cond) (cond) ? (void)0 : __builtin_debugtrap()

#pragma mark -Structs

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@interface WindowDelegate : NSObject <NSWindowDelegate>
@end

typedef struct MIDIMessage
{
    union
    {
        struct
        {
            unsigned char status;
            unsigned char data1;
            unsigned char data2;
        };
        unsigned char bytes[4];
        unsigned int  bytesAsInt;
    };
    /* Milliseconds since first connected to MIDI port */
    unsigned int timestampMs;
} MIDIMessage;

typedef struct MidiRingBuffer
{
    volatile int writePos;
    volatile int readPos;

    MIDIMessage buffer[CPLUG_MIDI_RINGBUFFER_SIZE];
} MidiRingBuffer;

enum MainMenuTag
{
    MainMenuTagApp,
    MainMenuTagAudio,
    MainMenuTagMIDI,
};
enum AudioMenuTag
{
    AudioMenuTagSampleRate,
    AudioMenuTagBlockSize,
    AudioMenuTagOutput,
};
enum MIDIMenuTag
{
    MIDIMenuTagInput,
};

#pragma mark -Global state

struct STAND_Plugin
{
#ifdef HOTRELOAD_LIB_PATH
    void* library;
#endif
    void* userPlugin;
    void* userGUI;

    void (*libraryLoad)();
    void (*libraryUnload)();
    void* (*createPlugin)();
    void (*destroyPlugin)(void* userPlugin);
    uint32_t (*getOutputBusChannelCount)(void*, uint32_t bus_idx);
    void (*setSampleRateAndBlockSize)(void*, double sampleRate, uint32_t maxBlockSize);
    void (*process)(void* userPlugin, CplugProcessContext* ctx);
    void (*saveState)(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc);
    void (*loadState)(void* userPlugin, const void* stateCtx, cplug_readProc readProc);

    void* (*createGUI)(void* userPlugin);
    void (*destroyGUI)(void* userGUI);
    void (*setParent)(void* userGUI, void* hwnd_or_nsview);
    void (*setVisible)(void* userGUI, bool visible);
    void (*setScaleFactor)(void* userGUI, float scale);
    void (*getSize)(void* userGUI, uint32_t* width, uint32_t* height);
    void (*checkSize)(void* userGUI, uint32_t* width, uint32_t* height);
    bool (*setSize)(void* userGUI, uint32_t width, uint32_t height);
} g_plugin;

#ifdef HOTRELOAD_BUILD_COMMAND
struct STAND_PluginStateContext
{
    uint8_t* data;
    size_t   bytesReserved;
    size_t   bytesCommited;

    size_t bytesWritten;
    size_t bytesRead;
} g_pluginState;
int64_t STAND_writeStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite);
int64_t STAND_readStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead);
#endif // HOTRELOAD_BUILD_COMMAND

mach_timebase_info_data_t g_timebase;
uint64_t                  g_appStartTime = 0;

const CFStringRef g_midiClientName        = CFSTR("Standalone MIDI Input Client");
const CFStringRef g_midiConnectedPortName = CFSTR("Standalone MIDI Input");

MIDIClientRef  g_midiClientRef         = 0;
MIDIPortRef    g_midiPortRef           = 0;
SInt32         g_midiConnectedUniqueID = 0;
MidiRingBuffer g_midiRingBuffer;

AudioDeviceIOProcID g_audioOutputProcID   = NULL;
AudioDeviceID       g_audioOutputDeviceID = 0;
float               g_audioBuffer[MAX_BLOCK_SIZE * 2 + 32];
Float64             g_audioSampleRate  = USER_SAMPLE_RATE;
UInt32              g_audioBlockSize   = USER_BLOCK_SIZE;
UInt32              g_audioNumChannels = USER_NUM_CHANNELS;
volatile bool       g_audioStopFlag    = false;
pthread_cond_t      g_audioStopCondition;
pthread_mutex_t     g_audioMutex;

FSEventStreamRef g_filesystemEventStream = NULL;

NSWindow* g_window = NULL;

#pragma mark -Utils

static inline uint64_t STAND_convertMachtimeNS(uint64_t start, uint64_t end)
{
    uint64_t diff = end - start;
    return (diff / g_timebase.denom) * g_timebase.numer +
           (diff % g_timebase.denom) * g_timebase.numer / g_timebase.denom;
}

static inline UInt64 STAND_roundDown(UInt64 value, UInt64 alignment)
{
    UInt64 num = value / alignment;
    return num * alignment;
}

static inline UInt64 STAND_roundUp(UInt64 value, UInt64 alignment)
{
    UInt64 v = STAND_roundDown(value, alignment);
    if (value % alignment)
        v += alignment;
    return v;
}

#pragma mark -Forward declarations

// Main thread
void STAND_openLibraryWithSymbols();
void STAND_midiConnectFirstAvailableDevice();
void STAND_midiDisconnect();
void STAND_audioStart();
void STAND_audioStop();
void STAND_menuRefreshAudioOutputItems();
void STAND_menuRefreshMIDIInputItems();
void STAND_filesystemEventCallback(
    ConstFSEventStreamRef          streamRef,
    void* __nullable               clientCallBackInfo,
    size_t                         numEvents,
    void*                          eventPaths,
    const FSEventStreamEventFlags* eventFlags,
    const FSEventStreamEventId*    eventIds);

// MIDI thread
void STAND_midiReadInputProc(
    const MIDIPacketList* pktlist,
    void* __nullable      readProcRefCon,
    void* __nullable      srcConnRefCon);

// Unknown thread
void     STAND_midiDeviceChangeListener(const MIDINotification* message, void* refCon);
OSStatus STAND_audioDeviceChangeListener(
    AudioObjectID                     inObjectID,
    UInt32                            inNumberAddresses,
    const AudioObjectPropertyAddress* inAddresses,
    void* __nullable                  inClientData);

#pragma mark -Application

@implementation AppDelegate

// Lifecycle
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)application
{
    return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    OSStatus status = noErr;

    // create user plugin
    memset(&g_plugin, 0, sizeof(g_plugin));
    STAND_openLibraryWithSymbols();

    g_plugin.libraryLoad();
    g_plugin.userPlugin = g_plugin.createPlugin();
    cplug_assert(g_plugin.userPlugin != NULL);

    // Init MIDI
    memset(&g_midiRingBuffer, 0, sizeof(g_midiRingBuffer));

    status = MIDIClientCreate(g_midiClientName, &STAND_midiDeviceChangeListener, NULL, &g_midiClientRef);
    cplug_assert(status == noErr);
    STAND_midiConnectFirstAvailableDevice();

    // Init audio
    pthread_mutex_init(&g_audioMutex, NULL);

    // Fixes macOS device detection problems
    // https://lists.apple.com/archives/Coreaudio-api/2010/Aug//msg00304.html
    CFRunLoopRef               runLoopRef = NULL;
    AudioObjectPropertyAddress addr       = {
        kAudioHardwarePropertyRunLoop,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster};
    AudioObjectSetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, sizeof(CFRunLoopRef), &runLoopRef);

    // Use wildcard to get all changes
    addr.mSelector = kAudioObjectPropertySelectorWildcard;
    // We're only interested in changes to outputs, but macOS doesn't support that. If you pass ScopeOutput, macOS will
    // return noErr but never use your callback
    addr.mScope = kAudioObjectPropertyScopeGlobal;
    status = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr, &STAND_audioDeviceChangeListener, NULL);
    assert(status == noErr);

    addr.mSelector  = kAudioHardwarePropertyDefaultSystemOutputDevice;
    UInt32 propSize = sizeof(g_audioOutputDeviceID);
    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &propSize, &g_audioOutputDeviceID);
    cplug_assert(status == noErr);
    cplug_assert(g_audioOutputDeviceID != 0);

    STAND_audioStart();

    // GUI
    g_plugin.userGUI = g_plugin.createGUI(g_plugin.userPlugin);
    assert(g_plugin.userGUI != NULL);

    uint32_t guiWidth, guiHeight;
    g_plugin.getSize(g_plugin.userGUI, &guiWidth, &guiHeight);

    g_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, guiWidth, guiHeight)
                                           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    [g_window setReleasedWhenClosed:NO];
    [g_window makeKeyAndOrderFront:nil];
    [g_window setTitle:@(CPLUG_PLUGIN_NAME)];
#if CPLUG_GUI_RESIZABLE
    [g_window setStyleMask:[g_window styleMask] | NSWindowStyleMaskResizable];
#endif
    [g_window setContentView:[[NSView alloc] init]];
    [g_window setDelegate:[[WindowDelegate alloc] init]];

    g_plugin.setParent(g_plugin.userGUI, [g_window contentView]);
    g_plugin.setSize(g_plugin.userGUI, guiWidth, guiHeight);
    g_plugin.setVisible(g_plugin.userGUI, true);

    //////////
    // MENU //
    //////////
    // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/MenuList/MenuList.html

    id menubar = [[NSMenu new] autorelease];
    [NSApp setMainMenu:menubar];

    // Main menu items
    NSMenuItem* appMenu   = [[NSMenuItem new] autorelease];
    NSMenuItem* audioMenu = [[NSMenuItem new] autorelease];
    NSMenuItem* midiMenu  = [[NSMenuItem new] autorelease];
    [appMenu setTag:MainMenuTagApp];
    [audioMenu setTag:MainMenuTagAudio];
    [midiMenu setTag:MainMenuTagMIDI];
    [menubar addItem:appMenu];
    [menubar addItem:audioMenu];
    [menubar addItem:midiMenu];

    // App menu
    [appMenu setSubmenu:[NSMenu new]];
    [[appMenu submenu] addItemWithTitle:[@"Quit " stringByAppendingString:[[NSProcessInfo processInfo] processName]]
                                 action:@selector(terminate:)
                          keyEquivalent:@"q"];
    // TODO: create open/save as for presets

    // Audio menu
    [audioMenu setSubmenu:[[NSMenu alloc] initWithTitle:@"Audio"]];

    // Sample Rate menu
    NSMenuItem* srMenu = [[audioMenu submenu] addItemWithTitle:@"Sample Rate" action:nil keyEquivalent:@""];
    [srMenu setSubmenu:[NSMenu new]];
    [srMenu setTag:AudioMenuTagSampleRate];
    static const int sampleRates[] = {
        44100,
        48000,
        88200,
        96000,
    };
    for (int i = 0; i < ARRSIZE(sampleRates); i++)
    {
        int         sampleRate = sampleRates[i];
        NSMenuItem* item       = [[srMenu submenu] addItemWithTitle:[@(sampleRate) stringValue]
                                                       action:@selector(handleClickSampleRate:)
                                                keyEquivalent:@""];
        if (sampleRate == g_audioSampleRate)
            [item setState:1];
        [item setTag:sampleRate];
    }
    // Block size menu
    NSMenuItem* bsMenu = [[audioMenu submenu] addItemWithTitle:@"Block Size" action:nil keyEquivalent:@""];
    [bsMenu setSubmenu:[NSMenu new]];
    [bsMenu setTag:AudioMenuTagBlockSize];

    static const int blockSizes[] = {
        128,
        192,
        256,
        384,
        448,
        512,
        768,
        1024,
        2048,
    };
    for (int i = 0; i < ARRSIZE(blockSizes); i++)
    {
        int         blockSize = blockSizes[i];
        NSMenuItem* item      = [[bsMenu submenu] addItemWithTitle:[@(blockSize) stringValue]
                                                       action:@selector(handleClickBlockSize:)
                                                keyEquivalent:@""];
        if (blockSize == g_audioBlockSize)
            [item setState:1];
        [item setTag:blockSize];
    }

    // Audio devices
    NSMenuItem* audioOutputMenu = [[audioMenu submenu] addItemWithTitle:@"Output" action:nil keyEquivalent:@""];
    [audioOutputMenu setSubmenu:[NSMenu new]];
    [audioOutputMenu setTag:AudioMenuTagOutput];
    STAND_menuRefreshAudioOutputItems();

    // MIDI menu
    [midiMenu setSubmenu:[[NSMenu alloc] initWithTitle:@"MIDI"]];
    NSMenuItem* midiInputMenu = [[midiMenu submenu] addItemWithTitle:@"Input" action:nil keyEquivalent:@""];
    [midiInputMenu setSubmenu:[NSMenu new]];
    [midiInputMenu setTag:MIDIMenuTagInput];
    STAND_menuRefreshMIDIInputItems();

#ifdef HOTRELOAD_WATCH_DIR
    memset(&g_pluginState, 0, sizeof(g_pluginState));

    // https://developer.apple.com/documentation/coreservices/file_system_events?language=objc
    static const void* watchpaths[] = {CFSTR(HOTRELOAD_WATCH_DIR)};
    CFArrayRef         arrref       = CFArrayCreate(NULL, (const void**)&watchpaths, ARRSIZE(watchpaths), NULL);
    cplug_assert(arrref != NULL);
    double onehundred_ms    = 0.1;
    g_filesystemEventStream = FSEventStreamCreate(
        NULL,
        &STAND_filesystemEventCallback,
        NULL,
        arrref,
        kFSEventStreamEventIdSinceNow,
        onehundred_ms,
        kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagFileEvents);
    cplug_assert(g_filesystemEventStream != NULL);

    FSEventStreamSetDispatchQueue(g_filesystemEventStream, dispatch_get_main_queue());
    bool started = FSEventStreamStart(g_filesystemEventStream);
    assert(started);
    CFRelease(arrref);
#endif // HOTRELOAD_WATCH_DIR

    [NSApp activateIgnoringOtherApps:YES];
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    FSEventStreamStop(g_filesystemEventStream);
    FSEventStreamInvalidate(g_filesystemEventStream);
    FSEventStreamRelease(g_filesystemEventStream);

    STAND_audioStop();

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceIsAlive,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster};
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr, &STAND_audioDeviceChangeListener, NULL);

    // MIDI
    STAND_midiDisconnect();
    if (g_midiClientRef)
    {
        MIDIClientDispose(g_midiClientRef);
        g_midiClientRef = 0;
    }
    pthread_mutex_destroy(&g_audioMutex);

#ifdef HOTRELOAD_LIB_PATH
    if (g_plugin.library)
    {
#endif
        g_plugin.setVisible(g_plugin.userGUI, false);
        g_plugin.setParent(g_plugin.userGUI, NULL);
        g_plugin.destroyGUI(g_plugin.userGUI);

        g_plugin.destroyPlugin(g_plugin.userPlugin);
        g_plugin.libraryUnload();
#ifdef HOTRELOAD_LIB_PATH
        dlclose(g_plugin.library);
        munmap(g_pluginState.data, g_pluginState.bytesReserved);
    }
#endif

    [g_window release];
    [[NSApp menu] release];
    [NSApp.delegate release];
}

#pragma mark -NSMenuItem click methods

- (void)handleClickSampleRate:(id)selector
{
    NSMenuItem* item = selector;
    [item setState:YES];

    NSMenuItem* prev = [[[item parentItem] submenu] itemWithTag:g_audioSampleRate];
    assert(prev != NULL);
    [prev setState:NO];

    STAND_audioStop();
    g_audioSampleRate = [item tag];
    STAND_audioStart();
}

- (void)handleClickBlockSize:(id)selector
{
    NSMenuItem* item = selector;
    [item setState:YES];

    NSMenuItem* prev = [[[item parentItem] submenu] itemWithTag:g_audioBlockSize];
    assert(prev != NULL);
    [prev setState:NO];

    STAND_audioStop();
    g_audioBlockSize = [item tag];
    STAND_audioStart();
}

- (void)handleClickAudioOutput:(id)selector
{
    NSMenuItem* item = selector;
    [item setState:YES];

    if (g_audioOutputDeviceID)
    {
        NSMenuItem* prev = [[[item parentItem] submenu] itemWithTag:g_audioOutputDeviceID];
        assert(prev != NULL);
        [prev setState:NO];
    }

    STAND_audioStop();
    g_audioOutputDeviceID = [item tag];
    STAND_audioStart();
}

- (void)handleClickMIDIInput:(id)selector
{
    NSMenuItem* item = selector;
    [item setState:YES];

    if (g_midiConnectedUniqueID)
    {
        NSMenuItem* prev = [[[item parentItem] submenu] itemWithTag:g_midiConnectedUniqueID];
        assert(prev != NULL);
        [prev setState:NO];
    }

    SInt32 uniqueID = [item tag];
    assert(uniqueID != 0);

    STAND_midiDisconnect();

    MIDIObjectRef  objRef  = 0;
    MIDIObjectType objType = kMIDIObjectType_Device;
    OSStatus       status  = MIDIObjectFindByUniqueID(uniqueID, &objRef, &objType);
    assert(status == noErr);
    assert(objRef != 0);
    assert(objType != kMIDIObjectType_Source);

    status =
        MIDIInputPortCreate(g_midiClientRef, g_midiConnectedPortName, STAND_midiReadInputProc, NULL, &g_midiPortRef);
    cplug_assert(status == noErr);
    assert(g_midiPortRef != 0);
    if (g_midiPortRef == 0)
        return;

    // Starts the MIDI read thread
    status = MIDIPortConnectSource(g_midiPortRef, objRef, NULL);
    cplug_assert(status == noErr);
    g_midiConnectedUniqueID = uniqueID;
}

@end

#pragma mark -Window

@implementation WindowDelegate
- (NSSize)windowWillResize:(NSWindow*)sender toSize:(NSSize)frameSize
{
#ifdef HOTRELOAD_LIB_PATH
    if (g_plugin.library == NULL)
        return frameSize;
#endif

    CGFloat windowHeight   = [sender frame].size.height;
    CGFloat contentHeight  = [[sender contentView] frame].size.height;
    CGFloat titleBarHeight = windowHeight - contentHeight;

    uint32_t width  = frameSize.width;
    uint32_t height = frameSize.height - titleBarHeight;
    g_plugin.checkSize(g_plugin.userGUI, &width, &height);
    frameSize.width  = width;
    frameSize.height = height + titleBarHeight;
    return frameSize;
}
- (void)windowDidResize:(NSNotification*)notification
{
#ifdef HOTRELOAD_LIB_PATH
    if (g_plugin.library == NULL)
        return;
#endif

    NSWindow* window = notification.object;
    CGSize    size   = [window contentView].frame.size;
    g_plugin.setSize(g_plugin.userGUI, size.width, size.height);
}
@end

int main()
{
    g_appStartTime = mach_absolute_time();
    mach_timebase_info(&g_timebase);

    [NSApplication sharedApplication];
    [NSApp setDelegate:[[AppDelegate alloc] init]];
    NSXPCListener* listener = [NSXPCListener serviceListener];

    [NSApp run];

    // NSApp never returns
    return 0;
}

#pragma mark -Menu methods

void STAND_menuRefreshAudioOutputItems()
{
    NSMenuItem* audioMenu = [[NSApp menu] itemWithTag:MainMenuTagAudio];
    assert(audioMenu != NULL);
    NSMenuItem* outputMenu = [[audioMenu submenu] itemWithTag:AudioMenuTagOutput];
    assert(outputMenu != NULL);

    NSMenu* menu = [outputMenu submenu];
    [menu removeAllItems];

    AudioObjectPropertyAddress addr;
    addr.mSelector = kAudioHardwarePropertyDevices;
    addr.mScope    = kAudioObjectPropertyScopeOutput;
    addr.mElement  = kAudioObjectPropertyElementMaster;

    // Even though we explicitly use the scope 'Output', macOS ignore this and simply returns ALL audio devices,
    // including any microphones (input only)
    // When you query the device properties however, suddenly the scopes work.
    // If we query the device for it's channel configurations using the scope 'output', an input device returns
    // with 0 channels. This is what we'll use to manually filter the results for devices WITH outputs.

    UInt32   propertySize = 0;
    OSStatus status       = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nil, &propertySize);
    cplug_assert(status == noErr);

    UInt32        count = propertySize / sizeof(AudioDeviceID);
    AudioDeviceID deviceIDs[count];

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &propertySize, &deviceIDs);
    cplug_assert(status == noErr);

    for (int i = 0; i < count; i++)
    {
        AudioDeviceID deviceID = deviceIDs[i];

        addr.mSelector = kAudioDevicePropertyStreamConfiguration;
        status         = AudioObjectGetPropertyDataSize(deviceID, &addr, 0, NULL, &propertySize);
        cplug_assert(status == noErr);

        int numLists = propertySize / sizeof(AudioBufferList);
        if (numLists == 0)
            continue;

        // Because we query using the scope Ouptut, I *think* this should never be greater than 1.
        cplug_assert(numLists == 1);

        AudioBufferList outputConfig;
        propertySize = sizeof(outputConfig);

        status = AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &propertySize, &outputConfig);
        cplug_assert(status == noErr);

        int numChannels = outputConfig.mBuffers[0].mNumberChannels;

        if (numChannels != g_audioNumChannels)
            continue;

        CFStringRef nameRef = 0;
        propertySize        = sizeof(CFStringRef);
        addr.mSelector      = kAudioDevicePropertyDeviceNameCFString;
        status              = AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &propertySize, &nameRef);
        cplug_assert(status == noErr);
        cplug_assert(nameRef != 0);

        const char* name = CFStringGetCStringPtr(nameRef, 0);

        NSMenuItem* item = [menu addItemWithTitle:@(name) action:@selector(handleClickAudioOutput:) keyEquivalent:@""];
        item.target      = [NSApp delegate];
        [item setTag:deviceID];
        [item setState:deviceID == g_audioOutputDeviceID];
    }
}

void STAND_menuRefreshMIDIInputItems()
{
    NSMenuItem* midiMenu = [[NSApp menu] itemWithTag:MainMenuTagMIDI];
    assert(midiMenu != NULL);
    NSMenuItem* miniInputMenu = [[midiMenu submenu] itemWithTag:MIDIMenuTagInput];
    assert(miniInputMenu != NULL);

    NSMenu* menu = [miniInputMenu submenu];
    [menu removeAllItems];

    OSStatus  status     = noErr;
    ItemCount numSources = MIDIGetNumberOfSources();
    for (int i = 0; i < numSources; i++)
    {
        MIDIEndpointRef sourceRef = MIDIGetSource(i);
        cplug_assert(sourceRef != 0);

        SInt32 midiDeviceID = 0;
        status              = MIDIObjectGetIntegerProperty(sourceRef, kMIDIPropertyUniqueID, &midiDeviceID);
        cplug_assert(status == noErr);
        cplug_assert(midiDeviceID != 0);

        CFStringRef nameRef;
        status = MIDIObjectGetStringProperty(sourceRef, kMIDIPropertyDisplayName, &nameRef);
        cplug_assert(status == noErr);

        NSMenuItem* item = [menu addItemWithTitle:(NSString*)nameRef
                                           action:@selector(handleClickMIDIInput:)
                                    keyEquivalent:@""];
        item.target      = [NSApp delegate];
        [item setTag:midiDeviceID];
        [item setState:midiDeviceID == g_midiConnectedUniqueID];

        CFRelease(nameRef);
    }
}

#pragma mark -MIDI methods
static inline unsigned STAND_midiCalcNumBytesFromStatus(unsigned char status_byte)
{
    /* https://www.midi.org/specifications-old/item/table-2-expanded-messages-list-status-bytes  */
    /* https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2 */
    /* https://www.recordingblogs.com/wiki/midi-quarter-frame-message */
    switch (status_byte)
    {
    case 0x80 ... 0xbf:
    case 0xe0 ... 0xef:
    case 0xf2:
        return 3;
    case 0xc0 ... 0xdf:
    case 0xf1:
        return 2;
    default:
        return 1;
    }
}

// MIDI thread
void STAND_midiReadInputProc(
    const MIDIPacketList* pktlist,
    void* __nullable      readProcRefCon,
    void* __nullable      srcConnRefCon)
{
    const MIDIPacket* packet   = &pktlist->packet[0];
    int               writePos = __atomic_load_n(&g_midiRingBuffer.writePos, __ATOMIC_SEQ_CST);

    for (UInt32 i = 0; i < pktlist->numPackets; ++i)
    {
        /* Either MacOS, or the cheap hardware I used while testing this, appears to send junk data if the device is
           unplugged then plugged back in. Behind the scenes, MacOS will simply reconnect you, then sends you the data.
           If this assumption is correct, then some sneaky data will lead with a valid status byte and get through...
           Here we cautiously exit the proc */
        if (packet->length == 0)
            return;
        if (*packet->data < 0x80)
            return;

        MIDIMessage message;
        message.timestampMs = STAND_convertMachtimeNS(g_appStartTime, packet->timeStamp) / 1e6;

        /* MacOS can send several MIDI messages within the same packet.
           Here we push each MIDI message to our ring buffer, ignoring the SYSEX packets */

        const Byte* bytes          = &packet->data[0];
        unsigned    remainingBytes = packet->length;

        while (remainingBytes != 0)
        {
            message.status = *bytes;

            /* Skip SYSEX */
            if (message.status == 0xf0)
                return;

            unsigned numMsgBytes = STAND_midiCalcNumBytesFromStatus(message.status);

            if (numMsgBytes != 1)
                message.data1 = bytes[1];
            if (numMsgBytes == 3)
                message.data2 = bytes[2];

            g_midiRingBuffer.buffer[writePos] = message;
            writePos++;
            writePos = writePos % ARRSIZE(g_midiRingBuffer.buffer);
            __atomic_store_n(&g_midiRingBuffer.writePos, writePos, __ATOMIC_SEQ_CST);

            bytes          += numMsgBytes;
            remainingBytes -= numMsgBytes;
        }

        packet = MIDIPacketNext(packet);
    }
}

// Main thread
void STAND_midiConnectFirstAvailableDevice()
{
    OSStatus status = noErr;
    assert(g_midiPortRef == 0);

    MIDIEndpointRef sourceRef = MIDIGetSource(0);
    if (sourceRef == 0)
    {
        printf("Failed connecting to first available MIDI input\n");
        return;
    }

    status = MIDIObjectGetIntegerProperty(sourceRef, kMIDIPropertyUniqueID, &g_midiConnectedUniqueID);
    cplug_assert(status == noErr);
    cplug_assert(g_midiConnectedUniqueID != 0);
    if (g_midiConnectedUniqueID == 0)
        return;

    status =
        MIDIInputPortCreate(g_midiClientRef, g_midiConnectedPortName, STAND_midiReadInputProc, NULL, &g_midiPortRef);
    cplug_assert(status == noErr);
    assert(g_midiPortRef != 0);
    if (g_midiPortRef == 0)
        return;

    // Starts the MIDI read thread
    status = MIDIPortConnectSource(g_midiPortRef, sourceRef, NULL);
    cplug_assert(status == noErr);
    if (g_midiConnectedUniqueID != 0)
        printf("Connected to MIDI input with ID: %d\n", g_midiConnectedUniqueID);
}

void STAND_midiDisconnect()
{
    if (g_midiPortRef != 0)
    {
        MIDIPortDispose(g_midiPortRef);
        g_midiPortRef           = 0;
        g_midiConnectedUniqueID = 0;
    }
}

#pragma mark -Audio methods

typedef struct OSXProcessContextTranlator
{
    CplugProcessContext cplugContext;

    float* output[2];
} OSXProcessContextTranlator;

bool OSXProcessContext_enqueueEvent(struct CplugProcessContext* ctx, const CplugEvent* e, uint32_t frameIdx)
{
    return true;
}

bool OSXProcessContext_dequeueEvent(struct CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    if (frameIdx == ctx->numFrames)
        return false;

    int head = __atomic_load_n(&g_midiRingBuffer.writePos, __ATOMIC_SEQ_CST);
    int tail = g_midiRingBuffer.readPos;
    if (tail != head)
    {
        MIDIMessage* msg       = &g_midiRingBuffer.buffer[tail];
        event->type            = CPLUG_EVENT_MIDI;
        event->midi.bytesAsInt = msg->bytesAsInt;

        tail++;
        tail %= CPLUG_MIDI_RINGBUFFER_SIZE;

        g_midiRingBuffer.readPos = tail;
        return true;
    }

    event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
    event->processAudio.endFrame = ctx->numFrames;
    return true;
}

float** OSXProcessContext_getAudioInput(const struct CplugProcessContext* ctx, uint32_t busIdx) { return NULL; }
float** OSXProcessContext_getAudioOutput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const OSXProcessContextTranlator* translator = (const OSXProcessContextTranlator*)ctx;
    if (busIdx == 0)
        return (float**)&translator->output;
    return NULL;
}

// Audio thread
OSStatus STAND_audioIOProc(
    AudioObjectID          inDevice,
    const AudioTimeStamp*  inNow,
    const AudioBufferList* inInputData,
    const AudioTimeStamp*  inInputTime,
    AudioBufferList*       outOutputData,
    const AudioTimeStamp*  inOutputTime,
    void* __nullable       inClientData)
{
    cplug_assert(outOutputData != NULL);
    cplug_assert(outOutputData->mNumberBuffers == 1);
    cplug_assert(outOutputData->mBuffers->mNumberChannels == g_audioNumChannels);
    cplug_assert(outOutputData->mBuffers->mDataByteSize == (g_audioNumChannels * g_audioBlockSize * sizeof(float)));

    OSXProcessContextTranlator translator  = {0};
    translator.cplugContext.numFrames      = g_audioBlockSize;
    translator.cplugContext.enqueueEvent   = OSXProcessContext_enqueueEvent;
    translator.cplugContext.dequeueEvent   = OSXProcessContext_dequeueEvent;
    translator.cplugContext.getAudioInput  = OSXProcessContext_getAudioInput;
    translator.cplugContext.getAudioOutput = OSXProcessContext_getAudioOutput;

    translator.output[0] = (float*)STAND_roundUp((UInt64)&g_audioBuffer, 32);
    translator.output[1] = translator.output[0] + g_audioBlockSize;

    g_plugin.process(g_plugin.userPlugin, &translator.cplugContext);

    // copy from non-interleaved to interleaved
    float* output = (float*)outOutputData->mBuffers->mData;
    for (int i = 0; i < g_audioBlockSize; i++)
        for (int ch = 0; ch < g_audioNumChannels; ch++)
            *output++ = translator.output[ch][i];

    if (__atomic_load_n(&g_audioStopFlag, __ATOMIC_SEQ_CST))
    {
        pthread_mutex_lock(&g_audioMutex);
        OSStatus status = AudioDeviceStop(g_audioOutputDeviceID, g_audioOutputProcID);
        pthread_cond_signal(&g_audioStopCondition);
        pthread_mutex_unlock(&g_audioMutex);
        return status;
    }

    return noErr;
}

// Main thread
void STAND_audioStart()
{
#ifdef HOTRELOAD_LIB_PATH
    if (g_plugin.library == NULL)
    {
        cplug_log("[FAILED] Called STAND_audioStart when no plugin is loaded");
        return;
    }
#endif // HOTRELOAD_LIB_PATH
    if (g_audioOutputProcID)
        STAND_audioStop();

    cplug_assert(g_audioOutputDeviceID > 0);
    cplug_assert(g_audioSampleRate > 0);
    cplug_assert(g_audioBlockSize > 0);

    OSStatus status = noErr;

    AudioObjectPropertyAddress addr;
    addr.mSelector  = kAudioDevicePropertyStreamConfiguration;
    addr.mScope     = kAudioObjectPropertyScopeOutput;
    addr.mElement   = kAudioObjectPropertyElementMaster;
    UInt32 propSize = 0;

    AudioBufferList list;
    propSize = sizeof(list);
    status   = AudioObjectGetPropertyData(g_audioOutputDeviceID, &addr, 0, NULL, &propSize, &list);
    cplug_assert(status == noErr);
    cplug_assert(list.mBuffers[0].mNumberChannels == g_audioNumChannels);

    addr.mSelector = kAudioDevicePropertyBufferFrameSize;
    propSize       = sizeof(UInt32);
    status         = AudioObjectSetPropertyData(g_audioOutputDeviceID, &addr, 0, NULL, propSize, &g_audioBlockSize);
    // If this fails, we may need to clamp the users buffer size using kAudioDevicePropertyBufferFrameSizeRange
    cplug_assert(status == noErr);

    addr.mSelector = kAudioDevicePropertyNominalSampleRate;
    propSize       = sizeof(g_audioSampleRate);
    status         = AudioObjectSetPropertyData(g_audioOutputDeviceID, &addr, 0, NULL, propSize, &g_audioSampleRate);
    cplug_assert(status == noErr);

    // Apple only support interleaved audio, so we have to process in our own deinterleaved buffers, then interleve it
    // https://developer.apple.com/documentation/audiotoolbox/1503207-audioqueuenewoutput#parameters
    addr.mSelector = kAudioStreamPropertyVirtualFormat;
    AudioStreamBasicDescription desc;
    desc.mSampleRate       = g_audioSampleRate;
    desc.mFormatID         = kAudioFormatLinearPCM;
    desc.mFormatFlags      = kAudioFormatFlagsNativeFloatPacked;
    desc.mBytesPerPacket   = sizeof(float) * g_audioNumChannels;
    desc.mFramesPerPacket  = 1;
    desc.mBytesPerFrame    = sizeof(float) * g_audioNumChannels;
    desc.mChannelsPerFrame = g_audioNumChannels;
    desc.mBitsPerChannel   = 32;
    desc.mReserved         = 0;

    propSize = sizeof(desc);
    status   = AudioObjectSetPropertyData(g_audioOutputDeviceID, &addr, 0, NULL, propSize, &desc);
    cplug_assert(status == noErr);

    g_plugin.setSampleRateAndBlockSize(g_plugin.userPlugin, g_audioSampleRate, g_audioBlockSize);

    status = AudioDeviceCreateIOProcID(g_audioOutputDeviceID, &STAND_audioIOProc, NULL, &g_audioOutputProcID);
    cplug_assert(status == noErr);

    g_audioStopFlag = false;
    pthread_cond_init(&g_audioStopCondition, NULL);
    status = AudioDeviceStart(g_audioOutputDeviceID, &STAND_audioIOProc);
    cplug_assert(status == noErr);

    printf("Connected to audio output with ID: %u\n", g_audioOutputDeviceID);
}

// Main thread
void STAND_audioStop()
{
    if (g_audioOutputProcID == NULL)
    {
        cplug_log("[WARNING] Called STAND_audioStop() when audio is not running");
        return;
    }
    cplug_assert(g_audioOutputDeviceID != 0);
    // AudioDeviceStop is not synchronous. It signals some internal state to stop... sometime.
    // If you call stop from your main thread, your audio callback may be called several times after calling stop.
    // Apple guarantee that if you call stop from the audio thread, there will be no more successive calls
    // Source: https://lists.apple.com/archives/coreaudio-api/2006/May/msg00172.html
    // Our solution is to set a flag for the audio thread to respond to
    pthread_mutex_lock(&g_audioMutex);
    __atomic_store_n(&g_audioStopFlag, true, __ATOMIC_SEQ_CST);
    pthread_cond_wait(&g_audioStopCondition, &g_audioMutex);
    pthread_cond_destroy(&g_audioStopCondition);
    pthread_mutex_unlock(&g_audioMutex);

    OSStatus status = AudioDeviceDestroyIOProcID(g_audioOutputDeviceID, g_audioOutputProcID);
    cplug_assert(status == noErr);
    g_audioOutputProcID = NULL;
}

#pragma mark -MIDI Device changes

// Main thread
void handleMIDIDeviceAdded()
{
    printf("Detected new MIDI device\n");
    if (g_midiPortRef == 0)
        STAND_midiConnectFirstAvailableDevice();

    STAND_menuRefreshMIDIInputItems();
}

// Main thread
void handleMIDIDeviceRemoved()
{
    printf("Detected removed MIDI device\n");
    if (g_midiConnectedUniqueID != 0)
    {
        // Check if the connected device was removed
        ItemCount N                = MIDIGetNumberOfSources();
        bool      shouldDisconnect = N == 0;

        for (ItemCount sourceIndex = 0; sourceIndex < N; sourceIndex++)
        {
            MIDIEndpointRef sourceRef = MIDIGetSource(sourceIndex);
            cplug_assert(sourceRef != 0);
            if (sourceRef == 0)
                continue;

            SInt32   uniqueID = 0;
            OSStatus status   = MIDIObjectGetIntegerProperty(sourceRef, kMIDIPropertyUniqueID, &uniqueID);
            cplug_assert(status == noErr);
            cplug_assert(uniqueID != 0);

            if (uniqueID == g_midiConnectedUniqueID)
            {
                shouldDisconnect = true;
                break;
            }
        }

        if (shouldDisconnect)
        {
            printf("disconnecting...\n");
            STAND_midiDisconnect();
        }
    }

    STAND_menuRefreshMIDIInputItems();
}

// Unknown thread
void STAND_midiDeviceChangeListener(const MIDINotification* message, void* refCon)
{
    switch (message->messageID)
    {
    case kMIDIMsgObjectAdded:
        dispatch_async(dispatch_get_main_queue(), ^{
          handleMIDIDeviceAdded();
        });
        break;
    case kMIDIMsgObjectRemoved:
        dispatch_async(dispatch_get_main_queue(), ^{
          handleMIDIDeviceRemoved();
        });
        break;
    default:
        break;
    }
}

#pragma mark -Audio Device changes

// Main thread
void handleAudioDeviceChange()
{
    if (g_audioOutputDeviceID)
    {
        AudioObjectPropertyAddress addr;
        addr.mSelector = kAudioHardwarePropertyDevices;
        addr.mScope    = kAudioObjectPropertyScopeOutput;
        addr.mElement  = kAudioObjectPropertyElementMaster;

        // Even though we explicitly use the scope 'Output', macOS ignore this and simply returns ALL audio devices,
        // including any microphones (input only)
        // When you query the device properties however, suddenly the scopes work.
        // If we query the device for it's channel configurations using the scope 'output', an input device returns
        // with 0 channels. This is what we'll use to manually filter the results for devices WITH outputs.

        UInt32   propertySize = 0;
        OSStatus status       = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nil, &propertySize);
        cplug_assert(status == noErr);

        bool deviceStillExists = false;

        UInt32        count = propertySize / sizeof(AudioDeviceID);
        AudioDeviceID deviceIDs[count];

        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &propertySize, &deviceIDs);
        cplug_assert(status == noErr);

        for (int i = 0; i < count; i++)
            if (deviceIDs[i] == g_audioOutputDeviceID)
                deviceStillExists = true;

        if (! deviceStillExists)
        {
            printf("Warning: Disconnected active audio output");

            AudioDeviceStop(g_audioOutputDeviceID, g_audioOutputProcID);
            AudioDeviceDestroyIOProcID(g_audioOutputDeviceID, g_audioOutputProcID);
            g_audioOutputDeviceID = 0;
            g_audioOutputProcID   = 0;
        }
    }
    STAND_menuRefreshAudioOutputItems();
}

// Unknown thread
OSStatus STAND_audioDeviceChangeListener(
    AudioObjectID                     inObjectID,
    UInt32                            inNumberAddresses,
    const AudioObjectPropertyAddress* inAddresses,
    void* __nullable                  inClientData)
{
    switch (inAddresses->mSelector)
    {
    // case kAudioHardwarePropertyDevices: ?
    case kAudioObjectPropertyOwnedObjects:
        dispatch_async(dispatch_get_main_queue(), ^{
          handleAudioDeviceChange();
        });
        break;
    }
    return noErr;
}

#pragma mark -Hotreloading
void STAND_openLibraryWithSymbols()
{
#ifdef HOTRELOAD_LIB_PATH
    cplug_assert(g_plugin.library == NULL);
    g_plugin.library = dlopen(HOTRELOAD_LIB_PATH, RTLD_NOW);
    cplug_assert(g_plugin.library != NULL);
#define CPLUG_DLSYM(name) dlsym(g_plugin.library, #name)
#else
#define CPLUG_DLSYM(func) func
#endif // HOTRELOAD_LIB_PATH

    // The ugly pointer silliness seen here is to deal with C++ not liking us setting void pointers
    *(size_t*)&g_plugin.libraryLoad               = (size_t)CPLUG_DLSYM(cplug_libraryLoad);
    *(size_t*)&g_plugin.libraryUnload             = (size_t)CPLUG_DLSYM(cplug_libraryUnload);
    *(size_t*)&g_plugin.createPlugin              = (size_t)CPLUG_DLSYM(cplug_createPlugin);
    *(size_t*)&g_plugin.destroyPlugin             = (size_t)CPLUG_DLSYM(cplug_destroyPlugin);
    *(size_t*)&g_plugin.getOutputBusChannelCount  = (size_t)CPLUG_DLSYM(cplug_getOutputBusChannelCount);
    *(size_t*)&g_plugin.setSampleRateAndBlockSize = (size_t)CPLUG_DLSYM(cplug_setSampleRateAndBlockSize);
    *(size_t*)&g_plugin.process                   = (size_t)CPLUG_DLSYM(cplug_process);
    *(size_t*)&g_plugin.saveState                 = (size_t)CPLUG_DLSYM(cplug_saveState);
    *(size_t*)&g_plugin.loadState                 = (size_t)CPLUG_DLSYM(cplug_loadState);

    *(size_t*)&g_plugin.createGUI      = (size_t)CPLUG_DLSYM(cplug_createGUI);
    *(size_t*)&g_plugin.destroyGUI     = (size_t)CPLUG_DLSYM(cplug_destroyGUI);
    *(size_t*)&g_plugin.setParent      = (size_t)CPLUG_DLSYM(cplug_setParent);
    *(size_t*)&g_plugin.setVisible     = (size_t)CPLUG_DLSYM(cplug_setVisible);
    *(size_t*)&g_plugin.setScaleFactor = (size_t)CPLUG_DLSYM(cplug_setScaleFactor);
    *(size_t*)&g_plugin.getSize        = (size_t)CPLUG_DLSYM(cplug_getSize);
    *(size_t*)&g_plugin.checkSize      = (size_t)CPLUG_DLSYM(cplug_checkSize);
    *(size_t*)&g_plugin.setSize        = (size_t)CPLUG_DLSYM(cplug_setSize);

    cplug_assert(NULL != g_plugin.libraryLoad);
    cplug_assert(NULL != g_plugin.libraryUnload);
    cplug_assert(NULL != g_plugin.createPlugin);
    cplug_assert(NULL != g_plugin.destroyPlugin);
    cplug_assert(NULL != g_plugin.getOutputBusChannelCount);
    cplug_assert(NULL != g_plugin.setSampleRateAndBlockSize);
    cplug_assert(NULL != g_plugin.process);
    cplug_assert(NULL != g_plugin.saveState);
    cplug_assert(NULL != g_plugin.loadState);

    cplug_assert(NULL != g_plugin.createGUI);
    cplug_assert(NULL != g_plugin.destroyGUI);
    cplug_assert(NULL != g_plugin.setParent);
    cplug_assert(NULL != g_plugin.setVisible);
    cplug_assert(NULL != g_plugin.setScaleFactor);
    cplug_assert(NULL != g_plugin.getSize);
    cplug_assert(NULL != g_plugin.checkSize);
    cplug_assert(NULL != g_plugin.setSize);
}

#ifdef HOTRELOAD_BUILD_COMMAND
// main thread
void STAND_filesystemEventCallback(
    ConstFSEventStreamRef          streamRef,
    void* __nullable               clientCallBackInfo,
    size_t                         numEvents,
    void*                          eventPaths,
    const FSEventStreamEventFlags* eventFlags,
    const FSEventStreamEventId*    eventIds)
{
    for (int i = 0; i < numEvents; i++)
    {
        CFArrayRef  arr  = (CFArrayRef)eventPaths;
        CFStringRef val  = (CFStringRef)CFArrayGetValueAtIndex(arr, i);
        const char* path = CFStringGetCStringPtr(val, 0);

        // Saving a file usually also contains the flag kFSEventStreamEventFlagItemInodeMetaMod
        static const FSEventStreamEventFlags kFSEventStreamEventFlagFileModified =
            kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemIsFile;

        if ((eventFlags[i] & kFSEventStreamEventFlagFileModified) == kFSEventStreamEventFlagFileModified)
        {
            fprintf(stderr, "File changed: %s\n", path);
            UInt64 reloadStart = mach_absolute_time();

            STAND_audioStop();

            if (g_plugin.library)
            {
                NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
                g_plugin.setVisible(g_plugin.userGUI, false);
                g_plugin.setParent(g_plugin.userGUI, NULL);
                g_plugin.destroyGUI(g_plugin.userGUI);

                g_pluginState.bytesWritten = 0;
                g_pluginState.bytesRead    = 0;
                g_plugin.saveState(g_plugin.userPlugin, &g_pluginState, STAND_writeStateProc);

                g_plugin.destroyPlugin(g_plugin.userPlugin);
                g_plugin.libraryUnload();
                // Explicitly drain the pool to clean up any possible reference counting in the users library
                // Failing to do this before dlclose will cause segfaults when the main runloops pool drains
                [pool release];
                dlclose(g_plugin.library);
                memset(&g_plugin, 0, sizeof(g_plugin));
            }

            UInt64 buildStart = mach_absolute_time();
            int    code       = system(HOTRELOAD_BUILD_COMMAND);
            UInt64 buildEnd   = mach_absolute_time();

            if (code != 0)
            {
                cplug_log("[WARNING] Rebuild failed. Exited with code: %d", code);
            }
            else
            {
                STAND_openLibraryWithSymbols();
                g_plugin.libraryLoad();
                g_plugin.userPlugin = g_plugin.createPlugin();
                cplug_assert(g_plugin.userPlugin != NULL);
                g_plugin.loadState(g_plugin.userPlugin, &g_pluginState, STAND_readStateProc);

                STAND_audioStart();

                g_plugin.userGUI = g_plugin.createGUI(g_plugin.userPlugin);
                cplug_assert(g_plugin.userGUI != NULL);

                uint32_t width, height;
                CGSize   subviewSize = [g_window contentView].frame.size;
                width                = subviewSize.width;
                height               = subviewSize.height;
                g_plugin.setSize(g_plugin.userGUI, width, height);
                g_plugin.setParent(g_plugin.userGUI, [g_window contentView]);
                g_plugin.setVisible(g_plugin.userGUI, true);
            }

            UInt64 reloadEnd = mach_absolute_time();

            double rebuild_ms = (double)STAND_convertMachtimeNS(buildStart, buildEnd) / 1.e6;
            double reload_ms  = (double)STAND_convertMachtimeNS(reloadStart, reloadEnd) / 1.e6;
            fprintf(stderr, "Rebuild time %.2fms\n", rebuild_ms);
            fprintf(stderr, "Reload time %.2fms\n", reload_ms);
        }
    }
}

int64_t STAND_writeStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    cplug_assert(stateCtx != NULL);
    cplug_assert(writePos != NULL);
    cplug_assert(numBytesToWrite > 0);

    // The idea is we reserve heaps of address space up front, and hope we never spill over it.
    // In the rare case your plugin does, simply reserve more address space
    // Some plugins may save big audio files in their state, hence the BIG reserve
    struct STAND_PluginStateContext* ctx = (struct STAND_PluginStateContext*)stateCtx;

    if (ctx->data == NULL)
    {
        size_t largePageSize = 2 * 1024 * 1024;
        size_t bigreserve    = 8 * STAND_roundUp(numBytesToWrite, largePageSize);
        ctx->data            = (UInt8*)mmap(MAP_FILE, bigreserve, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
        cplug_assert((uint64_t)ctx->data != 0xffffffffffffffff);
        ctx->bytesReserved = bigreserve;

        size_t bigcommit = numBytesToWrite * 4;
        int    retval    = mprotect(ctx->data, bigcommit, PROT_READ | PROT_WRITE);
        cplug_assert(retval == 0);
        ctx->bytesCommited = bigcommit;
    }
    // If you hit this assertion, you need to reserve more address space above!
    cplug_assert(numBytesToWrite < (ctx->bytesReserved - ctx->bytesCommited));
    if (numBytesToWrite > (ctx->bytesCommited - ctx->bytesWritten))
    {
        size_t nextcommit = 2 * ctx->bytesCommited;
        int    retval     = mprotect(ctx->data, nextcommit, PROT_READ | PROT_WRITE);
        cplug_assert(retval == 0);
        ctx->bytesCommited = nextcommit;
    }
    memcpy(ctx->data + ctx->bytesWritten, writePos, numBytesToWrite);
    ctx->bytesWritten += numBytesToWrite;
    return numBytesToWrite;
}

int64_t STAND_readStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    struct STAND_PluginStateContext* ctx = (struct STAND_PluginStateContext*)stateCtx;

    cplug_assert(stateCtx != NULL);
    cplug_assert(readPos != NULL);
    cplug_assert(maxBytesToRead > 0);

    size_t remainingBytes     = ctx->bytesWritten - ctx->bytesRead;
    size_t bytesToActualyRead = maxBytesToRead > remainingBytes ? remainingBytes : maxBytesToRead;

    if (bytesToActualyRead)
    {
        memcpy(readPos, ctx->data + ctx->bytesRead, bytesToActualyRead);
        ctx->bytesRead += bytesToActualyRead;
    }

    return bytesToActualyRead;
}
#endif // HOTRELOAD_BUILD_COMMAND