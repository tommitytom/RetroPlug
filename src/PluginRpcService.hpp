#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "config/UserConfigSerialization.hpp"
#include "system/SystemTypes.hpp"

class Project;
class CommandQueue;
class EventQueue;
class SystemBase;
class UserConfig;

// rpcpp service surface. Plain reflect-cpp-friendly methods — no QuickJS
// or LVGL references. PluginJsBridge wraps an instance of this class in a
// TypedRpcServer<PluginRpcService, MsgpackCodec> and exposes the bridge to
// the JS runtime as a single `__rpcSend` C function.
//
// Lifetimes mirror PluginJsBridge: caller owns the shared-state pointers
// and outlives the service. In LV2-UI any of the pointers may be null;
// methods that need them degrade (return false / nullopt) rather than
// crash.
class PluginRpcService {
public:
    // Reflect-cpp DTOs ------------------------------------------------------
    //
    // Each struct's public fields are serialized as a msgpack map. Optional
    // fields become msgpack nil when absent, which the client side surfaces
    // as `undefined`.

    struct FrameResponse {
        std::uint32_t width;
        std::uint32_t height;
        // RGBA, row-major. rfl::Bytestring (= std::vector<std::byte>) is
        // the only vector type reflect-cpp's parser routes to msgpack BIN;
        // std::vector<std::uint8_t> would go through VectorParser and end
        // up as a msgpack array of integers (~5× wire size + JS decoded as
        // a number Array instead of Uint8Array).
        rfl::Bytestring buffer;
    };

    struct SystemEntry {
        std::uint32_t id;
        std::string   kind;                          // "sameboy" | "mesen" | "gba"
        std::optional<double>        gainDb;
        std::optional<std::uint32_t> linkGroupId;
        std::optional<std::uint32_t> lsdjSyncMode;
        std::optional<std::uint32_t> lsdjTempoDivisor;
    };

    struct OpenRomOpts {
        std::optional<std::string> mode;  // "add" | "replace" (default replace)
    };

    // Construction ----------------------------------------------------------

    // `userConfig` is optional (nullptr in LV2-UI and in rpc-schema-dump).
    // When null, getUserConfig() returns a default-initialised DTO and
    // setActiveBindings() is a no-op.
    PluginRpcService(Project*,
                     CommandQueue*,
                     EventQueue*,
                     std::atomic<double>*       sampleRate,
                     std::atomic<SystemId>*     focusedSystemId,
                     UserConfig*                userConfig = nullptr);

    PluginRpcService(const PluginRpcService&)            = delete;
    PluginRpcService& operator=(const PluginRpcService&) = delete;

    // Wiring (set by PluginJsBridge after construction) ---------------------

    using EmitEventFn              = std::function<void(const std::string& channel, const std::string& payload)>;
    using OpenFileBrowserFn        = std::function<void(const char* title, bool saving, const char* defaultName)>;
    using SetWindowSizeFn          = std::function<void(unsigned w, unsigned h)>;
    using IsWindowSizeControlledFn = std::function<bool()>;

    void setEmitEventCallback(EmitEventFn fn)              { emitEvent_ = std::move(fn); }
    void setOpenFileBrowserCallback(OpenFileBrowserFn fn)  { openFileBrowser_ = std::move(fn); }
    void setWindowSizeCallback(SetWindowSizeFn fn)         { setWindowSize_ = std::move(fn); }
    void setIsWindowSizeControlledQuery(IsWindowSizeControlledFn fn) { isWindowSizeControlled_ = std::move(fn); }

    // Public helpers used by PluginJsBridge / PluginUI directly (not RPC) ---

    // PluginUI::uiFileBrowserSelected routes the chosen path here; we then
    // dispatch to load / add / save / load-project based on which open*
    // call set pendingFileMode_ most recently.
    void onFileBrowserSelected(const char* path);

    // Auto-load on startup (PluginUI reads RETROPLUG_AUTOLOAD_PROJECT and
    // calls this directly — bypasses the file browser).
    bool loadProjectFromPath(const std::string& path);

    // RPC surface (registered via TypedRpcServer::addMethod<&...>()) --------

    std::optional<FrameResponse> getFrame(std::uint32_t systemId);
    bool openRomBrowser(OpenRomOpts opts);
    bool openSaveProjectBrowser();
    bool openLoadProjectBrowser();
    bool loadRomFromPath(std::string path);
    bool addRomFromPath(std::string path);
    bool replaceRomFromPath(std::uint32_t id, std::string path);
    bool removeSystem(std::uint32_t id);
    std::vector<SystemEntry> listSystems();
    bool setFocus(std::uint32_t id);
    std::uint32_t getFocus();
    bool pressButton(std::int32_t button, bool down, std::optional<std::uint32_t> systemId);
    bool setLinkGroupId(std::uint32_t id, std::uint32_t groupId);
    std::uint32_t getMidiRouting();
    bool setMidiRouting(std::uint32_t routing);
    bool setLsdjSyncConfig(std::uint32_t id, std::uint32_t mode, std::uint32_t divisor);
    bool setWindowSize(std::uint32_t w, std::uint32_t h);
    bool isWindowSizeControlled();

    // User config / key-pad bindings. See src/config/UserConfig.hpp.
    UserConfigDto getUserConfig();
    bool          setActiveBindings(std::string name);

private:
    // Content-dispatched ROM loader. Lives here rather than on PluginJsBridge
    // because the service owns the file IO + system construction.
    SystemBase* buildSystemFromPath(const std::string& path);

    // saveProjectToPath / addRomFromPath / etc share the same "emit
    // (channel, path)" pattern; this is the one indirection that points
    // back at the JS engine.
    void emit(const std::string& channel, const std::string& payload) const;

    // File-browser callback target. Open-* methods set this; the DPF host
    // delivers the chosen path back via onFileBrowserSelected.
    enum class PendingFileMode { LoadRom, AddRom, LoadProject, SaveProject };

    bool saveProjectToPath(const std::string& path);

    Project*                  project_              = nullptr;
    CommandQueue*             commands_             = nullptr;
    EventQueue*               events_               = nullptr;
    std::atomic<double>*      sampleRate_           = nullptr;
    std::atomic<SystemId>*    focusedSystemId_      = nullptr;
    UserConfig*               userConfig_           = nullptr;

    EmitEventFn               emitEvent_;
    OpenFileBrowserFn         openFileBrowser_;
    SetWindowSizeFn           setWindowSize_;
    IsWindowSizeControlledFn  isWindowSizeControlled_;

    PendingFileMode           pendingFileMode_      = PendingFileMode::LoadRom;
};
