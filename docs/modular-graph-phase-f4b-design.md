# Phase F4b: the plain-TypeScript surfaces

**Date:** 2026-11-09 · **Branch:** `feature/modularize`
**Status:** designed (sections 1-4 approved 2026-11-09; the draft's questions answered) over `b96fb1bb9` (the F4a
tip plus the Windows and BSD-sed merge). Section 9 keeps only what is still open.

F4b is the second of three pieces of the package split's fourth phase ([package-split.md](package-split.md)), after F4a
([modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md)), which took the non-UI registrations out of
`packages/retroplug/src`. What F4a left in the app shell that names a cart or a console is the part of the shell that is
plain TypeScript and still hard-codes them: `ui/screens/menu/menuDefs.ts` (2,574 lines, 25 cart import lines), the N8
and Launchpad menu modules, the watchers in `ui/App.tsx`, `ui/host/render.ts`'s split table, the cart CLI tools in
`cli/sessions` (2,831 lines in seven files) and the example sessions that name a cart. The survey behind it is
`.superpowers/f-split/f4b-survey.md` (taken at `b96fb1bb9`; section 1 lists every figure the tree corrected), and the
user's decisions are the `## F4` section of `.superpowers/f-split/decisions.md`, from "F4b cut" on.

The phase is a boundary refactor, not a feature: no audio changes, no saved-file format changes, no graph document
changes, and no baseline is re-recorded (section 7). Its behaviour edges are recorded in section 6, each with what was
found; the one that is user-visible is the order of the CLI's command index.

## 1. Goal and scope

**Goal.** The package split's goal for the two TypeScript surfaces that are not React: a cart's or a console's menu
rows, CLI tools and example sessions are its package's, reached through registrations the package contributes, and the
shell holds no code that names one. After F4b `ui/` (less the six files F4c moves) and `cli/` import an explicit list of
contract specifiers and nothing else from a package, one host contract replaces the three ad hoc slices F4a introduced,
and a third-party tracker or device is a package with no edit to the shell's menus or tool table. Leaner products are
not the driver.

**Scope (one phase, the plain-TypeScript half of the UI list, by mechanism).** A `Host` contract in core (section 3); a
new `ui` package holding the menu model and the contribution types, the menu contributions of every cart and device
(section 4); the CLI tool contract, the seven cart tools and the example sessions moved into their packages, and the
host-module list (section 5); and the proof: rule 9 and the literal scanner over `cli/`, a ratchet over `ui/`, a bundle
check (section 7). It runs as one prototype in six stages (section 8).

**Not in F4b**, and where it goes:

- **F4c (React in packages).** A UI kit grown into the `ui` package (`@retroplug/ui/kit`: `Box`, `lv`, `PixelCanvas`,
  `useNativeEvent`, the frame-poll hooks, `DebugReadout`), a UI-only view registry (the `ViewDefinition` in
  `src/graph/defineExtension.ts` is consumed by nothing but its own test today), the LSDj HD player and the two overlays
  as views, how packages declare React (the `retroplug.host` list gains `react`, `react/jsx-runtime`, `lvgljs-ui`,
  `lvgljs`), and rule 9 and the scanner over `ui/`. F4b leaves the six files that import a cart (section 7.3) on a
  ratchet.
- **Per-product bundles (owed, unchanged from F4a).** Every product still lists the same 16 packages. F4b keeps the
  precondition true and adds a check that the non-UI bundles carry no UI code (section 7.4).
- **Native.** F4b changes no C++. Its CMake edits are the example-session target and the generator's byproduct list
  (section 5.4).

**What the tree has** (checked against `b96fb1bb9`; every figure below was counted or opened, and the survey's are
corrected here).

