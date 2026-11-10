# Phase F4c: React in packages

**Date:** 2026-11-10 · **Branch:** `feature/modularize`
**Status:** designed, over `c8a35d34b` (the F4b tip). The user's decisions are the `### F4c` entries of
`.superpowers/f-split/decisions.md` (taken 2026-11-10); approved by the user 2026-11-10; section 10 keeps
what is still open.

F4c is the last of the package split's fourth phase ([package-split.md](package-split.md)), after F4a
([modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md)) and F4b
([modular-graph-phase-f4b-design.md](modular-graph-phase-f4b-design.md)). F4b moved every plain-TypeScript surface of
the shell (the menus, the CLI tools, the example sessions) into the packages and left five files under `ui/` that
import a cart, held by a ratchet: the LSDj HD player's data pump, the two debug overlays and their two runtime hooks.
They are React, or feed React, and they are the parts of the UI that draw something no menu row can. F4c moves them,
and the key the overlays hang on, out of the shell. The survey behind it is `.superpowers/f-split/f4c-survey.md` (taken
at `c8a35d34b`; section 1 lists what the tree corrected).

The phase is a boundary refactor, not a feature: no audio change, no saved-file format change, no graph document change,
and no baseline is re-recorded except the recorded menu matrix's bindings menus (section 7). Its behaviour edges are
recorded in section 7, each with what was found; the user-visible ones are two new rows in each bindings menu and a
backtick that is rebindable per overlay.

## 1. Goal and scope

**Goal.** After F4c no cart code lives in the shell's `ui/`: `ui/` joins rule 9 and the literal scanner, and the ratchet
retires. A package contributes a view (a full-window screen, or a layer over an emulator tile) and a key binding
(an app action the binding profiles and the bindings menus carry), and the shell renders and routes them. A view is
declared, not coded in React: a package that wants React for one has it (`@retroplug/ui-react`, a new package) and a
package that does not never meets it.

**Scope (one phase, by mechanism).** Two contribution lists in `@retroplug/ui` (`views`, `actions`) and the host
members they read (section 3); the new `ui-react` package and what moves into it (section 4); the shell's side: window
and tile slots, three renderers, a session controller, key routing and the bindings menus (section 5); the LSDj HD
player, the two overlays and their actions as the packages' own (section 6); and the proof: the typecheck settings, rule
3, rule 9 over `ui/`, the scanner, the bundle check and a fixture pack that carries a React view (section 8). It runs as
one prototype in five stages (section 9).

**Not in F4c**, and where it goes:

- **The controller-app display bug** (owed since F4b): a hand-edited `appConfig` reads one way in the menu and runs
  another. Its own change with its own re-record of `hand-controller` (F4b design section 10).
- **Per-product bundles** (owed, unchanged from F4a): every product still lists the same 16 packages. F4c keeps the
  precondition true: `ui-react` counts as UI in the bundle check (section 8.5).
- **The shell's hard dependency on `launchpad`** (`registry`, `enums`), unchanged.
- **Windows, macOS, arm64 and CI** for every new script and package, owed as F4a's and F4b's were. Nothing F4c adds is
  platform code, but the first run of its checks on those hosts is on push.
- **A real host rendering a `component` view.** The UI test binary embeds the production bundle, so a component view
  has no pixels in any test until a product ships one (section 8.6 says what is proved instead).

**What the tree has** (checked against `c8a35d34b`; every figure was counted or opened, and the survey's are corrected
here).

- **The five ratchet files and four more.** `test/boundaries/ui-cart-imports.test.ts:14-20` lists
  `ui/screens/grid/{LsdjOverlay.tsx (41 lines), RisaOverlay.tsx (36), useLsdjRuntime.ts (56), useRisaRuntime.ts (53)}`
  and `ui/screens/hd/useLsdjHdSession.ts (170)`. The cart-free neighbours that go with them are `LsdjHdScreen.tsx` (67),
  `lsdjDebug.ts` (24, the process-wide toggle over `@retroplug/ui/menu`'s `createSubscribable`, flipped only at
  `App.tsx:205` and read by both overlays) and `DebugReadout.tsx` (36).
- **How a view is mounted today.** The overlays are mounted unconditionally in every tile (`EmulatorTile.tsx:87` and
  `:90`) and render `null` unless the toggle is on and their runtime hook found a supported cart. The HD player is
  App's `hdSystemId` state (`App.tsx:73`), opened through `uiViews`' one `"lsdj-hd"` mapping (`uiHost.ts:31-37`; the
  survey said 27-37), cleared when its system goes (`App.tsx:150-152`), closed by Esc (`:202`) and rendered by the
  full-window branch (`:314-318`) after the overlay and empty branches. Game input stays live under it
  (`useGameInput`, `App.tsx:295`, is keyed on `playing`, which an open HD view does not change).
- **What each reads.** `useLsdjRuntime`: `backend.readFilePrefix(rom, 0x150)` once, `LsdjReader.fromHeader`, then
  `backend.readRam(id)` per frame, through `useFrameSampled(sample, signature)` (`useFramePoll.ts:315`).
  `useRisaRuntime`: `backend.readFile(rom)` (the `RISA V` marker is deep in the PRG), `runtime.identifyRisaVersion`,
  `resolveRisaLayout`, `decodeRisaState`; its `hasRole(roles, "risa")` is the one scanner hit over `ui/`.
  `useLsdjHdSession`: `readFilePrefix`, `readFile`, the lsdj-assets overrides through `filesPngHost(backend)`
  (`../../../src/hostGroups`, the one `src/` reach in it), `readSram` every 15 frames (128 KiB) and `readRam` per frame,
  rebuilt when the ROM path, the `lsdj-sync` role or the `lsdj-assets` config changes (`useLsdjHdSession.ts:226-237`).
- **The binding model.** `AppAction = "OpenMenu" | "CycleNext" | "CyclePrev"` (`keyCodes.ts:132`);
  `buildKeyToAction` / `buildGamepadToAction` (`:139`, `:155`) map a key or pad name to ONE action and skip any id not
  in that list; the profile model is `src/bindingMap.ts` (`keyboardActions` / `gamepadActions`, seeded per section by
  `actionChannel`, `:34`); `App.tsx:115-116` builds the two maps and `runAction` (`:191`) dispatches. The backtick is a
  literal in App (`:48`, `:205`). The bindings editor is `bindingsChildren` (`menuDefs.ts:1191-1295`; `APP_ACTION_ROWS`
  at `:1178`; the survey's range 1177-1331 runs into `settingsChildren`, which starts at `:1303`).
- **`ViewDefinition` is not unused.** `src/graph/defineExtension.ts:27-46` declares `ViewPlacement` and
  `ViewDefinition`, `Contribution.views` and `Collected.views` (`:96`, filled at `:194`), and
  `test/graph/extension.test.ts:19` and `:46` contribute and read a view. No production extension contributes one. The
  brief called it an unused seed; it is a seed with one test.
- **Where the kit's members are imported.** Counted by relative specifier: `Box` 7 files (`App`, `StableSlot`,
  `DebugReadout`, `EmulatorTile`, `SystemGrid`, `LsdjHdScreen`, `Menu`), `lv` 5, `PixelCanvas` 2, `useNativeEvent` 10,
  `useFramePoll` 6, `useRerender` 4 (`useFramePoll.ts` itself imports `useForceUpdate` from it), `DebugReadout` 2.
- **Rule 9 today** has 29 `SHELL_IMPORTS` entries (the survey said 37) and two roots, `src` and `cli`
  (`check-boundaries.mjs:119`, `:799-803`); `ui/` is not judged. `ui/` outside `generated/` names 9 contract specifiers
  (`@retroplug/ui/menu` 17 times, `ui/contribution` 9, `core/systemRoles` 6, `core/host` 4, `tracker/songCatalog` 2,
  `tracker/assetCatalog` 2, `tracker/trackerIntegration`, `launchpad/enums`, `core/midiRouting`), seven of them already
  on the list, and 6 cart specifier lines in the five ratchet files plus `uiHost.ts`.

## 2. The decisions

