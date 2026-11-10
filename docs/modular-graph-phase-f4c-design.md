# Phase F4c: React in packages

**Date:** 2026-11-10 · **Branch:** `feature/modularize`
**Status:** built on `feature/modularize`: designed `663632d16`, planned `a35330a8a` and executed as the five task
commits section 12 records (`7e4c4114b`, `666513625`, `a518a276a`, `fcb337418`, `484dba777`), which replay a five-stage
prototype each stage of which was Opus-reviewed and fixed. Designed over `c8a35d34b` (the F4b tip). The user's
decisions are the `### F4c` entries of `.superpowers/f-split/decisions.md` (taken 2026-11-10); approved by the user
2026-11-10. Sections 1 to 11 are corrected in place to what the prototype built, section 10 says what the prototype
answered and what stayed open, and section 12 records how.

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
| 5 | Typecheck | One setting everywhere plus rules: `jsx: react-jsx` in `tsconfig.base.json`, four React host modules with `types`, the generated paths carry them, a package that declares a UI host module (or lists examples) extends the generated host-modules config, rule 3 refuses a `.tsx` outside a `ts/ui/` tree (user, option A) | A separate UI program per package that has React (B) |
| 6 | Who owns keys | Packages contribute their own bindings and the shell routes them: developers will want keys beyond the debug toggle (user, option B) | A shell-owned debug layer that shows and hides every overlay (A) |
| 7 | The key's shape | Contributed app actions in the binding profiles: `{ id, label, keys, pad?, run }`, rebindable in the bindings menus, an id absent from a stored profile taking its default (additive, no migration), two contributions on one key both running (user, option A) | Raw key handlers a package registers (B) |
| 8 | The package's name | `packages/ui-react`, `@retroplug/ui-react`, not "ui-kit": "kit" reads as an LSDj sample kit (user) | `ui-kit` |
| 9 | The overlay's visibility | Each package's own `Readable<boolean>` (section 3.1); backtick toggles each package's flag through its own action, so one press still flips both | One shell-owned flag (decision 6, A) |
| 10 | The prototype model | DeepSeek Flash implements each stage, Opus reviews each stage, as F4a and F4b ran; the parallel kit with `sdl:controller` run alone; the per-stage tree rule of F4b section 11 | n/a |
| 11 | The extend rule | The generator asks a package for the generated host-modules extend when it lists examples or declares a UI host module. A plain-TypeScript view needs no React path, and `views` is a TypeScript member the `.mjs` generator cannot see; risa's `tsconfig.json` is unchanged (controller, stage 1 review) | "Has views", as the draft said |
| 12 | The UI bundle is no oracle | The bundle's hash moves whenever a bundled file moves (esbuild heads each module with its path), so stage 1 is held by a normalised diff, not a hash (stage 1 finding) | A hash equal before and after, as the draft said |
| 13 | A key bound to two shell actions | One shell action per key, the later binding winning as the old map did; contributed ids accumulate beside it (controller, stage 3 review) | Every action on a key fires, as the draft said |
| 14 | E9 | A backtick the user bound to a shell action now toggles the overlays AND runs that action, as any key bound to two actions does (controller, stage 3 review) | Gating the toggles to keep today's behaviour |
| 15 | The tile slots | One `StableSlot` per REGISTERED tile view, always, with `when` checked inside it (stage 2 review) | A slot per view whose `when` passes now, as the draft said |
| 16 | The default readout signature | The overlays carry none: the default (the lines and the flag) displays what the old hand-written key did (stage 4) | Narrowing it with a `signature` per sampler |
| 17 | The PixelView guard | Folded back into stage 2, where the bug was; the plan must never carry a broken intermediate (stage 4 review) | Leaving it in stage 4, where `lsdj-hd` found it |

