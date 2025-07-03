/* Released into the public domain by Tré Dudman - 2024
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define CINTERFACE
#define COBJMACROS
#include <windows.h>

#include <audioclient.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <mmeapi.h>
#include <synchapi.h>

#include <cplug.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "cfgmgr32.lib")

#ifndef ARRSIZE
#define ARRSIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define cplug_assert(cond) (cond) ? (void)0 : __debugbreak()

#if ! defined(CPLUG_MIDI_BUFFER_COUNT) || ! defined(CPLUG_MIDI_BUFFER_SIZE) || ! defined(CPLUG_MIDI_RINGBUFFER_SIZE)
#define CPLUG_MIDI_BUFFER_COUNT    4
#define CPLUG_MIDI_BUFFER_SIZE     1024
#define CPLUG_MIDI_RINGBUFFER_SIZE 128
#endif

#if ! defined(CPLUG_DEFAULT_BLOCK_SIZE) || ! defined(CPLUG_DEFAULT_SAMPLE_RATE)
// WARNING: using 44100 is currently glitchy, don't know why. It's not a default for now
#define CPLUG_DEFAULT_SAMPLE_RATE 48000
#define CPLUG_DEFAULT_BLOCK_SIZE  512
#endif

#ifdef __cplusplus
#define CPLUG_WTF_IS_A_REFERENCE(obj) obj
#else
#define CPLUG_WTF_IS_A_REFERENCE(obj) &obj
#endif

////////////
// Plugin //
////////////

struct CPWIN_Plugin
{
#ifdef HOTRELOAD_LIB_PATH
    HMODULE Library;
#endif
    void* UserPlugin;
    void* UserGUI;

    void (*libraryLoad)();
    void (*libraryUnload)();
    void* (*createPlugin)();
    void (*destroyPlugin)(void* userPlugin);
    uint32_t (*getOutputBusChannelCount)(void*, uint32_t bus_idx);
    void (*setSampleRateAndBlockSize)(void*, double sampleRate, uint32_t maxBlockSize);
    void (*process)(void*, CplugProcessContext*);
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
} _gCPLUG;
// Loads the DLL + loads symbols for library functions
void CPWIN_LoadPlugin();

#ifdef HOTRELOAD_WATCH_DIR
struct CPWIN_PluginStateContext
{
    BYTE*  Data;
    SIZE_T BytesReserved;
    SIZE_T BytesCommited;

    SIZE_T BytesWritten;
    SIZE_T BytesRead;
} _gPluginState;
int64_t CPWIN_WriteStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite);
int64_t CPWIN_ReadStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead);

// File watch thread
DWORD WINAPI CPWIN_WatchFileChangesProc(LPVOID hwnd);
int          _gFlagExitFileWatchThread;
HANDLE       _ghWatchThread;

// Get time func taken from here https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
struct
{
    LARGE_INTEGER freq, start;
} _gTimer;
static inline INT64 CPWIN_GetNowNS()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    now.QuadPart -= _gTimer.start.QuadPart;
    INT64 q       = now.QuadPart / _gTimer.freq.QuadPart;
    INT64 r       = now.QuadPart % _gTimer.freq.QuadPart;
    return q * 1000000000 + r * 1000000000 / _gTimer.freq.QuadPart;
}
#endif // HOTRELOAD_WATCH_DIR

//////////
// MIDI //
//////////

typedef struct MIDIMessage
{
    union
    {
        struct
        {
            BYTE status;
            BYTE data1;
            BYTE data2;
        };
        BYTE bytes[4];
        UINT bytesAsInt;
    };
    /* Milliseconds since first connected to MIDI port */
    UINT timestampMs;
} MIDIMessage;

struct
{
    HMIDIIN hInput;
    int     IsConnected;

    MIDIINCAPS2A LastConnectedInput;

    struct
    {
        volatile LONG writePos;
        volatile LONG readPos;

        MIDIMessage buffer[CPLUG_MIDI_RINGBUFFER_SIZE];
    } RingBuffer;

    struct
    {
        MIDIHDR header;
        char    buffer[CPLUG_MIDI_BUFFER_SIZE];
    } SystemBuffers[CPLUG_MIDI_BUFFER_COUNT];
} _gMIDI;
// Main Thread
UINT CPWIN_MIDI_ConnectInput(UINT portNum);
void CPWIN_MIDI_DisconnectInput();

///////////
// AUDIO //
///////////

struct
{
    // Devices
    IMMDeviceEnumerator* pIMMDeviceEnumerator;
    IMMDevice*           pIMMDevice;
    WCHAR                DeviceIDBuffer[64];
    // Process
    IAudioClient*       pIAudioClient;
    IAudioRenderClient* pIAudioRenderClient;
    HANDLE              hAudioEvent;
    HANDLE              hAudioProcessThread;
    UINT32              FlagExitAudioThread;

    SIZE_T ProcessBufferCap;
    BYTE*  ProcessBuffer;
    UINT32 ProcessBufferMaxFrames;
    UINT32 ProcessBufferNumOverprocessedFrames;
    // Config
    UINT32 NumChannels;
    UINT32 SampleRate;
    UINT32 BlockSize;
} _gAudio;
// Audio Thread
DWORD WINAPI CPWIN_Audio_RunProcessThread(LPVOID data);
// Main Thread
void CPWIN_Audio_Stop();
void CPWIN_Audio_Start();
// Pass a deviceIdx < 0 for default device
void CPWIN_Audio_SetDevice(int deviceIdx);

///////////
// MENUS //
///////////

enum
{
    IDM_SampleRate_44100,
    IDM_SampleRate_48000,
    IDM_SampleRate_88200,
    IDM_SampleRate_96000,
    IDM_BlockSize_128,
    IDM_BlockSize_192,
    IDM_BlockSize_256,
    IDM_BlockSize_384,
    IDM_BlockSize_448,
    IDM_BlockSize_512,
    IDM_BlockSize_768,
    IDM_BlockSize_1024,
    IDM_BlockSize_2048,

    IDM_HandleRemovedMIDIDevice,
    IDM_HandleAddedMIDIDevice,

    IDM_Hotreload,

    IDM_OFFSET_AUDIO_DEVICES   = 50,
    IDM_RefreshAudioDeviceList = 99,

    IDM_OFFSET_MIDI_DEVICES   = 100,
    IDM_RefreshMIDIDeviceList = 149,
};

struct
{
    HMENU hMain;

    HMENU hAudioMenu;
    HMENU hSampleRateSubmenu;
    HMENU hBlockSizeSubmenu;
    HMENU hAudioOutputSubmenu;
    UINT  numAudioOutputs;

    HMENU hMIDIMenu;
    HMENU hMIDIInputsSubMenu;
} _gMenus;
void CPWIN_Menu_RefreshSampleRates();
void CPWIN_Menu_RefreshBlockSizes();
void CPWIN_Menu_RefreshAudioOutputs();
void CPWIN_Menu_RefreshMIDIInputs();

// Unknown system thread. Notify Connected/disconnected devices. We only check Audio/MIDI
DWORD CALLBACK CPWIN_HandleDeviceChange(
    HCMNOTIFICATION       hNotify,
    PVOID                 Context,
    CM_NOTIFY_ACTION      Action,
    PCM_NOTIFY_EVENT_DATA EventData,
    DWORD                 EventDataSize);
HCMNOTIFICATION _ghCMNotification;

