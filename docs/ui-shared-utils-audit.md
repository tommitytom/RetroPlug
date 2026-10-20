# UI shared-utils audit (2026-10-19)

A pass over the LVGL React UI ([packages/retroplug/ui](../packages/retroplug/ui)) looking for hacks,
workarounds and repeated patterns that could move into shared utilities. The shared layer that already
exists is good and is what most of the proposals build on: `Box` (theme-chrome reset), `StableSlot`
(the insertChildBefore workaround), `useFocusGroup`, `useNativeEvent`, `useFramePoll`, `createSubscribable`.

Ranked by payoff. Status is tracked inline.

## 1. The full-window overlay stack in App (DONE)

[App.tsx](../packages/retroplug/ui/App.tsx) rendered the same `<Box full-window black><Menu tree onClose/></Box>`
block five times (file browser, close guard, project modals, song import, start menu), and spelled out
"is an overlay up" (`closeGuard.active || modals.active || browser.active || songImport.active`) by hand in
five places.

Real bug from the drift: the right-click handler's copy omitted `browser.active`, so a right-click while
the in-app file browser was open opened the instance menu underneath it, which then appeared when the
browse closed.

Fix: every overlay hook already returns the same `{ active, tree, onClose }` shape. App collects them into
one priority-ordered list ([overlay.ts](../packages/retroplug/ui/lvgl/overlay.ts) `topOverlay`), renders
the first live one through a single `FullWindowMenu`, and derives `overlayActive` once. The close-guard tree
moved into `useCloseGuard`, so all four hooks return the same `Overlay`. Regression:
[overlay-input.test.ts](../packages/retroplug/test-ui/overlay-input.test.ts) (fails on the old App).

## 2. Menu-item builders shared outside menuDefs (DONE)

[menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts) had private `action` / `submenu` / `sep` /
`cycler` / `actionCycler` helpers; every other tree builder hand-rolled item literals instead:
`useProjectModals` and `songImport` each defined an identical keepOpen `btn`, App built the close-guard
tree inline, plus `unsavedRows` and `fileBrowserMenu`.

The hand-rolled copies drifted on read-only rows: `unsavedRows` marks them `disabled` (greyed, skipped by
nav), but the notice body, the song-discard "has unsaved changes" line and the import notice body were
focusable no-op actions, so the cursor landed on them and Enter did nothing.

Fix: a pure [menuItems.ts](../packages/retroplug/ui/screens/menu/menuItems.ts) with the builders plus
`button` (keepOpen action), `infoRow` (disabled readout) and `noticeTree` (body + OK).

## 3. `__rp_*` native hooks: 52 `globalThis as {...}` casts in 16 files (DONE)

Every native seam re-declares its own structural type at each call site; `audioDraft.ts` re-casts
`__rp_getAudioConfig` five times. `Symbol.for("plugin")` is resolved separately in `main.tsx` and
`StoreProvider.tsx`.

Proposal: one `lvgl/nativeHooks.ts` with an `RpHooks` interface and a single `native` handle, plus a
`pluginNamespace()` accessor, used for both calling hooks and installing them (`useCloseGuard`,
`useFileBrowser`, `main.tsx`). One place documents the whole TS/C++ editor surface.

