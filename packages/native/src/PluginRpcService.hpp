#pragma once

#include <atomic>
#include <chrono>
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
#include "project/ProjectMissingFiles.hpp"
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
        std::string   kind;                          // "sameboy" | "nes" | "gba"
        std::optional<double>        gainDb;
        std::optional<std::uint32_t> linkGroupId;
        std::optional<std::uint32_t> lsdjSyncMode;
        std::optional<std::uint32_t> lsdjTempoDivisor;
        // True when an LsdjKitPatchRole is attached (sniffer auto-attaches
        // it on every LSDJ ROM, so this lights up the Kit Editor menu
        // entry whenever an LSDJ ROM is loaded).
        std::optional<bool>          hasLsdjKitRole;
        // SameBoy-only. Values mirror SameBoyModel.
        std::optional<std::uint32_t> model;
        std::optional<bool>          fastBoot;
        std::optional<bool>          reloadOnRomChange;
        // SameBoy-only. Values mirror SameBoyHighpass: 0=Off, 1=Accurate, 2=RemoveDcOffset.
        std::optional<std::uint32_t> highpass;
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
        std::size_t                       offset = 0;
        std::size_t                       length = 0;
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

    // One entry of the recent-projects list. `name` is an optional display
    // alias (empty => UI derives a label from the path basename). `missing` is
    // computed per fetch via std::filesystem::exists so the UI can flag a
    // project whose `.rplg` has moved / been deleted.
    struct RecentFileDto {
        std::string path;
        std::string name;
        bool        missing = false;
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
    // Standalone-only: ask the host UI to quit (close the window). Set by PluginUI.
    using QuitFn                   = std::function<void()>;

    void setEmitEventCallback(EmitEventFn fn)              { emitEvent_ = std::move(fn); }
    void setOpenFileBrowserCallback(OpenFileBrowserFn fn)  { openFileBrowser_ = std::move(fn); }
    void setWindowSizeCallback(SetWindowSizeFn fn)         { setWindowSize_ = std::move(fn); }
    void setIsWindowSizeControlledQuery(IsWindowSizeControlledFn fn) { isWindowSizeControlled_ = std::move(fn); }
    void setQuitCallback(QuitFn fn)                        { quit_ = std::move(fn); }

    // Unsaved-changes tracking for the standalone close prompt -------------
    // True if the project structure/settings changed since the last save/load,
    // or any cartridge's battery RAM differs from its `.sav`. Called by PluginUI
    // from onClose() (C++→C++) and exposed over RPC for the modal.
    bool hasUnsavedChanges();
    // Emit "confirm-close" so the UI shows the unsaved-changes modal.
    void requestCloseConfirm() { emit("confirm-close", ""); }

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
    bool openExportZipBrowser();
    bool openLoadProjectBrowser();
    // Standalone unsaved-changes close prompt.
    struct UnsavedSummary { bool project = false; std::uint32_t sramSystems = 0; };
    UnsavedSummary getUnsavedSummary();
    // Path of the most recent project load/save ("" if never). Lets the modal
    // decide between saveProjectToPath and the save browser.
    std::string getCurrentProjectPath() { return currentProjectPath_; }
    // Write the sibling `<rom>.sav` for every system whose battery RAM is dirty.
    bool saveDirtySram();
    // Save the project to its known path silently (no dialog). False if there's
    // no current path yet — the caller should open the save browser instead.
    bool saveProject();
    // Standalone-only: actually quit (close the window) after the user confirms.
    bool quitStandalone();
    bool loadRomFromPath(std::string path);
    bool addRomFromPath(std::string path);
    bool replaceRomFromPath(std::uint32_t id, std::string path);
    bool removeSystem(std::uint32_t id);
    // Clone the selected system (same ROM, current SRAM, current savestate).
    // New instance is appended; linkGroupId is reset to 0 so the clone
    // doesn't inherit the source's link membership.
    bool duplicateSystem(std::uint32_t id);
    // Drops the path remembered from the last load/save. Called by the UI when
    // the system list transitions to empty, so a follow-up Save dialog doesn't
    // default to the previously loaded project's filename.
    bool clearCurrentProjectPath();
    std::vector<SystemEntry> listSystems();
    bool setFocus(std::uint32_t id);
    std::uint32_t getFocus();
    bool pressButton(std::int32_t button, bool down, std::optional<std::uint32_t> systemId);
    bool setLinkGroupId(std::uint32_t id, std::uint32_t groupId);
    std::uint32_t getMidiRouting();
    bool setMidiRouting(std::uint32_t routing);
    std::uint32_t getAudioRouting();
    bool setAudioRouting(std::uint32_t routing);
    // Returns the resolved zoom (1..6): per-project value if set, otherwise
    // the user-config default. setZoom always writes 1..6 to the project.
    std::uint32_t getZoom();
    bool setZoom(std::uint32_t zoom);
    std::uint32_t getLayout();
    bool setLayout(std::uint32_t layout);
    bool resetSystem(std::uint32_t id);
    bool newSram(std::uint32_t id);
    bool setFastBoot(std::uint32_t id, bool enabled);
    bool setModel(std::uint32_t id, std::uint32_t model);
    bool setHighpass(std::uint32_t id, std::uint32_t mode);
    bool setReloadOnRomChange(std::uint32_t id, bool enabled);

    // Poll the romPath of every SameBoy system whose `reloadOnRomChange` is
    // true; if the file's mtime has advanced since the last poll, reload
    // it (replacing the slot, preserving SRAM, dropping savestate). Called
    // from PluginUI::uiIdle.
    void pumpRomWatchers();
    // Global SRAM auto-save preference (UserConfig). When on, every system's
    // battery RAM is flushed to its sibling `<rom>.sav` on a timer (pumped from
    // PluginUI::uiIdle via pumpSramAutoSave). See system/SramAutoSave.hpp.
    bool setAutoSaveSram(bool enabled);
    // Global default zoom (1..6), persisted to config.json. Distinct from the
    // per-project setZoom — used as the fallback when a project carries no
    // explicit zoom. See UserConfig::setDefaultZoom.
    bool setDefaultZoom(std::uint32_t zoom);
    // Periodic battery-RAM flush; cheap no-op when the preference is off. Writes
    // only changed SRAM, creating the sibling .sav on first write. UI thread.
    void pumpSramAutoSave();
    // Seconds between auto-save scans (default 5). Exposed mainly so tests can
    // disable the throttle (0) to drive consecutive flushes deterministically.
    void setSramAutoSaveIntervalSec(double seconds) { sramAutoSaveIntervalSec_ = seconds; }
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

    // Relink-missing-files (a thin JSON project whose ROMs / kit WAVs moved).
    // After loadProjectFromPath finds missing files it holds the project pending
    // and emits "missing-files"; the UI walks the list, locating each file.
    using MissingFilesResponse = rp::MissingFilesResponse;
    MissingFilesResponse getMissingFiles();
    // Point one pending item at newPath (+ auto-relink siblings in its folder),
    // re-scan, and either commit (when nothing's left) or return the remainder.
    MissingFilesResponse relinkMissingFile(std::uint32_t systemIndex,
                                           std::int32_t  kitSlot,
                                           std::int32_t  sampleIndex,
                                           std::string   newPath);
    // Open a file browser for a relink; the path returns via "relink-path-selected".
    bool openRelinkBrowser(bool isRom);
    // Abandon the pending load, keeping the current project.
    bool cancelMissingFiles();

    // User config / key-pad bindings. See src/config/UserConfig.hpp.
    UserConfigDto getUserConfig();
    bool          setActiveKeyboardBindings(std::string name);
    bool          setActiveGamepadBindings(std::string name);
    // In-app bindings editor surface. Validation rules live in
    // UserConfig::isValidProfileName; all four return false / nullopt when
    // userConfig_ is null (LV2-UI / rpc-schema-dump).
    std::optional<BindingMapJson> getBindingProfile(std::string name);
    bool          saveBindingProfile  (std::string name, BindingMapJson bindings);
    bool          renameBindingProfile(std::string oldName, std::string newName);
    bool          deleteBindingProfile(std::string name);
    // Launch the platform file manager on the user config directory. False if
    // we have no UserConfig wired or the shell-out call fails.
    bool          openSettingsFolder();

    // Cartridge battery RAM I/O. saveSram writes to `<romPath>.sav`.
    // openSaveSramBrowser / openLoadSramBrowser / openSaveStateBrowser /
    // openLoadStateBrowser dispatch the chosen path back via
    // onFileBrowserSelected the same way openSaveProjectBrowser does.
    bool saveSram(std::uint32_t systemId);
    bool openSaveSramBrowser(std::uint32_t systemId);
    bool openLoadSramBrowser(std::uint32_t systemId);
    bool saveState(std::uint32_t systemId);
    bool openSaveStateBrowser(std::uint32_t systemId);
    bool openLoadStateBrowser(std::uint32_t systemId);

    // Recently-opened projects. Most-recent first; capped at
    // RecentFiles::kMaxEntries. See src/config/RecentFiles.hpp.
    std::vector<RecentFileDto> getRecentFiles();
    // Drop a recent entry. No-op (false) when the path isn't present.
    bool removeRecentFile(std::string path);
    // Set a display alias for a recent entry (empty clears it). The `.rplg`
    // file on disk is untouched. False when the path isn't present.
    bool renameRecentFile(std::string path, std::string newName);
    // Open a file browser to point a recent entry at a new `.rplg`. The chosen
    // path is applied server-side in onFileBrowserSelected (mode RelinkRecent),
    // which fires "recent-files-changed" so the menu refreshes. False when no
    // browser callback is wired.
    bool openRecentRelinkBrowser(std::string path);

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
    enum class PendingFileMode { LoadRom, AddRom, LoadProject, SaveProject, ExportZip,
                                 LoadSample, Relink, RelinkRecent,
                                 SaveSram, LoadSram, SaveState, LoadState };

    bool saveProjectToPath(const std::string& path);

    // Write a thin (path-only) `<rom>.rplg` beside `romPath` for a freshly-built
    // single system, unless one already exists. Returns the sibling path (whether
    // newly written or pre-existing), or empty on write failure.
    std::string writeSiblingProject(const SystemConfig& sysCfg,
                                    const std::string& romPath);
    bool exportZipToPath(const std::string& path);
    // Apply pendingProject_ to the DSP (recompiling kits first). Shared by
    // loadProjectFromPath (no missing files) and relinkMissingFile (all resolved).
    bool commitPendingProject();

    // Unsaved-changes tracking (standalone close prompt).
    void markProjectDirty() { projectDirty_ = true; }
    // Per-system battery dirtiness against the last-persisted baseline. Seeds
    // missing baselines (sibling .sav hash, else current battery) so a freshly
    // loaded system reads clean. Returns the count of dirty systems.
    std::uint32_t sramDirtyCount();

    Project*                  project_              = nullptr;
    CommandQueue*             commands_             = nullptr;
    EventQueue*               events_               = nullptr;
    std::atomic<double>*      sampleRate_           = nullptr;
    std::atomic<SystemId>*    focusedSystemId_      = nullptr;
    UserConfig*               userConfig_           = nullptr;
    RecentFiles*              recentFiles_          = nullptr;

    // Standalone unsaved-changes state. projectDirty_ flips on any project-
    // mutating RPC and clears on save/load. SRAM dirtiness is computed from the
    // per-system baseline hashes below.
    bool                      projectDirty_         = false;
    QuitFn                    quit_;

    // Lazy-allocated; constructed on first kit-related call so a project
    // that never opens an LSDJ ROM doesn't pay the enkiTS thread-pool
    // spin-up cost.
    std::unique_ptr<rp::lsdj::KitCompiler> kitCompiler_;

    // A parsed project awaiting missing-file relinks before it's applied. Held
    // on the UI thread between loadProjectFromPath and commit/cancel.
    std::optional<ProjectConfig> pendingProject_;
    std::string                  pendingProjectPath_;

    EmitEventFn               emitEvent_;
    OpenFileBrowserFn         openFileBrowser_;
    SetWindowSizeFn           setWindowSize_;
    IsWindowSizeControlledFn  isWindowSizeControlled_;

    PendingFileMode           pendingFileMode_      = PendingFileMode::LoadRom;
    // System id remembered for SaveSram / LoadSram / SaveState / LoadState
    // while the file dialog is up. 0 = none.
    std::uint32_t             pendingFileSystemId_  = 0;
    // Old path of the recent entry being relinked while its browser is up.
    std::string               pendingRelinkRecentPath_;

    // Common helpers used by saveSram / saveState / loadState.
    bool saveSramToPath(std::uint32_t systemId, const std::string& path);
    bool loadSramFromPath(std::uint32_t systemId, const std::string& path);
    bool saveStateToPath(std::uint32_t systemId, const std::string& path);
    bool loadStateFromPath(std::uint32_t systemId, const std::string& path);

    // Slice one memory region out of `sys`'s latest DSP-published state
    // snapshot (race-free). False if no snapshot yet or the region isn't
    // represented in the savestate (non-GB, or a cart with no battery RAM).
    bool sliceFromStateSnapshot(SystemBase* sys, rp::MemoryType type,
                                std::vector<std::uint8_t>& out);

    // Path of the most recent load/save. Used as the file-browser default name
    // so subsequent saves target the same file. Cleared when the project
    // becomes empty so a fresh session can't accidentally overwrite it.
    std::string               currentProjectPath_;

    MemorySubRegistry         memorySubs_;

    // Per-system romPath mtime cache for the reload-on-change watcher.
    // Entries are added/refreshed on first observation of an enabled flag
    // and dropped when the flag clears or the system disappears.
    struct RomWatchEntry {
        std::string                     path;
        std::filesystem::file_time_type mtime{};
    };
    std::map<SystemId, RomWatchEntry> romWatchers_;

    // Per-system "last-persisted SRAM hash" baseline (nullopt = not yet seeded).
    // Seeded from the sibling .sav (or the current battery) on first sight, and
    // updated on every persist (auto-save write + manual Save SRAM). Drives both
    // auto-save dedup and the unsaved-SRAM check. Pruned with their systems.
    std::map<SystemId, std::optional<std::uint64_t>> sramSavedHashes_;
    // Per-system SRAM hash captured at load (seeded once, never updated). Lets
    // the unsaved-SRAM check tell "changed since load" from "untouched."
    std::map<SystemId, std::uint64_t> sramLoadBaseline_;
    // Throttle: only scan for dirty SRAM every sramAutoSaveIntervalSec_ seconds.
    std::chrono::steady_clock::time_point lastSramAutoSave_{};
    double sramAutoSaveIntervalSec_ = 5.0;
};