- **The three host slices.** `ConstructCaps { fileExists, readFile, pngDecode }`
  (`packages/core/ts/systemRoles.ts:107`), the type of `onConstruct`'s second argument (65) and of the asset catalogs'
  `applyRoleConfig` (`tracker/ts/assetCatalog.ts:71`), the three asset roles, `ritAssets.applyRitOverride`
  (`ritAssets.ts:68`), `lsdj/ts/app.ts:26`'s seed hook and `SongImportHost.caps`: 10 source files name it. `RenderHost {
  renderAudio, readCpu, readRam, writeRam, pressButton }` and `RenderPlaySink { renderChunk }`
  (`tracker/ts/trackerRender.ts`): the three carts' render hooks, `render.ts:611` (the one place a `RenderHost` is
  built) and three test files. `SongImportHost { caps, writeFileAtomic, readSram, loadSram, setRoleConfig, savPathOf }`
  (`tracker/ts/trackerSongImport.ts`), adapted by `src/songImportHost.ts` and built ad hoc at `ui/App.tsx:272`,
  `menuDefs.ts:1648` and `test/systems/lsdj-song-import.test.ts:15`.
- **The CLI's session.** `Session { backend, registry, recent, project, audio }` (`src/bootSession.ts:19`). The seven
  cart tools use `s.backend.{readFile, writeFile, writeFileAtomic, pngEncode, pngDecode}` and `s.audio.compileKitFormat`
  and nothing else of it (counted per file); `n8-load` / `n8-play` call `makeCall()` (`src/rpcClient.ts:30`),
  `n8-bridge` / `launchpad-probe` call `createMidiClient()` (`src/realBackend.ts:252`), `keepAlive` and `exitProcess`
  (`cli/session.ts`).
- **The cart parts of `menuDefs.ts`** (survey 950 to 1,000 lines; recounted by block): the SameBoy tables and rows
  (`sameboyConfig` 727, `systemChildren`' block 908 to 936, the Link Group row 2536 to 2541), the Mesen rows
  (`mesenConfig` 773, the NES block 938 to 962, the SMS/GG block 964 to 973), LSDj (assets 1281 to 1340, songs 1576 to
  1660, working song 2004 to 2032, `lsdjExtras` 2132 to 2153), risa (`RitAssetOps` 1361 to 1440, songs 1945 to 2043),
  smsggdj (2045 to 2104), BlipToaster (settings 1450 to 1574), the N8 menu (542 to 605, with `n8Devices.ts` 74 lines and
  `n8SdOps.ts` 171) and the Launchpad menu (478 to 540, with `launchpadDevices.ts`, 444 lines and 26 exports, not 22).
  `TRACKER_UI` is 2111 to 2116. The generic consumers stay: `assetMenu` 1142, `romPatchRows` 1233, `songMenu` 1774,
  `loadGuard` 1689.
- **The cart literals in `ui/`.** The F4a scanner's shapes (a platform id, a ROM extension, a core id, a node type, plus
  the embed, tracker and kit ids) find 38 hits in four `ui/` files: `menu/menuDefs.ts` 33, `host/render.ts` 3,
  `menu/n8SdOps.ts` 1, `grid/useRisaRuntime.ts` 1 (the last is F4c's). The survey's grep said about 55. In `cli/` the
  same scan finds 21 hits in 13 files (12 in 10 files with F4a's four sets alone).
- **The cart imports in `ui/` and `cli/`.** `ui/` has nine files importing a cart package (`menuDefs`, `n8Devices`,
  `launchpadDevices` and the six F4c files, section 7.3). `cli/` has 25 refused specifiers in 10 files against the rule
  9 allowlist as it stands (`analyze-lsdj-sync` 1, `bliptoaster-rom` 5, `launchpad-probe` 2, `lsdj-rom` 4, `n8-bridge`
  2, `n8-load` 4, `n8-play` 1, `risa-rom` 4, and `@retroplug/testing` in `sdk.ts` and `rom-test.ts`), counted with the
  checker's own scanner.
- **Rule 9 today** has 24 `SHELL_IMPORTS` entries (the survey said 25), walks `packages/retroplug/src` only, and its
  relative half refuses a reach out of `src` (`check-boundaries.mjs:681 to 736`). The scanner takes one `--root` and
  walks `<root>/packages/retroplug/src` (`run-scan.mjs:190`).
- **The example sessions** are bundled by the `retroplug-example-session` target (`packages/native/CMakeLists.txt:296`,
  nine outputs under `build/cli/`) and driven by three root scripts (`reaper:analyze-smoke`, `cli:export-mgb-channels`,
  `reaper:analyze-lsdj-sync`). Six import `../session` and `../wav`, which are shell-relative; `analyze-lsdj-sync` also
  reaches `../../src/graph/directGraph`.

## 2. The decisions

| # | Decision | Chosen | Alternatives not taken |
| --- | --- | --- | --- |
| 1 | The cut | F4 split by layer into F4a and F4b, then F4b by mechanism: F4b the plain-TypeScript surfaces, F4c React in packages (user, option B) | One phase for all of F4b (A); by surface, the menus then the CLI (C) |
| 2 | Per-product bundles | Still owed (F4a's decision 2); F4b adds the bundle check that keeps the precondition true | n/a |
| 3 | The host contract | One grouped `Host<G>` in `core/ts/host.ts`; a hook types itself by the groups it uses; the shell builds one host per surface; F4a's three slices are re-expressed as group selections and deleted (user, option A) | One flat host interface (B); per-hook slices co-located with their hooks (C) |
| 4 | The menu contributions | Both forms: declarative role-config rows and builder functions, each naming its placement, behind a manifest member `retroplug.ui` imported only by `ui/` (user, option A) | Builders only (B); declarative rows only (C) |
| 5 | Where the contract lives | A new `ui` package: the menu model and contribution types in F4b (plain TypeScript), the React kit in F4c; the Host stays in core (user, option A) | Everything in core (B); two packages, menus and a UI kit (C) |
| 6 | The example sessions | Each moves into the package it is about and imports the SDK by its bare `retroplug-cli` specifier (user, option B) | A top-level `examples/sessions` (A); the shell keeps them, exempt by a named list (C) |
| 7 | Host-provided modules | A `retroplug.host` manifest member checked by rule 3 against one shell-owned list, which also generates the typecheck `paths` (user, option A) | A global allowlist in the checker (B); a shim package that re-exports each host module (C) |
| 8 | `bliptoaster-rom` and risa | The shared NES DMC pieces move to `tracker`; risa's kit format keeps id `"risa"`; BlipToaster compiles by that string id through `host.kits.compile` and stays risa-free (user, option A) | BlipToaster depends on `risa` (B); the tool goes to `risa` (C) |
| 9 | `n8-sync`'s home | `n8-sync` moves to `risa`, which gains a dependency on `n8`; `n8` exports the shared bridge loop, `n8-bridge` stays in `n8`, and `n8` stays free of every cart (user, option A) | `n8` depends on `risa` (B); a generic host-sync translator hook in `n8` (C) |
| 10 | The draft's other questions | Answered by the controller, 2026-11-09: the `.rit` UI code to `tracker`; four SDK additions; `n8-load`'s listing byte-identical through `host.songs.list`; the scanner skips prose and reads `sdkSurfaceCheck`'s role from the registry; the menu tests and the native tool tests stay in the shell, `n8Play`'s pure cases move; `@retroplug/core/path`; row equality pinned by a recorded table; `Embed.shortLabel`; a service test first in stage 4; new workspace dependencies with the lockfile change quoted; an in-memory `build-ui.js` instrument | Each question's other option, section 9 |
| 11 | The design's four sections | Approved by the user ("yes" four times), 2026-11-09: the grouped host, the `ui` package and contributions, the CLI tools and examples, the proof and six stages | n/a |
| 12 | Prototype model | DeepSeek Flash implements each stage, Opus reviews each stage, as F4a ran | n/a |


## 3. The host contract

**The decision (approved): `core/ts/host.ts` defines `Host<G>`, an object of named groups; a hook's type names the
groups it uses; the shell builds one host per surface, so asking for a group a surface lacks is a type error.** It
replaces `ConstructCaps`, `RenderHost`, `SongImportHost` and `src/songImportHost.ts`. The groups below are the brief's,
**corrected against every call site that will take a host** (section 3.3 lists what the check changed): a member exists
only where code outside the shell calls it today, and the shell's own generic machinery keeps using the stores directly.

### 3.1 The type

```ts
// packages/core/ts/host.ts (exported as @retroplug/core/host)
export interface FilesHost {
  readFile(path: string): Uint8Array | null;
  writeFile(path: string, bytes: Uint8Array): boolean;
  writeFileAtomic(path: string, bytes: Uint8Array): boolean;
  fileExists(path: string): boolean;
}
export interface PngHost {
  encode(width: number, height: number, rgba: Uint8Array): Uint8Array | null;
  decode(bytes: Uint8Array): PngImageData | null;                  // PngImageData moves here from src/backend.ts
}
export interface KitsHost {
  /** The bank format `id` names among the product's registered kit formats (AudioDriver.compileKitFormat). */
  compile<S>(id: string, spec: S): Promise<KitBank>;               // KitBank = { bytes; errors } (dsp's KitCompileResult)
}
export interface SystemsHost {
  view(): readonly SystemInfo[];                                   // the live systems, in slot order
  setRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
  reload(id: number): unknown;                                     // SystemsStore.reloadSystem
  readSram(id: number): Uint8Array | null;
  loadSram(id: number, path: string): unknown;                     // the one rebuild an import ends in
  savPathOf(id: number): string | null;                            // the .sav a save writes (src/songImportHost.ts' rule)
  nodeIdOf(id: number): string;                                    // graph/buildGraph.ts systemNodeId
  drivenCart(): { id: number; nodeId: string } | null;             // buildGraph's drivenCartSystem + cartNodeId
  stageNodeMidi(nodeId: string, bytes: number[]): boolean;
  capability<O extends CapabilityOp>(nodeId: string, op: O, args: CapabilityArgs,
    onProgress?: (p: CapabilityProgress) => void): Promise<CapabilityResults[O]>;
}
export interface SongsHost {
  /** The lines `n8-load --ls` prints for a battery image, byte for byte as the tool printed them before (the header,
   *  one row per song, or the "unrecognized battery image" line), composed by the registered tracker integrations'
   *  `batteryListing` (section 5.2). */
  list(bytes: Uint8Array): string[];
}
export interface LiveSongsHost {                                   // UI only: the Songs menu's cart callbacks
  mutate(sys: SystemInfo, fn: (sav: Uint8Array) => Uint8Array | null): boolean;   // liveSav.mutateLiveSav
  beginImport(sys: SystemInfo, source: Uint8Array): void;           // MenuContext.beginSongImport (useSongImport)
}
export interface EmulatorHost {                                    // render only
  renderAudio(ms: number): void;
  readCpu(id: number, addr: number): number | null;
  readRam(id: number): Uint8Array | null;
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean;
  pressButton(id: number, button: number, down: boolean): void;
}
export interface DialogsHost { browse(opts: BrowseOpts): Promise<string | null>; }   // FileBrowserOpts moves here
export interface DevicesHost {
  rpc: Call;                                                       // src/rpcClient.ts makeCall()
  midi(): MidiClient;                                              // src/realBackend.ts createMidiClient()
}
export interface ProcessHost { args(): string[]; keepAlive(): void; exit(code: number): void; }
export interface ViewsHost { open(id: string, systemId: number): void; }
export interface ControllerHost {                                  // UI only: the Launchpad menu's project scope
  settings(): ControllerSettings;
  set(patch: { enabled?: boolean; app?: string; appConfig?: Record<string, unknown>; target?: string }): void;
  cartSync(): string | null;                                       // ProjectStore.controllerCartSync
  syncOverride(): { driving: boolean } | null;                     // graph/controller.ts controllerSyncOverride
  apps(): readonly ControllerAppContribution[];
}
export interface ConfigHost {                                      // UI only: config.json's controlSurface
  controlSurface(): ControlSurfaceSettings;
  setControlSurface(patch: Partial<ControlSurfaceSettings>): void;
}

export interface HostGroups {
  files: FilesHost; png: PngHost; kits: KitsHost; systems: SystemsHost; songs: SongsHost; liveSongs: LiveSongsHost;
  emulator: EmulatorHost; dialogs: DialogsHost; devices: DevicesHost; process: ProcessHost; views: ViewsHost;
  controller: ControllerHost; config: ConfigHost;
}
export type Host<G extends keyof HostGroups = keyof HostGroups> = { readonly [K in G]: HostGroups[K] };