| # | Decision | Chosen | Alternatives not taken |
| --- | --- | --- | --- |
| 1 | The cut | F4 split by layer into F4a, F4b and F4c; F4c is React in packages (F4 decisions 1 and 2) | n/a |
| 2 | Where the React kit lives | A package of its own, `packages/ui-react`, with its React under its own `ts/ui/`; `@retroplug/ui` stays plain TypeScript. Reason: a pack author must not have to use React (user, option C) | The kit inside `@retroplug/ui` at `ts/ui/kit/` with rule 3 unchanged (A); `packages/ui/ts/kit/` plus a `retroplug.surface: "ui"` member (B) |
| 3 | The view contract | Declarative, with React optional: id, label, placement, `when`, and a body of one kind, `pixels`, `readout` or `component`; the shell renders pixels and readout itself (user, option A) | React components only (B); a raw LVGL mount callback (C) |
| 4 | The React half now | Ship `ui-react` and the `component` kind; the shell's pixels and readout renderers are built from `ui-react`, so `test:ui` renders the kit; the component kind is proved by a fixture pack, not rendered (user, option A) | Leave the React half for later (B); `ui-react` without the `component` kind (C) |
| 5 | Typecheck | One setting everywhere plus rules: `jsx: react-jsx` in `tsconfig.base.json`, four React host modules with `types`, the generated paths carry them, a package with views extends the generated host-modules config, rule 3 refuses a `.tsx` outside a `ts/ui/` tree (user, option A) | A separate UI program per package that has React (B) |
| 6 | Who owns keys | Packages contribute their own bindings and the shell routes them: developers will want keys beyond the debug toggle (user, option B) | A shell-owned debug layer that shows and hides every overlay (A) |
| 7 | The key's shape | Contributed app actions in the binding profiles: `{ id, label, keys, pad?, run }`, rebindable in the bindings menus, an id absent from a stored profile taking its default (additive, no migration), two contributions on one key both running (user, option A) | Raw key handlers a package registers (B) |
| 8 | The package's name | `packages/ui-react`, `@retroplug/ui-react`, not "ui-kit": "kit" reads as an LSDj sample kit (user) | `ui-kit` |
| 9 | The overlay's visibility | Each package's own `Readable<boolean>` (section 3.1); backtick toggles each package's flag through its own action, so one press still flips both | One shell-owned flag (decision 6, A) |
| 10 | The prototype model | DeepSeek Flash implements each stage, Opus reviews each stage, as F4a and F4b ran; the parallel kit with `sdl:controller` run alone; the per-stage tree rule of F4b section 11 | n/a |