// Main Thread
LRESULT CALLBACK CPWIN_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static inline UINT64 CPWIN_RoundUp(UINT64 v, UINT64 align)
{
    UINT64 inc = (align - (v % align)) % align;
    return v + inc;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
{
    // https://stackoverflow.com/questions/171213/how-to-block-running-two-instances-of-the-same-program
    HANDLE hMutexOneInstance = CreateMutexA(NULL, TRUE, "Single instance - " CPLUG_PLUGIN_NAME);
    if (hMutexOneInstance == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutexOneInstance)
        {
            ReleaseMutex(hMutexOneInstance);
            CloseHandle(hMutexOneInstance);
        }
        return 1;
    }

    if (FAILED(OleInitialize(NULL)))
    {
        fprintf(stderr, "Failed initialising COM\n");
        return 1;
    }

#ifdef HOTRELOAD_WATCH_DIR
    QueryPerformanceFrequency(&_gTimer.freq);
    QueryPerformanceCounter(&_gTimer.start);
    memset(&_gPluginState, 0, sizeof(_gPluginState));
#endif

    memset(&_gCPLUG, 0, sizeof(_gCPLUG));
    memset(&_gMIDI, 0, sizeof(_gMIDI));
    memset(&_gAudio, 0, sizeof(_gAudio));
    memset(&_gMenus, 0, sizeof(_gMenus));

    CPWIN_LoadPlugin();
    _gCPLUG.libraryLoad();
    _gCPLUG.UserPlugin = _gCPLUG.createPlugin();
    cplug_assert(_gCPLUG.UserPlugin != NULL);

    ///////////////
    // INIT MIDI //
    ///////////////

    for (int i = 0; i < ARRSIZE(_gMIDI.SystemBuffers); i++)
    {
        MIDIHDR* head        = &_gMIDI.SystemBuffers[i].header;
        head->lpData         = &_gMIDI.SystemBuffers[i].buffer[0];
        head->dwBufferLength = ARRSIZE(_gMIDI.SystemBuffers[i].buffer);
        head->dwUser         = i;
    }
    CPWIN_MIDI_ConnectInput(0);

    ////////////////
    // INIT AUDIO //
    ////////////////

    _gAudio.SampleRate  = CPLUG_DEFAULT_SAMPLE_RATE;
    _gAudio.BlockSize   = CPLUG_DEFAULT_BLOCK_SIZE;
    _gAudio.NumChannels = _gCPLUG.getOutputBusChannelCount(_gCPLUG.UserPlugin, 0);
    cplug_assert(_gAudio.NumChannels == 1 || _gAudio.NumChannels == 2); // TODO: supported other configurations

    // Scan for device
    static const GUID _CLSID_MMDeviceEnumerator =
        {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
    static const GUID _IID_IMMDeviceEnumerator =
        {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
    HRESULT hr = CoCreateInstance(
        (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_CLSID_MMDeviceEnumerator),
        0,
        CLSCTX_ALL,
        (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_IID_IMMDeviceEnumerator),
        (void**)&_gAudio.pIMMDeviceEnumerator);
    cplug_assert(! FAILED(hr));

    CPWIN_Audio_SetDevice(-1); // -1 == default device
    CPWIN_Audio_Start();
    cplug_assert(_gAudio.ProcessBuffer);

    /////////////////
    // INIT WINDOW //
    /////////////////

    MSG  msg;
    HWND hWindow;

    WNDCLASSEX wc;
    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = CPWIN_WindowProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = CPLUG_PLUGIN_NAME;
    wc.hIconSm       = LoadIconA(NULL, IDI_APPLICATION);

    if (! RegisterClassExA(&wc))
    {
        fprintf(stderr, "Could not register window class\n");
        return 1;
    }

    DPI_AWARENESS_CONTEXT prevDpiCtx = GetThreadDpiAwarenessContext();
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    _gCPLUG.UserGUI = _gCPLUG.createGUI(_gCPLUG.UserPlugin);
    cplug_assert(_gCPLUG.UserGUI != NULL);

    uint32_t guiWidth, guiHeight;
    _gCPLUG.getSize(_gCPLUG.UserGUI, &guiWidth, &guiHeight);

    RECT rect = {0, 0, (LONG)guiWidth, (LONG)guiHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

    hWindow = CreateWindowExA(
        0L,
        wc.lpszClassName,
        wc.lpszClassName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        hInst,
        NULL);
    if (hWindow == NULL)
    {
        fprintf(stderr, "Could not create window\n");
        return 1;
    }
    if (prevDpiCtx)
        SetThreadDpiAwarenessContext(prevDpiCtx);

    ///////////////
    // INIT MENU //
    ///////////////

    _gMenus.hMain = CreateMenu();

    _gMenus.hAudioMenu          = CreatePopupMenu();
    _gMenus.hSampleRateSubmenu  = CreatePopupMenu();
    _gMenus.hBlockSizeSubmenu   = CreatePopupMenu();
    _gMenus.hAudioOutputSubmenu = CreatePopupMenu();
    _gMenus.hMIDIMenu           = CreatePopupMenu();
    _gMenus.hMIDIInputsSubMenu  = CreatePopupMenu();

    AppendMenuA(_gMenus.hMain, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hAudioMenu, "Audio");
    AppendMenuA(_gMenus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hSampleRateSubmenu, "Sample Rate");
    AppendMenuA(_gMenus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hBlockSizeSubmenu, "Block Size");
    AppendMenuA(_gMenus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hAudioOutputSubmenu, "Outputs");

    AppendMenuA(_gMenus.hMain, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hMIDIMenu, "MIDI");
    AppendMenuA(_gMenus.hMIDIMenu, MF_STRING | MF_POPUP, (UINT_PTR)_gMenus.hMIDIInputsSubMenu, "Inputs");

    CPWIN_Menu_RefreshSampleRates();
    CPWIN_Menu_RefreshBlockSizes();
    CPWIN_Menu_RefreshAudioOutputs();
    CPWIN_Menu_RefreshMIDIInputs();

    SetMenu(hWindow, _gMenus.hMain);

    // Callback to detect connected/disconnected MIDI/Audio devices
    // Must be initialised afer the menu because the callback changes menu items based on new/removed devices
    CM_NOTIFY_FILTER notifyFilter;
    memset(&notifyFilter, 0, sizeof(notifyFilter));
    notifyFilter.cbSize     = sizeof(notifyFilter);
    notifyFilter.Flags      = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
    notifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;

    HRESULT result = CM_Register_Notification(&notifyFilter, hWindow, CPWIN_HandleDeviceChange, &_ghCMNotification);
    cplug_assert(result == CR_SUCCESS);
    cplug_assert(_ghCMNotification != NULL);

#ifdef HOTRELOAD_WATCH_DIR
    // Setup file watcher
    _gFlagExitFileWatchThread = 0;
    _ghWatchThread            = CreateThread(NULL, 0, &CPWIN_WatchFileChangesProc, hWindow, 0, 0);
    cplug_assert(_ghWatchThread != NULL);
#endif

    // Window ready
    _gCPLUG.setParent(_gCPLUG.UserGUI, hWindow);

    ShowWindow(hWindow, cmdshow);
    _gCPLUG.setVisible(_gCPLUG.UserGUI, true);
    SetForegroundWindow(hWindow);

    while (GetMessageA(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    OleUninitialize();
    ReleaseMutex(hMutexOneInstance);
    CloseHandle(hMutexOneInstance);
    return msg.wParam;
}

LRESULT CALLBACK CPWIN_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE: // User pressed the window X/Close button
        // Shutdown device notifications
        CM_Unregister_Notification(_ghCMNotification);

        // Shutdown audio
        if (_gAudio.hAudioEvent)
            CPWIN_Audio_Stop();
        cplug_assert(_gAudio.ProcessBuffer != NULL);
        VirtualFree(_gAudio.ProcessBuffer, _gAudio.ProcessBufferCap, 0);
        cplug_assert(_gAudio.pIMMDevice != NULL);
        _gAudio.pIMMDevice->lpVtbl->Release(_gAudio.pIMMDevice);
        cplug_assert(_gAudio.pIMMDeviceEnumerator != NULL);
        _gAudio.pIMMDeviceEnumerator->lpVtbl->Release(_gAudio.pIMMDeviceEnumerator);

        // Shutdown MIDI
        CPWIN_MIDI_DisconnectInput();

        // Destroy plugin
#ifdef HOTRELOAD_WATCH_DIR
        // Stop file watcher
        _gFlagExitFileWatchThread = 1;
        WaitForSingleObject(_ghWatchThread, INFINITE);
        CloseHandle(_ghWatchThread);
        if (_gCPLUG.Library)
        {
#endif
            _gCPLUG.setVisible(_gCPLUG.UserGUI, false);
            _gCPLUG.setParent(_gCPLUG.UserGUI, NULL);
            _gCPLUG.destroyGUI(_gCPLUG.UserGUI);
            _gCPLUG.destroyPlugin(_gCPLUG.UserPlugin);
            _gCPLUG.libraryUnload();
#ifdef HOTRELOAD_WATCH_DIR
            FreeLibrary(_gCPLUG.Library);
        }
        if (_gPluginState.Data)
            VirtualFree(_gPluginState.Data, _gPluginState.BytesReserved, 0);
#endif
        DestroyWindow(hWnd);
        return 0;
    case WM_SIZING: // User is resizing
    {
        RECT*    rect   = (RECT*)lParam;
        uint32_t width  = rect->right - rect->left;
        uint32_t height = rect->bottom - rect->top;

        RECT adjusted = *rect;
        AdjustWindowRect(&adjusted, WS_OVERLAPPEDWINDOW, TRUE);

        uint32_t px = (adjusted.right - adjusted.left) - width;
        uint32_t py = (adjusted.bottom - adjusted.top) - height;

        width  -= px;
        height -= py;
        _gCPLUG.checkSize(_gCPLUG.UserGUI, &width, &height);
        width  += px;
        height += py;

        rect->right  = rect->left + width;
        rect->bottom = rect->top + height;

        return TRUE;
    }
    case WM_SIZE: // Window has resized
    {
        RECT rect;
        GetClientRect(hWnd, &rect);
        uint32_t width  = rect.right - rect.left;
        uint32_t height = rect.bottom - rect.top;
        _gCPLUG.setSize(_gCPLUG.UserGUI, width, height);
        return 0;
    }
    case WM_DPICHANGED:
    {
        int   g_dpi  = HIWORD(wParam);
        FLOAT fscale = (float)g_dpi / USER_DEFAULT_SCREEN_DPI;
        _gCPLUG.setScaleFactor(_gCPLUG.UserGUI, fscale);

        RECT* const prcNewWindow = (RECT*)lParam;
        SetWindowPos(
            hWnd,
            NULL,
            prcNewWindow->left,
            prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    case WM_COMMAND: // clicking nav menu items triggers commands. You can also send commands for other things
    {
        switch (wParam)
        {
#ifdef HOTRELOAD_BUILD_COMMAND
        case IDM_Hotreload:
        {
            UINT64 reloadStart = CPWIN_GetNowNS();

            if (_gCPLUG.Library)
            {
                // Deinit
                _gCPLUG.setVisible(_gCPLUG.UserGUI, false);
                _gCPLUG.setParent(_gCPLUG.UserGUI, NULL);
                _gCPLUG.destroyGUI(_gCPLUG.UserGUI);

                CPWIN_Audio_Stop();

                _gPluginState.BytesWritten = 0;
                _gPluginState.BytesRead    = 0;
                _gCPLUG.saveState(_gCPLUG.UserPlugin, &_gPluginState, CPWIN_WriteStateProc);

                _gCPLUG.destroyPlugin(_gCPLUG.UserPlugin);
                _gCPLUG.libraryUnload();
                BOOL ok = FreeLibrary(_gCPLUG.Library);
                cplug_assert(ok);
                memset(&_gCPLUG, 0, sizeof(_gCPLUG));
            }

            // Using 'system()' to call our build command is way simpler, but creates some stdout buffering problems...
            // Windows prefer that you use CreateProcessA.
            // The idea is we create a new process using CREATE_NEW_CONSOLE while setting HIDE falgs in the STARTUPINFO
            // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
            STARTUPINFO         si;
            PROCESS_INFORMATION pi;
            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));

            si.cb      = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW; // These flags are necessarry to stop a terminal window popping up as it
            si.wShowWindow = SW_HIDE;          // runs the command

            UINT64 buildStart = CPWIN_GetNowNS();
            // Run build command in child process.
            const char* cmd = HOTRELOAD_BUILD_COMMAND;
            if (! CreateProcessA(0, (LPSTR)cmd, 0, 0, FALSE, CREATE_NEW_CONSOLE, 0, 0, &si, &pi))
            {
                printf("CreateProcess failed (%lu).\n", GetLastError());
                return 1;
            }

            // Wait until child process exits
            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            UINT64 buildEnd = CPWIN_GetNowNS();
            // Cleanup build process
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (exitCode != 0)
            {
                cplug_log("[WARNING] Rebuild failed. Exited with code: %lu", exitCode);
            }
            else
            {
                CPWIN_LoadPlugin();
                _gCPLUG.libraryLoad();
                _gCPLUG.UserPlugin = _gCPLUG.createPlugin();
                cplug_assert(_gCPLUG.UserPlugin != NULL);
                _gCPLUG.loadState(_gCPLUG.UserPlugin, &_gPluginState, CPWIN_ReadStateProc);

                CPWIN_Audio_Start();

                _gCPLUG.UserGUI = _gCPLUG.createGUI(_gCPLUG.UserPlugin);
                cplug_assert(_gCPLUG.UserGUI != NULL);

                RECT size;
                GetClientRect(hWnd, &size);
                _gCPLUG.setSize(_gCPLUG.UserGUI, size.right - size.left, size.bottom - size.top);

                _gCPLUG.setParent(_gCPLUG.UserGUI, hWnd);
                _gCPLUG.setVisible(_gCPLUG.UserGUI, true);
            }

            UINT64 reloadEnd = CPWIN_GetNowNS();

            double rebuild_ms = (double)(buildEnd - buildStart) / 1.e6;
            double reload_ms  = (double)(reloadEnd - reloadStart) / 1.e6;
            fprintf(stderr, "Rebuild time %.2fms\n", rebuild_ms);
            fprintf(stderr, "Reload time %.2fms\n", reload_ms);
            break;
        }
#endif // HOTRELOAD_BUILD_COMMAND
        case IDM_SampleRate_44100:
        case IDM_SampleRate_48000:
        case IDM_SampleRate_88200:
        case IDM_SampleRate_96000:
        {
            CPWIN_Audio_Stop();
            char text[8];
            int  numCharsCopied = GetMenuStringA(_gMenus.hSampleRateSubmenu, wParam, text, sizeof(text), MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            _gAudio.SampleRate = atoi(text);
            CPWIN_Audio_Start();
            CPWIN_Menu_RefreshSampleRates();
            break;
        }
        case IDM_BlockSize_128:
        case IDM_BlockSize_192:
        case IDM_BlockSize_256:
        case IDM_BlockSize_384:
        case IDM_BlockSize_448:
        case IDM_BlockSize_512:
        case IDM_BlockSize_768:
        case IDM_BlockSize_1024:
        case IDM_BlockSize_2048:
        {
            CPWIN_Audio_Stop();
            char text[8];
            int  numCharsCopied = GetMenuStringA(_gMenus.hBlockSizeSubmenu, wParam, text, sizeof(text), MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            _gAudio.BlockSize = atoi(text);
            CPWIN_Audio_Start();
            CPWIN_Menu_RefreshBlockSizes();
            break;
        }
        case IDM_RefreshAudioDeviceList:
            CPWIN_Menu_RefreshAudioOutputs();
            break;
        case IDM_RefreshMIDIDeviceList:
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        case IDM_HandleRemovedMIDIDevice:
        {
            fprintf(stderr, "Callback: Removed MIDI input device\n");
            if (_gMIDI.IsConnected)
            {
                UINT num = midiInGetNumDevs();
                if (num == 0)
                {
                    CPWIN_MIDI_DisconnectInput();
                    fprintf(stderr, "WARNING: Not connected to a MIDI input device\n");
                }
                else
                {
                    // Check it was the connected device which was removed
                    MIDIINCAPS2A caps;
                    UINT         i      = 0;
                    MMRESULT     result = 0;
                    for (; i < num; i++)
                    {
                        result = midiInGetDevCapsA(i, (MIDIINCAPS*)&caps, sizeof(caps));
                        if (result == MMSYSERR_NOERROR &&
                            memcmp(&caps.NameGuid, &_gMIDI.LastConnectedInput.NameGuid, sizeof(caps.NameGuid)) &&
                            memcmp(&caps.ProductGuid, &_gMIDI.LastConnectedInput.ProductGuid, sizeof(caps.ProductGuid)))
                            break;
                    }
                    // Failed to match our connected device
                    if (i == num)
                    {
                        fprintf(
                            stderr,
                            "Connected MIDI input device was removed. Trying to connecting to the next available "
                            "device\n");
                        CPWIN_MIDI_DisconnectInput();
                        CPWIN_MIDI_ConnectInput(0);
                    }
                }
            }
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        }
        case IDM_HandleAddedMIDIDevice:
            fprintf(stderr, "Callback: New MIDI input device\n");
            if (_gMIDI.IsConnected == 0)
            {
                fprintf(stderr, "Trying to connect new device\n");
                CPWIN_MIDI_ConnectInput(0);
            }
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        default:
        {
            if (wParam >= IDM_OFFSET_AUDIO_DEVICES && wParam < IDM_RefreshAudioDeviceList)
            {
                UINT idx = wParam - IDM_OFFSET_AUDIO_DEVICES;
                CPWIN_Audio_Stop();
                CPWIN_Audio_SetDevice(idx);
                CPWIN_Audio_Start();
                CPWIN_Menu_RefreshAudioOutputs();
            }
            if (wParam >= IDM_OFFSET_MIDI_DEVICES && wParam < IDM_RefreshMIDIDeviceList)
            {
                UINT idx = wParam - IDM_OFFSET_MIDI_DEVICES;
                CPWIN_MIDI_DisconnectInput();
                CPWIN_MIDI_ConnectInput(idx);
                CPWIN_Menu_RefreshMIDIInputs();
            }
        }
        }
        DrawMenuBar(hWnd);
        break;
    }
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

void CPWIN_LoadPlugin()
{
#ifdef HOTRELOAD_LIB_PATH
    cplug_assert(_gCPLUG.Library == NULL);
#define CPLUG_GET_PROC_ADDR(name) GetProcAddress(_gCPLUG.Library, #name)
    _gCPLUG.Library = LoadLibraryA(HOTRELOAD_LIB_PATH);
    cplug_assert(_gCPLUG.Library != NULL);
#else // not a hotrealoding build
#define CPLUG_GET_PROC_ADDR(func) func
#endif

    // This looks ugly because of the strict types in C++. C is ironically more elegant
    *(LONG_PTR*)&_gCPLUG.libraryLoad               = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_libraryLoad);
    *(LONG_PTR*)&_gCPLUG.libraryUnload             = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_libraryUnload);
    *(LONG_PTR*)&_gCPLUG.createPlugin              = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_createPlugin);
    *(LONG_PTR*)&_gCPLUG.destroyPlugin             = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_destroyPlugin);
    *(LONG_PTR*)&_gCPLUG.getOutputBusChannelCount  = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_getOutputBusChannelCount);
    *(LONG_PTR*)&_gCPLUG.setSampleRateAndBlockSize = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setSampleRateAndBlockSize);
    *(LONG_PTR*)&_gCPLUG.process                   = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_process);
    *(LONG_PTR*)&_gCPLUG.saveState                 = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_saveState);
    *(LONG_PTR*)&_gCPLUG.loadState                 = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_loadState);

    *(LONG_PTR*)&_gCPLUG.createGUI      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_createGUI);
    *(LONG_PTR*)&_gCPLUG.destroyGUI     = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_destroyGUI);
    *(LONG_PTR*)&_gCPLUG.setParent      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setParent);
    *(LONG_PTR*)&_gCPLUG.setVisible     = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setVisible);
    *(LONG_PTR*)&_gCPLUG.setScaleFactor = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setScaleFactor);
    *(LONG_PTR*)&_gCPLUG.getSize        = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_getSize);
    *(LONG_PTR*)&_gCPLUG.checkSize      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_checkSize);
    *(LONG_PTR*)&_gCPLUG.setSize        = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setSize);

    cplug_assert(NULL != _gCPLUG.libraryLoad);
    cplug_assert(NULL != _gCPLUG.libraryUnload);
    cplug_assert(NULL != _gCPLUG.createPlugin);
    cplug_assert(NULL != _gCPLUG.destroyPlugin);
    cplug_assert(NULL != _gCPLUG.getOutputBusChannelCount);
    cplug_assert(NULL != _gCPLUG.setSampleRateAndBlockSize);
    cplug_assert(NULL != _gCPLUG.process);
    cplug_assert(NULL != _gCPLUG.saveState);
    cplug_assert(NULL != _gCPLUG.loadState);

    cplug_assert(NULL != _gCPLUG.createGUI);
    cplug_assert(NULL != _gCPLUG.destroyGUI);
    cplug_assert(NULL != _gCPLUG.setParent);
    cplug_assert(NULL != _gCPLUG.setVisible);
    cplug_assert(NULL != _gCPLUG.setScaleFactor);
    cplug_assert(NULL != _gCPLUG.getSize);
    cplug_assert(NULL != _gCPLUG.checkSize);
    cplug_assert(NULL != _gCPLUG.setSize);
}