/** The part of a system a hook may read: SystemView less `focused` and `settings`, so the shell's view satisfies it. */
export interface SystemInfo {
  readonly id: number; readonly platform: Platform; readonly core: Core; readonly romPath: string;
  readonly savPath: string; readonly savSuffix: number; readonly embedded: boolean; readonly battery: boolean;
  readonly missing: boolean; readonly roles: readonly RoleInstance[];
}
```

The types the groups name and the shell owns today move to core as types only (`CapabilityOp`, `CapabilityArgs`,
`CapabilityResults`, `CapabilityProgress` from `src/capability.ts:18 to 80`, which re-exports them; `FileBrowserOpts`
and `PngImageData` from `src/backend.ts`; `MidiClient` and `Call`; `ControllerSettings` and `ControlSurfaceSettings`). A
`KitBank` is `KitCompileResult`'s two fields (`dsp/ts/kitContract.ts:12`); core cannot import dsp, so the type is
declared in core and dsp's own test asserts it is assignable both ways.

### 3.2 The three surfaces, and the hooks retyped

Each surface's builder lives in the shell, beside what it adapts, and returns exactly the groups its surface has:

| Surface | Builder | Groups | Built |
| --- | --- | --- | --- |
| UI | `ui/host/uiHost.ts`: `createUiHost(stores, views)` | `files png systems songs liveSongs dialogs devices views controller config` | once, in `ui/App.tsx` (`useMemo` over the stores) |
| CLI | `cli/cliHost.ts`: `cliHost(session)` | `files png kits devices process songs` | per tool run, from the `Session` |
| Render | `src/render/renderHost.ts`: `renderHost(ctx)` | `emulator files` | per render (replacing `render.ts:611`) |

```ts
export type UiHost = Host<"files" | "png" | "systems" | "songs" | "liveSongs" | "dialogs" | "devices" | "views" | "controller" | "config">;
export type CliHost = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">;
export type RenderHostGroups = Host<"emulator" | "files">;
```

Hooks, by the groups they use (each verified against the hook's body):

- `RoleType.onConstruct(spec, host: Host<"files" | "png">, config)` and `AssetCatalog.applyRoleConfig(..., host:
  Host<"files" | "png">, ...)`: they call `readFile`, `fileExists` (`lsdj/ts/app.ts:27`) and `pngDecode`
  (`lsdjAssetsRole.ts:74`). The asset roles' helpers (`applyOverridesToRom`, `applyRitOverride`) take the same type.
- `TrackerSongImport.import(host: Host<"files" | "systems">, sys, paths)`: it calls `readFile` (`lsdjSongImport.ts:27,
  33`), `readSram`, `savPathOf`, `writeFileAtomic`, `setRoleConfig` and `loadSram` (60 to 63). The shell's
  `songImportHost` adapter goes; the UI host's `systems.savPathOf` is its one rule (`resolveSavPath`, "null when the
  system is gone").
- The render hooks (`seedSong` takes no host; `loadSong`, `pressPlay`, `playingProbe`) take `Host<"emulator">`. **The
  play sink stays an explicit argument**: `pressPlay(host, id, rom, sink)` keeps `RenderPlaySink { renderChunk }`, which
  is the request's own stream (built per request, after the host), not a capability of the emulator; the type stays in
  `tracker/trackerRender.ts`. `renderChunk` is not a member of `emulator`, so `warnings` and `loadSong`, which run
  before the sink exists, cannot call it.
- The cart menu callbacks (section 4) take `UiHost` narrowed to the groups each uses.

Deleted: `ConstructCaps`, `RenderHost`, `SongImportHost`, `SongImportTarget` (replaced by `SystemInfo`) and
`src/songImportHost.ts`. A hook that asks for a group its surface lacks fails `tsc`: `core/ts/test/host.test.ts` holds
that with a `@ts-expect-error` case (a `Host<"dialogs">` hook given the CLI host).

### 3.3 What verifying the brief's groups changed

Each line is a correction to the brief's list, with the call sites that decided it (full tables are the stage 1
report's):

1. **`songs` is two groups.** The brief's `songs` mixes `list(bytes)`, which the CLI host needs, with the live-SAV
   helpers (`mutateLiveSav`, the discard guards, `saveWorkingSong`, `requestSong` and `settleSong`), which a CLI host
   cannot offer. One group cannot be typed on both surfaces, so `songs` keeps `list` and `liveSongs` is the UI-only
   rest. It also shrinks: the cart callbacks (`menuDefs.ts:1591, 1623, 1958, 1987, 2007, 2053`) call only
   `mutateLiveSav`, `readSram` and the picker (`beginSongImport`, a `MenuContext` member). The discard guards,
   `saveWorkingSong`, `requestSong`, `settleSong`, `recordSong` and `workingSongImage` are used by the shell's own
   `songMenu`, `loadGuard` and `songGuardedAction`, which stay in the shell and keep the stores; they are not host
   members.
2. **`dialogs` is `browse` alone.** `prompt`, `confirm` and `notice` are menu data (`MenuItem.prompt: PromptSpec`,
   `noticeTree`), built by a row and run by the menu, not calls a callback makes. No caller exists, so none is declared.
3. **`kits` is not in the UI host.** No file under `ui/` calls `compileKitFormat` (the UI composes no audio driver);
   only the CLI tools do. A UI kit editor adds the group when it builds a driver over `project.graph`.
4. **`systems.readRam` is dropped, `emulator.readRam` stays.** No cart menu callback reads work RAM (the two reads in
   `menuDefs.ts`, 1782 and 2446, are the shell's generic `songMenu` and `trackerCartLabel`). F4c's HD player reads it
   and adds the member to `systems` then.
5. **Four members the brief missed.** `systems.nodeIdOf` and `systems.drivenCart` (BlipToaster's pinning stages MIDI at
   `systemNodeId(sys.id)` and the cart's node, `menuDefs.ts:1533`; `n8SdOps.ts:62` derives the cart from
   `drivenCartSystem` and the literal `"mesen"`, which must not enter the n8 package); the `controller` group (the
   Launchpad submenu's whole project scope: `project.setController`, `controllerCartSync`,
   `controllerSyncOverride(ctx.systems, ...)` and the app registry, `menuDefs.ts:478 to 540`); the `config` group
   (`launchpadDevices.ts` reads and writes `UserConfigStore.controlSurface` in 7 places, 155 to 402).
6. **`devices.midi` is a factory.** `n8-bridge` calls `createMidiClient()` twice per run; a getter that returned one
   client would change the lifecycle.

## 4. The `ui` package and the menu contributions

**The decision (approved): `packages/ui`, new, plain TypeScript in F4b and the React kit in F4c; both contribution
forms; a manifest member `retroplug.ui` read by the generator into `ui/generated/menus.ts`, imported only by `ui/`.**

### 4.1 The package

`packages/ui` is an ordinary package: `private`, `type: "module"`, `exports` (no wildcard) `./menu` and
`./contribution`, a `tsconfig.json` extending `tsconfig.base.json`, a `typecheck` script, `ts/test/`. It depends on
`@retroplug/core` (the Host, `SystemInfo`) and `@retroplug/tracker` (the catalogs the specs name), and contributes no
manifest member itself, so it is not in `retroplug.products.desktop` (a package with no `app`, `consoles` or `protocol`
is left out of the ordering, as `tracker` is). The shell declares it a dependency (spec/06's "Adding a package", step
3).

`@retroplug/ui/menu` is `menuTree.ts` (66 lines: `MenuItem`, the seven kinds, `PromptSpec`, `MenuAction`, `MenuTree`)
and `menuItems.ts` (71: `action`, `button`, `infoRow`, `submenu`, `separator`, `actionCycler`, `cycler`, `cycleInt`,
`noticeTree`), moved unchanged: 26 files import `menuTree` and 6 `menuItems`, each repointed, the shell's `Menu.tsx`
among them. Neither imports React or the shell.

### 4.2 The contribution types

```ts
// @retroplug/ui/contribution
export type ConfigChoice = { readonly value: string | number; readonly label: string };
export type ConfigRow = { readonly key: string; readonly label: string } & (
  | { readonly choices: readonly ConfigChoice[] }                          // exact match; unknown value reads as index 0
  | { readonly toggle: true }                                              // Off/On
  | { readonly range: { readonly steps: readonly number[]; readonly labels: readonly string[] } }  // nearest step
);
/** A row over a system's role config: what the shell renders as a cycler and edits with systems.setRoleConfig. */
export type RoleRow = ConfigRow & {
  readonly id: string;                                  // the row id, byte-identical to today's ("sys-model")
  readonly role: string;                                // the role kind whose config holds `key`
  readonly platforms?: readonly string[];               // shown only on these platforms (the NES block)
  readonly when?: { readonly key: string; readonly in?: readonly string[]; readonly notIn?: readonly string[] };
  readonly separatorBefore?: string;                    // a separator id emitted before the first visible row carrying it
};

export type Placement =
  | "instance.tracker" | "instance.devices" | "instance.footer" | "system.settings" | "start" | "settings.midi";
