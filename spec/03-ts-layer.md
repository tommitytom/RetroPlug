# 03 — The TypeScript layer

`packages/retroplug/` is the TypeScript half of the build — the embodiment
of the thesis **"native owns bytes and cores; TypeScript owns meaning."** Everything a
user *decides* lives here: system identity and order, ROM classification, path/sibling/
suffix resolution, the project model and its serialization, the config schemas, routing
and role behaviour, and the whole UI. TS drives native through one narrow **`Backend`**
interface over resolved paths, opaque byte buffers, and opaque config blobs — it never
asks native to make a policy choice.

The layer is organised as three concentric rings, each depending only on the one inside
it:

- **Pure kernels** — no `Backend`, no IO, unit-testable in isolation: `platform.ts`,
  `systemsList.ts`, `savPaths.ts`, `projectConfig.ts`, `projectPaths.ts`, `pathUtil.ts`,
  `midiRouting.ts`, `projectBinaries.ts`, `projectMissing.ts`, `recentList.ts`, the
  `*Serialization.ts` codecs, the zod models, plus `dspKernel.ts` / `kernelProjection.ts`.
- **Stores** — a kernel wired to the `Backend`, owning live state: `systemsStore.ts`,
  `projectStore.ts`, `recentStore.ts`, `userConfigStore.ts`, `bindingsStore.ts`, plus the
  store-adjacent services `fileSelection.ts`, `fileWatcher.ts`, `sramAutoSave.ts`.
- **Runtime / host seams** — `backend.ts` / `realBackend.ts` (control-plane RPC),
  `dspRuntime.ts` (the DSP-context client), the composition roots
  `pluginControlPlane.ts` / `appStores.ts` / `appHost.ts`, and the React/LVGL `ui/` tree.

Roles and the DSP role kernel are their own subject — **doc 04** covers them end to end;
this doc summarises them and links across. For the threading model — the command ring,
the `SnapshotRegistry` read door, the release ring, the two QuickJS runtimes — see
**doc 01**; the seams below reference those concepts, they don't re-explain them.

---

## The `Backend` interface

[`src/backend.ts`](../packages/retroplug/src/backend.ts) is the **single** native
contract the application calls. It is deliberately narrow and deliberately grown: *if a
thing can be done in pure TS, it belongs in the application layer, not here.* It is
**synchronous** on purpose — in the real plugin these are in-process calls over the
txiki↔C++ bridge — which keeps application logic straight-line and trivially testable
against an in-memory mock. The **one** async method is `openFileBrowser`, which waits on
human input.

The methods, grouped by concern:

| Group | Methods | What native does |
|---|---|---|
| **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
| **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
| **Emulator lifecycle** | `constructSystem(spec, id)` · `removeSystem(id)` | Build/activate a core under a **TS-allocated id**; drop one. No `duplicate`/`reload` primitive — those are TS orchestration |
| **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path (the one async method) |
| **Live config** | `applySystemSetting(id,key,value)` · `applyRoleConfig(id,kind,config)` · `setAudioRouting(mode)` | Apply the two universal settings + a **system-role**'s knobs to the live core; set the block runner's output routing |
| **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
| **Live reads** | `readState(id)` · `readSram(id)` · `getFrame(id)` | Pull the latest tear-free snapshot from the `SnapshotRegistry` (see doc 01) — never touches a live core |
| **Byte codecs** | `zip(entries)` · `unzip(bytes)` | miniz deflate/inflate — the only native part of `.rplg` framing (TS assembles every entry) |
| **LSDj sav codec** | `savFromJson(json)` | Run the version-aware sav codec; `savFromJson("{}")` yields a valid 128 KiB image |