#ifdef HOTRELOAD_WATCH_DIR
#pragma region PLUGIN_STATE
int64_t        CPWIN_WriteStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    cplug_assert(stateCtx != NULL);
    cplug_assert(writePos != NULL);
    cplug_assert(numBytesToWrite > 0);

    // The idea is we reserve heaps of address space up front, and hope we never spill over it.
    // In the rare case your plugin does, simply reserve more address space
    // Some plugins may save big audio files in their state, hence the BIG reserve
    struct CPWIN_PluginStateContext* ctx = (struct CPWIN_PluginStateContext*)stateCtx;

    if (ctx->Data == NULL)
    {
        const SIZE_T largePageSize  = GetLargePageMinimum();
        SIZE_T       bigreserve     = CPWIN_RoundUp(numBytesToWrite, largePageSize);
        bigreserve                 *= 8;
        ctx->Data                   = (BYTE*)VirtualAlloc(NULL, bigreserve, MEM_RESERVE, PAGE_READWRITE);
        cplug_assert(ctx->Data != NULL);
        ctx->BytesReserved = bigreserve;

        SIZE_T bigcommit = numBytesToWrite * 4;
        LPVOID retval    = VirtualAlloc(ctx->Data, bigcommit, MEM_COMMIT, PAGE_READWRITE);
        cplug_assert(retval != NULL);
        ctx->BytesCommited = bigcommit;
    }
    // If you hit this assertion, you need to reserve more address space above!
    cplug_assert(numBytesToWrite < (ctx->BytesReserved - ctx->BytesCommited));
    if (numBytesToWrite > (ctx->BytesCommited - ctx->BytesWritten))
    {
        SIZE_T nextcommit = 2 * ctx->BytesCommited;
        LPVOID retval     = VirtualAlloc(ctx->Data, nextcommit, MEM_COMMIT, PAGE_READWRITE);
        cplug_assert(retval != NULL);
        ctx->BytesCommited = nextcommit;
    }
    memcpy(ctx->Data + ctx->BytesWritten, writePos, numBytesToWrite);
    ctx->BytesWritten += numBytesToWrite;
    return numBytesToWrite;
}

