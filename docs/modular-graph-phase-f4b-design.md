# Phase F4b: the plain-TypeScript surfaces

**Date:** 2026-11-09 · **Branch:** `feature/modularize`
**Status:** designed `8a74339b9`, built on `feature/modularize` in the six commits and the follow-up section 12 records
(the prototype they replay is section 11), accepted and reviewed. Designed over `b96fb1bb9` (the F4a tip plus the
Windows and BSD-sed merge; sections 1-4 approved 2026-11-09, the draft's questions answered). Section 9 keeps only what
is still open.

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

Sections 1 to 10 read as built: where the prototype (six commits on the design commit `8a74339b9`) departed from the
design as committed, the text now says what was built, and section 11 records the prototype itself (each stage's review
and fix pass, every departure in one list with its reason, the verification, and what acceptance must run beyond the
parallel kit).

## 1. Goal and scope

**Goal.** The package split's goal for the two TypeScript surfaces that are not React: a cart's or a console's menu
rows, CLI tools and example sessions are its package's, reached through registrations the package contributes, and the
shell holds no code that names one. After F4b `ui/` (less the five view files F4c moves) and `cli/` import an explicit list of
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
  `lvgljs`), and rule 9 and the scanner over `ui/`. F4b leaves the five view files that import a cart (section 7.3) on a
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
| 13 | Decisions taken during the prototype (controller) | The controller-app rows keep the old display for a hand-edited `appConfig` byte for byte, and the latent display bug is owed (section 10); the N8 Load ROM dialog's patterns and the services' remount behaviour (latches reset per mount) stay identical to the code they replaced; the host-module list's `surface` is enforced by rule 3 now; the host-module surface and the menu matrix are pinned, not described | Accept the typed reading as a named edge; narrow the dialog to `*.nes`; latches that survive a remount; leave `surface` as documentation |
| 14 | The history rule | Each stage's tree is the tree its commit holds, extracted after the last rewrite, proven equal to the commit and built and tested alone (a fix pass that rebased a stage 3 hunk into stage 2 broke the stage 2 tree: section 11) | Testing the tip only |


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
  /** The lines `n8-load --show-song` prints for a battery image, byte for byte as the tool printed them before (the header,
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
  rpc: Call;                                                       // a function resolving src/rpcClient.ts makeCall() on first use
  midi(): MidiClient;                                              // src/realBackend.ts createMidiClient()
}
export interface ProcessHost {
  args(): string[];   // the tool's own arguments (what run(host, args) was given), its name excluded; [] for a session run by path
  keepAlive(): void;
  exit(code: number): void;
}
export interface ViewsHost { open(id: string, systemId: number): void; }
export interface ControllerHost {                                  // UI only: the Launchpad menu's project scope
  settings(): ControllerSettings;
  set(patch: { enabled?: boolean; app?: string; appConfig?: Record<string, unknown>; target?: string }): void;   // throws on an unknown target
  cartSync(): string | null;                                       // ProjectStore.controllerCartSync
  syncOverride(): { driving: boolean } | null;                     // graph/controller.ts controllerSyncOverride
  apps(): readonly ControllerApp[];                                // core's structural ControllerApp (the registry's apps)
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

The types the groups name and the shell owned moved to core as types only, as built: `core/ts/host.ts` holds
`PngImageData`, `FileBrowserOpts`, `Call`, `MidiClient` and `ControlSurfaceSettings` (`src/backend.ts`,
`src/rpcClient.ts`, `src/realBackend.ts` and `src/userConfig.ts` re-export them, so no caller changed), and the
capability table is its own module, `core/ts/capability.ts` (`@retroplug/core/capability`: `CapabilityOp`,
`CapabilityArgs`, `CapabilityResults`, `CapabilityProgress` and the argument shapes), which `src/capability.ts`
re-exports and rule 9's list names. `ControllerSettings` is NOT moved whole: `target` is the launchpad package's enum
and core cannot import launchpad, so core declares a narrow structural `{ enabled, app, target: string, systemId,
appConfig }` (with `ControllerApp` and a `ControllerAppField` that is a union of the three row shapes, held equal to the
`ui` package's `ConfigRow` by a two-way assignability case in `ui/ts/test/rows.test.ts`, since core cannot import `ui`),
and the shell keeps its own enum-typed one. `ControllerHost.set` checks `target` against the launchpad package's
`CONTROLLER_TARGET_VALUES` and **throws** on an unknown one (a bad target would be written into the live settings, the
published document and the next `.rplg`). A `KitBank` is `KitCompileResult`'s two fields (`dsp/ts/kitContract.ts:12`);
core cannot import dsp, so the type is declared in core and the shell's `test/hosts/host-groups.test.ts` asserts it is
assignable both ways (dsp has no `ts/test` tree). `SystemInfo.roles` is `readonly`, which is what a view can satisfy;
`LiveSavTarget` and the shell's three tracker resolvers accept `readonly` too.

### 3.2 The three surfaces, and the hooks retyped

Each surface's builder lives in the shell, beside what it adapts, and returns exactly the groups its surface has:

| Surface | Builder | Groups | Built |
| --- | --- | --- | --- |
| UI | `ui/host/uiHost.ts`: `createUiHost(stores, peers)` | `files png systems songs liveSongs dialogs devices views controller config` | once, in `ui/App.tsx` (`useMemo` over the stores) |
| CLI | `cli/cliHost.ts`: `cliHost(session, args)` | `files png kits devices process songs` | per tool run, from the `Session` and the tool's own arguments |
| Render | `src/render/renderHost.ts`: `renderHost(ctx)` | `emulator files` | per render (replacing `render.ts:611`) |

`createUiHost` takes a second argument, `peers: { views, beginImport }`, because `liveSongs.beginImport` is
`useSongImport`'s picker, which is App's React state and not a store (`useSongImport`'s `begin` became a `useCallback`,
so the host is built once). `src/hostGroups.ts` holds `filesHost`, `pngHost` and `filesPngHost` over a backend, the one
definition of "the backend as a host", used by `SystemsStore`'s construct hooks, the asset bakes, the HD player and the
builders; `importHost(backend, systems)` in `uiHost.ts` builds the one `Host<"files" | "png" | "systems">` a menu call
site needs. `devices.rpc` is a plain function that resolves `makeCall()` on first use in both hosts (`makeCall` throws
when no native RPC channel is bound, which every mock-tier test that builds a host lacks; a getter could be forced by a
spread, a function cannot). `ProcessHost.args()` is the tool's own argument vector (what `run(host, args)` was given,
the subcommand's name excluded, and empty for a session run by path), which is `cliHost`'s second parameter;
`hostArgs()` stays the SDK's for a session file.

```ts
export type UiHost = Host<"files" | "png" | "systems" | "songs" | "liveSongs" | "dialogs" | "devices" | "views" | "controller" | "config">;
export type CliHost = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">;
export type RenderHostGroups = Host<"emulator" | "files">;
```

`UiHost` is declared in `@retroplug/ui/contribution` (a package may not import the shell, so its one definition lives in
the package) and `uiHost.ts` re-exports it; `CliHost` is `core/ts/cliTool.ts`'s. The shell's host-groups test
enumerates each surface's groups at run time, so a group added to a type and not built fails.

Hooks, by the groups they use (each verified against the hook's body):

- `RoleType.onConstruct(spec, host: Host<"files" | "png">, config)` and `AssetCatalog.applyRoleConfig(..., host:
  Host<"files" | "png">, ...)`: they call `readFile`, `fileExists` (`lsdj/ts/app.ts:27`) and `pngDecode`
  (`lsdjAssetsRole.ts:74`). The asset roles' helpers (`applyOverridesToRom`, `applyConfigToRom`) take the same type;
  `applyRitOverride` reads no PNG and takes `Host<"files">`. `ConstructCaps` was named in 13 source files and two tests
  at `8a74339b9` (the design's 10, plus `systemsStore`, `menuDefs` twice and the HD session); all were edited.
- `TrackerSongImport.import(host: Host<"files" | "png" | "systems">, sys, paths)`: it calls `readFile`, `readSram`,
  `savPathOf`, `writeFileAtomic`, `setRoleConfig` and `loadSram`, **and `png`**: `lsdjSongImport` folds the role's
  overrides through `applyOverridesToRom` to build the ROM a `.lsdprj`'s kits are deduped against, and a font override
  decodes a PNG there (the design's body check missed it). The shell's `songImportHost` adapter goes; the UI host's
  `systems.savPathOf` is its one rule (`resolveSavPath`, "null when the system is gone").
- The render hooks (`seedSong` and `warnings` take no host; `loadSong`, `pressPlay` and `playingProbe` take
  `Host<"emulator">`). **The play sink stays an explicit argument**: `pressPlay(host, id, rom, sink)` keeps
  `RenderPlaySink { renderChunk }`, which is the request's own stream (built per request, after the host), not a
  capability of the emulator; the type stays in `tracker/trackerRender.ts`. `renderChunk` is not a member of `emulator`,
  so `warnings` and `loadSong`, which run before the sink exists, cannot call it.
- The cart menu callbacks (section 4) type themselves over the groups each uses: LSDj `Host<"files" | "png" | "systems"
  | "liveSongs" | "views" | "dialogs">`, risa and smsggdj `Host<"files" | "systems" | "liveSongs" | "dialogs">`,
  BlipToaster `Host<"files" | "dialogs" | "systems">`; a builder receives the full `UiHost`.

Deleted: `ConstructCaps`, `RenderHost` (tracker's), `SongImportHost`, `SongImportTarget` (replaced by `SystemInfo`) and
`src/songImportHost.ts`. A hook that asks for a group its surface lacks fails `tsc`, held in two places:
`core/ts/test/host.test.ts` holds the group mechanics (a `files` host cannot be given a hook that decodes a PNG), and
the per-surface refusals, one `@ts-expect-error` per surface, live in the shell's `test/hosts/host-groups.test.ts`
because core cannot import a builder.

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
7. **`png` joins the song import hook** (section 3.2): an import reaches `applyOverridesToRom`, whose font branch
   decodes a PNG.
8. **`songs.list` is composed, not CLI-only.** It is `src/batteryListing.ts` over each tracker integration's
   `batteryListing` (section 5.2), built into both the CLI and the UI host from stage 1; the UI has no caller today.

## 4. The `ui` package and the menu contributions

**The decision (approved): `packages/ui`, new, plain TypeScript in F4b and the React kit in F4c; both contribution
forms; a manifest member `retroplug.ui` read by the generator into `ui/generated/menus.ts`, imported only by `ui/`.**

### 4.1 The package

`packages/ui` is an ordinary package: `private`, `type: "module"`, `exports` (no wildcard) `./menu` and
`./contribution`, a `tsconfig.json` extending `tsconfig.base.json`, a `typecheck` script, `ts/test/`, dependencies
`@retroplug/core` (the Host, `SystemInfo`) and `@retroplug/tracker` (the catalogs the specs name) and a devDependency
`@retroplug/testing`. It contributes no manifest member itself, so it is not in `retroplug.products.desktop`: the
generator refuses a listed package with no `retroplug` member and `cmake/packages.cmake` refuses one with an empty
member (a package with no `app`, `consoles` or `protocol` is left out of the ordering, as `tracker` is). The shell
declares it a dependency (spec/06's "Adding a package", step 3), and so does each package that imports its types
(`launchpad`, `lsdj`, `mesen`, `sameboy`, `smsggdj`, `risa`, `bliptoaster`, `n8`).

`@retroplug/ui/menu` is `ts/menu/index.ts` over `menuTree.ts` (66 lines: `MenuItem`, the seven kinds, `PromptSpec`,
`MenuAction`, `MenuTree`) and `menuItems.ts` (71: `action`, `button`, `infoRow`, `submenu`, `separator`, `actionCycler`,
`cycler`, `cycleInt`, `noticeTree`), `git mv`d out of `ui/screens/menu` with a 0-line diff: 25 shell files import them
and each was repointed (five had two imports of one module and now have one), `Menu.tsx` among them. Two modules joined
them later: `browseThen` (with the cart callbacks, stage 3) and `createSubscribable` (`subscribable.ts`, stage 4, so a
device pack can push a native change without importing the shell; the design had it in F4c's kit). None imports React or
the shell.

### 4.2 The contribution types

```ts
// @retroplug/ui/contribution
export type UiHost = Host<"files" | "png" | "systems" | "songs" | "liveSongs" | "dialogs" | "devices" | "views" | "controller" | "config">;
export interface ConfigChoice { readonly value: string | number; readonly label: string }
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

/** The placements are DATA, so the generated module checks a builder's placement against the same list the type uses. */
export const PLACEMENTS = ["instance.tracker", "instance.devices", "instance.footer", "system.settings", "start", "settings.midi"] as const;
export type Placement = (typeof PLACEMENTS)[number];
export interface MenuBuildContext {                      // what a builder reads that is not the host
  readonly systems: readonly SystemInfo[];
  assetRom(romPath: string): Uint8Array | null;           // the shell's per-path base-ROM cache
  readonly romPatterns: readonly string[];                // the shell's ROM dialog patterns (ROM_PATTERNS), built from the registry
}
export interface MenuBuilder {
  readonly placement: Placement;
  build(host: UiHost, sys: SystemInfo | null, ctx: MenuBuildContext): MenuItem[];   // [] hides the entry
}

