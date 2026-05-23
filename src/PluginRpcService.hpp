#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <memory>
#include <rfl/Bytestring.hpp>

#include "config/UserConfigSerialization.hpp"
#include "lsdj/Effects.hpp"
#include "system/MemoryType.hpp"
#include "system/SystemTypes.hpp"

class Project;
class CommandQueue;
class EventQueue;
class SystemBase;
class UserConfig;
class RecentFiles;

namespace rp::lsdj { class KitCompiler; }

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
        // True when an LsdjKitPatchRole is attached (sniffer auto-attaches
        // it on every LSDJ ROM, so this lights up the Kit Editor menu
        // entry whenever an LSDJ ROM is loaded).
        std::optional<bool>          hasLsdjKitRole;
    };

    // --- LSDJ kit-patch DTOs ----------------------------------------------
    //
    // Per-sample input for compileAndPatchKit. Mirrors `LsdjSampleConfig`
    // in [system/sameboy/roles/LsdjKitPatchRole.hpp] but with the
    // `effects` vector typed as the same reflectcpp variant. Kept here
    // so the rpc layer doesn't drag the role header into every TU.
    struct KitSampleSpec {
        std::string                       path;
        std::string                       name;
        std::optional<std::size_t>        offset;       // skip N source frames
        std::optional<std::size_t>        length;       // 0/missing = use all
        std::vector<rp::lsdj::LsdjEffect> effects;
        std::optional<std::uint8_t>       pitch;        // stored, not yet applied
        std::optional<std::uint8_t>       volume;       // stored, not yet applied
    };

    struct KitSampleEntry {
        std::string                       path;
        std::string                       name;
        std::uint8_t                      pitch  = 0x7F;
        std::uint8_t                      volume = 0xFF;
        std::uint64_t                     sourceHash = 0;
        std::vector<rp::lsdj::LsdjEffect> effects;
    };
    struct KitEntry {
        std::uint8_t                  slot;
        std::string                   name;
        std::uint64_t                 compiledHash = 0;
        std::size_t                   compiledSize = 0;       // raw bytes; lets the UI skip a base64 decode
        std::vector<KitSampleEntry>   samples;
    };
    struct KitsResponse {
        // Empty when the system has no LsdjKitPatchRole attached.
        std::vector<KitEntry> kits;
    };

    struct CompileKitResult {
        bool            ok = false;
        std::string     error;
        std::uint64_t   compiledHash = 0;
        rfl::Bytestring compiledBytes;        // 16 KB on success
    };

    struct AuditionResponse {
        bool            ok = false;
        std::uint32_t   sampleRate = 0;
        rfl::Bytestring pcmF32;               // mono float32 LE
    };

    // Cold-path memory read response. `regionSize` is the full size of the
    // requested region (independent of the caller's length); `bytes` is the
    // sliced window. Empty / nullopt = unknown system / unsupported type /
    // out-of-range offset.
    struct MemorySnapshotResponse {
        rfl::Bytestring bytes;
        std::uint64_t   hash       = 0;
        std::uint32_t   regionSize = 0;
    };

    struct OpenRomOpts {
        std::optional<std::string> mode;  // "add" | "replace" (default replace)
    };

    // One entry of the recent-files list. Surfaced over RPC verbatim — the
    // UI uses `kind` to dispatch to loadRomFromPath vs loadProjectFromPath.
    struct RecentFileDto {
        std::string path;
        std::string kind;   // "rom" | "project"
    };

    // Construction ----------------------------------------------------------

    // `userConfig` and `recentFiles` are optional (nullptr in LV2-UI and
    // in rpc-schema-dump). When null, getUserConfig() returns a default-
    // initialised DTO, setActiveBindings() is a no-op, getRecentFiles()
    // returns an empty list, and load handlers skip the recent-files
    // bookkeeping.
    PluginRpcService(Project*,
                     CommandQueue*,
                     EventQueue*,
                     std::atomic<double>*       sampleRate,
                     std::atomic<SystemId>*     focusedSystemId,
                     UserConfig*                userConfig  = nullptr,
                     RecentFiles*               recentFiles = nullptr);
    ~PluginRpcService();

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
    // Returns the resolved zoom (1..6): per-project value if set, otherwise
    // the user-config default. setZoom always writes 1..6 to the project.
    std::uint32_t getZoom();
    bool setZoom(std::uint32_t zoom);
    bool setLsdjSyncConfig(std::uint32_t id, std::uint32_t mode, std::uint32_t divisor);
    bool setWindowSize(std::uint32_t w, std::uint32_t h);
    bool isWindowSizeControlled();

    // LSDJ kit patching.
    KitsResponse     getKitsConfig(std::uint32_t systemId);
    CompileKitResult compileAndPatchKit(std::uint32_t systemId,
                                        std::uint8_t  kitIndex,
                                        std::string   kitName,
                                        std::vector<KitSampleSpec> samples);
    AuditionResponse auditionSample(std::string path);
    bool             eraseKit(std::uint32_t systemId, std::uint8_t kitIndex);
    // Opens the native file browser; the chosen path is delivered to JS via
    // a one-shot "sample-path-selected" event so the UI can resolve a
    // Promise around it. Matches the event-style flow openRomBrowser uses.
    bool             openSampleBrowser();

    // User config / key-pad bindings. See src/config/UserConfig.hpp.
    UserConfigDto getUserConfig();
    bool          setActiveBindings(std::string name);

    // Recently-loaded ROMs and projects. Most-recent first; capped at
    // RecentFiles::kMaxEntries. See src/config/RecentFiles.hpp.
    std::vector<RecentFileDto> getRecentFiles();

    // -- Memory snapshot API ----------------------------------------------
    //
    // One-shot read of an emulator region. Torn reads from live memory are
    // accepted for this cold path. Large regions (ROM, GBA EWRAM, large
    // SRAM) are allowed — no size cap.
    std::optional<MemorySnapshotResponse> getMemory(
        std::uint32_t systemId,
        std::uint32_t type,
        std::uint32_t offset,
        std::uint32_t length);

    // Refcounted live-stream subscription. The DSP allocates a per-(system,
    // type) triple-buffer on the first subscriber and frees it on the last.
    // Returns false if the type is unsupported on the target system or the
    // region exceeds the streamable size cap (use one-shot getMemory then).
    bool subscribeMemory(std::uint32_t systemId,
                         std::uint32_t type,
                         std::uint32_t hz);

    bool unsubscribeMemory(std::uint32_t systemId,
                           std::uint32_t type);

    // PluginJsBridge integration. The bridge calls pumpMemorySnapshots from
    // uiIdle; this struct holds the per-sub state it needs (hz throttle,
    // dedup hash, monotonic version counter for React memoization).
    struct MemorySubKey {
        SystemId       systemId;
        rp::MemoryType type;
        bool operator<(const MemorySubKey& other) const {
            if (systemId != other.systemId) return systemId < other.systemId;
            return static_cast<std::uint8_t>(type) < static_cast<std::uint8_t>(other.type);
        }
    };
    struct MemorySubState {
        std::uint32_t hz         = 0;   // 0 = no cadence cap
        std::uint64_t lastEmitNs = 0;   // steady_clock since-epoch
        std::uint64_t lastHash   = 0;
        std::uint32_t version    = 0;   // 0 = nothing emitted yet
    };
    using MemorySubRegistry = std::map<MemorySubKey, MemorySubState>;
    MemorySubRegistry&       memorySubs()       { return memorySubs_; }
    const MemorySubRegistry& memorySubs() const { return memorySubs_; }

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
    enum class PendingFileMode { LoadRom, AddRom, LoadProject, SaveProject, LoadSample };

    bool saveProjectToPath(const std::string& path);

    Project*                  project_              = nullptr;
    CommandQueue*             commands_             = nullptr;
    EventQueue*               events_               = nullptr;
    std::atomic<double>*      sampleRate_           = nullptr;
    std::atomic<SystemId>*    focusedSystemId_      = nullptr;
    UserConfig*               userConfig_           = nullptr;
    RecentFiles*              recentFiles_          = nullptr;

    // Lazy-allocated; constructed on first kit-related call so a project
    // that never opens an LSDJ ROM doesn't pay the enkiTS thread-pool
    // spin-up cost.
    std::unique_ptr<rp::lsdj::KitCompiler> kitCompiler_;

    EmitEventFn               emitEvent_;
    OpenFileBrowserFn         openFileBrowser_;
    SetWindowSizeFn           setWindowSize_;
    IsWindowSizeControlledFn  isWindowSizeControlled_;

    PendingFileMode           pendingFileMode_      = PendingFileMode::LoadRom;

    MemorySubRegistry         memorySubs_;
};