int64_t CPWIN_ReadStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    struct CPWIN_PluginStateContext* ctx = (struct CPWIN_PluginStateContext*)stateCtx;

    cplug_assert(stateCtx != NULL);
    cplug_assert(readPos != NULL);
    cplug_assert(maxBytesToRead > 0);

    SIZE_T remainingBytes     = ctx->BytesWritten - ctx->BytesRead;
    SIZE_T bytesToActualyRead = maxBytesToRead > remainingBytes ? remainingBytes : maxBytesToRead;

    if (bytesToActualyRead)
    {
        memcpy(readPos, ctx->Data + ctx->BytesRead, bytesToActualyRead);
        ctx->BytesRead += bytesToActualyRead;
    }

    return bytesToActualyRead;
}
#pragma endregion PLUGIN_STATE

DWORD WINAPI CPWIN_WatchFileChangesProc(LPVOID hwnd)
{
    // Most this code was taken from here: https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8
    fprintf(stderr, "Watching folder %s\n", HOTRELOAD_WATCH_DIR);

    HANDLE hDirectory = CreateFileA(
        HOTRELOAD_WATCH_DIR,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    if (hDirectory == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to get directory handle\n");
        return 1;
    }
    OVERLAPPED overlapped;
    overlapped.hEvent = CreateEventA(NULL, FALSE, 0, NULL);

    BYTE infobuffer[1024];
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
    BOOL success = ReadDirectoryChangesW(
        hDirectory,
        infobuffer,
        sizeof(infobuffer),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &overlapped,
        NULL);
    if (! success)
    {
        fprintf(stderr, "Failed to queue info buffer\n");
        return 1;
    }

    int throttlereload = 0;
    while (_gFlagExitFileWatchThread == 0)
    {
        DWORD result = WaitForSingleObject(overlapped.hEvent, 50);

        if (result == WAIT_TIMEOUT)
        {
            if (throttlereload != 0)
                PostMessageA((HWND)hwnd, WM_COMMAND, IDM_Hotreload, 0);
            throttlereload = 0;
        }
        else if (result == WAIT_OBJECT_0)
        {
            DWORD bytes_transferred;
            GetOverlappedResult(hDirectory, &overlapped, &bytes_transferred, TRUE);

            FILE_NOTIFY_INFORMATION* event = (FILE_NOTIFY_INFORMATION*)infobuffer;

            while (TRUE)
            {
                DWORD name_len = event->FileNameLength / sizeof(wchar_t);

                if (event->Action == FILE_ACTION_MODIFIED)
                {
                    fwprintf(stderr, L"File changed: %.*s\n", name_len, event->FileName);
                    throttlereload++;
                }

                // Iterate events
                if (event->NextEntryOffset)
                    *((BYTE**)&event) += event->NextEntryOffset;
                else
                    break;
            }

            // Queue next event
            success = ReadDirectoryChangesW(
                hDirectory,
                infobuffer,
                sizeof(infobuffer),
                TRUE,
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL,
                &overlapped,
                NULL);

            if (! success)
            {
                fprintf(stderr, "Failed to queue info buffer\n");
                return 1;
            }
        }
    }
    return 0;
}
#endif // HOTRELOAD_WATCH_DIR

#pragma region MENUS

static inline UINT CPWIN_MenuFlag(UINT a, UINT b) { return a == b ? (MF_STRING | MF_CHECKED) : MF_STRING; }

void CPWIN_Menu_RefreshSampleRates()
{
    while (RemoveMenu(_gMenus.hSampleRateSubmenu, 0, MF_BYPOSITION))
    {
    }

    AppendMenuA(_gMenus.hSampleRateSubmenu, CPWIN_MenuFlag(_gAudio.SampleRate, 44100), IDM_SampleRate_44100, "44100");
    AppendMenuA(_gMenus.hSampleRateSubmenu, CPWIN_MenuFlag(_gAudio.SampleRate, 48000), IDM_SampleRate_48000, "48000");
    AppendMenuA(_gMenus.hSampleRateSubmenu, CPWIN_MenuFlag(_gAudio.SampleRate, 88200), IDM_SampleRate_88200, "88200");
    AppendMenuA(_gMenus.hSampleRateSubmenu, CPWIN_MenuFlag(_gAudio.SampleRate, 96000), IDM_SampleRate_96000, "96000");
}

void CPWIN_Menu_RefreshBlockSizes()
{
    while (RemoveMenu(_gMenus.hBlockSizeSubmenu, 0, MF_BYPOSITION))
    {
    }

    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 128), IDM_BlockSize_128, "128");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 192), IDM_BlockSize_192, "192");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 256), IDM_BlockSize_256, "256");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 384), IDM_BlockSize_384, "384");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 448), IDM_BlockSize_448, "448");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 512), IDM_BlockSize_512, "512");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 768), IDM_BlockSize_768, "768");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 1024), IDM_BlockSize_1024, "1024");
    AppendMenuA(_gMenus.hBlockSizeSubmenu, CPWIN_MenuFlag(_gAudio.BlockSize, 2048), IDM_BlockSize_2048, "2048");
}