export interface MenuBuildContext {                      // what a builder reads that is not the host
  readonly systems: readonly SystemInfo[];
  assetRom(romPath: string): Uint8Array | null;           // the shell's per-path base-ROM cache (menuDefs.ts:1054)
}
export interface MenuBuilder {
  readonly placement: Placement;
  build(host: UiHost, sys: SystemInfo | null, ctx: MenuBuildContext): MenuItem[];   // [] hides the entry
}

export interface SongMenuSpec { /* menuDefs.ts:1659's shape; callbacks take (host, sys, ...) not (ctx, sys, ...) */ }
export interface AssetMenuSpec { /* menuDefs.ts:1038's shape, the same change */ }
export interface TrackerUi {
  readonly id: string;                                   // the integration id (TRACKER_UI's key)
  readonly song?: SongMenuSpec;
  readonly asset: AssetMenuSpec;
  readonly extras?: { readonly rows?: readonly RoleRow[]; build?(host: UiHost, sys: SystemInfo, ctx: MenuBuildContext): MenuItem[] };
}
export interface UiService {
  readonly id: string;
  start?(host: UiHost): void;                            // once, when the UI is up
  subscribe?(listener: () => void): () => void;          // a native-host state change that must repaint (subscribeN8)
  poll?: { readonly frames: number; run(host: UiHost): boolean };   // every `frames` render frames; true = repaint
}
export interface UiContribution {
  readonly rows?: readonly RoleRow[];                    // placement "system.settings" (the only one rows take)
  readonly menus?: readonly MenuBuilder[];
  readonly trackers?: readonly TrackerUi[];
  readonly services?: readonly UiService[];
}
export function defineUi(c: UiContribution): UiContribution { return c; }
```

The controller-app field shape (`ControllerAppField`, `launchpad/ts/registry.ts:19`: `key`, `label`, `choices?`,
`toggle?`) is the same shape without `id` and `role`: it becomes `ConfigRow` (an alias kept in `registry.ts`, so the
portable core and the shell's `controllerAppRows` keep their import), and gains `range`. `controllerAppRows`
(`menuDefs.ts:463`) and the new role-row renderer share one cycler builder; the controller rows keep the ids
`lp-app-<key>` the shell derives.

Reading a row's value reproduces today's two fallbacks exactly: a stored value of the wrong type (`typeof` differs from
the schema default's) reads as the default; a string or number the choices do not list reads as index 0 (`Math.max(0,
indexOf(...))`); a `range` reads the nearest step (`nearestIndex`, `menuDefs.ts:762`). The default is the role schema's
(`schema.parse({})[key]`), which equals the menu's own fallback in every row below (checked against
`sameboy/ts/app.ts:26 to 41` and `mesen/ts/app.ts:26 to 92`).

### 4.3 Which rows are declarative (verified, row by row)

| Row (id) | Today | Form | Notes |
| --- | --- | --- | --- |
| Model `sys-model`, Highpass `sys-highpass` | `X_VALUES[n]` over `X_NAMES`, default `cgbC` / `accurate` | `choices` | index of `MODEL_VALUES` equals the names' (14 and 3) |
| Fast Boot `sys-fastboot` | `c.fastBoot !== false` | `toggle` | default true |
| DMG Palette `sys-dmg-palette` | shown when the model is in `DMG_MODELS` (`dmgB, mgb, sgb, sgbPal, sgb2`) | `choices`, `when: { key: "model", in: [...] }` | `auto` is not DMG, so it gets the CGB rows |
| Color Correction `sys-color-correction`, Light Temp `sys-light-temp` | the complement | `choices` / `range` (11 steps), `when: { key: "model", notIn: [...] }` | the one separator `sys-sep-display` precedes whichever group shows: `separatorBefore` on the first row of each |
| Region `sys-nes-region`, Remove Sprite Limit `sys-nes-spritelimit` | NES only | `choices` (5) / `toggle` (default false) | `platforms: ["nes"]` |
| APU Latency `sys-nes-apu-latency`, Expansion Volume `sys-nes-exp-volume` | nearest of 5 / 8 steps | `range` | clamped by the role schema, not the row |
| Drive Real Cart `sys-nes-drive-cart` | `drivesRealCartOf(c)`, the field the NES descriptor names as `realCartOptOut` | `toggle`, key `driveRealCart`, default true | the row's key and the descriptor's `realCartOptOut` are two spellings in mesen; a test pins them equal |
| FM Audio `sys-sms-fm` | SMS and GG | `toggle` (default true) | `platforms: ["sms", "gg"]` |
| LSDj Mode `lsdj-mode`, Tempo Divisor `lsdj-divisor`, Auto Start `lsdj-autostart` | role `lsdj-sync` | `choices` (9) / `choices` (numeric 1, 2, 4, 8) / `toggle` (default false) | the HD Player action and `lsdj-assets-sep` follow as a builder |
| Link Group `inst-link` | only with several systems and `core === "sameboy"` | **builder**, placement `instance.footer` | the peer count is not row data |
| BlipToaster settings `bliptoaster-set-*` | read from the ROM's baked block, pinned with SysEx at the system's node and the cart's | **builder** | `menuDefs.ts:1537`; not role config |
| The N8 and Launchpad submenus, the Settings > MIDI scan rows | state machines over the native host | **builders** | section 4.5 |

Two facts make the table safe to rely on: every row's apply is a one-key patch, `systems.setRoleConfig(sys.id, role, {
key: value })` (21 call sites, each read), and gating by "the system has the role" (`sys.core === "sameboy"` today) is
the same set as "the system has the `sameboy` role" by the registry's rule (the core role attaches to every system of
that core); that equality is pinned by the menu equality test over the case matrix (section 7.5), not assumed.

### 4.4 Placements, and the trackers

The shell composes the menus from the generated list in product order, so a package's entry lands where its position in
`retroplug.products.desktop` puts it. Today's order is preserved by it: `sameboy` (4th) before `mesen` (11th) for the
system settings, `launchpad` (2nd) before `n8` (10th) for the instance's device entries.

- `system.settings`: `systemChildren`' knob block (`menuDefs.ts:908 to 973`) becomes the rows of every contribution
  whose role the system has, then the shell's own rows. The first four contributions' rows are today's code.
- `instance.tracker`: `TRACKER_UI` (`menuDefs.ts:2111`) goes; `trackerChildren` (2120) finds the `TrackerUi` whose `id`
  is the integration's, builds `extras` (rows, then the builder), the shared Songs menu, the asset menus and the bake
  rows, as now. `instance.devices` entries follow it inside the same fence (`inst-sep-tracker` appears when anything in
  the block is non-empty, as `tracker || n8Here || lpHere` decides today), Launchpad before N8.
- `start`: the N8 block with its own separator (`start-sep-n8`, `menuDefs.ts:2566`); `settings.midi`: the
  control-surface scan rows (`midiSettingsChildren`, 328); `instance.footer`: after Remove Instance, before Exit (the
  Link Group, 2536). The footer placement is the one the brief's list lacks.
- **The cart callbacks leave `menuDefs.ts` and become package code**, taking `(host, sys, ...)`: LSDj's export, replace
  (with `importLsdprj`), add, working-song and asset actions to `lsdj`; risa's and smsggdj's to their packages;
  BlipToaster's settings rows and its selectable-slot spec (`menuDefs.ts:1476`) to `bliptoaster`. The two callers of
  `mutateSavBytes` become `host.liveSongs.mutate`; the file pickers become `host.dialogs.browse(...).then(...)`, the one
  `browseThen` helper moving to `@retroplug/ui/menu` with the sanitising helpers (`sanitizeName`, `tryOp`, the
  `pathUtil` and `savPaths` pure functions the callbacks use move to core as `@retroplug/core/path`, decision 10).
- **The shared `.rit` asset code** (`RitAssetOps`, `exportRitAsset`, `replaceRitAsset`, `menuDefs.ts:1361 to 1440`) is
  used by risa and BlipToaster. **The brief put it in `risa` "because BlipToaster already depends on it"; it does not**
  (its manifest declares `core`, `tracker`, `zod`, and `risa/ts/rom/index.ts:6` says outright that BlipToaster "must not
  depend on risa"). The code needs no menu type (it takes a host and a role name), so it moves to
  `@retroplug/tracker/ritAssetOps` beside `ritAssets.ts`, which both already import (decision 10).
- `validSplits` (`ui/host/render.ts:112`) becomes `["mix", ...(d.splits?.channels ? ["channels"] : []),
  ...(d.splits?.pins ? ["pins"] : [])]` over the console descriptor (`sameboy` declares `channels`, the NES `channels`
  and `pins`, the GBA and SMS descriptors none), the same sets as `core === "sameboy" || platform === "nes"` for today's
  five platforms. **`validAudioRoutings` (`menuDefs.ts:623`) holds the same `platform === "nes"` literal for the pin
  routings, which the brief did not list; it reads `splits.pins` too.**
- The instance title's `"mGB"` (`menuDefs.ts:2468`) reads the embed declaration. `Embed.label` is `"mGB (GB MIDI
  Synth)"` (`mgb/ts/app.ts:24`; the start menu's row is `Load mGB (GB MIDI Synth)`), so the title needs a new optional
  `Embed.shortLabel` (`core/ts/definePackage.ts:28`, `"mGB"` in mgb's declaration, the generator's embed pairing check
  unchanged); with none it falls back to `label`.