Decisions the draft makes that the user did not take (each is a detail the contract needed; section 10 lists the ones
worth a second look): `Readable<T>` for `visible` (the brief's `Subscribable<boolean>` does not exist, 10.1); an
`undefined` sample and a `null` frame as "nothing moved" (3.1); the shell reopening a session on a ROM or role change
(5.4); `tagTestId` defined once, in `ui-react` (4.2); a real workspace package `ui-fixture` for the component-view
fixture (8.6).

## 3. The contracts

### 3.1 Views

`@retroplug/ui/contribution` gains the view types; none imports React, and a `component` body is opaque there.

```ts
// @retroplug/ui/contribution (additions)
/** A value a package owns and the shell watches: a view's `visible`, a debug flag. */
export interface Readable<T> { get(): T; subscribe(fn: () => void): () => void }
/** The writable half, so a package's own module can flip what it exposed (createSignal lives beside createSubscribable). */
export interface Signal<T> extends Readable<T> { set(value: T): void }

export interface PixelFrame { readonly pixels: Uint32Array; readonly width: number; readonly height: number }
export interface PixelSession {
  /** The next frame, or null when nothing moved (the shell skips the copy and the invalidate). */
  frame(): PixelFrame | null;
  close?(): void;
}
export interface ReadoutFrame { readonly lines: readonly string[]; readonly playing: boolean }
export interface ReadoutSampler {
  /** The current readout; null = show nothing (an unsupported cart); undefined = unreadable this frame, keep what shows. */
  sample(): ReadoutFrame | null | undefined;
  /** Dedupe key for a frame; omitted = the lines and the flag themselves. */
  signature?(frame: ReadoutFrame): string;
  close?(): void;
}
/** What the shell hands a component view. */
export interface ViewComponentProps { readonly host: UiHost; readonly systemId: number; readonly width: number; readonly height: number }

export type ViewBody =
  | { readonly kind: "pixels"; readonly unsupported?: string; open(host: UiHost, systemId: number): PixelSession | null }
  | { readonly kind: "readout"; open(host: UiHost, systemId: number): ReadoutSampler | null }
  | { readonly kind: "component"; readonly component: unknown };    // @retroplug/ui-react's reactView builds this

interface ViewBase {
  /** Also the view's test id: "lsdj-hd", "lsdj-overlay", "risa-overlay". Unique across the product. */
  readonly id: string;
  readonly label: string;
  /** The systems the view applies to; absent = every system. */
  when?(sys: SystemInfo): boolean;
  readonly body: ViewBody;
}
export interface WindowView extends ViewBase { readonly placement: "window" }
export interface TileView extends ViewBase {
  readonly placement: "tile";
  /** Shown only while true; absent = always. A hidden tile view opens no session and does no work. */
  readonly visible?: Readable<boolean>;
}
export type ViewDef = WindowView | TileView;
```

A window view takes the whole window in place of the grid and is opened by `host.views.open(id, systemId)`; a tile view
is a layer over every tile whose system its `when` accepts. `unsupported` is the text a `pixels` window view shows when
`open` returns `null` (LSDj's "HD view needs a supported LSDj cartridge" is the HD view's own string); a tile view whose
`open` returns `null` draws nothing, as the overlays do today. The unsupported box's test id is `<id>-unsupported`.

`UiContribution` gains `views?: readonly ViewDef[]` and `actions?: readonly UiAction[]` (3.2), and `defineUi` keeps
being the identity function with types. `createSignal<T>(initial)` joins `createSubscribable` in
`@retroplug/ui/menu/subscribable.ts`; it is the one place a flag's `get` / `set` / `subscribe` is written, so the two
overlays' flags are three lines each and the shell's React side reads them with `useSyncExternalStore`.

Why three kinds and not one (what the code the views replace needed, verified):

- **HD** is a `pixels` window view: its session draws into a `Uint32Array` it owns (`LsdjHdCanvas.getPixels()`, the same
  array every frame, `canvas.ts:102`), reports `null` when `flush()` found no dirty tile (`useLsdjHdSession.ts:188`),
  and has a size (`canvas.width`, `canvas.height`) the PixelCanvas needs per `setBuffer` call (`LsdjHdScreen.tsx:42`).
  `PixelFrame` carries the size for that reason.
- **The overlays** are `readout` tile views: three lines of text and a green-or-grey flag, deduped by a signature of
  exactly the displayed fields (`useLsdjRuntime.ts:231`). They need no pixel and no layout beyond the strip.
- **`component`** is what neither can say: a view that wants its own widgets. Nothing in the product uses it in F4c.

What a declarative body cannot express, found while verifying, and what the design does about it:

- **`useFrameSampled`'s `undefined`.** The hook skips a frame when the sample is `undefined` and treats `null` as a
  sample (`useFramePoll.ts:321-326`); the overlays return `undefined` for an unreadable RAM. `ReadoutSampler.sample`
  keeps the three-way answer above rather than collapsing it, and the PixelSession's `null` is "nothing moved" because
  the HD view has no other reading of it.
- **The overlay hides itself at the sampler.** Today an overlay's null render covers both "toggle off" and "state not
  supported" (`LsdjOverlay.tsx:164`: `!visible || !state || !state.supported`). Now `visible` gates the session and a
  `null` from `sample` covers an unsupported cart.
- **The HD screen's full-window box carries the view's test id even when unsupported** (`LsdjHdScreen.tsx:46-49`, the
  `lsdj-hd` box wraps both the canvas and the message); the shell's window slot keeps that.

### 3.2 Actions

```ts
export interface UiAction {
  /** "<package>.<name>": the manifest's package name, a dot, a name. Namespaced because the profile stores it. */
  readonly id: string;
  readonly label: string;               // the bindings menus' row text: "LSDj Debug Overlay"
  readonly keys?: readonly string[];    // default keyboard bindings, by the names the binding profiles use ("`", "F")
  readonly pad?: readonly string[];     // default gamepad bindings, by SDL's button names ("leftstick")
  run(host: UiHost, ctx: { readonly systemId: number | null }): void;   // systemId: the focused system, null with none
}
```

`ctx.systemId` is `stores.project.systems.focused()` when the project has a system and `null` on the start screen. An
action's `run` is called once per matching press; a throw is reported (`console.warn("[actions] <id>: ..."`) and the
other actions of that press still run, as `tickServices` isolates a service (`serviceTicker.ts`).

The two actions F4c ships: `lsdj.debug-overlay` (keys `["`"]`, no pad) and `risa.debug-overlay` (keys `["`"]`, no
pad). A pad default is optional on purpose: the debug overlays have none today, and an action with no default pad row
reads `-` in the gamepad bindings menu, as `Cycle Instances (Back)` does today (`menuDefs.ts:1275`, the `shown` helper).

### 3.3 The host additions

`core/ts/host.ts` gains two members and nothing else; `UiHost`'s group list is unchanged.

```ts
export interface FilesHost { /* ... */ readPrefix(path: string, bytes: number): Uint8Array | null; }   // the first `bytes` bytes
export interface SystemsHost { /* ... */ readRam(id: number): Uint8Array | null; }                  // UI only, see below
```

- **`files.readPrefix`** is every surface's: `filesHost(backend)` in `src/hostGroups.ts:11-20` wraps
  `backend.readFilePrefix` (already in `HostBackend`, `backend.ts:478`), and the CLI host and the render host build
  their `files` through it (`cli/cliHost.ts:22`, `src/render/renderHost.ts:20`), so the three builders gain it with one
  edit. The two hand-written `FilesHost` literals in tests need the member: `core/ts/test/host.test.ts:12` and
  `lsdj/ts/test/songImport.test.ts:47`. LSDj's overlay reads the 0x150-byte header through it; today the same read goes
  through `backend.readFilePrefix` (`useLsdjRuntime.ts:259`).
- **`systems.readRam`** is the UI host's only: `SystemsStore.readRam(id)` exists (`systemsStore.ts:551`) and
  `systemsHost` (`uiHost.ts:112`) gains the one-line member. The render host keeps its `emulator.readRam` (F4b section
  3.3 item 4: the groups are per surface); `systems` is not a group of the render surface, so the member is not there.
  `systems.readSram` is already in the group (the HD view's 15-frame poll, `useLsdjHdSession.ts:148`).
- **`useLsdjHdSession`'s `filesPngHost(backend)` reach into `src/`** becomes the session being handed the host: the
  `files` and `png` groups it needs are the UI host's (`importHost` builds them from the same backend,
  `uiHost.ts:45-50`), so `applyOverridesToRom(base, overrides, host)` takes the host the view's `open` received.
- **`views.open`** keeps its type (`ViewsHost.open(id, systemId): void`, `core/ts/host.ts:249`) and changes its
  meaning (5.1): the shell looks the id up in the generated registry; an unknown id is reported, not thrown.

### 3.4 What is deleted

- `ViewDefinition`, `ViewPlacement`, `Contribution.views` and `Collected.views` in `src/graph/defineExtension.ts`
  (`:27-46`, `:96`, `:132`, `:194`), with the one test that reads them (`test/graph/extension.test.ts:19` and `:46`: the
  fixture extension's `views` entry and the `c.views` expectation, and "and views" in the test's name at `:34`). The
  extension registry keeps nodes, wires, songs and assets. It is a seed the view contract replaces, and a UI-only
  concept does not belong in the control plane's extension model.
- `MenuContext.openLsdjHd` (`menuDefs.ts:89`, with the `uiViews(ctx.openLsdjHd)` in `hostOf` at `:104`) and the
  `openLsdjHd: () => {}` in 14 test files (`test/menu/{asset-file-actions, bliptoaster, launchpad, leaves, lsdj-assets,
  lsdj-songs, n8-sd-ops, recent-rows, recorded-cases, risa, rom-patch, song-guard, song-import}.test.ts`, three more in
  `test/tracker/working-song-save.test.ts`), counted by grep. `beginSongImport` stays (the song-import picker is App's
  React state).
- `uiViews` in `uiHost.ts:31-37` and its `"lsdj-hd"` mapping; the UI host's `views` peer becomes the registry-backed
  `open` of 5.1.

### 3.5 The generated registry

`ui/generated/menus.ts` (written by `renderMenus`, `packages.mjs:756`) gains two exports beside `UI_CONTRIBUTIONS`, in
product order, and the checks of one new section:

```ts
export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS.flatMap(...);
export const ACTIONS: readonly { pkg: string; action: UiAction }[] = UI_CONTRIBUTIONS.flatMap(...);
```

The checks run at load, in the generated module, as every F4b check does (the generator is `.mjs` and cannot read a
TypeScript contribution; `claimRow`, `checkRow` and the others are TypeScript in the generated text, and the generator
tests exercise them through the generated fixture module). They refuse, each naming the package or packages:

1. a view id declared twice (one package or two);
2. a view with a `placement` that is neither `window` nor `tile`, or a body `kind` that is none of the three, or a
   `visible` on a window view (the type refuses the last; the data check backs the type, as `PLACEMENTS` does);
3. an action id declared twice;
4. an action id not prefixed by `<its own package name>.` (the manifest name, `lsdj`, as `UI_CONTRIBUTIONS`' `pkg`
   carries it), so a profile's stored id names its owner;
5. a default key name `resolveKeyName` (`keyCodes.ts:60`) does not resolve, or a default pad name outside the gamepad
   name table (3.6: the table does not exist yet);
6. a default that collides with a shell default: a shell action's (`DEFAULT_KEYBOARD_ACTIONS` /
   `DEFAULT_GAMEPAD_ACTIONS`, `bindingMap.ts:28-29`: `Escape`, `Tab`, `leftshoulder`, `rightshoulder`) or a Game Boy
   button's in `defaultBindingMap()` (`bindingMap.ts:53`: the arrows, `Z`, `X`, `Enter` and the rest, keyboard and
   gamepad), since a key on both would fire the action AND the joypad. Two CONTRIBUTIONS on one key are allowed (the
   overlays' backtick).

The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES` and the two default tables from `../../src/`,
beside the `ROLES` import it already takes from `src/generated/packages`; `ui/` may reach `src/` (section 8.3).

### 3.6 The binding vocabulary the checks need

`resolveKeyName` accepts the named keys of `KEY_NAME_TO_DPF` (`keyCodes.ts:41`) and any single character, which makes
the backtick `"`"` a valid default today. Gamepad button names have NO table in the TypeScript: they are the raw names
the native `GamepadManager` hands the `gamepad-button` bus (`SDL_GameControllerGetStringForButton`,
`native/src/host/input/GamepadManager.cpp:136`), listed only as the defaults in `bindingMap.ts` and the nav names in
`menuNavForButton`. Check 5 therefore needs one new constant, `GAMEPAD_BUTTON_NAMES` in `keyCodes.ts`, SDL's canonical
set (`a b x y back guide start leftstick rightstick leftshoulder rightshoulder dpup dpdown dpleft dpright misc1 paddle1`
to `paddle4 touchpad`). Whether a contributed action may default to a stick token (`leftx+`) is left out of the set on
purpose: the `gamepad-axis` bus does not dispatch app actions (`App.tsx:210-214` handles buttons only), and a default
that could never fire is a bug the check should catch (10.2).

## 4. `ui-react`

### 4.1 The package

`packages/ui-react` is an ordinary package: `private`, `type: "module"`, `exports` (no wildcard) of one entry `.` ->
`./ts/ui/index.ts`, a `tsconfig.json` extending `tsconfig.base.json` AND the generated
`../retroplug/tsconfig.host-modules.json`, a `typecheck` script in lsdj's form (`node ../retroplug/scripts/lib/packages.mjs
&& tsc -p tsconfig.json`, so a fresh checkout generates the paths first), `ts/test/` for its pure cases,
`dependencies` `@retroplug/ui` (the view types `reactView` builds) and `@retroplug/core` (`UiHost`), and a `retroplug`
member of exactly `{ "host": ["react", "react/jsx-runtime", "lvgljs-ui", "lvgljs"] }`. Its React lives under its own
`ts/ui/`, which is what rule 3 admits React into (8.2). It contributes no `app`, `consoles`, `protocol`, `ui` or `cli`
module, so it is not in `retroplug.products.desktop`, as `ui` and `tracker` are not (`contributes()`,
`packages.mjs:150`); the shell declares it in `packages/retroplug/package.json` as `workspace:*` and `pnpm install`
runs without `--frozen-lockfile` (the stage report quotes the lockfile diff).

### 4.2 What moves in, and what it exports

All of these are the shell's own files today, moved with their text unchanged but for imports:

| From (`packages/retroplug/ui/`) | To (`packages/ui-react/ts/ui/`) | Notes |
| --- | --- | --- |
| `lvgl/lv.ts` (139 lines) | `lv.ts` | the typed handles on `lvgljs-ui` (`ViewEx`, `TextEx`, `CanvasEx`, `CanvasHandle`, `LV_IMAGE_ALIGN_CONTAIN`) |
| `lvgl/Box.tsx` (59) | `Box.tsx` | |
| `lvgl/PixelCanvas.tsx` (11) | `PixelCanvas.tsx` | |
| `lvgl/useNativeEvent.ts` (49) | `useNativeEvent.ts` | with its `NativeEvents` table, which mirrors the C++ emit sites by hand |
| `lvgl/useFramePoll.ts` (98) | `useFramePoll.ts` | `useFrameDivider`, `useVersionedPoll`, `useFrameSampled` |
| `hooks/useRerender.ts` (23) | `useRerender.ts` | `useForceUpdate`, `useRerenderOn` |
| `screens/grid/DebugReadout.tsx` (36) | `DebugReadout.tsx` | and `hex2` |

New: `tagTestId.ts` and `reactView.ts`. `tagTestId(testId)` is the ref callback that tags a widget's native uid for
`findByTestId`: `StableSlot.tsx:416` implements it through the shell's typed `native.__rp_tagTestId`
(`host/nativeHooks.ts`), and `DebugReadout` imports `StableSlot` for it, which is why the readout is shell-bound today
(survey B). The kit reads the same optional global itself (`(globalThis as {...}).__rp_tagTestId?.(...)`) and exports
`tagTestId`; the shell's `StableSlot` then imports the kit's, so the function exists once. The shell's `RpHooks` keeps
the `__rp_tagTestId` declaration, which documents the seam for the harness that installs it.

```ts
// @retroplug/ui-react
export { Box, CHROME_RESET } from "./Box";
export { ViewEx, TextEx, CanvasEx, LV_IMAGE_ALIGN_CONTAIN, type CanvasHandle, type LvStyle, /* ... */ } from "./lv";
export { PixelCanvas } from "./PixelCanvas";
export { useNativeEvent, type NativeEvents } from "./useNativeEvent";
export { useFrameDivider, useVersionedPoll, useFrameSampled } from "./useFramePoll";
export { useForceUpdate, useRerenderOn } from "./useRerender";
export { DebugReadout, hex2 } from "./DebugReadout";
export { tagTestId } from "./tagTestId";
export function reactView(Component: ComponentType<ViewComponentProps>): ViewBody;   // { kind: "component", component }
```

`reactView` is the typed way to build a `component` body: `@retroplug/ui` types the body's `component` as `unknown`, so
a pack author cannot hand the shell a function with the wrong props. A pack that draws pixels or text never imports this
package.

Shell-bound and staying in `packages/retroplug/ui/`: `StableSlot`, `useWindowSize`, `FocusProvider` / `useFocusGroup`,
`host/nativeHooks`, `stores/` (`useStores` needs `AppStores`), and `useStateRef` (only `Menu.tsx` uses it; moving it
would be speculative). The shell imports the kit from `@retroplug/ui-react`: 7 + 5 + 2 + 10 + 6 + 4 + 2 import
sites in about 22 files change their specifier and nothing else.

## 5. The shell side

### 5.1 The window slot

`App.tsx`'s `hdSystemId` (`:73`) becomes `openView: { id: string; systemId: number } | null`.

- **Opening.** `createUiHost(stores, peers)` takes `views: ViewsHost` as a peer today (`uiHost.ts:43`); App now builds it:
  `open: (id, systemId) => { const view = windowViewOf(id); if (!view) return report(...); setOpenView({ id, systemId }) }`,
  where `windowViewOf` reads the generated `VIEWS` through `menuContributions.ts` (`viewById(id)`, beside `roleRows()`
  and `uiServices()`). An unknown id, a `tile` view's id, and a system for which the view's `when` is false are
  reported (`console.warn("[views] ...")`) and open nothing; the report is the same shape as `firstWinsMenuIds`'
  (`menuContributions.ts`). `when` is checked at open so the unsupported box stays about a cart the view applies to but
  cannot read (a supported role on an unsupported version), not about a system it was never meant for (10.9).
- **Closing.** Esc closes it where it does now (`App.tsx:202`: after the overlay check, before the key map, so Esc is
  still the universal back); the stale-system clear (`:150-152`) becomes generic over `openView.systemId`.
- **Rendering.** The full-window branch (`:314-318`) renders `<ViewWindow view={...} systemId width height />`
  (5.3), keeping the order: overlay and empty branches first, the stale-id fall-through to the grid, and
  `useGameInput` / `useGamepadInput` live underneath (`:295-296`: they are keyed on `playing`, which the window view
  does not touch).

### 5.2 The tile slots

`EmulatorTile` mounts each tile view whose `when` passes for its system, in generated order, each in its own
`StableSlot` (the `insertChildBefore` gotcha, AGENTS.md "Framework gotchas"): a view that appears and disappears (a
`visible` flag, a session that opens late) would otherwise land at the END of the tile's LVGL child list, over the
focus border and the render badge. The slots sit where the two overlay lines are now (`EmulatorTile.tsx:84-90`), after
the dim and before the border; the slot's test id is `view-slot-<id>` (distinct from the view's own id, which tags the
readout strip inside it). A slot is the tile's size with `background-opacity: 0`, and the readout inside aligns to its
bottom middle exactly as it does to the tile's, since the two have the same size.

This changes one thing: today the z-order depends on mount history. An overlay toggled ON after the border and the
badge mounted is appended last, over them; a border or badge that mounts after the overlay (a focus move, a render
starting) is appended over it in turn. In a slot the strip has one fixed place, under both. Where the two overlap (a
render badge and an overlay, both at the tile's bottom) the badge now always draws over the strip. Section 7, E4.

A slot is a full-tile transparent `Box`, like the dim layer that already covers every unfocused tile
(`EmulatorTile.tsx:77-83`), through which a click still reaches the tile's `onClick` (that is how an unfocused tile is
focused today). Stage 2 checks that a slot passes the click the same way (`test:ui`'s focus-by-click case with an
overlay showing).

### 5.3 Three renderers

The shell holds one renderer per body kind, in `ui/views/`, each built from `@retroplug/ui-react`:

- **`PixelView`**: `useNativeEvent("frame")` -> `session.frame()` -> `PixelCanvas.setBuffer`, letterboxed by the canvas's
  CONTAIN alignment as today. It passes `pixels.buffer` when the array is the whole buffer and `pixels.slice().buffer`
  when it is a view onto a larger one: the HD canvas's array is its own (no copy per frame, as now,
  `LsdjHdScreen.tsx:42`), and an emulator frame is a subarray of the RPC wire buffer (`EmulatorTile.tsx:64-66` copies
  for that reason). `EmulatorTile` itself stays: its frame comes from `backend.getFrame` with a `published` flag, which
  is not a view.
- **`ReadoutView`**: `useFrameSampled(sample, signature)` -> `DebugReadout` with the tile's width and the view's id as
  its test id. The hook's memoised `sample` is the opened sampler's `sample`; its signature is the sampler's, or the
  lines and the flag.
- **`ComponentView`**: mounts the body's component with `{ host, systemId, width, height }` and nothing around it. The
  one cast in the shell lives here (`body.component as ComponentType<ViewComponentProps>`).

Test ids as today: a window `pixels` view tags its full-window box with the view id and its message box
`<id>-unsupported` (`lsdj-hd`, `lsdj-hd-unsupported`); a readout tags the `DebugReadout` strip (`lsdj-overlay`,
`risa-overlay`).

### 5.4 Sessions: opened by the shell, held by no one else

A pure-TypeScript controller owns the lifecycle so it can be tested without React: `ui/views/viewSession.ts` exports
`openSession(view, host, systemId)` (calls the body's `open`, catches a throw and reports it, returns the session or
`null`) and `closeSession(session)` (calls `close?.()`, catches). The three renderers call them from one effect:

- **open** on mount, and again when the system's ROM path or `roles` change (the renderer reads `useSystem(systemId)`,
  as `useLsdjRuntime` does, `:251`), and again when a tile view's `visible` flips to true; **close** on unmount, on the
  change that reopens it, and when `visible` flips to false. A package holds no React state: a session is a plain
  object the shell owns.
- The reopen key is `(view.id, systemId, romPath, roles)` by identity, which is the brief's. It is wider than today's
  hooks: `useLsdjRuntime` rebuilds on `isLsdj` (a boolean) and `romPath`, `useRisaRuntime` likewise, and the HD session
  on the `lsdj-assets` config object, so a role edit that touches none of those leaves them alone, where a `roles`
  identity change reopens every view of that system. `SystemsStore.setRoleConfig` replaces `roles` on a change
  (verified by reading `view()`, `systemsStore.ts:208-224`, which hands `e.roles` through; whether every write replaces
  the array is read from the setter and is a stage 2 check). The cost is one header read per LSDj overlay and one
  whole-ROM read per risa overlay on an edit made while an overlay is showing (the instance menu is open, so the tile is
  unmounted: the tile and its sessions close with it, so in practice the reopen happens when the menu closes, as it
  does today). Recorded as E5 and as a question (10.4).
- **Hidden work.** A hidden tile view opens no session (`visible` false means the renderer renders `null` and the
  effect does nothing), so a hidden overlay does the same zero work as `useXRuntime(id, false)` does now.

### 5.5 Keys: a list of actions per key

`buildKeyToAction(actions)` and `buildGamepadToAction(actions)` (`keyCodes.ts:139`, `:155`) change from
`Map<number, AppAction>` / `Map<string, AppAction>` to `Map<number, string[]>` / `Map<string, string[]>`: an action id
list, in the order the profile (then the registration order) gives them. A key bound to the shell's `OpenMenu` and to a
contributed action fires both. Unknown ids are still skipped; "known" is now the shell's three plus the product's
contributed ids (a stored id of an action no product registers is ignored, as an unknown one is today).

```ts
// ui/input/appActions.ts (new): the shell's dispatch over both kinds.
const shell: Record<AppAction, () => void> = { OpenMenu, CycleNext, CyclePrev };      // App's runAction bodies
function runActions(ids: readonly string[]): void {
  for (const id of ids) {
    if (id in shell) shell[id as AppAction]();
    else contributed.get(id)?.run(uiHost, { systemId: focusedOrNull() });             // each in a try: reported, then on
  }
}
```

`App.tsx`'s key handler keeps its order (`:197-207`): Esc is the universal back, then `if (overlay) return`, then the key
map; the backtick line (`:205`) and the `KEY_BACKTICK` constant (`:48`) go, the contributed action taking its place in
the map. Contributed actions fire in the same states backtick does today: any state without a full-window overlay,
whether the menu is open or not, and under an open window view (so backtick toggles an overlay while the HD view shows).
OpenMenu's `isMenuModalActive` deferral (`toggleMenuCore`, `:180`) is its own and is not applied to contributed actions,
which matches backtick's behaviour now (a capture row armed in the bindings menu, or a text prompt, still passes a
backtick to the toggles) and is one of the open questions (10.6). The gamepad handler (`:210-214`) dispatches the same
way. The three tests that read the old maps (`test/input/keymap.test.ts` 6 call sites, `test/input/gamepad.test.ts` 6,
`test/menu/leaves.test.ts` 3) change from a value to a one-element list; nothing else reads them.

### 5.6 The profile default fill

A stored profile has no entry for a contributed id until the user edits one. The fill is a pure function in
`src/bindingMap.ts`, `withActionDefaults(map, actions)`, taking `{ id, keys, pad }[]` and returning the map with every id
absent from `keyboardActions` / `gamepadActions` set to the action's default (`keys ?? []`, `pad ?? []`); an id PRESENT
in a section is the user's, including `[]` (a cleared binding is a choice, as `CyclePrev`'s `[]` is). It touches no file:

- `src/bindingsStore.ts` is unchanged: `resolvedBindings()` (`:209`) still returns the stored model, so the store knows
  no contribution and `src/` imports nothing from `ui/`. `loadProfile` is raw, and the editor's `write` (`menuDefs.ts:1215`)
  keeps loading raw and saving only what the user changed, so the file is not rewritten until an edit and an edit does
  not bake the defaults in.
- The two readers fill: `App.tsx` calls it on `bindings` before building the maps (`:115-116`), and `bindingsChildren`
  calls it on `ctx.bindings` before reading `actMap` (`menuDefs.ts:1206`). Both read the contributed list from
  `menuContributions.ts` (`contributedActions()`).
- A profile that already maps a contributed id (written by the editor after F4c, or hand-written) keeps its own mapping
  (E6). `Reset <Keyboard|Gamepad> to Defaults` (`menuDefs.ts:1292`) writes the section from `defaultBindingMap()`, which
  holds the shell's three; the contributed ids it leaves out read their defaults again through the fill, which is the
  same result as resetting them.
- Why not the store: `AppStores` is shared by the control plane and the UI (`appStores.ts`), and the contributions are
  UI-only (the generated module sits under `ui/`), so a registry in `src/` would need the UI to push the list in at
  mount, with the menu tests (which compose stores alone, `recorded-cases.ts:87`) not doing it. A pure function at the
  two readers needs no registration.

### 5.7 The bindings menus

`bindingsChildren` builds `actionRows` from `APP_ACTION_ROWS` (`menuDefs.ts:1178`: the shell's three); it appends one
row per contributed action after them, in registration order, with the same capture plumbing and the same id scheme:

| Menu | Row id | Label with the default | Label when unbound |
| --- | --- | --- | --- |
| Keyboard Bindings | `bind-act-lsdj.debug-overlay` | `LSDj Debug Overlay: ` followed by the backtick | `LSDj Debug Overlay: -` |
| Keyboard Bindings | `bind-act-risa.debug-overlay` | `risa Debug Overlay: ` followed by the backtick | `risa Debug Overlay: -` |
| Gamepad Bindings | `bind-gp-act-lsdj.debug-overlay` | `LSDj Debug Overlay: -` (no pad default) | same |
| Gamepad Bindings | `bind-gp-act-risa.debug-overlay` | `risa Debug Overlay: -` (no pad default) | same |

The labels follow the neighbouring rows' style (a title-cased phrase, then the bound names) and the cart names as the
tracker listing spells them ("LSDj", "risa", `risa/ts/app.ts:19`). The backtick shows as itself (`keyDisplayName`
passes a single printable character through, `keyCodes.ts:93-97`). Each channel's menu gains two capture rows
(11 -> 13 capture rows; the "GB buttons + 3" in the two test comments become "+ 5"), nothing else moves, and the keyboard
menu's `navTo("New Profile")` in `test-ui/bindings.test.ts` (24 steps allowed) still reaches it: from `A:` the path grows
from 9 steps to 11.

## 6. The moves

### 6.1 LSDj (`packages/lsdj/ts/ui/`, plain TypeScript)

- **`hdView.ts`**: a `WindowView`, `id: "lsdj-hd"`, `when: (sys) => hasRole(sys.roles, "lsdj-sync")`, body `pixels` with
  `unsupported: "HD view needs a supported LSDj cartridge"`. `open(host, systemId)` is `buildSession` of
  `useLsdjHdSession.ts:193-214` unchanged in what it reads and when: the 0x150 header through `host.files.readPrefix`,
  `LsdjReader.fromHeader`, the whole ROM through `host.files.readFile`, the lsdj-assets overrides folded in through
  `applyOverridesToRom(base, overrides, host)` (the host's `files` and `png`, replacing `filesPngHost(backend)`), and
  the `HdSession` class moved as it is, with `backend.readSram` -> `host.systems.readSram` (every 15 frames, the same
  FNV signature over the first 0x8000 bytes) and `backend.readRam` -> `host.systems.readRam`. `frame()` is
  `renderFrame()` of `:180-189` returning `{ pixels, width: canvas.width, height: canvas.height }` or `null`. The
  system's `lsdj-assets` config comes from `host.systems.view()` by id (the role config is `roleConfig(sys.roles,
  "lsdj-assets")`, `systems.view()` being the group's existing member). The "HD Player" row keeps
  `host.views.open("lsdj-hd", sys.id)` (`lsdj/ts/ui/index.ts:253-255`).
- **`overlayView.ts`**: a `TileView`, `id: "lsdj-overlay"`, `when` the same, `visible: lsdjDebug` (below), body `readout`.
  `open(host, systemId)`: `LsdjReader.fromHeader(host.files.readPrefix(rom, 0x150))`, null if unreadable or
  `!reader.supported`; the sampler's `sample()` is `host.systems.readRam(systemId)` -> `reader.read(wram)` ->
  `null` when `!state.supported` -> `{ lines, playing: state.playing }` with the three lines of `LsdjOverlay.tsx:170-175`
  (`versionStr`, `channelCell`, `hex2` move with it; `hex2` is the kit's `DebugReadout` helper today and becomes a
  six-line copy in the package, since a plain-TS package cannot import a React package's helper for a string format);
  `signature` is `useLsdjRuntime.ts:231` over the frame, which the lines cover by construction (the signature excludes
  `chainRow`, which the lines do not show either).
- **`debugOverlay.ts`**: `export const lsdjDebug = createSignal(false)` and the action `{ id: "lsdj.debug-overlay",
  label: "LSDj Debug Overlay", keys: ["`"], run: () => lsdjDebug.set(!lsdjDebug.get()) }`. Not persisted, off at start, as
  `lsdjDebug.ts`'s header says it must be. `lsdj/ts/ui/index.ts`'s `defineUi` gains
  `views: [hdView, overlayView], actions: [lsdjDebugOverlay]`.

### 6.2 risa (`packages/risa/ts/ui/`)

The same two files for `risa-overlay` and `risa.debug-overlay` (`keys: ["`"]`): `open` is `useRisaRuntime.ts:310-320`
(`host.files.readFile(rom)`, `runtime.identifyRisaVersion`, `resolveRisaLayout`, null when no layout), the sampler is
`decodeRisaState(ram, layout)` through `host.systems.readRam`, and the lines, `trackCell` and the 4X tempo rule are
`RisaOverlay.tsx:203-211` verbatim. `when` is `hasRole(sys.roles, "risa")`, which is the scanner's one `ui/` hit
(`useRisaRuntime.ts:306`) leaving the shell. `risa/ts/ui/index.ts` becomes `defineUi({ trackers: [RISA_TRACKER_UI],
views: [overlayView], actions: [risaDebugOverlay] })`. Its package `tsconfig.json` extends the generated host-modules
config (the generator enforces it for a package with views, 8.1); lsdj's already does.

### 6.3 What leaves the shell

`ui/screens/hd/` (both files), `ui/screens/grid/{LsdjOverlay, RisaOverlay, useLsdjRuntime, useRisaRuntime, lsdjDebug,
DebugReadout}`, `uiViews`, `MenuContext.openLsdjHd`, `src/graph/defineExtension.ts`'s `ViewDefinition`, and the
backtick code in `App.tsx` (`:34`, `:48`, `:205`). After it `ui/` outside `generated/` names no cart.
What arrives: `ui/views/{viewSession.ts, PixelView.tsx, ReadoutView.tsx, ComponentView.tsx, ViewWindow.tsx,
TileViews.tsx}` and `ui/input/appActions.ts`.

## 7. Behaviour edges

None changes audio, a saved file or a graph document. Each is listed with what was found.

- **E1. The bindings menus gain a row per contributed action** (5.7): `LSDj Debug Overlay` and `risa Debug Overlay` in
  each of Keyboard Bindings and Gamepad Bindings, after `Cycle Instances (Back)`. The recorded menu matrix holds the
  bindings menus in every case (the 45 ids of each of the 35 cases in `test/menu/recorded-matrix.json`), so its diff is
  four new items per case (two per menu) and nothing else; it is re-recorded in stage 4, when the two real actions
  arrive (stage 3's action is a test one and adds no product row), and the diff is the record.
  Two tests change: `test/menu/leaves.test.ts:929` and `:987` (the capture-row counts, 11 -> 13).
- **E2. Backtick is rebindable per overlay** (the point of decision 7). With the defaults a press still flips both
  overlays, as before; a user who rebinds or clears one has two independent toggles. The initial state is off for both.
- **E3. A stored profile that already maps a contributed id keeps its own mapping** (5.6). An older profile has no such
  id and reads the default; the file is not rewritten until an edit.
- **E4. The overlay's z-order against the focus border and the render badge** (5.2): it depended on which mounted last;
  in its slot the strip always sits under both. Only where the strip overlaps the badge (both are at the tile's bottom)
  can it be seen. Verify by `pnpm screenshot`.
- **E5. A role edit reopens the views of that system** (5.4): wider than the old hooks' dependencies, with no visible
  effect while the instance menu hides the tile.
- **E6. `host.views.open` of an unknown id, a `tile` view's id, or a system the view's `when` rejects is reported and
  opens nothing**; before, an unknown id was ignored silently (`uiHost.ts:31-37`).
- **E7. A contributed action that throws is reported and the rest of its press still runs**; before, `toggleLsdjDebug`
  could not throw.
- **E8. `extension.test.ts` loses its views expectation** with the seed (3.4); no production code changes for it.

Not edges, and why: the HD player's reads, its 15-frame poll and its frame skip are the old session's, moved; the
overlays' lines are the old lines; the Esc and game-input behaviour under the HD view is the old behaviour (5.1); the
window and tile test ids are the old ones.

## 8. Proof

### 8.1 The typecheck

- **`tsconfig.base.json`** gains `"jsx": "react-jsx"`. It changes what `tsc` accepts and nothing a bundler emits:
  `tools/ts-options.js` names the compiler options each build uses (its header says why), and the UI build passes
  `jsx: "automatic"` itself (`tools/build-ui.js:31`). Stage 1 proves it by hashing `build/ui/bundle.js` before and after.
- **`host-modules.json`** gains four entries, `surface: "ui"` each, with `types`: `react` and `react/jsx-runtime` ->
  `../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react` (and `/jsx-runtime`), `lvgljs` ->
  `../../deps/dpf.js/runtime/lvgljs/index.ts`, `lvgljs-ui` -> `ui/types/lvgljs-ui.d.ts` (the shell-owned 53-line slice
  stays where it is). They are exactly `tsconfig.ui.json`'s four hand-written paths.
- **Where each generated path lands.** The generator writes the same entries to both files it writes today
  (`renderHostModules`, `renderShellPaths`, `packages.mjs:978-1012`): `tsconfig.host-modules.json`, extended by every
  package with `examples` and now every package with `views` and `ui-react` itself; and `tsconfig.paths.json`, the shell
  program's (the host modules plus `ui-harness` from `tsconfig.aliases.json`). `paths` REPLACE across `extends` (F4b
  section 5.4), which is the reason there are two files, and is why `tsconfig.ui.json` could not extend the shell's
  config and keep `ui-harness`: it no longer has a `paths` of its own, so it inherits the whole of `tsconfig.json`'s and
  is reduced to `{ "extends": "./tsconfig.json", "include": ["ui"] }`. Whether to fold it into `tsconfig.json`
  (`include` gains `ui`) now that the two differ only by that line is left out (10.12).
- **What each program needs.** The shell's program already includes `../*/ts`, so `packages/ui-react/ts/ui/*.tsx` enters
  it, and it needs `jsx` (base) and the four paths (`tsconfig.paths.json`); it also type-checks dpf.js's
  `runtime/lvgljs/index.ts` as source, which only `tsconfig.ui.json` did before (a cost, and a place a strict-mode error
  in code we do not own would appear: unverified, a stage 1 check, 10.13). Each package program (`pnpm -r typecheck`)
  that imports `@retroplug/ui-react` or names a React module needs the generated host-modules config; the generator
  refuses a package with views whose `tsconfig.json`'s parsed `extends` does not name it, by the check F4b wrote for
  examples (`packages.mjs:1447-1462`, generalised from "has examples" to "has examples or views or declares a React host
  module"). risa's `tsconfig.json` does not extend it today and gains the line in stage 4.
- **The UI program** (`tsc --noEmit -p tsconfig.ui.json`) keeps its role: `ui/` plus what it imports.

### 8.2 Rule 3

- **A `.tsx` outside a `ts/ui/` tree is refused** (`check-boundaries.mjs`, in the import walk, next to the host-module
  branch at `:660-690`): a package file ending `.tsx` whose `surfaceOf(pkg, file)` is not `ui`. The shell is not judged
  by rules 1 to 8, so its `ui/` is admitted by construction. The message names the file and the tree.
- **A `.tsx` is a use of `react/jsx-runtime`.** Automatic JSX imports it implicitly, so no specifier in the source
  names it and the scanner never sees one. The rule adds it: a package with any `.tsx` must declare
  `react/jsx-runtime` in `retroplug.host`, and a `.tsx` counts as that declaration's use (so the unused-declaration
  warning does not fire for it). Without this a view that only uses JSX would pass the declaration check it was meant
  to fail.
- **The four modules are admitted only from `ts/ui/`**: `surfacesOf` already reads a list or a name, and the new entries
  carry `surface: "ui"`; `surfaceOf` already maps `ts/ui/` to `ui`. `retroplug-cli` stays `cli`. The error text names the
  trees as it does (`:683`).
- RED first: a copy of `ui-react`'s shape with the `.tsx` under `ts/` (refused), one without the declaration (refused),
  one declaring them with the `.tsx` in `ts/ui/` (accepted), and a `ts/cli/` file importing `react` (refused as a
  surface error).

### 8.3 Rule 9 over `ui/`

`roots` (`check-boundaries.mjs:799-803`) gains a third: `{ dir: ui, ignores: [packages/retroplug/ui/generated/], reaches:
[ui, src] }`. `ui/` may reach `ui/` and `src/` (it is the shell; `src/generated/` is reachable by a relative import as
it is from `cli/`); a relative import that lands anywhere else (`cli/`, `test*/`, `tools-src/`, another package's tree)
is refused with the existing `shell-source` message. `ui/generated/` is not judged: it is the one place that names every
package, as `src/generated/` is.

Its bare `@retroplug/*` imports are judged against a list of its own, `UI_IMPORTS`, and not against `SHELL_IMPORTS`: the
kit and the view contract are the UI's alone, and `src/` and `cli/` must not reach them. Each entry carries its reason, an
entry no `ui/` file uses fails the check (as `SHELL_IMPORTS`' do, `shellStale`), and the ten after the moves are:

| Specifier | Reason |
| --- | --- |
| `@retroplug/ui/menu` | the menu model every composer and the Menu draw |
| `@retroplug/ui/contribution` | the contribution types the generated registry and the composers read |
| `@retroplug/ui-react` | the kit the shell's own components and the three renderers are built from |
| `@retroplug/core/host` | the host contract the UI host implements |
| `@retroplug/core/systemRoles` | the role contract the UI reads a system's roles through |
| `@retroplug/core/midiRouting` | the routing model the MIDI settings menu edits |
| `@retroplug/launchpad/enums` | the controller target enum the UI host validates a package's target against |
| `@retroplug/tracker/songCatalog` | the song catalog the shell's Songs menu reads through the integration |
| `@retroplug/tracker/assetCatalog` | the asset catalog the shell's Assets menus read |
| `@retroplug/tracker/trackerIntegration` | the integration contract the shell resolves a cart's tracker by |

That is the nine specifiers `ui/` names today less the cart ones, plus `ui-react`; the final list is what the RED run
shows (the exact set depends on which of `core/systemRoles`' six uses stay, 10.14). The RED run is rule 9 over `ui/` on
the tree before the moves: it must name the six cart specifier lines of section 1 and nothing else. The F4b ratchet
(`test/boundaries/ui-cart-imports.test.ts` and `run-ui-cart-imports.mjs`) is deleted with its fixtures
(`fixtures/ui-relative`, `fixtures/ui-device` become `shell-ui-forbidden` and `shell-ui-relative` for the rule); the
allow-based rule covers what the ratchet's derived guard list did (a device pack with no app module is guarded because
nothing but the list is allowed). `SHELL_IMPORTS` loses its `export` only if nothing else reads it (the ratchet's runner
did).

### 8.4 The literal scanner over `ui/`

`run-scan.mjs --dirs` takes `src,cli,ui` (`test/source-literals/no-cart-literals.test.ts:118` passes `src,cli`); it
already skips any `generated` directory (`run-scan.mjs:112`) and so skips `ui/generated/`. RED first: over the tree
before the moves it finds the one hit F4b recorded, `useRisaRuntime.ts`'s `"risa"` (a core id / tracker id), and a new
fixture `literals-ui` pins the walk of the third directory. After the moves the shell's `ui/` has none. The scanner stays
blind to what F4b listed (device ids, node types of the surface nodes, non-console role kinds, identifiers).

### 8.5 The bundle check

`test/script/bundle-ui-split.test.ts`'s `isUi` (`:65-68`) matches `/packages/ui/` (the whole package) and
`/packages/*/ts/ui/`; `/packages/ui/` does not match `packages/ui-react/` (the slash follows `ui`), but every file of
the kit sits under `packages/ui-react/ts/ui/`, which the second pattern covers. The pattern is nevertheless widened to
`/\/packages\/ui(-react)?\//` so the kit's other files (its entry, a future `ts/` file) count too. The control plane,
the render worker, the script nodes, the SDK, the dispatcher and the planner must carry no `ui-react` module, nor a
package's views (they are in `ts/ui/`); the UI must still carry no CLI one. The component fixture package
(`packages/ui-fixture/`, 8.6) is asserted ABSENT from the UI bundle's inputs: listing nothing is what keeps it out, and
the assertion would catch a product list that picked it up.

### 8.6 The fixture pack

A real workspace package, `packages/ui-fixture` (`@retroplug/ui-fixture`; the name is the draft's, 10.10), private, listed
by no product, with `retroplug.ui: "ts/ui/index.ts"` and `retroplug.host` of the four React modules, a
`tsconfig.json` extending base and the generated host-modules config, and `ts/ui/{index.ts, Badge.tsx}`: a tile view
`{ id: "ui-fixture-badge", placement: "tile", body: reactView(Badge) }` whose component draws a `Box` and a `Text`
through `@retroplug/ui-react`. What it proves, and where:

- **it typechecks**: `pnpm -r typecheck` over a `.tsx` that uses `reactView`, `Box` and JSX (stage 1);
- **rule 3 passes** with `retroplug.host` declaring the modules, and **a copy without the declaration, and a copy with
  the `.tsx` outside `ts/ui/`, are refused** (boundary fixtures, 8.2);
- **the generator accepts it**: a generator fixture tree (`test/packages/fixtures`) lists a package shaped like it and
  its expected `menus.ts` is held; a second fixture per refusal of 3.5 (two view ids, two action ids, a foreign prefix, an
  unknown key name, an unknown pad name, a collision with a shell default);
- **it is never bundled into the product**: no product lists it, the UI bundle's inputs do not contain it (8.5), and
  `pnpm check:boundaries` and the native boundary check both accept an unlisted package with a `ui` member
  (unverified, stage 1: nothing scanned so far is known to walk every `packages/*/package.json`).

**What is not proved, said plainly.** The UI test binary embeds the production bundle (`test-ui/uiHarness.ts` bundles the
tests, not the UI), so no `test:ui` case can render a `component` view until a real package ships one. The
`ComponentView` renderer is a mount and a cast; it is covered by typecheck, by the controller's lifecycle test (a fake
component body whose `open`-equivalent is the mount) and by reading, not by pixels.

### 8.7 The oracles held, and the new tests

**Nothing is re-recorded but the edge:** the parity baseline (197 cases), the kit and render baselines, the device image
hashes and pack bytes, the help texts, the battery listing, the recorded menu matrix except its bindings menus (E1),
`test:ui`'s three view tests as they stand (`lsdj-hd`, `lsdj-overlay`, `risa-overlay`: the same test ids, texts,
letterbox checks, Esc and backtick), BlipToaster and NESVJ. The UI bundle's hash is compared at stage 1 (no change
expected) and its size reported at each stage.