void CPWIN_Menu_RefreshAudioOutputs()
{
    while (RemoveMenu(_gMenus.hAudioOutputSubmenu, 0, MF_BYPOSITION))
    {
    }

    IMMDeviceCollection* pCollection = NULL;
    _gAudio.pIMMDeviceEnumerator->lpVtbl
        ->EnumAudioEndpoints(_gAudio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
    cplug_assert(pCollection != NULL);

    pCollection->lpVtbl->GetCount(pCollection, &_gMenus.numAudioOutputs);

    for (UINT i = 0; i < _gMenus.numAudioOutputs; i++)
    {
        IMMDevice* pDevice = NULL;

        pCollection->lpVtbl->Item(pCollection, i, &pDevice);
        if (pDevice != NULL)
        {
            WCHAR* deviceID = NULL;
            pDevice->lpVtbl->GetId(pDevice, &deviceID);

            static const PROPERTYKEY _PKEY_Device_FriendlyName = {
                {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
                14};

            IPropertyStore* pProperties = NULL;
            HRESULT         hr          = pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pProperties);
            cplug_assert(! FAILED(hr));

            PROPVARIANT varName;
            pProperties->lpVtbl->GetValue(pProperties, CPLUG_WTF_IS_A_REFERENCE(_PKEY_Device_FriendlyName), &varName);

            if (varName.vt != VT_EMPTY)
            {
                UINT uFlags = MF_STRING;
                if (0 == wcsncmp(deviceID, _gAudio.DeviceIDBuffer, ARRSIZE(_gAudio.DeviceIDBuffer)))
                    uFlags |= MF_CHECKED;

                AppendMenuW(_gMenus.hAudioOutputSubmenu, uFlags, IDM_OFFSET_AUDIO_DEVICES + i, varName.pwszVal);
            }

            PropVariantClear(&varName);

            pProperties->lpVtbl->Release(pProperties);
            pDevice->lpVtbl->Release(pDevice);
        }
    }

    pCollection->lpVtbl->Release(pCollection);

    AppendMenuA(_gMenus.hAudioOutputSubmenu, MF_SEPARATOR, IDM_RefreshAudioDeviceList - 1, NULL);
    AppendMenuA(_gMenus.hAudioOutputSubmenu, MF_STRING, IDM_RefreshAudioDeviceList, "Refresh list");
}

void CPWIN_Menu_RefreshMIDIInputs()
{
    while (RemoveMenu(_gMenus.hMIDIInputsSubMenu, 0, MF_BYPOSITION))
    {
    }

    MIDIINCAPS2A caps;
    memset(&caps, 0, sizeof(caps));

    int numMidiIn = midiInGetNumDevs();
    for (int i = 0; i < numMidiIn; i++)
    {
        MMRESULT result = midiInGetDevCapsA(i, (MIDIINCAPS*)&caps, sizeof(caps));
        cplug_assert(result == MMSYSERR_NOERROR);

        if (result == MMSYSERR_NOERROR)
        {
            UINT uFlags = MF_STRING;
            if (0 == memcmp(&caps.NameGuid, &_gMIDI.LastConnectedInput.NameGuid, sizeof(caps.NameGuid)) &&
                0 == memcmp(&caps.ProductGuid, &_gMIDI.LastConnectedInput.ProductGuid, sizeof(caps.ProductGuid)))
                uFlags |= MF_CHECKED;

            AppendMenuA(_gMenus.hMIDIInputsSubMenu, uFlags, IDM_OFFSET_MIDI_DEVICES + i, caps.szPname);
        }
    }
}

#pragma endregion MENUS

DWORD CALLBACK CPWIN_HandleDeviceChange(
    HCMNOTIFICATION       hNotify,
    PVOID                 hwnd,
    CM_NOTIFY_ACTION      Action,
    PCM_NOTIFY_EVENT_DATA EventData,
    DWORD                 EventDataSize)
{
    WCHAR* InstanceId = &EventData->u.DeviceInstance.InstanceId[0];

    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
        // I've found updating MIDI lists here less reliable than in the following 2 enums
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
        // MIDI input instance IDs come in this format:
        // SWD\MMDEVAPI\MIDII_(4 byte hex).P_(2 byte hex)
        // Software device - MMDevice API - MIDI Input
        // For audio devices I'm less sure of thier format.
        // The format I have seen on my own PC is: L"SWD\MMDEVAPI\{0.0.0.00000000}.{(GUID)}""
        // TODO: test for patten used by audio devices
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/install/device-instance-ids
        if (0 == wcsncmp(L"SWD\\MMDEVAPI\\MIDII_", InstanceId, 19))
        {
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_HandleRemovedMIDIDevice, 0);
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_RefreshMIDIDeviceList, 0);
        }
        else if (0 == wcsncmp(L"SWD\\MMDEVAPI\\", InstanceId, 13))
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_RefreshAudioDeviceList, 0);
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
        if (0 == wcsncmp(L"SWD\\MMDEVAPI\\MIDII_", InstanceId, 19))
        {
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_HandleAddedMIDIDevice, 0);
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_RefreshMIDIDeviceList, 0);
        }
        else if (0 == wcsncmp(L"SWD\\MMDEVAPI\\", InstanceId, 13))
            PostMessageA((HWND)hwnd, WM_COMMAND, IDM_RefreshAudioDeviceList, 0);
        break;
    default:
        break;
    }
    return 0;
}