### 4.5 Device menus and services

`n8` and `launchpad` contribute `ts/ui/` modules: `n8Devices.ts` (74), `n8SdOps.ts` (171) and `launchpadDevices.ts`
(444, 26 exports) move unchanged but for their reads of the shell (`makeCall` becomes `host.devices.rpc`,
`UserConfigStore` becomes `host.config`, `drivenCartSystem` becomes `host.systems.drivenCart()`, `SystemView` becomes
`SystemInfo`), with the menu builders (`n8MenuChildren`, `launchpadMenuChildren`, `launchpadStatus`, `cartSyncLabel` and
the scan label) beside them. The module-global state they keep (the card job, the probed link, the scan) is per bundle,
as today.

The three watchers become `services`: `useN8SdWatch` (a versioned poll of `getN8CartJob` every 6 frames),
`useLaunchpadScanWatch` (a versioned poll of `pollLaunchpadScan` with a finishing-edge apply, and `pollControllerRole`,
every 3 frames) and `restoreLaunchpad` (`App.tsx:75`, once the UI is up). The version-and-edge latches
(`useVersionedPoll`'s `seenVersion` and `settled`) move into each service's own closure and `poll.run` returns whether
the version moved; `ui/app/useUiServices.ts` is the one generic hook: it runs `start` in an effect, subscribes every
`subscribe` through `useRerenderOn` (the list is a module constant, so the call is stable) and counts frames per service
on one `useNativeEvent("frame")`. `App.tsx` loses `useN8SdWatch`, `useLaunchpadScanWatch`, `restoreLaunchpad` and the
two `subscribe*` imports (lines 26 to 28, 69 to 75, 92).

**Opening the HD player** is `host.views.open("lsdj-hd", systemId)`: LSDj's `build` for `instance.tracker` calls it, and
the shell's `views.open` maps the id to today's screen by `setHdSystemId` (`App.tsx:83, 298`); the mapping and
`MenuContext.openLsdjHd` (`menuDefs.ts:149`) are F4c's to remove. `App.tsx:248`'s `targetSongExtensions` already reads
the integration (F4a) and is unchanged.

## 5. The CLI tools, the example sessions and the host modules

### 5.1 The tool contract

```ts
// packages/core/ts/cliTool.ts (exported as @retroplug/core/cliTool)
export interface CliTool<H = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">> {
  readonly name: string;                  // the subcommand
  readonly summary: string;               // the index line
  readonly help: string;                  // the `--help` text, byte-identical to today's per tool
  readonly longRunning?: boolean;         // calls host.process.keepAlive() and returns (a live bridge)
  run(host: H, args: string[]): void | Promise<void>;
}
export function defineCliTools(tools: readonly CliTool[]): readonly CliTool[] { return tools; }
```

A tool's `run` takes the host, not a `Session`: `host.files.*`, `host.png.*`, `host.kits.compile("lsdj", spec)`,
`host.devices.rpc` (`createN8Client(host.devices.rpc)`), `host.devices.midi()`, `host.process.{keepAlive, exit}`. The
shell's five tools (`test`, `run`, `render`, `analyze-capture`, `grab-frame`) use the whole session and keep their
`Session` signature: `cli/tools.ts` keeps exporting `CliTool` as `CoreCliTool<Session>`, so the five and
`test/cli/cli.test.ts` keep their type import, and one adapter, `fromContribution(tool)`, binds a generated tool to
`cliHost(session)` for the dispatcher. `cli/cli.ts` is unchanged (it reads `tools`).

### 5.2 The manifest member and the generated table

A package with tools names a module in `retroplug.cli` that default-exports `defineCliTools([...])`; the generator
writes `packages/retroplug/cli/generated/tools.ts` (gitignored, like `src/generated/`), concatenating them in product
order and refusing a duplicate name between packages; `cli/tools.ts` appends them after the shell's five and refuses a
name that clashes with one of those at load. `cli/` imports the generated module and nothing else names a tool.

| Tool | From | To | Imports it takes with it | Package dependency it adds |
| --- | --- | --- | --- | --- |
| `lsdj-rom` (469) | `cli/sessions` | `lsdj` | `lsdj/rom`, `lsdj/kitFormat`, `dsp` | none (has `dsp`) |
| `risa-rom` (485) | | `risa` | `risa/rom`, `risa/kitFormat`, `dsp` | none |
| `bliptoaster-rom` (652) | | `bliptoaster` | `bliptoaster/rom`, the NES codecs and the DMC kit spec types from `tracker/nes/*` (below), `dsp`, tracker's `.rit` helpers | `dsp` |
| `n8-load` (508) | | `n8` | `n8`, `tracker/nes/chrImage`, and `host.songs.list` in place of `isRisaSav` / `isLsdjSav` | `tracker` |
| `n8-play` (224), `n8-bridge` | | `n8` | `n8`, and it exports `runBridge` (`n8-bridge.ts:49`: the port, the MIDI client, the poll loop) for `n8-sync` | none |
| `n8-sync` (in `n8-bridge.ts`) | | **`risa`** (decision 9) | `risa/risaSyncTranslator`, the bridge loop from `n8` | `n8` (risa follows `n8` in the product list: n8 is 10th, risa 13th) |
| `launchpad-probe` (345) | | `launchpad` | `launchpad`, `launchpad/client` | none |

**`bliptoaster-rom` and risa (decision 8).** Every codec the tool imports through `risa/rom` already lives in `tracker`
(`risa/ts/rom/index.ts` re-exports them): `bankToModel`, `isBankPopulated`, `assembleKitBank`, `dpcmDecode`,
`AssembleSlot` (`tracker/nes/kitBank.ts`), `serializeRit`, `parseRit`, `decodeThemeFromRom`, `normalizeTheme`,
`encodeThemeRecord`, `encodeThemeName` (`tracker/nes/theme.ts`) and `PAL_DPCM_RATES_HZ`, `KIT_BANK_SIZE`,
`KIT_SLOT_COUNT`, `CHR_BANK_SIZE` (`tracker/nes/constants.ts`), so the tool imports them from `@retroplug/tracker/nes/*`
and none moves. What is risa-only is the DMC kit spec: `RisaDmcSampleSpec` and `RisaKitCompileSpec`
(`risa/ts/kitFormat.ts:21, 29`, extending dsp's `KitSampleSpec`). They move to `@retroplug/tracker/nes/kitSpec` as
`NesDmcSampleSpec` and `NesDmcKitSpec` (the risa name is wrong for a format BlipToaster shares; `kitFormat.ts`'s own
header says so), `risa/kitFormat` imports them and keeps the old names as aliases, and `tracker` gains a type-only
dependency on `dsp` (dsp depends on core alone, so no cycle). The format keeps its id `"risa"`: the tool calls
`host.kits.compile<NesDmcKitSpec>("risa", spec)` (the string, as `compileKitFormat("risa", ...)` does today), so
`bliptoaster` stays free of `risa`; its manifest gains `dsp` (`NO_SLOT_REASON`, `KitEffect`, `dsp/wav`), `ui` and
nothing else. Its tests keep calling the tool against a product that registers both packages.

**`n8-sync` and the bridge (decision 9).** `runBridge(args, clientName, transform)` (`n8-bridge.ts:49`) is the loop both
tools share; `n8` exports it from a new `./bridge` entry and `n8-bridge` stays in `n8` using it. `n8-sync` moves to
`risa` (`risa/ts/cli/n8Sync.ts`), builds a `RisaSyncTranslator` and calls `runBridge`; `risa` declares `@retroplug/n8`
and is listed after it (it is). `n8` imports no cart.

**`n8-load`'s listing (decision 10) stays byte-identical.** `host.songs.list(bytes)` returns the lines to print, so no
column is lost. Each tracker integration gains an optional `batteryListing: { name: string; noun: string; rank: number;
describe(sav): string[] | null }` (`tracker/trackerIntegration.ts`): `describe` answers `null` for a save it does not
read and otherwise one string per song, exactly the old row text (risa: `  [ 0] NAME  v1  12B`, with `version` and
`length` from its codec; LSDj: `  [ 0] NAME`). The shell composes the rest: the header `<name> battery - <n> <noun>(s):`
(`risa` / `song`, `LSDj` / `project`) and, when no integration reads the image, `unrecognized battery image (<n> bytes)
- not a <names> save` with the names joined by `" or "` in `rank` order (risa 0, LSDj 1, which is the old test order),
so the line stays `not a risa or LSDj save`. The integrations are tried in rank order, as `decodeAndPrint` tried risa
first. A test records the old output for a risa save, an LSDj save and a blank image before the move.

`encodeWav` and the other pure WAV functions (`wavHeader`, `pcm16`, `interleaveStereoStreams`, `deinterleaveStereo`,
`decodeWav`, `src/render/wav.ts:17 to 160`) move to `@retroplug/dsp/wav`; `createWavWriter` (the backend-bound half)
stays, importing them, and `cli/wav.ts` stays a re-export shim. Each tool's help text is byte-identical: a test records
every tool's `help` and `summary` at `b96fb1bb9` before the first move and holds the generated table to them. The tests:
`test-native/{lsdj-rom, risa-rom-cli, bliptoaster-rom-cli, n8-session}.test.ts` drive a tool against the real backend
and the real core, so they stay in `test-native/` (a package's `ts/test` runs on the pure-TypeScript mock tier and
cannot import the shell's `createRealBackend`); they import the tool from its package and call
`tool.run(cliHost(session), args)`. `test/cli/n8Play.test.ts` (62 lines, pure functions) moves to
`n8/ts/test/n8Play.test.ts`.

### 5.3 The example sessions

`mgb-smoke`, `analyze-mgb`, `export-mgb-channels` move to `packages/mgb/examples/`, `export-nes-channels` and
`export-nes-mono` to `packages/mesen/examples/`, `analyze-lsdj-sync` to `packages/lsdj/examples/`; `render-rom`,
`render-song` and `rom-test` (no cart) stay in `cli/sessions`. A package lists them in `retroplug.examples` (`[{ "name":
"mgb-smoke", "entry": "examples/mgb-smoke.ts" }]`) and declares `retroplug.host: ["retroplug-cli"]`; each imports the
SDK by the bare specifier. `retroplug-example-session` builds each to the same `build/cli/<name>.js` (so the three root
scripts, the Reaper analyze steps and `docs/lsdj.md:414` keep working), through a `retroplug_examples()` function in
`cmake/packages.cmake` that reads the manifests of `RP_DESKTOP_PACKAGES`; `tools/build-session.js` marks `retroplug-cli`
external (the binary serves it from its loaded-module table, as it does for a test).

**The SDK surface the examples need is larger than the brief's one addition (decision 10 accepts all four).** The six
examples import `runSession`, `hostArgs`, `encodeWav`, `interleaveStereoStreams`, `deinterleaveStereo` and `DirectGraph`
from shell-relative modules. `cli/sdk.ts` exports `hostArgs` and `encodeWav` and **does not export `runSession`** (its
header says so on purpose: a test calling it would `tjs.exit(0)` and report a false pass), `interleaveStereoStreams`,
`deinterleaveStereo` or `DirectGraph`. So F4b's SDK additions are `DirectGraph` (the class, which `analyze-lsdj-sync`
uses beside `s.project.graph`), the two stereo helpers (pure, from `dsp/wav`) and `runSession`, documented as for a
session file run by path and never from a test; each lands in `cli/sdk.ts`, `cli/sdk-types.d.ts` and
`cli/sdkSurfaceCheck.ts` (the drift check).

### 5.4 The host-module list

One shell-owned file, `packages/retroplug/host-modules.json`, lists each host module with the surface that provides it
and why: `[{ "module": "retroplug-cli", "surface": "cli", "provider": "the retroplug-cli binary's loaded-module table",
"types": "cli/sdk-types.d.ts" }]`; F4c adds `react`, `react/jsx-runtime`, `lvgljs-ui`, `lvgljs` with surface `ui`. Rule
3 (`check-boundaries.mjs:596`, the third-party branch) accepts a bare specifier on the list when the importing package's
manifest declares it in `retroplug.host`, refuses one it does not declare, and refuses a declared name the list does not
have; a declaration nothing imports warns, as an unused dependency does. The rule's import walk grows from `ts/` to
`ts/` plus `examples/` (today it reads `pkg.ts` only). The generator writes
`packages/retroplug/tsconfig.host-modules.json` (the `paths` entries; gitignored with the generated modules) and the
shell's `tsconfig.json` and every package with `examples/` extend it, replacing the hand-written `"retroplug-cli"` entry
(`tsconfig.json:12`) and adding `../*/examples` to the shell's `include`. `bundles.cmake` gains `ui/generated/menus.ts`
and `cli/generated/tools.ts` in `retroplug-ts-packages`'s byproducts; `retroplug-cli-bundle`,
`retroplug-example-session` and the UI bundle already depend on that target.

## 6. Behaviour edges

None changes audio, a saved file or a graph document. Each is listed with what was found.

- **E1. The command index changes order.** Today `topLevelHelp` prints `test run render lsdj-rom risa-rom
  bliptoaster-rom n8-load n8-bridge n8-sync n8-play analyze-capture grab-frame launchpad-probe`. After: the shell's
  five, then the packages' in product order (`launchpad-probe, lsdj-rom, n8-load, n8-bridge, n8-play, risa-rom, n8-sync,
  bliptoaster-rom`). No test pins the order: `test/cli/cli.test.ts` asserts presence only, and the brief's "test listing
  tool names updated" has nothing to update; `README.md:276`'s table is the one place the order is written. `<cmd>
  --help` and every tool's output are unchanged.
- **E2. `n8-load`'s battery listing is unchanged** (decision 10): `host.songs.list` returns the printed lines, composed
  by the integrations' `batteryListing` (section 5.2), including the `not a risa or LSDj save` line by `rank`.
- **E3. The instance title reads the embed's short label** (section 4.4): identical output, one new optional field.
- **E4. The examples no longer bundle the control plane.** Each imported `../session` and so carried the whole boot
  path; with the SDK external each is a few lines against the binary's own SDK, so `build/cli/*.js` differ in bytes (not
  in behaviour). They run against the SDK of the binary that runs them, which is the same build. Whether the binary
  registers the SDK module for a session run by path, as it does for `retroplug-cli test` and `run`, is **unverified**
  (AGENTS.md says the embedded module is read "before a session evaluates"); stage 5's first step is to run `mgb-smoke`
  that way.
- **E5. `n8-load` output paths and two cart strings move with the tools.** `"resources/roms/bliptoaster.nes"`
  (`n8-load.ts`) and the help examples keep their text.
- **E6. `validSplits` and `validAudioRoutings` are descriptor-driven**: equal for the five platforms, different only for
  a console that declares a split the old tables did not name.
- **E7. The watchers run as services at the same cadences** (6 and 3 frames), with the same latches; the observable
  change is the removal of three hooks from `App`. **No `test-ui` file drives the N8 card or the scan watchers** (only
  `midi-settings.test.ts` touches the MIDI rows, and only their device pickers), so stage 4 adds the service test of 7.5
  item 7 first.
- **E8. A hand-edited role config reads as before** (the fallback rules of section 4.2), row by row.
- **E9. The menu's device entries now sit by product order**, not by two literal lines; the order is the same today, and
  a future reorder of `retroplug.products.desktop` would move them (and the `[packages]` golden would show it).
- **E10. `UiHost`'s `kits` group does not exist**, so the UI cannot compile a kit; nothing does today.

## 7. Proof

### 7.1 Rule 9 over `cli/`

`check-boundaries.mjs` rule 9 grows from `src` to `src` and `cli`: every `@retroplug/*` specifier in either must be on
`SHELL_IMPORTS`, a relative import must stay in the file's own root or reach `src` from `cli` (a file in `src` reaches
only `src`, a file in `cli` reaches `cli` or `src`; today's rule is "outside `src`", and a plain union would let `src`
import `cli`), `cli/generated/` is unjudged (like `src/generated/`), and a stale entry still fails. The list gains
`@retroplug/core/host`, `@retroplug/core/cliTool`, `@retroplug/core/capability`, `@retroplug/dsp/wav` and
**`@retroplug/testing`** (the SDK re-exports `test`, `expect`, `skip` and `rom-test.ts` imports them; the brief's list
omits it), derived from the tree at stage 6 (about 29 entries). **RED first, on `b96fb1bb9`'s `cli/`:** 25 refusals in
10 files against today's 24-entry list (23 in 8 once `@retroplug/testing` is added); the rule is committed before any
tool moves, and the fixtures in `test/boundaries` get a `shell-cli-forbidden`, `shell-cli-allowed` and
`shell-cli-relative` case (a `src` file reaching `cli/` is refused).

### 7.2 The literal scanner over `cli/`

`run-scan.mjs` takes `--dirs src,cli` (default `src`, so the existing fixtures hold) and, new, `--carts`: the embed ids,
tracker ids and kit format ids from `src/generated/packages.ts` (`EMBEDS`, `TRACKER_INTEGRATIONS`, `KIT_FORMATS`),
without which the tools' `"lsdj"` and `"risa"` kit ids and the examples' `"mgb"` are invisible (the F4a sets are
platforms, extensions, cores and node types). `cli/generated/` and `cli/sdk-types.d.ts` are skipped. Over `src` the new
set adds two hits, `"mgb"` and `"bliptoaster"` in `projectConfig.ts`'s frozen v2 migration table, which join the
existing exemption by file and value. **RED first:** 21 hits in 13 files on `b96fb1bb9`'s `cli/`. After the moves three
would remain, none a tool or an example, and decision 10 clears them: the extension-suffix rule skips a string that
contains whitespace (the help prose of `run` and `test`, each ending in `bliptoaster.nes`; a path has none), and
`cli/sdkSurfaceCheck.ts:117` reads the core-role kind from the generated `CONSOLES` list instead of spelling `"mesen"`.
The scanner's own fixture gains a prose case and a path case.

### 7.3 The `ui/` ratchet

Until F4c a test, `test/boundaries/ui-cart-imports.test.ts`, walks `ui/` with the checker's scanner and fails for any
file not on its list that imports a cart or console package (the nine today minus what F4b moves, leaving **five**:
`LsdjOverlay.tsx`, `RisaOverlay.tsx`, `useLsdjRuntime.ts`, `useRisaRuntime.ts`, `useLsdjHdSession.ts`; the brief and
survey say six, adding `LsdjHdScreen.tsx`, which imports react, `lvgljs-ui` and `useLsdjHdSession` and no cart; the word
"@retroplug/lsdj" on its line 3 is a comment). A listed file that stops importing one fails too, so F4c deletes the list
by emptying it. RED first: on `b96fb1bb9`, nine files fail against the five-file list.

### 7.4 The bundle check

`test/script/bundle-ui-split.test.ts`, on the bundle-protocol test's instrument (`build-session.js`'s `bundleInputs`,
esbuild's metafile in a node child): the control plane (`src/pluginControlPlane.ts`), the render worker
(`src/render/worker.ts`, iife), the SDK (`cli/sdk.ts`), the CLI dispatcher (`cli/cli.ts`, **which the brief omits and
which is the bundle that carries `cli/generated`**) and the planner (`src/graph/protocolNodes.ts`) contain no
`@retroplug/ui` module and no `ui/generated` file; the UI bundle (`ui/main.tsx`) contains no `cli/generated`. The UI
bundle is built by `tools/build-ui.js`, which writes a depfile from its metafile but exports nothing; stage 6 gives it a
`bundleInputs` beside `build-session.js`'s (the same nodePaths and aliases, written to memory).
`bundle-protocol.test.ts` and `bundle-no-zod.test.ts` are unchanged.

### 7.5 The oracles held, and the new tests

Nothing is re-recorded and no `RP_*_UPDATE` is set. Held on the fixtures-present run with `RP_FAIL_ON_SKIP=1` and on the
CI picture: strict parity, the kit and render baselines, `render-chunks`, `render-cli`, the `buildGraph` and role
goldens and the protocol digests, the `[packages]` golden (a package manifest member adds no node type), the device and
box image hashes and pack bytes (no native source changes), `pnpm test`, `test:native`, `test:ui` (every file passes
unchanged, among them `lsdj-hd`, `lsdj-overlay`, `risa-overlay`, `bliptoaster-settings`, `audio-routing-nes/gb`,
`settings-cyclers`, `render-menu`), `test:plugin`, `check:boundaries`, `check:docs`, both consumer suites (BlipToaster
57/57, NESVJ 19/19) against the rebuilt CLI, `reaper:params` and `reaper:params-clap`, the Reaper suite (`pnpm
reaper:all`, with `RETROPLUG_RESOURCES_DIR` set, as F4a's acceptance found) and both SDL legs (`sdl:smoke`;
`sdl:controller` run alone, since it takes the ALSA fakes). The menu tests (`test/menu/*`, 14 files, 5,388 lines) all
build `buildInstanceMenu` over `MockBackend` and `composeAppStores`, so **they stay in the shell under their slugs**,
with imports repointed (`menuTree` to `@retroplug/ui/menu`; `n8SdOps` and `n8Devices` to `@retroplug/n8/ui/...`); a
package's `ts/test` cannot import them (decision 10). The four native tool tests stay in `test-native/` and import the
tools from their packages; `n8Play`'s pure cases move to `n8/ts/test`.

New tests, each RED first on the code it replaces:

1. **Menu equality** (`test/menu/recorded-trees.test.ts`, before the first menu edit): `b96fb1bb9`'s `menuDefs` bundled
   and run over a case matrix (every platform and each SameBoy model, each tracker with and without a readable ROM, one
   and several systems, N8 and Launchpad present and absent, a scan idle and busy, a standalone and a DAW host),
   recording each tree's ids, labels, kinds, `disabled` flags, children and verb lists as JSON; the contribution-built
   menus are held to it, which is what pins the row-by-row claims of 4.3 and the product-order claim of 4.4. Beside it,
   **a row table recorded from the old code** (`test/menu/recorded-rows.ts`: per row id its label, its value list, its
   default and its gate over the model and platform cases), which the declarative rows are generated against; the
   sameboy gate and the `driveRealCart` key are held there.
2. **Help and summaries**: every tool's `help` and `summary` recorded at `b96fb1bb9` and held by the generated table.
3. **Host typing** (`core/ts/test/host.test.ts`): a hook given a host lacking its group fails `tsc`
   (`@ts-expect-error`).
4. **The generator**: refusals for a `retroplug.ui` / `cli` / `examples` / `host` member naming a missing module, a
   duplicate tool name, a row id used twice in a placement, an example whose entry is outside `examples/`, a host
   declaration not on the list, plus the emitted modules, RED first (the F4a fixtures' pattern, `test/packages`).
5. **Rule 3 host modules** (`test/boundaries`): declared, undeclared, unknown and stale cases.
6. **Row rendering** (`ui/ts/test`): the three fallbacks, `when`, `separatorBefore`, `platforms`, nearest step.
7. **Services** (`test-ui`): the N8 card row and the scan advance and repaint through frames; `restoreLaunchpad` runs
   once.

## 8. The stages

Six stages on a prototype branch, DeepSeek Flash implementing and Opus reviewing each before the next (F4a's routing).
Each stage leaves the tree green on every oracle of 7.5 it can reach; stage runs use
`.superpowers/verify/tip-verify-par.sh` with `sdl:controller` run alone.

1. **The host contract.** `core/ts/host.ts` (and the types it takes from `src`), the three host builders, the three
   slices and `src/songImportHost.ts` deleted, every hook in 3.2 retyped (10 `ConstructCaps` files, the render hooks and
   their three test files, `lsdjSongImport` and its two tests), the host typing test. *Exit:* `tsc` clean everywhere;
   the new typing test RED then green; parity, kit and render baselines and the song-import and render-hook tests
   unchanged.
2. **The `ui` package and the declarative rows.** First the menu equality recording (RED anchor), then `packages/ui`,
   the menu model moved, the contribution types, `retroplug.ui` and `ui/generated/menus.ts`, the role-row renderer, the
   SameBoy and Mesen rows, LSDj's sync rows, the controller-app fields as `ConfigRow`, `validSplits` and
   `validAudioRoutings` off the descriptor. *Exit:* equality green over the system-settings cases; `test:ui` and
   `test/menu` unchanged; the generator fixtures green; `menuDefs.ts` holds no SameBoy or Mesen row.
3. **The tracker entries.** `TRACKER_UI` replaced by `TrackerUi` entries, the cart callbacks moved (lsdj, risa, smsggdj,
   BlipToaster), the `.rit` code in tracker, BlipToaster's settings, `host.views.open`. *Exit:* equality over the
   tracker cases; `lsdj-hd`, `lsdj-overlay`, `bliptoaster-settings` and the asset and song menu tests unchanged;
   `menuDefs.ts` imports no tracker package.
4. **The device menus.** First the N8 card and scan service tests, RED against the hooks, then the N8 and Launchpad
   entries, their device modules and `services`, the Link Group builder, the instance title's short label,
   `useUiServices`. *Exit:* equality over the device cases; `n8-sd-ops` and `launchpad` menu tests and
   `test-native/launchpad-devices` pass with repointed imports; `menuDefs.ts` and `App.tsx` import no cart; the ratchet
   (written here, RED first) holds the five files.
5. **The CLI.** `CliTool` and `retroplug.cli`, the seven tools moved, `dsp/wav`, `host.songs.list`, the examples with
   `retroplug.examples` and the SDK additions, `host-modules.json` with rule 3 and the generated `paths`, the CMake
   target. *Exit:* every tool's help byte-identical; the four tool tests unchanged but for imports; each example builds
   and runs (`mgb-smoke` by path first, E4); `pnpm test:cli-ts`; the three root scripts work; the SDK drift check green.
6. **The checks and the docs.** Rule 9 and the scanner over `cli/`, the bundle check, each RED first on the tree before
   the change that satisfies it; the docs pass (`AGENTS.md`, `spec/00`, `02`, `06`, `07`, `09`, `docs/package-split.md`
   gaining F4c, `docs/lsdj.md:264, 414`, `docs/n8-usb-capabilities.md:41`, `README.md:276`). *Exit:* `check:boundaries`
   green with the list as 7.1 gives it; every oracle of 7.5; BlipToaster, NESVJ, the Reaper suite and both SDL legs.

## 9. Risks and open questions

Found against the code. The draft's fifteen items are mostly decided now (decisions 8 to 10); each is marked, with a
pointer, and only what is still open is argued.

1. **`n8-sync` imports risa (decided, decision 9).** `n8-sync` moves to `risa`, which depends on `n8`; `n8` exports
   `runBridge` and stays cart-free; section 5.2.
2. **`bliptoaster-rom` needs risa's kit format (decided, decision 8).** The NES codecs are already tracker's, the DMC
   kit spec types move to `tracker/nes/kitSpec`, the tool compiles by the string id `"risa"`; section 5.2.
3. **Where the shared `.rit` UI code lives (decided, decision 10).** `tracker`; section 4.4.
4. **The SDK grows by four exports (decided, decision 10).** `runSession` (documented for a session file run by path),
   `DirectGraph`, `interleaveStereoStreams`, `deinterleaveStereo`; section 5.3.
5. **`n8-load`'s battery listing (decided, decision 10).** Byte-identical through `host.songs.list` and
   `batteryListing`; section 5.2. The one new contract field is `TrackerIntegration.batteryListing`.
6. **The scanner over `cli/` (decided, decision 10).** Whitespace-containing strings skip the suffix rule;
   `sdkSurfaceCheck` reads the registry; section 7.2.
7. **Which tests move (decided, decision 10).** Menu tests and the four native tool tests stay; `n8Play`'s pure cases
   move; section 7.5.
8. **Shared pure helpers (decided, decision 10).** `@retroplug/core/path` takes `pathUtil`'s pure functions,
   `SAV_PATTERNS`, `isSavPath` and `sanitizeName`; `tryOp` goes with the cart code that uses it; `browseThen` goes to
   `@retroplug/ui/menu`.
9. **Row equality (decided, decision 10).** Pinned by the recorded row table and the recorded trees (7.5), not by
   construction.
10. **`Embed.shortLabel` (decided, decision 10).** Accepted; section 4.4.
11. **Services replace React hooks (decided that a service test goes first).** Stage 4 writes the N8 card and scan
    service tests before it moves a watcher (no `test-ui` file drives them today). Still open: whether a poll that ran
    while the UI was hidden or a counter that reset on remount differs; only the device tests and the SDL leg could show
    it.
12. **Dependencies added (decided, decision 10).** `ui` (new); `tracker` to `dsp` (types) and `n8`; `n8` to `tracker`
    and `ui`; `risa` to `n8` and `ui`; `launchpad`, `lsdj`, `smsggdj`, `sameboy`, `mesen` to `ui`; `bliptoaster` to
    `dsp` and `ui`. `pnpm install` without `--frozen-lockfile`; the stage report quotes the lockfile diff. Still open:
    whether the new `tracker -> dsp` edge trips the native boundary check (it reads native targets, and tracker has
    none; unverified).
13. **The UI bundle instrument (decided, decision 10).** `build-ui.js` gains an in-memory `bundleInputs`; stage 6.
14. **Not verified here, still open.** That `retroplug-cli build/cli/<x>.js` loads the SDK module (E4; stage 5 runs it
    first); the Windows, macOS and arm64 builds of every new package and script (first on push, as F4a owed);
    `reaper:all`'s `/dev/shm` limit on its jackd count.
15. **Still open: the shell program and `examples/`.** The shell's `tsconfig.json` will include `../*/examples` and
    extend the generated `tsconfig.host-modules.json`; a package's own `tsconfig.json` needs the same two lines when it
    has examples, which `typecheck` alone would not catch if forgotten (the generator's fixture checks it).
16. **Product order carries menu and tool order (decided, controller).** Accepted as the coupling F1 defined: the
    product list is the registration order, so it orders the menus' contributed entries and the CLI index as it already
    orders the ROM providers. Both are pinned: stage 2 records the instance, start and settings menu trees (every row id
    in order) from the old code and holds the new menus to them, and stage 5's `test/cli/cli.test.ts` gains the full
    command index in the new order (E1), so a reorder of `retroplug.products.desktop` fails a test rather than moving
    silently.
17. **The brief's corrections** (decision 8's note) are recorded where they matter: groups (3.3), the `.rit` home (4.4),
    the ratchet list (7.3), the bundle list (7.4), the allowlist (7.1), the SDK (5.3), the moved tests (5.2, 7.5), the
    help order (E1).

## 10. What it leaves

For `spec/07-remaining-work.md`'s F4b bullet:

- **F4c**: a UI kit in `@retroplug/ui/kit` (`Box`, `lv`, `PixelCanvas`, `useNativeEvent`, `useFramePoll` and
  `useRerender`, `DebugReadout`, `lsdjDebug` with its `subscribable`; `tagTestId` and `useStores` stay shell-bound and
  reach a view through an injected host), how packages declare React (the `retroplug.host` list gains the four modules
  and rule 3 already reads it), a UI-only view registry (a view list as its own manifest member and generated module, so
  it stays out of the control plane and the render worker; `ViewDefinition` in `src/graph/defineExtension.ts` is the
  unused seed), the LSDj HD player and the two overlays as views with `host.views.open`'s mapping removed (`App.tsx:83,
  143, 194, 197, 298, 307`), `MenuContext.openLsdjHd` gone, rule 9 and the scanner over `ui/` (38 literals today, 4 left
  after F4b: `useRisaRuntime.ts` and the views), and the `ui/` ratchet deleted. The shell's `tsconfig.json` includes
  `../*/ts`, so a `.tsx` there fails the non-UI program: F4c needs a `jsx` setting per package or a separate UI program.
- **Per-product bundles**, still owed, with F4a's finding that hook code rides every bundle that loads a registry; F4b
  adds that `ui/generated/menus.ts` and `cli/generated/tools.ts` are separate modules so the UI and the CLI each carry
  only theirs.
- **The shell's hard dependency on `launchpad`** (`registry`, `enums`), unchanged from F4a.
- **A UI kit compile** (`host.kits` in the UI host) when a UI feature needs it, and `systems.readRam` for F4c's views.
- Windows, macOS and arm64 builds of every F4b script and package, owed as F4a's were; `MemoryType` stays closed (F2).