New tests, by stage (a RED run first wherever the old code can be run):

1. **Stage 1.** The rule 3 cases of 8.2; the typecheck of the fixture pack; the bundle assertion of 8.5. A shell test
   that every moved module is re-exported by the kit and the shell imports none from the old paths (a grep-free import
   walk, as the boundary check does it).
2. **Stage 2.** `ui/views/viewSession` over a fake host and fake views, in `pnpm test` (the mock tier, no React): opened
   once per `(view, system, rom, roles)`, reopened on a ROM change and a role change, closed on a visibility flip and
   on unmount, a throwing `open` reported and the system left unsessioned, a hidden tile view opening nothing. The
   generator refusals 1 to 3 of 3.5 and the generated registry's order. `files.readPrefix` over the three builders
   (the host-groups test enumerates the groups, `test/hosts/host-groups.test.ts`), and `systems.readRam`. A
   production-bundle `test:ui` case is not possible for a test view (8.6), so the rendering of the three renderers is
   proved in stage 4 on the real views.
3. **Stage 3.** The generator refusals 4 to 6; `withActionDefaults` (absent id filled, present id kept, `[]` kept); the
   key maps over lists (two actions on one key both returned, an unknown id skipped); the dispatch order and a throwing
   action isolated; the bindings menus' new rows (ids, labels, capture plumbing, reset); `test:ui`'s `app-actions`
   and `bindings` cases unchanged and green; the matrix unchanged (no product action exists yet: the new rows are
   proved over a test action in the unit tests).