Decisions the draft makes that the user did not take (each is a detail the contract needed; section 10 lists the ones
worth a second look): `Readable<T>` for `visible` (the brief's `Subscribable<boolean>` does not exist, 10.1); an
`undefined` sample and a `null` frame as "nothing moved" (3.1); the shell reopening a session on a ROM or role change
(5.4); `tagTestId` defined once, in `ui-react` (4.2); a real workspace package `ui-fixture` for the component-view
fixture (8.6).

Rows 11 to 17 are the decisions the controller took while the prototype ran; each is in `.superpowers/f-split/decisions.md`
and each has its reason in section 12.

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
/** What a `component` body is, and what @retroplug/ui-react's reactView returns. */
export type ComponentBody = Extract<ViewBody, { kind: "component" }>;

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
- **The overlays** are `readout` tile views: three lines of text and a green-or-grey flag. They need no pixel and no
  layout beyond the strip, and they carry no `signature`: the default (the lines and the flag) is a function of exactly
  the fields the old hand-written key (`useLsdjRuntime.ts:231`) covered, so it can change when and only when a displayed
  pixel can (the old key excluded `chainRow`, which the lines do not show either).
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

**As built.** Package code is guarded where the shell calls it, in plain TypeScript beside the session controller so a
test needs no React: a throwing `when` reads as "no", a throwing `visible` as hidden, a throwing `open` leaves the system
unsessioned, a throwing `frame` or `sample` drops the session (its reopen key stays held, so a broken cart is retried at
the next key change, not every frame), and a throwing `signature` falls back to the default; each is reported once per
view object and per thing that threw. A `component` body's render or effect is guarded too, by the one React error
boundary in the shell: React 18 unmounts the whole UI for an uncaught render error, so a throw there would take every
tile down, and the boundary reports it once per body (the same rule and wording) and renders nothing.
`ReadoutSampler.signature` can only coarsen the default; a sampler whose state moves without its lines changing narrows
it. A `PixelSession.frame` is asked for only once the shell has a canvas to draw the result into (a flush-and-diff
session, as the HD canvas is, cannot have a frame swallowed); `PixelView` checks the canvas first. `unsupported` is shown
by a WINDOW view only: a tile `pixels` view whose `open` returned `null` draws nothing.
### 3.2 Actions

```ts
export interface UiAction {
  /** "<package>.<name>": the manifest's package name, a dot, a name. Namespaced because the profile stores it. */
  readonly id: string;
  readonly label: string;               // the bindings menus' row text: "LSDj Debug Overlay"
  readonly keys?: readonly string[];    // default keyboard bindings, by the names the binding profiles use ("`", "f")
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
  As built `uiViews` and the `"lsdj-hd"` mapping went in stage 4, with `MenuContext.openLsdjHd` and its stubs (15 test and
  recorded-context files in all); until the cart registered the same id App held an interim shell-side `lsdj-hd` entry.
  The menu tests' fallback host (`hostOf`) resolves ids against the registry through `windowViewOrReport` and has no
  window to open (5.1).

### 3.5 The generated registry

`ui/generated/menus.ts` (written by `renderMenus`, `packages.mjs:756`) gains two exports beside `UI_CONTRIBUTIONS`, in
product order, and the checks of one new section:

```ts
export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS.flatMap(...);
export const ACTIONS: readonly ContributedAction[] = UI_CONTRIBUTIONS.flatMap(...);   // { pkg, action }, declared once in contribution
```

The checks run at load, in the generated module, as every F4b check does (the generator is `.mjs` and cannot read a
TypeScript contribution; `claimRow`, `checkRow` and the others are TypeScript in the generated text, and the generator
tests exercise them through the generated fixture module). They refuse, each naming the package or packages:

1. a view id declared twice (one package or two);
2. a view with a `placement` that is neither `window` nor `tile`, a body `kind` that is none of the three (or no body at
   all, which names the package rather than throwing), a `pixels` or `readout` body with no `open` or a `component` body
   with no `component` (the shell calls both, so an untyped pack's mistake would otherwise be a renderer's first failed
   call), or a `visible` on a window view (the type refuses the last; the data check backs the type, as `PLACEMENTS`
   does);
3. an action id declared twice;
4. an action id not prefixed by `<its own package name>.` (the manifest name, `lsdj`, as `UI_CONTRIBUTIONS`' `pkg`
   carries it), so a profile's stored id names its owner;
5. a default key name `resolveKeyName` (`keyCodes.ts:60`) does not resolve, a single UPPERCASE letter (the hosts
   deliver a letter unshifted, so `F` would show in the bindings menu and never fire; the refusal names the lowercase
   key), or a default pad name outside the gamepad name table (3.6: `GAMEPAD_BUTTON_NAMES` is new);
6. a default that collides with a shell default, compared by RESOLVED DPF CODE and not by spelling (so `Return` collides
   with Enter and `z` with Z): a shell action's (`DEFAULT_KEYBOARD_ACTIONS` /
   `DEFAULT_GAMEPAD_ACTIONS`, `bindingMap.ts:28-29`: `Escape`, `Tab`, `leftshoulder`, `rightshoulder`) or a Game Boy
   button's in `defaultBindingMap()` (`bindingMap.ts:53`: the arrows, `Z`, `X`, `Enter` and the rest, keyboard and
   gamepad), since a key on both would fire the action AND the joypad. Two CONTRIBUTIONS on one key are allowed (the
   overlays' backtick).

The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES`, the two default tables and `defaultBindingMap` from
`../../src/`, beside the `ROLES` import it already takes from `src/generated/packages`; `ui/` may reach `src/` (section
8.3). The generator's fixture tree holds one-line shims to the real `keyCodes.ts` and `bindingMap.ts` (aliased by the
driver), so the checks are held to the real vocabulary and the real defaults and cannot drift from a copy.

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
`dependencies` `@retroplug/ui` alone (the view types `reactView` builds, and `UiHost`, which `@retroplug/ui/contribution`
publishes; a `@retroplug/core` entry no file imports would draw the unused-dependency warning), and a `retroplug` member
of exactly `{ "host": ["react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"] }`. Its React lives under its own
`ts/ui/`, which is what rule 3 admits React into (8.2). It contributes no `app`, `consoles`, `protocol`, `ui` or `cli`
module, so it is not in `retroplug.products.desktop`, as `ui` and `tracker` are not (`contributes()`,
`packages.mjs:150`); the shell declares it in `packages/retroplug/package.json` as `workspace:*` and `pnpm install`
runs without `--frozen-lockfile` (the stage report quotes the lockfile diff).

### 4.2 What moves in, and what it exports

All of these are the shell's own files today, moved with their text unchanged but for imports:

| From (`packages/retroplug/ui/`) | To (`packages/ui-react/ts/ui/`) | Notes |
| --- | --- | --- |
| `lvgl/lv.ts` (80 lines) | `lv.ts` | the typed handles on `lvgljs-ui` (`ViewEx`, `TextEx`, `CanvasEx`, `CanvasHandle`, `LV_IMAGE_ALIGN_CONTAIN`) |
| `lvgl/Box.tsx` (59) | `Box.tsx` | |
| `lvgl/PixelCanvas.tsx` (11) | `PixelCanvas.tsx` | |
| `lvgl/useNativeEvent.ts` (49) | `useNativeEvent.ts` | with its `NativeEvents` table, which mirrors the C++ emit sites by hand |
| `lvgl/useFramePoll.ts` (98) | `useFramePoll.ts` | `useFrameDivider`, `useVersionedPoll`, `useFrameSampled` |
| `hooks/useRerender.ts` (23) | `useRerender.ts` | `useForceUpdate`, `useRerenderOn` |
| `screens/grid/DebugReadout.tsx` (36) | `DebugReadout.tsx` | `hex2` is `@retroplug/ui/format`'s, which the kit re-exports |

New: `tagTestId.ts` and `reactView.ts` (and the entry, `index.ts`, which re-exports every other module of the tree; a test
holds that). `tagTestId(testId)` is the ref callback that tags a widget's native uid for
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
export { DebugReadout } from "./DebugReadout";
export { hex2 } from "@retroplug/ui/format";                  // one definition, plain TypeScript, shared with the carts
export { tagTestId } from "./tagTestId";
export function reactView(Component: ComponentType<ViewComponentProps>): ComponentBody;   // { kind: "component", component }
export type { ViewComponentProps };                                                          // the contract's, published here too
```

`reactView` is the typed way to build a `component` body: `@retroplug/ui` types the body's `component` as `unknown`, so
a pack author cannot hand the shell a function with the wrong props. A pack that draws pixels or text never imports this
package.

Shell-bound and staying in `packages/retroplug/ui/`: `StableSlot`, `useWindowSize`, `FocusProvider` / `useFocusGroup`,
`host/nativeHooks`, `stores/` (`useStores` needs `AppStores`), and `useStateRef` (only `Menu.tsx` uses it; moving it
would be speculative). The shell imports the kit from `@retroplug/ui-react`: 19 files (33 import lines, merged to one
`@retroplug/ui-react` line per file) change their specifier and nothing else, `StableSlot` imports the kit's `tagTestId`,
and `EmulatorTile` and the HD screen import it from the kit rather than through `StableSlot`.

## 5. The shell side

### 5.1 The window slot

`App.tsx`'s `hdSystemId` (`:73`) becomes `openView: { id: string; systemId: number } | null`.

- **Opening.** `createUiHost(stores, peers)` takes `views: ViewsHost` as a peer today (`uiHost.ts:43`); App builds it,
  and its `open` is `openViewOf(id, systemId)`. `windowViewOrReport(id)` (in `menuContributions.ts`, beside `roleRows()`,
  `uiServices()` and `windowViewOf(id)`, all reading the generated `VIEWS`) is the one place the two refusals that need
  no project are worded (no view is registered under the id; the id is a tile view, which opens no window); App then
  checks the two things only a project answers (the system is gone; the view's `when` rejects it). Each is reported
  (`console.warn("[views] ...")`, the shape of `firstWinsMenuIds`' report) and opens nothing; otherwise
  `setOpenView({ id, systemId })`. `when` is checked at open so the unsupported box stays about a cart the view applies
  to but cannot read (a supported role on an unsupported version), not about a system it was never meant for (10.9).
  The menu tests' fallback host (`hostOf` in `menuDefs.ts`, built over the stores alone) runs the same
  `windowViewOrReport` and, for a view that resolves, says only that the menu's host has no window: only App's host
  has one. Until stage 4 registered `lsdj-hd`, App kept an interim shell-side `lsdj-hd` entry in `openViewOf` and in the
  full-window branch, and `uiViews` served the fallback host; stage 4 deleted all three.
- **Closing.** Esc closes it where it does now (`App.tsx:202`: after the overlay check, before the key map, so Esc is
  still the universal back); the stale-system clear (`:150-152`) becomes generic over `openView.systemId`.
- **Rendering.** The full-window branch (`:314-318`) renders `<ViewWindow view={...} systemId width height />`
  (5.3), keeping the order: overlay and empty branches first, the stale-id fall-through to the grid, and
  `useGameInput` / `useGamepadInput` live underneath (`:295-296`: they are keyed on `playing`, which the window view
  does not touch).

### 5.2 The tile slots

`EmulatorTile` takes the UI host as a prop (App builds the one `UiHost`; `SystemGrid` passes it down with the stores) and
mounts `TileViews`: ONE `StableSlot` per REGISTERED tile view, always, in generated order, each in its own slot (the
`insertChildBefore` gotcha, AGENTS.md "Framework gotchas"), with the view's `when` checked INSIDE the slot. The registry
is a module constant, so the slot list never changes length; a `when` that flips under a live tile swaps the slot's
child instead of appending a slot at the END of the tile's LVGL child list, over the focus border and the render badge,
so E4 holds for a flipping `when` too (the draft mounted a slot only for a view whose `when` passed, which did not).
A slot whose `when` fails is the empty transparent `Box` the dim layer already proves passes a click. Each slot also
subscribes to its view's `visible` itself, for every body kind, so a hidden `component` view mounts nothing at all. The
slots sit where the two overlay lines were (`EmulatorTile.tsx:84-90`), after the dim and before the border; the slot's
test id is `view-slot-<id>` (distinct from the view's own id, which tags the readout strip inside it). A slot is the
tile's size with `background-opacity: 0`, and the readout inside aligns to its bottom middle exactly as it did to the
tile's, since the two have the same size.

This changes one thing: today the z-order depends on mount history. An overlay toggled ON after the border and the
badge mounted is appended last, over them; a border or badge that mounts after the overlay (a focus move, a render
starting) is appended over it in turn. In a slot the strip has one fixed place, under both. Where the two overlap (a
render badge and an overlay, both at the tile's bottom) the badge now always draws over the strip. Section 7, E4.

A slot is a full-tile transparent `Box`, like the dim layer that already covers every unfocused tile
(`EmulatorTile.tsx:77-83`), through which a click still reaches the tile's `onClick` (that is how an unfocused tile is
focused today). No `test:ui` case could show it in stage 2 (no product registered a tile view, and `ui-fixture` is in no
product), so the case is stage 4's: `overlay-click-through` clicks an unfocused tile while `lsdj-overlay` shows.

### 5.3 Three renderers

The shell holds one renderer per body kind, in `ui/views/`, each built from `@retroplug/ui-react`:

- **`PixelView`**: `useNativeEvent("frame")` -> the canvas check -> `session.frame()` -> `PixelCanvas.setBuffer`,
  letterboxed by the canvas's CONTAIN alignment as today. The canvas is checked BEFORE the frame is read: the HD canvas's
  `frame()` is flush-and-diff, so a first (dirty) frame read while the ref was still null marked its tiles painted and
  the screen stayed black for good (stage 2's guard; the unchanged `lsdj-hd` case caught it, and failed without it).
  It passes `pixels.buffer` when the array is the whole buffer and `pixels.slice().buffer` when it is a view onto a
  larger one: the HD canvas's array is its own (no copy per frame, as now, `LsdjHdScreen.tsx:42`), and an emulator frame
  is a subarray of the RPC wire buffer (`EmulatorTile.tsx:64-66` copies for that reason). `EmulatorTile` itself stays:
  its frame comes from `backend.getFrame` with a `published` flag, which is not a view.
- **`ReadoutView`**: `useFrameSampled(sample, signature)` -> `DebugReadout` with the tile's width and the view's id as
  its test id. The hook's memoised `sample` is the opened sampler's `sample`; its signature is the sampler's, or the
  lines and the flag.
- **`ComponentView`**: mounts the body's component with `{ host, systemId, width, height }` and nothing around it. The
  one cast in the shell lives here (`body.component as ComponentType<ViewComponentProps>`).

All three read their session through `useViewSession` (5.4), which is three-state, and a renderer draws nothing for a
session that has not opened yet.
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
- **`useViewSession` is three-state and never lags what it renders** (`undefined`: not opened yet; `null`: refused or
  hidden or its system gone; else the session). `shownSession(session, shown, system)` is the whole rule, so a hide
  drops the session in the render that sees it, and the hook's `read(reader)` reads the session the CONTROLLER holds now,
  not the render's closure, so a native `frame` landing between a hide (or a reopen) and the re-render cannot frame or
  sample a session the shell has let go. What the user sees on opening the HD view: the black window box with nothing
  inside it until the session opens (one effect, a frame or two), then the canvas; the unsupported text only ever means
  "a cart the view applies to and cannot read". (As first built, every open of a supported cart flashed the unsupported
  text for one effect.)
- The reopen key is `(view.id, systemId, romPath, roles)` by identity, which is the brief's. It is wider than today's
  hooks: `useLsdjRuntime` rebuilds on `isLsdj` (a boolean) and `romPath`, `useRisaRuntime` likewise, and the HD session
  on the `lsdj-assets` config object, so a role edit that touches none of those leaves them alone, where a `roles`
  identity change reopens every view of that system. `SystemsStore.setRoleConfig` replaces `roles` on EVERY write
  (`systemsStore.ts:964-974` slices `e.roles` and swaps in the merged config even when the stored value is unchanged;
  `view()` hands the array through by reference), so the key moves on every role write: the width is as designed and
  no narrowing was needed. The cost is one header read per LSDj overlay and one whole-ROM read per risa overlay on an
  edit made while an overlay is showing; with the instance menu open the tile is unmounted, so in practice the reopen
  happens when the menu closes, as it does today. The one visible case is E5's.
- **Hidden work.** A hidden tile view opens no session (`visible` false means the renderer renders `null` and the
  effect does nothing), so a hidden overlay does the same zero work as `useXRuntime(id, false)` does now.

### 5.5 Keys: a list of actions per key

`buildKeyToAction(actions, contributed)` and `buildGamepadToAction(actions, contributed)` (`keyCodes.ts`) change from
`Map<number, AppAction>` / `Map<string, AppAction>` to `Map<number, string[]>` / `Map<string, string[]>`: an action id
list per key, in the order the profile (then the registration order) gives them. "Known" is the shell's three
(`isAppAction`; `APP_ACTIONS` is module-private) plus the ids the caller passes as `contributed` (App passes
`contributedActionIds()`, a test passes its own), and a stored id of an action no product registers is skipped as an
unknown one is today. One helper, `addActionId`, builds both lists, and it keeps what the old `Map.set` did: a key's list
holds at most ONE of the shell's own ids, a later shell binding replacing the earlier in place (so `{ OpenMenu: ["Tab"],
CycleNext: ["Tab"] }` gives `["CycleNext"]` as it did), a contributed id accumulates beside it, and an id the list already
holds is skipped (one action bound to one key twice fires once). The draft let every action on a key fire, which changed
a pre-existing double binding of the shell's own actions (stage 3's review).

```ts
// ui/input/appActions.ts (new): the shell's dispatch over both kinds.
function runActions(ids, shell, contributed, host, systemId): void {                     // contributed is a parameter: the pure tier can drive a test action
  for (const id of ids) {
    if (isAppAction(id)) shell[id]();
    else contributed.find((c) => c.action.id === id)?.action.run(host, { systemId });   // each in a try: reported, then on
  }
}
```

`App.tsx`'s key handler keeps its order (`:197-207`): Esc is the universal back, then `if (overlay) return`, then the key
map; the backtick line (`:205`) and the `KEY_BACKTICK` constant (`:48`) go, the contributed action taking its place in
the map. Contributed actions fire in every state without a full-window overlay, whether the menu is open or not, and
under an open window view (so backtick toggles an overlay while the HD view shows), EXCEPT while the menu is modal: a
text prompt or an armed capture row owns the key, so a press that lands there types or binds and runs no contributed
action, which is the same deferral OpenMenu makes (`toggleMenuCore`, `:180`) and is E10. The shell's own three keep
their own deferrals and their behaviour (the cycles still run). A backtick the user bound to a shell action now
toggles the overlays AND runs that action (E9). The gamepad handler (`:210-214`) dispatches the same
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

- `withActionDefaults` always returns a new object (two shallow copies and a new profile), even when nothing is filled:
  App memoises it on the profile, so the identity changes exactly when the profile does, and the caller's resolved
  profile is never mutated.
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

The contributed list reaches the composer through a new optional `MenuContext.actions`: App leaves it out and the
generated registry answers; a menu test passes its own list, which is the only way to drive a row for an action no product
carries (the menu tests compose a context over the stores alone). The row text is exactly `<action.label>: <bound names or
-> `, so the shell names no cart. The ids carry the action's dot (`bind-act-lsdj.debug-overlay`): nothing keys on an id's
characters (10.15).
## 6. The moves

### 6.1 LSDj (`packages/lsdj/ts/ui/`, plain TypeScript)

- **`hdView.ts`**: a `WindowView`, `id: "lsdj-hd"`, `when: (sys) => hasRole(sys.roles, "lsdj-sync")`, body `pixels` with
  `unsupported: "HD view needs a supported LSDj cartridge"`. `open(host, systemId)` is `buildSession` of
  `useLsdjHdSession.ts:193-214` unchanged in what it reads and when: the 0x150 header through `host.files.readPrefix`,
  `LsdjReader.fromHeader`, the whole ROM through `host.files.readFile`, the lsdj-assets overrides folded in through
  `applyOverridesToRom(base, overrides, host)` (the host's `files` and `png`, replacing `filesPngHost(backend)`), and
  the `HdSession` class moved as it is (exported, so its cadence is testable over a fake host), with `backend.readSram`
  -> `host.systems.readSram` and `backend.readRam` -> `host.systems.readRam`. The SRAM poll is a countdown of 15, so a
  read happens at frames 0, 16 and 32: the old cadence, 16 frames a period, with the same FNV signature over the first
  0x8000 bytes. `frame()` is `renderFrame()` of `:180-189` returning `{ pixels, width: canvas.width, height:
  canvas.height }` or `null`. `open` refuses what the old hook gated on itself (no `lsdj-sync` role, an empty ROM path)
  and an unreadable header or a ROM `LsdjRom` refuses, since the shell checks `when` only at `views.open` and a renderer
  can reach `open` with any system. The system's `lsdj-assets` config comes from `host.systems.view()` by id (the role
  config is `roleConfig(sys.roles, "lsdj-assets")`). The "HD Player" row keeps `host.views.open("lsdj-hd", sys.id)`
  (`lsdj/ts/ui/index.ts:253-255`).
- **`overlayView.ts`**: a `TileView`, `id: "lsdj-overlay"`, `when` the same, `visible: lsdjDebug` (below), body `readout`.
  `open(host, systemId)`: `LsdjReader.fromHeader(host.files.readPrefix(rom, 0x150))`, null if unreadable or
  `!reader.supported` or the system has no `lsdj-sync` role; the sampler's `sample()` is `host.systems.readRam(systemId)`
  -> `reader.read(wram)` -> `null` when `!state.supported` -> `{ lines, playing: state.playing }` with the three lines of
  `LsdjOverlay.tsx:170-175` (`versionStr` and `channelCell` move with it; `hex2` is `@retroplug/ui/format`'s, one
  definition both overlays and the kit share, and not a copy per package); an unreadable RAM answers `undefined`. No
  `signature`: the default is the lines and the flag, which displays what the old key did (3.1). The sampler's
  `!state.supported` branch is unreachable today (`decodeLsdjState` always answers supported, and `open` already refuses
  a reader with no layout); it stays as the contract's answer.
- **`debugOverlay.ts`**: `export const lsdjDebug = createSignal(false)` and the action `{ id: "lsdj.debug-overlay",
  label: "LSDj Debug Overlay", keys: ["`"], run: () => lsdjDebug.set(!lsdjDebug.get()) }`. Not persisted, off at start, as
  `lsdjDebug.ts`'s header says it must be; `run` ignores `ctx.systemId`, so it works on the start screen as the backtick
  did. `lsdj/ts/ui/index.ts`'s `defineUi` gains `views: [hdView, overlayView], actions: [lsdjDebugOverlay]`. The views
  are declared with `satisfies WindowView` / `satisfies TileView` and take the narrowest host: `HdSession` takes
  `Host<"systems">`, `hdView`'s `open` `Host<"files" | "png" | "systems">`, the overlays' `open` `Host<"files" |
  "systems">`, so a test supplies a fake of those groups and no cast.

### 6.2 risa (`packages/risa/ts/ui/`)

The same two files for `risa-overlay` and `risa.debug-overlay` (`keys: ["`"]`): `open` is `useRisaRuntime.ts:310-320`
(`host.files.readFile(rom)`, `runtime.identifyRisaVersion`, `resolveRisaLayout`, null when no layout), the sampler is
`decodeRisaState(ram, layout)` through `host.systems.readRam`, and the lines, `trackCell` and the 4X tempo rule are
`RisaOverlay.tsx:203-211` verbatim. `when` is `hasRole(sys.roles, "risa")`, which is the scanner's one `ui/` hit
(`useRisaRuntime.ts:306`) leaving the shell. `risa/ts/ui/index.ts` becomes `defineUi({ trackers: [RISA_TRACKER_UI],
views: [overlayView], actions: [risaDebugOverlay] })`. The view and its action are declared as lsdj's are
(`satisfies`, `Host<"files" | "systems">`), `open` refuses a system without the `risa` role, and `sample()` answers `null`
for a RAM image too short to decode. risa's `tsconfig.json` is UNCHANGED: the generator asks for the generated
host-modules extend of a package that lists examples or declares a UI host module (8.1), and risa's overlay is plain
TypeScript that declares no React module, so it needs no React path (lsdj's extends it already, for its examples).

### 6.3 What leaves the shell

`ui/screens/hd/` (both files), `ui/screens/grid/{LsdjOverlay, RisaOverlay, useLsdjRuntime, useRisaRuntime, lsdjDebug,
DebugReadout}`, `uiViews`, `MenuContext.openLsdjHd`, `src/graph/defineExtension.ts`'s `ViewDefinition`, and the
backtick code in `App.tsx` (`:34`, `:48`, `:205`). After it `ui/` outside `generated/` names no cart.
What arrives: `ui/views/{viewSession.ts, useViewSession.ts, PixelView.tsx, ReadoutView.tsx, ComponentView.tsx,
ViewWindow.tsx, TileViews.tsx}` and `ui/input/appActions.ts` (`useViewSession.ts` is the one effect and subscription the
three renderers share, kept out of `viewSession.ts` so the controller stays React-free for `pnpm test`).

## 7. Behaviour edges

None changes audio, a saved file or a graph document. Each is listed with what was found.

- **E1. The bindings menus gain a row per contributed action** (5.7): `LSDj Debug Overlay` and `risa Debug Overlay` in
  each of Keyboard Bindings and Gamepad Bindings, after `Cycle Instances (Back)`. The recorded menu matrix holds the
  bindings menus in every case (the 45 ids of each of the 35 cases in `test/menu/recorded-matrix.json`), so its diff is
  four new items per case (two per menu) and nothing else; it is re-recorded in stage 4, when the two real actions
  arrive (stage 3's action is a test one and adds no product row), and the diff is the record. As built it is
  `recorded-matrix.json` +700 lines, 0 deletions (35 cases x 2 menus x 2 items x 5 lines, in 70 hunks); the file was
  not re-taken wholesale but had only the two bindings submenus' `children` spliced from a throwaway recorder, and a deep
  comparison proved the spliced file equal to the full new record.
  Two tests change: `test/menu/leaves.test.ts:929` and `:987` (the capture-row counts, 11 -> 13).
- **E2. Backtick is rebindable per overlay** (the point of decision 7). With the defaults a press still flips both
  overlays, as before; a user who rebinds or clears one has two independent toggles. The initial state is off for both.
- **E3. A stored profile that already maps a contributed id keeps its own mapping** (5.6). An older profile has no such
  id and reads the default; the file is not rewritten until an edit.
- **E4. The overlay's z-order against the focus border and the render badge** (5.2): it depended on which mounted last;
  in its slot the strip always sits under both. Only where the strip overlaps the badge (both are at the tile's bottom)
  can be seen. It holds for a `when` that flips under a live tile as well (one slot per registered view, 5.2). Verify by
  `pnpm screenshot`, which the prototype did not run (section 12).
- **E5. A role edit reopens the views of that system** (5.4): wider than the old hooks' dependencies, with no visible
  effect while the instance menu hides the tile. The one reachable visible case: linking instance B to LSDj instance A
  from B's menu rewrites A's roles while A's tile still shows, so A's overlay reopens and blinks out for a frame.
- **E6. `host.views.open` of an unknown id, a `tile` view's id, or a system the view's `when` rejects is reported and
  opens nothing**; before, an unknown id was ignored silently (`uiHost.ts:31-37`). The wording of the first two lives in
  `windowViewOrReport`, which App and the menu tests' fallback host both call.
- **E7. A contributed action that throws is reported and the rest of its press still runs**; before, `toggleLsdjDebug`
  could not throw.
- **E8. `extension.test.ts` loses its views expectation** with the seed (3.4); no production code changes for it.
- **E9. A backtick the user bound to a shell action toggles the overlays AND runs that action.** Today App returns before
  the key map on a backtick, so such a binding does nothing but toggle; once the toggles are contributed actions it is a
  key bound to two actions like any other (5.5). Unreachable without that deliberate rebind and arguably a fix, a
  controller decision (stage 3 review) rather than a behaviour held. A key bound to two SHELL actions keeps today's
  single winner (5.5).
- **E10. A contributed action does not fire while the menu is modal** (5.5, 10.6): a text prompt or an armed capture
  row owns the key, so a backtick typed into a prompt no longer also toggles the debug overlays, and a key a package
  bound can no longer fire while the user types it (the user's decision, the final review's Important 2). The shell's own
  three are unchanged: `OpenMenu` keeps its own deferral and the cycles still run under a modal. What a user can see
  move is small for the two shipped actions, because the profile-name prompt filters a backtick out and the free-text
  prompts are the project's; the hazard it closes was general, since an action bound to a letter would otherwise fire on
  every keystroke of a typed name.

Not edges, and why: the HD player's reads, its 16-frame SRAM poll and its frame skip are the old session's, moved; the
overlays' lines are the old lines; the Esc and game-input behaviour under the HD view is the old behaviour (5.1); the
window and tile test ids are the old ones.

## 8. Proof

### 8.1 The typecheck

- **`tsconfig.base.json`** gains `"jsx": "react-jsx"`. It changes what `tsc` accepts and nothing a bundler emits:
  `tools/ts-options.js` names the compiler options each build uses (its header says why), and the UI build passes
  `jsx: "automatic"` itself (`tools/build-ui.js:31`). The UI bundle is NOT byte-identical across the move and cannot
  be: esbuild heads each module with its path, so moving a file moves bytes (1,997,971 -> 1,998,067 at stage 1). Stage 1
  proves it by a normalised diff instead: with the
  module-header comments dropped and the generated `import_*N` helper numbering canonicalised, the two bundles' line
  multisets are equal but for two blank lines and `tagTestId`'s body, which reads `globalThis.__rp_tagTestId` (`native`
  in `host/nativeHooks.ts` IS `globalThis`).
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
  `runtime/lvgljs/index.ts` as source, which only `tsconfig.ui.json` did before, and passes (10.13: 906 ms against 921 ms
  for the shell's program, so no measurable cost). Each package program (`pnpm -r typecheck`) that imports
  `@retroplug/ui-react` or names a React module needs the generated host-modules config, and the generator refuses a
  product package whose `tsconfig.json`'s parsed `extends` does not name it, by the check F4b wrote for examples
  (`packages.mjs:1447-1462`, generalised as built from "has examples" to "has examples or declares a UI host module",
  `why = examples ? "it lists examples" : uiHost ? "it declares a UI host module" : null`). The draft said "or views";
  the generator cannot see a TypeScript `views` member, and a plain-TypeScript view needs no React path (decision 11), so
  risa's `tsconfig.json` is unchanged. The check judges product packages only (it reads `contributionsOf`), so
  `ui-react` and `ui-fixture`, which no product lists, are held by their own `tsc -p tsconfig.json`: without the extend a
  `react` import does not resolve.
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
  As built these are four boundary fixtures, `react-allowed`, `react-outside-ui`, `react-undeclared` and `react-cli-import`
  (rules `jsx-surface` and `host-module`), with four cases in `boundaries-checker.test.ts`; the `.tsx`-outside-tree case
  also draws the surface refusal for whatever React module that file names.

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

That is the nine specifiers `ui/` names today less the cart ones, plus `ui-react`, and the RED run confirmed it: a grep
of every `@retroplug/*` specifier under `packages/retroplug/ui` outside `generated/` gives these ten and nothing else
(three of `core/systemRoles`' six uses remain; 10.14). The RED run is rule 9 over `ui/` on a fixture tree of the pre-move
shape (the live tree is already clean after the moves): it is refused, where the rule's absence reported `status 0` for
all three refusal trees. Both lists are module-private (`SHELL_IMPORTS` lost its `export`: the ratchet's runner was its
only other reader), and the unused-entry check runs on the real tree only (a fixture tree states no list). The F4b ratchet
(`test/boundaries/ui-cart-imports.test.ts` and `run-ui-cart-imports.mjs`) is deleted with its fixtures, which became
the rule's: `ui-device` -> `shell-ui-forbidden` (a ui file naming `@retroplug/launchpad/client`), `ui-relative` ->
`shell-ui-relative` (two files: one reaching another package's tree, one reaching `cli/` by `../cli/run`, asserted in
walk order, and shown RED with `cli` added to the ui root's `reaches`), plus `shell-ui-unlisted` (a ui file naming
`@retroplug/core/path`, which `SHELL_IMPORTS` carries and `UI_IMPORTS` does not: the case that proves the list is the
UI's own) and `shell-ui-allowed` (the positive tree, holding the `src/` reach and the `ui/generated/` ignore). The
allow-based rule covers what the ratchet's derived guard list did (a device pack with no app module is guarded because
nothing but the list is allowed).

### 8.4 The literal scanner over `ui/`

`run-scan.mjs --dirs` takes `src,cli,ui` (`test/source-literals/no-cart-literals.test.ts:118` passes `src,cli`); it
already skips any `generated` directory (`run-scan.mjs:112`) and so skips `ui/generated/`. RED first: over the tree
before the moves it finds the one hit F4b recorded, `useRisaRuntime.ts`'s `"risa"` (a core id / tracker id), and a new
fixture `literals-ui` pins the walk of the third directory. After the moves the shell's `ui/` has none. The scanner stays
blind to what F4b listed (device ids, node types of the surface nodes, non-console role kinds, identifiers).

As built the real case and a fixture case share one constant, `SHELL_DIRS = "src,cli,ui"`, so the fixture case fails if
`ui` leaves it; the RED was an extracted pre-move tree (`git archive 663632d16`), where `--dirs src,cli` misses the
`"risa"` and `--dirs src,cli,ui` finds it; the `literals-ui` fixture carries both pre-move hits (`"risa"` and a
`/roms/risa.nes` path) and is valid TypeScript (the shell's program includes the fixtures directory); and `run-scan.mjs`
now skips a directory that does not exist, as the boundary check's walk does, because the fixture holds only `ui/`.

### 8.5 The bundle check

`test/script/bundle-ui-split.test.ts`'s `isUi` (`:65-68`) matches `/packages/ui/` (the whole package) and
`/packages/*/ts/ui/`; `/packages/ui/` does not match `packages/ui-react/` (the slash follows `ui`), but every file of
the kit sits under `packages/ui-react/ts/ui/`, which the second pattern covers. The pattern is nevertheless widened to
`/\/packages\/ui(-react)?\//` so the kit's other files (its entry, a future `ts/` file) count too. The control plane,
the render worker, the script nodes, the SDK, the dispatcher and the planner must carry no `ui-react` module, nor a
package's views (they are in `ts/ui/`); the UI must still carry no CLI one. The component fixture package
(`packages/ui-fixture/`, 8.6) is asserted ABSENT from the UI bundle's inputs: listing nothing is what keeps it out, and
the assertion would catch a product list that picked it up. As built the case asserts the kit's modules are in the UI
bundle and that `packages/ui-fixture/` is in NO bundle's inputs, not the UI's alone.

### 8.6 The fixture pack

A real workspace package, `packages/ui-fixture` (`@retroplug/ui-fixture`; the name is the draft's, 10.10), private, listed
by no product, with `retroplug.ui: "ts/ui/index.ts"` and `retroplug.host` of `react` and `react/jsx-runtime` (it imports
`react` for a type and uses JSX, and draws through the kit, so it names neither `lvgljs` nor `lvgljs-ui`, which would warn
as unused; `ui-react`'s own manifest declares all four), a `tsconfig.json` extending base and the generated host-modules
config, and `ts/ui/{index.ts, Badge.tsx}`: a tile view `badgeView` (`{ id: "ui-fixture-badge", placement: "tile", body:
reactView(Badge) } satisfies TileView`) whose component draws a `Box` and a `TextEx` through `@retroplug/ui-react`, and
the package default-exports `defineUi({ views: [badgeView] })`. What it proves, and where:

- **it typechecks**: `pnpm -r typecheck` over a `.tsx` that uses `reactView`, `Box` and JSX (stage 1);
- **rule 3 passes** with `retroplug.host` declaring the modules, and **a copy without the declaration, and a copy with
  the `.tsx` outside `ts/ui/`, are refused** (boundary fixtures, 8.2);
- **the generator accepts it**: a generator fixture tree (`test/packages/fixtures`) lists a package shaped like it and
  its expected `menus.ts` is held; a second fixture per refusal of 3.5 (two view ids, two action ids, a foreign prefix, an
  unknown key name, an unknown pad name, a collision with a shell default);
- **it is never bundled into the product**: no product lists it, no bundle's inputs contain it (8.5), and
  `pnpm check:boundaries` and the native boundary check both accept an unlisted package with a `ui` member (verified in
  stage 1: the generator never reads an unlisted package; `check:boundaries` judges every `packages/*/package.json` with a
  `ts/` tree, so it walks `ui-fixture` and `ui-react` under rules 1 to 8 with 0 refusals and 0 warnings; the native check
  judges only the targets of packages `retroplug_package()` was called for, and the workspace-dir check asks for
  `packages/retroplug/node_modules/@retroplug/<name>` only for a package the shell declares, which `ui-react` is and
  `ui-fixture` is not).

**What is not proved, said plainly.** The UI test binary embeds the production bundle (`test-ui/uiHarness.ts` bundles the
tests, not the UI), so no `test:ui` case can render a `component` view until a real package ships one. The
`ComponentView` renderer is a mount, a cast and a keyed error boundary; it is covered by typecheck, by the controller's
lifecycle test (a fake component body whose `open`-equivalent is the mount), by the pure test of the boundary's state
logic (`componentFault`: the latch and the report once per body, in `test/ui/view-session.test.ts`, since the boundary
class itself imports `react` and the pure tier cannot) and by reading, not by pixels.

### 8.7 The oracles held, and the new tests

**Nothing is re-recorded but the edge:** the parity baseline (197 cases), the kit and render baselines, the device image
hashes and pack bytes, the help texts, the battery listing, the recorded menu matrix except its bindings menus (E1),
`test:ui`'s three view tests as they stand (`lsdj-hd`, `lsdj-overlay`, `risa-overlay`: the same test ids, texts,
letterbox checks, Esc and backtick), BlipToaster and NESVJ. The UI bundle's size is reported at each stage; its hash is
no oracle (8.1).

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
   no behaviour change; `pnpm typecheck` green in all programs; the UI bundle's normalised diff clean; `test:ui` unchanged; the
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

As prototyped, the stages ran in this order and each was Opus-reviewed before the next began, with the review's fix pass
folded into the commit it belongs to (by fixup and autosquash) before the next stage; one pass moved a hunk between
stages (the `PixelView` guard, back into stage 2), and the last pass amended stage 5. Every stage's tree was then
extracted after the final rewrite, proven equal to its commit by a fresh index read, and typechecked and tested alone.
Section 12 has the record.
## 10. Risks and open questions

Found against the code. Items 4, 6, 7, 9 and 10 carry a controller decision (in
`.superpowers/f-split/decisions.md`, "Controller decisions (F4c draft)"); the rest were left for the prototype to settle
by reading the code. Each item below ends with what the prototype answered, or says it stayed open and why.

1. **`visible?: Subscribable<boolean>` does not typecheck as written.** `Subscribable` (`ui/ts/menu/subscribable.ts:10`)
   is non-generic, has no value, and is `{ emit, subscribe }`. The draft adds `Readable<T>` / `Signal<T>` (3.1) and
   keeps `Subscribable` as it is. If the user wanted `Subscribable` itself generalised instead, `createSubscribable`'s
   callers would see a type parameter they do not use.
   *Built:* `Readable<T>` and `Signal<T>` in `@retroplug/ui/contribution`, `createSignal` beside `createSubscribable` in
   `subscribable.ts`; `Subscribable` is untouched.
2. **No gamepad button name table exists** (3.6), so the brief's "a pad name the binding tables do not know" has nothing
   to check against. The draft adds `GAMEPAD_BUTTON_NAMES` (SDL's set); the open part is whether a stick token (`leftx+`)
   may be an action default (the draft: no, the axis bus does not dispatch actions).
   *Built:* `GAMEPAD_BUTTON_NAMES` in `keyCodes.ts`, SDL's 21 canonical button names, read off `SDL_CONTROLLER_BUTTON_*`; a
   stick token stays out of it, so a default on one is refused.
3. **The refusals are load-time throws in the generated module, not the generator.** The generator is `.mjs` and reads
   manifests; an action id, a view id and a key name live in TypeScript. The draft keeps F4b's mechanism (the generated
   module checks at load, the generator tests run it), which is also where F4b's `claimRow` and `checkRow` run.
   *Built as drafted:* the generated module checks at load, and the generator tests run it through fixture trees, with the
   fixture's `keyCodes` and `bindingMap` as shims to the real modules.
4. **The reopen key is wider than today's** (5.4, E5): `roles` identity reopens every view of a system on any role write.
   If that is too wide, the alternative is an optional `reopenOn?(sys): string` on a view (the HD returns its ROM path
   and the lsdj-assets config; the overlays default to the ROM path), which is one more field in the contract and not in
   the brief. Whether `SystemsStore.setRoleConfig` replaces the `roles` array on every write is a stage 2 read of the setter.
   *Answered:* `SystemsStore.setRoleConfig` replaces `roles` on every write (read in stage 2), so the wide key moves on
   every role write. Kept as designed; `reopenOn?` was not built. E5 has one visible case.
5. **`undefined` and `null` as "nothing moved" / "unreadable"** (3.1) extend the brief's `sample(): { lines, playing } |
   null` and `frame(): ... | null`; the overlays' and the HD view's current behaviour needs both.
   *Built as drafted*, with the session hook three-state (5.4) so a renderer draws nothing for a session that has not
   opened.
6. **Contributed actions are not deferred under a menu modal** (5.5): a bindings-capture row armed, or a text prompt,
   still passes a backtick to the toggles, exactly as it does now. Gating them on `isMenuModalActive` as `OpenMenu`
   is would be arguably better and is a behaviour change; the draft keeps backtick's.
   *Reversed after acceptance* (the final review's Important 2, the user's decision, 2026-11-10): a contributed action
   does not fire while the menu is modal, which the shell's dispatch enforces with the same `isMenuModalActive`
   `OpenMenu` reads. E10 is the one behaviour change (a backtick typed into a prompt or a capture row no longer also
   toggles the overlays), and it is the right answer for a mechanism that is no longer one debug key.
7. **A default that collides with a Game Boy button's default** (3.5): `Z`, `X`, `Enter` and the arrows would fire the
   action AND the joypad. Controller decision: refused too (check 6), since it changes no behaviour and only refuses a
   contribution. A user's own rebinding onto a joypad key is still allowed, as it is for the shell's actions today.
   *Built:* check 6 compares resolved DPF codes, so `Return` collides with Enter.
8. **Two flags instead of one** (decision 9, E2): after a rebind the two overlays can disagree, which is the freedom
   decision 7 chose.
   *Stays, as E2.*
9. **`views.open` checks `when`** (5.1) so the HD view's unsupported box means "a supported cart this build cannot
   read"; today it would also show for a system the row was never offered for. That check is the draft's.
   *Built* (5.1), with the wording of the other refusals in `windowViewOrReport`.
10. **The fixture package's name and kind** (`packages/ui-fixture`, a real workspace member rather than a tree under
    `test/`) is the draft's: a real package typechecks in the ordinary way and doubles as the worked example spec/06's
    "a view" step copies. A tree under `test/` would need its own program (the fixture trees are excluded from the
    shell's).
    *Built as drafted* (`packages/ui-fixture`), and it doubles as the worked example spec/06's "a view" item names.
11. **`test:ui` cannot render a component view or run any view case without a ROM fixture** (8.6, 8.7): the view
    lifecycle is proved by the pure controller test in CI and by the fixture-gated real cases locally, and CI runs none
    of the three existing view tests (all SKIP without the ROM).
    *Stays.* The view lifecycle is proved by the pure controller tests in CI, by the packages' pure `ui-views` cases over
    a fake host, and by four new fixture-gated `test:ui` cases beside the three existing ones (12); the pure runner now
    injects `__RESOURCES_DIR__`, so `lsdj/ui-views` holds a pixel case that skips without the ROM (the one `ts` entry in
    the skip baseline).
12. **`tsconfig.ui.json` could be folded into `tsconfig.json`** (8.1): one `tsc` pass instead of two, at the cost of
    one program that includes `ui/` and the tests' harness alias. Left as built.
    *Left as built:* `tsconfig.ui.json` is `{ "extends": "./tsconfig.json", "include": ["ui"] }`.
13. **The shell program now type-checks dpf.js's `runtime/lvgljs/index.ts` as source** (8.1). `tsconfig.ui.json` already
    does under the same options, so it should pass; unverified, and the time cost is unmeasured.
    *Answered:* it passes, and the shell's program is no slower measurably (906 ms against 921 ms).
14. **The final `UI_IMPORTS` set** (8.3) depends on which uses of `core/systemRoles` (6 import lines today; three are in
    the files that leave) and `launchpad/enums` remain; the RED run decides.
    *Answered:* the ten of the 8.3 table; three of `core/systemRoles`' six uses remain, and `launchpad/enums` stays.
15. **Menu row ids with a dot** (`bind-act-lsdj.debug-overlay`): nothing found keys on an id's characters
    (`menu-ids.test.ts` checks uniqueness only), but the Menu's own use of an id was not read for a `split` on `.`;
    a stage 3 check.
    *Answered:* nothing parses an id (`Menu.tsx` uses it as a key and in maps; `navTo` matches on the label), and a unit case
    pins a dotted action id through `assertUniqueMenuIds`.
16. **Not verified here, still open.** The Windows, macOS and arm64 builds of every new script and package (first on
    push); a real-host look at the tile z-order (E4); `reaper:editor` and the SDL legs for the hosted editor with the
    views moved.
    *Still open, and why:* the Windows, macOS and arm64 builds (first on push); a real-host look at the tile z-order (E4, by
    `pnpm screenshot`); `reaper:editor` and the SDL legs with the views moved. Section 12 lists what acceptance must run.

**Corrections to the brief and the survey** (what the code showed): `ViewDefinition` is read by `Collected.views` and one
test (3.4); `Subscribable<boolean>` does not exist (10.1); `bindingsChildren` is `menuDefs.ts:1191-1295` (1303 begins
`settingsChildren`); `SHELL_IMPORTS` has 29 entries, not 37; `uiViews` is `uiHost.ts:31-37`; `MenuContext.beginSongImport`
and `openLsdjHd` are `:87` and `:89`; a pad name table does not exist (10.2); the generator cannot itself refuse a TS-level
id (10.3); `/packages/ui/` in the bundle check does not match `packages/ui-react/` but the kit's `ts/ui/` tree is already
matched by the second pattern (8.5); rule 3 cannot see `react/jsx-runtime` in a `.tsx` and needs the implicit-use clause
(8.2).

**Corrections the prototype made to the draft:** the generator's extend rule is "has examples or declares a UI host
module", not "has views" (8.1); the UI bundle's hash is no oracle (8.1); `ui-react` does not depend on `core` (4.1);
`hex2` is one export of `@retroplug/ui/format` (4.2, 6.1); `reactView` returns the contract's `ComponentBody` (4.2); the
HD SRAM poll is a countdown of 15 and a period of 16 frames (6.1); a key bound to two shell actions keeps today's single
winner (5.5); the tile slots are one per registered view (5.2).

## 11. What it leaves

For `spec/07-remaining-work.md`'s F4c entry and the package split's closing note:

- **The controller-app display bug**, the per-product bundles (with F4a's finding that hook code rides every bundle that
  loads a registry; `ui-react` and every package's views are now plain `ts/ui/` modules the generated `menus.ts` alone
  pulls in), and **the shell's hard dependency on `launchpad`**, unchanged.
- **A real `component` view.** The kind and its fixture ship; the first package that needs React for a view proves it on
  a host, and its `test:ui` case lands with it. Its `lvgljs-ui` types are still the shell's 54-line slice
  (`packages/retroplug/ui/types/lvgljs-ui.d.ts`), because the package ships TypeScript source rather than declarations
  and the slice is what `tsc` reads instead; a component pack that needs a widget the slice lacks (a slider, a list)
  extends that SHELL file, which no package can do. Moving what a pack needs into `@retroplug/ui-react`'s own
  declarations (a types package for the host module, generated like the paths) is the fix when a pack needs it.
- **A kit compile** (`host.kits` in the UI host) when a UI feature needs it, as F4b left it.
- **Gamepad axis tokens as action bindings**, and a stick-driven action, if a package wants one (10.2).
- **Windows, macOS, arm64 and CI** for F4c's scripts and packages, as F4a's and F4b's.
- **Folding `tsconfig.ui.json`** into the shell's program (10.12), and widening or narrowing the reopen key (10.4), if the
  stage 2 read shows either matters. The read showed the key moves on every role write; E5's one visible case is a blink.
- **Found while building, for later:**
  - a real `component` view rendered on a host (the kind and its fixture ship; no `test:ui` case can draw one until a
    product lists a package that has one);
  - the generator judges product packages only, so `ui-react` and `ui-fixture` are held by their own `tsc` and not by the
    extend check; a later change that wants the check over unlisted packages must say so;
  - if the generator is ever taught to see a package's `views` (it reads manifests, not TypeScript), the extend rule
    must be generalised to "has views" AND risa must gain the line with it (decision 11);
  - gamepad axis tokens as action bindings (10.2), and the reopen key's width if the blink in E5 ever matters;
  - LSDj's overlay sampler has an unreachable `!state.supported` branch (the reader always answers supported); if
    the reader grows an unsupported decode the branch stops being dead;
  - the `docs/*-plan.md` records and the dated audit docs still name the files this phase deleted; they are records of
    the trees they describe and are not rewritten, with one exception: `docs/ui-shared-utils-audit.md` had its four
    links repointed, because the files it names MOVED and a doc link to a moved file fails `check:docs`;
  - `ReadoutSampler.signature` is optional and both overlays use the default (the lines and the flag); a later sampler whose
    state moves without its lines changing can supply its own.

## 12. As built (the prototype)

The prototype is five commits on the design commit `663632d16`, one per stage, each built by DeepSeek Flash and reviewed
by Opus, with each review's fix pass folded into the commit it belongs to by fixup and autosquash (one pass moved a hunk
back from stage 4 into stage 2, and the last pass amended stage 5), so the five trees stand as final and a stage report's
sha is older than the branch's. **The shas in this section are the PROTOTYPE's**: `d1352d6ef`, `eb20634ea`, `c956580c4`,
`b841dc4e2` and stage 5, the commit that carries this section (so it cannot name its own sha: the tip of
`proto/f4c-react`). The whole prototype is 176 files, +5,337 -973 over the design commit, leaving this document out,
700 of the added lines the menu matrix's recorded JSON. Sections 1 to 11 above were corrected in place to what the
build found; this section is the record of how, and the plan is written against it.

**The stages**, by commit subject and current sha (files changed, lines added and removed):

1. `d1352d6ef feat(f4c): the React kit is a package, and the typecheck follows it` (72, +877 -99): `packages/ui-react` with
   the seven widget files moved and `tagTestId`, `reactView` and the entry new, the shell's 19 files importing it,
   `jsx: react-jsx`, the four host modules and their generated paths, `tsconfig.ui.json` reduced, the generator's extend
   check for a package that declares a UI host module, rule 3's `.tsx` placement and implicit `react/jsx-runtime` use
   with four fixtures, the bundle pattern, `packages/ui-fixture`, the kit walk test.
2. `eb20634ea feat(f4c): the view contract, its registry, and the shell's window and tile slots` (33, +1497 -93):
   `Readable` / `Signal` / `createSignal`, the view types, `files.readPrefix` and `systems.readRam`, the generated
   `VIEWS` with its first checks, `viewSession` and `useViewSession`, the three renderers, `ViewWindow` and `TileViews`,
   the generic `views.open`, `ViewDefinition` deleted, the `PixelView` canvas guard.
3. `c956580c4 feat(f4c): app actions, contributed bindings and the shell's dispatch` (22, +1024 -106): the action
   contract, `GAMEPAD_BUTTON_NAMES`, the key maps over lists, the generated `ACTIONS` with its six checks, `runActions`,
   `withActionDefaults`, the bindings menus' rows over a test action, `MenuContext.actions`.
4. `b841dc4e2 feat(f4c): the LSDj HD player and the two debug overlays are the packages'` (51, +1611 -433): the moves
   (`hdView.ts`, `overlayView.ts`, `debugOverlay.ts` in lsdj and risa), the shell's view files, `lsdjDebug`, `uiViews`
   and `openLsdjHd` deleted, E1's re-record, the package `ui-views` cases and four fixture-gated `test:ui` cases.
5. `feat(f4c): rule 9 and the literal scanner cover ui/, and the ratchet retires` (the tip; 38, +449 -363 without this
   document): rule 9's third root and `UI_IMPORTS`, the scanner's third directory, the ratchet and its runner deleted,
   the fixtures, the docs.

**What each review found, and what the fix pass changed.**

1. *Stage 1 (with fixes, no Critical).* Important: `reactView` declared placeholder types that did not match the
   contract's names (`ComponentViewProps` against `ViewComponentProps`) and `index.ts` published extras, so stage 2 would
   not have been "the one file"; `badgeView`'s `placement` widened to `string`, so the one-line wiring would not
   typecheck; and several files repeated `@retroplug/ui-react` on consecutive import lines. Minors: no case for what
   `reactView` exists for, the kit walk's frozen count and overclaiming comments, stale comments, `surfacesOf` twice, two
   em dashes on `spec/03` changed lines. Controller decision: the extend rule is "declares a UI host module, or has
   examples". Fix pass: `ViewComponentProps` exported from `reactView.ts`, `badgeView` as `satisfies TileView`, one
   import per file, a wrong-props `@ts-expect-error` case, the walk reading specifiers from `ts.preProcessFile`.
2. *Stage 2 (with fixes, no Critical).* Important: `useViewSession` lagged what it rendered in both directions (every
   open of a supported cart flashed the unsupported text for an effect, and a native `frame` could sample a closed
   session); a `component` tile view ignored `visible`; the tile slots mounted conditionally on `when`, so the
   `StableSlot` guarantee (and E4) held only while `when` did not flip; and stage 4 needed the click-through case this
   stage could not run. Minors: `windowViewOf` unused, `PixelView` showing `unsupported` in a tile, package code
   unguarded in render and per frame, comments saying process, `reactView` returning the whole `ViewBody`, an unchecked
   cast in `openSession`, `checkView` assuming a body, a registry-order case that could not tell product order from
   alphabetical, the commit message. Fix pass: the three-state hook with `shownSession` and a controller-side `read`, the
   `visible` subscription at the slot for every kind, one slot per registered view with `when` inside it, the guards
   (5.4, 3.1), `ComponentBody`, `isPixelSession` / `isReadoutSampler`, `checkView` naming the package, the `zeta`
   product fixture.
3. *Stage 3 (with fixes, no Critical).* Important: a key bound to two of the shell's own actions fired both, where the
   old map's last binding won. Minors: the generated `ACTIONS` export unread, the `{ pkg, action }` type declared three
   times, the code-not-name decision untested, `actions-bad-prefix` declaring a key that does not resolve, the refusals'
   RED proving less than it read, a miscounted message, four lines over 120 columns, a read warning naming no view,
   `APP_ACTIONS` newly exported. Controller decision: E9 accepted. Fix pass (RED shown): `addActionId` (one shell action
   per key, later wins, contributed accumulate), `ContributedAction` once, the `Return` / Enter case, a refusal RED per
   case's own reason, `read` naming the view and a dropped readout clearing.
4. *Stage 4 (with fixes, no Critical).* The moves were faithful line for line and E1 was exactly the new rows. Important:
   `overlay-actions` skipped after the press when risa was absent, which would mask a regression; and the `PixelView`
   canvas guard, which stage 4 had to add because the real `lsdj-hd` found the bug, belonged to stage 2 (the plan must
   never carry a broken intermediate: a stage that is not green on its own is a stage the plan cannot replay). Minors:
   the menu fallback host's message, history in test titles, untested null and role branches, a `tjs.env` fixture read,
   `HdSession` typed by the contract's whole host, `hex2` three times, descriptions. Fix pass (a rebase of stages 2 to 4):
   the guard and one `PixelSession.frame` doc sentence folded into stage 2 with its message saying no stage 2 tier can
   show the bug (reproduced: with the guard reverted, the unchanged `lsdj-hd` case fails on `distinctColors(snap) > 2`),
   `overlay-actions` deciding its gate before the press, `windowViewOrReport`, the runner's `__RESOURCES_DIR__`, host
   types as `Host<...>` groups with `satisfies`, `hex2` in `@retroplug/ui/format`, the descriptions.
5. *Stage 5 and the whole prototype (with fixes, no Critical).* A fresh `git archive` of the tip typechecked with no
   `build/` at all, and the REDs reproduce. Important: the ui root's "never `cli/`" was claimed and untested (change its
   `reaches` to `[ui, src, cli]` and every test still passed); and `spec/06`'s checklist told a pack author to put
   `@retroplug/ui-react` in `retroplug.host` (it goes in `dependencies`; `retroplug.host` takes only host modules) and
   misstated the extend rule. Ten minors (stacked JSDoc, `spec/06`'s rule 9 prose and `spec/00`'s shell row naming two
   trees, `spec/03`'s "LVGL primitives" section, `packages/ui`'s description, a fixture comment naming the retired
   ratchet, the scanner's fixture case not failing if `ui` left the list, a commit message crediting a stage 4 hunk,
   two typos). Fix pass (amending stage 5 only; stages 1 to 4 keep their shas): a `cli/run.ts` and a second ui file
   importing it in `shell-ui-relative`, shown RED, the spec text, `SHELL_DIRS`, and `run-scan.mjs` skipping a missing
   directory.

**Departures from the design as committed (`663632d16`), and why.** Each is also corrected in place above.

- **The UI bundle's hash is no oracle** (8.1, decision 12): esbuild heads every module with its path, so moving a file
  moves bytes; stage 1 is held by a normalised diff (+96 bytes, 1,997,971 -> 1,998,067).
- **The extend rule is "has examples or declares a UI host module"** (8.1, decision 11), not "has views": the `.mjs`
  generator cannot see a TypeScript member, and a plain-TypeScript view needs no React path. risa's `tsconfig.json` is
  unchanged. The check judges product packages only, so `ui-react` and `ui-fixture` are held by their own `tsc`.
- **`@retroplug/ui-react` depends on `@retroplug/ui` alone** (4.1): `UiHost` is published by `@retroplug/ui/contribution`,
  and a `@retroplug/core` entry no file imports draws `check:boundaries`' unused-dependency warning.
- **`reactView` returns `ComponentBody`** and `ViewComponentProps` is the contract's, re-exported by the kit (4.2); the
  stage 1 placeholders are gone.
- **`ui-fixture` declares `react` and `react/jsx-runtime` only** (8.6): it names neither `lvgljs` nor `lvgljs-ui`.
- **One `@retroplug/ui-react` import line per file** (4.2), after the stage 1 review.
- **`hex2` is one export of `@retroplug/ui/format`**, re-exported by the kit (4.2, 6.1), not a copy per package, and
  `ui` gained a `./format` export.
- **A `useViewSession.ts` joined the shell's view files** (6.3), the one effect and subscription the renderers share, kept
  out of `viewSession.ts` so the controller stays React-free.
- **The session hook is three-state, and package code is guarded** (3.1, 5.4): a pending session draws nothing, a hide
  drops the session in the render that sees it, `read` goes through the controller, and a throwing `when`, `visible`,
  `open`, `frame`, `sample` or `signature` is reported once and contained. What the user sees on opening the HD view: the
  black window box with nothing in it until the session opens, then the canvas.
- **`PixelView` checks the canvas before it reads a frame, and shows `unsupported` in a window view only** (5.3); the
  guard is stage 2's, found by `lsdj-hd` in stage 4 and folded back.
- **One `StableSlot` per registered tile view, `when` inside the slot** (5.2), each subscribing to `visible` itself.
- **`openSession` carries no generic cast;** the renderers narrow with `isPixelSession` / `isReadoutSampler` (5.4).
- **`windowViewOrReport` is the one wording of the open refusals**, called by App's `openViewOf` and by the menu tests'
  fallback host (5.1); until stage 4 an interim shell-side `lsdj-hd` entry stood in App.
- **One shell action per key, today's winner** (5.5, decision 13); **E9** accepted (decision 14).
- **Key collisions are compared by resolved DPF code** (3.5): `Return` collides with Enter.
- **`buildKeyToAction(actions, contributed)`**, with the contributed ids passed in (`src/` must not reach `ui/`),
  `runActions` taking the list as a parameter, `APP_ACTIONS` private (5.5).
- **`MenuContext.actions` is the injection seam** for the bindings rows, and `ContributedAction` is declared once in the
  contract (5.7, 3.5). The generated module imports `defaultBindingMap` too, and its fixture tree holds shims to the real
  `keyCodes.ts` and `bindingMap.ts` (3.5).
- **`withActionDefaults` always returns a new object** (5.6).
- **The overlays carry no `signature`** (3.1, 6.1, decision 16): the default displays what the old key did.
- **The HD SRAM poll is a countdown of 15, a period of 16 frames** (6.1): `readSram` at frames 0, 16 and 32, the old
  cadence. `HdSession` is exported and the views take `Host<...>` groups and are declared with `satisfies` (6.1).
- **LSDj's sampler `!state.supported` branch is unreachable** (6.1), recorded and kept.
- **E1's record was spliced, not re-taken wholesale** (7): +700 lines, 0 deletions, proved equal to the full new record.
- **The pure runner injects `__RESOURCES_DIR__`** (stage 4), so a pure-tier case may hold a fixture ROM; `lsdj/ui-views`
  is the one `ts` skip-baseline entry (one documented skip). **`overlay-actions` gates on tile 1's instance-menu `Risa`
  row before the press.**
- **Rule 9 as built** (8.3): `UI_IMPORTS` is the table's ten; both lists are module-private; the stale-entry check runs on
  the real tree only; the fixtures are `shell-ui-forbidden`, `shell-ui-relative` (with the `cli/` case),
  `shell-ui-unlisted` and `shell-ui-allowed`.
- **The scanner as built** (8.4): `SHELL_DIRS`, the `literals-ui` fixture with both pre-move hits, `run-scan.mjs`
  skipping a missing directory, the RED an extracted pre-move tree.
- **The docs pass touched more than the design's list** (stage 5): `AGENTS.md`, `spec/00`, `03`, `06`, `07`, `13`,
  `docs/lsdj.md`, `docs/package-split.md`, `packages/ui/package.json`'s description and the two carts', two stale code
  comments, and the comments of the two byte-frozen `test-ui` headers (comments only). The F4b and F4c designs and the
  `docs/*-plan.md` records were not edited, and of the dated audit docs only `docs/ui-shared-utils-audit.md`, whose four
  links were repointed because the files they name moved (`check:docs` reads links by path).

Existing tests whose shape changed, each judged by a stage review: the 15 `openLsdjHd: () => {}` stubs (gone with the
member), `test/input/keymap.test.ts`, `gamepad.test.ts` and `test/menu/leaves.test.ts` (a value became a one-element
list; the capture-row counts 11 -> 13), `core/ts/test/host.test.ts` and `lsdj/ts/test/songImport.test.ts` (the
`FilesHost` literals gained `readPrefix`), `test/graph/extension.test.ts` (loses its views expectation), and the two
byte-frozen `test-ui` view headers (comments). `lsdj-hd`, `lsdj-overlay` and `risa-overlay` pass unchanged. No golden,
baseline, digest, image or pack byte moved, and no `RP_*_UPDATE` was set but the one recorded edge (E1).

**Verification.** A baseline run at `663632d16` and a run at every stage and at the tip were each the parallel kit
(`tip-verify-par.sh`), every leg exit 0, with `pnpm sdl:controller` run alone (`SDL controller OK`); the tip's final run is
`/tmp/f4cpro/s5fv-*`, and `compare.sh` against the baseline reports only the lines below. One kit run (stage 3's fix pass) failed once on
`retroplug-launchpad-test`'s mid-drain swap case, a thread-timing assertion that passed alone and on the re-run (the same
load sensitivity F4b recorded).

| | baseline (`663632d16`) | prototype tip (`proto/f4c-react`) |
| --- | --- | --- |
| `pnpm test` | 219 files, 2286 cases | 226 files, 2359 cases (one documented skip: `lsdj/ui-views`) |
| `test:ui` | 43 files, 49 cases | 47 files, 53 cases (the four new fixture-gated cases) |
| `pnpm typecheck` | | plus `packages/ui-react` and `packages/ui-fixture` |
| parity, native, the CI picture, the kit and render baselines, `render-chunks`, `render-cli` | recorded | identical |
| `test:plugin`, tsan and asan legs, device check, box, device mode | recorded | identical |
| images and the three `bliptoaster` pack byte hashes | recorded | identical |
| recorded menu matrix | | +700 lines (E1), 0 deletions |
| `check:boundaries`, `check:docs` | 0 refusals, 96 files clean | 0 refusals and 0 warnings, 97 files clean |
| UI bundle | 1,997,971 bytes | 2,014,145 bytes |

The cp, cli and render-worker bundles each grew 70 to 72 bytes (`filesHost`'s `readPrefix`); no other bundle moved. The
+73 cases over the baseline are the phase's own tests, less the retired ratchet's three; the four fixture-gated `test:ui`
cases are `overlay-survives-menu`, `hd-view-closes`, `overlay-actions` and `overlay-click-through`. Each of the five
stage trees was also extracted after the final rewrite, proven equal to its commit by a fresh index read (0 diff lines
before and after the runs), and typechecked and tested alone (`menu`, `packages`, `hosts`, `ui`, `input`, `boundaries`,
`source-literals`, the package `ui-views` files, each exit 0 where its file exists); the tip's was a true fresh-checkout
typecheck with no `build/`. The fixture-gated `test:ui` cases ran at the final tree with `RETROPLUG_RESOURCES_DIR` set:
`lsdj-hd`, `lsdj-overlay`, `risa-overlay`, `overlay-actions`, `overlay-survives-menu`, `hd-view-closes` and
`overlay-click-through`, each 1 passed, 0 skipped.

**What acceptance must run beyond the parallel kit** (the stage 5 review's list):

- `pnpm typecheck` in a fresh `git archive` of the final tip BEFORE any build (CI's order), then `node
  tools/check-doc-links.mjs`;
- with the fixtures present (`RETROPLUG_RESOURCES_DIR`) and `RP_FAIL_ON_SKIP=1`: the seven fixture-gated `test:ui` view
  and action cases, the pure `lsdj/ui-views` pixel case, and `pnpm test:native parity` (nothing should move, and that is
  the only proof it did not);
- `pnpm screenshot` for E4 (the strip under the render badge), and one manual look at an overlay with a render running;
- `reaper:all`, `reaper:editor`, `reaper:editor-reopen`, `reaper:editor-autoload`, `reaper:two-instances` and
  `reaper:two-instances-load` (the views and the module-level debug signals now live in package modules; each instance's
  UI runtime must still own its own), and `reaper:params` / `reaper:params-clap` (the bindings menus grew);
- `pnpm sdl:smoke`, and `pnpm sdl:controller` alone;
- BlipToaster and NESVJ against the rebuilt CLI;
- `pnpm check:boundaries`, with the ui->cli fixture shown RED then green;
- the Windows build and `test.bat` via the user's Windows agent (the new packages, the generator's new checks and the
  scanner's missing-directory skip), with macOS and arm64 owed as for F4a and F4b.

The stage trees' proof script symlinks the worktree's `build/` into each tree; acceptance must not copy that symlink, or a
bundle step of `pnpm test` there writes into the shared build.