#pragma region MIDI

void CALLBACK CPWIN_MIDIInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    /* https://learn.microsoft.com/en-gb/windows/win32/multimedia/mim-data?redirectedfrom=MSDN */
    if (wMsg == MM_MIM_DATA)
    {
        MIDIMessage midi;
        LONG        writePos;

        /* take first 3 bytes. remember, the rest are junk, including possibly the ones we're taking */
        midi.bytesAsInt  = dwParam1 & 0xffffff;
        midi.timestampMs = dwParam2;

        writePos = _InterlockedCompareExchange(&_gMIDI.RingBuffer.writePos, 0, 0);

        _gMIDI.RingBuffer.buffer[writePos] = midi;
        writePos++;
        writePos = writePos % ARRSIZE(_gMIDI.RingBuffer.buffer);
        _InterlockedExchange(&_gMIDI.RingBuffer.writePos, writePos);
    }
    /* handle sysex*/
    /* https://www.midi.org/specifications-old/item/table-4-universal-system-exclusive-messages */
    /* else if (wMsg == MIM_LONGDATA) {} */
}

UINT CPWIN_MIDI_ConnectInput(UINT portNum)
{
    UINT result;

    // Set up are MIDI reading callback
    cplug_assert(_gMIDI.hInput == NULL);
    result = midiInOpen(&_gMIDI.hInput, portNum, (DWORD_PTR)&CPWIN_MIDIInProc, 0, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR)
        goto failed;

    memset(&_gMIDI.LastConnectedInput, 0, sizeof(_gMIDI.LastConnectedInput));
    result = midiInGetDevCapsA(0, (MIDIINCAPS*)&_gMIDI.LastConnectedInput, sizeof(_gMIDI.LastConnectedInput));
    cplug_assert(result == MMSYSERR_NOERROR);

    for (int i = 0; i < ARRSIZE(_gMIDI.SystemBuffers); i++)
    {
        result =
            midiInPrepareHeader(_gMIDI.hInput, &_gMIDI.SystemBuffers[i].header, sizeof(_gMIDI.SystemBuffers[i].header));
        if (result != MMSYSERR_NOERROR)
            goto failed;
        result = midiInAddBuffer(_gMIDI.hInput, &_gMIDI.SystemBuffers[i].header, sizeof(MIDIHDR));
        if (result != MMSYSERR_NOERROR)
            goto failed;
    }

    result = midiInStart(_gMIDI.hInput);
    if (result != MMSYSERR_NOERROR)
        goto failed;

    _gMIDI.IsConnected = 1;
    fprintf(stderr, "Connected to MIDI input %u\n", portNum);

    return result;

failed:
    if (_gMIDI.hInput)
    {
        midiInClose(_gMIDI.hInput);
        _gMIDI.hInput = 0;
    }
    return result;
}

void CPWIN_MIDI_DisconnectInput()
{
    if (_gMIDI.IsConnected)
    {
        UINT result;
        midiInReset(_gMIDI.hInput);
        midiInStop(_gMIDI.hInput);

        for (int i = 0; i < ARRSIZE(_gMIDI.SystemBuffers); i++)
        {
            MIDIHDR* head = &_gMIDI.SystemBuffers[i].header;
            result        = midiInUnprepareHeader(_gMIDI.hInput, head, sizeof(*head));

            if (result != MMSYSERR_NOERROR)
                break;
        }
        midiInClose(_gMIDI.hInput);
        _gMIDI.hInput      = NULL;
        _gMIDI.IsConnected = 0;
        memset(&_gMIDI.LastConnectedInput, 0, sizeof(_gMIDI.LastConnectedInput));
    }
}