4. **Stage 4.** Per package, pure-TS cases over a fake host in `packages/lsdj/ts/test` and `packages/risa/ts/test`,
   RED first against the old hooks where they can be run: the HD session's cadence (a `readSram` call every 15 frames
   and no more, a decode only when the signature moved, the `null` frame when nothing is dirty) and the overlays' lines
   from a synthetic RAM image (the existing `runtime.test.ts` layout and header builders feed them). A frame's PIXELS
   need the real ROM's fonts (`canvas.ready` is false until a font is set, `LsdjHdCanvas`), so the pixel-level case
   reads the fixture ROM and SKIPS without it, like the three `test:ui` view tests (CI runs none of them). The
   `test:ui` cases for the view lifecycle on a real cart (an overlay survives a tile swap to the menu and back; the HD
   view is closed when its system is removed) and the action dispatch (both overlays toggle on one press; rebinding one
   leaves the other) are fixture-gated for the same reason.
5. **Stage 5.** Rule 9 over `ui/` and the scanner over `ui/`, each RED first on the pre-move tree (8.3, 8.4); the
   ratchet deleted; the docs.

## 9. The stages

Five stages on a prototype branch, DeepSeek Flash implementing and Opus reviewing each before the next (F4a's and F4b's
routing). Each stage leaves the tree green on every oracle of 8.7 it can reach; stage runs use
`.superpowers/verify/tip-verify-par.sh` with `sdl:controller` run alone. After any fix-pass rebase, each stage's tree
is extracted after the final rewrite, proven equal to its commit, and typechecked and tested alone (F4b section 11:
a fix pass that rebased a stage 3 hunk into stage 2 broke the stage 2 tree).