The real adapter [`src/realBackend.ts`](../packages/retroplug/src/realBackend.ts)'s
`createRealBackend()` resolves `globalThis[Symbol.for("plugin")].__rpcSend` and wraps it in
a self-contained synchronous JSON-RPC `call()`
([realBackend.ts:65](../packages/retroplug/src/realBackend.ts#L65)). Binary rides as
`Uint8Array` in both directions (the QuickJS codec decodes a typed byte param straight into
`rfl::Bytestring`), so nothing is base64'd or number-array'd on this path. `specParams`
([realBackend.ts:79](../packages/retroplug/src/realBackend.ts#L79)) omits null path
fields (so native reads `nullopt`, not `""`) and attaches seed bytes only when present.
This same adapter serves all three hosts — the difference is only which
set of RPC facets is bound behind `__rpcSend` (see doc 01/02).

### `ConstructSpec` — the resolved-paths-only build request

`constructSystem` takes a `ConstructSpec`
([backend.ts:205](../packages/retroplug/src/backend.ts#L205)) in which **TS has
already resolved every path, suffix, sibling, and classification** — *native never sees a
suffix, never looks for a sibling, never classifies.*

| Field | Meaning |
|---|---|
| `romPath` / `embeddedRom` | The ROM file to slurp, or a binary-baked marker (e.g. `"mgb"`) with `romPath = ""` |
| `platform` / `core` | What the ROM targets + which factory builds it — TS classifies via [`platform.ts`](../packages/retroplug/src/platform.ts) |
| `savPath` / `statePath` | The exact battery file (load-from + auto-save-to) and savestate to boot from, or `null` |
| `replaceId?` | When set, swap this existing id **in place**; otherwise append |
| `sramBytes?` / `stateBytes?` | Seed bytes (zip-import blob, carried battery, role-synthesized sav) that override what native reads from disk; `savPath` stays the auto-save target |
| `settings?` | The system-role config as JSON, applied **at construct** so a restored savestate isn't nuked by a post-build restart |

`openFileBrowser` is the odd one out and does **not** ride the RPC bridge: it uses the
UI-direct hooks `__rp_openFileBrowser` / `__rp_onFileBrowserResult` on the shared context
([realBackend.ts:27-51](../packages/retroplug/src/realBackend.ts#L27)), with a
single module-level `pendingBrowse` slot (one dialog at a time). Where the hook is absent
(the headless UI harness), a browse resolves `null` — inert, like the other `__rp_*` seams.

---

## The stores

Every store is a plain, framework-agnostic class with a uniform shape: a constructor
`(backend, onChange?, …)`, getters that return a **fresh** array/object each call (`view()`
even does per-entry `fileExists` RPCs), mutators, and a **no-op-guarded** `onChange` signal
— a mutation that doesn't change serialized state neither writes nor notifies. The
persistent config models and on-disk shapes are the subject of **doc 05**; here we cover
responsibilities and the TS-owns-everything invariants.

The load-bearing point: **TypeScript is the source of truth for system identity, order,
focus, and dirty.** Native holds cores keyed by an id, but it never mints an id, never
tracks order, never knows which system is focused, and never decides the project is dirty.

### SystemsStore — [`src/systemsStore.ts`](../packages/retroplug/src/systemsStore.ts)

The live systems list. `SystemEntry[]`
([systemsList.ts:21](../packages/retroplug/src/systemsList.ts#L21)) is the persistent
model (`{ id, platform, core, romPath, savPath override, savSuffix, embeddedRom, settings,
roles }`); the private state is `entries`, `focusedId`, and `dirty`
([systemsStore.ts:73-76](../packages/retroplug/src/systemsStore.ts#L73)). `view()`
([systemsStore.ts:85](../packages/retroplug/src/systemsStore.ts#L85)) projects a
`SystemView[]` for the UI, computing the live `focused` and `missing` flags each call.

- **TS owns the system-id counter.** `allocSystemId()` increments a module-scoped
  `nextSystemId` ([systemsStore.ts:37](../packages/retroplug/src/systemsStore.ts#L37)) —
  *"native never allocates."* Ids are opaque handles, one id space per control-plane JS
  context (1:1 with the native `Project`), and start at **1** because the snapshot registry
  uses 0 as its free-slot sentinel.
- **`classifyRom`** ([systemsStore.ts:48](../packages/retroplug/src/systemsStore.ts#L48))
  is the one place ROM bytes enter TS — and just the `ROM_SNIFF_LEN` (0x134) header prefix.
- **Mutators through `Backend`:** `addSystem` (append), `loadRom` (replace focused, or defer
  to a sibling `<rom>.rplg`), `loadMgb` (embedded), `replaceSystem`, `removeSystem`, plus the
  per-system `setGain` / `setReloadOnRomChange` (→ `applySystemSetting`) and `setRoleConfig`
  ([systemsStore.ts:329](../packages/retroplug/src/systemsStore.ts#L329)) — where
  **only a `"system"`-category role's config crosses to the live core** (`applyRoleConfig`);
  a feature-role's config stays pure TS.
- **`duplicateSystem`** ([systemsStore.ts:170](../packages/retroplug/src/systemsStore.ts#L170))
  is TS orchestration with **no native duplicate method**: it pulls `readState(id)` (state
  includes SRAM), then `constructSystem` seeded with those `stateBytes` under a fresh
  `allocSystemId()`, and appends.

### The reconstruct-in-place idiom

The control plane **reconstructs** a core rather than live-injecting into it (no `GB_reset`, no
in-place SRAM clear). The private `rebuildInPlace(id, seed)`
([systemsStore.ts:233](../packages/retroplug/src/systemsStore.ts#L233)) is the shared
body: it captures the source spec, builds a fresh core under a **new id** that swaps the old
one (`replaceId: id`), and preserves identity + focus
(`replaceById(entries, id, {...src, id: newId})`; `if (focusedId === id) focusedId = newId`).
The `seed` overrides what native would read from disk — `sramBytes` = cold-boot battery,
`stateBytes` = boot-from-savestate — while `savPath` stays the auto-save target. Five public
operations are thin wrappers over it:

| Op | Seed |
|---|---|
| `reloadSystem` | live SRAM carried forward, cold boot |
| `reset` | live SRAM carried forward (hardware-style reset) |
| `loadState(path)` | `stateBytes` from the file |
| `loadSram(path)` | `sramBytes` from the file |
| `newSram` | a blank `new Uint8Array(0x20000)` battery |

A separate **project-load rebuild seam** is deliberately quiet (no dirty/onChange, since a
load isn't a user edit): `clear()` tears every system down; `adopt(config, blobs?)`
([systemsStore.ts:374](../packages/retroplug/src/systemsStore.ts#L374)) reconstructs
one system preserving its **exact** `savSuffix`/`savPath`, with `blobs` seeding SRAM/state on
a zip import. Default roles are derived before the build from a `ROLE_HEADER_LEN` (0x150)
header prefix via `defaultRoles`, and each role's `onConstruct` hook folds over the spec
(`applyConstructHooks`) — e.g. the LSDj empty-sav seed — before instantiation.

### ProjectStore — [`src/projectStore.ts`](../packages/retroplug/src/projectStore.ts)

The top-level source of truth. It **owns** the `SystemsStore` (wiring its `onChange` →
`markDirty` + `onSystemsChange`,
[projectStore.ts:60](../packages/retroplug/src/projectStore.ts#L60)), the project
settings (`ProjectSettings` = `{ layout, midiRouting, audioRouting, zoom }`,
[projectConfig.ts:20](../packages/retroplug/src/projectConfig.ts#L20)), the
`currentPath`, `dirty`, and a `pendingLoad` latch. It exposes **three** change signals:
`setOnSystemsChange` (structural — drives DSP re-projection), `setOnChange` (any state —
settings/dirty), and the transient focus signal via `systems.setOnFocusChange` (re-renders
tiles without dirtying or re-projecting).

Project I/O is doc 05's territory, but the store surface is: `newProject`, `save(path)` (a
thin `.rplg` — raw JSON, paths only), `export(path)` (an export `.rplg.zip` — PKZIP of
`project.json` + per-system SRAM/savestate blobs gathered from the read door), `exportBytes()`
(the in-memory chunk for DPF `getState`), `loadBytes(bytes, baseDir)` (the DPF `setState` zip
chunk), and `load(path)` (routes by **extension**: a `.rplg` is always thin JSON, a `.rplg.zip`
is PKZIP; a `.rplg` that isn't pure JSON errors — never loaded as a zip). The load lifecycle
refuses a **newer** schema stamp (`{kind:"incompatible"}`), absolutizes paths, scans for
missing files, and holds `pendingLoad` for `relink` or `commit`
([projectStore.ts:236](../packages/retroplug/src/projectStore.ts#L236)).
`setAudioRouting` is the **only** project setting that also reaches native audio, via
`pushAudioRouting()` → `backend.setAudioRouting`
([projectStore.ts:278](../packages/retroplug/src/projectStore.ts#L278)).

The project **name** is two-tier. `name()` is the project's own name - blank unless the user
typed one under `Project` > `Name` (`setName`, which marks the project dirty), and the ONLY
one persisted (`buildConfig` omits a blank `name`, so a nameless `.rplg` carries no field).
`displayName()` is what the window / menu titles SHOW - that name when set, else `deriveName()`:
the primary system's `savPath` stem, else its `romPath` stem (primary = focused, else first).
The recents entry gets a fuller derivation, `recentName()`: the project's own name when set,
else the primary cart's identity `"<sav> - <rom>"` - the loaded sav's stem (an explicit override,
or a battery cart's suffix-derived sibling) then the ROM's, collapsed to one segment when the
stems match (the usual `<rom>.sav` case) and empty for an embedded cart. Every recents record
(`save` / `export` / `adoptRomProject` / load `commit`) passes it alongside `currentSong()`, so
the menu composes `"SONG - sav - rom"` (or `"SONG - project name"`) - and none of it reaches disk.

### RecentStore — [`src/recentStore.ts`](../packages/retroplug/src/recentStore.ts)

A `RecentEntry[]` (`{ path, name }`) most-recent-first, capped at `MAX_ENTRIES = 10`
([recentList.ts:13](../packages/retroplug/src/recentList.ts#L13)). Incoming paths are
canonicalized via `backend.canonicalize` (the dedupe key); `view()` computes live `missing` +
`label` (the recorded `name`, else the basename minus the project extension). `name` is
whatever `ProjectStore.recentName()` resolved when the entry was recorded - there is no
per-entry rename; the UI's only naming verb is `Project` > `Name`. `commit(next)` ([recentStore.ts:91](../packages/retroplug/src/recentStore.ts#L91))
serializes and skips both write and notify when identical. Persists atomically to
`<configDir>/recent.json`.

### UserConfigStore — [`src/userConfigStore.ts`](../packages/retroplug/src/userConfigStore.ts)

`UserConfig` = `{ activeKeyboardBindings, activeGamepadBindings, defaultZoom 1-6,
sramAutoSave: "Off"|"OnProjectSave"|"Continuous" }`
([userConfig.ts:23](../packages/retroplug/src/userConfig.ts#L23)), persisted to
`config.json`. `load()` writes defaults on first run; `reload()`
([userConfigStore.ts:37](../packages/retroplug/src/userConfigStore.ts#L37)) is the
file-watch reaction and keeps the current value on a missing / malformed / newer-stamped
file. Setters validate and reject bad input.

### BindingsStore — [`src/bindingsStore.ts`](../packages/retroplug/src/bindingsStore.ts)

Per-profile `bindings/<name>.json` maps (`{ name, keyboard, gamepad, keyboardActions,
gamepadActions }` — the `*Actions` sections bind the app actions Open Menu / Cycle Instances,
resolved by `buildKeyToAction` / `buildGamepadToAction`), reading the
active-profile names through `UserConfigStore`. CRUD: `ensureDefaults` (seeds
`bindings/default.json`), `availableProfiles`, `loadProfile` / `saveProfile`, `renameProfile`
(no-clobber, repoints active refs), `deleteProfile` (refuses the active one), and
`resolvedBindings()` ([bindingsStore.ts:110](../packages/retroplug/src/bindingsStore.ts#L110))
which merges the active keyboard + gamepad profiles, each falling back to a default. Names
are validated by `isValidProfileName`
([bindingsStore.ts:24](../packages/retroplug/src/bindingsStore.ts#L24)).

### Store-adjacent services

- **FileSelection** — [`src/fileSelection.ts`](../packages/retroplug/src/fileSelection.ts):
  turns a file pick into a systems op. `browse(mode)` opens the ROM-or-sav dialog, classifies
  (ROM by content / `.sav` by extension / other), and routes. A picked `.sav` finds its
  sibling ROM, or opens a second ROM-only browser — *"just another `await` inside the same
  Promise"*, no pending-mode latch. `SelectionOutcome` = `loaded | added | deferred | error
  | cancelled`.
- **FileWatcher** — [`src/fileWatcher.ts`](../packages/retroplug/src/fileWatcher.ts):
  the TS reaction to native's watcher. **"Watcher = C++, policy = TS."** `pump()` drains
  `backend.drainChangedPaths()` at idle and routes each: `config.json` → `userConfig.reload()`,
  `bindings/*.json` → a refresh signal, a system's ROM → `reloadSystem(id)` **when
  `reloadOnRomChange` is on**. Native (`NativeFileWatcher`, efsw) owns the watching — the config dir +
  `bindings/` recursively, plus each ROM's parent dir (registered via `setWatchedRoms`, recomputed on
  every systems change); TS owns what a change *means*. The plugin composes it in
  [`pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) and drives `pump()` from the
  UI idle loop (`__rp_pumpWatcher`).
- **SramAutoSaver** — [`src/sramAutoSave.ts`](../packages/retroplug/src/sramAutoSave.ts):
  the loose-`.sav` mirror policy over `backend.readSram(id)` + `resolveSavPath`, gated on the
  `sramAutoSave` preference. `flushOnSave()` writes at save/quit; `pump()` is the Continuous
  idle-tick. An FNV-1a change hash dedups writes and distinguishes seed-vs-write against the
  on-disk file. Pure decision logic — native only reads SRAM and writes bytes.

---

## The control-plane composition

Three small modules compose the store graph and wire it to the host. They exist so every
host builds the *same* graph the *same* way.

### `pluginControlPlane.ts` — the plugin composition root

[`src/pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) is
evaluated once by the plugin host in its txiki context. It composes `createRealBackend()` +
`buildAppRegistry()` + `RecentStore` + `ProjectStore` + `createDspRuntime()`, loads the DSP
kernel (`dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__))`,
[pluginControlPlane.ts:74](../packages/retroplug/src/pluginControlPlane.ts#L74)), and
wires `project.setOnSystemsChange(() => syncDspFromStore(project, dsp))`.

It then exposes the **string-only C++→JS surface** for project I/O — the plugin drives these
directly with no further RPC:

| Global | Purpose |
|---|---|
| `__rp_loadProjectPath(path)` | Autoload a `.rplg` from disk |
| `__rp_loadProjectB64(b64)` | DPF `setState`: load an in-memory chunk (empty chunk = no-op) |
| `__rp_saveProjectB64()` | DPF `getState`: export the project as a base64 `.rplg` chunk |
| `__rp_newProject()` | DPF `setState("")`: reset to an empty project |
| `__rp_ready` | Set true once composition + kernel load succeeded |

Base64 is done **here** in runtime-independent code
([pluginControlPlane.ts:24-64](../packages/retroplug/src/pluginControlPlane.ts#L24)),
not native, because DPF state is NUL-terminated UTF-8 while a `.rplg` is binary PKZIP — the
C++ boundary stays string-only.

### `appStores.ts` — the shared store graph

[`src/appStores.ts`](../packages/retroplug/src/appStores.ts)'s `composeAppStores({
backend?, notify? })` builds the full graph — `registry, recent, userConfig, bindings,
project, fileSelection` — so every host constructs it identically. Change notification is
injected as one `notify(channel)` where `StoreChannel = "project" | "systems" | "recent" |
"userConfig" | "bindings"`. It wires the project's two signals plus focus
([appStores.ts:68-71](../packages/retroplug/src/appStores.ts#L68)) but **does not**
wire the DSP (that stays the control plane's job). The DPF plugin's editor builds its graph this
way (through [`StoreProvider`](../packages/retroplug/ui/stores/StoreProvider.tsx#L35)),
but the plugin's **control-plane bundle** ([`pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts))
still composes a *separate* graph inline — so the plugin currently runs two store graphs on its one
shared context (the control plane's, which drives DSP projection and DAW get/setState, and the
editor's) rather than one. Unifying them onto a single `composeAppStores` graph is flagged in
[appStores.ts:14-17](../packages/retroplug/src/appStores.ts#L14) but not done.

### `appHost.ts` — host glue

[`src/appHost.ts`](../packages/retroplug/src/appHost.ts) holds the wiring every host
reuses: `buildAppRegistry()` assembles the control-plane role registry (core roles + DSP
feature behaviours + ROM providers — see doc 04), and `syncDspFromStore(project, dsp)`
([appHost.ts:32](../packages/retroplug/src/appHost.ts#L32)) projects the live systems
into a kernel structure and pushes it — the callback installed on `onSystemsChange`.

### The `__rp_*` UI↔native direct-hook pattern

Window-owning and async seams bypass the RPC surface and hang functions on the shared
context (one UI thread), because they touch native window state that the fire-and-forget RPC
model can't. All of them optional-chain the globals, so they are **inert in the headless
harness**:

| Hook | Owner | Purpose |
|---|---|---|
| `__rp_openFileBrowser` / `__rp_onFileBrowserResult` | [realBackend.ts](../packages/retroplug/src/realBackend.ts) | The async file dialog |
| `__rp_setWindowSize` / `__rp_isWindowSizeControlled` | [useWindowSize.ts:43-51](../packages/retroplug/ui/lvgl/useWindowSize.ts#L43) | Fit-to-grid resize / tiling-WM detection |
| `__rp_mountUI` / `__rp_unmountUI` | [main.tsx:31-32](../packages/retroplug/ui/main.tsx#L31) | Render/teardown across window close/reopen |
| `__rp_tagTestId` | [StableSlot.tsx:20](../packages/retroplug/ui/lvgl/StableSlot.tsx#L20) | Test-only widget tagging (inert in production) |

---

## The React / LVGL UI — [`ui/`](../packages/retroplug/ui)

The UI renders React (via `react-reconciler`) to **LVGL** widgets through `lvgljs-ui` /
`lvgljs`. It reads the stores reactively and mutates them imperatively.

### Entry + provider chain

[`main.tsx`](../packages/retroplug/ui/main.tsx) defines
`<StoreProvider><FocusProvider><App/></FocusProvider></StoreProvider>` and installs
`__rp_mountUI` / `__rp_unmountUI` so the host renders after a display attaches and keeps
QuickJS alive across a window close/reopen.

- [`StoreProvider.tsx`](../packages/retroplug/ui/stores/StoreProvider.tsx) builds the
  graph lazily via `useRef` (so `createRealBackend()` resolves after the host binds
  `__rpcSend`) and passes `composeAppStores` a `notify(channel)` that fans out to per-channel
  listener `Set`s; it exposes `subscribe`.
- [`useStores.ts`](../packages/retroplug/ui/stores/useStores.ts) exposes `useStores()`
  (imperative mutators) and `useStoreSnapshot(channel, read)`
  ([useStores.ts:40](../packages/retroplug/ui/stores/useStores.ts#L40)), which caches
  `read()` and recomputes **only after a notify** (via `useSyncExternalStore` + an
  invalidate-on-notify ref) — necessary because store getters return a fresh reference every
  call. Public hooks: `useSystems`, `useProjectSettings`, `useIsDirty`, `useRecent`,
  `useUserConfig`, `useBindings`.

### App controller — [`App.tsx`](../packages/retroplug/ui/App.tsx)

Owns menu open/close and swaps between the start menu (empty project) and the system grid.
**Esc is owned here, in one place**
([App.tsx:61-72](../packages/retroplug/ui/App.tsx#L61)): closed + a focused system →
open the instance menu anchored to it; open → close. The start menu is always open when
empty. When the grid shows idle (no menu), the keypad is pointed at the sink group so arrow
keys don't leak into the clickable tiles. The window is fit to the grid on instance/zoom
change unless a tiling WM owns geometry (`isWindowSizeControlled()`). It drives
`useGameInput({ active: !empty && !menuOpen, focusedId })` and renders `<Menu>` (empty) or
`<SystemGrid>`.

### The grid — [`screens/grid/`](../packages/retroplug/ui/screens/grid)

- [`SystemGrid.tsx`](../packages/retroplug/ui/screens/grid/SystemGrid.tsx) renders one
  tile per system from `useSystems()`. `fitZoom`
  ([SystemGrid.tsx:25](../packages/retroplug/ui/screens/grid/SystemGrid.tsx#L25)) caps
  zoom to keep the whole grid visible. Each tile is wrapped in a
  `StableSlot key={slot-${sys.id}}`; the slot matching `menuSystemId` swaps its single child
  EmulatorTile↔`<Menu>` so sibling tiles keep rendering.
- [`EmulatorTile.tsx`](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx) pulls
  `backend.getFrame(systemId)` on each native `"frame"` event and blits it into an LVGL
  `Canvas` via `setBuffer` ([EmulatorTile.tsx:50](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx#L50)).
  Click → `setFocus`; an unfocused tile gets a translucent dim overlay, a focused one an accent
  border (both suppressed when it is the only tile).
- [`layout.ts`](../packages/retroplug/ui/screens/grid/layout.ts) is pure grid math
  (`GB_NATIVE_W/H`, `shapeFor`, `gridContentSize`, `getTileBounds`).

**Why `StableSlot`.** `lv_binding_js`'s `insertChildBefore` ignores its `beforeChild`
argument and always appends, so React reordering a child (or swapping its type) at a stable
position lands the new widget at the *end* of the LVGL child list.
[`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx) is the workaround: a
fixed-position wrapper View whose position never changes, whose *single* child swaps — where
`appendChild` lands correctly. Its `tagTestId`
([StableSlot.tsx:20](../packages/retroplug/ui/lvgl/StableSlot.tsx#L20)) tags a
widget's native uid for the test harness and is inert in production.

### The menu — [`screens/menu/`](../packages/retroplug/ui/screens/menu)

- [`menuTree.ts`](../packages/retroplug/ui/screens/menu/menuTree.ts) is the pure data
  model: `MenuItem { id, label, kind: "action"|"submenu"|"separator"|"cycler", … }` where each
  leaf carries its own effect callback (no dispatch).
- [`menuDefs.ts`](../packages/retroplug/ui/screens/menu/menuDefs.ts) builds the start
  and instance menus over a `MenuContext` (stores + current values, rebuilt each render).
  Leaves call store methods directly, current values are baked into labels, and
  `browseThen` opens the OS dialog before applying. The instance menu offers Duplicate /
  Remove / Load ROM / Add / Link Group / a System submenu (SameBoy model/highpass/fastBoot
  cyclers, Save/Load State + SRAM, New SRAM, Reset) / Project / Settings.
- [`Menu.tsx`](../packages/retroplug/ui/screens/menu/Menu.tsx) is the keyboard-driven
  tree renderer. The focus highlight is React state driven **only** by explicit nav / click /
  rebuild — never by LVGL `onFocus` events — so there's nothing for stray focus events to
  corrupt. It flattens depth-first into open submenus and **re-keys the inner scrollable View
  on `visibleKey`** ([Menu.tsx:207](../packages/retroplug/ui/screens/menu/Menu.tsx#L207))
  — the same append workaround, forcing a full remount so every row mounts in JSX order.
  `useFocusGroup` claims the keypad; Enter → activate, Up/Down move the cursor, Left/Right →
  `onCycle(±1)`.

### LVGL primitives — [`ui/lvgl/`](../packages/retroplug/ui/lvgl)

- [`Box.tsx`](../packages/retroplug/ui/lvgl/Box.tsx) — a `<View>` with the LVGL
  default-theme chrome zeroed, caller style merged on top.
- [`FocusProvider.tsx`](../packages/retroplug/ui/lvgl/FocusProvider.tsx) — owns the
  app-wide empty **"sink"** keyboard group's lifecycle so LVGL doesn't route arrows/Enter into
  clickable tiles. It deliberately does *not* claim the keypad itself (parent effects run
  after children's, so it would clobber a just-mounted menu).
- [`useFocusGroup.ts`](../packages/retroplug/ui/lvgl/useFocusGroup.ts) — the reusable
  claim-the-keypad dance: a `useLayoutEffect` builds a fresh group from ordered refs, focuses a
  target, `setKeyboardGroup(group)`, and on cleanup restores the sink (**never null**) and
  destroys the group.
- [`useNativeEvent.ts`](../packages/retroplug/ui/lvgl/useNativeEvent.ts) — subscribes to
  the `lvgljs` `on`/`off` bus (`frame` / `key` / `resize` / …) with a stable listener + handler
  ref (no re-subscribe on inline closures).
- [`useWindowSize.ts`](../packages/retroplug/ui/lvgl/useWindowSize.ts) — the live window
  size reactive to `"resize"`; `requestWindowSize` / `isWindowSizeControlled` are the TS side of
  the `__rp_*` native window seam.

### Game input — [`ui/input/useGameInput.ts`](../packages/retroplug/ui/input/useGameInput.ts)

Resolves each `"key"` bus code to a Game Boy button via `buildKeyToButton(bindings.keyboard)`
([keyCodes.ts](../packages/retroplug/src/keyCodes.ts)) and fires
`backend.pressButton(focusedId, button, down)`. A `targetsRef` (dpf code → routed system id)
does OS auto-repeat suppression and release-to-original-target, so a held button never sticks
on the wrong (or a removed) instance. Releases are always processed — opening a menu mid-hold
can't strand a key down.

---

## Roles and the DSP kernel (summary — see doc 04)

Two concepts touched throughout this doc belong fully to **doc 04**:

- **Roles.** A `RoleInstance = { kind, config }` is what serializes on a `SystemEntry`.
  A **system-role** (e.g. `"sameboy"` — model/highpass/linkGroup/fastBoot) is the *only*
  role config that crosses to the live core (via `applyRoleConfig`). **Feature-roles**
  (LSDj-sync, mGB, midi-routing) are pure TS behaviours whose config **never** gets a C++
  struct. The control-plane `RoleRegistry` (built by `buildAppRegistry`) attaches feature
  roles to a system by ROM identity ([`romProviders.ts`](../packages/retroplug/src/romProviders.ts),
  the TS twin of the native `RomSniffer`).
- **The DSP role kernel.** [`src/dspKernel.ts`](../packages/retroplug/src/dspKernel.ts)
  is compiled to bytecode and run inside a **second, bare QuickJS context** on the audio
  thread — the distinct **DSP runtime**, never shared with the control-plane runtime.
  [`src/dspRuntime.ts`](../packages/retroplug/src/dspRuntime.ts) is the control-plane
  client that compiles/loads the kernel and pushes the system structure (as a JSON string) over
  the same `__rpcSend` channel; it is a **distinct capability from `Backend`** so the mock
  stays clean. [`kernelProjection.ts`](../packages/retroplug/src/kernelProjection.ts)
  turns a `SystemView[]` + `midiRouting` into the kernel structure `syncDspFromStore` pushes.

Doc 04 covers the byte-sink ABI, the per-system pipelines, the drift-exact PPQ clock in JS,
and how the kernel runs on the bare context.

---

## Not yet built / deferred

- **The About panel** is the one menu item still marked deferred in
  [`menuDefs.ts`](../packages/retroplug/ui/screens/menu/menuDefs.ts). The keyboard/gamepad
  bindings editor, Open Settings Folder, and the LSDj Mode submenu are all built.
- The `ui?` field on a `RoleType` (a role's own settings render descriptor) is present in the
  model but not yet consumed — it is the seam the deferred kit-patch UI will use.

The store graph is now **unified**: the plugin's control-plane bundle composes the one
`composeAppStores` graph and publishes it on `globalThis[Symbol.for("plugin")]`, and the editor's
`StoreProvider` reuses it — so the editor and the DAW get/setState path observe one model. The
broader remaining feature gaps live in **doc 07**; this list is only the TS-layer-local deferrals.

---

## Key files

| Concern | File |
|---|---|
| Native contract | [`src/backend.ts`](../packages/retroplug/src/backend.ts) · [`src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) |
| Systems | [`src/systemsStore.ts`](../packages/retroplug/src/systemsStore.ts) · [`src/systemsList.ts`](../packages/retroplug/src/systemsList.ts) |
| Project | [`src/projectStore.ts`](../packages/retroplug/src/projectStore.ts) · [`src/projectConfig.ts`](../packages/retroplug/src/projectConfig.ts) |
| Config stores | [`src/recentStore.ts`](../packages/retroplug/src/recentStore.ts) · [`src/userConfigStore.ts`](../packages/retroplug/src/userConfigStore.ts) · [`src/bindingsStore.ts`](../packages/retroplug/src/bindingsStore.ts) |
| Services | [`src/fileSelection.ts`](../packages/retroplug/src/fileSelection.ts) · [`src/fileWatcher.ts`](../packages/retroplug/src/fileWatcher.ts) · [`src/sramAutoSave.ts`](../packages/retroplug/src/sramAutoSave.ts) |
| Composition | [`src/pluginControlPlane.ts`](../packages/retroplug/src/pluginControlPlane.ts) · [`src/appStores.ts`](../packages/retroplug/src/appStores.ts) · [`src/appHost.ts`](../packages/retroplug/src/appHost.ts) |
| UI | [`ui/main.tsx`](../packages/retroplug/ui/main.tsx) · [`ui/App.tsx`](../packages/retroplug/ui/App.tsx) · [`ui/screens/grid/`](../packages/retroplug/ui/screens/grid) · [`ui/screens/menu/`](../packages/retroplug/ui/screens/menu) · [`ui/lvgl/`](../packages/retroplug/ui/lvgl) |
| DSP client (doc 04) | [`src/dspRuntime.ts`](../packages/retroplug/src/dspRuntime.ts) · [`src/kernelProjection.ts`](../packages/retroplug/src/kernelProjection.ts) |