#pragma endregion MIDI

#pragma region AUDIO

typedef struct WindowsProcessContext
{
    CplugProcessContext cplugContext;
    float*              output[2];
} WindowsProcessContext;

bool CPWIN_Audio_enqueueEvent(struct CplugProcessContext* ctx, const CplugEvent* e, uint32_t frameIdx) { return true; }

bool CPWIN_Audio_dequeueEvent(struct CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    if (frameIdx >= ctx->numFrames)
        return false;

    LONG head = _InterlockedCompareExchange(&_gMIDI.RingBuffer.writePos, 0, 0);
    LONG tail = _InterlockedCompareExchange(&_gMIDI.RingBuffer.readPos, 0, 0);
    if (head != tail)
    {
        MIDIMessage* msg       = &_gMIDI.RingBuffer.buffer[tail];
        event->midi.type       = CPLUG_EVENT_MIDI;
        event->midi.bytesAsInt = msg->bytesAsInt;

        tail++;
        tail %= CPLUG_MIDI_RINGBUFFER_SIZE;

        _gMIDI.RingBuffer.readPos = tail;
        return true;
    }

    event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
    event->processAudio.endFrame = ctx->numFrames;
    return true;
}

float** CPWIN_Audio_getAudioInput(const struct CplugProcessContext* ctx, uint32_t busIdx) { return NULL; }

float** CPWIN_Audio_getAudioOutput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const WindowsProcessContext* winctx = (const WindowsProcessContext*)ctx;
    if (busIdx == 0)
        return (float**)&winctx->output[0];
    return NULL;
}

void CPWIN_Audio_Process(const UINT32 blockSize)
{
    BYTE*   outBuffer            = NULL;
    UINT32  remainingBlockFrames = blockSize;
    HRESULT hr = _gAudio.pIAudioRenderClient->lpVtbl->GetBuffer(_gAudio.pIAudioRenderClient, blockSize, &outBuffer);
    cplug_assert(outBuffer != NULL);
    if (FAILED(hr))
        return;

    if (_gAudio.ProcessBufferNumOverprocessedFrames)
    {
        // Our remaining samples are already in a deinterleaved format
        UINT32 framesToCopy = _gAudio.ProcessBufferNumOverprocessedFrames < remainingBlockFrames
                                  ? _gAudio.ProcessBufferNumOverprocessedFrames
                                  : remainingBlockFrames;
        SIZE_T bytesToCopy  = sizeof(float) * _gAudio.NumChannels * framesToCopy;
        memcpy(outBuffer, _gAudio.ProcessBuffer, bytesToCopy);

        remainingBlockFrames                        -= framesToCopy;
        _gAudio.ProcessBufferNumOverprocessedFrames -= framesToCopy;
        outBuffer                                   += bytesToCopy;
        cplug_assert(remainingBlockFrames < blockSize); // check overflow
    }

    WindowsProcessContext ctx       = {0};
    ctx.cplugContext.numFrames      = _gAudio.BlockSize;
    ctx.cplugContext.enqueueEvent   = CPWIN_Audio_enqueueEvent;
    ctx.cplugContext.dequeueEvent   = CPWIN_Audio_dequeueEvent;
    ctx.cplugContext.getAudioInput  = CPWIN_Audio_getAudioInput;
    ctx.cplugContext.getAudioOutput = CPWIN_Audio_getAudioOutput;

    SIZE_T processBufferOffset = sizeof(float) * _gAudio.NumChannels * _gAudio.ProcessBufferMaxFrames;
    processBufferOffset        = CPWIN_RoundUp(processBufferOffset, 32);
    ctx.output[0]              = (float*)(_gAudio.ProcessBuffer + processBufferOffset);
    ctx.output[1]              = ctx.output[0] + _gAudio.BlockSize;

    while (remainingBlockFrames > 0)
    {
        cplug_assert(_gAudio.ProcessBufferNumOverprocessedFrames == 0);

        _gCPLUG.process(_gCPLUG.UserPlugin, &ctx.cplugContext);

        UINT32 framesToCopy = remainingBlockFrames < _gAudio.BlockSize ? remainingBlockFrames : _gAudio.BlockSize;
        SIZE_T bytesToCopy  = sizeof(float) * _gAudio.NumChannels * framesToCopy;

        int    i                 = 0;
        float* outputInterleaved = (float*)outBuffer;
        for (; i < framesToCopy; i++)
            for (int ch = 0; ch < _gAudio.NumChannels; ch++)
                *outputInterleaved++ = ctx.output[ch][i];

        float* remainingInterleaved = (float*)_gAudio.ProcessBuffer;
        for (; i < _gAudio.BlockSize; i++)
            for (int ch = 0; ch < _gAudio.NumChannels; ch++)
                *remainingInterleaved++ = ctx.output[ch][i];
        _gAudio.ProcessBufferNumOverprocessedFrames = _gAudio.BlockSize - framesToCopy;

        remainingBlockFrames -= framesToCopy;
        outBuffer            += bytesToCopy;

        cplug_assert(remainingBlockFrames < blockSize); // check overflow
    }

    // This has a scary name 'Release', however I don't think any resources are deallocated,
    // rather space within a preallocated block is marked reserved/unreserved
    // This is just how you hand the buffer back to windows
    _gAudio.pIAudioRenderClient->lpVtbl->ReleaseBuffer(_gAudio.pIAudioRenderClient, blockSize, 0);
}

DWORD WINAPI CPWIN_Audio_RunProcessThread(LPVOID data)
{
    // NOTE: requested sizes do not come in the size requested, or even in a multiple of 32
    // On my machine, requesting a block size of 512 at 44100Hz gives me a max frame size of 1032 and variable
    // block sizes, usually consisting of 441 frames.The windows docs say this to guarantee enough audio in reserve to
    // prevent audible glicthes:
    // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
    // Unfortunately for us, this means we need to play silly games caching audio within a preallocated buffer to
    // make sure the users App recieves a sensible block size
    CPWIN_Audio_Process(_gAudio.ProcessBufferMaxFrames);

    _gAudio.pIAudioClient->lpVtbl->Start(_gAudio.pIAudioClient);

    while (! _gAudio.FlagExitAudioThread)
    {
        WaitForSingleObject(_gAudio.hAudioEvent, INFINITE);

        UINT32  padding = 0;
        HRESULT hr      = _gAudio.pIAudioClient->lpVtbl->GetCurrentPadding(_gAudio.pIAudioClient, &padding);

        if (FAILED(hr))
            continue;

        cplug_assert(_gAudio.ProcessBufferMaxFrames >= padding);
        UINT32 blockSize = _gAudio.ProcessBufferMaxFrames - padding;
        if (blockSize == 0)
            continue;

        CPWIN_Audio_Process(blockSize);
    }

    return 0;
}