1. **`ui-react` and the typecheck.** The package with the widgets moved and the shell importing them; `jsx` in the base;
   the four host modules and their generated paths, `tsconfig.ui.json` reduced; the generator's views-extend check; rule
   3's `.tsx` refusal and the implicit `react/jsx-runtime` use; the bundle pattern; `ui-fixture`; `tagTestId` once. *Exit:*
   no behaviour change; `pnpm typecheck` green in all programs; the UI bundle hash unchanged; `test:ui` unchanged; the
   rule 3 cases RED then green; `check:boundaries` and the native boundary check accept the new packages.
2. **The view contract.** `Readable` / `createSignal`, the view types, `files.readPrefix` and `systems.readRam`, the
   generator's `VIEWS` and its refusals 1 to 3, the shell's three renderers, the window and tile slots, the generic
   `views.open`, `viewSession`, `ViewDefinition` deleted. A test view (in the unit tests only) proves the lifecycle;
   the production views are still the shell's. *Exit:* the lifecycle tests green; `lsdj-hd`, `lsdj-overlay` and
   `risa-overlay` unchanged on the old code; the matrix unchanged.
3. **Actions.** The contract, the generator's `ACTIONS` and refusals 4 to 6 with `GAMEPAD_BUTTON_NAMES`, multi-dispatch
   keys, `withActionDefaults`, the bindings menus' rows. The shell still has its own backtick path until stage 4, so the
   stage ships with a TEST action only (in the unit tests, never in a product). *Exit:* the matrix unchanged; the key
   tests updated; `app-actions` and `bindings` green.
