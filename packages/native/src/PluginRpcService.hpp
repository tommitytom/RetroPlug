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
#include "system/MemoryType.hpp"
#include "system/SystemConfig.hpp"   // SystemConfig (writeSiblingProject)
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

    // Atomic seed for the editing UI: the whole UI-visible project view
    // (structure + focus + project-wide settings) fetched in ONE call, so the
    // UI doesn't fan out six getters that could observe a torn state across a
    // concurrent mutation. Blob-free (SystemEntry carries no ROM/SRAM/state).
    // `projectZoom` is the RAW per-project zoom (0 = inherit user default);
    // routing/layout mirror the getMidiRouting/getAudioRouting/getLayout values.
    struct ProjectView {
        std::vector<SystemEntry> systems;
        std::uint32_t            focus;
        std::uint32_t            midiRouting;
        std::uint32_t            audioRouting;
        std::uint32_t            layout;
        std::uint32_t            projectZoom;
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

    // Project save/load byte-mover primitives. The .rplg save/export
    // orchestration lives in shared TS (@retroplug/retroplug
    // projectSerialization.ts); these just move bytes for it. Output bytes use
    // rfl::Bytestring (msgpack BIN -> Uint8Array); input bytes use
    // std::vector<std::uint8_t> (reflect-cpp's reader is int-array-only for
    // binary — see the FrameResponse note above). Mirrors HarnessRpcService.
    struct ZipEntry { std::string name; rfl::Bytestring bytes; };            // read / unzip / snapshot output
    struct ZipInput { std::string name; std::vector<std::uint8_t> bytes; };  // zip input
    struct ProjectSnapshot { std::string config; std::vector<ZipEntry> blobs; };

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
    // patterns: whitespace-separated glob list ("*.gb *.gbc"); filterName: optional label. Both nullable.
    using OpenFileBrowserFn        = std::function<void(const char* title, bool saving, const char* defaultName,
                                                        const char* patterns, const char* filterName)>;
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
    // dispatch to load / add / save based on which open* call set
    // pendingFileMode_ most recently. Project *load* is handed to the UI (TS)
    // via a "load-path-selected" event; the orchestration lives there now.
    void onFileBrowserSelected(const char* path);

    // Hand a `.rplg` path to the UI's TS load orchestration (emits
    // "load-path-selected"). The project-load parse/scan/relink/commit machine
    // lives in the UI now; this is the C++-side entry the standalone autoload +
    // the test harness use (the browser + sibling-.rplg paths emit inline).
    void requestLoadProject(const std::string& path) { emit("load-path-selected", path); }

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

    // --- Project save/export byte-mover primitives -------------------------
    //
    // The .rplg save + zip-export orchestration lives in shared TS
    // (@retroplug/retroplug projectSerialization.ts, the same module the CLI
    // harness drives); these primitives move the bytes it can't. readFile /
    // writeFile are generic fs; zipEntries / unzipEntries wrap miniz;
    // snapshotProjectConfig walks the live project into a thin config + keyed
    // blobs. (The load rebuild — the harness's applyProjectConfig — is deferred
    // with the plugin *load* path, which is async via the CommandQueue.)
    rfl::Bytestring        readFile(std::string path);
    bool                   writeFile(std::string path, std::vector<std::uint8_t> bytes);
    rfl::Bytestring        zipEntries(std::vector<ZipInput> entries);
    std::vector<ZipEntry>  unzipEntries(std::vector<std::uint8_t> bytes);
    // baseDir non-empty => rebase asset paths relative to it (the thin path-only
    // save, portable folder); empty => leave absolute (the self-contained zip
    // export embeds every blob, so paths don't matter). Keeps ProjectPaths native.
    ProjectSnapshot        snapshotProjectConfig(std::string baseDir);
    // Post-write bookkeeping the old saveProjectToPath / exportZipToPath did
    // inline: clear the dirty flag, and for a real save (exported=false) add the
    // path to recents + remember it as currentProjectPath. Emits
    // "project-saved" / "project-exported".
    bool                   notifyProjectSaved(std::string path, bool exported);
    // Existence check for the TS missing-files scan (the scan/relink logic itself
    // is shared TS — @retroplug/retroplug missingFiles.ts).
    bool                   fileExists(std::string path);
    // Commit a resolved project. The .rplg *load* orchestration (parse, schema
    // check, toAbsolute, missing-file scan/relink) moved to shared TS; TS calls
    // this once everything's located. Restores the blob entries into the thin
    // config, recompiles kits, hands it to the DSP (Command::makeLoadProject —
    // async; the DSP applies + re-emits ProjectLoaded), and does the post-load
    // bookkeeping (recent + currentProjectPath + clear-dirty + emit
    // "project-loaded"). `path` is the source .rplg. Replaces commitPendingProject.
    bool                   commitProject(std::string config, std::vector<ZipInput> blobs, std::string path);
    // Discard the current project for a clean, empty one: drops all systems and
    // resets the project-wide settings (zoom/layout/routing) to defaults, exactly
    // as loading a default ProjectConfig would. Forgets the remembered project
    // path so a follow-up "Save Project" opens the dialog. Applied via a
    // LoadProject command so it runs on the DSP thread like any project mutation.
    bool newProject();
    // Standalone-only: actually quit (close the window) after the user confirms.
    bool quitStandalone();
    bool loadRomFromPath(std::string path);
    bool addRomFromPath(std::string path);
    bool replaceRomFromPath(std::uint32_t id, std::string path);
    // Load the mGB Game Boy MIDI-synth ROM that's embedded in the binary. Like
    // a "Load…" (replaces/adopts as the first system) but with no file: no
    // recent-files entry, no sibling .sav (it's pathless and battery-less). The
    // system carries embeddedRom="mgb" so saved projects reload it.
    bool loadMgb();
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
    // Atomic seed for the UI: systems + focus + settings in one call. Replaces
    // the UI's six-getter fan-out (listSystems/getFocus/getMidiRouting/
    // getAudioRouting/getProjectZoom/getLayout). See ProjectView.
    ProjectView getProjectView();
    bool setFocus(std::uint32_t id);
    std::uint32_t getFocus();
    bool pressButton(std::int32_t button, bool down, std::optional<std::uint32_t> systemId);
    bool setLinkGroupId(std::uint32_t id, std::uint32_t groupId);
    std::uint32_t getMidiRouting();
    bool setMidiRouting(std::uint32_t routing);
    std::uint32_t getAudioRouting();
    bool setAudioRouting(std::uint32_t routing);
    // App version as a bare semver string ("0.6.2"), from Version.hpp. The UI
    // shows it in the menu chrome title; the display layer adds any "v" prefix.
    std::string getVersion();
    // Returns the resolved zoom (1..6): per-project value if set, otherwise
    // the user-config default.
    std::uint32_t getZoom();
    // Returns the RAW per-project zoom (0..6): 0 = "inherit the user default",
    // 1..6 = explicit. The UI uses this to show "Default (Nx)" vs "Nx" and to
    // track the user default live while a project carries no explicit zoom.
    std::uint32_t getProjectZoom();
    // Writes the per-project zoom. 0 = inherit the user default, 1..6 = explicit.
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
    // Global loose-`.sav` mirror preference (UserConfig). `mode` is a SramMirror
    // enum name ("Off" | "OnProjectSave" | "Continuous"); unrecognised values
    // fall back to OnProjectSave. Persists to config.json and pushes the mode to
    // the DSP for its flush hooks. See config/SramMirror.hpp and porting/23 (D2).
    bool setSramMirror(std::string mode);
    // Global default zoom (1..6), persisted to config.json. Distinct from the
    // per-project setZoom — used as the fallback when a project carries no
    // explicit zoom. See UserConfig::setDefaultZoom.
    bool setDefaultZoom(std::uint32_t zoom);
    // Periodic battery-RAM flush; idle-tick writes happen only in Continuous
    // mirror mode (OnProjectSave/Off leave the loose .sav to the DSP flush
    // hooks). Also reconciles the DSP's mirror mode. Writes only changed SRAM,
    // creating the sibling .sav on first write. UI thread.
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

    // Relink-missing-files: a thin project whose ROMs / kit WAVs moved. The scan /
    // relink / pending-project orchestration lives in the UI (shared TS
    // missingFiles.ts over fileExists + commitProject); the only native piece left
    // is the file dialog for *locating* a file. Open a browser; the chosen path
    // returns via "relink-path-selected". `kind` ("rom" | "sram" | "sample")
    // selects the file-type filter.
    bool openRelinkBrowser(std::string kind);

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
    // because the service owns the file IO + system construction. When
    // `disambiguate` is set (adding an instance), the built system is given a
    // loose-battery suffix that doesn't collide with an existing same-ROM
    // system, and its sibling `.sav` is read from that suffixed path. When
    // `explicitSav` is non-empty, it seeds the battery and (unless it's the
    // natural sibling) becomes the system's persisted `savPath` override.
    SystemBase* buildSystemFromPath(const std::string& path, bool disambiguate,
                                    const std::string& explicitSav = "");

    // Find a ROM to pair with a user-picked `.sav`: a same-directory file whose
    // stem matches the save's (also trying the base stem when the save is a
    // `<name>-N.sav` duplicate slot), validated by content via detectRomFormat.
    // Returns the ROM path, or empty if none is found (caller opens a 2nd browser).
    std::string findSiblingRom(const std::string& savPath) const;

    // Build + push a (ROM, explicit sav) pairing. `add` => AddSystem (new instance);
    // else LoadRom (replace the focused tile, with recent/currentProjectPath
    // bookkeeping, but NOT deferring to a sibling `.rplg` — the user's sav wins).
    bool loadRomPaired(const std::string& romPath, const std::string& savPath, bool add);

    // Dispatch a first-dialog Open selection: a ROM loads/adds as before; a `.sav`
    // pairs with its sibling ROM, or arms a 2nd (ROM) browser when none is found.
    bool handleOpenRomSelection(const std::string& path, bool add);

    // Lowest free loose-battery suffix for `romPath`: 0 when no live system owns
    // the plain `<rom>.sav`, else the smallest N>=2 that neither a live system
    // owns nor already exists as `<rom>-N.sav` on disk. Skipping on-disk files
    // stops a duplicate from clobbering a since-removed instance's orphaned
    // battery file. See SystemBase::savSuffix.
    std::uint32_t assignSavSuffix(const std::string& romPath) const;

    // saveProjectToPath / addRomFromPath / etc share the same "emit
    // (channel, path)" pattern; this is the one indirection that points
    // back at the JS engine.
    void emit(const std::string& channel, const std::string& payload) const;

    // File-browser callback target. Open-* methods set this; the DPF host
    // delivers the chosen path back via onFileBrowserSelected.
    enum class PendingFileMode { LoadRom, AddRom, LoadProject, SaveProject, ExportZip,
                                 LoadSample, Relink, RelinkRecent,
                                 SaveSram, LoadSram, SaveState, LoadState,
                                 // 2nd-dialog modes: a picked `.sav` had no sibling
                                 // ROM, so the next pick is the ROM to pair with
                                 // pendingSavPath_ (Add = new instance vs replace).
                                 PairRomForSav, PairRomForSavAdd };

    // Write a thin (path-only) `<rom>.rplg` beside `romPath` for a freshly-built
    // single system, unless one already exists. Returns the sibling path (whether
    // newly written or pre-existing), or empty on write failure.
    std::string writeSiblingProject(const SystemConfig& sysCfg,
                                    const std::string& romPath);

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
    // A user-picked `.sav` awaiting its ROM in the 2nd browser (PairRomForSav*).
    std::string               pendingSavPath_;

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
    // Last mirror mode pushed to the DSP (SetSramMirror). The pump re-pushes
    // whenever the UserConfig value drifts from this, so a config.json edit
    // (efsw reload) or a toggle converges the DSP within one idle tick. -1 =
    // never pushed, so the first pump always sends the current mode.
    int lastPushedSramMirror_ = -1;
};