export interface SongMenuSpec { /* id, catalog, exportSong / replaceSong / addSong, and the optional working-song callbacks; each takes (host, sys, ...) not (ctx, sys, ...) */ }
export interface AssetMenuSpec { /* id, catalog, exportAsset / replaceAsset, and the optional selectedSlot / selectSlot, which also take the MenuBuildContext so a selectable-slot read hits the shell's ROM cache */ }
export interface TrackerUi {
  readonly id: string;                                   // the integration id
  readonly song?: SongMenuSpec;
  readonly asset?: AssetMenuSpec;                        // optional: a battery-only cart has none; all four carts contribute one
  readonly extras?: { readonly rows?: readonly RoleRow[]; build?(host: UiHost, sys: SystemInfo, ctx: MenuBuildContext): MenuItem[] };
}
export interface UiService {
  readonly id: string;
  start?(host: UiHost): void;                            // once per MOUNT of the UI (as the effect it replaced ran), and it puts the service's latches back where a mount finds them
  subscribe?(listener: () => void): () => void;          // a native-host state change that must repaint (the N8 link)
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

The module also holds the readers and the one cycler builder: `readConfigRow`, `nearestIndex`, `rowShownOn`,
`rowShownFor`, `configRowCycler` (which `ui/screens/menu/configRows.ts` re-exports, and which the launchpad package's
controller-app rows use too) and `legacyConfigIndex`. A `TrackerUi.asset` and `song` are optional because stage 2
contributed only LSDj's `extras.rows`; stage 3 filled them, and a future battery-only cart simply has no `asset`.

The controller-app field shape (`ControllerAppField`, `launchpad/ts/registry.ts:19`: `key`, `label`, `choices?`,
`toggle?`) is the same shape without `id` and `role`: it became `ConfigRow` (a type alias kept in `registry.ts` through a
type-only import, so the portable core and the shell's `controllerAppRows` keep their import; `launchpad` gained
`@retroplug/ui` as a dependency for it) and gained `range`. `controllerAppRows` and the role-row renderer share
`configRowCycler`; the controller rows keep the ids `lp-app-<key>` the shell derives.

Reading a row's value reproduces today's two fallbacks exactly: a stored value of the wrong type (`typeof` differs from
the schema default's) reads as the default; a string or number the choices do not list reads as index 0 (`Math.max(0,
indexOf(...))`); a `range` reads the nearest step (`nearestIndex`). The default is the role schema's
(`schema.parse({})[key]`), which equals the menu's own fallback in every row below (checked against
`sameboy/ts/app.ts:26 to 41` and `mesen/ts/app.ts:26 to 92`). **The controller-app rows are the one exception, kept so
on purpose**: their old reading (a toggle is On unless it is `false`, a choice list with no match shows its first label,
nothing typed) disagrees with the typed one for a hand-edited `appConfig` (`quantise: 3` read "Immediately", the typed
reading "Next bar"), so `legacyConfigIndex` reproduces the old display for them (a controller decision, section 11), and
the latent bug it keeps - an invalid stored value reads one way in the menu while the runtime falls back to the schema's
default through `parseAppConfig` - is owed (section 10).

**The generated module's refusals** (`ui/generated/menus.ts`, at load, so a bad contribution fails the product's start
and not a menu click): a row id used twice in a placement (a tracker's extras count as `instance.tracker:<id>`), a
builder at a placement `PLACEMENTS` does not list, a row over a role kind no registered role type declares, a row whose
`key` (or a `when.key`) its role's schema does not declare, a `TrackerUi.id` no registered tracker integration has or two
packages both contribute, and a `UiService.id` declared twice; the shell's menu composers add `assertUniqueMenuIds` over
every tree they build (a builder that emitted `inst-exit` is refused).

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
| LSDj Mode `lsdj-mode`, Tempo Divisor `lsdj-divisor`, Auto Start `lsdj-autostart` | role `lsdj-sync` | `choices` (9) / `choices` (numeric 1, 2, 4, 8) / `toggle` (default false) | the HD Player action and `lsdj-assets-sep` follow as a builder (LSDj's `extras.build`, which opens the player through `host.views.open`) |
| Link Group `inst-link` | only with several systems and `core === "sameboy"` | **builder**, placement `instance.footer` | the peer count is not row data; sameboy's `linkGroup` builder (stage 4), the stored value read as a number or Off by `typeof`, the old rule, not `Number(...)` (a hand-edited string group stays Off, held by the `hand-link` case) |
| BlipToaster settings `bliptoaster-set-*` | read from the ROM's baked block, pinned with SysEx at the system's node and the cart's | **builder** | not role config; staged at the system's OWN cart only (4.4) |
| The N8 and Launchpad submenus, the Settings > MIDI scan rows | state machines over the native host | **builders** | section 4.5 |

Two facts make the table safe to rely on: every row's apply is a one-key patch, `systems.setRoleConfig(sys.id, role, {
key: value })` (21 call sites, each read), and gating by "the system has the role" (`sys.core === "sameboy"` today) is
the same set as "the system has the `sameboy` role" by the registry's rule (the core role attaches to every system of
that core); that equality is pinned by the menu equality test over the case matrix (section 7.5), not assumed.

### 4.4 Placements, and the trackers

The shell composes the menus from the generated list in product order, so a package's entry lands where its position in
`retroplug.products.desktop` puts it. Today's order is preserved by it: `sameboy` (4th) before `mesen` (11th) for the
system settings, `launchpad` (2nd) before `n8` (10th) for the instance's device entries. The one function that reads the
generated list is `menuPlacement(placement, host, sys, ctx)` in `ui/host/menuContributions.ts` (with
`contributedTrackerUi(id)` and `uiServices()`). The menu is keyed by an item's id, so the composer keeps the FIRST of
two rows that share one and reports the rest on the console: a hand-edited asset override can produce a duplicate, and a
missing row is better than a menu that does not render. `assertUniqueMenuIds` stays as the same rule for the
generator's checks and a test.

- `system.settings`: `systemChildren`' knob block becomes the rows of every contribution whose role the system has,
  then the shell's own rows. The first four contributions' rows are today's code.
- `instance.tracker`: `TRACKER_UI` goes; `trackerChildren` finds the `TrackerUi` whose `id` is the integration's, builds
  `extras` (rows, then the builder), the shared Songs menu, the asset menus and the bake rows, as now. `instance.devices`
  entries follow it inside the same fence (`inst-sep-tracker` appears when anything in the block is non-empty, as
  `tracker || n8Here || lpHere` decides today), Launchpad before N8.
- `start`: the N8 block with its own separator (`start-sep-n8`); `settings.midi`: the control-surface scan rows
  (`midiSettingsChildren`; N8 contributes none); `instance.footer`: after Remove Instance, before Exit (the Link
  Group, which sameboy's `linkGroup` builder fills from the peer count and the system's own role). The footer placement
  is the one the brief's list lacks.
- **The cart callbacks leave `menuDefs.ts` and become package code**, taking `(host, sys, ...)` over the narrowed groups
  of 3.2: LSDj's export, replace (with `importLsdprj`), add, working-song and asset actions to `lsdj`; risa's and
  smsggdj's to their packages; BlipToaster's settings rows and its selectable-slot spec to `bliptoaster`. The two
  callers of `mutateSavBytes` became `host.liveSongs.mutate`; the file pickers became `host.dialogs.browse(...)`, the
  one `browseThen` helper moving to `@retroplug/ui/menu`. `menuDefs.ts` keeps the generic `songMenu`, `loadGuard`,
  `assetMenu`, `romPatchRows`, `bakeRom` and the shell's rows, and imports no cart package. `MenuContext.host` is
  optional: App passes the host it built, and a context without one (the menu tests build menus over the stores alone)
  gets `hostOf(ctx)`, the same host built from the context's own peers and cached per context, so a contribution always
  receives the full ten groups. **BlipToaster pins its settings at its OWN system's cart only**: the old `cartFor` checked
  that the cart's system was the menu's, and `drivenCart()` alone (the first NES in slot order) would have staged the
  SysEx at another NES's console in a two-NES project; the builder compares `cart.id` with `sys.id`. Its selectable slot
  reads the ROM through the build context's cache, not from disk per row.
- **The pure helpers move to `@retroplug/core/path`**: all of `pathUtil`, plus `SAV_EXTS`, `SAV_PATTERNS`,
  `isSavPath` and `sanitizeName`; `src/pathUtil.ts` and `src/savPaths.ts` stay as re-export shims so the shell's
  importers are untouched (so git sees `pathUtil.ts` modified and `core/ts/path.ts` added, not a rename), and
  `resolveSavPath` stays in the shell (the moved `importLsdprj` uses `host.systems.savPathOf`).
- **The shared `.rit` asset code** (`RitAssetOps`, `exportRitAsset`, `replaceRitAsset`) is used by risa and BlipToaster.
  **The brief put it in `risa` "because BlipToaster already depends on it"; it does not** (its manifest declares `core`,
  `tracker`, `zod`, and `risa/ts/rom/index.ts:6` says outright that BlipToaster "must not depend on risa"). The code
  needs no menu type, so it moved to `@retroplug/tracker/ritAssetOps` beside `ritAssets.ts` (decision 10): `openRom`
  takes `Host<"files">`, the overrides are read through one shared `readRitOverrides` in `ritAssets.ts`, and tracker has
  its own three-line `pick()` over `host.dialogs.browse` because `ui` depends on `tracker` and `browseThen` is out of
  its reach.
- `validSplits` (`ui/host/render.ts`) became `["mix", ...(d.splits?.channels ? ["channels"] : []),
  ...(d.splits?.pins ? ["pins"] : [])]` over the console descriptor read by `consoleOfPlatform(platform).splits`
  (`sameboy` declares `channels`, the NES `channels` and `pins`, the GBA and SMS descriptors none), the same sets as
  `core === "sameboy" || platform === "nes"` for today's five platforms (a new case cycles Audio Routing on one SMS and
  one Game Gear). **`validAudioRoutings` (`menuDefs.ts:623`) held the same `platform === "nes"` literal for the pin
  routings, which the brief did not list; it reads `splits.pins` too.**
- The instance title's `"mGB"` reads the embed declaration. `Embed.label` is `"mGB (GB MIDI Synth)"`
  (`mgb/ts/app.ts:24`; the start menu's row is `Load mGB (GB MIDI Synth)`), so the title reads a new optional
  `Embed.shortLabel` (`core/ts/definePackage.ts`, `"mGB"` in mgb's declaration, the generator's embed pairing check
  unchanged); with none it falls back to `label`. The shell's `SystemView` gained `embeddedRom` (the embed id; shell
  only, `SystemInfo` is unchanged) so the title can find the declaration without the shell holding a cart's name.

### 4.5 Device menus and services

`n8` and `launchpad` contribute `ts/ui/` modules, each published through one `./ui` export (a pack's UI contract is one
module: the contribution plus the seam a test or another surface reaches it by; n8 dropped the two subpath exports it
first had). `packages/n8/ts/ui/` holds `n8Devices.ts` and `n8SdOps.ts` (both git renames) and an `index.ts` with
`n8MenuChildren`, `n8Detected`, the card-row label and the two builders; `packages/launchpad/ts/ui/` holds
`launchpadDevices.ts` (a rename) and an `index.ts` with `launchpadMenuChildren`, `controllerAppRows`, the status, sync
and scan labels, the port-name helpers, the two builders and the service. They read the shell through the host
(`makeCall` became `host.devices.rpc`, `UserConfigStore` became `host.config`, `drivenCartSystem` became
`host.systems.drivenCart()` through `projectCart(host)`, so the `"mesen"` literal is gone from the pack, `SystemView`
became `SystemInfo`). `launchpadDevices.ts`'s `resolveClient` takes the caller's channel and has no global fallback
(`test-native/launchpad-devices`, the one host-less caller, passes `makeCall()` once). The module-global state they keep
(the card job, the probed link, the scan) is per bundle, as today. A menu's card rows act on the PROJECT's cart whichever
instance's menu is showing, as `cartFor(ctx, null)` did, and the Load ROM dialog offers the shell's `ROM_PATTERNS`
through `MenuBuildContext.romPatterns` (the pack names no list of its own; a test pins the dialog to it).

The three watchers became `services`: the N8 card's versioned poll every 6 frames, the Launchpad scan's versioned poll
with its finishing-edge apply and `pollControllerRole` every 3 frames, and `restoreLaunchpad` (the launchpad service's
`start`), plus a `subscribe` for each native-host push. The version-and-edge latches move into each service's own
closure and `poll.run` returns whether the version moved; **each `start` puts the latches back where a mount finds them**,
because the refs they replaced were per mount, so a remount repaints once and takes an already finished scan's finishing
edge once more (the applied-scan guard makes the re-applied edge a no-op for `config.json`). `ui/app/useUiServices.ts`
is the one generic hook: it runs `start` in an effect, subscribes every `subscribe` through `useRerenderOn` (the list is
a module constant, so the call is stable) and hands the frame counting to `ui/app/serviceTicker.ts`'s `tickServices`
over one `useNativeEvent("frame")`, which `test/services.test.ts` pins with fake services (per-service divisors, one
repaint answer per frame, a list whose length changed re-sizing its counters). `start` runs once per MOUNT and the hook
holds that mount's record in a ref, so a host object a render rebuilt cannot restart a service (a start emits, which
would re-render: a loop); a throw in a `start` or a poll is caught and reported, and skips only that service, which is
what the separate hooks it replaced gave. The Launchpad service's `start` also records the host's RPC channel, so
`__rp_scanControlSurface` (the SDL harness's hook, outside React) reaches the facet without a menu having read it
first. `App.tsx` lost `useN8SdWatch`,
`useLaunchpadScanWatch`, `restoreLaunchpad` and the two `subscribe*` imports. The packs' `resolveClient`, `resolveLink`
and the scan clock read a test-injected global (`__rp_launchpadClient`, `__rp_launchpadClock`, `__rp_n8LinkClient`)
before their module state, because the UI bundle holds its own copies of the modules a test could set; nothing in
production sets one.

The device-pack scan (`build-device-pack.mjs`'s whole-core scan) skips the modules a manifest names as `app`, `ui` and
`cli`, plus the directory of one that is its directory's index, and refuses a core module that imports one of them or a
module under such a directory, naming the importing file and the member.

**Opening the HD player** is `host.views.open("lsdj-hd", systemId)`: LSDj's `build` for `instance.tracker` calls it, and
the shell's `uiViews(openLsdjHd)` in `uiHost.ts` is the one place the interim cart id maps to today's screen by
`setHdSystemId` (`App.tsx`); the mapping and `MenuContext.openLsdjHd` are F4c's to remove. `App.tsx`'s
`targetSongExtensions` already reads the integration (F4a) and is unchanged.

## 5. The CLI tools, the example sessions and the host modules

### 5.1 The tool contract

```ts
// packages/core/ts/cliTool.ts (exported as @retroplug/core/cliTool)
export type CliHost = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">;
export interface CliTool<H = CliHost> {
  readonly name: string;                  // the subcommand
  readonly summary: string;               // the index line
  readonly help: string;                  // the `--help` text, byte-identical to today's per tool
  readonly longRunning?: boolean;         // calls host.process.keepAlive() and returns (a live bridge)
  run(host: H, args: string[]): void | Promise<void>;
}
export function defineCliTools(tools: readonly CliTool[]): readonly CliTool[] { return tools; }
```

`CliHost` is the one host name (the first cut had a second alias, `CliToolHost`, dropped by the fix pass). A tool's `run`
takes the host, not a `Session`: `host.files.*`, `host.png.*`, `host.kits.compile("lsdj", spec)`, `host.devices.rpc`
(`createN8Client(host.devices.rpc)`), `host.devices.midi()`, `host.process.{keepAlive, exit}` and `host.songs.list`; no
moved tool imports `Session`, `makeCall`, `createMidiClient` or `keepAlive`. The shell's five tools (`test`, `run`,
`render`, `analyze-capture`, `grab-frame`) use the whole session and keep their `Session` signature: `cli/tools.ts` keeps
exporting `CliTool` as `CoreCliTool<Session>`, so the five and `test/cli/cli.test.ts` keep their type import, one
adapter, `fromContribution(tool)`, binds a generated tool to `cliHost(session, args)` for the dispatcher, and
`composeTools(shell, contributed)` is the exported function that appends the packages' tools after the shell's five and
refuses a contributed name a shell tool has (tested, so the refusal is not a no-op `tsc` proves). `cli/cli.ts` is
unchanged (it reads `tools`).

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

As built, each tool is a module under its package's `ts/cli/` (`lsdjRom.ts`, `risaRom.ts`, `bliptoasterRom.ts`,
`n8Load.ts`, `n8Play.ts`, `n8Bridge.ts`, `n8Sync.ts`, `launchpadProbe.ts`) behind a `ts/cli/index.ts` that default-exports
`defineCliTools([...])` and is the manifest's `retroplug.cli`; the moves are `git mv`s, each tool imports its own package
relatively, and `n8` publishes `./cli`, `./bridge` and `./ui`.

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
first. **As built, the composition came forward into stage 1**: the stage 1 review found that the first cut of
`songs.list`, a `cli/batteryListing.ts`, made `ui/` import `cli/` (and the carts) and reached `n8-load` with no recorded
test. `src/batteryListing.ts` composes the header, the trial order and the refusal line over `TRACKER_INTEGRATIONS`,
naming no cart; both hosts call it; and `test/hosts/battery-listing.test.ts` holds the three strings recorded by running
`8a74339b9`'s `decodeAndPrint` verbatim over a risa save, an LSDj save and a blank image (section 7.5 item 8). The tool's
flag is `--show-song`; the design's `--ls` was a typo.

`encodeWav` and the other pure WAV functions (`wavHeader`, `pcm16`, `interleaveStereoStreams`, `deinterleaveStereo`,
`decodeWav`, `src/render/wav.ts:17 to 160`) move to `@retroplug/dsp/wav`; `createWavWriter` (the backend-bound half)
stays, importing them, and `cli/wav.ts` stays a re-export shim. Each tool's help text is byte-identical: the 13 tools'
`help` and `summary` were recorded from the old binary before the first tool moved (`test/cli/recorded-help.json`) and
the generated table is held to them (section 7.5 item 2); all 13 are identical. The tests:
`test-native/{lsdj-rom, risa-rom-cli, bliptoaster-rom-cli, n8-session}.test.ts` drive a tool against the real backend
and the real core, so they stay in `test-native/` (a package's `ts/test` runs on the pure-TypeScript mock tier and
cannot import the shell's `createRealBackend`); they import the tool from its package and call
`tool.run(cliHost(session), args)`. `test/cli/n8Play.test.ts` (62 lines, pure functions) moves to
`n8/ts/test/n8Play.test.ts`.

### 5.3 The example sessions

`mgb-smoke`, `analyze-mgb`, `export-mgb-channels` moved to `packages/mgb/examples/`, `export-nes-channels` and
`export-nes-mono` to `packages/mesen/examples/`, `analyze-lsdj-sync` to `packages/lsdj/examples/`; `render-rom`,
`render-song` and `rom-test` (no cart) stay in `cli/sessions`. A package lists them in `retroplug.examples` (`[{ "name":
"mgb-smoke", "entry": "examples/mgb-smoke.ts" }]`) and declares `retroplug.host: ["retroplug-cli"]`; each imports the
SDK by the bare specifier. `retroplug-example-session` builds each to the same `build/cli/<name>.js` (so the three root
scripts, the Reaper analyze steps and `docs/lsdj.md` keep working), through a `retroplug_examples()` function in
`cmake/packages.cmake` that reads the manifests of `RP_DESKTOP_PACKAGES` (it skips an empty list, and refuses a
duplicate name once the shell's three sessions are appended; the generator refuses the same duplicate, an entry
outside `examples/`, a name one of the shell's three sessions already owns, and a name that is not a plain file stem,
earlier); `tools/build-session.js` marks `retroplug-cli` external (the binary serves it from its loaded-module table,
as it does for a test).

**The SDK surface the examples need is larger than the brief's one addition (decision 10 accepts all four).** The six
examples imported `runSession`, `hostArgs`, `encodeWav`, `interleaveStereoStreams`, `deinterleaveStereo` and
`DirectGraph` from shell-relative modules. `cli/sdk.ts` exported `hostArgs` and `encodeWav` and **did not export
`runSession`** (its header says so on purpose: a test calling it would `tjs.exit(0)` and report a false pass),
`interleaveStereoStreams`, `deinterleaveStereo` or `DirectGraph`. F4b's SDK additions are `DirectGraph` (the class,
which `analyze-lsdj-sync` uses beside `s.project.graph`), the two stereo helpers (pure, from `dsp/wav`) and
`runSession`, documented as for a session file run by path and never from a test; each lands in `cli/sdk.ts`,
`cli/sdk-types.d.ts` and `cli/sdkSurfaceCheck.ts` (the drift check). The check grew with them: the `DirectGraph`
constructor's arity and every member the declaration describes, the real `ConstructSpec` assignable to the declared
one, the declared `EngineGraph` opaque, `runSession`'s arity, return and argument tuple, and `Exact` types for both
stereo helpers, with a usage body that constructs the declared class and round-trips the helpers (`runSession` is only
referenced, never called). Its first run found real drift, fixed in the declaration: `ConstructSpec` now carries
`romBytes?`, `sramBytes?`, `stateBytes?` and `coreConfig?` with the real types (`sramBytes` is `Uint8Array`). The check
compares `ConstructSpec`'s key set exactly both ways and each value both ways except `platform` and `core`, which the
declaration widens to plain strings on purpose (a one-way check cannot see an optional field the declaration drops).
**E4 was checked first**, as the design asked: `retroplug-cli build/cli/mgb-smoke.js` (the bundle is 357 bytes and says
`import { runSession } from "retroplug-cli"`) renders and writes its PNG, so the binary serves the SDK module to a
session run by path.

### 5.4 The host-module list

One shell-owned file, `packages/retroplug/host-modules.json`, lists each host module with the surface that provides it
and why: `[{ "module": "retroplug-cli", "surface": "cli", "provider": "the retroplug-cli binary's loaded-module table",
"types": "cli/sdk-types.d.ts" }]`; F4c adds `react`, `react/jsx-runtime`, `lvgljs-ui`, `lvgljs` with surface `ui`. Rule
3 (`check-boundaries.mjs`, the third-party branch) accepts a bare specifier on the list when the importing package's
manifest declares it in `retroplug.host`, refuses one it does not declare, and refuses a declared name the list does not
have; a declaration nothing imports warns, as an unused dependency does. The rule's import walk grew from `ts/` to
`ts/` plus `examples/`. **The `surface` field is enforced, not documentation**: `surfaceOf(pkg, file)` decides a file's
surface from its own directory (`examples/**` and `ts/cli/**` are `cli`, `ts/ui/**` is `ui`, any other file belongs to
no surface), `surfacesOf(entry)` reads the entry's `surface` as one name or a list, and a host module imported from a
file that surface does not own is refused, naming the surface and its trees (a manifest member cannot decide it, since
the app and protocol paths would have to be trusted, and a filename convention would let a rename open the hole), so
`retroplug-cli` is reachable only from a package's `examples/` and `ts/cli/`, and F4c's four React modules from
`ts/ui/` alone.

**The typecheck `paths` come from the generator, in two files.** The generator writes `tsconfig.host-modules.json` (each
module's `types`, relative to the shell) which every package with `examples/` extends, and `tsconfig.paths.json` (the
same entries plus the shell's own aliases from the shell-owned `tsconfig.aliases.json`, now `{ "ui-harness": [...] }`)
which the shell's `tsconfig.json` extends; both are gitignored with the generated modules. Two files because TypeScript
REPLACES a child's `paths` with the extending config's rather than merging them, so the shell cannot extend the
package-facing file and add its alias beside it (the design's single file with the shell's hand-written entry removed
did not survive that). A package with `retroplug.examples` must have a `tsconfig.json` whose parsed `extends` (one or a
list, resolved against the package) names the generated host-modules file; a missing or unextended one is refused by
the generator. The root `typecheck` script is `node packages/retroplug/scripts/lib/packages.mjs && pnpm -r typecheck`,
so the generator has written every generated file before any package's `tsc` runs: a fresh checkout, which has none of
them, typechecks in CI's order (install, typecheck, build). `bundles.cmake` lists `ui/generated/menus.ts`,
`cli/generated/tools.ts`, `tsconfig.host-modules.json` and `tsconfig.paths.json` in `retroplug-ts-packages`'s
byproducts (and `host-modules.json` and `tsconfig.aliases.json` among its sources); `retroplug-cli-bundle`,
`retroplug-example-session` and the UI bundle already depend on that target.

## 6. Behaviour edges

None changes audio, a saved file or a graph document. Each is listed with what was found.

- **E1. The command index changes order.** Today `topLevelHelp` prints `test run render lsdj-rom risa-rom
  bliptoaster-rom n8-load n8-bridge n8-sync n8-play analyze-capture grab-frame launchpad-probe`. After: the shell's
  five, then the packages' in product order (`launchpad-probe, lsdj-rom, n8-load, n8-bridge, n8-play, risa-rom, n8-sync,
  bliptoaster-rom`). As built, `test/cli/cli.test.ts` pins the whole
  command index in this order (the design found presence-only assertions and nothing to update), so a reorder of
  `retroplug.products.desktop` fails a test; `README.md`'s table follows the order. `<cmd>
  --help` and every tool's output are unchanged.
- **E2. `n8-load`'s battery listing is unchanged** (decision 10): `host.songs.list` returns the printed lines, composed
  by the integrations' `batteryListing` (section 5.2), including the `not a risa or LSDj save` line by `rank`.
- **E3. The instance title reads the embed's short label** (section 4.4): identical output, one new optional field.
- **E4. The examples no longer bundle the control plane.** Each imported `../session` and so carried the whole boot
  path; with the SDK external each is a few lines against the binary's own SDK, so `build/cli/*.js` differ in bytes (not
  in behaviour). They run against the SDK of the binary that runs them, which is the same build. Verified in stage 5
  before anything else: `retroplug-cli build/cli/mgb-smoke.js` (a 357-byte bundle) serves the SDK module to a session
  run by path, as it does for `retroplug-cli test` and `run`.
- **E5. `n8-load` output paths and two cart strings move with the tools.** `"resources/roms/bliptoaster.nes"`
  (`n8-load.ts`) and the help examples keep their text.
- **E6. `validSplits` and `validAudioRoutings` are descriptor-driven**: equal for the five platforms, different only for
  a console that declares a split the old tables did not name.
- **E7. The watchers run as services at the same cadences** (6 and 3 frames), with the same latches; the observable
  change is the removal of three hooks from `App`. **No `test-ui` file drives the N8 card or the scan watchers** (only
  `midi-settings.test.ts` touches the MIDI rows, and only their device pickers), so stage 4 added the service test of 7.5
  item 7 first (`test/services.test.ts`, mock tier; the stage 5 fix pass added `test-ui/ui-services.test.ts`, run against
  the old hooks first). As built the latches reset per mount and `start` runs once per mount, as the refs and effect they
  replaced did: a controller decision to keep the behaviour identical.
- **E8. A hand-edited role config reads as before** (the fallback rules of section 4.2), row by row.
- **E9. The menu's device entries now sit by product order**, not by two literal lines; the order is the same today, and
  a future reorder of `retroplug.products.desktop` would move them (and the `[packages]` golden would show it).
- **E10. `UiHost`'s `kits` group does not exist**, so the UI cannot compile a kit; nothing does today.
- **E11. The controller-app rows keep their old display** for a hand-edited `appConfig` (a controller decision during the
  prototype): the typed reading of 4.2 would have changed `quantise: 3` from "Immediately" to "Next bar", so
  `legacyConfigIndex` keeps the old reading, pinned by the matrix's `hand-controller` case. The latent bug is owed
  (section 10): the runtime falls back to the schema's default through `parseAppConfig` while the menu shows the first
  label.
- **E12. The N8 Load ROM dialog offers the shell's `ROM_PATTERNS`**, as before, through `MenuBuildContext.romPatterns`
  (the first cut narrowed it to `*.nes`; the matrix now records the card rows' dialog options).
- **E13. A settings row needs its role, not just the core.** Base gated the core's rows on `sys.core === "sameboy"` (or
  the platform) and read `roleConfig(...) ?? {}`; as built a row shows for a system whose roles hold the row's role
  (`hasRole`). That is the same set of systems for every project the stores write, since a system always carries its
  core role, and it differs for a hand-edited `.rplg` whose non-empty `roles` omits it: `withMissingFeatureRoles`
  deliberately never re-adds the core role, so base drew the six sameboy rows and `setRoleConfig` refused every one of
  them (an absent role is not writable), and the menu hides them now. Accepted as a fix of dead rows, pinned by the
  matrix's `hand-no-core-role` case, which is the one case recorded from the NEW code: base's record of the same case
  is the six items (`sys-model`, `sys-highpass`, `sys-fastboot`, `sys-sep-display`, `sys-color-correction`,
  `sys-light-temp`) and five cyclers that the record kept here lacks.

## 7. Proof

### 7.1 Rule 9 over `cli/`

`check-boundaries.mjs` rule 9 grew from `src` to `src` and `cli` with **per-root semantics**: a file in
`packages/retroplug/src` reaches only `src/`, a file in `packages/retroplug/cli` reaches `cli/` or `src/` (never `ui/`,
`test/`, `test-native/`, `tools-src/` or another package relatively; a plain union would let `src` import `cli`), each
root's `generated/` is unjudged, every `@retroplug/*` specifier in either must be on `SHELL_IMPORTS`, and a stale entry
fails (the message names both roots). The list is **one list on purpose** (the two roots are one program's halves; the
comment says so and why no `roots` field), 29 entries, gaining `@retroplug/core/host` and `@retroplug/core/capability`
(stage 1: every `src` file that names the contract needed them), `@retroplug/core/path` (stage 3), `@retroplug/dsp/wav`
(stage 5), and `@retroplug/core/cliTool` and **`@retroplug/testing`** (stage 6: the SDK serves the harness and
`rom-test.ts` imports it; the brief's list omits it), and losing `@retroplug/tracker/trackerSongImport`, which no `src`
file imports any more. **RED first**: 4 refusals in 4 files on the fixed stage 5 tree against the 24-entry list
(`core/cliTool` in `cli/cliHost.ts` and `cli/tools.ts`, `@retroplug/testing` in `cli/sdk.ts` and
`cli/sessions/rom-test.ts`); the whole-prototype review reproduced 23 refusals on the pre-F4b `cli/` (the design's
25 counted `@retroplug/testing` too). The fixtures in `test/boundaries`
gained `shell-cli-allowed` (a contract module plus a `../src` reach), `shell-cli-forbidden` (a cart imported from
`cli/`) and `shell-cli-relative` (a `src` file reaching `../cli/run` and a `cli/` file reaching `../ui/App`, each
refused: "rule 9's relative half is per root").

### 7.2 The literal scanner over `cli/`

`run-scan.mjs` takes `--dirs src,cli` (default `src`, so the existing fixtures hold) and `--carts`: the embed ids,
tracker ids and kit format ids, read by the test from `src/generated/packages.ts` (`EMBEDS`, `TRACKER_INTEGRATIONS`,
`KIT_FORMATS`), without which the tools' `"lsdj"` and `"risa"` kit ids and the examples' `"mgb"` are invisible (the F4a
sets are platforms, extensions, cores and node types). The flag may be empty (a fixture tree can declare no cart) but not
absent (an absent flag would scan a real tree with no cart guard). `generated/` directories and `cli/sdk-types.d.ts` are
skipped. Over `src` the new set adds two hits, `"mgb"` and `"bliptoaster"` in `projectConfig.ts`'s frozen v2 migration
table, which join the existing exemption (`FROZEN_MIGRATION_KEYS`) by file and value. A string that contains whitespace
skips the extension-suffix rule (the help prose of `run` and `test`, each ending in `bliptoaster.nes`; a path has none),
and a bare id is still a hit. **RED first** on the fixed stage 5 tree, where the tools and examples had already moved:
3 hits, all in `cli/sdkSurfaceCheck.ts` (`gb`, `sameboy`, `mesen`: the drift check's own new usage and its old role
literal); the design's estimate for `b96fb1bb9`'s `cli/` was 21 hits in 13 files. The SDK check now spells no console:
it reads the console a REAL cart drives from the registry (`REAL_CARTS[0]`'s platform, found in `CONSOLES`; the first
cut read `CONSOLES[0]`, which is sameboy's and put a Mesen knob on the Game Boy). The scanner's own fixture
`literals-cli` holds a prose case and a path case. What it still cannot see (spec/06 lists them): device packages' ids
and node types, a non-console role kind, an id inside a whitespace-containing string, identifiers and computed strings,
`.json` data, and the SDK declaration it skips.

### 7.3 The `ui/` ratchet

Until F4c a test, `test/boundaries/ui-cart-imports.test.ts` (over a Node runner, `run-ui-cart-imports.mjs`, because the
scanner is Node's), walks `ui/` and fails for any file not on its list that imports a cart, console or device package,
by specifier or by a relative path that lands in such a package's tree. The list is **five files**: `LsdjOverlay.tsx`,
`RisaOverlay.tsx`, `useLsdjRuntime.ts`, `useRisaRuntime.ts`, `useLsdjHdSession.ts`; the brief and survey said six, adding
`LsdjHdScreen.tsx`, which imports react, `lvgljs-ui` and `useLsdjHdSession` and no cart (the word "@retroplug/lsdj" on its
line 3 is a comment). A listed file that stops importing one fails too, so F4c deletes the list by emptying it. The
package list is DERIVED by the runner from the workspace manifests: a package a view may not name is one whose
manifest declares any member that names contributed code or baked bytes (`app`, `ui`, `cli`, `examples`, `protocol`,
`consoles`, `embed`, `script`), the shell excluded, so a package added to the product cannot be left off a hand-kept
list and a device pack with no app module (the Launchpad) is guarded too. A specifier on rule 9's `SHELL_IMPORTS` list
is a contract module and is allowed from either side (the Launchpad's `enums` and `registry` among them); a package
declaring none of those members declares a contract or only native targets, and a view may name it. `ui/generated/` is
not judged. RED first: on the pre-stage tree two files failed, and a relative-import fixture the first runner missed (it
returned no hits) now fails; the device-pack fixture (a pack with `ui` and `cli` and no `app`, its client imported from
`ui/`) found nothing under the app-only derivation and is a hit now.

### 7.4 The bundle check

`test/script/bundle-ui-split.test.ts`, over `test/script/bundle-split.mjs` (which bundles each entry through
`build-session.js`'s `bundleInputs`, or `tools/build-ui.js`'s for the UI, esbuild's metafile in a node child): six
non-UI bundles, the control plane (`src/pluginControlPlane.ts`), the render worker (`src/render/worker.ts`, iife), the
SDK (`cli/sdk.ts`), the CLI dispatcher (`cli/cli.ts`, **which the brief omits and which is the bundle that carries
`cli/generated`**), the planner (`src/graph/protocolNodes.ts`) and the script nodes (`src/generated/scriptNodes.ts`,
iife), must carry none of: a `@retroplug/ui` module (`/packages/ui/`), any package's own `ts/ui/` tree
(`/packages/<name>/ts/ui/`) or `ui/generated/`; the UI bundle (`ui/main.tsx`) must carry no CLI code: not
`cli/generated/`, not any package's `ts/cli/` tree, not `core/ts/cliTool.ts`. The dispatcher still carries its own table
and the planner its protocol data (cases). `tools/build-ui.js` exports `uiBuildOptions` and an in-memory
`bundleInputs(entry, outfile?)` (the same node paths, aliases and working directory, `write: false`), with the script
body behind `require.main === module` so the CMake call is unchanged. RED first, with temporary imports: the planner
carried 4 UI modules and the UI carried `cli/generated/tools.ts`; after the fix pass, `import "@retroplug/lsdj/cli"` in
`ui/main.tsx` failed the UI side and `import "@retroplug/sameboy/ui"` in the planner failed the non-UI side (the first
cut's UI side let a package's CLI module through). `bundle-protocol.test.ts` and `bundle-no-zod.test.ts` are unchanged.

### 7.5 The oracles held, and the new tests

Nothing is re-recorded and no `RP_*_UPDATE` is set. Held on the fixtures-present run with `RP_FAIL_ON_SKIP=1` and on the
CI picture: strict parity, the kit and render baselines, `render-chunks`, `render-cli`, the `buildGraph` and role
goldens and the protocol digests, the `[packages]` golden (a package manifest member adds no node type), the device and
box image hashes and pack bytes (no native source changes), `pnpm test`, `test:native`, `test:ui` (every file passes
unchanged, among them `lsdj-hd`, `lsdj-overlay`, `risa-overlay`, `bliptoaster-settings`, `audio-routing-nes/gb`,
`settings-cyclers`, `render-menu`), `test:plugin`, `check:boundaries`, `check:docs`, both consumer suites (BlipToaster
57/57, NESVJ 19/19) against the rebuilt CLI, `reaper:params` and `reaper:params-clap`, the Reaper suite (`pnpm
reaper:all`, with `RETROPLUG_RESOURCES_DIR` set, as F4a's acceptance found) and both SDL legs (`sdl:smoke`;
`sdl:controller` run alone, since it takes the ALSA fakes). The menu tests (`test/menu/*`) build `buildInstanceMenu` over
`MockBackend` and `composeAppStores`, so **they stay in the shell under their slugs**, with imports repointed (`menuTree`
to `@retroplug/ui/menu`; `n8SdOps` and `n8Devices` to `@retroplug/n8/ui`); a package's `ts/test` cannot import them
(decision 10). The four native tool tests stay in `test-native/` and import the tools from their packages, passing
`cliHost(session)` to `run` (the signature moved, so the call site did; no assertion changed); `n8Play`'s pure cases
moved to `n8/ts/test`.

New tests, each RED first on the code it replaces:

1. **Menu equality** (`test/menu/recorded-trees.test.ts`, over `recorded-cases.ts` and `recorded-matrix.json`, before the
   first menu edit): `8a74339b9`'s `menuDefs` run over a matrix and recorded **from a scratch tree of the design commit**
   (`git archive` into a temporary directory with the worktree's `node_modules`, a throwaway test printing the
   collector's JSON, run twice and compared), the committed `recorded-cases.ts` being the one definition of both the
   recorder and the replay. The matrix is **34 cases** and three equality cases each (102): the tree, every cycler, and
   the dialog options of the card rows. Cases: a default Game Boy, a DMG model, an SGB model, a GBA, a NES, an SMS, a
   Game Gear (region nibbles 4 and 6), the NES with an N8 link and with a Launchpad running MI.MAP (and an LSDj cart in
   SYNC MidiMap with the controller on), two Game Boys with a link group, two NESes, LSDj, risa, smsggdj and BlipToaster,
   a two-NES BlipToaster project, the trackers' states (LSDj songs and a linked edited working song, a risa multi-legacy
   battery, an LSDj kit override, a BlipToaster pinned theme, a tracker whose ROM cannot be read), hand-edited role
   configs (wrong types, unlisted choices, a string link group, missing keys), a hand-edited controller `appConfig`, a
   scan in flight, the standalone host (`__rp_isStandalone`, with stubbed MIDI and audio config, so Settings > MIDI,
   Audio and Exit are recorded) and the start menu three ways. Per case it records every item's id, kind, label,
   `disabled` flag, children and actionCycler verbs, and per cycler its starting label and, per step of one full
   cycle, the label it shows afterwards **beside the state the step wrote** (a diff of the project's role configs by
   position, the project settings and the user config) and the node MIDI it staged (by node position), so a step that
   writes another value, or aims a message at another node, fails even when its label reads the same. It replaces the
   design's `recorded-rows.ts` row table, which pinned labels but not values or fallbacks; the cases hold the product-order
   claim of 4.4, the sameboy gate and the `driveRealCart` key too (the last also in `mesen/ts/test/ui.test.ts`).
2. **Help and summaries**: `test/cli/recorded-help.json` (13 tools: name, summary and the whole `--help` text) was
   recorded from the OLD binary before stage 5 moved a tool, and `recorded-index.json` holds the old index text; the
   test rebuilds the index from those rows in the registry's order (failing for a row the record lacks or a tool no row
   answers to), so the record stays a recording and E1's order is derived; `cli.test.ts` also pins the full command
   index and `composeTools`' clash refusal.
3. **Host typing** (`core/ts/test/host.test.ts` and the shell's `test/hosts/host-groups.test.ts`): section 3.2.
4. **The generator** (`test/packages`): refusals for a `retroplug.ui` / `cli` / `examples` / `host` member naming a
   missing module, a duplicate tool name, a duplicate example name, a row id used twice in a placement, a `UiService.id`
   declared twice, a row key or `when.key` its schema lacks, an unknown or duplicate tracker id, an example whose entry
   is outside `examples/`, an examples package whose `tsconfig` does not extend the host-modules file, a host
   declaration not on the list, plus the emitted modules and product order (beta as a second contributor), RED first
   (the F4a fixtures' pattern).
5. **Rule 3 host modules** (`test/boundaries`): declared, undeclared, unknown, stale, the `examples/` walk and a declared
   module imported from a file its surface does not own.
6. **Row rendering** (`ui/ts/test/rows.test.ts`): the three fallbacks, `when` (both directions, a sibling of the wrong
   type), `separatorBefore` (`test/menu/row-carriers.test.ts`: two carriers emit once, the first gated out opens the
   fence, both gated out emit none), `platforms`, nearest step, and the `ControllerAppField` / `ConfigRow` pair.
7. **Services**: `test/services.test.ts` (mock tier; a service is plain data: divisors through `tickServices`, the
   repaint answer, the startup call, the remount's re-taken edge) and `test-ui/ui-services.test.ts` through frames (the
   scan advances and repaints, the restore runs once per mount, an N8 push repaints without a frame), which was run RED
   first against the OLD hooks restored in `App.tsx` and passed there (3 of 3), then against the new tree.
8. **Battery listing** (`test/hosts/battery-listing.test.ts`): the three strings recorded by running `8a74339b9`'s
   `decodeAndPrint` verbatim over a risa save, an LSDj save and a blank 32 KB image.
9. **The device-pack scan** (`graph/device-pack`): the `ui` and `cli` control-plane rule, the directory rule and a core
   importing a package's `cli` module, each refused.
10. **The oracles that caught defects**: BlipToaster's staged SysEx in a two-NES project, the per-row ROM read count (a
    counting reader), `inst-link`'s typeof read (`hand-link`), menu id uniqueness.

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

As built the stages held, with these moves: `batteryListing` and its recorded oracle came forward into stage 1; the
recorded menu matrix was extended through stages 2 to 4 and recorded from a scratch tree of the design commit each time;
the `ui/` ratchet and the device-pack scan's `ui` / `cli` rule landed in stage 4, with the service tests; stage 5 carried
the recorded help, rule 3 (surface included) and the generated `paths`; stage 6 carried rule 9, the scanner, the bundle
check, the root `typecheck` script and the docs. Between stages, each Opus review's fix pass was folded into the commit it
belongs to, under the per-stage tree rule of section 11.

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
    service tests before it moves a watcher (no `test-ui` file drives them today). As built the latches reset per mount and
    `start` runs once per mount, as the refs and effect they replaced did (kept identical by decision); a host that hides
    and shows the UI must not assume the restore is once per process. Still open: whether a poll that ran while the UI
    was hidden differs; only the device tests and the SDL leg could show it.
12. **Dependencies added (decided, decision 10).** `ui` (new); `tracker` to `dsp` (types) and `n8`; `n8` to `tracker`
    and `ui`; `risa` to `n8` and `ui`; `launchpad`, `lsdj`, `smsggdj`, `sameboy`, `mesen` to `ui`; `bliptoaster` to
    `dsp` and `ui`. `pnpm install` without `--frozen-lockfile`; the stage report quotes the lockfile diff. The
    new `tracker -> dsp` edge did not trip the native boundary check (it reads native targets, tracker has none): the
    kit's configure and build legs pass with it, and the lockfile moved by 55 lines in all.
13. **The UI bundle instrument (decided, decision 10).** `build-ui.js` gains an in-memory `bundleInputs`; stage 6.
14. **Not verified here, still open.** The Windows, macOS and arm64 builds of every new package and script (first on push, as F4a owed);
    `reaper:all`'s `/dev/shm` limit on its jackd count.
15. **The shell program and `examples/` (decided, built).** The shell's `tsconfig.json` includes `../*/examples` and
    extends the generated `tsconfig.paths.json`; a package with examples extends `tsconfig.host-modules.json`, which the
    generator checks by parsing the package's `extends` (section 5.4).
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
  `useRerender`, `DebugReadout`, `lsdjDebug` (its `subscribable` helper already moved to `@retroplug/ui/menu` in F4b); `tagTestId` and `useStores` stay shell-bound and
  reach a view through an injected host), how packages declare React (the `retroplug.host` list gains the four modules
  and rule 3 already reads it), a UI-only view registry (a view list as its own manifest member and generated module, so
  it stays out of the control plane and the render worker; `ViewDefinition` in `src/graph/defineExtension.ts` is the
  unused seed), the LSDj HD player and the two overlays as views with `host.views.open`'s mapping removed (`App.tsx:83,
  143, 194, 197, 298, 307`), `MenuContext.openLsdjHd` gone, rule 9 and the scanner over `ui/` (one literal there today after F4b:
  `useRisaRuntime.ts`'s `"risa"`, which the scanner finds over `ui/`), and the `ui/` ratchet deleted. The shell's `tsconfig.json` includes
  `../*/ts`, so a `.tsx` there fails the non-UI program: F4c needs a `jsx` setting per package or a separate UI program.
- **Per-product bundles**, still owed, with F4a's finding that hook code rides every bundle that loads a registry; F4b
  adds that `ui/generated/menus.ts` and `cli/generated/tools.ts` are separate modules so the UI and the CLI each carry
  only theirs.
- **The shell's hard dependency on `launchpad`** (`registry`, `enums`), unchanged from F4a.
- **A UI kit compile** (`host.kits` in the UI host) when a UI feature needs it, and `systems.readRam` for F4c's views.
- Windows, macOS and arm64 builds of every F4b script and package, owed as F4a's were; `MemoryType` stays closed (F2).
- **What the prototype found for F4c and later:**
  - **The controller-app display bug** (owed, deliberately not fixed by a move): an invalid stored value reads one way in
    the menu (`legacyConfigIndex`, the first label) while the runtime falls back to the schema's default
    (`parseAppConfig`); fix it with its own change and re-record `hand-controller`.
  - **`tsconfig.ui.json`'s four hand-written `paths`** (`react`, `react/jsx-runtime`, `lvgljs`, `lvgljs-ui`) are F4c's
    to generate from `host-modules.json`, for the reason `tsconfig.paths.json` exists (a child's `paths` replace the
    parent's); `ui-harness` is not among them and stays the shell-owned `tsconfig.aliases.json`'s entry (with
    `tsconfig.paths.json`'s, which a child's `paths` also replace).
  - **The surface rule extends to F4c's four modules**: rule 3 admits `react`, `react/jsx-runtime`, `lvgljs-ui` and
    `lvgljs` (surface `ui`) only from a package's `ts/ui/`, and the bundle check already keeps a React kit in
    `@retroplug/ui` out of the five non-UI bundles and the script-node bundle.
  - **The ratchet's five view files** (`LsdjOverlay.tsx`, `RisaOverlay.tsx`, `useLsdjRuntime.ts`, `useRisaRuntime.ts`,
    `useLsdjHdSession.ts`) are F4c's to move; it deletes the list by emptying it. `uiViews`' `"lsdj-hd"` mapping and
    `MenuContext.openLsdjHd` go with the HD player; `MenuContext.beginSongImport` and `openLsdjHd` are the test
    fallback host's inputs only.
  - **The scanner is blind to device ids and node types** (`n8`, `launchpad`, `rp.n8.*`, `rp.launchpad.*`; `buildGraph.ts`
    spells the surface node types, owed with the shell's launchpad dependency), non-console role kinds, an id inside a
    whitespace-containing string, identifiers, computed strings and `.json` data.
  - **Example sessions and out-of-tree packs**: the shell's three example names are known to CMake only (a pack naming
    one fails at configure, not generate), and an examples package's `extends` hard-codes `../retroplug/`, fine inside
    this workspace and worth a sentence in spec/06 for an out-of-tree pack.
  - **`UiService.start` runs once per mount**, so a host that hides and shows the UI must not assume the restore is once
    per process.
  - **Test infrastructure**: `retroplug-launchpad-test`'s swap case is load-sensitive, and concurrent copies of the binary
    collide on `/tmp/rp-launchpad-claim-test-<n>`; `graph/device-pack` cannot run in a hardlinked per-stage tree (the
    allowlist sees the main worktree's `packages/core`).

## 11. As built (the prototype)

The prototype is six commits on the design commit `8a74339b9`, one per stage, each built by DeepSeek Flash and reviewed
by Opus, with each review's fix pass folded into the commit it belongs to by fixup and autosquash (the last pass into
stages 1 to 6 at once, and one pass rebuilt the history of stages 2 to 4: below), so the six trees stand as final and a
stage report's sha is older than the branch's. **The shas in this section are the PROTOTYPE's**, on
`proto/f4b-surfaces` (`e11e84d44` is its stage 6); the commits executed on `feature/modularize` are section 12's, each
the prototype's tree plus the plan document. The whole prototype is 302 files,
+63,151 -3,060 over the design commit, 54,613 lines of it the menu matrix's recorded JSON. Sections 1 to 10 above were
corrected in place to what the build found; this section is the record of how, and the plan is written against it. Nothing
was pushed.

**The stages**, by commit subject and current sha (files changed, lines added and removed):

1. `bce662f7e feat(f4b): the host contract, and the three surfaces' hosts` (52, +1188 -443): `core/ts/host.ts` and
   `capability.ts`, the three builders over `src/hostGroups.ts`, the three slices and `songImportHost.ts` deleted, every
   hook retyped, `TrackerIntegration.batteryListing` and `src/batteryListing.ts`, the host typing tests.
2. `9539d0fe2 feat(f4b): the ui package and the declarative menu rows` (67, +56781 -291): `packages/ui`, the menu model
   moved, the contribution types, `retroplug.ui` and `ui/generated/menus.ts`, the role-row renderer, the SameBoy, Mesen
   and LSDj-sync rows, the controller-app fields as `ConfigRow`, `validSplits` and `validAudioRoutings` off the
   descriptor, the recorded menu matrix.
3. `f7bc32050 feat(f4b): the tracker entries contribute their own menus` (24, +1170 -886): `TRACKER_UI` replaced by
   `TrackerUi` entries in `lsdj`, `risa`, `smsggdj` and `bliptoaster`, the callbacks moved, the `.rit` code in tracker,
   `@retroplug/core/path`, `browseThen`, BlipToaster's settings, `host.views.open`.
4. `8e9c64128 feat(f4b): the device menus contribute themselves, and their services` (62, +1612 -625): the N8 and
   Launchpad `ts/ui/` modules, the Link Group builder, `Embed.shortLabel`, `useUiServices`, the `ui/` ratchet, the
   device-pack scan's `ui` / `cli` rule, the service tests.
5. `5777d1d59 feat(f4b): the CLI tools and the example sessions, and the host modules` (100, +1841 -722): `CliTool` and
   `retroplug.cli`, the seven tools, `dsp/wav`, the examples and the SDK additions, `host-modules.json` with rule 3
   (surface included) and the generated `paths`, the CMake target, the recorded help.
6. `4350d9148 feat(f4b): rule 9 and the scanner over cli/, the bundle split, and the docs` (53, +671 -205): rule 9 and
   the scanner over `cli/`, the bundle check with its two sides, the root `typecheck` script, the docs pass.

**What each review found, and what the fix pass changed.**

1. *Stage 1 (with fixes, no Critical).* Important: `ProcessHost.args` was documented as the vector after the tool's name
   and built as `hostArgs()`, which includes it; `controller.set` laundered an open string into the shell's closed enum
   with a cast; and `songs.list` was a stopgap in `cli/batteryListing.ts` that made `ui/` import `cli/` (and the carts)
   and reached `n8-load` with no recorded test. Ten minors. Fix pass: `cliHost(session, args)` returns the tool's own
   vector (pinned), `controller.set` checks `CONTROLLER_TARGET_VALUES` and throws, and design 5.2 came forward: the
   `batteryListing` hook on risa and LSDj, one composer in `src/`, the three strings recorded from `8a74339b9` first
   (the controller's decision); the shell typing test was shown RED by moving the builders aside.
2. *Stage 2 (with fixes, no Critical).* Important: the recorded rows pinned labels, not values or fallbacks; the matrix
   missed `inst-link`, the `lp-app-*` rows and the `disabled` flags; the Link Group reader had been rewritten (`Number()`
   for the old `typeof` rule); `UiHost` was defined twice; and the generated module's checks missed a mistyped key, an
   unknown tracker id and placement drift. Eight minors. Fix pass: the whole matrix re-recorded from a scratch tree of
   `8a74339b9` (23 cases, with stored values and hand-edited configs; the Link Group RED shown with a `hand-link` case),
   one `UiHost`, the generator's added refusals (RED first), `PLACEMENTS` as data.
3. *Stage 3 (with fixes).* Critical: BlipToaster's settings SysEx was staged at `drivenCart()` without the old
   `cart.system.id === sys.id` check, so in a two-NES project it reached the other NES's console (RED shown, fixed).
   Important: the selectable slot read the ROM from disk per row (RED with a counting reader: 34 reads for 17 rows x 2
   builds); the matrix missed `settings.midi`, Audio and Exit (the mock was not standalone), a scan in flight, an
   unreadable ROM, the actionCycler verbs, the staged MIDI and the trackers' states; and the stage 2 and 3 commit messages
   misdescribed their trees. Controller decision: the controller-app rows keep the old display byte for byte and the
   latent bug is owed (the first fix attempt had presented the changed reading as a named edge). Fix pass: the matrix
   extended (33 cases and 66 equality cases then; 34 and 102 at the end), `AssetMenuSpec.selectedSlot` taking the build
   context, `legacyConfigIndex`, the two messages reworded.
4. *Stage 4 (with fixes).* **Critical: the fix-pass rebase put stage 3's `menuDefs.ts` changes into stage 2** (a
   conflict at the replay was resolved by taking the post-stage-3 file), so the stage 2 commit did not build, which no
   review had run a per-commit tree to find. Important: the N8 Load ROM dialog had been narrowed to `*.nes` without a
   record; `launchpadDevices` kept a `hostCall()` global fallback; the service tests never ran against the old hooks and
   `useUiServices` had none; the device-pack scan refused only `app` imports; and the service latches survived a remount
   where the refs they replaced did not. Controller decisions: keep the dialog and the remount behaviour identical,
   delete the fallback, test `useUiServices` green on the old hooks first, revert a docs edit that was not path-only.
   Fix pass: **the history rebuilt** (stage 2's `menuDefs.ts` change is 46+/204-, stage 3's 143+/726-, each from the
   pre-rebase refs by plumbing and verified file by file), **and the per-stage tree rule it led to: every stage's tree is
   built and tested alone**, not only the tip's; `MenuBuildContext.romPatterns` with a dialog record in the matrix,
   `hostCall()` deleted, `test-ui/ui-services.test.ts` (3 of 3 green against the old hooks restored in `App.tsx`, then
   against the new tree) and `serviceTicker.ts`, per-mount latch resets, the scan's `ui` / `cli` import refusal.
5. *Stage 5 (with fixes).* Critical: the stage 3 tree did not typecheck (a stage 4 test hunk had landed in stage 3, and
   the tree had been extracted before the commits were sealed), and a fresh checkout's `pnpm typecheck` failed because
   packages extended the gitignored `tsconfig.host-modules.json` before the generator wrote it (CI's order). Important: no
   committed help-text oracle; the tool-name clash refusal untested; the SDK drift check not extended; the shell's
   tsconfig restating the `retroplug-cli` path; manifests reformatted by a serializer (ten, not the eight first counted);
   the `examples/` walk untested, with one RED claim in the report that was wrong. Seventeen minors. **The rule was
   tightened: each tested tree must equal its sealed commit** (extract after the last rewrite, compare every file's
   `git hash-object` with `git ls-tree`). Fix pass: the hunk moved to stage 4, the root `typecheck` script runs the
   generator first (proved on a fresh `git archive`), `recorded-help.json` and `recorded-index.json`, `composeTools`, the
   drift check's new shapes (which found real `ConstructSpec` drift), the two generated `paths` files, the manifests
   hand-formatted again, an `examples/` fixture.
6. *Stage 6 and the whole prototype (with fixes, no Critical).* All six stage trees typecheck from fresh archives (12
   runs), the REDs reproduce, and the help texts match the old binary byte for byte. Important: n8's manifest was still
   machine-formatted (restoring it also found a stale `cli` member in stage 4 that left a duplicate JSON key at stages 5
   and 6, which last-wins had hidden from every test);
   the bundle check's UI side let a package's own CLI module through and omitted the script-node bundle;
   `sdkSurfaceCheck.ts` read `CONSOLES[0]` (sameboy) and spelled a Mesen knob on the Game Boy; the docs pass left
   `spec/04`, a `spec/06` path and stale comments; and `host-modules.json`'s `surface` was unenforced (controller
   decision: enforce it now in rule 3). Fifteen minors. Fix pass (into all six commits): n8's manifest, both bundle-check
   sides with their REDs, `REAL_CARTS`, the docs sweep, `surfaceOf` in rule 3 (RED first), the ratchet's documentation,
   `composeTools`' message, the examples-`extends` check parsed rather than searched, and the six trees re-extracted and
   proven equal to their commits (the first run of the proof script was invalid, hashing through the worktree and
   leaving `deps/dpf.js` empty, and is reported rather than hidden).

**Departures from the design as committed (`8a74339b9`), and why.**

- **`png` joins the song import hook** (`Host<"files" | "png" | "systems">`): `lsdjSongImport` reaches
  `applyOverridesToRom`, whose font branch decodes a PNG.
- **`createUiHost(stores, peers)`**, not `(stores, views)`: `beginImport` is App's React state. `devices.rpc` is a plain
  function resolving on first use in both hosts (`makeCall` throws with no native channel). `ProcessHost.args()` is the
  tool's own vector. `controller.set` throws on an unknown target. `SystemInfo.roles` is `readonly`.
- **Core holds a narrow structural `ControllerSettings`** (`target: string`), and `ControllerAppField` is a union held
  equal to `ConfigRow` by a two-way assignability case: core cannot import launchpad or `ui`.
- **The capability types are `@retroplug/core/capability`**, their own module (the design said `src/capability.ts`
  re-exports them from core and named `@retroplug/core/capability` only in rule 9's allowlist).
- **`batteryListing` came forward into stage 1**, with its recorded oracle, so `ui/` never imported `cli/`; the design
  had it in stage 5.
- **`TrackerUi.asset` is optional**; `MenuBuildContext` gained `romPatterns` (the N8 Load ROM dialog, identical to
  the old one); `AssetMenuSpec.selectedSlot` and `selectSlot` take the build context; `MenuContext.host` is optional with
  a `hostOf(ctx)` fallback for the menu tests; the generated module's refusals are broader (7.5 item 4); `PLACEMENTS` is
  data; `UiHost` is declared in the package.
- **The controller-app rows keep their old display** (`legacyConfigIndex`), not the typed reading the design gave all
  rows; the display bug is owed (section 10). A controller decision.
- **Services**: `start` runs once per mount and each `start` resets the latches (the old refs were per mount); the
  service test is `test/services.test.ts` (mock tier) plus `test-ui/ui-services.test.ts`, with test-injected globals
  (`__rp_launchpadClient`, `__rp_launchpadClock`, `__rp_n8LinkClient`) because the UI bundle holds its own module
  copies; frame counting is `serviceTicker.ts`'s `tickServices`. `createSubscribable` moved to `@retroplug/ui/menu` in
  stage 4 (the design had it in F4c's kit).
- **The device packs publish one `./ui` export** (n8 dropped two subpath exports); `launchpadDevices.ts`'s `hostCall()`
  global fallback was deleted; the shell's `SystemView` gained `embeddedRom` for the title's short label.
- **The device-pack scan skips `ui` and `cli` modules** and refuses a core module that imports one, or a module under
  such a directory: the design did not foresee the Launchpad scenario reaching the host through its menu half.
- **The Link Group builder reads a number or Off** (`typeof`), the old rule, not `Number()`; **BlipToaster pins at its own
  system's cart only**, the old `cartFor` rule.
- **`src/pathUtil.ts` and `src/savPaths.ts` stay as re-export shims** over `@retroplug/core/path` (so no rename), and
  tracker keeps its own three-line `pick()`: `ui` depends on `tracker`.
- **Tools are `ts/cli/` modules** behind a `ts/cli/index.ts`; `n8` exports `./cli`, `./bridge` and `./ui`; `CliHost` is
  the one host name; `composeTools` is an exported function so the clash refusal is tested; the shell's three example
  names are known to CMake and refused there if reused.
- **The SDK drift check was extended** (the four additions' shapes) and found real drift in `sdk-types.d.ts`'s
  `ConstructSpec`; `sdkSurfaceCheck.ts` reads the console a REAL cart drives, not `CONSOLES[0]`.
- **Help and summaries are recorded from the old binary** before stage 5 (13 tools), not from `b96fb1bb9`'s source;
  `recorded-index.json` is the old index verbatim and the order is derived in the test.
- **The host-module `paths` are two generated files**, `tsconfig.host-modules.json` and `tsconfig.paths.json` (with the
  shell-owned `tsconfig.aliases.json`), because a child's `paths` replace its parent's; the generator checks an examples
  package's parsed `extends` and refuses duplicate example names; the root `typecheck` script runs the generator first;
  `bundles.cmake` lists the new generated files; `retroplug_examples()` skips an empty list.
- **Rule 3 enforces `surface`** (`surfaceOf`, a list-valued `surface`) and walks `examples/`; the design left the field as
  documentation. A controller decision.
- **Rule 9 has per-root semantics and one 29-entry list** (`core/path` and `dsp/wav` joined, `trackerSongImport` left).
  **The scanner's `--carts` may be empty**; its RED on the fixed stage 5 tree was 3 hits, not 21, because the tools had
  moved. **The ratchet** was written in stage 4, derives its package list from the manifests and catches a relative reach.
  **The bundle check** gained the script-node bundle, `/packages/<name>/ts/ui/` and `ts/cli/` on the right sides, and
  `tools/build-ui.js` exports `uiBuildOptions` and `bundleInputs`.
- **New workspace dependencies**: `ui` (new), `tracker` to `dsp`, `n8` to `tracker` and `ui`, `risa` to `n8` and `ui`,
  `bliptoaster` to `dsp` and `ui`, and `ui` into `launchpad`, `lsdj`, `mesen`, `sameboy`, `smsggdj` and the shell; the
  lockfile moved by 55 lines in all, no specifier or version changed.
- **No C++ or CMake logic changed.** The C++ edits are stale-path comments (`ControllerLink.hpp`, `N8Host.cpp`,
  `cli/main.cpp`); the CMake edits are the example target, `retroplug_examples()` and the byproduct list.
- **The docs pass touched more than the design's list**: `spec/03`, `spec/04`, `spec/13`, `docs/launchpad-plan.md` and
  `docs/ui-shared-utils-audit.md` (the last two path-only) as well as `AGENTS.md`, `spec/00`, `02`, `06`, `07`, `09`,
  `docs/lsdj.md`, `docs/n8-usb-capabilities.md`, `docs/package-split.md` and `README.md`.
- **The design's own typo**: the tool's flag is `--show-song`, not `--ls`.

Existing tests whose shape changed, each judged by a stage review: `test/render/ui-seam.test.ts` (`validSplits` drops the
unused `core` field), `test/controller/registry.test.ts` (narrows a `ConfigRow` by `in`), the four native tool tests (they
pass `cliHost(session)`, no assertion changed), `test-native/launchpad-devices.test.ts` (passes `makeCall()`),
`test/cli/cli.test.ts` (the full index), and the menu tests' imports. `lsdj-hd`,
`lsdj-overlay`, `bliptoaster-settings` and the asset and song menu tests pass unchanged. No golden, baseline, digest,
image or pack byte moved at any stage, and no `RP_*_UPDATE` was set.

**Verification.** A baseline run at `8a74339b9` and a run at every stage and at the tip were each 42 legs
(`tip-verify-par.sh`, the parallel kit), every one exit 0, with `pnpm sdl:controller` run alone; the tip's run is
`/tmp/f4bpro/s6fv-*` and `compare.sh` against the baseline reports only the lines below.

| | baseline (`8a74339b9`) | prototype tip (`4350d9148`) |
| --- | --- | --- |
| `pnpm test` | 208 files, 2101 cases | 219 files, 2274 cases |
| `test:ui` | 42 files, 46 cases | 43 files, 49 cases (`ui-services`) |
| `pnpm typecheck` | no `ui` line | plus `packages/ui` |
| parity, native, the CI picture, the kit and render baselines, `render-chunks`, `render-cli` | 30 files / 250 cases, 172 files | identical |
| `test:plugin` | 13 binaries, 336 cases | identical |
| tsan and asan legs, device check, box, device mode | recorded | identical |
| images and the three `bliptoaster` pack byte hashes | recorded | identical (`cmp` clean) |
| BlipToaster and NESVJ suites against the rebuilt CLI | 57/57 and 19/19 files | identical |
| `sdl:controller` alone | OK | OK, its functional lines identical |
| `check:boundaries`, `check:docs`, `retroplug-ui-bundle` | 0 refusals, 95 files clean | identical |

The +173 cases and +11 files over the baseline are the stages' own tests. Each of the six stage trees was also extracted
after the final rewrite, proven equal to its commit, and typechecked and tested alone (menu, packages, hosts, ui, cli,
boundaries, source-literals, bundle split, services, core/host and the recorded matrix, each exit 0 where its file
exists). `pnpm test graph/device-pack` passes at the tip (17/17) but cannot run in that tree recipe (see below). One leg
failed once, the first full run's `plugin` leg on one case of `retroplug-launchpad-test` (`a swap mid-drain leaves the
audio thread a whole stream and no stale claim's message`): load-sensitive and not this phase's (no launchpad native
file changed but a comment; the case lets the audio thread read the link's generation twice and allows a one-claim skew,
which a descheduled thread under the parallel kit can exceed); the plugin leg alone passes, the case passed 5 of 5 and 6
of 6 under 32 CPU hogs, and the second full run was green. Concurrent copies of that binary are not safe to run (every
rig claims `/tmp/rp-launchpad-claim-test-<n>` from a counter starting at 0).

**What acceptance must run beyond the parallel kit** (the stage 6 review's list):

- the serial `tip-verify.sh`;
- a fresh `git clone --recursive`: `pnpm install --frozen-lockfile` at the root and in `deps/dpf.js/deps/lv_binding_js`,
  then `pnpm typecheck` BEFORE any build (CI's order), then `node tools/check-doc-links.mjs`, then the build;
- the fixtures present (`RETROPLUG_RESOURCES_DIR`) with `RP_FAIL_ON_SKIP=1`, for parity, the kit and render baselines,
  `render-chunks`, `render-cli` and the `test-native` tool tests;
- `pnpm reaper:all` with `RETROPLUG_RESOURCES_DIR` set, plus `reaper:params`, `reaper:params-clap` and the
  `reaper:editor*` legs;
- `sdl:smoke`, and `sdl:controller` alone;
- BlipToaster and NESVJ after `pnpm sync:bliptoaster`: 57/57 and 19/19, and BlipToaster's own `tsc` against the shipped
  `.d.ts`, since the SDK's declarations changed (`DirectGraph`, `runSession`, the stereo helpers, `ConstructSpec`'s
  optional fields);
- the three example scripts (`reaper:analyze-smoke`, `cli:export-mgb-channels`, `reaper:analyze-lsdj-sync`) and all nine
  `build/cli/*.js` sessions run by path;
- `pnpm test:cli-ts`;
- `graph/device-pack` at the stage 4 and 5 commits in real `git worktree`s with `pnpm install` (the hardlinked-tree recipe
  resolves the scenario's SDK through the main worktree's `packages/core` and the allowlist refuses it);
- the Windows build and `test.bat` via the user's Windows agent (the new packages and scripts, and the root `typecheck`
  script's `&&` under cmd), with macOS and arm64 owed as for F4a.

## 12. Execution

The execution, on `feature/modularize` over the design commit `8a74339b9`, is the plan commit `9cf213f1f` and six task
commits after it, one per stage, each a replay of its prototype commit rather than a rewrite: every task's tree is the
prototype's plus the plan document (the final review's `git diff --name-only` names only
`docs/modular-graph-phase-f4b-plan.md` for all six pairs), and the commit messages are the prototype's. The prototype is
`proto/f4b-surfaces` (`bce662f7e` .. `e11e84d44`, section 11); the executed commits are:

1. `feat(f4b): the host contract, and the three surfaces' hosts` (`5a14140b0`; the prototype's `bce662f7e`).
2. `feat(f4b): the ui package and the declarative menu rows` (`52471e918`; `9539d0fe2`).
3. `feat(f4b): the tracker entries contribute their own menus` (`f50c0c888`; `f7bc32050`).
4. `feat(f4b): the device menus contribute themselves, and their services` (`e9e8b3e02`; `8e9c64128`).
5. `feat(f4b): the CLI tools and the example sessions, and the host modules` (`4b3b8ec56`; `5777d1d59`).
6. `feat(f4b): rule 9 and the scanner over cli/, the bundle split, and the docs` (`b553ef9e8`; `e11e84d44`).

**The implementers' findings** (the six task reports; every task recorded a clean tree and every suite green, none
re-recorded anything, and no difference from the plan's text is a tree change: each is a count a fix pass moved, a
plan-text mechanics slip, or a compiler's wording):

- **Stage 1**: the per-tree `git status` cannot run in a `git archive` tree (no `.git`), so the proof was the step's own
  file-by-file hash loop re-run after the install, the generator and the suites (3,433 files, 0 differ); `pnpm test`
  printed 211 files and 2,109 cases where the plan quotes the pre-fix-pass 210; the assertion totals moved where the
  ground rules call them moving (engine-graph, its TSan and ASan legs, the launchpad TSan binary), every case count
  exact.
- **Stage 2**: `pnpm test`'s sealed tree is 215 files (the plan quotes 214, its own pre-fix-pass figure) and the
  generator 74 cases (quoted 69); the install has to precede the configure, which the plan listed the other way round;
  two assertion totals moved.
- **Stage 3**: no code finding; the same `git status` slip, and the launchpad TSan total far enough below the
  prototype's (-20%) to note while its case count and its non-TSan total match.
- **Stage 4**: `pnpm test` 218 files and 2,247 cases where the plan quotes 217 and 2,202, and `test:ui` 43 files and 49
  cases where it quotes 42 - both by exactly the files and cases the plan's own note says the fix pass adds, with the
  tree's file lists identical to the commit's.
- **Stage 5**: the recorded-help listing prints 14 entries where the step expects 13 (the 13 captures plus `index.txt`;
  the substantive claim holds: every tool's help text byte-identical, only the index differing, which is E1); `pnpm
  test` 2,267 cases where the plan quotes the 2,256 of a run before the fix pass's cases; the `git status` slip again.
- **Stage 6**: no blocking finding; the runner's `jobs=16` (the box's pool) differs from every quoted figure by
  construction, and the `git status` slip is the plan's, not a task's.

**The acceptance run** (`/tmp/f4bexec/acc-report.md`, at `b553ef9e8`) was PASS WITH FINDINGS. All 43 legs of the
serial `tip-verify.sh` exited 0, its summary identical to the two parallel baselines' but the `sdlctl` line a serial run
prints differently: strict parity 30 files and 250 cases with `RP_FAIL_ON_SKIP=1`, `pnpm test` 219 files and 2,274
cases, `test:native` 172 files (144 counted skips on the fixture-free CI picture, byte-identical to the baseline),
`test:ui` 43 files and 49 cases, `test:plugin` 13 binaries and 336 cases, the engine-graph binary 336 cases, the kit
and render baselines, `render-chunks`, the three device image hashes and the `bliptoaster` pack's three byte hashes
identical to the baseline, a fresh clone typechecked before its cold build (`Scope: 23 of 24 workspace projects`, one
pre-existing warning), both consumer suites against the rebuilt CLI (57/57 and 19/19), the BlipToaster `tsc` against
the shipped `.d.ts`, `reaper:params` 2725/2725 and `reaper:params-clap` 644/644, the three `reaper:editor*` checks, all
15 `reaper:all` checks with the resources tree, `sdl:smoke`, `sdl:controller` alone (its five functional lines identical
to the baseline's), and `check:boundaries`/`check:docs` clean. Its finding **F1**: `pnpm test:native render-cli` prints 6
cases where the plan expects 9; the file holds 6 at `8a74339b9` and at the tip and F4b never touches it, so the plan's
figure matches no record (exit 0, 0 skips, 0 failures). Its F2 to F6 were plan-text slips (a scratch path, the serial
summary's `sdlctl` line, `jobs=16`, moving assertion totals, and the "two documented skips" of the `ui` leg, which no
log holds and which did not occur).

**The final review** (`/tmp/f4bexec/final-review.md`, a read-only pass over `8a74339b9..b553ef9e8`) found no Critical
issue and two Important: **the `ui/` ratchet's guarded set was derived from `retroplug.app` alone**, so the Launchpad
(which contributes `ui`, `cli` and `protocol` and no app module) was never guarded and its two `CONTRACTS` entries were
dead code; and **`UiService`'s doc promised `start` "runs once when the UI is up"**, the opposite of the per-mount
behaviour the hook and every service implement, in the file a pack author copies. Its seventeen minors were the
behaviour edges nothing recorded (a row needing its role, a duplicate menu id thrown inside App's render, a throw in one
service skipping the others' frame, the startup restore keyed on the host object's identity, the automation hook needing
a prior facet probe), the one-way `ConstructSpec` check, `runSession`'s false pass, the generator's example-name gaps,
the non-exclusive `ConfigRow`, the examples packages' per-package `typecheck`, comments that recorded a change rather
than the code, the matrix's unrecorded re-record recipe, and five docs claims. It reproduced the whole matrix from the
design commit's own code (34 cases, 0 diffs) and audited every move line by line; its verdict was ready to call F4b
complete **with fixes**, and that the two Important items should land before F4c copies the templates.

**The follow-up** is four commits on top of the six, each with what it held green:

- `fix(f4b): the ui ratchet guards every contributing pack` derives the guarded set from every manifest member that
  names contributed code or baked bytes (`app`, `ui`, `cli`, `examples`, `protocol`, `consoles`, `embed`, `script`),
  which makes the `CONTRACTS` allowance the real rule: a specifier on rule 9's `SHELL_IMPORTS` list (exported from
  `check-boundaries.mjs` rather than hand-copied) is a contract module a view may name. RED: a device-pack fixture with
  `ui` and `cli` and no `app`, its client imported from `ui/`, reported no hit; on the real tree a temporary `ui/` file
  importing `@retroplug/launchpad/client` was one of the ten hits after the change and none before.
- `fix(f4b): the services contract and the menu's edges` corrects the `UiService` doc and the hook's header to the
  per-mount rule, runs each service's `start` and `poll` isolated (a throw is reported and skips only that service),
  makes the startup restore once per mount explicitly (a mount record in a ref, the host read through a ref and not a
  dependency), keeps the first of two menu rows that share an id and reports it instead of throwing inside App's
  render, records the host's RPC channel for `__rp_scanControlSurface`, and adds the matrix's `hand-no-core-role` case
  (E13). RED: the two thrown errors escaping `startServices`/`tickServices`, `expected true, got false` from the
  automation hook, and E13's two expectations failing against base's record of the same case.
- `fix(f4b): the final review's minors` closes the other fifteen: `ConstructSpec` compared both ways (RED: a dropped
  `coreConfig` typechecked clean before, and fails naming the field after), `runSession` refusing when a test case is
  registered (RED: the new cli-ts leg read `expected exit 1, got 0`), the generator's shell-name and file-stem refusals
  for an example (RED: both accepted), the exclusive `ConfigRow`/`ControllerAppField` (RED: three unused
  `@ts-expect-error`s), each examples package's `typecheck` running the generator first (RED: the filtered typecheck
  failed on the gitignored paths file), the stale comments, and the matrix's re-record recipe written beside the test.
- `docs(f4b): record the acceptance run, the review and the follow-up` is this section, the status line, section 11's
  sha note, the corrections above, AGENTS.md's ratchet count, spec/07's bullet and package-split's status.

**Verification of the final tree** (`/tmp/f4bexec/fuAv-*`, the same parallel 43-leg kit plus `sdl:controller` alone):
every leg exited 0 with the acceptance run's counts plus the follow-up's cases: `pnpm test` 219 files and 2,286 cases
with 0 skips (twelve new cases in five existing files: E13's three matrix cases, four service cases, two kept-first
menu rows, the ratchet's device-pack fixture and the two generator refusals; two `@ts-expect-error` pins are
compile-time), `test:ui` 43 files and 49 cases, `test:plugin` 13 binaries and 336 cases, parity 30 files and 250 cases,
`test:native` 172 files, `check:docs` 96 files clean, `check:boundaries` clean, and the three device image hashes and
the pack's three byte hashes identical to the baseline's (`compare.sh` against `8a74339b9`'s run reports only the
`test`/`ui`/typecheck count lines above). `pnpm sdl:controller` alone passed with its 15 functional lines byte-identical
to the baseline's, `pnpm reaper:params` and `pnpm reaper:params-clap` passed (2725 and 644 parameters, count
unchanged), and a fresh `git archive` of the tree typechecked at the root (`Scope: 23 of 24 workspace projects`) and
per package (`pnpm --filter @retroplug/mgb typecheck` with no generated paths file). Nothing was re-recorded: no
`RP_*_UPDATE` was set, and no golden, baseline, digest, image or pack byte moved (the matrix gained one case, whose
record is E13).

**What F4b leaves for F4c** (the final review's list, which section 10's items follow):

1. **The kit's location against rule 3's surface rule.** `surfaceOf` treats only a package's `ts/ui/` as the `ui`
   surface, so a React kit at `packages/ui/ts/kit/` would have `react` and `lvgljs-ui` refused: either the kit lives
   under `packages/ui/ts/ui/`, or `surfaceOf` learns that the `ui` package's whole tree is the UI surface. Decide
   before writing the kit.
2. **The UI program's paths and its `jsx`.** `tsconfig.ui.json` hand-writes four paths and its `paths` replace the
   parent's, so it silently drops `retroplug-cli` and `ui-harness`: generate it from `host-modules.json` the way
   `tsconfig.paths.json` is, and decide how a package's `.tsx` gets `jsx` (the shell's main program includes `../*/ts`).
3. **The ratchet before rule 9**: rule 9 and the scanner over `ui/` should start from the fixed derivation, which now
   guards a device pack without an app module.
4. **The views' host needs and the interim mapping**: the five ratchet files are the views and hooks that need
   `systems.readRam` (not yet on the UI host) plus a view registry kept out of the control plane; `uiViews`' `"lsdj-hd"`
   mapping and `MenuContext.openLsdjHd`/`beginSongImport` (the fallback host's inputs) go with the HD player.
5. **Services and remounts**: `UiService.start` runs per mount and is isolated now; if F4c's views mount and unmount
   independently of App, keep services App-scoped.
6. **The bundle check** already forbids `/packages/ui/` and any `ts/ui/` in the six non-UI bundles, so a kit that leaks
   into the control plane fails a test; keep the kit's module set out of the planner and the script nodes when adding
   view types.
7. **The controller-app display bug** is still owed; fix it with its own change and re-record `hand-controller`.