4. **The moves.** The LSDj HD player, overlay and action, the risa overlay and action; the shell's view files,
   `lsdjDebug`, `uiViews` and `openLsdjHd` deleted; the sessions tested over a fake host; the matrix edge (E1)
   re-recorded here. *Exit:* the three `test:ui` view tests green with the fixtures and unchanged; E1 recorded and its
   diff reviewed as the four items per case; `ui/` imports no cart.
5. **Rule 9 and the scanner over `ui/`, the ratchet retired, the docs.** By stage 5 the shell is already clean, so the
   RED for each check is a fixture tree of the pre-move shape (8.3, 8.4) rather than the live tree; the docs pass is `AGENTS.md`
   (the ratchet paragraph, the rule 3 and rule 9 text, the F4b "five files" sentences), `spec/06-build-test.md`'s "Adding
   a package" gaining "a view" and "an action" and the scanner's `ui/` sentence (`:220`), `spec/07-remaining-work.md`
   (the F4b bullet and the F4c entry), `docs/package-split.md`, `spec/00-overview.md:94` (the `ui` row and a `ui-react`
   row), and this document's as-built section later. *Exit:* `check:boundaries` green with the lists as 8.3 gives them;
   every oracle of 8.7; BlipToaster, NESVJ, the Reaper suite and both SDL legs.