void CPWIN_Audio_Stop()
{
    if (_gAudio.hAudioProcessThread == NULL)
    {
        cplug_log("[WARNING] Called CPWIN_Audio_Stop() when audio is not running");
        return;
    }
    cplug_assert(_gAudio.FlagExitAudioThread == 0);
    _gAudio.FlagExitAudioThread = 1;
    cplug_assert(_gAudio.hAudioEvent);
    SetEvent(_gAudio.hAudioEvent);

    cplug_assert(_gAudio.hAudioProcessThread != NULL);
    WaitForSingleObject(_gAudio.hAudioProcessThread, INFINITE);
    CloseHandle(_gAudio.hAudioProcessThread);
    _gAudio.hAudioProcessThread = NULL;

    cplug_assert(_gAudio.pIAudioClient != NULL);
    _gAudio.pIAudioClient->lpVtbl->Stop(_gAudio.pIAudioClient);
    cplug_assert(_gAudio.pIAudioRenderClient != NULL);
    _gAudio.pIAudioRenderClient->lpVtbl->Release(_gAudio.pIAudioRenderClient);
    cplug_assert(_gAudio.pIAudioClient != NULL);
    _gAudio.pIAudioClient->lpVtbl->Release(_gAudio.pIAudioClient);
    _gAudio.pIAudioClient       = NULL;
    _gAudio.pIAudioRenderClient = NULL;

    cplug_assert(_gAudio.hAudioEvent != NULL);
    CloseHandle(_gAudio.hAudioEvent);
    _gAudio.hAudioEvent = NULL;
}

void CPWIN_Audio_SetDevice(int deviceIdx)
{
    cplug_assert(_gAudio.hAudioProcessThread == NULL);

    if (_gAudio.pIMMDevice != NULL)
        _gAudio.pIMMDevice->lpVtbl->Release(_gAudio.pIMMDevice);

    if (deviceIdx >= 0)
    {
        IMMDeviceCollection* pCollection = NULL;
        _gAudio.pIMMDeviceEnumerator->lpVtbl
            ->EnumAudioEndpoints(_gAudio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
        cplug_assert(pCollection != NULL);

        UINT numDevices = 0;
        pCollection->lpVtbl->GetCount(pCollection, &numDevices);

        if (deviceIdx < numDevices)
            pCollection->lpVtbl->Item(pCollection, deviceIdx, &_gAudio.pIMMDevice);

        pCollection->lpVtbl->Release(pCollection);
    }

    if (_gAudio.pIMMDevice == NULL)
    {
        // eConsole or eMultimedia? Microsoft say console is for games, multimedia for playing live music
        // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles
        HRESULT hr = _gAudio.pIMMDeviceEnumerator->lpVtbl->GetDefaultAudioEndpoint(
            _gAudio.pIMMDeviceEnumerator,
            eRender,
            eMultimedia,
            &_gAudio.pIMMDevice);
        cplug_assert(! FAILED(hr));
    }

    WCHAR* audioDeviceID = NULL;
    _gAudio.pIMMDevice->lpVtbl->GetId(_gAudio.pIMMDevice, &audioDeviceID);
    wcscpy_s(_gAudio.DeviceIDBuffer, ARRSIZE(_gAudio.DeviceIDBuffer), audioDeviceID);
    _gAudio.DeviceIDBuffer[ARRSIZE(_gAudio.DeviceIDBuffer) - 1] = 0;
}

void CPWIN_Audio_Start()
{
#ifdef HOTRELOAD_BUILD_COMMAND
    if (_gCPLUG.Library == NULL)
    {
        cplug_log("[FAILED] Called CPWIN_Audio_Start when no plugin is loaded");
        return;
    }
#endif
    cplug_assert(_gAudio.SampleRate != 0);
    cplug_assert(_gAudio.BlockSize != 0);
    static const IID _IID_IAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
    static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
        {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    static const IID _IID_IAudioRenderClient =
        {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};

    cplug_assert(_gAudio.pIMMDevice != NULL);
    cplug_assert(_gAudio.pIAudioClient == NULL);
    HRESULT hr = _gAudio.pIMMDevice->lpVtbl->Activate(
        _gAudio.pIMMDevice,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioClient),
        CLSCTX_ALL,
        0,
        (void**)&_gAudio.pIAudioClient);
    cplug_assert(! FAILED(hr));

    // https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible
    WAVEFORMATEXTENSIBLE fmtex;
    memset(&fmtex, 0, sizeof(fmtex));
    fmtex.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    fmtex.Format.nChannels            = _gAudio.NumChannels;
    fmtex.Format.nSamplesPerSec       = _gAudio.SampleRate;
    fmtex.Format.wBitsPerSample       = 32;
    fmtex.Format.nBlockAlign          = (fmtex.Format.nChannels * fmtex.Format.wBitsPerSample) / 8;
    fmtex.Format.nAvgBytesPerSec      = fmtex.Format.nSamplesPerSec * fmtex.Format.nBlockAlign;
    fmtex.Format.cbSize               = 22;
    fmtex.Samples.wValidBitsPerSample = 32;

    if (fmtex.Format.nChannels == 1)
        fmtex.dwChannelMask = SPEAKER_FRONT_CENTER;
    else
        fmtex.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

    fmtex.SubFormat = _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    REFERENCE_TIME reftime = (double)_gAudio.BlockSize / ((double)_gAudio.SampleRate * 1.e-7);

    // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
    hr = _gAudio.pIAudioClient->lpVtbl->Initialize(
        _gAudio.pIAudioClient,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        reftime,
        0,
        (WAVEFORMATEX*)&fmtex,
        0);
    cplug_assert(! FAILED(hr));

    hr = _gAudio.pIAudioClient->lpVtbl->GetBufferSize(_gAudio.pIAudioClient, &_gAudio.ProcessBufferMaxFrames);
    cplug_assert(! FAILED(hr));

    _gAudio.pIAudioClient->lpVtbl->GetService(
        _gAudio.pIAudioClient,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioRenderClient),
        (void**)&_gAudio.pIAudioRenderClient);

    cplug_assert(_gAudio.hAudioEvent == NULL);
    _gAudio.hAudioEvent = CreateEventA(0, 0, 0, 0);
    cplug_assert(_gAudio.hAudioEvent != NULL);
    _gAudio.pIAudioClient->lpVtbl->SetEventHandle(_gAudio.pIAudioClient, _gAudio.hAudioEvent);

    SIZE_T req_bytes_reserve    = sizeof(float) * _gAudio.NumChannels * _gAudio.ProcessBufferMaxFrames;
    SIZE_T req_bytes_processing = sizeof(float) * _gAudio.NumChannels * _gAudio.BlockSize;
    req_bytes_reserve           = CPWIN_RoundUp(req_bytes_reserve, 32);
    req_bytes_processing        = CPWIN_RoundUp(req_bytes_processing, 32);

    SIZE_T requiredCap = CPWIN_RoundUp(req_bytes_reserve + req_bytes_processing, 4096);
    if (requiredCap > _gAudio.ProcessBufferCap)
    {
        if (_gAudio.ProcessBuffer != NULL)
            VirtualFree(_gAudio.ProcessBuffer, _gAudio.ProcessBufferCap, 0);

        _gAudio.ProcessBufferCap = requiredCap;
        _gAudio.ProcessBuffer =
            (BYTE*)VirtualAlloc(NULL, _gAudio.ProcessBufferCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        cplug_assert(_gAudio.ProcessBuffer != NULL);
    }

    _gCPLUG.setSampleRateAndBlockSize(_gCPLUG.UserPlugin, _gAudio.SampleRate, _gAudio.BlockSize);

    _gAudio.ProcessBufferNumOverprocessedFrames = 0;
    _gAudio.FlagExitAudioThread                 = 0;

    _gAudio.hAudioProcessThread = CreateThread(NULL, 0, CPWIN_Audio_RunProcessThread, NULL, 0, 0);
    cplug_assert(_gAudio.hAudioProcessThread != NULL);
}
#pragma endregion AUDIO