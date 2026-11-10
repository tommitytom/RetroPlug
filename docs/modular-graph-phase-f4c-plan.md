# Phase F4c: React in packages - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After F4c no cart's code lives in the shell's `ui/`. `ui/` joins rule 9 and the literal scanner and the F4b
ratchet retires; a package contributes a VIEW (a full-window screen or a layer over an emulator tile) and an ACTION (a
key or pad binding the profiles and the bindings menus carry), and the shell renders and routes both. A view is
declared, not coded in React: its body is `pixels`, `readout` or `component`, and the shell draws the first two itself.
The React kit becomes its own package, `packages/ui-react`, so a pack author never has to use React; the LSDj HD player
and the two debug overlays move into `lsdj` and `risa` as pixels/readout views plus their own actions, and the shell
keeps no cart import at all. **F4c changes no audio, no saved-file format and no graph document:** strict parity, the kit
and render baselines, `render-chunks`, `render-cli`, the `buildGraph` and role goldens, the protocol digests, the
`[packages]` golden, `edio-golden.json`, both device images, the box's and the `bliptoaster` pack's bytes hold
unmodified. Exactly one record moves: the recorded menu matrix's two bindings menus gain a row per contributed action
(E1). Task 6 is the acceptance run.

**Architecture:** Per [modular-graph-phase-f4c-design.md](modular-graph-phase-f4c-design.md), whose sections 1 to 11 are
corrected in place to what the prototype built and whose section 12 records how, and
[package-split.md](package-split.md) (F4c is the React half of its fourth phase, after F4a and F4b). Task 1 lands
`packages/ui-react` with the shell's React widgets moved in by `git mv`, `tagTestId` and `reactView` new, the shell's 19
importing files naming the package, `jsx: react-jsx` in the typecheck base, the four React host modules and their
generated paths, `tsconfig.ui.json` reduced, the generator's extend rule for a package declaring a UI host module, rule
3's `.tsx` placement and implicit `react/jsx-runtime` use, the wider bundle pattern, and `packages/ui-fixture` (a real
workspace package listed by no product, carrying a React view). Task 2 lands the view contract in
`@retroplug/ui/contribution` (`Readable`/`Signal`, the pixel and readout bodies, `ViewDef`, `views`), `createSignal`, the
two host additions (`files.readPrefix`, `systems.readRam`), the generated `VIEWS` with its checks, the shell's
`ui/views/` (`viewSession.ts`, `useViewSession.ts`, the three renderers, `ViewWindow`, `TileViews`), the generic
`views.open`, and `ViewDefinition` deleted. Task 3 lands the action contract (`UiAction`, `UiContribution.actions`),
`GAMEPAD_BUTTON_NAMES`, the key maps over lists, the generated `ACTIONS` with its nine refusals, `runActions`,
`withActionDefaults` and the bindings menus' rows over a test action. Task 4 lands the moves: `lsdj/ts/ui/{hdView,
overlayView,debugOverlay}.ts` and `risa/ts/ui/{overlayView,debugOverlay}.ts`, the shell's seven view files deleted, the
interim `lsdj-hd` entry and `openLsdjHd` gone, E1's record, the packages' `ui-views` cases and four fixture-gated
`test:ui` cases. Task 5 lands rule 9's third root with `UI_IMPORTS`, the scanner's third directory, the ratchet deleted,
and the docs pass including the design's as-built section. Task 6 is the acceptance run.

**Tech stack:** no native build change and no dependency change beyond workspace links: TypeScript (the workspace
packages under `packages/`, the shell in `packages/retroplug/src`, `ui/`, `cli/`, `test/`, `test-native/` and
`test-ui/`), React through `lv_binding_js` as the shell always had it, the F1 package generator
(`packages/retroplug/scripts/lib/packages.mjs`) and the boundary check
(`packages/retroplug/scripts/check-boundaries.mjs`) with the pinned `typescript-5` it loads, esbuild through
`tools/build-session.js` and `tools/build-ui.js`, the pnpm workspace's own runners, and CMake only where the generated
modules are build inputs (`cmake/packages.cmake`, `packages/native/cmake/bundles.cmake`).

**Spec:** [modular-graph-phase-f4c-design.md](modular-graph-phase-f4c-design.md) (sections 3 to 8 are the contracts,
the shell's side, the moves and the proof) and [package-split.md](package-split.md). The mechanism it builds on is F1's
([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md) section 4's `definePackage`, spec/06's "Adding a
package") and F4b's ([modular-graph-phase-f4b-design.md](modular-graph-phase-f4b-design.md): `@retroplug/ui`'s menu
model and contribution types, `retroplug.ui`, the generated `ui/generated/menus.ts`, `host-modules.json` and rule 3's
surface rule, rule 9 and the literal scanner over `src/` and `cli/`, the bundle split, the `ui/` ratchet, and the
recorded menu matrix and its recorder). [modular-graph-phase-f4b-plan.md](modular-graph-phase-f4b-plan.md) is a plan
executed the way this one is.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/f4c-react` in this worktree, on top of the design commit `663632d16`: five commits, one per task (Task 6 is the
acceptance run and commits nothing), each built by DeepSeek Flash and reviewed by Opus, with every review's fix pass
folded into the commit it belongs to by fixup and autosquash (two fix passes rebuilt the history of stages 2 to 4 and
of stage 5, so a stage report's sha is older than the branch's), so the five trees stand as final. A block that creates a
file is that file as the prototype commit left it; one that changes a file is the prototype's diff of it, so `git apply`
it from the step's text; a move is a `git mv` block followed by the prototype's diff of its new path when its bytes
changed; a deletion is a `git rm`. Each task's verification is its stage's own full run of the parallel kit and the legs
its fix pass re-ran (each task's suites step says which run each figure is from); Task 6's is the base's and the final
verification's (`/tmp/f4cpro/base-*` and `s5fv-*`) plus the serial kit's legs and the acceptance-only legs. Every
expected pass, test count and figure is quoted from the stage reports in /tmp/f4cpro, those logs, the design's record,
the controller's record (`results.md`, with every review's verdict) or the user's decisions, which is what the generator checks; a figure no log records is said to be so, and
the step asks for it. When this plan was generated it was replayed in a scratch clone of `663632d16`, every block
written, moved or applied in order, and each task committed with the command given: each commit's tree came out exactly
as the prototype's, and each commit message as its. The prototype commits, by task:

- Task 1 `d1352d6ef`: feat(f4c): the React kit is a package, and the typecheck follows it
- Task 2 `eb20634ea`: feat(f4c): the view contract, its registry, and the shell's window and tile slots
- Task 3 `c956580c4`: feat(f4c): app actions, contributed bindings and the shell's dispatch
- Task 4 `b841dc4e2`: feat(f4c): the LSDj HD player and the two debug overlays are the packages'
- Task 5 `8987a27f3`: feat(f4c): rule 9 and the literal scanner cover ui/, and the ratchet retires
- Task 6 is the acceptance run and commits nothing.

**Where this plan departs from the design as committed (`663632d16`).** The design's section 12 records all of it;
this plan follows what was built. In short (a bullet quotes the record where it uses quotation marks):

- **The UI bundle's hash is no oracle** (design 8.1, its decision 12): esbuild heads every module with its path, so
  moving a file moves bytes. Stage 1 is held by a normalised diff instead: `1,997,971 -> 1,998,067` bytes, and with the
  module headers dropped and the helper numbering canonicalised the two bundles' line multisets are equal but for two
  blank lines and `tagTestId`'s body, which reads `globalThis.__rp_tagTestId` (`native` in `host/nativeHooks.ts` IS
  `globalThis`). The stage 1 report is the record.
- **The generator's extend rule is "has examples or declares a UI host module"** (design 8.1, its decision 11), not
  "has views": the generator is `.mjs` and reads manifests; a TypeScript `views` member is invisible to it, and a
  plain-TypeScript view needs no React path. `risa`'s `tsconfig.json` is therefore unchanged. The check judges product
  packages only, so `ui-react` and `ui-fixture` are held by their own `tsc`.
- **`@retroplug/ui-react` depends on `@retroplug/ui` alone** (design 4.1 lists `core`): `UiHost` is published by
  `@retroplug/ui/contribution`, and a `@retroplug/core` entry no file imports draws `check:boundaries`' unused
  dependency warning.
- **`reactView` returns the contract's `ComponentBody`**, and `ViewComponentProps` is the contract's, re-exported by
  the kit; the stage 1 placeholders are gone.
- **One `@retroplug/ui-react` import line per file** (after the stage 1 review): `EmulatorTile` had six, `LsdjHdScreen`
  five, `App` and `Menu` three and `useUiServices` two, merged to one each.
- **`ui-fixture` declares `react` and `react/jsx-runtime` only**: it names neither `lvgljs` nor `lvgljs-ui` (which would
  warn as unused); `ui-react`'s own manifest declares all four.
- **`hex2` is one export of `@retroplug/ui/format`** (a new export), re-exported by the kit: a plain-TypeScript package
  cannot import the kit for a string format, so both overlays import it from `ui`.
- **A `useViewSession.ts` joined the shell's view files** (design 6.3's list has six): it is the one effect and
  subscription the three renderers share, kept out of `viewSession.ts` so the controller stays React-free for `pnpm
  test`.
- **The session hook is three-state, and package code is guarded** (design 5.4): a pending session draws nothing, a
  hide or a gone system drops the session in the render that sees it, a per-frame reader reaches the session the
  controller holds NOW, and a throwing `when`, `visible`, `open`, `frame`, `sample` or `signature` is reported once and
  contained. What the user sees on opening the HD view: the black window box with nothing in it until the session
  opens (one effect), then the canvas; the unsupported text only ever means a cart the view applies to and cannot
  read.
- **`PixelView` checks the canvas before it reads a frame, and shows `unsupported` in a window view only** (design
  5.3). The guard is stage 2's, found by `lsdj-hd` in stage 4 and folded back: the HD canvas's `frame()` is
  flush-and-diff, so a first frame read while the ref was null stayed black for good. Reproduced during the rebase
  (with the guard reverted the unchanged `lsdj-hd` case failed on `distinctColors(snap) > 2`).
- **One `StableSlot` per REGISTERED tile view, `when` checked inside it** (design 5.2), each subscribing to `visible`
  itself for every body kind, so a flip under a live tile swaps a child instead of appending a slot over the border and
  the badge (E4 then holds for a flipping `when` too).
- **`openSession` carries no generic cast;** the renderers narrow with `isPixelSession` / `isReadoutSampler`, and the
  guards themselves are in `viewSession.ts` so they are tested with no React.
- **`windowViewOrReport` is the one wording of the open refusals** (design 5.1), called by App's `openViewOf` and by
  the menu tests' fallback host; until stage 4 an interim shell-side `lsdj-hd` entry stood in App, and stage 4 deleted
  it with `uiViews`.
- **One shell action per key, the later binding winning as the old map did** (design 5.5, its decision 13); **E9** is
  accepted (its decision 14): a backtick the user bound to a shell action now toggles the overlays AND runs that action.
- **Key collisions are compared by resolved DPF code** (design 3.5): `Return` collides with the Game Boy Start default
  `Enter` (both 0x0d), and `z` with Z.
- **`buildKeyToAction(actions, contributed)`** takes the contributed ids as a parameter, `runActions` takes the list as
  an argument, and `APP_ACTIONS` is module-private: `src/` must not reach `ui/`, and the pure tier drives a test action.
- **`MenuContext.actions` is the injection seam** for the bindings rows, and `ContributedAction` is declared once, in
  the contract. The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES`, the two default tables and
  `defaultBindingMap` from `../../src/`, and its fixture tree holds one-line shims to the real `keyCodes.ts` and
  `bindingMap.ts` (aliased by the driver) so the checks cannot drift from a copy.
- **`withActionDefaults` always returns a new object** (design 5.6), even when nothing is filled: App memoises it on
  the profile, so the identity changes exactly when the profile does.
- **The overlays carry no `signature`** (design 3.1, its decision 16): the default (the lines and the flag) is a
  function of exactly the fields the old hand-written key covered, so it displays what the old key did.
- **The HD SRAM poll is a countdown of 15, a period of 16 frames**: `readSram` at frames 0, 16 and 32, the old cadence.
  `HdSession` is exported so its cadence is testable over a fake host.
- **The views take `Host<...>` groups and are declared with `satisfies`** (design 6.1): a caller sees the minimum the
  view reads, not the contract's whole `UiHost`, and the package tests need no cast.
- **LSDj's overlay sampler `!state.supported` branch is unreachable** (the reader always answers supported, and `open`
  already refuses a reader with no layout); it stays as the contract's defensive answer, recorded rather than
  fabricated.
- **E1's record was spliced, not re-taken wholesale** (design 7): the two bindings submenus' `children` came from a
  throwaway recorder over the new code, and a deep comparison proved the spliced file equal to the full new record. The
  diff is `+700` lines, 0 deletions, 70 hunks.
- **The pure runner injects `__RESOURCES_DIR__`** (from `RETROPLUG_RESOURCES_DIR`), so `lsdj/ui-views` holds a pixel
  case that SKIPs without the ROM; it is the one `ts` entry in the skip baseline. **`overlay-actions` decides its
  fixture gate BEFORE the press** (tile 1's instance menu names `Risa` only when the risa drop really loaded it), so a
  broken risa toggle fails instead of skipping.
- **Rule 9 as built** (design 8.3): `UI_IMPORTS` is the design's ten, both lists are module-private (`SHELL_IMPORTS`
  lost its export), the unused-entry check runs on the real tree only, and the fixtures are `shell-ui-forbidden`,
  `shell-ui-relative` (with the `cli/` reach as a second file, asserted in walk order), `shell-ui-unlisted` and
  `shell-ui-allowed`.
- **The scanner as built** (design 8.4): one `SHELL_DIRS = "src,cli,ui"` constant passed by the real case and by the
  `literals-ui` fixture case, and `run-scan.mjs` skips a directory that does not exist (as the boundary check's walk
  does), because the fixture holds only `ui/`.
- **The docs pass touched more than the design's list** (stage 5): `AGENTS.md`, `spec/00`, `03`, `06`, `07`, `13`,
  `docs/lsdj.md`, `docs/package-split.md`, `packages/ui/package.json`'s and the two carts' descriptions, two stale code
  comments, and the comments of the two byte-frozen `test-ui` headers (comments only: every id, label, key and text is
  unchanged). The F4b design, the `docs/*-plan.md` records and the dated audit docs were not edited.

**The behaviour edges** (design section 7, each with what was found). None changes audio, a saved file or a graph
document.

- **E1. The bindings menus gain a row per contributed action**: `LSDj Debug Overlay` and `risa Debug Overlay` in each of
  Keyboard Bindings and Gamepad Bindings, after `Cycle Instances (Back)`. The recorded matrix holds the bindings menus
  in every case, so its diff is four new items per case and nothing else (700 added lines, 0 deletions); it is
  re-recorded in Task 4, when the two real actions arrive, and the diff is the record.
- **E2. Backtick is rebindable per overlay.** With the defaults a press still flips both overlays; a user who rebinds
  or clears one has two independent toggles, and the two flags can then disagree. Both start off.
- **E3. A stored profile that already maps a contributed id keeps its own mapping.** An older profile has no such id
  and reads the default; the file is not rewritten until an edit.
- **E4. The overlay's z-order against the focus border and the render badge** depended on mount history; in its slot
  the strip always sits under both, and only where it overlaps the badge (both at the tile's bottom) can be seen. It
  holds for a flipping `when` too. Seen by the `pnpm screenshot` look Task 6 describes.
- **E5. A role edit reopens the views of that system** (the reopen key includes `roles` by identity): with the instance
  menu open the tile is unmounted, so the one reachable visible case is linking instance B to the LSDj instance A from
  B's menu while A's tile still shows; A's overlay reopens and blinks out for a frame.
- **E6. `host.views.open` of an unknown id, a `tile` view's id, or a system the view's `when` rejects is reported and
  opens nothing**; before, an unknown id was ignored silently.
- **E7. A contributed action that throws is reported and the rest of its press still runs**; before, the backtick
  handler could not throw.
- **E8. `extension.test.ts` loses its views expectation** with the seed (`ViewDefinition`); no production code changes
  for it.
- **E9. A backtick the user bound to a shell action toggles the overlays AND runs that action** (controller, stage 3
  review). Unreachable without that deliberate rebind; a key bound to two SHELL actions keeps today's single winner.

**The one record: E1, and nothing else.** Never set `RP_PARITY_UPDATE`, `RP_KIT_GOLDEN_UPDATE`, `RP_RENDER_GOLDEN_UPDATE`,
`RP_GOLDEN_UPDATE`, `RP_GRAPH_GOLDEN_UPDATE`, `RP_PLAN_BLOB_UPDATE`, `RP_PRODUCT_REGISTRATIONS_UPDATE` or
`RP_SKIP_BASELINE_UPDATE`. Strict parity (30 files, 250 cases), the kit and render baselines, `render-chunks`,
`render-cli`, the `buildGraph` and role goldens, the protocol digests, the `[packages]` golden, `edio-golden.json`, both
device images, the box's and the pack's bytes are this phase's proof that nothing moved, and each task's suites step
runs them. The recorded menu matrix changes in Task 4 by its bindings items only (the diff is quoted there and the
blob hash pins the result); the skip baseline gains only the entries Task 4's new files document (five of them, checked
from the file's own JSON). Every other existing test passes unchanged, but for the shape changes its own step names. A
golden, parity or digest change is a bug to find, never a re-record.

**The user's decisions** (`.superpowers/f-split/decisions.md`, the `### F4c` section, 2026-11-10; all approved by the
user that day), and the controller's decisions taken while the prototype ran:

- **The React kit is a package of its own** (`packages/ui-react`, `@retroplug/ui-react`), separate from
  `@retroplug/ui`, which stays plain TypeScript: a pack author must not have to use React (user, option C).
- **The view contract is declarative**, with a body of one of three kinds, `pixels`, `readout` or `component`; the shell
  renders pixels and readout itself (user, option A). The React half ships now, and the component kind is proved by a
  fixture pack rather than rendered (user, option A).
- **The typecheck:** `jsx: react-jsx` in the base, the four React host modules with `types`, generated paths, a
  package with a UI host module extends the generated host-modules config, rule 3 refuses a `.tsx` outside a `ts/ui/`
  tree (user, option A).
- **Carts contribute their own key bindings and the shell routes them** (user, option B), as **app actions**
  (`{ id, label, keys, pad?, run }`) in the binding profiles and the bindings menus, rebindable, additive with no
  migration, two contributions on one key both running (user, option A). The package name is `packages/ui-react`, not
  "ui-kit".
- **Each overlay's visibility is its own `Readable<boolean>`** (decision 9), so backtick still flips both through two
  actions, and a rebind separates them.
- **The controller decisions of the draft and the reviews:** the reopen key is `(view, system, romPath, roles)` by
  identity (a stage 2 read of `setRoleConfig` confirmed the cost); contributed actions fire in the backtick's states
  today, not gated on `isMenuModalActive`; a default colliding with a Game Boy button default is refused too;
  `views.open` checks the view's `when`; the component fixture is a real unlisted workspace package
  `packages/ui-fixture`; E1 is recorded in Task 4 (the two real actions), while Task 3 proves the rows over a test
  action; E4 is recorded as "the z-order was mount-history dependent, the slot fixes it under the border and the
  badge"; the extend rule is "declares a UI host module, or has examples"; E9 is accepted; a key bound to two shell
  actions keeps today's single winner.
- **Model routing** (user 2026-11-07, unchanged for this phase): DeepSeek Flash built the prototype's stages and this
  plan generator; Flash-Next (xhigh) executes the plan's tasks and the acceptance run; Opus reviewed each prototype
  stage and the whole. The controller coordinates, reviews plans and risky code. The design document was drafted by an
  ANTHROPIC model (the user).

**HARDWARE SAFETY. A real NES with an Everdrive N8 Pro, a Pico box, a probe, a Zoom L6 and MIDI interfaces are wired
in. No step in this plan may touch any of them, and these rules are absolute:**

- **Never touch the NES, the N8 cart, the Pico box or the probe**, by serial port, by USB, by a flash tool or by hand.
  The moved N8 tools (`n8-load`, `n8-play`, `n8-bridge`, `n8-sync`) are exercised only by their existing fake-backed
  tests (`pnpm test:n8` over fake port factories, and the native tool tests over scripted backends). Every device step
  here happens on a **build**: `pnpm device:check` and `pnpm device:box` compile their images and run the check's plan
  in an arena, built and never flashed.
- **Nothing that lists or opens a system MIDI port may run, with one exception.** `pnpm sdl:controller`
  (`tools/run-sdl-controller.sh`) is that exception: it sets `RP_MIDI_PORT_ALLOW` itself, from its two ALSA fakes' full
  port names, refuses a caller that preset the variable, lists before it claims anything, and fails the run when a
  listed port is not one of its fakes. Run it as `pnpm sdl:controller`, never a bare `retroplug-sdl`, never with the
  variable preset, and never any other MIDI-port-enumerating harness. **Never touch the Zoom L6 or any MIDI interface.**
- **`pnpm sdl:smoke` runs under a scratch `RETROPLUG_USER_CONFIG_DIR` with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which
  matches nothing** (an EMPTY list means every port). It sets that itself; do not remove it or export a list of your
  own.
- **Never run `amidi`, `aconnect`, `aseqdump`, `aplaymidi` or any other port tool against this machine**, and never
  build or run with a saved enabled `controlSurface` in the real `~/.config/retroplug`: a saved, enabled pair is
  claimed at startup and its close bytes go out at quit, which is why the SDL tools take a scratch config dir.
- **`pnpm test`, `test:plugin`, `test:native`, `test:ui`, `device:check`, `device:box` and the CLI legs open no
  port**: their MIDI fakes are scripted openers in C++ and TypeScript. If a run seems to be waiting on MIDI, stop and
  report it rather than reaching for a queueing tool.
- `deps/sameboy` shows as modified after any configure (two tracked patches). That is expected; never stage, reset or
  stash it, and never touch any submodule (`deps/dpf.js` and everything under it included).

**Ground rules for whoever executes this:**

- This plan lives at `docs/modular-graph-phase-f4c-plan.md` and is committed before Task 1 (a docs commit of its own):
  the header's links are relative to that location; every command and path in the steps is relative to the worktree
  root.
- Commit after each task (the commands are given; Task 6 commits nothing unless a step fails and is fixed, in a commit
  of its own, named for what it fixes). A commit message is a subject line, a blank line, then the body, and the
  prototype's bodies are quoted whole: use `git commit -F -` as the blocks show. No `Co-Authored-By` or "generated
  with" trailer. Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context lines of older code, the specs, and a tool's own output
  a diff quotes): leave those be. The prototype's own passes removed every emdash from an added line.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the session
  itself. Never bare `git stash`, never switch branches with uncommitted work in the tree, and never `rm -rf build` to
  fix CMake: investigate.
- **Every whole-file block below (a `Create`) is followed by the `sed -n` range that extracts it from this plan and by
  the blob hash the file must have:** write or extract the file, then `git hash-object <path>` must print the hash the
  step quotes, which is the prototype commit's blob. **Every diff block is followed by its extraction, a
  `git apply --check`, and the blob hash of each file it changes**, so an applied diff is the prototype's bytes and not
  just something that applies. There are no temporary `patch` blocks in this plan: each RED is a recorded log or a
  derivation from a parent tree, and the step says which.
- **The generated modules are never committed.** `packages/retroplug/src/generated/{packages,consoles,protocol,
  scriptNodes}.ts`, `packages/retroplug/ui/generated/menus.ts`, `packages/retroplug/cli/generated/tools.ts`,
  `packages/retroplug/tsconfig.host-modules.json` and `packages/retroplug/tsconfig.paths.json` are written by
  `packages/retroplug/scripts/lib/packages.mjs`, gitignored, and regenerated by the runners, `pnpm typecheck` and the
  CMake `retroplug-ts-packages` target. A worktree a prototype build ran in holds all of them, untracked and ignored
  (this one does); no `git status` list below shows them. If one ever does, `.gitignore` is wrong: stop and report it.
- **The lockfile moves in ONE task, and only by workspace links.** Task 1 adds the two packages and the shell's
  dependency, so it runs `pnpm install --offline` (the prototype's command: **not** `--frozen-lockfile`, and never a
  bare re-resolve that prunes another platform's optional packages) and quotes the whole `pnpm-lock.yaml` diff, which
  is part of its changed-file diff block. Tasks 2 to 5 add no dependency and run
  `pnpm install --frozen-lockfile --offline`, checking the lockfile's blob.
- **Case counts are exact. Assertion totals are exact too, with three exceptions:** a run of
  `retroplug-engine-graph-test` reports a total that moves from run to run (its threaded cases assert in loops), and so
  do `retroplug-launchpad-test`'s and the ThreadSanitizer run of `[script-pack]`; for those the case count must match and
  the total is the prototype's, quoted for scale. Catch2 pads its `test cases:` columns with spaces, which the quotes
  below collapse to one. `pnpm test`'s final line counts FILES; a case total is quoted from a report and is not the
  runner's own line, and the `jobs=N` a runner prints is the pool it was given (the kit's caps), not a figure to match.
- **The Catch2 binaries and `retroplug-render-host-test` are `EXCLUDE_FROM_ALL` or outside the default build:**
  `cmake --build build` does not rebuild them, so a library edit needs the target built by name before a run
  (`pnpm test:plugin`, `test:n8` and `test:native` build theirs first, and the render-host tool is built by name).
  **A manifest change needs `cmake -S . -B build` first** (the generator target's inputs are the manifests), which is
  why Task 1 configures before it builds.
- **The scratch root, and one filesystem rule.** Every scratch path this plan writes is under `/tmp/f4c-accept`,
  `/tmp/f4c-tree-<n>` or `/tmp/f4c-v*`; none is inside the repository. The per-task and fresh-archive recipes run
  `pnpm install --frozen-lockfile --offline` in the tree and give the pure runner the worktree's tjs binary through
  `RETROPLUG_TJS`; **a task's extracted tree never links this worktree's `build/`**, because a run that wrote there
  would write into this worktree. Do not `cp -al node_modules` into a scratch tree either (a hardlink cannot cross a
  filesystem, and a plain symlink makes pnpm's per-package links resolve back into this worktree).
- The build trees were last built at the prototype's tip, so each task's first build recompiles what changed and
  regenerates the generated modules; that is expected. Task 6 rebuilds all of it and hashes the images from the dirs it
  made.
- **When a step's result differs from its Expected, stop and report.** A different exit status, case count or quoted
  text is a finding. Three exceptions: an assertion total the rule above calls moving; a predicted failure whose
  failing file, case or target is the one predicted but whose wording differs (the red steps say which are predicted);
  and a device script's trap-probe lines, whose object path and offsets are this devcontainer's and this binutils' (the
  device scripts judge a probe by its own object and the two symbol names, `__wrap__Znwj` and `__wrap_malloc`). Never
  edit code, a test or a golden to make a step pass. In Tasks 1 to 5 each task's tree must equal its prototype commit,
  so a real defect found there is reported, not fixed; a fix belongs to a commit of its own after the plan.
- **Track progress outside the committed plan:** never tick its checkboxes. Ticking one modifies
  `docs/modular-graph-phase-f4c-plan.md`, and every task's expected `git status` list would then gain a line it does
  not show.
- The Windows build has not seen this tree (the prototype is local and Linux-only), so an MSVC-only warning or error is
  a real finding to report, not a stale quote: Task 6 states the owed items, and no step below builds on Windows. macOS
  and the arm64 Linux CI job have not run either. **No step below needs a real device, and none may touch one.**

**Commit this plan before Task 1.** Before the commit, check where the tree is: `git rev-parse --abbrev-ref HEAD` must
print `feature/modularize`, `git rev-parse --short=8 HEAD` must print `663632d1`, and
`git status --short --untracked-files=all` must show ` m deps/sameboy` and nothing else (the generated modules are
ignored, so they do not appear). If the branch or the commit differs, stop: every step below assumes the design commit
on a tree with nothing else in it. (`git -C deps/sameboy status --short` shows the five files the two tracked SameBoy
patches change: `Core/apu.c`, `Core/apu.h`, `Core/camera.c`, `Core/random.c` and `Core/sgb.c`.) The run below copies the
plan to its home in the repository and commits it on its own, so every later task's `git status` and `git add` see a
clean tree:

````bash
cp .superpowers/f4cplan/plan.md docs/modular-graph-phase-f4c-plan.md
git add -- docs/modular-graph-phase-f4c-plan.md
git commit -F - <<'EOF'
docs: the F4c implementation plan

The plan for Phase F4c, React in packages, generated from the prototype's five commits on proto/f4c-react: the React
kit as its own package with the typecheck following it; the declarative view contract, its generated registry and the
shell's window and tile slots; app actions, the contributed bindings and the shell's dispatch; the LSDj HD player and
the two debug overlays as their packages' views and actions, with the menu matrix's one recorded edge; and rule 9 and
the literal scanner over ui/, with the ratchet retired and the docs.

Every expected count is quoted from a stage report, a stage log, the design's record, the reviews or the user's
decisions, and the code was replayed commit by commit against the prototype's trees.
EOF
````

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/ui-react/` | new: the React kit's one export (`ts/ui/index.ts`) over `Box`, `lv` (`ViewEx`, `TextEx`, `CanvasEx`, the CONTAIN alignment), `PixelCanvas`, `useNativeEvent`, the three frame hooks, `useForceUpdate`/`useRerenderOn`, `DebugReadout`, `tagTestId`, `reactView` and the re-exported `hex2`; `ts/test/reactView.test.ts` (Task 1) |
| `packages/ui-fixture/` | new: a real workspace package listed by NO product, with a `tile` view through `reactView`; the component kind's proof by typecheck, rule 3, the generator and the bundle split (Task 1) |
| `packages/ui/ts/` | plain TypeScript: `contribution.ts` (the view and action types plus F4b's rows and services), `menu/subscribable.ts` (`createSignal`), `format.ts` (`hex2`), `ts/test/signal.test.ts` (Tasks 2, 3, 4) |
| `packages/lsdj/ts/ui/` | `hdView.ts` (the window `pixels` view, `HdSession`), `overlayView.ts` (the `readout` view), `debugOverlay.ts` (the signal and the action), `index.ts`'s `defineUi({ views, actions })`, `ts/test/ui-views.test.ts` (Task 4) |
| `packages/risa/ts/ui/` | `overlayView.ts`, `debugOverlay.ts`, `index.ts`, `ts/test/ui-views.test.ts` (Task 4) |
| `packages/core/ts/host.ts` | the two host additions: `FilesHost.readPrefix`, `SystemsHost.readRam` (Task 2) |
| `packages/retroplug/ui/` | `views/` (`viewSession.ts`, `useViewSession.ts`, `PixelView.tsx`, `ReadoutView.tsx`, `ComponentView.tsx`, `ViewWindow.tsx`, `TileViews.tsx`), `input/appActions.ts`, `host/menuContributions.ts` (the registry readers and `windowViewOrReport`), `host/uiHost.ts`, `App.tsx`, `screens/grid/EmulatorTile.tsx` and `SystemGrid.tsx`, `screens/menu/menuDefs.ts` (Tasks 2, 3, 4) |
| `packages/retroplug/src/` | `hostGroups.ts` (`readPrefix`), `keyCodes.ts` (`GAMEPAD_BUTTON_NAMES`, the maps over id lists), `bindingMap.ts` (`withActionDefaults`), `graph/defineExtension.ts` (the seed deleted) (Tasks 2, 3) |
| `packages/retroplug/scripts/` | `lib/packages.mjs` (`VIEWS` and `ACTIONS` with their load-time checks, the extend rule), `check-boundaries.mjs` (rule 3's `.tsx` and rule 9's third root with `UI_IMPORTS`), `run-tests.mjs` (`__RESOURCES_DIR__`), `skip-baseline.json` (Tasks 1, 2, 3, 4, 5) |
| `packages/retroplug/test/`, `test-ui/`, `test-native/` | the generator and boundary fixtures, the view-session and action cases, the bindings rows, the matrix (E1), the four new fixture-gated `test:ui` cases, the kit walk and the pre-move scanner fixture, and every changed existing test (Tasks 1 to 5) |
| `packages/retroplug/{package.json,host-modules.json,tsconfig*.json}`, `tsconfig.base.json`, `pnpm-lock.yaml` | `jsx: react-jsx`, the four React host modules and their generated paths, the two new packages and the shell's dependency (Task 1) |
| `docs/`, `spec/`, `AGENTS.md` | Task 1's path fixes, Task 4's two link fixes, Task 5's docs pass and the design's as-built record |

---
### Task 1: `ui-react`, and the typecheck that follows it

Design sections 4, 8.1 and 8.2, and the design's first stage. The shell's React half becomes a package of its own, and
the typecheck learns what a React file needs: JSX in the base config, the four React host modules as generated paths,
and rule 3's two new refusals. Nothing about behaviour changes; the moves are `git mv`s and the shell's edits are
import specifiers, plus `tagTestId`, which moves out of `StableSlot` so the function exists once.

The package is declared by no product (as `ui` and `tracker` are not): it contributes no `app`, `consoles`, `protocol`,
`ui` or `cli` module, so the generator never reads it and the product list does not change. `packages/ui-fixture` is
likewise listed by no product, and is the component-body proof: a real package whose `.tsx` draws through the kit,
typechecked, judged by rule 3, accepted by the generator and asserted absent from every bundle.

**Files:**

- Create (30): `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json`, `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx`, `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts`, `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json`, `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json`, `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts`, `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json`, `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json`, `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx`, `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json`, `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json`, `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx`, `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts`, `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json`, `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/run-ui-kit-walk.mjs`, `packages/retroplug/test/boundaries/ui-kit-imports.test.ts`, `packages/ui-fixture/package.json`, `packages/ui-fixture/ts/ui/Badge.tsx`, `packages/ui-fixture/ts/ui/index.ts`, `packages/ui-fixture/tsconfig.json`, `packages/ui-react/package.json`, `packages/ui-react/ts/test/reactView.test.ts`, `packages/ui-react/ts/ui/index.ts`, `packages/ui-react/ts/ui/reactView.ts`, `packages/ui-react/ts/ui/tagTestId.ts`, `packages/ui-react/tsconfig.json`
- Move (7 files, each by the `git mv` its step prints; 2 of them changed too and carry their diffs after their moves): from `packages/retroplug/ui/hooks/`, `packages/retroplug/ui/lvgl/`, `packages/retroplug/ui/screens/grid/`
- Modify (35 files, as one diff block per group its step names: 6 under `packages/retroplug/ui/app/`; 6 under `packages/retroplug/ui/screens/grid/`; 4 under `packages/retroplug/`; 2 under `packages/retroplug/test/packages/`; 2 under `packages/retroplug/ui/input/`; 2 under `packages/retroplug/ui/lvgl/` and the rest)

- [ ] **Step 1: The widgets' move: seven files, two of them changed on the way**

Seven `git mv`s into `packages/ui-react/ts/ui`, with the destination directory made first: `lv.ts`, `PixelCanvas.tsx`,
`Box.tsx`, `useNativeEvent.ts` and `useRerender.ts` are byte identical, `useFramePoll.ts` changes one import
(`./useRerender`, the moved name) and `DebugReadout.tsx` two (`./Box` and `./tagTestId`). A moved file that changed
carries its own diff of the NEW path after the move, which is what `git apply` takes.

The moves, as one script (each `git mv` stages its move, and `set -e` stops at the first that fails):

````bash
mkdir -p packages/ui-react/ts/ui
set -e
git mv packages/retroplug/ui/lvgl/Box.tsx packages/ui-react/ts/ui/Box.tsx
git mv packages/retroplug/ui/screens/grid/DebugReadout.tsx packages/ui-react/ts/ui/DebugReadout.tsx
git mv packages/retroplug/ui/lvgl/PixelCanvas.tsx packages/ui-react/ts/ui/PixelCanvas.tsx
git mv packages/retroplug/ui/lvgl/lv.ts packages/ui-react/ts/ui/lv.ts
git mv packages/retroplug/ui/lvgl/useFramePoll.ts packages/ui-react/ts/ui/useFramePoll.ts
git mv packages/retroplug/ui/lvgl/useNativeEvent.ts packages/ui-react/ts/ui/useNativeEvent.ts
git mv packages/retroplug/ui/hooks/useRerender.ts packages/ui-react/ts/ui/useRerender.ts
````

Run: `bash -c 'git status --porcelain --untracked-files=no | grep -c "^R"'`
Expected: `7`: every move of this task so far is staged (7 of them here), and none failed.

Its change (`packages/retroplug/ui/screens/grid/DebugReadout.tsx` -> `packages/ui-react/ts/ui/DebugReadout.tsx`, 92% similar), as a diff of the new path:

````diff
diff --git a/packages/ui-react/ts/ui/DebugReadout.tsx b/packages/ui-react/ts/ui/DebugReadout.tsx
index f94de4fde..078da39cb 100644
--- a/packages/ui-react/ts/ui/DebugReadout.tsx
+++ b/packages/ui-react/ts/ui/DebugReadout.tsx
@@ -2,8 +2,8 @@
 // tile's bottom edge carrying a few lines of live readout. Each overlay only decides WHAT the lines say.
 import { EAlignType, Text } from "lvgljs-ui";
 
-import { Box } from "../../lvgl/Box";
-import { tagTestId } from "../../lvgl/StableSlot";
+import { Box } from "./Box";
+import { tagTestId } from "./tagTestId";
 
 const HEIGHT = 44;
 
````

Run: `sed -n '409,423p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/ui-react/ts/ui/DebugReadout.tsx`
Expected: `078da39cb8d9cad6064e466b710e9df4ea940841`: the blob d1352d6ef holds for this file, so the applied diff is the prototype's change to it, byte for byte.

Its change (`packages/retroplug/ui/lvgl/useFramePoll.ts` -> `packages/ui-react/ts/ui/useFramePoll.ts`, 98% similar), as a diff of the new path:

````diff
diff --git a/packages/ui-react/ts/ui/useFramePoll.ts b/packages/ui-react/ts/ui/useFramePoll.ts
index 97971fffe..90f22c3be 100644
--- a/packages/ui-react/ts/ui/useFramePoll.ts
+++ b/packages/ui-react/ts/ui/useFramePoll.ts
@@ -19,7 +19,7 @@
 import { useEffect, useRef, useState } from "react";
 
 import { useNativeEvent } from "./useNativeEvent";
-import { useForceUpdate } from "../hooks/useRerender";
+import { useForceUpdate } from "./useRerender";
 
 /** Run `fn` on every `frames`-th render frame. The counter is per-component, not global. */
 export function useFrameDivider(frames: number, fn: () => void): void {
````

Run: `sed -n '435,447p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/ui-react/ts/ui/useFramePoll.ts`
Expected: `90f22c3bedb3e29d211cc22cd81b2ec6dddf26c3`: the blob d1352d6ef holds for this file, so the applied diff is the prototype's change to it, byte for byte.

- [ ] **Step 2: The kit's new files: the entry, `reactView`, `tagTestId`, and the package's own config**

`index.ts` re-exports every module of the tree but itself, plus `hex2` from `@retroplug/ui/format` and the two new
modules; a test in step 8 holds that nothing is left out. `reactView(Component)` is the typed way to build a
`component` body: the contract's `component` field is `unknown`, so a pack author cannot hand the shell a function
with the wrong props. Its `ViewComponentProps` are declared here, because `@retroplug/ui` publishes neither the props
type nor the body until Task 2; Task 1's own review asked for the names the contract will use, so Task 2 changes this
file's two declarations and its signature and nothing else. `tagTestId` reads the optional global the harness installs
(`__rp_tagTestId`) itself, so the shell's `StableSlot` imports it from here rather than defining it. `package.json` is
an ordinary package: `private`, `type: "module"`, one export (`.` -> `./ts/ui/index.ts`, no wildcard), a `typecheck`
script in lsdj's form (so a fresh checkout generates the paths first), `@retroplug/ui` as its one dependency, and a
`retroplug.host` member of exactly the four React modules; its `tsconfig.json` extends the base and the generated
host-modules config.

Create `packages/ui-react/package.json`:

````json
{
  "name": "@retroplug/ui-react",
  "version": "0.1.0",
  "private": true,
  "scripts": {
    "typecheck": "node ../retroplug/scripts/lib/packages.mjs && tsc -p tsconfig.json"
  },
  "type": "module",
  "description": "The UI's React half: the typed handles on the lvgljs-ui components (lv, Box, PixelCanvas), the frame-bus and re-render hooks, the debug readout, reactView for a component view, and tagTestId. Its own ts/ui/ tree is the one place rule 3 admits React into, and @retroplug/ui stays plain TypeScript so a package that does not want React never meets it.",
  "exports": {
    ".": "./ts/ui/index.ts"
  },
  "dependencies": {
    "@retroplug/ui": "workspace:*"
  },
  "devDependencies": {
    "@retroplug/testing": "workspace:*",
    "typescript": "^7.0.2"
  },
  "retroplug": {
    "host": ["react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"]
  }
}
````

Run: `mkdir -p packages/ui-react; sed -n '473,495p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/package.json`
Expected: `040afa17abdbbdad4b401ed4a1c83fa9e1edb511`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-react/tsconfig.json`:

````json
{
  "extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"],
  "include": ["ts"]
}
````

Run: `mkdir -p packages/ui-react; sed -n '507,510p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/tsconfig.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/tsconfig.json`
Expected: `0d3c9ecf3313d3559c80900284a68f2ddb4ce565`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-react/ts/ui/index.ts`:

````ts
// @retroplug/ui-react: the React half of the UI's contract.
//
// A package that wants React for a view, or the shell's own components, gets it here; @retroplug/ui stays plain
// TypeScript, so a pack author who does not want React never meets it. Everything in this package lives under ts/ui/,
// which is the one tree rule 3 admits React into, and the four React host modules (`react`, `react/jsx-runtime`,
// `lvgljs`, `lvgljs-ui`) are declared in its manifest's `retroplug.host`.
//
// The widgets are the shell's own, moved here unchanged: `lv` is the typed handle on the lvgljs-ui components, `Box` a
// View with the theme chrome zeroed, `PixelCanvas` the crisp scaled canvas, and the hooks the frame bus and a forced
// re-render. `DebugReadout` and `tagTestId` are what the debug overlays and the UI tests draw with.
export { Box, CHROME_RESET } from "./Box";
export { DebugReadout, hex2 } from "./DebugReadout";
export {
  CanvasEx,
  LV_IMAGE_ALIGN_CONTAIN,
  TextEx,
  ViewEx,
  type CanvasExProps,
  type CanvasHandle,
  type LvAlign,
  type LvEvent,
  type LvKeyEvent,
  type LvStyle,
  type TextChildren,
  type TextExProps,
  type ViewExProps,
} from "./lv";
export { PixelCanvas } from "./PixelCanvas";
export { reactView, type ViewComponentProps } from "./reactView";
export { tagTestId } from "./tagTestId";
export { useFrameDivider, useFrameSampled, useVersionedPoll } from "./useFramePoll";
export { useNativeEvent, type NativeEvents } from "./useNativeEvent";
export { useForceUpdate, useRerenderOn } from "./useRerender";
````

Run: `mkdir -p packages/ui-react/ts/ui; sed -n '522,554p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/ts/ui/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/ts/ui/index.ts`
Expected: `158999a9d83ca52d5207681bcf00e8352b9e36b1`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-react/ts/ui/reactView.ts`:

````ts
// reactView: the typed way to build a view body out of a React component.
//
// @retroplug/ui types a `component` body's component as `unknown`, because the view contract imports no React; a pack
// author who hands the shell a function with the wrong props would otherwise learn it in the shell. So a package that
// wants React for a view calls this and gets the body back typed.
//
// The props and the body shape are declared here until @retroplug/ui publishes them (`ViewComponentProps` and
// `ViewBody`, which a body of the "component" kind cannot use while the contract imports no React): they are that
// contract's, so the replacement is this file's two declarations gone and the signature named by the imported types,
// with no call site touched.
import type { ComponentType } from "react";

import type { UiHost } from "@retroplug/ui/contribution";

/** What the shell hands a component view: its host, the system it is drawn for, and the space it has. */
export interface ViewComponentProps {
  readonly host: UiHost;
  readonly systemId: number;
  readonly width: number;
  readonly height: number;
}

/** A body of the view contract's "component" kind, local until the contract publishes `ViewBody`: the component,
 *  wrapped so the shell can tell it from a pixels or a readout body. */
interface ReactViewBody {
  readonly kind: "component";
  readonly component: ComponentType<ViewComponentProps>;
}

/** A component a package draws a view with. */
export function reactView(Component: ComponentType<ViewComponentProps>): ReactViewBody {
  return { kind: "component", component: Component };
}
````

Run: `mkdir -p packages/ui-react/ts/ui; sed -n '566,598p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/ts/ui/reactView.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/ts/ui/reactView.ts`
Expected: `b3d6b7120105b8e331b9a5e8ba366254a013404c`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-react/ts/ui/tagTestId.ts`:

````ts
// tagTestId: a ref callback that tags a widget's native uid with a stable testId (findByTestId).
//
// It reads the optional `__rp_tagTestId` global itself rather than through the shell's native hooks, so the kit can tag a
// widget without the AppStores types those hooks carry: the hook is one of the `__rp_*` seam's (packages/retroplug/ui/
// host/nativeHooks.ts declares it, which documents the seam for the harness that installs it), and a host that installs
// none leaves it inert. The shell's StableSlot imports this, so the function exists once.

/** The ref callback for a widget a test must find. Inert in production: `__rp_tagTestId` is installed by the UI test
 *  harness alone. */
export function tagTestId(testId: string) {
  return (node: { uid?: unknown } | null) => {
    if (node) {
      (globalThis as { __rp_tagTestId?: (uid: string, name: string) => void }).__rp_tagTestId?.(
        String(node.uid),
        testId,
      );
    }
  };
}
````

Run: `mkdir -p packages/ui-react/ts/ui; sed -n '610,628p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/ts/ui/tagTestId.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/ts/ui/tagTestId.ts`
Expected: `ab8d491e226e31c6473c06aa3d9442cf76736f01`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-react/ts/test/reactView.test.ts`:

````ts
// reactView, the kit's way to make a view body out of a React component (docs/modular-graph-phase-f4c-design.md section
// 4.2). It is a wrap and nothing else: the `kind` is what the shell tells one body from another by, and the component is
// handed back untouched, because the shell mounts exactly the function the package wrote. What a type test can hold is
// that a component reading only some of the props the shell hands it is accepted: `width` alone is a component view as
// much as one that takes the whole props object.
import { test, expect } from "@retroplug/testing";

import { reactView } from "../ui/reactView";

/** A component of the whole props object; it draws nothing, which is all this case needs of it. */
const Draw = () => null;
/** A component that reads one prop, so a narrower prop type than the contract's is accepted. */
const DrawWidth = ({ width }: { readonly width: number }) => width;

test("reactView wraps a component as the contract's component kind", () => {
  const body = reactView(Draw);
  expect(body.kind).toBe("component");
  expect(body.component).toBe(Draw);
});

test("a component that reads one prop is a component view too", () => {
  const body = reactView(DrawWidth);
  expect(body.kind).toBe("component");
  expect(body.component).toBe(DrawWidth);
});

test("a component asking for props the shell never hands is refused", () => {
  // The point of reactView: @retroplug/ui cannot type the component, so this function does, and a component with the
  // wrong props fails here rather than in the shell. The error is the case, so the body asserts nothing at runtime.
  // @ts-expect-error a `nope` prop is not one the shell hands a component view
  reactView(({ nope }: { readonly nope: string }) => nope);
});
````

Run: `mkdir -p packages/ui-react/ts/test; sed -n '640,671p' docs/modular-graph-phase-f4c-plan.md > packages/ui-react/ts/test/reactView.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-react/ts/test/reactView.test.ts`
Expected: `9d7fdc3cb4bb7662a1943bafe6a7e7ef2e78fbcb`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 3: `packages/ui-fixture`: the component body's proof**

A real workspace package, listed by no product: `retroplug.ui` names its module, `retroplug.host` declares `react` and
`react/jsx-runtime` only (it draws through the kit, so it never names `lvgljs` or `lvgljs-ui`), its tsconfig extends
the base and the generated host-modules config, and its `ts/ui/` holds a `Badge.tsx` (a `Box` and a `TextEx` by JSX)
and an index that exports the component, exports `badgeView` (a `tile` view through `reactView`, declared with
`satisfies TileView`) and default-exports `defineUi({ views: [badgeView] })`. It proves a React view typechecks and
that the generators and checks accept an unlisted package with a `ui` member; it is never bundled, which the bundle
case asserts.

Create `packages/ui-fixture/package.json`:

````json
{
  "name": "@retroplug/ui-fixture",
  "version": "0.1.0",
  "private": true,
  "scripts": {
    "typecheck": "node ../retroplug/scripts/lib/packages.mjs && tsc -p tsconfig.json"
  },
  "type": "module",
  "description": "The fixture pack a package's React view is proved by: a real workspace package no product lists, whose one tile view draws a Box and a Text through @retroplug/ui-react. It is what `pnpm -r typecheck` holds a .tsx to, what rule 3's refusals are copies of, and the worked example a new pack's view is copied from; nothing ships it.",
  "exports": {
    "./ui": "./ts/ui/index.ts"
  },
  "dependencies": {
    "@retroplug/ui": "workspace:*",
    "@retroplug/ui-react": "workspace:*"
  },
  "devDependencies": {
    "typescript": "^7.0.2"
  },
  "retroplug": {
    "ui": "ts/ui/index.ts",
    "host": ["react", "react/jsx-runtime"]
  }
}
````

Run: `mkdir -p packages/ui-fixture; sed -n '693,716p' docs/modular-graph-phase-f4c-plan.md > packages/ui-fixture/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-fixture/package.json`
Expected: `b8fe0a6361c758b1da706756555953a1735996b0`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-fixture/tsconfig.json`:

````json
{
  "extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"],
  "include": ["ts"]
}
````

Run: `mkdir -p packages/ui-fixture; sed -n '728,731p' docs/modular-graph-phase-f4c-plan.md > packages/ui-fixture/tsconfig.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-fixture/tsconfig.json`
Expected: `0d3c9ecf3313d3559c80900284a68f2ddb4ce565`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-fixture/ts/ui/index.ts`:

````ts
// @retroplug/ui-fixture's UI module: the component view a package's React half is proved by.
//
// The package is listed by no product, so nothing here reaches a bundle: it is the worked example spec/06's "a view"
// step copies, and the shape rule 3's refusals are copies of (a .tsx under ts/ui/, the React host modules its files
// import declared in the manifest, and a tsconfig that extends the generated host-module paths).
import { defineUi } from "@retroplug/ui/contribution";
import { reactView } from "@retroplug/ui-react";

import { Badge } from "./Badge";

export { Badge };

/** The tile view this package contributes, built the way the kit asks: a component body. It is the module's own export,
 *  so the contribution below stays the four lists defineUi takes today; a product that lists the package wires the view
 *  from here, and an unlisted package's module is read by nothing. */
export const badgeView = { id: "ui-fixture-badge", label: "Fixture Badge", placement: "tile" as const, body: reactView(Badge) };

/** The package's contribution: the rows, menus, trackers and services it adds, which is none of them. */
export default defineUi({});
````

Run: `mkdir -p packages/ui-fixture/ts/ui; sed -n '743,761p' docs/modular-graph-phase-f4c-plan.md > packages/ui-fixture/ts/ui/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-fixture/ts/ui/index.ts`
Expected: `3a013d28dd6c8137339d66a0ae938b08227fb597`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/ui-fixture/ts/ui/Badge.tsx`:

````ts
// The fixture's component: a tile view's body, drawn from the kit.
//
// It is a .tsx under the package's ts/ui/ tree, which is the one place rule 3 admits React into, and it takes the props
// the shell hands a component view (ViewComponentProps: the host, the system and the space). What it draws is
// deliberately small: a Box (the kit's chrome-reset container) holding one Text, so a typecheck proves the kit's own
// exports and JSX together, and a harness can see it if a product ever ships it.
import type { ReactNode } from "react";

import { Box, TextEx, type ViewComponentProps } from "@retroplug/ui-react";

export function Badge({ width }: ViewComponentProps): ReactNode {
  return (
    <Box style={{ width, height: 24, "background-color": "#202020" }}>
      <TextEx style={{ "text-color": "#8bc34a", "font-size": 12 }}>ui-fixture</TextEx>
    </Box>
  );
}
````

Run: `mkdir -p packages/ui-fixture/ts/ui; sed -n '773,789p' docs/modular-graph-phase-f4c-plan.md > packages/ui-fixture/ts/ui/Badge.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui-fixture/ts/ui/Badge.tsx`
Expected: `bc307debbd2b82db05b1d19abe10f0c93a1f6681`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 4: The shell's imports: one specifier each, and `tagTestId` from the kit**

Nineteen shell files change and their edits are import specifiers: each import that named a moved file now names
`@retroplug/ui-react`, with its own names, and the files that had several such lines (six in `EmulatorTile`, five in
`LsdjHdScreen`, three in `App` and `Menu`, two in `useUiServices`) have one multi-line import after the stage 1
review. `StableSlot.tsx` no longer defines `tagTestId` and imports the kit's, and `EmulatorTile` and the HD screen
import it from the kit rather than through `StableSlot`. The shell's `RpHooks` keeps the `__rp_tagTestId` declaration:
it documents the seam the harness installs. `ui/types/lvgljs-ui.d.ts` is the shell's declaration slice for `lvgljs-ui`
and stays where it is; its header now says the mapping comes from `host-modules.json` through the generated paths.

````diff
diff --git a/packages/retroplug/ui/App.tsx b/packages/retroplug/ui/App.tsx
index abbe839fc..e8f719d62 100644
--- a/packages/retroplug/ui/App.tsx
+++ b/packages/retroplug/ui/App.tsx
@@ -13,9 +13,7 @@ import { setKeyboardGroup } from "lvgljs";
 import type { SystemInfo } from "@retroplug/core/host";
 import { useStores, useSystems, useProjectSettings, useUserConfig, useRecent, useBindings } from "./stores/useStores";
 import { useSinkGroup } from "./lvgl/FocusProvider";
-import { Box } from "./lvgl/Box";
-import { useNativeEvent } from "./lvgl/useNativeEvent";
-import { useRerenderOn } from "./hooks/useRerender";
+import { Box, useNativeEvent, useRerenderOn } from "@retroplug/ui-react";
 import { useWindowSize, requestWindowSize, isWindowSizeControlled, setWindowTitle } from "./lvgl/useWindowSize";
 import { useCloseGuard } from "./app/useCloseGuard";
 import { useProjectModals } from "./app/useProjectModals";
diff --git a/packages/retroplug/ui/app/useFileBrowser.ts b/packages/retroplug/ui/app/useFileBrowser.ts
index 3c6c9c2d2..0ea02a324 100644
--- a/packages/retroplug/ui/app/useFileBrowser.ts
+++ b/packages/retroplug/ui/app/useFileBrowser.ts
@@ -16,7 +16,7 @@ import {
 } from "../../src/fileBrowser";
 import { buildFileBrowserMenu } from "../screens/menu/fileBrowserMenu";
 import type { Overlay } from "./overlay";
-import { useRerenderOn } from "../hooks/useRerender";
+import { useRerenderOn } from "@retroplug/ui-react";
 import { native } from "../host/nativeHooks";
 
 /** The browse as an overlay; onClose (Esc / B) cancels it (resolves null). */
diff --git a/packages/retroplug/ui/app/useJobPump.ts b/packages/retroplug/ui/app/useJobPump.ts
index e0c643a27..6f8990e17 100644
--- a/packages/retroplug/ui/app/useJobPump.ts
+++ b/packages/retroplug/ui/app/useJobPump.ts
@@ -11,7 +11,7 @@
 // opened again, or at the next submission.
 
 import type { AppStores } from "../../src/appStores";
-import { useFrameDivider } from "../lvgl/useFramePoll";
+import { useFrameDivider } from "@retroplug/ui-react";
 
 export function useJobPump(stores: AppStores): void {
   useFrameDivider(1, () => {
diff --git a/packages/retroplug/ui/app/useProjectModals.ts b/packages/retroplug/ui/app/useProjectModals.ts
index 434195b0a..02cb721f3 100644
--- a/packages/retroplug/ui/app/useProjectModals.ts
+++ b/packages/retroplug/ui/app/useProjectModals.ts
@@ -23,7 +23,7 @@ import type { LoadOutcome } from "../../src/projectStore";
 import { hasUnsavedChanges } from "../../src/unsavedChanges";
 import { savEditWouldDiscard, saveWorkingSong } from "../../src/tracker/liveSav";
 import { flushDirtySram } from "../../src/sramAutoSave";
-import { useNativeEvent } from "../lvgl/useNativeEvent";
+import { useNativeEvent } from "@retroplug/ui-react";
 import type { Overlay } from "./overlay";
 import { buildProjectModal, SONG_SAVE_REFUSED, type PendingModal } from "./projectModals";
 import { saveProjectInteractive } from "./saveProjectInteractive";
diff --git a/packages/retroplug/ui/app/useSongWatch.ts b/packages/retroplug/ui/app/useSongWatch.ts
index 03df24b69..bbbdf768d 100644
--- a/packages/retroplug/ui/app/useSongWatch.ts
+++ b/packages/retroplug/ui/app/useSongWatch.ts
@@ -12,7 +12,7 @@
 // opened again.
 
 import type { AppStores } from "../../src/appStores";
-import { useFrameDivider } from "../lvgl/useFramePoll";
+import { useFrameDivider } from "@retroplug/ui-react";
 
 // ~0.5 s at 60 fps. Fast enough that the row is there by the time the user reopens the menu, slow enough
 // that the battery read never shows up in a profile.
diff --git a/packages/retroplug/ui/app/useSramAutoSave.ts b/packages/retroplug/ui/app/useSramAutoSave.ts
index 024bd33ea..56dc751fb 100644
--- a/packages/retroplug/ui/app/useSramAutoSave.ts
+++ b/packages/retroplug/ui/app/useSramAutoSave.ts
@@ -15,7 +15,7 @@ import { useRef } from "react";
 
 import type { AppStores } from "../../src/appStores";
 import { SramAutoSaver } from "../../src/sramAutoSave";
-import { useFrameDivider } from "../lvgl/useFramePoll";
+import { useFrameDivider } from "@retroplug/ui-react";
 
 // ~2 s at 60 fps. Deliberately slower than useSongWatch's 0.5 s: that polls a header-only name parse,
 // whereas a battery check can reach sramSignature's full encodeSong(decodeSong(...)) round-trip. The raw
diff --git a/packages/retroplug/ui/app/useUiServices.ts b/packages/retroplug/ui/app/useUiServices.ts
index 0487e3d48..ea25880cd 100644
--- a/packages/retroplug/ui/app/useUiServices.ts
+++ b/packages/retroplug/ui/app/useUiServices.ts
@@ -9,8 +9,7 @@ import { useEffect, useRef } from "react";
 
 import type { UiHost } from "@retroplug/ui/contribution";
 import { uiServices } from "../host/menuContributions";
-import { useRerenderOn } from "../hooks/useRerender";
-import { useNativeEvent } from "../lvgl/useNativeEvent";
+import { useNativeEvent, useRerenderOn } from "@retroplug/ui-react";
 import { startServices, tickServices } from "./serviceTicker";
 
 export function useUiServices(host: UiHost): void {
diff --git a/packages/retroplug/ui/input/useGameInput.ts b/packages/retroplug/ui/input/useGameInput.ts
index 56baf0514..8f3220e12 100644
--- a/packages/retroplug/ui/input/useGameInput.ts
+++ b/packages/retroplug/ui/input/useGameInput.ts
@@ -9,7 +9,7 @@
 import { useMemo } from "react";
 
 import { useBindings } from "../stores/useStores";
-import { useNativeEvent } from "../lvgl/useNativeEvent";
+import { useNativeEvent } from "@retroplug/ui-react";
 import { buildKeyToButton } from "../../src/keyCodes";
 import { useHeldRouting } from "./useHeldInput";
 
diff --git a/packages/retroplug/ui/input/useGamepadInput.ts b/packages/retroplug/ui/input/useGamepadInput.ts
index 9fe2617df..8e0923000 100644
--- a/packages/retroplug/ui/input/useGamepadInput.ts
+++ b/packages/retroplug/ui/input/useGamepadInput.ts
@@ -11,7 +11,7 @@
 import { useMemo } from "react";
 
 import { useBindings } from "../stores/useStores";
-import { useNativeEvent } from "../lvgl/useNativeEvent";
+import { useNativeEvent } from "@retroplug/ui-react";
 import { buildGamepadToButton } from "../../src/keyCodes";
 import { useAxisEdges, useHeldRouting } from "./useHeldInput";
 
diff --git a/packages/retroplug/ui/lvgl/StableSlot.tsx b/packages/retroplug/ui/lvgl/StableSlot.tsx
index a6dbf9e1c..26bed0ad5 100644
--- a/packages/retroplug/ui/lvgl/StableSlot.tsx
+++ b/packages/retroplug/ui/lvgl/StableSlot.tsx
@@ -12,17 +12,7 @@
 
 import type { ReactNode } from "react";
 
-import { Box } from "./Box";
-import { native } from "../host/nativeHooks";
-
-/** A ref callback that tags a widget's native uid with a stable testId (findByTestId). Inert in
- *  production — the __rp_tagTestId hook only exists in the UI test harness. Shared by any widget a test
- *  needs to locate. */
-export function tagTestId(testId: string) {
-  return (node: { uid?: unknown } | null) => {
-    if (node) native.__rp_tagTestId?.(String(node.uid), testId);
-  };
-}
+import { Box, tagTestId } from "@retroplug/ui-react";
 
 export function StableSlot({
   testId,
diff --git a/packages/retroplug/ui/lvgl/useWindowSize.ts b/packages/retroplug/ui/lvgl/useWindowSize.ts
index 93ba74a5f..cd59c63d8 100644
--- a/packages/retroplug/ui/lvgl/useWindowSize.ts
+++ b/packages/retroplug/ui/lvgl/useWindowSize.ts
@@ -12,7 +12,7 @@
 import { useState } from "react";
 import { Dimensions } from "lvgljs-ui";
 
-import { useNativeEvent } from "./useNativeEvent";
+import { useNativeEvent } from "@retroplug/ui-react";
 import { native } from "../host/nativeHooks";
 
 const FALLBACK = { width: 480, height: 432 };
diff --git a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
index 586a76d1a..6b38350a6 100644
--- a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
+++ b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
@@ -10,12 +10,14 @@ import { useCallback, useRef } from "react";
 import { EAlignType, Text } from "lvgljs-ui";
 
 import { useStores } from "../../stores/useStores";
-import { useNativeEvent } from "../../lvgl/useNativeEvent";
-import { useFrameSampled } from "../../lvgl/useFramePoll";
-import { Box } from "../../lvgl/Box";
-import { PixelCanvas } from "../../lvgl/PixelCanvas";
-import type { CanvasHandle } from "../../lvgl/lv";
-import { tagTestId } from "../../lvgl/StableSlot";
+import {
+  Box,
+  PixelCanvas,
+  tagTestId,
+  useFrameSampled,
+  useNativeEvent,
+  type CanvasHandle,
+} from "@retroplug/ui-react";
 import { LsdjOverlay } from "./LsdjOverlay";
 import { RisaOverlay } from "./RisaOverlay";
 import { pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../host/render";
diff --git a/packages/retroplug/ui/screens/grid/LsdjOverlay.tsx b/packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
index 2f1bfa62e..92f00ae0b 100644
--- a/packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
+++ b/packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
@@ -3,7 +3,7 @@
 // → LsdjReader): the visible screen + its cursor position, play/stop, song position, tempo, the ROM
 // version, and each channel's playing phrase.row. Renders nothing for a non-LSDj / unsupported ROM, so
 // EmulatorTile can mount it unconditionally.
-import { DebugReadout, hex2 } from "./DebugReadout";
+import { DebugReadout, hex2 } from "@retroplug/ui-react";
 import { useLsdjRuntime } from "./useLsdjRuntime";
 import { useLsdjDebugVisible } from "./lsdjDebug";
 import type { LsdjChannelState, LsdjState } from "@retroplug/lsdj/runtime";
diff --git a/packages/retroplug/ui/screens/grid/RisaOverlay.tsx b/packages/retroplug/ui/screens/grid/RisaOverlay.tsx
index 4b918ce86..c79681a22 100644
--- a/packages/retroplug/ui/screens/grid/RisaOverlay.tsx
+++ b/packages/retroplug/ui/screens/grid/RisaOverlay.tsx
@@ -3,7 +3,7 @@
 // decodeRisaState): the visible screen + cursor, play/mode, tempo, ROM version, and each of the 5 tracks'
 // playing phrase.row. Renders nothing for a non-risa / unsupported ROM, so EmulatorTile mounts it
 // unconditionally. Shares the backtick debug toggle with LsdjOverlay (one dev switch, per-system readout).
-import { DebugReadout, hex2 } from "./DebugReadout";
+import { DebugReadout, hex2 } from "@retroplug/ui-react";
 import { useRisaRuntime } from "./useRisaRuntime";
 import { useLsdjDebugVisible } from "./lsdjDebug";
 import type { RisaTrackState } from "@retroplug/risa/runtime";
diff --git a/packages/retroplug/ui/screens/grid/SystemGrid.tsx b/packages/retroplug/ui/screens/grid/SystemGrid.tsx
index ede628d0c..c1305a784 100644
--- a/packages/retroplug/ui/screens/grid/SystemGrid.tsx
+++ b/packages/retroplug/ui/screens/grid/SystemGrid.tsx
@@ -9,7 +9,7 @@
 // are handled by the menu (App), not here.
 
 import { useStores, useSystems, useProjectSettings, useUserConfig } from "../../stores/useStores";
-import { Box } from "../../lvgl/Box";
+import { Box } from "@retroplug/ui-react";
 import { useWindowSize } from "../../lvgl/useWindowSize";
 import { StableSlot } from "../../lvgl/StableSlot";
 import { EmulatorTile } from "./EmulatorTile";
diff --git a/packages/retroplug/ui/screens/grid/useLsdjRuntime.ts b/packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
index 31b2e3c85..7d620d333 100644
--- a/packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
+++ b/packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
@@ -5,7 +5,7 @@
 import { useMemo } from "react";
 
 import { useStores, useSystem } from "../../stores/useStores";
-import { useFrameSampled } from "../../lvgl/useFramePoll";
+import { useFrameSampled } from "@retroplug/ui-react";
 import { hasRole } from "@retroplug/core/systemRoles";
 import { LsdjReader, type LsdjState } from "@retroplug/lsdj/runtime";
 
diff --git a/packages/retroplug/ui/screens/grid/useRisaRuntime.ts b/packages/retroplug/ui/screens/grid/useRisaRuntime.ts
index 6b23257c3..220700fee 100644
--- a/packages/retroplug/ui/screens/grid/useRisaRuntime.ts
+++ b/packages/retroplug/ui/screens/grid/useRisaRuntime.ts
@@ -6,7 +6,7 @@
 import { useMemo } from "react";
 
 import { useStores, useSystem } from "../../stores/useStores";
-import { useFrameSampled } from "../../lvgl/useFramePoll";
+import { useFrameSampled } from "@retroplug/ui-react";
 import { hasRole } from "@retroplug/core/systemRoles";
 import { runtime } from "@retroplug/risa";
 import type { RisaState } from "@retroplug/risa/runtime";
diff --git a/packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx b/packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
index c6cb78d47..9f65816f0 100644
--- a/packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
+++ b/packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
@@ -12,11 +12,7 @@
 import { useRef } from "react";
 import { EAlignType, Text } from "lvgljs-ui";
 
-import { useNativeEvent } from "../../lvgl/useNativeEvent";
-import { Box } from "../../lvgl/Box";
-import { PixelCanvas } from "../../lvgl/PixelCanvas";
-import type { CanvasHandle } from "../../lvgl/lv";
-import { tagTestId } from "../../lvgl/StableSlot";
+import { Box, PixelCanvas, tagTestId, useNativeEvent, type CanvasHandle } from "@retroplug/ui-react";
 import { useLsdjHdSession } from "./useLsdjHdSession";
 
 export function LsdjHdScreen({
diff --git a/packages/retroplug/ui/screens/menu/Menu.tsx b/packages/retroplug/ui/screens/menu/Menu.tsx
index f3bf470be..6b13fa6a0 100644
--- a/packages/retroplug/ui/screens/menu/Menu.tsx
+++ b/packages/retroplug/ui/screens/menu/Menu.tsx
@@ -20,11 +20,9 @@ import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react
 import { Text, ELvKey } from "lvgljs-ui";
 
 import { useFocusGroup } from "../../lvgl/useFocusGroup";
-import { useNativeEvent } from "../../lvgl/useNativeEvent";
+import { Box, TextEx, useNativeEvent, type LvEvent, type LvKeyEvent } from "@retroplug/ui-react";
 import { useStateRef } from "../../hooks/useStateRef";
 import { useAxisEdges } from "../../input/useHeldInput";
-import { Box } from "../../lvgl/Box";
-import { TextEx, type LvEvent, type LvKeyEvent } from "../../lvgl/lv";
 import {
   dpfCodeToKeyName,
   menuNavForButton,
diff --git a/packages/retroplug/ui/types/lvgljs-ui.d.ts b/packages/retroplug/ui/types/lvgljs-ui.d.ts
index 2053f7899..cd18b3d81 100644
--- a/packages/retroplug/ui/types/lvgljs-ui.d.ts
+++ b/packages/retroplug/ui/types/lvgljs-ui.d.ts
@@ -1,17 +1,18 @@
-// The slice of lvgljs-ui the UI uses, declared for the type-check only: tsconfig.ui.json maps the bare
-// specifier here, while every bundle (esbuild) still resolves the real package. It exists because the
+// The slice of lvgljs-ui the UI uses, declared for the type-check only: host-modules.json maps the bare
+// specifier here, through the generated paths both the shell's and the UI's programs extend, while every bundle
+// (esbuild) still resolves the real package. It exists because the
 // package ships TypeScript SOURCE rather than declarations, which tsc then checks under our strict settings
 // (~1600 errors in code we don't own; skipLibCheck only skips .d.ts files).
 //
-// Keep it to what ui/ imports. The widgets are declared loosely on purpose: ui/lvgl/lv.ts gives each one the
-// prop types native actually accepts, and that is where call sites should get them from.
+// Keep it to what the UI imports. The widgets are declared loosely on purpose: @retroplug/ui-react's lv.ts gives each
+// one the prop types native actually accepts, and that is where call sites should get them from.
 
 import type { ComponentType, ReactElement } from "react";
 
 export declare const View: unknown;
 export declare const Canvas: unknown;
-/** Plain styled text. For ref / key / hover props use TextEx (ui/lvgl/lv.ts). Children are as narrow as
- *  native's setter: a string, a number, or an array of only those (see TextChildren in lv.ts). */
+/** Plain styled text. For ref / key / hover props use TextEx (the kit's lv.ts). Children are as narrow as
+ *  native's setter: a string, a number, or an array of only those (see TextChildren in @retroplug/ui-react's lv.ts). */
 export declare const Text: ComponentType<{ style?: Record<string, unknown>; children?: string | number | (string | number)[] }>;
 
 /** lv_align_t (lv_area.h). */
````

Run: `sed -n '809,1117p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/ui/App.tsx
packages/retroplug/ui/app/useFileBrowser.ts
packages/retroplug/ui/app/useJobPump.ts
packages/retroplug/ui/app/useProjectModals.ts
packages/retroplug/ui/app/useSongWatch.ts
packages/retroplug/ui/app/useSramAutoSave.ts
packages/retroplug/ui/app/useUiServices.ts
packages/retroplug/ui/input/useGameInput.ts
packages/retroplug/ui/input/useGamepadInput.ts
packages/retroplug/ui/lvgl/StableSlot.tsx
packages/retroplug/ui/lvgl/useWindowSize.ts
packages/retroplug/ui/screens/grid/EmulatorTile.tsx
packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
packages/retroplug/ui/screens/grid/RisaOverlay.tsx
packages/retroplug/ui/screens/grid/SystemGrid.tsx
packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
packages/retroplug/ui/screens/grid/useRisaRuntime.ts
packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
packages/retroplug/ui/screens/menu/Menu.tsx
packages/retroplug/ui/types/lvgljs-ui.d.ts
EOF
````

Expected:

````text
e8f719d62f28b1b9c868cea603c6929fb3741081
0ea02a3249da085440933de7e7de08bf8f8f3fe6
6f8990e179eabd5a90a84370233d27a4b63d270f
02cb721f3bdc950a40f9661f06fdee97bc1ac4ce
bbbdf768da9cb8c5c34cc5a301e9d76d2401523a
56dc751fbc6e1f2e0a99c807762ba32d52a31577
ea25880cd9292384b556550c73c8aae06dd8fdda
8f3220e12aba475b724ac4aa2c19a804d99f3f03
8e092300030616c300d854dd4952e2c77dbe33d4
26bed0ad5b6380f307f93f903782f9a9a200e718
cd59c63d84ff08ed9f79f9394a5f314a116df170
6b38350a60bb531b53b837e2607f03008c53ce1b
92f00ae0bc746b80e08a4f121cb838c233c4abd6
c79681a22500788b3b730ce9d720582c5dd29abe
c1305a784222b5deae4c4b049ce4493880d5a243
7d620d33370536767c7b4da77526780869f5370d
220700fee1a737a391c101f85f368af25fb7354b
9f65816f0649737067044efe40355d3f6a97aa75
6b13fa6a0f78b4a0e618b9c0b3be921086cb022e
cd18b3d813e353f09985ca1872520e185473baa1
````

- [ ] **Step 5: The typecheck: JSX in the base, four React host modules, and the shell's paths**

`tsconfig.base.json` gains `"jsx": "react-jsx"`, which changes what `tsc` accepts and nothing a bundler emits (the UI
build passes `jsx: "automatic"` itself). `host-modules.json` gains four entries with `surface: "ui"` and the `types`
`tsconfig.ui.json` hand-wrote: `react` and `react/jsx-runtime` into lv_binding_js's `@types/react` (and
`/jsx-runtime`), `lvgljs` into dpf.js's `runtime/lvgljs/index.ts`, and `lvgljs-ui` into the shell's own declaration
slice. The generator writes every entry that has `types` into both generated path files, so the shell's program and
every package's program resolve them; `tsconfig.ui.json` is left as `{ "extends": "./tsconfig.json", "include": ["ui"]
}`, with no `paths` and no `jsx` of its own. The shell's `tsconfig.json`'s comment names the React modules too.

````diff
diff --git a/packages/retroplug/host-modules.json b/packages/retroplug/host-modules.json
index bb594fff0..6b285720c 100644
--- a/packages/retroplug/host-modules.json
+++ b/packages/retroplug/host-modules.json
@@ -4,5 +4,29 @@
     "surface": "cli",
     "provider": "the retroplug-cli binary's loaded-module table",
     "types": "cli/sdk-types.d.ts"
+  },
+  {
+    "module": "react",
+    "surface": "ui",
+    "provider": "lv_binding_js's node_modules (@types/react)",
+    "types": "../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react"
+  },
+  {
+    "module": "react/jsx-runtime",
+    "surface": "ui",
+    "provider": "lv_binding_js's node_modules (@types/react/jsx-runtime)",
+    "types": "../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react/jsx-runtime"
+  },
+  {
+    "module": "lvgljs",
+    "surface": "ui",
+    "provider": "dpf.js's typed runtime front door",
+    "types": "../../deps/dpf.js/runtime/lvgljs/index.ts"
+  },
+  {
+    "module": "lvgljs-ui",
+    "surface": "ui",
+    "provider": "lv_binding_js's component registry, declared by the shell's slice of it",
+    "types": "ui/types/lvgljs-ui.d.ts"
   }
 ]
diff --git a/packages/retroplug/tsconfig.json b/packages/retroplug/tsconfig.json
index f19195914..9d45c058b 100644
--- a/packages/retroplug/tsconfig.json
+++ b/packages/retroplug/tsconfig.json
@@ -1,9 +1,9 @@
 {
   // The shell's own program: the app, its tests, the UI harness and every workspace package's TS half. The compiler
   // options are tsconfig.base.json's (the same ones each package's tsconfig extends); the paths are the generated
-  // file's, which carries both the host modules a package imports through its surface's host (`retroplug-cli` today,
-  // from host-modules.json) and the one alias the UI tests use (from tsconfig.aliases.json). The runtime's ambient
-  // globals come from the base's `files`.
+  // file's, which carries both the host modules a package imports through its surface's host (`retroplug-cli`, and the
+  // React modules the UI alone may name, from host-modules.json) and the one alias the UI tests use (from
+  // tsconfig.aliases.json). The runtime's ambient globals come from the base's `files`.
   "extends": ["../../tsconfig.base.json", "./tsconfig.paths.json"],
   // The shell's own trees, and every workspace package's TS half by one glob, so a package added to the workspace is
   // type-checked without an entry here: its own ts/ tree, sources and ts/test alike. packages/native's device-mode
diff --git a/packages/retroplug/tsconfig.ui.json b/packages/retroplug/tsconfig.ui.json
index 1dfcb73a6..fa22f3d02 100644
--- a/packages/retroplug/tsconfig.ui.json
+++ b/packages/retroplug/tsconfig.ui.json
@@ -1,16 +1,9 @@
 {
-  // The React/LVGL UI (ui/), which the main tsconfig can't reach: `react` and `lvgljs-ui` only resolve at
-  // bundle time, through lv_binding_js. Mapped here instead - react to lv_binding_js's own @types, lvgljs to
-  // dpf.js's typed runtime front door, lvgljs-ui to a declaration of the slice we use (ui/types/).
+  // The React/LVGL UI (ui/) as a program of its own: the shell's config over one more tree, no paths of its own, because
+  // a child's paths REPLACE what it extends rather than merging. It needs nothing the shell's config lacks - the React,
+  // lvgljs and lvgljs-ui specifiers resolve through the generated host-module paths both configs now carry - so it is
+  // kept apart only until someone folds ui/ into tsconfig.json's include (docs/modular-graph-phase-f4c-design.md
+  // section 10.12 leaves that open).
   "extends": "./tsconfig.json",
-  "compilerOptions": {
-    "jsx": "react-jsx",
-    "paths": {
-      "react": ["../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react"],
-      "react/jsx-runtime": ["../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react/jsx-runtime"],
-      "lvgljs": ["../../deps/dpf.js/runtime/lvgljs/index.ts"],
-      "lvgljs-ui": ["./ui/types/lvgljs-ui.d.ts"]
-    }
-  },
   "include": ["ui"]
 }
diff --git a/tsconfig.base.json b/tsconfig.base.json
index 85caefb2a..528a408ae 100644
--- a/tsconfig.base.json
+++ b/tsconfig.base.json
@@ -13,6 +13,11 @@
     "noImplicitOverride": true,
     "esModuleInterop": true,
     "resolveJsonModule": true,
+    // Automatic JSX: a .tsx compiles to react/jsx-runtime calls, which is the one place a component names React without
+    // importing it. Every bundle already builds this way (tools/build-ui.js passes jsx: "automatic"); this makes the
+    // typecheck accept the same sources. The runtime and its types are mapped by the host-module list, never resolved
+    // from node_modules.
+    "jsx": "react-jsx",
     "skipLibCheck": true,
     // ES2022.Array only, for Array.prototype.at() in the UI tests. NOT the whole of ES2022: the app runs on QuickJS,
     // and a blanket lib bump would let src reach for APIs the runtime may not have.
````

Run: `sed -n '1186,1278p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/host-modules.json
packages/retroplug/tsconfig.json
packages/retroplug/tsconfig.ui.json
tsconfig.base.json
EOF
````

Expected:

````text
6b285720cae0bfcf4c6569278f7e127c215d1f96
9d45c058b08ba7111f593ab8791a0db37aac4ca4
fa22f3d02f03bde6fb9537daf2e609c2e597c71f
528a408ae8a056235e17075b791d57f99396abdb
````

The shell's manifest declares the package, and the lockfile follows from one install. The stage 1 report states the
whole of it: "`pnpm-lock.yaml`, +29 lines, nothing else moved", and the command was "Lockfile (quoted, `pnpm install
--offline`, no `--frozen-lockfile`)".

````diff
diff --git a/packages/retroplug/package.json b/packages/retroplug/package.json
index b4edb39c0..edc0ca0ad 100644
--- a/packages/retroplug/package.json
+++ b/packages/retroplug/package.json
@@ -25,6 +25,7 @@
     "@retroplug/host-io": "workspace:*",
     "@retroplug/system": "workspace:*",
     "@retroplug/ui": "workspace:*",
+    "@retroplug/ui-react": "workspace:*",
     "zod": "^4.4.3"
   },
   "devDependencies": {
````

Run: `sed -n '1309,1320p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/retroplug/package.json`
Expected: `edc0ca0ad80d3f3557180bbb61abf54a721fdec3`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `pnpm install --offline`
Expected: exit 0: the workspace resolves the shell's `@retroplug/ui-react` link and the two new importers, which is the lockfile diff quoted in the block above; nothing else in the lockfile moves. Run it BEFORE the build, so the link exists.

````diff
diff --git a/pnpm-lock.yaml b/pnpm-lock.yaml
index e818c714f..e2cd7faa9 100644
--- a/pnpm-lock.yaml
+++ b/pnpm-lock.yaml
@@ -340,6 +340,9 @@ importers:
       '@retroplug/ui':
         specifier: workspace:*
         version: link:../ui
+      '@retroplug/ui-react':
+        specifier: workspace:*
+        version: link:../ui-react
       zod:
         specifier: ^4.4.3
         version: 4.4.3
@@ -477,6 +480,32 @@ importers:
         specifier: ^7.0.2
         version: 7.0.2
 
+  packages/ui-fixture:
+    dependencies:
+      '@retroplug/ui':
+        specifier: workspace:*
+        version: link:../ui
+      '@retroplug/ui-react':
+        specifier: workspace:*
+        version: link:../ui-react
+    devDependencies:
+      typescript:
+        specifier: ^7.0.2
+        version: 7.0.2
+
+  packages/ui-react:
+    dependencies:
+      '@retroplug/ui':
+        specifier: workspace:*
+        version: link:../ui
+    devDependencies:
+      '@retroplug/testing':
+        specifier: workspace:*
+        version: link:../testing
+      typescript:
+        specifier: ^7.0.2
+        version: 7.0.2
+
 packages:
 
   '@esbuild/aix-ppc64@0.28.1':
````

Run: `sed -n '1333,1379p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object pnpm-lock.yaml`
Expected: `e2cd7faa9f446b847414406017de095d668b3116`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 6: The generator: the extend rule for a package declaring a UI host module**

The generator's "must extend `tsconfig.host-modules.json`" check fired only for a package listing examples; it now
fires for a package that declares a host module whose entry's `surface` is `ui`, with a message naming that reason and
the file a `.tsx` resolves React through. The examples messages keep their wording. The rule judges the packages of a
product list, as the examples check does, so `ui-react` and `ui-fixture` are held by their own `tsc -p tsconfig.json`
(a missing `react` path fails there). The fixture tree's shell host-modules list gains the four React entries, and
three fixture modes hold the check: a UI host module with no tsconfig refused, with an unextended one refused, with
the extended one accepted.

````diff
diff --git a/packages/retroplug/scripts/lib/packages.mjs b/packages/retroplug/scripts/lib/packages.mjs
index 74f9fad02..b89926a12 100644
--- a/packages/retroplug/scripts/lib/packages.mjs
+++ b/packages/retroplug/scripts/lib/packages.mjs
@@ -980,8 +980,8 @@ function renderHostModules(list) {
   const entries = typed.map((entry) => pathEntry(entry.module, [`./${entry.types.replaceAll("\\", "/")}`]));
   return [
     "// Generated by packages/retroplug/scripts/lib/packages.mjs from the shell's host-modules.json. Do not edit; edit",
-    "// the list and rebuild. A package whose tsconfig extends this resolves a host module (`retroplug-cli` today) to",
-    "// the declaration file a consumer reads, exactly as their tsconfig does.",
+    "// the list and rebuild. A package whose tsconfig extends this resolves a host module to the declaration file a",
+    "// consumer reads, exactly as their tsconfig does.",
     "{",
     '  "compilerOptions": {',
     '    "paths": {',
@@ -1439,24 +1439,40 @@ export function generate({ root = REPO } = {}) {
     }
   }
 
-  // An example's TypeScript is part of the shell's own program (`../*/examples`), and it resolves a host module through
-  // the generated paths, so its package's tsconfig must extend them. A tsconfig that includes `examples` without the
-  // extend fails its own `tsc` (an unresolved host specifier), which is the visible half; one that leaves `examples`
-  // out of `include` typechecks clean and hands the file to the shell's program alone, which is the half nothing else
-  // catches. The parsed `extends` decides, never the text: naming the file in a comment resolves nothing.
+  // A package's TypeScript that resolves a host module through the generated paths must have a tsconfig extending them:
+  // an example session (the shell's program includes `../*/examples`), and a package declaring a host module the UI
+  // owns (`surface: "ui"`), because a `.tsx` reaches `react` and `react/jsx-runtime` only through those paths. A
+  // tsconfig that includes the files without the extend fails its own `tsc` (an unresolved host specifier), which is
+  // the visible half; one that leaves them out of `include` typechecks clean and hands the files to the shell's program
+  // alone, which is the half nothing else catches. The parsed `extends` decides, never the text: naming the file in a
+  // comment resolves nothing.
   const hostConfigName = "tsconfig.host-modules.json";
   const hostConfig = join(shell.dir, hostConfigName);
-  for (const { pkg, examples } of contributionsOf) {
-    if (!examples.length) continue;
+  // Reading a host-modules entry's surfaces exactly as check-boundaries.mjs's surfacesOf does, non-string entries
+  // dropped: the generator and the checker judge one manifest shape and must agree on it.
+  const surfacesOf = (entry) =>
+    typeof entry.surface === "string"
+      ? [entry.surface]
+      : Array.isArray(entry.surface)
+        ? entry.surface.filter((surface) => typeof surface === "string")
+        : [];
+  for (const { pkg, examples, host } of contributionsOf) {
+    const uiHost = host.some((module) =>
+      surfacesOf(hostList.find((entry) => entry.module === module) ?? {}).includes("ui"));
+    const why = examples.length ? "it lists examples" : uiHost ? "it declares a UI host module" : null;
+    if (why === null) continue;
+    // What the extend is for, in the two messages' own phrasing (they predate the React half and read as they did).
+    const must = examples.length ? "an example resolves its host module" : "a .tsx resolves React";
+    const where = examples.length ? "an example resolves a host module" : "a .tsx resolves React";
     const tsconfigPath = join(pkg.dir, "tsconfig.json");
     if (!existsSync(tsconfigPath)) {
-      throw new Error(`${pkg.name}: it lists examples but has no tsconfig.json, which must extend ` +
-        `../${SHELL}/${hostConfigName} so an example resolves its host module`);
+      throw new Error(`${pkg.name}: ${why} but has no tsconfig.json, which must extend ` +
+        `../${SHELL}/${hostConfigName} so ${must}`);
     }
     const extensions = tsconfigExtends(tsconfigPath).map((entry) => resolve(pkg.dir, entry));
     if (!extensions.includes(hostConfig)) {
-      throw new Error(`${pkg.name}: it lists examples but its tsconfig.json does not extend ` +
-        `../${SHELL}/${hostConfigName}, where an example resolves a host module`);
+      throw new Error(`${pkg.name}: ${why} but its tsconfig.json does not extend ` +
+        `../${SHELL}/${hostConfigName}, where ${where}`);
     }
   }
 
diff --git a/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json b/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json
index 97b3d69a8..89d35031d 100644
--- a/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json
+++ b/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json
@@ -4,5 +4,11 @@
     "surface": "cli",
     "provider": "the fixture's own loaded-module table",
     "types": "src/fixture-lib.d.ts"
+  },
+  {
+    "module": "fixture-react",
+    "surface": "ui",
+    "provider": "the fixture's own React types",
+    "types": "src/fixture-react.d.ts"
   }
 ]
diff --git a/packages/retroplug/test/packages/packages-generator.test.ts b/packages/retroplug/test/packages/packages-generator.test.ts
index e61096dd2..424b921c5 100644
--- a/packages/retroplug/test/packages/packages-generator.test.ts
+++ b/packages/retroplug/test/packages/packages-generator.test.ts
@@ -15,7 +15,9 @@
 //   module that does not exist, a contribution its exports do not publish, a manifest whose name is not its
 //   directory's, a contribution outside ts/ and a contributing package the shell does not declare. A dependency that
 //   contributes nothing, and one whose manifest has no retroplug member, are left out of the ordering rather than
-//   refused; a package contributing an app and no nodes names no nodes module and still registers its app;
+//   refused; a package contributing an app and no nodes names no nodes module and still registers its app; a tsconfig
+//   that must extend the generated host-module paths because it lists an example session or declares a host module the
+//   UI owns;
 // - the generated module itself refuses a duplicate key in the lists the registries look up rather than sequence: a
 //   role kind, a kit format's id and a tracker integration's marker role, each with its own message, and an embedded
 //   ROM whose declaration and manifest entry do not pair (or name a console this product does not run);
@@ -246,6 +248,29 @@ test("a package with examples whose tsconfig extends the generated host modules
   expect(error).toBe(null);
 });
 
+test("a package declaring a UI host module without a tsconfig is refused", async () => {
+  const { error } = await drive("react-tsconfig-missing");
+  console.log(`# a UI host module without a tsconfig: ${error}`);
+  expect(error).toBe(
+    "alpha: it declares a UI host module but has no tsconfig.json, which must extend " +
+      "../retroplug/tsconfig.host-modules.json so a .tsx resolves React",
+  );
+});
+
+test("a package declaring a UI host module whose tsconfig does not extend the generated host modules is refused", async () => {
+  const { error } = await drive("react-tsconfig-unextended");
+  console.log(`# a UI host module's tsconfig unextended: ${error}`);
+  expect(error).toBe(
+    "alpha: it declares a UI host module but its tsconfig.json does not extend " +
+      "../retroplug/tsconfig.host-modules.json, where a .tsx resolves React",
+  );
+});
+
+test("a package declaring a UI host module whose tsconfig extends the generated host modules is accepted", async () => {
+  const { error } = await drive("react-tsconfig-extended");
+  expect(error).toBe(null);
+});
+
 test("an example named after one of the shell's own sessions is refused, naming the shell's", async () => {
   const { error } = await drive("examples-shell-name");
   console.log(`# shell example name: ${error}`);
diff --git a/packages/retroplug/test/packages/run-generator.mjs b/packages/retroplug/test/packages/run-generator.mjs
index 5f5b86d2f..aa768bce5 100644
--- a/packages/retroplug/test/packages/run-generator.mjs
+++ b/packages/retroplug/test/packages/run-generator.mjs
@@ -33,7 +33,8 @@
 //   ui-unknown-tracker | ui-duplicate-tracker | ui-duplicate-service
 //   cli | cli-duplicate-tool | cli-missing-module | examples-outside | examples-tsconfig-missing |
 //   examples-tsconfig-unextended | examples-tsconfig-extended | examples-duplicate-name | examples-shell-name |
-//   examples-unsafe-name | host-unknown | host-known
+//   examples-unsafe-name | host-unknown | host-known | react-tsconfig-missing | react-tsconfig-unextended |
+//   react-tsconfig-extended
 //                   the menus module: ui reports the contributing packages in product order, the others load it into
 //                   its own refusal ({ error })
 //                   (unlisted-plain generates, and reports files like generate)
@@ -572,6 +573,24 @@ try {
       result = { error: null };
       break;
     }
+    case "react-tsconfig-missing":
+    case "react-tsconfig-unextended":
+    case "react-tsconfig-extended": {
+      // A package that declares a host module the UI owns resolves React through the generated host-module paths, so
+      // its tsconfig must extend them even without an example session.
+      editManifest("alpha", (m) => {
+        m.retroplug.host = ["fixture-react"];
+      });
+      if (MODE !== "react-tsconfig-missing") {
+        const extendsLine =
+          MODE === "react-tsconfig-extended"
+            ? '"extends": ["../../tsconfig.base.json", "../retroplug/tsconfig.host-modules.json"]'
+            : '// extend tsconfig.host-modules.json (not really)\n  "extends": ["../../tsconfig.base.json"]';
+        writeTree("packages/alpha/tsconfig.json", `{\n  ${extendsLine},\n  "include": ["ts"]\n}\n`);
+      }
+      result = MODE === "react-tsconfig-extended" ? { error: refusal().error } : refusal();
+      break;
+    }
     case "ui-missing-module": {
       editManifest("alpha", (m) => {
         m.retroplug.ui = "ts/nothing.ts";
````

Run: `sed -n '1399,1565p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/lib/packages.mjs
packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json
packages/retroplug/test/packages/packages-generator.test.ts
packages/retroplug/test/packages/run-generator.mjs
EOF
````

Expected:

````text
b89926a127a3138901b9e4e6ed95fc7f61dae309
89d35031dff9a470ea39ce5c65dcc8f680c288ee
424b921c5adb4028571958b3e9a75c0706a1e654
aa768bce5d362e840f263befb555aec9cdf6adc5
````

- [ ] **Step 7: Rule 3: where a `.tsx` may live, and what JSX imports**

Two refusals join the import walk (`check-boundaries.mjs`, beside the host-module branch): a package file ending
`.tsx` whose `surfaceOf(pkg, file)` is not `ui` is refused, naming the file and `packages/<name>/ts/ui/`; and a
package with any `.tsx` must declare `react/jsx-runtime` in `retroplug.host`, because automatic JSX imports it with no
specifier, so no scanner would see the dependency. A `.tsx` counts as that declaration's use, so the
unused-host-module warning does not fire for it. The four React modules are admitted only from `ts/ui/` by the
existing surface rule (`surfacesOf` already reads a string or a list, and `surfaceOf` already maps `ts/ui/` to `ui`).
Four fixture trees hold it, each a tiny workspace: `react-allowed` (accepted, all four modules declared and used),
`react-outside-ui` (a `.tsx` under `ts/`), `react-undeclared` (a `.tsx` under `ts/ui/` with one of the two modules
declared) and `react-cli-import` (a `ts/cli/` file naming `react`, which the surface rule refuses).

Create `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json`:

````json
{
  "name": "@retroplug/alpha",
  "private": true,
  "retroplug": { "host": ["react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"] }
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha; sed -n '1606,1610p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json`
Expected: `39868a10821e3c047aa20c39f217c6531918a021`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx`:

````ts
// A component in the package's UI tree. Its JSX imports react/jsx-runtime implicitly, and its `react` type import is
// the other half of what a .tsx needs; both are host modules the manifest declares.
import type { ReactNode } from "react";
import { View } from "lvgljs-ui";

export function Badge(): ReactNode {
  return <View />;
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui; sed -n '1622,1629p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx`
Expected: `ebf5c3035d9d9e9a781cb600c993a937582f406a`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts`:

````ts
// The frame bus, so every declared host module is used: lvgljs here, lvgljs-ui and react through the component, and
// react/jsx-runtime through its JSX.
import { on } from "lvgljs";

export { Badge } from "./Badge";

export const listen = on;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui; sed -n '1641,1647p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts`
Expected: `78ab27a9109b06cf06c7a398f6416571bed57d3b`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json`:

````json
[
  { "module": "react", "surface": "ui", "types": "types/react.d.ts" },
  { "module": "react/jsx-runtime", "surface": "ui", "types": "types/react-jsx-runtime.d.ts" },
  { "module": "lvgljs", "surface": "ui", "types": "types/lvgljs.d.ts" },
  { "module": "lvgljs-ui", "surface": "ui", "types": "types/lvgljs-ui.d.ts" }
]
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug; sed -n '1659,1664p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json`
Expected: `0bfc440e7f9238757beae9d8af8795b68833c5c6`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug; sed -n '1676,1679p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json`:

````json
{
  "name": "@retroplug/alpha",
  "private": true,
  "retroplug": { "host": ["react"] }
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha; sed -n '1691,1695p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json`
Expected: `1c1c0c14f04a4d450acd49ccd4b0bf490933fa9f`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts`:

````ts
// A package's CLI tree naming React: React is the UI surface's, and a cli host module is the only kind its own files
// reach.
import { createElement } from "react";

export const element = createElement;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli; sed -n '1707,1711p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts`
Expected: `ab659ca6f3412abc034d3949e3d39a76eb9ec8b9`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json`:

````json
[
  { "module": "react", "surface": "ui", "types": "types/react.d.ts" },
  { "module": "react/jsx-runtime", "surface": "ui", "types": "types/react-jsx-runtime.d.ts" },
  { "module": "lvgljs", "surface": "ui", "types": "types/lvgljs.d.ts" },
  { "module": "lvgljs-ui", "surface": "ui", "types": "types/lvgljs-ui.d.ts" }
]
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug; sed -n '1723,1728p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json`
Expected: `0bfc440e7f9238757beae9d8af8795b68833c5c6`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug; sed -n '1740,1743p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json`:

````json
{
  "name": "@retroplug/alpha",
  "private": true,
  "retroplug": { "host": ["react/jsx-runtime"] }
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha; sed -n '1755,1759p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json`
Expected: `3409efc2957b174344c9cc738a01f896c25be9de`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx`:

````ts
// A .tsx where a package's plain tree is. React belongs to the package's ts/ui/ tree alone, whatever the component
// draws, so the placement is what this tree gets wrong; the fragment needs no import, which leaves the placement the
// one refusal.
export function Badge() {
  return <></>;
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts; sed -n '1771,1776p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx`
Expected: `f0d605b269094d4f5efdcd28ecac11b0e9795e7c`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json`:

````json
[
  { "module": "react", "surface": "ui", "types": "types/react.d.ts" },
  { "module": "react/jsx-runtime", "surface": "ui", "types": "types/react-jsx-runtime.d.ts" },
  { "module": "lvgljs", "surface": "ui", "types": "types/lvgljs.d.ts" },
  { "module": "lvgljs-ui", "surface": "ui", "types": "types/lvgljs-ui.d.ts" }
]
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug; sed -n '1788,1793p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json`
Expected: `0bfc440e7f9238757beae9d8af8795b68833c5c6`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug; sed -n '1805,1808p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json`:

````json
{
  "name": "@retroplug/alpha",
  "private": true,
  "retroplug": { "host": ["react", "lvgljs", "lvgljs-ui"] }
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha; sed -n '1820,1824p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json`
Expected: `b4347a8fc1162709ea5ad34ba5a4533092629bee`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx`:

````ts
// A component in the package's UI tree. Its JSX imports react/jsx-runtime implicitly, and its `react` type import is
// the other half of what a .tsx needs; this fixture's point is that the manifest declares one of the two, not both.
import type { ReactNode } from "react";
import { View } from "lvgljs-ui";

export function Badge(): ReactNode {
  return <View />;
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui; sed -n '1836,1843p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx`
Expected: `f1ad7e244a0319239c8c6aab78fa2905b11f51e9`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts`:

````ts
// The frame bus, so every declared host module is used: lvgljs here, lvgljs-ui and react through the component, and
// react/jsx-runtime through its JSX.
import { on } from "lvgljs";

export { Badge } from "./Badge";

export const listen = on;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui; sed -n '1855,1861p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts`
Expected: `78ab27a9109b06cf06c7a398f6416571bed57d3b`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json`:

````json
[
  { "module": "react", "surface": "ui", "types": "types/react.d.ts" },
  { "module": "react/jsx-runtime", "surface": "ui", "types": "types/react-jsx-runtime.d.ts" },
  { "module": "lvgljs", "surface": "ui", "types": "types/lvgljs.d.ts" },
  { "module": "lvgljs-ui", "surface": "ui", "types": "types/lvgljs-ui.d.ts" }
]
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug; sed -n '1873,1878p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json`
Expected: `0bfc440e7f9238757beae9d8af8795b68833c5c6`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug; sed -n '1890,1893p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/check-boundaries.mjs b/packages/retroplug/scripts/check-boundaries.mjs
index 7a6bc1c64..49a590c6e 100644
--- a/packages/retroplug/scripts/check-boundaries.mjs
+++ b/packages/retroplug/scripts/check-boundaries.mjs
@@ -43,6 +43,13 @@
 //      not a source's to import. src/generated/ is not judged (it is the one place that names every package), and an
 //      entry no import uses fails the check.
 //
+// A host module (the bare specifiers packages/retroplug/host-modules.json lists) is judged twice more: the package must
+// declare it in `retroplug.host`, and it is importable only from the files of the surface its entry names (a package's
+// `ts/ui/` for `surface: "ui"`, its `ts/cli/` for `"cli"`), since a package's halves are separate programs. A `.tsx` is
+// also a use of `react/jsx-runtime` with no specifier naming it, because automatic JSX imports it implicitly, so the
+// declaration is the only place it shows; and a `.tsx` outside the package's `ts/ui/` tree is refused, that tree being
+// the one place a React host module is admissible from.
+//
 // Rule 1's second half and rule 8's first are one leak: a test file may use its package's devDependencies, so source
 // that reaches a test file - by a relative import, or because the test file is published - hands those devDependencies
 // to every importer of that source.
@@ -724,6 +731,37 @@ export function checkBoundaries({ root = REPO } = {}) {
       }
     }
 
+    // Rule 3, the React half: a package's React lives in its ts/ui/ tree alone, so a .tsx anywhere else is a component
+    // no surface owns (its host modules would be refused from there as well; this names the placement, which is the
+    // rule). And automatic JSX imports `react/jsx-runtime` with no specifier, so no import of it is ever seen: the
+    // declaration is what a .tsx requires, and the .tsx is what makes the declaration used.
+    const components = sourceFiles.filter((file) => file.endsWith(".tsx"));
+    for (const file of components) {
+      if (surfaceOf(pkg, file) === "ui") continue;
+      refuse(
+        "jsx-surface",
+        shown(root, file),
+        "",
+        `${shown(root, file)}: is a .tsx file outside packages/${pkg.name}/ts/ui/, the one tree rule 3 admits React ` +
+          "into: a component belongs to the package's UI surface, whose files are the only ones a React host module " +
+          "is reachable from",
+      );
+    }
+    if (components.length) {
+      if (!declaredHost.includes("react/jsx-runtime")) {
+        refuse(
+          "host-module",
+          shown(root, pkg.manifestPath),
+          "react/jsx-runtime",
+          `${shown(root, pkg.manifestPath)}: has a .tsx file (${shown(root, components[0])}) and does not declare ` +
+            "react/jsx-runtime in retroplug.host: automatic JSX imports it with no specifier, so the declaration is the " +
+            "only place the dependency shows",
+        );
+      } else {
+        usedHost.add("react/jsx-runtime");
+      }
+    }
+
     for (const module of declaredHost) {
       if (hostModules.has(module) && !usedHost.has(module)) {
         warnings.push(`${pkg.name}: declares the host module ${module}, which no file of its imports`);
diff --git a/packages/retroplug/test/boundaries/boundaries-checker.test.ts b/packages/retroplug/test/boundaries/boundaries-checker.test.ts
index 0d1ed1405..5e85c085d 100644
--- a/packages/retroplug/test/boundaries/boundaries-checker.test.ts
+++ b/packages/retroplug/test/boundaries/boundaries-checker.test.ts
@@ -17,7 +17,9 @@
 // - a devDependency a source file imports, which only a test file may;
 // - source that imports one of its own package's test modules: the same devDependency leak, reached by a relative path;
 // - a host module imported from a file its surface does not own (a `cli` module from plain source, not examples/ or
-//   ts/cli/);
+//   ts/cli/, and React from a package's CLI tree);
+// - a .tsx outside the package's ts/ui/ tree, and a package whose .tsx has no react/jsx-runtime declaration (automatic
+//   JSX imports it with no specifier, so the .tsx is its only use);
 // - a declared package that does not exist;
 // - a `workspace:` or `link:` alias of the shell, which the key alone would present as a third-party name;
 // - an `npm:` alias naming a package the workspace does not have;
@@ -417,6 +419,49 @@ test("a host declaration nothing imports warns, as an unused dependency does", a
   expect(report.warnings).toEqual(["alpha: declares the host module fixture-lib, which no file of its imports"]);
 });
 
+test("a .tsx under the package's ts/ui/ tree with the React host modules declared is admitted", async () => {
+  // The good shape: the component in ts/ui/, the four host modules in retroplug.host, and the .tsx counted as the use
+  // of react/jsx-runtime (which no import names), so nothing is declared unused.
+  const report = await drive("react-allowed");
+  expect(report.status).toBe(0);
+  expect(report.refused).toEqual([]);
+  expect(report.warnings).toEqual([]);
+});
+
+test("a .tsx outside the package's ts/ui/ tree is refused, naming the file and the tree", async () => {
+  const report = await drive("react-outside-ui");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    "packages/alpha/ts/Badge.tsx: is a .tsx file outside packages/alpha/ts/ui/, the one tree rule 3 admits React " +
+      "into: a component belongs to the package's UI surface, whose files are the only ones a React host module is " +
+      "reachable from",
+  ]);
+});
+
+test("a package with a .tsx and no react/jsx-runtime declaration is refused", async () => {
+  // Automatic JSX imports react/jsx-runtime with no specifier in the source, so a declaration check that read only
+  // what an import names would admit a component whose React was never declared.
+  const report = await drive("react-undeclared");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    "packages/alpha/package.json: has a .tsx file (packages/alpha/ts/ui/Badge.tsx) and does not declare " +
+      "react/jsx-runtime in retroplug.host: automatic JSX imports it with no specifier, so the declaration is the only " +
+      "place the dependency shows",
+  ]);
+});
+
+test("a React import from a package's CLI tree is refused as a surface error", async () => {
+  const report = await drive("react-cli-import");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    'packages/alpha/ts/cli/tool.ts: imports "react", a ui host module, from a file that surface does not own: ' +
+      "a ui host module is reached only from the package's ts/ui/ trees",
+  ]);
+});
+
 test("the real tree holds its boundaries with no exception left", async () => {
   const report = await drive("--tree");
   const allowed = report.allowed as string[];
````

Run: `sed -n '1903,2023p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/check-boundaries.mjs
packages/retroplug/test/boundaries/boundaries-checker.test.ts
EOF
````

Expected:

````text
49a590c6ebd26663c44ac02a46d5aa1fc1bfac30
5e85c085d7199d1067fc2d793e0266c1e9bb0aee
````

- [ ] **Step 8: The bundle split's pattern**

`test/script/bundle-ui-split.test.ts`'s `isUi` learned `/packages/ui/` and `/packages/*/ts/ui/`; a kit file that ever
sits outside its `ts/ui/` tree (its entry, a future `ts/` file) was invisible to both. The pattern is widened to
`/\/packages\/ui(-react)?\//`, and a new case asserts the UI bundle carries the kit's modules and that
`packages/ui-fixture/` is in NO bundle's inputs.

````diff
diff --git a/packages/retroplug/test/script/bundle-ui-split.test.ts b/packages/retroplug/test/script/bundle-ui-split.test.ts
index b7ad12260..015903120 100644
--- a/packages/retroplug/test/script/bundle-ui-split.test.ts
+++ b/packages/retroplug/test/script/bundle-ui-split.test.ts
@@ -4,8 +4,9 @@
 // ui/generated/menus.ts (the packages' menu contributions) and cli/generated/tools.ts (their CLI tools), and beside
 // them the packages' own `ts/ui/` and `ts/cli/` trees, reached through their `./ui` and `./cli` entries. The control
 // plane, the render worker, the script nodes, the SDK, the CLI dispatcher and the planner must carry no `ui` module
-// (the package, any package's `ts/ui/`, the menus); the UI must carry no CLI one (any package's `ts/cli/`, the tool
-// contract, the tools). The dispatcher is the bundle that carries the tools, which is why it is named here.
+// (the package, the React kit, any package's `ts/ui/`, the menus), nor a package's view; the UI must carry no CLI one
+// (any package's `ts/cli/`, the tool contract, the tools). The dispatcher is the bundle that carries the tools, which is
+// why it is named here.
 //
 // The bundles are built as the build builds them, in memory: tools/build-session.js's bundleInputs (esm/iife) and
 // tools/build-ui.js's, and esbuild's metafile says which modules went in. The build is Node's, so test/script/
@@ -61,10 +62,10 @@ async function bundles(): Promise<Record<string, string[]>> {
   return JSON.parse(out) as Record<string, string[]>;
 }
 
-/** The UI's own modules: the `ui` package, any package's UI module tree, and the shell's generated menu table. No
- *  non-UI surface may load one. */
+/** The UI's own modules: the `ui` package, the React kit (a package of its own, so one regex covers both), any
+ *  package's UI module tree, and the shell's generated menu table. No non-UI surface may load one. */
 const isUi = (path: string): boolean =>
-  /\/packages\/ui\//.test(path) ||
+  /\/packages\/ui(-react)?\//.test(path) ||
   /\/packages\/[^/]+\/ts\/ui\//.test(path) ||
   /\/packages\/retroplug\/ui\/generated\//.test(path);
 /** The CLI's own modules: the shell's generated tool table, any package's CLI module tree, and the tool contract every
@@ -94,6 +95,20 @@ test("the UI bundle carries no package's CLI module, no tool contract and no too
   expect(cli).toEqual([]);
 });
 
+test("the UI bundle carries the kit, and no bundle carries the unlisted fixture package", async () => {
+  // The kit is UI code by package, not only by tree: its entry and any later ts/ file count as much as the components.
+  // The fixture package is listed by no product, so nothing pulls it in, and this is what would catch a product list
+  // that picked it up.
+  const built = await bundles();
+  const kit = built.ui!.filter((path) => /\/packages\/ui-react\//.test(path));
+  console.log(`# ui: ${kit.length} @retroplug/ui-react modules: ${kit.join(", ")}`);
+  expect(kit.length > 0, "the UI bundle carries no @retroplug/ui-react module").toBe(true);
+  for (const name of Object.keys(built)) {
+    const fixture = built[name]!.filter((path) => /\/packages\/ui-fixture\//.test(path));
+    expect(fixture, `${name} carries the unlisted fixture package`).toEqual([]);
+  }
+});
+
 test("the CLI dispatcher carries the generated tool table, and the planner the generated protocol data", async () => {
   // The split is not "carry no generated module": each surface carries its own. A dispatcher that lost the tool table
   // would list no package command, and a planner without the protocol data would place none.
````

Run: `sed -n '2053,2103p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/retroplug/test/script/bundle-ui-split.test.ts`
Expected: `015903120c86a24b90da4fda8b73372149dbb1dd`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 9: The kit walk: the move, held by the compiler**

`run-ui-kit-walk.mjs` walks `packages/ui-react/ts/ui` and the shell's `ui/` (skipping `generated/`) with TypeScript's
own scanner (`ts.preProcessFile`, so an `export { Box } from "../lvgl/Box"` is caught too) and prints one JSON line:
the kit's modules, the old paths a shell file still imports, and the names the shell imports from the package. The
test holds three things: every module of the kit's tree is re-exported by its index, no shell file imports a moved
module at its old path, and every name the shell imports from `@retroplug/ui-react` is one the index publishes.

Create `packages/retroplug/test/boundaries/run-ui-kit-walk.mjs`:

````js
#!/usr/bin/env node
// Drives the kit-move walk for test/boundaries/ui-kit-imports.test.ts: the shell's React widgets live in
// @retroplug/ui-react, the kit's index re-exports every one of them, and no file under packages/retroplug/ui still
// reaches a moved module at its old path.
//
// The walk is the compiler's, as check-boundaries.mjs's is: TypeScript's scanner (ts.preProcessFile) finds every
// specifier a compiler follows, so an old path is caught however it is spelled (a value import, a type import, an
// `export ... from`, a type-position `import("x")`), and its parser names what each import declaration binds, so the
// kit's index is checked against the names the shell actually imports. Nothing here greps for a path, which would match
// the comments that still describe the move.
//
//   node test/boundaries/run-ui-kit-walk.mjs [--root <dir>]
//
// Prints one JSON line: { kitIndex, movedModules, kitModules, unexported, oldPathImports, foreignBindings, kitBindings },
// where a module is a path from the root.
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const at = process.argv.indexOf("--root");
const root = at === -1 ? REPO : resolve(process.argv[at + 1] ?? "");

// TypeScript's scanner and parser, resolved from the workspace root's `typescript-5` alias (the typecheck uses 7,
// which exports no scanner). This is boundaried code, so a missing alias is a broken checkout, not a case.
const ts = createRequire(import.meta.url)("typescript-5");

const KIT = "packages/ui-react/ts/ui";
const KIT_INDEX = `${KIT}/index.ts`;
const SHELL_UI = "packages/retroplug/ui";

/** The modules that moved into the kit, as the shell had them: a relative import landing on one of these is the reach
 *  the move removed. */
const MOVED = [
  "packages/retroplug/ui/lvgl/lv.ts",
  "packages/retroplug/ui/lvgl/Box.tsx",
  "packages/retroplug/ui/lvgl/PixelCanvas.tsx",
  "packages/retroplug/ui/lvgl/useNativeEvent.ts",
  "packages/retroplug/ui/lvgl/useFramePoll.ts",
  "packages/retroplug/ui/hooks/useRerender.ts",
  "packages/retroplug/ui/screens/grid/DebugReadout.tsx",
];

const shown = (path) => relative(root, path).split("\\").join("/");

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const entry of readdirSync(dir).sort()) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) {
      // The generated menu table names every package, so it is no shell file a module is moved out of.
      if (entry !== "generated") walk(path, out);
    } else if (/\.(ts|tsx)$/.test(entry)) {
      out.push(path);
    }
  }
  return out;
}

function parse(file) {
  return ts.createSourceFile(file, readFileSync(file, "utf8"), ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
}

/** Every specifier a compiler follows in `file`, whatever form it is written in. */
function scannedSpecifiers(file) {
  return ts.preProcessFile(readFileSync(file, "utf8"), true, true).importedFiles.map((entry) => entry.fileName);
}

/** Every import of `source`: its specifier and the names it binds (the default import as "default", a namespace as
 *  "*"). */
function imports(source) {
  const out = [];
  for (const statement of source.statements) {
    if (!ts.isImportDeclaration(statement) || !ts.isStringLiteral(statement.moduleSpecifier)) continue;
    const specifier = statement.moduleSpecifier.text;
    const names = [];
    const clause = statement.importClause;
    if (clause?.name) names.push(clause.name.text);
    const bindings = clause?.namedBindings;
    if (bindings) {
      if (ts.isNamespaceImport(bindings)) names.push("*");
      else for (const element of bindings.elements) names.push((element.propertyName ?? element.name).text);
    }
    out.push({ specifier, names });
  }
  return out;
}

/** The names a module's index exports, from its export declarations: the clause's names, and a re-exported module's
 *  own names through `export *`. */
function exportsOf(source, file, root, seen = new Set()) {
  const names = new Set();
  const modules = [];
  for (const statement of source.statements) {
    if (!ts.isExportDeclaration(statement)) continue;
    const target = statement.moduleSpecifier && ts.isStringLiteral(statement.moduleSpecifier)
      ? statement.moduleSpecifier.text
      : null;
    if (target !== null) modules.push(target);
    if (!statement.exportClause) {
      // `export * from "./x"`: the names are that module's, resolved one level (the kit's index spells its own out).
      const resolved = target === null ? null : resolveFile(dirname(file), target);
      if (resolved === null || seen.has(resolved)) continue;
      seen.add(resolved);
      for (const name of exportsOf(parse(resolved), resolved, root, seen)) names.add(name);
    } else if (ts.isNamedExports(statement.exportClause)) {
      for (const element of statement.exportClause.elements) names.add(element.name.text);
    }
  }
  return names;
}

const EXTENSIONS = [".ts", ".tsx", ".d.ts", ".js", ".jsx", ".mjs", ".cjs"];

/** A specifier's file, resolved as a compiler resolves one: the path, then each extension, then each directory index. */
function resolveFile(fromDir, specifier) {
  const base = resolve(fromDir, specifier);
  return [base, ...EXTENSIONS.map((e) => base + e), ...EXTENSIONS.map((e) => join(base, `index${e}`))].find((path) =>
    existsSync(path),
  ) ?? null;
}

/** The kit's modules: every file of its ts/ui tree but the index, which is the one that exports them. */
const kitIndex = join(root, KIT_INDEX);
const kitFiles = walk(join(root, KIT));
const kit = existsSync(kitIndex) ? parse(kitIndex) : null;
const kitExports = kit === null ? new Set() : exportsOf(kit, kitIndex, root);
const reexported = new Set();
if (kit !== null) {
  for (const statement of kit.statements) {
    if (!ts.isExportDeclaration(statement) || !statement.moduleSpecifier) continue;
    if (!ts.isStringLiteral(statement.moduleSpecifier)) continue;
    const resolved = resolveFile(dirname(kitIndex), statement.moduleSpecifier.text);
    if (resolved !== null) reexported.add(resolved);
  }
}

const unexported = kitFiles.filter((file) => file !== kitIndex && !reexported.has(file)).map(shown);

const oldPaths = new Set(MOVED.map((path) => join(root, path)));
const oldPathImports = [];
const foreignBindings = [];
let kitBindings = 0;
for (const file of walk(join(root, SHELL_UI))) {
  // The scanner's specifiers, not the parser's: an `export { Box } from "../lvgl/Box"` reaches an old path exactly as an
  // import does, and only the scanner sees it.
  for (const specifier of scannedSpecifiers(file)) {
    if (!specifier.startsWith(".")) continue;
    const landed = resolveFile(dirname(file), specifier);
    if (landed !== null && oldPaths.has(landed)) {
      oldPathImports.push({ file: shown(file), specifier });
      continue;
    }
    // A specifier that lands on no file but spells a moved module's own name is the same reach, told by the path it
    // would resolve at rather than by a file that still exists.
    const spelled = [...oldPaths].find((path) => path.replace(/\.tsx?$/, "") === resolve(dirname(file), specifier));
    if (landed === null && spelled !== undefined) oldPathImports.push({ file: shown(file), specifier });
  }
  for (const { specifier, names } of imports(parse(file))) {
    if (specifier !== "@retroplug/ui-react") continue;
    for (const name of names) {
      if (name === "*") continue;
      kitBindings += 1;
      if (!kitExports.has(name)) foreignBindings.push({ file: shown(file), specifier, name });
    }
  }
}

console.log(
  JSON.stringify({
    kitIndex: existsSync(kitIndex) ? KIT_INDEX : null,
    movedModules: MOVED,
    kitModules: kitFiles.filter((file) => file !== kitIndex).map(shown),
    unexported,
    oldPathImports,
    foreignBindings,
    kitBindings,
  }),
);
````

Run: `mkdir -p packages/retroplug/test/boundaries; sed -n '2123,2303p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/run-ui-kit-walk.mjs`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/run-ui-kit-walk.mjs`
Expected: `6da821a97fd16fe4090330fe6a4f745b87ca3a8c`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/ui-kit-imports.test.ts`:

````ts
// The kit's move (docs/modular-graph-phase-f4c-design.md section 4.2): the shell's React widgets are @retroplug/ui-react's
// now, so every file of the kit's ts/ui tree is re-exported by its index, no file under packages/retroplug/ui reaches one
// at its old path, and every name the shell imports from the package is one the index publishes.
//
// The walk is the compiler's, as the boundary check's is (test/boundaries/run-ui-kit-walk.mjs): a grep would match the
// comments that still describe where each widget came from, and would miss a specifier spelled any other way.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-ui-split.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(
    args: string[],
    opts: { stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

async function text(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const chunks: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  let total = 0;
  for (const c of chunks) total += c.length;
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.length;
  }
  return new TextDecoder().decode(all);
}

interface Walk {
  kitIndex: string | null;
  movedModules: string[];
  kitModules: string[];
  unexported: string[];
  oldPathImports: { file: string; specifier: string }[];
  foreignBindings: { file: string; specifier: string; name: string }[];
  kitBindings: number;
}

/** The walk's report for a tree (this one by default, or a checkout of another). */
async function walk(root?: string): Promise<Walk> {
  const child = tjs.spawn(
    [tjs.env.RP_NODE || "node", "test/boundaries/run-ui-kit-walk.mjs", ...(root ? ["--root", root] : [])],
    { stdout: "pipe", stderr: "pipe" },
  );
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the kit walk failed: ${err || out}`);
  return JSON.parse(out.trim().split("\n").at(-1)!) as Walk;
}

test("every module of the kit's ts/ui tree is re-exported by its index", async () => {
  const report = await walk();
  console.log(`# ${report.kitModules.length} kit modules, ${report.unexported.length} unexported`);
  expect(report.kitIndex).toBe("packages/ui-react/ts/ui/index.ts");
  // No frozen size: the kit may gain a module, and the case is that the walk saw the tree and nothing of it is left out.
  expect(report.kitModules.length, "the walk saw no kit module").toBeGreaterThan(0);
  expect(report.unexported).toEqual([]);
});

test("no shell file imports a moved module from its old path", async () => {
  const report = await walk();
  console.log(`# ${report.oldPathImports.length} old-path import(s)`);
  expect(report.oldPathImports).toEqual([]);
});

test("every name the shell imports from the kit is one its index publishes", async () => {
  // A name the index does not export would fail `tsc` too; this says which name, and holds the walk itself to seeing
  // the shell's imports rather than passing because it found none.
  const report = await walk();
  console.log(`# ${report.kitBindings} kit binding(s), ${report.foreignBindings.length} the kit does not export`);
  expect(report.kitBindings, "the walk saw no name the shell imports from the kit").toBeGreaterThan(0);
  expect(report.foreignBindings).toEqual([]);
});
````

Run: `mkdir -p packages/retroplug/test/boundaries; sed -n '2315,2402p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/ui-kit-imports.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/ui-kit-imports.test.ts`
Expected: `b307717b454d2d7e6ac976fa950f2f20c575eea1`: the blob d1352d6ef holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 10: The two docs' links**

Two documents linked the seven moved files by path; their links are repointed and their prose is Task 5's.

````diff
diff --git a/docs/ui-shared-utils-audit.md b/docs/ui-shared-utils-audit.md
index 7e63be93e..3a6695946 100644
--- a/docs/ui-shared-utils-audit.md
+++ b/docs/ui-shared-utils-audit.md
@@ -60,7 +60,7 @@ channel map (`{ key: [code, press, mod]; frame: [tick]; mouse: [button, press, x
 [pad, name, press]; "gamepad-axis": [pad, axis, value]; resize: [w, h]; "file-drop": [paths, x, y] }`)
 makes `useNativeEvent("key", (code, press, mod) => ...)` type-checked.
 
-Done: `NativeEvents` in [useNativeEvent.ts](../packages/retroplug/ui/lvgl/useNativeEvent.ts), each channel
+Done: `NativeEvents` in [useNativeEvent.ts](../packages/ui-react/ts/ui/useNativeEvent.ts), each channel
 annotated with the native files that emit it; every `args[n] as T` is gone. Note the type is only as good
 as the emit sites it was read off - nothing checks the C++ side against it.
 
@@ -77,10 +77,10 @@ Related: `sys.roles.find((r) => r.kind === X)?.config` appears ~10 times (menuDe
 runtime hooks, the HD session) and `systems.find((s) => s.id === id)` 6 times. `roleConfig(sys, kind)` /
 `hasRole(sys, kind)` helpers and a `useSystem(id)` hook.
 
-Done: `useFrameSampled(sample, signature)` in [useFramePoll.ts](../packages/retroplug/ui/lvgl/useFramePoll.ts)
+Done: `useFrameSampled(sample, signature)` in [useFramePoll.ts](../packages/ui-react/ts/ui/useFramePoll.ts)
 (sample per frame, re-render only when the displayed signature moves; `undefined` skips a frame; a new
 `sample` identity resets) now backs both runtime hooks and the render badge.
-[DebugReadout.tsx](../packages/retroplug/ui/screens/grid/DebugReadout.tsx) is the shared overlay shell + `hex2`.
+[DebugReadout.tsx](../packages/ui-react/ts/ui/DebugReadout.tsx) is the shared overlay shell + `hex2`.
 `findRole` / `roleConfig` / `hasRole` live next to `RoleInstance` in
 [systemRoles.ts](../packages/core/ts/systemRoles.ts) and replace every `roles.find(...)` in `ui/`
 (menuDefs' private `roleConfigFor` is gone); `src/` still has 10 of its own, untouched. `useSystem(id)` is
@@ -97,9 +97,9 @@ Proposal: `lvgl/lv.ts` with the constants and typed `TextEx` / `CanvasEx` wrappe
 binding traps currently documented only inside Menu.tsx comments (`flex-grow` needs `display: flex` on the
 child, a Box needs a measured height since there's no LV_SIZE_CONTENT, Texts bubble clicks).
 
-Done: [lv.ts](../packages/retroplug/ui/lvgl/lv.ts) holds `ViewEx` / `TextEx` / `CanvasEx` (the one place the
+Done: [lv.ts](../packages/ui-react/ts/ui/lv.ts) holds `ViewEx` / `TextEx` / `CanvasEx` (the one place the
 components are cast, behind prop types for what native accepts), `CanvasHandle`, the event types and
-`LV_IMAGE_ALIGN_CONTAIN`; [PixelCanvas.tsx](../packages/retroplug/ui/lvgl/PixelCanvas.tsx) is the shared
+`LV_IMAGE_ALIGN_CONTAIN`; [PixelCanvas.tsx](../packages/ui-react/ts/ui/PixelCanvas.tsx) is the shared
 nearest-neighbour CONTAIN canvas. The `LV_ALIGN_*` copies are gone: lvgljs-ui already exports `EAlignType`.
 No `as any` is left in `ui/`. The binding-trap notes were left in Menu.tsx next to the code they explain.
 
@@ -122,7 +122,7 @@ both SystemGrid and hitTestTile call it.
   ([useStateRef.ts](../packages/retroplug/ui/hooks/useStateRef.ts)).
 - DONE: `const [, bump] = useState(0)` force-updates in App, `useFileBrowser`, `useVersionedPoll`, and App's
   five-subscribable effect: `useForceUpdate` / `useRerenderOn(...subscribes)`
-  ([useRerender.ts](../packages/retroplug/ui/hooks/useRerender.ts)).
+  ([useRerender.ts](../packages/ui-react/ts/ui/useRerender.ts)).
 - DONE: `lsdjDebug.ts` hand-rolled a listener set; it uses `createSubscribable` now.
 - DONE: held-slot press/release routing (useGameInput / useGamepadInput) is `useHeldRouting`, and stick
   edge detection (useGamepadInput / Menu) is `useAxisEdges`
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index a0792e382..f1c2359db 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -624,7 +624,7 @@ widget's native uid for the test harness and is inert in production.
 
 ### LVGL primitives — [`ui/lvgl/`](../packages/retroplug/ui/lvgl)
 
-- [`Box.tsx`](../packages/retroplug/ui/lvgl/Box.tsx) — a `<View>` with the LVGL
+- [`Box.tsx`](../packages/ui-react/ts/ui/Box.tsx) - a `<View>` with the LVGL
   default-theme chrome zeroed, caller style merged on top.
 - [`FocusProvider.tsx`](../packages/retroplug/ui/lvgl/FocusProvider.tsx) — owns the
   app-wide empty **"sink"** keyboard group's lifecycle so LVGL doesn't route arrows/Enter into
@@ -634,17 +634,17 @@ widget's native uid for the test harness and is inert in production.
   claim-the-keypad dance: a `useLayoutEffect` builds a fresh group from ordered refs, focuses a
   target, `setKeyboardGroup(group)`, and on cleanup restores the sink (**never null**) and
   destroys the group.
-- [`useNativeEvent.ts`](../packages/retroplug/ui/lvgl/useNativeEvent.ts) — subscribes to
+- [`useNativeEvent.ts`](../packages/ui-react/ts/ui/useNativeEvent.ts) - subscribes to
   the `lvgljs` `on`/`off` bus (`frame` / `key` / `resize` / …) with a stable listener + handler
   ref (no re-subscribe on inline closures).
 - [`useWindowSize.ts`](../packages/retroplug/ui/lvgl/useWindowSize.ts) — the live window
   size reactive to `"resize"`; `requestWindowSize` / `isWindowSizeControlled` are the TS side of
   the `__rp_*` native window seam.
-- [`lv.ts`](../packages/retroplug/ui/lvgl/lv.ts) - typed `ViewEx` / `TextEx` / `CanvasEx`: the one
+- [`lv.ts`](../packages/ui-react/ts/ui/lv.ts) - typed `ViewEx` / `TextEx` / `CanvasEx`: the one
   place the lvgljs-ui components are cast, behind prop types for what native accepts. Plus
-  [`PixelCanvas.tsx`](../packages/retroplug/ui/lvgl/PixelCanvas.tsx) (a crisp, aspect-kept frame
+  [`PixelCanvas.tsx`](../packages/ui-react/ts/ui/PixelCanvas.tsx) (a crisp, aspect-kept frame
   canvas) and [`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx).
-- [`useFramePoll.ts`](../packages/retroplug/ui/lvgl/useFramePoll.ts) - the frame-clocked polls:
+- [`useFramePoll.ts`](../packages/ui-react/ts/ui/useFramePoll.ts) - the frame-clocked polls:
   every-N-frames, versioned native status, and per-frame sampling deduped on a display signature.
 
 The rest of `ui/` outside `screens/` is split by what it depends on:
````

Run: `sed -n '2416,2497p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
docs/ui-shared-utils-audit.md
spec/03-ts-layer.md
EOF
````

Expected:

````text
3a6695946b0b57ce449ad13b9706358d35559652
f1c2359dbf61f9cd3a03f83df591d0d9151a6fe0
````

- [ ] **Step 11: The red proofs**

Four REDs, each recorded by the stage that ran it, and each is the check BEFORE the change. They are quoted whole,
since each is short and every line is the proof.

````text
=== react-allowed (OLD checker)
warning: alpha: declares the host module react/jsx-runtime, which no file of its imports
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 1 warning(s))
status=0
=== react-outside-ui (OLD checker)
warning: alpha: declares the host module react/jsx-runtime, which no file of its imports
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 1 warning(s))
status=0
=== react-undeclared (OLD checker)
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))
status=0
=== react-cli-import (OLD checker)
warning: alpha: declares the host module react, which no file of its imports
refused: packages/alpha/ts/cli/tool.ts: imports "react", a ui host module, from a file that surface does not own: a ui host module is reached only from the package's ts/ui/ trees
check-boundaries: 1 refusal(s), 0 stale exception(s) under cuttlefish/packages/retroplug/test/boundaries/fixtures/react-cli-import
status=1
````

Run: `sed -n '2525,2540p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `c32035b9b2646427abedc33d76aceb29e248e8750ebbf818b3a5f6496a5a1a70  -`: the log's own bytes (`/tmp/f4cpro/s1-red-rule3.log`), so the block above is the run and not a selection from it. The old checker over the four new fixture trees: `react-outside-ui` and `react-undeclared` exit 0 (admitted), the accepted one warns that `react/jsx-runtime` is unused, and `react-cli-import` is refused by the surface rule on both sides (the case is kept because the design lists it, and it proves the surface half).

````text
OLD generator: accepted a package declaring a UI host module with no tsconfig (RED)
````

Run: `sed -n '2547,2547p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `e8f89b8a630306f0c99ef33a3ffa51e96eec0a09816df077a585e83b93918554  -`: the log's own bytes (`/tmp/f4cpro/s1-red-generator.log`), so the block above is the run and not a selection from it. The old generator accepts a package declaring a UI host module with no tsconfig. The reproduction at this task's parent: extract the design commit's `packages/retroplug/scripts/lib/packages.mjs` over the new fixture tree's driver, or run `git show HEAD:packages/retroplug/scripts/lib/packages.mjs` as the driver's generator, and the generator's cases for the three new modes fail as this line says.

````text
a kit file outside ts/ui/: old isUi -> false , new isUi -> true
````

Run: `sed -n '2554,2554p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `9af7687f887d561fed2c73297a40382e321ff297caf9f479eae1bead6bd0d596  -`: the log's own bytes (`/tmp/f4cpro/s1-red-bundle-regex.log`), so the block above is the run and not a selection from it. The old `isUi` misses a kit file outside `ts/ui/`; the new one sees it.

````text
control-plane old pattern UI modules: 0 | new pattern: packages/ui-react/ts/probe.ts
render-worker old pattern UI modules: 0 | new pattern: packages/ui-react/ts/probe.ts
script-nodes old pattern UI modules: 0 | new pattern: 
cli-sdk old pattern UI modules: 0 | new pattern: packages/ui-react/ts/probe.ts
cli-dispatcher old pattern UI modules: 0 | new pattern: packages/ui-react/ts/probe.ts
planner old pattern UI modules: 0 | new pattern: packages/ui-react/ts/probe.ts
````

Run: `sed -n '2561,2566p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `579b0b41930425dd564bda796ef7dd27cacb8c615f04dd13717767bd1d0dcd80  -`: the log's own bytes (`/tmp/f4cpro/s1-red-bundle-deliberate.log`), so the block above is the run and not a selection from it. A deliberate `packages/ui-react/ts/probe.ts` imported by the control plane was carried by five non-UI bundles and named by no old-pattern UI module. The probe was reverted, and the new pattern names it in all five (the script-nodes bundle does not reach it).

The kit walk's RED is the walk run over the pre-move tree, which is why the tree is derived rather than the live one
changed: the next step derives it from the design commit and runs the walk there.

Run: `bash -c 'D=/tmp/f4c-premove; rm -rf "$D" && mkdir -p "$D" && git archive 663632d16 | tar -x -C "$D" && node packages/retroplug/test/boundaries/run-ui-kit-walk.mjs --root "$D" | head -c 400; echo'`
Expected: one JSON line beginning `{"kitIndex":null,"movedModules":[`: over the tree BEFORE the moves the kit's index does not exist (`kitIndex` null) and every one of the shell's 36 old-path reaches is reported, which is the RED (the walk is this task's, the tree is the design commit's). The log above is the same run, its `old-path imports: 36` line and the first of the list.

Run: `bash -c 'D=/tmp/f4c-premove; [ -d "$D" ] || { mkdir -p "$D" && git archive 663632d16 | tar -x -C "$D"; }; node packages/retroplug/test/boundaries/run-ui-kit-walk.mjs --root "$D" | grep -c specifier'`
Expected: `36`: the number of old-path imports the pre-move tree holds, which this task's move takes to 0 (the live tree's walk reports an empty `oldPathImports`, and the test's `no shell file imports a moved module at its old path` case passes).

- [ ] **Step 12: Configure, install, build, and the bytes that must not move**

The configure reads the two new manifests (an unlisted package with no native part needs nothing from CMake, and the
shell's dependency is a workspace link), and the build writes the four React paths into the generated configs. The UI
bundle's hash is NOT an oracle (see the header): its size is reported and its normalised diff is what stage 1 checked.

Run: `cmake -S . -B build`
Expected: exit 0: the two new packages' manifests are read, the product list is unchanged (neither is a product), and the native boundary check has nothing to judge in either (no native member, no product entry).

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and the lockfile is the one the block above placed: `git hash-object pnpm-lock.yaml` prints the commit's blob.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree, and the generator's line for the module lists it writes (the four React paths are in `tsconfig.host-modules.json` and `tsconfig.paths.json`, both regenerated).

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

- [ ] **Step 13: Run the suites**

Task 1's figures are stage 1's own full run (`/tmp/f4cpro/s1v-*.log`, every leg exit 0, no retries, at `8f0f5e17c`),
whose code tree is this task's but for the stage 1 fix pass: `reactView`'s props type, `badgeView`'s `satisfies
TileView`, one `@retroplug/ui-react` import per file, the wrong-props type case, the walk's `ts.preProcessFile`, the
stale comments and the two em dashes. That run was taken before the pass was folded in; the pass's own exit was `pnpm
typecheck` and `pnpm test` (221 files) and `check:boundaries`, recorded in the stage 2 report, and the stage 2 run
below is over the fixed tree. The code the plan places is the commit's, so a file count here that differs from your
run is the pass's one added case, not a finding.

The verification is the parallel kit, one command, run in the foreground and waited for: it sets each leg's own
`RETROPLUG_RESOURCES_DIR`, skips `sdl:controller` (which claims system-wide ALSA ports) and logs one file per leg. Run
`pnpm sdl:controller` on its own afterwards, never another way, and compare the summary and the gated logs with the
baseline at the design commit.

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4c-v bash .superpowers/verify/tip-verify-par.sh`
Expected: exit 0 and `== every leg passed`, with no `/tmp/f4c-v-retries.txt` (a retry is a leg the kit re-ran; report it). The base's run is `/tmp/f4cpro/base-*` and every leg below quotes this task's own log.

Run: `pnpm sdl:controller`
Expected: exit 0: `SDL controller OK`, after fifteen `ok:` lines. The first ten are the bliptoaster pack's toolchain, which the SDL build runs and prints (the same ten the baseline's log has), and the last five are the harness's own legs, byte-identical to the baseline's. It sets `RP_MIDI_PORT_ALLOW` itself, from the two fakes' ready lines, and refuses a caller's list. The five:

````text
  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream
  ok: reclaimed before MIDI opens and audio starts; its notes kept out of the shared stream; released at quit
  ok: Programmer mode first, LED traffic, the release last
  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside
  ok: a second standalone leaves the first one's device alone, and does not hear its pads
```` This is the one command in this plan that may touch a MIDI port at all, and it sets `RP_MIDI_PORT_ALLOW` from its two ALSA fakes itself.

Run: `bash .superpowers/verify/compare.sh /tmp/f4cpro/base /tmp/f4c-v`
Expected: exit 1 (anything differs), printing only the lines this task is expected to move: the `test` case and file counts, `ui`'s from Task 4, Task 1's two `typecheck` programs and the `measure` sizes (info only, never gated). `imagesha` and `packbytes` must be ABSENT from the diff, and every other leg's counts must be the baseline's. A difference the task does not name is a finding.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree. No compiler warning is expected: every stage's build log has none.

Run: `pnpm typecheck`
Expected: `Scope: 25 of 26 workspace projects`, then `packages/retroplug typecheck: Done`: every project with a `typecheck` script typechecks on its own tsconfig, and the shell's program covers every package's `ts/` as well and `packages/ui-react typecheck: Done` and `packages/ui-fixture typecheck: Done` (the two packages this task adds, each on its own tsconfig). The baseline's scope is `Scope: 23 of 24 workspace projects` with 17 `Done` lines; this tree's is 25 of 26 and 19.

Run: `pnpm test`
Expected: `# 221 test file(s) passed (jobs=5)`. The runner's own line counts FILES; the case total is the stage 1 report's table: "`test` | ok=2286, 219 files | ok=2299, 221 files", plus the one case its fix pass adds (the wrong-props `reactView` case, `reactView.test.ts`'s third), so the amended tree's own run is 221 files and one case more than that figure, since no runner prints one in this suite. The pool size it prints is the kit's `TV_TS_JOBS` (5); a hand run prints its own, so the file count is what must match.

Run: `pnpm test:plugin`
Expected: `# 13 plugin test binary(ies) passed (jobs=5)`: every binary exits 0, and the counts are (quoted as the log prints them, the engine-graph suite's assertion total and its eleven skips as that binary prints them):

````text
retroplug-mesen-test: `All tests passed (82321 assertions in 80 test cases)`
retroplug-watcher-test: `All tests passed (6 assertions in 2 test cases)`
retroplug-audio-test: `All tests passed (378 assertions in 16 test cases)`
retroplug-graph-test: `All tests passed (356662 assertions in 296 test cases)`
retroplug-classid-test: `All tests passed (10 assertions in 3 test cases)`
retroplug-lottie-test: `All tests passed (7 assertions in 1 test case)`
retroplug-plugin-test: `All tests passed (21 assertions in 6 test cases)`
retroplug-dynparams-test: `All tests passed (24 assertions in 7 test cases)`
retroplug-midi-test: `All tests passed (78 assertions in 24 test cases)`
retroplug-launchpad-test: `All tests passed (11747 assertions in 53 test cases)`
retroplug-sameboy-test: `All tests passed (1152140 assertions in 7 test cases)`
retroplug-device-mode-test: `All tests passed (1504408 assertions in 96 test cases)`
retroplug-engine-graph-test: `test cases: 336 | 325 passed | 11 skipped`
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1715430 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the fixture-gated cases run.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, none skipped, nothing re-recorded: 30 files and 250 cases (the reports' count), the device images' and the pack's bytes identical to the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 144 case(s) SKIPPED in 65 file(s)` (its list of files follows) and the skip check passing: CI's view, its skips the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 43 UI test file(s) passed (jobs=4)`. The case total is the stage 1, 2 and 3 reports': `43 files, 49 cases`, unchanged by the three stages (no product gains a view or an action until Task 4) (a case that cannot run SKIPS and is counted, and this leg runs WITH the fixtures).

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks.

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

Run: `pnpm test:n8`
Expected: `All tests passed (1468 assertions in 104 test cases)` (`retroplug-n8-test`) and `All tests passed (17 assertions in 2 test cases)` (`retroplug-serial-test`), the baseline's: fakes only, no port is opened (the `[retroplug] N8 restore: port '/dev/ttyACM0'` line it prints is a test case's, over a fake port factory and a scratch n8.cfg; the baseline's run printed it too).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed (the counts are the reports' tables, each stage's).

Run: `bash -c 'for t in render-menu render-badge; do pnpm test:ui $t || exit 1; done'`
Expected: exit 0: `render-menu` and `render-badge`, one test file each and 1 passed.

Run: `pnpm test render`
Expected: `# 9 test file(s) passed (jobs=5)`: the render worker's own files.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after the smoke's eight `ok:` lines (the build before them prints the bliptoaster pack's toolchain lines, which start `ok:` too). It runs under a scratch config dir with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which matches nothing, so it claims and opens nothing.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three ThreadSanitizer binaries, built from this tree's sources (their objects are the prototype's until the first build here).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1706656 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`.

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'`
Expected: `All tests passed (1675 assertions in 53 test cases)`.

Run: `bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'`
Expected: `All tests passed (1499768 assertions in 51 test cases)`.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three AddressSanitizer binaries, built from this tree's sources.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1682111 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356662 assertions in 296 test cases)`.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'`
Expected: `All tests passed (1504408 assertions in 96 test cases)`. The prototype's first full run failed this leg once, on the script pack's deadline case under ASAN on a loaded machine (no device-mode code touched by any stage), and its re-run was green; every stage's recorded run after it is green. If yours fails, re-run it ONCE and report both.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (1504408 assertions in 96 test cases)`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Then the per-stage tree rule, which the prototype adopted after a broken intermediate commit: the tree this task
commits is extracted from the commit AFTER the commit step (so it is the sealed tree), proven equal to it by a fresh
index read from the commit, installed, generated, typechecked and tested on its own. Nothing here links this
worktree's `build/`; the pure runner reads the tjs binary through `RETROPLUG_TJS` instead.

Run: `bash -c 'D=/tmp/f4c-tree-1; rm -rf "$D" && mkdir -p "$D" && git archive d1352d6ef | tar -x -C "$D" && echo extracted'`
Expected: `extracted`: the commit's tree, with no generated module in it (they are gitignored and `git archive` does not carry them) and no `build/`.

Run: `bash -c 'D=/tmp/f4c-tree-1; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree d1352d6ef && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-1-eq.txt; e=$?; rm -f $E; wc -l < /tmp/f4c-tree-1-eq.txt'`
Expected: `0`: the extracted tree is its commit's, line for line, read against a fresh index because this worktree's own index holds a later task's deletions. `deps` is excluded: its submodule directories are links, which is the next step's doing.

Run: `bash -c 'D=/tmp/f4c-tree-1; W=$PWD; for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done; cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo wired'`
Expected: `wired`: the archive's submodule paths are links to this worktree's checkouts (read only) and its own offline install wires the workspace's links from the COMMIT's lockfile, which is also what proves that lockfile installs.

Run: `bash -c 'D=/tmp/f4c-tree-1; T=/workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs; cd "$D" && pnpm -r typecheck && RETROPLUG_TJS=$T node packages/retroplug/scripts/run-tests.mjs menu && node packages/retroplug/scripts/run-tests.mjs packages && node packages/retroplug/scripts/run-tests.mjs hosts && node packages/retroplug/scripts/run-tests.mjs boundaries && node packages/retroplug/scripts/run-tests.mjs ui-react/reactView && node packages/retroplug/scripts/run-tests.mjs script/bundle-ui-split'`
Expected: exit 0: every package typechecks against its own tsconfig, and each named suite passes on this tree alone, with the worktree's tjs binary read through `RETROPLUG_TJS` and nothing written to this worktree. The per-tree record, which this plan's generator ran for all five trees (each extract equal to its commit before and after, 19 `Done` lines, every suite exit 0): tree 1 `menu` 17 files, `packages` 2, `hosts` 2, `boundaries` 3, `ui-react/reactView` 1 and `script/bundle-ui-split` 1; tree 2 `menu` 17, `packages` 2, `hosts` 2, `ui` 4 and `boundaries` 3; tree 3 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `input` 3, `config` 6 and `boundaries` 3; tree 4 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `lsdj/ui-views` 1 and `risa/ui-views` 1; tree 5 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `boundaries` 2 and `source-literals` 1.

Run: `bash -c 'D=/tmp/f4c-tree-1; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree d1352d6ef && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-1-eq2.txt; rm -f $E; wc -l < /tmp/f4c-tree-1-eq2.txt'`
Expected: `0`: the install, the generator and the suites changed no tracked file, so the tree is still its commit's after the runs (the generated modules and the runner's build dir are ignored).

- [ ] **Step 14: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M docs/ui-shared-utils-audit.md
 M packages/retroplug/host-modules.json
 M packages/retroplug/package.json
 M packages/retroplug/scripts/check-boundaries.mjs
 M packages/retroplug/scripts/lib/packages.mjs
 M packages/retroplug/test/boundaries/boundaries-checker.test.ts
 M packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json
 M packages/retroplug/test/packages/packages-generator.test.ts
 M packages/retroplug/test/packages/run-generator.mjs
 M packages/retroplug/test/script/bundle-ui-split.test.ts
 M packages/retroplug/tsconfig.json
 M packages/retroplug/tsconfig.ui.json
 M packages/retroplug/ui/App.tsx
 M packages/retroplug/ui/app/useFileBrowser.ts
 M packages/retroplug/ui/app/useJobPump.ts
 M packages/retroplug/ui/app/useProjectModals.ts
 M packages/retroplug/ui/app/useSongWatch.ts
 M packages/retroplug/ui/app/useSramAutoSave.ts
 M packages/retroplug/ui/app/useUiServices.ts
 M packages/retroplug/ui/input/useGameInput.ts
 M packages/retroplug/ui/input/useGamepadInput.ts
 M packages/retroplug/ui/lvgl/StableSlot.tsx
 M packages/retroplug/ui/lvgl/useWindowSize.ts
 M packages/retroplug/ui/screens/grid/EmulatorTile.tsx
 M packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
 M packages/retroplug/ui/screens/grid/RisaOverlay.tsx
 M packages/retroplug/ui/screens/grid/SystemGrid.tsx
 M packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
 M packages/retroplug/ui/screens/grid/useRisaRuntime.ts
 M packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
 M packages/retroplug/ui/screens/menu/Menu.tsx
 M packages/retroplug/ui/types/lvgljs-ui.d.ts
R  packages/retroplug/ui/lvgl/Box.tsx -> packages/ui-react/ts/ui/Box.tsx
RM packages/retroplug/ui/screens/grid/DebugReadout.tsx -> packages/ui-react/ts/ui/DebugReadout.tsx
R  packages/retroplug/ui/lvgl/PixelCanvas.tsx -> packages/ui-react/ts/ui/PixelCanvas.tsx
R  packages/retroplug/ui/lvgl/lv.ts -> packages/ui-react/ts/ui/lv.ts
RM packages/retroplug/ui/lvgl/useFramePoll.ts -> packages/ui-react/ts/ui/useFramePoll.ts
R  packages/retroplug/ui/lvgl/useNativeEvent.ts -> packages/ui-react/ts/ui/useNativeEvent.ts
R  packages/retroplug/ui/hooks/useRerender.ts -> packages/ui-react/ts/ui/useRerender.ts
 M pnpm-lock.yaml
 M spec/03-ts-layer.md
 M tsconfig.base.json
?? packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json
?? packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx
?? packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts
?? packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json
?? packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json
?? packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts
?? packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json
?? packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json
?? packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx
?? packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json
?? packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json
?? packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx
?? packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts
?? packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json
?? packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/run-ui-kit-walk.mjs
?? packages/retroplug/test/boundaries/ui-kit-imports.test.ts
?? packages/ui-fixture/package.json
?? packages/ui-fixture/ts/ui/Badge.tsx
?? packages/ui-fixture/ts/ui/index.ts
?? packages/ui-fixture/tsconfig.json
?? packages/ui-react/package.json
?? packages/ui-react/ts/test/reactView.test.ts
?? packages/ui-react/ts/ui/index.ts
?? packages/ui-react/ts/ui/reactView.ts
?? packages/ui-react/ts/ui/tagTestId.ts
?? packages/ui-react/tsconfig.json
````

````bash
git add -- docs/ui-shared-utils-audit.md packages/retroplug/host-modules.json packages/retroplug/package.json \
  packages/retroplug/scripts/check-boundaries.mjs packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/test/boundaries/boundaries-checker.test.ts \
  packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/package.json \
  packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/Badge.tsx \
  packages/retroplug/test/boundaries/fixtures/react-allowed/packages/alpha/ts/ui/index.ts \
  packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/host-modules.json \
  packages/retroplug/test/boundaries/fixtures/react-allowed/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/package.json \
  packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/alpha/ts/cli/tool.ts \
  packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/host-modules.json \
  packages/retroplug/test/boundaries/fixtures/react-cli-import/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/package.json \
  packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/alpha/ts/Badge.tsx \
  packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/host-modules.json \
  packages/retroplug/test/boundaries/fixtures/react-outside-ui/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/package.json \
  packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/Badge.tsx \
  packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/alpha/ts/ui/index.ts \
  packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/host-modules.json \
  packages/retroplug/test/boundaries/fixtures/react-undeclared/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/run-ui-kit-walk.mjs packages/retroplug/test/boundaries/ui-kit-imports.test.ts \
  packages/retroplug/test/packages/fixtures/tree/packages/retroplug/host-modules.json \
  packages/retroplug/test/packages/packages-generator.test.ts packages/retroplug/test/packages/run-generator.mjs \
  packages/retroplug/test/script/bundle-ui-split.test.ts packages/retroplug/tsconfig.json \
  packages/retroplug/tsconfig.ui.json packages/retroplug/ui/App.tsx packages/retroplug/ui/app/useFileBrowser.ts \
  packages/retroplug/ui/app/useJobPump.ts packages/retroplug/ui/app/useProjectModals.ts \
  packages/retroplug/ui/app/useSongWatch.ts packages/retroplug/ui/app/useSramAutoSave.ts \
  packages/retroplug/ui/app/useUiServices.ts packages/retroplug/ui/input/useGameInput.ts \
  packages/retroplug/ui/input/useGamepadInput.ts packages/retroplug/ui/lvgl/StableSlot.tsx \
  packages/retroplug/ui/lvgl/useWindowSize.ts packages/retroplug/ui/screens/grid/EmulatorTile.tsx \
  packages/retroplug/ui/screens/grid/LsdjOverlay.tsx packages/retroplug/ui/screens/grid/RisaOverlay.tsx \
  packages/retroplug/ui/screens/grid/SystemGrid.tsx packages/retroplug/ui/screens/grid/useLsdjRuntime.ts \
  packages/retroplug/ui/screens/grid/useRisaRuntime.ts packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx \
  packages/retroplug/ui/screens/menu/Menu.tsx packages/retroplug/ui/types/lvgljs-ui.d.ts \
  packages/ui-fixture/package.json packages/ui-fixture/ts/ui/Badge.tsx packages/ui-fixture/ts/ui/index.ts \
  packages/ui-fixture/tsconfig.json packages/ui-react/package.json packages/ui-react/ts/test/reactView.test.ts \
  packages/ui-react/ts/ui/DebugReadout.tsx packages/ui-react/ts/ui/index.ts packages/ui-react/ts/ui/reactView.ts \
  packages/ui-react/ts/ui/tagTestId.ts packages/ui-react/ts/ui/useFramePoll.ts packages/ui-react/tsconfig.json \
  pnpm-lock.yaml spec/03-ts-layer.md tsconfig.base.json
git commit -F - <<'EOF'
feat(f4c): the React kit is a package, and the typecheck follows it

@retroplug/ui-react holds the shell's React half: lv.ts, Box.tsx, PixelCanvas.tsx, useNativeEvent.ts,
useFramePoll.ts, useRerender.ts and DebugReadout.tsx (with hex2) moved by `git mv` with their text unchanged but for
their own imports, plus tagTestId.ts (which reads the optional `__rp_tagTestId` global itself, so the function exists
once and StableSlot imports it) and reactView.ts (the typed component body; its props and body shape are the view
contract's, declared locally because @retroplug/ui publishes neither yet, so the replacement is this file's two
declarations and its signature). Every file of the kit's ts/ui tree is re-exported by its index, the shell's 19
files (33 import lines, now one per file) name the package instead of the old paths, and the shell's manifest
declares it (the lockfile gains the two importers and the one dependency edge).

The typecheck: "jsx": "react-jsx" in tsconfig.base.json; react, react/jsx-runtime, lvgljs and lvgljs-ui in
host-modules.json as surface "ui" entries with the types tsconfig.ui.json hand-wrote, so the generator writes all
four into tsconfig.host-modules.json and tsconfig.paths.json; tsconfig.ui.json keeps include ["ui"] and nothing else.
The generator's "must extend the generated host-module paths" check now fires for a package that declares a UI host
module as well as for one with example sessions. The shell's program now typechecks dpf.js's runtime/lvgljs/index.ts
as source and does so cleanly, as tsconfig.ui.json already did.

Rule 3: a package's .tsx outside its ts/ui/ tree is refused, a package with any .tsx must declare react/jsx-runtime in
retroplug.host (automatic JSX imports it with no specifier, and the .tsx is that declaration's use), and the four React
modules are admitted only from ts/ui/, which the existing surface rule now covers. Four boundary fixtures hold it; the
first two were ADMITTED by the checker before this (RED), and the accepted one warned that react/jsx-runtime was
unused before the .tsx counted as its use.

The bundle split's isUi regex covers packages/ui-react; the UI bundle carries the kit's ten modules, no non-UI bundle
carries one, and packages/ui-fixture (a real, unlisted workspace pack with a component built by reactView) is asserted
absent from every bundle. RED for the widened regex: a kit file outside ts/ui/ reached by five non-UI bundles was
invisible to the old pattern. A shell test walks ui/ with the compiler and holds the move: every kit module is
re-exported by the index, no shell file imports one at its old path (RED: 36 such imports in the pre-move tree), and
every binding the shell imports from the package is one the index publishes.

The UI bundle is NOT byte-identical, which the design expected and cannot be: esbuild heads each module with its path
as a comment, so moving a file moves the bytes, and the emitted module order (the index's) renumbers the React and
jsx-runtime import helpers. Its size goes 1997971 -> 1998067 bytes and its sha256 374470ad -> 1374b968; the residual
difference is tagTestId reading globalThis instead of the shell's `native` alias (the same object) and that
renumbering. Typecheck green in every program, check:boundaries green, test:ui unchanged (43 files, 49 cases), and the
pure suite 221 files / 2299 cases with the thirteen cases this adds.
EOF
````

---

### Task 2: the view contract, its registry, and the shell's slots

Design sections 3.1, 3.3, 3.5 and 5.1 to 5.4, and the design's second stage. `@retroplug/ui` gains the declarative view
contract; the generator writes `VIEWS` and refuses the data errors a TypeScript contribution can hide; the shell gets one
renderer per body kind, one session controller, a window slot and one tile slot per registered view. No product
registers a view yet, so the shell's own HD screen and its two overlays stay exactly as they were, and `test:ui` and the
recorded matrix are unchanged.

The interim `lsdj-hd` entry is the bridge: App still special-cases the id (and its full-window branch still renders the
shell's `LsdjHdScreen`, and `uiViews` still serves the menu tests' fallback host), so the HD row works while no cart
registers a view. Task 4 deletes all three when `lsdj/ts/ui/hdView.ts` registers the same id.

**Files:**

- Create (11): `packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json`, `packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts`, `packages/retroplug/test/ui/view-session.test.ts`, `packages/retroplug/ui/views/ComponentView.tsx`, `packages/retroplug/ui/views/PixelView.tsx`, `packages/retroplug/ui/views/ReadoutView.tsx`, `packages/retroplug/ui/views/TileViews.tsx`, `packages/retroplug/ui/views/ViewWindow.tsx`, `packages/retroplug/ui/views/useViewSession.ts`, `packages/retroplug/ui/views/viewSession.ts`, `packages/ui/ts/test/signal.test.ts`
- Modify (22 files, as one diff block per group its step names: 2 under `packages/retroplug/test/packages/`; 2 under `packages/retroplug/ui/host/`; 2 under `packages/retroplug/ui/screens/grid/`; 1 under `packages/core/ts/`; 1 under `packages/core/ts/test/`; 1 under `packages/lsdj/ts/test/` and the rest)

- [ ] **Step 1: The contract, and `createSignal`**

`ts/contribution.ts` gains `Readable<T>` / `Signal<T>` (a value a package owns and the shell watches; `Subscribable`
stays as it is, since it is non-generic and carries no value), `PixelFrame` / `PixelSession` (the next frame, or null
when nothing moved, with the array and its size), `ReadoutFrame` / `ReadoutSampler` (three lines and a playing flag;
`null` means show nothing, `undefined` means unreadable this frame and keep what shows; an optional `signature` may
coarsen the dedupe key), `ViewComponentProps`, `ViewBody` (the three kinds, the component one opaque as `unknown`),
`ComponentBody` (that one arm, so `reactView` can return it), `WindowView`, `TileView` and `ViewDef`, and
`UiContribution.views`. None of it imports React, and `PixelSession.frame`'s doc states the guarantee the shell gives
a session: it is asked for a frame only once there is a canvas to put one in (the HD canvas is flush-and-diff, so a
frame read too early would be lost). `createSignal(initial)` joins `createSubscribable` in `ts/menu/subscribable.ts`:
the one place a flag's `get` / `set` / `subscribe` is written, returning the contract's `Signal`.

````diff
diff --git a/packages/ui/ts/contribution.ts b/packages/ui/ts/contribution.ts
index cce27558a..f16b9f6bb 100644
--- a/packages/ui/ts/contribution.ts
+++ b/packages/ui/ts/contribution.ts
@@ -5,7 +5,8 @@
 // defineUi({ ... }); the generator concatenates them in product order into packages/retroplug/ui/generated/menus.ts.
 //
 // Everything here is plain TypeScript: the contract a package's module is checked against, and the helpers that read a
-// row's stored value the way the shell renders it. The React kit is F4c's, in this package beside the menu model.
+// row's stored value the way the shell renders it. The React kit is @retroplug/ui-react, a package of its own, so a
+// package that draws no React never meets it.
 import type { Host, SystemInfo } from "@retroplug/core/host";
 import type { AssetCatalog, AssetTypeInfo } from "@retroplug/tracker/assetCatalog";
 import type { SongCatalog } from "@retroplug/tracker/songCatalog";
@@ -18,6 +19,101 @@ export type UiHost = Host<
   "files" | "png" | "systems" | "songs" | "liveSongs" | "dialogs" | "devices" | "views" | "controller" | "config"
 >;
 
+/** A value a package owns and the shell watches: a view's `visible`, a debug flag. `get` is synchronous and cheap
+ *  (the shell reads it during a render), and `subscribe` asks for a repaint when it moves. */
+export interface Readable<T> {
+  get(): T;
+  subscribe(fn: () => void): () => void;
+}
+
+/** The writable half, so a package's own module can flip what it exposed. `createSignal` (`@retroplug/ui/menu`) is the
+ *  one implementation, so a flag's get/set/subscribe is written once. */
+export interface Signal<T> extends Readable<T> {
+  set(value: T): void;
+}
+
+/** One frame of a pixels view: the packed pixels and the size they are laid out in. The array is the view's own (the
+ *  HD canvas hands the same one every frame) or a subarray of a shared wire buffer; the shell copies only the
+ *  latter. */
+export interface PixelFrame {
+  readonly pixels: Uint32Array;
+  readonly width: number;
+  readonly height: number;
+}
+
+/** A pixels view's session, opened by the shell for one system. */
+export interface PixelSession {
+  /** The next frame, or null when nothing moved (the shell skips the copy and the invalidate). The shell asks for a
+   *  frame only once it has a canvas to draw the result into, so a flush-and-diff session (the HD canvas reports just
+   *  the tiles that changed since its last flush) cannot have a frame swallowed. */
+  frame(): PixelFrame | null;
+  close?(): void;
+}
+
+/** One frame of a readout view: the lines it shows and whether the cart is playing (the colour). */
+export interface ReadoutFrame {
+  readonly lines: readonly string[];
+  readonly playing: boolean;
+}
+
+/** A readout view's session. */
+export interface ReadoutSampler {
+  /** The current readout: null = show nothing (an unsupported cart), undefined = unreadable this frame, keep what
+   *  shows. */
+  sample(): ReadoutFrame | null | undefined;
+  /** Dedupe key for a frame; omitted = the lines and the flag themselves, which is exactly what a readout draws, so a
+   *  sampler whose state moves without its lines changing narrows this and one that shows everything needs nothing.
+   *  It can only coarsen what the default dedupes, and a signature that throws falls back to the default. */
+  signature?(frame: ReadoutFrame): string;
+  close?(): void;
+}
+
+/** What the shell hands a component view: its host, the system it is drawn for, and the space it has. */
+export interface ViewComponentProps {
+  readonly host: UiHost;
+  readonly systemId: number;
+  readonly width: number;
+  readonly height: number;
+}
+
+/** A view's body, one kind each: the shell draws pixels and readout itself, and mounts a component as it is given
+ *  (`reactView`, in @retroplug/ui-react). `component` is opaque here because this contract imports no React. */
+export type ViewBody =
+  | {
+      readonly kind: "pixels";
+      /** The text to show when `open` returns null (an unsupported cart); absent = draw nothing. */
+      readonly unsupported?: string;
+      open(host: UiHost, systemId: number): PixelSession | null;
+    }
+  | { readonly kind: "readout"; open(host: UiHost, systemId: number): ReadoutSampler | null }
+  | { readonly kind: "component"; readonly component: unknown };
+
+/** The component kind's body: what `reactView` builds, with the component still opaque here. */
+export type ComponentBody = Extract<ViewBody, { kind: "component" }>;
+
+interface ViewBase {
+  /** Also the view's test id: "lsdj-hd", "lsdj-overlay", "risa-overlay". Unique across the product. */
+  readonly id: string;
+  readonly label: string;
+  /** The systems the view applies to; absent = every system. */
+  when?(sys: SystemInfo): boolean;
+  readonly body: ViewBody;
+}
+
+/** A view that takes the whole window in place of the grid, opened by host.views.open(id, systemId). */
+export interface WindowView extends ViewBase {
+  readonly placement: "window";
+}
+
+/** A view that is a layer over every tile whose system `when` accepts. */
+export interface TileView extends ViewBase {
+  readonly placement: "tile";
+  /** Shown only while true; absent = always. A hidden tile view opens no session and does no work. */
+  readonly visible?: Readable<boolean>;
+}
+
+export type ViewDef = WindowView | TileView;
+
 /** One choice of a config row: the value the role schema stores and the label the cycler shows. */
 export interface ConfigChoice {
   readonly value: string | number;
@@ -153,6 +249,9 @@ export interface UiContribution {
   readonly menus?: readonly MenuBuilder[];
   readonly trackers?: readonly TrackerUi[];
   readonly services?: readonly UiService[];
+  /** The window and tile views the package draws (declarative: the shell renders pixels and readout, and mounts a
+   *  component the package built with @retroplug/ui-react). */
+  readonly views?: readonly ViewDef[];
 }
 
 /** The identity function with types: a UI module's default export, checked against what a package may contribute. */
diff --git a/packages/ui/ts/menu/subscribable.ts b/packages/ui/ts/menu/subscribable.ts
index 2daa16b3c..ef51c274a 100644
--- a/packages/ui/ts/menu/subscribable.ts
+++ b/packages/ui/ts/menu/subscribable.ts
@@ -6,6 +6,7 @@
 // No React here on purpose: the module is reachable from the pure-TypeScript tests, whose tsconfig does not resolve
 // `react`. The React side is the shell's useRerender hook (`useRerenderOn(subscribeX, ...)`, what the app uses), or
 // `useSyncExternalStore(x.subscribe, ...)` as the LSDj debug readout does.
+import type { Signal } from "../contribution";
 
 export interface Subscribable {
   /** Notify every listener. Call after mutating the module's own state. */
@@ -14,6 +15,25 @@ export interface Subscribable {
   subscribe(fn: () => void): () => void;
 }
 
+/** A value its owner can set and the shell can read and watch: the writable counterpart of the contribution contract's
+ *  `Readable` / `Signal`, and the one implementation of a view's `visible` or a package's debug flag. */
+export function createSignal<T>(initial: T): Signal<T> {
+  let value = initial;
+  const listeners = new Set<() => void>();
+  return {
+    get: () => value,
+    set(next: T): void {
+      if (Object.is(next, value)) return;
+      value = next;
+      for (const l of listeners) l();
+    },
+    subscribe(fn: () => void): () => void {
+      listeners.add(fn);
+      return () => void listeners.delete(fn);
+    },
+  };
+}
+
 export function createSubscribable(): Subscribable {
   const listeners = new Set<() => void>();
   return {
````

Run: `sed -n '3020,3183p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/ui/ts/contribution.ts
packages/ui/ts/menu/subscribable.ts
EOF
````

Expected:

````text
f16b9f6bb97801baed3a22827c568ddebf8608d4
ef51c274a4769eccc4c46fc14bea463ea65e04fb
````

Create `packages/ui/ts/test/signal.test.ts`:

````ts
// createSignal, the writable Readable a package owns and the shell watches (the view contract's `visible` and a
// package's own debug flag): a get, a set, and a subscription told only when the value moves.
import { test, expect } from "@retroplug/testing";

import { createSignal } from "../menu/subscribable";

test("a signal reads back what it was set to", () => {
  const flag = createSignal(false);
  expect(flag.get()).toBe(false);
  flag.set(true);
  expect(flag.get()).toBe(true);
});

test("a signal notifies every subscriber, and only when the value moves", () => {
  const flag = createSignal(false);
  let a = 0;
  let b = 0;
  const offA = flag.subscribe(() => (a += 1));
  flag.subscribe(() => (b += 1));
  flag.set(true);
  expect(`${a}/${b}`).toBe("1/1");
  // Object.is: the same value is not a change, so a set to what is already there costs no repaint.
  flag.set(true);
  expect(`${a}/${b}`).toBe("1/1");
  offA();
  flag.set(false);
  expect(`${a}/${b}`).toBe("1/2");
});
````

Run: `mkdir -p packages/ui/ts/test; sed -n '3208,3235p' docs/modular-graph-phase-f4c-plan.md > packages/ui/ts/test/signal.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui/ts/test/signal.test.ts`
Expected: `2c8b1e31c70bf22631361766f0f3296a12c481ba`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 2: `reactView` returns the contract's body, and the fixture wires its view**

`reactView` now imports `ViewBody` and `ViewComponentProps` from `@retroplug/ui/contribution`, re-exports the props
type and returns `ComponentBody`; its two local declarations are gone and no call site changes. `ui-fixture` declares
its badge view through `defineUi({ views: [badgeView] })`, which is the one line the stage 1 report promised.

````diff
diff --git a/packages/ui-fixture/ts/ui/index.ts b/packages/ui-fixture/ts/ui/index.ts
index 3a013d28d..8b950f348 100644
--- a/packages/ui-fixture/ts/ui/index.ts
+++ b/packages/ui-fixture/ts/ui/index.ts
@@ -3,17 +3,21 @@
 // The package is listed by no product, so nothing here reaches a bundle: it is the worked example spec/06's "a view"
 // step copies, and the shape rule 3's refusals are copies of (a .tsx under ts/ui/, the React host modules its files
 // import declared in the manifest, and a tsconfig that extends the generated host-module paths).
-import { defineUi } from "@retroplug/ui/contribution";
+import { defineUi, type TileView } from "@retroplug/ui/contribution";
 import { reactView } from "@retroplug/ui-react";
 
 import { Badge } from "./Badge";
 
 export { Badge };
 
-/** The tile view this package contributes, built the way the kit asks: a component body. It is the module's own export,
- *  so the contribution below stays the four lists defineUi takes today; a product that lists the package wires the view
- *  from here, and an unlisted package's module is read by nothing. */
-export const badgeView = { id: "ui-fixture-badge", label: "Fixture Badge", placement: "tile" as const, body: reactView(Badge) };
+/** The tile view this package contributes, built the way the kit asks: a component body. `satisfies` is what the
+ *  contract checks here, since the view is declared as data rather than through a helper of its own. */
+export const badgeView = {
+  id: "ui-fixture-badge",
+  label: "Fixture Badge",
+  placement: "tile",
+  body: reactView(Badge),
+} satisfies TileView;
 
-/** The package's contribution: the rows, menus, trackers and services it adds, which is none of them. */
-export default defineUi({});
+/** The package's contribution: the view it adds and none of the rows, menus, trackers or services. */
+export default defineUi({ views: [badgeView] });
diff --git a/packages/ui-react/ts/ui/reactView.ts b/packages/ui-react/ts/ui/reactView.ts
index b3d6b7120..43a89c134 100644
--- a/packages/ui-react/ts/ui/reactView.ts
+++ b/packages/ui-react/ts/ui/reactView.ts
@@ -2,32 +2,15 @@
 //
 // @retroplug/ui types a `component` body's component as `unknown`, because the view contract imports no React; a pack
 // author who hands the shell a function with the wrong props would otherwise learn it in the shell. So a package that
-// wants React for a view calls this and gets the body back typed.
-//
-// The props and the body shape are declared here until @retroplug/ui publishes them (`ViewComponentProps` and
-// `ViewBody`, which a body of the "component" kind cannot use while the contract imports no React): they are that
-// contract's, so the replacement is this file's two declarations gone and the signature named by the imported types,
-// with no call site touched.
+// wants React for a view calls this and gets the contract's own body back typed.
 import type { ComponentType } from "react";
 
-import type { UiHost } from "@retroplug/ui/contribution";
-
-/** What the shell hands a component view: its host, the system it is drawn for, and the space it has. */
-export interface ViewComponentProps {
-  readonly host: UiHost;
-  readonly systemId: number;
-  readonly width: number;
-  readonly height: number;
-}
+import type { ComponentBody, ViewComponentProps } from "@retroplug/ui/contribution";
 
-/** A body of the view contract's "component" kind, local until the contract publishes `ViewBody`: the component,
- *  wrapped so the shell can tell it from a pixels or a readout body. */
-interface ReactViewBody {
-  readonly kind: "component";
-  readonly component: ComponentType<ViewComponentProps>;
-}
+/** The contract's props type, published from here too so a pack author takes it from the kit beside `reactView`. */
+export type { ViewComponentProps };
 
 /** A component a package draws a view with. */
-export function reactView(Component: ComponentType<ViewComponentProps>): ReactViewBody {
+export function reactView(Component: ComponentType<ViewComponentProps>): ComponentBody {
   return { kind: "component", component: Component };
 }
````

Run: `sed -n '3251,3325p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/ui-fixture/ts/ui/index.ts
packages/ui-react/ts/ui/reactView.ts
EOF
````

Expected:

````text
8b950f348dd31ced977a77aadc18097aa4bfc107
43a89c13419bd16ee1e5e56e7d923e6c24c91abb
````

- [ ] **Step 3: The host: `files.readPrefix` and `systems.readRam`**

`FilesHost` gains `readPrefix(path, bytes)`, which `filesHost` implements once over `backend.readFilePrefix`, so the
UI, CLI and render builders all gain it; the two hand-written `FilesHost` literals in tests gain the member, and a new
host-groups case drives it over the three builders and the UI's `systems.readRam` (with a `@ts-expect-error` proving
the CLI and render hosts have no `systems` group). `SystemsHost` gains `readRam(id)`, wired in the UI's `systemsHost`
only: the render host keeps its `emulator.readRam` (the groups are per surface) and the CLI has no `systems` group at
all.

````diff
diff --git a/packages/core/ts/host.ts b/packages/core/ts/host.ts
index bca6eac33..271f8eae9 100644
--- a/packages/core/ts/host.ts
+++ b/packages/core/ts/host.ts
@@ -140,6 +140,9 @@ export interface ControlSurfaceSettings {
 /** The file I/O every surface has, and the backend's own methods one for one. */
 export interface FilesHost {
   readFile(path: string): Uint8Array | null;
+  /** The first `bytes` bytes of the file, or null when it cannot be read. A header read does not copy the whole
+   *  image: the backend truncates to the file's real size. */
+  readPrefix(path: string, bytes: number): Uint8Array | null;
   writeFile(path: string, bytes: Uint8Array): boolean;
   writeFileAtomic(path: string, bytes: Uint8Array): boolean;
   fileExists(path: string): boolean;
@@ -167,6 +170,9 @@ export interface SystemsHost {
   reload(id: number): unknown;
   /** The system's live battery, or null when its core has none to read. */
   readSram(id: number): Uint8Array | null;
+  /** The system's live work RAM, or null when it has none to read. A UI-only read: the render surface reaches work RAM
+   *  through its one emulator group instead. */
+  readRam(id: number): Uint8Array | null;
   /** Reboot a system from the `.sav` at `path`: the one rebuild an import ends in. */
   loadSram(id: number, path: string): unknown;
   /** The `.sav` a save of system `id` writes: its paired-save override when it has one, else the file beside its ROM.
diff --git a/packages/core/ts/test/host.test.ts b/packages/core/ts/test/host.test.ts
index b6ba26beb..d1af39a4c 100644
--- a/packages/core/ts/test/host.test.ts
+++ b/packages/core/ts/test/host.test.ts
@@ -7,6 +7,7 @@ import type { FilesHost, Host, PngHost } from "@retroplug/core/host";
 
 const files: FilesHost = {
   readFile: () => null,
+  readPrefix: () => null,
   writeFile: () => true,
   writeFileAtomic: () => true,
   fileExists: () => false,
diff --git a/packages/lsdj/ts/test/songImport.test.ts b/packages/lsdj/ts/test/songImport.test.ts
index ffcc4bbd5..1afd50e11 100644
--- a/packages/lsdj/ts/test/songImport.test.ts
+++ b/packages/lsdj/ts/test/songImport.test.ts
@@ -42,6 +42,7 @@ class FakeHost {
     return {
       files: {
         readFile: (path) => this.files.get(path) ?? null,
+        readPrefix: (path, bytes) => this.files.get(path)?.slice(0, bytes) ?? null,
         writeFile: (path, bytes) => this.write(path, bytes),
         writeFileAtomic: (path, bytes) => this.writeFileAtomic(path, bytes),
         fileExists: (path) => this.files.has(path),
@@ -52,6 +53,7 @@ class FakeHost {
         setRoleConfig: (id, kind, config) => this.setRoleConfig(id, kind, config),
         reload: () => null,
         readSram: () => this.readSram(),
+        readRam: () => null,
         loadSram: (id, path) => this.loadSram(id, path),
         savPathOf: () => this.savPathOf(),
         nodeIdOf: (id) => `sys-${id}`,
diff --git a/packages/retroplug/src/hostGroups.ts b/packages/retroplug/src/hostGroups.ts
index 15ba9b9b8..4966cd185 100644
--- a/packages/retroplug/src/hostGroups.ts
+++ b/packages/retroplug/src/hostGroups.ts
@@ -7,10 +7,11 @@ import type { HostBackend } from "./backend";
 /** The backend's file I/O as core's files group. The methods are wrapped rather than handed over, so what a hook
  *  receives is a table it cannot reach the rest of the backend through. */
 export function filesHost(
-  backend: Pick<HostBackend, "readFile" | "writeFile" | "writeFileAtomic" | "fileExists">,
+  backend: Pick<HostBackend, "readFile" | "readFilePrefix" | "writeFile" | "writeFileAtomic" | "fileExists">,
 ): FilesHost {
   return {
     readFile: (path) => backend.readFile(path),
+    readPrefix: (path, bytes) => backend.readFilePrefix(path, bytes),
     writeFile: (path, bytes) => backend.writeFile(path, bytes),
     writeFileAtomic: (path, bytes) => backend.writeFileAtomic(path, bytes),
     fileExists: (path) => backend.fileExists(path),
@@ -27,7 +28,10 @@ export function pngHost(backend: Pick<HostBackend, "pngEncode" | "pngDecode">):
 
 /** The two groups a construct-time hook or an asset bake is handed. */
 export function filesPngHost(
-  backend: Pick<HostBackend, "readFile" | "writeFile" | "writeFileAtomic" | "fileExists" | "pngEncode" | "pngDecode">,
+  backend: Pick<
+    HostBackend,
+    "readFile" | "readFilePrefix" | "writeFile" | "writeFileAtomic" | "fileExists" | "pngEncode" | "pngDecode"
+  >,
 ): Host<"files" | "png"> {
   return { files: filesHost(backend), png: pngHost(backend) };
 }
diff --git a/packages/retroplug/test/hosts/host-groups.test.ts b/packages/retroplug/test/hosts/host-groups.test.ts
index 017fe03b4..6cc6c926f 100644
--- a/packages/retroplug/test/hosts/host-groups.test.ts
+++ b/packages/retroplug/test/hosts/host-groups.test.ts
@@ -122,6 +122,32 @@ test("a render host is its two groups", () => {
   ]);
 });
 
+test("every surface's files group reads a prefix, and the UI host reads work RAM", () => {
+  const backend = new MockBackend("/cfg");
+  backend.seed("/cfg/rom.bin", new Uint8Array([1, 2, 3, 4]));
+  const ui = createUiHost(composeAppStores({ backend }), { views: { open: () => {} }, beginImport: () => {} });
+  const session = {
+    backend,
+    audio: { compileKitFormat: async () => ({ bytes: new Uint8Array(0), errors: [] }) },
+  } as unknown as Session;
+  const cli = cliHost(session);
+  const ctx = { backend, audio: { renderAudio: () => {}, pressButton: () => {} } } as unknown as RenderContext;
+  const render = renderHost(ctx);
+  // One edit in src/hostGroups.ts (filesHost wraps the backend's readFilePrefix), so all three builders gain it.
+  for (const files of [ui.files, cli.files, render.files]) {
+    expect([...(files.readPrefix("/cfg/rom.bin", 2) ?? [])]).toEqual([1, 2]);
+    expect(files.readPrefix("/cfg/missing.bin", 2)).toBe(null);
+  }
+  // Work RAM is the UI host's alone: a render reaches it through its emulator group, and the CLI has no systems.
+  backend.setRam(7, new Uint8Array([9, 8, 7]));
+  expect([...(ui.systems.readRam(7) ?? [])]).toEqual([9, 8, 7]);
+  expect(ui.systems.readRam(8)).toBe(null);
+  // @ts-expect-error a CLI host has no systems group
+  cli.systems;
+  // @ts-expect-error the render host's systems are absent; its one emulator group carries readRam instead
+  render.systems;
+});
+
 test("a controller target is checked against the launchpad package's list", () => {
   const stores = composeAppStores({ backend: new MockBackend("/cfg") });
   const ui = createUiHost(stores, { views: { open: () => {} }, beginImport: () => {} });
diff --git a/packages/retroplug/ui/host/uiHost.ts b/packages/retroplug/ui/host/uiHost.ts
index d44f4c78c..38ccca9d8 100644
--- a/packages/retroplug/ui/host/uiHost.ts
+++ b/packages/retroplug/ui/host/uiHost.ts
@@ -25,9 +25,9 @@ import { batteryListing } from "../../src/batteryListing";
 export type { UiHost } from "@retroplug/ui/contribution";
 
 /** The shell's own full-window screens, by the id a cart names them with. The ids are the leftover shell knowledge of a
- *  cart: "lsdj-hd" is LSDj's HD player, and it is the ONE interim cart id the shell keeps until F4c moves that screen
- *  and the two overlays into the carts' own view registrations. An id this shell does not own is ignored, so a package's
- *  newer view cannot open the wrong screen here. */
+ *  cart: "lsdj-hd" is LSDj's HD player, the one cart id the shell still maps itself; the carts register their views
+ *  (the HD player and the two overlays) once those move. An id this shell does not own is ignored, so a package's newer
+ *  view cannot open the wrong screen here. */
 export function uiViews(openLsdjHd: (systemId: number) => void): ViewsHost {
   return {
     open: (id, systemId) => {
@@ -116,6 +116,7 @@ function systemsHost(backend: ControlPlaneBackend, systems: SystemsStore): Syste
     setRoleConfig: (id, kind, config) => systems.setRoleConfig(id, kind, config),
     reload: (id) => systems.reloadSystem(id),
     readSram: (id) => systems.readSram(id),
+    readRam: (id) => systems.readRam(id),
     loadSram: (id, path) => systems.loadSram(id, path),
     savPathOf: (id) => {
       const sys = view().find((s) => s.id === id);
````

Run: `sed -n '3357,3503p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/core/ts/host.ts
packages/core/ts/test/host.test.ts
packages/lsdj/ts/test/songImport.test.ts
packages/retroplug/src/hostGroups.ts
packages/retroplug/test/hosts/host-groups.test.ts
packages/retroplug/ui/host/uiHost.ts
EOF
````

Expected:

````text
271f8eae97af2dc313d90ae2b92520b3b5a82a17
d1af39a4c067af71f277c35f462e2e2a057f7c12
1afd50e11402f3b5b787ee044b93bc565f17866c
4966cd185e2f055f7616c8d24ae24f06c4386899
6cc6c926ffa53178b8e63327b799e125dfedc2c3
38ccca9d8b9db4493523d573e97bd25e1325a98b
````

- [ ] **Step 4: The generated registry: `VIEWS`, and the three data refusals**

`renderMenus` gains `VIEWS` (`{ pkg, view }[]`, in product order through `UI_CONTRIBUTIONS.flatMap`) and the load-time
checks, each naming the package or both: a view id declared twice; a `placement` that is neither `window` nor `tile`;
a body `kind` that is none of the three; a `visible` on a window view (the type refuses it, and the data check backs
the type as `PLACEMENTS` does); and a view with no body at all, which the first cut met as a `TypeError` from reading
a field off nothing. The fixture product list gains a third package, `zeta`, named BEFORE `alpha` and `beta` and
contributing no app of its own, so the ui, views and cli order cases report the list's order and an alphabetical sort
fails them.

Create `packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json`:

````json
{
  "name": "@retroplug/zeta",
  "exports": {
    "./ui": "./ts/ui.ts"
  },
  "private": true,
  "retroplug": { "ui": "ts/ui.ts" }
}
````

Run: `mkdir -p packages/retroplug/test/packages/fixtures/tree/packages/zeta; sed -n '3546,3553p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json`
Expected: `0eb009db81cd01cb33637ec02d3b68d580048ea2`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts`:

````ts
// A fixture package's UI contribution, with no app and no node of its own: the product list names it FIRST, so the
// generated menus order (zeta, alpha, beta) is the list's rather than an alphabetical sort of the same names.
export default {};
````

Run: `mkdir -p packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts; sed -n '3565,3567p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts`
Expected: `ae0a05213620d649481594ca5bf5d6241c8330ed`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/lib/packages.mjs b/packages/retroplug/scripts/lib/packages.mjs
index b89926a12..62dbb9268 100644
--- a/packages/retroplug/scripts/lib/packages.mjs
+++ b/packages/retroplug/scripts/lib/packages.mjs
@@ -765,7 +765,7 @@ function renderMenus({ packages, outDir }) {
     "// system-settings rows, menu builders, tracker specs and services it adds. The shell composes the menus from this",
     "// list, so a package's place in `retroplug.products.desktop` is where its rows and entries land. Only ui/ imports",
     "// this module: the control plane, the render worker, the SDK and the planner carry no menu contribution.",
-    'import { PLACEMENTS, type RoleRow, type UiContribution } from "@retroplug/ui/contribution";',
+    'import { PLACEMENTS, type RoleRow, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";',
   ];
   for (const p of contributors) {
     lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.ui)}";`);
@@ -782,6 +782,12 @@ function renderMenus({ packages, outDir }) {
   }
   lines.push("];");
   lines.push("");
+  lines.push("/** The product's views, in product order, with the package each came from: what host.views.open looks");
+  lines.push(" *  a window view up by id, and the tile layers every tile mounts in turn. */");
+  lines.push("export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS.flatMap(({ pkg, ui }) =>");
+  lines.push("  (ui.views ?? []).map((view) => ({ pkg, view })),");
+  lines.push(");");
+  lines.push("");
   lines.push("// A row's id has to be unique inside its placement: the menu is keyed by id, so a second row of one id would");
   lines.push("// take the first one's place (a system-settings row lands in the System submenu, a tracker's extras in its own");
   lines.push("// submenu).");
@@ -808,6 +814,11 @@ function renderMenus({ packages, outDir }) {
   lines.push("// duplicate is refused rather than let the second definition win.");
   lines.push("const serviceOwners = new Map<string, string>();");
   lines.push("const placements = new Set<string>(PLACEMENTS);");
+  lines.push("// A view's id is its test id and the id host.views.open names, so it is unique across the product; its");
+  lines.push("// placement and its body's kind are the two data fields the type cannot hold a package to at load.");
+  lines.push("const viewOwners = new Map<string, string>();");
+  lines.push('const viewPlacements = new Set<string>(["window", "tile"]);');
+  lines.push('const bodyKinds = new Set<string>(["pixels", "readout", "component"]);');
   lines.push("");
   lines.push("/** One contributed row: its role must be registered, its id unique in its placement, and its key (and its");
   lines.push(" *  gate's key) one its role's schema reads. */");
@@ -832,8 +843,34 @@ function renderMenus({ packages, outDir }) {
   lines.push("  }");
   lines.push("}");
   lines.push("");
+  lines.push("/** One contributed view: its id unique across the product, its placement one the shell has, its body kind");
+  lines.push(" *  one the shell renders, and no `visible` on a window view (the type refuses that; this backs the type). */");
+  lines.push("function checkView(pkg: string, view: ViewDef): void {");
+  lines.push("  const owner = viewOwners.get(view.id);");
+  lines.push("  if (owner !== undefined) {");
+  lines.push("    throw new Error(owner === pkg");
+  lines.push("      ? `the package ${pkg} declares the view ${view.id} twice`");
+  lines.push("      : `two packages declare the view ${view.id}: ${owner} and ${pkg}`);");
+  lines.push("  }");
+  lines.push("  viewOwners.set(view.id, pkg);");
+  lines.push("  if (!viewPlacements.has(view.placement)) {");
+  lines.push("    throw new Error(`the package ${pkg} declares the view ${view.id} at the placement ${view.placement}, ` +");
+  lines.push('      `which is no placement (the placements are window, tile)`);');
+  lines.push("  }");
+  lines.push("  const kind = view.body?.kind;");
+  lines.push("  if (!bodyKinds.has(kind)) {");
+  lines.push("    throw new Error(`the package ${pkg} declares the view ${view.id} with the body kind ${kind}, ` +");
+  lines.push('      `which is no body kind (the kinds are pixels, readout, component)`);');
+  lines.push("  }");
+  lines.push("  if (view.placement !== \"tile\" && (view as { readonly visible?: unknown }).visible !== undefined) {");
+  lines.push("    throw new Error(`the package ${pkg} declares the window view ${view.id} with a visible flag, which only ` +");
+  lines.push('      `a tile view has`);');
+  lines.push("  }");
+  lines.push("}");
+  lines.push("");
   lines.push("for (const { pkg, ui } of UI_CONTRIBUTIONS) {");
   lines.push("  for (const row of ui.rows ?? []) checkRow(\"system.settings\", pkg, row);");
+  lines.push("  for (const view of ui.views ?? []) checkView(pkg, view);");
   lines.push("  for (const tracker of ui.trackers ?? []) {");
   lines.push("    if (!trackerIds.has(tracker.id)) {");
   lines.push(
diff --git a/packages/retroplug/test/packages/fixtures/expected/menus.ts b/packages/retroplug/test/packages/fixtures/expected/menus.ts
index 225efd04a..6a6984704 100644
--- a/packages/retroplug/test/packages/fixtures/expected/menus.ts
+++ b/packages/retroplug/test/packages/fixtures/expected/menus.ts
@@ -5,15 +5,23 @@
 // system-settings rows, menu builders, tracker specs and services it adds. The shell composes the menus from this
 // list, so a package's place in `retroplug.products.desktop` is where its rows and entries land. Only ui/ imports
 // this module: the control plane, the render worker, the SDK and the planner carry no menu contribution.
-import { PLACEMENTS, type RoleRow, type UiContribution } from "@retroplug/ui/contribution";
+import { PLACEMENTS, type RoleRow, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";
+import ui_zeta from "@retroplug/zeta/ui";
 import ui_alpha from "@retroplug/alpha/ui";
 import { ROLES, TRACKER_INTEGRATIONS } from "../../src/generated/packages";
 
 /** Each package's UI contribution, in product order. */
 export const UI_CONTRIBUTIONS: readonly { pkg: string; ui: UiContribution }[] = [
+  { pkg: "zeta", ui: ui_zeta },
   { pkg: "alpha", ui: ui_alpha },
 ];
 
+/** The product's views, in product order, with the package each came from: what host.views.open looks
+ *  a window view up by id, and the tile layers every tile mounts in turn. */
+export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS.flatMap(({ pkg, ui }) =>
+  (ui.views ?? []).map((view) => ({ pkg, view })),
+);
+
 // A row's id has to be unique inside its placement: the menu is keyed by id, so a second row of one id would
 // take the first one's place (a system-settings row lands in the System submenu, a tracker's extras in its own
 // submenu).
@@ -38,6 +46,11 @@ const trackerOwners = new Map<string, string>();
 // duplicate is refused rather than let the second definition win.
 const serviceOwners = new Map<string, string>();
 const placements = new Set<string>(PLACEMENTS);
+// A view's id is its test id and the id host.views.open names, so it is unique across the product; its
+// placement and its body's kind are the two data fields the type cannot hold a package to at load.
+const viewOwners = new Map<string, string>();
+const viewPlacements = new Set<string>(["window", "tile"]);
+const bodyKinds = new Set<string>(["pixels", "readout", "component"]);
 
 /** One contributed row: its role must be registered, its id unique in its placement, and its key (and its
  *  gate's key) one its role's schema reads. */
@@ -58,8 +71,34 @@ function checkRow(placement: string, pkg: string, row: RoleRow): void {
   }
 }
 
+/** One contributed view: its id unique across the product, its placement one the shell has, its body kind
+ *  one the shell renders, and no `visible` on a window view (the type refuses that; this backs the type). */
+function checkView(pkg: string, view: ViewDef): void {
+  const owner = viewOwners.get(view.id);
+  if (owner !== undefined) {
+    throw new Error(owner === pkg
+      ? `the package ${pkg} declares the view ${view.id} twice`
+      : `two packages declare the view ${view.id}: ${owner} and ${pkg}`);
+  }
+  viewOwners.set(view.id, pkg);
+  if (!viewPlacements.has(view.placement)) {
+    throw new Error(`the package ${pkg} declares the view ${view.id} at the placement ${view.placement}, ` +
+      `which is no placement (the placements are window, tile)`);
+  }
+  const kind = view.body?.kind;
+  if (!bodyKinds.has(kind)) {
+    throw new Error(`the package ${pkg} declares the view ${view.id} with the body kind ${kind}, ` +
+      `which is no body kind (the kinds are pixels, readout, component)`);
+  }
+  if (view.placement !== "tile" && (view as { readonly visible?: unknown }).visible !== undefined) {
+    throw new Error(`the package ${pkg} declares the window view ${view.id} with a visible flag, which only ` +
+      `a tile view has`);
+  }
+}
+
 for (const { pkg, ui } of UI_CONTRIBUTIONS) {
   for (const row of ui.rows ?? []) checkRow("system.settings", pkg, row);
+  for (const view of ui.views ?? []) checkView(pkg, view);
   for (const tracker of ui.trackers ?? []) {
     if (!trackerIds.has(tracker.id)) {
       throw new Error(`the package ${pkg} declares a tracker UI for ${tracker.id}, which no registered ` +
diff --git a/packages/retroplug/test/packages/fixtures/tree/package.json b/packages/retroplug/test/packages/fixtures/tree/package.json
index 138a5182e..fc270916d 100644
--- a/packages/retroplug/test/packages/fixtures/tree/package.json
+++ b/packages/retroplug/test/packages/fixtures/tree/package.json
@@ -2,6 +2,6 @@
   "name": "@retroplug/fixture-root",
   "private": true,
   "retroplug": {
-    "products": { "desktop": ["alpha", "beta"] }
+    "products": { "desktop": ["zeta", "alpha", "beta"] }
   }
 }
diff --git a/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json b/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json
index 9403b96bc..b64b5fc7e 100644
--- a/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json
+++ b/packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json
@@ -3,7 +3,8 @@
   "private": true,
   "dependencies": {
     "@retroplug/alpha": "workspace:*",
-    "@retroplug/beta": "workspace:*"
+    "@retroplug/beta": "workspace:*",
+    "@retroplug/zeta": "workspace:*"
   },
   "retroplug": {
     "app": "src/app.ts",
diff --git a/packages/retroplug/test/packages/packages-generator.test.ts b/packages/retroplug/test/packages/packages-generator.test.ts
index 424b921c5..91122d850 100644
--- a/packages/retroplug/test/packages/packages-generator.test.ts
+++ b/packages/retroplug/test/packages/packages-generator.test.ts
@@ -105,9 +105,9 @@ async function matchesFixture(mode: string, name: string): Promise<boolean> {
 
 test("the fixture product list and manifests generate the expected packages.ts", async () => {
   const report = await drive("generate");
-  // The list is the order (alpha, then beta, which depends on it), and the shell closes the app: no package may depend
-  // on it.
-  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
+  // The list is the order: zeta is listed first and depends on nothing, alpha next, then beta (which depends on it);
+  // the shell closes the app, so no package may depend on it.
+  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
   expect(await matchesFixture("generate", "packages.ts")).toBe(true);
 });
 
@@ -128,8 +128,9 @@ test("the fixture product list and manifests generate the expected menus.ts", as
 });
 
 test("the generated menus module carries the packages' UI contributions in product order", async () => {
+  // zeta is listed before alpha and beta, so this is the product list's order and not an alphabetical sort.
   const { packages } = await drive("ui");
-  expect(packages).toEqual(["alpha", "beta"]);
+  expect(packages).toEqual(["zeta", "alpha", "beta"]);
 });
 
 test("the generated menus module refuses a row id used twice in a placement", async () => {
@@ -191,6 +192,62 @@ test("the generated menus module refuses a UI service id declared twice", async
   expect(error).toBe("the package alpha declares the UI service alpha-watch twice");
 });
 
+test("the generated menus module carries the packages' views in product order", async () => {
+  const { views, error } = await drive("views");
+  if (error) console.log(`# views: ${error}`);
+  expect(views).toEqual([
+    { pkg: "zeta", id: "zeta-view", placement: "tile", kind: "readout" },
+    { pkg: "alpha", id: "alpha-view", placement: "tile", kind: "readout" },
+    { pkg: "beta", id: "beta-view", placement: "window", kind: "pixels" },
+  ]);
+});
+
+test("the generated menus module refuses a view id one package declares twice", async () => {
+  const { error } = await drive("views-duplicate-id");
+  console.log(`# duplicate view id: ${error}`);
+  expect(error).toBe("the package alpha declares the view same-view twice");
+});
+
+test("the generated menus module refuses a view id two packages declare", async () => {
+  const { error } = await drive("views-duplicate-across");
+  console.log(`# view id across packages: ${error}`);
+  expect(error).toBe("two packages declare the view same-view: alpha and beta");
+});
+
+test("the generated menus module refuses a view at a placement no placement has", async () => {
+  const { error } = await drive("views-bad-placement");
+  console.log(`# unknown view placement: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the view alpha-view at the placement float, which is no placement (the placements " +
+      "are window, tile)",
+  );
+});
+
+test("the generated menus module refuses a view body kind none of the three", async () => {
+  const { error } = await drive("views-bad-kind");
+  console.log(`# unknown body kind: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the view alpha-view with the body kind hologram, which is no body kind (the kinds " +
+      "are pixels, readout, component)",
+  );
+});
+
+test("the generated menus module refuses a view with no body, naming the package", async () => {
+  const { error } = await drive("views-no-body");
+  console.log(`# missing body: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the view alpha-view with the body kind undefined, which is no body kind (the kinds " +
+      "are pixels, readout, component)",
+  );
+});
+
+test("the generated menus module refuses a visible flag on a window view", async () => {
+  const { error } = await drive("views-window-visible");
+  console.log(`# window view visible: ${error}`);
+  expect(error).toBe("the package alpha declares the window view alpha-view with a visible flag, which only a tile " +
+    "view has");
+});
+
 test("a retroplug.ui member naming a module that is not there is refused", async () => {
   const { error } = await drive("ui-missing-module");
   console.log(`# missing ui module: ${error}`);
@@ -198,8 +255,10 @@ test("a retroplug.ui member naming a module that is not there is refused", async
 });
 
 test("the generated cli module carries the packages' tools in product order", async () => {
+  // zeta is listed before alpha and beta, so this is the product list's order and not an alphabetical sort.
   const { tools } = await drive("cli");
   expect(tools).toEqual([
+    { pkg: "zeta", tools: ["zeta-tool"] },
     { pkg: "alpha", tools: ["alpha-tool"] },
     { pkg: "beta", tools: ["beta-tool"] },
   ]);
@@ -346,7 +405,7 @@ test("an unlisted dependency that would contribute to the app is refused", async
 
 test("an unlisted dependency that contributes nothing is left out of the ordering", async () => {
   const report = await drive("unlisted-plain");
-  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
+  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
   expect(await matchesFixture("unlisted-plain", "packages.ts")).toBe(true);
 });
 
@@ -821,7 +880,7 @@ test("a package contributing an app and no nodes names no module and still regis
   // The shape an app bridge has: a protocol member with apps alone, and no nodes.ts to satisfy a check.
   const report = await drive("apps-only");
   const files = report.files as Record<string, string>;
-  expect(report.packages).toEqual(["alpha", "beta", "retroplug"]);
+  expect(report.packages).toEqual(["zeta", "alpha", "beta", "retroplug"]);
   expect(files["scriptNodes.ts"].includes("import app_alpha_alpha_app from")).toBe(true);
   expect(files["scriptNodes.ts"].includes("registerControllerApps(apps);")).toBe(true);
   expect(files["scriptNodes.ts"].includes("nodes_alpha")).toBe(false); // nothing of alpha's to register
diff --git a/packages/retroplug/test/packages/run-generator.mjs b/packages/retroplug/test/packages/run-generator.mjs
index aa768bce5..35fd54cbe 100644
--- a/packages/retroplug/test/packages/run-generator.mjs
+++ b/packages/retroplug/test/packages/run-generator.mjs
@@ -30,7 +30,8 @@
 //   realcart-node-twice | ui-missing-module
 //                   { error: <the generator's message, or null when it did not refuse> }
 //   ui | ui-duplicate-row | ui-unknown-placement | ui-role-undeclared | ui-unknown-key | ui-unknown-when-key |
-//   ui-unknown-tracker | ui-duplicate-tracker | ui-duplicate-service
+//   ui-unknown-tracker | ui-duplicate-tracker | ui-duplicate-service | views | views-duplicate-id |
+//   views-duplicate-across | views-bad-placement | views-bad-kind | views-no-body | views-window-visible
 //   cli | cli-duplicate-tool | cli-missing-module | examples-outside | examples-tsconfig-missing |
 //   examples-tsconfig-unextended | examples-tsconfig-extended | examples-duplicate-name | examples-shell-name |
 //   examples-unsafe-name | host-unknown | host-known | react-tsconfig-missing | react-tsconfig-unextended |
@@ -186,6 +187,7 @@ async function loadMenus() {
       "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
       "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
       "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
+      "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui.ts"),
       // The ui package itself is a workspace dependency of the shell, not a fixture package: a real build resolves it
       // through the pnpm link, so the driver hands the bundle the tree's own copy.
       "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
@@ -195,6 +197,28 @@ async function loadMenus() {
   return loaded.UI_CONTRIBUTIONS.map((entry) => entry.pkg);
 }
 
+/** The generated menus module's VIEWS, bundled and loaded the way the shell loads it: product order, and each view's
+ *  id, placement and body kind as the registry reports them. */
+async function loadViews() {
+  const out = join(work, "views.mjs");
+  const { buildSync } = require("esbuild");
+  buildSync({
+    ...buildOptions(join(root, "packages/retroplug/ui/generated/menus.ts"), out, "esm"),
+    alias: {
+      "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
+      "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
+      "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
+      "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
+      "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
+      "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
+      "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui.ts"),
+      "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
+    },
+  });
+  const loaded = await import(pathToFileURL(out).href);
+  return loaded.VIEWS.map(({ pkg, view }) => ({ pkg, id: view.id, placement: view.placement, kind: view.body.kind }));
+}
+
 /** The generated CLI tools module, bundled and loaded as the dispatcher loads it: loading it runs its own refusal of a
  *  name two packages declare. */
 async function loadCli() {
@@ -206,6 +230,7 @@ async function loadCli() {
       "@retroplug/core/cliTool": join(HERE, "../../../core/ts/cliTool.ts"),
       "@retroplug/alpha/cli": join(root, "packages/alpha/ts/cli.ts"),
       "@retroplug/beta/cli": join(root, "packages/beta/ts/cli.ts"),
+      "@retroplug/zeta/cli": join(root, "packages/zeta/ts/cli.ts"),
     },
   });
   const loaded = await import(pathToFileURL(out).href);
@@ -332,8 +357,9 @@ try {
       break;
     case "ui":
       // The positive side: alpha's app contributes the role its ui module's row names, and beta contributes a builder,
-      // so the menus module loads and its contributions are reported in product order (two contributors, so the order
-      // is what is reported rather than a list of one).
+      // so the menus module loads and its contributions are reported in product order. zeta contributes without an app
+      // of its own and is named FIRST in the product list, so the reported order (zeta, alpha, beta) is the list's and
+      // not a sort of the three names.
       writeTree("packages/alpha/ts/app.ts", uiApp("alpha"));
       writeTree("packages/beta/ts/ui.ts", 'export default { menus: [{ placement: "start", build: () => [] }] };\n');
       editManifest("beta", (m) => {
@@ -450,8 +476,158 @@ try {
         result = { error: e.message };
       }
       break;
+    case "views": {
+      // Three contributors, so the reported order is the product order (zeta is listed first) rather than an
+      // alphabetical sort. A view is data: its id, its placement and a body of one kind, which the generated module
+      // names in VIEWS.
+      writeTree(
+        "packages/zeta/ts/ui.ts",
+        'export default { views: [{ id: "zeta-view", label: "Zeta View", placement: "tile", ' +
+          'body: { kind: "readout", open: () => null } }] };\n',
+      );
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
+          'body: { kind: "readout", open: () => null } }] };\n',
+      );
+      writeTree(
+        "packages/beta/ts/ui.ts",
+        'export default { views: [{ id: "beta-view", label: "Beta View", placement: "window", ' +
+          'body: { kind: "pixels", open: () => null } }] };\n',
+      );
+      for (const name of ["alpha", "beta"]) {
+        editManifest(name, (m) => {
+          m.retroplug.ui = "ts/ui.ts";
+          m.exports["./ui"] = "./ts/ui.ts";
+        });
+      }
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    }
+    case "views-no-body":
+      // A contribution whose view has no body at all: the data check backs the type with a message that names the
+      // package, rather than a bare TypeError from reading `kind` off nothing.
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile" }] };\n',
+      );
+      editManifest("alpha", (m) => {
+        m.retroplug.ui = "ts/ui.ts";
+        m.exports["./ui"] = "./ts/ui.ts";
+      });
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    case "views-duplicate-id":
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { views: [\n' +
+          '  { id: "same-view", label: "First", placement: "tile", body: { kind: "readout", open: () => null } },\n' +
+          '  { id: "same-view", label: "Second", placement: "window", body: { kind: "pixels", open: () => null } },\n' +
+          '] };\n',
+      );
+      editManifest("alpha", (m) => {
+        m.retroplug.ui = "ts/ui.ts";
+        m.exports["./ui"] = "./ts/ui.ts";
+      });
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    case "views-duplicate-across":
+      for (const name of ["alpha", "beta"]) {
+        writeTree(
+          `packages/${name}/ts/ui.ts`,
+          `export default { views: [{ id: "same-view", label: "${name}", placement: "tile", ` +
+            'body: { kind: "readout", open: () => null } }] };\n',
+        );
+        editManifest(name, (m) => {
+          m.retroplug.ui = "ts/ui.ts";
+          m.exports["./ui"] = "./ts/ui.ts";
+        });
+      }
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    case "views-bad-placement":
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "float", ' +
+          'body: { kind: "readout", open: () => null } }] };\n',
+      );
+      editManifest("alpha", (m) => {
+        m.retroplug.ui = "ts/ui.ts";
+        m.exports["./ui"] = "./ts/ui.ts";
+      });
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    case "views-bad-kind":
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "tile", ' +
+          'body: { kind: "hologram", open: () => null } }] };\n',
+      );
+      editManifest("alpha", (m) => {
+        m.retroplug.ui = "ts/ui.ts";
+        m.exports["./ui"] = "./ts/ui.ts";
+      });
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    case "views-window-visible":
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'const visible = { get: () => true, subscribe: () => () => {} };\n' +
+          'export default { views: [{ id: "alpha-view", label: "Alpha View", placement: "window", visible, ' +
+          'body: { kind: "readout", open: () => null } }] };\n',
+      );
+      editManifest("alpha", (m) => {
+        m.retroplug.ui = "ts/ui.ts";
+        m.exports["./ui"] = "./ts/ui.ts";
+      });
+      generate({ root });
+      try {
+        result = { views: await loadViews() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
     case "cli": {
-      // The positive side: beta contributes one tool and alpha another, so the reported order is the product order.
+      // The positive side: beta contributes one tool and alpha another, and zeta a third; zeta is listed first, so the
+      // reported order is the product order rather than an alphabetical sort of the three names.
+      writeTree(
+        "packages/zeta/ts/cli.ts",
+        'export default [{ name: "zeta-tool", summary: "", help: "", run: () => {} }];\n',
+      );
+      editManifest("zeta", (m) => {
+        m.retroplug.cli = "ts/cli.ts";
+        m.exports["./cli"] = "./ts/cli.ts";
+      });
       writeTree("packages/alpha/ts/cli.ts", 'export default [{ name: "alpha-tool", summary: "", help: "", run: () => {} }];\n');
       writeTree("packages/beta/ts/cli.ts", 'export default [{ name: "beta-tool", summary: "", help: "", run: () => {} }];\n');
       for (const name of ["alpha", "beta"]) {
@@ -954,7 +1130,8 @@ try {
       renameSync(join(root, "packages/beta"), join(root, "packages/a"));
       editList("a-b", "a");
       editManifest("retroplug", (json) => {
-        json.dependencies = { ...json.dependencies, "@retroplug/a-b": "workspace:*", "@retroplug/a": "workspace:*" };
+        // The renamed pair alone: zeta leaves the product list here, so the shell must not depend on it any more.
+        json.dependencies = { "@retroplug/a-b": "workspace:*", "@retroplug/a": "workspace:*" };
       });
       editManifest("a-b", (json) => {
         json.name = "@retroplug/a-b";
````

Run: `sed -n '3577,4112p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/lib/packages.mjs
packages/retroplug/test/packages/fixtures/expected/menus.ts
packages/retroplug/test/packages/fixtures/tree/package.json
packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json
packages/retroplug/test/packages/packages-generator.test.ts
packages/retroplug/test/packages/run-generator.mjs
EOF
````

Expected:

````text
62dbb9268950f78b1ebe46a068fc62f32f224d99
6a69847042d3c20ff1d8364b45dcf6b6f3faf197
fc270916dd668ed08c8adda6783f162cafac4382
b64b5fc7e2d56657d6fdb30b7a573eb7e9e5b7bc
91122d8506764cffb59845f3f1de3bdf78765cdd
35fd54cbe2e3f2656fb43172457dede8ecd83b39
````

- [ ] **Step 5: The shell's view files: one controller, one effect, three renderers, two slots**

`viewSession.ts` is plain TypeScript and React-free, so `pnpm test` can hold it: `ReopenKey` / `reopenKey` /
`sameReopenKey` (the view's id, the system, its ROM path and its `roles` BY IDENTITY), `viewAppliesTo` (a throwing
`when` reads as "no"), `viewShown` / `subscribeViewShown` (a throwing `visible` reads as hidden and subscribes to
nothing), `openSession` / `closeSession` (a throw is reported as `[views] <id>: ...` and contained), `shownSession`
(hidden or gone is `null`, else the held session, `undefined` included), `isPixelSession` / `isReadoutSampler`,
`readoutKey` (a throwing `signature` is reported once and the default over the lines and the flag taken, since a
signature can only coarsen it), `ViewSessionController` (`sync` opens once per key, closes on a key change, a hide or
a null system; `read` frames or samples the session the CONTROLLER holds now and answers `undefined` once it holds
none, so a native frame landing between a hide and a re-render cannot touch a session the shell has let go; Task 3
makes that answer `null`, so a dropped readout clears) and a `WeakMap` so each fault is reported once per view object
and per thing that threw. `useViewSession.ts` is the one effect and subscription the renderers share:
`useSyncExternalStore` on a tile view's `visible`, then `sync` keyed on view, host, system and shown, and `close` on
unmount. `PixelView` reads a frame only once it has a canvas (the guard), passes the array's own buffer when whole and
a copy when it is a view onto a larger one, and shows `unsupported` for a WINDOW view only. `ReadoutView` samples
through `useFrameSampled` with the sampler's signature or the default, and tags the kit's `DebugReadout` with the
view's id. `ComponentView` mounts the body's component, the one cast in the shell. `ViewWindow` is a window view in
place of the grid, tagged with its id. `TileViews` mounts ONE `StableSlot` per REGISTERED tile view, always, in
registry order, each tagged `view-slot-<id>`, after the dim and before the border, with `when` inside the slot and
each slot subscribing to its own `visible` for every body kind.

Create `packages/retroplug/ui/views/viewSession.ts`:

````ts
// View sessions (docs/modular-graph-phase-f4c-design.md section 5.4): the shell opens one per (view, system, ROM,
// roles), holds it for as long as that key holds, and closes it when the key changes, the view hides or the component
// unmounts.
//
// Plain TypeScript on purpose, so the lifecycle is tested without React: the renderers call this from one effect
// (`useViewSession`), and a package owns no session state of its own. The key is `(view.id, systemId, romPath, roles)`
// by identity: a role edit replaces the roles array (SystemsStore.setRoleConfig slices it on every write), so any role
// write reopens the system's views.
//
// Every call into a package's code goes through a guard here: a `when`, a `visible.get()`, a session's per-frame
// reader, its `signature` and its `close` all run inside the shell's render or its frame loop, and a throw in one of
// them must cost that view only. A guard reports and then answers what "no" answers, so the rest of the UI carries on.
import type { SystemInfo } from "@retroplug/core/host";
import type { PixelSession, ReadoutFrame, ReadoutSampler, UiHost, ViewDef } from "@retroplug/ui/contribution";

/** What a session can do on close; both contracts make it optional. */
export interface ViewSession {
  close?(): void;
}

/** What opening a body produces: its kind selects which contract's session it is. */
export type OpenedSession = PixelSession | ReadoutSampler;

/** The two readers are disjoint, so a session's kind reads off the object itself: a renderer only ever opens a view of
 *  its own kind, and these make that a check rather than a claim about what the registry holds. */
export function isPixelSession(session: OpenedSession): session is PixelSession {
  return "frame" in session;
}

export function isReadoutSampler(session: OpenedSession): session is ReadoutSampler {
  return "sample" in session;
}

/** What a session is reused over. `roles` is compared by identity: a role write replaces the array even when the
 *  values are equal, so the comparison is what makes a role edit reopen the view. */
export interface ReopenKey {
  readonly id: string;
  readonly systemId: number;
  readonly romPath: string;
  readonly roles: SystemInfo["roles"];
}

export function reopenKey(view: ViewDef, system: SystemInfo): ReopenKey {
  return { id: view.id, systemId: system.id, romPath: system.romPath, roles: system.roles };
}

export function sameReopenKey(a: ReopenKey, b: ReopenKey): boolean {
  return a.id === b.id && a.systemId === b.systemId && a.romPath === b.romPath && a.roles === b.roles;
}

function message(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** The reported faults, per owner object and per thing that threw: a `when` read during a render or a `visible.get`
 *  on every repaint would otherwise report once per frame, and one bad predicate would bury every other message. */
const reported = new WeakMap<object, Set<string>>();

function reportOnce(owner: object, tag: string, text: string): void {
  let tags = reported.get(owner);
  if (tags === undefined) {
    tags = new Set();
    reported.set(owner, tags);
  }
  if (tags.has(tag)) return;
  tags.add(tag);
  console.warn(text);
}

/** Whether a view's `when` accepts a system: absent = every system, and a throw reads as "no" and is reported (the
 *  predicate runs inside a render, App's open and every tile slot). */
export function viewAppliesTo(view: ViewDef, system: SystemInfo): boolean {
  if (!view.when) return true;
  try {
    return view.when(system);
  } catch (error) {
    reportOnce(view, "when", `[views] ${view.id}: its when threw: ${message(error)}`);
    return false;
  }
}

/** A tile view's visibility as a render reads it: a window view has none (always shown), and a throw in a tile view's
 *  `get` reads as hidden and is reported. */
export function viewShown(view: ViewDef): boolean {
  if (view.placement !== "tile" || view.visible === undefined) return true;
  try {
    return view.visible.get();
  } catch (error) {
    reportOnce(view, "visible", `[views] ${view.id}: its visible threw: ${message(error)}`);
    return false;
  }
}

/** Subscribe to a tile view's visibility, a throwing subscribe reported and treated as "nothing to watch": a flag the
 *  shell cannot watch leaves the view at its last reading rather than taking the renderer down. */
export function subscribeViewShown(view: ViewDef, onChange: () => void): () => void {
  if (view.placement !== "tile" || view.visible === undefined) return () => {};
  try {
    return view.visible.subscribe(onChange);
  } catch (error) {
    reportOnce(view, "visible", `[views] ${view.id}: its visible threw: ${message(error)}`);
    return () => {};
  }
}

/** Open a view body's session, reporting a throw and answering null for it: the system is left with no session rather
 *  than with a half-opened one, and the shell renders what an unreadable cart renders. A component view opens nothing
 *  (it is mounted, not sampled), and the body's kind selects which contract's `open` runs. */
export function openSession(view: ViewDef, host: UiHost, systemId: number): OpenedSession | null {
  try {
    const body = view.body;
    if (body.kind === "pixels") return body.open(host, systemId);
    if (body.kind === "readout") return body.open(host, systemId);
    return null;
  } catch (error) {
    console.warn(`[views] ${view.id}: opening the session failed: ${message(error)}`);
    return null;
  }
}

/** Close a session, reporting a throw: one view's teardown must not stop the shell's. */
export function closeSession(session: ViewSession | null): void {
  try {
    session?.close?.();
  } catch (error) {
    console.warn(`[views] closing a session failed: ${message(error)}`);
  }
}

/** The default readout dedupe: exactly the fields a readout frame shows. */
export function readoutSignature(frame: ReadoutFrame): string {
  return `${frame.playing ? 1 : 0}|${frame.lines.join("\n")}`;
}

/** One frame's dedupe key: the sampler's own signature, else the default over the lines and the flag. A signature that
 *  throws is reported once and the default taken, so a bad dedupe costs a repaint rather than the frame bus. */
export function readoutKey(view: ViewDef, sampler: ReadoutSampler, frame: ReadoutFrame): string {
  const own = sampler.signature;
  if (!own) return readoutSignature(frame);
  try {
    return own(frame);
  } catch (error) {
    reportOnce(sampler, "signature", `[views] ${view.id}: its signature threw: ${message(error)}`);
    return readoutSignature(frame);
  }
}

/** What a renderer draws, from the controller's state: null while the view is hidden or its system is gone (so a hide
 *  drops the session in the render that sees it, not one effect later), `undefined` before the first open produced
 *  anything (nothing to draw yet, which is NOT "unsupported"), and otherwise the held session, null included (an open
 *  that was refused). */
export function shownSession(
  session: OpenedSession | null | undefined,
  shown: boolean,
  system: SystemInfo | null,
): OpenedSession | null | undefined {
  return shown && system !== null ? session : null;
}

/** Holds one session per reopen key. `sync` is the whole lifecycle (open once, close on a change, a hide or a null
 *  system), `read` is the per-frame access, and `close` is the unmount. */
export class ViewSessionController {
  private held: { key: ReopenKey; session: OpenedSession | null } | null = null;

  /** The session for `system`, opening one when the key moved and closing the previous session either way. A system
   *  that is gone, or a view that is hidden, closes what was held and starts nothing: a hidden tile view does no work.
   *  `shown` is the renderer's reading of the view's visibility (`viewShown`), so a throwing flag costs nothing. */
  sync(view: ViewDef, host: UiHost, system: SystemInfo | null, shown: boolean): OpenedSession | null {
    if (system === null || !shown) {
      this.close();
      return null;
    }
    const key = reopenKey(view, system);
    if (this.held !== null && sameReopenKey(this.held.key, key)) return this.held.session;
    this.close();
    const session = openSession(view, host, system.id);
    this.held = { key, session };
    return session;
  }

  /** Read the session held NOW, for a per-frame handler that must not use a render's closure: a session a hide or a
   *  reopen closed since that render answers undefined and is not called. A reader that throws is reported once and
   *  drops the session, the key staying held so the shell retries at the next key change (as it does for an open that
   *  threw). */
  read<R>(reader: (session: OpenedSession) => R): R | undefined {
    const held = this.held;
    if (held === null || held.session === null) return undefined;
    try {
      return reader(held.session);
    } catch (error) {
      console.warn(`[views] reading a session failed: ${message(error)}`);
      closeSession(held.session);
      held.session = null;
      return undefined;
    }
  }

  close(): void {
    if (this.held === null) return;
    closeSession(this.held.session);
    this.held = null;
  }
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4167,4369p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/viewSession.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/viewSession.ts`
Expected: `6b1f9b1b51bf2d5092c54fcf53859b27fdb1ea49`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/useViewSession.ts`:

````ts
// One effect behind the three renderers (docs/modular-graph-phase-f4c-design.md sections 5.3 and 5.4): the session is
// opened once per (view, system, ROM, roles), reopened when that key or the view's visibility moves, and closed on
// unmount. The lifecycle itself is plain TypeScript (viewSession.ts), so it is tested without React; this file is only
// the effect that calls it, and the handle a renderer draws and reads through.
import { useCallback, useEffect, useRef, useState } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, ViewDef } from "@retroplug/ui/contribution";
import { shownSession, ViewSessionController, type OpenedSession } from "./viewSession";

/** What a renderer gets: the session to draw (`shownSession` has its three states) and the per-frame reader. */
export interface ViewSessionHandle {
  readonly session: OpenedSession | null | undefined;
  read<R>(reader: (session: OpenedSession) => R): R | undefined;
}

/** `shown` is the renderer's own reading of the view's visibility (`viewShown`, subscribed by the caller): a window
 *  view passes true, and each tile slot subscribes for itself, so a hidden view of any body kind does no work. */
export function useViewSession(
  view: ViewDef,
  host: UiHost,
  system: SystemInfo | null,
  shown: boolean,
): ViewSessionHandle {
  const controller = useRef<ViewSessionController | null>(null);
  if (controller.current === null) controller.current = new ViewSessionController();
  const [session, setSession] = useState<OpenedSession | null | undefined>(undefined);

  // The key is the sync's to compare, so this re-runs on any candidate change and the controller is what decides
  // whether it is a real one. `setSession` with the same object does not re-render.
  useEffect(() => {
    setSession(controller.current!.sync(view, host, system, shown));
  }, [view, host, system, shown]);

  useEffect(() => () => controller.current?.close(), []);

  // The reader is the controller's, not this render's closure: a frame that lands after a hide or a reopen must not
  // read the session that change closed. It is stable, so a source that memoises it does not reset every render.
  const read = useCallback(
    <R,>(reader: (session: OpenedSession) => R): R | undefined => controller.current!.read(reader),
    [],
  );

  return { session: shownSession(session, shown, system), read };
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4381,4425p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/useViewSession.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/useViewSession.ts`
Expected: `26f92751dd76ecbb003e5a38221909c43637d0e7`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/PixelView.tsx`:

````ts
// The pixels renderer (docs/modular-graph-phase-f4c-design.md section 5.3): a view that draws its own frame, blitted
// into an LVGL Canvas the way the emulator tile and the LSDj HD player do. The shell draws it; a package only says how
// to open the session and what to show when there is nothing to read (the view's own `unsupported` text).
import { useRef } from "react";
import { EAlignType, Text } from "lvgljs-ui";

import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, ViewDef } from "@retroplug/ui/contribution";
import { Box, PixelCanvas, tagTestId, useNativeEvent, type CanvasHandle } from "@retroplug/ui-react";
import { useViewSession } from "./useViewSession";
import { isPixelSession } from "./viewSession";

export function PixelView({
  view,
  host,
  system,
  width,
  height,
  placement,
}: {
  view: ViewDef;
  host: UiHost;
  system: SystemInfo | null;
  width: number;
  height: number;
  /** Where the view sits: only a window view shows the body's `unsupported` text, since a tile view whose open was
   *  refused draws nothing (the contract says so, and the overlays' own null render was that). */
  placement: "window" | "tile";
}) {
  const canvasRef = useRef<CanvasHandle | null>(null);
  const { session, read } = useViewSession(view, host, system, true);

  useNativeEvent("frame", () => {
    // Do not ask the session for a frame until there is a canvas to put it in: the session's `frame()` is a
    // flush-and-diff (the HD canvas reports only the tiles that changed since its last flush), so reading one into
    // nothing would report it painted and never draw it. The canvas mounts with the session, one effect after the
    // first frame tick at the latest.
    const canvas = canvasRef.current;
    if (!canvas) return;
    // The controller's session, not this render's: a frame after a hide or a reopen must not read a closed one.
    const frame = read((open) => (isPixelSession(open) ? open.frame() : null));
    if (!frame) return; // nothing moved: skip the copy and the invalidate
    const pixels = frame.pixels;
    // The HD canvas hands its own array every frame, so that one is passed as it is; a frame that is a view onto a
    // larger buffer (the emulator's RPC wire buffer) is copied, since setBuffer wants a whole-buffer object.
    const whole = pixels.byteOffset === 0 && pixels.byteLength === pixels.buffer.byteLength;
    canvas.setBuffer((whole ? pixels.buffer : pixels.slice().buffer) as ArrayBuffer, frame.width, frame.height);
  });

  // Not opened yet: nothing to draw. That is a frame or two at most, and it must not read as "unsupported", which
  // would show the message on a cart the view reads perfectly well.
  if (session === undefined) return null;
  if (session !== null) {
    if (!isPixelSession(session)) return null;
    return <PixelCanvas canvasRef={canvasRef} width={width} height={height} />;
  }
  const unsupported = view.body.kind === "pixels" ? view.body.unsupported : undefined;
  if (placement !== "window" || !unsupported) return null;
  return (
    <Box
      innerRef={tagTestId(`${view.id}-unsupported`)}
      align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
      style={{ width, height: 40 }}
    >
      <Text style={{ "text-color": "#ffffff", "font-size": 16, width, height: 40 }}>{unsupported}</Text>
    </Box>
  );
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4437,4504p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/PixelView.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/PixelView.tsx`
Expected: `a567c397ce76fb0ee6954f0cd62a36ffaf46d6ee`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/ReadoutView.tsx`:

````ts
// The readout renderer (docs/modular-graph-phase-f4c-design.md section 5.3): three lines of text and a colour, deduped
// by the view's signature (or by the lines and the flag themselves) so a 60 fps frame tick does not thrash React. It is
// the strip the two debug overlays draw today, with the lines now coming from a package.
import { useCallback, useMemo } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { ReadoutFrame, UiHost, ViewDef } from "@retroplug/ui/contribution";
import { DebugReadout, useFrameSampled } from "@retroplug/ui-react";
import { useViewSession } from "./useViewSession";
import { isReadoutSampler, readoutKey, readoutSignature } from "./viewSession";

export function ReadoutView({
  view,
  host,
  system,
  width,
}: {
  view: ViewDef;
  host: UiHost;
  system: SystemInfo | null;
  width: number;
}) {
  const { session: sampler, read } = useViewSession(view, host, system, true);
  // useFrameSampled wants a memoised sampler: a new identity means a new source, and it resets what shows. The sample
  // reads the session the controller holds NOW, so a frame after a hide or a reopen never samples a closed one.
  const sample = useMemo(
    () =>
      sampler
        ? (): ReadoutFrame | null | undefined =>
            read((open) => (isReadoutSampler(open) ? open.sample() : null))
        : null,
    [sampler, read],
  );
  const signature = useCallback(
    (frame: ReadoutFrame | null): string => {
      if (frame === null) return "";
      return sampler && isReadoutSampler(sampler) ? readoutKey(view, sampler, frame) : readoutSignature(frame);
    },
    [view, sampler],
  );
  const frame = useFrameSampled(sample, signature);
  if (!frame) return null;
  return <DebugReadout lines={[...frame.lines]} playing={frame.playing} width={width} testId={view.id} />;
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4516,4559p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/ReadoutView.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/ReadoutView.tsx`
Expected: `09b8925a339d0a4f06b6199f77b396673d4ea7a7`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/ComponentView.tsx`:

````ts
// The component renderer (docs/modular-graph-phase-f4c-design.md section 5.3): mounts a package's own React component
// with the props the contract hands a view, and nothing around it. The body's component is `unknown` in
// @retroplug/ui (that contract imports no React), and this is the one cast the shell makes: reactView is what typed it.
import type { ComponentType } from "react";

import type { UiHost, ViewComponentProps } from "@retroplug/ui/contribution";

export function ComponentView({
  component,
  host,
  systemId,
  width,
  height,
}: {
  component: unknown;
  host: UiHost;
  systemId: number;
  width: number;
  height: number;
}) {
  const Component = component as ComponentType<ViewComponentProps>;
  return <Component host={host} systemId={systemId} width={width} height={height} />;
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4571,4593p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/ComponentView.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/ComponentView.tsx`
Expected: `060c5a1c1c6fb829b5887f6faadfd1928e6d1d08`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/ViewWindow.tsx`:

````ts
// A window view's frame (docs/modular-graph-phase-f4c-design.md sections 5.1 and 5.3): it takes the whole window in
// place of the grid, tags itself with the view's id (which is also the test id the HD player has today) and hands the
// body to the renderer its kind names.
import { Box, tagTestId } from "@retroplug/ui-react";
import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, WindowView } from "@retroplug/ui/contribution";
import { ComponentView } from "./ComponentView";
import { PixelView } from "./PixelView";
import { ReadoutView } from "./ReadoutView";

export function ViewWindow({
  view,
  host,
  system,
  systemId,
  width,
  height,
}: {
  view: WindowView;
  host: UiHost;
  system: SystemInfo | null;
  systemId: number;
  width: number;
  height: number;
}) {
  if (view.body.kind === "component") {
    return (
      <ComponentView
        component={view.body.component}
        host={host}
        systemId={systemId}
        width={width}
        height={height}
      />
    );
  }
  return (
    <Box innerRef={tagTestId(view.id)} style={{ width, height, "background-color": "#000000" }}>
      {view.body.kind === "pixels" ? (
        <PixelView view={view} host={host} system={system} width={width} height={height} placement="window" />
      ) : (
        <ReadoutView view={view} host={host} system={system} width={width} />
      )}
    </Box>
  );
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4605,4650p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/ViewWindow.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/ViewWindow.tsx`
Expected: `81148b879e0d216894aac4d886ea203756b1a976`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/ui/views/TileViews.tsx`:

````ts
// The tile slots (docs/modular-graph-phase-f4c-design.md section 5.2): every REGISTERED tile view, in product order,
// each in a StableSlot of its own, always. The slot is why a view that appears late (a flag flipping, a session opening
// on the next frame) does not land at the END of the tile's child list: lv_binding_js's insertChildBefore always
// appends (AGENTS.md "Framework gotchas"), so a fixed wrapper with one swappable child is the one way React can place
// it. The list is the registry's, which is a module constant, so the slot list never changes length; a `when` that
// flips under a live tile swaps the slot's child rather than appending a slot over the border and the badge, and a slot
// with nothing in it is an empty transparent Box (the same shape the dim layer proves passes a click to the tile).
import { useCallback, useSyncExternalStore } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { TileView, UiHost } from "@retroplug/ui/contribution";
import { StableSlot } from "../lvgl/StableSlot";
import { ComponentView } from "./ComponentView";
import { PixelView } from "./PixelView";
import { ReadoutView } from "./ReadoutView";
import { tileViews } from "../host/menuContributions";
import { useSystem } from "../stores/useStores";
import { subscribeViewShown, viewAppliesTo, viewShown } from "./viewSession";

export function TileViews({
  host,
  systemId,
  width,
  height,
}: {
  host: UiHost;
  systemId: number;
  width: number;
  height: number;
}) {
  const system = useSystem(systemId);
  // A system that went away between the tile's render and its own is the grid's to unmount: no slot, no work.
  if (system === undefined) return null;
  return (
    <>
      {tileViews().map(({ view }) => (
        <StableSlot key={view.id} testId={`view-slot-${view.id}`} width={width} height={height}>
          <TileViewBody view={view} host={host} system={system} width={width} height={height} />
        </StableSlot>
      ))}
    </>
  );
}

/** One tile view's body: the view's own visibility (subscribed here, for every body kind, so a hidden component view
 *  mounts nothing either) and its `when`, then the renderer its kind names. */
function TileViewBody({
  view,
  host,
  system,
  width,
  height,
}: {
  view: TileView;
  host: UiHost;
  system: SystemInfo;
  width: number;
  height: number;
}) {
  const shown = useSyncExternalStore(
    useCallback((onChange: () => void) => subscribeViewShown(view, onChange), [view]),
    useCallback(() => viewShown(view), [view]),
  );
  if (!shown || !viewAppliesTo(view, system)) return null;
  if (view.body.kind === "component") {
    return (
      <ComponentView component={view.body.component} host={host} systemId={system.id} width={width} height={height} />
    );
  }
  if (view.body.kind === "pixels") {
    return <PixelView view={view} host={host} system={system} width={width} height={height} placement="tile" />;
  }
  return <ReadoutView view={view} host={host} system={system} width={width} />;
}
````

Run: `mkdir -p packages/retroplug/ui/views; sed -n '4662,4735p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/views/TileViews.tsx`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/views/TileViews.tsx`
Expected: `d70d75d6a1b5bbfb991be93083eec06d92c8418a`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 6: App, the tile and the grid: the window slot and the host down the tree**

App's `hdSystemId` becomes `openView: { id, systemId } | null`; `openViewOf` looks the id up, reports (never throws)
an unknown id, a tile view's id or a system the view's `when` rejects, and opens nothing, while `"lsdj-hd"` is the
interim entry. Esc closes it after the overlay check, the stale-system clear is generic, and the full-window branch
renders `ViewWindow` for the registry's view or falls back to `LsdjHdScreen` for the interim id. `EmulatorTile` takes
the UI host as a prop, mounts `TileViews`, and still mounts the two hard overlays as they are; `SystemGrid` passes the
host down. `menuContributions.ts` gains `viewById`, `windowViewOf` and `tileViews()` beside its other registry
readers.

````diff
diff --git a/packages/retroplug/ui/App.tsx b/packages/retroplug/ui/App.tsx
index e8f719d62..92857ce67 100644
--- a/packages/retroplug/ui/App.tsx
+++ b/packages/retroplug/ui/App.tsx
@@ -40,7 +40,10 @@ import type { MenuTree } from "@retroplug/ui/menu";
 import { isMenuModalActive } from "./screens/menu/menuModal";
 import { buildKeyToAction, buildGamepadToAction, KEY_ESCAPE, type AppAction } from "../src/keyCodes";
 import { resolveDropAction } from "../src/fileDrop";
-import { createUiHost, uiViews } from "./host/uiHost";
+import { createUiHost } from "./host/uiHost";
+import { viewById, windowViewOf } from "./host/menuContributions";
+import { ViewWindow } from "./views/ViewWindow";
+import { viewAppliesTo } from "./views/viewSession";
 import { resolveTracker } from "../src/trackerRegistry";
 
 const KEY_BACKTICK = 0x60; // ` — toggles the LSDj runtime debug overlay (a developer aid, off by default)
@@ -66,13 +69,17 @@ export function App() {
 
   const [menuOpen, setMenuOpen] = useState(true);
   const [menuSystemId, setMenuSystemId] = useState<number | null>(null);
-  // The LSDj HD player, when open: the id of the system it's showing. It replaces the grid but is NOT an
+  // The window view that is open, if any: its id and the system it is showing. It replaces the grid but is NOT an
   // input overlay - the cart keeps playing and keys still reach its joypad, so it doesn't gate `playing`.
-  const [hdSystemId, setHdSystemId] = useState<number | null>(null);
+  //
+  // `host.views.open` looks the id up in the generated registry. The LSDj HD player is the shell's own screen, not a
+  // registry view: "lsdj-hd" is a shell-side entry in `openViewOf` and in the full-window render below, and both go
+  // when the cart registers the same id as a window view.
+  const [openView, setOpenView] = useState<{ id: string; systemId: number } | null>(null);
   // The UI's host (docs/modular-graph-phase-f4b-design.md section 3.2), built once over the stores: the groups a cart's
-  // or a console's UI code reaches the shell through. Its views are the shell's own screens (the HD player today), and
-  // its import picker is useSongImport's overlay, which is resolved against the live system an import names rather than
-  // the snapshot the caller held.
+  // or a console's UI code reaches the shell through. Its `views` group opens the generated registry's window views
+  // (through `openViewOf`, which keeps the HD player's shell-side entry), and its import picker is useSongImport's
+  // overlay, which is resolved against the live system an import names rather than the snapshot the caller held.
   const beginImport = useCallback(
     (sys: SystemInfo, source: Uint8Array): void => {
       const live = stores.project.systems.view().find((s) => s.id === sys.id);
@@ -80,9 +87,39 @@ export function App() {
     },
     [stores, songImport.begin],
   );
-  const uiHost = useMemo(() => createUiHost(stores, { views: uiViews(setHdSystemId), beginImport }), [
+  const openViewOf = useCallback(
+    (id: string, systemId: number): void => {
+      if (id === "lsdj-hd") {
+        // The shell's own entry (above): the HD player is not a registry view yet.
+        setOpenView({ id, systemId });
+        return;
+      }
+      const entry = viewById(id);
+      if (!entry) {
+        console.warn(`[views] no view is registered under the id ${id}`);
+        return;
+      }
+      if (entry.view.placement !== "window") {
+        console.warn(`[views] ${id} is a tile view, which opens no window`);
+        return;
+      }
+      const sys = stores.project.systems.view().find((s) => s.id === systemId);
+      if (!sys) {
+        console.warn(`[views] ${id} was asked for the system ${systemId}, which is not in the project`);
+        return;
+      }
+      if (!viewAppliesTo(entry.view, sys)) {
+        console.warn(`[views] ${id} does not apply to the system ${systemId}`);
+        return;
+      }
+      setOpenView({ id, systemId });
+    },
+    [stores],
+  );
+  const uiHost = useMemo(() => createUiHost(stores, { views: { open: openViewOf }, beginImport }), [
     stores,
     beginImport,
+    openViewOf,
   ]);
   // The product's UI services: the devices whose state lives in the native host (the N8 Pro's card job, a control
   // surface's scan and its startup restore) started, subscribed and polled at their own cadences.
@@ -143,11 +180,11 @@ export function App() {
     }
   }, [menuOpen, menuSystemId, systems, empty]);
 
-  // Close the HD player if the system it's showing goes away (removed, or the project reloaded) - a stale
-  // id would otherwise strand the UI on a screen with nothing to draw.
+  // Close any window view whose system goes away (removed, or the project reloaded): a stale id would otherwise
+  // strand the UI on a screen with nothing to draw. The check is generic over the open view, not the HD player alone.
   useEffect(() => {
-    if (hdSystemId != null && !systems.some((sys) => sys.id === hdSystemId)) setHdSystemId(null);
-  }, [hdSystemId, systems]);
+    if (openView != null && !systems.some((sys) => sys.id === openView.systemId)) setOpenView(null);
+  }, [openView, systems]);
 
   // Idle: point the keypad at the sink when the grid shows without a menu. Not while a modal overlay owns
   // the keypad (close prompt / project modal) — else closing the menu to raise one steals its focus.
@@ -197,7 +234,7 @@ export function App() {
     // Esc always cancels an active overlay — a universal back, independent of the (rebindable) OpenMenu key.
     if (key === KEY_ESCAPE) {
       if (overlay) return void overlay.onClose();
-      if (hdSystemId != null) return void setHdSystemId(null); // HD player → back to the grid
+      if (openView != null) return void setOpenView(null); // a window view → back to the grid
     }
     if (overlay) return; // an overlay owns input; actions don't fire under it
     if (key === KEY_BACKTICK) return void toggleLsdjDebug(); // dev: show/hide the LSDj runtime readout
@@ -301,18 +338,33 @@ export function App() {
     if (!veto) native.__rp_quitWindow?.();
   };
 
-  const ctx: MenuContext = { stores, host: uiHost, settings, userConfig, bindings, systems, recent, version, newProject: modals.newProject, loadProject: modals.loadProject, loadRomAsProject: modals.loadRomAsProject, requestExit, beginSongImport: beginImport, openLsdjHd: setHdSystemId };
+  const ctx: MenuContext = { stores, host: uiHost, settings, userConfig, bindings, systems, recent, version, newProject: modals.newProject, loadProject: modals.loadProject, loadRomAsProject: modals.loadRomAsProject, requestExit, beginSongImport: beginImport, openLsdjHd: (systemId) => openViewOf("lsdj-hd", systemId) };
 
   // A full-window overlay (file browser / close prompt / project modal / song import) draws above
   // everything and owns the keypad; the start menu (empty project) is the same full-window menu, always open.
   if (overlay) return <FullWindowMenu size={windowSize} zoom={resolvedZoom} tree={overlay.tree} onClose={overlay.onClose} />;
   if (empty) return <FullWindowMenu size={windowSize} zoom={resolvedZoom} tree={buildStartMenu(ctx)} onClose={() => {}} />;
 
-  // The HD player takes the whole window in place of the grid. A stale id (its system was removed, or the
-  // project reloaded) falls through to the grid here and is cleared by the effect above.
-  if (hdSystemId != null && systems.some((sys) => sys.id === hdSystemId)) {
+  // A window view takes the whole window in place of the grid. A stale id (its system was removed, or the project
+  // reloaded) falls through to the grid here and is cleared by the effect above.
+  if (openView != null && systems.some((sys) => sys.id === openView.systemId)) {
     const { width, height } = windowSize;
-    return <LsdjHdScreen systemId={hdSystemId} width={width} height={height} testId="lsdj-hd" />;
+    const view = windowViewOf(openView.id);
+    if (view) {
+      const system = systems.find((sys) => sys.id === openView.systemId) ?? null;
+      return (
+        <ViewWindow
+          view={view}
+          host={uiHost}
+          system={system}
+          systemId={openView.systemId}
+          width={width}
+          height={height}
+        />
+      );
+    }
+    // The shell's own entry: the LSDj HD player, until its cart registers the id as a window view.
+    return <LsdjHdScreen systemId={openView.systemId} width={width} height={height} testId="lsdj-hd" />;
   }
 
   // The anchor FOLLOWS its system through an in-place rebuild (a keep-open row like FM Audio resets the
@@ -323,6 +375,7 @@ export function App() {
   const menuTree = anchored ? buildInstanceMenu({ ...ctx, system: anchored }) : undefined;
   return (
     <SystemGrid
+      host={uiHost}
       menuSystemId={menuTree ? anchorId : null}
       menuTree={menuTree}
       onMenuClose={() => {
diff --git a/packages/retroplug/ui/host/menuContributions.ts b/packages/retroplug/ui/host/menuContributions.ts
index c8fb4bc94..0b2e5badf 100644
--- a/packages/retroplug/ui/host/menuContributions.ts
+++ b/packages/retroplug/ui/host/menuContributions.ts
@@ -5,17 +5,36 @@
 // kind no registered type declares or over a key its schema does not read, and a tracker UI no integration matches, so
 // what this module reads it can index.
 import type {
-  MenuBuildContext, MenuBuilder, RoleRow, TrackerUi, UiHost, UiService,
+  MenuBuildContext, MenuBuilder, RoleRow, TileView, TrackerUi, UiHost, UiService, ViewDef, WindowView,
 } from "@retroplug/ui/contribution";
 import type { SystemInfo } from "@retroplug/core/host";
 import type { MenuItem } from "@retroplug/ui/menu";
-import { UI_CONTRIBUTIONS } from "../generated/menus";
+import { UI_CONTRIBUTIONS, VIEWS } from "../generated/menus";
 
 /** Every contributed role row, in product order: the System submenu's package-owned block. */
 export function roleRows(): readonly RoleRow[] {
   return UI_CONTRIBUTIONS.flatMap((entry) => entry.ui.rows ?? []);
 }
 
+/** The view a package registers under `id`, with the package it came from, or undefined. The generated module has
+ *  already refused a duplicate id, so the first match is the only one. */
+export function viewById(id: string): { pkg: string; view: ViewDef } | undefined {
+  return VIEWS.find((entry) => entry.view.id === id);
+}
+
+/** The window view a package registers under `id`, or undefined for an unknown id or a tile view. What
+ *  `host.views.open` looks an id up in, so a tile view's id opens nothing. */
+export function windowViewOf(id: string): WindowView | undefined {
+  const entry = viewById(id);
+  return entry?.view.placement === "window" ? entry.view : undefined;
+}
+
+/** Every tile view, in product order, with the package it came from: the layers EmulatorTile mounts over each tile
+ *  whose system the view's `when` accepts. */
+export function tileViews(): readonly { pkg: string; view: TileView }[] {
+  return VIEWS.filter((entry): entry is { pkg: string; view: TileView } => entry.view.placement === "tile");
+}
+
 /** The tracker UI a cart contributes, by its integration id, or undefined for a cart with no contributed specs. */
 export function contributedTrackerUi(id: string): TrackerUi | undefined {
   for (const entry of UI_CONTRIBUTIONS) {
diff --git a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
index 6b38350a6..e2d9f1368 100644
--- a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
+++ b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
@@ -18,12 +18,15 @@ import {
   useNativeEvent,
   type CanvasHandle,
 } from "@retroplug/ui-react";
+import type { UiHost } from "@retroplug/ui/contribution";
 import { LsdjOverlay } from "./LsdjOverlay";
 import { RisaOverlay } from "./RisaOverlay";
+import { TileViews } from "../../views/TileViews";
 import { pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../host/render";
 
 export function EmulatorTile({
   systemId,
+  host,
   focused,
   single,
   width,
@@ -32,6 +35,8 @@ export function EmulatorTile({
   dimTestId,
 }: {
   systemId: number;
+  /** The UI host: what a package's view is handed when it opens its session. */
+  host: UiHost;
   focused: boolean;
   /** The only system in the project. A lone tile fills the window with no focus border and no dim — the
    *  focus cue only means something when there's another tile to distinguish it from. */
@@ -90,6 +95,10 @@ export function EmulatorTile({
       {/* The risa twin — a live NES-tracker readout (screen + cursor / mode / tempo / version / per-track
           phrase). Renders nothing for a non-risa ROM, so it too is mounted unconditionally. */}
       <RisaOverlay systemId={systemId} width={width} testId="risa-overlay" />
+      {/* The product's tile views, one StableSlot each (docs/modular-graph-phase-f4c-design.md section 5.2): a
+          fixed place in the child list, so a view that appears late does not land over the border or badge. The
+          product registers none yet; a cart's overlays land here once they move. */}
+      <TileViews host={host} systemId={systemId} width={width} height={height} />
       {showBorder && (
         // Accent border on the focused tile — the visible focus cue (the dim is imperceptible on a
         // near-black GB screen). Drawn as a transparent full-tile overlay layered OVER the Canvas, not
diff --git a/packages/retroplug/ui/screens/grid/SystemGrid.tsx b/packages/retroplug/ui/screens/grid/SystemGrid.tsx
index c1305a784..936f295b9 100644
--- a/packages/retroplug/ui/screens/grid/SystemGrid.tsx
+++ b/packages/retroplug/ui/screens/grid/SystemGrid.tsx
@@ -15,14 +15,18 @@ import { StableSlot } from "../../lvgl/StableSlot";
 import { EmulatorTile } from "./EmulatorTile";
 import { Menu } from "../menu/Menu";
 import type { MenuTree } from "@retroplug/ui/menu";
+import type { UiHost } from "@retroplug/ui/contribution";
 import type { SystemView } from "../../../src/systemsStore";
 import { SystemLayout, shapeFor, tileWidth, tileHeight, displayZoom } from "./layout";
 
 export function SystemGrid({
+  host,
   menuSystemId,
   menuTree,
   onMenuClose,
 }: {
+  /** The UI host a tile's views are handed (docs/modular-graph-phase-f4c-design.md section 5.2). */
+  host: UiHost;
   menuSystemId?: number | null;
   menuTree?: MenuTree;
   onMenuClose?: () => void;
@@ -61,6 +65,7 @@ export function SystemGrid({
                   ) : (
                     <EmulatorTile
                       systemId={sys.id}
+                      host={host}
                       focused={sys.focused}
                       single={single}
                       width={tw}
````

Run: `sed -n '4755,5020p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/ui/App.tsx
packages/retroplug/ui/host/menuContributions.ts
packages/retroplug/ui/screens/grid/EmulatorTile.tsx
packages/retroplug/ui/screens/grid/SystemGrid.tsx
EOF
````

Expected:

````text
92857ce67fed3c8ed0484c8239b640db67687c5a
0b2e5badf9eb6ac006e41c1e2c6a28584cedce43
e2d9f13689133c5d44c42359821ddac2df21c721
936f295b9dc8130a998100bb4c0a082dc77a70b6
````

- [ ] **Step 7: `ViewDefinition` leaves the extension seed**

`src/graph/defineExtension.ts` loses `ViewPlacement`, `ViewDefinition`, `Contribution.views` and `Collected.views`,
with the one test that read them: the fixture extension's `views` entry, the `c.views` expectation and "and views" in
the case's name. No production extension contributed one, and a UI-only concept does not belong in the control plane's
extension model (E8).

````diff
diff --git a/packages/retroplug/src/graph/defineExtension.ts b/packages/retroplug/src/graph/defineExtension.ts
index d0ec7e2cc..d01facc98 100644
--- a/packages/retroplug/src/graph/defineExtension.ts
+++ b/packages/retroplug/src/graph/defineExtension.ts
@@ -1,6 +1,6 @@
 // Extensions: control-plane features that attach to a node INSTANCE (docs/modular-graph-design.md
 // sections 2.6, 2.7 and 7.1). An extension has no ports; it is chosen by a matcher over the node's
-// identity, and contributes nodes it inserts and wires, views, and song / asset catalogs.
+// identity, and contributes nodes it inserts and wires, and song / asset catalogs.
 //
 // Port ownership: each node input may be claimed by only ONE contributor - an extension on a node, or the
 // base graph the builder made itself. collect() reports a second claim as a conflict naming every claimant,
@@ -24,26 +24,11 @@ export interface NodeInfo {
   };
 }
 
-export type ViewPlacement = "tile" | "overlay" | "fullscreen";
-
-export interface ViewDefinition {
-  id: string;
-  label: string;
-  placement: ViewPlacement;
-  /** Beats the node type's own default view (BlipToaster opens on its panel, not NES video). */
-  default?: boolean;
-  /** The UI component. Typed loosely on purpose: src/ never imports ui/, which owns the component types. */
-  component: unknown;
-  /** Data the view reads, so the host can arm only those snapshot ranges. Optional. */
-  needs?: { memory?: { region: string; range: [number, number] }[]; rate?: "frame" | "block" };
-}
-
 /** What an extension adds for one node. Menu items and actions are deliberately absent until the package
  *  split extracts the menu item model (design section 7.7). */
 export interface Contribution {
   nodes?: NodeDoc[];
   wires?: WireDoc[];
-  views?: ViewDefinition[];
   songs?: SongCatalog;
   assets?: AssetCatalog;
 }
@@ -93,15 +78,14 @@ export interface CollectError {
 export interface Collected {
   nodes: NodeDoc[];
   wires: WireDoc[];
-  views: { node: string; extension: string; view: ViewDefinition }[];
   attached: { node: string; extension: string; contribution: Contribution }[];
   conflicts: Conflict[];
   errors: CollectError[];
 }
 
 // A JSON-only copy of a node or wire document, so a builder that edits the collected graph cannot reach the
-// contribution it came from (or the other way round). Views and catalogs are not documents (they carry
-// components and methods), so they are passed through as they are.
+// contribution it came from (or the other way round). Catalogs are not documents (they carry methods), so they are
+// passed through as they are.
 const copy = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;
 
 // Extensions are kept sorted by id, and contributions are collected node by node in that order, so the
@@ -129,7 +113,7 @@ export class ExtensionRegistry {
     settingsFor: (node: string, extension: string) => unknown = () => undefined,
     base: { nodes: readonly NodeDoc[]; wires: readonly WireDoc[] } = { nodes: [], wires: [] },
   ): Collected {
-    const out: Collected = { nodes: [], wires: [], views: [], attached: [], conflicts: [], errors: [] };
+    const out: Collected = { nodes: [], wires: [], attached: [], conflicts: [], errors: [] };
     const inputs = new Map<string, Contributor[]>();
     const ids = new Map<string, Contributor[]>();
     const claim = (map: Map<string, Contributor[]>, key: string, by: Contributor) => map.set(key, [...(map.get(key) ?? []), by]);
@@ -191,7 +175,6 @@ export class ExtensionRegistry {
           claim(inputs, w.to, by);
           out.wires.push(copy(w));
         }
-        for (const view of c.views ?? []) out.views.push({ node: node.id, extension: ext.id, view });
       }
     }
     for (const [input, by] of inputs) if (by.length > 1) out.conflicts.push({ kind: "input", input, by });
diff --git a/packages/retroplug/test/graph/extension.test.ts b/packages/retroplug/test/graph/extension.test.ts
index b20299fdb..c9debc9d1 100644
--- a/packages/retroplug/test/graph/extension.test.ts
+++ b/packages/retroplug/test/graph/extension.test.ts
@@ -16,7 +16,6 @@ const lsdj = defineExtension({
   contribute: ({ node, settings }) => ({
     nodes: [{ id: `${node.id}/lsdj-sync`, type: "rp.lsdj.sync", params: { mode: settings.syncMode } }],
     wires: [{ from: endpoint(`${node.id}/lsdj-sync`, "serialOut"), to: endpoint(node.id, "serialIn") }],
-    views: [{ id: "hd", label: "HD", placement: "fullscreen", component: () => null }],
   }),
 });
 
@@ -31,7 +30,7 @@ test("an extension attaches only to the nodes its matcher accepts", () => {
   expect(() => defineExtension({ id: "  ", matches: () => true, contribute: () => ({}) })).toThrow("whitespace");
 });
 
-test("collect parses each extension's settings and gathers its nodes, wires and views", () => {
+test("collect parses each extension's settings and gathers its nodes, wires and catalogs", () => {
   const reg = new ExtensionRegistry();
   reg.add(lsdj);
   const c = reg.collect([gb, gb2, nes], (node, ext) => (node === "sys-1" && ext === "rp.lsdj" ? { syncMode: "off" } : undefined));
@@ -43,7 +42,6 @@ test("collect parses each extension's settings and gathers its nodes, wires and
     { from: "sys-1/lsdj-sync.serialOut", to: "sys-1.serialIn" },
     { from: "sys-2/lsdj-sync.serialOut", to: "sys-2.serialIn" },
   ]);
-  expect(c.views.map((v) => [v.node, v.extension, v.view.id])).toEqual([["sys-1", "rp.lsdj", "hd"], ["sys-2", "rp.lsdj", "hd"]]);
   expect(c.attached.map((a) => [a.node, a.extension])).toEqual([["sys-1", "rp.lsdj"], ["sys-2", "rp.lsdj"]]);
   expect(c.conflicts.length).toBe(0);
   expect(c.errors.length).toBe(0);
````

Run: `sed -n '5054,5156p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/src/graph/defineExtension.ts
packages/retroplug/test/graph/extension.test.ts
EOF
````

Expected:

````text
d01facc98bbb2ce5e96b4c7c24f1271ea442fbb2
c9debc9d188d1cc0b5305e3e829b08273ab1a99a
````

- [ ] **Step 8: The controller's cases, over fake views and a fake host**

`test/ui/view-session.test.ts` is the one file that needs no React: it drives the controller over fake views and a
fake host and pins the lifecycle (open once per key, reopen on a ROM change and on a role change, nothing for a hidden
tile view, a gone system closing it, a throwing open and close reported), the three-state read (a frame reads the
session held now, a throwing reader is reported once and drops it, a view not opened yet draws nothing), and the
guards (a throwing `when` and `visible` are reported once each, a throwing signature falls back to the default).

Create `packages/retroplug/test/ui/view-session.test.ts`:

````ts
// The view session lifecycle (docs/modular-graph-phase-f4c-design.md section 5.4), over fake views and a fake host: no
// React, no display. The renderers call the same controller from one effect (ui/views/useViewSession.ts), so what is
// held, reopened, closed and read here is what an overlay or the HD screen gets.
import { test, expect } from "@retroplug/testing";
import type { SystemInfo } from "@retroplug/core/host";
import type {
  PixelSession, ReadoutFrame, Signal, TileView, UiHost, ViewDef, WindowView,
} from "@retroplug/ui/contribution";
import { createSignal } from "@retroplug/ui/menu";

import { warned } from "../_warnings";
import {
  isPixelSession,
  openSession,
  readoutKey,
  reopenKey,
  sameReopenKey,
  shownSession,
  subscribeViewShown,
  ViewSessionController,
  viewAppliesTo,
  viewShown,
} from "../../ui/views/viewSession";

/** The host a fake view's `open` receives: the fakes read nothing of it, and no group is reached. */
const host = {} as UiHost;

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/game.gb",
    savPath: "/roms/game.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [],
    ...over,
  };
}

/** A fake session that records its close and every frame it was asked for. */
function session(label: string, opened: string[], closed: string[], frames: string[] = []): PixelSession {
  opened.push(label);
  return {
    frame: () => {
      frames.push(label);
      return null;
    },
    close: () => closed.push(label),
  };
}

/** A window view whose body opens a recorded session, so the controller's decisions are all it tests. */
function fakeView(id: string, opened: string[], closed: string[], frames: string[] = []): WindowView {
  return {
    id,
    label: id,
    placement: "window",
    body: { kind: "pixels", open: () => session(id, opened, closed, frames) },
  };
}

test("a session opens once per key and closes on the next change", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  const sys = system();
  const first = held.sync(view, host, sys, true);
  // The same key, twice: the same session object, opened once.
  expect(held.sync(view, host, sys, true) === first).toBe(true);
  expect(opened).toEqual(["test-view"]);
  expect(closed).toEqual([]);
  // A ROM change is a new key: the old session closes, a new one opens.
  const second = held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true);
  expect(second === first).toBe(false);
  expect(opened).toEqual(["test-view", "test-view"]);
  expect(closed).toEqual(["test-view"]);
  held.close();
  expect(closed).toEqual(["test-view", "test-view"]);
});

test("a role edit reopens the views of that system", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  const sys = system({ roles: [{ kind: "sameboy", config: {} }] as SystemInfo["roles"] });
  held.sync(view, host, sys, true);
  // SystemsStore.setRoleConfig always hands back a NEW roles array, so identity is what the key sees.
  const edited = { ...sys, roles: [...sys.roles] };
  held.sync(view, host, edited, true);
  expect(opened.length).toBe(2);
  expect(closed.length).toBe(1);
  // A new system view with the SAME roles array is not a role edit: no reopen.
  held.sync(view, host, { ...edited }, true);
  expect(opened.length).toBe(2);
});

test("a hidden tile view opens nothing, and a visibility flip closes what was open", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const visible = createSignal(false);
  const view: TileView = {
    id: "test-overlay",
    label: "Test Overlay",
    placement: "tile",
    visible,
    body: { kind: "pixels", open: () => session("overlay", opened, closed) },
  };
  const held = new ViewSessionController();
  expect(held.sync(view, host, system(), viewShown(view))).toBe(null);
  expect(opened).toEqual([]); // hidden: no session and no work
  visible.set(true);
  expect(held.sync(view, host, system(), viewShown(view)) === null).toBe(false);
  expect(opened).toEqual(["overlay"]);
  visible.set(false);
  expect(held.sync(view, host, system(), viewShown(view))).toBe(null);
  expect(closed).toEqual(["overlay"]); // the flip closes it
});

test("a system that is gone closes the session", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  held.sync(view, host, system(), true);
  expect(held.sync(view, host, null, true)).toBe(null);
  expect(closed).toEqual(["test-view"]);
});

test("a throwing open is reported and leaves the system without a session", () => {
  let opens = 0;
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => {
        opens += 1;
        throw new Error("the cart is unreadable");
      },
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  const { result, warnings } = warned(() => held.sync(view, host, sys, true));
  expect(result).toBe(null);
  expect(warnings).toEqual(["[views] test-view: opening the session failed: the cart is unreadable"]);
  // The key is held with no session: the shell retries only when the key moves, not on every frame's render.
  held.sync(view, host, sys, true);
  expect(opens).toBe(1);
  held.close(); // a null session closes without a warning or a second throw
});

test("a throwing close is reported and does not stop the next open", () => {
  const opened: string[] = [];
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => {
        opened.push("open");
        return { frame: () => null, close: () => { throw new Error("teardown failed"); } };
      },
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  held.sync(view, host, sys, true);
  const { warnings } = warned(() => held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true));
  expect(warnings).toEqual(["[views] closing a session failed: teardown failed"]);
  expect(opened.length).toBe(2);
});

test("a component body opens no session, and the key is its id, the system, the ROM and the roles", () => {
  const component: ViewDef = {
    id: "test-component",
    label: "Test Component",
    placement: "window",
    body: { kind: "component", component: () => null },
  };
  expect(openSession(component, host, 1)).toBe(null);

  const sys = system({ roles: [{ kind: "sameboy", config: {} }] as SystemInfo["roles"] });
  const view = fakeView("test-view", [], []);
  const key = reopenKey(view, sys);
  expect(sameReopenKey(key, reopenKey(view, { ...sys }))).toBe(true);
  expect(sameReopenKey(key, reopenKey(view, { ...sys, romPath: "/roms/other.gb" }))).toBe(false);
  expect(sameReopenKey(key, reopenKey(view, { ...sys, roles: [...sys.roles] }))).toBe(false);
  expect(sameReopenKey(reopenKey(view, sys), reopenKey(fakeView("other-view", [], []), sys))).toBe(false);
});

test("a frame reads the session held now: none before the first open, none after a hide or a reopen", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const frames: string[] = [];
  const view = fakeView("test-view", opened, closed, frames);
  const held = new ViewSessionController();
  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(undefined); // nothing opened: no call at all

  const sys = system();
  held.sync(view, host, sys, true);
  held.read((s) => (isPixelSession(s) ? s.frame() : null));
  expect(frames).toEqual(["test-view"]);

  // A hide closes the session, and a frame that lands before the next render must not reach it.
  held.sync(view, host, sys, false);
  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(undefined);
  expect(closed).toEqual(["test-view"]);
  expect(frames).toEqual(["test-view"]);

  // A reopen swaps the session: a frame reads the NEW one, never the closed one.
  held.sync(view, host, sys, true);
  const reopened = held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true);
  let read: unknown = null;
  held.read((s) => {
    read = s;
    return null;
  });
  expect(read === reopened).toBe(true);
  expect(read === null).toBe(false);
});

test("a reader that throws is reported once and drops the session until the key moves", () => {
  let reads = 0;
  const closed: string[] = [];
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => ({
        frame: () => {
          reads += 1;
          throw new Error("the frame is broken");
        },
        close: () => closed.push("test-view"),
      }),
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  held.sync(view, host, sys, true);
  const { result, warnings } = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
  expect(result).toBe(undefined);
  expect(warnings).toEqual(["[views] reading a session failed: the frame is broken"]);
  expect(closed).toEqual(["test-view"]); // the faulted session is closed, not left half-usable
  // Dropped, so the next frame does not call it again: one report per faulty session.
  const after = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
  expect(after.result).toBe(undefined);
  expect(after.warnings).toEqual([]);
  expect(reads).toBe(1);
});

test("a view that has not opened yet draws nothing, and a hide drops the session in the same render", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const sys = system();
  const session = openSession(view, host, sys.id);
  // Not opened yet: undefined, which a renderer draws nothing for. It must not read as "unsupported".
  expect(shownSession(undefined, true, sys)).toBe(undefined);
  // Opened, then hidden or its system gone: null in the render that sees it, not one effect later.
  expect(shownSession(session, true, sys) === session).toBe(true);
  expect(shownSession(session, false, sys)).toBe(null);
  expect(shownSession(session, true, null)).toBe(null);
});

test("a throwing when reads as no and a throwing visible as hidden, each reported once", () => {
  const sys = system();
  let whenCalls = 0;
  const visible: Signal<boolean> = {
    get: () => {
      throw new Error("the flag is broken");
    },
    set: () => {},
    subscribe: () => {
      throw new Error("the flag is broken");
    },
  };
  const view: TileView = {
    id: "test-overlay",
    label: "Test Overlay",
    placement: "tile",
    when: () => {
      whenCalls += 1;
      throw new Error("the predicate is broken");
    },
    visible,
    body: { kind: "readout", open: () => null },
  };
  const { result, warnings } = warned(() => ({
    applies: viewAppliesTo(view, sys),
    shown: viewShown(view),
    unsubscribe: subscribeViewShown(view, () => {}),
  }));
  expect(result.applies).toBe(false); // a throw is "no": nothing renders, and the render tree survives
  expect(result.shown).toBe(false);
  expect(typeof result.unsubscribe).toBe("function"); // nothing to watch, and no throw out of the hook
  expect(warnings.filter((w) => w.includes("its when threw"))).toEqual([
    "[views] test-overlay: its when threw: the predicate is broken",
  ]);
  expect(warnings.filter((w) => w.includes("its visible threw"))).toEqual([
    "[views] test-overlay: its visible threw: the flag is broken",
  ]);
  // Read again on the next render: still "no", and not a second report for the same thing.
  const again = warned(() => {
    viewAppliesTo(view, sys);
    viewShown(view);
  });
  expect(again.warnings).toEqual([]);
  expect(whenCalls).toBe(2);
});

test("a throwing signature is reported once and the default over the lines and the flag is taken", () => {
  const view = fakeView("test-view", [], []);
  const frame: ReadoutFrame = { lines: ["a", "b"], playing: true };
  const sampler = {
    sample: () => frame,
    signature: (): string => {
      throw new Error("the signature is broken");
    },
  };
  const { result, warnings } = warned(() => readoutKey(view, sampler, frame));
  expect(result).toBe("1|a\nb");
  expect(warnings).toEqual(["[views] test-view: its signature threw: the signature is broken"]);
  expect(warned(() => readoutKey(view, sampler, frame)).warnings).toEqual([]);
});
````

Run: `mkdir -p packages/retroplug/test/ui; sed -n '5189,5524p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/ui/view-session.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/ui/view-session.test.ts`
Expected: `87ed5aeee48d06cf4bf826e02df180dea32c5ba3`: the blob eb20634ea holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 9: The red proofs**

Two REDs, each recorded by the stage or its fix pass. The generator's six new cases were written first and run against
STAGE 1's generator, which has no `VIEWS`: every one fails, and the refusal cases fail on `Cannot read properties of
undefined (reading 'map')` rather than on their own reason, which is what the fix pass then made each case fail for.
The stage 2 fix pass added the guards and the body checks, and its own RED is a build failure naming every export that
did not exist yet.

Run: `bash -c 'cp packages/retroplug/scripts/lib/packages.mjs /tmp/f4c-task2-packages.mjs && git show HEAD:packages/retroplug/scripts/lib/packages.mjs > packages/retroplug/scripts/lib/packages.mjs && node packages/retroplug/scripts/run-tests.mjs packages/packages-generator > /tmp/f4c-s1gen.log 2>&1; e=$?; cp /tmp/f4c-task2-packages.mjs packages/retroplug/scripts/lib/packages.mjs; echo "runner exit $e"; grep -c "^not ok" /tmp/f4c-s1gen.log'`
Expected: the runner exits nonzero (`runner exit 1` on this box) and `6`: with the PARENT task's generator in place (HEAD's, saved back afterwards), the six new view cases are `not ok` and the rest of the generator suite still passes. This is the derivation the log `/tmp/f4cpro/s2-red-views.log` recorded (89 passed, 6 failed); the second `cp` puts this task's generator back, so `git status` is the task's own afterwards.

The six failing cases, quoted as the log prints them (each one's own reason, the order case's `undefined`):

The suite's own line after them is `# 89 passed, 0 skipped, 6 failed`: the rest of the generator suite, including the
old expected `menus.ts`, still passed, which is what makes the six failures the checks' absence and nothing else.

The fix pass's RED is the new `view-session.test.ts` run against stage 2's first `viewSession.ts`, which lacks every
export the guards need:

- [ ] **Step 10: Configure, install, build, and the bytes that must not move**

Run: `cmake -S . -B build`
Expected: exit 0: no manifest changes (no product lists a view yet, and no dependency moves), so the configure only re-reads the product list and the generator's inputs.

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and the lockfile is Task 1's: this task adds no dependency.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree: the UI bundle gains the three renderers, the slots and App's window branch.

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

- [ ] **Step 11: Run the suites**

Task 2's figures are stage 2's own full run (`/tmp/f4cpro/s2v-*.log`, every leg exit 0, no retries, at `6a6855b07`),
the tree before stage 2's own fix pass (the three-state `useViewSession`, a slot per registered view with `when`
inside it, the guards, the body-kind checks, the `zeta` fixture order). That run was taken before the pass was folded
in; the pass's own exit was `pnpm typecheck`, `pnpm test` (223 files, 2322 cases) and `check:boundaries`, recorded in
the stage 3 report, and the stage 3 run below is over the fixed tree. The code the plan places is the commit's.

The verification is the parallel kit, one command, run in the foreground and waited for: it sets each leg's own
`RETROPLUG_RESOURCES_DIR`, skips `sdl:controller` (which claims system-wide ALSA ports) and logs one file per leg. Run
`pnpm sdl:controller` on its own afterwards, never another way, and compare the summary and the gated logs with the
baseline at the design commit.

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4c-v bash .superpowers/verify/tip-verify-par.sh`
Expected: exit 0 and `== every leg passed`, with no `/tmp/f4c-v-retries.txt` (a retry is a leg the kit re-ran; report it). The base's run is `/tmp/f4cpro/base-*` and every leg below quotes this task's own log.

Run: `pnpm sdl:controller`
Expected: exit 0: `SDL controller OK`, after fifteen `ok:` lines. The first ten are the bliptoaster pack's toolchain, which the SDL build runs and prints (the same ten the baseline's log has), and the last five are the harness's own legs, byte-identical to the baseline's. It sets `RP_MIDI_PORT_ALLOW` itself, from the two fakes' ready lines, and refuses a caller's list. The five:

````text
  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream
  ok: reclaimed before MIDI opens and audio starts; its notes kept out of the shared stream; released at quit
  ok: Programmer mode first, LED traffic, the release last
  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside
  ok: a second standalone leaves the first one's device alone, and does not hear its pads
```` This is the one command in this plan that may touch a MIDI port at all, and it sets `RP_MIDI_PORT_ALLOW` from its two ALSA fakes itself.

Run: `bash .superpowers/verify/compare.sh /tmp/f4cpro/base /tmp/f4c-v`
Expected: exit 1 (anything differs), printing only the lines this task is expected to move: the `test` case and file counts, `ui`'s from Task 4, Task 1's two `typecheck` programs and the `measure` sizes (info only, never gated). `imagesha` and `packbytes` must be ABSENT from the diff, and every other leg's counts must be the baseline's. A difference the task does not name is a finding.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree. No compiler warning is expected: every stage's build log has none.

Run: `pnpm typecheck`
Expected: `Scope: 25 of 26 workspace projects`, then `packages/retroplug typecheck: Done`: every project with a `typecheck` script typechecks on its own tsconfig, and the shell's program covers every package's `ts/` as well and `packages/ui-react typecheck: Done` and `packages/ui-fixture typecheck: Done` (Task 1's two packages, unchanged). The baseline's scope is `Scope: 23 of 24 workspace projects` with 17 `Done` lines; this tree's is 25 of 26 and 19.

Run: `pnpm test`
Expected: `# 223 test file(s) passed (jobs=5)`. The runner's own line counts FILES; the case total is the stage 3 report's exit on the amended stage 2 tree: `test exit=0 # 223 test file(s) passed (jobs=16), 2322 cases, 0 skipped`, since no runner prints one in this suite. The pool size it prints is the kit's `TV_TS_JOBS` (5); a hand run prints its own, so the file count is what must match.

Run: `pnpm test:plugin`
Expected: `# 13 plugin test binary(ies) passed (jobs=5)`: every binary exits 0, and the counts are (quoted as the log prints them, the engine-graph suite's assertion total and its eleven skips as that binary prints them):

````text
retroplug-mesen-test: `All tests passed (82321 assertions in 80 test cases)`
retroplug-watcher-test: `All tests passed (6 assertions in 2 test cases)`
retroplug-audio-test: `All tests passed (378 assertions in 16 test cases)`
retroplug-launchpad-test: `All tests passed (12925 assertions in 53 test cases)`
retroplug-classid-test: `All tests passed (10 assertions in 3 test cases)`
retroplug-plugin-test: `All tests passed (21 assertions in 6 test cases)`
retroplug-lottie-test: `All tests passed (7 assertions in 1 test case)`
retroplug-dynparams-test: `All tests passed (24 assertions in 7 test cases)`
retroplug-midi-test: `All tests passed (78 assertions in 24 test cases)`
retroplug-graph-test: `All tests passed (356662 assertions in 296 test cases)`
retroplug-sameboy-test: `All tests passed (1152140 assertions in 7 test cases)`
retroplug-device-mode-test: `All tests passed (1504408 assertions in 96 test cases)`
retroplug-engine-graph-test: `test cases: 336 | 325 passed | 11 skipped`
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1714534 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the fixture-gated cases run.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, none skipped, nothing re-recorded: 30 files and 250 cases (the reports' count), the device images' and the pack's bytes identical to the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 144 case(s) SKIPPED in 65 file(s)` (its list of files follows) and the skip check passing: CI's view, its skips the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 43 UI test file(s) passed (jobs=4)`. The case total is the stage 1, 2 and 3 reports': `43 files, 49 cases`, unchanged by the three stages (no product gains a view or an action until Task 4) (a case that cannot run SKIPS and is counted, and this leg runs WITH the fixtures).

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks.

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

Run: `pnpm test:n8`
Expected: `All tests passed (1468 assertions in 104 test cases)` (`retroplug-n8-test`) and `All tests passed (17 assertions in 2 test cases)` (`retroplug-serial-test`), the baseline's: fakes only, no port is opened (the `[retroplug] N8 restore: port '/dev/ttyACM0'` line it prints is a test case's, over a fake port factory and a scratch n8.cfg; the baseline's run printed it too).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed (the counts are the reports' tables, each stage's).

Run: `bash -c 'for t in render-menu render-badge; do pnpm test:ui $t || exit 1; done'`
Expected: exit 0: `render-menu` and `render-badge`, one test file each and 1 passed.

Run: `pnpm test render`
Expected: `# 9 test file(s) passed (jobs=5)`: the render worker's own files.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after the smoke's eight `ok:` lines (the build before them prints the bliptoaster pack's toolchain lines, which start `ok:` too). It runs under a scratch config dir with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which matches nothing, so it claims and opens nothing.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three ThreadSanitizer binaries, built from this tree's sources (their objects are the prototype's until the first build here).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1699098 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`.

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'`
Expected: `All tests passed (1675 assertions in 53 test cases)`.

Run: `bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'`
Expected: `All tests passed (1499768 assertions in 51 test cases)`.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three AddressSanitizer binaries, built from this tree's sources.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1682147 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356662 assertions in 296 test cases)`.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'`
Expected: `All tests passed (1504408 assertions in 96 test cases)`. The prototype's first full run failed this leg once, on the script pack's deadline case under ASAN on a loaded machine (no device-mode code touched by any stage), and its re-run was green; every stage's recorded run after it is green. If yours fails, re-run it ONCE and report both.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (1504408 assertions in 96 test cases)`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Then the per-stage tree rule, which the prototype adopted after a broken intermediate commit: the tree this task
commits is extracted from the commit AFTER the commit step (so it is the sealed tree), proven equal to it by a fresh
index read from the commit, installed, generated, typechecked and tested on its own. Nothing here links this
worktree's `build/`; the pure runner reads the tjs binary through `RETROPLUG_TJS` instead.

Run: `bash -c 'D=/tmp/f4c-tree-2; rm -rf "$D" && mkdir -p "$D" && git archive eb20634ea | tar -x -C "$D" && echo extracted'`
Expected: `extracted`: the commit's tree, with no generated module in it (they are gitignored and `git archive` does not carry them) and no `build/`.

Run: `bash -c 'D=/tmp/f4c-tree-2; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree eb20634ea && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-2-eq.txt; e=$?; rm -f $E; wc -l < /tmp/f4c-tree-2-eq.txt'`
Expected: `0`: the extracted tree is its commit's, line for line, read against a fresh index because this worktree's own index holds a later task's deletions. `deps` is excluded: its submodule directories are links, which is the next step's doing.

Run: `bash -c 'D=/tmp/f4c-tree-2; W=$PWD; for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done; cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo wired'`
Expected: `wired`: the archive's submodule paths are links to this worktree's checkouts (read only) and its own offline install wires the workspace's links from the COMMIT's lockfile, which is also what proves that lockfile installs.

Run: `bash -c 'D=/tmp/f4c-tree-2; T=/workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs; cd "$D" && pnpm -r typecheck && RETROPLUG_TJS=$T node packages/retroplug/scripts/run-tests.mjs menu && node packages/retroplug/scripts/run-tests.mjs packages && node packages/retroplug/scripts/run-tests.mjs hosts && node packages/retroplug/scripts/run-tests.mjs ui && node packages/retroplug/scripts/run-tests.mjs boundaries && node packages/retroplug/scripts/run-tests.mjs graph/extension'`
Expected: exit 0: every package typechecks against its own tsconfig, and each named suite passes on this tree alone, with the worktree's tjs binary read through `RETROPLUG_TJS` and nothing written to this worktree. The per-tree record, which this plan's generator ran for all five trees (each extract equal to its commit before and after, 19 `Done` lines, every suite exit 0): tree 1 `menu` 17 files, `packages` 2, `hosts` 2, `boundaries` 3, `ui-react/reactView` 1 and `script/bundle-ui-split` 1; tree 2 `menu` 17, `packages` 2, `hosts` 2, `ui` 4 and `boundaries` 3; tree 3 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `input` 3, `config` 6 and `boundaries` 3; tree 4 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `lsdj/ui-views` 1 and `risa/ui-views` 1; tree 5 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `boundaries` 2 and `source-literals` 1.

Run: `bash -c 'D=/tmp/f4c-tree-2; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree eb20634ea && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-2-eq2.txt; rm -f $E; wc -l < /tmp/f4c-tree-2-eq2.txt'`
Expected: `0`: the install, the generator and the suites changed no tracked file, so the tree is still its commit's after the runs (the generated modules and the runner's build dir are ignored).

- [ ] **Step 12: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/core/ts/host.ts
 M packages/core/ts/test/host.test.ts
 M packages/lsdj/ts/test/songImport.test.ts
 M packages/retroplug/scripts/lib/packages.mjs
 M packages/retroplug/src/graph/defineExtension.ts
 M packages/retroplug/src/hostGroups.ts
 M packages/retroplug/test/graph/extension.test.ts
 M packages/retroplug/test/hosts/host-groups.test.ts
 M packages/retroplug/test/packages/fixtures/expected/menus.ts
 M packages/retroplug/test/packages/fixtures/tree/package.json
 M packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json
 M packages/retroplug/test/packages/packages-generator.test.ts
 M packages/retroplug/test/packages/run-generator.mjs
 M packages/retroplug/ui/App.tsx
 M packages/retroplug/ui/host/menuContributions.ts
 M packages/retroplug/ui/host/uiHost.ts
 M packages/retroplug/ui/screens/grid/EmulatorTile.tsx
 M packages/retroplug/ui/screens/grid/SystemGrid.tsx
 M packages/ui-fixture/ts/ui/index.ts
 M packages/ui-react/ts/ui/reactView.ts
 M packages/ui/ts/contribution.ts
 M packages/ui/ts/menu/subscribable.ts
?? packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json
?? packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts
?? packages/retroplug/test/ui/view-session.test.ts
?? packages/retroplug/ui/views/ComponentView.tsx
?? packages/retroplug/ui/views/PixelView.tsx
?? packages/retroplug/ui/views/ReadoutView.tsx
?? packages/retroplug/ui/views/TileViews.tsx
?? packages/retroplug/ui/views/ViewWindow.tsx
?? packages/retroplug/ui/views/useViewSession.ts
?? packages/retroplug/ui/views/viewSession.ts
?? packages/ui/ts/test/signal.test.ts
````

````bash
git add -- packages/core/ts/host.ts packages/core/ts/test/host.test.ts packages/lsdj/ts/test/songImport.test.ts \
  packages/retroplug/scripts/lib/packages.mjs packages/retroplug/src/graph/defineExtension.ts \
  packages/retroplug/src/hostGroups.ts packages/retroplug/test/graph/extension.test.ts \
  packages/retroplug/test/hosts/host-groups.test.ts packages/retroplug/test/packages/fixtures/expected/menus.ts \
  packages/retroplug/test/packages/fixtures/tree/package.json \
  packages/retroplug/test/packages/fixtures/tree/packages/retroplug/package.json \
  packages/retroplug/test/packages/fixtures/tree/packages/zeta/package.json \
  packages/retroplug/test/packages/fixtures/tree/packages/zeta/ts/ui.ts \
  packages/retroplug/test/packages/packages-generator.test.ts packages/retroplug/test/packages/run-generator.mjs \
  packages/retroplug/test/ui/view-session.test.ts packages/retroplug/ui/App.tsx \
  packages/retroplug/ui/host/menuContributions.ts packages/retroplug/ui/host/uiHost.ts \
  packages/retroplug/ui/screens/grid/EmulatorTile.tsx packages/retroplug/ui/screens/grid/SystemGrid.tsx \
  packages/retroplug/ui/views/ComponentView.tsx packages/retroplug/ui/views/PixelView.tsx \
  packages/retroplug/ui/views/ReadoutView.tsx packages/retroplug/ui/views/TileViews.tsx \
  packages/retroplug/ui/views/ViewWindow.tsx packages/retroplug/ui/views/useViewSession.ts \
  packages/retroplug/ui/views/viewSession.ts packages/ui-fixture/ts/ui/index.ts packages/ui-react/ts/ui/reactView.ts \
  packages/ui/ts/contribution.ts packages/ui/ts/menu/subscribable.ts packages/ui/ts/test/signal.test.ts
git commit -F - <<'EOF'
feat(f4c): the view contract, its registry, and the shell's window and tile slots

@retroplug/ui/contribution gains the view contract: Readable/Signal (a value a package owns and the shell watches),
PixelFrame/PixelSession and ReadoutFrame/ReadoutSampler (what the shell draws itself), ViewComponentProps and ViewBody
(whose "component" kind is opaque here, with ComponentBody naming that one arm, so this package still imports no React),
and WindowView/TileView/ViewDef. A tile view's `visible` is a Readable, so a hidden overlay does no work; UiContribution
gains `views`. createSignal joins createSubscribable in @retroplug/ui/menu and is the one get/set/subscribe. reactView
returns the contract's ComponentBody, and ui-fixture declares its badge view through defineUi({ views }).

The host: FilesHost gains readPrefix, which filesHost implements once in src/hostGroups.ts, so the UI, CLI and render
builders all gain it (the two hand-written FilesHost literals in tests gain it too). SystemsHost gains readRam, wired in
the UI's systemsHost only; the render surface keeps its emulator.readRam and the CLI has no systems group.

The generated registry: ui/generated/menus.ts gains VIEWS in product order, and its load-time checks refuse a view id
declared twice (naming the package, or both) and a view whose placement or body kind is not one the shell has, or a
`visible` on a window view (the type refuses that; the data check backs it), and a view with no body at all (the same
check, rather than a TypeError from reading a field off nothing). Both refusals are RED first: seven new fixture modes
in run-generator.mjs run against the previous generator and fail there, the missing-body one on a bare TypeError. The
fixture product list gains a third package, zeta, named BEFORE alpha and beta and contributing no app of its own, so
the ui, views and cli order cases report the list's order and not an alphabetical sort of the same names.

The shell: ui/views/ holds viewSession.ts (the pure controller: open once per (id, system, romPath, roles) by identity,
reopen on a change, close on a hide or unmount, report a throwing open or close), useViewSession.ts (the one effect the
three renderers share, and the handle they read through), PixelView/ReadoutView/ComponentView, ViewWindow and TileViews.
App's hdSystemId becomes openView: host.views.open looks the id up in the registry, reports an unknown id, a tile view's
id or a system the view's `when` rejects, and opens nothing; Esc closes it, the stale-system clear is generic, and the
full-window branch renders the view through windowViewOf. The LSDj HD player is the shell's own screen, so "lsdj-hd" is
a shell-side entry in App (and uiViews keeps the menu tests' fallback host): both go when the cart registers the view.
EmulatorTile mounts one StableSlot per REGISTERED tile view, always, tagged view-slot-<id>, after the dim and before
the border, and still mounts today's two overlays as they are. SystemGrid passes the UI host down.

PixelView reads a session's frame only once there is a canvas to draw it in. The HD canvas's `frame()` is a
flush-and-diff (it reports just the tiles that changed since its last flush) and a session is opened an effect before
its canvas mounts, so asking for a frame first would mark the layer painted into nothing and the screen would stay
black. PixelSession.frame's doc states the guarantee the shell gives a session: it is asked for a frame only when it
has a canvas to put one in. No tier of this stage can show that bug - the pure suite has no React, and 8.6 rules out a
test view in the production bundle - so the stage 4 task's `lsdj-hd` test:ui case, which renders the real view, is what
pins it (it failed while the guard was missing).

What the renderer draws is the controller's state, not a render behind it: `undefined` means "not opened yet" and draws
nothing (so a supported cart never flashes the unsupported text on open, which is what the old hook's null did), a hide
or a gone system drops the session in the render that sees it, and a per-frame reader reaches the session the
controller holds NOW, so a frame never reads a session a hide or a reopen closed. Each tile slot subscribes to its own
`visible` for every body kind, so a hidden component view mounts nothing, its `when` is checked inside the slot rather
than around it, and a slot never mounts late over the focus border and the render badge.

Every call into a package's code is guarded: a throwing `when` reads as "no", a throwing `visible` as hidden, and a
throwing frame, sample or signature is reported once and drops the session; each is a pure case, with no React.

ViewDefinition, ViewPlacement, Contribution.views and Collected.views leave src/graph/defineExtension.ts with the one
test that read them; no production extension contributed one.

Tests: 22 new cases (12 viewSession over fake views and a fake host, 7 generator refusals and the VIEWS order, 2 for
createSignal, 1 for readPrefix over the three builders and the UI's readRam), plus the stage 1 review's type case. This
tree's pure suite is 223 files / 2322 cases, 0 skipped; test:ui is unchanged (43 files, 49 cases, the three view tests
included); the menu matrix is unchanged; check:boundaries and check:docs are clean. The UI bundle grows 1997971 ->
2011773 bytes (the renderers, App's slot and the guards); the cp, cli and render-worker bundles each grow 70 to 72 bytes
from filesHost's readPrefix wrapper.

Decisions: the effect every renderer shares is its own file (useViewSession.ts) rather than three copies of it, since
viewSession.ts must stay React-free for the pure suite; the UI host reaches EmulatorTile as a prop (App -> SystemGrid ->
EmulatorTile), as the shell already passes stores down; the "click passes through the slot" case cannot be a test:ui
case while no product view is registered (the fixture pack is in no product, as stage 1 asserts), and the existing
focus-by-click case passes unchanged over the same transparent-Box shape the dim layer already proves.

SystemsStore.setRoleConfig slices the roles array on every write, so any role write (even one storing an equal config)
reopens that system's views, which is the design's E5 exactly.
EOF
````

---

### Task 3: app actions, contributed bindings, and the shell's dispatch

Design sections 3.2, 3.5, 3.6 and 5.5 to 5.7, and the design's third stage. A package contributes an app action: an id
that names its owner, a label, default key and pad names, and a `run`. The binding profiles carry the contributed ids,
the generator writes `ACTIONS` and refuses what a stored profile could not survive, and the shell routes one press to
every action bound to it, running the shell's own as it did and each package's in its own `try`. The bindings menus gain
a capture row per action, which is E1's row: this stage proves the rows over a TEST action, because no product carries
one yet, and the two real actions arrive in Task 4 with the matrix's record.

The stage also carries the stage 2 review's fix pass and the stage 3 review's: the session hook's three-state read and
the guards (viewSession.ts, useViewSession.ts and their cases), and the key maps' one shell action per key. The backtick
stays App's own line until Task 4, so the product's behaviour does not move: the menus, the matrix and `test:ui` are
unchanged.

**Files:**

- Create (5): `packages/retroplug/test/input/app-actions.test.ts`, `packages/retroplug/test/menu/action-rows.test.ts`, `packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts`, `packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts`, `packages/retroplug/ui/input/appActions.ts`
- Modify (17 files, as one diff block per group its step names: 2 under `packages/retroplug/src/`; 2 under `packages/retroplug/test/input/`; 2 under `packages/retroplug/test/packages/`; 2 under `packages/retroplug/ui/views/`; 1 under `packages/retroplug/scripts/lib/`; 1 under `packages/retroplug/test/config/` and the rest)

- [ ] **Step 1: The action contract, and the binding vocabulary**

`ts/contribution.ts` gains `UiAction { id, label, keys?, pad?, run(host, ctx: { systemId }) }` and
`UiContribution.actions`. An id is `<package>.<name>` because a profile stores it, so the generated registry can check
that it names its owner; `ctx.systemId` is the focused system, `null` on the start screen. `src/keyCodes.ts` gains
`GAMEPAD_BUTTON_NAMES`, SDL's 21 canonical button names as the native `GamepadManager` emits them
(`SDL_CONTROLLER_BUTTON_*`), with the stick half-axis tokens left out on purpose: the `gamepad-axis` bus dispatches no
app action, so a default on one could never fire. `buildKeyToAction` and `buildGamepadToAction` answer a LIST of ids
per key or button, in the profile's order, and both go through one `addActionId`: a key keeps at most ONE of the
shell's three ids, a later shell binding replacing the earlier in place exactly as the old `Map.set` did, a
contributed id accumulates beside it, and an id the list already holds is skipped. `APP_ACTIONS` stays module-private
and `isAppAction` is exported. `src/bindingMap.ts` gains `withActionDefaults(map, actions)`: every contributed id
absent from a section takes its default (`keys ?? []`, `pad ?? []`), an id PRESENT is the user's (`[]` included), and
it always returns a new object so App can memoise it on the profile.

````diff
diff --git a/packages/retroplug/src/bindingMap.ts b/packages/retroplug/src/bindingMap.ts
index be859ffca..aaf749f7a 100644
--- a/packages/retroplug/src/bindingMap.ts
+++ b/packages/retroplug/src/bindingMap.ts
@@ -45,6 +45,32 @@ export const bindingMapSchema = z.object({
   gamepadActions: actionChannel(DEFAULT_GAMEPAD_ACTIONS),
 });
 
+/** A contributed action's defaults: the two channels' own names for what a UI contribution declares (src/ knows no
+ *  contribution, so this is the shape it reads, and @retroplug/ui's UiAction is assignable to it). */
+export interface ActionDefaults {
+  readonly id: string;
+  readonly keys?: readonly string[];
+  readonly pad?: readonly string[];
+}
+
+/** Fill a stored profile's two app-action sections with the contributed actions' defaults
+ *  (docs/modular-graph-phase-f4c-design.md section 5.6). A stored profile has no entry for a contributed id until the
+ *  user edits one, and an id PRESENT in a section is the user's, `[]` included: a cleared binding is a choice, as
+ *  CyclePrev's empty default is.
+ *
+ *  A pure function at the two readers (App builds its maps through it, the bindings menus read theirs through it), not
+ *  a store change: AppStores is shared by the control plane and the UI, the contributions are UI-only, and the file
+ *  on disk is not rewritten until the user edits a binding (the menus load raw and save only what was edited). */
+export function withActionDefaults(map: BindingMap, actions: readonly ActionDefaults[]): BindingMap {
+  const keyboardActions = { ...map.keyboardActions };
+  const gamepadActions = { ...map.gamepadActions };
+  for (const action of actions) {
+    if (!(action.id in keyboardActions)) keyboardActions[action.id] = [...(action.keys ?? [])];
+    if (!(action.id in gamepadActions)) gamepadActions[action.id] = [...(action.pad ?? [])];
+  }
+  return { ...map, keyboardActions, gamepadActions };
+}
+
 /** The built-in "default" profile — the seed for bindings/default.json and the fallback when an active
  *  profile is absent. Keyboard mirrors native's defaultBindingMap(); the gamepad channel additionally binds
  *  the LEFT STICK alongside the d-pad hat (the `<axis><sign>` tokens — TS-only, native binds only the
diff --git a/packages/retroplug/src/keyCodes.ts b/packages/retroplug/src/keyCodes.ts
index a536e6379..68155785e 100644
--- a/packages/retroplug/src/keyCodes.ts
+++ b/packages/retroplug/src/keyCodes.ts
@@ -128,36 +128,77 @@ export function buildGamepadToButton(gamepad: Record<string, string[]>): Map<str
 
 /** An app-level (non-GB) action a key or controller button can be bound to: open/close the menu, and cycle
  *  which instance is focused (forward / back). These live in the separate keyboardActions/gamepadActions
- *  binding sections, so they never collide with the GB-button resolvers above. */
+ *  binding sections, so they never collide with the GB-button resolvers above. A package contributes more of
+ *  them (docs/modular-graph-phase-f4c-design.md section 3.2); the shell's three keep their bare ids. */
 export type AppAction = "OpenMenu" | "CycleNext" | "CyclePrev";
 const APP_ACTIONS: readonly AppAction[] = ["OpenMenu", "CycleNext", "CyclePrev"];
-const isAppAction = (id: string): id is AppAction => (APP_ACTIONS as readonly string[]).includes(id);
 
-/** Invert a keyboard app-action map (AppAction id → key-name[]) into a DPF-code → action lookup — the
- *  app-action twin of buildKeyToButton. Null-tolerant (an older profile may lack the section); unknown action
- *  ids and unresolvable key names are skipped; a later binding for the same code wins. */
-export function buildKeyToAction(actions: Record<string, string[]> | undefined): Map<number, AppAction> {
-  const map = new Map<number, AppAction>();
+/** Whether an id is one of the shell's three app actions (not a contribution's, which carries a package prefix). */
+export function isAppAction(id: string): id is AppAction {
+  return (APP_ACTIONS as readonly string[]).includes(id);
+}
+
+/** Every name the native GamepadManager hands the "gamepad-button" bus: SDL's canonical button names, from
+ *  SDL_GameControllerGetStringForButton over SDL_CONTROLLER_BUTTON_* in order (GamepadManager.cpp is the one emitter,
+ *  so this is SDL's set and not ours). A stick half-axis token ("leftx+") is NOT a button: the axis bus dispatches no
+ *  app action, so a default on one could never fire, which is why the set excludes it. */
+export const GAMEPAD_BUTTON_NAMES: readonly string[] = [
+  "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
+  "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
+];
+
+/** Add one action id to a key's list: a key keeps at most ONE of the shell's own ids, a later shell binding
+ *  replacing the earlier in place (what the old Map.set did, so a key two shell actions share fires what it fires
+ *  today), while contributed ids accumulate, and an id the list already holds is skipped (a hand-edited
+ *  `OpenMenu: ["Enter", "Return"]` is one code once). */
+function addActionId<K>(map: Map<K, string[]>, key: K, id: string): void {
+  const ids = map.get(key);
+  if (ids === undefined) {
+    map.set(key, [id]);
+    return;
+  }
+  if (ids.includes(id)) return;
+  if (!isAppAction(id)) {
+    ids.push(id);
+    return;
+  }
+  const at = ids.findIndex((held) => isAppAction(held));
+  if (at === -1) ids.push(id);
+  else ids[at] = id;
+}
+
+/** Invert a keyboard app-action map (action id → key-name[]) into a DPF-code → action-id LIST, the app-action twin
+ *  of buildKeyToButton. A key can carry several actions (the shell's and a package's, or two packages'), and the list
+ *  keeps the order the profile gives them, since a press fires all of them. Null-tolerant (an older profile may lack
+ *  the section); an id that is neither the shell's nor in `contributed` is skipped, as an unresolvable key name is. */
+export function buildKeyToAction(
+  actions: Record<string, string[]> | undefined,
+  contributed: readonly string[] = [],
+): Map<number, string[]> {
+  const map = new Map<number, string[]>();
   if (!actions) return map;
   for (const [id, keyNames] of Object.entries(actions)) {
-    if (!isAppAction(id)) continue;
+    if (!isAppAction(id) && !contributed.includes(id)) continue;
     for (const keyName of keyNames) {
       const code = resolveKeyName(keyName);
-      if (code !== null) map.set(code, id);
+      if (code === null) continue;
+      addActionId(map, code, id);
     }
   }
   return map;
 }
 
-/** The gamepad twin: SDL-button-name → action (the names ARE the raw "gamepad-button" bus names, so no code
- *  table, like buildGamepadToButton). Null-tolerant; unknown action ids skipped; a later binding for the same
- *  name wins. */
-export function buildGamepadToAction(actions: Record<string, string[]> | undefined): Map<string, AppAction> {
-  const map = new Map<string, AppAction>();
+/** The gamepad twin: SDL-button-name → action-id list (the names ARE the raw "gamepad-button" bus names, so no code
+ *  table, like buildGamepadToButton). Null-tolerant; the same "known" rule as the keyboard map. */
+export function buildGamepadToAction(
+  actions: Record<string, string[]> | undefined,
+  contributed: readonly string[] = [],
+): Map<string, string[]> {
+  const map = new Map<string, string[]>();
   if (!actions) return map;
   for (const [id, padNames] of Object.entries(actions)) {
-    if (!isAppAction(id)) continue;
-    for (const padName of padNames) map.set(padName, id);
+    if (!isAppAction(id) && !contributed.includes(id)) continue;
+    for (const padName of padNames) addActionId(map, padName, id);
   }
   return map;
 }
diff --git a/packages/ui/ts/contribution.ts b/packages/ui/ts/contribution.ts
index f16b9f6bb..a58c3dd2a 100644
--- a/packages/ui/ts/contribution.ts
+++ b/packages/ui/ts/contribution.ts
@@ -1,8 +1,9 @@
 // What a package's UI module declares (docs/modular-graph-phase-f4b-design.md section 4.2): the system-settings rows it
 // adds as DATA (a role-config knob, its choices and its gate), the menu fragments a function builds where data cannot
-// say it (a state machine over a native host, a peer count), its tracker specs, and the services its device needs
-// polled or subscribed to. A manifest's `retroplug.ui` member names the module, which default-exports
-// defineUi({ ... }); the generator concatenates them in product order into packages/retroplug/ui/generated/menus.ts.
+// say it (a state machine over a native host, a peer count), its tracker specs, the services its device needs polled or
+// subscribed to, its views, and its app actions. A manifest's `retroplug.ui` member names the module, which
+// default-exports defineUi({ ... }); the generator concatenates them in product order into
+// packages/retroplug/ui/generated/menus.ts.
 //
 // Everything here is plain TypeScript: the contract a package's module is checked against, and the helpers that read a
 // row's stored value the way the shell renders it. The React kit is @retroplug/ui-react, a package of its own, so a
@@ -242,6 +243,35 @@ export interface UiService {
   poll?: { readonly frames: number; run(host: UiHost): boolean };
 }
 
+/** An app action a package contributes: a rebindable key or gamepad press, carried by the binding profiles and shown in
+ *  the bindings menus beside the shell's own three. The shell dispatches it in the states the shell's app actions fire
+ *  in (no full-window overlay), once per matching press, and reports a throw rather than letting it stop the others.
+ *  Not persisted as a contribution: the profile stores the id, and an id absent from a stored profile reads the
+ *  default here (src/bindingMap.ts `withActionDefaults`), so adding an action needs no migration. */
+export interface UiAction {
+  /** "<package>.<name>", the manifest's package name and a dot: the profile stores it, so it names its owner, and the
+   *  generated registry refuses an id its own package does not prefix. */
+  readonly id: string;
+  /** The bindings menus' row text ("LSDj Debug Overlay"); the row shows it, then the bound names. */
+  readonly label: string;
+  /** Default keyboard bindings, by the names the profiles store ("`", "F"). A key the shell already binds by default
+   *  (a shell action's or a Game Boy button's) is refused at load, since one press would fire both. */
+  readonly keys?: readonly string[];
+  /** Default gamepad bindings, by SDL's button names ("leftshoulder", "leftstick"); absent = unbound, which the
+   *  gamepad bindings menu shows as "-". */
+  readonly pad?: readonly string[];
+  /** Run the action. `systemId` is the focused system, null on the start screen. */
+  run(host: UiHost, ctx: { readonly systemId: number | null }): void;
+}
+
+/** A contributed action with the package it came from: the generated registry's shape (`ACTIONS`), which the shell's
+ *  dispatch, the key maps' "known" set and the bindings menus all read. One declaration, so a consumer cannot drift
+ *  from the generated list. */
+export interface ContributedAction {
+  readonly pkg: string;
+  readonly action: UiAction;
+}
+
 /** Everything a package's UI module contributes. */
 export interface UiContribution {
   /** Role-config rows, which the shell renders in the System submenu for every system whose roles they name. */
@@ -252,6 +282,8 @@ export interface UiContribution {
   /** The window and tile views the package draws (declarative: the shell renders pixels and readout, and mounts a
    *  component the package built with @retroplug/ui-react). */
   readonly views?: readonly ViewDef[];
+  /** The app actions the package contributes, which the key and gamepad maps dispatch and the bindings menus list. */
+  readonly actions?: readonly UiAction[];
 }
 
 /** The identity function with types: a UI module's default export, checked against what a package may contribute. */
````

Run: `sed -n '5957,6153p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/src/bindingMap.ts
packages/retroplug/src/keyCodes.ts
packages/ui/ts/contribution.ts
EOF
````

Expected:

````text
aaf749f7a8ded7aead74c25f7dc824947f6e1e80
68155785e39d392fd9edcf80f4a7aade7116410b
a58c3dd2a2de6dd064bb3ebbfe91144dac931f9a
````

- [ ] **Step 2: The generated `ACTIONS`, and the nine refusals**

`renderMenus` gains `ACTIONS` in product order beside `VIEWS` and `UI_CONTRIBUTIONS`, and the load-time checks, each
naming the package or both: an action id declared twice; an id its own package does not prefix; a default key name
`resolveKeyName` does not resolve; a default pad name outside `GAMEPAD_BUTTON_NAMES`; and a default colliding with one
the shell already binds, keyboard AND gamepad, compared by RESOLVED DPF CODE rather than spelling (so `Return`
collides with the Game Boy Start default `Enter`, both 0x0d, and `z` with Z), since a key on both would fire the
action AND the joypad. Two contributions on one key are allowed and pinned by a positive fixture case: the two
overlays' backtick is one. The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES`, the two default
tables and `defaultBindingMap` from `../../src/`; the fixture tree gains two one-line shims that re-export the real
shell's `keyCodes` and `bindingMap`, aliased by the driver, so the checks read the real vocabulary and defaults rather
than a copy that could drift.

Create `packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts`:

````ts
// The fixture shell's binding vocabulary, re-exported from the real shell's: the generated menus module imports
// ../../src/keyCodes for the key-name resolver and SDL's button names, and a check held to a copy of that table would
// only prove the copy. The driver aliases the bare specifier to the worktree's own file.
export * from "@retroplug/shell/keyCodes";
````

Run: `mkdir -p packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src; sed -n '6193,6196p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts`
Expected: `3a9570a735450239f344e9035a4b641904ae47a3`: the blob c956580c4 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts`:

````ts
// The fixture shell's profile model, re-exported from the real shell's: the generated menus module imports
// ../../src/bindingMap for the defaults a contributed action may not collide with.
export * from "@retroplug/shell/bindingMap";
````

Run: `mkdir -p packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src; sed -n '6208,6210p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts`
Expected: `fcdbc9976464187e192c2a5528e81cc4fd75f543`: the blob c956580c4 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/lib/packages.mjs b/packages/retroplug/scripts/lib/packages.mjs
index 62dbb9268..7ae69c645 100644
--- a/packages/retroplug/scripts/lib/packages.mjs
+++ b/packages/retroplug/scripts/lib/packages.mjs
@@ -765,7 +765,8 @@ function renderMenus({ packages, outDir }) {
     "// system-settings rows, menu builders, tracker specs and services it adds. The shell composes the menus from this",
     "// list, so a package's place in `retroplug.products.desktop` is where its rows and entries land. Only ui/ imports",
     "// this module: the control plane, the render worker, the SDK and the planner carry no menu contribution.",
-    'import { PLACEMENTS, type RoleRow, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";',
+    'import { PLACEMENTS, type ContributedAction, type RoleRow, type UiAction, type UiContribution, type ViewDef } ' +
+      'from "@retroplug/ui/contribution";',
   ];
   for (const p of contributors) {
     lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.ui)}";`);
@@ -774,6 +775,12 @@ function renderMenus({ packages, outDir }) {
   // registers, or over a key its role's schema does not read, would render nowhere and do nothing, and a tracker UI no
   // integration matches would never be found - typos the compiler cannot see in a data literal.
   lines.push('import { ROLES, TRACKER_INTEGRATIONS } from "../../src/generated/packages";');
+  // The binding vocabulary an action's default is checked against (the key names the profiles resolve, SDL's pad
+  // names, and the defaults the shell and the Game Boy buttons already take), all of it the shell's own modules.
+  lines.push('import { GAMEPAD_BUTTON_NAMES, resolveKeyName } from "../../src/keyCodes";');
+  lines.push(
+    'import { DEFAULT_GAMEPAD_ACTIONS, DEFAULT_KEYBOARD_ACTIONS, defaultBindingMap } from "../../src/bindingMap";',
+  );
   lines.push("");
   lines.push("/** Each package's UI contribution, in product order. */");
   lines.push("export const UI_CONTRIBUTIONS: readonly { pkg: string; ui: UiContribution }[] = [");
@@ -788,6 +795,12 @@ function renderMenus({ packages, outDir }) {
   lines.push("  (ui.views ?? []).map((view) => ({ pkg, view })),");
   lines.push(");");
   lines.push("");
+  lines.push("/** The product's app actions, in product order, with the package each came from: the bindings menus'");
+  lines.push(" *  rows, the key and gamepad maps' known ids, and the dispatch's lookups. */");
+  lines.push("export const ACTIONS: readonly ContributedAction[] = UI_CONTRIBUTIONS.flatMap(({ pkg, ui }) =>");
+  lines.push("  (ui.actions ?? []).map((action) => ({ pkg, action })),");
+  lines.push(");");
+  lines.push("");
   lines.push("// A row's id has to be unique inside its placement: the menu is keyed by id, so a second row of one id would");
   lines.push("// take the first one's place (a system-settings row lands in the System submenu, a tracker's extras in its own");
   lines.push("// submenu).");
@@ -819,6 +832,26 @@ function renderMenus({ packages, outDir }) {
   lines.push("const viewOwners = new Map<string, string>();");
   lines.push('const viewPlacements = new Set<string>(["window", "tile"]);');
   lines.push('const bodyKinds = new Set<string>(["pixels", "readout", "component"]);');
+  lines.push("// An action's id is what a binding profile stores, so it is unique across the product and names its");
+  lines.push("// package. Its defaults are held to the names the shell binds and to the ones it already binds at a");
+  lines.push("// default: a key on both would fire the action AND the Game Boy button (or the shell action) it is on.");
+  lines.push("const actionOwners = new Map<string, string>();");
+  lines.push("const padButtonNames = new Set<string>(GAMEPAD_BUTTON_NAMES);");
+  lines.push("const defaultMap = defaultBindingMap();");
+  lines.push("const takenKeyCodes = new Set<number>();");
+  lines.push("for (const names of Object.values(DEFAULT_KEYBOARD_ACTIONS)) {");
+  lines.push("  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);");
+  lines.push("}");
+  lines.push("for (const names of Object.values(defaultMap.keyboard)) {");
+  lines.push("  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);");
+  lines.push("}");
+  lines.push("const takenPadNames = new Set<string>();");
+  lines.push("for (const names of Object.values(DEFAULT_GAMEPAD_ACTIONS)) {");
+  lines.push("  for (const name of names) takenPadNames.add(name);");
+  lines.push("}");
+  lines.push("for (const names of Object.values(defaultMap.gamepad)) {");
+  lines.push("  for (const name of names) takenPadNames.add(name);");
+  lines.push("}");
   lines.push("");
   lines.push("/** One contributed row: its role must be registered, its id unique in its placement, and its key (and its");
   lines.push(" *  gate's key) one its role's schema reads. */");
@@ -868,9 +901,49 @@ function renderMenus({ packages, outDir }) {
   lines.push("  }");
   lines.push("}");
   lines.push("");
+  lines.push("/** One contributed action: its id unique across the product and prefixed by its own package's name,");
+  lines.push(" *  its default key and pad names ones the shell resolves, and no default the shell (or a Game Boy");
+  lines.push(" *  button) already binds, since one press would fire both.");
+  lines.push(" *  Two contributions on one key are allowed: the overlays' backtick. */");
+  lines.push("function checkAction(pkg: string, action: UiAction): void {");
+  lines.push("  const owner = actionOwners.get(action.id);");
+  lines.push("  if (owner !== undefined) {");
+  lines.push("    throw new Error(owner === pkg");
+  lines.push("      ? `the package ${pkg} declares the action ${action.id} twice`");
+  lines.push("      : `two packages declare the action ${action.id}: ${owner} and ${pkg}`);");
+  lines.push("  }");
+  lines.push("  actionOwners.set(action.id, pkg);");
+  lines.push("  if (!action.id.startsWith(`${pkg}.`)) {");
+  lines.push("    throw new Error(`the package ${pkg} declares the action ${action.id}, which is not prefixed ` +");
+  lines.push('      `by its own name ("${pkg}.")`);');
+  lines.push("  }");
+  lines.push("  for (const name of action.keys ?? []) {");
+  lines.push("    const code = resolveKeyName(name);");
+  lines.push("    if (code === null) {");
+  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +");
+  lines.push('        `${name}, which is no key name`);');
+  lines.push("    }");
+  lines.push("    if (takenKeyCodes.has(code)) {");
+  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +");
+  lines.push('        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);');
+  lines.push("    }");
+  lines.push("  }");
+  lines.push("  for (const name of action.pad ?? []) {");
+  lines.push("    if (!padButtonNames.has(name)) {");
+  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +");
+  lines.push('        `${name}, which is no SDL button name (the names are GAMEPAD_BUTTON_NAMES)`);');
+  lines.push("    }");
+  lines.push("    if (takenPadNames.has(name)) {");
+  lines.push("      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +");
+  lines.push('        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);');
+  lines.push("    }");
+  lines.push("  }");
+  lines.push("}");
+  lines.push("");
   lines.push("for (const { pkg, ui } of UI_CONTRIBUTIONS) {");
   lines.push("  for (const row of ui.rows ?? []) checkRow(\"system.settings\", pkg, row);");
   lines.push("  for (const view of ui.views ?? []) checkView(pkg, view);");
+  lines.push("  for (const action of ui.actions ?? []) checkAction(pkg, action);");
   lines.push("  for (const tracker of ui.trackers ?? []) {");
   lines.push("    if (!trackerIds.has(tracker.id)) {");
   lines.push(
diff --git a/packages/retroplug/test/packages/fixtures/expected/menus.ts b/packages/retroplug/test/packages/fixtures/expected/menus.ts
index 6a6984704..b9c6ce548 100644
--- a/packages/retroplug/test/packages/fixtures/expected/menus.ts
+++ b/packages/retroplug/test/packages/fixtures/expected/menus.ts
@@ -5,10 +5,12 @@
 // system-settings rows, menu builders, tracker specs and services it adds. The shell composes the menus from this
 // list, so a package's place in `retroplug.products.desktop` is where its rows and entries land. Only ui/ imports
 // this module: the control plane, the render worker, the SDK and the planner carry no menu contribution.
-import { PLACEMENTS, type RoleRow, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";
+import { PLACEMENTS, type ContributedAction, type RoleRow, type UiAction, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";
 import ui_zeta from "@retroplug/zeta/ui";
 import ui_alpha from "@retroplug/alpha/ui";
 import { ROLES, TRACKER_INTEGRATIONS } from "../../src/generated/packages";
+import { GAMEPAD_BUTTON_NAMES, resolveKeyName } from "../../src/keyCodes";
+import { DEFAULT_GAMEPAD_ACTIONS, DEFAULT_KEYBOARD_ACTIONS, defaultBindingMap } from "../../src/bindingMap";
 
 /** Each package's UI contribution, in product order. */
 export const UI_CONTRIBUTIONS: readonly { pkg: string; ui: UiContribution }[] = [
@@ -22,6 +24,12 @@ export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS
   (ui.views ?? []).map((view) => ({ pkg, view })),
 );
 
+/** The product's app actions, in product order, with the package each came from: the bindings menus'
+ *  rows, the key and gamepad maps' known ids, and the dispatch's lookups. */
+export const ACTIONS: readonly ContributedAction[] = UI_CONTRIBUTIONS.flatMap(({ pkg, ui }) =>
+  (ui.actions ?? []).map((action) => ({ pkg, action })),
+);
+
 // A row's id has to be unique inside its placement: the menu is keyed by id, so a second row of one id would
 // take the first one's place (a system-settings row lands in the System submenu, a tracker's extras in its own
 // submenu).
@@ -51,6 +59,26 @@ const placements = new Set<string>(PLACEMENTS);
 const viewOwners = new Map<string, string>();
 const viewPlacements = new Set<string>(["window", "tile"]);
 const bodyKinds = new Set<string>(["pixels", "readout", "component"]);
+// An action's id is what a binding profile stores, so it is unique across the product and names its
+// package. Its defaults are held to the names the shell binds and to the ones it already binds at a
+// default: a key on both would fire the action AND the Game Boy button (or the shell action) it is on.
+const actionOwners = new Map<string, string>();
+const padButtonNames = new Set<string>(GAMEPAD_BUTTON_NAMES);
+const defaultMap = defaultBindingMap();
+const takenKeyCodes = new Set<number>();
+for (const names of Object.values(DEFAULT_KEYBOARD_ACTIONS)) {
+  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);
+}
+for (const names of Object.values(defaultMap.keyboard)) {
+  for (const name of names) takenKeyCodes.add(resolveKeyName(name)!);
+}
+const takenPadNames = new Set<string>();
+for (const names of Object.values(DEFAULT_GAMEPAD_ACTIONS)) {
+  for (const name of names) takenPadNames.add(name);
+}
+for (const names of Object.values(defaultMap.gamepad)) {
+  for (const name of names) takenPadNames.add(name);
+}
 
 /** One contributed row: its role must be registered, its id unique in its placement, and its key (and its
  *  gate's key) one its role's schema reads. */
@@ -96,9 +124,49 @@ function checkView(pkg: string, view: ViewDef): void {
   }
 }
 
+/** One contributed action: its id unique across the product and prefixed by its own package's name,
+ *  its default key and pad names ones the shell resolves, and no default the shell (or a Game Boy
+ *  button) already binds, since one press would fire both.
+ *  Two contributions on one key are allowed: the overlays' backtick. */
+function checkAction(pkg: string, action: UiAction): void {
+  const owner = actionOwners.get(action.id);
+  if (owner !== undefined) {
+    throw new Error(owner === pkg
+      ? `the package ${pkg} declares the action ${action.id} twice`
+      : `two packages declare the action ${action.id}: ${owner} and ${pkg}`);
+  }
+  actionOwners.set(action.id, pkg);
+  if (!action.id.startsWith(`${pkg}.`)) {
+    throw new Error(`the package ${pkg} declares the action ${action.id}, which is not prefixed ` +
+      `by its own name ("${pkg}.")`);
+  }
+  for (const name of action.keys ?? []) {
+    const code = resolveKeyName(name);
+    if (code === null) {
+      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
+        `${name}, which is no key name`);
+    }
+    if (takenKeyCodes.has(code)) {
+      throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
+        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
+    }
+  }
+  for (const name of action.pad ?? []) {
+    if (!padButtonNames.has(name)) {
+      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
+        `${name}, which is no SDL button name (the names are GAMEPAD_BUTTON_NAMES)`);
+    }
+    if (takenPadNames.has(name)) {
+      throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
+        `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
+    }
+  }
+}
+
 for (const { pkg, ui } of UI_CONTRIBUTIONS) {
   for (const row of ui.rows ?? []) checkRow("system.settings", pkg, row);
   for (const view of ui.views ?? []) checkView(pkg, view);
+  for (const action of ui.actions ?? []) checkAction(pkg, action);
   for (const tracker of ui.trackers ?? []) {
     if (!trackerIds.has(tracker.id)) {
       throw new Error(`the package ${pkg} declares a tracker UI for ${tracker.id}, which no registered ` +
diff --git a/packages/retroplug/test/packages/packages-generator.test.ts b/packages/retroplug/test/packages/packages-generator.test.ts
index 91122d850..8282a42dc 100644
--- a/packages/retroplug/test/packages/packages-generator.test.ts
+++ b/packages/retroplug/test/packages/packages-generator.test.ts
@@ -13,7 +13,9 @@
 //   with no nodes, a sync override whose key, node type or unused off-mode is wrong, two contributions whose generated
 //   identifiers mangle to one name, a label below 16, a gap, a role naming an undeclared or an unwired type, a named
 //   module that does not exist, a contribution its exports do not publish, a manifest whose name is not its
-//   directory's, a contribution outside ts/ and a contributing package the shell does not declare. A dependency that
+//   directory's, a contribution outside ts/ and a contributing package the shell does not declare, an action id
+//   twice (one package or two), an action id its package does not prefix, and an action default on a key or pad
+//   name the shell does not resolve or already binds. A dependency that
 //   contributes nothing, and one whose manifest has no retroplug member, are left out of the ordering rather than
 //   refused; a package contributing an app and no nodes names no nodes module and still registers its app; a tsconfig
 //   that must extend the generated host-module paths because it lists an example session or declares a host module the
@@ -248,6 +250,91 @@ test("the generated menus module refuses a visible flag on a window view", async
     "view has");
 });
 
+test("the generated menus module carries the packages' actions in product order", async () => {
+  // One action per package, in the product list's order (zeta first); zeta's and alpha's share a backtick, which two
+  // contributions may, and beta's has a pad default and no key.
+  const { actions, error } = await drive("actions");
+  if (error) console.log(`# actions: ${error}`);
+  expect(actions).toEqual([
+    { pkg: "zeta", id: "zeta.first", label: "Zeta First", keys: ["`"], pad: [] },
+    { pkg: "alpha", id: "alpha.second", label: "Alpha Second", keys: ["`"], pad: [] },
+    { pkg: "beta", id: "beta.third", label: "Beta Third", keys: [], pad: ["guide"] },
+  ]);
+});
+
+test("the generated menus module refuses an action id one package declares twice", async () => {
+  const { error } = await drive("actions-duplicate-id");
+  console.log(`# duplicate action id: ${error}`);
+  expect(error).toBe("the package alpha declares the action alpha.same twice");
+});
+
+test("the generated menus module refuses an action id two packages declare", async () => {
+  const { error } = await drive("actions-duplicate-across");
+  console.log(`# action id across packages: ${error}`);
+  expect(error).toBe("two packages declare the action alpha.same: alpha and beta");
+});
+
+test("the generated menus module refuses an action id its own package does not prefix", async () => {
+  const { error } = await drive("actions-bad-prefix");
+  console.log(`# action id prefix: ${error}`);
+  expect(error).toBe(
+    'the package alpha declares the action zeta.stolen, which is not prefixed by its own name ("alpha.")',
+  );
+});
+
+test("the generated menus module refuses an action default key name no key resolves", async () => {
+  const { error } = await drive("actions-unknown-key");
+  console.log(`# action key name: ${error}`);
+  expect(error).toBe("the package alpha declares the action alpha.key on the key Nonsense, which is no key name");
+});
+
+test("the generated menus module refuses an action default pad name outside SDL's button names", async () => {
+  const { error } = await drive("actions-unknown-pad");
+  console.log(`# action pad name: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the action alpha.pad on the gamepad button nonsense, which is no SDL button name " +
+      "(the names are GAMEPAD_BUTTON_NAMES)",
+  );
+});
+
+test("the generated menus module refuses an action default on a key a shell action already binds", async () => {
+  const { error } = await drive("actions-shell-key");
+  console.log(`# action on a shell key: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the action alpha.menu on the key Escape by default, which the shell already binds " +
+      "(a shell action or a Game Boy button)",
+  );
+});
+
+test("the generated menus module refuses an action default on a Game Boy button's keyboard key", async () => {
+  const { error } = await drive("actions-gb-key");
+  console.log(`# action on a GB key: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the action alpha.jump on the key Z by default, which the shell already binds " +
+      "(a shell action or a Game Boy button)",
+  );
+});
+
+test("the generated menus module refuses an action default on a key code a Game Boy button already binds", async () => {
+  // "Return" is a different name from the Start default "Enter" and the same code (0x0d): the check compares resolved
+  // codes, which is what one press sees.
+  const { error } = await drive("actions-return-key");
+  console.log(`# action on a GB key code: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the action alpha.enter on the key Return by default, which the shell already binds " +
+      "(a shell action or a Game Boy button)",
+  );
+});
+
+test("the generated menus module refuses an action default on a Game Boy button's gamepad button", async () => {
+  const { error } = await drive("actions-gb-pad");
+  console.log(`# action on a GB pad button: ${error}`);
+  expect(error).toBe(
+    "the package alpha declares the action alpha.jump on the gamepad button a by default, which the shell already " +
+      "binds (a shell action or a Game Boy button)",
+  );
+});
+
 test("a retroplug.ui member naming a module that is not there is refused", async () => {
   const { error } = await drive("ui-missing-module");
   console.log(`# missing ui module: ${error}`);
diff --git a/packages/retroplug/test/packages/run-generator.mjs b/packages/retroplug/test/packages/run-generator.mjs
index 35fd54cbe..1228a380f 100644
--- a/packages/retroplug/test/packages/run-generator.mjs
+++ b/packages/retroplug/test/packages/run-generator.mjs
@@ -35,7 +35,9 @@
 //   cli | cli-duplicate-tool | cli-missing-module | examples-outside | examples-tsconfig-missing |
 //   examples-tsconfig-unextended | examples-tsconfig-extended | examples-duplicate-name | examples-shell-name |
 //   examples-unsafe-name | host-unknown | host-known | react-tsconfig-missing | react-tsconfig-unextended |
-//   react-tsconfig-extended
+//   react-tsconfig-extended | actions | actions-duplicate-id | actions-duplicate-across | actions-bad-prefix |
+//   actions-unknown-key | actions-unknown-pad | actions-shell-key | actions-gb-key | actions-return-key |
+//   actions-gb-pad
 //                   the menus module: ui reports the contributing packages in product order, the others load it into
 //                   its own refusal ({ error })
 //                   (unlisted-plain generates, and reports files like generate)
@@ -172,53 +174,81 @@ function uiApp(name) {
   );
 }
 
-/** The generated menus module, bundled and loaded as the UI loads it: its imports of the packages' ui modules are
- *  aliased the way a pnpm link resolves them, and loading it runs its checks (a row's role kind, a row id twice, a
- *  builder's placement). */
-async function loadMenus() {
-  const out = join(work, "menus.mjs");
+/** The aliases every menu-module loader needs: the fixture packages' ui and app modules the way a pnpm link resolves
+ *  them, the real @retroplug/ui contract, and the fixture shell's src/*.ts shims of the real shell's binding
+ *  vocabulary. */
+function menusAlias() {
+  return {
+    "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
+    "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
+    "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
+    "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
+    "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
+    "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
+    "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui.ts"),
+    // The ui package itself is a workspace dependency of the shell, not a fixture package: a real build resolves it
+    // through the pnpm link, so the driver hands the bundle the tree's own copy.
+    "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
+    // The fixture shell's src/*.ts shims re-export the real shell's modules, so an action's defaults are checked
+    // against the real key names, pad names and shell defaults rather than against a copy of them.
+    "@retroplug/shell/keyCodes": join(HERE, "../../src/keyCodes.ts"),
+    "@retroplug/shell/bindingMap": join(HERE, "../../src/bindingMap.ts"),
+  };
+}
+
+/** Load the generated menus module the way the UI loads it, running its checks (a row's role kind, a row id twice, a
+ *  builder's placement, a view's id and body, an action's id and defaults). */
+async function importMenus(name) {
+  const out = join(work, name);
   const { buildSync } = require("esbuild");
   buildSync({
     ...buildOptions(join(root, "packages/retroplug/ui/generated/menus.ts"), out, "esm"),
-    alias: {
-      "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
-      "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
-      "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
-      "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
-      "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
-      "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
-      "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui.ts"),
-      // The ui package itself is a workspace dependency of the shell, not a fixture package: a real build resolves it
-      // through the pnpm link, so the driver hands the bundle the tree's own copy.
-      "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
-    },
+    alias: menusAlias(),
   });
-  const loaded = await import(pathToFileURL(out).href);
-  return loaded.UI_CONTRIBUTIONS.map((entry) => entry.pkg);
+  return import(pathToFileURL(out).href);
+}
+
+async function loadMenus() {
+  return (await importMenus("menus.mjs")).UI_CONTRIBUTIONS.map((entry) => entry.pkg);
 }
 
-/** The generated menus module's VIEWS, bundled and loaded the way the shell loads it: product order, and each view's
- *  id, placement and body kind as the registry reports them. */
+/** The generated menus module's VIEWS: product order, and each view's id, placement and body kind as the registry
+ *  reports them. */
 async function loadViews() {
-  const out = join(work, "views.mjs");
-  const { buildSync } = require("esbuild");
-  buildSync({
-    ...buildOptions(join(root, "packages/retroplug/ui/generated/menus.ts"), out, "esm"),
-    alias: {
-      "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
-      "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
-      "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
-      "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
-      "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui.ts"),
-      "@retroplug/beta/ui": join(root, "packages/beta/ts/ui.ts"),
-      "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui.ts"),
-      "@retroplug/ui/contribution": join(HERE, "../../../ui/ts/contribution.ts"),
-    },
-  });
-  const loaded = await import(pathToFileURL(out).href);
+  const loaded = await importMenus("views.mjs");
   return loaded.VIEWS.map(({ pkg, view }) => ({ pkg, id: view.id, placement: view.placement, kind: view.body.kind }));
 }
 
+/** The generated menus module's ACTIONS: product order, and each action's id, label and default key and pad names as
+ *  the registry reports them. */
+async function loadActions() {
+  const loaded = await importMenus("actions.mjs");
+  return loaded.ACTIONS.map(({ pkg, action }) => ({
+    pkg,
+    id: action.id,
+    label: action.label,
+    keys: action.keys ?? [],
+    pad: action.pad ?? [],
+  }));
+}
+
+/** Load the menus module with alpha contributing `ui`, and report the registry's own refusal (or null): the action
+ *  checks run as the module loads, so a bad default is a load error. */
+async function actionRefusal(ui) {
+  writeTree("packages/alpha/ts/ui.ts", ui);
+  editManifest("alpha", (m) => {
+    m.retroplug.ui = "ts/ui.ts";
+    m.exports["./ui"] = "./ts/ui.ts";
+  });
+  generate({ root });
+  try {
+    await loadActions();
+    return { error: null };
+  } catch (e) {
+    return { error: e.message };
+  }
+}
+
 /** The generated CLI tools module, bundled and loaded as the dispatcher loads it: loading it runs its own refusal of a
  *  name two packages declare. */
 async function loadCli() {
@@ -617,6 +647,102 @@ try {
         result = { error: e.message };
       }
       break;
+    case "actions": {
+      // The positive side: one action per package, two of them on the same backtick (which two CONTRIBUTIONS may
+      // share), so the report is the product order and the defaults as declared.
+      writeTree(
+        "packages/zeta/ts/ui.ts",
+        'export default { actions: [{ id: "zeta.first", label: "Zeta First", keys: ["`"], run: () => {} }] };\n',
+      );
+      writeTree(
+        "packages/alpha/ts/ui.ts",
+        'export default { actions: [{ id: "alpha.second", label: "Alpha Second", keys: ["`"], run: () => {} }] };\n',
+      );
+      writeTree(
+        "packages/beta/ts/ui.ts",
+        'export default { actions: [{ id: "beta.third", label: "Beta Third", pad: ["guide"], run: () => {} }] };\n',
+      );
+      for (const name of ["alpha", "beta"]) {
+        editManifest(name, (m) => {
+          m.retroplug.ui = "ts/ui.ts";
+          m.exports["./ui"] = "./ts/ui.ts";
+        });
+      }
+      generate({ root });
+      try {
+        result = { actions: await loadActions() };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    }
+    case "actions-duplicate-id":
+      result = await actionRefusal(
+        'export default { actions: [\n' +
+          '  { id: "alpha.same", label: "First", keys: ["9"], run: () => {} },\n' +
+          '  { id: "alpha.same", label: "Second", keys: ["0"], run: () => {} },\n' +
+          '] };\n',
+      );
+      break;
+    case "actions-duplicate-across": {
+      // Both packages in one load: the check runs as the module loads, and a module already loaded is not checked
+      // again.
+      for (const name of ["alpha", "beta"]) {
+        writeTree(
+          `packages/${name}/ts/ui.ts`,
+          `export default { actions: [{ id: "alpha.same", label: "${name}", run: () => {} }] };\n`,
+        );
+        editManifest(name, (m) => {
+          m.retroplug.ui = "ts/ui.ts";
+          m.exports["./ui"] = "./ts/ui.ts";
+        });
+      }
+      generate({ root });
+      try {
+        await loadActions();
+        result = { error: null };
+      } catch (e) {
+        result = { error: e.message };
+      }
+      break;
+    }
+    case "actions-bad-prefix":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "zeta.stolen", label: "Stolen", keys: ["9"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-unknown-key":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.key", label: "Key", keys: ["Nonsense"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-unknown-pad":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.pad", label: "Pad", pad: ["nonsense"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-shell-key":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.menu", label: "Menu", keys: ["Escape"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-gb-key":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.jump", label: "Jump", keys: ["Z"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-return-key":
+      // "Return" and the Game Boy Start default "Enter" are one DPF code (0x0d), so the collision is by code, not by
+      // name: a check that compared names would let this through.
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.enter", label: "Enter", keys: ["Return"], run: () => {} }] };\n',
+      );
+      break;
+    case "actions-gb-pad":
+      result = await actionRefusal(
+        'export default { actions: [{ id: "alpha.jump", label: "Jump", pad: ["a"], run: () => {} }] };\n',
+      );
+      break;
     case "cli": {
       // The positive side: beta contributes one tool and alpha another, and zeta a third; zeta is listed first, so the
       // reported order is the product order rather than an alphabetical sort of the three names.
````

Run: `sed -n '6220,6788p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/lib/packages.mjs
packages/retroplug/test/packages/fixtures/expected/menus.ts
packages/retroplug/test/packages/packages-generator.test.ts
packages/retroplug/test/packages/run-generator.mjs
EOF
````

Expected:

````text
7ae69c64584a55e8cb4067c787f9256a0606dc4d
b9c6ce54873c41c897747ec7f50c55675b1c736a
8282a42dc69318f76089672390384458d691e39d
1228a380f558f11a41582d522cae516665bb70db
````

- [ ] **Step 3: The dispatch: one press, every id, each in its own try**

`ui/input/appActions.ts` is the shell's dispatch over both kinds: `runActions(ids, shell, contributed, host,
systemId)` runs the ids in order, a shell id through App's own body, a contributed id through `action.run(host, {
systemId })`, each in a `try` that reports `[actions] <id>: its run threw: ...` and carries on, and an id neither
knows skipped. It takes the contributed list as a parameter, so a test can drive an action no product ships and `src/`
need not reach `ui/`. App builds both maps over `withActionDefaults(bindings, ...)` with `contributedActionIds()` as
the known set, passes `empty ? null : systems.focused()` as `ctx.systemId`, and dispatches the list after Esc and
after the overlay check (not gated on `isMenuModalActive`, which is the backtick's behaviour today). **The backtick
line and `KEY_BACKTICK` stay App's own in this stage.** `menuContributions.ts` imports `ACTIONS` and exports
`contributedActions()` / `contributedActionIds()`; `menuDefs.ts`'s `bindingsChildren` appends one capture row per
contributed action after the shell's three, in registration order, with ids `bind-act-<id>` / `bind-gp-act-<id>`, the
label `<label>: <bound names or ->`, the same capture plumbing and the same reset, and the contributed list reaches
the composer through the new optional `MenuContext.actions` (App leaves it out and the registry answers; a menu test
passes its own).

Create `packages/retroplug/ui/input/appActions.ts`:

````ts
// One press's app actions (docs/modular-graph-phase-f4c-design.md section 5.5): the shell's three, then whatever the
// profile bound to that key or controller button, which may be a package's contributed action as well as the shell's.
// Kept out of App so the order and each action's isolation are testable with no display.
//
// The contributed list is a parameter rather than a registry read: App passes the generated registry's list, and a test
// passes its own action, which is the only way an action no product ships can drive this.
import { isAppAction, type AppAction } from "../../src/keyCodes";
import type { ContributedAction, UiHost } from "@retroplug/ui/contribution";

/** What the shell's own three do, by id: App's bodies, built at the press. */
export type ShellActions = Readonly<Record<AppAction, () => void>>;

/** Run one press's resolved action ids in order. A shell id runs its shell body; a contributed id runs its action with
 *  the focused system (null on the start screen); an id neither knows is skipped (a profile can name an action no
 *  product registers any more, as it can name an unknown one today). A contributed action that throws is reported and
 *  the rest of the press still runs: one package's bug must not swallow the shell's menu key on the same press. */
export function runActions(
  ids: readonly string[],
  shell: ShellActions,
  contributed: readonly ContributedAction[],
  host: UiHost,
  systemId: number | null,
): void {
  for (const id of ids) {
    if (isAppAction(id)) {
      shell[id]();
      continue;
    }
    const found = contributed.find((entry) => entry.action.id === id);
    if (!found) continue;
    try {
      found.action.run(host, { systemId });
    } catch (error) {
      const why = error instanceof Error ? error.message : String(error);
      console.warn(`[actions] ${id}: its run threw: ${why}`);
    }
  }
}
````

Run: `mkdir -p packages/retroplug/ui/input; sed -n '6833,6870p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/ui/input/appActions.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/ui/input/appActions.ts`
Expected: `0d42d4f86d5e7c6cc8145efa4de3a1b286406e41`: the blob c956580c4 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/ui/App.tsx b/packages/retroplug/ui/App.tsx
index 92857ce67..2a011287c 100644
--- a/packages/retroplug/ui/App.tsx
+++ b/packages/retroplug/ui/App.tsx
@@ -38,10 +38,12 @@ import { subscribeMidi } from "./screens/menu/midiDevices";
 import { subscribeTransport, pollTransport } from "./screens/menu/transport";
 import type { MenuTree } from "@retroplug/ui/menu";
 import { isMenuModalActive } from "./screens/menu/menuModal";
-import { buildKeyToAction, buildGamepadToAction, KEY_ESCAPE, type AppAction } from "../src/keyCodes";
+import { buildKeyToAction, buildGamepadToAction, KEY_ESCAPE } from "../src/keyCodes";
 import { resolveDropAction } from "../src/fileDrop";
 import { createUiHost } from "./host/uiHost";
-import { viewById, windowViewOf } from "./host/menuContributions";
+import { contributedActionIds, contributedActions, viewById, windowViewOf } from "./host/menuContributions";
+import { runActions } from "./input/appActions";
+import { withActionDefaults } from "../src/bindingMap";
 import { ViewWindow } from "./views/ViewWindow";
 import { viewAppliesTo } from "./views/viewSession";
 import { resolveTracker } from "../src/trackerRegistry";
@@ -146,9 +148,23 @@ export function App() {
   const overlayActive = overlay !== null; // a stable effect dep (the hooks return a fresh object each render)
   // "In play": a tile is showing and no menu/overlay owns input. Gates game input AND the cycle actions.
   const playing = !empty && !menuOpen && !overlay;
-  // App-action lookups (open menu / cycle instances), rebuilt only when the bindings change.
-  const keyToAction = useMemo(() => buildKeyToAction(bindings.keyboardActions), [bindings.keyboardActions]);
-  const padToAction = useMemo(() => buildGamepadToAction(bindings.gamepadActions), [bindings.gamepadActions]);
+  // App-action lookups (open menu / cycle instances, and each package's contributed actions), rebuilt only when the
+  // profile changes. The profile is filled with the contributed actions' defaults first (design 5.6), so an id the
+  // user has never edited reads its default; a `[]` the user wrote stays empty. The value is a LIST per key: one press
+  // can carry the shell's action and a package's, and each runs.
+  const actionIds = contributedActionIds(); // a module constant of the generated registry: a stable dep
+  const actionBindings = useMemo(
+    () => withActionDefaults(bindings, contributedActions().map((entry) => entry.action)),
+    [bindings],
+  );
+  const keyToAction = useMemo(
+    () => buildKeyToAction(actionBindings.keyboardActions, actionIds),
+    [actionBindings, actionIds],
+  );
+  const padToAction = useMemo(
+    () => buildGamepadToAction(actionBindings.gamepadActions, actionIds),
+    [actionBindings, actionIds],
+  );
   const resolvedZoom = resolveZoom(settings.zoom, userConfig.defaultZoom);
 
   // The standalone OS window title: version + the focused tracker cart's "<song> - <ROM name>" when one is
@@ -222,11 +238,24 @@ export function App() {
     }
   };
 
-  // Dispatch a resolved app action: open/close the menu, or cycle the focused instance (only in play).
-  const runAction = (action: AppAction | undefined) => {
-    if (action === "OpenMenu") toggleMenuCore();
-    else if (action === "CycleNext" && playing) stores.project.systems.focusNext(1);
-    else if (action === "CyclePrev" && playing) stores.project.systems.focusNext(-1);
+  // Dispatch a resolved press's action ids: the shell's three (open/close the menu, or cycle the focused instance,
+  // the cycles only in play), then each contributed action, which gets the focused system or null on the start screen.
+  const runResolved = (ids: readonly string[]): void => {
+    runActions(
+      ids,
+      {
+        OpenMenu: toggleMenuCore,
+        CycleNext: () => {
+          if (playing) stores.project.systems.focusNext(1);
+        },
+        CyclePrev: () => {
+          if (playing) stores.project.systems.focusNext(-1);
+        },
+      },
+      contributedActions(),
+      uiHost,
+      empty ? null : stores.project.systems.focused(),
+    );
   };
 
   useNativeEvent("key", (key, press) => {
@@ -238,14 +267,14 @@ export function App() {
     }
     if (overlay) return; // an overlay owns input; actions don't fire under it
     if (key === KEY_BACKTICK) return void toggleLsdjDebug(); // dev: show/hide the LSDj runtime readout
-    runAction(keyToAction.get(key));
+    runResolved(keyToAction.get(key) ?? []);
   });
   // Gamepad app actions. Overlays (close prompt / project modal) render a <Menu>, so they're cancelled by the
   // Menu B button — the gamepad handler just defers under them.
   useNativeEvent("gamepad-button", (_pad, name, press) => {
     if (!press) return;
     if (overlay) return;
-    runAction(padToAction.get(name));
+    runResolved(padToAction.get(name) ?? []);
   });
 
   // Right-click an instance → open its menu (anchored to the clicked tile, which also becomes focused). The
diff --git a/packages/retroplug/ui/host/menuContributions.ts b/packages/retroplug/ui/host/menuContributions.ts
index 0b2e5badf..25a78b14f 100644
--- a/packages/retroplug/ui/host/menuContributions.ts
+++ b/packages/retroplug/ui/host/menuContributions.ts
@@ -5,11 +5,12 @@
 // kind no registered type declares or over a key its schema does not read, and a tracker UI no integration matches, so
 // what this module reads it can index.
 import type {
-  MenuBuildContext, MenuBuilder, RoleRow, TileView, TrackerUi, UiHost, UiService, ViewDef, WindowView,
+  ContributedAction, MenuBuildContext, MenuBuilder, RoleRow, TileView, TrackerUi, UiHost, UiService, ViewDef,
+  WindowView,
 } from "@retroplug/ui/contribution";
 import type { SystemInfo } from "@retroplug/core/host";
 import type { MenuItem } from "@retroplug/ui/menu";
-import { UI_CONTRIBUTIONS, VIEWS } from "../generated/menus";
+import { ACTIONS, UI_CONTRIBUTIONS, VIEWS } from "../generated/menus";
 
 /** Every contributed role row, in product order: the System submenu's package-owned block. */
 export function roleRows(): readonly RoleRow[] {
@@ -58,6 +59,21 @@ export function menuPlacement(
   );
 }
 
+/** Every app action a package contributes, in product order, with the package it came from: the bindings menus' rows,
+ *  the key and gamepad maps' "known" ids, and the dispatch's lookups. The generated list itself, so the export the
+ *  generator's checks run over is the one the product reads. A module constant, so a hook's dependency (and a memo
+ *  over it) is stable. */
+const ACTION_IDS: readonly string[] = ACTIONS.map((entry) => entry.action.id);
+
+export function contributedActions(): readonly ContributedAction[] {
+  return ACTIONS;
+}
+
+/** Their ids alone, for the maps' "known" set and the bindings rows. */
+export function contributedActionIds(): readonly string[] {
+  return ACTION_IDS;
+}
+
 /** Every service a package contributes, in product order: the list `useUiServices` runs, subscribes and polls. It is a
  *  module constant derived from the generated list, so the hook's dependency is stable. */
 const SERVICES: readonly UiService[] = UI_CONTRIBUTIONS.flatMap((entry) => entry.ui.services ?? []);
diff --git a/packages/retroplug/ui/screens/menu/menuDefs.ts b/packages/retroplug/ui/screens/menu/menuDefs.ts
index 43ca273a5..addc4c937 100644
--- a/packages/retroplug/ui/screens/menu/menuDefs.ts
+++ b/packages/retroplug/ui/screens/menu/menuDefs.ts
@@ -10,12 +10,18 @@ import type { ProjectSettings } from "../../../src/projectConfig";
 import { MIDI_ROUTING_VALUES } from "@retroplug/core/midiRouting";
 import { consoleOfPlatform } from "../../../src/consoles";
 import { roleRowItems } from "./configRows";
-import { contributedTrackerUi, firstWinsMenuIds, menuPlacement, roleRows } from "../../host/menuContributions";
+import {
+  contributedActions,
+  contributedTrackerUi,
+  firstWinsMenuIds,
+  menuPlacement,
+  roleRows,
+} from "../../host/menuContributions";
 import { LAYOUT_VALUES, AUDIO_ROUTING_VALUES, type AudioRouting } from "../../../src/settingsEnums";
 import type { UserConfig } from "../../../src/userConfig";
 import { SRAM_AUTO_SAVES, RENDER_SAMPLE_RATES, RENDER_ON_EXISTS } from "../../../src/userConfig";
 import type { SplitMode } from "../../../src/render";
-import { defaultBindingMap, type BindingMap } from "../../../src/bindingMap";
+import { defaultBindingMap, withActionDefaults, type BindingMap } from "../../../src/bindingMap";
 import { keyDisplayName } from "../../../src/keyCodes";
 import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
 import type { RecentView } from "../../../src/recentStore";
@@ -34,6 +40,7 @@ import { effectiveAssets, readAssetOverrides } from "@retroplug/tracker/assetCat
 import type { AssetOverride, AssetSlotRow, AssetTypeInfo } from "@retroplug/tracker/assetCatalog";
 import { workingSongTargets } from "@retroplug/tracker/songCatalog";
 import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
+import type { ContributedAction } from "@retroplug/ui/contribution";
 import { resolveTracker } from "../../../src/trackerRegistry";
 import type { HostBackend, ControlPlaneBackend } from "../../../src/backend";
 import { openPath } from "../../host/openPath";
@@ -91,6 +98,10 @@ export interface MenuContext {
    *  the stores; a composer that only has the stores (a menu test) leaves it out and `hostOf` builds the same host
    *  from this context's own peers, so a contribution sees the groups either way. */
   host?: UiHost;
+  /** The contributed app actions the bindings editor lists after the shell's three. App leaves it out and the
+   *  generated registry answers; a test passes its own action, which is the only way a row for an action no product
+   *  ships can be built. */
+  actions?: readonly ContributedAction[];
 }
 
 /** The UI host for a context: the one App built when it passed one, else one built from the context's own peers (the
@@ -1203,7 +1214,10 @@ function bindingsChildren(ctx: MenuContext, channel: BindingsChannel): MenuItem[
   const label = channel === "keyboard" ? "Keyboard" : "Gamepad";
 
   const actionsKey: "keyboardActions" | "gamepadActions" = channel === "keyboard" ? "keyboardActions" : "gamepadActions";
-  const actMap = ctx.bindings[actionsKey]; // resolved active app-action map
+  // The active app-action map, filled with the contributed actions' defaults first (design 5.6): a profile with no
+  // entry for a contributed id shows the action's default, and only an id the user wrote (a key or `[]`) is theirs.
+  const contributed = ctx.actions ?? contributedActions();
+  const actMap = withActionDefaults(ctx.bindings, contributed.map((entry) => entry.action))[actionsKey];
 
   // The bound-names half of a row label ("-" when unbound). Gamepad names are the raw SDL names and show
   // as-is; keyboard names get their display form, so a space reads "Space" rather than an invisible glyph.
@@ -1268,9 +1282,14 @@ function bindingsChildren(ctx: MenuContext, channel: BindingsChannel): MenuItem[
     },
   }));
 
-  // The app-action rows (Open Menu / Cycle Instances / Cycle Instances (Back)) — same capture plumbing as a GB
-  // row, written into the actions section. The "<label>: " head is kept so Menu's "Press a key/button…" swap works.
-  const actionRows: MenuItem[] = APP_ACTION_ROWS.map((a) => ({
+  // The app-action rows: the shell's three (Open Menu / Cycle Instances / Cycle Instances (Back)), then one per
+  // contributed action in registration order. Same capture plumbing as a GB row, written into the actions section
+  // under the action's own id (a dot included, which nothing here or in Menu parses). The "<label>: " head is kept so
+  // Menu's "Press a key/button…" swap works.
+  const actionRows: MenuItem[] = [
+    ...APP_ACTION_ROWS.map((a) => ({ id: a.id, label: a.label })),
+    ...contributed.map(({ action }) => ({ id: action.id, label: action.label })),
+  ].map((a) => ({
     id: `${idp}-act-${a.id}`,
     label: `${a.label}: ${shown(actMap[a.id])}`,
     kind: "capture" as const,
````

Run: `sed -n '6880,7087p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/ui/App.tsx
packages/retroplug/ui/host/menuContributions.ts
packages/retroplug/ui/screens/menu/menuDefs.ts
EOF
````

Expected:

````text
2a011287c903fe2d0839c6e3a1109ae3c90130bf
25a78b14ff28727cb95fdadd5fd9734d59e5bba8
addc4c937084b993d55cbf6078016fdf1f0ccfea
````

- [ ] **Step 4: The stage 2 review's session fix, and the stage 3 review's key winner**

The fix pass folded into this tree: the three-state `useViewSession`, `shownSession`, the controller-side `read`
(which answers `null` once it holds no session, so a dropped readout clears rather than keeping its last lines) and
the guards, with their cases; and, from the stage 3 review, one shell action per key (the later binding winning) and
one firing per key even when a hand-edited profile binds the same action twice. Both are in this tree's blocks above
(the maps' `addActionId`) and below.

````diff
diff --git a/packages/retroplug/test/ui/view-session.test.ts b/packages/retroplug/test/ui/view-session.test.ts
index 87ed5aeee..41f0fbdb6 100644
--- a/packages/retroplug/test/ui/view-session.test.ts
+++ b/packages/retroplug/test/ui/view-session.test.ts
@@ -11,6 +11,7 @@ import { createSignal } from "@retroplug/ui/menu";
 import { warned } from "../_warnings";
 import {
   isPixelSession,
+  isReadoutSampler,
   openSession,
   readoutKey,
   reopenKey,
@@ -203,7 +204,7 @@ test("a frame reads the session held now: none before the first open, none after
   const frames: string[] = [];
   const view = fakeView("test-view", opened, closed, frames);
   const held = new ViewSessionController();
-  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(undefined); // nothing opened: no call at all
+  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(null); // nothing opened: no call at all
 
   const sys = system();
   held.sync(view, host, sys, true);
@@ -212,7 +213,7 @@ test("a frame reads the session held now: none before the first open, none after
 
   // A hide closes the session, and a frame that lands before the next render must not reach it.
   held.sync(view, host, sys, false);
-  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(undefined);
+  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(null);
   expect(closed).toEqual(["test-view"]);
   expect(frames).toEqual(["test-view"]);
 
@@ -250,16 +251,47 @@ test("a reader that throws is reported once and drops the session until the key
   const sys = system();
   held.sync(view, host, sys, true);
   const { result, warnings } = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
-  expect(result).toBe(undefined);
-  expect(warnings).toEqual(["[views] reading a session failed: the frame is broken"]);
+  expect(result).toBe(null);
+  expect(warnings).toEqual(["[views] test-view: reading a session failed: the frame is broken"]);
   expect(closed).toEqual(["test-view"]); // the faulted session is closed, not left half-usable
   // Dropped, so the next frame does not call it again: one report per faulty session.
   const after = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
-  expect(after.result).toBe(undefined);
+  expect(after.result).toBe(null);
   expect(after.warnings).toEqual([]);
   expect(reads).toBe(1);
 });
 
+test("a live sampler's undefined passes through, and a dropped session answers null so its strip clears", () => {
+  // The two "nothing shows" answers are different: a sampler's undefined is "unreadable this frame, keep what shows",
+  // while a session the controller dropped is null ("draw nothing"), which is what clears a readout's last lines.
+  const view: WindowView = {
+    id: "test-view",
+    label: "Test View",
+    placement: "window",
+    body: {
+      kind: "readout",
+      open: () => ({
+        sample: () => {
+          throw new Error("the RAM is unreadable");
+        },
+      }),
+    },
+  };
+  const held = new ViewSessionController();
+  held.sync(view, host, system(), true);
+  const { result } = warned(() => held.read((s) => (isReadoutSampler(s) ? s.sample() : null)));
+  expect(result).toBe(null); // the sampler threw, so the session was dropped
+  const unreadable = new ViewSessionController();
+  const view2: WindowView = {
+    id: "test-view-2",
+    label: "Test View 2",
+    placement: "window",
+    body: { kind: "readout", open: () => ({ sample: () => undefined }) },
+  };
+  unreadable.sync(view2, host, system(), true);
+  expect(unreadable.read((s) => (isReadoutSampler(s) ? s.sample() : null))).toBe(undefined); // not dropped
+});
+
 test("a view that has not opened yet draws nothing, and a hide drops the session in the same render", () => {
   const opened: string[] = [];
   const closed: string[] = [];
diff --git a/packages/retroplug/ui/views/useViewSession.ts b/packages/retroplug/ui/views/useViewSession.ts
index 26f92751d..817f8923f 100644
--- a/packages/retroplug/ui/views/useViewSession.ts
+++ b/packages/retroplug/ui/views/useViewSession.ts
@@ -11,7 +11,7 @@ import { shownSession, ViewSessionController, type OpenedSession } from "./viewS
 /** What a renderer gets: the session to draw (`shownSession` has its three states) and the per-frame reader. */
 export interface ViewSessionHandle {
   readonly session: OpenedSession | null | undefined;
-  read<R>(reader: (session: OpenedSession) => R): R | undefined;
+  read<R>(reader: (session: OpenedSession) => R): R | null;
 }
 
 /** `shown` is the renderer's own reading of the view's visibility (`viewShown`, subscribed by the caller): a window
@@ -37,7 +37,7 @@ export function useViewSession(
   // The reader is the controller's, not this render's closure: a frame that lands after a hide or a reopen must not
   // read the session that change closed. It is stable, so a source that memoises it does not reset every render.
   const read = useCallback(
-    <R,>(reader: (session: OpenedSession) => R): R | undefined => controller.current!.read(reader),
+    <R,>(reader: (session: OpenedSession) => R): R | null => controller.current!.read(reader),
     [],
   );
 
diff --git a/packages/retroplug/ui/views/viewSession.ts b/packages/retroplug/ui/views/viewSession.ts
index 6b1f9b1b5..cc4bfc1f0 100644
--- a/packages/retroplug/ui/views/viewSession.ts
+++ b/packages/retroplug/ui/views/viewSession.ts
@@ -179,19 +179,20 @@ export class ViewSessionController {
   }
 
   /** Read the session held NOW, for a per-frame handler that must not use a render's closure: a session a hide or a
-   *  reopen closed since that render answers undefined and is not called. A reader that throws is reported once and
-   *  drops the session, the key staying held so the shell retries at the next key change (as it does for an open that
-   *  threw). */
-  read<R>(reader: (session: OpenedSession) => R): R | undefined {
+   *  reopen closed since that render answers null and is not called, so a readout that was showing a dropped session
+   *  clears rather than keeping its last lines (a `null` sample is "draw nothing"; the sampler's own `undefined` is
+   *  "unreadable this frame, keep what shows"). A reader that throws is reported once, naming the view, and drops the
+   *  session, the key staying held so the shell retries at the next key change (as it does for an open that threw). */
+  read<R>(reader: (session: OpenedSession) => R): R | null {
     const held = this.held;
-    if (held === null || held.session === null) return undefined;
+    if (held === null || held.session === null) return null;
     try {
       return reader(held.session);
     } catch (error) {
-      console.warn(`[views] reading a session failed: ${message(error)}`);
+      console.warn(`[views] ${held.key.id}: reading a session failed: ${message(error)}`);
       closeSession(held.session);
       held.session = null;
-      return undefined;
+      return null;
     }
   }
 
````

Run: `sed -n '7120,7254p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/test/ui/view-session.test.ts
packages/retroplug/ui/views/useViewSession.ts
packages/retroplug/ui/views/viewSession.ts
EOF
````

Expected:

````text
41f0fbdb6b6a04c5b71627ae1ad3ae45a9aacd3f
817f8923fd403d5991edbe855594caec02f3c314
cc4bfc1f059a8823b66cb36e10e340fdd28dea58
````

- [ ] **Step 5: The cases: dispatch, maps, rows and the two leaves counts**

New: `test/input/app-actions.test.ts` (order, the focused system and `null`, an unknown id skipped, a throwing action
reported with the rest of the press still running), `test/menu/action-rows.test.ts` (the keyboard and gamepad rows
over a test action: id, label, the default fill, the capture, a cleared binding keeping `[]`, the reset reading the
default again, and a dotted row id through `assertUniqueMenuIds` and the composer), and the six new key and gamepad
map cases (a key's list in the profile's order, one action twice firing once, an unknown id skipped,
`GAMEPAD_BUTTON_NAMES` and its relation to the Game Boy defaults). Changed in shape, keeping their cases: the two old
default-map cases now read one-element lists, and `leaves.test.ts`'s two assertions that read a map value now read
`toEqual(["..."])`. The capture-row counts stay 11 (8 Game Boy + the shell's 3): no product action exists yet.

Create `packages/retroplug/test/input/app-actions.test.ts`:

````ts
// One press's app actions (docs/modular-graph-phase-f4c-design.md section 5.5): the shell's three and each package's
// contributed ones, dispatched in the order the resolved list gives, with one action's throw isolated from the rest.
// No display and no React: the dispatch is a function, and App only hands it the ids a key or button resolved to.
import { test, expect } from "@retroplug/testing";
import type { UiAction, UiHost } from "@retroplug/ui/contribution";

import { warned } from "../_warnings";
import { runActions, type ShellActions } from "../../ui/input/appActions";

/** The host a contributed action receives: none of the fakes read it. */
const host = {} as UiHost;

/** A shell body per id that records itself, plus the contributed actions under test. */
function press(): { log: string[]; shell: ShellActions } {
  const log: string[] = [];
  return {
    log,
    shell: {
      OpenMenu: () => log.push("OpenMenu"),
      CycleNext: () => log.push("CycleNext"),
      CyclePrev: () => log.push("CyclePrev"),
    },
  };
}

function action(id: string, log: string[]): { pkg: string; action: UiAction } {
  return { pkg: id.split(".")[0]!, action: { id, label: id, run: () => log.push(id) } };
}

test("a press runs its ids in order: the shell's actions and the contributed ones alike", () => {
  const { log, shell } = press();
  const contributed = [action("lsdj.debug-overlay", log), action("risa.debug-overlay", log)];
  // One backtick carries both overlays' actions; the list is the profile's order.
  runActions(["lsdj.debug-overlay", "risa.debug-overlay"], shell, contributed, host, 7);
  expect(log).toEqual(["lsdj.debug-overlay", "risa.debug-overlay"]);
  runActions(["CycleNext", "lsdj.debug-overlay"], shell, contributed, host, 7);
  expect(log).toEqual(["lsdj.debug-overlay", "risa.debug-overlay", "CycleNext", "lsdj.debug-overlay"]);
});

test("a contributed action gets the focused system, and null on the start screen", () => {
  const seen: (number | null)[] = [];
  const contributed = [
    {
      pkg: "lsdj",
      action: {
        id: "lsdj.debug-overlay",
        label: "LSDj Debug Overlay",
        run: (_h: UiHost, ctx: { systemId: number | null }) => seen.push(ctx.systemId),
      },
    },
  ];
  const { shell } = press();
  runActions(["lsdj.debug-overlay"], shell, contributed, host, 3);
  runActions(["lsdj.debug-overlay"], shell, contributed, host, null);
  expect(seen).toEqual([3, null]);
});

test("an id neither the shell nor the registry knows is skipped", () => {
  const { log, shell } = press();
  // A profile keeps the id of an action no product registers any more, as it can keep an unknown one today: skipped,
  // and no throw on the way out.
  runActions(["gone.action", "CyclePrev"], shell, [], host, 1);
  expect(log).toEqual(["CyclePrev"]);
});

test("a throwing action is reported and the rest of its press still runs", () => {
  const { log, shell } = press();
  const contributed = [
    {
      pkg: "lsdj",
      action: {
        id: "lsdj.debug-overlay",
        label: "LSDj Debug Overlay",
        run: () => {
          throw new Error("the overlay is broken");
        },
      },
    },
  ];
  const { warnings } = warned(() => runActions(["lsdj.debug-overlay", "CycleNext"], shell, contributed, host, 2));
  expect(warnings).toEqual(["[actions] lsdj.debug-overlay: its run threw: the overlay is broken"]);
  expect(log).toEqual(["CycleNext"]); // the shell's action on the same press still ran
});
````

Run: `mkdir -p packages/retroplug/test/input; sed -n '7292,7374p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/input/app-actions.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/input/app-actions.test.ts`
Expected: `da085e1061f03dd3b6309f2e8bfea8ccae3f72f2`: the blob c956580c4 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/menu/action-rows.test.ts`:

````ts
// The bindings menus' contributed action rows (docs/modular-graph-phase-f4c-design.md section 5.7), built with no
// product action at all: the menu context takes the contributed list, so this file passes its own action and drives the
// rows the two menus build. What the shipped product shows for its two real actions is the same code path with the
// generated registry's list, which the recorded menu matrix holds once a product carries one.
import { test, expect } from "@retroplug/testing";

import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";
import { assertUniqueMenuIds } from "../../ui/host/menuContributions";

/** The one test action: a package-prefixed id (a dot, as the registry holds a contributed id), a backtick default and
 *  no pad default, which is the shapes the two real overlays' actions have. */
const TEST_ACTION: UiAction = {
  id: "test.debug-overlay",
  label: "Test Debug Overlay",
  keys: ["`"],
  run: () => {},
};

// The MenuContext App assembles from its hooks, minus React, with the test action injected in place of the registry's
// list.
function ctxOf(stores: AppStores, actions: readonly { pkg: string; action: UiAction }[]): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    openLsdjHd: () => {},
    beginSongImport: () => {},
    actions,
  };
}

const kids = (items: MenuItem[], id: string): MenuItem[] => {
  const found = items.find((item) => item.id === id);
  return found && found.kind === "submenu" ? found.children ?? [] : [];
};

const bindingsMenu = (stores: AppStores, id: string): MenuItem[] => {
  const ctx = ctxOf(stores, [{ pkg: "test", action: TEST_ACTION }]);
  return kids(kids(buildStartMenu(ctx).items, "start-settings"), id);
};

const run = (stores: AppStores): MenuItem[] => bindingsMenu(stores, "set-keybindings");

const pads = (stores: AppStores): MenuItem[] => bindingsMenu(stores, "set-gamepad-bindings");

test("Keyboard Bindings: the test action's row follows the shell's three, with its default and its capture", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const rows = run(stores);
  // The shell's three are still there, and the contributed row is appended after them in registration order.
  const captureIds = rows.filter((row) => row.kind === "capture").map((row) => row.id);
  expect(captureIds.slice(8)).toEqual([
    "bind-act-OpenMenu",
    "bind-act-CycleNext",
    "bind-act-CyclePrev",
    "bind-act-test.debug-overlay",
  ]);
  const row = rows.find((item) => item.id === "bind-act-test.debug-overlay")!;
  expect(row.kind).toBe("capture");
  expect(row.label).toBe("Test Debug Overlay: `"); // the action's default, filled in (the profile has no entry)
  expect(row.capture!.source).toBe("keyboard");

  // Capturing writes the action's own id into the profile, and the rebuilt row reads what was captured.
  const active = stores.userConfig.config().activeKeyboardBindings;
  row.capture!.onCapture("Q");
  expect(stores.bindings.loadProfile(active)!.keyboardActions["test.debug-overlay"]).toEqual(["Q"]);
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: Q");

  // Clearing is the user's `[]`, which the fill leaves alone: the row reads "-" rather than the default again.
  run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.capture!.onClear();
  expect(stores.bindings.loadProfile(active)!.keyboardActions["test.debug-overlay"]).toEqual([]);
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: -");

  // Reset writes the shell's defaults for the channel; the contributed id it leaves out reads its default through the
  // fill again, which is the same as resetting it.
  run(stores).find((item) => item.id === "bind-reset")!.onSelect!();
  expect(stores.bindings.resolvedBindings().keyboardActions).toEqual({
    OpenMenu: ["Escape"],
    CycleNext: ["Tab"],
    CyclePrev: [],
  });
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: `");
});

test("Gamepad Bindings: the row reads '-' for the action's missing pad default, and captures a pad button", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const row = pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!;
  expect(row.kind).toBe("capture");
  expect(row.label).toBe("Test Debug Overlay: -"); // no pad default, as Cycle Instances (Back) has none
  expect(row.capture!.source).toBe("gamepad");

  const active = stores.userConfig.config().activeGamepadBindings;
  row.capture!.onCapture("misc1");
  expect(stores.bindings.loadProfile(active)!.gamepadActions["test.debug-overlay"]).toEqual(["misc1"]);
  expect(pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.label).toBe(
    "Test Debug Overlay: misc1",
  );

  // A pad capture clears to the same "-", and the filled default (an empty list) is what the map then holds.
  pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.capture!.onClear();
  expect(pads(stores).find((item) => item.id === "bind-gp-act-test.debug-overlay")!.label).toBe(
    "Test Debug Overlay: -",
  );
  expect(stores.bindings.resolvedBindings().gamepadActions["test.debug-overlay"]).toEqual([]);
});

test("a row id with a dot in it is an id like any other: unique, and kept whole", () => {
  // A contributed id carries a package prefix, so every contributed row's id has a dot. The menu trees are keyed by id
  // and Menu only ever compares, stores and joins them (read: ui/screens/menu/Menu.tsx has no split on an id), and the
  // composer's uniqueness check is over the whole string.
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const tree = buildStartMenu(ctxOf(stores, [{ pkg: "test", action: TEST_ACTION }])).items;
  assertUniqueMenuIds(tree, "start"); // throws on a duplicate; a dotted id is not one
  const ids = run(stores).map((item) => item.id);
  expect(ids.filter((id) => id === "bind-act-test.debug-overlay").length).toBe(1);
  // No row renames or truncates the dotted id on the way out either: the menu keeps the string it was given.
  expect(run(stores).find((item) => item.id === "bind-act-test.debug-overlay")!.label).toBe("Test Debug Overlay: `");
});
````

Run: `mkdir -p packages/retroplug/test/menu; sed -n '7386,7517p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/menu/action-rows.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/menu/action-rows.test.ts`
Expected: `e4e2cfe9e91b2152b2caf56b45adca2ddd4f0854`: the blob c956580c4 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test/config/bindings.test.ts b/packages/retroplug/test/config/bindings.test.ts
index fd15c2519..3f1623a85 100644
--- a/packages/retroplug/test/config/bindings.test.ts
+++ b/packages/retroplug/test/config/bindings.test.ts
@@ -2,7 +2,7 @@
 // on-disk shape + field spellings + defaultBindingMap values match native's
 // BindingMapJson, so a real bindings/*.json round-trips.
 import { test, expect } from "@retroplug/testing";
-import { defaultBindingMap } from "../../src/bindingMap";
+import { defaultBindingMap, withActionDefaults } from "../../src/bindingMap";
 import { parseBindingMap, serializeBindingMap, BINDINGS_SCHEMA } from "../../src/bindingSerialization";
 import { isValidProfileName } from "../../src/bindingsStore";
 
@@ -71,3 +71,34 @@ test("isValidProfileName: allows names, rejects empty / reserved / bad chars", (
   expect(isValidProfileName("a.b")).toBeFalsy();
   expect(isValidProfileName("a/b")).toBeFalsy();
 });
+
+test("withActionDefaults: an absent id takes the action's default, a present one (a key or []) is the user's", () => {
+  const map = defaultBindingMap();
+  const filled = withActionDefaults(map, [
+    { id: "lsdj.debug-overlay", keys: ["`"] },
+    { id: "risa.debug-overlay", keys: ["`"], pad: ["misc1"] },
+  ]);
+  // Absent from both sections: the action's own defaults, and no pad default reads as an empty list.
+  expect(filled.keyboardActions["lsdj.debug-overlay"]).toEqual(["`"]);
+  expect(filled.keyboardActions["risa.debug-overlay"]).toEqual(["`"]);
+  expect(filled.gamepadActions["lsdj.debug-overlay"]).toEqual([]);
+  expect(filled.gamepadActions["risa.debug-overlay"]).toEqual(["misc1"]);
+  // The shell's own three are untouched, and the profile is a new object: both channels are copied, not mutated.
+  expect(filled.keyboardActions.OpenMenu).toEqual(["Escape"]);
+  expect(map.keyboardActions["lsdj.debug-overlay"]).toBe(undefined);
+
+  // A present entry is the user's, `[]` included (a cleared binding is a choice, as CyclePrev's is).
+  const mine = { ...map, keyboardActions: { ...map.keyboardActions, "lsdj.debug-overlay": ["F"] } };
+  expect(withActionDefaults(mine, [{ id: "lsdj.debug-overlay", keys: ["`"] }]).keyboardActions[
+    "lsdj.debug-overlay"
+  ]).toEqual(["F"]);
+  const cleared = { ...map, keyboardActions: { ...map.keyboardActions, "lsdj.debug-overlay": [] } };
+  expect(withActionDefaults(cleared, [{ id: "lsdj.debug-overlay", keys: ["`"] }]).keyboardActions[
+    "lsdj.debug-overlay"
+  ]).toEqual([]);
+
+  // No contributed actions at all is the profile unchanged, and a profile with an id no action names keeps it.
+  expect(withActionDefaults(map, []).keyboardActions).toEqual(map.keyboardActions);
+  const stale = { ...map, gamepadActions: { ...map.gamepadActions, "gone.action": ["misc1"] } };
+  expect(withActionDefaults(stale, [{ id: "lsdj.debug-overlay" }]).gamepadActions["gone.action"]).toEqual(["misc1"]);
+});
diff --git a/packages/retroplug/test/input/gamepad.test.ts b/packages/retroplug/test/input/gamepad.test.ts
index d33fa9d64..3399b5b58 100644
--- a/packages/retroplug/test/input/gamepad.test.ts
+++ b/packages/retroplug/test/input/gamepad.test.ts
@@ -4,7 +4,15 @@
 // so there is no code table — unlike the keyboard channel's resolveKeyName.
 import { test, expect } from "@retroplug/testing";
 import { defaultBindingMap } from "../../src/bindingMap";
-import { buildGamepadToButton, buildGamepadToAction, axisToken, menuNavForButton, menuNavForAxisToken, BUTTON_VALUE } from "../../src/keyCodes";
+import {
+  buildGamepadToButton,
+  buildGamepadToAction,
+  axisToken,
+  menuNavForButton,
+  menuNavForAxisToken,
+  BUTTON_VALUE,
+  GAMEPAD_BUTTON_NAMES,
+} from "../../src/keyCodes";
 
 test("buildGamepadToButton: the default bindings invert to the native button values", () => {
   const m = buildGamepadToButton(defaultBindingMap().gamepad);
@@ -84,17 +92,52 @@ test("menuNavForAxisToken: only the left stick navigates, per the SDL sign conve
   expect(menuNavForAxisToken("")).toBe(null);
 });
 
-test("buildGamepadToAction: the default app-action bindings invert to SDL-name → action", () => {
+test("buildGamepadToAction: the default app-action bindings invert to SDL-name → a one-id list", () => {
   const m = buildGamepadToAction(defaultBindingMap().gamepadActions);
-  expect(m.get("leftshoulder")).toBe("OpenMenu"); // L1
-  expect(m.get("rightshoulder")).toBe("CycleNext"); // R1
+  expect(m.get("leftshoulder")).toEqual(["OpenMenu"]); // L1
+  expect(m.get("rightshoulder")).toEqual(["CycleNext"]); // R1
   expect(m.get("a")).toBe(undefined); // a face button is a GB button, not an action
 });
 
 test("buildGamepadToAction: unknown action ids skipped; null-tolerant", () => {
   const m = buildGamepadToAction({ OpenMenu: ["guide"], Bogus: ["x"] });
-  expect(m.get("guide")).toBe("OpenMenu");
+  expect(m.get("guide")).toEqual(["OpenMenu"]);
   expect(m.get("x")).toBe(undefined); // Bogus action id dropped
   expect(m.size).toBe(1);
   expect(buildGamepadToAction(undefined).size).toBe(0); // older profile lacking the section
 });
+
+test("buildGamepadToAction: one button carries every action bound to it, and an unknown id is skipped", () => {
+  const m = buildGamepadToAction(
+    { "lsdj.debug-overlay": ["misc1"], "risa.debug-overlay": ["misc1"], "gone.action": ["misc1"] },
+    ["lsdj.debug-overlay", "risa.debug-overlay"],
+  );
+  expect(m.get("misc1")).toEqual(["lsdj.debug-overlay", "risa.debug-overlay"]);
+});
+
+test("GAMEPAD_BUTTON_NAMES: SDL's canonical button names, and no stick token", () => {
+  // SDL_GameControllerGetStringForButton over SDL_CONTROLLER_BUTTON_* in order; the axis tokens are not buttons.
+  expect(GAMEPAD_BUTTON_NAMES.length).toBe(21);
+  expect(GAMEPAD_BUTTON_NAMES.slice(0, 6)).toEqual(["a", "b", "x", "y", "back", "guide"]);
+  expect(GAMEPAD_BUTTON_NAMES.slice(-4)).toEqual(["paddle2", "paddle3", "paddle4", "touchpad"]);
+  for (const name of ["leftstick", "rightstick", "leftshoulder", "rightshoulder", "dpup", "misc1", "paddle1"]) {
+    expect(GAMEPAD_BUTTON_NAMES.includes(name)).toBe(true);
+  }
+  // Every GB button's default pad name is one of them, except the left stick's half-axis tokens (TS-only, native
+  // binds just the hat), which is why a stick token cannot be an action's default.
+  for (const names of Object.values(defaultBindingMap().gamepad)) {
+    for (const name of names) {
+      if (name.startsWith("leftx") || name.startsWith("lefty")) continue;
+      expect(GAMEPAD_BUTTON_NAMES.includes(name)).toBe(true);
+    }
+  }
+});
+
+test("buildGamepadToAction: only one of the shell's own actions keeps a button, and the later binding wins", () => {
+  expect(buildGamepadToAction({ OpenMenu: ["misc1"], CycleNext: ["misc1"] }).get("misc1")).toEqual(["CycleNext"]);
+  expect(buildGamepadToAction({ CycleNext: ["misc1"], OpenMenu: ["misc1"] }).get("misc1")).toEqual(["OpenMenu"]);
+});
+
+test("buildGamepadToAction: one action bound to one button twice fires once", () => {
+  expect(buildGamepadToAction({ OpenMenu: ["misc1", "misc1"] }).get("misc1")).toEqual(["OpenMenu"]);
+});
diff --git a/packages/retroplug/test/input/keymap.test.ts b/packages/retroplug/test/input/keymap.test.ts
index ec2b7257b..60d050ea7 100644
--- a/packages/retroplug/test/input/keymap.test.ts
+++ b/packages/retroplug/test/input/keymap.test.ts
@@ -74,17 +74,47 @@ test("buildKeyToButton: unknown button names + unresolvable keys are skipped", (
   expect(m.size).toBe(1);
 });
 
-test("buildKeyToAction: the default app-action bindings invert to DPF-code → action", () => {
+test("buildKeyToAction: the default app-action bindings invert to DPF-code → a one-id list", () => {
   const m = buildKeyToAction(defaultBindingMap().keyboardActions);
-  expect(m.get(0x1b)).toBe("OpenMenu"); // Escape
-  expect(m.get(0x09)).toBe("CycleNext"); // Tab
+  expect(m.get(0x1b)).toEqual(["OpenMenu"]); // Escape
+  expect(m.get(0x09)).toEqual(["CycleNext"]); // Tab
   expect(m.get(0x5a)).toBe(undefined); // Z is a GB button, not an action key
 });
 
 test("buildKeyToAction: unknown action ids + unresolvable keys skipped; null-tolerant", () => {
   const m = buildKeyToAction({ OpenMenu: ["Escape"], Bogus: ["Enter"], CyclePrev: ["Nonsense"] });
-  expect(m.get(0x1b)).toBe("OpenMenu");
+  expect(m.get(0x1b)).toEqual(["OpenMenu"]);
   expect(m.get(0x0d)).toBe(undefined); // Bogus action id dropped
   expect(m.size).toBe(1); // CyclePrev's "Nonsense" doesn't resolve
   expect(buildKeyToAction(undefined).size).toBe(0); // an older profile lacking the section
 });
+
+test("buildKeyToAction: one key carries every action bound to it, in the profile's order", () => {
+  // The shell's Open Menu and two contributed actions on one key: a press fires all three, so the map keeps the list.
+  const m = buildKeyToAction(
+    { OpenMenu: ["Escape"], "lsdj.debug-overlay": ["Escape", "`"], "risa.debug-overlay": ["`"] },
+    ["lsdj.debug-overlay", "risa.debug-overlay"],
+  );
+  expect(m.get(0x1b)).toEqual(["OpenMenu", "lsdj.debug-overlay"]);
+  expect(m.get(0x60)).toEqual(["lsdj.debug-overlay", "risa.debug-overlay"]);
+});
+
+test("buildKeyToAction: a contributed id nobody passes as known is skipped, as an unknown one is", () => {
+  // A profile keeps an id after its action leaves the product: "known" is the shell's three plus the caller's list.
+  const profile = { "gone.action": ["`"], "lsdj.debug-overlay": ["`"] };
+  expect(buildKeyToAction(profile, ["lsdj.debug-overlay"]).get(0x60)).toEqual(["lsdj.debug-overlay"]);
+  expect(buildKeyToAction(profile).get(0x60)).toBe(undefined);
+});
+
+test("buildKeyToAction: only one of the shell's own actions keeps a key, and the later binding wins", () => {
+  // Two shell ids on one key is reachable from the editor (capturing Tab for Open Menu while Cycle Next keeps its
+  // Tab default). The old Map.set let the later one replace the earlier, so firing both would change what the key
+  // does today; a contributed id is not a shell id and still accumulates.
+  expect(buildKeyToAction({ OpenMenu: ["Tab"], CycleNext: ["Tab"] }).get(0x09)).toEqual(["CycleNext"]);
+  expect(buildKeyToAction({ CycleNext: ["Tab"], OpenMenu: ["Tab"] }).get(0x09)).toEqual(["OpenMenu"]);
+});
+
+test("buildKeyToAction: one action bound to one key twice fires once", () => {
+  // "Enter" and "Return" are one DPF code; a hand-edited profile listing both must not toggle the menu twice.
+  expect(buildKeyToAction({ OpenMenu: ["Enter", "Return"] }).get(0x0d)).toEqual(["OpenMenu"]);
+});
diff --git a/packages/retroplug/test/menu/leaves.test.ts b/packages/retroplug/test/menu/leaves.test.ts
index 4601c6531..4f83e8ec8 100644
--- a/packages/retroplug/test/menu/leaves.test.ts
+++ b/packages/retroplug/test/menu/leaves.test.ts
@@ -942,7 +942,9 @@ test("Keyboard Bindings: 8 GB + 3 app-action rows; capture rebinds write-through
   // Rebind Open Menu → M: written into keyboardActions; the action lookup follows.
   keyboardBindings(stores).find((r) => r.id === "bind-act-OpenMenu")!.capture!.onCapture("M");
   expect(stores.bindings.loadProfile(active)!.keyboardActions.OpenMenu).toEqual(["M"]);
-  expect(buildKeyToAction(stores.bindings.resolvedBindings().keyboardActions).get("M".charCodeAt(0))).toBe("OpenMenu");
+  expect(buildKeyToAction(stores.bindings.resolvedBindings().keyboardActions).get("M".charCodeAt(0))).toEqual([
+    "OpenMenu",
+  ]);
 
   // Clear A → the button unbinds.
   keyboardBindings(stores).find((r) => r.id === "bind-A")!.capture!.onClear();
@@ -1005,7 +1007,9 @@ test("Gamepad Bindings: gamepad-source capture rows; button + axis-token rebinds
   // Bind Cycle (Back) → a stick click: written into gamepadActions; the action lookup follows.
   gamepadBindings(stores).find((r) => r.id === "bind-gp-act-CyclePrev")!.capture!.onCapture("leftstick");
   expect(stores.bindings.loadProfile(active)!.gamepadActions.CyclePrev).toEqual(["leftstick"]);
-  expect(buildGamepadToAction(stores.bindings.resolvedBindings().gamepadActions).get("leftstick")).toBe("CyclePrev");
+  expect(buildGamepadToAction(stores.bindings.resolvedBindings().gamepadActions).get("leftstick")).toEqual([
+    "CyclePrev",
+  ]);
 
   // Clear A → unbinds; reset restores BOTH the default gamepad map and the app actions (preserving keyboard).
   gamepadBindings(stores).find((r) => r.id === "bind-gp-A")!.capture!.onClear();
````

Run: `sed -n '7527,7734p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/test/config/bindings.test.ts
packages/retroplug/test/input/gamepad.test.ts
packages/retroplug/test/input/keymap.test.ts
packages/retroplug/test/menu/leaves.test.ts
EOF
````

Expected:

````text
3f1623a859e0de9196042249d8a48445127794c7
3399b5b586eb9c40ed979c34de2f90da0667f545
60d050ea7116a6cdcebaa94e871727687f120f3c
4f83e8ec82a6f6590d5faa676fea2457002fe0d8
````

- [ ] **Step 6: The red proofs**

Three REDs. The generator's nine refusals and the ACTIONS order case were written first and run against STAGE 2's
generator: all ten fail, the ACTIONS ones with `got undefined` and the refusal modes with `Cannot read properties of
undefined (reading 'map')`. The fix pass's two map REDs are the old builders' answers, quoted as the log prints them.
The refusal RED that proves each case fails for ITS OWN missing check used an archive of stage 2's generator with the
ACTIONS export spliced in and no check, so every mode loads cleanly (`error: null`): the ten cases then fail for their
own reason and not for a crash.

Run: `bash -c 'cp packages/retroplug/scripts/lib/packages.mjs /tmp/f4c-task3-packages.mjs && git show HEAD:packages/retroplug/scripts/lib/packages.mjs > packages/retroplug/scripts/lib/packages.mjs && node packages/retroplug/scripts/run-tests.mjs packages/packages-generator > /tmp/f4c-s2gen.log 2>&1; e=$?; cp /tmp/f4c-task3-packages.mjs packages/retroplug/scripts/lib/packages.mjs; echo runner-exit-$e; grep -c '^not ok' /tmp/f4c-s2gen.log'`
Expected: the runner exits nonzero (`runner-exit-1` on this box) and `10`: with the PARENT task's generator in place (HEAD's, put back afterwards), the ACTIONS order case and the nine refusal cases are `not ok`, and nothing else fails. The recorded run is `/tmp/f4cpro/s3-red-actions.log`; its summary line is below.

The failing cases, quoted as the log prints them, and the suite's own line after them:

`# 95 passed, 0 skipped, 10 failed`: ten failures, the generator's other 95 cases green.

The two map REDs, quoted as printed (they are the stage 3 review's own two findings, against stage 3's first
builders):

The each-case-its-own-reason RED, over stage 2's generator with an unchecked `ACTIONS` export spliced in:

````text
== actions-duplicate-id
{"error":null}
== actions-duplicate-across
{"error":null}
== actions-bad-prefix
{"error":null}
== actions-unknown-key
{"error":null}
== actions-unknown-pad
{"error":null}
== actions-shell-key
{"error":null}
== actions-gb-key
{"error":null}
== actions-return-key
{"error":null}
== actions-gb-pad
{"error":null}
== actions
{"actions":[{"pkg":"zeta","id":"zeta.first","label":"Zeta First","keys":["`"],"pad":[]},{"pkg":"alpha","id":"alpha.second","label":"Alpha Second","keys":["`"],"pad":[]},{"pkg":"beta","id":"beta.third","label":"Beta Third","keys":[],"pad":["guide"]}]}
````

Run: `sed -n '7782,7801p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `40d1c97c149b275c0b7d7a8247d393327f192e246e5992884755dfaa41c69341  -`: the log's own bytes (`/tmp/f4cpro/s4fix-red-refusals.log`), so the block above is the run and not a selection from it. every refusal mode loads with `error: null`, so each case below fails for the check it is missing rather than for a crash; the last line is the order case, which reports the fixture product list's three actions.

- [ ] **Step 7: Configure, install, build, and the bytes that must not move**

Run: `cmake -S . -B build`
Expected: exit 0: no manifest change (no product carries an action yet), so the configure only re-reads the product list and the generator's inputs.

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and the lockfile is Task 1's: this task adds no dependency.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree: the UI bundle gains the dispatch and the rows, and the generated menus module is rewritten.

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

- [ ] **Step 8: Run the suites**

Task 3's figures are the fixed stage 3's own full run (`/tmp/f4cpro/s3fixv-*.log`, every leg exit 0, no retries),
whose tree equals this task's commit: the fix pass's `addActionId` (one shell action per key), one
`ContributedAction`, the code-collision case and the readout's `null`. One kit attempt before it failed the launchpad
`retroplug-launchpad-test` mid-drain swap case under the kit's load; the leg passed alone and the whole kit passed on
the re-run (the same load sensitivity F4b recorded).

The verification is the parallel kit, one command, run in the foreground and waited for: it sets each leg's own
`RETROPLUG_RESOURCES_DIR`, skips `sdl:controller` (which claims system-wide ALSA ports) and logs one file per leg. Run
`pnpm sdl:controller` on its own afterwards, never another way, and compare the summary and the gated logs with the
baseline at the design commit.

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4c-v bash .superpowers/verify/tip-verify-par.sh`
Expected: exit 0 and `== every leg passed`, with no `/tmp/f4c-v-retries.txt` (a retry is a leg the kit re-ran; report it). The base's run is `/tmp/f4cpro/base-*` and every leg below quotes this task's own log.

Run: `pnpm sdl:controller`
Expected: exit 0: `SDL controller OK`, after fifteen `ok:` lines. The first ten are the bliptoaster pack's toolchain, which the SDL build runs and prints (the same ten the baseline's log has), and the last five are the harness's own legs, byte-identical to the baseline's. It sets `RP_MIDI_PORT_ALLOW` itself, from the two fakes' ready lines, and refuses a caller's list. The five:

````text
  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream
  ok: reclaimed before MIDI opens and audio starts; its notes kept out of the shared stream; released at quit
  ok: Programmer mode first, LED traffic, the release last
  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside
  ok: a second standalone leaves the first one's device alone, and does not hear its pads
```` This is the one command in this plan that may touch a MIDI port at all, and it sets `RP_MIDI_PORT_ALLOW` from its two ALSA fakes itself.

Run: `bash .superpowers/verify/compare.sh /tmp/f4cpro/base /tmp/f4c-v`
Expected: exit 1 (anything differs), printing only the lines this task is expected to move: the `test` case and file counts, `ui`'s from Task 4, Task 1's two `typecheck` programs and the `measure` sizes (info only, never gated). `imagesha` and `packbytes` must be ABSENT from the diff, and every other leg's counts must be the baseline's. A difference the task does not name is a finding.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree. No compiler warning is expected: every stage's build log has none.

Run: `pnpm typecheck`
Expected: `Scope: 25 of 26 workspace projects`, then `packages/retroplug typecheck: Done`: every project with a `typecheck` script typechecks on its own tsconfig, and the shell's program covers every package's `ts/` as well and `packages/ui-react typecheck: Done` and `packages/ui-fixture typecheck: Done` (Task 1's two packages, unchanged). The baseline's scope is `Scope: 23 of 24 workspace projects` with 17 `Done` lines; this tree's is 25 of 26 and 19.

Run: `pnpm test`
Expected: `# 225 test file(s) passed (jobs=5)`. The runner's own line counts FILES; the case total is the stage 4 report's table: `the test counts (2286 -> 2349, 219 -> 225 files)`, since no runner prints one in this suite. The pool size it prints is the kit's `TV_TS_JOBS` (5); a hand run prints its own, so the file count is what must match.

Run: `pnpm test:plugin`
Expected: `# 13 plugin test binary(ies) passed (jobs=5)`: every binary exits 0, and the counts are (quoted as the log prints them, the engine-graph suite's assertion total and its eleven skips as that binary prints them):

````text
retroplug-mesen-test: `All tests passed (82321 assertions in 80 test cases)`
retroplug-watcher-test: `All tests passed (6 assertions in 2 test cases)`
retroplug-audio-test: `All tests passed (378 assertions in 16 test cases)`
retroplug-launchpad-test: `All tests passed (12925 assertions in 53 test cases)`
retroplug-classid-test: `All tests passed (10 assertions in 3 test cases)`
retroplug-plugin-test: `All tests passed (21 assertions in 6 test cases)`
retroplug-midi-test: `All tests passed (78 assertions in 24 test cases)`
retroplug-dynparams-test: `All tests passed (24 assertions in 7 test cases)`
retroplug-lottie-test: `All tests passed (7 assertions in 1 test case)`
retroplug-graph-test: `All tests passed (356662 assertions in 296 test cases)`
retroplug-sameboy-test: `All tests passed (1152140 assertions in 7 test cases)`
retroplug-device-mode-test: `All tests passed (1504408 assertions in 96 test cases)`
retroplug-engine-graph-test: `test cases: 336 | 325 passed | 11 skipped`
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1715487 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the fixture-gated cases run.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, none skipped, nothing re-recorded: 30 files and 250 cases (the reports' count), the device images' and the pack's bytes identical to the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 144 case(s) SKIPPED in 65 file(s)` (its list of files follows) and the skip check passing: CI's view, its skips the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 43 UI test file(s) passed (jobs=4)`. The case total is the stage 1, 2 and 3 reports': `43 files, 49 cases`, unchanged by the three stages (no product gains a view or an action until Task 4) (a case that cannot run SKIPS and is counted, and this leg runs WITH the fixtures).

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks.

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

Run: `pnpm test:n8`
Expected: `All tests passed (1468 assertions in 104 test cases)` (`retroplug-n8-test`) and `All tests passed (17 assertions in 2 test cases)` (`retroplug-serial-test`), the baseline's: fakes only, no port is opened (the `[retroplug] N8 restore: port '/dev/ttyACM0'` line it prints is a test case's, over a fake port factory and a scratch n8.cfg; the baseline's run printed it too).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed (the counts are the reports' tables, each stage's).

Run: `bash -c 'for t in render-menu render-badge; do pnpm test:ui $t || exit 1; done'`
Expected: exit 0: `render-menu` and `render-badge`, one test file each and 1 passed.

Run: `pnpm test render`
Expected: `# 9 test file(s) passed (jobs=5)`: the render worker's own files.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after the smoke's eight `ok:` lines (the build before them prints the bliptoaster pack's toolchain lines, which start `ok:` too). It runs under a scratch config dir with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which matches nothing, so it claims and opens nothing.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three ThreadSanitizer binaries, built from this tree's sources (their objects are the prototype's until the first build here).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1701604 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`.

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'`
Expected: `All tests passed (2089 assertions in 53 test cases)`.

Run: `bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'`
Expected: `All tests passed (1499768 assertions in 51 test cases)`.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three AddressSanitizer binaries, built from this tree's sources.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1682117 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356662 assertions in 296 test cases)`.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'`
Expected: `All tests passed (1504408 assertions in 96 test cases)`. The prototype's first full run failed this leg once, on the script pack's deadline case under ASAN on a loaded machine (no device-mode code touched by any stage), and its re-run was green; every stage's recorded run after it is green. If yours fails, re-run it ONCE and report both.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (1504408 assertions in 96 test cases)`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Then the per-stage tree rule, which the prototype adopted after a broken intermediate commit: the tree this task
commits is extracted from the commit AFTER the commit step (so it is the sealed tree), proven equal to it by a fresh
index read from the commit, installed, generated, typechecked and tested on its own. Nothing here links this
worktree's `build/`; the pure runner reads the tjs binary through `RETROPLUG_TJS` instead.

Run: `bash -c 'D=/tmp/f4c-tree-3; rm -rf "$D" && mkdir -p "$D" && git archive c956580c4 | tar -x -C "$D" && echo extracted'`
Expected: `extracted`: the commit's tree, with no generated module in it (they are gitignored and `git archive` does not carry them) and no `build/`.

Run: `bash -c 'D=/tmp/f4c-tree-3; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree c956580c4 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-3-eq.txt; e=$?; rm -f $E; wc -l < /tmp/f4c-tree-3-eq.txt'`
Expected: `0`: the extracted tree is its commit's, line for line, read against a fresh index because this worktree's own index holds a later task's deletions. `deps` is excluded: its submodule directories are links, which is the next step's doing.

Run: `bash -c 'D=/tmp/f4c-tree-3; W=$PWD; for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done; cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo wired'`
Expected: `wired`: the archive's submodule paths are links to this worktree's checkouts (read only) and its own offline install wires the workspace's links from the COMMIT's lockfile, which is also what proves that lockfile installs.

Run: `bash -c 'D=/tmp/f4c-tree-3; T=/workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs; cd "$D" && pnpm -r typecheck && RETROPLUG_TJS=$T node packages/retroplug/scripts/run-tests.mjs menu && node packages/retroplug/scripts/run-tests.mjs packages && node packages/retroplug/scripts/run-tests.mjs hosts && node packages/retroplug/scripts/run-tests.mjs ui && node packages/retroplug/scripts/run-tests.mjs input && node packages/retroplug/scripts/run-tests.mjs config && node packages/retroplug/scripts/run-tests.mjs boundaries'`
Expected: exit 0: every package typechecks against its own tsconfig, and each named suite passes on this tree alone, with the worktree's tjs binary read through `RETROPLUG_TJS` and nothing written to this worktree. The per-tree record, which this plan's generator ran for all five trees (each extract equal to its commit before and after, 19 `Done` lines, every suite exit 0): tree 1 `menu` 17 files, `packages` 2, `hosts` 2, `boundaries` 3, `ui-react/reactView` 1 and `script/bundle-ui-split` 1; tree 2 `menu` 17, `packages` 2, `hosts` 2, `ui` 4 and `boundaries` 3; tree 3 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `input` 3, `config` 6 and `boundaries` 3; tree 4 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `lsdj/ui-views` 1 and `risa/ui-views` 1; tree 5 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `boundaries` 2 and `source-literals` 1.

Run: `bash -c 'D=/tmp/f4c-tree-3; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree c956580c4 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-3-eq2.txt; rm -f $E; wc -l < /tmp/f4c-tree-3-eq2.txt'`
Expected: `0`: the install, the generator and the suites changed no tracked file, so the tree is still its commit's after the runs (the generated modules and the runner's build dir are ignored).

- [ ] **Step 9: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M packages/retroplug/scripts/lib/packages.mjs
 M packages/retroplug/src/bindingMap.ts
 M packages/retroplug/src/keyCodes.ts
 M packages/retroplug/test/config/bindings.test.ts
 M packages/retroplug/test/input/gamepad.test.ts
 M packages/retroplug/test/input/keymap.test.ts
 M packages/retroplug/test/menu/leaves.test.ts
 M packages/retroplug/test/packages/fixtures/expected/menus.ts
 M packages/retroplug/test/packages/packages-generator.test.ts
 M packages/retroplug/test/packages/run-generator.mjs
 M packages/retroplug/test/ui/view-session.test.ts
 M packages/retroplug/ui/App.tsx
 M packages/retroplug/ui/host/menuContributions.ts
 M packages/retroplug/ui/screens/menu/menuDefs.ts
 M packages/retroplug/ui/views/useViewSession.ts
 M packages/retroplug/ui/views/viewSession.ts
 M packages/ui/ts/contribution.ts
?? packages/retroplug/test/input/app-actions.test.ts
?? packages/retroplug/test/menu/action-rows.test.ts
?? packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts
?? packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts
?? packages/retroplug/ui/input/appActions.ts
````

````bash
git add -- packages/retroplug/scripts/lib/packages.mjs packages/retroplug/src/bindingMap.ts \
  packages/retroplug/src/keyCodes.ts packages/retroplug/test/config/bindings.test.ts \
  packages/retroplug/test/input/app-actions.test.ts packages/retroplug/test/input/gamepad.test.ts \
  packages/retroplug/test/input/keymap.test.ts packages/retroplug/test/menu/action-rows.test.ts \
  packages/retroplug/test/menu/leaves.test.ts packages/retroplug/test/packages/fixtures/expected/menus.ts \
  packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/bindingMap.ts \
  packages/retroplug/test/packages/fixtures/tree/packages/retroplug/src/keyCodes.ts \
  packages/retroplug/test/packages/packages-generator.test.ts packages/retroplug/test/packages/run-generator.mjs \
  packages/retroplug/test/ui/view-session.test.ts packages/retroplug/ui/App.tsx \
  packages/retroplug/ui/host/menuContributions.ts packages/retroplug/ui/input/appActions.ts \
  packages/retroplug/ui/screens/menu/menuDefs.ts packages/retroplug/ui/views/useViewSession.ts \
  packages/retroplug/ui/views/viewSession.ts packages/ui/ts/contribution.ts
git commit -F - <<'EOF'
feat(f4c): app actions, contributed bindings and the shell's dispatch

@retroplug/ui/contribution gains UiAction (id, label, default key and pad names, run(host, ctx)) and
UiContribution.actions. An id is "<package>.<name>": a profile stores it, so it names its owner, and the generated
registry refuses an id its own package does not prefix. run() gets the focused system, or null on the start screen.

The binding vocabulary: keyCodes.ts gains GAMEPAD_BUTTON_NAMES, SDL's canonical button names as the native
GamepadManager's GamepadManager.cpp emits them (and without the stick half-axis tokens, which the axis bus dispatches
no app action for). buildKeyToAction and buildGamepadToAction now answer a LIST of action ids per key or button, in the
order the profile gives them, since one press can carry the shell's action and a package's; "known" is the shell's three
plus the ids the caller passes, so a stored id whose action left the product is skipped as an unknown one is. A key
keeps at most ONE of the shell's own ids, a later shell binding replacing the earlier in place exactly as the old
Map.set did, so a profile with OpenMenu and CycleNext on Tab fires what it fires today; contributed ids accumulate
after it, and an id bound to one key twice (a hand-edited OpenMenu: ["Enter", "Return"]) fires once.

The generated registry: ui/generated/menus.ts gains ACTIONS in product order, and its load-time checks refuse an action
id declared twice (naming the package, or both), an id its own package does not prefix, a default key name
resolveKeyName does not resolve or a pad name outside GAMEPAD_BUTTON_NAMES, and a default that collides with one the
shell already binds (a shell action's or a Game Boy button's, keyboard and gamepad), since a key on both would fire the
action AND the joypad. Two contributions on one key are allowed: the two overlays' backtick is one. The collision is
checked by resolved code, not by spelling, so "Return" is refused against the Game Boy Start default "Enter" (0x0d).
All nine refusals are RED first: with the previous generator's generated module given an unchecked ACTIONS export, each
refusal mode loads cleanly (error null), so each case fails for its own absent check rather than for a missing export.
The fixture tree gains two src/*.ts shims that re-export the real shell's keyCodes and bindingMap, so the checks read
the real vocabulary and defaults. The generated ACTIONS list is also the one the shell reads (menuContributions.ts
imports it beside VIEWS, with one ContributedAction type in @retroplug/ui/contribution).

The dispatch: ui/input/appActions.ts runs one press's ids in order, a shell id through App's own body and a contributed
id through its run, each contributed one in a try that reports ("[actions] <id>: its run threw: ...") and carries on, so
one package's bug cannot swallow the shell's menu key on the same press. App's key and gamepad handlers dispatch the
resolved list in the states the backtick fires today (after Esc and the overlay check, not gated on a menu modal); the
backtick line stays App's own in this stage, and no product contributes an action yet, so the bindings menus show
exactly the rows they showed before and the recorded menu matrix does not move.

The profile default fill: withActionDefaults(map, actions) in src/bindingMap.ts sets every contributed id absent from a
section to the action's default (keys ?? [], pad ?? []), an id present being the user's, [] included. App applies it
before building both maps and bindingsChildren before reading the action map; the store, the file and the reset row are
untouched, so a profile is not rewritten until the user edits a binding and "Reset to Defaults" leaves the contributed
ids reading their defaults again.

The bindings menus: after the shell's three rows, one capture row per contributed action in registration order, ids
bind-act-<id> / bind-gp-act-<id> (a dot and all), labels as the neighbouring rows read (the action's label, then the
bound names or "-"), the same capture plumbing and the same reset. The contributed list reaches the composer through
the menu context, so the rows are driven here over a test action no product carries.

Tests: 27 new cases (9 generator refusals and the ACTIONS order, 4 dispatch, 3 bindings rows over a test action,
1 withActionDefaults, 6 key/gamepad map cases including GAMEPAD_BUTTON_NAMES, and the reader's null when no session
lives), and the 2 leaves cases that read the maps now read a one-element list. A read that finds no live session now
answers null rather than undefined, so a dropped readout clears instead of keeping its last lines, and its report names
the view. This tree's pure suite is 225 files / 2349 cases, 0 skipped; test:ui is unchanged (43 files, 49 cases, the
bindings and app-actions cases included); the menu matrix is unchanged; check:boundaries and check:docs are clean. The
UI bundle grows 2011773 -> 2016720 bytes; the cp, cli and render-worker bundles do not move.

The full parallel verification passes every leg with no retries, and sdl:controller passes on its own; imagesha and
packbytes are byte-identical to the baseline, whose only other differences are this phase's test counts and stage 1's
two typecheck programs.

Decisions: a key default is checked by its resolved DPF code rather than by its spelling, so "Return" collides with the
shell's Enter and "z" with Z's; the pad table is data (a stick token is not a button and the axis bus dispatches no
action); the registry imports the shell's keyCodes and bindingMap directly rather than through a copy, which the
fixture tree mirrors with two one-line shims. A menu row id with a dot needed no change anywhere: Menu keys rows by the
whole string (Maps, Sets and React keys) and never splits or parses an id, navTo finds a row by its label text, and the
composer's uniqueness check is over the whole string.
EOF
````

---

### Task 4: the LSDj HD player and the two overlays are the packages'

Design sections 6 and 7, and the design's fourth stage. The five ratchet files and the two cart-free neighbours become
plain TypeScript in `packages/lsdj/ts/ui/` and `packages/risa/ts/ui/`: a `pixels` window view and a `readout` tile view
per cart, each cart's own `createSignal` flag and its action. The shell deletes its seven view files, its interim
`lsdj-hd` entry, `uiViews` and `MenuContext.openLsdjHd`, and after this commit `ui/` names no cart. E1 is recorded here,
when the two real actions exist, and the four fixture-gated `test:ui` cases pin the views and the action dispatch on a
real cart.

One non-move is in this stage and is named in the header: the `PixelView` canvas guard, which stage 4's `lsdj-hd` case
found (the HD canvas flushes and diffs, so a frame read before the ref exists stays black). It is stage 2's code in this
plan, because a stage that is not green on its own is a stage the plan cannot replay; this task's tree carries it in
`PixelView.tsx`, which Task 2 places.

**Files:**

- Create (11): `packages/lsdj/ts/test/ui-views.test.ts`, `packages/lsdj/ts/ui/debugOverlay.ts`, `packages/lsdj/ts/ui/overlayView.ts`, `packages/retroplug/test-ui/hd-view-closes.test.ts`, `packages/retroplug/test-ui/overlay-actions.test.ts`, `packages/retroplug/test-ui/overlay-click-through.test.ts`, `packages/retroplug/test-ui/overlay-survives-menu.test.ts`, `packages/risa/ts/test/ui-views.test.ts`, `packages/risa/ts/ui/debugOverlay.ts`, `packages/risa/ts/ui/overlayView.ts`, `packages/ui/ts/format.ts`
- Move (1 file, each by the `git mv` its step prints; 1 of them changed too and carries that file's diff after its move): from `packages/retroplug/ui/screens/hd/`
- Modify (33 files, as one diff block per group its step names: 15 under `packages/retroplug/test/menu/`; 2 under `packages/retroplug/scripts/`; 2 under `packages/retroplug/ui/host/`; 2 under `packages/ui-react/ts/ui/`; 1 under `docs/`; 1 under `packages/lsdj/` and the rest)
- Delete (6): `packages/retroplug/ui/screens/grid/LsdjOverlay.tsx`, `packages/retroplug/ui/screens/grid/RisaOverlay.tsx`, `packages/retroplug/ui/screens/grid/lsdjDebug.ts`, `packages/retroplug/ui/screens/grid/useLsdjRuntime.ts`, `packages/retroplug/ui/screens/grid/useRisaRuntime.ts`, `packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx`

- [ ] **Step 1: LSDj: the HD session moved, and its two views and action**

`hdView.ts` is `useLsdjHdSession.ts` moved (`git mv`, so history follows it) and rewritten into a `WindowView`: id
`lsdj-hd`, `when` the `lsdj-sync` role, a `pixels` body whose `unsupported` text is the old screen's. `HdSession` is
the old class: the 0x150 header through `host.files.readPrefix`, the whole ROM through `host.files.readFile`, the
lsdj-assets overrides folded in through `applyOverridesToRom(base, overrides, host)` (the host's `files` and `png`, in
place of `filesPngHost(backend)`), `readSram` on the same 15-frame countdown with the same FNV signature over the
first `0x8000` bytes, `readRam` per frame, and `frame()` = `renderFrame()` plus `{ width, height }` from the canvas.
`open` refuses what the old hook gated on itself (no `lsdj-sync` role, an empty ROM path) and also an unreadable
header or a ROM `LsdjRom` refuses, because the shell checks `when` only at `views.open` and a renderer can reach
`open` with any system; the system's `lsdj-assets` config comes from `host.systems.view()` by id. `overlayView.ts` is
the tile view `lsdj-overlay`: the old hook's sampler as plain code over `host.files.readPrefix` and
`host.systems.readRam`, the old three lines verbatim (with `versionStr`, `channelCell` and `hex2` from
`@retroplug/ui/format`), `null` for an unsupported reader, `undefined` for unreadable RAM, and the DEFAULT signature
(the lines and the flag are exactly what the strip draws, so the old hand-written key bought nothing visible).
`debugOverlay.ts` is the package's own `createSignal(false)` (off at start, not persisted) and the action
`lsdj.debug-overlay` (label `LSDj Debug Overlay`, keys `["`"]`, no pad), whose `run` ignores `ctx.systemId` so it
works on the start screen as the backtick did. `index.ts` declares `defineUi({ trackers: [LSDJ_TRACKER_UI], views:
[hdView, overlayView], actions: [lsdjDebugOverlay] })`; the HD Player row is unchanged (`host.views.open("lsdj-hd",
sys.id)`). The views take the `Host<...>` groups they read and are declared with `satisfies`, so the package's test
needs no cast.

The moves, as one script (each `git mv` stages its move, and `set -e` stops at the first that fails):

````bash
mkdir -p packages/lsdj/ts/ui
set -e
git mv packages/retroplug/ui/screens/hd/useLsdjHdSession.ts packages/lsdj/ts/ui/hdView.ts
````

Run: `bash -c 'git status --porcelain --untracked-files=no | grep -c "^R"'`
Expected: `1`: every move of this task so far is staged (1 of them here), and none failed.

Its change (`packages/retroplug/ui/screens/hd/useLsdjHdSession.ts` -> `packages/lsdj/ts/ui/hdView.ts`, 59% similar), as a diff of the new path:

````diff
diff --git a/packages/lsdj/ts/ui/hdView.ts b/packages/lsdj/ts/ui/hdView.ts
index d55f8f9dd..75479765c 100644
--- a/packages/lsdj/ts/ui/hdView.ts
+++ b/packages/lsdj/ts/ui/hdView.ts
@@ -1,28 +1,28 @@
-// The HD player's data pump: everything the renderer needs for one system, kept fresh at the right rate.
+// The HD player as a window view (docs/modular-graph-phase-f4c-design.md section 6.1): the song order, all four
+// channels' chains and all four channels' phrases on one full-window screen, drawn in the cartridge's own font and
+// palette. The shell renders the pixels; this module only says how to open the session and what to show when there is
+// nothing to read.
 //
 // Three inputs, on three very different clocks:
 //   - The ROM assets (font, palette, kit sample names) change only when the cartridge does, so the
-//     LsdjRom view is built once per system and the kit names are memoised on first use.
+//     LsdjRom view is built once per session and the kit names are memoised on first use.
 //   - The runtime state (playback positions, screen, cursor) changes every frame, and reading it is cheap
-//     (backend.readRam → LsdjReader), so it is sampled per frame like LsdjOverlay does.
-//   - The working song changes only when the user EDITS, but reading it is expensive: backend.readSram
+//     (host.systems.readRam -> LsdjReader), so it is sampled per frame like the overlay does.
+//   - The working song changes only when the user EDITS, but reading it is expensive: host.systems.readSram
 //     copies the whole 128 KiB image and decodeSong builds ~700 objects. So it is polled a few times a
 //     second and only re-decoded when the working-song region actually changed.
 //
-// The result is exposed as `renderFrame()`, which returns the pixel buffer when something moved and null
-// when it didn't - so the caller can skip pushing an unchanged surface into LVGL.
-
-import { useMemo } from "react";
-
-import { useStores, useSystem } from "../../stores/useStores";
+// `frame()` returns the pixel buffer when something moved and null when it didn't, so the shell can skip pushing an
+// unchanged surface into LVGL.
+import type { Host } from "@retroplug/core/host";
 import { hasRole, roleConfig } from "@retroplug/core/systemRoles";
-import { LsdjReader } from "@retroplug/lsdj/runtime";
-import { decodeSong, type Song } from "@retroplug/lsdj";
-import { LsdjRom } from "@retroplug/lsdj/rom";
-import { applyOverridesToRom, readOverrides } from "@retroplug/lsdj/lsdjAssetsRole";
-import { filesPngHost } from "../../../src/hostGroups";
-import { LsdjHdCanvas, HD_COLS, HD_ROWS, renderMode2, type KitSampleNameLookup } from "@retroplug/lsdj/hd";
-import type { ControlPlaneBackend } from "../../../src/backend";
+import type { PixelFrame, WindowView } from "@retroplug/ui/contribution";
+import { HD_COLS, HD_ROWS, LsdjHdCanvas, renderMode2, type KitSampleNameLookup } from "../hd";
+import { decodeSong } from "../codec/song";
+import type { Song } from "../model";
+import { LsdjRom } from "../rom";
+import { LsdjReader } from "../runtime";
+import { applyOverridesToRom, readOverrides } from "../lsdjAssetsRole";
 
 /** The working song is the first 0x8000 bytes of the sav image (codec/sav.ts's kWorkingSong..kSongBytes). */
 const WORKING_SONG_BYTES = 0x8000;
@@ -44,7 +44,9 @@ function songSignature(sav: Uint8Array): number {
   return h;
 }
 
-class HdSession {
+/** One system's HD data pump. The shell opens it, reads `frame()` per frame and closes it when the window goes; it
+ *  owns no React state. Exported so the cadence is tested over a fake host, with no display. */
+export class HdSession {
   readonly canvas = new LsdjHdCanvas(HD_COLS, HD_ROWS);
   song: Song | null = null;
 
@@ -55,7 +57,7 @@ class HdSession {
   private readonly kitNames = new Map<number, string>();
 
   constructor(
-    private readonly backend: ControlPlaneBackend,
+    private readonly host: Host<"systems">,
     private readonly systemId: number,
     private readonly reader: LsdjReader,
     private readonly rom: LsdjRom,
@@ -78,7 +80,7 @@ class HdSession {
     if (this.pollCountdown-- > 0) return;
     this.pollCountdown = SONG_POLL_FRAMES;
 
-    const sav = this.backend.readSram(this.systemId);
+    const sav = this.host.systems.readSram(this.systemId);
     if (!sav || sav.length < WORKING_SONG_BYTES) return;
 
     const sig = songSignature(sav);
@@ -109,12 +111,19 @@ class HdSession {
     }
   }
 
+  /** The next frame, with the size the canvas lays it out in; null when nothing moved. What the shell's pixels
+   *  renderer reads, so the session is its own PixelSession. */
+  frame(): PixelFrame | null {
+    const pixels = this.renderFrame();
+    return pixels ? { pixels, width: this.canvas.width, height: this.canvas.height } : null;
+  }
+
   /** Draw one frame. Returns the pixel buffer if anything changed, else null. */
   renderFrame(): Uint32Array | null {
     this.pollSong();
     if (!this.song || !this.canvas.ready) return null;
 
-    const wram = this.backend.readRam(this.systemId);
+    const wram = this.host.systems.readRam(this.systemId);
     if (!wram) return null;
 
     renderMode2(this.canvas, this.song, this.reader.read(wram), this.kitSampleName);
@@ -124,47 +133,44 @@ class HdSession {
 
 /** Build the HD session for `systemId`, or null when the system isn't a supported LSDj cart. */
 function buildSession(
-  backend: ControlPlaneBackend,
+  host: Host<"files" | "png" | "systems">,
   systemId: number,
   romPath: string,
   overrideConfig: Record<string, unknown> | undefined,
 ): HdSession | null {
-  const header = backend.readFilePrefix(romPath, 0x150);
+  const header = host.files.readPrefix(romPath, 0x150);
   if (!header) return null;
   const reader = LsdjReader.fromHeader(header);
   if (!reader.supported) return null;
 
-  const base = backend.readFile(romPath);
+  const base = host.files.readFile(romPath);
   if (!base) return null;
   // Fold in the lsdj-assets overrides so the HD view reads the SAME effective ROM the emulator was
   // constructed with - otherwise a replaced font or palette would show on the cart but not here.
   const overrides = readOverrides(overrideConfig);
-  const effective = overrides.length ? applyOverridesToRom(base, overrides, filesPngHost(backend)) : base;
+  const effective = overrides.length ? applyOverridesToRom(base, overrides, host) : base;
   const rom = LsdjRom.fromBytes(effective);
   if (!rom.isLsdj) return null;
 
-  return new HdSession(backend, systemId, reader, rom);
+  return new HdSession(host, systemId, reader, rom);
 }
 
-export interface LsdjHdSession {
-  /** Draw the next frame; null when nothing moved (so the caller skips the LVGL push). */
-  renderFrame(): Uint32Array | null;
-  readonly canvas: LsdjHdCanvas;
-}
-
-/**
- * The HD render session for one system, or null when it isn't a supported LSDj cart (the caller shows a
- * message instead). `enabled` gates the build, so a closed screen costs nothing.
- */
-export function useLsdjHdSession(systemId: number, enabled = true): LsdjHdSession | null {
-  const { backend } = useStores();
-  const sys = useSystem(systemId);
-  const romPath = sys?.romPath ?? "";
-  const isLsdj = hasRole(sys?.roles, "lsdj-sync");
-  const overrideConfig = roleConfig(sys?.roles, "lsdj-assets");
-
-  return useMemo(() => {
-    if (!enabled || !isLsdj || !romPath) return null;
-    return buildSession(backend, systemId, romPath, overrideConfig);
-  }, [enabled, isLsdj, romPath, systemId, backend, overrideConfig]);
-}
+/** The playable window view: it applies to an LSDj-sync system, and its `open` refuses a system with no such role or
+ *  no ROM path itself, since the shell checks `when` only at `views.open` and a renderer opens a session too. `satisfies`
+ *  rather than an annotation, so `open` keeps the host groups it declares (what a pack copied from this one sees)
+ *  instead of widening to the contract's whole UI host. */
+export const hdView = {
+  id: "lsdj-hd",
+  label: "HD Player",
+  placement: "window",
+  when: (sys) => hasRole(sys.roles, "lsdj-sync"),
+  body: {
+    kind: "pixels",
+    unsupported: "HD view needs a supported LSDj cartridge",
+    open: (host: Host<"files" | "png" | "systems">, systemId: number) => {
+      const sys = host.systems.view().find((s) => s.id === systemId);
+      if (!sys || !hasRole(sys.roles, "lsdj-sync") || !sys.romPath) return null;
+      return buildSession(host, systemId, sys.romPath, roleConfig(sys.roles, "lsdj-assets"));
+    },
+  },
+} satisfies WindowView;
````

Run: `sed -n '8214,8385p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/lsdj/ts/ui/hdView.ts`
Expected: `75479765c5c1a9e8b5b2f1241bd5bf48a86e3a3e`: the blob b841dc4e2 holds for this file, so the applied diff is the prototype's change to it, byte for byte.

````diff
diff --git a/packages/lsdj/package.json b/packages/lsdj/package.json
index 10a2a7cb7..27f1e0e4c 100644
--- a/packages/lsdj/package.json
+++ b/packages/lsdj/package.json
@@ -6,7 +6,7 @@
     "typecheck": "node ../retroplug/scripts/lib/packages.mjs && tsc -p tsconfig.json"
   },
   "type": "module",
-  "description": "LSDj's half of the app: the .sav/.lsdsng/.lsdprj codecs and the live/codec model they carry, the ROM asset codec (kits, palettes, fonts), the runtime layout reader and drift detector, the playback predictor, the HD screen model, the lsdj-sync and lsdj-assets roles, its Songs/Assets tracker integration, its kit bank format, its ROM provider, and the rp.lsdj.sync (span label 21) and rp.lsdj.serial-out (23) protocol nodes with the Arduinoboy/kernel-message helpers they read.",
+  "description": "LSDj's half of the app: the .sav/.lsdsng/.lsdprj codecs and the live/codec model they carry, the ROM asset codec (kits, palettes, fonts), the runtime layout reader and drift detector, the playback predictor, the HD screen model, the lsdj-sync and lsdj-assets roles, its Songs/Assets tracker integration, its kit bank format, its ROM provider, its two views (the \"lsdj-hd\" full-window HD player and the \"lsdj-overlay\" debug readout) with the \"lsdj.debug-overlay\" action that toggles the readout, and the rp.lsdj.sync (span label 21) and rp.lsdj.serial-out (23) protocol nodes with the Arduinoboy/kernel-message helpers they read.",
   "exports": {
     ".": "./ts/index.ts",
     "./app": "./ts/app.ts",
diff --git a/packages/lsdj/ts/ui/index.ts b/packages/lsdj/ts/ui/index.ts
index 10c879b39..9f7c8fadf 100644
--- a/packages/lsdj/ts/ui/index.ts
+++ b/packages/lsdj/ts/ui/index.ts
@@ -26,6 +26,9 @@ import { planLsdprjImport } from "../lsdjLsdprjImport";
 import { lsdjIntegration } from "../lsdjIntegration";
 import { LSDJ_MODE_VALUES } from "../enums";
 import { LsdjRom, decodeLsdpal, encodeLsdpal } from "../rom";
+import { hdView } from "./hdView";
+import { overlayView } from "./overlayView";
+import { lsdjDebugOverlay } from "./debugOverlay";
 import { activeSlot as lsdjActiveSlot, decompressSlot, savSongName, savSongVersion } from "../codec/sav";
 import { encodeLsdsngRaw } from "../codec/lsdsng";
 
@@ -283,4 +286,8 @@ export const LSDJ_TRACKER_UI: TrackerUi = {
   },
 };
 
-export default defineUi({ trackers: [LSDJ_TRACKER_UI] });
+export default defineUi({
+  trackers: [LSDJ_TRACKER_UI],
+  views: [hdView, overlayView],
+  actions: [lsdjDebugOverlay],
+});
````

Run: `sed -n '8395,8431p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/lsdj/package.json
packages/lsdj/ts/ui/index.ts
EOF
````

Expected:

````text
27f1e0e4c661c00af478bea335378197c19a5bf6
9f7c8fadf27244f3696083390d19a2385426494e
````

Create `packages/lsdj/ts/ui/overlayView.ts`:

````ts
// A compact live readout of an LSDj instance's runtime state, layered over its emulator tile: the visible screen and
// its cursor position, play/stop, song position, tempo, the ROM version, and each channel's playing phrase.row. It is
// a readout view, so the shell draws the strip and this module only opens the sampler (docs/modular-graph-phase-f4c
// design section 6.1).
//
// The sampler is the old hook's body as plain code: the reader is built once from the ROM header, then every frame
// pulls the per-block WRAM snapshot (host.systems.readRam, safe while the core plays) and decodes it. Null means the
// cart is not supported, so nothing shows; undefined means this frame's RAM was unreadable, so the strip keeps what
// shows.
import type { Host } from "@retroplug/core/host";
import { hasRole } from "@retroplug/core/systemRoles";
import { hex2 } from "@retroplug/ui/format";
import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
import { LsdjReader, type LsdjState } from "../runtime";
import { lsdjDebug } from "./debugOverlay";

/** "9.4.2" / "9.2.L-aboy" from the parsed version (patchLabel keeps the raw component, e.g. "2" or "L"). */
function versionStr(v: LsdjState["version"]): string {
  if (!v) return "?";
  return `${v.major}.${v.minor}.${v.patchLabel}${v.build ? "-" + v.build : ""}`;
}

/** A per-channel cell: the playing phrase.row (hex), or "--" when the channel is parked. */
function channelCell(label: string, c: LsdjState["channels"]["pu1"]): string {
  return c.playing ? `${label} ${hex2(c.phrase)}.${hex2(c.phraseRow)}` : `${label} --`;
}

/** The three lines the strip shows, exactly as the old overlay drew them. */
function linesOf(state: LsdjState): string[] {
  const { channels: ch } = state;
  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  return [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? "PLAY" : "STOP"} S${hex2(state.songRow)}` +
      `  ${state.tempo != null ? `${state.tempo}bpm` : "--bpm"}  v${versionStr(state.version)}`,
    `${channelCell("1", ch.pu1)}  ${channelCell("2", ch.pu2)}  ` +
      `${channelCell("W", ch.wav)}  ${channelCell("N", ch.noi)}`,
  ];
}

/** The LSDj debug overlay: a readout over every LSDj tile, shown while the package's flag is on (backtick by
 *  default, through the `lsdj.debug-overlay` action). The default dedupe (the lines and the flag) is exactly what the
 *  strip draws, so the old hook's narrower signature is not needed and the displayed output is identical. */
export const overlayView = {
  id: "lsdj-overlay",
  label: "LSDj Debug Overlay",
  placement: "tile",
  visible: lsdjDebug,
  when: (sys) => hasRole(sys.roles, "lsdj-sync"),
  body: {
    kind: "readout",
    open: (host: Host<"files" | "systems">, systemId: number) => {
      const sys = host.systems.view().find((s) => s.id === systemId);
      const romPath = sys?.romPath ?? "";
      if (!sys || !romPath || !hasRole(sys.roles, "lsdj-sync")) return null;
      const header = host.files.readPrefix(romPath, 0x150);
      if (!header) return null;
      const reader = LsdjReader.fromHeader(header);
      if (!reader.supported) return null;
      return {
        sample: () => {
          const wram = host.systems.readRam(systemId);
          if (!wram) return undefined;
          const state = reader.read(wram);
          if (!state.supported) return null;
          const frame: ReadoutFrame = { lines: linesOf(state), playing: state.playing };
          return frame;
        },
      };
    },
  },
} satisfies TileView;
````

Run: `mkdir -p packages/lsdj/ts/ui; sed -n '8456,8527p' docs/modular-graph-phase-f4c-plan.md > packages/lsdj/ts/ui/overlayView.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/lsdj/ts/ui/overlayView.ts`
Expected: `5cb4a4531c338840cb4a547bc2ef5703ceed8d8c`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/lsdj/ts/ui/debugOverlay.ts`:

````ts
// The LSDj debug overlay's visibility flag and the app action that flips it (docs/modular-graph-phase-f4c-design
// section 6.1). The overlay is a developer debugging aid, so it is OFF at start and deliberately NOT persisted: it is
// an ephemeral switch, so it carries no user-config or migration surface and resets each session.
//
// The flag is the package's own Signal; the shell watches it (a tile view's `visible`) and flips it through the action,
// which the binding profiles carry like any other. One press with the defaults flips this and risa's, so the backtick
// still shows both overlays.
import { createSignal } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";

export const lsdjDebug = createSignal(false);

export const lsdjDebugOverlay: UiAction = {
  id: "lsdj.debug-overlay",
  label: "LSDj Debug Overlay",
  keys: ["`"],
  run: () => lsdjDebug.set(!lsdjDebug.get()),
};
````

Run: `mkdir -p packages/lsdj/ts/ui; sed -n '8539,8556p' docs/modular-graph-phase-f4c-plan.md > packages/lsdj/ts/ui/debugOverlay.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/lsdj/ts/ui/debugOverlay.ts`
Expected: `b4d4ba45150bf5a9b5c501f6abcc2b368fe27b2c`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/lsdj/ts/test/ui-views.test.ts`:

````ts
// The LSDj views over a fake host (docs/modular-graph-phase-f4c-design section 8.7): the HD session's cadence and the
// overlay's lines, with no display and no emulator. The pixel case needs the real ROM's fonts, so it reads the fixture
// and SKIPs without it.
import { test, expect, skip } from "@retroplug/testing";
import type { Host, SystemInfo } from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import { encodeSong, SongSchema } from "../index";
import { LsdjReader } from "../runtime";
import type { LsdjRom } from "../rom";
import { HdSession, hdView } from "../ui/hdView";
import { lsdjDebug, lsdjDebugOverlay } from "../ui/debugOverlay";
import { overlayView } from "../ui/overlayView";

/** The sibling resources tree, injected by the runner (scripts/run-tests.mjs) as the native suite's define is, so
 *  pointing it at a missing directory reproduces CI. */
declare const __RESOURCES_DIR__: string;
declare const tjs: { readFile(path: string): Promise<Uint8Array> };

/** A host member these views never call: the fake supplies every member of the groups it declares, so the type is the
 *  real contract and a view reaching for one more fails here rather than in production. */
function unused(): never {
  throw new Error("a view called a host member it does not use");
}

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/lsdj.gb",
    savPath: "/roms/lsdj.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [{ kind: "lsdj-sync", config: {} }],
    ...over,
  } as SystemInfo;
}

/** A host with the groups these views read. `state` is mutable so a case can change what the next read sees. */
function hostWith(state: {
  header?: Uint8Array | null;
  rom?: Uint8Array | null;
  ram?: Uint8Array | null;
  sram?: Uint8Array | null;
  systems?: readonly SystemInfo[];
  onSram?: () => void;
}): Host<"files" | "png" | "systems"> {
  return {
    files: {
      readPrefix: () => state.header ?? null,
      readFile: () => state.rom ?? null,
      writeFile: unused,
      writeFileAtomic: unused,
      fileExists: unused,
    },
    png: { encode: unused, decode: unused },
    systems: {
      view: () => state.systems ?? [system()],
      readRam: () => state.ram ?? null,
      readSram: () => {
        state.onSram?.();
        return state.sram ?? null;
      },
      setRoleConfig: unused,
      reload: unused,
      loadSram: unused,
      savPathOf: unused,
      nodeIdOf: unused,
      drivenCart: unused,
      stageNodeMidi: unused,
      capability: unused,
    },
  };
}

function header(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

/** The synthetic WRAM the recorded lines came from (real 9.4.2 offsets). */
function lsdjWram(): Uint8Array {
  const w = new Uint8Array(0x8000);
  w[0x0e0] = 1;
  w[0x0e1] = 1;
  w[0x168] = 0x10;
  w[0x169] = 0x11;
  w[0x16a] = 0xff;
  w[0x16b] = 0xff;
  w[0x16c] = 3;
  w[0x16d] = 4;
  w[0x16e] = 0xff;
  w[0x16f] = 0xff;
  w[0x200] = 2;
  w[0x201] = 5;
  w[0x202] = 0xff;
  w[0x203] = 0xff;
  w[0x401] = 4; // current screen: song
  w[0x41d] = 6; // song cursor col
  w[0x41e] = 1; // song cursor row
  w[0x529] = 128; // tempo
  return w;
}

test("the HD session reads SRAM on its 15-frame countdown and decodes only when the signature moved", () => {
  const reader = LsdjReader.fromHeader(header("LSDj-v9.4.2"))!;
  const sav = new Uint8Array(0x8000);
  sav.set(encodeSong(SongSchema.parse({})));
  let reads = 0;
  const host = hostWith({ sram: sav, onSram: () => (reads += 1), ram: lsdjWram() });
  const session = new HdSession(host, 1, reader, { fonts: () => [], palettes: () => [] } as unknown as LsdjRom);

  const at: number[] = [];
  const identities: string[] = [];
  let first: unknown = null;
  for (let frame = 0; frame < 33; frame++) {
    const before = reads;
    session.frame();
    if (reads > before) at.push(frame);
    if (frame === 0) first = session.song;
    identities.push(session.song === null ? "null" : session.song === first ? "first" : "other");
    if (frame === 16) sav[0] ^= 0x01; // an edit between polls
  }

  // The first frame, then every 16th, and a re-decode only on the edit.
  expect(at).toEqual([0, 16, 32]);
  expect(identities.slice(0, 32).every((id) => id === "first")).toBeTruthy();
  expect(identities[32]).toBe("other");
  // Nothing is dirty while the canvas has no font yet, so every frame draws nothing.
  expect(session.frame()).toBe(null);
});

test("the HD view refuses what the old hook gated on: no LSDj role, no ROM path, an unreadable header", () => {
  expect(hdView.body.kind).toBe("pixels");
  if (hdView.body.kind !== "pixels") return;
  expect(hdView.body.unsupported).toBe("HD view needs a supported LSDj cartridge");
  expect(hdView.when?.(system())).toBeTruthy();
  expect(hdView.when?.(system({ roles: [] }))).toBeFalsy();

  const open = (state: Parameters<typeof hostWith>[0], sys = system()) =>
    hdView.body.kind === "pixels" ? hdView.body.open(hostWith({ ...state, systems: [sys] }), sys.id) : null;
  expect(open({ header: header("LSDj-v9.4.2"), rom: null })).toBe(null); // no ROM bytes
  expect(open({ header: null, rom: new Uint8Array(4) })).toBe(null); // no readable header
  expect(open({ header: header("MGB"), rom: new Uint8Array(4) })).toBe(null); // not an LSDj cart
  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ roles: [] }))).toBe(null);
  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ romPath: "" }))).toBe(null);
});

test("the LSDj overlay's lines are the old overlay's", () => {
  const state = { header: header("LSDj-v9.4.2"), ram: lsdjWram() as Uint8Array | null };
  const host = hostWith(state);
  const sampler = overlayView.body.kind === "readout" ? overlayView.body.open(host, 1) : null;
  expect(sampler != null).toBeTruthy();
  const frame = sampler!.sample();
  expect(frame).toEqual({
    lines: ["SONG @6,1", "PLAY S05  128bpm  v9.4.2", "1 10.03  2 11.04  W --  N --"],
    playing: true,
  });

  // An unreadable RAM frame keeps what shows (undefined).
  state.ram = null;
  expect(sampler!.sample()).toBe(undefined);

  // The overlay applies to an LSDj-sync system and opens on nothing else: a system without the role, a header that is
  // not an LSDj cart (the reader's own `supported` gate, which is how this view says "unsupported": a sampler it opened
  // is always one whose reader resolves a layout), and no header at all.
  const open = (sys = system()) =>
    overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("LSDj-v9.4.2"), systems: [sys] }), sys.id) : null;
  expect(overlayView.when?.(system())).toBeTruthy();
  expect(overlayView.when?.(system({ roles: [] }))).toBeFalsy();
  expect(open(system({ roles: [] }))).toBe(null);
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: null }), 1) : null).toBe(null);
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("MGB") }), 1) : null).toBe(null);
});

test("the debug flag is off at start, and the action flips it (and works on the start screen)", () => {
  expect(lsdjDebug.get()).toBe(false);
  expect(overlayView.visible).toBe(lsdjDebug);
  lsdjDebugOverlay.run({} as UiHost, { systemId: null });
  expect(lsdjDebug.get()).toBe(true);
  lsdjDebugOverlay.run({} as UiHost, { systemId: null });
  expect(lsdjDebug.get()).toBe(false);
  expect(lsdjDebugOverlay.id).toBe("lsdj.debug-overlay");
  expect(lsdjDebugOverlay.keys).toEqual(["`"]);
  expect(lsdjDebugOverlay.label).toBe("LSDj Debug Overlay");
});

test("the HD view paints the real cartridge and skips an unchanged frame (fixture)", async () => {
  const rom = await tjs.readFile(`${__RESOURCES_DIR__}/roms/lsdj/lsdj9_4_2.gb`).catch(() => null);
  if (!rom) skip("lsdj views: the LSDj fixture ROM is absent");
  const sav = await tjs.readFile("../lsdj/ts/test/savs/all.sav");
  const host = hostWith({ header: rom.subarray(0, 0x150), rom, sram: sav, ram: lsdjWram() });
  const session = hdView.body.kind === "pixels" ? hdView.body.open(host, 1) : null;
  expect(session != null).toBeTruthy();
  const first = session!.frame();
  expect(first != null).toBeTruthy();
  expect(first!.width).toBe(792);
  expect(first!.height).toBe(576);
  expect(first!.pixels.length).toBe(first!.width * first!.height);
  // The same live state again: the canvas flushed no tile, so there is nothing to push.
  expect(session!.frame()).toBe(null);
});
````

Run: `mkdir -p packages/lsdj/ts/test; sed -n '8568,8772p' docs/modular-graph-phase-f4c-plan.md > packages/lsdj/ts/test/ui-views.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/lsdj/ts/test/ui-views.test.ts`
Expected: `deec6dd77fb9878e8a1a3971fe8e684862ec8a82`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 2: risa: the overlay and its action**

The same two files for `risa-overlay` and `risa.debug-overlay` (keys `["`"]`): `open` is the old hook's
(`host.files.readFile(rom)`, `runtime.identifyRisaVersion`, `resolveRisaLayout`, `null` with no layout), the sampler
is `decodeRisaState(ram, layout)` through `host.systems.readRam` (and `null` for a RAM image too short to decode), the
lines, `trackCell`, `TRACK_LABELS` and the 4X tempo rule are `RisaOverlay.tsx`'s verbatim, and `when` is the `risa`
role, the one scanner hit the old `ui/` had. `risa/ts/ui/index.ts` becomes `defineUi({ trackers: [RISA_TRACKER_UI],
views: [overlayView], actions: [risaDebugOverlay] })`. **risa's `tsconfig.json` is UNCHANGED**: it lists no examples
and declares no React host module, so the generator's extend rule does not ask it for one, and a plain readout view
needs no React path (the header's departure).

````diff
diff --git a/packages/risa/package.json b/packages/risa/package.json
index b1993b3e6..49a92c400 100644
--- a/packages/risa/package.json
+++ b/packages/risa/package.json
@@ -6,7 +6,7 @@
     "typecheck": "tsc -p tsconfig.json"
   },
   "type": "module",
-  "description": "risa's half of the app: the RSav catalog and song-payload codec, the .rkit-compatible ROM asset layer (kits, themes, fonts), the RAM-layout reader and drift detector, the risa, risa-assets and risa-sync roles, its Songs/Assets tracker integration, its DMC kit bank format, its ROM provider, and the rp.risa.sync protocol node (span label 19) that drives its host-sync receive path.",
+  "description": "risa's half of the app: the RSav catalog and song-payload codec, the .rkit-compatible ROM asset layer (kits, themes, fonts), the RAM-layout reader and drift detector, the risa, risa-assets and risa-sync roles, its Songs/Assets tracker integration, its DMC kit bank format, its ROM provider, its \"risa-overlay\" debug readout view with the \"risa.debug-overlay\" action that toggles it, and the rp.risa.sync protocol node (span label 19) that drives its host-sync receive path.",
   "exports": {
     ".": "./ts/index.ts",
     "./app": "./ts/app.ts",
diff --git a/packages/risa/ts/ui/index.ts b/packages/risa/ts/ui/index.ts
index fda3d0db9..11022cc1e 100644
--- a/packages/risa/ts/ui/index.ts
+++ b/packages/risa/ts/ui/index.ts
@@ -12,6 +12,8 @@ import {
   type TrackerUi,
 } from "@retroplug/ui/contribution";
 import { exportRitAsset, replaceRitAsset, type RitAssetOps } from "@retroplug/tracker/ritAssetOps";
+import { overlayView } from "./overlayView";
+import { risaDebugOverlay } from "./debugOverlay";
 import { risaAssetCatalog } from "../risaAssetCatalog";
 import { risaSongCatalog } from "../risaSongCatalog";
 import { RisaRom } from "../rom";
@@ -135,4 +137,8 @@ export const RISA_TRACKER_UI: TrackerUi = {
   asset: risaAssetSpec,
 };
 
-export default defineUi({ trackers: [RISA_TRACKER_UI] });
+export default defineUi({
+  trackers: [RISA_TRACKER_UI],
+  views: [overlayView],
+  actions: [risaDebugOverlay],
+});
````

Run: `sed -n '8793,8828p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/risa/package.json
packages/risa/ts/ui/index.ts
EOF
````

Expected:

````text
49a92c4009f71932747f1139ae308249f915e2a5
11022cc1e8a731b02fce926b570c9f8e7a2aeb56
````

Create `packages/risa/ts/ui/overlayView.ts`:

````ts
// The risa debug overlay: the risa twin of LSDj's readout (docs/modular-graph-phase-f4c-design section 6.2). It shows
// the visible screen and cursor, play/mode, tempo, the ROM version, and each of the five tracks' playing phrase.row,
// over every risa tile whose package flag is on.
//
// The sampler is the old hook's body as plain code: the internal-RAM layout is resolved once from the ROM (the "RISA V"
// marker is deep in the PRG, so the whole file is read), then every frame pulls the RAM snapshot (host.systems.readRam)
// and decodes it. Null means the cart is not supported; undefined means this frame's RAM was unreadable.
import type { Host } from "@retroplug/core/host";
import { hasRole } from "@retroplug/core/systemRoles";
import { hex2 } from "@retroplug/ui/format";
import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
import * as runtime from "../runtime";
import type { RisaState, RisaTrackState } from "../runtime";
import { risaDebug } from "./debugOverlay";

const TRACK_LABELS = ["1", "2", "T", "N", "D"]; // pulse1, pulse2, triangle, noise, dmc

/** A per-track cell: the playing phrase.row (hex), or "--" when the track is parked. */
function trackCell(label: string, t: RisaTrackState): string {
  return t.active ? `${label} ${hex2(t.phraseId)}.${hex2(t.phraseRow)}` : `${label} --`;
}

/** The three lines the strip shows, exactly as the old overlay drew them. */
function linesOf(state: RisaState): string[] {
  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  const tempo = state.fourX ? "4X" : state.bpm != null ? `${state.bpm}bpm` : "--bpm";
  return [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? state.mode.toUpperCase() : "STOP"}  ${tempo}  v${state.version ?? "?"}`,
    state.tracks.map((t, i) => trackCell(TRACK_LABELS[i], t)).join("  "),
  ];
}

/** The risa debug overlay: a readout over every risa tile, shown while the package's flag is on (backtick by
 *  default, through the `risa.debug-overlay` action). The default dedupe (the lines and the flag) is exactly what the
 *  strip draws, so no narrower signature is needed and the displayed output is identical. */
export const overlayView = {
  id: "risa-overlay",
  label: "risa Debug Overlay",
  placement: "tile",
  visible: risaDebug,
  when: (sys) => hasRole(sys.roles, "risa"),
  body: {
    kind: "readout",
    open: (host: Host<"files" | "systems">, systemId: number) => {
      const sys = host.systems.view().find((s) => s.id === systemId);
      const romPath = sys?.romPath ?? "";
      if (!sys || !romPath || !hasRole(sys.roles, "risa")) return null;
      const rom = host.files.readFile(romPath);
      if (!rom) return null;
      const layout = runtime.resolveRisaLayout(runtime.identifyRisaVersion(rom));
      if (!layout) return null;
      return {
        sample: () => {
          const ram = host.systems.readRam(systemId);
          if (!ram) return undefined;
          const state = runtime.decodeRisaState(ram, layout);
          if (!state.supported) return null;
          const frame: ReadoutFrame = { lines: linesOf(state), playing: state.playing };
          return frame;
        },
      };
    },
  },
} satisfies TileView;
````

Run: `mkdir -p packages/risa/ts/ui; sed -n '8853,8917p' docs/modular-graph-phase-f4c-plan.md > packages/risa/ts/ui/overlayView.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/risa/ts/ui/overlayView.ts`
Expected: `71099df1c1c4336d21f1cd3c8fe1f3a4a878873e`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/risa/ts/ui/debugOverlay.ts`:

````ts
// risa's debug overlay flag and the app action that flips it: the risa twin of LSDj's (docs/modular-graph-phase-f4c
// design section 6.2). Off at start and not persisted, as the LSDj one is; the default backtick binding flips both.
import { createSignal } from "@retroplug/ui/menu";
import type { UiAction } from "@retroplug/ui/contribution";

export const risaDebug = createSignal(false);

export const risaDebugOverlay: UiAction = {
  id: "risa.debug-overlay",
  label: "risa Debug Overlay",
  keys: ["`"],
  run: () => risaDebug.set(!risaDebug.get()),
};
````

Run: `mkdir -p packages/risa/ts/ui; sed -n '8929,8941p' docs/modular-graph-phase-f4c-plan.md > packages/risa/ts/ui/debugOverlay.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/risa/ts/ui/debugOverlay.ts`
Expected: `231acb2929e39d56fd0f0329edc1960708a31b69`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/risa/ts/test/ui-views.test.ts`:

````ts
// risa's debug overlay over a fake host (docs/modular-graph-phase-f4c-design section 8.7): the lines the old overlay
// drew, asserted over a synthetic RAM image; no display, no emulator.
import { test, expect } from "@retroplug/testing";
import type { Host, SystemInfo } from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import { resolveRisaLayout } from "../runtime";
import { overlayView } from "../ui/overlayView";
import { risaDebug, risaDebugOverlay } from "../ui/debugOverlay";

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "nes",
    core: "mesen",
    romPath: "/roms/risa.nes",
    savPath: "/roms/risa.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [{ kind: "risa", config: {} }],
    ...over,
  } as SystemInfo;
}

/** A host member these views never call: the fake supplies every member of the groups it declares, so the type is the
 *  real contract and a view reaching for one more fails here rather than in production. */
function unused(): never {
  throw new Error("a view called a host member it does not use");
}

function hostWith(state: {
  rom?: Uint8Array | null;
  ram?: Uint8Array | null;
  systems?: readonly SystemInfo[];
}): Host<"files" | "systems"> {
  return {
    files: {
      readFile: () => state.rom ?? null,
      readPrefix: unused,
      writeFile: unused,
      writeFileAtomic: unused,
      fileExists: unused,
    },
    systems: {
      view: () => state.systems ?? [system()],
      readRam: () => state.ram ?? null,
      readSram: unused,
      setRoleConfig: unused,
      reload: unused,
      loadSram: unused,
      savPathOf: unused,
      nodeIdOf: unused,
      drivenCart: unused,
      stageNodeMidi: unused,
      capability: unused,
    },
  };
}

/** A ROM carrying risa's version marker where identifyRisaVersion scans for it. */
function risaRom(version = "2.2.1"): Uint8Array {
  const rom = new Uint8Array(0x400);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // iNES header
  const marker = `RISA V${version}`;
  for (let i = 0; i < marker.length; i++) rom[0x123 + i] = marker.charCodeAt(i);
  return rom;
}

/** The synthetic RAM the recorded lines came from (the old runtime test's builder). */
function risaRam(): Uint8Array {
  const layout = resolveRisaLayout("2.2.1")!;
  const ram = new Uint8Array(0x800);
  ram[layout.seqMode] = 1;
  ram[layout.seqActive] = 0b00101;
  ram[layout.bpm] = 128;
  ram[layout.currentScreen] = 2;
  ram[layout.cursorRow] = 3;
  ram[layout.cursorCol] = 4;
  ram[layout.songLastRow + 0] = 0xff;
  ram[layout.songRow + 0] = 7;
  ram[layout.phraseId + 0] = 10;
  ram[layout.phraseLastRow + 0] = 5;
  for (const base of [
    layout.songLastRow,
    layout.songRow,
    layout.chainId,
    layout.chainRow,
    layout.phraseId,
    layout.phraseLastRow,
    layout.note,
    layout.lastInst,
  ])
    ram[base + 1] = 0xff;
  ram[layout.phraseId + 2] = 0x20;
  ram[layout.phraseLastRow + 2] = 0x0a;
  return ram;
}

test("the risa overlay's lines are the old overlay's", () => {
  const state = { rom: risaRom() as Uint8Array | null, ram: risaRam() as Uint8Array | null };
  const host = hostWith(state);
  const sampler = overlayView.body.kind === "readout" ? overlayView.body.open(host, 1) : null;
  expect(sampler != null).toBeTruthy();
  expect(sampler!.sample()).toEqual({
    lines: ["SONG @4,3", "SONG  128bpm  v2.2.1", "1 0A.05  2 --  T 20.0A  N --  D --"],
    playing: true,
  });

  // Unreadable RAM keeps the strip (undefined).
  state.ram = null;
  expect(sampler!.sample()).toBe(undefined);

  // A RAM image too short for the layout is an unsupported state, so the strip shows nothing (null): the reader
  // degrades rather than reading past the snapshot's end.
  state.ram = new Uint8Array(4);
  expect(sampler!.sample()).toBe(null);

  // A ROM no version resolves opens nothing.
  const noVersion =
    overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ rom: new Uint8Array(4) }), 1) : null;
  expect(noVersion).toBe(null);
});

test("the risa overlay applies to risa systems only, and opens none without the role", () => {
  expect(overlayView.when?.(system())).toBeTruthy();
  expect(overlayView.when?.(system({ roles: [{ kind: "lsdj-sync", config: {} }] }))).toBeFalsy();
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({}), 1) : null).toBe(null);
  const noRole = system({ roles: [] });
  const sampler =
    overlayView.body.kind === "readout"
      ? overlayView.body.open(hostWith({ rom: risaRom(), systems: [noRole] }), noRole.id)
      : null;
  expect(sampler).toBe(null);
});

test("risa's debug flag is off at start, and its action flips it", () => {
  expect(risaDebug.get()).toBe(false);
  expect(overlayView.visible).toBe(risaDebug);
  risaDebugOverlay.run({} as UiHost, { systemId: null });
  expect(risaDebug.get()).toBe(true);
  expect(risaDebugOverlay.id).toBe("risa.debug-overlay");
  expect(risaDebugOverlay.keys).toEqual(["`"]);
  expect(risaDebugOverlay.label).toBe("risa Debug Overlay");
  risaDebugOverlay.run({} as UiHost, { systemId: null });
  expect(risaDebug.get()).toBe(false);
});
````

Run: `mkdir -p packages/risa/ts/test; sed -n '8953,9099p' docs/modular-graph-phase-f4c-plan.md > packages/risa/ts/test/ui-views.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/risa/ts/test/ui-views.test.ts`
Expected: `e866b7de20496203b88d59e590062ddfd2dec6f8`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 3: `hex2` once, in `@retroplug/ui/format`**

A plain-TypeScript package cannot import the React kit for a string format, so `hex2` moves to `@retroplug/ui`'s new
`./format` export; both overlays import it there, the kit re-exports it in place of its own copy, and the kit's
`DebugReadout` imports it from the package. `packages/ui/package.json`'s description says what the package is: the
menu model, the contribution types including the view and action shapes, the row helpers and the format helpers, and
that it imports no React.

Create `packages/ui/ts/format.ts`:

````ts
// The UI's small text formatters, plain TypeScript: what a readout draws a byte as. The two debug overlays are plain
// TypeScript in their packages and the kit's DebugReadout is React, so the one copy lives here, where both sides can
// reach it without a cart importing the React kit for a string format.
/** A byte as two uppercase hex digits, "--" when absent: the readout cells' format. */
export const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));
````

Run: `mkdir -p packages/ui/ts; sed -n '9119,9123p' docs/modular-graph-phase-f4c-plan.md > packages/ui/ts/format.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/ui/ts/format.ts`
Expected: `043d30467da1d4941dd7d84fcf7eb35272a43c2b`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/ui-react/ts/ui/DebugReadout.tsx b/packages/ui-react/ts/ui/DebugReadout.tsx
index 078da39cb..77c2ea9ba 100644
--- a/packages/ui-react/ts/ui/DebugReadout.tsx
+++ b/packages/ui-react/ts/ui/DebugReadout.tsx
@@ -7,9 +7,6 @@ import { tagTestId } from "./tagTestId";
 
 const HEIGHT = 44;
 
-/** A byte as two uppercase hex digits, "--" when unknown. */
-export const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));
-
 export function DebugReadout({
   lines,
   playing,
diff --git a/packages/ui-react/ts/ui/index.ts b/packages/ui-react/ts/ui/index.ts
index 158999a9d..1b1062e9e 100644
--- a/packages/ui-react/ts/ui/index.ts
+++ b/packages/ui-react/ts/ui/index.ts
@@ -9,7 +9,8 @@
 // View with the theme chrome zeroed, `PixelCanvas` the crisp scaled canvas, and the hooks the frame bus and a forced
 // re-render. `DebugReadout` and `tagTestId` are what the debug overlays and the UI tests draw with.
 export { Box, CHROME_RESET } from "./Box";
-export { DebugReadout, hex2 } from "./DebugReadout";
+export { DebugReadout } from "./DebugReadout";
+export { hex2 } from "@retroplug/ui/format";
 export {
   CanvasEx,
   LV_IMAGE_ALIGN_CONTAIN,
diff --git a/packages/ui/package.json b/packages/ui/package.json
index 57c5796f7..a33a2de37 100644
--- a/packages/ui/package.json
+++ b/packages/ui/package.json
@@ -9,6 +9,7 @@
   "description": "The UI's contract, plain TypeScript: the menu data model and its item builders, the contribution types a package's UI module declares (declarative role-config rows, menu builders and their placements, tracker specs and UI services), and the row-reading helpers the shell renders a row with. The React kit follows in F4c.",
   "exports": {
     "./contribution": "./ts/contribution.ts",
+    "./format": "./ts/format.ts",
     "./menu": "./ts/menu/index.ts"
   },
   "dependencies": {
````

Run: `sed -n '9133,9172p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/ui-react/ts/ui/DebugReadout.tsx
packages/ui-react/ts/ui/index.ts
packages/ui/package.json
EOF
````

Expected:

````text
77c2ea9baf31b05ccdecc6ac62de53f214d57c70
1b1062e9e5bff424cb598a03ab48d4520448cf92
a33a2de372d5409035d68ff1946ba56dd9063c7d
````

- [ ] **Step 4: What leaves the shell**

The two shell screens and the four hooks go with `git rm` (their successors are new files, not renames: their
rendering became the shell's generic renderer, and only the HD session was a `git mv`), and `EmulatorTile` no longer
mounts the two hard overlays: the slots Task 2 placed are what draws the packages' views now. `uiHost.ts` loses
`uiViews` (its `views` peer is the registry's `open`), `menuDefs.ts` loses `MenuContext.openLsdjHd` and `hostOf` gains
the registry-checking fallback host, `menuContributions.ts` gains `windowViewOrReport` (the one wording of an open's
refusals, shared by App and the fallback host), and `App.tsx` loses `KEY_BACKTICK`, the backtick line, the
`LsdjHdScreen` import, the interim `"lsdj-hd"` entry and its full-window fallback: `openViewOf` is the generated
registry's lookup alone, and the full-window branch renders `ViewWindow` for the registry's view or falls through to
the grid.

Delete `packages/retroplug/ui/screens/grid/LsdjOverlay.tsx`, `packages/retroplug/ui/screens/grid/RisaOverlay.tsx`, `packages/retroplug/ui/screens/grid/lsdjDebug.ts`, `packages/retroplug/ui/screens/grid/useLsdjRuntime.ts`, `packages/retroplug/ui/screens/grid/useRisaRuntime.ts`, `packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx` with `git rm`, which stages the deletion:

````bash
git rm -q -- packages/retroplug/ui/screens/grid/LsdjOverlay.tsx packages/retroplug/ui/screens/grid/RisaOverlay.tsx \
  packages/retroplug/ui/screens/grid/lsdjDebug.ts packages/retroplug/ui/screens/grid/useLsdjRuntime.ts \
  packages/retroplug/ui/screens/grid/useRisaRuntime.ts packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
````

````diff
diff --git a/packages/retroplug/ui/App.tsx b/packages/retroplug/ui/App.tsx
index 2a011287c..77cc78671 100644
--- a/packages/retroplug/ui/App.tsx
+++ b/packages/retroplug/ui/App.tsx
@@ -28,8 +28,6 @@ import { native } from "./host/nativeHooks";
 import { useGameInput } from "./input/useGameInput";
 import { useGamepadInput } from "./input/useGamepadInput";
 import { SystemGrid } from "./screens/grid/SystemGrid";
-import { LsdjHdScreen } from "./screens/hd/LsdjHdScreen";
-import { toggleLsdjDebug } from "./screens/grid/lsdjDebug";
 import { Menu } from "./screens/menu/Menu";
 import { gridContentSize, hitTestTile, resolveZoom, SystemLayout } from "./screens/grid/layout";
 import { buildInstanceMenu, buildStartMenu, composeWindowTitle, trackerCartLabel, type MenuContext } from "./screens/menu/menuDefs";
@@ -41,14 +39,13 @@ import { isMenuModalActive } from "./screens/menu/menuModal";
 import { buildKeyToAction, buildGamepadToAction, KEY_ESCAPE } from "../src/keyCodes";
 import { resolveDropAction } from "../src/fileDrop";
 import { createUiHost } from "./host/uiHost";
-import { contributedActionIds, contributedActions, viewById, windowViewOf } from "./host/menuContributions";
+import { contributedActionIds, contributedActions, windowViewOrReport, windowViewOf } from "./host/menuContributions";
 import { runActions } from "./input/appActions";
 import { withActionDefaults } from "../src/bindingMap";
 import { ViewWindow } from "./views/ViewWindow";
 import { viewAppliesTo } from "./views/viewSession";
 import { resolveTracker } from "../src/trackerRegistry";
 
-const KEY_BACKTICK = 0x60; // ` — toggles the LSDj runtime debug overlay (a developer aid, off by default)
 const MOUSE_BUTTON_RIGHT = 2; // DPF kMouseButtonRight (left=1, right=2, middle=3) — the "mouse" bus button field
 
 export function App() {
@@ -74,14 +71,12 @@ export function App() {
   // The window view that is open, if any: its id and the system it is showing. It replaces the grid but is NOT an
   // input overlay - the cart keeps playing and keys still reach its joypad, so it doesn't gate `playing`.
   //
-  // `host.views.open` looks the id up in the generated registry. The LSDj HD player is the shell's own screen, not a
-  // registry view: "lsdj-hd" is a shell-side entry in `openViewOf` and in the full-window render below, and both go
-  // when the cart registers the same id as a window view.
+  // `host.views.open` looks the id up in the generated registry, so every window view is a package's.
   const [openView, setOpenView] = useState<{ id: string; systemId: number } | null>(null);
   // The UI's host (docs/modular-graph-phase-f4b-design.md section 3.2), built once over the stores: the groups a cart's
-  // or a console's UI code reaches the shell through. Its `views` group opens the generated registry's window views
-  // (through `openViewOf`, which keeps the HD player's shell-side entry), and its import picker is useSongImport's
-  // overlay, which is resolved against the live system an import names rather than the snapshot the caller held.
+  // or a console's UI code reaches the shell through. Its `views` group opens the generated registry's window views,
+  // and its import picker is useSongImport's overlay, which is resolved against the live system an import names rather
+  // than the snapshot the caller held.
   const beginImport = useCallback(
     (sys: SystemInfo, source: Uint8Array): void => {
       const live = stores.project.systems.view().find((s) => s.id === sys.id);
@@ -91,18 +86,9 @@ export function App() {
   );
   const openViewOf = useCallback(
     (id: string, systemId: number): void => {
-      if (id === "lsdj-hd") {
-        // The shell's own entry (above): the HD player is not a registry view yet.
-        setOpenView({ id, systemId });
-        return;
-      }
-      const entry = viewById(id);
-      if (!entry) {
-        console.warn(`[views] no view is registered under the id ${id}`);
-        return;
-      }
-      if (entry.view.placement !== "window") {
-        console.warn(`[views] ${id} is a tile view, which opens no window`);
+      const open = windowViewOrReport(id);
+      if ("report" in open) {
+        console.warn(open.report);
         return;
       }
       const sys = stores.project.systems.view().find((s) => s.id === systemId);
@@ -110,7 +96,7 @@ export function App() {
         console.warn(`[views] ${id} was asked for the system ${systemId}, which is not in the project`);
         return;
       }
-      if (!viewAppliesTo(entry.view, sys)) {
+      if (!viewAppliesTo(open.view, sys)) {
         console.warn(`[views] ${id} does not apply to the system ${systemId}`);
         return;
       }
@@ -266,7 +252,6 @@ export function App() {
       if (openView != null) return void setOpenView(null); // a window view → back to the grid
     }
     if (overlay) return; // an overlay owns input; actions don't fire under it
-    if (key === KEY_BACKTICK) return void toggleLsdjDebug(); // dev: show/hide the LSDj runtime readout
     runResolved(keyToAction.get(key) ?? []);
   });
   // Gamepad app actions. Overlays (close prompt / project modal) render a <Menu>, so they're cancelled by the
@@ -367,7 +352,21 @@ export function App() {
     if (!veto) native.__rp_quitWindow?.();
   };
 
-  const ctx: MenuContext = { stores, host: uiHost, settings, userConfig, bindings, systems, recent, version, newProject: modals.newProject, loadProject: modals.loadProject, loadRomAsProject: modals.loadRomAsProject, requestExit, beginSongImport: beginImport, openLsdjHd: (systemId) => openViewOf("lsdj-hd", systemId) };
+  const ctx: MenuContext = {
+    stores,
+    host: uiHost,
+    settings,
+    userConfig,
+    bindings,
+    systems,
+    recent,
+    version,
+    newProject: modals.newProject,
+    loadProject: modals.loadProject,
+    loadRomAsProject: modals.loadRomAsProject,
+    requestExit,
+    beginSongImport: beginImport,
+  };
 
   // A full-window overlay (file browser / close prompt / project modal / song import) draws above
   // everything and owns the keypad; the start menu (empty project) is the same full-window menu, always open.
@@ -392,8 +391,6 @@ export function App() {
         />
       );
     }
-    // The shell's own entry: the LSDj HD player, until its cart registers the id as a window view.
-    return <LsdjHdScreen systemId={openView.systemId} width={width} height={height} testId="lsdj-hd" />;
   }
 
   // The anchor FOLLOWS its system through an in-place rebuild (a keep-open row like FM Audio resets the
diff --git a/packages/retroplug/ui/host/menuContributions.ts b/packages/retroplug/ui/host/menuContributions.ts
index 25a78b14f..64a0d5763 100644
--- a/packages/retroplug/ui/host/menuContributions.ts
+++ b/packages/retroplug/ui/host/menuContributions.ts
@@ -30,6 +30,17 @@ export function windowViewOf(id: string): WindowView | undefined {
   return entry?.view.placement === "window" ? entry.view : undefined;
 }
 
+/** Resolve a window open against the generated registry: the view, or the report saying why it cannot open. App's
+ *  `views.open` runs this and then the two checks only a project can answer (a system that is gone, a view whose `when`
+ *  rejects it); the menu's fallback host (`hostOf`, for a context built over the stores alone) runs this much and says
+ *  the same words. One function, so the refusals cannot drift apart. */
+export function windowViewOrReport(id: string): { view: WindowView } | { report: string } {
+  const entry = viewById(id);
+  if (!entry) return { report: `[views] no view is registered under the id ${id}` };
+  if (entry.view.placement !== "window") return { report: `[views] ${id} is a tile view, which opens no window` };
+  return { view: entry.view };
+}
+
 /** Every tile view, in product order, with the package it came from: the layers EmulatorTile mounts over each tile
  *  whose system the view's `when` accepts. */
 export function tileViews(): readonly { pkg: string; view: TileView }[] {
diff --git a/packages/retroplug/ui/host/uiHost.ts b/packages/retroplug/ui/host/uiHost.ts
index 38ccca9d8..7ae33f381 100644
--- a/packages/retroplug/ui/host/uiHost.ts
+++ b/packages/retroplug/ui/host/uiHost.ts
@@ -24,19 +24,7 @@ import { batteryListing } from "../../src/batteryListing";
  *  can still name it. This module re-exports the package's type rather than restating the group list. */
 export type { UiHost } from "@retroplug/ui/contribution";
 
-/** The shell's own full-window screens, by the id a cart names them with. The ids are the leftover shell knowledge of a
- *  cart: "lsdj-hd" is LSDj's HD player, the one cart id the shell still maps itself; the carts register their views
- *  (the HD player and the two overlays) once those move. An id this shell does not own is ignored, so a package's newer
- *  view cannot open the wrong screen here. */
-export function uiViews(openLsdjHd: (systemId: number) => void): ViewsHost {
-  return {
-    open: (id, systemId) => {
-      if (id === "lsdj-hd") openLsdjHd(systemId);
-    },
-  };
-}
-
-/** What the host needs that is not a store: the views the shell owns and the song-import picker, both of them App's
+/** What the host needs that is not a store: the window the views open in and the song-import picker, both of them App's
  *  React state (the picker is useSongImport's overlay). */
 export interface UiHostPeers {
   readonly views: ViewsHost;
diff --git a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
index e2d9f1368..965a21a3f 100644
--- a/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
+++ b/packages/retroplug/ui/screens/grid/EmulatorTile.tsx
@@ -19,8 +19,6 @@ import {
   type CanvasHandle,
 } from "@retroplug/ui-react";
 import type { UiHost } from "@retroplug/ui/contribution";
-import { LsdjOverlay } from "./LsdjOverlay";
-import { RisaOverlay } from "./RisaOverlay";
 import { TileViews } from "../../views/TileViews";
 import { pickActiveRenderJob, renderBadgeLabel, type RenderJobStatus } from "../../host/render";
 
@@ -88,16 +86,9 @@ export function EmulatorTile({
           style={{ width, height, "background-color": "#000000", "background-opacity": 0.5 }}
         />
       )}
-      {/* Live LSDj runtime readout (screen + cursor / play / song row / tempo / version / per-channel
-          phrase). Renders nothing for a non-LSDj ROM, so it's mounted unconditionally; its per-frame
-          re-renders stay in this child subtree. */}
-      <LsdjOverlay systemId={systemId} width={width} testId="lsdj-overlay" />
-      {/* The risa twin — a live NES-tracker readout (screen + cursor / mode / tempo / version / per-track
-          phrase). Renders nothing for a non-risa ROM, so it too is mounted unconditionally. */}
-      <RisaOverlay systemId={systemId} width={width} testId="risa-overlay" />
       {/* The product's tile views, one StableSlot each (docs/modular-graph-phase-f4c-design.md section 5.2): a
-          fixed place in the child list, so a view that appears late does not land over the border or badge. The
-          product registers none yet; a cart's overlays land here once they move. */}
+          fixed place in the child list, so a view that appears late does not land over the border or badge. The two
+          debug overlays are LSDj's and risa's, registered through their packages. */}
       <TileViews host={host} systemId={systemId} width={width} height={height} />
       {showBorder && (
         // Accent border on the focused tile — the visible focus cue (the dim is imperceptible on a
diff --git a/packages/retroplug/ui/screens/menu/menuDefs.ts b/packages/retroplug/ui/screens/menu/menuDefs.ts
index addc4c937..863f68a96 100644
--- a/packages/retroplug/ui/screens/menu/menuDefs.ts
+++ b/packages/retroplug/ui/screens/menu/menuDefs.ts
@@ -16,6 +16,7 @@ import {
   firstWinsMenuIds,
   menuPlacement,
   roleRows,
+  windowViewOrReport,
 } from "../../host/menuContributions";
 import { LAYOUT_VALUES, AUDIO_ROUTING_VALUES, type AudioRouting } from "../../../src/settingsEnums";
 import type { UserConfig } from "../../../src/userConfig";
@@ -29,7 +30,7 @@ import { resolveSavPath, siblingPath, SAV_PATTERNS } from "../../../src/savPaths
 import { stem, dirname, basename, extension, joinPath, sanitizeName, shortenMiddle } from "../../../src/pathUtil";
 import { ROM_PATTERNS } from "../../../src/fileSelection";
 import { filesPngHost } from "../../../src/hostGroups";
-import { createUiHost, uiViews, type UiHost } from "../../host/uiHost";
+import { createUiHost, type UiHost } from "../../host/uiHost";
 import { embeddedRomOf, embeddedRoms } from "../../../src/embeds";
 import {
   savEditWouldDiscard,
@@ -86,17 +87,17 @@ export interface MenuContext {
   loadRomAsProject: (romPath: string, explicitSav?: string) => void;
   /** Quit the standalone (unsaved-changes guarded). No-op in a DAW (the host owns the window). */
   requestExit: () => void;
-  // Inputs to the FALLBACK host only (below), for a composer that has no `host` of its own: the menu tests, which
-  // build this context over the stores alone. App passes a host, so it reads neither of these.
+  // The one input the FALLBACK host (below) needs: a composer that has no `host` of its own is a menu test's context,
+  // built over the stores alone, and `hostOf` builds a host over this picker. App passes a host, so its own host is
+  // what a contribution uses there.
   //
   // Open the Songs "import from a .sav" picker (validate the source against the cart's console, then show a
   // checkbox list) - owned by useSongImport, wired from App like the project modals.
   beginSongImport: (sys: SystemInfo, source: Uint8Array) => void;
-  /** Show the LSDj HD player for a system (a full-window view; Esc closes it). Owned by App; F4c removes the id. */
-  openLsdjHd: (systemId: number) => void;
   /** The UI host a contributed builder or cart callback reaches the shell through. App passes the one it built over
-   *  the stores; a composer that only has the stores (a menu test) leaves it out and `hostOf` builds the same host
-   *  from this context's own peers, so a contribution sees the groups either way. */
+   *  the stores; a composer that has only the stores (a menu test) leaves it out and `hostOf` builds one over this
+   *  context's `beginSongImport` and the generated view registry, so a contribution always has a host, though only
+   *  App's has a window for a window view to open in. */
   host?: UiHost;
   /** The contributed app actions the bindings editor lists after the shell's three. App leaves it out and the
    *  generated registry answers; a test passes its own action, which is the only way a row for an action no product
@@ -104,15 +105,24 @@ export interface MenuContext {
   actions?: readonly ContributedAction[];
 }
 
-/** The UI host for a context: the one App built when it passed one, else one built from the context's own peers (the
- *  views mapping and the song-import picker), cached for the context's life so a callback sees a stable host. */
+/** The UI host for a context: the one App built when it passed one, else one built over the stores and this
+ *  context's `beginSongImport`, cached for the context's life so a callback sees a stable host. */
 const hosts = new WeakMap<MenuContext, UiHost>();
 function hostOf(ctx: MenuContext): UiHost {
   const given = ctx.host;
   if (given) return given;
   let built = hosts.get(ctx);
   if (!built) {
-    built = createUiHost(ctx.stores, { views: uiViews(ctx.openLsdjHd), beginImport: ctx.beginSongImport });
+    // A context built over the stores alone (the menu tests) has no window to open a view in. Its refusals are App's
+    // (windowViewOrReport is the one place they are worded); a view that resolves simply has nowhere to go here.
+    const views = {
+      open: (id: string) => {
+        const open = windowViewOrReport(id);
+        if ("report" in open) console.warn(open.report);
+        else console.warn(`[views] the menu's host has no window to open ${id} in`);
+      },
+    };
+    built = createUiHost(ctx.stores, { views, beginImport: ctx.beginSongImport });
     hosts.set(ctx, built);
   }
   return built;
````

Run: `sed -n '9217,9487p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/ui/App.tsx
packages/retroplug/ui/host/menuContributions.ts
packages/retroplug/ui/host/uiHost.ts
packages/retroplug/ui/screens/grid/EmulatorTile.tsx
packages/retroplug/ui/screens/menu/menuDefs.ts
EOF
````

Expected:

````text
77cc78671141ddd074ecb5d860468aea42f23c1b
64a0d5763030d2962100feeba82483ac74455446
7ae33f3815944567c01de2bb3a84020d177bea08
965a21a3f3c5cf12c5b15ba341d3a0bbf317b3bd
863f68a9672593030b6ea9368938f7bd1f996933
````

- [ ] **Step 5: The `openLsdjHd` stubs, and the leaves counts**

Fifteen test and recorded-context files carried the `openLsdjHd: () => {}` stub the menu context required; the member
is gone, so the stubs go with it. `leaves.test.ts`'s two capture-row counts read 13 (8 Game Boy buttons + the shell's
3 + the 2 new action rows), its keyboard case's title says "+ 5 app-action rows", and its comments name the two
overlays: that is E1's row count in the menus themselves.

````diff
diff --git a/packages/retroplug/test/menu/action-rows.test.ts b/packages/retroplug/test/menu/action-rows.test.ts
index e4e2cfe9e..4f5a81808 100644
--- a/packages/retroplug/test/menu/action-rows.test.ts
+++ b/packages/retroplug/test/menu/action-rows.test.ts
@@ -35,7 +35,6 @@ function ctxOf(stores: AppStores, actions: readonly { pkg: string; action: UiAct
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
     actions,
   };
diff --git a/packages/retroplug/test/menu/asset-file-actions.test.ts b/packages/retroplug/test/menu/asset-file-actions.test.ts
index 59d3c856e..ccc64c347 100644
--- a/packages/retroplug/test/menu/asset-file-actions.test.ts
+++ b/packages/retroplug/test/menu/asset-file-actions.test.ts
@@ -37,7 +37,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 
diff --git a/packages/retroplug/test/menu/bliptoaster.test.ts b/packages/retroplug/test/menu/bliptoaster.test.ts
index a1622ac8d..a9eff3226 100644
--- a/packages/retroplug/test/menu/bliptoaster.test.ts
+++ b/packages/retroplug/test/menu/bliptoaster.test.ts
@@ -23,7 +23,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
diff --git a/packages/retroplug/test/menu/launchpad.test.ts b/packages/retroplug/test/menu/launchpad.test.ts
index 1eef1025d..22cde1225 100644
--- a/packages/retroplug/test/menu/launchpad.test.ts
+++ b/packages/retroplug/test/menu/launchpad.test.ts
@@ -45,7 +45,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     requestExit: () => {},
     beginSongImport: () => {},
-    openLsdjHd: () => {},
   };
 }
 
diff --git a/packages/retroplug/test/menu/leaves.test.ts b/packages/retroplug/test/menu/leaves.test.ts
index 4f83e8ec8..25697a0e0 100644
--- a/packages/retroplug/test/menu/leaves.test.ts
+++ b/packages/retroplug/test/menu/leaves.test.ts
@@ -36,7 +36,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: (romPath: string, explicitSav?: string) =>
       stores.project.openRom(romPath, explicitSav ? { explicitSav } : undefined),
     requestExit: () => {}, // App wires this to the native quit path; inert here.
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   };
 }
@@ -921,12 +920,13 @@ function keyboardBindings(stores: AppStores): MenuItem[] {
   return submenuChildren(settings, "set-keybindings");
 }
 
-test("Keyboard Bindings: 8 GB + 3 app-action rows; capture rebinds write-through, clear + reset", () => {
+test("Keyboard Bindings: 8 GB + 5 app-action rows; capture rebinds write-through, clear + reset", () => {
   const be = new MockBackend("/cfg");
   const stores = composeAppStores({ backend: be });
 
   const rows = keyboardBindings(stores);
-  expect(rows.filter((r) => r.kind === "capture").length).toBe(11); // 8 GB buttons + Open Menu / Cycle / Cycle (Back)
+  // 8 GB buttons + Open Menu / Cycle / Cycle (Back) + the two cart debug overlays.
+  expect(rows.filter((r) => r.kind === "capture").length).toBe(13);
   expect(findItem(rows, "bind-A")!.label).toBe("A: Z, z"); // resolved default baked into the label
   expect(findItem(rows, "bind-act-OpenMenu")!.label).toBe("Open Menu: Escape"); // app action seeded default
   expect(findItem(rows, "bind-act-CyclePrev")!.label).toBe("Cycle Instances (Back): -"); // unbound by default
@@ -987,7 +987,8 @@ test("Gamepad Bindings: gamepad-source capture rows; button + axis-token rebinds
   const stores = composeAppStores({ backend: be });
 
   const rows = gamepadBindings(stores);
-  expect(rows.filter((r) => r.kind === "capture").length).toBe(11); // 8 GB buttons + 3 app actions
+  // 8 GB buttons + the shell's three app actions + the two cart debug overlays.
+  expect(rows.filter((r) => r.kind === "capture").length).toBe(13);
   expect(findItem(rows, "bind-gp-A")!.capture!.source).toBe("gamepad"); // arms the pad bus, not the key bus
   expect(findItem(rows, "bind-gp-Up")!.label).toBe("Up: dpup, lefty-"); // default d-pad hat + left stick
   expect(findItem(rows, "bind-gp-act-OpenMenu")!.label).toBe("Open Menu: leftshoulder"); // seeded gamepad action
diff --git a/packages/retroplug/test/menu/lsdj-assets.test.ts b/packages/retroplug/test/menu/lsdj-assets.test.ts
index 707a9f085..19bb4b157 100644
--- a/packages/retroplug/test/menu/lsdj-assets.test.ts
+++ b/packages/retroplug/test/menu/lsdj-assets.test.ts
@@ -24,7 +24,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
diff --git a/packages/retroplug/test/menu/lsdj-songs.test.ts b/packages/retroplug/test/menu/lsdj-songs.test.ts
index ec0a94062..e80ac1e90 100644
--- a/packages/retroplug/test/menu/lsdj-songs.test.ts
+++ b/packages/retroplug/test/menu/lsdj-songs.test.ts
@@ -25,7 +25,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
diff --git a/packages/retroplug/test/menu/n8-sd-ops.test.ts b/packages/retroplug/test/menu/n8-sd-ops.test.ts
index 3947f6771..43467e04b 100644
--- a/packages/retroplug/test/menu/n8-sd-ops.test.ts
+++ b/packages/retroplug/test/menu/n8-sd-ops.test.ts
@@ -31,7 +31,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     requestExit: () => {},
     beginSongImport: () => {},
-    openLsdjHd: () => {},
   };
 }
 
diff --git a/packages/retroplug/test/menu/recent-rows.test.ts b/packages/retroplug/test/menu/recent-rows.test.ts
index 8859c4da4..fc99ec5b2 100644
--- a/packages/retroplug/test/menu/recent-rows.test.ts
+++ b/packages/retroplug/test/menu/recent-rows.test.ts
@@ -29,7 +29,6 @@ function ctxOf(stores: AppStores, loadProject: MenuContext["loadProject"] = () =
     loadProject,
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   };
 }
diff --git a/packages/retroplug/test/menu/recorded-cases.ts b/packages/retroplug/test/menu/recorded-cases.ts
index ddf3be132..0ece4d854 100644
--- a/packages/retroplug/test/menu/recorded-cases.ts
+++ b/packages/retroplug/test/menu/recorded-cases.ts
@@ -92,7 +92,6 @@ export function ctxOf(stores: AppStores): MenuContext {
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   };
 }
diff --git a/packages/retroplug/test/menu/risa.test.ts b/packages/retroplug/test/menu/risa.test.ts
index 63ac33bb1..2e49d42b6 100644
--- a/packages/retroplug/test/menu/risa.test.ts
+++ b/packages/retroplug/test/menu/risa.test.ts
@@ -39,7 +39,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
diff --git a/packages/retroplug/test/menu/rom-patch.test.ts b/packages/retroplug/test/menu/rom-patch.test.ts
index 9e0f551ce..1ab5c2a28 100644
--- a/packages/retroplug/test/menu/rom-patch.test.ts
+++ b/packages/retroplug/test/menu/rom-patch.test.ts
@@ -34,7 +34,6 @@ function ctxOf(stores: AppStores): MenuContext {
     loadRomAsProject: () => {},
     beginSongImport: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 }
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
diff --git a/packages/retroplug/test/menu/song-guard.test.ts b/packages/retroplug/test/menu/song-guard.test.ts
index 0f78fedc4..4221bba29 100644
--- a/packages/retroplug/test/menu/song-guard.test.ts
+++ b/packages/retroplug/test/menu/song-guard.test.ts
@@ -39,7 +39,6 @@ function cart(sram: Uint8Array) {
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   });
   const songRow = (index: number): MenuItem[] => {
diff --git a/packages/retroplug/test/menu/song-import.test.ts b/packages/retroplug/test/menu/song-import.test.ts
index 4a00f08bc..7cee54710 100644
--- a/packages/retroplug/test/menu/song-import.test.ts
+++ b/packages/retroplug/test/menu/song-import.test.ts
@@ -232,7 +232,6 @@ test("Songs Add... hands a picked .sav to beginSongImport (not the single-file i
     loadRomAsProject: () => {},
     beginSongImport: (s, source) => (imported = { id: s.id, bytes: source.length }),
     requestExit: () => {},
-    openLsdjHd: () => {},
   };
 
   const songs = submenuChildren(submenuChildren(buildInstanceMenu({ ...ctx, system: sys() }).items, "inst-lsdj"), "lsdj-songs");
diff --git a/packages/retroplug/test/tracker/working-song-save.test.ts b/packages/retroplug/test/tracker/working-song-save.test.ts
index 5a074e061..df09aeca5 100644
--- a/packages/retroplug/test/tracker/working-song-save.test.ts
+++ b/packages/retroplug/test/tracker/working-song-save.test.ts
@@ -357,7 +357,6 @@ function smsMenu() {
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   });
   const rows = (): MenuItem[] => {
@@ -649,7 +648,6 @@ test("the Songs menu still guards its rows while a reset is carrying the song ba
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   };
   const sys = stores.project.systems.view().find((x) => x.id === newId)!;
@@ -862,7 +860,6 @@ test("the Songs menu offers Save Working Song & Load for a work-RAM song, and do
     loadProject: () => {},
     loadRomAsProject: () => {},
     requestExit: () => {},
-    openLsdjHd: () => {},
     beginSongImport: () => {},
   };
   const items = buildInstanceMenu({ ...ctx, system: stores.project.systems.view()[0] }).items;
````

Run: `sed -n '9523,9744p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/test/menu/action-rows.test.ts
packages/retroplug/test/menu/asset-file-actions.test.ts
packages/retroplug/test/menu/bliptoaster.test.ts
packages/retroplug/test/menu/launchpad.test.ts
packages/retroplug/test/menu/leaves.test.ts
packages/retroplug/test/menu/lsdj-assets.test.ts
packages/retroplug/test/menu/lsdj-songs.test.ts
packages/retroplug/test/menu/n8-sd-ops.test.ts
packages/retroplug/test/menu/recent-rows.test.ts
packages/retroplug/test/menu/recorded-cases.ts
packages/retroplug/test/menu/risa.test.ts
packages/retroplug/test/menu/rom-patch.test.ts
packages/retroplug/test/menu/song-guard.test.ts
packages/retroplug/test/menu/song-import.test.ts
packages/retroplug/test/tracker/working-song-save.test.ts
EOF
````

Expected:

````text
4f5a81808a4857b4ec7e5d608d63fda1753d7fa0
ccc64c347eaec7fc04133e049421655f784a5a34
a9eff3226b9b7b52acc977070cbcd66d15b8e4fc
22cde12256d6586fa5cc0a31aa8e30274c22bf44
25697a0e06ebf3678c24a3ed5845561b218f4290
19bb4b15785b9e26d8923738f0b7fe8810269403
e80ac1e905c4d23df2f9285ac97cad1da163e4dd
43467e04be640f0f2eb97ee32d7b7cefc9d5d420
fc99ec5b2fc539b66e57e5d9cd11d0199343a556
0ece4d854801015a4d216b01c1d459c70facd07f
2e49d42b679cb205c053520b1eced2e15248e07d
1ab5c2a289f7752db4ab9f89eff9809c28dfff63
4221bba295773b9ef08248bead01b96d93ffe5fb
7cee54710c91397b8a072e137cca496306e77e22
df09aeca55fb91bf93ec01950d57f3f3940694e2
````

- [ ] **Step 6: E1: the recorded menu matrix**

The two bindings menus gain the four action rows per case, and nothing else moves. The record was NOT re-taken
wholesale: the stage 4 report says "a throwaway recorder (new code) printed the matrix, I spliced only the two
bindings submenus' `children` into the existing `recorded-matrix.json`, and a deep comparison proved the spliced file
is **semantically identical** to the full new record". The diff below is therefore the record: +700 lines, 0
deletions, 70 hunks, 35 cases x 2 menus x 2 items x 5 lines. The recorder's own recipe is
`test/menu/recorded-trees.test.ts`'s RE-RECORDING block, whose own rule is `The record is taken from the OLD tree, so
it means running THIS file's CASES over that` and whose last step writes the printed objects `JSON with an indent of 1
and one trailing newline`. It was not run a second time, because the splice plus the suite below is the proof that the
file is what this tree's code produces.

````diff
diff --git a/packages/retroplug/test/menu/recorded-matrix.json b/packages/retroplug/test/menu/recorded-matrix.json
index 17c828769..28fce7401 100644
--- a/packages/retroplug/test/menu/recorded-matrix.json
+++ b/packages/retroplug/test/menu/recorded-matrix.json
@@ -321,6 +321,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -435,6 +445,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -1923,6 +1943,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -2037,6 +2067,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -3422,6 +3462,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -3536,6 +3586,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -4899,6 +4959,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -5013,6 +5083,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -6208,6 +6288,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -6322,6 +6412,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -7709,6 +7809,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -7823,6 +7933,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -9020,6 +9140,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -9134,6 +9264,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -10412,6 +10552,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -10526,6 +10676,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -12049,6 +12209,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -12163,6 +12333,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -13970,6 +14150,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -14084,6 +14274,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -15842,6 +16042,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -15956,6 +16166,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -17497,6 +17717,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -17611,6 +17841,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -19118,6 +19358,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -19232,6 +19482,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -20924,6 +21184,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -21038,6 +21308,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -22477,6 +22757,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -22591,6 +22881,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -23965,6 +24265,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -24079,6 +24389,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -26263,6 +26583,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -26377,6 +26707,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -27876,6 +28216,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -27990,6 +28340,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -29542,6 +29902,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -29656,6 +30026,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -31154,6 +31534,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -31268,6 +31658,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -32762,6 +33162,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -32876,6 +33286,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -34557,6 +34977,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -34671,6 +35101,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -36544,6 +36984,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -36658,6 +37108,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -38190,6 +38650,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -38304,6 +38774,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -40080,6 +40560,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -40194,6 +40684,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -42458,6 +42958,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -42572,6 +43082,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -44340,6 +44860,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -44454,6 +44984,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -46456,6 +46996,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -46570,6 +47120,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -47081,6 +47641,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -47195,6 +47765,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -47777,6 +48357,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -47891,6 +48481,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -48688,6 +49288,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -48802,6 +49412,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -50334,6 +50954,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -50448,6 +51078,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -51450,6 +52090,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -51564,6 +52214,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -53259,6 +53919,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -53373,6 +54043,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
@@ -54902,6 +55582,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: `"
+       },
+       {
+        "id": "bind-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: `"
+       },
        {
         "id": "bind-sep-reset",
         "kind": "separator",
@@ -55016,6 +55706,16 @@
         "kind": "capture",
         "label": "Cycle Instances (Back): -"
        },
+       {
+        "id": "bind-gp-act-lsdj.debug-overlay",
+        "kind": "capture",
+        "label": "LSDj Debug Overlay: -"
+       },
+       {
+        "id": "bind-gp-act-risa.debug-overlay",
+        "kind": "capture",
+        "label": "risa Debug Overlay: -"
+       },
        {
         "id": "bind-gp-sep-reset",
         "kind": "separator",
````

Run: `sed -n '9805,10998p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/retroplug/test/menu/recorded-matrix.json`
Expected: `28fce7401b9d820fb4f7aa4bdbd84a32cd2d12b4`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Run: `git hash-object packages/retroplug/test/menu/recorded-matrix.json`
Expected: `28fce7401b9d820fb4f7aa4bdbd84a32cd2d12b4`: the blob this task's commit holds for the matrix, so the applied hunks ARE the record, byte for byte.

Run: `pnpm test menu/recorded-trees`
Expected: exit 0, and `# 105 passed, 0 skipped, 0 failed`: every case's tree, cyclers and dialogs equal `collect()`'s answer over THIS tree's menus, which is what proves the spliced file is the full new record. (105 = 35 cases x 3 cases per case.)

- [ ] **Step 7: The four fixture-gated `test:ui` cases**

Each is its own file and process, so one skip cannot hide another: `overlay-survives-menu` (backtick shows the strip,
Esc opens the menu and the tile body goes, Esc closes it and the strip returns without a second press),
`hd-view-closes` (open the HD player, load a project through the Discard of the unsaved prompt, and the grid returns
because the view's system id is gone), `overlay-actions` (two instances, risa over the second, one backtick shows
both, then rebinding only `LSDj Debug Overlay` to Q leaves backtick toggling risa alone and Q LSDj alone; the fixture
gate is decided BEFORE the press, on tile 1's instance menu naming `Risa`, so a broken risa toggle fails instead of
skipping) and `overlay-click-through` (two LSDj tiles with the overlay showing, a click on the unfocused tile clears
its dim and sets the other's: the stable slot passes the click). They use the runner's staged ROM paths and SKIP
without the fixtures, like the three existing view tests, and the skip baseline records each one.

Create `packages/retroplug/test-ui/overlay-survives-menu.test.ts`:

````ts
// An overlay survives the instance menu opening and closing (docs/modular-graph-phase-f4c-design section 8.7):
// the menu replaces the tile, which unmounts the overlay's session; the package's flag is the package's, so closing the
// menu reopens the session and the strip comes back without a second press. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60;

test("the LSDj overlay survives the menu opening and closing", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-survives-menu: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // Esc opens the instance menu (into the tile's stable slot, which stays tagged): the tile's body, and with it the
  // overlay's session, is gone while the menu shows.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTextContaining("Duplicate Instance") != null).toBeTruthy();
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);

  // Esc again closes the menu; the flag was never flipped, so the tile's overlay opens again.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  expect(ui.findByTextContaining("v9.4.2") != null).toBeTruthy();
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '11028,11065p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test-ui/overlay-survives-menu.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/overlay-survives-menu.test.ts`
Expected: `c52e4102ccb4b0f67b65231bbc37991a7109c068`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-ui/hd-view-closes.test.ts`:

````ts
// The HD view closes when its system is removed (docs/modular-graph-phase-f4c-design section 8.7): a window
// view is opened for a system's id, and that id going away (here a project load rebuilds the session) must return the
// UI to the grid rather than strand it on a screen with nothing to draw. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, navTo, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";

test("the HD view closes when its system is removed", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("hd-view-closes: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // Open it the way the menu does: an LSDj ROM auto-attaches the sync role, so the submenu has HD Player.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("LSDj")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("HD Player")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-hd") != null).toBeTruthy();

  // A load rebuilds the project under a new system id: the open view's system is gone, so the grid returns. (In a
  // single-instance project a dropped ROM is the guarded "Load...", the same route the menu takes, so the unsaved
  // project asks first; Discard is the load.)
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(40);
  expect(navTo("Don't Save")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(40);
  expect(ui.findByTestId("lsdj-hd")).toBe(null);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '11077,11116p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test-ui/hd-view-closes.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/hd-view-closes.test.ts`
Expected: `bc7ae25ddcd813a74a15ab8602b1bd5a0e7cffdf`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-ui/overlay-actions.test.ts`:

````ts
// Backtick still flips BOTH debug overlays, and rebinding one leaves the other alone (docs/modular-graph-phase-f4c
// design section 8.7): the two cart packages each own a flag and an action, and one press with the defaults
// carries both ids. SKIPs when either fixture ROM was not staged.
import { test, expect, skip, ui, navTo, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const RISA = () => ui.romDir() + "/risa.nes";
const BACKTICK = 0x60;
const KEY_Q = "Q".charCodeAt(0);

test("one backtick toggles both overlays, and rebinding one leaves the other", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-actions: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // A second instance, then risa over it: two carts with a readout each.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  const t1 = ui.findByTestId("tile-1");
  expect(t1 != null).toBeTruthy();
  ui.fileDrop(RISA(), t1!.x + Math.floor(t1!.width / 2), t1!.y + Math.floor(t1!.height / 2));
  ui.pump(40);

  // The fixture gate, decided BEFORE the press: tile 1 must be running risa, or a broken risa toggle would read as a
  // missing ROM (the case exists to prove one press reaches both packages). The instance menu names the cart it
  // resolved: "Risa >" for a risa tile, "LSDj >" for the Game Boy a risa ROM that was not staged leaves behind.
  ui.clickAt(t1!.x + Math.floor(t1!.width / 2), t1!.y + Math.floor(t1!.height / 2));
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(10);
  const risaLoaded = navTo("Risa");
  ui.tapKey(Key.Esc); // close the menu again (Esc is the OpenMenu binding)
  ui.pump(10);
  if (!risaLoaded) skip("overlay-actions: no risa ROM staged in romDir, so the second instance stayed a Game Boy");

  // Backtick on: one press carries both packages' action ids, so both strips appear.
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay")).toBe(null);

  // Rebind only LSDj's action to Q: Settings -> Keyboard Bindings -> its row -> capture.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Keyboard Bindings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("LSDj Debug Overlay")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused()!.text.includes("Press a key")).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.pump(10);
  expect(ui.findByTextContaining("LSDj Debug Overlay: Q") != null).toBeTruthy();
  ui.tapKey(Key.Esc); // close the menu (Esc is the OpenMenu binding)
  ui.pump(10);

  // Backtick now reaches risa only; Q is LSDj's.
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy(); // risa is untouched by LSDj's rebind
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '11128,11214p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test-ui/overlay-actions.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/overlay-actions.test.ts`
Expected: `ef2a2922a98206491d003007cc252edba1aa5f7d`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test-ui/overlay-click-through.test.ts`:

````ts
// A tile's view slot passes the click (docs/modular-graph-phase-f4c-design section 5.2): a slot is
// a transparent full-tile Box, so clicking an unfocused tile with an overlay SHOWING must still focus it, exactly as
// clicking the dim layer does when no view covers it. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, navTo, Key, type WidgetInfo } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60;

function clickCenter(w: WidgetInfo): void {
  ui.clickAt(w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2));
}

test("clicking an unfocused tile focuses it while the LSDj overlay shows", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-click-through: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // A second instance, and the overlay shown over both.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // Tile 0 is focused after the duplicate, so tile 1 is dimmed; the click lands on its overlay strip's slot.
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-1")).toBe(null);
  expect(ui.findByTestId("dim-0") != null).toBeTruthy();
});
````

Run: `mkdir -p packages/retroplug/test-ui; sed -n '11226,11264p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test-ui/overlay-click-through.test.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test-ui/overlay-click-through.test.ts`
Expected: `7ac9f5a5747cf048f802f7faf6d282b62fe42e64`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 8: `__RESOURCES_DIR__` for the pure runner, and the skip baseline**

The pure runner now computes `RESOURCES_DIR` (`RETROPLUG_RESOURCES_DIR`, else the sibling tree) and injects it as
`__RESOURCES_DIR__`, as the native runner does, so a pure case can hold a real ROM and never reads `tjs.env`. The
package's pixel case uses it: it reads the fixture ROM, paints the 792x576 HD surface and asserts a second frame
reports no dirty tile; without the fixture it SKIPs, which is the one `ts` entry the skip baseline gains. The
baseline's other four new entries are the `test:ui` files above, one documented skip each; no existing entry's counts
change (a check in `gen_checks.py` reads the file's JSON).

````diff
diff --git a/packages/retroplug/scripts/run-tests.mjs b/packages/retroplug/scripts/run-tests.mjs
index 882724836..7c32798ac 100644
--- a/packages/retroplug/scripts/run-tests.mjs
+++ b/packages/retroplug/scripts/run-tests.mjs
@@ -66,6 +66,11 @@ if (!existsSync(TJS)) {
 const jobs = resolveJobs();
 const filter = stripRunnerFlags()[0];
 
+// The sibling resources tree (ROMs/manuals/...), overridable exactly as the native runner's is: injected into the
+// bundle as __RESOURCES_DIR__ so a pure-tier case that needs a real ROM reaches it through a define, never through
+// tjs.env. Pointing it at a nonexistent directory is how CI's picture is reproduced.
+const RESOURCES_DIR = process.env.RETROPLUG_RESOURCES_DIR || resolve(REPO, "../resources");
+
 function walk(dir, out = []) {
   if (!existsSync(dir)) return out;
   for (const ent of readdirSync(dir, { withFileTypes: true })) {
@@ -123,7 +128,10 @@ async function runOne({ file, slug }) {
       mainFields: ["module", "main"],
       target: "es2020",
       outfile: outFile,
-      define: { "process.env.NODE_ENV": '"production"' },
+      define: {
+        "process.env.NODE_ENV": '"production"',
+        __RESOURCES_DIR__: JSON.stringify(RESOURCES_DIR),
+      },
     });
   } catch (e) {
     flush(`BUILD FAILED: ${slug}`, `${e?.message ?? e}`);
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index a3485e3ef..3976e4ebc 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -261,8 +261,17 @@
    "total": 2
   }
  },
- "ts": {},
+ "ts": {
+  "lsdj/ui-views": {
+   "skipped": 1,
+   "total": 5
+  }
+ },
  "ui": {
+  "hd-view-closes": {
+   "skipped": 1,
+   "total": 1
+  },
   "lsdj-hd": {
    "skipped": 1,
    "total": 1
@@ -271,6 +280,18 @@
    "skipped": 1,
    "total": 1
   },
+  "overlay-actions": {
+   "skipped": 1,
+   "total": 1
+  },
+  "overlay-click-through": {
+   "skipped": 1,
+   "total": 1
+  },
+  "overlay-survives-menu": {
+   "skipped": 1,
+   "total": 1
+  },
   "risa-overlay": {
    "skipped": 1,
    "total": 1
````

Run: `sed -n '11283,11352p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/run-tests.mjs
packages/retroplug/scripts/skip-baseline.json
EOF
````

Expected:

````text
7c32798acf4b7695a36b9c725315757f8b8a065f
3976e4ebc6a05277b69150953d94679771c7f714
````

- [ ] **Step 9: The ratchet's list is emptied**

`test/boundaries/ui-cart-imports.test.ts` listed the five files this task moves. Its list is emptied and the test now
requires an empty hit set: it stays until Task 5 replaces it with rule 9 over `ui/`, which is the check that covers an
allow-based walk rather than a derived list.

````diff
diff --git a/packages/retroplug/test/boundaries/ui-cart-imports.test.ts b/packages/retroplug/test/boundaries/ui-cart-imports.test.ts
index 09371dd65..825f59e1d 100644
--- a/packages/retroplug/test/boundaries/ui-cart-imports.test.ts
+++ b/packages/retroplug/test/boundaries/ui-cart-imports.test.ts
@@ -1,25 +1,18 @@
-// The ui/ ratchet (docs/modular-graph-phase-f4b-design.md section 7.3): until F4c moves the remaining React views into
-// their packages, ONLY the five view files below may import a cart's code, and each must still import one (so F4c
-// deletes this list by emptying it, rather than leaving a rule that has quietly stopped applying to anything). The two
-// overlays are React and the three hooks/data modules are not; all five are F4c's views.
+// The ui/ ratchet (docs/modular-graph-phase-f4b-design.md section 7.3): F4b left five view files allowed to import a
+// cart's code, each still required to import one. F4c's moves took all five into their packages, so the list is now
+// empty and the test requires an empty hit set; it stays (with its runner and fixtures) until the allow-based rule
+// replaces it, rather than being deleted with nothing to show the boundary held.
 //
-// Everything else under ui/ reaches a cart, a console or a device through the generated registries: ui/generated/menus.ts
-// (not judged: it is the one place that names every package), src/generated/* through a store, or - for a menu entry -
-// the package's own `ui` module, which the device menus moved into. A view importing a cart's runtime is the last of
-// F4c's work, which is why the list exists rather than a broad permission.
+// Under ui/, a cart, a console or a device is reached through the generated registries: ui/generated/menus.ts (not
+// judged: it is the one place that names every package), src/generated/* through a store, or - for a menu entry - the
+// package's own `ui` module, which the device menus moved into.
 //
 // The scanner is Node's (the boundary check's), so the test spawns test/boundaries/run-ui-cart-imports.mjs.
 import { test, expect } from "@retroplug/testing";
 
-/** F4b's five remaining cart imports under ui/: the two overlays, the two runtime hooks and the HD session, which F4c
- *  moves into their packages. */
-const ALLOWED = [
-  "packages/retroplug/ui/screens/grid/LsdjOverlay.tsx",
-  "packages/retroplug/ui/screens/grid/RisaOverlay.tsx",
-  "packages/retroplug/ui/screens/grid/useLsdjRuntime.ts",
-  "packages/retroplug/ui/screens/grid/useRisaRuntime.ts",
-  "packages/retroplug/ui/screens/hd/useLsdjHdSession.ts",
-];
+/** The list F4b left, emptied by the moves that took the five files into their packages: the test is kept so the
+ *  ratchet's runner and fixtures still hold an empty hit set until the allow-based rule replaces it. */
+const ALLOWED: string[] = [];
 
 // The packages whose code a view may not name are DERIVED by the runner from the workspace manifests: any package that
 // declares code or baked bytes (an app, ui, cli, examples, protocol, consoles, embed or script member) is guarded, and a
@@ -70,7 +63,7 @@ async function scan(root?: string): Promise<{ hits: { file: string; specifier: s
   return JSON.parse(out.trim().split("\n").at(-1)!) as { hits: { file: string; specifier: string }[] };
 }
 
-test("only the five remaining view files import a cart, a console or a device package", async () => {
+test("no ui/ file imports a cart, a console or a device package", async () => {
   const { hits } = await scan();
   const files = [...new Set(hits.map((hit) => hit.file))].sort();
   // A file NOT on the list importing one is the ratchet biting: it would reach a cart's code directly.
````

Run: `sed -n '11381,11429p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object packages/retroplug/test/boundaries/ui-cart-imports.test.ts`
Expected: `825f59e1d4c75642de74ae11604af2b7b4a170e0`: the blob b841dc4e2 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 10: The two docs' links**

`docs/lsdj.md`'s two read-path links and `spec/07-remaining-work.md`'s HD link named the moved files by path; they are
repointed and their prose is the fix pass's (Task 5's docs pass carries the rest).

````diff
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 375b3e18b..bb449df76 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -181,8 +181,8 @@ const state = reader.read(wramBytes);                  // -> LsdjState (supporte
   render regime). Running plugin: `backend.readRam(id)` — the SnapshotRegistry per-block WRAM
   triple on the emulator facet, safe while audio plays (the CLI-only debug `readMemory` is not
   on the plugin channel). The live overlay
-  ([LsdjOverlay.tsx](../packages/retroplug/ui/screens/grid/LsdjOverlay.tsx) +
-  [useLsdjRuntime.ts](../packages/retroplug/ui/screens/grid/useLsdjRuntime.ts)) reads `readRam`
+  ([LsdjOverlay.tsx](../packages/lsdj/ts/ui/overlayView.ts) +
+  [useLsdjRuntime.ts](../packages/lsdj/ts/ui/overlayView.ts)) reads `readRam`
   each frame and renders the state over the emulator tile.
 
 Tests: [test/lsdj/runtime.test.ts](../packages/lsdj/ts/test/runtime.test.ts) +
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 19cbb47ae..a29ee9455 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -49,7 +49,7 @@ built ([05-data-persistence.md](05-data-persistence.md)).
 There is no live "watch RAM" streaming path. Reads are one-shot through
 the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
 consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
-([useLsdjHdSession.ts](../packages/retroplug/ui/screens/hd/useLsdjHdSession.ts)) on a frame divider,
+([useLsdjHdSession.ts](../packages/lsdj/ts/ui/hdView.ts)) on a frame divider,
 because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
 cost question, not a capability one - an arming seam would let those two stop copying the whole image
 to read a few hundred bytes of it. Nothing of the old mechanism is left to switch on: `enableMemorySnapshot`,
````

Run: `sed -n '11444,11471p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
docs/lsdj.md
spec/07-remaining-work.md
EOF
````

Expected:

````text
bb449df765241455f3b63afa5fbedf983b365e36
a29ee945584582c82fb7c57070968b37d7436a06
````

- [ ] **Step 11: The red proofs: the old lines and the old cadence, and the guard**

The expected lines and the SRAM cadence were recorded from the tree BEFORE the moves (`git archive` of stage 3's
commit, `pnpm install`, the submodules linked), by a throwaway recorder holding the old overlay line expressions
verbatim over that tree's `LsdjReader.read` / `decodeRisaState`, and the old `HdSession` class body verbatim (minus
its font, palette and render, which need the display) over a fake backend. It printed the three lines per overlay and
the frames the SRAM read happens at, which the package's cases then assert. The recorder is not in any commit, and the
log is the record:

````text
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))

# menu/old-record
{
 "lsdjLines": [
  "SONG @6,1",
  "PLAY S05  128bpm  v9.4.2",
  "1 10.03  2 11.04  W --  N --"
 ],
 "risaLines": [
  "SONG @4,3",
  "SONG  128bpm  v2.2.1",
  "1 0A.05  2 --  T 20.0A  N --  D --"
 ],
 "hdReadsAt": [
  0,
  16,
  32
 ],
 "hdSongIdentities": [
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "first",
  "other"
 ]
}
TAP version 13
1..1
ok 1 - old outputs
# 1 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
````

Run: `sed -n '11503,11563p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `39562b2d721d78675be920295149b9d3d9b16c37d098508206ee09a66e2bdf80  -`: the log's own bytes (`/tmp/f4cpro/s4-old-record.log`), so the block above is the run and not a selection from it. the old code's own answers, which the new views reproduce; the package cases assert these exact strings and frames.

The `PixelView` guard's RED is the `lsdj-hd` case failing with the guard reverted, recorded during the rebase:
`/tmp/f4cpro/fx-lsdj-hd.log` failed on `expect(distinctColors(snap) > 2)` (the HD surface was black), and
`/tmp/f4cpro/fx-lsdj-hd2.log` passed with the guard in stage 2. No tier of stage 2 could show it (the pure suite has
no React, and the fixture package is in no product), which is why the case is stage 4's and the guard stage 2's. The
reproduction at this task's tree: revert `PixelView.tsx` to stage 3's version (`git show
eb20634ea:packages/retroplug/ui/views/PixelView.tsx > ...`) and run `pnpm test:ui lsdj-hd` with the fixtures; it fails
on that expectation, and passes with this task's file back.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui lsdj-hd`
Expected: exit 0, `ok 1 - the LSDj HD player opens from the instance menu and Esc returns to the grid` and `# 1 passed, 0 skipped, 0 failed`: the unchanged case over the real view, which is what pins the guard (the stage 5 fix pass's log re-ran it at the final tree).

- [ ] **Step 12: Configure, install, build, and the bytes that must not move**

Run: `cmake -S . -B build`
Expected: exit 0: no product lists a package change (the carts already are products), so the configure only re-reads the manifests and the generator's inputs.

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and the lockfile is Task 1's: this task adds no dependency.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree: the UI bundle shrinks by the shell's seven view files and grows by the general renderers, and the generated menus module now carries the two carts' views and actions.

Run: `cmake --build build --target retroplug-ui-test -j$(nproc)`
Expected: exit 0: the UI test binary embeds the production bundle, which is what the four new `test:ui` cases run.

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

- [ ] **Step 13: Run the suites**

Task 4's figures are stage 4's own full run (`/tmp/f4cpro/s4v-*.log`, every leg exit 0, no retries, at `4bcc8dc93`),
whose tree equals this task's commit: the rebase that folded the `PixelView` guard back into stage 2 and amended the
messages left stage 4's tree unchanged. The figures include the phase's only record, the menu matrix (E1).

The verification is the parallel kit, one command, run in the foreground and waited for: it sets each leg's own
`RETROPLUG_RESOURCES_DIR`, skips `sdl:controller` (which claims system-wide ALSA ports) and logs one file per leg. Run
`pnpm sdl:controller` on its own afterwards, never another way, and compare the summary and the gated logs with the
baseline at the design commit.

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4c-v bash .superpowers/verify/tip-verify-par.sh`
Expected: exit 0 and `== every leg passed`, with no `/tmp/f4c-v-retries.txt` (a retry is a leg the kit re-ran; report it). The base's run is `/tmp/f4cpro/base-*` and every leg below quotes this task's own log.

Run: `pnpm sdl:controller`
Expected: exit 0: `SDL controller OK`, after fifteen `ok:` lines. The first ten are the bliptoaster pack's toolchain, which the SDL build runs and prints (the same ten the baseline's log has), and the last five are the harness's own legs, byte-identical to the baseline's. It sets `RP_MIDI_PORT_ALLOW` itself, from the two fakes' ready lines, and refuses a caller's list. The five:

````text
  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream
  ok: reclaimed before MIDI opens and audio starts; its notes kept out of the shared stream; released at quit
  ok: Programmer mode first, LED traffic, the release last
  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside
  ok: a second standalone leaves the first one's device alone, and does not hear its pads
```` This is the one command in this plan that may touch a MIDI port at all, and it sets `RP_MIDI_PORT_ALLOW` from its two ALSA fakes itself.

Run: `bash .superpowers/verify/compare.sh /tmp/f4cpro/base /tmp/f4c-v`
Expected: exit 1 (anything differs), printing only the lines this task is expected to move: the `test` case and file counts, `ui`'s from Task 4, Task 1's two `typecheck` programs and the `measure` sizes (info only, never gated). `imagesha` and `packbytes` must be ABSENT from the diff, and every other leg's counts must be the baseline's. A difference the task does not name is a finding.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree. No compiler warning is expected: every stage's build log has none.

Run: `pnpm typecheck`
Expected: `Scope: 25 of 26 workspace projects`, then `packages/retroplug typecheck: Done`: every project with a `typecheck` script typechecks on its own tsconfig, and the shell's program covers every package's `ts/` as well and `packages/ui-react typecheck: Done` and `packages/ui-fixture typecheck: Done` (Task 1's two packages, unchanged). The baseline's scope is `Scope: 23 of 24 workspace projects` with 17 `Done` lines; this tree's is 25 of 26 and 19.

Run: `pnpm test`
Expected: `# 227 test file(s) passed (jobs=5)`. The runner's own line counts FILES; the case total is the stage 4 report's table: "`test`: `tap ok=2286 plan=2286`, 219 files -> `tap ok=2357 plan=2357`, 227 files", since no runner prints one in this suite. The pool size it prints is the kit's `TV_TS_JOBS` (5); a hand run prints its own, so the file count is what must match.

Run: `pnpm test:plugin`
Expected: `# 13 plugin test binary(ies) passed (jobs=5)`: every binary exits 0, and the counts are (quoted as the log prints them, the engine-graph suite's assertion total and its eleven skips as that binary prints them):

````text
retroplug-mesen-test: `All tests passed (82321 assertions in 80 test cases)`
retroplug-audio-test: `All tests passed (378 assertions in 16 test cases)`
retroplug-watcher-test: `All tests passed (6 assertions in 2 test cases)`
retroplug-launchpad-test: `All tests passed (12925 assertions in 53 test cases)`
retroplug-classid-test: `All tests passed (10 assertions in 3 test cases)`
retroplug-plugin-test: `All tests passed (21 assertions in 6 test cases)`
retroplug-lottie-test: `All tests passed (7 assertions in 1 test case)`
retroplug-dynparams-test: `All tests passed (24 assertions in 7 test cases)`
retroplug-midi-test: `All tests passed (78 assertions in 24 test cases)`
retroplug-graph-test: `All tests passed (356662 assertions in 296 test cases)`
retroplug-sameboy-test: `All tests passed (1152140 assertions in 7 test cases)`
retroplug-device-mode-test: `All tests passed (1504408 assertions in 96 test cases)`
retroplug-engine-graph-test: `test cases: 336 | 325 passed | 11 skipped`
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1714663 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the fixture-gated cases run.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, none skipped, nothing re-recorded: 30 files and 250 cases (the reports' count), the device images' and the pack's bytes identical to the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 144 case(s) SKIPPED in 65 file(s)` (its list of files follows) and the skip check passing: CI's view, its skips the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 47 UI test file(s) passed (jobs=4)`. The case total is the stage 4 report's: "`ui`: `ok=49` / 43 files -> `ok=53` / 47 files", the four new fixture-gated cases (a case that cannot run SKIPS and is counted, and this leg runs WITH the fixtures).

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks.

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

Run: `pnpm test:n8`
Expected: `All tests passed (1468 assertions in 104 test cases)` (`retroplug-n8-test`) and `All tests passed (17 assertions in 2 test cases)` (`retroplug-serial-test`), the baseline's: fakes only, no port is opened (the `[retroplug] N8 restore: port '/dev/ttyACM0'` line it prints is a test case's, over a fake port factory and a scratch n8.cfg; the baseline's run printed it too).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed (the counts are the reports' tables, each stage's).

Run: `bash -c 'for t in render-menu render-badge; do pnpm test:ui $t || exit 1; done'`
Expected: exit 0: `render-menu` and `render-badge`, one test file each and 1 passed.

Run: `pnpm test render`
Expected: `# 9 test file(s) passed (jobs=5)`: the render worker's own files.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after the smoke's eight `ok:` lines (the build before them prints the bliptoaster pack's toolchain lines, which start `ok:` too). It runs under a scratch config dir with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which matches nothing, so it claims and opens nothing.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three ThreadSanitizer binaries, built from this tree's sources (their objects are the prototype's until the first build here).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1700153 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`.

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'`
Expected: `All tests passed (2029 assertions in 53 test cases)`.

Run: `bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'`
Expected: `All tests passed (1499768 assertions in 51 test cases)`.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three AddressSanitizer binaries, built from this tree's sources.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1682354 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356662 assertions in 296 test cases)`.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'`
Expected: `All tests passed (1504408 assertions in 96 test cases)`. The prototype's first full run failed this leg once, on the script pack's deadline case under ASAN on a loaded machine (no device-mode code touched by any stage), and its re-run was green; every stage's recorded run after it is green. If yours fails, re-run it ONCE and report both.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (1504408 assertions in 96 test cases)`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Run: `bash -c 'for t in lsdj-hd lsdj-overlay risa-overlay overlay-survives-menu hd-view-closes overlay-actions overlay-click-through; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui $t || exit 1; done'`
Expected: exit 0: each of the seven view and action cases runs its own process and prints `# 1 passed, 0 skipped, 0 failed` (the three existing ones unchanged, the four new ones). The stage 4 report's log lists the same seven, and the stage 5 fix pass re-ran them at this tree.

Then the per-stage tree rule, which the prototype adopted after a broken intermediate commit: the tree this task
commits is extracted from the commit AFTER the commit step (so it is the sealed tree), proven equal to it by a fresh
index read from the commit, installed, generated, typechecked and tested on its own. Nothing here links this
worktree's `build/`; the pure runner reads the tjs binary through `RETROPLUG_TJS` instead.

Run: `bash -c 'D=/tmp/f4c-tree-4; rm -rf "$D" && mkdir -p "$D" && git archive b841dc4e2 | tar -x -C "$D" && echo extracted'`
Expected: `extracted`: the commit's tree, with no generated module in it (they are gitignored and `git archive` does not carry them) and no `build/`.

Run: `bash -c 'D=/tmp/f4c-tree-4; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree b841dc4e2 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-4-eq.txt; e=$?; rm -f $E; wc -l < /tmp/f4c-tree-4-eq.txt'`
Expected: `0`: the extracted tree is its commit's, line for line, read against a fresh index because this worktree's own index holds a later task's deletions. `deps` is excluded: its submodule directories are links, which is the next step's doing.

Run: `bash -c 'D=/tmp/f4c-tree-4; W=$PWD; for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done; cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo wired'`
Expected: `wired`: the archive's submodule paths are links to this worktree's checkouts (read only) and its own offline install wires the workspace's links from the COMMIT's lockfile, which is also what proves that lockfile installs.

Run: `bash -c 'D=/tmp/f4c-tree-4; T=/workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs; cd "$D" && pnpm -r typecheck && RETROPLUG_TJS=$T node packages/retroplug/scripts/run-tests.mjs menu && node packages/retroplug/scripts/run-tests.mjs packages && node packages/retroplug/scripts/run-tests.mjs hosts && node packages/retroplug/scripts/run-tests.mjs ui && node packages/retroplug/scripts/run-tests.mjs lsdj/ui-views && node packages/retroplug/scripts/run-tests.mjs risa/ui-views'`
Expected: exit 0: every package typechecks against its own tsconfig, and each named suite passes on this tree alone, with the worktree's tjs binary read through `RETROPLUG_TJS` and nothing written to this worktree. The per-tree record, which this plan's generator ran for all five trees (each extract equal to its commit before and after, 19 `Done` lines, every suite exit 0): tree 1 `menu` 17 files, `packages` 2, `hosts` 2, `boundaries` 3, `ui-react/reactView` 1 and `script/bundle-ui-split` 1; tree 2 `menu` 17, `packages` 2, `hosts` 2, `ui` 4 and `boundaries` 3; tree 3 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `input` 3, `config` 6 and `boundaries` 3; tree 4 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `lsdj/ui-views` 1 and `risa/ui-views` 1; tree 5 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `boundaries` 2 and `source-literals` 1.

Run: `bash -c 'D=/tmp/f4c-tree-4; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree b841dc4e2 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-4-eq2.txt; rm -f $E; wc -l < /tmp/f4c-tree-4-eq2.txt'`
Expected: `0`: the install, the generator and the suites changed no tracked file, so the tree is still its commit's after the runs (the generated modules and the runner's build dir are ignored).

- [ ] **Step 14: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 m deps/sameboy
 M docs/lsdj.md
 M packages/lsdj/package.json
RM packages/retroplug/ui/screens/hd/useLsdjHdSession.ts -> packages/lsdj/ts/ui/hdView.ts
 M packages/lsdj/ts/ui/index.ts
 M packages/retroplug/scripts/run-tests.mjs
 M packages/retroplug/scripts/skip-baseline.json
 M packages/retroplug/test/boundaries/ui-cart-imports.test.ts
 M packages/retroplug/test/menu/action-rows.test.ts
 M packages/retroplug/test/menu/asset-file-actions.test.ts
 M packages/retroplug/test/menu/bliptoaster.test.ts
 M packages/retroplug/test/menu/launchpad.test.ts
 M packages/retroplug/test/menu/leaves.test.ts
 M packages/retroplug/test/menu/lsdj-assets.test.ts
 M packages/retroplug/test/menu/lsdj-songs.test.ts
 M packages/retroplug/test/menu/n8-sd-ops.test.ts
 M packages/retroplug/test/menu/recent-rows.test.ts
 M packages/retroplug/test/menu/recorded-cases.ts
 M packages/retroplug/test/menu/recorded-matrix.json
 M packages/retroplug/test/menu/risa.test.ts
 M packages/retroplug/test/menu/rom-patch.test.ts
 M packages/retroplug/test/menu/song-guard.test.ts
 M packages/retroplug/test/menu/song-import.test.ts
 M packages/retroplug/test/tracker/working-song-save.test.ts
 M packages/retroplug/ui/App.tsx
 M packages/retroplug/ui/host/menuContributions.ts
 M packages/retroplug/ui/host/uiHost.ts
 M packages/retroplug/ui/screens/grid/EmulatorTile.tsx
D  packages/retroplug/ui/screens/grid/LsdjOverlay.tsx
D  packages/retroplug/ui/screens/grid/RisaOverlay.tsx
D  packages/retroplug/ui/screens/grid/lsdjDebug.ts
D  packages/retroplug/ui/screens/grid/useLsdjRuntime.ts
D  packages/retroplug/ui/screens/grid/useRisaRuntime.ts
D  packages/retroplug/ui/screens/hd/LsdjHdScreen.tsx
 M packages/retroplug/ui/screens/menu/menuDefs.ts
 M packages/risa/package.json
 M packages/risa/ts/ui/index.ts
 M packages/ui-react/ts/ui/DebugReadout.tsx
 M packages/ui-react/ts/ui/index.ts
 M packages/ui/package.json
 M spec/07-remaining-work.md
?? packages/lsdj/ts/test/ui-views.test.ts
?? packages/lsdj/ts/ui/debugOverlay.ts
?? packages/lsdj/ts/ui/overlayView.ts
?? packages/retroplug/test-ui/hd-view-closes.test.ts
?? packages/retroplug/test-ui/overlay-actions.test.ts
?? packages/retroplug/test-ui/overlay-click-through.test.ts
?? packages/retroplug/test-ui/overlay-survives-menu.test.ts
?? packages/risa/ts/test/ui-views.test.ts
?? packages/risa/ts/ui/debugOverlay.ts
?? packages/risa/ts/ui/overlayView.ts
?? packages/ui/ts/format.ts
````

````bash
git add -- docs/lsdj.md packages/lsdj/package.json packages/lsdj/ts/test/ui-views.test.ts \
  packages/lsdj/ts/ui/debugOverlay.ts packages/lsdj/ts/ui/hdView.ts packages/lsdj/ts/ui/index.ts \
  packages/lsdj/ts/ui/overlayView.ts packages/retroplug/scripts/run-tests.mjs \
  packages/retroplug/scripts/skip-baseline.json packages/retroplug/test-ui/hd-view-closes.test.ts \
  packages/retroplug/test-ui/overlay-actions.test.ts packages/retroplug/test-ui/overlay-click-through.test.ts \
  packages/retroplug/test-ui/overlay-survives-menu.test.ts \
  packages/retroplug/test/boundaries/ui-cart-imports.test.ts packages/retroplug/test/menu/action-rows.test.ts \
  packages/retroplug/test/menu/asset-file-actions.test.ts packages/retroplug/test/menu/bliptoaster.test.ts \
  packages/retroplug/test/menu/launchpad.test.ts packages/retroplug/test/menu/leaves.test.ts \
  packages/retroplug/test/menu/lsdj-assets.test.ts packages/retroplug/test/menu/lsdj-songs.test.ts \
  packages/retroplug/test/menu/n8-sd-ops.test.ts packages/retroplug/test/menu/recent-rows.test.ts \
  packages/retroplug/test/menu/recorded-cases.ts packages/retroplug/test/menu/recorded-matrix.json \
  packages/retroplug/test/menu/risa.test.ts packages/retroplug/test/menu/rom-patch.test.ts \
  packages/retroplug/test/menu/song-guard.test.ts packages/retroplug/test/menu/song-import.test.ts \
  packages/retroplug/test/tracker/working-song-save.test.ts packages/retroplug/ui/App.tsx \
  packages/retroplug/ui/host/menuContributions.ts packages/retroplug/ui/host/uiHost.ts \
  packages/retroplug/ui/screens/grid/EmulatorTile.tsx packages/retroplug/ui/screens/menu/menuDefs.ts \
  packages/risa/package.json packages/risa/ts/test/ui-views.test.ts packages/risa/ts/ui/debugOverlay.ts \
  packages/risa/ts/ui/index.ts packages/risa/ts/ui/overlayView.ts packages/ui-react/ts/ui/DebugReadout.tsx \
  packages/ui-react/ts/ui/index.ts packages/ui/package.json packages/ui/ts/format.ts spec/07-remaining-work.md
git commit -F - <<'EOF'
feat(f4c): the LSDj HD player and the two debug overlays are the packages'

The moves of docs/modular-graph-phase-f4c-design.md section 6, and the edge its section 7 records. After this
commit the shell's ui/ outside generated/ imports no cart: LSDj and risa own their views and their keys.

LSDj (packages/lsdj/ts/ui/): hdView.ts is a window view, id "lsdj-hd", for an lsdj-sync system, a pixels body whose
unsupported text is the old screen's. HdSession is useLsdjHdSession's class moved: the 0x150 header through
host.files.readPrefix, the whole ROM through host.files.readFile, the lsdj-assets overrides folded in through the
host's files and png, readSram on the same 15-frame countdown with the same FNV signature over the first 0x8000
bytes, readRam per frame, and frame() is renderFrame() plus the canvas size. Its open refuses a system with no
lsdj-sync role or no ROM path itself, since the shell checks a view's when only at views.open. overlayView.ts is the
tile view "lsdj-overlay": the old hook's sampler as plain code over host.files.readPrefix and host.systems.readRam,
with the old three lines verbatim, and the DEFAULT signature (the lines and the flag are exactly what the strip
draws, so the old hook's narrower signature bought nothing visible). debugOverlay.ts is the package's own Signal,
off at start and not persisted, and the action "lsdj.debug-overlay" whose run ignores ctx.systemId so it still works
on the start screen. The HD Player row is unchanged: host.views.open("lsdj-hd", sys.id).

risa (packages/risa/ts/ui/): the overlay and its action, "risa-overlay" and "risa.debug-overlay", the same way;
its lines and 4X tempo rule are RisaOverlay's verbatim. Its tsconfig does NOT gain the generated host-modules extend:
risa declares no React host module and lists no examples, so the generator's rule (examples or a UI host module)
does not ask it for one (design 8.1 said a package with views would; stage 1 did not build that clause, and a plain
overlay needs no React path).

The views declare the host groups they read and nothing more (hdView's open and buildSession Host<"files" | "png" |
"systems">, HdSession and both overlays' open Host<"files" | "systems">/Host<"systems">), and each is declared with
satisfies rather than a ViewDef annotation, so a caller - the shell, or a test over a fake host - sees that minimum
instead of widening to the contract's whole UiHost. hex2 is defined once, in @retroplug/ui (a new "./format" export,
plain TypeScript), because a plain-TypeScript package cannot import the kit for a string format: both overlays import
it and @retroplug/ui-react re-exports it.

The shell: App's "lsdj-hd" branches, its backtick line and KEY_BACKTICK constant, and the interim comments go;
openViewOf is the generated registry's lookup alone. uiViews and MenuContext.openLsdjHd go, with every test's
openLsdjHd stub. A window open's registry checks are one function now (windowViewOrReport, in menuContributions.ts):
App runs it and then the two checks that need the project, and the menu host built over the stores alone (the menu
tests, which have no App and therefore no window) runs the same function, so an unknown id or a tile view's id is
refused in the same words wherever it was asked from. EmulatorTile's two hard-mounted overlays go; the tile slots the
two packages' views land in are stage 2's. ui/screens/hd/ and the five moved grid files are gone. Both carts'
package.json descriptions name their views and actions.

E1, the one recorded edge: the menu matrix's two bindings menus gain the four action rows per case (LSDj Debug
Overlay and risa Debug Overlay, keyboard and gamepad), and nothing else moved. The record was spliced from the NEW
code's binding menus into the existing file, and the result is semantically identical to a full new record of the
matrix, so the diff is exactly 35 cases x 2 menus x 2 items (700 added lines, 0 removed, 70 hunks). A profile that
already maps an id keeps its own mapping (E3), a backtick the user had bound to a shell action now toggles AND runs it
(the accepted edge, design 7), and the two flags can disagree after a rebind (E2). leaves.test.ts's two capture-row
counts read 13 (8 GB + the shell's 3 + the 2 new).

Tests: 2 new pure package files (lsdj/ui-views 5 cases, risa/ui-views 3) over a fake host that supplies every member
of the groups those views declare. The expected lines and the cadence were recorded from the tree before the moves by
a throwaway recorder: the old overlay line expressions copied verbatim over that tree's LsdjReader.read and
decodeRisaState, and the old HdSession class body verbatim (minus its font, palette and render, which need the
display) over a fake backend. It printed the three lines per overlay and readSram at frames 0, 16 and 32; the
fixture-gated test:ui cases are what hold the real components. The cases also pin an unreadable RAM frame
(undefined), risa's too-short image (an unsupported state, so null), each overlay's open refusing a system without
its role, and each action's id, label, keys and toggle. The LSDj pixel case reads the fixture ROM through the runner's
__RESOURCES_DIR__ define (scripts/run-tests.mjs now injects it, as the native runner does), paints the 792x576
surface, and a second frame reports no dirty tile; it SKIPs without the fixture.

Four new fixture-gated test:ui files: an overlay survives the menu opening and closing, the HD view closes when its
system is removed, one backtick toggles both overlays and rebinding one leaves the other, and a click on an unfocused
tile lands through a showing overlay's slot. The both-overlays case decides its fixture gate BEFORE the press (tile
1's instance menu names "Risa" only when the second drop really loaded risa), so a risa action that stopped firing
fails the case instead of skipping it. The skip baseline gains only those new files' documented skips (ts:
lsdj/ui-views; ui: the four new files).

Verification: pnpm typecheck green; the full pure suite 227 files / 2357 plan (2356 passed, the one documented
skip); test:ui 47 files / 53 cases; the three view tests unchanged and green with RETROPLUG_RESOURCES_DIR set; the
full parallel kit passes every leg (no retries) and sdl:controller passes alone. Against base, compare.sh shows only
the test counts (2286 -> 2357, 219 -> 227 files; ui 49 -> 53, 43 -> 47), this phase's two typecheck programs, and the
new skip line; imagesha and packbytes are identical. grep for a cart specifier under ui/ outside generated/ is empty,
and check:boundaries and check:docs are clean.

Not re-recorded: parity, the kit and render baselines, the device and box images, the pack bytes and the buildGraph
and role goldens. The matrix is the one record, and its diff is the E1 record.
EOF
````

---

### Task 5: rule 9 and the scanner over `ui/`, the ratchet retired, the docs

Design sections 8.3, 8.4 and 9's fifth stage. The shell's `ui/` is judged like `src/` and `cli/` now: rule 9 gains a
third root with a list of its own, the literal scanner a third directory, and the F4b ratchet and its runner are deleted
with their fixtures becoming the rule's. The docs pass lands, and the design's as-built section records the build.

By this stage the live tree is already clean, so each RED is a fixture tree of the pre-move shape or an extracted
pre-move tree: a check cannot be shown failing on a tree that already passes.

**Files:**

- Create (14): `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts`, `packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json`, `packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts`, `packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts`
- Move (2 files, each by the `git mv` its step prints; byte identical): from `packages/retroplug/test/boundaries/fixtures/ui-device/packages/launchpad/`, `packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/`
- Modify (18 files, as one diff block per group its step names: 5 under `spec/`; 3 under `docs/`; 2 under `packages/retroplug/test-ui/`; 2 under `packages/retroplug/test/source-literals/`; 1 under `AGENTS.md`; 1 under `packages/retroplug/scripts/` and the rest)
- Delete (5): `packages/retroplug/test/boundaries/fixtures/ui-device/packages/retroplug/ui/probe.ts`, `packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/ts/index.ts`, `packages/retroplug/test/boundaries/fixtures/ui-relative/packages/retroplug/ui/evil.ts`, `packages/retroplug/test/boundaries/run-ui-cart-imports.mjs`, `packages/retroplug/test/boundaries/ui-cart-imports.test.ts`

- [ ] **Step 1: Rule 9's third root: `UI_IMPORTS`, and what `ui/` may reach**

`check-boundaries.mjs` gains `UI_IMPORTS`, the UI's own list beside `SHELL_IMPORTS`, ten entries each with the reason
it is a contract: `@retroplug/ui/menu`, `@retroplug/ui/contribution`, `@retroplug/ui-react`, `@retroplug/core/host`,
`@retroplug/core/systemRoles`, `@retroplug/core/midiRouting`, `@retroplug/launchpad/enums`,
`@retroplug/tracker/songCatalog`, `@retroplug/tracker/assetCatalog`, `@retroplug/tracker/trackerIntegration`. It is
deliberately NOT `SHELL_IMPORTS`: the kit and the view contract are the UI's alone, so no `src/` or `cli/` file may
name `@retroplug/ui-react`, and the control plane has no use for the menu model. A third root `{ dir: ui, ignores:
[ui/generated/], reaches: [ui, src] }` says a `ui/` file may reach `ui/` or `src/` relatively (the UI is a surface
over the stores the control plane owns) and nothing else; `ui/generated/` is not judged, as `src/generated/` is not.
Each root judges its bare imports against its own list, and an entry no import of its root uses fails the check (as an
unused exception does). `SHELL_IMPORTS` loses its `export`: the deleted ratchet's runner was its only other reader.
Four fixture trees hold it: `shell-ui-forbidden` (a `ui/` file naming a device pack's client), `shell-ui-unlisted` (a
`ui/` file naming `@retroplug/core/path`, which is on the shell's list and not the UI's: the case that proves the list
is the UI's own), `shell-ui-relative` (a `ui/` file reaching another package's tree, and a second reaching the shell's
`cli/run`, asserted in walk order) and `shell-ui-allowed` (a contract import from the UI's list, a relative reach into
`src/`, and a `ui/generated/` file naming a cart, which is not judged).

The moves, as one script (each `git mv` stages its move, and `set -e` stops at the first that fails):

````bash
mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/launchpad \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake
set -e
git mv packages/retroplug/test/boundaries/fixtures/ui-device/packages/launchpad/package.json packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/launchpad/package.json
git mv packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/package.json packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/package.json
````

Run: `bash -c 'git status --porcelain --untracked-files=no | grep -c "^R"'`
Expected: `2`: every move of this task so far is staged (2 of them here), and none failed.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug; sed -n '12032,12035p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts`:

````ts
// The shell's own module, which a ui file may reach.
export const ROLES = [{ kind: "sameboy" }];
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src; sed -n '12047,12048p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts`
Expected: `84a5e72d1e498f8bb622366c08b05bc36d0fbb1d`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts`:

````ts
// The generated registry: the one UI module that names every package, and the rule does not judge it.
import { ALPHA_APP } from "@retroplug/alpha/app";

export const MENUS = [ALPHA_APP];
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated; sed -n '12060,12063p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts`
Expected: `d149994c7104b1ca76eca0961a30917ff0106191`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts`:

````ts
// A UI file the rule admits: a contract module from the UI's own list, and a relative reach into the shell's src (the
// UI is a surface over the stores the control plane owns).
import { action } from "@retroplug/ui/menu";
import { ROLES } from "../src/roles";

export const row = [action("ok", "OK", () => {}), ROLES];
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui; sed -n '12075,12080p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts`
Expected: `4ffbf35344afb9759d6427c5058279771c53514b`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug; sed -n '12092,12095p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts`:

````ts
// A UI file naming a device pack's client rather than reaching it through the generated registry: no contract module,
// so the UI's list refuses it whatever the pack contributes.
import { createLaunchpadClient } from "@retroplug/launchpad/client";

export const client = createLaunchpadClient;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui; sed -n '12107,12111p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts`
Expected: `6fba94c70505981371beb2a1a5c75a1c1f6ee5bc`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts`:

````ts
// A fixture console package: rule 9 counts a relative reach into its tree as reaching a console's code.
export const fake = true;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts; sed -n '12123,12124p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts`
Expected: `9d88de6d8c7a768b1271413adc12619476905933`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts`:

````ts
export const run = (): void => {};
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli; sed -n '12136,12136p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts`
Expected: `ddff636bbb6c36fb107b7a7343b88a9ec1159b39`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug; sed -n '12148,12151p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts`:

````ts
// A UI file reaching a console package's code by PATH rather than by its specifier: the UI reaches ui/ and src/ only.
import { fake } from "../../fake/ts/index";

export const reached = fake;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui; sed -n '12163,12166p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts`
Expected: `453740c9616cbf79d5ffdf2d84cbb4bdc13d5030`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts`:

````ts
// A UI file reaching the CLI by PATH: the UI is a surface over the stores src/ owns, not a tree the dispatcher's modules
// live in, so cli/ is outside what ui/ may reach.
import { run } from "../cli/run";

export const main = run;
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui; sed -n '12178,12182p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts`
Expected: `94a3d1561543d9a4382d5ddc9db3e7a23bd6805f`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json`:

````json
{
  "name": "@retroplug/retroplug",
  "private": true
}
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug; sed -n '12194,12197p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json`
Expected: `d73174c5b2fbc0f95eda7f16e96236c5e099054a`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

Create `packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts`:

````ts
// A UI file naming a contract module its OWN list does not carry: @retroplug/core/path is one of SHELL_IMPORTS' (the
// shell's src/ and cli/ may reach it), and the UI's list is a list of its own, so importing it here is refused.
import { joinPath } from "@retroplug/core/path";

export const joined = joinPath("a", "b");
````

Run: `mkdir -p packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui; sed -n '12209,12213p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts`
Expected: `ca47e390fa9ea852dd17c48b719bd246715d0e48`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/scripts/check-boundaries.mjs b/packages/retroplug/scripts/check-boundaries.mjs
index 49a590c6e..4ad0b7791 100644
--- a/packages/retroplug/scripts/check-boundaries.mjs
+++ b/packages/retroplug/scripts/check-boundaries.mjs
@@ -3,10 +3,10 @@
 // imports what it declares and nothing else, and no package imports the shell.
 //
 // What it judges: every workspace package that has a ts/ tree, which means every file under packages/<name>/ts, sources
-// and tests alike, and - by rule 9 below - the app shell's own sources: packages/retroplug/src may reach only the
-// contract modules the list names, and through them no cart's or console's code. packages/native (the engine's tree) is
-// the other shell: it may import anything and is not judged, and nothing may import either shell. The app's ui/, cli/
-// and test trees are outside src and are not judged (F4b extends the rule to ui/ and cli/).
+// and tests alike, and - by rule 9 below - the app shell's own sources: packages/retroplug's src, cli and ui may each
+// reach only the contract modules its list names, and through them no cart's or console's code. packages/native (the
+// engine's tree) is the other shell: it may import anything and is not judged, and nothing may import either shell.
+// The app's test and test-ui trees are not judged.
 //
 // Each file's imports come from TypeScript's own scanner (ts.preProcessFile), so every form a compiler follows is
 // judged: a value import, an `import type`, an `export type ... from`, a type-position `import("x").T`, a `require`,
@@ -36,12 +36,13 @@
 //      *.test.ts), handing a test file's devDependencies to another package's source; one that reaches outside the
 //      package's ts/ tree, which no check reads; or one naming no file, an entry still saying the package publishes a
 //      module that moved away;
-//   9. an @retroplug/<package> specifier in packages/retroplug/src that is not one of SHELL_IMPORTS below, the shell's
-//      explicit list of contract modules (each entry with the reason it is a contract rather than a cart), or a
-//      RELATIVE import from there that lands outside packages/retroplug/src: a file of the app may reach a cart or a
-//      console only through the generated registries src/generated/ writes, and the app's ui/, cli/ and test trees are
-//      not a source's to import. src/generated/ is not judged (it is the one place that names every package), and an
-//      entry no import uses fails the check.
+//   9. an @retroplug/<package> specifier in the shell's own src, cli or ui that is not one of that root's list
+//      (SHELL_IMPORTS for src and cli, UI_IMPORTS for ui), each an explicit list of contract modules with the reason
+//      each entry is a contract rather than a cart; or a RELATIVE import from there that lands outside what that root
+//      may reach (src: src; cli: cli and src; ui: ui and src): a file of the app may reach a cart or a console only
+//      through the generated registries the generator writes, and the app's other trees are not a source's to import.
+//      Each generated/ tree is not judged (it is the one place that names every package), and an entry no import of its
+//      root uses fails the check.
 //
 // A host module (the bare specifiers packages/retroplug/host-modules.json lists) is judged twice more: the package must
 // declare it in `retroplug.host`, and it is importable only from the files of the surface its entry names (a package's
@@ -113,17 +114,15 @@ const ts = createRequire(import.meta.url)("typescript-5");
  *  a rule today. */
 const EXCEPTIONS = [];
 
-/** Rule 9's list: every @retroplug/<package> specifier `packages/retroplug/src` and `packages/retroplug/cli` may
- *  import, with the reason it is the shell's to reach. Each is a contract module (or a library that is not a cart), so
- *  a second console or a second tracker still needs no edit here; a cart's or a console's own module is reached through
- *  the generated registries (src/generated/packages.ts, protocol.ts and consoles.ts for the control plane,
- *  cli/generated/tools.ts for the CLI), which this rule does not judge. One list serves both roots on purpose: they are
- *  one program's two halves, so a contract module only cli/ imports today stays reachable from src/ without a second
- *  entry, and an entry per root would claim a precision the rule does not need. An entry no import of either root uses
- *  fails the check, so this shrinks with the shell. */
-/** Rule 9's list, exported because the `ui/` ratchet reads it too (test/boundaries/run-ui-cart-imports.mjs): a
- *  specifier the shell may name as a contract is a module a view may name as well. */
-export const SHELL_IMPORTS = [
+/** Rule 9's list for `src/` and `cli/`: every @retroplug/<package> specifier they may import, with the reason it is the
+ *  shell's to reach. Each is a contract module (or a library that is not a cart), so a second console or a second
+ *  tracker still needs no edit here; a cart's or a console's own module is reached through the generated registries
+ *  (src/generated/packages.ts, protocol.ts and consoles.ts for the control plane, cli/generated/tools.ts for the CLI),
+ *  which this rule does not judge. One list serves both roots on purpose: they are one program's two halves, so a
+ *  contract module only cli/ imports today stays reachable from src/ without a second entry, and an entry per root
+ *  would claim a precision the rule does not need. An entry no import of either root uses fails the check, so this
+ *  shrinks with the shell. */
+const SHELL_IMPORTS = [
   // The contract every package implements: the shell is its host, not a participant.
   { specifier: "@retroplug/core/canonicalJson", reason: "the document encoder every protocol driver writes through" },
   { specifier: "@retroplug/core/capability", reason: "the capability jobs' operation table a host's systems group names and a cart types a job by" },
@@ -161,6 +160,24 @@ export const SHELL_IMPORTS = [
   { specifier: "@retroplug/testing", reason: "the TAP harness a session's own tests run through (the SDK serves it to a consumer)" },
 ];
 
+/** Rule 9's list for `ui/`: a list of its own, because the kit and the view contract are the UI's alone (src/ and cli/
+ *  must never reach `@retroplug/ui-react`), and because the control plane needs no menu model or contribution type.
+ *  Each entry is a contract module rather than a cart's or a console's code, which the UI reaches through the
+ *  generated `ui/generated/menus.ts`; an entry no `ui/` file imports fails the check, as an unused SHELL_IMPORTS entry
+ *  does. */
+const UI_IMPORTS = [
+  { specifier: "@retroplug/ui/menu", reason: "the menu model every composer and the Menu draw" },
+  { specifier: "@retroplug/ui/contribution", reason: "the contribution and view types the generated registry and the composers read" },
+  { specifier: "@retroplug/ui-react", reason: "the kit the shell's own components and the three renderers are built from" },
+  { specifier: "@retroplug/core/host", reason: "the host contract the UI host implements" },
+  { specifier: "@retroplug/core/systemRoles", reason: "the role contract the UI reads a system's roles through" },
+  { specifier: "@retroplug/core/midiRouting", reason: "the routing model the MIDI settings menu edits" },
+  { specifier: "@retroplug/launchpad/enums", reason: "the controller target enum the UI host validates a package's target against" },
+  { specifier: "@retroplug/tracker/songCatalog", reason: "the song catalog the shell's Songs menu reads through the integration" },
+  { specifier: "@retroplug/tracker/assetCatalog", reason: "the asset catalog the shell's Assets menus read" },
+  { specifier: "@retroplug/tracker/trackerIntegration", reason: "the integration contract the shell resolves a cart's tracker by" },
+];
+
 function readJson(path) {
   try {
     return JSON.parse(readFileSync(path, "utf8"));
@@ -819,25 +836,52 @@ export function checkBoundaries({ root = REPO } = {}) {
     }
   }
 
-  // Rule 9: the app shell's own sources reach only SHELL_IMPORTS' contract modules, never a cart's or a console's code
-  // (which the generated registries in src/generated name instead, and which this rule does not judge), and a relative
-  // import of theirs stays inside src. The list is the real tree's and applies to a fixture tree too: the allowed
-  // fixture passes because its one contract import is an entry (core's systemRoles), not because a fixture states a
-  // list of its own.
+  // Rule 9: the app shell's own sources reach only their list's contract modules, never a cart's or a console's code
+  // (which the generated registries name instead, and which this rule does not judge), and a relative import of theirs
+  // stays inside what that root may reach. The lists are the real tree's and apply to a fixture tree too: an allowed
+  // fixture passes because its contract imports are entries (core's systemRoles, the UI's menu model), not because a
+  // fixture states a list of its own.
   const shell = packages.get("retroplug");
   const shellAllowed = new Map(SHELL_IMPORTS.map((entry) => [entry.specifier, entry.reason]));
+  const uiAllowed = new Map(UI_IMPORTS.map((entry) => [entry.specifier, entry.reason]));
   const shellUsed = new Set();
+  const uiUsed = new Set();
   if (shell !== undefined) {
     const src = join(shell.dir, "src");
     const cli = join(shell.dir, "cli");
-    // Rule 9's two roots and what each may reach relatively: a src file stays in src, and a cli file stays in cli or
-    // reaches src (the CLI is a surface OVER the shell's control plane). Anything else - ui/, test/, test-native/,
-    // tools-src/, another package - is reached only through a generated registry, or not at all.
+    const ui = join(shell.dir, "ui");
+    // Rule 9's three roots, what each may reach relatively and the list its bare imports are judged against: a src file
+    // stays in src; a cli file stays in cli or reaches src (the CLI is a surface OVER the shell's control plane); and a
+    // ui file stays in ui or reaches src (the UI is a surface over the stores src owns, and reads nothing of the
+    // dispatcher or the SDK). Anything else - test/, test-native/, tools-src/, another package - is reached only
+    // through a generated registry, or not at all.
     const roots = [
-      { dir: src, ignores: [`${PACKAGES}/retroplug/src/generated/`], reaches: [src] },
-      { dir: cli, ignores: [`${PACKAGES}/retroplug/cli/generated/`], reaches: [cli, src] },
+      {
+        dir: src,
+        ignores: [`${PACKAGES}/retroplug/src/generated/`],
+        reaches: [src],
+        allowed: shellAllowed,
+        used: shellUsed,
+        bare: "the shell's src/ and cli/",
+      },
+      {
+        dir: cli,
+        ignores: [`${PACKAGES}/retroplug/cli/generated/`],
+        reaches: [cli, src],
+        allowed: shellAllowed,
+        used: shellUsed,
+        bare: "the shell's src/ and cli/",
+      },
+      {
+        dir: ui,
+        ignores: [`${PACKAGES}/retroplug/ui/generated/`],
+        reaches: [ui, src],
+        allowed: uiAllowed,
+        used: uiUsed,
+        bare: "the shell's ui/",
+      },
     ];
-    for (const { dir, ignores, reaches } of roots) {
+    for (const { dir, ignores, reaches, allowed, used, bare } of roots) {
       for (const file of walk(dir, isTypeScript)) {
         const path = shown(root, file);
         if (ignores.some((prefix) => path.startsWith(prefix))) continue;
@@ -864,29 +908,39 @@ export function checkBoundaries({ root = REPO } = {}) {
             continue;
           }
           if (!specifier.startsWith("@retroplug/")) continue;
-          if (shellAllowed.has(specifier)) {
-            shellUsed.add(specifier);
+          if (allowed.has(specifier)) {
+            used.add(specifier);
             continue;
           }
           refuse(
             "shell-source",
             path,
             specifier,
-            `${path}: imports "${specifier}", which is not one of the contract modules the shell's src/ and cli/ may ` +
+            `${path}: imports "${specifier}", which is not one of the contract modules ${bare} may ` +
               "reach (rule 9): a cart's or a console's code is reached through the generated registries",
           );
         }
       }
     }
   }
-  const shellStale =
+  // An entry no import of its own root uses has outlived what it allowed, so it fails the check as an unused exception
+  // does. A fixture tree is not the real tree, so it judges no list.
+  const staleList = (entries, used, name, where) =>
     root === REPO
-      ? SHELL_IMPORTS.filter((entry) => !shellUsed.has(entry.specifier)).map(
-          (entry) =>
-            `SHELL_IMPORTS names ${entry.specifier} (${entry.reason}), which no file of packages/retroplug/src or ` +
-            "packages/retroplug/cli imports: delete the entry",
-        )
+      ? entries
+          .filter((entry) => !used.has(entry.specifier))
+          .map(
+            (entry) =>
+              `${name} names ${entry.specifier} (${entry.reason}), which no file of ${where} imports: delete the entry`,
+          )
       : [];
+  const shellStale = staleList(
+    SHELL_IMPORTS,
+    shellUsed,
+    "SHELL_IMPORTS",
+    "packages/retroplug/src or packages/retroplug/cli",
+  );
+  const uiStale = staleList(UI_IMPORTS, uiUsed, "UI_IMPORTS", "packages/retroplug/ui");
 
   // Rule 5: a cycle in the declared dependencies, over every package (the shell's declarations can close a loop too).
   for (const cycle of cycles(packages)) {
@@ -943,6 +997,7 @@ export function checkBoundaries({ root = REPO } = {}) {
           "removed it, so delete the entry",
       ),
     ...shellStale,
+    ...uiStale,
   ];
 
   return { refusals: refusals.filter((refusal) => !allowedRefusals.has(refusal)), allowed, warnings, stale };
diff --git a/packages/retroplug/test/boundaries/boundaries-checker.test.ts b/packages/retroplug/test/boundaries/boundaries-checker.test.ts
index 5e85c085d..a3c90cd05 100644
--- a/packages/retroplug/test/boundaries/boundaries-checker.test.ts
+++ b/packages/retroplug/test/boundaries/boundaries-checker.test.ts
@@ -28,6 +28,10 @@
 // - a shell source file importing a cart's module, which rule 9 admits only through the generated registries beside the
 //   contract modules the list names (and the positive tree imports a contract module, with a cart reached from
 //   src/generated/ and not judged);
+// - a ui source importing a cart's module, or a contract module the shell's src may reach but the UI's own list
+//   omits, or reaching outside ui/ and src/ by a relative path, into another package or into the shell's cli/ (and the
+//   positive tree imports the UI's contract module, reaches src, and has a ui/generated file naming a cart, which is
+//   not judged);
 //
 // and three positive trees: one where a package declares a package it imports by name, a declared third-party subpath, a
 // devDependency from a test file (a type package's globals included), its own interface by a relative path, a
@@ -362,6 +366,53 @@ test("rule 9's relative half is per root: a src file reaches src, a cli file cli
   ]);
 });
 
+test("a ui source importing a cart's module is refused against the UI's own list (rule 9)", async () => {
+  // The UI reaches a cart's code through the generated registry it is handed, and a device pack's client is not a
+  // contract module: the refusal is UI_IMPORTS', which names no package that contributes code.
+  const report = await drive("shell-ui-forbidden");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    'packages/retroplug/ui/probe.ts: imports "@retroplug/launchpad/client", which is not one of the contract modules ' +
+      "the shell's ui/ may reach (rule 9): a cart's or a console's code is reached through the generated registries",
+  ]);
+});
+
+test("a contract module the shell's src may reach but the UI's list omits is refused there too (rule 9)", async () => {
+  // One list would let the UI name whatever the control plane names. It has its own, so a module only src/ and cli/
+  // need is refused here even though SHELL_IMPORTS carries it.
+  const report = await drive("shell-ui-unlisted");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    'packages/retroplug/ui/evil.ts: imports "@retroplug/core/path", which is not one of the contract modules the ' +
+      "shell's ui/ may reach (rule 9): a cart's or a console's code is reached through the generated registries",
+  ]);
+});
+
+test("a ui source's relative import stays in ui/ or reaches src/, never cli/ (rule 9)", async () => {
+  // One file per direction: ui into cli/ and ui into another package. The UI is a surface over the stores src/ owns, so
+  // neither the dispatcher's tree nor a cart's is a tree it may import, and the two refusals come in walk order.
+  const report = await drive("shell-ui-relative");
+  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
+  expect(report.status).toBe(1);
+  expect(report.refused).toEqual([
+    'packages/retroplug/ui/evil.ts: imports "../../fake/ts/index", which resolves outside packages/retroplug/ui and ' +
+      "packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the " +
+      "generator writes, and the shell's other trees are not a source's to import",
+    'packages/retroplug/ui/sneaky.ts: imports "../cli/run", which resolves outside packages/retroplug/ui and ' +
+      "packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the " +
+      "generator writes, and the shell's other trees are not a source's to import",
+  ]);
+});
+
+test("a ui source may import its list's contract and reach src, and ui/generated is not judged (rule 9)", async () => {
+  const report = await drive("shell-ui-allowed");
+  expect(report.status).toBe(0);
+  expect(report.refused).toEqual([]);
+  expect(report.stale).toEqual([]);
+});
+
 test("a host module the package declares is admitted, from the files its surface owns", async () => {
   // The positive tree imports it from ts/cli/ and from examples/, which are the cli surface's own files.
   const report = await drive("host-module-declared");
````

Run: `sed -n '12223,12503p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/scripts/check-boundaries.mjs
packages/retroplug/test/boundaries/boundaries-checker.test.ts
EOF
````

Expected:

````text
4ad0b7791785bda59f7cc446b3dc8345cfb6fba4
a3c90cd05df17ce18ae39fc69c072910dd08e772
````

- [ ] **Step 2: The ratchet retires**

The F4b ratchet and its runner go, with the three old fixture files whose roles the new trees took over. What the
ratchet's derived package list did, the allow-based rule does: nothing but the list is admitted, so a device pack with
no app module is guarded because nothing else may be named, not because a generator walked it.

Delete `packages/retroplug/test/boundaries/ui-cart-imports.test.ts`, `packages/retroplug/test/boundaries/run-ui-cart-imports.mjs`, `packages/retroplug/test/boundaries/fixtures/ui-device/packages/retroplug/ui/probe.ts`, `packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/ts/index.ts`, `packages/retroplug/test/boundaries/fixtures/ui-relative/packages/retroplug/ui/evil.ts` with `git rm`, which stages the deletion:

````bash
git rm -q -- packages/retroplug/test/boundaries/ui-cart-imports.test.ts \
  packages/retroplug/test/boundaries/run-ui-cart-imports.mjs \
  packages/retroplug/test/boundaries/fixtures/ui-device/packages/retroplug/ui/probe.ts \
  packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/ts/index.ts \
  packages/retroplug/test/boundaries/fixtures/ui-relative/packages/retroplug/ui/evil.ts
````

- [ ] **Step 3: The scanner's third directory, and the fixture that pins it**

`no-cart-literals.test.ts` scans `src,cli,ui` through one `SHELL_DIRS` constant passed by the real case AND by the new
fixture case, so a directory leaving the list fails the fixture too; a new fixture,
`test/source-literals/fixtures/literals-ui/.../useRisaRuntime.ts`, holds the pre-move shape (a role test naming the
tracker id, a path ending in a ROM extension) and is valid TypeScript, because the shell's program includes the
fixtures directory. `run-scan.mjs`'s usage line and header name the three directories and its walk now skips a
directory that does not exist, as the boundary check's walk does: the fixture holds only `ui/`, and a caller that
names a tree a fixture does not have should get the files it has.

Create `packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts`:

````ts
// The shell's UI before the views moved, as the check recorded it: a tracker id in a role test, and a ROM extension in
// a path. The walk reads ui/ as it reads src/ and cli/, so both are hits there; the real tree's ui/ names neither.
import { hasRole, type RoleInstance } from "@retroplug/core/systemRoles";

export function isRisa(roles: readonly RoleInstance[]): boolean {
  return hasRole(roles, "risa");
}

export const RISA_ROM = "/roms/risa.nes";
````

Run: `mkdir -p packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid; sed -n '12554,12562p' docs/modular-graph-phase-f4c-plan.md > packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts`
Expected: exit 0: the file is the block above, byte for byte.

Run: `git hash-object packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts`
Expected: `04a625422fc215a17a01c5d717f233fce52bf2a8`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

````diff
diff --git a/packages/retroplug/test/source-literals/no-cart-literals.test.ts b/packages/retroplug/test/source-literals/no-cart-literals.test.ts
index 1e74204ac..31e77783c 100644
--- a/packages/retroplug/test/source-literals/no-cart-literals.test.ts
+++ b/packages/retroplug/test/source-literals/no-cart-literals.test.ts
@@ -15,6 +15,8 @@
 // boundary check says of itself.
 //
 // The scanner is Node's, so the test spawns test/source-literals/run-scan.mjs, as the boundary test spawns its runner.
+// It walks src/, cli/ and ui/ (each skipping its generated/ tree), so the shell's three trees are held to the same
+// rule; the guard sets are the registries', and a fixture pins each directory's walk.
 import { test, expect } from "@retroplug/testing";
 import { CONSOLES } from "../../src/generated/consoles";
 import { EMBEDS, KIT_FORMATS, TRACKER_INTEGRATIONS } from "../../src/generated/packages";
@@ -39,6 +41,10 @@ const CART_IDS = [
   ]),
 ].sort();
 
+/** The shell's own trees, which rule 9 judges and the scan below walks. One constant, because both cases have to name
+ *  all three: the fixture case scans it too, so a directory dropping out of the real case fails there as well. */
+const SHELL_DIRS = "src,cli,ui";
+
 /** The one deliberate copy of a package's knowledge in the shell, held to being used: the v2 to v3 step's enum
  *  tables are keyed by role kind, and four of those kinds are a console's or a cart's own. The step may not read the
  *  registry - it freezes the v2 writer's lists so a package's rename cannot move what an old file means (decision
@@ -110,12 +116,12 @@ async function scan(root?: string, dirs = "src"): Promise<{ file: string; value:
   return (JSON.parse(out) as { literals: { file: string; value: string }[] }).literals;
 }
 
-test("the shell's sources and cli/ spell no platform, ROM extension or cart id", async () => {
+test("the shell's sources, cli/ and ui/ spell no platform, ROM extension or cart id", async () => {
   console.log(
     `# the registry's ids: ${PLATFORM_IDS.join(", ")}; extensions: ${ROM_EXTENSIONS.join(", ")}; ` +
       `cores: ${CORE_IDS.join(", ")}; node types: ${NODE_TYPES.join(", ")}; carts: ${CART_IDS.join(", ")}`,
   );
-  const literals = await scan(undefined, "src,cli");
+  const literals = await scan(undefined, SHELL_DIRS);
   console.log(`# platform/extension/core/node-type/cart literals: ${JSON.stringify(literals)}`);
   const remaining = literals.filter(
     (hit) => !FROZEN_MIGRATION_KEYS.some((key) => key.file === hit.file && key.value === hit.value),
@@ -154,6 +160,17 @@ test("the walk reads a table's keys, a pattern and a template tail, not a commen
   ]);
 });
 
+test("the third directory is walked: the pre-move ui/ hit is found there", async () => {
+  // Before the views moved, ui/screens/grid/useRisaRuntime.ts was the one ui/ file spelling a cart's id. It scans the
+  // list the real case scans, so a directory leaving SHELL_DIRS fails this case too, and a return of that shape fails
+  // the real tree's.
+  const literals = await scan("test/source-literals/fixtures/literals-ui", SHELL_DIRS);
+  expect(literals).toEqual([
+    { file: "packages/retroplug/ui/screens/grid/useRisaRuntime.ts", value: "risa" },
+    { file: "packages/retroplug/ui/screens/grid/useRisaRuntime.ts", value: "/roms/risa.nes" },
+  ]);
+});
+
 test("a whitespace string ending in an extension is prose, and a path is not", async () => {
   // The CLI's help texts name their inputs in a sentence ("... a ROM (.nes/.gb), or a .sav"); a value a program
   // compares is a path with no whitespace in it. The fixture holds one of each, and the prose is not a hit.
diff --git a/packages/retroplug/test/source-literals/run-scan.mjs b/packages/retroplug/test/source-literals/run-scan.mjs
index 859c11e1e..f3817ade5 100644
--- a/packages/retroplug/test/source-literals/run-scan.mjs
+++ b/packages/retroplug/test/source-literals/run-scan.mjs
@@ -10,7 +10,7 @@
 // `${...}` that closes a span.
 //
 //   node test/source-literals/run-scan.mjs --platforms <id,...> --extensions <.ext,...> \
-//     --cores <id,...> --node-types <type,...> --carts <id,...> [--dirs src,cli] [--root <dir>]
+//     --cores <id,...> --node-types <type,...> --carts <id,...> [--dirs src,cli,ui] [--root <dir>]
 //
 // The guarded sets are the console registry's, not this file's: the test imports the generated CONSOLES list and passes
 // its platform ids, extensions, cores and node types, so a console a product registers later is guarded without an edit
@@ -20,8 +20,8 @@
 // class that happens to hold one (a filename sanitiser) out, and the bare-letter test keeps a permissive pattern such as
 // /^[\x20-\x7e]*$/ out. What it cannot see is what a program computes rather than spells: a platform assembled at
 // runtime (["g", "b"].join("")), a table keyed by a computed name, and a dotless pattern such as /(gb|gbc)$/.
-// Prints one JSON line: { literals: [{ file, value }] }, src/generated/ not scanned.
-import { readdirSync, readFileSync } from "node:fs";
+// Prints one JSON line: { literals: [{ file, value }] }, each directory's generated/ tree not scanned.
+import { existsSync, readdirSync, readFileSync } from "node:fs";
 import { createRequire } from "node:module";
 import { dirname, join, relative, resolve } from "node:path";
 import { fileURLToPath } from "node:url";
@@ -104,8 +104,11 @@ const isPatternHit = (source, flags) => {
 };
 
 /** Every .ts/.tsx file under `dir`, in a stable order, without the generated registries or the SDK's declaration file
- *  (a consumer's view of the SDK, written for an editor, not the shell's own code). */
+ *  (a consumer's view of the SDK, written for an editor, not the shell's own code). A directory the tree does not have
+ *  is skipped, as the boundary check's walk does: a fixture may be given the list the real tree is given while holding
+ *  only one of its trees. */
 function walk(dir, out = []) {
+  if (!existsSync(dir)) return out;
   for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
     const path = join(dir, entry.name);
     if (entry.isDirectory()) {
````

Run: `sed -n '12572,12665p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
packages/retroplug/test/source-literals/no-cart-literals.test.ts
packages/retroplug/test/source-literals/run-scan.mjs
EOF
````

Expected:

````text
31e77783c617d62d8156d5695a0ab446b4a9ebb6
f3817ade5204bf9472754097e982d007da68dad0
````

- [ ] **Step 4: The docs**

`AGENTS.md`: the ratchet paragraph becomes rule 9's three roots and their two lists (with the `ui` root's relative
half and the unused-entry rule), the scanner sentence names all three directories, the package list gains `ui-react`
and `ui-fixture`, the manifest paragraph's `defineUi({ rows, menus, trackers, services })` gains `views` and
`actions`, and the host-module sentence gains rule 3's `.tsx` admission and why automatic JSX makes a `.tsx` a use of
`react/jsx-runtime`. `spec/06-build-test.md`: "Adding a package" gains item 11 (a view: pixels and readout in plain
TypeScript, React through `reactView` with `@retroplug/ui-react` in `dependencies` - never `retroplug.host`, which
takes only host modules - and the React host modules declared, the tsconfig extending the generated one, where each
kind of view is tested, and the registry's refusals) and item 12 (an action: the `<package>.<name>` id, the labels and
defaults, and the refusals); its item 5, its extend rule and its rule 9 paragraph name the three roots, both lists and
the five host modules over two surfaces, and state rule 3's `.tsx` placement. `spec/07-remaining-work.md`: the HD link
text and the stale readers' list, the F4b proof's ratchet sentence in the past tense, and a new **F4c built** entry
recording what shipped and what it leaves (design section 11). `docs/package-split.md`'s status and F4 paragraph;
`spec/00-overview.md`'s `ui` row plus a `ui-react` row (with `ui-fixture`); `spec/03-ts-layer.md`'s kit primitives
split from the shell's own (`ui/lvgl/`), the moved hooks, `ui/views/` and `appActions`, the tile slots;
`docs/lsdj.md`'s read-path prose; `spec/13-subsystems.md`'s HD code line; `packages/ui`'s description. The two
byte-frozen `test-ui` view headers no longer describe deleted hooks: **comments only**, and every id, label, key and
text is unchanged. One stale code comment in `ui-react`'s `DebugReadout` and one in `test-native/lsdjPlaybackProbe.ts`
are refreshed.

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 5e97b489f..4b4d38189 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -12,8 +12,9 @@ Start with these — all short:
   `packages/risa/` and `packages/smsggdj/`, the tracker layer they share in
   `packages/tracker/`, BlipToaster's app half and its MicroQuickJS MIDI policy in `packages/bliptoaster/`, the control
   surface in `packages/launchpad/` with the LSDj MI.MAP app that drives it in `packages/lsdj-launchpad/`, the plain-TS
-  UI contract (the menu model and the contribution types) in `packages/ui/`, the test harness in `packages/testing/`, the
-  box's device nodes in `packages/device-io/`, the device check's test nodes in
+  UI contract (the menu model, the contribution and view types) in `packages/ui/` with the React kit in
+  `packages/ui-react/` (and its component-view fixture, listed by no product, in `packages/ui-fixture/`), the test
+  harness in `packages/testing/`, the box's device nodes in `packages/device-io/`, the device check's test nodes in
   `packages/devtest/` (listed by no product), and everything about the Everdrive N8 Pro in `packages/n8/`. The older
   **legacy** build has been removed; [spec/07-remaining-work.md](spec/07-remaining-work.md) tracks the residual
   cleanup + feature gaps.
@@ -204,8 +205,9 @@ The rules below are the parts that don't fit those.
   and must carry no schema) default-exporting a package's console descriptors and the physical carts one of them can
   drive; `retroplug.protocol` names the module that default-exports a package's `DefinedNode`s beside the `nodes` (type,
   span label: 16 up with no gaps, unique) and the role kinds it places; `retroplug.ui` names a module default-exporting
-  the package's `defineUi({ rows, menus, trackers, services })` (the menu contributions and the UI services, imported
-  only by the UI), `retroplug.cli` one default-exporting `defineCliTools` (the package's subcommands), and
+  the package's `defineUi({ rows, menus, trackers, services, views, actions })` (the menu contributions, the UI
+  services, the views the shell renders and the app actions the key maps dispatch, imported only by the UI),
+  `retroplug.cli` one default-exporting `defineCliTools` (the package's subcommands), and
   `retroplug.examples` its example sessions under `examples/`; `retroplug.host` names each host module the package
   imports by bare specifier (`retroplug-cli` today), from the shell-owned list in
   `packages/retroplug/host-modules.json`. A tracker integration carries the cart hooks the
@@ -227,28 +229,27 @@ The rules below are the parts that don't fit those.
   provider's place in it is behaviour: the providers run in that order, and the first to match a ROM fixes a system's
   role-list order. Roles, kit formats and tracker integrations are looked up by kind, id or an exclusive marker role,
   so for those a duplicate - refused by the generated module - is the hazard, not the order. Two checks hold the
-  boundary, with one deliberate allowlist between them (rule 9's `SHELL_IMPORTS`; `EXCEPTIONS` is empty, and the native
-  check has none): `pnpm check:boundaries`
+  boundary, with no exception between them (`EXCEPTIONS` is empty, and the native check has none):
+  `pnpm check:boundaries`
   ([check-boundaries.mjs](packages/retroplug/scripts/check-boundaries.mjs): a package imports only what its
   `dependencies` declare, nothing imports a shell, a cycle is refused, and - rule 9 - the app shell's own
-  `packages/retroplug/src` and `packages/retroplug/cli` may import only the contract modules its explicit list names,
-  each with its reason, so a cart's or a console's code is reached only through the generated registries (a src file
-  reaches only src, a cli file cli or src, and either root's `generated/` is not judged; an entry no import uses fails
-  it), and [cmake/packages.cmake](cmake/packages.cmake)'s native twin (a package's targets link and include only
-  declared packages' trees). A test also walks both roots with TypeScript's syntax tree and refuses a platform id, a ROM
+  `packages/retroplug/src`, `cli` and `ui` may each import only the contract modules its explicit list names
+  (`SHELL_IMPORTS` for the first two, `UI_IMPORTS` for the UI, which is where the kit and the view contract live so that
+  no src or cli file may name `@retroplug/ui-react`), each entry with its reason, so a cart's or a console's code is
+  reached only through the generated registries (a src file reaches only src, a cli file cli or src, a ui file ui or
+  src, and each root's `generated/` is not judged; an entry no import of its root uses fails it), and
+  [cmake/packages.cmake](cmake/packages.cmake)'s native twin (a package's targets link and include only declared
+  packages' trees). A test also walks all three roots with TypeScript's syntax tree and refuses a platform id, a ROM
   extension, a core id, a node type or a cart id there (`test/source-literals`, its ids read from the generated console
   and package registries, so a console's or a cart's knowledge is its package's alone), and a bundle check holds the
   control plane, the render worker, the script nodes, the SDK, the dispatcher and the planner to carrying no UI (no
   `ui` package module, no package's `ts/ui/`, no `ui/generated`) and the UI to carrying no CLI (no package's `ts/cli/`,
-  no `core/cliTool.ts`, no `cli/generated`) (`test/script/bundle-ui-split.test.ts`). Until F4c moves the five files that
-  import a cart, `test/boundaries/ui-cart-imports.test.ts` is a ratchet over `ui/`: the files on its list may import
-  one, any other file fails, and a listed file that stops fails too (`pnpm test boundaries`). The packages it guards
-  are derived from the manifests - any package contributing code or baked bytes (an `app`, `ui`, `cli`, `examples`,
-  `protocol`, `consoles`, `embed` or `script` member), so a device pack with no app module is guarded too - and a
-  specifier on rule 9's list is a contract module a view may name. One `Host<G>` contract in
+  no `core/cliTool.ts`, no `cli/generated`) (`test/script/bundle-ui-split.test.ts`). One `Host<G>` contract in
   `packages/core/ts/host.ts` is what a hook or a CLI tool asks a surface for: named groups, a surface's builder
   offering only its own, and a host module (`host-modules.json`'s `surface`) is importable only from that surface's
-  own files - a package's `examples/` or `ts/cli/` for `cli`, `ts/ui/` for `ui`. Details:
+  own files - a package's `examples/` or `ts/cli/` for `cli`, `ts/ui/` for `ui` - and a package's `.tsx` lives only
+  under its `ts/ui/` tree (rule 3; automatic JSX makes the `.tsx` a use of `react/jsx-runtime`, so a package with one
+  declares that host module). Details:
   [spec/06-build-test.md](spec/06-build-test.md) and
   [docs/modular-graph-phase-f1-design.md](docs/modular-graph-phase-f1-design.md).
 - **A device pack is TypeScript on MicroQuickJS, and it runs the same bytes on the desktop and the box.** A package
diff --git a/docs/lsdj.md b/docs/lsdj.md
index bb449df76..e98bdaf2f 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -181,9 +181,9 @@ const state = reader.read(wramBytes);                  // -> LsdjState (supporte
   render regime). Running plugin: `backend.readRam(id)` — the SnapshotRegistry per-block WRAM
   triple on the emulator facet, safe while audio plays (the CLI-only debug `readMemory` is not
   on the plugin channel). The live overlay
-  ([LsdjOverlay.tsx](../packages/lsdj/ts/ui/overlayView.ts) +
-  [useLsdjRuntime.ts](../packages/lsdj/ts/ui/overlayView.ts)) reads `readRam`
-  each frame and renders the state over the emulator tile.
+  ([overlayView.ts](../packages/lsdj/ts/ui/overlayView.ts), the "lsdj-overlay" view the LSDj package
+  registers) reads `readRam` each frame and draws its three-line readout over the emulator tile; the
+  HD player ([hdView.ts](../packages/lsdj/ts/ui/hdView.ts)) is the other live reader.
 
 Tests: [test/lsdj/runtime.test.ts](../packages/lsdj/ts/test/runtime.test.ts) +
 [test/lsdj/detect.test.ts](../packages/lsdj/ts/test/detect.test.ts) (pure-TS — the reader
diff --git a/docs/package-split.md b/docs/package-split.md
index d00f87dd1..b2ddc8a10 100644
--- a/docs/package-split.md
+++ b/docs/package-split.md
@@ -1,17 +1,19 @@
 # The package split
 
-**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** roadmap; F1, F3, F2, F4a and F4b built
+**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** roadmap; F1, F3, F2, F4a, F4b and F4c built
 ([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md), whose section 11 records it and its follow-up
 pass; [modular-graph-phase-f3-design.md](modular-graph-phase-f3-design.md), whose section 12 records it;
 [modular-graph-phase-f2-design.md](modular-graph-phase-f2-design.md), whose section 12 records its execution and its
-follow-up pass; [modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md) and
-[modular-graph-phase-f4b-design.md](modular-graph-phase-f4b-design.md), the two halves of F4; what each leaves is
-[07-remaining-work.md](../spec/07-remaining-work.md)'s F1, F3, F2, F4a and F4b bullets). F4 split into **F4a**, the
-non-UI registrations and the shell's boundary (built), and **F4b**, the plain-TypeScript surfaces (built: the `ui`
+follow-up pass; [modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md),
+[modular-graph-phase-f4b-design.md](modular-graph-phase-f4b-design.md) and
+[modular-graph-phase-f4c-design.md](modular-graph-phase-f4c-design.md), the three parts of F4; what each leaves is
+[07-remaining-work.md](../spec/07-remaining-work.md)'s F1, F3, F2, F4a, F4b and F4c bullets). F4 split into **F4a**, the
+non-UI registrations and the shell's boundary (built), **F4b**, the plain-TypeScript surfaces (built: the `ui`
 package and the menu contributions, the cart CLI tools and the example sessions, the `Host` contract, rule 9 and the
-scanner over `cli/`, and the bundle split). **F4c** (React in packages: a `@retroplug/ui/kit`, the views and the
-overlays, rule 9 and the scanner over `ui/`) is next and not yet designed. Per-product bundles stay owed until a
-product's package list differs from the desktop's.
+scanner over `cli/`, and the bundle split), and **F4c**, the React half (built: the `ui-react` kit, the view contract
+and the action shape, the shell's renderers and slots, the LSDj HD player and the two debug overlays in their
+packages, and rule 9 and the scanner over `ui/`). Per-product bundles stay owed until a product's package list differs
+from the desktop's.
 
 [modular-graph-design.md](modular-graph-design.md) section 11 ("Package split") listed what carving RetroPlug into
 workspace packages would take. E1 started it (`node-sdk`, `graph-runtime`, `core`, `dsp`), E2 carved `n8`, and E4 made
@@ -64,11 +66,21 @@ not the driver: they become cheap afterwards, and nothing here is judged by them
     callbacks, the device menus and the N8/Launchpad watchers move into their packs; the seven cart CLI tools become
     `retroplug.cli` contributions run against the CLI host, the example sessions move into their packages and import
     the SDK by its bare specifier, and the host-module list generates the typecheck paths. The proof: rule 9 and the
-    literal scanner over `cli/`, the `ui/` cart-import ratchet, and a bundle check that the control plane, the render
-    worker, the script nodes, the SDK, the dispatcher and the planner carry no UI while the UI carries no CLI. Nothing
-    re-recorded; the one behaviour edge an existing output shows is the CLI's command index order, and the second
-    (design section 6's E13) is a hand-edited project whose roles omit the system's core role losing the core's rows.
-    What it leaves is F4c and the per-product bundles, below. F4c is next.
+    literal scanner over `cli/`, the `ui/` cart-import ratchet (F4c retired it for rule 9's `ui/` root), and a bundle
+    check that the control plane, the render worker, the script nodes, the SDK, the dispatcher and the planner carry no
+    UI while the UI carries no CLI. Nothing re-recorded; the one behaviour edge an existing output shows is the CLI's
+    command index order, and the second (design section 6's E13) is a hand-edited project whose roles omit the system's
+    core role losing the core's rows. What it left is F4c, below, and the per-product bundles.
+  - **F4c: React in packages** ([design](modular-graph-phase-f4c-design.md), **built** on `feature/modularize`). The
+    React kit moves into `packages/ui-react` (`Box`, `lv`, the frame-poll hooks, `DebugReadout`, `tagTestId`,
+    `reactView`), so `@retroplug/ui` stays plain TypeScript and a package that wants no React never meets it; the
+    contribution gains declarative views (a full-window screen, or a layer over a tile, drawn as pixels or a readout by
+    the shell, or mounted as a component the package built with `reactView`) and actions (a key binding the profile and
+    the bindings menus carry); the shell renders views in the grid's place and in per-tile slots, owns their sessions,
+    and routes the actions beside its own. The LSDj HD player, the LSDj and risa debug overlays and their two
+    `debug-overlay` actions are their packages', so the shell's `ui/` names no cart: rule 9's third root judges `ui/`
+    against its own list, the literal scanner covers it, and the ratchet is gone. The one re-record is the design's edge
+    E1 (each bindings menu gains a row per contributed action). What it leaves is design section 11's.
 
 ## 3. The order
 
diff --git a/packages/retroplug/test-native/lsdjPlaybackProbe.ts b/packages/retroplug/test-native/lsdjPlaybackProbe.ts
index 953600a87..3d4a5d5b5 100644
--- a/packages/retroplug/test-native/lsdjPlaybackProbe.ts
+++ b/packages/retroplug/test-native/lsdjPlaybackProbe.ts
@@ -15,7 +15,7 @@
 //      experiment that is about our code.
 //
 //   2. It samples position per render slice via the per-block WRAM seam (backend.readRam →
-//      LsdjReader, the same path LsdjOverlay uses) and returns a TIMELINE, so a caller can watch how
+//      LsdjReader, the same path the overlay view uses) and returns a TIMELINE, so a caller can watch how
 //      state evolves rather than probing a single settled point.
 //
 // Every entry point returns null when the aboy ROM is absent so callers skip cleanly, matching the
diff --git a/packages/retroplug/test-ui/lsdj-overlay.test.ts b/packages/retroplug/test-ui/lsdj-overlay.test.ts
index ab9b28a7b..3a36b75dc 100644
--- a/packages/retroplug/test-ui/lsdj-overlay.test.ts
+++ b/packages/retroplug/test-ui/lsdj-overlay.test.ts
@@ -1,10 +1,11 @@
 // The LSDj runtime debug overlay, end to end on the headless display. The overlay is a developer aid,
-// HIDDEN by default and toggled with the backtick key (App → toggleLsdjDebug). We load a real LSDj ROM
-// (auto-attaches the lsdj-sync role, so useLsdjRuntime engages), then assert:
+// HIDDEN by default and toggled with the backtick key (the lsdj package's `lsdj.debug-overlay` action).
+// We load a real LSDj ROM (auto-attaches the lsdj-sync role, so the package's "lsdj-overlay" view applies
+// and opens its sampler), then assert:
 //   - it is absent by default (not shown to players);
-//   - backtick reveals it (the WRAM reader resolves a supported layout → the readout renders);
+//   - backtick reveals it (the WRAM reader resolves a supported layout -> the readout renders);
 //   - backtick again hides it.
-// SKIPs when no LSDj ROM was staged (a large external asset — see run-ui-tests.mjs).
+// SKIPs when no LSDj ROM was staged (a large external asset, see run-ui-tests.mjs).
 
 import { test, expect, skip, ui } from "ui-harness";
 
@@ -29,7 +30,7 @@ test("the LSDj overlay is hidden by default and toggles on/off with backtick", (
 
   // Backtick reveals it: the reader identifies v9.4.2 + resolves a layout, so the readout renders.
   ui.tapKey(BACKTICK);
-  ui.advance(300); // a frame tick drives useLsdjRuntime to read + decode WRAM
+  ui.advance(300); // a frame tick drives the view's sampler to read + decode WRAM
   ui.pump(10);
   expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
   expect(ui.findByTextContaining("v9.4.2") != null).toBeTruthy(); // the decoded readout, not just the strip
diff --git a/packages/retroplug/test-ui/risa-overlay.test.ts b/packages/retroplug/test-ui/risa-overlay.test.ts
index 289b95778..5028c4992 100644
--- a/packages/retroplug/test-ui/risa-overlay.test.ts
+++ b/packages/retroplug/test-ui/risa-overlay.test.ts
@@ -1,8 +1,9 @@
-// The risa runtime debug overlay, end to end on the headless display — the risa twin of lsdj-overlay.
-// HIDDEN by default, toggled with backtick (App → toggleLsdjDebug, shared). We load a real risa ROM
-// (auto-attaches the `risa` role, so useRisaRuntime engages + identifies the version from the PRG), then
-// assert: absent by default; backtick reveals the readout (even idle risa decodes a `supported` state);
-// backtick again hides it. SKIPs when no risa ROM was staged (see run-ui-tests.mjs).
+// The risa runtime debug overlay, end to end on the headless display, the risa twin of lsdj-overlay.
+// HIDDEN by default, toggled with backtick (the risa package's `risa.debug-overlay` action). We load a real
+// risa ROM (auto-attaches the `risa` role, so the package's "risa-overlay" view applies, identifies the
+// version from the PRG and resolves its layout), then assert: absent by default; backtick reveals the
+// readout (even idle risa decodes a `supported` state); backtick again hides it. SKIPs when no risa ROM was
+// staged (see run-ui-tests.mjs).
 import { test, expect, skip, ui } from "ui-harness";
 
 const RISA = () => ui.romDir() + "/risa.nes";
@@ -23,10 +24,10 @@ test("the risa overlay is hidden by default and toggles on/off with backtick", (
   // Hidden by default.
   expect(ui.findByTestId("risa-overlay")).toBe(null);
 
-  // Backtick reveals it: useRisaRuntime identifies v2.2.1 from the PRG + resolves the layout, so even the
+  // Backtick reveals it: the view identifies v2.2.1 from the PRG + resolves the layout, so even the
   // idle (stopped) state renders as a supported readout.
   ui.tapKey(BACKTICK);
-  ui.advance(300); // a frame tick drives useRisaRuntime to read + decode RAM
+  ui.advance(300); // a frame tick drives the view's sampler to read + decode RAM
   ui.pump(10);
   expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();
   expect(ui.findByTextContaining("v2.2.1") != null).toBeTruthy(); // the decoded readout, not just the strip
diff --git a/packages/ui-react/ts/ui/DebugReadout.tsx b/packages/ui-react/ts/ui/DebugReadout.tsx
index 77c2ea9ba..336cf9c25 100644
--- a/packages/ui-react/ts/ui/DebugReadout.tsx
+++ b/packages/ui-react/ts/ui/DebugReadout.tsx
@@ -1,5 +1,5 @@
-// The shell both tracker debug overlays draw into (LsdjOverlay, RisaOverlay): a translucent strip along the
-// tile's bottom edge carrying a few lines of live readout. Each overlay only decides WHAT the lines say.
+// The strip both tracker debug overlays draw into (the LSDj and risa packages' readout views): a translucent
+// strip along the tile's bottom edge carrying a few lines of live readout. Each view only decides WHAT the lines say.
 import { EAlignType, Text } from "lvgljs-ui";
 
 import { Box } from "./Box";
diff --git a/packages/ui/package.json b/packages/ui/package.json
index a33a2de37..40885c0b1 100644
--- a/packages/ui/package.json
+++ b/packages/ui/package.json
@@ -6,7 +6,7 @@
     "typecheck": "tsc -p tsconfig.json"
   },
   "type": "module",
-  "description": "The UI's contract, plain TypeScript: the menu data model and its item builders, the contribution types a package's UI module declares (declarative role-config rows, menu builders and their placements, tracker specs and UI services), and the row-reading helpers the shell renders a row with. The React kit follows in F4c.",
+  "description": "The UI's contract, plain TypeScript: the menu data model and its item builders, the contribution types a package's UI module declares (declarative role-config rows, menu builders and their placements, tracker specs, UI services, the view and action shapes), the row-reading helpers the shell renders a row with, and the readout's byte format (`@retroplug/ui/format`). It imports no React: a React view's body comes from `@retroplug/ui-react`, and a plain-TypeScript view implements this package's view type directly.",
   "exports": {
     "./contribution": "./ts/contribution.ts",
     "./format": "./ts/format.ts",
diff --git a/spec/00-overview.md b/spec/00-overview.md
index 32c0d58b0..ba3aa5da8 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -90,8 +90,9 @@ system routed to one pair by its audio routing.
 | Package | Contents | Lifetime |
 |---|---|---|
 | [`packages/native/`](../packages/native) | The C++ **shell**: the RPC services + their facets, `Engine`, the command/snapshot/release seams, the script host the TS nodes (the MIDI routing, the controller session and the protocol nodes) run in, the offline job pool, the DPF plugin, both test hosts, and the format-neutral helpers only they use (`SampleCache`, `FileSync`, lodepng). It names no console: a system node carries its own package's build function (`rp.host.systems/2`). | The one tree every product links. |
-| [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane, the CLI and the React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the protocol nodes that run them, the UI, and the CLI's dispatcher, sessions and SDK. Its own trees (`src/` and `cli/`) name no cart and no console (the boundary check's rule 9 over both): a cart is reached through its package's contribution (its tracker integration's hooks and render hooks, its role hooks, its kit format, its menu module and its CLI tool through the generated tables), a console through its descriptor, and an embedded ROM through the `embeds` declaration `SystemsStore.loadEmbedded(id)` looks up. | The sole control-plane + UI + CLI tree. |
-| [`packages/ui/`](../packages/ui) | The plain-TypeScript UI contract: `@retroplug/ui/menu` (the menu model and its item builders) and `@retroplug/ui/contribution` (the row, builder, tracker-UI and service shapes a package's `retroplug.ui` module declares, gated by `defineUi`). The React kit and the view registry are F4c's. | The shell and every package with menu rows. |
+| [`packages/retroplug/`](../packages/retroplug) | The TypeScript control plane, the CLI and the React/LVGL UI: the `Backend` interface, the stores (Systems/Project/Recent/UserConfig/Bindings), roles + the protocol nodes that run them, the UI, and the CLI's dispatcher, sessions and SDK. Its own trees (`src/`, `cli/` and `ui/`) name no cart and no console (the boundary check's rule 9 over all three): a cart is reached through its package's contribution (its tracker integration's hooks and render hooks, its role hooks, its kit format, its menu module and its CLI tool through the generated tables), a console through its descriptor, and an embedded ROM through the `embeds` declaration `SystemsStore.loadEmbedded(id)` looks up. | The sole control-plane + UI + CLI tree. |
+| [`packages/ui/`](../packages/ui) | The plain-TypeScript UI contract: `@retroplug/ui/menu` (the menu model and its item builders), `@retroplug/ui/contribution` (the row, builder, tracker-UI, service, view and action shapes a package's `retroplug.ui` module declares, gated by `defineUi`) and `@retroplug/ui/format` (the readout's byte format). The React kit is a package of its own. | The shell and every package with menu rows, views or actions. |
+| [`packages/ui-react/`](../packages/ui-react) | The React half of the UI: the typed lvgljs-ui handles (`lv`, `Box`, `PixelCanvas`), the frame-bus and re-render hooks, `DebugReadout`, `tagTestId`, and `reactView` (the typed way to build a component view's body). Its `ts/ui/` tree is the one place rule 3 admits React into, so a package that draws pixels or text never imports it; [`packages/ui-fixture/`](../packages/ui-fixture) is a component view's fixture, listed by no product. | The shell's own components and every package with a React view. |
 | [`packages/system/`](../packages/system) | The **contract** every console implements and the engine drives: `SystemBase` and the capability interfaces, the shared system types, the transport primitives (`FrameBufferTriple`, `MemorySnapshotTriple`; `SpscRing` lives in `node-sdk`), the build request and the build function a node's package carries, the host's system services (`rp.host.systems/2`, `rp.engine.sync-trace/1`), the generic `SystemNode` template with the knob helpers, and the core-only utilities. | Shared by every console package and by the shell. |
 | [`packages/sameboy/`](../packages/sameboy) / [`packages/mesen/`](../packages/mesen) | The consoles, each beside its TS: `SameBoySystem` and the Game Boy node (`rp.sameboy.gb`); the NES, GBA and SMS/GG systems, their roles, the emulated N8 FIFO, their build functions and nodes (`rp.mesen.nes` / `.gba` / `.sms`). Their TS is the console descriptor the shell classifies a ROM with and places a system by (its detectors' rank, extensions, settings, split modes, link cable and real cart), plus their roles, catalogs and protocol nodes. `mesen` declares `n8` (the reliable link's header) and `system`; `sameboy` declares `system`. | The products that list them. |
 | [`packages/host-io/`](../packages/host-io) | The host's own I/O as node types (`rp.host.midi-in` / `rp.host.midi-out`) and the engine service they read, `rp.engine.host-io/3`, with the staging bounds the engine sizes from. | Every desktop product. |
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index f1c2359db..3897fb73c 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -557,7 +557,11 @@ short-circuits on the project flag precisely because it runs on every menu rebui
   `backend.getFrame(systemId)` on each native `"frame"` event and blits it into an LVGL
   `Canvas` via `setBuffer` ([EmulatorTile.tsx:50](../packages/retroplug/ui/screens/grid/EmulatorTile.tsx#L50)).
   Click → `setFocus`; an unfocused tile gets a translucent dim overlay, a focused one an accent
-  border (both suppressed when it is the only tile).
+  border (both suppressed when it is the only tile). Between the dim and the border, `TileViews`
+  ([TileViews.tsx](../packages/retroplug/ui/views/TileViews.tsx)) mounts one `StableSlot` per registered
+  tile view whose `when` accepts the system: the two debug overlays are their carts' packages now
+  (`lsdj-overlay`, `risa-overlay`), and the slot's fixed place is what keeps a strip that appears
+  late from landing over the border and the render badge.
 - [`layout.ts`](../packages/retroplug/ui/screens/grid/layout.ts) is pure grid math
   (`GB_NATIVE_W/H`, `shapeFor`, `gridContentSize`, `getTileBounds`).
 
@@ -622,34 +626,46 @@ widget's native uid for the test harness and is inert in production.
   `useFocusGroup` claims the keypad; Enter → activate, Up/Down move the cursor, Left/Right →
   `onCycle(±1)`.
 
-### LVGL primitives — [`ui/lvgl/`](../packages/retroplug/ui/lvgl)
+### The LVGL kit - [`@retroplug/ui-react`](../packages/ui-react)
 
 - [`Box.tsx`](../packages/ui-react/ts/ui/Box.tsx) - a `<View>` with the LVGL
   default-theme chrome zeroed, caller style merged on top.
-- [`FocusProvider.tsx`](../packages/retroplug/ui/lvgl/FocusProvider.tsx) — owns the
-  app-wide empty **"sink"** keyboard group's lifecycle so LVGL doesn't route arrows/Enter into
-  clickable tiles. It deliberately does *not* claim the keypad itself (parent effects run
-  after children's, so it would clobber a just-mounted menu).
-- [`useFocusGroup.ts`](../packages/retroplug/ui/lvgl/useFocusGroup.ts) — the reusable
-  claim-the-keypad dance: a `useLayoutEffect` builds a fresh group from ordered refs, focuses a
-  target, `setKeyboardGroup(group)`, and on cleanup restores the sink (**never null**) and
-  destroys the group.
 - [`useNativeEvent.ts`](../packages/ui-react/ts/ui/useNativeEvent.ts) - subscribes to
   the `lvgljs` `on`/`off` bus (`frame` / `key` / `resize` / …) with a stable listener + handler
   ref (no re-subscribe on inline closures).
-- [`useWindowSize.ts`](../packages/retroplug/ui/lvgl/useWindowSize.ts) — the live window
-  size reactive to `"resize"`; `requestWindowSize` / `isWindowSizeControlled` are the TS side of
-  the `__rp_*` native window seam.
 - [`lv.ts`](../packages/ui-react/ts/ui/lv.ts) - typed `ViewEx` / `TextEx` / `CanvasEx`: the one
   place the lvgljs-ui components are cast, behind prop types for what native accepts. Plus
   [`PixelCanvas.tsx`](../packages/ui-react/ts/ui/PixelCanvas.tsx) (a crisp, aspect-kept frame
-  canvas) and [`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx).
+  canvas).
 - [`useFramePoll.ts`](../packages/ui-react/ts/ui/useFramePoll.ts) - the frame-clocked polls:
   every-N-frames, versioned native status, and per-frame sampling deduped on a display signature.
 
+### The shell's LVGL primitives - [`ui/lvgl/`](../packages/retroplug/ui/lvgl)
+
+These touch the app's own window and focus model, so they stay in the shell rather than the kit.
+
+- [`FocusProvider.tsx`](../packages/retroplug/ui/lvgl/FocusProvider.tsx) - owns the
+  app-wide empty **"sink"** keyboard group's lifecycle so LVGL doesn't route arrows/Enter into
+  clickable tiles. It deliberately does *not* claim the keypad itself (parent effects run
+  after children's, so it would clobber a just-mounted menu).
+- [`useFocusGroup.ts`](../packages/retroplug/ui/lvgl/useFocusGroup.ts) - the reusable
+  claim-the-keypad dance: a `useLayoutEffect` builds a fresh group from ordered refs, focuses a
+  target, `setKeyboardGroup(group)`, and on cleanup restores the sink (**never null**) and
+  destroys the group.
+- [`useWindowSize.ts`](../packages/retroplug/ui/lvgl/useWindowSize.ts) - the live window
+  size reactive to `"resize"`; `requestWindowSize` / `isWindowSizeControlled` are the TS side of
+  the `__rp_*` native window seam.
+- [`StableSlot.tsx`](../packages/retroplug/ui/lvgl/StableSlot.tsx) - a fixed-position wrapper
+  for one item of a reordering or type-swapping list, because lv_binding_js's `insertChildBefore`
+  always appends: only the wrapper's single child ever changes.
+
 The rest of `ui/` outside `screens/` is split by what it depends on:
-[`ui/hooks/`](../packages/retroplug/ui/hooks) holds plain React helpers with no LVGL in them
-(`useStateRef`, `useForceUpdate` / `useRerenderOn`);
+[`ui/hooks/`](../packages/retroplug/ui/hooks) holds the one plain React helper left in the shell
+(`useStateRef`; the frame and re-render hooks are the React kit's);
+[`ui/views/`](../packages/retroplug/ui/views) is the shell's side of a package's views - `viewSession.ts`
+(the pure session controller), `useViewSession.ts` (the one effect the three renderers share), the pixels,
+readout and component renderers, the window and tile slots - and `ui/input/appActions.ts` dispatches the
+contributed actions beside the shell's own three;
 [`ui/host/`](../packages/retroplug/ui/host) is the native editor seam - `nativeHooks.ts` types
 every `__rp_*` hook once, beside the render-job and open-path seams; and
 [`ui/app/`](../packages/retroplug/ui/app) holds the app's own flows - the full-window overlays
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index 53cd9a48f..c2853e2de 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -120,8 +120,9 @@ in product order (the menu contributions, imported only by the UI), and `cli/gen
 `retroplug.cli` module in product order (the CLI tools, imported only by the dispatcher). The same run writes the
 typecheck paths: `tsconfig.host-modules.json` (each host module's declaration file, which a package with a
 `retroplug.host` declaration extends) and `tsconfig.paths.json` (the same entries plus the shell's own aliases, from
-`tsconfig.aliases.json`: a child's `paths` REPLACE its parent's, so the shell's program needs one file carrying both). The product list is the **dependency order and the registration
-order both** (a package before the ones that use it, the shell last), and only a ROM provider's place in it is
+`tsconfig.aliases.json`: a child's `paths` REPLACE its parent's, so the shell's program needs one file carrying both).
+The product list is the **dependency order and the registration order both** (a package before the ones that use it,
+the shell last), and only a ROM provider's place in it is
 behaviour: the providers run in that order, and the first to match a ROM fixes a system's role-list order. A role's
 kind, a kit format's id and a tracker integration's marker role are looked up rather than sequenced (one package's
 roles interleave another's without consequence, and the markers are exclusive), so for those a duplicate is the
@@ -143,14 +144,18 @@ generated modules and both path configs are gitignored, and `retroplug-ts-packag
 content changes. Seven targets depend on it, so they exist before the first esbuild run of a fresh checkout: the
 control-plane, UI, script, render-worker and CLI bundles, the example sessions and the CLI SDK assets. The root
 `pnpm typecheck` script runs the generator before any package's `tsc`, so a checkout with no generated file typechecks
-(CI's order is install, typecheck, build). A package with examples must have a `tsconfig.json` whose parsed `extends`
-names `../<shell>/tsconfig.host-modules.json` (the generator refuses one without, because the shell's program compiles
-`../*/examples`; naming the file in a comment is not extending it); a package with `retroplug.host` declares each host
-module it imports, and the generator and rule 3 both hold the declaration to the one shell-owned list,
-[host-modules.json](../packages/retroplug/host-modules.json). That list's `surface` is enforced too: a host module is
-importable only from the files its surface owns, which is the package's `examples/` and `ts/cli/` for `cli` and its
-`ts/ui/` for `ui`, and rule 3 refuses one imported from an app, protocol, console or test file, saying which surface
-the module belongs to.
+(CI's order is install, typecheck, build). A package that lists examples, or declares a host module the UI's surface
+owns, must have a `tsconfig.json` whose parsed `extends` names `../<shell>/tsconfig.host-modules.json` (the generator
+refuses one without: the shell's program compiles `../*/examples`, and a `.tsx` resolves `react` and
+`react/jsx-runtime` only through those paths; naming the file in a comment is not extending it); a package with
+`retroplug.host` declares each host module it imports, and the generator and rule 3 both hold the declaration to the one
+shell-owned list, [host-modules.json](../packages/retroplug/host-modules.json): five modules over two surfaces,
+`retroplug-cli` for `cli` and `react`, `react/jsx-runtime`, `lvgljs` and `lvgljs-ui` for `ui`. That list's `surface` is
+enforced too: a host module is importable only from the files its surface owns, which is the package's `examples/` and
+`ts/cli/` for `cli` and its `ts/ui/` for `ui`, and rule 3 refuses one imported from an app, protocol, console or test
+file, saying which surface the module belongs to. Rule 3's other half is where a package's React may live: a `.tsx` is
+admitted only under the package's `ts/ui/` tree, and automatic JSX makes it a use of `react/jsx-runtime` with no
+specifier, so a package with one declares that module.
 
 **The host contract.** The three TypeScript surfaces build a host for their hooks and tools from
 [core/ts/host.ts](../packages/core/ts/host.ts): `Host<G>` is an object of named groups (`files`, `png`, `kits`,
@@ -167,17 +172,22 @@ and the boundary check still sees the file as the package's source (it is outsid
 `devDependencies`). The shell's own tests ([test/systems/fixtures.ts](../packages/retroplug/test/systems/fixtures.ts))
 re-export the ones they share, so a store or menu test keeps one import.
 
-**The shell imports contracts, not carts (F4a's rule 9).** `packages/retroplug/src` may import an `@retroplug/*`
-specifier only from the boundary check's explicit list, each entry with the reason it is a contract rather than a cart:
-core's contract modules (`systemRoles`, `consoles`, `graphDocument`, `midiRouting`, `romParameters` and the rest), the
-tracker contract (`songCatalog`, `assetCatalog`, `trackerIntegration`, `trackerRender`, `trackerPlayback`,
-`trackerSongImport`, `workingSongBlob`), dsp's kit contract and package-contributions shape, and launchpad's registry
-and enums. A cart's or a console's own module is reached through the generated registries, which is why the four doors
-that used to re-export carts (`src/kit`, `src/tracker/index.ts`, `trackerRegistry`'s integration re-exports and
-`audioDriver`'s spec re-exports) are gone: an importer names the owning package's subpath (`@retroplug/lsdj/kitFormat`,
-`@retroplug/dsp`, `@retroplug/smsggdj/smsggdjIntegration`), and the shell's own cart-named entry points are lookups in
-the registries (`SystemsStore.loadEmbedded(id)`, `AudioDriver.compileKitFormat(id, spec)`). An allowlist entry no import
-uses fails the check, and `src/generated/` is not judged (it is the one place that names every package).
+**The shell imports contracts, not carts (rule 9).** The boundary check judges the app shell's three own trees, `src/`,
+`cli/` and `ui/`, each against an explicit list of the `@retroplug/*` specifiers it may import, each entry with the
+reason it is a contract rather than a cart. `src/` and `cli/` share `SHELL_IMPORTS`: core's contract modules
+(`systemRoles`, `consoles`, `graphDocument`, `midiRouting`, `romParameters` and the rest), the tracker contract
+(`songCatalog`, `assetCatalog`, `trackerIntegration`, `trackerRender`, `trackerPlayback`, `trackerSongImport`,
+`workingSongBlob`), dsp's kit contract and package-contributions shape, launchpad's registry and enums, and the test
+harness. `ui/` has a list of its own, `UI_IMPORTS`, because the kit and the view contract are the UI's alone (no `src/`
+or `cli/` file may name `@retroplug/ui-react`) and the control plane needs no menu model or contribution type. A cart's
+or a console's own module is reached through the generated registries, which is why the four doors that used to
+re-export carts (`src/kit`, `src/tracker/index.ts`, `trackerRegistry`'s integration re-exports and `audioDriver`'s spec
+re-exports) are gone: an importer names the owning package's subpath (`@retroplug/lsdj/kitFormat`, `@retroplug/dsp`,
+`@retroplug/smsggdj/smsggdjIntegration`), and the shell's own cart-named entry points are lookups in the registries
+(`SystemsStore.loadEmbedded(id)`, `AudioDriver.compileKitFormat(id, spec)`). A relative import is judged by where it
+lands: a `src/` file stays in `src/`, a `cli/` file in `cli/` or `src/` (the CLI is a surface above the control plane),
+and a `ui/` file in `ui/` or `src/` (the UI is a surface over the stores). An allowlist entry no import of its root
+uses fails the check, and a root's `generated/` tree is not judged (it is the one place that names every package).
 
 **The two boundary checks.** `pnpm check:boundaries`
 ([check-boundaries.mjs](../packages/retroplug/scripts/check-boundaries.mjs)) judges every file under a package's
@@ -193,15 +203,16 @@ trees, by target, by a library path, or through an include flag (`-I`, `-iquote`
 binary that opens a package's file, say its embedded ROM, reaches that package's tree though it links nothing of it),
 and the configure refuses anything else. It reads the TARGET's own properties, never a CMakeLists' text, so a path a
 comment names is no reach; the TS check, which has no target to read, counts a declaration as used from the file's text
-with its comments stripped, and only to raise or silence a warning. The TS check's one allowlist is rule 9's
-`SHELL_IMPORTS` (the shell's contract modules, each with the reason it is one); otherwise neither check excuses a reach
-(`EXCEPTIONS` is empty, and the native check has no exception at all). Each has its own fixtures
-([test/boundaries](../packages/retroplug/test/boundaries);
-`packages/native/test/packages`) that fail one rule each; the TS check's rule 9 (the shell's source may import only its
-contract modules) has a shell tree per answer, one with a cart import and one with only contract imports beside a
-generated one. They guard against accident and drift, not a determined author: each checker's header lists what it
-deliberately does not cover. `pnpm test` runs the TS check before it bundles a single test file, so a boundary a change
-broke is named as such rather than surfacing as a bundler error.
+with its comments stripped, and only to raise or silence a warning. The TS check's allowlists are rule 9's
+`SHELL_IMPORTS` and `UI_IMPORTS` (the shell's contract modules per root, each with the reason it is one); otherwise
+neither check excuses a reach (`EXCEPTIONS` is empty, and the native check has no exception at all). Each has its own
+fixtures ([test/boundaries](../packages/retroplug/test/boundaries); `packages/native/test/packages`) that fail one rule
+each; the TS check's rule 9 (the shell's src, cli and ui may import only their own list's contract modules) has a
+shell tree per answer: one per root with a cart import, one per root with only contract imports beside a generated one,
+and a tree whose UI file names a contract module only the shell's own list carries. They guard against accident and
+drift, not a determined author: each checker's header lists what it deliberately does not cover. `pnpm test` runs the
+TS check before it bundles a single test file, so a boundary a change broke is named as such rather than surfacing as a
+bundler error.
 
 **The shell's sources hold no cart or console literal.** A test
 ([test/source-literals](../packages/retroplug/test/source-literals)) walks `packages/retroplug/src` (not
@@ -217,7 +228,8 @@ place. The ids, extensions, cores and node types are the console registry's, rea
 and the cart ids (an embed's, a tracker integration's and a kit format's) from the generated package list, so a
 console or a cart a product adds is guarded without an edit to the test, and its knowledge stays its package's. The
 exemptions are the frozen v2 to v3 step's four package role-kind keys, which may not read the registry (decision 13).
-`cli/` is scanned too, skipping `cli/generated/` and the SDK's declaration file. `ui/` is F4c's. What it cannot see: a
+`cli/` and `ui/` are scanned too, each skipping its own `generated/` tree, and the SDK's declaration file. What it
+cannot see: a
 device package's ids and node types (an `n8`, a `launchpad`, an `rp.n8.*`), a non-console role kind, any id inside a
 string with whitespace in it, and the SDK's declaration file, which it skips. Like rule 9 it catches an accident rather
 than a determined author (a computed `["g", "b"].join("")`, a computed table key and a dotless pattern such as
@@ -291,9 +303,11 @@ the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by
    A package with `retroplug.ui` exports the module it names (the shell imports it through the generated menus), one
    with `retroplug.cli` exports its tools module (default-exporting `defineCliTools`), one with `retroplug.examples`
    keeps each entry under `examples/` and adds the generated `../retroplug/tsconfig.host-modules.json` to its
-   `extends`, and one with `retroplug.host` declares every host module it imports (today `retroplug-cli` alone). The
+   `extends`, and one with `retroplug.host` declares every host module it imports (one of the shell's five:
+   `retroplug-cli` for the `cli` surface, or a React module for `ui`). The
    generator refuses a missing module, a duplicate tool name or example name, an example outside `examples/`, a host
-   declaration the shell's list lacks and an examples package without that `extends`.
+   declaration the shell's list lacks, a missing `extends` on a package that lists examples or declares a UI host
+   module, and a React import or `.tsx` outside the package's `ts/ui/`.
 6. A protocol node's span label is the next free one (24 today: 16 up, with no gaps), which the manifest's `label` and
    the generator's check demand.
 7. A new contribution KIND is not data. It means editing `renderPackages` in
@@ -333,6 +347,33 @@ the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by
     service). A host and a provider of different `RP_PROVIDER_ABI_VERSION`s refuse each other in both directions, at
     runtime: there is no optional read of a newer member. The product calls `rp_wake_providers` once per frame, and stops
     its providers after reclaiming its plans.
+11. A **view** is declared in the package's `retroplug.ui` module (`defineUi({ views })`), not coded in the shell: an
+    id, a label, `placement` `window` or `tile`, an optional `when(sys)` gate, an optional `visible` flag (a tile view
+    only), and one body kind. Two need nothing but plain TypeScript: a `pixels` body's `open` returns a session whose
+    `frame()` is the next `{ pixels, width, height }` (or null when nothing moved), and a `readout` body's `open`
+    returns a sampler whose `sample()` is the strip's lines and playing flag (null = an unsupported cart, undefined =
+    unreadable this frame). The shell renders both. A `component` body is React and is built with `reactView` from
+    `@retroplug/ui-react`: the package declares that workspace package in `dependencies` (as
+    [ui-fixture's manifest](../packages/ui-fixture/package.json) does) and, in `retroplug.host`, the React host modules
+    it imports (`react`, `react/jsx-runtime`, and `lvgljs` / `lvgljs-ui` where it names them; `retroplug.host` takes
+    only what the shell's host-modules.json lists), and its `tsconfig.json` extends the generated
+    `../retroplug/tsconfig.host-modules.json` so the typecheck resolves them; a package whose views are plain TS needs
+    neither the dependency nor the host declarations. React is admitted only under a package's `ts/ui/` tree, so a React
+    view's test cannot live in `ts/test/`: the pure controller (open, reopen, close) is tested there over fake views and
+    a fake host ([view-session.test.ts](../packages/retroplug/test/ui/view-session.test.ts)), and what the component
+    draws is proved by a `test:ui` case in the shell, where the display is - which needs a product to list the package,
+    since the UI test binary embeds the production bundle. A plain-TypeScript view needs none of that: its own tests
+    live in the package's `ts/test/` over a fake host, as
+    [ui-views.test.ts](../packages/lsdj/ts/test/ui-views.test.ts) does. The generated registry refuses a duplicate view
+    id (naming both packages), a placement or body kind the shell has not, and a `visible` on a window view.
+12. An **action** is a key binding the package owns (`defineUi({ actions })`): `{ id, label, keys?, pad?, run }`. Its id
+    is prefixed by its own package's manifest name (`lsdj.debug-overlay`), because a stored binding profile keeps
+    ids and a user's profile must name the owner; `label` is the bindings menus' row text, `keys`/`pad` are the
+    DEFAULTS (a missing entry reads `-`), and `run(host, ctx)` gets the UI host and the focused system's id (null on the
+    start screen). The generated registry refuses a duplicate action id, an id not prefixed by its package's name, a
+    default key `resolveKeyName` cannot resolve, a default pad name outside `GAMEPAD_BUTTON_NAMES` (SDL's button set),
+    and a default that collides with a shell default or a Game Boy button's (a key on both would fire the action and the
+    joypad). Two packages may share a default key; that is how one backtick still flips both debug overlays.
 
 **A console package's native half.** A package whose console the engine runs is a native entry like any other, plus
 three rules the two console packages (four node types between them) follow (F2,
@@ -410,7 +451,8 @@ which has no reading for the files it did not execute), and set `RP_FAIL_ON_SKIP
 skips on a machine with a complete fixture set.
 
 Fixtures are reached through injected paths, never a literal: `__REPO_RESOURCES_DIR__` (committed,
-in-repo, always present), `__RESOURCES_DIR__` (the sibling resources tree, `RETROPLUG_RESOURCES_DIR`),
+in-repo, always present), `__RESOURCES_DIR__` (the sibling resources tree, `RETROPLUG_RESOURCES_DIR`, which the pure
+runner injects as the native one does),
 `__RISA_SRC__` (`RISA_SRC`) and `__MGB_ROM__` (the mGB synth's committed ROM, `packages/mgb/rom/mGB.gb`,
 which F2 moved into its package). Pointing `__RESOURCES_DIR__` and `__RISA_SRC__` at a nonexistent directory is how
 the CI picture is reproduced locally.
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index a29ee9455..f8a47a517 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -49,7 +49,7 @@ built ([05-data-persistence.md](05-data-persistence.md)).
 There is no live "watch RAM" streaming path. Reads are one-shot through
 the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
 consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
-([useLsdjHdSession.ts](../packages/lsdj/ts/ui/hdView.ts)) on a frame divider,
+([hdView.ts](../packages/lsdj/ts/ui/hdView.ts)) on a frame divider,
 because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
 cost question, not a capability one - an arming seam would let those two stop copying the whole image
 to read a few hundred bytes of it. Nothing of the old mechanism is left to switch on: `enableMemorySnapshot`,
@@ -678,9 +678,10 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     `trackerIntegrationFor` ([trackerIntegration.ts](../packages/tracker/ts/trackerIntegration.ts), by
     `markerRole` presence), so the LSDj menu's sync cyclers
     ([menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts)) edit a left-out `lsdj-sync` on an mGB cart;
-    `hasRole(..., "lsdj-sync")` in `ui/App.tsx`, `ui/screens/grid/useLsdjRuntime.ts`,
-    `ui/screens/hd/useLsdjHdSession.ts` and `projectStore.ts` (which tests `roles.some((r) => r.kind ===
-    "lsdj-sync")`); and `projectParameterMap`
+    `hasRole(..., "lsdj-sync")` in
+    [lsdj/ts/ui/hdView.ts](../packages/lsdj/ts/ui/hdView.ts) and
+    [lsdj/ts/ui/overlayView.ts](../packages/lsdj/ts/ui/overlayView.ts) and `projectStore.ts` (which tests
+    `roles.some((r) => r.kind === "lsdj-sync")`); and `projectParameterMap`
     ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)), which still exposes mGB's CC lanes for an LSDj
     cart carrying a stale `mgb`. The fix is `placedRole(...)`
     ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
@@ -854,30 +855,54 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   and import the SDK by its bare `retroplug-cli` specifier, and `retroplug.host` + the shell's
   [host-modules.json](../packages/retroplug/host-modules.json) generate the typecheck paths. The proof: rule 9 holds
   both `src/` and `cli/` to an explicit contract list (with per-root relative semantics), the literal scanner covers
-  `cli/` with the console and cart registries' ids, a ratchet names the five `ui/` files F4c moves, and a bundle check
+  `cli/` with the console and cart registries' ids, a ratchet named the five `ui/` files F4c then moved (F4c retired it
+  for rule 9's `ui/` root), and a bundle check
   says the control plane, the render worker, the script nodes, the SDK, the dispatcher and the planner carry no UI (no
   `ui` package module, no package's `ts/ui/`, no `ui/generated`) while the UI carries no CLI (no package's `ts/cli/`, no
   `core/cliTool.ts`, no `cli/generated`). Nothing was re-recorded; the behaviour edges are the CLI command index's
   order (the shell's five, then the packages' in product order), which a test pins, and a hand-edited project whose
   roles omit the system's core role no longer drawing the core's dead rows (design section 6's E13). What it leaves:
-  - **F4c: React in packages, next.** A `@retroplug/ui/kit` (`Box`, `lv`, `PixelCanvas`, `useNativeEvent`, the
-    frame-poll hooks, `DebugReadout`), the LSDj HD player and the two overlays as views, a UI-only view registry, how
-    packages declare React (`retroplug.host` gains `react`, `react/jsx-runtime`, `lvgljs-ui`, `lvgljs`), rule 9 and the
-    scanner over `ui/` (one literal there today: `useRisaRuntime.ts`'s `"risa"`), and the `ui/` ratchet deleted. The
-    shell's `tsconfig.json` includes `../*/ts`, so a `.tsx` in a package needs a `jsx` setting per package or a separate
-    UI program - and `tsconfig.ui.json`'s four hand-written `paths` (`react`, `react/jsx-runtime`, `lvgljs`,
-    `lvgljs-ui`) must be generated from `host-modules.json` for the same paths-replace reason `tsconfig.paths.json`
-    exists, while `ui-harness` stays the shell-owned `tsconfig.aliases.json`'s entry.
+  - **F4c: React in packages, built below.** The React kit, the views and the actions moved into the packages; what
+    that left is the next bullet's.
   - **The controller app's display bug is owed, deliberately kept identical.** A project whose controller app's config
     is hand-edited (a value the schema or a row does not list) still shows what the old menu showed, fallback for
     fallback; the display edge the F4b review found is recorded rather than fixed, because F4b changes no behaviour.
   - **Per-product bundles** stay owed (F4a's item); F4b adds that `ui/generated/menus.ts` and `cli/generated/tools.ts`
     are separate modules, so the UI and the CLI each carry only theirs, and the bundle check keeps that true.
   - **The shell's hard dependency on `launchpad`** (`registry`, `enums`) is unchanged, and a UI kit compile
-    (`host.kits` in the UI host) and `systems.readRam` for F4c's views are not there yet.
+    (`host.kits` in the UI host) stayed F4c's and is still owed.
   - **Windows, macOS and arm64 builds** of every F4b package and script are owed, as F4a's were (`MemoryType` stays a
     closed wire enum, F2's item).
 
+- **F4c built: React in packages** ([modular-graph-phase-f4c-design.md](../docs/modular-graph-phase-f4c-design.md), on
+  `feature/modularize`): the React kit is a package of its own, `@retroplug/ui-react`
+  ([packages/ui-react](../packages/ui-react)), with its React under its own `ts/ui/` tree (the one place rule 3 admits
+  React into) and the four React host modules generated into the typecheck paths; `tsconfig.base.json` gains
+  `jsx: react-jsx`, and `packages/ui-fixture` (listed by no product) proves a `component` view typechecks. The `ui`
+  package's contribution gained the view contract (`Readable`/`Signal`, `PixelSession`/`ReadoutSampler`, the three body
+  kinds, window and tile views) and the action shape, and the generated `ui/generated/menus.ts` carries `VIEWS` and
+  `ACTIONS` beside `UI_CONTRIBUTIONS`, each with its load-time refusals. The shell renders a window view in place of the
+  grid and a tile view in a per-view `StableSlot` over every tile its `when` accepts, owns the session lifecycle in
+  `ui/views/` (open, reopen on a ROM path or role change, close on a hide or unmount), and dispatches contributed
+  actions beside its own three through the key and gamepad maps. The LSDj HD player, the LSDj and risa debug overlays
+  and their two `debug-overlay` actions are their packages' now
+  ([lsdj/ts/ui](../packages/lsdj/ts/ui), [risa/ts/ui](../packages/risa/ts/ui)); `uiViews`, `MenuContext.openLsdjHd`,
+  `lsdjDebug` and the six moved `ui/` files are gone, so the shell's `ui/` names no cart. The proof: rule 9's third root
+  (`ui/` judged against its own `UI_IMPORTS`), the literal scanner over `ui/`, the `ui/` ratchet deleted, and the
+  recorded menu matrix's one edge (E1: each bindings menu gains a row per contributed action, 700 added lines and
+  nothing else). Nothing else was re-recorded; the behaviour edges are design section 7's (the backtick is rebindable
+  per overlay, `views.open` reports what it refuses, a throwing action is isolated, and a role write reopens that
+  system's views). What it leaves (design section 11):
+  - **A real `component` view**: the kind and its fixture ship, and the first package that needs React for a view proves
+    it on a host, with the `test:ui` case that lands with it.
+  - **A kit compile** (`host.kits` in the UI host) when a UI feature needs one, and gamepad axis tokens as action
+    bindings if a package wants a stick-driven action.
+  - **The controller app's display bug**, the per-product bundles and the shell's hard dependency on `launchpad`,
+    unchanged.
+  - **Windows, macOS, arm64 and CI** for F4c's scripts and packages, as F4a's and F4b's; and two shape questions left
+    as built: whether `tsconfig.ui.json` folds into the shell's program, and whether the reopen key stays as wide as a
+    role write.
+
 - **F2 built: the native carve** ([modular-graph-phase-f2-design.md](../docs/modular-graph-phase-f2-design.md), on
   `feature/modularize`; the design's section 12 records its plan, task commits, acceptance run, final review and the
   follow-up that closed it): `packages/system` (the contract every console implements and the engine drives, the
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index fa8a3a435..f37c0271e 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -76,7 +76,8 @@ new native code — it polls `readRam` through the snapshot registry on a frame
 copying 128 KiB and decoding a song every frame is too expensive (see the live-memory entry in
 [07-remaining-work.md](07-remaining-work.md)).
 
-- Code: `packages/lsdj/ts/hd/` (tiles, render, canvas), `ui/screens/hd/`.
+- Code: `packages/lsdj/ts/hd/` (tiles, render, canvas) and
+  `packages/lsdj/ts/ui/hdView.ts` (the "lsdj-hd" window view the shell renders).
 - **Prove a change:** `pnpm test:ui lsdj-hd`, `pnpm test lsdj/hd`.
 
 ---
````

Run: `sed -n '12709,13325p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Then the bytes, one hash per path in the block's order:

````bash
xargs git hash-object <<'EOF'
AGENTS.md
docs/lsdj.md
docs/package-split.md
packages/retroplug/test-native/lsdjPlaybackProbe.ts
packages/retroplug/test-ui/lsdj-overlay.test.ts
packages/retroplug/test-ui/risa-overlay.test.ts
packages/ui-react/ts/ui/DebugReadout.tsx
packages/ui/package.json
spec/00-overview.md
spec/03-ts-layer.md
spec/06-build-test.md
spec/07-remaining-work.md
spec/13-subsystems.md
EOF
````

Expected:

````text
4b4d38189bcb4942be37fef754b78724642911d5
e98bdaf2f286e9c0db3c15e3306162d9f90d6dfc
b2ddc8a10e295980eeeff29ac21a3ae30cf413ee
3d4a5d5b50dafae046f58bca4af4a1d7a6951d36
3a36b75dc1cc688ddbbdc0916df958e79712105a
5028c4992d8e8c1748a3351422a1800af0b26b73
336cf9c25541187812958ba536505444aac52ad0
40885c0b1947b89d26207fe6cfef50882cd40c85
ba3aa5da8d69bfc8a9e9fffd21ae4b79fd5218f1
3897fb73c23d597e9b2d4eaceb7402c60f0cb531
c2853e2de93dffd3af4f3caada9c3225a5bb3bcd
f8a47a5170b6ad66260fe5d6f9766dfdfc6c1310
f37c0271ef55b4c3f11217d349989a0320df8b00
````

- [ ] **Step 5: The design's as-built record**

The design document is corrected in place to what the prototype built (its sections 1 to 11) and records the prototype
in a new section 12: the five commits and their counts, what each review found and what its fix pass changed, the
departures this plan's header lists, the one record (E1), the existing tests whose shape changed, the verification
table and the acceptance list. It is the last large block of the phase and the source of this plan's header.

````diff
diff --git a/docs/modular-graph-phase-f4c-design.md b/docs/modular-graph-phase-f4c-design.md
index 962095959..045b5bbb9 100644
--- a/docs/modular-graph-phase-f4c-design.md
+++ b/docs/modular-graph-phase-f4c-design.md
@@ -1,9 +1,11 @@
 # Phase F4c: React in packages
 
 **Date:** 2026-11-10 · **Branch:** `feature/modularize`
-**Status:** designed, over `c8a35d34b` (the F4b tip). The user's decisions are the `### F4c` entries of
-`.superpowers/f-split/decisions.md` (taken 2026-11-10); approved by the user 2026-11-10; section 10 keeps
-what is still open.
+**Status:** designed 663632d16; prototyped (five stages, each Opus-reviewed and fixed); as built in section 12;
+plan next. Designed over `c8a35d34b` (the F4b tip). The user's decisions are the `### F4c` entries of
+`.superpowers/f-split/decisions.md` (taken 2026-11-10); approved by the user 2026-11-10. Sections 1 to 11 are corrected
+in place to what the prototype built, section 10 says what the prototype answered and what stayed open, and section 12
+records how.
 
 F4c is the last of the package split's fourth phase ([package-split.md](package-split.md)), after F4a
 ([modular-graph-phase-f4a-design.md](modular-graph-phase-f4a-design.md)) and F4b
@@ -94,12 +96,19 @@ here).
 | 2 | Where the React kit lives | A package of its own, `packages/ui-react`, with its React under its own `ts/ui/`; `@retroplug/ui` stays plain TypeScript. Reason: a pack author must not have to use React (user, option C) | The kit inside `@retroplug/ui` at `ts/ui/kit/` with rule 3 unchanged (A); `packages/ui/ts/kit/` plus a `retroplug.surface: "ui"` member (B) |
 | 3 | The view contract | Declarative, with React optional: id, label, placement, `when`, and a body of one kind, `pixels`, `readout` or `component`; the shell renders pixels and readout itself (user, option A) | React components only (B); a raw LVGL mount callback (C) |
 | 4 | The React half now | Ship `ui-react` and the `component` kind; the shell's pixels and readout renderers are built from `ui-react`, so `test:ui` renders the kit; the component kind is proved by a fixture pack, not rendered (user, option A) | Leave the React half for later (B); `ui-react` without the `component` kind (C) |
-| 5 | Typecheck | One setting everywhere plus rules: `jsx: react-jsx` in `tsconfig.base.json`, four React host modules with `types`, the generated paths carry them, a package with views extends the generated host-modules config, rule 3 refuses a `.tsx` outside a `ts/ui/` tree (user, option A) | A separate UI program per package that has React (B) |
+| 5 | Typecheck | One setting everywhere plus rules: `jsx: react-jsx` in `tsconfig.base.json`, four React host modules with `types`, the generated paths carry them, a package that declares a UI host module (or lists examples) extends the generated host-modules config, rule 3 refuses a `.tsx` outside a `ts/ui/` tree (user, option A) | A separate UI program per package that has React (B) |
 | 6 | Who owns keys | Packages contribute their own bindings and the shell routes them: developers will want keys beyond the debug toggle (user, option B) | A shell-owned debug layer that shows and hides every overlay (A) |
 | 7 | The key's shape | Contributed app actions in the binding profiles: `{ id, label, keys, pad?, run }`, rebindable in the bindings menus, an id absent from a stored profile taking its default (additive, no migration), two contributions on one key both running (user, option A) | Raw key handlers a package registers (B) |
 | 8 | The package's name | `packages/ui-react`, `@retroplug/ui-react`, not "ui-kit": "kit" reads as an LSDj sample kit (user) | `ui-kit` |
 | 9 | The overlay's visibility | Each package's own `Readable<boolean>` (section 3.1); backtick toggles each package's flag through its own action, so one press still flips both | One shell-owned flag (decision 6, A) |
 | 10 | The prototype model | DeepSeek Flash implements each stage, Opus reviews each stage, as F4a and F4b ran; the parallel kit with `sdl:controller` run alone; the per-stage tree rule of F4b section 11 | n/a |
+| 11 | The extend rule | The generator asks a package for the generated host-modules extend when it lists examples or declares a UI host module. A plain-TypeScript view needs no React path, and `views` is a TypeScript member the `.mjs` generator cannot see; risa's `tsconfig.json` is unchanged (controller, stage 1 review) | "Has views", as the draft said |
+| 12 | The UI bundle is no oracle | The bundle's hash moves whenever a bundled file moves (esbuild heads each module with its path), so stage 1 is held by a normalised diff, not a hash (stage 1 finding) | A hash equal before and after, as the draft said |
+| 13 | A key bound to two shell actions | One shell action per key, the later binding winning as the old map did; contributed ids accumulate beside it (controller, stage 3 review) | Every action on a key fires, as the draft said |
+| 14 | E9 | A backtick the user bound to a shell action now toggles the overlays AND runs that action, as any key bound to two actions does (controller, stage 3 review) | Gating the toggles to keep today's behaviour |
+| 15 | The tile slots | One `StableSlot` per REGISTERED tile view, always, with `when` checked inside it (stage 2 review) | A slot per view whose `when` passes now, as the draft said |
+| 16 | The default readout signature | The overlays carry none: the default (the lines and the flag) displays what the old hand-written key did (stage 4) | Narrowing it with a `signature` per sampler |
+| 17 | The PixelView guard | Folded back into stage 2, where the bug was; the plan must never carry a broken intermediate (stage 4 review) | Leaving it in stage 4, where `lsdj-hd` found it |
 
 Decisions the draft makes that the user did not take (each is a detail the contract needed; section 10 lists the ones
 worth a second look): `Readable<T>` for `visible` (the brief's `Subscribable<boolean>` does not exist, 10.1); an
@@ -107,6 +116,9 @@ worth a second look): `Readable<T>` for `visible` (the brief's `Subscribable<boo
 (5.4); `tagTestId` defined once, in `ui-react` (4.2); a real workspace package `ui-fixture` for the component-view
 fixture (8.6).
 
+Rows 11 to 17 are the decisions the controller took while the prototype ran; each is in `.superpowers/f-split/decisions.md`
+and each has its reason in section 12.
+
 ## 3. The contracts
 
 ### 3.1 Views
@@ -141,6 +153,8 @@ export type ViewBody =
   | { readonly kind: "pixels"; readonly unsupported?: string; open(host: UiHost, systemId: number): PixelSession | null }
   | { readonly kind: "readout"; open(host: UiHost, systemId: number): ReadoutSampler | null }
   | { readonly kind: "component"; readonly component: unknown };    // @retroplug/ui-react's reactView builds this
+/** What a `component` body is, and what @retroplug/ui-react's reactView returns. */
+export type ComponentBody = Extract<ViewBody, { kind: "component" }>;
 
 interface ViewBase {
   /** Also the view's test id: "lsdj-hd", "lsdj-overlay", "risa-overlay". Unique across the product. */
@@ -175,8 +189,10 @@ Why three kinds and not one (what the code the views replace needed, verified):
   array every frame, `canvas.ts:102`), reports `null` when `flush()` found no dirty tile (`useLsdjHdSession.ts:188`),
   and has a size (`canvas.width`, `canvas.height`) the PixelCanvas needs per `setBuffer` call (`LsdjHdScreen.tsx:42`).
   `PixelFrame` carries the size for that reason.
-- **The overlays** are `readout` tile views: three lines of text and a green-or-grey flag, deduped by a signature of
-  exactly the displayed fields (`useLsdjRuntime.ts:231`). They need no pixel and no layout beyond the strip.
+- **The overlays** are `readout` tile views: three lines of text and a green-or-grey flag. They need no pixel and no
+  layout beyond the strip, and they carry no `signature`: the default (the lines and the flag) is a function of exactly
+  the fields the old hand-written key (`useLsdjRuntime.ts:231`) covered, so it can change when and only when a displayed
+  pixel can (the old key excluded `chainRow`, which the lines do not show either).
 - **`component`** is what neither can say: a view that wants its own widgets. Nothing in the product uses it in F4c.
 
 What a declarative body cannot express, found while verifying, and what the design does about it:
@@ -191,6 +207,15 @@ What a declarative body cannot express, found while verifying, and what the desi
 - **The HD screen's full-window box carries the view's test id even when unsupported** (`LsdjHdScreen.tsx:46-49`, the
   `lsdj-hd` box wraps both the canvas and the message); the shell's window slot keeps that.
 
+**As built.** Package code is guarded where the shell calls it, in plain TypeScript beside the session controller so a
+test needs no React: a throwing `when` reads as "no", a throwing `visible` as hidden, a throwing `open` leaves the system
+unsessioned, a throwing `frame` or `sample` drops the session (its reopen key stays held, so a broken cart is retried at
+the next key change, not every frame), and a throwing `signature` falls back to the default; each is reported once per
+view object and per thing that threw. `ReadoutSampler.signature` can only coarsen the default; a sampler whose state
+moves without its lines changing narrows it. A `PixelSession.frame` is asked for only once the shell has a canvas to
+draw the result into (a flush-and-diff session, as the HD canvas is, cannot have a frame swallowed); `PixelView` checks
+the canvas first. `unsupported` is shown by a WINDOW view only: a tile `pixels` view whose `open` returned `null` draws
+nothing.
 ### 3.2 Actions
 
 ```ts
@@ -251,6 +276,10 @@ export interface SystemsHost { /* ... */ readRam(id: number): Uint8Array | null;
   React state).
 - `uiViews` in `uiHost.ts:31-37` and its `"lsdj-hd"` mapping; the UI host's `views` peer becomes the registry-backed
   `open` of 5.1.
+  As built `uiViews` and the `"lsdj-hd"` mapping went in stage 4, with `MenuContext.openLsdjHd` and its stubs (15 test and
+  recorded-context files in all); until the cart registered the same id App held an interim shell-side `lsdj-hd` entry.
+  The menu tests' fallback host (`hostOf`) resolves ids against the registry through `windowViewOrReport` and has no
+  window to open (5.1).
 
 ### 3.5 The generated registry
 
@@ -259,7 +288,7 @@ product order, and the checks of one new section:
 
 ```ts
 export const VIEWS: readonly { pkg: string; view: ViewDef }[] = UI_CONTRIBUTIONS.flatMap(...);
-export const ACTIONS: readonly { pkg: string; action: UiAction }[] = UI_CONTRIBUTIONS.flatMap(...);
+export const ACTIONS: readonly ContributedAction[] = UI_CONTRIBUTIONS.flatMap(...);   // { pkg, action }, declared once in contribution
 ```
 
 The checks run at load, in the generated module, as every F4b check does (the generator is `.mjs` and cannot read a
@@ -267,21 +296,25 @@ TypeScript contribution; `claimRow`, `checkRow` and the others are TypeScript in
 tests exercise them through the generated fixture module). They refuse, each naming the package or packages:
 
 1. a view id declared twice (one package or two);
-2. a view with a `placement` that is neither `window` nor `tile`, or a body `kind` that is none of the three, or a
-   `visible` on a window view (the type refuses the last; the data check backs the type, as `PLACEMENTS` does);
+2. a view with a `placement` that is neither `window` nor `tile`, a body `kind` that is none of the three (or no body at
+   all, which names the package rather than throwing), or a `visible` on a window view (the type refuses the last; the
+   data check backs the type, as `PLACEMENTS` does);
 3. an action id declared twice;
 4. an action id not prefixed by `<its own package name>.` (the manifest name, `lsdj`, as `UI_CONTRIBUTIONS`' `pkg`
    carries it), so a profile's stored id names its owner;
 5. a default key name `resolveKeyName` (`keyCodes.ts:60`) does not resolve, or a default pad name outside the gamepad
-   name table (3.6: the table does not exist yet);
-6. a default that collides with a shell default: a shell action's (`DEFAULT_KEYBOARD_ACTIONS` /
+   name table (3.6: `GAMEPAD_BUTTON_NAMES` is new);
+6. a default that collides with a shell default, compared by RESOLVED DPF CODE and not by spelling (so `Return` collides
+   with Enter and `z` with Z): a shell action's (`DEFAULT_KEYBOARD_ACTIONS` /
    `DEFAULT_GAMEPAD_ACTIONS`, `bindingMap.ts:28-29`: `Escape`, `Tab`, `leftshoulder`, `rightshoulder`) or a Game Boy
    button's in `defaultBindingMap()` (`bindingMap.ts:53`: the arrows, `Z`, `X`, `Enter` and the rest, keyboard and
    gamepad), since a key on both would fire the action AND the joypad. Two CONTRIBUTIONS on one key are allowed (the
    overlays' backtick).
 
-The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES` and the two default tables from `../../src/`,
-beside the `ROLES` import it already takes from `src/generated/packages`; `ui/` may reach `src/` (section 8.3).
+The generated module imports `resolveKeyName`, `GAMEPAD_BUTTON_NAMES`, the two default tables and `defaultBindingMap` from
+`../../src/`, beside the `ROLES` import it already takes from `src/generated/packages`; `ui/` may reach `src/` (section
+8.3). The generator's fixture tree holds one-line shims to the real `keyCodes.ts` and `bindingMap.ts` (aliased by the
+driver), so the checks are held to the real vocabulary and the real defaults and cannot drift from a copy.
 
 ### 3.6 The binding vocabulary the checks need
 
@@ -303,8 +336,9 @@ that could never fire is a bug the check should catch (10.2).
 `./ts/ui/index.ts`, a `tsconfig.json` extending `tsconfig.base.json` AND the generated
 `../retroplug/tsconfig.host-modules.json`, a `typecheck` script in lsdj's form (`node ../retroplug/scripts/lib/packages.mjs
 && tsc -p tsconfig.json`, so a fresh checkout generates the paths first), `ts/test/` for its pure cases,
-`dependencies` `@retroplug/ui` (the view types `reactView` builds) and `@retroplug/core` (`UiHost`), and a `retroplug`
-member of exactly `{ "host": ["react", "react/jsx-runtime", "lvgljs-ui", "lvgljs"] }`. Its React lives under its own
+`dependencies` `@retroplug/ui` alone (the view types `reactView` builds, and `UiHost`, which `@retroplug/ui/contribution`
+publishes; a `@retroplug/core` entry no file imports would draw the unused-dependency warning), and a `retroplug` member
+of exactly `{ "host": ["react", "react/jsx-runtime", "lvgljs", "lvgljs-ui"] }`. Its React lives under its own
 `ts/ui/`, which is what rule 3 admits React into (8.2). It contributes no `app`, `consoles`, `protocol`, `ui` or `cli`
 module, so it is not in `retroplug.products.desktop`, as `ui` and `tracker` are not (`contributes()`,
 `packages.mjs:150`); the shell declares it in `packages/retroplug/package.json` as `workspace:*` and `pnpm install`
@@ -322,9 +356,10 @@ All of these are the shell's own files today, moved with their text unchanged bu
 | `lvgl/useNativeEvent.ts` (49) | `useNativeEvent.ts` | with its `NativeEvents` table, which mirrors the C++ emit sites by hand |
 | `lvgl/useFramePoll.ts` (98) | `useFramePoll.ts` | `useFrameDivider`, `useVersionedPoll`, `useFrameSampled` |
 | `hooks/useRerender.ts` (23) | `useRerender.ts` | `useForceUpdate`, `useRerenderOn` |
-| `screens/grid/DebugReadout.tsx` (36) | `DebugReadout.tsx` | and `hex2` |
+| `screens/grid/DebugReadout.tsx` (36) | `DebugReadout.tsx` | `hex2` is `@retroplug/ui/format`'s, which the kit re-exports |
 
-New: `tagTestId.ts` and `reactView.ts`. `tagTestId(testId)` is the ref callback that tags a widget's native uid for
+New: `tagTestId.ts` and `reactView.ts` (and the entry, `index.ts`, which re-exports every other module of the tree; a test
+holds that). `tagTestId(testId)` is the ref callback that tags a widget's native uid for
 `findByTestId`: `StableSlot.tsx:416` implements it through the shell's typed `native.__rp_tagTestId`
 (`host/nativeHooks.ts`), and `DebugReadout` imports `StableSlot` for it, which is why the readout is shell-bound today
 (survey B). The kit reads the same optional global itself (`(globalThis as {...}).__rp_tagTestId?.(...)`) and exports
@@ -339,9 +374,11 @@ export { PixelCanvas } from "./PixelCanvas";
 export { useNativeEvent, type NativeEvents } from "./useNativeEvent";
 export { useFrameDivider, useVersionedPoll, useFrameSampled } from "./useFramePoll";
 export { useForceUpdate, useRerenderOn } from "./useRerender";
-export { DebugReadout, hex2 } from "./DebugReadout";
+export { DebugReadout } from "./DebugReadout";
+export { hex2 } from "@retroplug/ui/format";                  // one definition, plain TypeScript, shared with the carts
 export { tagTestId } from "./tagTestId";
-export function reactView(Component: ComponentType<ViewComponentProps>): ViewBody;   // { kind: "component", component }
+export function reactView(Component: ComponentType<ViewComponentProps>): ComponentBody;   // { kind: "component", component }
+export type { ViewComponentProps };                                                          // the contract's, published here too
 ```
 
 `reactView` is the typed way to build a `component` body: `@retroplug/ui` types the body's `component` as `unknown`, so
@@ -350,8 +387,9 @@ package.
 
 Shell-bound and staying in `packages/retroplug/ui/`: `StableSlot`, `useWindowSize`, `FocusProvider` / `useFocusGroup`,
 `host/nativeHooks`, `stores/` (`useStores` needs `AppStores`), and `useStateRef` (only `Menu.tsx` uses it; moving it
-would be speculative). The shell imports the kit from `@retroplug/ui-react`: 7 + 5 + 2 + 10 + 6 + 4 + 2 import
-sites in about 22 files change their specifier and nothing else.
+would be speculative). The shell imports the kit from `@retroplug/ui-react`: 19 files (33 import lines, merged to one
+`@retroplug/ui-react` line per file) change their specifier and nothing else, `StableSlot` imports the kit's `tagTestId`,
+and `EmulatorTile` and the HD screen import it from the kit rather than through `StableSlot`.
 
 ## 5. The shell side
 
@@ -359,13 +397,18 @@ sites in about 22 files change their specifier and nothing else.
 
 `App.tsx`'s `hdSystemId` (`:73`) becomes `openView: { id: string; systemId: number } | null`.
 
-- **Opening.** `createUiHost(stores, peers)` takes `views: ViewsHost` as a peer today (`uiHost.ts:43`); App now builds it:
-  `open: (id, systemId) => { const view = windowViewOf(id); if (!view) return report(...); setOpenView({ id, systemId }) }`,
-  where `windowViewOf` reads the generated `VIEWS` through `menuContributions.ts` (`viewById(id)`, beside `roleRows()`
-  and `uiServices()`). An unknown id, a `tile` view's id, and a system for which the view's `when` is false are
-  reported (`console.warn("[views] ...")`) and open nothing; the report is the same shape as `firstWinsMenuIds`'
-  (`menuContributions.ts`). `when` is checked at open so the unsupported box stays about a cart the view applies to but
-  cannot read (a supported role on an unsupported version), not about a system it was never meant for (10.9).
+- **Opening.** `createUiHost(stores, peers)` takes `views: ViewsHost` as a peer today (`uiHost.ts:43`); App builds it,
+  and its `open` is `openViewOf(id, systemId)`. `windowViewOrReport(id)` (in `menuContributions.ts`, beside `roleRows()`,
+  `uiServices()` and `windowViewOf(id)`, all reading the generated `VIEWS`) is the one place the two refusals that need
+  no project are worded (no view is registered under the id; the id is a tile view, which opens no window); App then
+  checks the two things only a project answers (the system is gone; the view's `when` rejects it). Each is reported
+  (`console.warn("[views] ...")`, the shape of `firstWinsMenuIds`' report) and opens nothing; otherwise
+  `setOpenView({ id, systemId })`. `when` is checked at open so the unsupported box stays about a cart the view applies
+  to but cannot read (a supported role on an unsupported version), not about a system it was never meant for (10.9).
+  The menu tests' fallback host (`hostOf` in `menuDefs.ts`, built over the stores alone) runs the same
+  `windowViewOrReport` and, for a view that resolves, says only that the menu's host has no window: only App's host
+  has one. Until stage 4 registered `lsdj-hd`, App kept an interim shell-side `lsdj-hd` entry in `openViewOf` and in the
+  full-window branch, and `uiViews` served the fallback host; stage 4 deleted all three.
 - **Closing.** Esc closes it where it does now (`App.tsx:202`: after the overlay check, before the key map, so Esc is
   still the universal back); the stale-system clear (`:150-152`) becomes generic over `openView.systemId`.
 - **Rendering.** The full-window branch (`:314-318`) renders `<ViewWindow view={...} systemId width height />`
@@ -375,13 +418,18 @@ sites in about 22 files change their specifier and nothing else.
 
 ### 5.2 The tile slots
 
-`EmulatorTile` mounts each tile view whose `when` passes for its system, in generated order, each in its own
-`StableSlot` (the `insertChildBefore` gotcha, AGENTS.md "Framework gotchas"): a view that appears and disappears (a
-`visible` flag, a session that opens late) would otherwise land at the END of the tile's LVGL child list, over the
-focus border and the render badge. The slots sit where the two overlay lines are now (`EmulatorTile.tsx:84-90`), after
-the dim and before the border; the slot's test id is `view-slot-<id>` (distinct from the view's own id, which tags the
-readout strip inside it). A slot is the tile's size with `background-opacity: 0`, and the readout inside aligns to its
-bottom middle exactly as it does to the tile's, since the two have the same size.
+`EmulatorTile` takes the UI host as a prop (App builds the one `UiHost`; `SystemGrid` passes it down with the stores) and
+mounts `TileViews`: ONE `StableSlot` per REGISTERED tile view, always, in generated order, each in its own slot (the
+`insertChildBefore` gotcha, AGENTS.md "Framework gotchas"), with the view's `when` checked INSIDE the slot. The registry
+is a module constant, so the slot list never changes length; a `when` that flips under a live tile swaps the slot's
+child instead of appending a slot at the END of the tile's LVGL child list, over the focus border and the render badge,
+so E4 holds for a flipping `when` too (the draft mounted a slot only for a view whose `when` passed, which did not).
+A slot whose `when` fails is the empty transparent `Box` the dim layer already proves passes a click. Each slot also
+subscribes to its view's `visible` itself, for every body kind, so a hidden `component` view mounts nothing at all. The
+slots sit where the two overlay lines were (`EmulatorTile.tsx:84-90`), after the dim and before the border; the slot's
+test id is `view-slot-<id>` (distinct from the view's own id, which tags the readout strip inside it). A slot is the
+tile's size with `background-opacity: 0`, and the readout inside aligns to its bottom middle exactly as it did to the
+tile's, since the two have the same size.
 
 This changes one thing: today the z-order depends on mount history. An overlay toggled ON after the border and the
 badge mounted is appended last, over them; a border or badge that mounts after the overlay (a focus move, a render
@@ -390,15 +438,17 @@ render badge and an overlay, both at the tile's bottom) the badge now always dra
 
 A slot is a full-tile transparent `Box`, like the dim layer that already covers every unfocused tile
 (`EmulatorTile.tsx:77-83`), through which a click still reaches the tile's `onClick` (that is how an unfocused tile is
-focused today). Stage 2 checks that a slot passes the click the same way (`test:ui`'s focus-by-click case with an
-overlay showing).
+focused today). No `test:ui` case could show it in stage 2 (no product registered a tile view, and `ui-fixture` is in no
+product), so the case is stage 4's: `overlay-click-through` clicks an unfocused tile while `lsdj-overlay` shows.
 
 ### 5.3 Three renderers
 
 The shell holds one renderer per body kind, in `ui/views/`, each built from `@retroplug/ui-react`:
 
-- **`PixelView`**: `useNativeEvent("frame")` -> `session.frame()` -> `PixelCanvas.setBuffer`, letterboxed by the canvas's
-  CONTAIN alignment as today. It passes `pixels.buffer` when the array is the whole buffer and `pixels.slice().buffer`
+- **`PixelView`**: `useNativeEvent("frame")` -> the canvas check -> `session.frame()` -> `PixelCanvas.setBuffer`,
+  letterboxed by the canvas's CONTAIN alignment as today. The canvas is checked BEFORE the frame is read: the HD canvas's
+  `frame()` is flush-and-diff, so a first (dirty) frame read while the ref was still null marked its tiles painted and
+  the screen stayed black for good (stage 2's guard; the unchanged `lsdj-hd` case caught it, and failed without it). It passes `pixels.buffer` when the array is the whole buffer and `pixels.slice().buffer`
   when it is a view onto a larger one: the HD canvas's array is its own (no copy per frame, as now,
   `LsdjHdScreen.tsx:42`), and an emulator frame is a subarray of the RPC wire buffer (`EmulatorTile.tsx:64-66` copies
   for that reason). `EmulatorTile` itself stays: its frame comes from `backend.getFrame` with a `published` flag, which
@@ -409,6 +459,8 @@ The shell holds one renderer per body kind, in `ui/views/`, each built from `@re
 - **`ComponentView`**: mounts the body's component with `{ host, systemId, width, height }` and nothing around it. The
   one cast in the shell lives here (`body.component as ComponentType<ViewComponentProps>`).
 
+All three read their session through `useViewSession` (5.4), which is three-state, and a renderer draws nothing for a
+session that has not opened yet.
 Test ids as today: a window `pixels` view tags its full-window box with the view id and its message box
 `<id>-unsupported` (`lsdj-hd`, `lsdj-hd-unsupported`); a readout tags the `DebugReadout` strip (`lsdj-overlay`,
 `risa-overlay`).
@@ -423,33 +475,45 @@ A pure-TypeScript controller owns the lifecycle so it can be tested without Reac
   as `useLsdjRuntime` does, `:251`), and again when a tile view's `visible` flips to true; **close** on unmount, on the
   change that reopens it, and when `visible` flips to false. A package holds no React state: a session is a plain
   object the shell owns.
+- **`useViewSession` is three-state and never lags what it renders** (`undefined`: not opened yet; `null`: refused or
+  hidden or its system gone; else the session). `shownSession(session, shown, system)` is the whole rule, so a hide
+  drops the session in the render that sees it, and the hook's `read(reader)` reads the session the CONTROLLER holds now,
+  not the render's closure, so a native `frame` landing between a hide (or a reopen) and the re-render cannot frame or
+  sample a session the shell has let go. What the user sees on opening the HD view: the black window box with nothing
+  inside it until the session opens (one effect, a frame or two), then the canvas; the unsupported text only ever means
+  "a cart the view applies to and cannot read". (As first built, every open of a supported cart flashed the unsupported
+  text for one effect.)
 - The reopen key is `(view.id, systemId, romPath, roles)` by identity, which is the brief's. It is wider than today's
   hooks: `useLsdjRuntime` rebuilds on `isLsdj` (a boolean) and `romPath`, `useRisaRuntime` likewise, and the HD session
   on the `lsdj-assets` config object, so a role edit that touches none of those leaves them alone, where a `roles`
-  identity change reopens every view of that system. `SystemsStore.setRoleConfig` replaces `roles` on a change
-  (verified by reading `view()`, `systemsStore.ts:208-224`, which hands `e.roles` through; whether every write replaces
-  the array is read from the setter and is a stage 2 check). The cost is one header read per LSDj overlay and one
-  whole-ROM read per risa overlay on an edit made while an overlay is showing (the instance menu is open, so the tile is
-  unmounted: the tile and its sessions close with it, so in practice the reopen happens when the menu closes, as it
-  does today). Recorded as E5 and as a question (10.4).
+  identity change reopens every view of that system. `SystemsStore.setRoleConfig` replaces `roles` on EVERY write
+  (`systemsStore.ts:964-974` slices `e.roles` and swaps in the merged config even when the stored value is unchanged;
+  `view()` hands the array through by reference), so the key moves on every role write: the width is as designed and
+  no narrowing was needed. The cost is one header read per LSDj overlay and one whole-ROM read per risa overlay on an
+  edit made while an overlay is showing; with the instance menu open the tile is unmounted, so in practice the reopen
+  happens when the menu closes, as it does today. The one visible case is E5's.
 - **Hidden work.** A hidden tile view opens no session (`visible` false means the renderer renders `null` and the
   effect does nothing), so a hidden overlay does the same zero work as `useXRuntime(id, false)` does now.
 
 ### 5.5 Keys: a list of actions per key
 
-`buildKeyToAction(actions)` and `buildGamepadToAction(actions)` (`keyCodes.ts:139`, `:155`) change from
+`buildKeyToAction(actions, contributed)` and `buildGamepadToAction(actions, contributed)` (`keyCodes.ts`) change from
 `Map<number, AppAction>` / `Map<string, AppAction>` to `Map<number, string[]>` / `Map<string, string[]>`: an action id
-list, in the order the profile (then the registration order) gives them. A key bound to the shell's `OpenMenu` and to a
-contributed action fires both. Unknown ids are still skipped; "known" is now the shell's three plus the product's
-contributed ids (a stored id of an action no product registers is ignored, as an unknown one is today).
+list per key, in the order the profile (then the registration order) gives them. "Known" is the shell's three
+(`isAppAction`; `APP_ACTIONS` is module-private) plus the ids the caller passes as `contributed` (App passes
+`contributedActionIds()`, a test passes its own), and a stored id of an action no product registers is skipped as an
+unknown one is today. One helper, `addActionId`, builds both lists, and it keeps what the old `Map.set` did: a key's list
+holds at most ONE of the shell's own ids, a later shell binding replacing the earlier in place (so `{ OpenMenu: ["Tab"],
+CycleNext: ["Tab"] }` gives `["CycleNext"]` as it did), a contributed id accumulates beside it, and an id the list already
+holds is skipped (one action bound to one key twice fires once). The draft let every action on a key fire, which changed
+a pre-existing double binding of the shell's own actions (stage 3's review).
 
 ```ts
 // ui/input/appActions.ts (new): the shell's dispatch over both kinds.
-const shell: Record<AppAction, () => void> = { OpenMenu, CycleNext, CyclePrev };      // App's runAction bodies
-function runActions(ids: readonly string[]): void {
+function runActions(ids, shell, contributed, host, systemId): void {                     // contributed is a parameter: the pure tier can drive a test action
   for (const id of ids) {
-    if (id in shell) shell[id as AppAction]();
-    else contributed.get(id)?.run(uiHost, { systemId: focusedOrNull() });             // each in a try: reported, then on
+    if (isAppAction(id)) shell[id]();
+    else contributed.find((c) => c.action.id === id)?.action.run(host, { systemId });   // each in a try: reported, then on
   }
 }
 ```
@@ -460,7 +524,8 @@ the map. Contributed actions fire in the same states backtick does today: any st
 whether the menu is open or not, and under an open window view (so backtick toggles an overlay while the HD view shows).
 OpenMenu's `isMenuModalActive` deferral (`toggleMenuCore`, `:180`) is its own and is not applied to contributed actions,
 which matches backtick's behaviour now (a capture row armed in the bindings menu, or a text prompt, still passes a
-backtick to the toggles) and is one of the open questions (10.6). The gamepad handler (`:210-214`) dispatches the same
+backtick to the toggles) and is one of the open questions (10.6). A backtick the user bound to a shell action now
+toggles the overlays AND runs that action (E9). The gamepad handler (`:210-214`) dispatches the same
 way. The three tests that read the old maps (`test/input/keymap.test.ts` 6 call sites, `test/input/gamepad.test.ts` 6,
 `test/menu/leaves.test.ts` 3) change from a value to a one-element list; nothing else reads them.
 
@@ -487,6 +552,9 @@ in a section is the user's, including `[]` (a cleared binding is a choice, as `C
   mount, with the menu tests (which compose stores alone, `recorded-cases.ts:87`) not doing it. A pure function at the
   two readers needs no registration.
 
+- `withActionDefaults` always returns a new object (two shallow copies and a new profile), even when nothing is filled:
+  App memoises it on the profile, so the identity changes exactly when the profile does, and the caller's resolved
+  profile is never mutated.
 ### 5.7 The bindings menus
 
 `bindingsChildren` builds `actionRows` from `APP_ACTION_ROWS` (`menuDefs.ts:1178`: the shell's three); it appends one
@@ -506,6 +574,11 @@ passes a single printable character through, `keyCodes.ts:93-97`). Each channel'
 menu's `navTo("New Profile")` in `test-ui/bindings.test.ts` (24 steps allowed) still reaches it: from `A:` the path grows
 from 9 steps to 11.
 
+The contributed list reaches the composer through a new optional `MenuContext.actions`: App leaves it out and the
+generated registry answers; a menu test passes its own list, which is the only way to drive a row for an action no product
+carries (the menu tests compose a context over the stores alone). The row text is exactly `<action.label>: <bound names or
+-> `, so the shell names no cart. The ids carry the action's dot (`bind-act-lsdj.debug-overlay`): nothing keys on an id's
+characters (10.15).
 ## 6. The moves
 
 ### 6.1 LSDj (`packages/lsdj/ts/ui/`, plain TypeScript)
@@ -515,24 +588,31 @@ from 9 steps to 11.
   `useLsdjHdSession.ts:193-214` unchanged in what it reads and when: the 0x150 header through `host.files.readPrefix`,
   `LsdjReader.fromHeader`, the whole ROM through `host.files.readFile`, the lsdj-assets overrides folded in through
   `applyOverridesToRom(base, overrides, host)` (the host's `files` and `png`, replacing `filesPngHost(backend)`), and
-  the `HdSession` class moved as it is, with `backend.readSram` -> `host.systems.readSram` (every 15 frames, the same
-  FNV signature over the first 0x8000 bytes) and `backend.readRam` -> `host.systems.readRam`. `frame()` is
-  `renderFrame()` of `:180-189` returning `{ pixels, width: canvas.width, height: canvas.height }` or `null`. The
-  system's `lsdj-assets` config comes from `host.systems.view()` by id (the role config is `roleConfig(sys.roles,
-  "lsdj-assets")`, `systems.view()` being the group's existing member). The "HD Player" row keeps
-  `host.views.open("lsdj-hd", sys.id)` (`lsdj/ts/ui/index.ts:253-255`).
+  the `HdSession` class moved as it is (exported, so its cadence is testable over a fake host), with `backend.readSram`
+  -> `host.systems.readSram` and `backend.readRam` -> `host.systems.readRam`. The SRAM poll is a countdown of 15, so a
+  read happens at frames 0, 16 and 32: the old cadence, 16 frames a period, with the same FNV signature over the first
+  0x8000 bytes. `frame()` is `renderFrame()` of `:180-189` returning `{ pixels, width: canvas.width, height:
+  canvas.height }` or `null`. `open` refuses what the old hook gated on itself (no `lsdj-sync` role, an empty ROM path)
+  and an unreadable header or a ROM `LsdjRom` refuses, since the shell checks `when` only at `views.open` and a renderer
+  can reach `open` with any system. The system's `lsdj-assets` config comes from `host.systems.view()` by id (the role
+  config is `roleConfig(sys.roles, "lsdj-assets")`). The "HD Player" row keeps `host.views.open("lsdj-hd", sys.id)`
+  (`lsdj/ts/ui/index.ts:253-255`).
 - **`overlayView.ts`**: a `TileView`, `id: "lsdj-overlay"`, `when` the same, `visible: lsdjDebug` (below), body `readout`.
   `open(host, systemId)`: `LsdjReader.fromHeader(host.files.readPrefix(rom, 0x150))`, null if unreadable or
-  `!reader.supported`; the sampler's `sample()` is `host.systems.readRam(systemId)` -> `reader.read(wram)` ->
-  `null` when `!state.supported` -> `{ lines, playing: state.playing }` with the three lines of `LsdjOverlay.tsx:170-175`
-  (`versionStr`, `channelCell`, `hex2` move with it; `hex2` is the kit's `DebugReadout` helper today and becomes a
-  six-line copy in the package, since a plain-TS package cannot import a React package's helper for a string format);
-  `signature` is `useLsdjRuntime.ts:231` over the frame, which the lines cover by construction (the signature excludes
-  `chainRow`, which the lines do not show either).
+  `!reader.supported` or the system has no `lsdj-sync` role; the sampler's `sample()` is `host.systems.readRam(systemId)`
+  -> `reader.read(wram)` -> `null` when `!state.supported` -> `{ lines, playing: state.playing }` with the three lines of
+  `LsdjOverlay.tsx:170-175` (`versionStr` and `channelCell` move with it; `hex2` is `@retroplug/ui/format`'s, one
+  definition both overlays and the kit share, and not a copy per package); an unreadable RAM answers `undefined`. No
+  `signature`: the default is the lines and the flag, which displays what the old key did (3.1). The sampler's
+  `!state.supported` branch is unreachable today (`decodeLsdjState` always answers supported, and `open` already refuses
+  a reader with no layout); it stays as the contract's answer.
 - **`debugOverlay.ts`**: `export const lsdjDebug = createSignal(false)` and the action `{ id: "lsdj.debug-overlay",
   label: "LSDj Debug Overlay", keys: ["`"], run: () => lsdjDebug.set(!lsdjDebug.get()) }`. Not persisted, off at start, as
-  `lsdjDebug.ts`'s header says it must be. `lsdj/ts/ui/index.ts`'s `defineUi` gains
-  `views: [hdView, overlayView], actions: [lsdjDebugOverlay]`.
+  `lsdjDebug.ts`'s header says it must be; `run` ignores `ctx.systemId`, so it works on the start screen as the backtick
+  did. `lsdj/ts/ui/index.ts`'s `defineUi` gains `views: [hdView, overlayView], actions: [lsdjDebugOverlay]`. The views
+  are declared with `satisfies WindowView` / `satisfies TileView` and take the narrowest host: `HdSession` takes
+  `Host<"systems">`, `hdView`'s `open` `Host<"files" | "png" | "systems">`, the overlays' `open` `Host<"files" |
+  "systems">`, so a test supplies a fake of those groups and no cast.
 
 ### 6.2 risa (`packages/risa/ts/ui/`)
 
@@ -541,16 +621,20 @@ The same two files for `risa-overlay` and `risa.debug-overlay` (`keys: ["`"]`):
 `decodeRisaState(ram, layout)` through `host.systems.readRam`, and the lines, `trackCell` and the 4X tempo rule are
 `RisaOverlay.tsx:203-211` verbatim. `when` is `hasRole(sys.roles, "risa")`, which is the scanner's one `ui/` hit
 (`useRisaRuntime.ts:306`) leaving the shell. `risa/ts/ui/index.ts` becomes `defineUi({ trackers: [RISA_TRACKER_UI],
-views: [overlayView], actions: [risaDebugOverlay] })`. Its package `tsconfig.json` extends the generated host-modules
-config (the generator enforces it for a package with views, 8.1); lsdj's already does.
+views: [overlayView], actions: [risaDebugOverlay] })`. The view and its action are declared as lsdj's are
+(`satisfies`, `Host<"files" | "systems">`), `open` refuses a system without the `risa` role, and `sample()` answers `null`
+for a RAM image too short to decode. risa's `tsconfig.json` is UNCHANGED: the generator asks for the generated
+host-modules extend of a package that lists examples or declares a UI host module (8.1), and risa's overlay is plain
+TypeScript that declares no React module, so it needs no React path (lsdj's extends it already, for its examples).
 
 ### 6.3 What leaves the shell
 
 `ui/screens/hd/` (both files), `ui/screens/grid/{LsdjOverlay, RisaOverlay, useLsdjRuntime, useRisaRuntime, lsdjDebug,
 DebugReadout}`, `uiViews`, `MenuContext.openLsdjHd`, `src/graph/defineExtension.ts`'s `ViewDefinition`, and the
 backtick code in `App.tsx` (`:34`, `:48`, `:205`). After it `ui/` outside `generated/` names no cart.
-What arrives: `ui/views/{viewSession.ts, PixelView.tsx, ReadoutView.tsx, ComponentView.tsx, ViewWindow.tsx,
-TileViews.tsx}` and `ui/input/appActions.ts`.
+What arrives: `ui/views/{viewSession.ts, useViewSession.ts, PixelView.tsx, ReadoutView.tsx, ComponentView.tsx,
+ViewWindow.tsx, TileViews.tsx}` and `ui/input/appActions.ts` (`useViewSession.ts` is the one effect and subscription the
+three renderers share, kept out of `viewSession.ts` so the controller stays React-free for `pnpm test`).
 
 ## 7. Behaviour edges
 
@@ -560,7 +644,10 @@ None changes audio, a saved file or a graph document. Each is listed with what w
   each of Keyboard Bindings and Gamepad Bindings, after `Cycle Instances (Back)`. The recorded menu matrix holds the
   bindings menus in every case (the 45 ids of each of the 35 cases in `test/menu/recorded-matrix.json`), so its diff is
   four new items per case (two per menu) and nothing else; it is re-recorded in stage 4, when the two real actions
-  arrive (stage 3's action is a test one and adds no product row), and the diff is the record.
+  arrive (stage 3's action is a test one and adds no product row), and the diff is the record. As built it is
+  `recorded-matrix.json` +700 lines, 0 deletions (35 cases x 2 menus x 2 items x 5 lines, in 70 hunks); the file was
+  not re-taken wholesale but had only the two bindings submenus' `children` spliced from a throwaway recorder, and a deep
+  comparison proved the spliced file equal to the full new record.
   Two tests change: `test/menu/leaves.test.ts:929` and `:987` (the capture-row counts, 11 -> 13).
 - **E2. Backtick is rebindable per overlay** (the point of decision 7). With the defaults a press still flips both
   overlays, as before; a user who rebinds or clears one has two independent toggles. The initial state is off for both.
@@ -568,16 +655,24 @@ None changes audio, a saved file or a graph document. Each is listed with what w
   id and reads the default; the file is not rewritten until an edit.
 - **E4. The overlay's z-order against the focus border and the render badge** (5.2): it depended on which mounted last;
   in its slot the strip always sits under both. Only where the strip overlaps the badge (both are at the tile's bottom)
-  can it be seen. Verify by `pnpm screenshot`.
+  can be seen. It holds for a `when` that flips under a live tile as well (one slot per registered view, 5.2). Verify by
+  `pnpm screenshot`, which the prototype did not run (section 12).
 - **E5. A role edit reopens the views of that system** (5.4): wider than the old hooks' dependencies, with no visible
-  effect while the instance menu hides the tile.
+  effect while the instance menu hides the tile. The one reachable visible case: linking instance B to LSDj instance A
+  from B's menu rewrites A's roles while A's tile still shows, so A's overlay reopens and blinks out for a frame.
 - **E6. `host.views.open` of an unknown id, a `tile` view's id, or a system the view's `when` rejects is reported and
-  opens nothing**; before, an unknown id was ignored silently (`uiHost.ts:31-37`).
+  opens nothing**; before, an unknown id was ignored silently (`uiHost.ts:31-37`). The wording of the first two lives in
+  `windowViewOrReport`, which App and the menu tests' fallback host both call.
 - **E7. A contributed action that throws is reported and the rest of its press still runs**; before, `toggleLsdjDebug`
   could not throw.
 - **E8. `extension.test.ts` loses its views expectation** with the seed (3.4); no production code changes for it.
+- **E9. A backtick the user bound to a shell action toggles the overlays AND runs that action.** Today App returns before
+  the key map on a backtick, so such a binding does nothing but toggle; once the toggles are contributed actions it is a
+  key bound to two actions like any other (5.5). Unreachable without that deliberate rebind and arguably a fix, a
+  controller decision (stage 3 review) rather than a behaviour held. A key bound to two SHELL actions keeps today's
+  single winner (5.5).
 
-Not edges, and why: the HD player's reads, its 15-frame poll and its frame skip are the old session's, moved; the
+Not edges, and why: the HD player's reads, its 16-frame SRAM poll and its frame skip are the old session's, moved; the
 overlays' lines are the old lines; the Esc and game-input behaviour under the HD view is the old behaviour (5.1); the
 window and tile test ids are the old ones.
 
@@ -587,7 +682,11 @@ window and tile test ids are the old ones.
 
 - **`tsconfig.base.json`** gains `"jsx": "react-jsx"`. It changes what `tsc` accepts and nothing a bundler emits:
   `tools/ts-options.js` names the compiler options each build uses (its header says why), and the UI build passes
-  `jsx: "automatic"` itself (`tools/build-ui.js:31`). Stage 1 proves it by hashing `build/ui/bundle.js` before and after.
+  `jsx: "automatic"` itself (`tools/build-ui.js:31`). The UI bundle is NOT byte-identical across the move and cannot be: esbuild heads each module with its path, so moving
+  a file moves bytes (1,997,971 -> 1,998,067 at stage 1). Stage 1 proves it by a normalised diff instead: with the
+  module-header comments dropped and the generated `import_*N` helper numbering canonicalised, the two bundles' line
+  multisets are equal but for two blank lines and `tagTestId`'s body, which reads `globalThis.__rp_tagTestId` (`native`
+  in `host/nativeHooks.ts` IS `globalThis`).
 - **`host-modules.json`** gains four entries, `surface: "ui"` each, with `types`: `react` and `react/jsx-runtime` ->
   `../../deps/dpf.js/deps/lv_binding_js/node_modules/@types/react` (and `/jsx-runtime`), `lvgljs` ->
   `../../deps/dpf.js/runtime/lvgljs/index.ts`, `lvgljs-ui` -> `ui/types/lvgljs-ui.d.ts` (the shell-owned 53-line slice
@@ -602,12 +701,16 @@ window and tile test ids are the old ones.
   (`include` gains `ui`) now that the two differ only by that line is left out (10.12).
 - **What each program needs.** The shell's program already includes `../*/ts`, so `packages/ui-react/ts/ui/*.tsx` enters
   it, and it needs `jsx` (base) and the four paths (`tsconfig.paths.json`); it also type-checks dpf.js's
-  `runtime/lvgljs/index.ts` as source, which only `tsconfig.ui.json` did before (a cost, and a place a strict-mode error
-  in code we do not own would appear: unverified, a stage 1 check, 10.13). Each package program (`pnpm -r typecheck`)
-  that imports `@retroplug/ui-react` or names a React module needs the generated host-modules config; the generator
-  refuses a package with views whose `tsconfig.json`'s parsed `extends` does not name it, by the check F4b wrote for
-  examples (`packages.mjs:1447-1462`, generalised from "has examples" to "has examples or views or declares a React host
-  module"). risa's `tsconfig.json` does not extend it today and gains the line in stage 4.
+  `runtime/lvgljs/index.ts` as source, which only `tsconfig.ui.json` did before, and passes (10.13: 906 ms against 921 ms
+  for the shell's program, so no measurable cost). Each package program (`pnpm -r typecheck`) that imports
+  `@retroplug/ui-react` or names a React module needs the generated host-modules config, and the generator refuses a
+  product package whose `tsconfig.json`'s parsed `extends` does not name it, by the check F4b wrote for examples
+  (`packages.mjs:1447-1462`, generalised as built from "has examples" to "has examples or declares a UI host module",
+  `why = examples ? "it lists examples" : uiHost ? "it declares a UI host module" : null`). The draft said "or views";
+  the generator cannot see a TypeScript `views` member, and a plain-TypeScript view needs no React path (decision 11), so
+  risa's `tsconfig.json` is unchanged. The check judges product packages only (it reads `contributionsOf`), so
+  `ui-react` and `ui-fixture`, which no product lists, are held by their own `tsc -p tsconfig.json`: without the extend a
+  `react` import does not resolve.
 - **The UI program** (`tsc --noEmit -p tsconfig.ui.json`) keeps its role: `ui/` plus what it imports.
 
 ### 8.2 Rule 3
@@ -626,6 +729,9 @@ window and tile test ids are the old ones.
 - RED first: a copy of `ui-react`'s shape with the `.tsx` under `ts/` (refused), one without the declaration (refused),
   one declaring them with the `.tsx` in `ts/ui/` (accepted), and a `ts/cli/` file importing `react` (refused as a
   surface error).
+  As built these are four boundary fixtures, `react-allowed`, `react-outside-ui`, `react-undeclared` and `react-cli-import`
+  (rules `jsx-surface` and `host-module`), with four cases in `boundaries-checker.test.ts`; the `.tsx`-outside-tree case
+  also draws the surface refusal for whatever React module that file names.
 
 ### 8.3 Rule 9 over `ui/`
 
@@ -652,14 +758,20 @@ entry no `ui/` file uses fails the check (as `SHELL_IMPORTS`' do, `shellStale`),
 | `@retroplug/tracker/assetCatalog` | the asset catalog the shell's Assets menus read |
 | `@retroplug/tracker/trackerIntegration` | the integration contract the shell resolves a cart's tracker by |
 
-That is the nine specifiers `ui/` names today less the cart ones, plus `ui-react`; the final list is what the RED run
-shows (the exact set depends on which of `core/systemRoles`' six uses stay, 10.14). The RED run is rule 9 over `ui/` on
-the tree before the moves: it must name the six cart specifier lines of section 1 and nothing else. The F4b ratchet
-(`test/boundaries/ui-cart-imports.test.ts` and `run-ui-cart-imports.mjs`) is deleted with its fixtures
-(`fixtures/ui-relative`, `fixtures/ui-device` become `shell-ui-forbidden` and `shell-ui-relative` for the rule); the
+That is the nine specifiers `ui/` names today less the cart ones, plus `ui-react`, and the RED run confirmed it: a grep
+of every `@retroplug/*` specifier under `packages/retroplug/ui` outside `generated/` gives these ten and nothing else
+(three of `core/systemRoles`' six uses remain; 10.14). The RED run is rule 9 over `ui/` on a fixture tree of the pre-move
+shape (the live tree is already clean after the moves): it is refused, where the rule's absence reported `status 0` for
+all three refusal trees. Both lists are module-private (`SHELL_IMPORTS` lost its `export`: the ratchet's runner was its
+only other reader), and the unused-entry check runs on the real tree only (a fixture tree states no list). The F4b ratchet
+(`test/boundaries/ui-cart-imports.test.ts` and `run-ui-cart-imports.mjs`) is deleted with its fixtures, which became
+the rule's: `ui-device` -> `shell-ui-forbidden` (a ui file naming `@retroplug/launchpad/client`), `ui-relative` ->
+`shell-ui-relative` (two files: one reaching another package's tree, one reaching `cli/` by `../cli/run`, asserted in
+walk order, and shown RED with `cli` added to the ui root's `reaches`), plus `shell-ui-unlisted` (a ui file naming
+`@retroplug/core/path`, which `SHELL_IMPORTS` carries and `UI_IMPORTS` does not: the case that proves the list is the
+UI's own) and `shell-ui-allowed` (the positive tree, holding the `src/` reach and the `ui/generated/` ignore). The
 allow-based rule covers what the ratchet's derived guard list did (a device pack with no app module is guarded because
-nothing but the list is allowed). `SHELL_IMPORTS` loses its `export` only if nothing else reads it (the ratchet's runner
-did).
+nothing but the list is allowed).
 
 ### 8.4 The literal scanner over `ui/`
 
@@ -669,6 +781,12 @@ before the moves it finds the one hit F4b recorded, `useRisaRuntime.ts`'s `"risa
 fixture `literals-ui` pins the walk of the third directory. After the moves the shell's `ui/` has none. The scanner stays
 blind to what F4b listed (device ids, node types of the surface nodes, non-console role kinds, identifiers).
 
+As built the real case and a fixture case share one constant, `SHELL_DIRS = "src,cli,ui"`, so the fixture case fails if
+`ui` leaves it; the RED was an extracted pre-move tree (`git archive 663632d16`), where `--dirs src,cli` misses the
+`"risa"` and `--dirs src,cli,ui` finds it; the `literals-ui` fixture carries both pre-move hits (`"risa"` and a
+`/roms/risa.nes` path) and is valid TypeScript (the shell's program includes the fixtures directory); and `run-scan.mjs`
+now skips a directory that does not exist, as the boundary check's walk does, because the fixture holds only `ui/`.
+
 ### 8.5 The bundle check
 
 `test/script/bundle-ui-split.test.ts`'s `isUi` (`:65-68`) matches `/packages/ui/` (the whole package) and
@@ -678,15 +796,18 @@ the kit sits under `packages/ui-react/ts/ui/`, which the second pattern covers.
 the render worker, the script nodes, the SDK, the dispatcher and the planner must carry no `ui-react` module, nor a
 package's views (they are in `ts/ui/`); the UI must still carry no CLI one. The component fixture package
 (`packages/ui-fixture/`, 8.6) is asserted ABSENT from the UI bundle's inputs: listing nothing is what keeps it out, and
-the assertion would catch a product list that picked it up.
+the assertion would catch a product list that picked it up. As built the case asserts the kit's modules are in the UI
+bundle and that `packages/ui-fixture/` is in NO bundle's inputs, not the UI's alone.
 
 ### 8.6 The fixture pack
 
 A real workspace package, `packages/ui-fixture` (`@retroplug/ui-fixture`; the name is the draft's, 10.10), private, listed
-by no product, with `retroplug.ui: "ts/ui/index.ts"` and `retroplug.host` of the four React modules, a
-`tsconfig.json` extending base and the generated host-modules config, and `ts/ui/{index.ts, Badge.tsx}`: a tile view
-`{ id: "ui-fixture-badge", placement: "tile", body: reactView(Badge) }` whose component draws a `Box` and a `Text`
-through `@retroplug/ui-react`. What it proves, and where:
+by no product, with `retroplug.ui: "ts/ui/index.ts"` and `retroplug.host` of `react` and `react/jsx-runtime` (it imports
+`react` for a type and uses JSX, and draws through the kit, so it names neither `lvgljs` nor `lvgljs-ui`, which would warn
+as unused; `ui-react`'s own manifest declares all four), a `tsconfig.json` extending base and the generated host-modules
+config, and `ts/ui/{index.ts, Badge.tsx}`: a tile view `badgeView` (`{ id: "ui-fixture-badge", placement: "tile", body:
+reactView(Badge) } satisfies TileView`) whose component draws a `Box` and a `TextEx` through `@retroplug/ui-react`, and
+the package default-exports `defineUi({ views: [badgeView] })`. What it proves, and where:
 
 - **it typechecks**: `pnpm -r typecheck` over a `.tsx` that uses `reactView`, `Box` and JSX (stage 1);
 - **rule 3 passes** with `retroplug.host` declaring the modules, and **a copy without the declaration, and a copy with
@@ -694,9 +815,13 @@ through `@retroplug/ui-react`. What it proves, and where:
 - **the generator accepts it**: a generator fixture tree (`test/packages/fixtures`) lists a package shaped like it and
   its expected `menus.ts` is held; a second fixture per refusal of 3.5 (two view ids, two action ids, a foreign prefix, an
   unknown key name, an unknown pad name, a collision with a shell default);
-- **it is never bundled into the product**: no product lists it, the UI bundle's inputs do not contain it (8.5), and
-  `pnpm check:boundaries` and the native boundary check both accept an unlisted package with a `ui` member
-  (unverified, stage 1: nothing scanned so far is known to walk every `packages/*/package.json`).
+- **it is never bundled into the product**: no product lists it, no bundle's inputs contain it (8.5), and
+  `pnpm check:boundaries` and the native boundary check both accept an unlisted package with a `ui` member (verified in
+  stage 1: the generator never reads an unlisted package; `check:boundaries` judges every `packages/*/package.json` with a
+  `ts/` tree, so it walks `ui-fixture` and `ui-react` under rules 1 to 8 with 0 refusals and 0 warnings; the native check
+  judges only the targets of packages `retroplug_package()` was called for, and the workspace-dir check asks for
+  `packages/retroplug/node_modules/@retroplug/<name>` only for a package the shell declares, which `ui-react` is and
+  `ui-fixture` is not).
 
 **What is not proved, said plainly.** The UI test binary embeds the production bundle (`test-ui/uiHarness.ts` bundles the
 tests, not the UI), so no `test:ui` case can render a `component` view until a real package ships one. The
@@ -708,8 +833,8 @@ component body whose `open`-equivalent is the mount) and by reading, not by pixe
 **Nothing is re-recorded but the edge:** the parity baseline (197 cases), the kit and render baselines, the device image
 hashes and pack bytes, the help texts, the battery listing, the recorded menu matrix except its bindings menus (E1),
 `test:ui`'s three view tests as they stand (`lsdj-hd`, `lsdj-overlay`, `risa-overlay`: the same test ids, texts,
-letterbox checks, Esc and backtick), BlipToaster and NESVJ. The UI bundle's hash is compared at stage 1 (no change
-expected) and its size reported at each stage.
+letterbox checks, Esc and backtick), BlipToaster and NESVJ. The UI bundle's size is reported at each stage; its hash is
+no oracle (8.1).
 
 New tests, by stage (a RED run first wherever the old code can be run):
 
@@ -751,7 +876,7 @@ a fix pass that rebased a stage 3 hunk into stage 2 broke the stage 2 tree).
 1. **`ui-react` and the typecheck.** The package with the widgets moved and the shell importing them; `jsx` in the base;
    the four host modules and their generated paths, `tsconfig.ui.json` reduced; the generator's views-extend check; rule
    3's `.tsx` refusal and the implicit `react/jsx-runtime` use; the bundle pattern; `ui-fixture`; `tagTestId` once. *Exit:*
-   no behaviour change; `pnpm typecheck` green in all programs; the UI bundle hash unchanged; `test:ui` unchanged; the
+   no behaviour change; `pnpm typecheck` green in all programs; the UI bundle's normalised diff clean; `test:ui` unchanged; the
    rule 3 cases RED then green; `check:boundaries` and the native boundary check accept the new packages.
 2. **The view contract.** `Readable` / `createSignal`, the view types, `files.readPrefix` and `systems.readRam`, the
    generator's `VIEWS` and its refusals 1 to 3, the shell's three renderers, the window and tile slots, the generic
@@ -774,57 +899,88 @@ a fix pass that rebased a stage 3 hunk into stage 2 broke the stage 2 tree).
    row), and this document's as-built section later. *Exit:* `check:boundaries` green with the lists as 8.3 gives them;
    every oracle of 8.7; BlipToaster, NESVJ, the Reaper suite and both SDL legs.
 
+As prototyped, the stages ran in this order and each was Opus-reviewed before the next began, with the review's fix pass
+folded into the commit it belongs to (by fixup and autosquash) before the next stage; one pass moved a hunk between
+stages (the `PixelView` guard, back into stage 2), and the last pass amended stage 5. Every stage's tree was then
+extracted after the final rewrite, proven equal to its commit by a fresh index read, and typechecked and tested alone.
+Section 12 has the record.
 ## 10. Risks and open questions
 
 Found against the code. Items 4, 6, 7, 9 and 10 carry a controller decision (in
-`.superpowers/f-split/decisions.md`, "Controller decisions (F4c draft)"); the rest stay open for the prototype to settle
-by reading the code.
+`.superpowers/f-split/decisions.md`, "Controller decisions (F4c draft)"); the rest were left for the prototype to settle
+by reading the code. Each item below ends with what the prototype answered, or says it stayed open and why.
 
 1. **`visible?: Subscribable<boolean>` does not typecheck as written.** `Subscribable` (`ui/ts/menu/subscribable.ts:10`)
    is non-generic, has no value, and is `{ emit, subscribe }`. The draft adds `Readable<T>` / `Signal<T>` (3.1) and
    keeps `Subscribable` as it is. If the user wanted `Subscribable` itself generalised instead, `createSubscribable`'s
    callers would see a type parameter they do not use.
+   *Built:* `Readable<T>` and `Signal<T>` in `@retroplug/ui/contribution`, `createSignal` beside `createSubscribable` in
+   `subscribable.ts`; `Subscribable` is untouched.
 2. **No gamepad button name table exists** (3.6), so the brief's "a pad name the binding tables do not know" has nothing
    to check against. The draft adds `GAMEPAD_BUTTON_NAMES` (SDL's set); the open part is whether a stick token (`leftx+`)
    may be an action default (the draft: no, the axis bus does not dispatch actions).
+   *Built:* `GAMEPAD_BUTTON_NAMES` in `keyCodes.ts`, SDL's 21 canonical button names, read off `SDL_CONTROLLER_BUTTON_*`; a
+   stick token stays out of it, so a default on one is refused.
 3. **The refusals are load-time throws in the generated module, not the generator.** The generator is `.mjs` and reads
    manifests; an action id, a view id and a key name live in TypeScript. The draft keeps F4b's mechanism (the generated
    module checks at load, the generator tests run it), which is also where F4b's `claimRow` and `checkRow` run.
+   *Built as drafted:* the generated module checks at load, and the generator tests run it through fixture trees, with the
+   fixture's `keyCodes` and `bindingMap` as shims to the real modules.
 4. **The reopen key is wider than today's** (5.4, E5): `roles` identity reopens every view of a system on any role write.
    If that is too wide, the alternative is an optional `reopenOn?(sys): string` on a view (the HD returns its ROM path
    and the lsdj-assets config; the overlays default to the ROM path), which is one more field in the contract and not in
    the brief. Whether `SystemsStore.setRoleConfig` replaces the `roles` array on every write is a stage 2 read of the setter.
+   *Answered:* `SystemsStore.setRoleConfig` replaces `roles` on every write (read in stage 2), so the wide key moves on
+   every role write. Kept as designed; `reopenOn?` was not built. E5 has one visible case.
 5. **`undefined` and `null` as "nothing moved" / "unreadable"** (3.1) extend the brief's `sample(): { lines, playing } |
    null` and `frame(): ... | null`; the overlays' and the HD view's current behaviour needs both.
+   *Built as drafted*, with the session hook three-state (5.4) so a renderer draws nothing for a session that has not
+   opened.
 6. **Contributed actions are not deferred under a menu modal** (5.5): a bindings-capture row armed, or a text prompt,
    still passes a backtick to the toggles, exactly as it does now. Gating them on `isMenuModalActive` as `OpenMenu`
    is would be arguably better and is a behaviour change; the draft keeps backtick's.
+   *Kept as drafted* (not gated on `isMenuModalActive`); the new E9 is the one behaviour this brings beyond backtick's.
 7. **A default that collides with a Game Boy button's default** (3.5): `Z`, `X`, `Enter` and the arrows would fire the
    action AND the joypad. Controller decision: refused too (check 6), since it changes no behaviour and only refuses a
    contribution. A user's own rebinding onto a joypad key is still allowed, as it is for the shell's actions today.
+   *Built:* check 6 compares resolved DPF codes, so `Return` collides with Enter.
 8. **Two flags instead of one** (decision 9, E2): after a rebind the two overlays can disagree, which is the freedom
    decision 7 chose.
+   *Stays, as E2.*
 9. **`views.open` checks `when`** (5.1) so the HD view's unsupported box means "a supported cart this build cannot
    read"; today it would also show for a system the row was never offered for. That check is the draft's.
+   *Built* (5.1), with the wording of the other refusals in `windowViewOrReport`.
 10. **The fixture package's name and kind** (`packages/ui-fixture`, a real workspace member rather than a tree under
     `test/`) is the draft's: a real package typechecks in the ordinary way and doubles as the worked example spec/06's
     "a view" step copies. A tree under `test/` would need its own program (the fixture trees are excluded from the
     shell's).
+    *Built as drafted* (`packages/ui-fixture`), and it doubles as the worked example spec/06's "a view" item names.
 11. **`test:ui` cannot render a component view or run any view case without a ROM fixture** (8.6, 8.7): the view
     lifecycle is proved by the pure controller test in CI and by the fixture-gated real cases locally, and CI runs none
     of the three existing view tests (all SKIP without the ROM).
+    *Stays.* The view lifecycle is proved by the pure controller tests in CI, by the packages' pure `ui-views` cases over
+    a fake host, and by four new fixture-gated `test:ui` cases beside the three existing ones (12); the pure runner now
+    injects `__RESOURCES_DIR__`, so `lsdj/ui-views` holds a pixel case that skips without the ROM (the one `ts` entry in
+    the skip baseline).
 12. **`tsconfig.ui.json` could be folded into `tsconfig.json`** (8.1): one `tsc` pass instead of two, at the cost of
     one program that includes `ui/` and the tests' harness alias. Left as built.
+    *Left as built:* `tsconfig.ui.json` is `{ "extends": "./tsconfig.json", "include": ["ui"] }`.
 13. **The shell program now type-checks dpf.js's `runtime/lvgljs/index.ts` as source** (8.1). `tsconfig.ui.json` already
     does under the same options, so it should pass; unverified, and the time cost is unmeasured.
+    *Answered:* it passes, and the shell's program is no slower measurably (906 ms against 921 ms).
 14. **The final `UI_IMPORTS` set** (8.3) depends on which uses of `core/systemRoles` (6 import lines today; three are in
     the files that leave) and `launchpad/enums` remain; the RED run decides.
+    *Answered:* the ten of the 8.3 table; three of `core/systemRoles`' six uses remain, and `launchpad/enums` stays.
 15. **Menu row ids with a dot** (`bind-act-lsdj.debug-overlay`): nothing found keys on an id's characters
     (`menu-ids.test.ts` checks uniqueness only), but the Menu's own use of an id was not read for a `split` on `.`;
     a stage 3 check.
+    *Answered:* nothing parses an id (`Menu.tsx` uses it as a key and in maps; `navTo` matches on the label), and a unit case
+    pins a dotted action id through `assertUniqueMenuIds`.
 16. **Not verified here, still open.** The Windows, macOS and arm64 builds of every new script and package (first on
     push); a real-host look at the tile z-order (E4); `reaper:editor` and the SDL legs for the hosted editor with the
     views moved.
+    *Still open, and why:* the Windows, macOS and arm64 builds (first on push); a real-host look at the tile z-order (E4, by
+    `pnpm screenshot`); `reaper:editor` and the SDL legs with the views moved. Section 12 lists what acceptance must run.
 
 **Corrections to the brief and the survey** (what the code showed): `ViewDefinition` is read by `Collected.views` and one
 test (3.4); `Subscribable<boolean>` does not exist (10.1); `bindingsChildren` is `menuDefs.ts:1191-1295` (1303 begins
@@ -834,6 +990,12 @@ id (10.3); `/packages/ui/` in the bundle check does not match `packages/ui-react
 matched by the second pattern (8.5); rule 3 cannot see `react/jsx-runtime` in a `.tsx` and needs the implicit-use clause
 (8.2).
 
+**Corrections the prototype made to the draft:** the generator's extend rule is "has examples or declares a UI host
+module", not "has views" (8.1); the UI bundle's hash is no oracle (8.1); `ui-react` does not depend on `core` (4.1);
+`hex2` is one export of `@retroplug/ui/format` (4.2, 6.1); `reactView` returns the contract's `ComponentBody` (4.2); the
+HD SRAM poll is a countdown of 15 and a period of 16 frames (6.1); a key bound to two shell actions keeps today's single
+winner (5.5); the tile slots are one per registered view (5.2).
+
 ## 11. What it leaves
 
 For `spec/07-remaining-work.md`'s F4c entry and the package split's closing note:
@@ -847,4 +1009,206 @@ For `spec/07-remaining-work.md`'s F4c entry and the package split's closing note
 - **Gamepad axis tokens as action bindings**, and a stick-driven action, if a package wants one (10.2).
 - **Windows, macOS, arm64 and CI** for F4c's scripts and packages, as F4a's and F4b's.
 - **Folding `tsconfig.ui.json`** into the shell's program (10.12), and widening or narrowing the reopen key (10.4), if the
-  stage 2 read shows either matters.
+  stage 2 read shows either matters. The read showed the key moves on every role write; E5's one visible case is a blink.
+- **Found while building, for later:**
+  - a real `component` view rendered on a host (the kind and its fixture ship; no `test:ui` case can draw one until a
+    product lists a package that has one);
+  - the generator judges product packages only, so `ui-react` and `ui-fixture` are held by their own `tsc` and not by the
+    extend check; a later change that wants the check over unlisted packages must say so;
+  - if the generator is ever taught to see a package's `views` (it reads manifests, not TypeScript), the extend rule
+    must be generalised to "has views" AND risa must gain the line with it (decision 11);
+  - gamepad axis tokens as action bindings (10.2), and the reopen key's width if the blink in E5 ever matters;
+  - LSDj's overlay sampler has an unreachable `!state.supported` branch (the reader always answers supported); if
+    the reader grows an unsupported decode the branch stops being dead;
+  - the `docs/*-plan.md` records and the dated audit docs still name the files this phase deleted; they are records of the trees
+    they describe and were not edited;
+  - `ReadoutSampler.signature` is optional and both overlays use the default (the lines and the flag); a later sampler whose
+    state moves without its lines changing can supply its own.
+
+## 12. As built (the prototype)
+
+The prototype is five commits on the design commit `663632d16`, one per stage, each built by DeepSeek Flash and reviewed
+by Opus, with each review's fix pass folded into the commit it belongs to by fixup and autosquash (one pass moved a hunk
+back from stage 4 into stage 2, and the last pass amended stage 5), so the five trees stand as final and a stage report's
+sha is older than the branch's. **The shas in this section are the PROTOTYPE's**: `d1352d6ef`, `eb20634ea`, `c956580c4`,
+`b841dc4e2` and stage 5, the commit that carries this section (so it cannot name its own sha: the tip of
+`proto/f4c-react`). The whole prototype is 176 files, +5,337 -973 over the design commit, leaving this document out,
+700 of the added lines the menu matrix's recorded JSON. Sections 1 to 11 above were corrected in place to what the build found; this section is the record
+of how, and the plan is written against it.
+
+**The stages**, by commit subject and current sha (files changed, lines added and removed):
+
+1. `d1352d6ef feat(f4c): the React kit is a package, and the typecheck follows it` (72, +877 -99): `packages/ui-react` with
+   the seven widget files moved and `tagTestId`, `reactView` and the entry new, the shell's 19 files importing it,
+   `jsx: react-jsx`, the four host modules and their generated paths, `tsconfig.ui.json` reduced, the generator's extend
+   check for a package that declares a UI host module, rule 3's `.tsx` placement and implicit `react/jsx-runtime` use
+   with four fixtures, the bundle pattern, `packages/ui-fixture`, the kit walk test.
+2. `eb20634ea feat(f4c): the view contract, its registry, and the shell's window and tile slots` (33, +1497 -93):
+   `Readable` / `Signal` / `createSignal`, the view types, `files.readPrefix` and `systems.readRam`, the generated
+   `VIEWS` with its first checks, `viewSession` and `useViewSession`, the three renderers, `ViewWindow` and `TileViews`,
+   the generic `views.open`, `ViewDefinition` deleted, the `PixelView` canvas guard.
+3. `c956580c4 feat(f4c): app actions, contributed bindings and the shell's dispatch` (22, +1024 -106): the action
+   contract, `GAMEPAD_BUTTON_NAMES`, the key maps over lists, the generated `ACTIONS` with its six checks, `runActions`,
+   `withActionDefaults`, the bindings menus' rows over a test action, `MenuContext.actions`.
+4. `b841dc4e2 feat(f4c): the LSDj HD player and the two debug overlays are the packages'` (51, +1611 -433): the moves
+   (`hdView.ts`, `overlayView.ts`, `debugOverlay.ts` in lsdj and risa), the shell's view files, `lsdjDebug`, `uiViews`
+   and `openLsdjHd` deleted, E1's re-record, the package `ui-views` cases and four fixture-gated `test:ui` cases.
+5. `feat(f4c): rule 9 and the literal scanner cover ui/, and the ratchet retires` (the tip; 38,
+   +449 -363 without this document): rule 9's third root and `UI_IMPORTS`, the scanner's third directory, the ratchet and its runner deleted, the fixtures, the docs.
+
+**What each review found, and what the fix pass changed.**
+
+1. *Stage 1 (with fixes, no Critical).* Important: `reactView` declared placeholder types that did not match the
+   contract's names (`ComponentViewProps` against `ViewComponentProps`) and `index.ts` published extras, so stage 2 would
+   not have been "the one file"; `badgeView`'s `placement` widened to `string`, so the one-line wiring would not
+   typecheck; and several files repeated `@retroplug/ui-react` on consecutive import lines. Minors: no case for what
+   `reactView` exists for, the kit walk's frozen count and overclaiming comments, stale comments, `surfacesOf` twice, two
+   em dashes on `spec/03` changed lines. Controller decision: the extend rule is "declares a UI host module, or has
+   examples". Fix pass: `ViewComponentProps` exported from `reactView.ts`, `badgeView` as `satisfies TileView`, one
+   import per file, a wrong-props `@ts-expect-error` case, the walk reading specifiers from `ts.preProcessFile`.
+2. *Stage 2 (with fixes, no Critical).* Important: `useViewSession` lagged what it rendered in both directions (every
+   open of a supported cart flashed the unsupported text for an effect, and a native `frame` could sample a closed
+   session); a `component` tile view ignored `visible`; the tile slots mounted conditionally on `when`, so the
+   `StableSlot` guarantee (and E4) held only while `when` did not flip; and stage 4 needed the click-through case this
+   stage could not run. Minors: `windowViewOf` unused, `PixelView` showing `unsupported` in a tile, package code
+   unguarded in render and per frame, comments saying process, `reactView` returning the whole `ViewBody`, an unchecked
+   cast in `openSession`, `checkView` assuming a body, a registry-order case that could not tell product order from
+   alphabetical, the commit message. Fix pass: the three-state hook with `shownSession` and a controller-side `read`, the
+   `visible` subscription at the slot for every kind, one slot per registered view with `when` inside it, the guards
+   (5.4, 3.1), `ComponentBody`, `isPixelSession` / `isReadoutSampler`, `checkView` naming the package, the `zeta`
+   product fixture.
+3. *Stage 3 (with fixes, no Critical).* Important: a key bound to two of the shell's own actions fired both, where the
+   old map's last binding won. Minors: the generated `ACTIONS` export unread, the `{ pkg, action }` type declared three
+   times, the code-not-name decision untested, `actions-bad-prefix` declaring a key that does not resolve, the refusals'
+   RED proving less than it read, a miscounted message, four lines over 120 columns, a read warning naming no view,
+   `APP_ACTIONS` newly exported. Controller decision: E9 accepted. Fix pass (RED shown): `addActionId` (one shell action
+   per key, later wins, contributed accumulate), `ContributedAction` once, the `Return` / Enter case, a refusal RED per
+   case's own reason, `read` naming the view and a dropped readout clearing.
+4. *Stage 4 (with fixes, no Critical).* The moves were faithful line for line and E1 was exactly the new rows. Important:
+   `overlay-actions` skipped after the press when risa was absent, which would mask a regression; and the `PixelView`
+   canvas guard, which stage 4 had to add because the real `lsdj-hd` found the bug, belonged to stage 2 (the plan must
+   never carry a broken intermediate: a stage that is not green on its own is a stage the plan cannot replay). Minors:
+   the menu fallback host's message, history in test titles, untested null and role branches, a `tjs.env` fixture read,
+   `HdSession` typed by the contract's whole host, `hex2` three times, descriptions. Fix pass (a rebase of stages 2 to 4):
+   the guard and one `PixelSession.frame` doc sentence folded into stage 2 with its message saying no stage 2 tier can
+   show the bug (reproduced: with the guard reverted, the unchanged `lsdj-hd` case fails on `distinctColors(snap) > 2`),
+   `overlay-actions` deciding its gate before the press, `windowViewOrReport`, the runner's `__RESOURCES_DIR__`, host
+   types as `Host<...>` groups with `satisfies`, `hex2` in `@retroplug/ui/format`, the descriptions.
+5. *Stage 5 and the whole prototype (with fixes, no Critical).* A fresh `git archive` of the tip typechecked with no
+   `build/` at all, and the REDs reproduce. Important: the ui root's "never `cli/`" was claimed and untested (change its
+   `reaches` to `[ui, src, cli]` and every test still passed); and `spec/06`'s checklist told a pack author to put
+   `@retroplug/ui-react` in `retroplug.host` (it goes in `dependencies`; `retroplug.host` takes only host modules) and
+   misstated the extend rule. Ten minors (stacked JSDoc, `spec/06`'s rule 9 prose and `spec/00`'s shell row naming two
+   trees, `spec/03`'s "LVGL primitives" section, `packages/ui`'s description, a fixture comment naming the retired
+   ratchet, the scanner's fixture case not failing if `ui` left the list, a commit message crediting a stage 4 hunk,
+   two typos). Fix pass (amending stage 5 only; stages 1 to 4 keep their shas): a `cli/run.ts` and a second ui file
+   importing it in `shell-ui-relative`, shown RED, the spec text, `SHELL_DIRS`, and `run-scan.mjs` skipping a missing
+   directory.
+
+**Departures from the design as committed (`663632d16`), and why.** Each is also corrected in place above.
+
+- **The UI bundle's hash is no oracle** (8.1, decision 12): esbuild heads every module with its path, so moving a file
+  moves bytes; stage 1 is held by a normalised diff (+96 bytes, 1,997,971 -> 1,998,067).
+- **The extend rule is "has examples or declares a UI host module"** (8.1, decision 11), not "has views": the `.mjs`
+  generator cannot see a TypeScript member, and a plain-TypeScript view needs no React path. risa's `tsconfig.json` is
+  unchanged. The check judges product packages only, so `ui-react` and `ui-fixture` are held by their own `tsc`.
+- **`@retroplug/ui-react` depends on `@retroplug/ui` alone** (4.1): `UiHost` is published by `@retroplug/ui/contribution`,
+  and a `@retroplug/core` entry no file imports draws `check:boundaries`' unused-dependency warning.
+- **`reactView` returns `ComponentBody`** and `ViewComponentProps` is the contract's, re-exported by the kit (4.2); the
+  stage 1 placeholders are gone.
+- **`ui-fixture` declares `react` and `react/jsx-runtime` only** (8.6): it names neither `lvgljs` nor `lvgljs-ui`.
+- **One `@retroplug/ui-react` import line per file** (4.2), after the stage 1 review.
+- **`hex2` is one export of `@retroplug/ui/format`**, re-exported by the kit (4.2, 6.1), not a copy per package, and
+  `ui` gained a `./format` export.
+- **A `useViewSession.ts` joined the shell's view files** (6.3), the one effect and subscription the renderers share, kept
+  out of `viewSession.ts` so the controller stays React-free.
+- **The session hook is three-state, and package code is guarded** (3.1, 5.4): a pending session draws nothing, a hide
+  drops the session in the render that sees it, `read` goes through the controller, and a throwing `when`, `visible`,
+  `open`, `frame`, `sample` or `signature` is reported once and contained. What the user sees on opening the HD view: the
+  black window box with nothing in it until the session opens, then the canvas.
+- **`PixelView` checks the canvas before it reads a frame, and shows `unsupported` in a window view only** (5.3); the
+  guard is stage 2's, found by `lsdj-hd` in stage 4 and folded back.
+- **One `StableSlot` per registered tile view, `when` inside the slot** (5.2), each subscribing to `visible` itself.
+- **`openSession` carries no generic cast;** the renderers narrow with `isPixelSession` / `isReadoutSampler` (5.4).
+- **`windowViewOrReport` is the one wording of the open refusals**, called by App's `openViewOf` and by the menu tests'
+  fallback host (5.1); until stage 4 an interim shell-side `lsdj-hd` entry stood in App.
+- **One shell action per key, today's winner** (5.5, decision 13); **E9** accepted (decision 14).
+- **Key collisions are compared by resolved DPF code** (3.5): `Return` collides with Enter.
+- **`buildKeyToAction(actions, contributed)`**, with the contributed ids passed in (`src/` must not reach `ui/`),
+  `runActions` taking the list as a parameter, `APP_ACTIONS` private (5.5).
+- **`MenuContext.actions` is the injection seam** for the bindings rows, and `ContributedAction` is declared once in the
+  contract (5.7, 3.5). The generated module imports `defaultBindingMap` too, and its fixture tree holds shims to the real
+  `keyCodes.ts` and `bindingMap.ts` (3.5).
+- **`withActionDefaults` always returns a new object** (5.6).
+- **The overlays carry no `signature`** (3.1, 6.1, decision 16): the default displays what the old key did.
+- **The HD SRAM poll is a countdown of 15, a period of 16 frames** (6.1): `readSram` at frames 0, 16 and 32, the old
+  cadence. `HdSession` is exported and the views take `Host<...>` groups and are declared with `satisfies` (6.1).
+- **LSDj's sampler `!state.supported` branch is unreachable** (6.1), recorded and kept.
+- **E1's record was spliced, not re-taken wholesale** (7): +700 lines, 0 deletions, proved equal to the full new record.
+- **The pure runner injects `__RESOURCES_DIR__`** (stage 4), so a pure-tier case may hold a fixture ROM; `lsdj/ui-views`
+  is the one `ts` skip-baseline entry (one documented skip). **`overlay-actions` gates on tile 1's instance-menu `Risa`
+  row before the press.**
+- **Rule 9 as built** (8.3): `UI_IMPORTS` is the table's ten; both lists are module-private; the stale-entry check runs on
+  the real tree only; the fixtures are `shell-ui-forbidden`, `shell-ui-relative` (with the `cli/` case),
+  `shell-ui-unlisted` and `shell-ui-allowed`.
+- **The scanner as built** (8.4): `SHELL_DIRS`, the `literals-ui` fixture with both pre-move hits, `run-scan.mjs`
+  skipping a missing directory, the RED an extracted pre-move tree.
+- **The docs pass touched more than the design's list** (stage 5): `AGENTS.md`, `spec/00`, `03`, `06`, `07`, `13`,
+  `docs/lsdj.md`, `docs/package-split.md`, `packages/ui/package.json`'s description and the two carts', two stale code
+  comments, and the comments of the two byte-frozen `test-ui` headers (comments only). The F4b and F4c designs, the
+  `docs/*-plan.md` records and the dated audit docs were not edited.
+
+Existing tests whose shape changed, each judged by a stage review: the 15 `openLsdjHd: () => {}` stubs (gone with the
+member), `test/input/keymap.test.ts`, `gamepad.test.ts` and `test/menu/leaves.test.ts` (a value became a one-element
+list; the capture-row counts 11 -> 13), `core/ts/test/host.test.ts` and `lsdj/ts/test/songImport.test.ts` (the
+`FilesHost` literals gained `readPrefix`), `test/graph/extension.test.ts` (loses its views expectation), and the two
+byte-frozen `test-ui` view headers (comments). `lsdj-hd`, `lsdj-overlay` and `risa-overlay` pass unchanged. No golden,
+baseline, digest, image or pack byte moved, and no `RP_*_UPDATE` was set but the one recorded edge (E1).
+
+**Verification.** A baseline run at `663632d16` and a run at every stage and at the tip were each the parallel kit
+(`tip-verify-par.sh`), every leg exit 0, with `pnpm sdl:controller` run alone (`SDL controller OK`); the tip's final run is
+`/tmp/f4cpro/s5fv-*`, and `compare.sh` against the baseline reports only the lines below. One kit run (stage 3's fix pass) failed once on
+`retroplug-launchpad-test`'s mid-drain swap case, a thread-timing assertion that passed alone and on the re-run (the same
+load sensitivity F4b recorded).
+
+| | baseline (`663632d16`) | prototype tip (`proto/f4c-react`) |
+| --- | --- | --- |
+| `pnpm test` | 219 files, 2286 cases | 226 files, 2359 cases (one documented skip: `lsdj/ui-views`) |
+| `test:ui` | 43 files, 49 cases | 47 files, 53 cases (the four new fixture-gated cases) |
+| `pnpm typecheck` | | plus `packages/ui-react` and `packages/ui-fixture` |
+| parity, native, the CI picture, the kit and render baselines, `render-chunks`, `render-cli` | recorded | identical |
+| `test:plugin`, tsan and asan legs, device check, box, device mode | recorded | identical |
+| images and the three `bliptoaster` pack byte hashes | recorded | identical |
+| recorded menu matrix | | +700 lines (E1), 0 deletions |
+| `check:boundaries`, `check:docs` | 0 refusals, 96 files clean | 0 refusals and 0 warnings, 97 files clean |
+| UI bundle | 1,997,971 bytes | 2,014,145 bytes |
+
+The cp, cli and render-worker bundles each grew 70 to 72 bytes (`filesHost`'s `readPrefix`); no other bundle moved. The
++73 cases over the baseline are the phase's own tests, less the retired ratchet's three; the four fixture-gated `test:ui`
+cases are `overlay-survives-menu`, `hd-view-closes`, `overlay-actions` and `overlay-click-through`. Each of the five
+stage trees was also extracted after the final rewrite, proven equal to its commit by a fresh index read (0 diff lines
+before and after the runs), and typechecked and tested alone (`menu`, `packages`, `hosts`, `ui`, `input`, `boundaries`,
+`source-literals`, the package `ui-views` files, each exit 0 where its file exists); the tip's was a true fresh-checkout
+typecheck with no `build/`. The fixture-gated `test:ui` cases ran at the final tree with `RETROPLUG_RESOURCES_DIR` set:
+`lsdj-hd`, `lsdj-overlay`, `risa-overlay`, `overlay-actions`, `overlay-survives-menu`, `hd-view-closes` and
+`overlay-click-through`, each 1 passed, 0 skipped.
+
+**What acceptance must run beyond the parallel kit** (the stage 5 review's list):
+
+- `pnpm typecheck` in a fresh `git archive` of the final tip BEFORE any build (CI's order), then `node
+  tools/check-doc-links.mjs`;
+- with the fixtures present (`RETROPLUG_RESOURCES_DIR`) and `RP_FAIL_ON_SKIP=1`: the seven fixture-gated `test:ui` view
+  and action cases, the pure `lsdj/ui-views` pixel case, and `pnpm test:native parity` (nothing should move, and that is
+  the only proof it did not);
+- `pnpm screenshot` for E4 (the strip under the render badge), and one manual look at an overlay with a render running;
+- `reaper:all`, `reaper:editor`, `reaper:editor-reopen`, `reaper:editor-autoload`, `reaper:two-instances` and
+  `reaper:two-instances-load` (the views and the module-level debug signals now live in package modules; each instance's
+  UI runtime must still own its own), and `reaper:params` / `reaper:params-clap` (the bindings menus grew);
+- `pnpm sdl:smoke`, and `pnpm sdl:controller` alone;
+- BlipToaster and NESVJ against the rebuilt CLI;
+- `pnpm check:boundaries`, with the ui->cli fixture shown RED then green;
+- the Windows build and `test.bat` via the user's Windows agent (the new packages, the generator's new checks and the
+  scanner's missing-directory skip), with macOS and arm64 owed as for F4a and F4b.
+
+The stage trees' proof script symlinks the worktree's `build/` into each tree; acceptance must not copy that symlink, or a
+bundle step of `pnpm test` there writes into the shared build.
````

Run: `sed -n '13377,14314p' docs/modular-graph-phase-f4c-plan.md | git apply --check`
Expected: nothing: the range is this block's diff and it applies cleanly. Run the same range without `--check` to apply it, which keeps trailing whitespace in context lines exactly as the prototype has it.

Run: `git hash-object docs/modular-graph-phase-f4c-design.md`
Expected: `045b5bbb9986a2ad667cb67151515204f223f4f6`: the blob 8987a27f3 holds for this file, so a retyped slip fails here rather than in a test that never reads the line.

- [ ] **Step 6: The red proofs**

The live tree is clean after Task 4, so each RED is a fixture tree of the pre-move shape or an extracted pre-move
tree. Four recorded runs, each quoted whole because each is short.

````text

> retroplug2-ui@0.0.0 test /workspaces/RetroPlug-main/cuttlefish
> node scripts/cmake-build.js tjs-cli retroplug-devtest-script-pack && node packages/retroplug/scripts/run-tests.mjs boundaries

[  0%] Built target GENHDR
[  0%] Built target everest
[  0%] Built target miniz
[  0%] Built target qjs
[  0%] Built target sqlite3
[  6%] Built target p256m
[  6%] Built target ada
[ 13%] Built target mimalloc-static
[ 20%] Built target uv_a
[ 46%] Built target mbedcrypto
[ 53%] Built target mbedx509
[ 53%] Built target mbedtls
[ 86%] Built target websockets
[100%] Built target tjs
[100%] Built target tjs-cli
Built target mqjs_stdlib
Built target rp_mqjs_stdlib-tool
Built target reflectcpp
Built target retroplug-mqjs-engine
Built target mqjs
Built target retroplug-node-sdk
Built target retroplug-mqjs-host
Built target retroplug-mqjs-run
Compiling the devtest device script pack (MicroQuickJS bytecode, 64- and 32-bit)
  ok: bundled 5 modules, all in the pack or the SDK
  ok: lowered to ES5 (40.8 KB)
  ok: no library calls outside MicroQuickJS's stdlib, in the pack or anywhere in the SDK
  ok: defined 3 node type(s) under Node
  ok: compiled to bytecode, 60416 bytes at 64-bit
  ok: the device's runtime, 19.7 KB lowered, compiled to 22324 bytes at 32-bit (the box's) and 32960 at 64-bit (the desktop's)
  ok: MicroQuickJS (deps/mquickjs, 6d4d7eb) runs the bytecode and matches Node byte for byte (4 lines)
  ok: the device's runtime lays its 3 node type(s) out as the pack does, for their default settings
  ok: the pack defines, creates and runs its nodes in its declared heap by the 3/2 check (57344 bytes, the box's context; 86016 at 64 bits; the check would pass 38184, and the run leaves 46336 bytes live at 64 bits; the box's boot line says what it takes there)
  ok: the device's runtime, which every host runs the nodes from, runs the same blocks to the same output (digest 39f5717a2110dec7) in its declared heap; the check would pass 36584 for it
device pack devtest OK (build/packages/devtest/script)
Built target retroplug-devtest-script-pack
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))

# boundaries/ui-kit-imports
# 9 kit modules, 0 unexported
# 0 old-path import(s)
# 36 kit binding(s), 0 the kit does not export
TAP version 13
1..3
ok 1 - every module of the kit's ts/ui tree is re-exported by its index
ok 2 - no shell file imports a moved module from its old path
ok 3 - every name the shell imports from the kit is one its index publishes
# 3 passed, 0 skipped, 0 failed

# boundaries/boundaries-checker
# packages/alpha/ts/index.ts: imports "@retroplug/beta", which is the beta package (packages/beta), and alpha does not declare it in its dependencies (its tests may also use devDependencies)
# packages/gamma/ts/index.ts: imports "../../retroplug/src/shell", which resolves into the shell (packages/retroplug), outside packages/gamma/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/delta/ts/index.ts: imports "zod", a third-party module, which delta does not declare in its dependencies (its tests may also use devDependencies)
# the packages' declared dependencies are a cycle: epsilon -> zeta -> epsilon
# packages/theta/native/src/x.cpp: includes "../../../other/native/src/y.hpp", which resolves into the other package (packages/other), outside packages/theta: a package's native sources include their own by path
# packages/iota/package.json: declares @retroplug/retroplug in its dependencies, which is the shell (packages/retroplug): no package may depend on the shell
# packages/iota/ts/index.ts: imports "@retroplug/retroplug", which is the shell (packages/retroplug): no package may depend on the shell
# packages/kappa/ts/index.ts:3:22: cannot be resolved: ./nope names no file
# packages/kappa/ts/other.ts: imports "zod", a third-party module, which kappa does not declare in its dependencies (its tests may also use devDependencies)
# 
# packages/alpha/ts/index.ts: imports "../../retroplug/src/a", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: imports "../../retroplug/src/b", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: imports "../../retroplug/src/c", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: a /// <reference path> "../../retroplug/src/a.ts", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/beta/ts/index.ts: a /// <reference types="node"> names @types/node, which beta does not declare in its dependencies (its tests may also use devDependencies)
# packages/delta/package.json: exports "./test/rig" at ./ts/test/rig.ts, which is a test module: a test file may use devDependencies, so publishing one lets another package's source reach them through it
# packages/epsilon/package.json: exports "./app" at ./app.ts, which is outside packages/epsilon/ts, so no check reads the code it publishes
# packages/eta/package.json: exports "./*" at ./*.ts, which can name files outside packages/eta/ts, so no check reads the code it publishes
# packages/missing/package.json: exports "." at ./ts/gone.ts, which names no file: the module moved or was never there, and the entry is the one thing still saying the package publishes it
# packages/zeta/package.json: exports "./*" at ./ts/*.ts, which can name a test module: a test file may use devDependencies, so publishing one lets another package's source reach them through it
# packages/alpha/ts/index.ts: imports "@retroplug/testing", which is the testing package (packages/testing), and alpha does not declare it in its dependencies (its tests may also use devDependencies)
# packages/alpha/ts/nes/find.ts: imports "../test/support", which is a test module of its own package, from a file that is not a test: a test file may use the package's devDependencies, so importing one from source hands them to every importer of the source
# packages/beta/package.json: declares app as "workspace:@retroplug/retroplug@*", which is the shell (packages/retroplug): no package may depend on the shell
# packages/beta/package.json: declares @retroplug/core as "link:../retroplug", which is the shell (packages/retroplug): no package may depend on the shell
# packages/beta/package.json: declares ghostdep as "npm:@retroplug/ghost@1.0.0", which names @retroplug/ghost, not a package in packages
# packages/alpha/package.json: declares @retroplug/ghost, which is not a package in packages
# alpha: declares @retroplug/gamma, which no file of its imports and no native target of it names
# packages/retroplug/src/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/src/index.ts: imports "../../alpha/ts/app", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/src/index.ts: imports "../../ui/App", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/cli/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/src/index.ts: imports "../cli/run", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/cli/evil.ts: imports "../ui/App", which resolves outside packages/retroplug/cli and packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# 
# 
# 
# packages/alpha/ts/index.ts: imports "fixture-lib", a cli host module, from a file that surface does not own: a cli host module is reached only from the package's examples/ or ts/cli/ trees
# packages/alpha/ts/index.ts: imports "fixture-lib", which is a host module, and alpha does not declare it in retroplug.host
# packages/alpha/examples/session.ts: imports "fixture-lib", which is a host module, and alpha does not declare it in retroplug.host
# packages/alpha/package.json: declares retroplug.host "ghost", which the shell's host-modules.json does not list
# alpha: declares the host module fixture-lib, which no file of its imports
# packages/alpha/ts/Badge.tsx: is a .tsx file outside packages/alpha/ts/ui/, the one tree rule 3 admits React into: a component belongs to the package's UI surface, whose files are the only ones a React host module is reachable from
# packages/alpha/package.json: has a .tsx file (packages/alpha/ts/ui/Badge.tsx) and does not declare react/jsx-runtime in retroplug.host: automatic JSX imports it with no specifier, so the declaration is the only place the dependency shows
# packages/alpha/ts/cli/tool.ts: imports "react", a ui host module, from a file that surface does not own: a ui host module is reached only from the package's ts/ui/ trees
# 0 allowed by EXCEPTIONS:
TAP version 13
1..38
ok 1 - an @retroplug import a package does not declare is refused, naming the file, the import and owner
ok 2 - a relative reach into the shell is refused, naming the file, the import and the shell
ok 3 - a third-party module a package does not declare is refused
ok 4 - a cycle in the declared dependencies is refused, naming the path
ok 5 - an #include that climbs out of the package is refused, naming the file, the include and the owner
ok 6 - a dependency on the shell is refused, declared in the manifest and imported alike
ok 7 - an import nothing resolves is reported by its file and specifier, and the package is still judged
ok 8 - a package that declares everything it reaches holds, its interface and its compiler included
ok 9 - type-only imports are judged: an import type, an export type ... from and an import("x").T alike
ok 10 - a triple-slash reference path into the shell is refused, and an undeclared reference types with it
ok 11 - publishing a test module, a module outside ts/, or one naming no file, is refused
ok 12 - a devDependency is a test file's: a source file that imports one is refused
ok 13 - source that imports its own package's test module is refused
ok 14 - an alias of the shell is refused, spelled workspace: or link:
ok 15 - an alias naming a package the workspace does not have is refused, naming the alias
ok 16 - a declared package that does not exist is refused on its own
ok 17 - a native target's path is a use and a CMake comment is not, which the warning distinguishes
ok 18 - a shell source importing a cart's module is refused, naming the file and the specifier (rule 9)
ok 19 - a shell source's relative import that lands outside src is refused (rule 9)
ok 20 - a shell source may import a contract module and its own, and src/generated is not judged (rule 9)
ok 21 - a cli source may import a contract module and reach src, and names no package it does not declare
ok 22 - a cli source may not import a cart or a console (rule 9 over cli/)
ok 23 - rule 9's relative half is per root: a src file reaches src, a cli file cli or src
not ok 24 - a ui source importing a cart's module is refused against the UI's own list (rule 9)
  ---
  expected 1, got 0
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:28:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:36:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:353:25)
  
  ...
not ok 25 - a contract module the shell's src may reach but the UI's list omits is refused there too (rule 9)
  ---
  expected 1, got 0
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:28:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:36:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:361:25)
  
  ...
not ok 26 - a ui source's relative import stays in ui/ or reaches src/, never cli/ (rule 9)
  ---
  expected 1, got 0
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:28:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:36:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:369:25)
  
  ...
ok 27 - a ui source may import a contract module from its list and reach src, and ui/generated is not judged (rule 9)
ok 28 - a host module the package declares is admitted, from the files its surface owns
ok 29 - a host module imported from a file its surface does not own is refused, naming the surface
ok 30 - a host module the package does not declare is refused, naming the file and the module
ok 31 - a host module imported by an example session is refused too, naming that file
ok 32 - a host declaration the shell's list does not have is refused
ok 33 - a host declaration nothing imports warns, as an unused dependency does
ok 34 - a .tsx under the package's ts/ui/ tree with the React host modules declared is admitted
ok 35 - a .tsx outside the package's ts/ui/ tree is refused, naming the file and the tree
ok 36 - a package with a .tsx and no react/jsx-runtime declaration is refused
ok 37 - a React import from a package's CLI tree is refused as a surface error
ok 38 - the real tree holds its boundaries with no exception left
# 35 passed, 0 skipped, 3 failed

# 1/2 test file(s) FAILED: boundaries/boundaries-checker
 ELIFECYCLE  Test failed. See above for more details.
````

Run: `sed -n '14329,14492p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `483856341cd48efa4f36bd7f00f4d36a524a6636d5a58325bd19668ab0c3fa22  -`: the log's own bytes (`/tmp/f4cpro/s5-red-rule9.log`), so the block above is the run and not a selection from it. The four new boundary cases run against the check BEFORE this task's rule: the three refusal cases report status 0 (nothing is refused) and the allowed tree passes trivially. The cases then refuse each with its own words (the green run is `/tmp/f4cpro/s5-green-rule9.log`).

````text
stale: check-boundaries: UI_IMPORTS names @retroplug/dsp/wav (a probe), which no file of packages/retroplug/ui imports: delete the entry
check-boundaries: 0 refusal(s), 1 stale exception(s) under .
exit=1
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))
exit=0
````

Run: `sed -n '14499,14503p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `6acab52b9ac95297a62a579946d19f6f4805cf1dadbd152aebb81ec2f4302b8b  -`: the log's own bytes (`/tmp/f4cpro/s5-red-ui-stale.log`), so the block above is the run and not a selection from it. The unused-entry failure, shown by adding one entry nothing imports: `UI_IMPORTS names @retroplug/dsp/wav (a probe), which no file of packages/retroplug/ui imports: delete the entry`. It is reverted.

````text

> retroplug2-ui@0.0.0 test /workspaces/RetroPlug-main/cuttlefish
> node scripts/cmake-build.js tjs-cli retroplug-devtest-script-pack && node packages/retroplug/scripts/run-tests.mjs boundaries

[  0%] Built target GENHDR
[  0%] Built target qjs
[  0%] Built target everest
[  0%] Built target miniz
[  0%] Built target ada
[  0%] Built target sqlite3
[  6%] Built target p256m
[ 13%] Built target mimalloc-static
[ 20%] Built target uv_a
[ 46%] Built target mbedcrypto
[ 53%] Built target mbedx509
[ 53%] Built target mbedtls
[ 86%] Built target websockets
[100%] Built target tjs
[100%] Built target tjs-cli
Built target rp_mqjs_stdlib-tool
Built target mqjs_stdlib
Built target reflectcpp
Built target retroplug-mqjs-engine
Built target mqjs
Built target retroplug-node-sdk
Built target retroplug-mqjs-host
Built target retroplug-mqjs-run
Compiling the devtest device script pack (MicroQuickJS bytecode, 64- and 32-bit)
  ok: bundled 5 modules, all in the pack or the SDK
  ok: lowered to ES5 (40.8 KB)
  ok: no library calls outside MicroQuickJS's stdlib, in the pack or anywhere in the SDK
  ok: defined 3 node type(s) under Node
  ok: compiled to bytecode, 60416 bytes at 64-bit
  ok: the device's runtime, 19.7 KB lowered, compiled to 22324 bytes at 32-bit (the box's) and 32960 at 64-bit (the desktop's)
  ok: MicroQuickJS (deps/mquickjs, 6d4d7eb) runs the bytecode and matches Node byte for byte (4 lines)
  ok: the device's runtime lays its 3 node type(s) out as the pack does, for their default settings
  ok: the pack defines, creates and runs its nodes in its declared heap by the 3/2 check (57344 bytes, the box's context; 86016 at 64 bits; the check would pass 38184, and the run leaves 46336 bytes live at 64 bits; the box's boot line says what it takes there)
  ok: the device's runtime, which every host runs the nodes from, runs the same blocks to the same output (digest 39f5717a2110dec7) in its declared heap; the check would pass 36584 for it
device pack devtest OK (build/packages/devtest/script)
Built target retroplug-devtest-script-pack
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))

# boundaries/ui-kit-imports
# 9 kit modules, 0 unexported
# 0 old-path import(s)
# 36 kit binding(s), 0 the kit does not export
TAP version 13
1..3
ok 1 - every module of the kit's ts/ui tree is re-exported by its index
ok 2 - no shell file imports a moved module from its old path
ok 3 - every name the shell imports from the kit is one its index publishes
# 3 passed, 0 skipped, 0 failed

# boundaries/boundaries-checker
# packages/alpha/ts/index.ts: imports "@retroplug/beta", which is the beta package (packages/beta), and alpha does not declare it in its dependencies (its tests may also use devDependencies)
# packages/gamma/ts/index.ts: imports "../../retroplug/src/shell", which resolves into the shell (packages/retroplug), outside packages/gamma/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/delta/ts/index.ts: imports "zod", a third-party module, which delta does not declare in its dependencies (its tests may also use devDependencies)
# the packages' declared dependencies are a cycle: epsilon -> zeta -> epsilon
# packages/theta/native/src/x.cpp: includes "../../../other/native/src/y.hpp", which resolves into the other package (packages/other), outside packages/theta: a package's native sources include their own by path
# packages/iota/package.json: declares @retroplug/retroplug in its dependencies, which is the shell (packages/retroplug): no package may depend on the shell
# packages/iota/ts/index.ts: imports "@retroplug/retroplug", which is the shell (packages/retroplug): no package may depend on the shell
# packages/kappa/ts/index.ts:3:22: cannot be resolved: ./nope names no file
# packages/kappa/ts/other.ts: imports "zod", a third-party module, which kappa does not declare in its dependencies (its tests may also use devDependencies)
# 
# packages/alpha/ts/index.ts: imports "../../retroplug/src/a", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: imports "../../retroplug/src/b", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: imports "../../retroplug/src/c", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/alpha/ts/index.ts: a /// <reference path> "../../retroplug/src/a.ts", which resolves into the shell (packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>; the shell not at all
# packages/beta/ts/index.ts: a /// <reference types="node"> names @types/node, which beta does not declare in its dependencies (its tests may also use devDependencies)
# packages/delta/package.json: exports "./test/rig" at ./ts/test/rig.ts, which is a test module: a test file may use devDependencies, so publishing one lets another package's source reach them through it
# packages/epsilon/package.json: exports "./app" at ./app.ts, which is outside packages/epsilon/ts, so no check reads the code it publishes
# packages/eta/package.json: exports "./*" at ./*.ts, which can name files outside packages/eta/ts, so no check reads the code it publishes
# packages/missing/package.json: exports "." at ./ts/gone.ts, which names no file: the module moved or was never there, and the entry is the one thing still saying the package publishes it
# packages/zeta/package.json: exports "./*" at ./ts/*.ts, which can name a test module: a test file may use devDependencies, so publishing one lets another package's source reach them through it
# packages/alpha/ts/index.ts: imports "@retroplug/testing", which is the testing package (packages/testing), and alpha does not declare it in its dependencies (its tests may also use devDependencies)
# packages/alpha/ts/nes/find.ts: imports "../test/support", which is a test module of its own package, from a file that is not a test: a test file may use the package's devDependencies, so importing one from source hands them to every importer of the source
# packages/beta/package.json: declares app as "workspace:@retroplug/retroplug@*", which is the shell (packages/retroplug): no package may depend on the shell
# packages/beta/package.json: declares @retroplug/core as "link:../retroplug", which is the shell (packages/retroplug): no package may depend on the shell
# packages/beta/package.json: declares ghostdep as "npm:@retroplug/ghost@1.0.0", which names @retroplug/ghost, not a package in packages
# packages/alpha/package.json: declares @retroplug/ghost, which is not a package in packages
# alpha: declares @retroplug/gamma, which no file of its imports and no native target of it names
# packages/retroplug/src/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/src/index.ts: imports "../../alpha/ts/app", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/src/index.ts: imports "../../ui/App", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/cli/index.ts: imports "@retroplug/alpha/app", which is not one of the contract modules the shell's src/ and cli/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/src/index.ts: imports "../cli/run", which resolves outside packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/cli/evil.ts: imports "../ui/App", which resolves outside packages/retroplug/cli and packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/retroplug/ui/probe.ts: imports "@retroplug/launchpad/client", which is not one of the contract modules the shell's ui/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/ui/evil.ts: imports "@retroplug/core/path", which is not one of the contract modules the shell's ui/ may reach (rule 9): a cart's or a console's code is reached through the generated registries
# packages/retroplug/ui/evil.ts: imports "../../fake/ts/index", which resolves outside packages/retroplug/ui and packages/retroplug/src and packages/retroplug/cli (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import
# packages/alpha/ts/index.ts: imports "fixture-lib", a cli host module, from a file that surface does not own: a cli host module is reached only from the package's examples/ or ts/cli/ trees
# packages/alpha/ts/index.ts: imports "fixture-lib", which is a host module, and alpha does not declare it in retroplug.host
# packages/alpha/examples/session.ts: imports "fixture-lib", which is a host module, and alpha does not declare it in retroplug.host
# packages/alpha/package.json: declares retroplug.host "ghost", which the shell's host-modules.json does not list
# alpha: declares the host module fixture-lib, which no file of its imports
# packages/alpha/ts/Badge.tsx: is a .tsx file outside packages/alpha/ts/ui/, the one tree rule 3 admits React into: a component belongs to the package's UI surface, whose files are the only ones a React host module is reachable from
# packages/alpha/package.json: has a .tsx file (packages/alpha/ts/ui/Badge.tsx) and does not declare react/jsx-runtime in retroplug.host: automatic JSX imports it with no specifier, so the declaration is the only place the dependency shows
# packages/alpha/ts/cli/tool.ts: imports "react", a ui host module, from a file that surface does not own: a ui host module is reached only from the package's ts/ui/ trees
# 0 allowed by EXCEPTIONS:
TAP version 13
1..38
ok 1 - an @retroplug import a package does not declare is refused, naming the file, the import and owner
ok 2 - a relative reach into the shell is refused, naming the file, the import and the shell
ok 3 - a third-party module a package does not declare is refused
ok 4 - a cycle in the declared dependencies is refused, naming the path
ok 5 - an #include that climbs out of the package is refused, naming the file, the include and the owner
ok 6 - a dependency on the shell is refused, declared in the manifest and imported alike
ok 7 - an import nothing resolves is reported by its file and specifier, and the package is still judged
ok 8 - a package that declares everything it reaches holds, its interface and its compiler included
ok 9 - type-only imports are judged: an import type, an export type ... from and an import("x").T alike
ok 10 - a triple-slash reference path into the shell is refused, and an undeclared reference types with it
ok 11 - publishing a test module, a module outside ts/, or one naming no file, is refused
ok 12 - a devDependency is a test file's: a source file that imports one is refused
ok 13 - source that imports its own package's test module is refused
ok 14 - an alias of the shell is refused, spelled workspace: or link:
ok 15 - an alias naming a package the workspace does not have is refused, naming the alias
ok 16 - a declared package that does not exist is refused on its own
ok 17 - a native target's path is a use and a CMake comment is not, which the warning distinguishes
ok 18 - a shell source importing a cart's module is refused, naming the file and the specifier (rule 9)
ok 19 - a shell source's relative import that lands outside src is refused (rule 9)
ok 20 - a shell source may import a contract module and its own, and src/generated is not judged (rule 9)
ok 21 - a cli source may import a contract module and reach src, and names no package it does not declare
ok 22 - a cli source may not import a cart or a console (rule 9 over cli/)
ok 23 - rule 9's relative half is per root: a src file reaches src, a cli file cli or src
ok 24 - a ui source importing a cart's module is refused against the UI's own list (rule 9)
ok 25 - a contract module the shell's src may reach but the UI's list omits is refused there too (rule 9)
not ok 26 - a ui source's relative import stays in ui/ or reaches src/, never cli/ (rule 9)
  ---
  expected ["packages/retroplug/ui/evil.ts: imports \"../../fake/ts/index\", which resolves outside packages/retroplug/ui and packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import","packages/retroplug/ui/sneaky.ts: imports \"../cli/run\", which resolves outside packages/retroplug/ui and packages/retroplug/src (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import"], got ["packages/retroplug/ui/evil.ts: imports \"../../fake/ts/index\", which resolves outside packages/retroplug/ui and packages/retroplug/src and packages/retroplug/cli (rule 9): a cart's or a console's code is reached through the generated registries the generator writes, and the shell's other trees are not a source's to import"]
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:28:15)
      at toEqual (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:41:20)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/boundaries/boundaries-checker.js:370:26)
  
  ...
ok 27 - a ui source may import its list's contract and reach src, and ui/generated is not judged (rule 9)
ok 28 - a host module the package declares is admitted, from the files its surface owns
ok 29 - a host module imported from a file its surface does not own is refused, naming the surface
ok 30 - a host module the package does not declare is refused, naming the file and the module
ok 31 - a host module imported by an example session is refused too, naming that file
ok 32 - a host declaration the shell's list does not have is refused
ok 33 - a host declaration nothing imports warns, as an unused dependency does
ok 34 - a .tsx under the package's ts/ui/ tree with the React host modules declared is admitted
ok 35 - a .tsx outside the package's ts/ui/ tree is refused, naming the file and the tree
ok 36 - a package with a .tsx and no react/jsx-runtime declaration is refused
ok 37 - a React import from a package's CLI tree is refused as a surface error
ok 38 - the real tree holds its boundaries with no exception left
# 37 passed, 0 skipped, 1 failed

# 1/2 test file(s) FAILED: boundaries/boundaries-checker
 ELIFECYCLE  Test failed. See above for more details.
````

Run: `sed -n '14510,14659p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `04759cbb37409f6b8351e2df64c83cea076e91ae5302e0a84d0bb4862dec5b23  -`: the log's own bytes (`/tmp/f4cpro/s5f-red-uicli.log`), so the block above is the run and not a selection from it. The `ui` root's "never `cli/`" half, pinned by a second fixture file: with `cli` added to the root's `reaches`, the case reports only the package reach instead of both, in walk order. The green run is `/tmp/f4cpro/s5f-green-uicli.log`.

````text

> retroplug2-ui@0.0.0 test /workspaces/RetroPlug-main/cuttlefish
> node scripts/cmake-build.js tjs-cli retroplug-devtest-script-pack && node packages/retroplug/scripts/run-tests.mjs source-literals

[  0%] Built target GENHDR
[  6%] Built target p256m
[  6%] Built target miniz
[  6%] Built target qjs
[  6%] Built target everest
[  6%] Built target sqlite3
[  6%] Built target ada
[ 13%] Built target mimalloc-static
[ 20%] Built target uv_a
[ 46%] Built target mbedcrypto
[ 53%] Built target mbedx509
[ 53%] Built target mbedtls
[ 86%] Built target websockets
[100%] Built target tjs
[100%] Built target tjs-cli
Built target rp_mqjs_stdlib-tool
Built target mqjs_stdlib
Built target reflectcpp
Built target retroplug-mqjs-engine
Built target mqjs
Built target retroplug-node-sdk
Built target retroplug-mqjs-host
Built target retroplug-mqjs-run
Compiling the devtest device script pack (MicroQuickJS bytecode, 64- and 32-bit)
  ok: bundled 5 modules, all in the pack or the SDK
  ok: lowered to ES5 (40.8 KB)
  ok: no library calls outside MicroQuickJS's stdlib, in the pack or anywhere in the SDK
  ok: defined 3 node type(s) under Node
  ok: compiled to bytecode, 60416 bytes at 64-bit
  ok: the device's runtime, 19.7 KB lowered, compiled to 22324 bytes at 32-bit (the box's) and 32960 at 64-bit (the desktop's)
  ok: MicroQuickJS (deps/mquickjs, 6d4d7eb) runs the bytecode and matches Node byte for byte (4 lines)
  ok: the device's runtime lays its 3 node type(s) out as the pack does, for their default settings
  ok: the pack defines, creates and runs its nodes in its declared heap by the 3/2 check (57344 bytes, the box's context; 86016 at 64 bits; the check would pass 38184, and the run leaves 46336 bytes live at 64 bits; the box's boot line says what it takes there)
  ok: the device's runtime, which every host runs the nodes from, runs the same blocks to the same output (digest 39f5717a2110dec7) in its declared heap; the check would pass 36584 for it
device pack devtest OK (build/packages/devtest/script)
Built target retroplug-devtest-script-pack
check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))

# source-literals/no-cart-literals
# the registry's ids: gb, gba, gg, nes, sms; extensions: .gb, .gba, .gbc, .gg, .nes, .sms; cores: mesen, sameboy; node types: rp.mesen.gba, rp.mesen.nes, rp.mesen.sms, rp.sameboy.gb; carts: bliptoaster, lsdj, mgb, risa, smsggdj
# platform/extension/core/node-type/cart literals: [{"file":"packages/retroplug/src/projectConfig.ts","value":"mgb"},{"file":"packages/retroplug/src/projectConfig.ts","value":"sameboy"},{"file":"packages/retroplug/src/projectConfig.ts","value":"mesen"},{"file":"packages/retroplug/src/projectConfig.ts","value":"bliptoaster"}]
TAP version 13
1..4
ok 1 - the shell's sources, cli/ and ui/ spell no platform, ROM extension or cart id
ok 2 - the walk reads a table's keys, a pattern and a template tail, not a comment or a property access
not ok 3 - the third directory is walked: the pre-move ui/ hit is found there
  ---
  expected [{"file":"packages/retroplug/ui/screens/grid/useRisaRuntime.ts","value":"risa"},{"file":"packages/retroplug/ui/screens/grid/useRisaRuntime.ts","value":"/roms/risa.nes"}], got []
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/source-literals/no-cart-literals.js:34:15)
      at toEqual (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/source-literals/no-cart-literals.js:47:20)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/source-literals/no-cart-literals.js:23088:20)
  
  ...
ok 4 - a whitespace string ending in an extension is prose, and a path is not
# 3 passed, 0 skipped, 1 failed

# 1/1 test file(s) FAILED: source-literals/no-cart-literals
 ELIFECYCLE  Test failed. See above for more details.
````

Run: `sed -n '14666,14727p' docs/modular-graph-phase-f4c-plan.md | sha256sum`
Expected: `476bfa3956c3c232bf63d8c53d9078bcf63946e2ba19d185afd9f86440ae5d36  -`: the log's own bytes (`/tmp/f4cpro/s5f-red-shelldirs.log`), so the block above is the run and not a selection from it. `SHELL_DIRS`' pin: with `ui` dropped from the constant the fixture case finds nothing (`got []`) while the real case still passes.

The scanner's RED is an extracted pre-move tree, because the live tree cannot show the hit after Task 4 moved it:

The scanner's guarded sets are the generated registry's, which the test prints before it scans: `# the registry's ids:
gb, gba, gg, nes, sms; extensions: .gb, .gba, .gbc, .gg, .nes, .sms; cores: mesen, sameboy; node types: rp.mesen.gba,
rp.mesen.nes, rp.mesen.sms, rp.sameboy.gb; carts: bliptoaster, lsdj, mgb, risa, smsggdj`. They are passed literally
below so the run needs no generated module in the pre-move tree.

Run: `bash -c 'D=/tmp/f4c-premove-scan; rm -rf "$D" && mkdir -p "$D" && git archive 663632d16 | tar -x -C "$D" && node packages/retroplug/test/source-literals/run-scan.mjs --platforms gb,gba,gg,nes,sms --extensions .gb,.gba,.gbc,.gg,.nes,.sms --cores mesen,sameboy --node-types rp.mesen.gba,rp.mesen.nes,rp.mesen.sms,rp.sameboy.gb --carts bliptoaster,lsdj,mgb,risa,smsggdj --dirs src,cli --root "$D"'`
Expected: one JSON line whose `literals` array is the four frozen migration keys and nothing else: `projectConfig.ts`'s `mgb`, `sameboy`, `mesen` and `bliptoaster`. The pre-move `ui/` is NOT scanned, so its one hit is missed, which is the RED. The log `/tmp/f4cpro/s5-red-scan.log` is the same pair of runs.

Run: `bash -c 'D=/tmp/f4c-premove-scan; [ -d "$D" ] || git archive 663632d16 | tar -x -C "$D"; node packages/retroplug/test/source-literals/run-scan.mjs --platforms gb,gba,gg,nes,sms --extensions .gb,.gba,.gbc,.gg,.nes,.sms --cores mesen,sameboy --node-types rp.mesen.gba,rp.mesen.nes,rp.mesen.sms,rp.sameboy.gb --carts bliptoaster,lsdj,mgb,risa,smsggdj --dirs src,cli,ui --root "$D" | grep -o useRisaRuntime.ts | head -1'`
Expected: `useRisaRuntime.ts`: with `ui` added the same scan finds the one hit F4b recorded, `"risa"` in that file, which is what makes the third directory the proof the design asks for. The live tree's `ui/` has none (the `source-literals` suite is green).

- [ ] **Step 7: Configure, install, build, and the bytes that must not move**

Run: `cmake -S . -B build`
Expected: exit 0: no manifest change, so the configure only re-reads the product list and the generator's inputs.

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and the lockfile is Task 1's: this task adds no dependency.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree: the UI bundle moves by this task's comment-only edits, and nothing else compiles differently.

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

- [ ] **Step 8: Run the suites**

Task 5's figures are the final verification (`/tmp/f4cpro/s5fv-*.log`, every leg exit 0, no retries), run at the
amended tip whose tree is this task's but for the design document's as-built section, which this task also lands (a
Markdown file no leg reads): `compare.sh` between this run and the run before the amendment is a MATCH on all 42
summary legs and 44 gated logs. The plan's own file makes `check:docs` 98, not the 97 every log records.

The verification is the parallel kit, one command, run in the foreground and waited for: it sets each leg's own
`RETROPLUG_RESOURCES_DIR`, skips `sdl:controller` (which claims system-wide ALSA ports) and logs one file per leg. Run
`pnpm sdl:controller` on its own afterwards, never another way, and compare the summary and the gated logs with the
baseline at the design commit.

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4c-v bash .superpowers/verify/tip-verify-par.sh`
Expected: exit 0 and `== every leg passed`, with no `/tmp/f4c-v-retries.txt` (a retry is a leg the kit re-ran; report it). The base's run is `/tmp/f4cpro/base-*` and every leg below quotes this task's own log.

Run: `pnpm sdl:controller`
Expected: exit 0: `SDL controller OK`, after fifteen `ok:` lines. The first ten are the bliptoaster pack's toolchain, which the SDL build runs and prints (the same ten the baseline's log has), and the last five are the harness's own legs, byte-identical to the baseline's. It sets `RP_MIDI_PORT_ALLOW` itself, from the two fakes' ready lines, and refuses a caller's list. The five:

````text
  ok: nothing selected, nothing claimed; unclaimed, the device's notes reach the shared stream
  ok: reclaimed before MIDI opens and audio starts; its notes kept out of the shared stream; released at quit
  ok: Programmer mode first, LED traffic, the release last
  ok: a real scan found and confirmed the device, saved it, and claimed it mid-session - its notes stop, others' do not, the shared output steps aside
  ok: a second standalone leaves the first one's device alone, and does not hear its pads
```` This is the one command in this plan that may touch a MIDI port at all, and it sets `RP_MIDI_PORT_ALLOW` from its two ALSA fakes itself.

Run: `bash .superpowers/verify/compare.sh /tmp/f4cpro/base /tmp/f4c-v`
Expected: exit 1 (anything differs), printing only the lines this task is expected to move: the `test` case and file counts, `ui`'s from Task 4, Task 1's two `typecheck` programs and the `measure` sizes (info only, never gated). `imagesha` and `packbytes` must be ABSENT from the diff, and every other leg's counts must be the baseline's. A difference the task does not name is a finding.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree. No compiler warning is expected: every stage's build log has none.

Run: `pnpm typecheck`
Expected: `Scope: 25 of 26 workspace projects`, then `packages/retroplug typecheck: Done`: every project with a `typecheck` script typechecks on its own tsconfig, and the shell's program covers every package's `ts/` as well and `packages/ui-react typecheck: Done` and `packages/ui-fixture typecheck: Done` (Task 1's two packages, unchanged). The baseline's scope is `Scope: 23 of 24 workspace projects` with 17 `Done` lines; this tree's is 25 of 26 and 19.

Run: `pnpm test`
Expected: `# 226 test file(s) passed (jobs=5)`. The runner's own line counts FILES; the case total is the last pass's table: "`test`: `ok=2286` / 219 files -> `ok=2359` / 226 files", since no runner prints one in this suite. The pool size it prints is the kit's `TV_TS_JOBS` (5); a hand run prints its own, so the file count is what must match.

Run: `pnpm test:plugin`
Expected: `# 13 plugin test binary(ies) passed (jobs=5)`: every binary exits 0, and the counts are (quoted as the log prints them, the engine-graph suite's assertion total and its eleven skips as that binary prints them):

````text
retroplug-mesen-test: `All tests passed (82321 assertions in 80 test cases)`
retroplug-audio-test: `All tests passed (378 assertions in 16 test cases)`
retroplug-watcher-test: `All tests passed (6 assertions in 2 test cases)`
retroplug-launchpad-test: `All tests passed (12863 assertions in 53 test cases)`
retroplug-classid-test: `All tests passed (10 assertions in 3 test cases)`
retroplug-plugin-test: `All tests passed (21 assertions in 6 test cases)`
retroplug-lottie-test: `All tests passed (7 assertions in 1 test case)`
retroplug-midi-test: `All tests passed (78 assertions in 24 test cases)`
retroplug-dynparams-test: `All tests passed (24 assertions in 7 test cases)`
retroplug-graph-test: `All tests passed (356662 assertions in 296 test cases)`
retroplug-sameboy-test: `All tests passed (1152140 assertions in 7 test cases)`
retroplug-device-mode-test: `All tests passed (1504408 assertions in 96 test cases)`
retroplug-engine-graph-test: `test cases: 336 | 325 passed | 11 skipped`
````

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1714598 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the fixture-gated cases run.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, none skipped, nothing re-recorded: 30 files and 250 cases (the reports' count), the device images' and the pack's bytes identical to the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed (jobs=12)`, with `# 144 case(s) SKIPPED in 65 file(s)` (its list of files follows) and the skip check passing: CI's view, its skips the baseline's.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 47 UI test file(s) passed (jobs=4)`. The case total is the stage 4 report's: "`ui`: `ok=49` / 43 files -> `ok=53` / 47 files", the four new fixture-gated cases (a case that cannot run SKIPS and is counted, and this leg runs WITH the fixtures).

Run: `pnpm test:node`
Expected: `pass 13`, `fail 0`.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks.

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`.

Run: `pnpm test:n8`
Expected: `All tests passed (1468 assertions in 104 test cases)` (`retroplug-n8-test`) and `All tests passed (17 assertions in 2 test cases)` (`retroplug-serial-test`), the baseline's: fakes only, no port is opened (the `[retroplug] N8 restore: port '/dev/ttyACM0'` line it prints is a test case's, over a fake port factory and a scratch n8.cfg; the baseline's run printed it too).

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'`
Expected: exit 0: `audio-render` 1, `render-guard` 1, `risa-render` (its three files) 6, 1 and 1, `risa-render-hff` 1, `risa-render-region` 1 and `sms-render` 6 passed (the counts are the reports' tables, each stage's).

Run: `bash -c 'for t in render-menu render-badge; do pnpm test:ui $t || exit 1; done'`
Expected: exit 0: `render-menu` and `render-badge`, one test file each and 1 passed.

Run: `pnpm test render`
Expected: `# 9 test file(s) passed (jobs=5)`: the render worker's own files.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool still builds.

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK` after the smoke's eight `ok:` lines (the build before them prints the bliptoaster pack's toolchain lines, which start `ok:` too). It runs under a scratch config dir with `RP_MIDI_PORT_ALLOW=RP_NO_PORT`, which matches nothing, so it claims and opens nothing.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three ThreadSanitizer binaries, built from this tree's sources (their objects are the prototype's until the first build here).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1698971 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"`
Expected: `All tests passed (263230 assertions in 80 test cases)`.

Run: `env TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'`
Expected: `All tests passed (1829 assertions in 53 test cases)`.

Run: `bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'`
Expected: `All tests passed (1499768 assertions in 51 test cases)`.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)`
Expected: exit 0: the three AddressSanitizer binaries, built from this tree's sources.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR=/workspaces/resources build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"`
Expected: `All tests passed (1684201 assertions in 172 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's).

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test`
Expected: `All tests passed (356662 assertions in 296 test cases)`.

Run: `env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test`
Expected: `All tests passed (1468 assertions in 104 test cases)`.

Run: `bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'`
Expected: `All tests passed (1504408 assertions in 96 test cases)`. The prototype's first full run failed this leg once, on the script pack's deadline case under ASAN on a loaded machine (no device-mode code touched by any stage), and its re-run was green; every stage's recorded run after it is green. If yours fails, re-run it ONCE and report both.

Run: `build/bin/retroplug-device-mode-test`
Expected: `All tests passed (1504408 assertions in 96 test cases)`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages; a consumer's suite does not change.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Run: `bash -c 'pnpm test boundaries && pnpm test source-literals'`
Expected: exit 0: `boundaries` is two files and 41 cases (`ui-kit-imports` 3, `boundaries-checker` 38) and `source-literals` one file and 4 cases, the scanner's third directory pinned by its fixture. The stage 5 fix pass's logs are `/tmp/f4cpro/s5f-final-boundaries.log` and `s5f-final-source-literals.log`.

Then the per-stage tree rule, which the prototype adopted after a broken intermediate commit: the tree this task
commits is extracted from the commit AFTER the commit step (so it is the sealed tree), proven equal to it by a fresh
index read from the commit, installed, generated, typechecked and tested on its own. Nothing here links this
worktree's `build/`; the pure runner reads the tjs binary through `RETROPLUG_TJS` instead.

Run: `bash -c 'D=/tmp/f4c-tree-5; rm -rf "$D" && mkdir -p "$D" && git archive 8987a27f3 | tar -x -C "$D" && echo extracted'`
Expected: `extracted`: the commit's tree, with no generated module in it (they are gitignored and `git archive` does not carry them) and no `build/`.

Run: `bash -c 'D=/tmp/f4c-tree-5; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree 8987a27f3 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-5-eq.txt; e=$?; rm -f $E; wc -l < /tmp/f4c-tree-5-eq.txt'`
Expected: `0`: the extracted tree is its commit's, line for line, read against a fresh index because this worktree's own index holds a later task's deletions. `deps` is excluded: its submodule directories are links, which is the next step's doing.

Run: `bash -c 'D=/tmp/f4c-tree-5; W=$PWD; for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done; cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo wired'`
Expected: `wired`: the archive's submodule paths are links to this worktree's checkouts (read only) and its own offline install wires the workspace's links from the COMMIT's lockfile, which is also what proves that lockfile installs.

Run: `bash -c 'D=/tmp/f4c-tree-5; T=/workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs; cd "$D" && pnpm -r typecheck && RETROPLUG_TJS=$T node packages/retroplug/scripts/run-tests.mjs menu && node packages/retroplug/scripts/run-tests.mjs packages && node packages/retroplug/scripts/run-tests.mjs hosts && node packages/retroplug/scripts/run-tests.mjs ui && node packages/retroplug/scripts/run-tests.mjs boundaries && node packages/retroplug/scripts/run-tests.mjs source-literals'`
Expected: exit 0: every package typechecks against its own tsconfig, and each named suite passes on this tree alone, with the worktree's tjs binary read through `RETROPLUG_TJS` and nothing written to this worktree. The per-tree record, which this plan's generator ran for all five trees (each extract equal to its commit before and after, 19 `Done` lines, every suite exit 0): tree 1 `menu` 17 files, `packages` 2, `hosts` 2, `boundaries` 3, `ui-react/reactView` 1 and `script/bundle-ui-split` 1; tree 2 `menu` 17, `packages` 2, `hosts` 2, `ui` 4 and `boundaries` 3; tree 3 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `input` 3, `config` 6 and `boundaries` 3; tree 4 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `lsdj/ui-views` 1 and `risa/ui-views` 1; tree 5 `menu` 18, `packages` 2, `hosts` 2, `ui` 4, `boundaries` 2 and `source-literals` 1.

Run: `bash -c 'D=/tmp/f4c-tree-5; W=$PWD; E=$(mktemp); GIT_INDEX_FILE=$E git read-tree 8987a27f3 && GIT_INDEX_FILE=$E git --work-tree="$D" diff --stat -- . ":!deps" > /tmp/f4c-tree-5-eq2.txt; rm -f $E; wc -l < /tmp/f4c-tree-5-eq2.txt'`
Expected: `0`: the install, the generator and the suites changed no tracked file, so the tree is still its commit's after the runs (the generated modules and the runner's build dir are ignored).

- [ ] **Step 9: Commit**

Run: `git status --short --untracked-files=all`
Expected: these lines, in this order, and nothing else:

````
 M AGENTS.md
 m deps/sameboy
 M docs/lsdj.md
 M docs/modular-graph-phase-f4c-design.md
 M docs/package-split.md
 M packages/retroplug/scripts/check-boundaries.mjs
 M packages/retroplug/test-native/lsdjPlaybackProbe.ts
 M packages/retroplug/test-ui/lsdj-overlay.test.ts
 M packages/retroplug/test-ui/risa-overlay.test.ts
 M packages/retroplug/test/boundaries/boundaries-checker.test.ts
R  packages/retroplug/test/boundaries/fixtures/ui-device/packages/launchpad/package.json -> packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/launchpad/package.json
R  packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/package.json -> packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/package.json
D  packages/retroplug/test/boundaries/fixtures/ui-device/packages/retroplug/ui/probe.ts
D  packages/retroplug/test/boundaries/fixtures/ui-relative/packages/fake/ts/index.ts
D  packages/retroplug/test/boundaries/fixtures/ui-relative/packages/retroplug/ui/evil.ts
D  packages/retroplug/test/boundaries/run-ui-cart-imports.mjs
D  packages/retroplug/test/boundaries/ui-cart-imports.test.ts
 M packages/retroplug/test/source-literals/no-cart-literals.test.ts
 M packages/retroplug/test/source-literals/run-scan.mjs
 M packages/ui-react/ts/ui/DebugReadout.tsx
 M packages/ui/package.json
 M spec/00-overview.md
 M spec/03-ts-layer.md
 M spec/06-build-test.md
 M spec/07-remaining-work.md
 M spec/13-subsystems.md
?? packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts
?? packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json
?? packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts
?? packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts
````

````bash
git add -- AGENTS.md docs/lsdj.md docs/modular-graph-phase-f4c-design.md docs/package-split.md \
  packages/retroplug/scripts/check-boundaries.mjs packages/retroplug/test-native/lsdjPlaybackProbe.ts \
  packages/retroplug/test-ui/lsdj-overlay.test.ts packages/retroplug/test-ui/risa-overlay.test.ts \
  packages/retroplug/test/boundaries/boundaries-checker.test.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/src/roles.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/generated/menus.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-allowed/packages/retroplug/ui/ok.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/shell-ui-forbidden/packages/retroplug/ui/probe.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/fake/ts/index.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/cli/run.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/evil.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-relative/packages/retroplug/ui/sneaky.ts \
  packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/package.json \
  packages/retroplug/test/boundaries/fixtures/shell-ui-unlisted/packages/retroplug/ui/evil.ts \
  packages/retroplug/test/source-literals/fixtures/literals-ui/packages/retroplug/ui/screens/grid/useRisaRuntime.ts \
  packages/retroplug/test/source-literals/no-cart-literals.test.ts \
  packages/retroplug/test/source-literals/run-scan.mjs packages/ui-react/ts/ui/DebugReadout.tsx \
  packages/ui/package.json spec/00-overview.md spec/03-ts-layer.md spec/06-build-test.md spec/07-remaining-work.md \
  spec/13-subsystems.md
git commit -F - <<'EOF'
feat(f4c): rule 9 and the literal scanner cover ui/, and the ratchet retires

The proof of docs/modular-graph-phase-f4c-design.md sections 8.3 and 8.4, the ratchet F4b left, and the prose pass.

Rule 9 gains a third root. packages/retroplug/ui is judged against UI_IMPORTS, a list of its own beside SHELL_IMPORTS:
the kit and the view contract are the UI's alone, so no src or cli file may name @retroplug/ui-react, and the control
plane has no use for the menu model. The ten entries are what ui/ actually names (ui/menu, ui/contribution, ui-react,
core/host, core/systemRoles, core/midiRouting, launchpad/enums, the three tracker contracts), each with the reason it
is a contract; a ui file may reach ui/ and src/ relatively (the UI is a surface over the stores the control plane owns)
and never cli/, and ui/generated/ is not judged, as src/generated/ is not. An entry no import of its root uses fails
the check, as an unused exception does. SHELL_IMPORTS loses its export: the deleted ratchet's runner was its only other
reader.

RED first, on the fixtures the live tree no longer can be: four new boundary trees (shell-ui-forbidden, a ui file naming
a device pack's client; shell-ui-unlisted, a ui file naming @retroplug/core/path, which is on the shell's list and not
the UI's; shell-ui-relative, a ui file reaching a console package by path beside a second one reaching the shell's
cli/; shell-ui-allowed, a contract import, a relative reach into src, and a ui/generated file naming a cart). Before the
rule, the three refusal cases report status 0 - nothing is refused - and the allowed tree passes trivially; the check
then refuses each with its own words. The ui root's "never cli/" half is pinned by the second file: with cli added to
what the ui root may reach, shell-ui-relative reports only the package reach instead of both, in walk order. The
unused-entry failure is shown by adding one entry nothing imports: "UI_IMPORTS names @retroplug/dsp/wav (a probe),
which no file of packages/retroplug/ui imports: delete the entry".

The literal scanner gains the third directory: `--dirs src,cli,ui` (no-cart-literals.test.ts), and a literals-ui
fixture holds the pre-move shape (a tracker id in a role test, a ROM extension in a path) so the third directory's walk
is pinned. RED over the tree before the moves (git archive 663632d16): with `--dirs src,cli` it finds the four frozen
migration keys and misses ui/screens/grid/useRisaRuntime.ts's "risa" - the one hit F4b recorded; with `ui` added it
finds it. The real tree's ui/ has none. Both cases pass one constant, SHELL_DIRS, so a directory leaving the list fails
the fixture case too (dropping `ui` leaves it no hits), and the walk skips a tree its fixture does not have, as the
boundary check's walk does.

The ratchet retires: test/boundaries/ui-cart-imports.test.ts and its run-ui-cart-imports.mjs are deleted, and its two
fixtures became the rule's (ui-device -> shell-ui-forbidden, ui-relative -> shell-ui-relative), with a
packages/retroplug/package.json each so the shell tree exists to judge. What the ratchet's derived package list did,
the allow-based rule does: nothing but the list is admitted.

The docs: AGENTS.md's ratchet paragraph becomes rule 9's three roots and their lists, its scanner sentence names all
three directories, the package list gains ui-react and ui-fixture, the manifest paragraph gains views and actions, and
the host-module sentence gains rule 3's .tsx admission. spec/06's "Adding a package" gains item 11 (a view: pixels and
readout in plain TypeScript, React through ui-react's reactView with @retroplug/ui-react in dependencies and the React
host modules in retroplug.host, the tsconfig extending the generated one, and where each kind of view is tested) and
item 12 (an action: the package-name prefix, the defaults, the refusals); its item 5, its extend rule and its rule 9
paragraph name the three roots, both lists and the five host modules over two surfaces, and state rule 3's .tsx
placement. spec/07's HD link text and its stale readers' list are fixed, the F4b proof's ratchet sentence is past
tense, and a new F4c entry records what was built and what it leaves (design section 11). docs/package-split's status,
spec/00's ui row with a ui-react row, spec/03 (the kit's primitives in ui-react split from the shell's own in ui/lvgl/,
the moved hooks, ui/views and appActions, the tile slots), docs/lsdj.md's overlay prose and spec/13's HD code line
follow, and packages/ui's description says what the package is. The two byte-frozen test-ui headers no longer describe
deleted hooks (comments only: ids, labels and texts are unchanged). The F4c design records what was built: sections 1
to 11 corrected in place, section 10's questions answered, and a new section 12, the prototype as built. The F4b
design and the *-plan.md records are untouched.

Verification: pnpm typecheck green in every program (19); check:boundaries clean with the final UI_IMPORTS (0 allowed, 0
warnings); pnpm check:docs 97 files clean; test boundaries green (2 files, 41 cases) and
source-literals green; the pure suite 226 files / 2359 cases (the ratchet's 3 cases gone, 5 added: 4 boundary trees and
the scanner's third directory; the one documented lsdj/ui-views skip) and test:ui 47 files / 53 cases; the full parallel
kit every leg exit 0 with no retry, and sdl:controller alone says SDL controller OK; images and pack bytes identical to
base. No em dash on an added line.
EOF
````

---

### Task 6: the acceptance run, and what is owed

This task commits nothing (unless a step fails and is fixed, in a commit of its own, named for what it fixes). It is the
phase's acceptance: the tree and a full build, the SERIAL kit's 43 legs, the three oracle families under their require
flags, the seven fixture-gated `test:ui` view and action cases and the pure `lsdj/ui-views` pixel case, both device
images and the pack's bytes, the SDL legs and `pnpm screenshot`, the Reaper parameter runs and the whole Reaper suite, a
fresh clone that typechecks before it builds, the boundary and docs checks, both consumer suites with BlipToaster's own
typecheck, and the bundle module diff base against tip. Its figures are compared with the baseline recorded at
`663632d16` (`/tmp/f4cpro/base-summary.txt` and `base-*.log`) and with the final verification at the prototype's tip
(`/tmp/f4cpro/s5fv-summary.txt` and `s5fv-*.log`). **There is no hardware leg: no step may touch the NES, the N8, the
Pico box, the probe, the Zoom L6 or any MIDI interface, and the hardware safety rules in this plan's header govern every
step here too.**

The baseline and the final run were both the parallel kit's, 47 summary lines each, every leg exit 0, with `pnpm
sdl:controller` run alone. The controller's record states the baseline: `Baseline at 663632d16 (parallel kit): legs exit 0, sdl:controller alone OK`. The final run is `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=/tmp/f4cpro/s5fv bash .superpowers/verify/tip-verify-par.sh` with every leg exit 0 and no retries. The two summaries are identical
line for line once the wall time is stripped, which the second step checks.

- [ ] **Step 1: The tree, and the build**

Start where the branch must be: `feature/modularize` at Task 5's commit, a clean tree but the expected modified
submodule, and no untracked derived file. The five tasks' commits and this plan's docs commit are this phase's;
nothing else should be there.

Run: `bash -c 'git rev-parse --abbrev-ref HEAD; git rev-parse --short=8 HEAD; git status --short --untracked-files=all'`
Expected: `feature/modularize`, then this tree's Task 5 commit (the five tasks' last, its sha is the one the last step's commit printed), then ` m deps/sameboy` and nothing else. A different branch, a dirty file or an untracked one is a finding to report before any leg runs.

The build is the whole tree from the state it is in, and the two sanitizer trees are built from the same sources.

Run: `cmake -S . -B build`
Expected: exit 0: the product list, every package's manifest, the generator's inputs and the UI's entry read.

Run: `cmake --build build -j$(nproc)`
Expected: exit 0, the whole tree (the plugin's three formats, the standalone, the host, the CLI, the SDL binary, the bundles, the generated modules). No compiler warning: no stage's build log has one.

Run: `cmake --build build --target retroplug-render-host-test -j$(nproc)`
Expected: exit 0: the manual render tool builds.

Run: `pnpm install --frozen-lockfile --offline`
Expected: exit 0, and `pnpm-lock.yaml` is unchanged: Task 1's lockfile, which is the design commit's blob plus the phase's three links (the two packages and the shell's dependency).

Run: `git hash-object pnpm-lock.yaml`
Expected: `e2cd7faa9f446b847414406017de095d668b3116`: the blob Task 5's commit holds, which is Task 1's. Nothing this task does may change it.

- [ ] **Step 2: The verification script, whole, and its 43 legs**

The kit is `.superpowers/verify/tip-verify.sh`, the same 43 legs the parallel script runs, in the same order. Write it
to a scratch path of its own (never into the repository), run it with `TVL` naming the log prefix, and keep both the
summary and every leg's log: the figures below are the final verification's, and a difference is a finding except
where the rules above say otherwise. The script is quoted whole because the set of legs IS the acceptance, and its
`HERE` makes `norm-sha.sh` and `measure.sh` the kit's own.

````bash
#!/usr/bin/env bash
# tip-verify.sh: the SERIAL full verification run, root-relative and timed.
#
#   TV_ROOT=<repo>      the tree to verify (default: the repo this script lives in, ../..)
#   TVL=<prefix>        the log prefix (summary: <prefix>-summary.txt, legs: <prefix>-<leg>.log)
#   TV_SKIP_SDLCTL=1    skip the sdlctl leg (it claims system-wide ALSA MIDI ports)
#
# Every leg's summary line is "<leg> exit <code> <secs>s", so the "<leg> exit <code>" prefix existing greps use is
# untouched. Every leg runs under `nice -n 19 ionice -c3` (a shared box), inherited by its children. Scratch dirs are
# unique per run.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TV_ROOT="${TV_ROOT:-$(cd "$HERE/../.." && pwd)}"
export TV_ROOT
cd "$TV_ROOT" || exit 2
L=${TVL:-/tmp/tv-verify}
RES=${RETROPLUG_RESOURCES_DIR:-/workspaces/resources}
run() {
  local n=$1; shift
  local start end rc
  start=$(date +%s)
  nice -n 19 ionice -c3 "$@" > "$L-$n.log" 2>&1
  rc=$?
  end=$(date +%s)
  echo "$n exit $rc $((end - start))s" >> "$L-summary.txt"
}
: > "$L-summary.txt"

run build cmake --build build -j$(nproc)
run typecheck pnpm typecheck
run test pnpm test
run plugin pnpm test:plugin
run egfix env RETROPLUG_RESOURCES_DIR="$RES" build/bin/retroplug-engine-graph-test
run parity env RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR="$RES" pnpm test:native parity
run native env RETROPLUG_RESOURCES_DIR="$RES" pnpm test:native
run nativeci env RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native
run ui env RETROPLUG_RESOURCES_DIR="$RES" pnpm test:ui
run node pnpm test:node
run clits pnpm test:cli-ts
run docs pnpm check:docs
run n8 pnpm test:n8
S="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1"
run tsanbuild cmake --build build-tsan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)
run tsaneg env TSAN_OPTIONS="$S" RETROPLUG_RESOURCES_DIR="$RES" build-tsan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"
run tsang env TSAN_OPTIONS="$S" build-tsan/bin/retroplug-graph-test "[dsp],[dsp-nodes],[dsp-resample],[dsp-buffer-nodes],[resource],[resources]"
run tsann8 env TSAN_OPTIONS="$S" build-tsan/bin/retroplug-n8-test
run tsanlp bash -c 'cmake --build build-tsan --target retroplug-launchpad-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-launchpad-test'
run tsandevmode bash -c 'cmake -S . -B build-tsan >/dev/null && cmake --build build-tsan --target retroplug-device-mode-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-device-mode-test "[script-pack]"'
run asanbuild cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test retroplug-n8-test -j$(nproc)
run asaneg env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" RETROPLUG_RESOURCES_DIR="$RES" build-asan/bin/retroplug-engine-graph-test "[host-io],[systems],[b2],[alloc],[devices],[capability],[n8-node],[n8-adapters],[providers],[launchpad],[offline],[engine-jobs]"
run asang env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test
run asann8 env ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-n8-test
run asandevmode bash -c 'cmake --build build-asan --target retroplug-device-mode-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-device-mode-test'
R=$PWD
BT=$(mktemp -d "${TMPDIR:-/tmp}/tvpar-bt.XXXXXX")
VJ=$(mktemp -d "${TMPDIR:-/tmp}/tvpar-vj.XXXXXX")
run bt bash -c "mkdir -p '$BT/build' '$BT/retroplug-cli' && cp -r /workspaces/bliptoaster/src '$BT/' && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map '$BT/build/' && cp -r /workspaces/bliptoaster/retroplug-cli/tests '$BT/retroplug-cli/' && cd '$BT' && $R/build/bin/retroplug-cli test retroplug-cli/tests --rom build/bliptoaster.nes; e=\$?; rm -rf -- '$BT'; exit \$e"
run vj bash -c "mkdir -p '$VJ/build' '$VJ/retroplug-cli' && cp -r /workspaces/nesvj/src '$VJ/' && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map '$VJ/build/' && cp -r /workspaces/nesvj/retroplug-cli/tests '$VJ/retroplug-cli/' && cd '$VJ' && $R/build/bin/retroplug-cli test retroplug-cli/tests --rom build/nesvj.nes; e=\$?; rm -rf -- '$VJ'; exit \$e"
echo DONE >> "$L-summary.txt"
run kitbase env RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline
run kitedges env RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile
run asankit bash -c 'cmake --build build-asan --target retroplug-audio-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-audio-test "[dither]"'
run sdl pnpm sdl:smoke
if [ "${TV_SKIP_SDLCTL:-0}" = 1 ]; then
  echo "sdlctl skipped (validation)" >> "$L-summary.txt"
else
  run sdlctl pnpm sdl:controller
fi
echo DONE2 >> "$L-summary.txt"
run rhtbuild cmake --build build --target retroplug-render-host-test -j$(nproc)
run rendertests bash -c 'for t in audio-render render-guard risa-render risa-render-hff risa-render-region sms-render; do RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native $t || exit 1; done'
run renderui bash -c 'pnpm test:ui render-menu && pnpm test:ui render-badge && pnpm test render'
run renderbase env RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR="$RES" pnpm test:native render-baseline
run renderchunks env RETROPLUG_RESOURCES_DIR="$RES" pnpm test:native render-chunks
run devcheck pnpm device:check
run devbox pnpm device:box
run devmode build/bin/retroplug-device-mode-test
echo DONE3 >> "$L-summary.txt"
run boundaries pnpm check:boundaries
run imagesha bash -c "bash '$HERE/norm-sha.sh' \$(find build-device build-box -name '*.elf' -not -path '*CMakeFiles*' -not -name 'bs2_default.elf' 2>/dev/null | sort)"
run packbytes bash -c 'sha256sum build/packages/bliptoaster/script/*.bin'
run measure bash "$HERE/measure.sh"
echo DONE4 >> "$L-summary.txt"
````

Run: `mkdir -p /tmp/f4c-accept; sed -n '15172,15254p' docs/modular-graph-phase-f4c-plan.md > /tmp/f4c-accept/tip-verify.sh`
Expected: exit 0: the script is the block above, byte for byte. Its sha256 is `6bbacc869898d38fb1c121a7c47df91e42d8217e8ab7670676a12bf0a8809ea4  -`. The two scripts it reaches through its own directory are the kit's own: copy them beside it, as the next command does.

Run: `bash -c 'cp .superpowers/verify/norm-sha.sh .superpowers/verify/measure.sh /tmp/f4c-accept/'`
Expected: exit 0: `norm-sha.sh` and `measure.sh` are beside the extracted script, whose `HERE` is then `/tmp/f4c-accept`.

Run: `TV_ROOT=$PWD TVL=/tmp/f4c-accept/tv bash /tmp/f4c-accept/tip-verify.sh`
Expected: the last leg exits 0, and `/tmp/f4c-accept/tv-summary.txt` is the 43 `exit` lines and the four markers, every exit 0. Run it in the foreground and wait: about 20 minutes of run time after a warm build, longer cold. This SERIAL run differs from the baseline's PARALLEL summary in exactly one line: it runs `sdl:controller`, so `sdlctl skipped (validation)` reads `sdlctl exit 0` there.

Run: `bash -c 'diff <(sed -E "s/ [0-9]+s$//; s/^sdlctl .*/sdlctl (ran)/" /tmp/f4cpro/base-summary.txt) <(sed -E "s/ [0-9]+s$//; s/^sdlctl .*/sdlctl (ran)/" /tmp/f4c-accept/tv-summary.txt); echo "diff exit $?"'`
Expected: `diff exit 0` and nothing printed: 47 lines, the same legs in the same order, every exit 0 and the four markers. The two `sed`s strip the wall times and fold the one line the serial run adds. This plan's generator ran the diff over the baseline's and the final PARALLEL summaries and got `diff exit 0`, so a difference here is the serial run's own. A leg that fails is found, fixed and re-run; a flaky leg is re-run once and both results reported (the launchpad swap case is the precedent).

Then the figures, leg by leg, against the baseline and the final run. Each is the line the leg's own log prints (the
engine-graph assertion totals move from run to run, which every stage measured; the case counts must match), read from
`/tmp/f4cpro/s5fv-*.log` unless a bullet says otherwise:

- **`typecheck`**: `Scope: 25 of 26 workspace projects`, `packages/retroplug typecheck: Done`, `packages/ui-react
  typecheck: Done` and `packages/ui-fixture typecheck: Done`. The baseline is `Scope: 23 of 24 workspace projects`
  with 17 `Done` lines: Task 1's two packages are the difference.
- **`test`**: `# 226 test file(s) passed (jobs=5)` and `# 1 case(s) SKIPPED in 1 file(s): lsdj/ui-views(1)`. The
  case total is the last pass's table, "`ok=2359` / 226 files"; the baseline's `# 219 test file(s) passed` and 2286
  cases. The +73 cases are the phase's own tests less the retired ratchet's three, and 226 files is 219 plus the new
  files less the ratchet's one.
- **`plugin`**: `# 13 plugin test binary(ies) passed (jobs=5)`, the baseline's 13 binaries and their counts (the
  engine-graph suite's `336 | 325 passed | 11 skipped` among them). Nothing native changed this phase, so every
  count is the baseline's.
- **`egfix`**: `All tests passed (1714598 assertions in 336 test cases)` (the case count is exact; the assertion
  total moves from run to run, as the ground rules say, and this is the prototype's), the baseline's 336 cases.
- **`parity`** (strict, fixtures, `RP_FAIL_ON_SKIP=1`): `# 30 native test file(s) passed (jobs=12)`, 30 files and
  250 cases, 0 skips: the baseline's, and no hash moved.
- **`native`** (fixtures): `# 172 native test file(s) passed (jobs=12)` with `# 2 case(s) SKIPPED in 2 file(s):
  dsp-bench(1), nes-bench(1)`.
- **`nativeci`** (no fixtures, the CI picture): `# 172 native test file(s) passed (jobs=12)` with `# 144 case(s)
  SKIPPED in 65 file(s)` and the file list that follows (the baseline's, file for file).
- **`ui`**: `# 47 UI test file(s) passed (jobs=4)`, 53 cases (the baseline is 43 files and 49 cases; Task 4's four
  fixture-gated cases are the difference).
- **`node`, `clits`, `docs`, `boundaries`**: `pass 13` / `fail 0`, `PASS: retroplug-cli TypeScript harness`,
  `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the
  prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked
  Markdown file `check-doc-links` walks., and `check-boundaries: the packages hold their boundaries (0 allowed by
  EXCEPTIONS, 0 warning(s))`.
- **`n8`**: `All tests passed (1468 assertions in 104 test cases)` and `All tests passed (17 assertions in 2 test
  cases)`, fakes only, no port opened.
- **`kitbase`, `kitedges`, `asankit`**: `# 3 passed, 0 skipped, 0 failed`, `# 9 passed, 0 skipped, 0 failed` and the
  dither tag's `All tests passed`: the baseline's, under `RP_KIT_GOLDEN_REQUIRE_TARGET=1`.
- **`sdl`**: `SDL smoke OK` (under `RP_MIDI_PORT_ALLOW=RP_NO_PORT`).
- **`rhtbuild`, `rendertests`, `renderui`, `renderbase`, `renderchunks`, `devcheck`, `devbox`, `devmode`**: the
  render-host tool builds, the six render filters pass with the reports' counts, `render-menu` / `render-badge` /
  `pnpm test render` pass, the render baseline is `# 5 passed, 0 skipped, 0 failed` with `render-chunks` the same,
  the device check and box print their trap and size lines, and the device-mode binary's `All tests passed`.
- **`tsanbuild`, `tsaneg`, `tsang`, `tsann8`, `tsanlp`, `tsandevmode` and the five asan legs**: every binary builds
  from this tree's sources and every suite passes.
- **`bt`, `vj`**: `PASS: 57/57 test file(s) ok` and `PASS: 19/19 test file(s) ok`, the baseline's.
- **`imagesha`, `packbytes`, `measure`**: the three normalised image hashes and the pack's three bytecodes are
  IDENTICAL to the baseline's (the fifth step checks them against their full strings), and `measure` is info only:
  the UI bundle is 2014145 bytes against the baseline's 1997971, the cp, cli and render-worker bundles each 70 to 72
  bytes larger from `filesHost`'s `readPrefix`, and no other bundle moved (the tenth step proves the module diff).

- [ ] **Step 3: The three oracle families, under their require flags**

`tip-verify.sh` runs them, but a phase that changes no behaviour is accepted only on a run where the exact bytes are
DEMANDED, so run them again by name with their flags on: each refuses to pass off a foreign host rather than comparing
a tree with itself.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=12)`, 250 cases, 0 skips (the reports' count), and no hash moved.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources build/bin/retroplug-engine-graph-test`
Expected: `All tests passed (1714598 assertions in 336 test cases)` (the case count is exact; the assertion total moves from run to run, as the ground rules say, and this is the prototype's): the case count is exact and the assertion total is the run's.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-baseline`
Expected: `# 3 passed, 0 skipped, 0 failed`.

Run: `RP_KIT_GOLDEN_REQUIRE_TARGET=1 pnpm test:native kit-compile`
Expected: `# 9 passed, 0 skipped, 0 failed`.

Run: `RP_FAIL_ON_SKIP=1 RP_RENDER_GOLDEN_REQUIRE_TARGET=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-baseline`
Expected: `# 5 passed, 0 skipped, 0 failed`, every output WAV's hash and size the recorded one.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native render-chunks`
Expected: `# 5 passed, 0 skipped, 0 failed`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 172 native test file(s) passed`, then the CI picture's skip line: `# 144 case(s) SKIPPED in 65 file(s)` and the file list, every case the fixtures would run counted as a skip with its reason. This is the run CI does, so it is the one a missing fixture cannot make green by accident.

- [ ] **Step 4: The seven fixture-gated `test:ui` cases, and the pure pixel case**

With the fixtures present and `RP_FAIL_ON_SKIP=1`, the seven cases must RUN and pass, not skip. Each is its own file
and process; the first three are the pre-existing view tests, byte-unchanged; the other four are Task 4's.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui lsdj-hd`
Expected: exit 0, `ok 1 - the LSDj HD player opens from the instance menu and Esc returns to the grid` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui lsdj-overlay`
Expected: exit 0, `ok 1 - the LSDj overlay is hidden by default and toggles on/off with backtick` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui risa-overlay`
Expected: exit 0, `ok 1 - the risa overlay is hidden by default and toggles on/off with backtick` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui overlay-survives-menu`
Expected: exit 0, `ok 1 - the LSDj overlay survives the menu opening and closing` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui hd-view-closes`
Expected: exit 0, `ok 1 - the HD view closes when its system is removed` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui overlay-actions`
Expected: exit 0, `ok 1 - one backtick toggles both overlays, and rebinding one leaves the other` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui overlay-click-through`
Expected: exit 0, `ok 1 - clicking an unfocused tile focuses it while the LSDj overlay shows` and `# 1 passed, 0 skipped, 0 failed`: the case RAN over the real cart, which is what `RP_FAIL_ON_SKIP=1` demands (the stage 5 fix pass's log re-ran all seven at the final tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test lsdj/ui-views`
Expected: exit 0, `# 5 passed, 0 skipped, 0 failed` and `# 1 test file(s) passed`: the package's five cases, the HD cadence, the two open refusals, the overlay's lines, the action's toggle, and the pixel case that reads the fixture ROM and paints the 792x576 surface. Without the fixture the pixel case SKIPs (it is the one `ts` entry in the skip baseline); the recorded run with the fixture is `/tmp/f4cpro/s4-test-lsdj-views-fixture.log`.

- [ ] **Step 5: The device images and the pack's bytes**

The task's own device step, here as the acceptance's own, with the three normalised image hashes written out in full
(the baseline's and the final run's logs are the same file, `cmp` clean):

Run: `cmake --build build --target retroplug-bliptoaster-script-pack -j$(nproc)`
Expected: exit 0: the pack's toolchain, its `ok:` lines ending `device pack bliptoaster OK (build/packages/bliptoaster/script)`, and the pack's three bytecodes rewritten from this task's TypeScript.

Run: `sha256sum build/packages/bliptoaster/script/*.bin`
Expected: three lines, the pack's three bytecodes, in this order (the baseline's and the final run's, byte for byte, the pack being compiled from TypeScript this phase does not change):

````text
066c7980302ef37833e5776e0d20d69566f32afdfe239d9dbeb5c82859c13157 build/packages/bliptoaster/script/bliptoaster.device64.bin
fd1daf9b6128581ea0074a05a08ddc8cf3d9af26f5d3cbec09a5894807b92459 build/packages/bliptoaster/script/bliptoaster.mqjs32.bin
b5017c7c9bef8b85c882eab4b734dfc7e379941b3e0df8c055325d5738cfa489 build/packages/bliptoaster/script/bliptoaster.mqjs64.bin
````

Run: `pnpm device:check`
Expected: exit 0: `-- rp-device-check: the link trap wraps 87 symbols`, the size table by owner ending `total 305620 2004 422279` (the three numbers, 305620, 2004 and 422279, are what must match; the log pads them), and the probe, `rp-device-trap-probe`, failing to link as it must, naming its own object:

````text
  CMakeFiles/rp-device-trap-probe.dir/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

(The `TinyUSB submodule has not been initialized` hint above it is `/opt/pico-sdk`'s own: the check needs no USB, which is why it uses that SDK.)

Run: `pnpm device:box`
Expected: exit 0: `build-box: PICO_SDK_PATH (/opt/pico-sdk) has no TinyUSB submodule; using /workspaces/pico-sdk, as loop.sh does` first (this devcontainer's `PICO_SDK_PATH` names an SDK with no TinyUSB), then `== the plan exporter (desktop: retroplug-export-plan)` (the script builds the exporter in the desktop build by name: it is in no default target), then for each variant, PIO-USB first and `RP_NATIVE_USB=ON` second, `-- n8-host: the link trap wraps 87 symbols`, the link of `n8_host.elf`, and its probe failing as it must:

````text
  CMakeFiles/n8_host_trap_probe.dir/workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp.o: in function `main':
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:11:(.text.startup.main+0x4): undefined reference to `__wrap__Znwj'
  /workspaces/RetroPlug-main/cuttlefish/packages/graph-runtime/device-check/trap-probe.cpp:12:(.text.startup.main+0x14): undefined reference to `__wrap_malloc'
````

and the image's sizes: `== pio: flash 199536, data 46120, bss 120600 bytes; 191960 of the flash runs from RAM; the load image ends 245656 bytes into flash; RAM 358704 bytes in all` and `== native: flash 199920, data 32084, bss 114368 bytes; 192344 of the flash runs from RAM; the load image ends 232004 bytes into flash; RAM 338824 bytes in all`.

An image carries the day it was built: the Pico SDK's standard binary info stamps it with `__DATE__`
(`pico_standard_binary_info`, since nothing sets `PICO_NO_BI_PROGRAM_BUILD_DATE`), once, a fixed-length string. So the
check counts that string (it must be exactly one) and hashes a copy with it normalised to 4 November 2026, as
`.superpowers/verify/norm-sha.sh` normalises it for the baseline's and every stage's run; nothing else may differ, and
the sizes the device scripts print do not move with the date. The bytes also rest on the build's inputs, so a hash
compares only an image built from this worktree's path, against the same SDK checkouts (`/opt/pico-sdk` for the check,
`/workspaces/pico-sdk` for the box), the same Pico-PIO-USB checkout (`/workspaces/Pico-PIO-USB`) and
`arm-none-eabi-g++` 13.2.1.

Run: `bash -c 'R="(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}"; for e in build-device/rp-device-check.elf build-box/pio/n8_host.elf build-box/native/n8_host.elf; do arm-none-eabi-objcopy -O binary $e /tmp/f4c-img.bin && LC_ALL=C grep -a -c -E "$R" /tmp/f4c-img.bin && LC_ALL=C sed -E "s/$R/Nov  4 2026/" /tmp/f4c-img.bin | sha256sum; done; rm -f /tmp/f4c-img.bin'`
Expected: `1` then `38665e2c0199a5972a9accdb6851f30422a6d208246ce857435fd893214ee997  -`, and `1` then `4b4f9abc43a2a22b7bc105b8a248743a5a7304b859d93a216be35be3c5f863bb  -`, and `1` then `af36f223ac227be4b55c84ad3ddf66f51c1716a6c382646a536f0a5ac678f595  -`.

Run: `bash -c 'cmp /tmp/f4cpro/base-imagesha.log /tmp/f4cpro/s5fv-imagesha.log && cmp /tmp/f4cpro/base-packbytes.log /tmp/f4cpro/s5fv-packbytes.log && echo identical'`
Expected: `identical`: the baseline's and the final run's image and pack logs are the same bytes. This plan's generator checks that too, so a retyped hash cannot reach these lines.

- [ ] **Step 6: The SDL legs, and `pnpm screenshot` for E4**

Run: `pnpm sdl:smoke`
Expected: `SDL smoke OK`, under the smoke's own scratch config dir and `RP_MIDI_PORT_ALLOW=RP_NO_PORT` (which matches nothing). It opens no port.

Run: `pnpm sdl:controller`
Expected: exit 0, `SDL controller OK`, after the fifteen `ok:` lines the kit's own log prints (ten are the bliptoaster pack's toolchain, five are the harness's legs). **This is the only command that may touch a MIDI port, and only the two ALSA fakes it sets `RP_MIDI_PORT_ALLOW` for itself; never run it with the variable preset and never any other MIDI-enumerating harness.**

Run: `pnpm screenshot`
Expected: exit 0, the harness's two lines: `screenshot: /tmp/retroplug.png (<N> bytes, sha1 <12 hex>)` and `idle reclaim: the editor's idle freed a plan the audio thread handed back`. This plan's generator ran it once at the design tree (the start screen) and saw `screenshot: /tmp/retroplug.png (4306 bytes, sha1 01ea08d6b401)` and that second line; the size and sha are the run's own, and this step is about the harness running at all.

**E4's look is a human step, described and not pixel-compared.** The default four-second capture photographs the start
screen, so it cannot show the overlay strip; to see E4, load a project (so a tile and, with a render started from
`System > Render`, its badge exist), toggle the overlay with the backtick, and look at the tile's bottom: the strip
must sit UNDER the focus border and UNDER the render badge, at the same place whichever mounted last, and a click on
an unfocused tile must still focus it through the strip's slot. `tools/run-standalone.sh <path> <seconds> <interval>`
with `tools/standalone-key.sh` (DISPLAY and `RETROPLUG_WINDOW_NAME`) drives it and writes the PNG. No run of it at the
tip with a project is recorded, so this plan quotes no figure for the look; the strip's PLACE is also held by code
(one `StableSlot` per registered view, after the dim and before the border) and by `overlay-survives-menu` and
`overlay-click-through`, which is what the phase can pin headlessly.

- [ ] **Step 7: The Reaper legs**

The views and the module-level debug signals now live in package modules, so each instance's UI runtime must still own
its own, and the bindings menus grew two rows: a host that draws them is the acceptance's own question. `pnpm
reaper:params` and `pnpm reaper:params-clap` read the plugin's parameter names through ReaScript (the only proof a
host acts on the re-read flag), the three editor legs cover the hosted editor and session restore, and `pnpm
reaper:all` runs the whole Reaper leg.

Run: `pnpm reaper:params`
Expected: PASS, the parameter count unchanged before and after the click on `Load mGB` (the per-ROM CC labels replace the generic pool, and the COUNT holds). The run is not in CI and needs a real Reaper. No F4c stage ran it, so this plan quotes no number for it; the run's own line carries the count (earlier phases recorded 2725 VST3).

Run: `pnpm reaper:params-clap`
Expected: PASS, the parameter count unchanged before and after the click (CLAP). Same caveat: the count is the run's own line (earlier phases recorded 644 CLAP).

Run: `pnpm reaper:editor && pnpm reaper:editor-reopen && pnpm reaper:editor-autoload`
Expected: each PASS: the hosted plugin editor renders, a close and reopen still shows the project, and a project already in the control plane shows on a float (the single-store graph the editor and the control plane share).

Run: `pnpm reaper:two-instances && pnpm reaper:two-instances-load`
Expected: both PASS: N instances and N editors load and the host survives, and loading a ROM through instance 1's editor with instance 2 already open keeps the ReaScript defer heartbeat moving (the freeze the second leg exists to catch). Neither is in CI.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm reaper:all`
Expected: exit 0, each of its checks PASS or SKIP where a fixture it needs is absent (the runner counts 15: 9 renders and 6 editor/host, as AGENTS.md records), in isolated JACK servers and config dirs by job tag (`tools/run-reaper-suite.sh`). **Without `RETROPLUG_RESOURCES_DIR` the LSDj and risa fixtures SKIP**, so the set supplies it. It is not in CI and no stage ran it.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither consumer repository was edited by the legs above.

- [ ] **Step 8: A fresh clone, typechecked before it builds**

A tree this plan's steps did not build in, with nothing inherited: the clone proves the commits are complete (no
generated module, no build artefact and no `.superpowers/` file is needed) and that a checkout typechecks in CI's
order (install, typecheck, build). Each submodule path is symlinked to this worktree's checkout, so nothing is fetched
and no submodule is touched.

Run: `bash -c 'rm -rf /tmp/f4c-fresh-clone && git clone -q --single-branch --branch feature/modularize "$PWD" /tmp/f4c-fresh-clone && cd /tmp/f4c-fresh-clone && for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$m"; mkdir -p "$(dirname "$m")"; ln -s "/workspaces/RetroPlug-main/cuttlefish/$m" "$m"; done && ls -d deps/*/ | wc -l'`
Expected: `13`: the eight submodule directories are links to this worktree's checkouts, and the five vendored trees come from the clone itself. The clone cannot fetch its submodules here (`deps/dpf.js`'s URL is an SSH one), so this leg is a proof about our own tree, not about submodules.

Run: `bash -c 'test ! -e /tmp/f4c-fresh-clone/packages/retroplug/src/generated && test ! -e /tmp/f4c-fresh-clone/packages/retroplug/tsconfig.host-modules.json && echo "no generated modules"'`
Expected: `no generated modules`: the clone carries neither the module trees nor the generated path files (they are derived), and every step below regenerates them.

Run: `bash -c 'cd /tmp/f4c-fresh-clone && pnpm install --frozen-lockfile --offline'`
Expected: exit 0: the workspace installs offline from the lockfile alone (the clone has no `node_modules`).

Run: `bash -c 'cd /tmp/f4c-fresh-clone && pnpm typecheck'`
Expected: exit 0, `Scope: 25 of 26 workspace projects` and 19 `Done` lines, INCLUDING `packages/ui-react` and `packages/ui-fixture`, whose tsconfigs extend the generated host-modules file: the root script runs the generator first, which is what makes this pass before any build. A failure here is the CI order's finding. This plan's generator ran these three legs against a clone of the prototype's tip: install exit 0, typecheck exit 0 with `Scope: 25 of 26` and 19 `Done`.

Run: `bash -c 'cd /tmp/f4c-fresh-clone && node tools/check-doc-links.mjs'`
Expected: `check-doc-links: 98 file(s) clean`, the clone's own tree (this plan included; the prototype's tip without this plan file prints 97, which is what every stage's log records, and the generator saw that 97).

Run: `bash -c 'cd /tmp/f4c-fresh-clone && cmake -S . -B build >/dev/null && cmake --build build -j$(nproc) >/dev/null && echo built'`
Expected: `built`: the configure runs the generator (which writes the modules and the generated path files the clone had none of) and the whole tree builds. A cold build prints one pre-existing GCC `-Wformat-truncation=` warning in `packages/native/plugin/PluginDSP.cpp` (the generator ran a cold build of the tip's clone and saw exactly that one and no other); a second warning is a finding.

Run: `bash -c 'cd /tmp/f4c-fresh-clone && pnpm test && pnpm check:boundaries && git status --short --untracked-files=all --ignore-submodules=all'`
Expected: exit 0: `# 226 test file(s) passed`, the boundary check with rule 9's three roots green (0 allowed by EXCEPTIONS, 0 warnings), and then NOTHING from `git status`: the clone gained no tracked change and no untracked file, so the commits carry everything the tree needs. `--ignore-submodules=all` is what makes the status readable here: the submodule directories are symlinks, and a plain `git status` refuses them (`expected submodule path 'deps/catch2' not to be a symbolic link`) and prints nothing at all.

- [ ] **Step 9: The boundary check, the docs, and the two consumers**

Run: `pnpm check:boundaries`
Expected: `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`: rule 9's three roots with their two lists, every entry used, rule 3's host modules and surfaces, and no stale exception. The `ui` root's `cli/` refusal is pinned by `shell-ui-relative`'s two files.

Run: `pnpm check:docs`
Expected: `check-doc-links: 98 file(s) clean`: every log records 97, at the design commit and at every stage (the prototype's tree carries no F4c plan; this phase's design is already counted), and this plan is the 98th tracked Markdown file `check-doc-links` walks. Every link the phase moved resolves, and no plan record was edited.

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-bliptoaster && mkdir -p /tmp/f4c-bliptoaster/build /tmp/f4c-bliptoaster/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/f4c-bliptoaster/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/f4c-bliptoaster/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/f4c-bliptoaster/retroplug-cli/ && cd /tmp/f4c-bliptoaster && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes; e=$?; rm -rf /tmp/f4c-bliptoaster; exit $e'`
Expected: `PASS: 57/57 test file(s) ok`. The CLI is a product of the same packages and its dispatcher's table is generated from them, so a consumer's suite is the outside check that the moved code still runs. This phase changes no CLI surface.

Run: `bash -c 'R=$PWD; rm -rf /tmp/f4c-nesvj && mkdir -p /tmp/f4c-nesvj/build /tmp/f4c-nesvj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/f4c-nesvj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/f4c-nesvj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/f4c-nesvj/retroplug-cli/ && cd /tmp/f4c-nesvj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes; e=$?; rm -rf /tmp/f4c-nesvj; exit $e'`
Expected: `PASS: 19/19 test file(s) ok`. The CLI is a product of the same packages and its dispatcher's table is generated from them, so a consumer's suite is the outside check that the moved code still runs. This phase changes no CLI surface.

Run: `bash -c 'git -C /workspaces/bliptoaster status --short; git -C /workspaces/nesvj status --short'`
Expected: nothing printed: neither repository was edited.

Run: `bash -c 'rm -rf /tmp/f4c-bt && mkdir -p /tmp/f4c-bt && tar -C /workspaces/bliptoaster/retroplug-cli -cf - tsconfig.json package.json tests sdk node_modules | tar -C /tmp/f4c-bt -xf - && cp build/cli-sdk/retroplug-cli.d.ts /tmp/f4c-bt/sdk/retroplug-cli.d.ts && ls /tmp/f4c-bt/tests/*.test.ts | wc -l'`
Expected: `57`: the consumer's `tsconfig.json`, `package.json`, its 57 `.test.ts` files and their helper, its `sdk` directory and the `typescript` its `devDependencies` resolve to are a copy under `/tmp`, and the `sdk` directory's one declaration is this tree's built one. Nothing in `/workspaces/bliptoaster` is written. The copy carries the consumer's own `node_modules`, so its `tsc` is the version its package declares and nothing needs installing.

Run: `bash -c 'cd /tmp/f4c-bt && npm run typecheck'`
Expected: exit 0: BlipToaster's own `tsc --noEmit`, run through its `typecheck` script, against this tree's `retroplug-cli.d.ts`. This phase changes no SDK declaration, so this is the control that the harness still reads the built one. The plan's generator ran it: 57 files, `tsc --noEmit` exit 0. NESVJ has no `package.json` and no typecheck: its suite is the whole of its check.

- [ ] **Step 10: The bundle module diff, base against tip**

The design's finding is that the UI bundle is no oracle (esbuild heads each module with its path), so the phase's
proof about bundling is a module-by-module diff: every non-UI bundle must be unchanged, and the UI bundle must gain
exactly `ui-react`'s modules and every package's `ts/ui/` (and the shell's new `ui/views/` and
`ui/input/appActions.ts`). `tools/build-session.js`'s `bundleInputs(entry, format)` bundles in memory and returns
every module that went in; `tools/build-ui.js` exports the same instrument for the UI. The recorder is written to a
scratch path, and two trees are made: a full `git archive` of the design commit with its own offline install and
generator, and this worktree. The recorder, whole:

````js
// Records, per bundle, the modules esbuild put in: the F4c bundle diff (Task 6's leg).
//
//   node bundle-modules.mjs <tree> <outDir>
//
// The build-session entries resolve against packages/retroplug (the repo's own bundle tests run there), so the recorder
// changes there before it loads the tree's esbuild. The UI bundle is not a build-session one: tools/build-ui.js builds it
// with its own options and exports the same instrument (F4b's Task 6).
import { createRequire } from "node:module";
import { chdir } from "node:process";
import { mkdirSync, realpathSync } from "node:fs";
import { relative, resolve, sep } from "node:path";

const tree = resolve(process.argv[2]);
const outDir = resolve(process.argv[3] ?? "/tmp/f4c-accept/bundles");
mkdirSync(outDir, { recursive: true });
const ENTRIES = [
  ["cp", "src/pluginControlPlane.ts", "esm"],
  ["worker", "src/render/worker.ts", "iife"],
  ["sdk", "cli/sdk.ts", "esm"],
  ["cli", "cli/cli.ts", "esm"],
  ["planner", "src/graph/protocolNodes.ts", "esm"],
  ["script-nodes", "src/generated/scriptNodes.ts", "iife"],
];
chdir(`${tree}/packages/retroplug`);
const require = createRequire(`${tree}/packages/retroplug/`);
const { bundleInputs } = require(`${tree}/tools/build-session.js`);
const REAL = realpathSync(tree);
// A module outside the tree (a symlinked submodule's own node_modules) is named by its `deps/...` tail, so the two
// trees' lists compare: their roots differ, and a resolved path through a submodule symlink leaves the tree.
const norm = (p) => {
  const rel = relative(REAL, resolve(p)).split(sep).join("/");
  if (!rel.startsWith("../")) return rel;
  const at = rel.indexOf("/deps/");
  return at >= 0 ? rel.slice(at + 1) : rel;
};
const out = {};
for (const [name, entry, format] of ENTRIES) {
  const r = bundleInputs(entry, format);
  out[name] = { modules: r.inputs.length, inputs: r.inputs.map(norm).sort() };
}
const ui = require(`${tree}/tools/build-ui.js`);
const r = ui.bundleInputs(`${tree}/packages/retroplug/ui/main.tsx`);
out.ui = { modules: r.inputs.length, inputs: r.inputs.map(norm).sort() };
console.log(JSON.stringify(out, null, 1));
````

Run: `mkdir -p /tmp/f4c-accept; sed -n '15537,15580p' docs/modular-graph-phase-f4c-plan.md > /tmp/f4c-accept/bundle-modules.mjs`
Expected: exit 0: the recorder is the block above, byte for byte.

Run: `bash -c 'W=$PWD; D=/tmp/f4c-accept/base; rm -rf "$D" && mkdir -p "$D" && git archive 663632d16 | tar -x -C "$D" && for m in $(git config --file .gitmodules --get-regexp path | sed "s/^[^ ]* //"); do rm -rf "$D/$m"; mkdir -p "$(dirname "$D/$m")"; ln -s "$W/$m" "$D/$m"; done && cd "$D" && pnpm install --frozen-lockfile --offline >/dev/null && node packages/retroplug/scripts/lib/packages.mjs && echo base-ready'`
Expected: `base-ready`: the design commit's tree, its submodule paths linked to this worktree's checkouts (the UI bundle reaches dpf.js and React through them), its own offline install (so its workspace links resolve inside it), and its generator (so the modules its sources import exist).

Run: `node /tmp/f4c-accept/bundle-modules.mjs /tmp/f4c-accept/base /tmp/f4c-accept/out-base > /tmp/f4c-accept/bundles-base.json && node /tmp/f4c-accept/bundle-modules.mjs "$PWD" /tmp/f4c-accept/out-tip > /tmp/f4c-accept/bundles-tip.json && echo recorded`
Expected: `recorded`: both trees' bundles, the base's and this worktree's. The recorder needs nothing built; the worktree's generated modules are the build's.

Run: `node -e 'const a=require("/tmp/f4c-accept/bundles-base.json"), b=require("/tmp/f4c-accept/bundles-tip.json"); for (const k of Object.keys(a)) { const gone=a[k].inputs.filter(f=>!b[k].inputs.includes(f)); const added=b[k].inputs.filter(f=>!a[k].inputs.includes(f)); console.log(k+": "+a[k].modules+" -> "+b[k].modules+" modules; removed: "+(gone.join(", ")||"-")+"; added: "+(added.join(", ")||"-")); }'`
Expected: seven lines, the run's own output. **This plan's generator ran it**, from archives of the design commit and of the prototype's tip, and its output was: `cp` 277 -> 277, `worker` 273 -> 273, `sdk` 281 -> 281, `cli` 321 -> 321, `planner` 2 -> 2 and `script-nodes` 46 -> 46, each `removed: -; added: -`; and `ui` 453 -> 463, whose removed set is the shell's fourteen old view and widget modules (`ui/hooks/useRerender.ts`, `ui/lvgl/Box.tsx`, `PixelCanvas.tsx`, `lv.ts`, `useFramePoll.ts`, `useNativeEvent.ts`, `ui/screens/grid/DebugReadout.tsx`, `LsdjOverlay.tsx`, `RisaOverlay.tsx`, `lsdjDebug.ts`, `useLsdjRuntime.ts`, `useRisaRuntime.ts` and `ui/screens/hd/LsdjHdScreen.tsx`, `useLsdjHdSession.ts`) and whose added set is `ui-react`'s ten modules, `lsdj/ts/ui/`'s three, `risa/ts/ui/`'s two, the shell's seven `ui/views/` files, `ui/input/appActions.ts` and `ui/ts/format.ts`: `ui-react` and every package's `ts/ui/` in the UI bundle ONLY. A non-UI bundle that gained a module, or a UI module outside the UI bundle, is a finding.

- [ ] **Step 11: What is owed**

These are the phase's debts, recorded so nothing thinks they ran:

- **Windows**: the build and `test.bat` (all four suites) through the user's Windows agent, and `reaper:win` with
  the two Windows Reaper checks. The new packages, the generator's fixtures and rule 3's `.tsx` and JSX clauses have
  not seen MSVC; the build is `/W4 /WX`, so an MSVC-only warning is a real finding.
- **macOS and the arm64 Linux CI job**: neither has built these packages. The exact-byte oracles refuse a foreign
  host rather than comparing a tree with itself (`RP_PARITY_REQUIRE_TARGET`, `RP_KIT_GOLDEN_REQUIRE_TARGET`,
  `RP_RENDER_GOLDEN_REQUIRE_TARGET`), so their first run must set those flags and report, not quietly pass.
- **CI**: `build.yml` runs on `workflow_dispatch`, pull requests and pushes to `main` only, so F4c's first CI run is
  a dispatched run or a PR the user starts. The 226-file suite, the 47-file UI suite and the new fixtures are what
  it will run.
- **A real `component` view on a host**: the kind and `ui-fixture` ship, but no product lists a package with one, so
  no `test:ui` case can draw it (design 8.6). The first package that needs React for a view proves it on a host, and
  its case lands with it.
- **E4's look on a real host** (the SDL step) and **the real-Launchpad checklist** F3 owed: both by hand, at the
  desk.
- **Nothing else**: the per-product bundles, the controller-app display bug, the shell's hard dependency on
  `launchpad`, a kit compile in the UI host, gamepad axis tokens, folding `tsconfig.ui.json` and narrowing the
  reopen key are design section 11's follow-ups, not this phase's debts.