Done as [nativeHooks.ts](../packages/retroplug/ui/lvgl/nativeHooks.ts): `RpHooks` (grouped by which native
file binds each hook), `native` (a live, typed view of globalThis, so test fakes installed later are still
seen) and `pluginNamespace()`. No `globalThis` cast is left in `ui/` outside that file. Out of scope: the
control-plane side in `src/` (e.g. realBackend's `__rp_openFileBrowser` call) still declares its own.

## 4. `useNativeEvent` args are untyped (DONE)

Every subscriber unpacks `(...args)` with `args[0] as number` / `args[1] as boolean` (~12 sites). A typed
channel map (`{ key: [code, press, mod]; frame: [tick]; mouse: [button, press, x, y]; "gamepad-button":
[pad, name, press]; "gamepad-axis": [pad, axis, value]; resize: [w, h]; "file-drop": [paths, x, y] }`)
makes `useNativeEvent("key", (code, press, mod) => ...)` type-checked.

Done: `NativeEvents` in [useNativeEvent.ts](../packages/retroplug/ui/lvgl/useNativeEvent.ts), each channel
annotated with the native files that emit it; every `args[n] as T` is gone. Note the type is only as good
as the emit sites it was read off - nothing checks the C++ side against it.

## 5. LSDj / risa runtime readout twins (DONE)

- `useLsdjRuntime.ts` and `useRisaRuntime.ts` share one skeleton: find the system, check a role, memoise a
  decoder, keep state + a signature ref, dedupe per frame.
- `LsdjOverlay.tsx` and `RisaOverlay.tsx` are the same render shell; `hex2` is defined in both.
- `EmulatorTile.tsx`'s render badge uses the same key-compare-then-setState dedupe.

Proposal: `useFrameSampled(read, signature, deps)` + a `<DebugReadout lines testId>` component.

Related: `sys.roles.find((r) => r.kind === X)?.config` appears ~10 times (menuDefs, App, n8Devices, both
runtime hooks, the HD session) and `systems.find((s) => s.id === id)` 6 times. `roleConfig(sys, kind)` /
`hasRole(sys, kind)` helpers and a `useSystem(id)` hook.

Done: `useFrameSampled(sample, signature)` in [useFramePoll.ts](../packages/retroplug/ui/lvgl/useFramePoll.ts)
(sample per frame, re-render only when the displayed signature moves; `undefined` skips a frame; a new
`sample` identity resets) now backs both runtime hooks and the render badge.
[DebugReadout.tsx](../packages/retroplug/ui/screens/grid/DebugReadout.tsx) is the shared overlay shell + `hex2`.
`findRole` / `roleConfig` / `hasRole` live next to `RoleInstance` in
[systemRoles.ts](../packages/retroplug/src/systemRoles.ts) and replace every `roles.find(...)` in `ui/`
(menuDefs' private `roleConfigFor` is gone); `src/` still has ~12 of its own, untouched. `useSystem(id)` is
in `useStores.ts`. The overlay UI tests now assert the decoded version text, not just the strip.

## 6. lvgljs typing gaps (DONE)

- `View`, `Text`, `Canvas` each cast `as any` (Box, Menu, EmulatorTile, LsdjHdScreen).
- `CanvasHandle` interface duplicated.
- `LV_ALIGN_CENTER`, `LV_ALIGN_BOTTOM_MID`, `LV_IMAGE_ALIGN_CONTAIN` redefined in four files (two comments
  record the CONTAIN/COVER mix-up that already happened once).

Proposal: `lvgl/lv.ts` with the constants and typed `TextEx` / `CanvasEx` wrappers. Also a home for the
binding traps currently documented only inside Menu.tsx comments (`flex-grow` needs `display: flex` on the
child, a Box needs a measured height since there's no LV_SIZE_CONTENT, Texts bubble clicks).

Done: [lv.ts](../packages/retroplug/ui/lvgl/lv.ts) holds `ViewEx` / `TextEx` / `CanvasEx` (the one place the
components are cast, behind prop types for what native accepts), `CanvasHandle`, the event types and
`LV_IMAGE_ALIGN_CONTAIN`; [PixelCanvas.tsx](../packages/retroplug/ui/lvgl/PixelCanvas.tsx) is the shared
nearest-neighbour CONTAIN canvas. The `LV_ALIGN_*` copies are gone: lvgljs-ui already exports `EAlignType`.
No `as any` is left in `ui/`. The binding-trap notes were left in Menu.tsx next to the code they explain.

Found on the way, both in the lv_binding_js submodule (not ours to edit, so recorded here): its Canvas
docs number `LV_IMAGE_ALIGN_CONTAIN` as 14 (lv_image.h says 13; 14 is COVER, the value that once cropped
our tiles), and its event typings spell the method `stopPropogation` while native defines
`stopPropagation`.

## 7. Zoom resolved three ways (DONE)

`layout.resolveZoom` clamps the user default; `SystemGrid.tsx` re-implements it with its own `MIN_ZOOM` /
`MAX_ZOOM` and does not clamp the default; `hitTestTile` has a third inline copy. On a closer look this
was duplication, not a live bug: `fitZoom` clamps its cap, so the three agreed. Done anyway, since the
grid and the hit-test MUST agree: `displayZoom` in `layout.ts` is the one "zoom the grid is drawn at", and
both SystemGrid and hitTestTile call it.

## 8. Smaller items

- DONE: `Menu.tsx` mirrored state into a ref three times (capturing, actionIdx, prompt): `useStateRef`
  ([useStateRef.ts](../packages/retroplug/ui/lvgl/useStateRef.ts)).
- DONE: `const [, bump] = useState(0)` force-updates in App, `useFileBrowser`, `useVersionedPoll`, and App's
  five-subscribable effect: `useForceUpdate` / `useRerenderOn(...subscribes)`
  ([useRerender.ts](../packages/retroplug/ui/lvgl/useRerender.ts)).
- DONE: `lsdjDebug.ts` hand-rolled a listener set; it uses `createSubscribable` now.
- DONE: held-slot press/release routing (useGameInput / useGamepadInput) is `useHeldRouting`, and stick
  edge detection (useGamepadInput / Menu) is `useAxisEdges`
  ([useHeldInput.ts](../packages/retroplug/ui/input/useHeldInput.ts)). Nothing tested this routing, so
  [game-input.test.ts](../packages/retroplug/test-ui/game-input.test.ts) now pins it at the pressButton RPC
  (repeat drop, release-to-original-target across a focus change, stick flips); it passes on the code before
  and after the refactor.
- NOT DONE, on purpose: a cross-file `theme.ts`. Counted, the repeated colours are the accent (Menu.tsx only)
  and black backgrounds, so Menu got local `ACCENT` / `FOCUS_BAR` constants instead.
- DONE (with item 1): `KEY_ESCAPE` redefined in App. DONE (with item 2): the half-deleted doc comment in
  `songImport.ts`.
- OPEN: `ui/lvgl/` mixes framework shims (Box, StableSlot, lv, focus, events, the frame / rerender / state
  hooks) with LVGL-free app logic (`render.ts`, `songImport.ts`, `unsavedRows.ts`, `saveProjectInteractive.ts`,
  `nativeHooks.ts`, the overlay hooks). Splitting it into `lvgl/` + an app-level directory is a pure move,
  but it touches most import lines in ui/ and test/, so it is worth doing as its own change, not folded in.

## 9. `ui/` is not type-checked (DONE)

`pnpm typecheck` covers `src`, `test*`, `cli`, `tools-src`; the `.tsx` UI is reached by nothing in that set
(`react` does not resolve under `tsc`, it comes through lv_binding_js at bundle time), so only the pure
`.ts` files a test imports get checked. A scratch tsconfig pointing `react` / `lvgljs-ui` at
`deps/dpf.js/deps/lv_binding_js/node_modules` checks the whole tree and surfaces real, pre-existing errors:

- (fixed with item 5) `screens/hd/useLsdjHdSession.ts` typed its backend as `HostBackend`, which has no
  `readSram` / `readRam`; it is `ControlPlaneBackend` now. `RisaOverlay.tsx`'s unused import is gone too.
- `menuDefs.ts` uses `console` / `TextEncoder` / `TextDecoder`, which the `lib` setting does not declare.
- `lvgljs` (the native module: `on` / `off` / `createGroup` / `setKeyboardGroup`) has no declaration file.

Done: [tsconfig.ui.json](../packages/retroplug/tsconfig.ui.json) maps `react` to lv_binding_js's @types,
`lvgljs` to dpf.js's typed `runtime/lvgljs/index.ts`, and `lvgljs-ui` to
[ui/types/lvgljs-ui.d.ts](../packages/retroplug/ui/types/lvgljs-ui.d.ts) - a declaration of the slice we
use, because the package ships TS source that fails our strict settings ~1600 times. `pnpm typecheck` runs
it after the main config, so CI (which already installs lv_binding_js first) covers it. It is clean: the
`console` / `TextEncoder` errors above were an artifact of the scratch config missing `src/env.d.ts`.