## 10. Risks and open questions

Found against the code. Items 4, 6, 7, 9 and 10 carry a controller decision (in
`.superpowers/f-split/decisions.md`, "Controller decisions (F4c draft)"); the rest stay open for the prototype to settle
by reading the code.

1. **`visible?: Subscribable<boolean>` does not typecheck as written.** `Subscribable` (`ui/ts/menu/subscribable.ts:10`)
   is non-generic, has no value, and is `{ emit, subscribe }`. The draft adds `Readable<T>` / `Signal<T>` (3.1) and
   keeps `Subscribable` as it is. If the user wanted `Subscribable` itself generalised instead, `createSubscribable`'s
   callers would see a type parameter they do not use.
2. **No gamepad button name table exists** (3.6), so the brief's "a pad name the binding tables do not know" has nothing
   to check against. The draft adds `GAMEPAD_BUTTON_NAMES` (SDL's set); the open part is whether a stick token (`leftx+`)
   may be an action default (the draft: no, the axis bus does not dispatch actions).
3. **The refusals are load-time throws in the generated module, not the generator.** The generator is `.mjs` and reads
   manifests; an action id, a view id and a key name live in TypeScript. The draft keeps F4b's mechanism (the generated
   module checks at load, the generator tests run it), which is also where F4b's `claimRow` and `checkRow` run.
4. **The reopen key is wider than today's** (5.4, E5): `roles` identity reopens every view of a system on any role write.
   If that is too wide, the alternative is an optional `reopenOn?(sys): string` on a view (the HD returns its ROM path
   and the lsdj-assets config; the overlays default to the ROM path), which is one more field in the contract and not in
   the brief. Whether `SystemsStore.setRoleConfig` replaces the `roles` array on every write is a stage 2 read of the setter.
5. **`undefined` and `null` as "nothing moved" / "unreadable"** (3.1) extend the brief's `sample(): { lines, playing } |
   null` and `frame(): ... | null`; the overlays' and the HD view's current behaviour needs both.
6. **Contributed actions are not deferred under a menu modal** (5.5): a bindings-capture row armed, or a text prompt,
   still passes a backtick to the toggles, exactly as it does now. Gating them on `isMenuModalActive` as `OpenMenu`
   is would be arguably better and is a behaviour change; the draft keeps backtick's.
7. **A default that collides with a Game Boy button's default** (3.5): `Z`, `X`, `Enter` and the arrows would fire the
   action AND the joypad. Controller decision: refused too (check 6), since it changes no behaviour and only refuses a
   contribution. A user's own rebinding onto a joypad key is still allowed, as it is for the shell's actions today.
8. **Two flags instead of one** (decision 9, E2): after a rebind the two overlays can disagree, which is the freedom
   decision 7 chose.
9. **`views.open` checks `when`** (5.1) so the HD view's unsupported box means "a supported cart this build cannot
   read"; today it would also show for a system the row was never offered for. That check is the draft's.
10. **The fixture package's name and kind** (`packages/ui-fixture`, a real workspace member rather than a tree under
    `test/`) is the draft's: a real package typechecks in the ordinary way and doubles as the worked example spec/06's
    "a view" step copies. A tree under `test/` would need its own program (the fixture trees are excluded from the
    shell's).
11. **`test:ui` cannot render a component view or run any view case without a ROM fixture** (8.6, 8.7): the view
    lifecycle is proved by the pure controller test in CI and by the fixture-gated real cases locally, and CI runs none
    of the three existing view tests (all SKIP without the ROM).
12. **`tsconfig.ui.json` could be folded into `tsconfig.json`** (8.1): one `tsc` pass instead of two, at the cost of
    one program that includes `ui/` and the tests' harness alias. Left as built.
13. **The shell program now type-checks dpf.js's `runtime/lvgljs/index.ts` as source** (8.1). `tsconfig.ui.json` already
    does under the same options, so it should pass; unverified, and the time cost is unmeasured.
14. **The final `UI_IMPORTS` set** (8.3) depends on which uses of `core/systemRoles` (6 import lines today; three are in
    the files that leave) and `launchpad/enums` remain; the RED run decides.
15. **Menu row ids with a dot** (`bind-act-lsdj.debug-overlay`): nothing found keys on an id's characters
    (`menu-ids.test.ts` checks uniqueness only), but the Menu's own use of an id was not read for a `split` on `.`;
    a stage 3 check.
16. **Not verified here, still open.** The Windows, macOS and arm64 builds of every new script and package (first on
    push); a real-host look at the tile z-order (E4); `reaper:editor` and the SDL legs for the hosted editor with the
    views moved.

**Corrections to the brief and the survey** (what the code showed): `ViewDefinition` is read by `Collected.views` and one
test (3.4); `Subscribable<boolean>` does not exist (10.1); `bindingsChildren` is `menuDefs.ts:1191-1295` (1303 begins
`settingsChildren`); `SHELL_IMPORTS` has 29 entries, not 37; `uiViews` is `uiHost.ts:31-37`; `MenuContext.beginSongImport`
and `openLsdjHd` are `:87` and `:89`; a pad name table does not exist (10.2); the generator cannot itself refuse a TS-level
id (10.3); `/packages/ui/` in the bundle check does not match `packages/ui-react/` but the kit's `ts/ui/` tree is already
matched by the second pattern (8.5); rule 3 cannot see `react/jsx-runtime` in a `.tsx` and needs the implicit-use clause
(8.2).

## 11. What it leaves

For `spec/07-remaining-work.md`'s F4c entry and the package split's closing note:

- **The controller-app display bug**, the per-product bundles (with F4a's finding that hook code rides every bundle that
  loads a registry; `ui-react` and every package's views are now plain `ts/ui/` modules the generated `menus.ts` alone
  pulls in), and **the shell's hard dependency on `launchpad`**, unchanged.
- **A real `component` view.** The kind and its fixture ship; the first package that needs React for a view proves it on
  a host, and its `test:ui` case lands with it.
- **A kit compile** (`host.kits` in the UI host) when a UI feature needs it, as F4b left it.
- **Gamepad axis tokens as action bindings**, and a stick-driven action, if a package wants one (10.2).
- **Windows, macOS, arm64 and CI** for F4c's scripts and packages, as F4a's and F4b's.
- **Folding `tsconfig.ui.json`** into the shell's program (10.12), and widening or narrowing the reopen key (10.4), if the
  stage 2 read shows either matters.
