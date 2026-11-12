# Known bugs, batch 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The three LSDj entries from [spec/07](../spec/07-remaining-work.md)'s known bugs: the readers that decide what
a system is ask what its graph places rather than what its stored role list holds, the MI.MAP controller's predictor
walks the clock divisor its cart is set to, and MI.OUT's capture feeds the Game Boy the 0s an Arduinoboy does, so the
decoder hears exactly what LSDj sends; then the documents record the batch.

**Architecture:** Task 1 adds `placedKinds` and `placedRole` beside `protocolPipeline`
(`packages/retroplug/src/graph/protocolNodes.ts`), `placed` to the system view and to the host's `SystemInfo`, and
`isPlaced` to `@retroplug/core/systemRoles`; `resolveTracker`, the config rows, `projectParameterMap` and the three cart
views then ask placement. Task 2 adds an optional `divisor` to a controller app's sync rule (manifest data, checked by
the launchpad kind), the placer reads the named setting off the driven cart's node in `PlaceSystem.pipeline` and hands
the session node `tickDivisor`, and the node walks `24 / tickDivisor` ticks a quarter, read every block. Task 3 changes
one argument in `SameBoySystem::writeAudioSample` (`GB_serial_set_data_bit(gb_, false)`), rewrites the MI.OUT
real-core test as one file a rate holding the exact sequence (and a song value of 0x7F reaching the decoder's clock),
reworks the three tests built on the defect, and re-records the four parity cases it moves. Task 4 is the docs.

**Tech Stack:** TypeScript with the repo's own TAP runners (`pnpm test`, `pnpm test:native`, `pnpm test:ui`), C++20
with Catch2 (`retroplug-engine-graph-test`, and `pnpm test:plugin` for all fourteen binaries), the profiling host in
`build-prof` for dsp-bench (`tools/run-profile.sh`), the pinned SameBoy submodule (no pointer moves, no patch change).

**Spec:** [known-bugs-batch-2-design.md](known-bugs-batch-2-design.md). Its section 4 lists the tasks; the acceptance
run is the controller's, after this batch lands.

**Provenance:** every code block below was applied to this worktree at `6690c8bc3` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3 (`linux-x64-gnu13`, the
target parity and the goldens compare exactly on), with the sibling fixture tree at `/workspaces/resources`. There is no
prototype branch, so afterward the worktree was restored to its starting state and the plan was replayed by text: each
task's patches applied in order to copies of the touched files at `6690c8bc3`, the result byte-identical to the
validated tree. Every Expected line is quoted from a real run, and a failing-test step's output is what the code
without that step's change produces (ANSI colour codes stripped, and the stack frames of a TAP failure left out).

**How to apply a step's code.** Each code step gives one `patch` block, a `git apply`-able unified diff against the
tree as the steps before it left it. The blocks are fenced with four backticks, because the docs patch carries
three-backtick lines of its own. Apply one by running this with the block's text (not its fences) between the
heredoc's first line and the closing `PATCH`, or by making the same hunks with the editor:

```
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the step's patch block>
PATCH
```

**Derived files.** Three files are written by a re-record command, not by a patch: Task 2's
`packages/retroplug/test/graph/golden/build-graph-roles.json` (`RP_GOLDEN_UPDATE=1`) and Task 3's
`packages/retroplug/test-native/parity/baseline/lsdj-modes.json` and `.../blocks-tiny-lsdj.json`
(`RP_PARITY_UPDATE=1`). Each step that writes one proves its diff holds only what it should and checks the result by
blob hash (`git hash-object`), which the validated run produced.

## Ground rules

These are the repo's rules this batch must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish`. Never switch branches, never `git stash`, never `git reset --hard`,
  never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset, stash or check out a path in it, and never touch `deps/dpf.js` or any other submodule.
  `git status --porcelain` lists it; `git add` only the paths a step names.
- **Never `rm -rf build`** (or any `build*/`). Build a target with `cmake --build <dir> --target <t> -j$(nproc)`: the
  default is single-threaded. `tools/run-profile.sh` configures and builds its own `build-prof/`.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";".
- **A commit message is a subject line, a blank line, then the body**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **Nothing in this batch opens an audio or MIDI device.** Never run `retroplug-sdl`, any `sdl:*` script, any `reaper:*`
  script, the tip-verify kit or anything else that initialises PortAudio or SDL audio, and never run a
  MIDI-port-enumerating harness. The Catch2 binaries, `pnpm test`, `pnpm test:native`, `pnpm test:ui`,
  `pnpm test:plugin` and `tools/run-profile.sh stats` open no device.
- **Re-record only what a step names**: the role goldens in Task 2 (`RP_GOLDEN_UPDATE=1`) and the parity groups
  `lsdj-modes` and `blocks-tiny-lsdj` in Task 3 (`RP_PARITY_UPDATE=1`, one group by its full name), each followed by
  its proof. Any other golden, parity, digest or transcript change is a bug to find, not a step.
- **Fixture runs**: a native run that needs the LSDj ROMs is given `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on
  the same command line; `RETROPLUG_RESOURCES_DIR=/nonexistent` reproduces CI's picture, the skip baseline included.
- **Never send repository content to an external service.**

---

## Task 1: the readers ask what is placed

A system's stored role list can keep a role the graph leaves out: after a ROM swap an mGB cart can still carry
`lsdj-sync` and an LSDj cart `mgb`, and `protocolPipeline` places one writer per system input. The tracker a system
resolves, the config rows its menu shows, the DAW lanes it exposes and the LSDj and risa cart views decided by
presence. The fix keeps the stored roles and makes every one of those readers ask placement: `placedKinds(roles)` is
the kinds the graph places, in role order, each once (a kind with a protocol node when the pipeline places it, a kind
with none when it is present, since the pipeline never leaves one of those out), and `placedRole(roles, kind)` the role
of a placed kind. The system view carries `placed` (filled from `placedKinds`), which the host's `SystemInfo` declares,
and a package's view asks it through `isPlaced(sys, kind)`.

The design's `placed` is "the kinds the pipeline places"; this plan's includes the present no-node kinds as well,
because risa's tracker marker, `risa`, has no node and its overlay must still read as placed (`isPlaced` lives in core,
which cannot ask the shell's table). LSDj declares no DAW lanes, so "an LSDj cart storing a stale `mgb` exposes LSDj's
parameters" is, measured, "exposes none" where it exposed mGB's.

**Files** (line numbers are the file at `6690c8bc3`):

- Modify: `packages/retroplug/src/graph/protocolNodes.ts:134-136` (the two helpers go after `protocolPipeline`, at the
  file's end).
- Modify: `packages/core/ts/host.ts:82-87` (`SystemInfo`), `packages/core/ts/systemRoles.ts:43-48` (`isPlaced` after
  `hasRole`).
- Modify: `packages/retroplug/src/systemsStore.ts:55-61` (the import), `:119-124` (`SystemView`), `:220-225` (`view()`).
- Modify: `packages/retroplug/src/trackerRegistry.ts:8-22`, `packages/retroplug/ui/screens/menu/configRows.ts:3-38`,
  `packages/retroplug/src/parameterMap.ts:17-22, 73-90`.
- Modify: `packages/lsdj/ts/ui/hdView.ts:15-21, 155-175`, `packages/lsdj/ts/ui/overlayView.ts:8-14, 38-58`,
  `packages/risa/ts/ui/overlayView.ts:6-12, 39-51`.
- Create: `packages/retroplug/test/graph/placed-roles.test.ts`.
- Test: `packages/lsdj/ts/test/ui-views.test.ts:34-43, 133-151, 164-177`,
  `packages/risa/ts/test/ui-views.test.ts:19-24, 122-132`; the two `SystemInfo` literals the new required field
  reaches: `packages/lsdj/ts/test/songImport.test.ts:113-118` and
  `packages/retroplug/test/ui/view-session.test.ts:39-44`.

**Interfaces:**

- Consumes: `protocolPipeline`, `protocolNodeOf` (`protocolNodes.ts`), `RoleInstance`, `hasRole`, `roleConfig`
  (`@retroplug/core/systemRoles`), `TRACKER_INTEGRATIONS` (the generated packages module), `SystemsStore.adopt` and its
  load ordering (`suggestedFirst`, `withMissingFeatureRoles`).
- Produces: `placedKinds(roles: readonly { readonly kind: string }[]): string[]` and
  `placedRole<R extends { readonly kind: string }>(roles: readonly R[], kind: string): R | undefined` in
  `packages/retroplug/src/graph/protocolNodes.ts`; `SystemInfo.placed: readonly string[]` (`@retroplug/core/host`);
  `SystemView.placed: string[]` (`systemsStore.ts`);
  `isPlaced(sys: { readonly placed: readonly string[] }, kind: string): boolean` (`@retroplug/core/systemRoles`). No
  later task uses them.

### Steps

- [ ] **Step 1: write the failing test for the helpers and the system view**

````patch
diff --git a/packages/retroplug/test/graph/placed-roles.test.ts b/packages/retroplug/test/graph/placed-roles.test.ts
new file mode 100644
index 000000000..cc811d84c
--- /dev/null
+++ b/packages/retroplug/test/graph/placed-roles.test.ts
@@ -0,0 +1,72 @@
+// The readers that decide what a system is ask what its graph places, not what its stored role list holds
+// (docs/known-bugs-batch-2-design.md section 1). A stored list keeps a role the graph leaves out: an mGB cart can still
+// carry lsdj-sync after a ROM swap, an LSDj cart mgb, and protocolPipeline places one writer per system input. So the
+// tracker a system resolves, the config rows its menu shows, the DAW lanes it exposes and the cart views a package
+// gates on `isPlaced` follow placement, while the stored role and its config stay, so a ROM swapped back brings them
+// with it.
+import { test, expect } from "@retroplug/testing";
+import { isPlaced, type RoleInstance } from "@retroplug/core/systemRoles";
+import { placedKinds, placedRole } from "../../src/graph/protocolNodes";
+import { buildAppRegistry } from "../../src/appHost";
+import { SystemsStore } from "../../src/systemsStore";
+import { MockBackend } from "../../testing/mockBackend";
+import { lsdjRom } from "../systems/fixtures";
+
+const LSDJ_SYNC: RoleInstance = { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } };
+const MGB: RoleInstance = { kind: "mgb", config: {} };
+const SAMEBOY: RoleInstance = { kind: "sameboy", config: {} };
+const ASSETS: RoleInstance = { kind: "lsdj-assets", config: { overrides: [] } };
+
+/** An mGB cart that still stores the lsdj-sync it carried before a ROM swap: both nodes write serialIn, and a load puts
+ *  the ROM's own role first, so mgb is placed and lsdj-sync left out. */
+const MGB_CART: readonly RoleInstance[] = [SAMEBOY, MGB, LSDJ_SYNC];
+/** The reverse: an LSDj cart that still stores mgb. */
+const LSDJ_CART: readonly RoleInstance[] = [SAMEBOY, LSDJ_SYNC, MGB, ASSETS];
+
+test("placedRole answers the role the pipeline places, and a role with no protocol node when it is present", () => {
+  expect(placedRole(MGB_CART, "mgb")).toBe(MGB);
+  expect(placedRole(MGB_CART, "lsdj-sync")).toBe(undefined);
+  expect(placedRole(LSDJ_CART, "lsdj-sync")).toBe(LSDJ_SYNC);
+  expect(placedRole(LSDJ_CART, "mgb")).toBe(undefined);
+  // The core's config role and an asset override have no node, so the pipeline never leaves them out.
+  expect(placedRole(LSDJ_CART, "sameboy")).toBe(SAMEBOY);
+  expect(placedRole(LSDJ_CART, "lsdj-assets")).toBe(ASSETS);
+  // Absent is absent, node or none.
+  expect(placedRole(LSDJ_CART, "risa-sync")).toBe(undefined);
+  expect(placedRole(LSDJ_CART, "risa")).toBe(undefined);
+  // A kind named twice is placed once, as its first.
+  expect(placedRole([MGB, { kind: "mgb", config: { a: 1 } }], "mgb")).toBe(MGB);
+});
+
+test("placedKinds lists each kind the graph places once, in role order", () => {
+  expect(placedKinds(MGB_CART)).toEqual(["sameboy", "mgb"]);
+  expect(placedKinds(LSDJ_CART)).toEqual(["sameboy", "lsdj-sync", "lsdj-assets"]);
+  expect(placedKinds([MGB, MGB, ASSETS, ASSETS])).toEqual(["mgb", "lsdj-assets"]);
+  // Nodes that write different inputs are all placed, beside a marker role with none.
+  const nes = [{ kind: "mesen" }, { kind: "risa" }, { kind: "nes-n8-midi" }, { kind: "risa-sync" }];
+  expect(placedKinds(nes)).toEqual(["mesen", "risa", "nes-n8-midi", "risa-sync"]);
+  expect(placedKinds([])).toEqual([]);
+});
+
+test("isPlaced reads the kinds a system view says its graph places, not its stored roles", () => {
+  expect(isPlaced({ placed: ["sameboy", "mgb"] }, "mgb")).toBe(true);
+  expect(isPlaced({ placed: ["sameboy", "mgb"] }, "lsdj-sync")).toBe(false);
+  expect(isPlaced({ placed: [] }, "sameboy")).toBe(false);
+});
+
+test("the system view says which of its stored roles the graph places, and keeps the ones it leaves out", () => {
+  const be = new MockBackend("/cfg");
+  const store = new SystemsStore(be, undefined, buildAppRegistry());
+  be.seed("/roms/mgb.gb", lsdjRom("MGB"));
+  be.seed("/roms/lsdj.gb", lsdjRom("LSDJ"));
+  // An mGB ROM at a path a project stored with LSDj's role, and the reverse: the load puts the ROM's own role first.
+  const mgbId = store.adopt({ romPath: "/roms/mgb.gb", core: "sameboy", roles: [SAMEBOY, LSDJ_SYNC] })!;
+  const lsdjId = store.adopt({ romPath: "/roms/lsdj.gb", core: "sameboy", roles: [SAMEBOY, MGB] })!;
+  const view = (id: number) => store.view().find((v) => v.id === id)!;
+  expect(view(mgbId).roles.map((r) => r.kind)).toEqual(["sameboy", "mgb", "lsdj-sync"]);
+  expect(view(mgbId).placed).toEqual(["sameboy", "mgb"]);
+  expect(view(lsdjId).roles.map((r) => r.kind)).toEqual(["sameboy", "lsdj-sync", "mgb", "lsdj-assets"]);
+  expect(view(lsdjId).placed).toEqual(["sameboy", "lsdj-sync", "lsdj-assets"]);
+  expect(isPlaced(view(mgbId), "lsdj-sync")).toBe(false);
+  expect(isPlaced(view(lsdjId), "lsdj-sync")).toBe(true);
+});
````

- [ ] **Step 2: run it and see it fail**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/placed-roles
```

Expected: exit 1, the bundle refusing the three imports that do not exist yet:

```
✘ [ERROR] No matching export in "packages/core/ts/systemRoles.ts" for import "isPlaced"
...
✘ [ERROR] No matching export in "packages/retroplug/src/graph/protocolNodes.ts" for import "placedKinds"
...
✘ [ERROR] No matching export in "packages/retroplug/src/graph/protocolNodes.ts" for import "placedRole"
...
# BUILD FAILED: graph/placed-roles
Build failed with 3 errors:
packages/retroplug/test/graph/placed-roles.test.ts:8:9: ERROR: No matching export in "packages/core/ts/systemRoles.ts" for import "isPlaced"
packages/retroplug/test/graph/placed-roles.test.ts:9:9: ERROR: No matching export in "packages/retroplug/src/graph/protocolNodes.ts" for import "placedKinds"
packages/retroplug/test/graph/placed-roles.test.ts:9:22: ERROR: No matching export in "packages/retroplug/src/graph/protocolNodes.ts" for import "placedRole"

# 1/1 test file(s) FAILED: graph/placed-roles
```

- [ ] **Step 3: the helpers, `SystemInfo.placed`, `isPlaced` and the system view's `placed`**

`SystemInfo.placed` is required, so the two test literals that build one gain it in the same step.

````patch
diff --git a/packages/core/ts/host.ts b/packages/core/ts/host.ts
index f473cc7ce..effb8a9ab 100644
--- a/packages/core/ts/host.ts
+++ b/packages/core/ts/host.ts
@@ -82,6 +82,10 @@ export interface SystemInfo {
   readonly battery: boolean;
   readonly missing: boolean;
   readonly roles: readonly RoleInstance[];
+  /** The kinds of `roles` the system's graph places, in role order (the shell's protocol pipeline): a stored role the
+   *  graph leaves out, a stale one after a ROM swap, is in `roles` and not here. A view asks it through `isPlaced`
+   *  (systemRoles) before it treats a system as its cart's. */
+  readonly placed: readonly string[];
 }
 
 /** The file I/O every surface has, and the backend's own methods one for one. */
diff --git a/packages/core/ts/systemRoles.ts b/packages/core/ts/systemRoles.ts
index 0a62ad665..c21b061d3 100644
--- a/packages/core/ts/systemRoles.ts
+++ b/packages/core/ts/systemRoles.ts
@@ -43,6 +43,14 @@ export function hasRole(roles: readonly RoleInstance[] | undefined, kind: string
   return findRole(roles, kind) !== undefined;
 }
 
+/** Whether a system's graph places a role of `kind`: what a view asks before it treats the system as its cart's. A
+ *  stored role list can keep a role the graph leaves out (an mGB cart still carrying lsdj-sync after a ROM swap), which
+ *  `hasRole` still finds, so its config reads as stored; `placed` is the shell's answer (SystemInfo.placed), and a
+ *  role with no protocol node is placed whenever it is present. */
+export function isPlaced(sys: { readonly placed: readonly string[] }, kind: string): boolean {
+  return sys.placed.includes(kind);
+}
+
 /** A role's config validator: a zod schema whose `.parse` fills defaults + clamps a
  *  (possibly partial/invalid) config into a full one. Structurally-typed so the
  *  registry stays zod-agnostic; the schemas themselves are built with zod
diff --git a/packages/lsdj/ts/test/songImport.test.ts b/packages/lsdj/ts/test/songImport.test.ts
index 1afd50e11..56858ab6c 100644
--- a/packages/lsdj/ts/test/songImport.test.ts
+++ b/packages/lsdj/ts/test/songImport.test.ts
@@ -113,6 +113,7 @@ function target(): SystemInfo {
     battery: true,
     missing: false,
     roles: [{ kind: "lsdj-assets", config: { overrides: [] } }],
+    placed: ["lsdj-assets"],
   };
 }
 
diff --git a/packages/retroplug/src/graph/protocolNodes.ts b/packages/retroplug/src/graph/protocolNodes.ts
index 714d723d3..ad38cd4a1 100644
--- a/packages/retroplug/src/graph/protocolNodes.ts
+++ b/packages/retroplug/src/graph/protocolNodes.ts
@@ -134,3 +134,26 @@ export function protocolPipeline(
   }
   return out;
 }
+
+/** The kinds of `roles` the graph places, in role order, each once: a kind with a protocol node when protocolPipeline
+ *  places it, and a kind with none (a core's config role, an asset override, a tracker's marker) when it is present,
+ *  as the pipeline never leaves one of those out. What a reader asks before it treats a system as a cart's: a stored
+ *  role list can keep a role the graph leaves out (an mGB cart still carrying lsdj-sync after a ROM swap, an LSDj cart
+ *  carrying mgb), and the stored role stays, so a ROM swapped back brings its settings back. The pipeline decides by
+ *  kind and order alone, so no config is read. */
+export function placedKinds(roles: readonly { readonly kind: string }[]): string[] {
+  const withNodes = new Set(protocolPipeline(roles.map((r) => ({ kind: r.kind, config: {} }))).map((p) => p.kind));
+  const out: string[] = [];
+  for (const r of roles) {
+    if (out.includes(r.kind)) continue;
+    if (withNodes.has(r.kind) || protocolNodeOf(r.kind) === undefined) out.push(r.kind);
+  }
+  return out;
+}
+
+/** The role of `kind` in `roles` when the graph places it (placedKinds), else undefined; of a kind named twice, the
+ *  first, which is the one placed. */
+export function placedRole<R extends { readonly kind: string }>(roles: readonly R[], kind: string): R | undefined {
+  const role = roles.find((r) => r.kind === kind);
+  return role && placedKinds(roles).includes(kind) ? role : undefined;
+}
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 6bf8a3da9..530fe3163 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -55,7 +55,7 @@ import {
 import { type CommonSettings, DEFAULT_COMMON_SETTINGS, clampGain, commonSettingsSchema } from "./systemSettings";
 import { ROLE_HEADER_LEN, type RoleRegistry, type RoleInstance } from "@retroplug/core/systemRoles";
 import type { Host } from "@retroplug/core/host";
-import { protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
+import { placedKinds, protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
 import { resolveSongCatalog, resolveTracker } from "./trackerRegistry";
 import type { TrackedSlot, WorkingSongImage } from "@retroplug/tracker/songCatalog";
 
@@ -119,6 +119,9 @@ export interface SystemView {
   missing: boolean;
   settings: CommonSettings;
   roles: RoleInstance[];
+  /** The kinds of `roles` the graph places (graph/protocolNodes.ts placedKinds): a stored role it leaves out stays in
+   *  `roles` and is not here. */
+  placed: string[];
 }
 
 /** loadRom outcome: defer to a sibling project, a built system, or a failure. */
@@ -220,6 +223,7 @@ export class SystemsStore {
       missing: e.romPath !== "" && e.embeddedRom === "" && !this.backend.fileExists(e.romPath),
       settings: e.settings ?? DEFAULT_COMMON_SETTINGS,
       roles: e.roles ?? [],
+      placed: placedKinds(e.roles ?? []),
     }));
   }
 
diff --git a/packages/retroplug/test/ui/view-session.test.ts b/packages/retroplug/test/ui/view-session.test.ts
index 0458ba32e..6831f437a 100644
--- a/packages/retroplug/test/ui/view-session.test.ts
+++ b/packages/retroplug/test/ui/view-session.test.ts
@@ -39,6 +39,7 @@ function system(over: Partial<SystemInfo> = {}): SystemInfo {
     battery: true,
     missing: false,
     roles: [],
+    placed: [],
     ...over,
   };
 }
````

- [ ] **Step 4: run it and the typecheck**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/placed-roles
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected: the four cases pass (the store logs the role it leaves out of each system), and the typecheck exits 0:

```
# graph/placed-roles
[systems] /roms/mgb.gb: its lsdj-sync role is not placed: its node would write serialIn, which its mgb role's already does
[systems] /roms/lsdj.gb: its mgb role is not placed: its node would write serialIn, which its lsdj-sync role's already does
TAP version 13
1..4
ok 1 - placedRole answers the role the pipeline places, and a role with no protocol node when it is present
ok 2 - placedKinds lists each kind the graph places once, in role order
ok 3 - isPlaced reads the kinds a system view says its graph places, not its stored roles
ok 4 - the system view says which of its stored roles the graph places, and keeps the ones it leaves out
# 4 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: write the failing reader tests**

The tracker, the config rows and the DAW lanes in the new file; the LSDj HD view and overlay and the risa overlay in
their packages' view tests, whose fixture systems now carry `placed` (an mGB cart storing a stale `lsdj-sync`, and a
system with `placed` emptied, are the new negatives).

````patch
diff --git a/packages/lsdj/ts/test/ui-views.test.ts b/packages/lsdj/ts/test/ui-views.test.ts
index deec6dd77..fd7f98452 100644
--- a/packages/lsdj/ts/test/ui-views.test.ts
+++ b/packages/lsdj/ts/test/ui-views.test.ts
@@ -34,10 +34,15 @@ function system(over: Partial<SystemInfo> = {}): SystemInfo {
     battery: true,
     missing: false,
     roles: [{ kind: "lsdj-sync", config: {} }],
+    placed: ["lsdj-sync"],
     ...over,
   } as SystemInfo;
 }
 
+/** An mGB cart still storing the lsdj-sync it carried before a ROM swap: its graph places mgb and leaves lsdj-sync out,
+ *  which is what the views ask. */
+const STALE_SYNC = system({ roles: [{ kind: "mgb", config: {} }, { kind: "lsdj-sync", config: {} }], placed: ["mgb"] });
+
 /** A host with the groups these views read. `state` is mutable so a case can change what the next read sees. */
 function hostWith(state: {
   header?: Uint8Array | null;
@@ -133,19 +138,21 @@ test("the HD session reads SRAM on its 15-frame countdown and decodes only when
   expect(session.frame()).toBe(null);
 });
 
-test("the HD view refuses what the old hook gated on: no LSDj role, no ROM path, an unreadable header", () => {
+test("the HD view refuses what the old hook gated on: no placed LSDj role, no ROM path, an unreadable header", () => {
   expect(hdView.body.kind).toBe("pixels");
   if (hdView.body.kind !== "pixels") return;
   expect(hdView.body.unsupported).toBe("HD view needs a supported LSDj cartridge");
   expect(hdView.when?.(system())).toBeTruthy();
-  expect(hdView.when?.(system({ roles: [] }))).toBeFalsy();
+  expect(hdView.when?.(system({ roles: [], placed: [] }))).toBeFalsy();
+  expect(hdView.when?.(STALE_SYNC)).toBeFalsy();
 
   const open = (state: Parameters<typeof hostWith>[0], sys = system()) =>
     hdView.body.kind === "pixels" ? hdView.body.open(hostWith({ ...state, systems: [sys] }), sys.id) : null;
   expect(open({ header: header("LSDj-v9.4.2"), rom: null })).toBe(null); // no ROM bytes
   expect(open({ header: null, rom: new Uint8Array(4) })).toBe(null); // no readable header
   expect(open({ header: header("MGB"), rom: new Uint8Array(4) })).toBe(null); // not an LSDj cart
-  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ roles: [] }))).toBe(null);
+  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ roles: [], placed: [] }))).toBe(null);
+  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, STALE_SYNC)).toBe(null);
   expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ romPath: "" }))).toBe(null);
 });
 
@@ -164,14 +171,17 @@ test("the LSDj overlay's lines are the old overlay's", () => {
   state.ram = null;
   expect(sampler!.sample()).toBe(undefined);
 
-  // The overlay applies to an LSDj-sync system and opens on nothing else: a system without the role, a header that is
-  // not an LSDj cart (the reader's own `supported` gate, which is how this view says "unsupported": a sampler it opened
-  // is always one whose reader resolves a layout), and no header at all.
+  // The overlay applies to a system whose graph places lsdj-sync and opens on nothing else: a system without the role
+  // or storing it unplaced (an mGB cart after a ROM swap), a header that is not an LSDj cart (the reader's own
+  // `supported` gate, which is how this view says "unsupported": a sampler it opened is always one whose reader
+  // resolves a layout), and no header at all.
   const open = (sys = system()) =>
     overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("LSDj-v9.4.2"), systems: [sys] }), sys.id) : null;
   expect(overlayView.when?.(system())).toBeTruthy();
-  expect(overlayView.when?.(system({ roles: [] }))).toBeFalsy();
-  expect(open(system({ roles: [] }))).toBe(null);
+  expect(overlayView.when?.(system({ roles: [], placed: [] }))).toBeFalsy();
+  expect(overlayView.when?.(STALE_SYNC)).toBeFalsy();
+  expect(open(system({ roles: [], placed: [] }))).toBe(null);
+  expect(open(STALE_SYNC)).toBe(null);
   expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: null }), 1) : null).toBe(null);
   expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("MGB") }), 1) : null).toBe(null);
 });
diff --git a/packages/retroplug/test/graph/placed-roles.test.ts b/packages/retroplug/test/graph/placed-roles.test.ts
index cc811d84c..7e29c7bba 100644
--- a/packages/retroplug/test/graph/placed-roles.test.ts
+++ b/packages/retroplug/test/graph/placed-roles.test.ts
@@ -5,11 +5,17 @@
 // gates on `isPlaced` follow placement, while the stored role and its config stay, so a ROM swapped back brings them
 // with it.
 import { test, expect } from "@retroplug/testing";
-import { isPlaced, type RoleInstance } from "@retroplug/core/systemRoles";
+import { MidiRouting } from "@retroplug/core/midiRouting";
+import { isPlaced, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
+import type { RoleRow } from "@retroplug/ui/contribution";
+import { MGB_PARAMETERS } from "@retroplug/mgb/parameters";
 import { placedKinds, placedRole } from "../../src/graph/protocolNodes";
+import { resolveTracker } from "../../src/trackerRegistry";
+import { projectParameterMap } from "../../src/parameterMap";
 import { buildAppRegistry } from "../../src/appHost";
 import { SystemsStore } from "../../src/systemsStore";
 import { MockBackend } from "../../testing/mockBackend";
+import { roleRowItems } from "../../ui/screens/menu/configRows";
 import { lsdjRom } from "../systems/fixtures";
 
 const LSDJ_SYNC: RoleInstance = { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } };
@@ -70,3 +76,46 @@ test("the system view says which of its stored roles the graph places, and keeps
   expect(isPlaced(view(mgbId), "lsdj-sync")).toBe(false);
   expect(isPlaced(view(lsdjId), "lsdj-sync")).toBe(true);
 });
+
+test("a tracker is resolved by its placed marker role: an mGB cart storing a stale lsdj-sync resolves none", () => {
+  expect(resolveTracker(MGB_CART)).toBe(undefined);
+  expect(resolveTracker(LSDJ_CART)?.id).toBe("lsdj");
+  // risa's marker has no node, so its presence is its placement.
+  expect(resolveTracker([{ kind: "mesen", config: {} }, { kind: "risa", config: {} }])?.id).toBe("risa");
+});
+
+test("a role's config rows show only when the role is placed, and a role with no node's when it is present", () => {
+  const defaults = { tempoDivisor: 1, model: "x" };
+  const roleType = (kind: string): RoleType =>
+    ({ kind, schema: { parse: (v: Record<string, unknown>) => ({ ...defaults, ...v }) } }) as unknown as RoleType;
+  const divisor: RoleRow = {
+    id: "lsdj-divisor",
+    label: "Tempo Divisor",
+    role: "lsdj-sync",
+    key: "tempoDivisor",
+    choices: [1, 2, 4, 8].map((value) => ({ value, label: String(value) })),
+  };
+  const model: RoleRow = {
+    id: "sameboy-model",
+    label: "Model",
+    role: "sameboy",
+    key: "model",
+    choices: [{ value: "x", label: "X" }],
+  };
+  const ids = (roles: readonly RoleInstance[]) =>
+    roleRowItems([divisor, model], { platform: "gb", roles }, roleType, () => {}).map((i) => i.id);
+  expect(ids(MGB_CART)).toEqual(["sameboy-model"]);
+  expect(ids(LSDJ_CART)).toEqual(["lsdj-divisor", "sameboy-model"]);
+});
+
+test("the DAW lanes are the first placed role's with a table: an LSDj cart storing a stale mgb exposes none", () => {
+  const registry = buildAppRegistry();
+  const lanes = (roles: readonly RoleInstance[]) =>
+    projectParameterMap([{ roles: [...roles] }], MidiRouting.SendToAll, registry);
+  // LSDj declares no lanes, so its cart claims no slot, where the stale mgb stored after its lsdj-sync claimed mGB's.
+  expect(lanes(LSDJ_CART)).toEqual([]);
+  expect(lanes(MGB_CART).length).toBe(MGB_PARAMETERS.length);
+  // A role with no node keeps its table beside a placed node of another kind (BlipToaster's chip beside the N8's MIDI).
+  const blip = lanes([{ kind: "mesen", config: {} }, { kind: "nes-n8-midi", config: {} }, { kind: "bliptoaster", config: {} }]);
+  expect(blip.length > 0).toBe(true);
+});
diff --git a/packages/risa/ts/test/ui-views.test.ts b/packages/risa/ts/test/ui-views.test.ts
index e866b7de2..871a20f46 100644
--- a/packages/risa/ts/test/ui-views.test.ts
+++ b/packages/risa/ts/test/ui-views.test.ts
@@ -19,6 +19,7 @@ function system(over: Partial<SystemInfo> = {}): SystemInfo {
     battery: true,
     missing: false,
     roles: [{ kind: "risa", config: {} }],
+    placed: ["risa"],
     ...over,
   } as SystemInfo;
 }
@@ -122,11 +123,13 @@ test("the risa overlay's lines are the old overlay's", () => {
   expect(noVersion).toBe(null);
 });
 
-test("the risa overlay applies to risa systems only, and opens none without the role", () => {
+test("the risa overlay applies to risa systems only, and opens none without the role placed", () => {
   expect(overlayView.when?.(system())).toBeTruthy();
-  expect(overlayView.when?.(system({ roles: [{ kind: "lsdj-sync", config: {} }] }))).toBeFalsy();
+  expect(overlayView.when?.(system({ roles: [{ kind: "lsdj-sync", config: {} }], placed: ["lsdj-sync"] }))).toBeFalsy();
+  // The view asks what the system's graph places, which the shell says in `placed`, not what its roles store.
+  expect(overlayView.when?.(system({ placed: [] }))).toBeFalsy();
   expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({}), 1) : null).toBe(null);
-  const noRole = system({ roles: [] });
+  const noRole = system({ roles: [], placed: [] });
   const sampler =
     overlayView.body.kind === "readout"
       ? overlayView.body.open(hostWith({ rom: risaRom(), systems: [noRole] }), noRole.id)
````

- [ ] **Step 6: run them and see them fail**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/placed-roles
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test lsdj/ui-views
pnpm -C /workspaces/RetroPlug-main/cuttlefish test risa/ui-views
```

Expected: each exits 1. `graph/placed-roles`:

```
not ok 5 - a tracker is resolved by its placed marker role: an mGB cart storing a stale lsdj-sync resolves none
  expected undefined, got {"id":"lsdj","label":"LSDj","markerRole":"lsdj-sync",...}
not ok 6 - a role's config rows show only when the role is placed, and a role with no node's when it is present
  expected ["sameboy-model"], got ["lsdj-divisor","sameboy-model"]
not ok 7 - the DAW lanes are the first placed role's with a table: an LSDj cart storing a stale mgb exposes none
  expected [], got [{"slot":0,"cc":1,"channel":0,"name":"PU1 Pulse Width","shortName":"PU1 Width"},...]
# 4 passed, 0 skipped, 3 failed
```

`lsdj/ui-views`:

```
not ok 2 - the HD view refuses what the old hook gated on: no placed LSDj role, no ROM path, an unreadable header
  expected falsy, got true
not ok 3 - the LSDj overlay's lines are the old overlay's
  expected falsy, got true
# 3 passed, 0 skipped, 2 failed
```

`risa/ui-views`:

```
not ok 2 - the risa overlay applies to risa systems only, and opens none without the role placed
  expected falsy, got true
# 2 passed, 0 skipped, 1 failed
```

- [ ] **Step 7: the readers ask placement**

````patch
diff --git a/packages/lsdj/ts/ui/hdView.ts b/packages/lsdj/ts/ui/hdView.ts
index 75479765c..ba6e420ec 100644
--- a/packages/lsdj/ts/ui/hdView.ts
+++ b/packages/lsdj/ts/ui/hdView.ts
@@ -15,7 +15,7 @@
 // `frame()` returns the pixel buffer when something moved and null when it didn't, so the shell can skip pushing an
 // unchanged surface into LVGL.
 import type { Host } from "@retroplug/core/host";
-import { hasRole, roleConfig } from "@retroplug/core/systemRoles";
+import { isPlaced, roleConfig } from "@retroplug/core/systemRoles";
 import type { PixelFrame, WindowView } from "@retroplug/ui/contribution";
 import { HD_COLS, HD_ROWS, LsdjHdCanvas, renderMode2, type KitSampleNameLookup } from "../hd";
 import { decodeSong } from "../codec/song";
@@ -155,21 +155,22 @@ function buildSession(
   return new HdSession(host, systemId, reader, rom);
 }
 
-/** The playable window view: it applies to an LSDj-sync system, and its `open` refuses a system with no such role or
- *  no ROM path itself, since the shell checks `when` only at `views.open` and a renderer opens a session too. `satisfies`
- *  rather than an annotation, so `open` keeps the host groups it declares (what a pack copied from this one sees)
- *  instead of widening to the contract's whole UI host. */
+/** The playable window view: it applies to a system whose graph places lsdj-sync (not one that merely stores it, an
+ *  mGB cart after a ROM swap), and its `open` refuses any other system or one with no ROM path itself, since the shell
+ *  checks `when` only at `views.open` and a renderer opens a session too. `satisfies` rather than an annotation, so
+ *  `open` keeps the host groups it declares (what a pack copied from this one sees) instead of widening to the
+ *  contract's whole UI host. */
 export const hdView = {
   id: "lsdj-hd",
   label: "HD Player",
   placement: "window",
-  when: (sys) => hasRole(sys.roles, "lsdj-sync"),
+  when: (sys) => isPlaced(sys, "lsdj-sync"),
   body: {
     kind: "pixels",
     unsupported: "HD view needs a supported LSDj cartridge",
     open: (host: Host<"files" | "png" | "systems">, systemId: number) => {
       const sys = host.systems.view().find((s) => s.id === systemId);
-      if (!sys || !hasRole(sys.roles, "lsdj-sync") || !sys.romPath) return null;
+      if (!sys || !isPlaced(sys, "lsdj-sync") || !sys.romPath) return null;
       return buildSession(host, systemId, sys.romPath, roleConfig(sys.roles, "lsdj-assets"));
     },
   },
diff --git a/packages/lsdj/ts/ui/overlayView.ts b/packages/lsdj/ts/ui/overlayView.ts
index 5cb4a4531..885f4e269 100644
--- a/packages/lsdj/ts/ui/overlayView.ts
+++ b/packages/lsdj/ts/ui/overlayView.ts
@@ -8,7 +8,7 @@
 // cart is not supported, so nothing shows; undefined means this frame's RAM was unreadable, so the strip keeps what
 // shows.
 import type { Host } from "@retroplug/core/host";
-import { hasRole } from "@retroplug/core/systemRoles";
+import { isPlaced } from "@retroplug/core/systemRoles";
 import { hex2 } from "@retroplug/ui/format";
 import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
 import { LsdjReader, type LsdjState } from "../runtime";
@@ -38,21 +38,22 @@ function linesOf(state: LsdjState): string[] {
   ];
 }
 
-/** The LSDj debug overlay: a readout over every LSDj tile, shown while the package's flag is on (backtick by
- *  default, through the `lsdj.debug-overlay` action). The default dedupe (the lines and the flag) is exactly what the
- *  strip draws, so the old hook's narrower signature is not needed and the displayed output is identical. */
+/** The LSDj debug overlay: a readout over every tile whose graph places lsdj-sync, shown while the package's flag is on
+ *  (backtick by default, through the `lsdj.debug-overlay` action). The default dedupe (the lines and the flag) is
+ *  exactly what the strip draws, so the old hook's narrower signature is not needed and the displayed output is
+ *  identical. */
 export const overlayView = {
   id: "lsdj-overlay",
   label: "LSDj Debug Overlay",
   placement: "tile",
   visible: lsdjDebug,
-  when: (sys) => hasRole(sys.roles, "lsdj-sync"),
+  when: (sys) => isPlaced(sys, "lsdj-sync"),
   body: {
     kind: "readout",
     open: (host: Host<"files" | "systems">, systemId: number) => {
       const sys = host.systems.view().find((s) => s.id === systemId);
       const romPath = sys?.romPath ?? "";
-      if (!sys || !romPath || !hasRole(sys.roles, "lsdj-sync")) return null;
+      if (!sys || !romPath || !isPlaced(sys, "lsdj-sync")) return null;
       const header = host.files.readPrefix(romPath, 0x150);
       if (!header) return null;
       const reader = LsdjReader.fromHeader(header);
diff --git a/packages/retroplug/src/parameterMap.ts b/packages/retroplug/src/parameterMap.ts
index 0f947bb4e..92a74f111 100644
--- a/packages/retroplug/src/parameterMap.ts
+++ b/packages/retroplug/src/parameterMap.ts
@@ -17,6 +17,7 @@
 import { MidiRouting } from "@retroplug/core/midiRouting";
 import type { RoleRegistry } from "@retroplug/core/systemRoles";
 import type { RomParameter } from "@retroplug/core/romParameters";
+import { placedKinds } from "./graph/protocolNodes";
 
 /** A claimed pool slot: the same descriptor, resolved to a pool index and a pre-routing channel. */
 export interface ParameterSlot extends RomParameter {
@@ -73,7 +74,9 @@ export interface ParameterSystem {
 
 /** Project the loaded systems onto the plugin's fixed slot pool. Slot `i * CC_SLOTS_PER_SYSTEM + n`
  *  belongs to system `i`; every slot not returned stays unclaimed (hidden, generically named). A system's table is its
- *  first role whose type serves `parameters`, asked with that role's own config (a BlipToaster's `chip`, say). */
+ *  first role the graph places (graph/protocolNodes.ts placedKinds) whose type serves `parameters`, asked with that
+ *  role's own config (a BlipToaster's `chip`, say): a stored role the graph leaves out, mgb on an LSDj cart after a ROM
+ *  swap, exposes no lanes for a protocol that is not running, and a role with no node keeps its table. */
 export function projectParameterMap(
   systems: ParameterSystem[],
   mode: MidiRouting,
@@ -84,7 +87,9 @@ export function projectParameterMap(
 
   for (let i = 0; i < Math.min(systemCount, MAX_PARAMETER_SYSTEMS); i++) {
     let table: readonly RomParameter[] | undefined;
+    const placed = placedKinds(systems[i].roles);
     for (const r of systems[i].roles) {
+      if (!placed.includes(r.kind)) continue;
       table = registry.roleType(r.kind)?.parameters?.((r.config ?? {}) as Record<string, unknown>);
       if (table) break;
     }
diff --git a/packages/retroplug/src/trackerRegistry.ts b/packages/retroplug/src/trackerRegistry.ts
index 988232a48..97384569a 100644
--- a/packages/retroplug/src/trackerRegistry.ts
+++ b/packages/retroplug/src/trackerRegistry.ts
@@ -8,15 +8,17 @@ import type { AssetCatalog } from "@retroplug/tracker/assetCatalog";
 import type { SongCatalog } from "@retroplug/tracker/songCatalog";
 import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
 import { TRACKER_INTEGRATIONS } from "./generated/packages";
+import { placedRole } from "./graph/protocolNodes";
 
 /** Every tracker integration, in the product order: a system takes the one whose markerRole it has, and the markers are
  *  exclusive, so the order decides nothing. The generated list, re-exported below as a live binding. */
 export { TRACKER_INTEGRATIONS };
 
-/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
- *  integration's markerRole). undefined for a non-tracker system. */
+/** The tracker integration for a system, resolved from its roles: the integration whose markerRole the graph places
+ *  (graph/protocolNodes.ts placedRole). undefined for a non-tracker system, and for one whose stored marker the graph
+ *  leaves out: an mGB cart still carrying lsdj-sync after a ROM swap is not an LSDj cart. */
 export function resolveTracker(roles: readonly RoleInstance[]): TrackerIntegration | undefined {
-  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
+  return TRACKER_INTEGRATIONS.find((t) => placedRole(roles, t.markerRole) !== undefined);
 }
 
 /** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
diff --git a/packages/retroplug/ui/screens/menu/configRows.ts b/packages/retroplug/ui/screens/menu/configRows.ts
index a021c7668..18a9d66fe 100644
--- a/packages/retroplug/ui/screens/menu/configRows.ts
+++ b/packages/retroplug/ui/screens/menu/configRows.ts
@@ -3,10 +3,11 @@
 // value through the same fallbacks and step through the same label list. A row is data a package declares; what a step
 // DOES is the caller's, because that is the one part only a surface knows (systems.setRoleConfig, a project's
 // controller).
-import { hasRole, roleConfig, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
+import { roleConfig, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
 import type { RoleRow } from "@retroplug/ui/contribution";
 import { configRowCycler, rowShownFor, rowShownOn } from "@retroplug/ui/contribution";
 import { separator, type MenuItem } from "@retroplug/ui/menu";
+import { placedRole } from "../../../src/graph/protocolNodes";
 
 export { configRowCycler };
 
@@ -17,11 +18,12 @@ export interface RoleRowSystem {
   readonly roles: readonly RoleInstance[];
 }
 
-/** The items a set of contributed rows builds for a system, in the rows' order: a row shows only on a system that has
- *  its role (the core role attaches to every system of that core, so this is the old `core === "sameboy"` gate), where
- *  its platform and `when` gates allow, and a `separatorBefore` id is emitted once, before the first row that shows
- *  carrying it. A row's default is its role schema's (`schema.parse({})[key]`), which is what makes an untouched config
- *  read as the value the core really runs. */
+/** The items a set of contributed rows builds for a system, in the rows' order: a row shows only on a system whose
+ *  graph places its role (graph/protocolNodes.ts placedRole: the core role, which has no node, attaches to every system
+ *  of that core, so this is the old `core === "sameboy"` gate, and a stored role the graph leaves out, LSDj's sync on
+ *  an mGB cart after a ROM swap, shows nothing), where its platform and `when` gates allow, and a `separatorBefore` id
+ *  is emitted once, before the first row that shows carrying it. A row's default is its role schema's
+ *  (`schema.parse({})[key]`), which is what makes an untouched config read as the value the core really runs. */
 export function roleRowItems(
   rows: readonly RoleRow[],
   sys: RoleRowSystem,
@@ -32,7 +34,7 @@ export function roleRowItems(
   const emitted = new Set<string>();
   const defaultsOf = new Map<string, Record<string, unknown>>();
   for (const row of rows) {
-    if (!hasRole(sys.roles, row.role)) continue;
+    if (!placedRole(sys.roles, row.role)) continue;
     if (!rowShownOn(row, sys.platform)) continue;
     let defaults = defaultsOf.get(row.role);
     if (!defaults) {
diff --git a/packages/risa/ts/ui/overlayView.ts b/packages/risa/ts/ui/overlayView.ts
index 71099df1c..8b8c6ddcb 100644
--- a/packages/risa/ts/ui/overlayView.ts
+++ b/packages/risa/ts/ui/overlayView.ts
@@ -6,7 +6,7 @@
 // marker is deep in the PRG, so the whole file is read), then every frame pulls the RAM snapshot (host.systems.readRam)
 // and decodes it. Null means the cart is not supported; undefined means this frame's RAM was unreadable.
 import type { Host } from "@retroplug/core/host";
-import { hasRole } from "@retroplug/core/systemRoles";
+import { isPlaced } from "@retroplug/core/systemRoles";
 import { hex2 } from "@retroplug/ui/format";
 import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
 import * as runtime from "../runtime";
@@ -39,13 +39,13 @@ export const overlayView = {
   label: "risa Debug Overlay",
   placement: "tile",
   visible: risaDebug,
-  when: (sys) => hasRole(sys.roles, "risa"),
+  when: (sys) => isPlaced(sys, "risa"),
   body: {
     kind: "readout",
     open: (host: Host<"files" | "systems">, systemId: number) => {
       const sys = host.systems.view().find((s) => s.id === systemId);
       const romPath = sys?.romPath ?? "";
-      if (!sys || !romPath || !hasRole(sys.roles, "risa")) return null;
+      if (!sys || !romPath || !isPlaced(sys, "risa")) return null;
       const rom = host.files.readFile(romPath);
       if (!rom) return null;
       const layout = runtime.resolveRisaLayout(runtime.identifyRisaVersion(rom));
````

- [ ] **Step 8: run the three again**

Same commands as Step 6. Expected: each exits 0, `# 7 passed, 0 skipped, 0 failed` (`graph/placed-roles`),
`# 5 passed, 0 skipped, 0 failed` (`lsdj/ui-views`, its fixture case included) and `# 3 passed, 0 skipped, 0 failed`
(`risa/ui-views`).

- [ ] **Step 9: the wider suites**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:ui
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm -C /workspaces/RetroPlug-main/cuttlefish test
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
```

Expected, each exit 0 (the counts are the per-file `# N passed` lines summed):

- `pnpm test`: `# 239 test file(s) passed (jobs=16)`, 2506 passed, 0 skipped (the boundaries check passes first:
  `check-boundaries: the packages hold their boundaries (0 allowed by EXCEPTIONS, 0 warning(s))`);
- the typecheck exits 0;
- `test:ui`: `# 48 UI test file(s) passed (jobs=16)`, 54 passed;
- `test:native`: `# 173 native test file(s) passed (jobs=16)`, `#   2 case(s) SKIPPED in 2 file(s): dsp-bench(1),
  nes-bench(1)`, 633 passed;
- CI's picture: `pnpm test` 2505 passed and 1 skipped (`lsdj/ui-views(1)`), `test:native` 510 passed and 125 skipped,
  the skip baseline holding for both.

- [ ] **Step 10: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/retroplug/src/graph/protocolNodes.ts \
  packages/core/ts/host.ts \
  packages/core/ts/systemRoles.ts \
  packages/retroplug/src/systemsStore.ts \
  packages/retroplug/src/trackerRegistry.ts \
  packages/retroplug/ui/screens/menu/configRows.ts \
  packages/retroplug/src/parameterMap.ts \
  packages/lsdj/ts/ui/hdView.ts \
  packages/lsdj/ts/ui/overlayView.ts \
  packages/risa/ts/ui/overlayView.ts \
  packages/retroplug/test/graph/placed-roles.test.ts \
  packages/lsdj/ts/test/ui-views.test.ts \
  packages/risa/ts/test/ui-views.test.ts \
  packages/lsdj/ts/test/songImport.test.ts \
  packages/retroplug/test/ui/view-session.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb2): decide what a system is by what its graph places

A system's stored role list can keep a role the graph leaves out: after a ROM swap an mGB cart can still carry
lsdj-sync and an LSDj cart mgb, and protocolPipeline places one writer per system input. resolveTracker (behind the
tracker menus, song import, the render's tracker and the live SAV), the config rows, projectParameterMap and the LSDj
and risa cart views decided by presence, so an mGB cart showed LSDj's sync rows, HD view and overlay, and an LSDj cart
exposed mGB's CC lanes to the DAW.

placedKinds and placedRole, beside protocolPipeline, answer what the graph places (a kind with no protocol node counts
as placed when present, since the pipeline never leaves one out), and those readers ask them. The system view carries
the answer as `placed`, which SystemInfo declares, and a package's view asks it through isPlaced in
@retroplug/core/systemRoles. Stored roles are never removed, so a ROM swapped back brings its settings back.
MSG
```

---

## Task 2: the MI.MAP predictor walks the divisor its cart is set to

rp.lsdj.sync clocks a cart in MI.MAP at `24 / (config.tempoDivisor || 1)` ticks a quarter, while the controller
session walked 24 for its predictor, so at a divisor of 2 or more its playhead, and a launch cued for the end of a row,
ran that many times ahead of the cart. The `launchpad` pack may not import `lsdj` and the placer names no tracker
field, so the sync rule carries the setting's name as data: `lsdj-launchpad`'s manifest declares
`"divisor": "tempoDivisor"`, the kind checks it is a non-empty string, the placer reads `config[rule.divisor]` off the
driven cart's node in `PlaceSystem.pipeline` (1 unless it is a finite number of at least 1) and hands the session
`tickDivisor`, and the session walks `TICKS_PER_QUARTER / tickDivisor`, the node's own expression. `tickDivisor` is read
every block and is not part of the session's shape, so a change while a song plays rebuilds nothing and keeps the
session's cue; the session's walk and the cart's both run through `TickClock`, so they stay equal across the change,
the block that switches resolution included.

Where the design names test files the walk is not in, the cases go where the code is: the walk is the session NODE's
(`controllerSession.ts`), whose tests are `packages/retroplug/test/script/nodes/controller-session.test.ts`
(launchpad's `session.test.ts` hands `ControllerSession` its ticks ready counted), and the lsdj-launchpad case drives
that node over the real `rp.lsdj.sync` through `@retroplug/testing/rig`, so it counts the clocks the cart is sent. A
quantise of `bar` or `beat` still counts the host's bars (the app's `tick` is the host position): at divisor 2 a cart's
phrase lasts two of them. Two exact expectations of the session's settings in `store-graph.test.ts` gain
`tickDivisor: 1`, and its controller case gains a line proving a role edit reaches the session. The generator
fixture's `expected/protocol.ts` moves only if its manifest does, so its stand-in for a controller app's entry gains
the key too. The new real-core case skips without the fixtures, so the skip baseline's `lsdj-launchpad` entry grows.

**Files** (line numbers are the file at `6690c8bc3`):

- Modify: `packages/launchpad/ts/control/kind.ts:15-22, 45-50`, `packages/launchpad/ts/control/placer.ts:15-21, 36-41,
  90-98, 150-189`, `packages/launchpad/ts/controllerSession.ts:49-54, 107-116, 209-215`,
  `packages/lsdj-launchpad/package.json:36-42`.
- Test: `packages/launchpad/ts/test/kind.test.ts:30-73`,
  `packages/retroplug/test/script/nodes/controller-session.test.ts:89-140, 431-436`,
  `packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts:6-14, 337-339`,
  `packages/retroplug/test/graph/build-graph.test.ts:389-419, 548-554`,
  `packages/retroplug/test/systems/store-graph.test.ts:688-699, 1305-1311`,
  `packages/retroplug/test-native/lsdj-launchpad.test.ts:13-21, 75-84, 132-134`,
  `packages/retroplug/scripts/skip-baseline.json:73-80`.
- Test fixtures: `packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json:40-46`,
  `packages/retroplug/test/packages/fixtures/expected/protocol.ts:35-40`.
- Re-recorded: `packages/retroplug/test/graph/golden/build-graph-roles.json` (derived, Step 5).

**Interfaces:**

- Consumes: `PlaceSystem.pipeline` / `PlaceRole.config` (`@retroplug/core/placer`), `KindData`, `TickClock`, the
  session node's `settings` schema (`s.number`), `NodeRig` and `bytesOn` (`@retroplug/testing/rig`), `MIDIMAP_CLOCK` and
  `MIDIMAP_NOTEOFF` (`@retroplug/lsdj/lsdjSyncBytes`), `songRowTicks` (`@retroplug/lsdj/playback`).
- Produces: `ControllerSyncRule.divisor?: string`; the session setting `tickDivisor: number` (default 1);
  `controllerSettings(controller: GraphController, target: number, tickDivisor: number)` (its third parameter is new);
  the file-local `tickDivisorOf(rule, sync, systems): number` in `placer.ts`. No later task uses them.

### Steps

- [ ] **Step 1: write the failing tests**

````patch
diff --git a/packages/launchpad/ts/test/kind.test.ts b/packages/launchpad/ts/test/kind.test.ts
index 6a29a4bfa..aadca8a77 100644
--- a/packages/launchpad/ts/test/kind.test.ts
+++ b/packages/launchpad/ts/test/kind.test.ts
@@ -30,7 +30,7 @@ test("a syncOverride that is not an object is refused", () => {
 
 test("a syncOverride key nothing reads is refused, naming the keys there are", () => {
   expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", of: "off" }))
-    .toThrow('has "of", which nothing reads (its keys are node, mode, listening, off)');
+    .toThrow('has "of", which nothing reads (its keys are node, mode, listening, off, divisor)');
 });
 
 test("a syncOverride with no node or no mode is refused", () => {
@@ -59,6 +59,12 @@ test("a listening value and its off mode must both be strings, and off must diff
     .toThrow('names "midiMap" as both its mode and the mode a cart that is not listening is put in');
 });
 
+test("a divisor must name the setting that divides the node's clock: a non-empty string", () => {
+  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", divisor: 2 }))
+    .toThrow('\'s "divisor" must name the setting that divides that node\'s 24-tick clock, not 2');
+  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", divisor: "" })).toThrow('\'s "divisor" must name');
+});
+
 test("a node type the product wires no protocol node for is refused", () => {
   expect(() => refuse({ node: "rp.lsdj.syncs", mode: "midiMap" }))
     .toThrow('names "rp.lsdj.syncs", which no package declares a protocol node for');
@@ -68,6 +74,7 @@ test("a well-formed override passes, with or without a listening value", () => {
   // A bare call: the harness has no negated `toThrow`, and a throw here fails the case.
   refuse({ node: "rp.lsdj.sync", mode: "midiMap" });
   refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off" });
+  refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off", divisor: "tempoDivisor" });
 });
 
 test("accept fills the app registry and hands the menu the data it applies", () => {
diff --git a/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts b/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
index 251253b51..00ba88016 100644
--- a/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
+++ b/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
@@ -6,9 +6,16 @@
 import { test, expect } from "@retroplug/testing";
 import { FakeLaunchpad } from "@retroplug/launchpad/testSupport/fakeDevice";
 import { ControllerSession } from "@retroplug/launchpad/session";
+import { controllerSessionNode } from "@retroplug/launchpad/controllerSession";
+import { registerControllerApps } from "@retroplug/launchpad/apps";
+import { NodeRig, bytesOn, type BlockSpec } from "@retroplug/testing/rig";
+import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
+import { MIDIMAP_CLOCK, MIDIMAP_NOTEOFF } from "@retroplug/lsdj/lsdjSyncBytes";
+import { songRowTicks } from "@retroplug/lsdj/playback";
+import runtime from "../appRuntime";
 import { lsdjMidiMap, rowAt, channelAt, followRow, WINDOW_ROWS, MAX_PAGE } from "../lsdjMidiMap";
 import { lsdjMidiMapTarget } from "../launchTarget";
-import { Palette, palette, type Led } from "@retroplug/launchpad";
+import { Palette, padIndex, palette, type Led } from "@retroplug/launchpad";
 import { SongSchema, type Song } from "@retroplug/lsdj/model";
 import { PredictedLsdjModel } from "@retroplug/lsdj/playback";
 import { idlePosition, type PlaybackModel, type PlaybackPosition } from "@retroplug/tracker/playbackModel";
@@ -337,3 +344,42 @@ test("driven by a real predictor over a long run, the lit playhead tracks the mo
   }
   expect(checked > 80).toBeTruthy(); // the run really did span several song wraps
 });
+
+test("at a tempo divisor of 2, a launch cued for the row's end reaches the cart as the cart's row ends", () => {
+  // The session node walks the clock the cart hears (24 / tickDivisor a quarter, as rp.lsdj.sync clocks MI.MAP), so
+  // the row its predictor turns over on is the cart's own. Counted in the clocks rp.lsdj.sync sent the cart, the launch
+  // cued while row 0 plays reaches it once row 0 has had all 96 of its ticks; walking every host tick would release it
+  // halfway through, at 48.
+  registerControllerApps([runtime]);
+  const released = (tickDivisor: number): number[] => {
+    const controller = new NodeRig(controllerSessionNode, {
+      app: "lsdj-midimap",
+      target: "system",
+      systemId: 1,
+      appConfig: { quantise: "rowEnd", follow: true, page: 0 },
+      songRowTicks: songRowTicks(fullSong(16)),
+      tickDivisor,
+    });
+    const cart = new NodeRig(lsdjSyncNode, { mode: "midiMap", tempoDivisor: 2 });
+    const pad = (row: number) => [{ frame: 0, data: [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100] }];
+    const at: number[] = []; // how many clocks the cart had been sent when each row byte (not a NoteOff's) reached it
+    let clocks = 0;
+    for (let block = 0; block < 8; block++) {
+      // One second at 120 BPM: 48 host ticks, 24 of the cart's. Row 0 is launched at once (nothing plays yet), row 9 is
+      // cued in the next block.
+      const spec: BlockSpec = { frames: 44100, playing: true, ppq: 2 * block, tempo: 120 };
+      const surface = block === 0 ? pad(0) : block === 1 ? pad(9) : [];
+      const launches = controller
+        .run({ ...spec, inputs: { controllerIn: surface } })
+        .filter((s) => s.port === "launches")
+        .map((s) => ({ frame: s.queued, data: s.data }));
+      for (const [, byte] of bytesOn(cart.run({ ...spec, inputs: { launches } }), "serialIn")) {
+        if (byte === MIDIMAP_CLOCK) clocks++;
+        else if (byte !== MIDIMAP_NOTEOFF) at.push(clocks);
+      }
+    }
+    return at;
+  };
+  expect(released(2)).toEqual([0, 96]);
+  expect(released(1)).toEqual([0, 48]);
+});
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index 79ebbe632..9a1dd9cdd 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -73,8 +73,8 @@
    "total": 1
   },
   "lsdj-launchpad": {
-   "skipped": 2,
-   "total": 4
+   "skipped": 3,
+   "total": 5
   },
   "lsdj-launchpad-role": {
    "skipped": 2,
diff --git a/packages/retroplug/test-native/lsdj-launchpad.test.ts b/packages/retroplug/test-native/lsdj-launchpad.test.ts
index a90315da7..55e06473f 100644
--- a/packages/retroplug/test-native/lsdj-launchpad.test.ts
+++ b/packages/retroplug/test-native/lsdj-launchpad.test.ts
@@ -13,9 +13,11 @@
 import { test, expect, skip } from "@retroplug/testing";
 import { type SavInput } from "@retroplug/lsdj";
 import { launchMessage } from "@retroplug/lsdj-launchpad/launchTarget";
-import { PredictedLsdjModel } from "@retroplug/lsdj/playback";
+import { PredictedLsdjModel, songRowTicks } from "@retroplug/lsdj/playback";
 import { SongSchema } from "@retroplug/lsdj/model";
-import { LsdjProbe, fmtSample, type ProbeProject } from "./lsdjPlaybackProbe";
+import { padIndex } from "@retroplug/launchpad";
+import { createAudioDriver } from "../src/audioDriver";
+import { LsdjProbe, fmtSample, transitions, type ProbeProject, type ProbeSample } from "./lsdjPlaybackProbe";
 import type { GraphController } from "@retroplug/launchpad/placer";
 
 // 130 rows, every one a single-phrase chain, so every row is playable and 96 ticks long. Reaching past
@@ -75,10 +77,10 @@ test("the controller layer's launch bytes move a real cart, on both MIDI channel
 // controller forces its cart's sync mode, so what is under test is the pipeline a project would really produce.
 
 /** A project with the roles a freshly built LSDj cart has, with the STORED (default) sync mode, and `controller`. */
-const lsdjProject = (controller: GraphController | null, mode = "midiSync"): ProbeProject => ({
+const lsdjProject = (controller: GraphController | null, mode = "midiSync", tempoDivisor = 1): ProbeProject => ({
   midiRouting: "sendToAll",
   controller,
-  roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode, tempoDivisor: 1 } }],
+  roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode, tempoDivisor } }],
 });
 
 const enabled = (over: Partial<GraphController> = {}): GraphController => ({
@@ -132,3 +134,47 @@ test("a cart whose OWN SYNC is not MI.MAP is left alone rather than clocked at",
   console.log(`[lsdj-launchpad] cart SYNC=LSDJ, row 42: ${fmtSample(after)}`);
   expect(after.channels.pu1.songRow !== 42).toBe(true);
 });
+
+test("at a tempo divisor of 2, launches cued for a row's end land as the cart's own rows end, row after row", () => {
+  LsdjProbe.closeAll(); // drop the carts earlier tests left running (renderAudio drives them all)
+  // MI.MAP clocks the cart at 24 / tempoDivisor ticks a quarter, so at divisor 2 a row of 96 ticks lasts eight
+  // quarters, 4 s at 120 BPM. The session's predictor walks the same division, so a launch quantised to the end of a
+  // row is released as the cart plays that row's last step, and the cart plays each row through before the cued one.
+  // Walking every host tick, the predictor turned over halfway, and the cart left each row on step 8 of its 16.
+  const ticks = songRowTicks(SongSchema.parse(songJson));
+  const controller = enabled({ appConfig: { quantise: "rowEnd" }, songRowTicks: ticks });
+  const p = LsdjProbe.create({ song: SONG, project: () => lsdjProject(controller, "midiSync", 2) });
+  if (!p) return skip("lsdj-launchpad: aboy ROM not found / unsupported version");
+  const audio = createAudioDriver();
+  const pad = (row: number) =>
+    expect(audio.stageControllerIn([0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100])).toBe(true);
+
+  p.bpm(120);
+  p.transport(true);
+  p.render(100);
+  pad(0); // nothing plays yet, so this launch is not quantised
+  const samples: ProbeSample[] = p.runFree(500, 25);
+  // Each next row is cued half a second into the one before it, once the cart is seen playing that one.
+  for (const [on, next] of [[0, 5], [5, 9], [9, 2]] as const) {
+    for (let waited = 0; p.sample().channels.pu1.songRow !== on && waited < 8000; waited += 25) {
+      samples.push(p.render(25));
+    }
+    samples.push(...p.runFree(500, 25));
+    pad(next);
+  }
+  samples.push(...p.runFree(4500, 25)); // to the end of row 9, the third row of eight quarters
+
+  // The row and the step it was on when the cart left it, for each row it left.
+  const left: { row: number; step: number | null }[] = [];
+  let last: ProbeSample["channels"]["pu1"] | null = null;
+  for (const s of samples) {
+    const pu1 = s.channels.pu1;
+    if (pu1.songRow === null) continue;
+    if (last && pu1.songRow !== last.songRow) left.push({ row: last.songRow!, step: last.phraseRow });
+    if (last?.songRow !== pu1.songRow || pu1.phraseRow !== null) last = pu1;
+  }
+  const rows = transitions(samples, (s) => s.channels.pu1.songRow).filter((r) => r !== null);
+  console.log(`[lsdj-launchpad] divisor 2, rowEnd: rows ${JSON.stringify(rows)}, left ${JSON.stringify(left)}`);
+  expect(rows).toEqual([0, 5, 9, 2]);
+  expect(left.map((l) => l.step)).toEqual([15, 15, 15]);
+});
diff --git a/packages/retroplug/test/graph/build-graph.test.ts b/packages/retroplug/test/graph/build-graph.test.ts
index 34ace8a9b..b9cf761ba 100644
--- a/packages/retroplug/test/graph/build-graph.test.ts
+++ b/packages/retroplug/test/graph/build-graph.test.ts
@@ -389,7 +389,7 @@ test("the controller takes the surface, sends it its LEDs, and its target ('0 is
     type: CONTROLLER_SESSION_NODE_TYPE,
     settings: {
       app: "lsdj-midimap", target: "system", systemId: 7, appConfig: { quantise: "bar", follow: true, page: 0 },
-      songRowTicks: [], anchorRows: [5, 5, null, 5], anchorSeq: 3,
+      songRowTicks: [], anchorRows: [5, 5, null, 5], anchorSeq: 3, tickDivisor: 1,
     },
   });
   // The device's own side: its traffic in and its connected flag, and the session's LEDs back out, each through its own
@@ -414,6 +414,26 @@ test("the controller takes the surface, sends it its LEDs, and its target ('0 is
   expect(without.node("surface-out")).toBe(undefined);
 });
 
+test("the session is handed the divisor its cart's clock is divided by, read from that cart's node", () => {
+  // MI.MAP clocks a cart at 24 / tempoDivisor ticks a quarter, so the session walks the same division (tickDivisor).
+  // The app's rule names the setting (its manifest's `divisor`) and the placer reads it off the pipeline it is handed,
+  // so no shell module names it; what it cannot read is 1, as the node's own `config.tempoDivisor || 1` reads it.
+  const divided = (tempoDivisor: unknown): ProtocolRole =>
+    role("lsdj-sync", { mode: "midiSync", tempoDivisor, autoStart: false });
+  const tickDivisor = (systems: GraphSystem[], over: Partial<GraphController> = {}): unknown =>
+    built(systems, "stereo", multi(2), withController(over)).node("controller")?.settings?.tickDivisor;
+  expect(tickDivisor([gb(7, 0, [divided(4)]), gb(8, 0, [LSDJ()])])).toBe(4);
+  // The cart the session drives, chosen or first, and no other.
+  expect(tickDivisor([gb(7, 0, [LSDJ()]), gb(8, 0, [divided(2)])], { systemId: 8 })).toBe(2);
+  expect(tickDivisor([gb(7, 0, [divided(4)]), gb(8, 0, [LSDJ()])], { systemId: 8 })).toBe(1);
+  // No node of the rule's type on that cart, no such setting, or a value the node would not divide by.
+  expect(tickDivisor([gb(7, 0, [MGB])])).toBe(1);
+  expect(tickDivisor([gb(7, 0, [role("lsdj-sync", { mode: "midiSync" })])])).toBe(1);
+  for (const bad of [0, 0.5, -2, "2", null]) expect(tickDivisor([gb(7, 0, [divided(bad)])]), String(bad)).toBe(1);
+  // Launches out of the host drive no emulated cart, and a real Game Boy's divisor is not one the graph can read.
+  expect(tickDivisor([gb(7, 0, [divided(4)])], { target: "midiOut" })).toBe(1);
+});
+
 test("the controller's launches reach only a 'system' target's lsdj-sync node", () => {
   // Bound for a real Game Boy, they leave the host instead; a target cart with no lsdj-sync node takes them nowhere,
   // its mGB or N8 node included; a chosen id that names no system routes them nowhere.
@@ -548,7 +568,11 @@ test("the sync override is the app's DATA: a node type, a mode, and what counts
   // the whole rule. A second app with a different cart sync node type is a manifest entry, not a branch here.
   expect(Object.keys(KIND_DATA.controllerApps)).toEqual(["lsdj-midimap"]);
   expect(KIND_DATA.controllerApps["lsdj-midimap"])
-    .toEqual({ syncOverride: { node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off" } });
+    .toEqual({
+      syncOverride: {
+        node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off", divisor: "tempoDivisor",
+      },
+    });
   // `driving` is the app's own answer - the mode its launches need is the one the cart runs - and not "the mode is not
   // the string off": a second app's off-mode is its manifest's to spell.
   expect(controllerSyncOverride([{ id: 7 }], { ...CONTROLLER, cartSync: "MidiMap" }))
diff --git a/packages/retroplug/test/script/nodes/controller-session.test.ts b/packages/retroplug/test/script/nodes/controller-session.test.ts
index e3c4a4d9c..405a3bfa8 100644
--- a/packages/retroplug/test/script/nodes/controller-session.test.ts
+++ b/packages/retroplug/test/script/nodes/controller-session.test.ts
@@ -89,10 +89,10 @@ class Wired {
   constructor(over: Record<string, unknown> = {}) {
     this.controller = session(over) as NodeRig<unknown>;
   }
-  run(step: Step, midi: In[] = []): { controller: Sent[]; lsdj: Sent[] } {
-    const controller = this.controller.run(blockOf(step));
+  run(step: Step, midi: In[] = [], frames = 1024): { controller: Sent[]; lsdj: Sent[] } {
+    const controller = this.controller.run(blockOf(step, frames));
     const launches = controller.filter((s) => s.port === "launches").map((s) => ({ frame: s.queued, data: s.data }));
-    const lsdj = this.lsdj.run({ ...blockOf(step), inputs: { midi, launches } });
+    const lsdj = this.lsdj.run({ ...blockOf(step, frames), inputs: { midi, launches } });
     return { controller, lsdj };
   }
 }
@@ -126,6 +126,7 @@ test("settings default as the role's did, and keep the parsed app config and the
     songRowTicks: [],
     anchorRows: [],
     anchorSeq: 0,
+    tickDivisor: 1,
   });
   const given = {
     app: "lsdj-midimap",
@@ -135,6 +136,7 @@ test("settings default as the role's did, and keep the parsed app config and the
     songRowTicks: [[96, null], [96]],
     anchorRows: [1, null, 3, 4],
     anchorSeq: 2,
+    tickDivisor: 2,
   };
   expect(controllerSessionNode.parseSettings(given)).toEqual(given);
   expect(() => controllerSessionNode.parseSettings({ target: "elsewhere" })).toThrow("target: Invalid option");
@@ -431,6 +433,49 @@ test("adopting a new song table does not restart the song", () => {
   expect(dev.lit(0, 4)).toBe(true);
 });
 
+test("at tickDivisor 2 the predictor walks half the ticks a block, as the cart clocked at 24 / 2 hears them", () => {
+  // MI.MAP clocks its cart at 24 / tempoDivisor ticks a quarter (rp.lsdj.sync), so at divisor 2 a row of 96 ticks
+  // lasts eight quarters. The session walks the same division: four blocks of 96 host ticks after the launch are 192 of
+  // the cart's, row 2, where walking every host tick would put the playhead on row 4, two rows ahead of the cart.
+  const twoSeconds = 88200; // 96 ticks a block at divisor 1
+  const lit = (tickDivisor: number): number => {
+    const rig = session({ songRowTicks: table(), tickDivisor });
+    const dev = new Device();
+    dev.feed(rig.run(blockOf({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] }, twoSeconds)));
+    for (let i = 1; i <= 4; i++) dev.feed(rig.run(blockOf({ playing: true, ppq: 4 * i, connected: true }, twoSeconds)));
+    return [0, 1, 2, 3, 4, 5].findIndex((row) => dev.leds.get(pad(0, row)) === Palette.greenBright);
+  };
+  expect(lit(2)).toBe(2);
+  expect(lit(1)).toBe(4);
+});
+
+test("a divisor changed while a song plays reaches the running session at its next block, its cue kept", () => {
+  // tickDivisor is read every block and is not part of the session's shape, so the change rebuilds nothing: no hello,
+  // and the launch cued for the end of row 0 still waiting. The session walks what its cart is clocked, the block the
+  // walk changes resolution in included, so the cued row reaches the cart once the cart has been sent row 0's 96 ticks.
+  const w = new Wired({ appConfig: parsed({ quantise: "rowEnd" }) });
+  const second = 44100; // 48 host ticks a block at 120 BPM
+  let clocks = 0;
+  const run = (step: Step): { controller: Sent[]; rows: number[] } => {
+    const out = w.run(step, [], second);
+    const rows: number[] = [];
+    for (const [, byte] of bytesOn(out.lsdj, "serialIn")) {
+      if (byte === 0xff) clocks++;
+      else if (byte !== 0xfe) rows.push(clocks);
+    }
+    return { controller: out.controller, rows };
+  };
+  expect(run({ ppq: 0, surface: [PRESS_ROW_0] }).rows).toEqual([0]); // nothing was playing: launched at once
+  expect(w.controller.set(settingsOf({ appConfig: parsed({ quantise: "rowEnd" }), tickDivisor: 2 }))).toBe(true);
+  expect(w.lsdj.set({ mode: "midiMap", tempoDivisor: 2 })).toBe(true);
+  const changed = run({ ppq: 2, surface: [press(9)] });
+  expect(surface(changed.controller).some((m) => sameBytes(m, HELLO))).toBe(false); // the same session, not a new one
+  expect(changed.rows).toEqual([]); // cued for the end of row 0
+  const reached: number[] = [];
+  for (let block = 2; block < 8; block++) reached.push(...run({ ppq: 2 * block }).rows);
+  expect(reached).toEqual([96]);
+});
+
 test("an app knob changes behaviour on the next block", () => {
   const secondLaunchFires = (appConfig: Record<string, unknown>): boolean => {
     const rig = quiet();
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index f326081b8..ec09cffef 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -688,12 +688,15 @@ test("a controller session is a node of the document, with the sync mode its car
   expect(be.graphNode("controller")?.settings).toEqual(
     canon({
       app: "lsdj-midimap", target: "system", systemId: id, appConfig: { quantise: "bar", follow: true, page: 0 },
-      songRowTicks: [], anchorRows: [], anchorSeq: 0,
+      songRowTicks: [], anchorRows: [], anchorSeq: 0, tickDivisor: 1,
     }),
   );
   expect(wired(be, "controller.launches", `sys-${id}/lsdj-sync.launches`)).toBe(true);
   expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe("midiMap"); // forced in the document, never saved
   expect(sys.view()[0].roles.find((r) => r.kind === "lsdj-sync")?.config.mode).toBe(LsdjSyncMode.MidiSync);
+  // The session walks the clock its cart hears: the cart's divisor, read off the role's config the pipeline carries.
+  sys.setRoleConfig(id, "lsdj-sync", { tempoDivisor: 4 });
+  expect(be.graphNode("controller")?.settings?.tickDivisor).toBe(4);
   p.setSection(CONTROLLER_SECTION, { enabled: false });
   expect(be.graphNode("controller")).toBe(undefined);
   expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe(LsdjSyncMode.MidiSync);
@@ -1305,7 +1308,7 @@ test("DirectGraph takes the project's controller as one of the graph's models, i
   expect(be.graphNode("controller")?.settings).toEqual(
     canon({
       app: "lsdj-midimap", target: "midiOut", systemId: 1, appConfig: { quantise: "beat", follow: true, page: 0 },
-      songRowTicks: [], anchorRows: [], anchorSeq: 0,
+      songRowTicks: [], anchorRows: [], anchorSeq: 0, tickDivisor: 1,
     }),
   );
   // Bound for a real Game Boy, its launches leave the host, under the system it resolved.
````

- [ ] **Step 2: run them and see them fail**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test launchpad/kind
pnpm -C /workspaces/RetroPlug-main/cuttlefish test script/nodes/controller-session
pnpm -C /workspaces/RetroPlug-main/cuttlefish test lsdj-launchpad/lsdjMidiMap
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/build-graph
pnpm -C /workspaces/RetroPlug-main/cuttlefish test systems/store-graph
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native lsdj-launchpad
```

Expected: each exits 1. `launchpad/kind`:

```
not ok 2 - a syncOverride key nothing reads is refused, naming the keys there are
  expected throw matching "has \"of\", which nothing reads (its keys are node, mode, listening, off, divisor)", got "the controller app a's syncOverride has \"of\", which nothing reads (its keys are node, mode, listening, off)"
not ok 6 - a divisor must name the setting that divides the node's clock: a non-empty string
  expected throw matching "'s \"divisor\" must name the setting that divides that node's 24-tick clock, not 2", got "the controller app a's syncOverride has \"divisor\", which nothing reads (its keys are node, mode, listening, off)"
not ok 8 - a well-formed override passes, with or without a listening value
  the controller app a's syncOverride has "divisor", which nothing reads (its keys are node, mode, listening, off)
# 6 passed, 0 skipped, 3 failed
```

`script/nodes/controller-session`:

```
not ok 2 - settings default as the role's did, and keep the parsed app config and the tables as given
  expected {"app":"","target":"system","systemId":0,"appConfig":{},"songRowTicks":[],"anchorRows":[],"anchorSeq":0,"tickDivisor":1}, got {"app":"","target":"system","systemId":0,"appConfig":{},"songRowTicks":[],"anchorRows":[],"anchorSeq":0}
not ok 25 - at tickDivisor 2 the predictor walks half the ticks a block, as the cart clocked at 24 / 2 hears them
  expected 2, got 4
not ok 26 - a divisor changed while a song plays reaches the running session at its next block, its cue kept
  expected true, got false
# 28 passed, 0 skipped, 3 failed
```

`lsdj-launchpad/lsdjMidiMap`:

```
not ok 21 - at a tempo divisor of 2, a launch cued for the row's end reaches the cart as the cart's row ends
  expected [0,96], got [0,48]
# 20 passed, 0 skipped, 1 failed
```

`graph/build-graph`:

```
not ok 23 - the controller takes the surface, sends it its LEDs, and its target ('0 is the first system') resolved
  expected {"id":"controller",...,"anchorRows":[5,5,null,5],"anchorSeq":3,"tickDivisor":1}}, got {"id":"controller",...,"anchorRows":[5,5,null,5],"anchorSeq":3}}
not ok 24 - the session is handed the divisor its cart's clock is divided by, read from that cart's node
  expected 4, got undefined
not ok 36 - the sync override is the app's DATA: a node type, a mode, and what counts as listening
  expected {"syncOverride":{"node":"rp.lsdj.sync","mode":"midiMap","listening":"MidiMap","off":"off","divisor":"tempoDivisor"}}, got {"syncOverride":{"node":"rp.lsdj.sync","mode":"midiMap","listening":"MidiMap","off":"off"}}
# 39 passed, 0 skipped, 3 failed
```

`systems/store-graph`:

```
not ok 29 - a controller session is a node of the document, with the sync mode its cart needs
  expected {"anchorRows":[],"anchorSeq":0,...,"systemId":74,"target":"system","tickDivisor":1}, got {"anchorRows":[],"anchorSeq":0,...,"systemId":74,"target":"system"}
not ok 53 - DirectGraph takes the project's controller as one of the graph's models, its app config parsed by the placer
  expected {"anchorRows":[],"anchorSeq":0,...,"systemId":1,"target":"midiOut","tickDivisor":1}, got {"anchorRows":[],"anchorSeq":0,...,"systemId":1,"target":"midiOut"}
# 52 passed, 0 skipped, 2 failed
```

`test:native lsdj-launchpad` (which runs `lsdj-launchpad-role` too, unchanged and passing): the real cart left each row
on step 8 of its 16, and the cue for row 2 went to another page's row once the predicted playhead had run a page ahead:

```
[lsdj-launchpad] divisor 2, rowEnd: rows [0,5,9,85], left [{"row":0,"step":8},{"row":5,"step":8},{"row":9,"step":8}]
...
not ok 5 - at a tempo divisor of 2, launches cued for a row's end land as the cart's own rows end, row after row
  expected [0,5,9,2], got [0,5,9,85]
# 4 passed, 0 skipped, 1 failed

# 1/2 native test file(s) FAILED: lsdj-launchpad
```

- [ ] **Step 3: the rule's `divisor`, the placer's read and the session's walk**

````patch
diff --git a/packages/launchpad/ts/control/kind.ts b/packages/launchpad/ts/control/kind.ts
index c9de14693..9d5133316 100644
--- a/packages/launchpad/ts/control/kind.ts
+++ b/packages/launchpad/ts/control/kind.ts
@@ -15,8 +15,9 @@ import { controllerApps, type ControllerAppContribution } from "../registry";
 import { setControllerSyncData } from "./placer";
 
 /** The sync override's fields: the node type it applies to, the mode it sets, the polled SYNC value that counts as the
- *  cart listening, and the mode a cart that is not listening is put in. */
-const SYNC_KEYS = ["node", "mode", "listening", "off"];
+ *  cart listening, the mode a cart that is not listening is put in, and the name of that node's setting that divides
+ *  the clock it sends the cart. */
+const SYNC_KEYS = ["node", "mode", "listening", "off", "divisor"];
 
 /** One app's `syncOverride`, checked: the data a placer applies and a menu reads. */
 function checkSyncOverride(appId: string, sync: unknown, ctx: KindCheckContext): void {
@@ -45,6 +46,14 @@ function checkSyncOverride(appId: string, sync: unknown, ctx: KindCheckContext):
   if (!ctx.nodeTypes.includes(rule.node)) {
     throw new Error(`${where} names ${JSON.stringify(rule.node)}, which no package declares a protocol node for`);
   }
+  // A divisor is read off the node's own config by name (the placer hands the session what it finds there), so it is a
+  // setting's name or nothing: a number here would be read as no setting and leave the session walking every tick.
+  if (rule.divisor !== undefined && (typeof rule.divisor !== "string" || rule.divisor === "")) {
+    throw new Error(
+      `${where}'s "divisor" must name the setting that divides that node's 24-tick clock, not ` +
+        JSON.stringify(rule.divisor),
+    );
+  }
   if (rule.listening === undefined) {
     if (rule.off !== undefined) {
       throw new Error(`${where} names an "off" mode and no "listening" value, so the mode it is for is never used`);
diff --git a/packages/launchpad/ts/control/placer.ts b/packages/launchpad/ts/control/placer.ts
index d65627447..d69d3a1a8 100644
--- a/packages/launchpad/ts/control/placer.ts
+++ b/packages/launchpad/ts/control/placer.ts
@@ -15,7 +15,7 @@
 // mode that node is put in, and the polled SYNC value that counts as the cart listening. The graph reads it through
 // PlaceContext.kindData (generated/protocol.ts's KIND_DATA), and the menu reads the same data from the copy the kind's
 // `accept` handed this package (setControllerSyncData): one shape, one reader, two carriers.
-import { definePlacer, type Placement, type Placed, type PlaceContext } from "@retroplug/core/placer";
+import { definePlacer, type Placement, type Placed, type PlaceContext, type PlaceSystem } from "@retroplug/core/placer";
 import type { KindData } from "@retroplug/core/defineKind";
 import type { ControllerAnchor, RowTicksTable } from "@retroplug/tracker/trackerPlayback";
 import { type WireDoc } from "@retroplug/core/graphDocument";
@@ -36,6 +36,10 @@ export interface ControllerSyncRule {
   readonly listening?: string;
   /** The mode that means "send this cart nothing", for one polled as not listening. */
   readonly off?: string;
+  /** The name of that node's setting that divides the 24-tick clock it sends the cart (LSDj's `tempoDivisor`): the
+   *  session walks the same division (tickDivisorOf), so its predicted playhead stays on the cart's. Absent: the cart
+   *  hears every tick. */
+  readonly divisor?: string;
 }
 
 /** An enabled controller: the user's choices, and what the control plane derived for it. */
@@ -90,9 +94,9 @@ function ruleOfData(data: KindData, app: string): ControllerSyncRule | null {
   if (item === null || typeof item !== "object") return null;
   const rule = (item as { syncOverride?: unknown }).syncOverride;
   if (rule === null || typeof rule !== "object") return null;
-  const { node, mode, listening, off } = rule as ControllerSyncRule;
+  const { node, mode, listening, off, divisor } = rule as ControllerSyncRule;
   if (typeof node !== "string" || node === "" || typeof mode !== "string" || mode === "") return null;
-  return { node, mode, listening, off };
+  return { node, mode, listening, off, divisor };
 }
 
 /** The `lsdj-sync` mode the controller needs the cart it drives to be in, and which system that is.
@@ -150,9 +154,28 @@ export function syncModeFor(rule: ControllerSyncRule, cartSync: string | null):
   return rule.mode;
 }
 
+/** What the cart a controller drives divides its 24-tick clock by: the setting the app's rule names (`divisor`), on
+ *  that system's node of the rule's type, as the pipeline the placer is handed carries its config. 1 when nothing is
+ *  driven, the rule names no setting, the system has no such node or the value is not a finite number of at least 1,
+ *  which is what the node itself clocks by then (rp.lsdj.sync's `config.tempoDivisor || 1`). */
+function tickDivisorOf(
+  rule: ControllerSyncRule | null,
+  sync: { systemId: number } | null,
+  systems: readonly PlaceSystem[],
+): number {
+  if (!rule || !sync || typeof rule.divisor !== "string") return 1;
+  const role = systems.find((s) => s.id === sync.systemId)?.pipeline.find((r) => r.node === rule.node);
+  const value = role?.config[rule.divisor];
+  return typeof value === "number" && Number.isFinite(value) && value >= 1 ? value : 1;
+}
+
 /** The session node's settings (@retroplug/launchpad/controllerSession): its system resolved (0 is the node's none),
- *  and the anchor spelt as the node takes it. */
-export function controllerSettings(controller: GraphController, target: number): Record<string, unknown> {
+ *  the anchor spelt as the node takes it, and the divisor of the clock the cart it drives hears (tickDivisorOf). */
+export function controllerSettings(
+  controller: GraphController,
+  target: number,
+  tickDivisor: number,
+): Record<string, unknown> {
   return {
     app: controller.app,
     target: controller.target,
@@ -163,6 +186,7 @@ export function controllerSettings(controller: GraphController, target: number):
     // rather than yanking the playhead back on every unrelated structure edit.
     anchorRows: controller.anchor?.rows ?? [],
     anchorSeq: controller.anchor?.seq ?? 0,
+    tickDivisor,
   };
 }
 
@@ -178,12 +202,19 @@ export const controllerPlacer = definePlacer<GraphController>({
     const target = controllerTarget(model, systems);
     // The app's sync rule as its manifest declares it, through the context's kind data: what the generated protocol
     // module hands every placer, so the planner carries no registry and no app.
-    const sync = overrideWith(ruleOf(ctx.kindData, model.app), systems, model);
+    const rule = ruleOf(ctx.kindData, model.app);
+    const sync = overrideWith(rule, systems, model);
     // The device's two nodes are placed on their own sides of the session (inputs before it, outputs after), so neither
     // is in a cycle with it; the session's three input wires are its wires, pushed where the session sits.
     const inputs: Placed = { nodes: [{ id: "surface-in", type: SURFACE_IN_NODE_TYPE }], wires: [] };
     const session: Placed = {
-      nodes: [{ id: "controller", type: CONTROLLER_SESSION_NODE_TYPE, settings: controllerSettings(model, target) }],
+      nodes: [
+        {
+          id: "controller",
+          type: CONTROLLER_SESSION_NODE_TYPE,
+          settings: controllerSettings(model, target, tickDivisorOf(rule, sync, systems)),
+        },
+      ],
       wires: [
         { from: "surface-in.in", to: "controller.controllerIn" },
         { from: "surface-in.connected", to: "controller.controllerConnected" },
diff --git a/packages/launchpad/ts/controllerSession.ts b/packages/launchpad/ts/controllerSession.ts
index 1a9b60738..ddd3da015 100644
--- a/packages/launchpad/ts/controllerSession.ts
+++ b/packages/launchpad/ts/controllerSession.ts
@@ -49,6 +49,10 @@ const settings = s.object({
    *  number so one anchor is applied once. Empty and 0 on the real-hardware path. */
   anchorRows: s.json<(number | null)[]>([]),
   anchorSeq: s.int({ min: 0, max: 0x7fffffff, default: 0 }),
+  /** What the cart the launches drive divides its 24-tick clock by: its sync node's own divisor setting, which the
+   *  app's sync rule names and the placer reads off the pipeline (1 when it cannot). Read every block and not part of
+   *  the session's shape, so a divisor changed while a song plays reaches the running session at its next block. */
+  tickDivisor: s.number(1),
 });
 
 export type ControllerSessionSettings = Infer<typeof settings>;
@@ -107,10 +111,10 @@ export const controllerSessionNode = defineNode<ControllerSessionSettings>({
     // The session's whole input for a block, one object refilled each block so none is built per block.
     const frame = { input, tick: 0, transport: false, ticks: 0 };
     // The clock the cart is sent, walked here so the predictor can be given the COUNT of ticks a block fired. It is the
-    // walk the cart's own protocol node runs (packages/core/ts/ticks.ts), at 24 ticks a quarter. What it counts is what
-    // MI.MAP would clock at divisor 1: an app's override sets the cart's sync MODE, never its tempoDivisor, and MI.MAP
-    // clocks the cart at 24 / tempoDivisor, so at divisor 2 and above this walk runs that many times ahead of the cart -
-    // the predicted playhead and the quantise with it. A cart in a mode that clocks only on a launch counts its own
+    // walk the cart's own protocol node runs (packages/core/ts/ticks.ts), at its resolution: an app's override sets the
+    // cart's sync MODE and never its divisor, and MI.MAP clocks the cart at 24 / tempoDivisor ticks a quarter, so the
+    // walk divides by the cart's divisor (tickDivisor) or the predicted playhead, and a launch cued for the end of a
+    // row, would run that many times ahead of the cart. A cart in a mode that clocks only on a launch counts its own
     // either way. The callback only counts, and is made once so a block allocates nothing for it.
     const clock = new TickClock();
     let walked = 0;
@@ -209,7 +213,7 @@ export const controllerSessionNode = defineNode<ControllerSessionSettings>({
         // playhead behind the cart for good. Counting the walk gives the predictor exactly the ticks the cart heard.
         clock.begin(b);
         counted = 0;
-        clock.walk(TICKS_PER_QUARTER, countTick);
+        clock.walk(TICKS_PER_QUARTER / (config.tickDivisor >= 1 ? config.tickDivisor : 1), countTick);
         const elapsed = walked;
         walked = counted;
 
diff --git a/packages/lsdj-launchpad/package.json b/packages/lsdj-launchpad/package.json
index 0324386c0..63c974044 100644
--- a/packages/lsdj-launchpad/package.json
+++ b/packages/lsdj-launchpad/package.json
@@ -36,7 +36,8 @@
               "node": "rp.lsdj.sync",
               "mode": "midiMap",
               "listening": "MidiMap",
-              "off": "off"
+              "off": "off",
+              "divisor": "tempoDivisor"
             }
           }
         ]
````

- [ ] **Step 4: run the six again, and the role goldens**

Same commands as Step 2, then:

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/build-graph-role-goldens
```

Expected: the six exit 0: `# 9 passed` (`launchpad/kind`), `# 31 passed` (`script/nodes/controller-session`),
`# 21 passed` (`lsdj-launchpad/lsdjMidiMap`), `# 42 passed` (`graph/build-graph`), `# 54 passed`
(`systems/store-graph`), each with `0 skipped, 0 failed`, and the native one:

```
[lsdj-launchpad] divisor 2, rowEnd: rows [0,5,9,2], left [{"row":0,"step":15},{"row":5,"step":15},{"row":9,"step":15}]
...
ok 5 - at a tempo divisor of 2, launches cued for a row's end land as the cart's own rows end, row after row
# 5 passed, 0 skipped, 0 failed
# 2 native test file(s) passed (jobs=16)
```

The role goldens now fail, structurally (every case with a controller, its session's settings gaining `tickDivisor`):

```
not ok 2 - the goldens are what buildGraph builds for every case, and nothing else
  expected [], got ["controller/system/first","controller/system/chosen-cart-off","controller/midiOut","controller/no-systems","rig/controller/system","rig/controller/midiOut","rig/controller-orders/system","rig/controller-orders/midiOut","rig/stress/controller","rig/stress/controller-no-systems","rig/controller-survives/one-cart","rig/controller-survives/system-added"]
```

- [ ] **Step 5: the generator fixture, and the role goldens re-recorded**

The fixture tree's stand-in for a controller app's manifest entry carries the new key, and its expected module with it:

````patch
diff --git a/packages/retroplug/test/packages/fixtures/expected/protocol.ts b/packages/retroplug/test/packages/fixtures/expected/protocol.ts
index 29f41121f..326a8eebc 100644
--- a/packages/retroplug/test/packages/fixtures/expected/protocol.ts
+++ b/packages/retroplug/test/packages/fixtures/expected/protocol.ts
@@ -35,6 +35,6 @@ export const KIND_DATA: Readonly<Record<string, KindData>> = {
   "widgets": {
   },
   "controllerApps": {
-    "alpha-app": {"syncOverride":{"node":"rp.alpha.midi","mode":"midiMap","listening":"MidiMap","off":"off"}},
+    "alpha-app": {"syncOverride":{"node":"rp.alpha.midi","mode":"midiMap","listening":"MidiMap","off":"off","divisor":"tempoDivisor"}},
   },
 };
diff --git a/packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json b/packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json
index 006806e75..1c8f3f5d5 100644
--- a/packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json
+++ b/packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json
@@ -40,7 +40,8 @@
               "node": "rp.alpha.midi",
               "mode": "midiMap",
               "listening": "MidiMap",
-              "off": "off"
+              "off": "off",
+              "divisor": "tempoDivisor"
             }
           }
         ]
````

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish test packages/packages-generator
RP_GOLDEN_UPDATE=1 pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/build-graph-role-goldens
git -C /workspaces/RetroPlug-main/cuttlefish diff -U0 -- packages/retroplug/test/graph/golden/build-graph-roles.json | node -e '
const d = require("fs").readFileSync(0, "utf8").split("\n");
const minus = d.filter((l) => l.startsWith("-") && !l.startsWith("---")).map((l) => l.slice(1));
const plus = d.filter((l) => l.startsWith("+") && !l.startsWith("+++")).map((l) => l.slice(1));
const ok = minus.length === plus.length && plus.every((l, i) => l.split(",\"tickDivisor\":1").join("") === minus[i]);
console.log(`${minus.length} line(s) changed, each only by ,"tickDivisor":1: ${ok}`);
process.exit(ok ? 0 : 1);'
git -C /workspaces/RetroPlug-main/cuttlefish hash-object packages/retroplug/test/graph/golden/build-graph-roles.json
pnpm -C /workspaces/RetroPlug-main/cuttlefish test graph/build-graph-role-goldens
```

Expected: the generator suite `# 134 passed, 0 skipped, 0 failed`; the re-record `# recorded 37 cases into
test/graph/golden/build-graph-roles.json`; the proof `11 line(s) changed, each only by ,"tickDivisor":1: true` (exit 0:
the file holds a document a line, and each of the eleven documents with a controller gained that field and nothing
else); the hash `b6b93c66892c5504e96b2ec40f6f744b941f4e81`; and the goldens `# 4 passed, 0 skipped, 0 failed`.

- [ ] **Step 6: the wider suites**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:ui
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm -C /workspaces/RetroPlug-main/cuttlefish test
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:plugin
```

Expected, each exit 0:

- `pnpm test`: `# 239 test file(s) passed (jobs=16)`, 2511 passed, 0 skipped; it runs both scenario files
  (`launchpad/scenario`, `lsdj-launchpad/scenario`), whose transcripts stay byte-identical between Node and MicroQuickJS
  and unchanged (at divisor 1 the walk reaches no output);
- the typecheck exits 0; `test:ui`: 54 passed in 48 files;
- `test:native`: `# 173 native test file(s) passed (jobs=16)`, 634 passed, the same 2 skipped (dsp-bench, nes-bench);
- CI's picture: `pnpm test` 2510 passed and 1 skipped, `test:native` 510 passed and 126 skipped (`lsdj-launchpad(3)`
  among them, as the baseline now allows);
- `test:plugin`: `# 14 plugin test binary(ies) passed (jobs=16)`, `retroplug-engine-graph-test` with
  `All tests passed (1714300 assertions in 341 test cases)` (its BuildGraphGoldens and ProtocolNodes cases plan and run
  the re-recorded documents; the ProtocolNodes digests are unchanged).

- [ ] **Step 7: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/launchpad/ts/control/kind.ts \
  packages/launchpad/ts/control/placer.ts \
  packages/launchpad/ts/controllerSession.ts \
  packages/lsdj-launchpad/package.json \
  packages/launchpad/ts/test/kind.test.ts \
  packages/retroplug/test/script/nodes/controller-session.test.ts \
  packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts \
  packages/retroplug/test/graph/build-graph.test.ts \
  packages/retroplug/test/systems/store-graph.test.ts \
  packages/retroplug/test-native/lsdj-launchpad.test.ts \
  packages/retroplug/scripts/skip-baseline.json \
  packages/retroplug/test/packages/fixtures/tree/packages/alpha/package.json \
  packages/retroplug/test/packages/fixtures/expected/protocol.ts \
  packages/retroplug/test/graph/golden/build-graph-roles.json
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb2): walk the MI.MAP predictor at the divisor its cart is set to

rp.lsdj.sync clocks a cart in MI.MAP at 24 / tempoDivisor ticks a quarter, but the controller session walked 24 for
its predictor, so at a divisor of 2 or more its playhead, and a launch cued for the end of a row, ran that many times
ahead of the cart: on a real LSDj at divisor 2 each cued row landed halfway through the one before it.

A controller app's sync rule gains an optional `divisor`, the name of the sync node's setting that divides its clock,
which lsdj-launchpad's manifest declares as tempoDivisor and the launchpad kind checks. The placer reads that setting
off the driven cart's node in the pipeline it is handed (1 unless it is a finite number of at least 1, as the node
reads it) and hands the session `tickDivisor`, and the session walks 24 / tickDivisor, reading it every block, so a
divisor changed while a song plays reaches it without a rebuild. The role goldens are re-recorded: each controller
session's settings gain tickDivisor 1, and nothing else moved.
MSG
```

---

## Task 3: MI.OUT feeds the Game Boy 0, as the Arduinoboy does

In `drive` capture `SameBoySystem::writeAudioSample` clocks one bit a host audio frame while `SC == 0x80`, and fed 1
back on the Game Boy's serial input, where the Arduinoboy (`Mode_LSDJ_Midiout.ino`, `getIncommingSlaveByte`,
`GB_SET(clock, 0, 0)`) holds it at 0. LSDj's serial interrupt re-arms the port (`SC = $80` at `$363C`) after every
transfer, so the capture also clocks bits the CPU never wrote: as 1s each read as a start bit and took the next 7 real
bits as its payload, re-framing the stream by an amount the host rate set. Fed 0, the decode is the song's at every
rate. LSDj's own writes do not change, and neither does any clock, frame or audio sample.

What moves with it: the MI.OUT real-core test becomes one file a rate (44.1, 48 and 96 kHz, each its own host process
so its engine is empty when it sets the rate) through a shared helper, holding the exact sequence; `render-blocks`'
`0xF8` check becomes a NoteOn check; dsp-bench's MI.OUT window counts NoteOns; the two `HostIo.test.cpp` cases built on
mGB's idle line keep their structural checks and hold that the line, `0x00` now, decodes to nothing. The four parity
cases re-record in their MIDI-out and work-RAM hashes alone (LSDj stores the received byte at `$CC20`: `$00` now, as on
hardware, where it was `$FF`).

**The `0x7F` path.** LSDj 9.3.3's MI.OUT sender (bank 3, `$7ADE`-`$7BBC`, the only callers of the send routine at
`$7BBD`/`$7BE4`) sends a command (`$70`-`$7B`) and its value for each N, X, Y and Q command, and `$7D`/`$7E` at its
transport's edges; no setting adds a `$7F`, and the ROM's other writers of the byte to send (`$CC1F`) never reach the
MI.OUT sender. But N, X and Y send their value raw (`$7B7E`: `ld a,($ff8f)`, no range check), so an N command whose
value is `$7F` puts the byte an Arduinoboy reads as a clock on the wire. Measured on the real core with the fix: N
values `48 7F 52 7F 55 7F 60 7F` decode to `FA, NoteOn 48, F8, NoteOn 52, F8, ...`. So the clock path gets a real-core
case (`midiOutClockCase`, the 44.1 kHz file's second case), beside the decoder's unit test.

**Files** (line numbers are the file at `6690c8bc3`):

- Modify: `packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp:678-696` (the comment block above the capture
  and the fed bit), `packages/lsdj/ts/lsdjArduinoboy.ts:6-17` (the module comment's framing paragraph).
- Create: `packages/retroplug/test-native/lsdjMidiOut.ts` (the shared cases),
  `packages/retroplug/test-native/lsdj-midiout-48k.test.ts`, `packages/retroplug/test-native/lsdj-midiout-96k.test.ts`.
- Rewrite: `packages/retroplug/test-native/lsdj-midiout.test.ts` (whole file).
- Test: `packages/retroplug/test-native/parity/render-blocks.test.ts:225-231`,
  `packages/retroplug/test-native/dsp-bench.test.ts:304-382`,
  `packages/native/test/engine-graph/HostIo.test.cpp:882-997` (the two cases),
  `packages/retroplug/scripts/skip-baseline.json:93-98`.
- Re-recorded: `packages/retroplug/test-native/parity/baseline/lsdj-modes.json` and
  `packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj.json` (derived, Step 6).

**Interfaces:**

- Consumes: `GB_serial_set_data_bit` (SameBoy's `Core/gb.c`), `createAudioDriver`'s `setSampleRate`, `renderAudio`,
  `drainMidiOut`, `pressButton`, `directGraph`, `savFrom`; `serialStamped`, `withRoms`, `stereoDocument`,
  `graphRequest`, `docNode`, `docWire`, `loopOut` and `block` (HostIo.test.cpp's own helpers);
  `SameBoySystem::serialOutLog_`.
- Produces: `midiOutCase(rate: number): void`, `midiOutClockCase(): void` and `EXPECTED_MIDI_OUT` in
  `packages/retroplug/test-native/lsdjMidiOut.ts`. No later task uses them.

### Steps

- [ ] **Step 1: the tests, as they must read once the capture feeds 0**

````patch
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index dba6a1113..85492caa6 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -882,14 +882,14 @@ TEST_CASE("master sync's stop after Engine::setSampleRate drops the rate during
     CHECK(e.faultCount() == 0);
 }
 
-TEST_CASE("what a Game Boy's core captures decodes through rp.lsdj.serial-out into Engine::midiOut inside each block",
+TEST_CASE("what a Game Boy's core captures reaches rp.lsdj.serial-out, where mGB's idle line decodes to nothing",
           "[engine-graph][host-io]") {
-    // A real core's capture through the real decoder into the host's MIDI out, with no fixture: mGB, with capture
-    // armed in drive, has its idle line clocked out of its serial port, 0xFF byte after 0xFF byte (after one other as
-    // it boots), and MI.OUT reads 0xFF as a wire frame of flag 1 and payload 0x7F, LSDj's clock, so a 0xF8 at the frame
-    // of the byte that completes it. In ragged blocks, a 1-frame one among them, each block's messages lie inside it,
-    // each at the frame of a byte the core captured in that block (a wire frame straddling two blocks completes in the
-    // later one), at most one a byte, and Engine::midiOut is frame-sorted.
+    // A real core's capture into the real decoder, with no fixture: mGB, with capture armed in drive, has its idle line
+    // clocked out of its serial port. An Arduinoboy holds the Game Boy's serial input low, and so does the capture, so
+    // every bit the core shifts in is 0 and its port carries 0x00 byte after 0x00 byte (after one other as it boots),
+    // which MI.OUT reads as idle bits: the decoder sends nothing, where one stray 1 bit would be a start bit and a
+    // message LSDj never sent. In ragged blocks, a 1-frame one among them. What LSDj's own bytes decode to, at their
+    // frames, is the fixture-gated cases' (test-native/lsdj-midiout and the parity groups' MI.OUT cases).
     Engine e(kRate);
     rp::host::test::Roms roms;
     std::vector<DocSystem> systems{{1, kGb, 0, R"("model":"dmgB","serialOutCapture":"drive")"}};
@@ -901,48 +901,30 @@ TEST_CASE("what a Game Boy's core captures decodes through rp.lsdj.serial-out in
     SameBoySystem* const gb = dynamic_cast<SameBoySystem*>(e.findSystem(1));
     REQUIRE(gb != nullptr);
     const std::uint32_t sizes[] = {1024, 1, 333, 64, 2048, 7};
-    std::size_t clocks = 0, bytes = 0;
-    bool spread = false;
+    std::size_t bytes = 0, idle = 0;
     for (int b = 0; b < 600; ++b) {
         const std::uint32_t frames = sizes[b % std::size(sizes)];
         block(e, frames);
-        const auto stamped = serialStamped(*gb, frames);
-        const auto& out = e.midiOut();
-        bool clocksOnly = true, inside = true, sorted = true, atBytes = true;
-        std::size_t j = 0;  // the next captured byte a message may lie at
-        for (std::size_t i = 0; i < out.size(); ++i) {
-            const auto& m = out[i];
-            clocksOnly = clocksOnly && m.system == kLoop && m.data == std::vector<std::uint8_t>{0xF8};
-            inside = inside && m.frame < frames;
-            sorted = sorted && (i == 0 || out[i - 1].frame <= m.frame);
-            while (j < stamped.size() && stamped[j].first != m.frame) ++j;
-            atBytes = atBytes && j < stamped.size();  // at a captured byte's frame, after the last message's byte
-            ++j;
+        for (const auto& [frame, byte] : serialStamped(*gb, frames)) {
+            idle += byte == 0x00 ? 1 : 0;
+            ++bytes;
         }
         INFO("block " << b << ", " << frames << " frames");
-        CHECK(clocksOnly);
-        CHECK(inside);
-        CHECK(sorted);
-        CHECK(atBytes);
-        clocks += out.size();
-        bytes += stamped.size();
-        spread = spread || (out.size() > 1 && out.front().frame != out.back().frame);
+        CHECK(e.midiOut().empty());
     }
-    CHECK(clocks > 1000);
-    // A clock a byte, but for the one other byte as it boots and the one that completes an unpaired value.
-    CHECK(clocks + 2 >= bytes);
-    CHECK(spread);  // the frames are the bytes', not one frame for the whole block
+    CHECK(bytes > 1000);      // the port is clocked, and its bytes reach the decoder every block
+    CHECK(idle + 1 >= bytes);  // all of them idle but the one as it boots
     CHECK(e.scriptThrows() == 0);
     CHECK(e.droppedMessages() == 0);
 }
 
 TEST_CASE("one setGraph switches a Game Boy's capture and its decoder's mode together, in the same block",
           "[engine-graph][host-io][settings]") {
-    // MI.OUT drives mGB's port, and its decoder reads the idle 0xFF bytes as LSDj's clock; master sync only listens,
-    // and mGB, waiting on an external clock, sends nothing. The capture is a setting of the Game Boy's node and the
-    // mode one of its decoder's (docs/modular-graph-phase-c2-design.md section 4), so one document carries both and
-    // they land in one block: the first block of the plan that carries them captures in the new mode and decodes in it.
-    // Moved by two paths, the capture a block behind, master sync would decode a block of MI.OUT's bytes as clocks,
+    // MI.OUT drives mGB's port, whose idle line its decoder reads as nothing; master sync only listens, and mGB,
+    // waiting on an external clock, sends nothing. The capture is a setting of the Game Boy's node and the mode one of
+    // its decoder's (docs/modular-graph-phase-c2-design.md section 4), so one document carries both and they land in
+    // one block: the first block of the plan that carries them captures in the new mode and decodes in it. Moved by
+    // two paths, a capture in drive under a master sync decoder would decode a block of MI.OUT's bytes as clocks,
     // starting a run (docs/lsdj-master-sync-fix-design.md section 10).
     Engine e(kRate);
     rp::host::test::Roms roms;
@@ -960,23 +942,17 @@ TEST_CASE("one setGraph switches a Game Boy's capture and its decoder's mode tog
                                         std::string(R"("model":"dmgB","serialOutCapture":")") + capture + "\""}};
         return graphRequest(stereoDocument(systems, extras), {1});
     };
-    const auto clocksOnly = [&] {
-        for (const auto& m : e.midiOut())
-            if (m.data != std::vector<std::uint8_t>{0xF8}) return false;
-        return !e.midiOut().empty();
-    };
     REQUIRE(e.setGraph(switched("drive", "midiOut")).ok);
     const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
     REQUIRE(gb);
-    // mGB opens its port once the boot ROM has run, about 2.5 s in; then every block clocks.
+    // mGB opens its port once the boot ROM has run, about 2.5 s in; then every block clocks a block's worth of bytes.
     int b = 0;
-    while (b < 200 && e.midiOut().empty()) {
+    do {
         block(e);
         ++b;
-    }
+        CHECK(e.midiOut().empty());  // the idle line, 0s, decodes to nothing
+    } while (b < 200 && gb->serialOutLog_.size() <= 64);
     REQUIRE(b < 200);
-    block(e);
-    REQUIRE(clocksOnly());
 
     REQUIRE(e.setGraph(switched("listen", "masterSync")).ok);
     CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);  // the plan is adopted; the capture waits for its block
@@ -989,9 +965,9 @@ TEST_CASE("one setGraph switches a Game Boy's capture and its decoder's mode tog
     }
 
     REQUIRE(e.setGraph(switched("drive", "midiOut")).ok);
-    block(e);  // its first block captures in drive and decodes in MI.OUT
+    block(e);  // its first block captures in drive and decodes in MI.OUT, which a master sync decoder would clock on
     CHECK(gb->serialOutLog_.size() > 64);
-    CHECK(clocksOnly());
+    CHECK(e.midiOut().empty());
     CHECK(e.scriptThrows() == 0);
     CHECK(e.droppedMessages() == 0);
 }
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index 9a1dd9cdd..d2b9f34be 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -93,6 +93,14 @@
    "total": 2
   },
   "lsdj-midiout": {
+   "skipped": 2,
+   "total": 2
+  },
+  "lsdj-midiout-48k": {
+   "skipped": 1,
+   "total": 1
+  },
+  "lsdj-midiout-96k": {
    "skipped": 1,
    "total": 1
   },
diff --git a/packages/retroplug/test-native/dsp-bench.test.ts b/packages/retroplug/test-native/dsp-bench.test.ts
index 88a23e06e..f2af501dd 100644
--- a/packages/retroplug/test-native/dsp-bench.test.ts
+++ b/packages/retroplug/test-native/dsp-bench.test.ts
@@ -304,9 +304,9 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
   // The lsdj-sync role's two MIDI-out modes run their decoders, in rp.lsdj.serial-out after the Game Boy, only on what
   // a playing LSDj clocks out, so each has a window of its own on a real LSDj, the mGB systems gone: master sync on a
   // stock LSDj in SYNC=LSDj (a byte a 24-PPQN tick, which the core captures in `listen`, as lsdj-mastersync.test.ts
-  // runs it), and MI.OUT on an Arduinoboy build in SYNC=MI.OUT (its clock and the N commands' NoteOns, captured in
-  // `drive`, as lsdj-midiout.test.ts runs it). Each window is held to the mGB window's zero, the clocks its decoder
-  // sent say LSDj played through it, and the same proof as above says the window counted.
+  // runs it), and MI.OUT on an Arduinoboy build in SYNC=MI.OUT (the N commands' NoteOns, captured in `drive`, as
+  // lsdj-midiout.test.ts runs it). Each window is held to the mGB window's zero, what its decoder sent (master sync's
+  // clocks, MI.OUT's NoteOns) says LSDj played through it, and the same proof as above says the window counted.
   //
   // RP_BENCH_LSDJ=0 leaves them out, as tools/run-profile.sh's valgrind modes do by default: their boots and warmups do
   // not follow RP_BENCH_BLOCKS, so under valgrind they would make the run several times longer and fill the census
@@ -319,15 +319,15 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
   for (const id of ids) graph.remove(id);
   const be = createRealBackend();
   // A window of RP_BENCH_BLOCKS, but at most 400 blocks (9.3 s), the LSDj windows coming on top of the mGB one, and at
-  // least 100 (2.3 s): MI.OUT's clocks come unevenly (at the default warmup none in the window's first 20 blocks, 8 in
-  // its first 100 and 40 in 400), so a shorter window can fail the playing check below for want of them.
+  // least 100 (2.3 s), so the playing check below counts a phrase or more of MI.OUT's NoteOns (eight a 1.9 s phrase).
   const lsdjBlocks = Math.max(100, Math.min(blocks, 400));
   const lsdjWindow = (
     workload: string,
     romPath: string,
     song: SavInput,
     mode: string,
-    minClocks: number,
+    played: "clocks" | "noteOns",
+    min: number,
   ): void => {
     if (!be.fileExists(romPath)) skip(`dsp-bench: the ${workload} window needs the LSDj ROM at ${romPath}`);
     const id = extra + 1;
@@ -364,12 +364,12 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
         }),
     );
     expect(w.blockCount, `${workload}: blocks measured`).toBe(lsdjBlocks);
-    // LSDj played through the window: its decoder sent more clocks than the bound, which is most of what its tempo
-    // sends in master sync (40 a second, of about 51 at tempo 128, a byte a 24-PPQN tick) and half of it in MI.OUT (2
-    // a second, of about 4). The bound is the default warmup's, as the zero is: MI.OUT's clocks come unevenly, and a
+    // LSDj played through the window: its decoder sent more than the bound, which is most of what LSDj sends: master
+    // sync's clocks (40 a second, of about 51 at tempo 128, a byte a 24-PPQN tick) and MI.OUT's NoteOns (3 a second, of
+    // about 4.3: the phrase's eight N commands every 1.9 s). The bound is the default warmup's, as the zero is: a
     // shorter warmup starts the window earlier.
     if (warmup >= DEFAULT_WARMUP) {
-      expect(clocks, `${workload}: clocks decoded in the window`).toBeGreaterThan(minClocks);
+      expect(played === "clocks" ? clocks : notes, `${workload}: ${played} decoded in the window`).toBeGreaterThan(min);
       expect(w.allocCalls, `${workload}: allocations in the measured window`).toBe(0);
       expect(w.maxBlockAllocCalls, `${workload}: allocations in the worst block`).toBe(0);
     }
@@ -377,6 +377,6 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
     expectWindowLive();
   };
   const seconds = (lsdjBlocks * BLOCK_MS) / 1000;
-  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", 40 * seconds);
-  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", 2 * seconds);
+  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", "clocks", 40 * seconds);
+  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", "noteOns", 3 * seconds);
 });
diff --git a/packages/retroplug/test-native/lsdj-midiout-48k.test.ts b/packages/retroplug/test-native/lsdj-midiout-48k.test.ts
new file mode 100644
index 000000000..2d07992c2
--- /dev/null
+++ b/packages/retroplug/test-native/lsdj-midiout-48k.test.ts
@@ -0,0 +1,8 @@
+// Arduinoboy MIDIOUT (MI.OUT, mode 7) at 48 kHz: the case lsdj-midiout.test.ts runs at 44.1 (./lsdjMidiOut.ts), at a
+// rate whose clocking once decoded the song into other notes and Program Changes it never sends.
+import { test } from "@retroplug/testing";
+import { midiOutCase } from "./lsdjMidiOut";
+
+test("MIDIOUT (mode 7) at 48 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays", () => {
+  midiOutCase(48000);
+});
diff --git a/packages/retroplug/test-native/lsdj-midiout-96k.test.ts b/packages/retroplug/test-native/lsdj-midiout-96k.test.ts
new file mode 100644
index 000000000..ee184134f
--- /dev/null
+++ b/packages/retroplug/test-native/lsdj-midiout-96k.test.ts
@@ -0,0 +1,8 @@
+// Arduinoboy MIDIOUT (MI.OUT, mode 7) at 96 kHz: the case lsdj-midiout.test.ts runs at 44.1 (./lsdjMidiOut.ts), at a
+// rate whose clocking once lost all but five of the song's twenty NoteOns.
+import { test } from "@retroplug/testing";
+import { midiOutCase } from "./lsdjMidiOut";
+
+test("MIDIOUT (mode 7) at 96 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays", () => {
+  midiOutCase(96000);
+});
diff --git a/packages/retroplug/test-native/lsdj-midiout.test.ts b/packages/retroplug/test-native/lsdj-midiout.test.ts
index 04538ed27..2a651d34b 100644
--- a/packages/retroplug/test-native/lsdj-midiout.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout.test.ts
@@ -1,116 +1,18 @@
-// Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) — the full end-to-end functional proof on a
-// real LSDj core. A song authored with SYNC=MI.OUT + N (NoteOn) commands, once playing, emits the
-// Arduinoboy protocol on LSDj's serial-out port; native captures those raw bytes (`drive`, the Game Boy node's
-// capture setting the mode places) and feeds them, in the block it captured them, to the lsdj-sync role's
-// decoder after the Game Boy (rp.lsdj.serial-out), where the mode-7 decoder strips the flag-gated framing +
-// decodes the byte protocol into host MIDI (its midiOut port), each message at the frame of the byte that
-// completes it. We drain that MIDI and assert a transport START (0xFA), a clock stream (0xF8), and NoteOns
-// (0x9x), at frames inside their blocks.
+// Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) at 44.1 kHz: the full end-to-end proof on a real LSDj
+// core, that the MIDI its decoder sends is exactly what the song plays, and the one way a real LSDj reaches the
+// decoder's clock, an N command's value of 0x7F (./lsdjMidiOut.ts runs the cases; the lsdj-midiout-48k and
+// lsdj-midiout-96k files run the first at their rates, each a host process of its own).
 //
-// This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT
-// is byte 9 (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing — which the raw 8-bit
-// capture had mis-read as garbage — decodes cleanly.
-import { test, expect, skip } from "@retroplug/testing";
-import { createRealBackend } from "../src/realBackend";
-import { createAudioDriver } from "../src/audioDriver";
-import { savFrom, type SavInput } from "@retroplug/lsdj";
-import { directGraph } from "../src/graph/directGraph";
-
-// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
-const graph = directGraph();
-
-declare const __RESOURCES_DIR__: string;
-
-const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
-const START = 7; // GameboyButton::Start
-const MIDIOUT = "midiOut"; // LsdjSyncMode / lsdj-sync role mode
-
-// A phrase of eight rows, each an audible note plus an `N` (NoteOn) command whose value is the MIDI note
-// to transmit — that command is what drives the MI.OUT NoteOn protocol. SYNC=MI.OUT so LSDj emits the
-// Arduinoboy stream (clock + START on play, NoteOns from the N commands).
-const MIDIOUT_SONG: SavInput = {
-  workingSong: {
-    formatVersion: 22,
-    settings: { syncMode: "MidiOut" },
-    rows: [{ chains: [0] }],
-    chains: [{ phrases: [0] }],
-    phrases: [
-      {
-        notes: [1, 13, 25, 1, 13, 25, 1, 13],
-        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
-        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
-        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
-      },
-    ],
-    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
-  },
-};
-
-const lsdjSync = (mode: string) => [{ kind: "lsdj-sync", config: { mode } }];
-
-// Flatten a drainMidiOut result into the raw status/data byte messages for assertions.
-const messages = (drained: { data: Uint8Array }[]): number[][] => drained.map((m) => Array.from(m.data));
-
-test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI its decoder turns into host MIDI", () => {
-  const be = createRealBackend();
-  if (!be.fileExists(ABOY)) skip(`lsdj-midiout: aboy LSDj ROM not found at ${ABOY}`);
-
-  const audio = createAudioDriver();
-
-  const id = 1;
-  expect(graph.add({
-    romPath: ABOY,
-    platform: "gb",
-    core: "sameboy",
-    embeddedRom: "",
-    savPath: null,
-    statePath: null,
-    sramBytes: savFrom(MIDIOUT_SONG),
-  }, id)).toBeTruthy();
-
-  expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
-  // The mode's capture, `drive`, rides the same document on the Game Boy's node, as a store places it.
-  expect(graph.setPipeline(id, lsdjSync(MIDIOUT))).toBeTruthy();
-
-  audio.renderAudio(6000); // reach the song screen
-  audio.drainMidiOut(); // discard any boot-time noise
-
-  // LSDj's START button toggles play/stop; each tap sends the transport bookend on its serial-out (the
-  // MI.OUT stream — LSDj is the serial master and self-clocks). We play, stop, then play again: the
-  // stop→restart lands a clean 0xFC/0xFA transport edge mid-stream, after the external-clock serial link
-  // has settled (the very first byte at play-start can be lost to the capture-arming handshake).
-  const tap = () => {
-    audio.pressButton(id, START, true);
-    audio.renderAudio(80);
-    audio.pressButton(id, START, false);
-  };
-  tap(); // play
-  audio.renderAudio(2000); // let the phrase run so N commands + clocks transmit
-  tap(); // stop → 0xFC
-  audio.renderAudio(400);
-  tap(); // play again → 0xFA, mid-stream where framing is aligned
-  audio.renderAudio(2000);
-
-  const out = audio.drainMidiOut();
-  const drained = messages(out);
-  const status = drained.map((m) => m[0]);
-  const starts = status.filter((s) => s === 0xfa).length;
-  const stops = status.filter((s) => s === 0xfc).length;
-  const clocks = status.filter((s) => s === 0xf8).length;
-  const noteOns = status.filter((s) => (s & 0xf0) === 0x90).length;
-  console.log(`[lsdj-midiout] ${drained.length} msgs; starts=${starts} stops=${stops} clocks=${clocks} noteOns=${noteOns}`);
+// This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT is byte 9
+// (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing, which the raw 8-bit capture had mis-read
+// as garbage, decodes cleanly once the capture feeds the Game Boy the 0s an Arduinoboy does.
+import { test } from "@retroplug/testing";
+import { midiOutCase, midiOutClockCase } from "./lsdjMidiOut";
+
+test("MIDIOUT (mode 7) at 44.1 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays", () => {
+  midiOutCase(44100);
+});
 
-  // A clock stream — LSDj clocks its MI.OUT output while playing (the transport heartbeat).
-  expect(clocks > 4).toBeTruthy();
-  // NoteOns from the N commands — the actual musical MIDI content.
-  expect(noteOns > 0).toBeTruthy();
-  // A transport bookend — the stop→restart toggle sends 0xFC then 0xFA on the (aligned) MI.OUT stream.
-  expect(starts + stops > 0).toBeTruthy();
-  // Each message at its byte's frame in the block it was captured in: inside renderAudio's 1024-frame blocks, and
-  // spread across them, never all at a block's frame 0 as a decoder timestamping by block would send them.
-  const frames = out.map((m) => m.frame);
-  const offZero = frames.filter((f) => f > 0).length;
-  console.log(`[lsdj-midiout] frames ${Math.min(...frames)}..${Math.max(...frames)}, ${offZero} of ${frames.length} past frame 0`);
-  expect(frames.every((f) => f >= 0 && f < 1024)).toBeTruthy();
-  expect(offZero > frames.length / 2).toBeTruthy();
+test("MIDIOUT (mode 7): an N command's value of 0x7F reaches the host as an Arduinoboy reads it, a clock", () => {
+  midiOutClockCase();
 });
diff --git a/packages/retroplug/test-native/lsdjMidiOut.ts b/packages/retroplug/test-native/lsdjMidiOut.ts
new file mode 100644
index 000000000..cad5c66f4
--- /dev/null
+++ b/packages/retroplug/test-native/lsdjMidiOut.ts
@@ -0,0 +1,148 @@
+// The MI.OUT cases the lsdj-midiout files run, one file a host rate (44.1, 48 and 96 kHz): each file is its own host
+// process, so its engine is empty when a case sets its rate. Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master",
+// mode 7) end to end on a real LSDj core: a song authored with SYNC=MI.OUT and N (NoteOn) commands, once playing,
+// sends the Arduinoboy protocol out of LSDj's serial port; native clocks the port as the Arduinoboy does (`drive`,
+// the capture the Game Boy node's setting places, feeding the Game Boy 0s on SIN) and captures the bits, which the
+// lsdj-sync role's decoder after the Game Boy (rp.lsdj.serial-out) reads in the block they were captured in: a start
+// bit, then 7 payload bits, decoded into host MIDI on its midiOut port, each message at the frame of the byte that
+// completes it.
+//
+// What LSDj writes to its serial port is the same at every rate (the 43 bytes this song plays: 2 starts, a stop and 20
+// NoteOns; docs/known-bugs-batch-2-design.md section 3), so the decoded MIDI is held to exactly that sequence: every
+// NoteOn the song plays, with the values it authors, and nothing it does not send (no clock, no Program Change).
+// (Not a *.test.ts, so the runner does not run it on its own; the three files import it.)
+import { expect, skip } from "@retroplug/testing";
+import { createRealBackend } from "../src/realBackend";
+import { createAudioDriver } from "../src/audioDriver";
+import { savFrom, type SavInput } from "@retroplug/lsdj";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for these files' directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
+
+declare const __RESOURCES_DIR__: string;
+
+const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
+const START = 7; // GameboyButton::Start
+const MIDIOUT = "midiOut"; // LsdjSyncMode / lsdj-sync role mode
+
+/** The N commands' values: the MIDI notes the phrase sends, one a row. */
+const SONG_NOTES = [48, 52, 55, 60, 64, 67, 72, 36];
+
+/** A phrase of eight rows, each an audible note plus an `N` (NoteOn) command whose value is what LSDj transmits: that
+ *  command is what drives the MI.OUT NoteOn protocol. SYNC=MI.OUT, so LSDj sends the Arduinoboy stream (START and STOP
+ *  on its transport, a command and its value for each N command). */
+const midiOutSong = (values: readonly number[]): SavInput => ({
+  workingSong: {
+    formatVersion: 22,
+    settings: { syncMode: "MidiOut" },
+    rows: [{ chains: [0] }],
+    chains: [{ phrases: [0] }],
+    phrases: [
+      {
+        notes: [1, 13, 25, 1, 13, 25, 1, 13],
+        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
+        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
+        commandValues: [...values],
+      },
+    ],
+    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
+  },
+});
+
+const noteOn = (note: number): number[] => [0x90, note, 127];
+
+/** What 2 s of play sends: the phrase's eight NoteOns, then the first two of its next pass. */
+const PLAY = [...SONG_NOTES, SONG_NOTES[0], SONG_NOTES[1]].map(noteOn);
+
+/** The whole run's MIDI: START and the first play, STOP, START and the second play. */
+export const EXPECTED_MIDI_OUT: readonly (readonly number[])[] = [[0xfa], ...PLAY, [0xfc], [0xfa], ...PLAY];
+
+/** Boot `song` at `rate`, play it for 2 s with a START tap and, with `replay`, stop it and play it again, and return
+ *  the host MIDI its decoder sent from the first tap on. */
+function played(song: SavInput, rate: number, replay: boolean): { frame: number; data: number[] }[] {
+  const be = createRealBackend();
+  if (!be.fileExists(ABOY)) skip(`lsdj-midiout: aboy LSDj ROM not found at ${ABOY}`);
+
+  const audio = createAudioDriver();
+  expect(audio.setSampleRate(rate)).toBeTruthy();
+
+  const id = 1;
+  expect(graph.add({
+    romPath: ABOY,
+    platform: "gb",
+    core: "sameboy",
+    embeddedRom: "",
+    savPath: null,
+    statePath: null,
+    sramBytes: savFrom(song),
+  }, id)).toBeTruthy();
+
+  expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
+  // The mode's capture, `drive`, rides the same document on the Game Boy's node, as a store places it.
+  expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: MIDIOUT } }])).toBeTruthy();
+
+  audio.renderAudio(6000); // reach the song screen
+  audio.drainMidiOut(); // discard any boot-time noise
+
+  // LSDj's START button toggles play and stop, and each tap sends the transport's bookend on its serial out. Play,
+  // stop, then play again, so the run holds both edges and a second pass of the same notes.
+  const tap = () => {
+    audio.pressButton(id, START, true);
+    audio.renderAudio(80);
+    audio.pressButton(id, START, false);
+  };
+  tap(); // play: 0xFA, then the N commands' NoteOns
+  audio.renderAudio(2000);
+  if (replay) {
+    tap(); // stop: 0xFC
+    audio.renderAudio(400);
+    tap(); // play again: 0xFA
+    audio.renderAudio(2000);
+  }
+  const out = audio.drainMidiOut().map((m) => ({ frame: m.frame, data: Array.from(m.data) }));
+  graph.remove(id); // a live system is emulated by every later case in the file
+  return out;
+}
+
+/** Play, stop and play the song at `rate` and hold the decoded MIDI to what LSDj sent: the exact sequence, and each
+ *  message at its byte's frame inside the block it was captured in. */
+export function midiOutCase(rate: number): void {
+  const out = played(midiOutSong(SONG_NOTES), rate, true);
+  const drained = out.map((m) => m.data);
+  const status = drained.map((m) => m[0]);
+  const count = (pick: (s: number) => boolean) => status.filter(pick).length;
+  const noteOns = count((s) => (s & 0xf0) === 0x90);
+  const starts = count((s) => s === 0xfa);
+  const stops = count((s) => s === 0xfc);
+  const clocks = count((s) => s === 0xf8);
+  const counts = `starts=${starts} stops=${stops} clocks=${clocks} noteOns=${noteOns}`;
+  console.log(`[lsdj-midiout] ${rate}: ${drained.length} msgs; ${counts}`);
+  // Every NoteOn the song plays, with its own value, both transport edges, and nothing else: a clock or a Program
+  // Change here is a bit the capture made up, which LSDj never sent.
+  expect(drained).toEqual(EXPECTED_MIDI_OUT);
+  expect(noteOns).toBe(20);
+
+  // Each message at its byte's frame in the block it was captured in: inside renderAudio's 1024-frame blocks, and
+  // spread across them, never all at a block's frame 0 as a decoder timestamping by block would send them.
+  const frames = out.map((m) => m.frame);
+  const offZero = frames.filter((f) => f > 0).length;
+  const span = `${Math.min(...frames)}..${Math.max(...frames)}`;
+  console.log(`[lsdj-midiout] ${rate}: frames ${span}, ${offZero} of ${frames.length} past frame 0`);
+  expect(frames.every((f) => f >= 0 && f < 1024)).toBeTruthy();
+  expect(offZero > frames.length / 2).toBeTruthy();
+}
+
+/** LSDj's MI.OUT sender has no clock of its own: it sends a command (0x70 to 0x7B) and its value for each N, X, Y and
+ *  Q command, and 0x7D and 0x7E at its transport's edges, and no setting adds a 0x7F (ROM bank 3, $7ADE to $7BBC, in
+ *  9.3.3). But an N command's value goes on the wire as it is, so N7F sends the byte an Arduinoboy reads as a clock,
+ *  its NoteOn left waiting for a value: the one way a real LSDj reaches the decoder's 0x7F path. Played at 44.1 kHz,
+ *  every other row's N7F is a 0xF8 where its NoteOn would be. */
+export function midiOutClockCase(): void {
+  const values = [48, 0x7f, 52, 0x7f, 55, 0x7f, 60, 0x7f];
+  const drained = played(midiOutSong(values), 44100, false).map((m) => m.data);
+  console.log(`[lsdj-midiout] N7F: ${JSON.stringify(drained)}`);
+  const clock = [0xf8];
+  expect(drained).toEqual([[0xfa], noteOn(48), clock, noteOn(52), clock, noteOn(55), clock, noteOn(60), clock,
+    noteOn(48), clock]);
+}
diff --git a/packages/retroplug/test-native/parity/render-blocks.test.ts b/packages/retroplug/test-native/parity/render-blocks.test.ts
index 81b084fd6..4245f78aa 100644
--- a/packages/retroplug/test-native/parity/render-blocks.test.ts
+++ b/packages/retroplug/test-native/parity/render-blocks.test.ts
@@ -225,7 +225,9 @@ test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, t
   });
 
   expect(viaAudio.length).toBeGreaterThan(4); // a real stream, not two empties agreeing
-  expect(viaAudio.some((m) => m.data[0] === 0xf8)).toBeTruthy();
+  // The song's N commands, as NoteOns; it sends no clock, so a 0xF8 would be a bit the capture made up.
+  expect(viaAudio.some((m) => (m.data[0] & 0xf0) === 0x90)).toBeTruthy();
+  expect(viaAudio.some((m) => m.data[0] === 0xf8)).toBe(false);
   expect(JSON.stringify(viaBlocks)).toBe(JSON.stringify(viaAudio));
   expect(Math.max(...blockOf)).toBeGreaterThan(0); // entries carry the block they came from, not always 0
 });
````

- [ ] **Step 2: run them on the unchanged core and see them fail**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native lsdj-midiout
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/render-blocks
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[host-io]"
RETROPLUG_RESOURCES_DIR=/workspaces/resources bash /workspaces/RetroPlug-main/cuttlefish/tools/run-profile.sh stats dsp-bench
```

Expected: every one fails. `lsdj-midiout` (its three files; the bytes are the capture's re-framing):

```
# lsdj-midiout-48k
[lsdj-midiout] 48000: 42 msgs; starts=1 stops=1 clocks=23 noteOns=14
not ok 1 - MIDIOUT (mode 7) at 48 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
  expected [[250],[144,48,127],[144,52,127],...], got [[248],[248],[195,97],[248],[144,90,127],...]
# lsdj-midiout-96k
[lsdj-midiout] 96000: 32 msgs; starts=0 stops=2 clocks=21 noteOns=5
not ok 1 - MIDIOUT (mode 7) at 96 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
  expected [[250],[144,48,127],[144,52,127],...], got [[248],[248],[195,97],[248],[144,109,127],...]
# lsdj-midiout
[lsdj-midiout] 44100: 38 msgs; starts=1 stops=1 clocks=23 noteOns=12
[lsdj-midiout] N7F: [[248],[248],[195,97],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248]]
not ok 1 - MIDIOUT (mode 7) at 44.1 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
  expected [[250],[144,48,127],[144,52,127],...], got [[248],[248],[195,97],[248],[144,109,127],...]
not ok 2 - MIDIOUT (mode 7): an N command's value of 0x7F reaches the host as an Arduinoboy reads it, a clock
  expected [[250],[144,48,127],[248],[144,52,127],[248],[144,55,127],[248],[144,60,127],[248],[144,48,127],[248]], got [[248],[248],[195,97],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248],[248]]
```

`parity/render-blocks`:

```
not ok 6 - MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, tagged with its block
  expected false, got true
```

`[host-io]` (exit 42; the idle line decodes to a clock a byte, block after block):

```
HostIo.test.cpp:913: FAILED:
  CHECK( e.midiOut().empty() )
with expansion:
  false
...
HostIo.test.cpp:916: FAILED:
  CHECK( idle + 1 >= bytes )
with expansion:
  1 >= 35573 (0x8af5)
...
HostIo.test.cpp:953: FAILED:
  CHECK( e.midiOut().empty() )
...
HostIo.test.cpp:970: FAILED:
  CHECK( e.midiOut().empty() )
...
test cases:   25 |   23 passed |   2 failed
assertions: 6339 | 5861 passed | 478 failed
```

(The check at line 913 fails in 475 of the first case's 600 blocks.) dsp-bench, in the profiling host (the first run
of `tools/run-profile.sh` configures and builds `build-prof/`, a few minutes):

```
DSP-BENCH {"workload":"lsdj-midiOut",...,"midiOut":60,"clocks":40,"noteOns":20}
not ok 1 - dsp-bench: the script nodes' per-block allocation under an mGB + MIDI workload
  lsdj-midiOut: noteOns decoded in the window: expected > 27.863945578231295, got 20
```

- [ ] **Step 3: feed the Game Boy 0**

The fix is the one argument; the comment block above it said the capture fed "idle-high", and the decoder's module
comment said a captured byte reads as `0x80|payload`, which only held when the stray clocks came in eights.

````patch
diff --git a/packages/lsdj/ts/lsdjArduinoboy.ts b/packages/lsdj/ts/lsdjArduinoboy.ts
index 0b8b995c8..18f9f2b26 100644
--- a/packages/lsdj/ts/lsdjArduinoboy.ts
+++ b/packages/lsdj/ts/lsdjArduinoboy.ts
@@ -6,12 +6,14 @@
 // Two layers, both pure + testable in isolation:
 //
 //   1. FLAG-GATED FRAMING (arduinoboyDecodeSerialOut). LSDj's MI.OUT wire sends each command as a wire
-//      frame, `1 data-present flag bit + 7 payload bits`, MSB-first, so a captured 8-bit byte reads as
-//      `0x80|payload` and the native 8-bit `captureSerialOutBit` mis-frames it as high-bit "garbage".
-//      We reconstruct the raw bit stream (MSB-first per captured byte) and apply: read 1 flag; if it
-//      is 1, read 7 payload bits → a 7-bit command byte; else it's an idle bit. A wire frame that
-//      straddles a block boundary is buffered in `state` (bitAcc/bitCount) across calls. A "frame"
-//      alone is an audio frame, as everywhere in the graph: where a message lies in its block.
+//      frame, `1 data-present flag bit + 7 payload bits`, MSB-first (LSDj writes SB = 0x80|payload). The
+//      native capture's 8-bit groups are not wire bytes: it also clocks the bits LSDj's serial interrupt
+//      leaves armed between its writes (the 0s an Arduinoboy holds the line at), so a group can straddle
+//      two writes. The groups carry the bit stream in order, though, so we reconstruct it (MSB-first per
+//      captured byte) and apply: read 1 flag; if it is 1, read 7 payload bits → a 7-bit command byte;
+//      else it's an idle bit. A wire frame that straddles a block boundary is buffered in `state`
+//      (bitAcc/bitCount) across calls. A "frame" alone is an audio frame, as everywhere in the graph:
+//      where a message lies in its block.
 //
 //   2. BYTE PROTOCOL (arduinoboyDecodeByte). The 7-bit command byte drives the Arduinoboy protocol
 //      (verbatim from the trash80/Arduinoboy firmware, Mode_LSDJ_Midiout.ino):
diff --git a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
index c92a58b27..8c7fb332b 100644
--- a/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp
@@ -678,7 +678,7 @@ void SameBoySystem::writeAudioSample(int16_t left, int16_t right) {
     // MI.OUT (Arduinoboy master-out) mode sets SC=0x80 (transfer enable + EXTERNAL clock) and shifts a byte
     // OUT one bit at a time, waiting for a master to clock it. We play that master here: read the
     // outgoing bit from SB.bit7 BEFORE the shift, then clock it via GB_serial_set_data_bit (feeding
-    // idle-high back on SIN). It can't move to SameBoy's serial bit callbacks — those fire only as
+    // back the 0 the master drives on SIN). It can't move to SameBoy's serial bit callbacks - those fire only as
     // internal-clock master (SC=0x81) and are dormant in external-clock mode — and it can't be batched:
     // the OUT bit must be sampled full-duplex as the GB shifts it during GB_run, which only the
     // per-sample hook sees. Serial IN (mGB / LSDJ KEYBD scancodes) is delivered separately, per byte,
@@ -690,7 +690,14 @@ void SameBoySystem::writeAudioSample(int16_t left, int16_t right) {
         if ((sc & 0x81) == 0x80) {
             const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
             captureSerialOutBit(outBit);
-            GB_serial_set_data_bit(gb_, true);
+            // The Arduinoboy drives SIN low throughout (Mode_LSDJ_Midiout.ino's getIncommingSlaveByte clocks with
+            // GB_SET(clock, 0, 0)), so every bit the Game Boy shifts in is 0 and SB reads $00 once a byte is out.
+            // LSDj's serial interrupt re-arms the port (SC = $80, $363C) after every transfer, whether or not it has
+            // a byte to send, so this clocks bits the CPU never wrote: they are what it shifted in, 0s, which the
+            // decoder's start-bit framing reads as idle. Fed 1s, each of them would read as a start bit and take the
+            // next 7 real bits as its payload, re-framing the stream across wire bytes (by an amount that depends on
+            // the host rate): a NoteOn's value lost or turned into another note.
+            GB_serial_set_data_bit(gb_, false);
         }
     }
 }
````

- [ ] **Step 4: rebuild and run the four again**

```bash
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-engine-graph-test -j$(nproc)
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native lsdj-midiout
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/render-blocks
/workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-engine-graph-test "[host-io]"
RETROPLUG_RESOURCES_DIR=/workspaces/resources bash /workspaces/RetroPlug-main/cuttlefish/tools/run-profile.sh stats dsp-bench
```

(`test:native` rebuilds `retroplug-host` itself.) Expected, each exit 0. `lsdj-midiout`, the same 23 messages at every
rate:

```
# lsdj-midiout-48k
[lsdj-midiout] 48000: 23 msgs; starts=2 stops=1 clocks=0 noteOns=20
[lsdj-midiout] 48000: frames 55..1020, 23 of 23 past frame 0
ok 1 - MIDIOUT (mode 7) at 48 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
# lsdj-midiout-96k
[lsdj-midiout] 96000: 23 msgs; starts=2 stops=1 clocks=0 noteOns=20
[lsdj-midiout] 96000: frames 93..999, 23 of 23 past frame 0
ok 1 - MIDIOUT (mode 7) at 96 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
# lsdj-midiout
[lsdj-midiout] 44100: 23 msgs; starts=2 stops=1 clocks=0 noteOns=20
[lsdj-midiout] 44100: frames 82..993, 23 of 23 past frame 0
[lsdj-midiout] N7F: [[250],[144,48,127],[248],[144,52,127],[248],[144,55,127],[248],[144,60,127],[248],[144,48,127],[248]]
ok 1 - MIDIOUT (mode 7) at 44.1 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays
ok 2 - MIDIOUT (mode 7): an N command's value of 0x7F reaches the host as an Arduinoboy reads it, a clock
# 2 passed, 0 skipped, 0 failed
# 3 native test file(s) passed (jobs=16)
```

`parity/render-blocks`: `# 8 passed, 0 skipped, 0 failed`. `[host-io]`: `All tests passed (6339 assertions in 25 test
cases)`. dsp-bench:

```
DSP-BENCH {"workload":"lsdj-masterSync",...,"midiOut":476,"clocks":476,"noteOns":0}
DSP-BENCH {"workload":"lsdj-midiOut",...,"midiOut":40,"clocks":0,"noteOns":40}
ok 1 - dsp-bench: the script nodes' per-block allocation under an mGB + MIDI workload
```

- [ ] **Step 5: the four parity cases, failing as approved**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/lsdj-modes
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/blocks-tiny-lsdj
```

Expected: each exits 1, on exactly the four MI.OUT cases, in their MIDI out and work RAM (each failure also prints its
actual digest, whose segment lanes match the baseline's):

```
not ok 6 - midiOut
  MIDI out: expected 42 message(s) hashing c8f61b17, got 23 hashing 8642472a
  work RAM slot 0: expected 32768 bytes hashing 2d2fb067, got 32768 hashing 1caeaea0
# 8 passed, 0 skipped, 1 failed
```

```
not ok 7 - midiOut-1
  MIDI out: expected 38 message(s) hashing 15804b2c, got 21 hashing 2444c942
  work RAM slot 0: expected 32768 bytes hashing e5b0806a, got 32768 hashing f3ba11b1
not ok 8 - midiOut-fl
  MIDI out: expected 38 message(s) hashing 6108dcb3, got 21 hashing 3d00ffd2
  work RAM slot 0: expected 32768 bytes hashing 49240d8a, got 32768 hashing 8bf2bc35
not ok 9 - midiOut-fl256
  MIDI out: expected 38 message(s) hashing 3f054ada, got 21 hashing e2d50f05
  work RAM slot 0: expected 32768 bytes hashing f0613a11, got 32768 hashing f573627e
ok 10 - blocks-tiny-lsdj: the baseline holds only these cases
# 7 passed, 0 skipped, 3 failed
```

- [ ] **Step 6: re-record the two groups, and prove what moved**

One group by its full name at a time (spec/06's rule), on `linux-x64-gnu13`, then the proof that each baseline's diff
touches only the four cases' `midiOut` and `ram`, no segment (audio) hash and no other case or key:

```bash
RP_PARITY_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/lsdj-modes
RP_PARITY_UPDATE=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity/blocks-tiny-lsdj
node -e '
const { execFileSync } = require("child_process");
const R = "/workspaces/RetroPlug-main/cuttlefish", B = "packages/retroplug/test-native/parity/baseline/";
const moved = { "lsdj-modes": ["midiOut"], "blocks-tiny-lsdj": ["midiOut-1", "midiOut-fl", "midiOut-fl256"] };
let bad = 0;
for (const [group, cases] of Object.entries(moved)) {
  const was = JSON.parse(execFileSync("git", ["-C", R, "show", `HEAD:${B}${group}.json`], { encoding: "utf8" }));
  const now = JSON.parse(require("fs").readFileSync(`${R}/${B}${group}.json`, "utf8"));
  const names = [...new Set([...Object.keys(was.cases), ...Object.keys(now.cases)])];
  for (const name of names) for (const key of new Set([...Object.keys(was.cases[name] ?? {}), ...Object.keys(now.cases[name] ?? {})])) {
    const same = JSON.stringify(was.cases[name]?.[key]) === JSON.stringify(now.cases[name]?.[key]);
    const allowed = cases.includes(name) && (key === "midiOut" || key === "ram");
    if (!same) console.log(`${group}/${name}.${key}: moved${allowed ? "" : " (NOT ALLOWED)"}`);
    if (allowed && same) console.log(`${group}/${name}.${key}: expected to move and did not`);
    if (same === allowed) bad++;
  }
  const rest = (o) => JSON.stringify({ ...o, cases: undefined });
  if (rest(was) !== rest(now)) { console.log(`${group}: a key outside cases moved`); bad++; }
}
console.log(bad ? `FAIL: ${bad} unexpected` : "only the four cases moved, and only their midiOut and ram");
process.exit(bad ? 1 : 0);'
git -C /workspaces/RetroPlug-main/cuttlefish hash-object \
  packages/retroplug/test-native/parity/baseline/lsdj-modes.json \
  packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj.json
```

Expected: both re-records exit 0 with every case `(recording)` and `ok 9 - lsdj-modes: write the baseline` /
`ok 10 - blocks-tiny-lsdj: write the baseline`; the proof prints

```
lsdj-modes/midiOut.midiOut: moved
lsdj-modes/midiOut.ram: moved
blocks-tiny-lsdj/midiOut-1.midiOut: moved
blocks-tiny-lsdj/midiOut-1.ram: moved
blocks-tiny-lsdj/midiOut-fl.midiOut: moved
blocks-tiny-lsdj/midiOut-fl.ram: moved
blocks-tiny-lsdj/midiOut-fl256.midiOut: moved
blocks-tiny-lsdj/midiOut-fl256.ram: moved
only the four cases moved, and only their midiOut and ram
```

and exits 0; the hashes are

```
d3e562d5316c4de4ca447ccee5ec1d33f36f8b3b
c82114d9a88f302cc6e9c6595a957af9d261e5b1
```

- [ ] **Step 7: the wider suites**

```bash
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:plugin
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test
pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:ui
RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native parity
RETROPLUG_RESOURCES_DIR=/nonexistent pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native
```

Expected, each exit 0:

- `test:plugin`: the fourteen binaries pass, among them `retroplug-sameboy-test` `All tests passed (1152140 assertions
  in 7 test cases)` and `retroplug-audio-test` `All tests passed (385 assertions in 17 test cases)`, both as before, and
  `retroplug-engine-graph-test` `All tests passed (1712547 assertions in 341 test cases)` (the two reworked cases run
  fewer per-message checks than the clocks once gave them);
- `pnpm test`: 2511 passed in 239 files; the typecheck exits 0; `test:ui`: 54 passed in 48 files;
- `test:native`: `# 175 native test file(s) passed (jobs=16)`, 637 passed, the same 2 skipped; `lsdj-mastersync`,
  `lsdj-arduinoboy-slave` and `lsdj-capture-settings` pass unchanged;
- strict parity: `# 30 native test file(s) passed (jobs=16)`, 250 passed, 0 skipped: every other case, and every audio
  lane of the four, holds the old baseline;
- CI's picture: 510 passed and 129 skipped, `lsdj-midiout(2)`, `lsdj-midiout-48k(1)` and `lsdj-midiout-96k(1)` among
  them, as the baseline now allows.

- [ ] **Step 8: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  packages/sameboy/native/src/system/sameboy/SameBoySystem.cpp \
  packages/lsdj/ts/lsdjArduinoboy.ts \
  packages/retroplug/test-native/lsdjMidiOut.ts \
  packages/retroplug/test-native/lsdj-midiout.test.ts \
  packages/retroplug/test-native/lsdj-midiout-48k.test.ts \
  packages/retroplug/test-native/lsdj-midiout-96k.test.ts \
  packages/retroplug/test-native/parity/render-blocks.test.ts \
  packages/retroplug/test-native/dsp-bench.test.ts \
  packages/native/test/engine-graph/HostIo.test.cpp \
  packages/retroplug/scripts/skip-baseline.json \
  packages/retroplug/test-native/parity/baseline/lsdj-modes.json \
  packages/retroplug/test-native/parity/baseline/blocks-tiny-lsdj.json
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb2): feed the Game Boy 0 on MI.OUT's clocked serial input, as an Arduinoboy does

MI.OUT decoded about half the NoteOns a song plays, and which ones depended on the host rate (12, 14 and 5 of 20 at
44.1, 48 and 96 kHz, most of them other notes). The capture clocks LSDj's port as an Arduinoboy does, but fed the Game
Boy 1s on its serial input where the Arduinoboy holds it at 0 (Mode_LSDJ_Midiout.ino's GB_SET(clock, 0, 0)), and
LSDj's serial interrupt re-arms the port after every transfer, so the bits clocked between its writes were 1s, start
bits to the decoder, which re-framed the stream. Fed 0, the decode is exactly what LSDj writes, at every rate, and no
clock, frame, CPU write or audio sample changes.

lsdj-midiout becomes a file a rate holding the song's exact sequence, with a case for the one way a real LSDj sends the
decoder a clock (an N command whose value is 0x7F; its MI.OUT sender has no clock of its own). render-blocks checks
NoteOns, dsp-bench's MI.OUT window counts them, and HostIo's two mGB cases hold that the idle line decodes to nothing.
lsdj-modes' midiOut and blocks-tiny-lsdj's three midiOut cases are re-recorded, in their MIDI out and work RAM (LSDj's
received-byte shadow, now $00) alone.
MSG
```

---

## Task 4: the docs

The batch's record in [spec/07](../spec/07-remaining-work.md), and every document that says otherwise now: spec/04 (the
readers and the session's divisor), spec/06 (the sync rule's keys, and the recorded defects: one is pinned now),
AGENTS.md (the same count), `docs/lsdj.md` (the snippet, the fed bit, the `0x7F` and `0x80+` rows and the corrected
record), the B0 design's section 2 (the seventh defect, fixed) and section 9, `docs/lsdj-serial-out-design.md`
(sections 4 and 11 and its last bullets), and the design's own prose lines past 120 columns, rewrapped (its table rows
and its section 4 stay; its as-built section is the controller's). The design asked the serial-out design's "about 40
NoteOns" to become "about 65 to 70": measured with the fix, dsp-bench's window decodes 40, so the figure stays and the
bullet records it.

**Files:**

- Modify: `spec/07-remaining-work.md:41-51` (the known-bugs list), `:758-766` (the MI.OUT entry), `:777-795` (the
  readers' entry), `:912-922` (the controller's tick count).
- Modify: `spec/04-roles-protocol-nodes.md:438-445, 469-479`, `spec/06-build-test.md:434-444, 672-695`,
  `AGENTS.md:756-766`.
- Modify: `docs/lsdj.md:620-628, 648-658, 694-701, 720-729`, `docs/modular-graph-phase-b0-design.md:210-221, 247-253,
  426-433`, `docs/lsdj-serial-out-design.md:97-103, 207-213, 322-339`, `docs/known-bugs-batch-2-design.md:11-136` (seven
  prose paragraphs rewrapped, no word changed).

**Interfaces:**

- Consumes: the three commits Tasks 1 to 3 made.
- Produces: nothing code uses.

### Steps

- [ ] **Step 1: the doc changes**

````patch
diff --git a/AGENTS.md b/AGENTS.md
index 2a3a22fa5..5402f980e 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -756,11 +756,12 @@ order-dependent cases and an ignored stimulus. Three traps, all met building it:
 fixed state (`RETROPLUG_DETERMINISTIC=1`, which the runner sets for `parity/` only; the SMS VDP's random start
 scanline was the last source); mGB ignores MIDI for its first ~1.5 s, so a case sends nothing before
 `BOOT_MS`; and a second instance of one ROM path gets a fresh `-2.sav` battery slot, so two systems that each
-need a battery each need their own copy (`rig.rom(src, prefix)`). Two engine defects are pinned on purpose (design
-section 2.5 of [docs/modular-graph-phase-b0-design.md](docs/modular-graph-phase-b0-design.md)); fixing one is its
-own change with a re-record, as the LSDj master-sync fix and the narrow-host NES fix were
+need a battery each need their own copy (`rig.rom(src, prefix)`). One engine defect is pinned on purpose (design
+section 2.5 of [docs/modular-graph-phase-b0-design.md](docs/modular-graph-phase-b0-design.md)); fixing it is its own
+change with a re-record, as the LSDj master-sync fix, the narrow-host NES fix and the MI.OUT fix were
 ([docs/lsdj-master-sync-fix-design.md](docs/lsdj-master-sync-fix-design.md),
-[docs/modular-graph-phase-c4-design.md](docs/modular-graph-phase-c4-design.md) section 10). The fixture groups need
+[docs/modular-graph-phase-c4-design.md](docs/modular-graph-phase-c4-design.md) section 10,
+[docs/known-bugs-batch-2-design.md](docs/known-bugs-batch-2-design.md) section 3). The fixture groups need
 `RETROPLUG_RESOURCES_DIR` pointed at the sibling resources tree, and CI runs only the 152 fixture-free cases
 (every LSDj case, its MIDI-out and link-cable ones among them, and every controller, risa and GBA case is
 fixture-only), so a Phase B step is accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`.
diff --git a/docs/known-bugs-batch-2-design.md b/docs/known-bugs-batch-2-design.md
index 6a2930a4e..7cdf96ddc 100644
--- a/docs/known-bugs-batch-2-design.md
+++ b/docs/known-bugs-batch-2-design.md
@@ -9,21 +9,21 @@ Three entries from [spec/07](../spec/07-remaining-work.md)'s known bugs, all abo
 | 3 | MI.OUT decodes about half the NoteOns a song plays | `sameboy` (the synthetic Arduinoboy clock) | LSDj's serial-out node |
 
 User decisions (2026-11-12): readers ask placement and stored roles are kept (section 1); the predictor follows the
-divisor the cart is set to (section 2); the MI.OUT fix re-records the four parity cases it moves (section 3). The process
-is batch 1's: this design, a plan whose code its author validates on the tree, one task per bug plus docs, each
+divisor the cart is set to (section 2); the MI.OUT fix re-records the four parity cases it moves (section 3). The
+process is batch 1's: this design, a plan whose code its author validates on the tree, one task per bug plus docs, each
 implemented and given a spec review and an Opus code review, the kit as acceptance and an Opus final review.
 
 ## 1. Readers ask what is placed
 
 A system's stored role list can keep a role the graph leaves out: after a ROM swap an mGB cart can still carry
-`lsdj-sync`, an LSDj cart `mgb`. `protocolPipeline(roles)` (`packages/retroplug/src/graph/protocolNodes.ts`) decides what
-is placed (the stored order, which a load sorts so the ROM's suggested role comes first; one writer per system input; a
-kind once), and `syncLatency.ts` and `serialCapture.ts` already read it. Other readers decide by presence:
-
-- `resolveTracker(roles)` (`src/trackerRegistry.ts`, by `markerRole` presence; spec/07 calls it `trackerIntegrationFor`),
-  behind the tracker menus, Songs and Assets, song import, the render's tracker label, unsaved changes, the live SAV and
-  `projectStore`'s tracker decisions (spec/07's `roles.some((r) => r.kind === "lsdj-sync")` there is gone; its tracker
-  questions now go through `resolveTracker`);
+`lsdj-sync`, an LSDj cart `mgb`. `protocolPipeline(roles)` (`packages/retroplug/src/graph/protocolNodes.ts`) decides
+what is placed (the stored order, which a load sorts so the ROM's suggested role comes first; one writer per system
+input; a kind once), and `syncLatency.ts` and `serialCapture.ts` already read it. Other readers decide by presence:
+
+- `resolveTracker(roles)` (`src/trackerRegistry.ts`, by `markerRole` presence; spec/07 calls it
+  `trackerIntegrationFor`), behind the tracker menus, Songs and Assets, song import, the render's tracker label, unsaved
+  changes, the live SAV and `projectStore`'s tracker decisions (spec/07's `roles.some((r) => r.kind === "lsdj-sync")`
+  there is gone; its tracker questions now go through `resolveTracker`);
 - the config rows (`ui/screens/menu/configRows.ts`, `hasRole(sys.roles, row.role)`), which show LSDj's sync cyclers and
   divisor row on an mGB cart that stores a stale `lsdj-sync`;
 - `projectParameterMap` (`src/parameterMap.ts`), which takes the first role with a parameter table, so an LSDj cart with
@@ -51,8 +51,8 @@ kind once), and `syncLatency.ts` and `serialCapture.ts` already read it. Other r
 overlay; an LSDj cart storing a stale `mgb` exposes LSDj's parameters, not mGB's CC lanes; a no-node role keeps its
 parameter table; unit cases for `placedRole` and `isPlaced`, and for the system view's `placed`.
 
-**Docs.** spec/04 (which names `placedRole` as if it existed) matches the helper; spec/07's entry is closed and its stale
-names corrected.
+**Docs.** spec/04 (which names `placedRole` as if it existed) matches the helper; spec/07's entry is closed and its
+stale names corrected.
 
 ## 2. The MI.MAP predictor follows the cart's divisor
 
@@ -77,8 +77,8 @@ and the placer names no tracker field.
   setting's reason.
 
 **Tests.** `kind.test.ts` (the key accepted, a bad type refused); the placer (the divisor read from the pipeline's
-config, 1 when the key or the role is absent); `session.test.ts` (half the ticks a block at `tickDivisor` 2, a live change
-without a rebuild); `lsdjMidiMap.test.ts` (a launch released at its row at divisor 2); the real-core
+config, 1 when the key or the role is absent); `session.test.ts` (half the ticks a block at `tickDivisor` 2, a live
+change without a rebuild); `lsdjMidiMap.test.ts` (a launch released at its row at divisor 2); the real-core
 `test-native/lsdj-launchpad` case at divisor 2 (the predicted row holds the cart's polled row over several bars). At
 divisor 1 nothing changes.
 
@@ -119,8 +119,9 @@ audio hash.
 **Tests built on the defect, reworked:** `render-blocks`' `0xF8` check becomes a NoteOn check; dsp-bench's MI.OUT window
 counts NoteOns, not clocks, and its "uneven clocks" comment goes; the two `HostIo.test.cpp` cases keep their structural
 checks (the capture switched in the block its decoder's mode is, bytes reaching `rp.lsdj.serial-out`) and assert that
-mGB's idle line, `$00` now, decodes to nothing. The fixture-free suites then prove the wiring and the idle line; the real
-decode is proved by the fixture-gated cases and parity, and spec/07 records that CI sees no real-core MI.OUT decode.
+mGB's idle line, `$00` now, decodes to nothing. The fixture-free suites then prove the wiring and the idle line; the
+real decode is proved by the fixture-gated cases and parity, and spec/07 records that CI sees no real-core MI.OUT
+decode.
 
 **The `0x7F` clock path.** No real-core case sends one. The plan's author checks LSDj's MI.OUT sender for a `$7F` send:
 if a setting makes LSDj send clocks, that setting gets a case; if not, the decoder's unit test
@@ -131,9 +132,9 @@ if a setting makes LSDj send clocks, that setting gets a case; if not, the decod
 four cases.
 
 **Docs.** `docs/lsdj.md` (the snippet, the `0x80+` row and the "7D 7F 70 69" record), `lsdjArduinoboy.ts`'s module
-comment (the capture's 8-bit groups are not wire bytes; the decoder reads the bit stream), the B0 design's section 2 (the
-seventh defect, fixed), `docs/lsdj-serial-out-design.md`'s last bullet (and its "about 40 NoteOns" figure: LSDj sends
-about 65 to 70 in that window), spec/07's entry closed.
+comment (the capture's 8-bit groups are not wire bytes; the decoder reads the bit stream), the B0 design's section 2
+(the seventh defect, fixed), `docs/lsdj-serial-out-design.md`'s last bullet (and its "about 40 NoteOns" figure: LSDj
+sends about 65 to 70 in that window), spec/07's entry closed.
 
 ## 4. Plan, execution and acceptance
 
diff --git a/docs/lsdj-serial-out-design.md b/docs/lsdj-serial-out-design.md
index f65e8150c..e71bac476 100644
--- a/docs/lsdj-serial-out-design.md
+++ b/docs/lsdj-serial-out-design.md
@@ -97,7 +97,8 @@ frames. The one addition is a case's last block, now decoded.
 the frame of the captured byte whose bits complete it. A command whose bits straddle two blocks completes in the
 later one, at its completing byte's frame there. Its known defect (what it decodes depends on the host's sample
 rate, pinned by `lsdj-modes`' `midiOut` case, B0's seventh defect) stays as it is: this change moves MI.OUT's
-frames, not its bytes.
+frames, not its bytes. (Fixed since, with kb2: the capture fed the Game Boy 1s where an Arduinoboy holds its serial
+input at 0; [known-bugs-batch-2-design.md](known-bugs-batch-2-design.md) section 3.)
 
 **The emit.** Both decoders call `emit(frame, len, b0, b1, b2)` (a message is one to three bytes), and the node
 fills one of three reused arrays, one per length, and sends it: `send` takes an array-like and sends all of it, so a
@@ -207,7 +208,7 @@ Accepted on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`, plus the su
 ## 11. Left as it is
 
 - **The core's byte timing moves with the split** (section 8): a SameBoy matter, its own change if wanted.
-- **MI.OUT's rate-dependent decode** (section 4), still pinned.
+- **MI.OUT's rate-dependent decode** (section 4), left pinned here and fixed since, with kb2.
 - **The SDL standalone sends host MIDI at block time** (section 5).
 - **No DAW check records the host MIDI out**: section 8's tests hold `Engine::midiOut`, and the Reaper legs analyse
   audio.
@@ -322,18 +323,21 @@ The commits' reviews found more, fixed in the commits after them:
   cases are TS (the decoder, and the node on its rig's transport), the protocol rig and the bench feed it stand-ins,
   and the cases that capture from LSDj need the fixtures.
   [HostIo.test.cpp](../packages/native/test/engine-graph/HostIo.test.cpp) now runs both on an `Engine`: mGB's idle
-  line, which the capture clocks out of its serial port 0xFF byte after 0xFF byte and MI.OUT reads as LSDj's clock,
-  decoded into a 0xF8 at each completing byte's frame, inside each block of a ragged split; and a master-sync run
-  opened at 48 kHz, carried over `Engine::setSampleRate(44100)`, whose stop goes at frame 445 of the next block, the
-  first past 200 ms (the decoder before the rescale put it at frame -295, which the guard now sends at the block's last
-  frame).
-- **dsp-bench's LSDj windows needed a floor, and stay out of the valgrind modes.** MI.OUT's clocks come unevenly
-  (at the default warmup none in a window's first 20 blocks, 8 in its first 100, 40 in 400), so a window of
+  line, which the capture clocked out of its serial port 0xFF byte after 0xFF byte and MI.OUT read as LSDj's clock,
+  decoded into a 0xF8 at each completing byte's frame, inside each block of a ragged split (since kb2 the capture
+  feeds the 0s an Arduinoboy does, so the line is 0x00 bytes, which decode to nothing, and the case holds that); and
+  a master-sync run opened at 48 kHz, carried over `Engine::setSampleRate(44100)`, whose stop goes at frame 445 of
+  the next block, the first past 200 ms (the decoder before the rescale put it at frame -295, which the guard now
+  sends at the block's last frame).
+- **dsp-bench's LSDj windows needed a floor, and stay out of the valgrind modes.** MI.OUT's clocks came unevenly (at
+  the default warmup none in a window's first 20 blocks, 8 in its first 100, 40 in 400), so a window of
   `RP_BENCH_BLOCKS` 5, 10 or 20 failed the playing check for want of one: a window runs at least 100 blocks, and the
-  clock bound, like the zero, holds only at the default warmup. Their boots and warmups do not shrink with
-  `RP_BENCH_BLOCKS`, so `tools/run-profile.sh`'s valgrind modes, which lower it, would have run several times longer
-  with an LSDj's setup in the census of the mGB workload they attribute: `RP_BENCH_LSDJ=0` leaves the windows out,
-  and those modes default to it.
-- **dsp-bench's MI.OUT window decodes about half the NoteOns its song plays.** It sends 20 NoteOns in 400 blocks
-  (9.3 s), where the phrase's eight notes at tempo 128 come to about 40. Likely MI.OUT's rate-dependent decode
-  (section 4, B0's seventh defect); not looked into.
+  clock bound, like the zero, holds only at the default warmup. (Those clocks were the defect kb2 fixed, LSDj's song
+  sends none: the MI.OUT window counts NoteOns since, more than 3 a second, and the floor keeps a phrase of them.)
+  Their boots and warmups do not shrink with `RP_BENCH_BLOCKS`, so `tools/run-profile.sh`'s valgrind modes, which
+  lower it, would have run several times longer with an LSDj's setup in the census of the mGB workload they
+  attribute: `RP_BENCH_LSDJ=0` leaves the windows out, and those modes default to it.
+- **dsp-bench's MI.OUT window decoded about half the NoteOns its song plays: fixed with kb2.** It sent 20 NoteOns
+  in 400 blocks (9.3 s), where the phrase's eight notes at tempo 128 come to about 40; it now sends those 40. The
+  cause was B0's seventh defect (section 4): the capture fed the Game Boy 1s where an Arduinoboy holds its serial
+  input at 0 ([known-bugs-batch-2-design.md](known-bugs-batch-2-design.md) section 3).
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 442512c3b..53780e58e 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -620,9 +620,11 @@ completed it, and the decoders send each message in that same block, at its byte
 `renderAudio` block it went out in (`renderBlocks` adds the block's index). The core's own byte timing
 moves by tens of frames with the host's block split, so a test holds each message to its byte, not to one
 absolute time shared by every split.
-See [lsdj-midiout.test.ts](../packages/retroplug/test-native/lsdj-midiout.test.ts),
-which authors SYNC=MI.OUT with `N` commands, plays, and asserts a transport START, a clock
-stream and NoteOns come back out as host MIDI, at frames inside their blocks and mostly past frame 0, and
+See [lsdj-midiout.test.ts](../packages/retroplug/test-native/lsdj-midiout.test.ts) (and its 48 and 96 kHz
+twins), which authors SYNC=MI.OUT with `N` commands, plays, stops and plays again, and holds the host MIDI to
+exactly what the song sends: both transport edges and its 20 NoteOns with their own values, no clock (the song sends
+none) and no Program Change, at frames inside their blocks and mostly past frame 0 (its second case plays N7F,
+which decodes as a clock), and
 [lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts), which holds master
 sync to one story (start, clocks, stop, start again) in three block splits at 44.1 and 48 kHz, and its
 clocks to LSDj's tempo in absolute frames in every split: consecutive clocks of a run within 3% of a tempo
@@ -648,11 +650,20 @@ play that role:
       if ((sc & 0x81) == 0x80) {
           const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
           captureSerialOutBit(outBit);           // MI.OUT: capture LSDJ's outgoing byte
-          GB_serial_set_data_bit(gb_, true);      // clock it; feed idle-high back on SIN
+          GB_serial_set_data_bit(gb_, false);     // clock it; feed SIN the 0 an Arduinoboy holds it at
       }
   }
   ```
 
+  **SIN is fed 0, as an Arduinoboy holds it.** The firmware clocks with `GB_SET(clock, 0, 0)` throughout
+  (`Mode_LSDJ_Midiout.ino`'s `getIncommingSlaveByte`), so every bit the Game Boy shifts in is 0 and SB reads `$00`
+  once a byte is out. LSDj's serial interrupt re-arms the port (`SC=$80`, at `$363C`) after every transfer, whether
+  or not it has a byte to send, so the capture also clocks bits the CPU never wrote: as 0s they are idle bits to the
+  decoder. Fed 1s, as the capture was until kb2, each of them read as a start bit and took the next 7 real bits as
+  its payload, re-framing the stream across LSDj's bytes by an amount the host rate set, so MI.OUT decoded about half
+  a song's NoteOns, most of them as other notes, plus clocks and Program Changes it never sent
+  ([known-bugs-batch-2-design.md](known-bugs-batch-2-design.md) section 3).
+
   **Drive only, never master sync.** A stopped stock LSDj in SYNC=LSDj also waits on an external clock,
   for another master. Clocked, it takes the idle `0xFF` bytes for a master's clock, turns slave and
   ignores START, and every bit clocked is captured: the "idle flood" (one bit a frame, about 5500 bytes a
@@ -694,8 +705,8 @@ always 0). Decoder rules:
 | `0x7C` | Reserved / no-op. The firmware consumes the value byte but does nothing. |
 | `0x7D` | Transport start — emit `0xFA`. |
 | `0x7E` | Transport stop — emit `0xFC`. |
-| `0x7F` | Clock tick — emit `0xF8`. |
-| `0x80+` | NOT part of MI.OUT. Captured for diagnostics but the decoder ignores them. |
+| `0x7F` | Clock tick: emit `0xF8`. LSDj's MI.OUT sends none of its own (below); an N, X or Y value of `0x7F` puts one on the wire. |
+| `0x80+` | Never a payload: the framing yields 7-bit bytes (LSDj writes each with its high bit set, the frame's start bit), and the decoder's byte layer drops one of `0x80` or more without disturbing a pending command. |
 
 LSDJ-side effect commands that drive this protocol (placed in note/table cells):
 
@@ -720,10 +731,16 @@ cleanly to byte 9). Verified empirically: **cold-authoring `syncMode:"MidiOut"`
 savestate fixture needed.
 
 The reason it *looked* like idle garbage: MI.OUT frames on the link as **1
-data-present flag bit + 7 payload bits**, so each data byte arrives as `0x80|payload`.
-`captureSerialOutBit` assembles fixed 8-bit bytes, which mis-frames the flag-gated
-stream into high-bit bytes. To decode, reconstruct the bit stream (MSB-first per
-captured byte) and apply the Arduinoboy framing — `read 1 flag; if 1 read 7 payload
-→ a 7-bit command byte (0x00-0x7F); else idle` — which recovers a clean stream, e.g.
-`7D(start) 7F(clock) 70 69(NoteOn ch0, note) 7F 70 6A …`. So functional MI.OUT
-playback IS achievable headlessly; the flag-gated framing is the load-bearing detail.
+data-present flag bit + 7 payload bits** (LSDj writes each byte as `0x80|payload`), and
+`captureSerialOutBit` assembles fixed 8-bit groups of whatever it clocks: LSDj's bytes and
+the idle bits between them, since LSDj's serial interrupt re-arms the port after every
+transfer, so a group is no wire byte. To decode, reconstruct the bit stream (MSB-first per
+captured byte) and apply the Arduinoboy framing (`read 1 flag; if 1 read 7 payload
+→ a 7-bit command byte (0x00-0x7F); else idle`), which recovers the stream LSDj wrote, e.g.
+`7D(start) 70 30(NoteOn ch0, note 48) 70 34 …` for the suite's song. So functional MI.OUT
+playback IS achievable headlessly; the flag-gated framing is the load-bearing detail. (An
+earlier version of this record read `7D 7F 70 69 7F 70 6A`: those clocks and notes were the
+capture's own, made while it fed the Game Boy 1s, above. LSDj's MI.OUT sender, bank 3
+`$7ADE`-`$7BBC` in 9.3.3, sends a command and its value for each N, X, Y and Q command and
+`7D`/`7E` at its transport's edges, never a clock: no setting adds one, and only an N, X or
+Y value of `7F` reaches the decoder's clock.)
diff --git a/docs/modular-graph-phase-b0-design.md b/docs/modular-graph-phase-b0-design.md
index ea54db146..2fa090ab4 100644
--- a/docs/modular-graph-phase-b0-design.md
+++ b/docs/modular-graph-phase-b0-design.md
@@ -210,12 +210,16 @@ master waits on one for another master, so, clocked, it turned slave: it flooded
 Master sync now only listens (capture's `listen` mode, MI.OUT keeping `drive`), and `masterSync-48000` plays,
 re-recorded with the eighth defect's fix.
 
-A seventh defect is recorded as it is, not fixed (§1): **MI.OUT decoding depends on the host sample rate.**
-Measured: the decoded NoteOns are 0x52-0x6D plus program changes the song never sends at 48 kHz, and
-0x69-0x6D at 44.1 kHz; neither matches the song's N values (48-72), so the capture is partly an artefact.
-The `midiOut` case pins what is captured today (at 48 kHz); fixing it is its own change with a re-record. A
-likely cause, unconfirmed: MI.OUT's capture clocks LSDj one bit per audio frame, so its bit rate is the host's
-sample rate ([lsdj-master-sync-fix-design.md](lsdj-master-sync-fix-design.md) section 2).
+A seventh defect was recorded as it was (§1), and has since been fixed: **MI.OUT decoding depended on the host
+sample rate.** Measured: the decoded NoteOns were 0x52-0x6D plus program changes the song never sends at 48 kHz, and
+0x69-0x6D at 44.1 kHz; neither matches the song's N values (48-72), so the capture was partly an artefact. The
+`midiOut` case pinned what was captured (at 48 kHz). The cause, found with kb2
+([known-bugs-batch-2-design.md](known-bugs-batch-2-design.md) section 3), was not the bit rate this record guessed
+at: the capture fed the Game Boy 1s on its serial input, where an Arduinoboy holds it at 0, and LSDj's serial
+interrupt re-arms the port after every transfer, so the bits clocked between LSDj's writes were 1s, start bits to
+the decoder, which re-framed the stream by an amount the host rate set. Fed 0s, the decode is the song's at every
+rate from 8 to 192 kHz, and the four MI.OUT cases (`lsdj-modes`' `midiOut` and `blocks-tiny-lsdj`'s three
+`midiOut-*`) were re-recorded with the fix, in their MIDI out and work RAM alone.
 
 An eighth defect was recorded as it was (§1), and has since been fixed: **in small host blocks LSDj master sync
 decoded a stopped LSDj as clocks.** The decoder (`arduinoboyMasterSyncBlock`, `lsdjArduinoboy.ts`) took a block
@@ -247,7 +251,7 @@ suite is 23 test files with `determinism`, `render-blocks` and `comparator`); th
 | `edits-live`, `edits-structure` | gain through a held note, live role knobs; reset, duplicate, add, remove, replace mid-run, a split losing and regaining its single system, a pin split left | edit timing, per-frame gain smoothing, the tap restore; fix 2 is covered by its unit test in `test/systems/bliptoaster-assets.test.ts`, not by this case (duplicate-mid-run duplicates an mGB, which has no construct hooks) |
 | `sync-sms` | smsggdj SYNC IN24 on SMS and GG: start, bpm change, stop, locate, a short block at a loop wrap whose end is off the 24-PPQN tick grid (so its locate shows); at 48 kHz / 512 and at 44.1 kHz / 8192-frame blocks | transport-driven sync |
 | `lsdj-sync` *(fixture)* | SYNC=MIDI at divisors 1/2/4/8 and with autoStart; Arduinoboy at 1024 (divisors 1 and 4) and 512, ending with a transport stop | lsdj-sync's clock modes; the Arduinoboy defect |
-| `lsdj-modes` *(fixture)* | off, keyboard (inert: the role emits nothing), midiMap (clocked by the transport), keyboardMidi, midiPassthrough (raw clocks into a dense SYNC=MIDI song), midiOut (play / stop / play; the sample-rate defect), masterSync at 44.1 and 48 kHz (the 48 kHz one pinned the sixth defect until the master-sync fix) | every other lsdj-sync mode; MI.OUT and master clock as MIDI out |
+| `lsdj-modes` *(fixture)* | off, keyboard (inert: the role emits nothing), midiMap (clocked by the transport), keyboardMidi, midiPassthrough (raw clocks into a dense SYNC=MIDI song), midiOut (play / stop / play; it pinned the seventh defect until the kb2 fix), masterSync at 44.1 and 48 kHz (the 48 kHz one pinned the sixth defect until the master-sync fix) | every other lsdj-sync mode; MI.OUT and master clock as MIDI out |
 | `lsdj-link` *(fixture)* | leader and follower on one cable, in blocks of 512 and of every other size `blocks-*` uses (64, 199, 1024, 8192 and the cycle) | link-cable sync; lockstep stepping across partitions (the follower's lanes differ in each) |
 | `risa` *(fixture)* | the transport script, also under PinSplit | risa-sync |
 | `gba` *(fixture)* | nanoloop booting under Stereo, and under TwoPerInstance with START pressed (silent until played) | the GBA core at all |
@@ -426,8 +430,8 @@ The implementation plan's tasks, each verified before the next:
 - Exact audio on arm64, macOS and Windows: those targets get the determinism and integer-state checks of
   §5.2 (never yet run), and §1's argument for the audio bits, which a B1 change to multiply-add fusion on
   arm64 could break unseen.
-- Fixing either of the two defects still pinned (§2.5), the Arduinoboy block-size one and MI.OUT's sample-rate
-  dependence; the narrow-host NES split and LSDj master-sync defects were fixed with re-records since.
+- Fixing the defect still pinned (§2.5), the Arduinoboy block-size one; the narrow-host NES split, LSDj master-sync
+  and MI.OUT sample-rate defects were fixed with re-records since.
 
 Regression classes the suite would not catch, so a B step that touches one needs its own check:
 
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index 76bbd9ca6..eced02d5e 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -438,8 +438,20 @@ and leaves out a later one, and a kind already placed, whole: the role stays on
 load, `adopt` first moves each role the ROM's providers suggest ahead of the earliest role they do not suggest whose
 node writes an input its own does, so the role today's ROM calls for is the one placed; the left-out one is logged
 once, after the core has built. The serial-out capture and the reported sync latency follow the `lsdj-sync` the
-pipeline places (`placedRole`), so a left-out one arms nothing. `DirectGraph.setPipeline` sends the document
-without the later role and can report it through a callback.
+pipeline places, so a left-out one arms nothing. `DirectGraph.setPipeline` sends the document without the later role
+and can report it through a callback.
+
+**Readers ask what is placed.** Whatever decides what a system is asks the same question: `placedRole(roles, kind)`,
+beside `protocolPipeline`, is the role of `kind` the pipeline places, and a role whose kind has no node (a core's
+config role, an asset override, a tracker's marker such as risa's `risa`) counts as placed when it is present, since
+the pipeline never leaves one out. The tracker a system resolves (`resolveTracker`, behind every tracker menu, song
+import, the render's tracker and the live SAV), the config rows its menu shows (`roleRowItems`) and the DAW lanes it
+exposes (`projectParameterMap`, the first placed role with a parameter table) read it, so an mGB cart that still
+stores `lsdj-sync` after a ROM swap is no LSDj cart, and an LSDj cart that stores `mgb` exposes no mGB lanes. The
+system view says the same in `placed` (`SystemInfo.placed`, from `placedKinds`), which a package's cart view asks
+through `isPlaced` (`@retroplug/core/systemRoles`). Reads of a role's own config, where presence is right (the asset
+overrides, the core's config, the role editor, persistence), keep `hasRole` and `roleConfig`, and the left-out role
+stays stored, so a ROM swapped back brings its settings back.
 
 **Loaded role configs are parsed.** A loaded protocol role's config is parsed by its `RoleType` schema in `adopt`
 (so the node's settings, the capture its Game Boy's node runs, the menus and a save all see the parsed one), and a
@@ -469,11 +481,14 @@ The rest of the document follows from those nodes:
   from the cart's battery, on the control plane. The `lsdj-sync` nodes of the cart the MI.MAP app drives
   are forced to `midiMap`, or to `off` when the cart's own SYNC setting is not MI.MAP
   (`controllerSyncOverride`, whose rule is the app's `protocol.kinds.controllerApps` entry `syncOverride` as DATA: the
-  cart sync node
-  type, the mode it forces, the polled SYNC value that counts as the cart listening, and the mode not listening
-  means), its `rp.lsdj.serial-out` with its `rp.lsdj.sync`, so a forced cart sends no
-  host MIDI, and its Game Boy's node captures nothing for them. The override is those nodes' settings and that
-  capture alone, so nothing reaches the `.rplg`.
+  cart sync node type, the mode it forces, the polled SYNC value that counts as the cart listening, the mode not
+  listening means and the name of that node's setting that divides its clock), its `rp.lsdj.serial-out` with its
+  `rp.lsdj.sync`, so a forced cart sends no host MIDI, and its Game Boy's node captures nothing for them. The
+  override is those nodes' settings and that capture alone, so nothing reaches the `.rplg`. The session is handed
+  that cart's divisor as `tickDivisor` (the setting the rule's `divisor` names, read off the cart's node in the
+  pipeline it is placed from, 1 when it cannot be read), and walks `24 / tickDivisor` ticks a quarter, the
+  expression MI.MAP clocks the cart by, so its predicted playhead stays on the cart's; the session reads it every
+  block, so a divisor changed while a song plays reaches it without a rebuild.
 
 Where nothing produces for a system input, or no controller is enabled, the input or the host I/O node is
 left unwired. The plugin loads nothing for any of this
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index efacdcb2a..f9b199043 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -434,11 +434,10 @@ the allocator library is `EXCLUDE_FROM_ALL` because only test binaries (built by
    [launchpad/ts/control/kind.ts](../packages/launchpad/ts/control/kind.ts), with MI.MAP contributing the control-plane
    half from [lsdj-launchpad/ts/app.ts](../packages/lsdj-launchpad/ts/app.ts) and the runtime half and `syncOverride`
    data from its manifest. What an app's launches need the cart to run in travels as `syncOverride` DATA (`node`,
-   `mode`,
-   `listening`, `off`), applied by the launchpad pack's graph placer (`ts/control/placer.ts`), never as a function a
-   planner's bundle would have to import. The generator and the fixture suite hold the mechanism over a fixture kind
-   that is no real pack's (`widgets`); each real kind's own rules are its package's test
-   ([kind.test.ts](../packages/launchpad/ts/test/kind.test.ts)).
+   `mode`, `listening`, `off`, `divisor`), applied by the launchpad pack's graph placer (`ts/control/placer.ts`),
+   never as a function a planner's bundle would have to import. The generator and the fixture suite hold the
+   mechanism over a fixture kind that is no real pack's (`widgets`); each real kind's own rules are its package's
+   test ([kind.test.ts](../packages/launchpad/ts/test/kind.test.ts)).
 9. A device pack's imports are limited to `build-device-pack.mjs`'s four-file core SDK (`defineNode`,
    `settingsSchema`, `descriptor`, `register`), and `pico/n8-host/exporter-stale.sh` watches `packages/core/ts` plus
    each pack's own `ts/`.
@@ -672,24 +671,26 @@ The suite is 30 files: a file per group, and four with no baseline of their own
   passes) gives the files to compare against. An
   intentional audio change re-records the groups it moves in the same commit, and that diff is the record
   of what moved.
-- **Recorded defects.** The baseline pins two engine defects on purpose; fixing each is its own change
+- **Recorded defects.** The baseline pins one engine defect on purpose; fixing it is its own change
   with a deliberate re-record: Arduinoboy MIDI sync plays only in some host block partitions (at divisor
   1 silent at 128 to 768 frames, in 1-frame blocks and in `fl256`, playing in `fl`, most of whose blocks
-  carry almost no clock tick; at divisor 4 silent at 1024 too); MI.OUT's decoded notes depend on the host sample
-  rate. Details in design section 2.5
-  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Three more were pinned and
+  carry almost no clock tick; at divisor 4 silent at 1024 too). Details in design section 2.5
+  ([modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md)). Four more were pinned and
   fixed: a single NES under a split routing on a host narrower than the split (the capture tap armed over the mix,
   which drifted ~25 frames per block at 48 kHz / 512; the core now steps on the ring the block drains, so the
   narrow host hears the mix, and its cases were re-recorded with that fix, design section 2.5 and
-  [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md) section 10), and LSDj master sync
-  silent at 48 kHz together with its decoder reading a stopped LSDj as clocks in small
-  blocks ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)). The MI.OUT and
-  master-sync cases (`lsdj-modes`' `midiOut` and two `masterSync`, all six of `blocks-tiny-lsdj-master`,
-  `blocks-tiny-lsdj`'s three `midiOut-*`) record the host MIDI out of `rp.lsdj.serial-out`, after the Game Boy
-  ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) sections 7 and 12): each message at its
-  byte's frame in the block the core captured it, which here lies off frame 0 but in the 1-frame splits and for
-  one message of 85, a clock, in each master-sync `fl256` case, whose byte the core completed at its block's
-  first frame.
+  [modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md) section 10), LSDj master sync
+  silent at 48 kHz together with its decoder reading a stopped LSDj as clocks in small blocks
+  ([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)), and MI.OUT's decoded notes depending
+  on the host sample rate (the capture fed the Game Boy 1s where an Arduinoboy holds its serial input at 0, so the
+  bits it clocked between LSDj's writes read as start bits; the four MI.OUT cases were re-recorded with the fix,
+  their MIDI out and work RAM alone, [known-bugs-batch-2-design.md](../docs/known-bugs-batch-2-design.md) section
+  3). The MI.OUT and master-sync cases (`lsdj-modes`' `midiOut` and two `masterSync`, all six of
+  `blocks-tiny-lsdj-master`, `blocks-tiny-lsdj`'s three `midiOut-*`) record the host MIDI out of
+  `rp.lsdj.serial-out`, after the Game Boy ([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) sections
+  7 and 12): each message at its byte's frame in the block the core captured it, which here lies off frame 0 but in
+  the 1-frame splits and for one message of 85, a clock, in each master-sync `fl256` case, whose byte the core
+  completed at its block's first frame.
 - LSDj, risa, nanoloop and the controller cases need the sibling fixtures (`RETROPLUG_RESOURCES_DIR`), so
   those groups skip in CI, counted in the skip baseline. CI therefore runs only the 152 fixture-free cases
   (every MIDI-out, controller, LSDj, risa and GBA case is fixture-only, LSDj's link-cable cases among
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index bbc56c7eb..a90da587f 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -41,11 +41,14 @@ each (a phase name means its "What it leaves" list in section 5).
 - [ ] The shape question the view reopen key's width leaves open (F4c)
 
 **Known bugs**
-- [ ] "Is this LSDj" decided by a role's presence, not its placement; needs `placedRole` (found building B3b)
-- [ ] MI.MAP's predicted playhead runs ahead at a tempo divisor of 2 or more (F3)
+- [x] "Is this LSDj" decided by a role's presence, not its placement (found building B3b; kb2, <t1>: the readers ask
+      `placedRole`, a cart view `isPlaced`, and the stored role stays)
+- [x] MI.MAP's predicted playhead runs ahead at a tempo divisor of 2 or more (F3; kb2, <t2>: the session walks the
+      divisor its cart is set to)
 - [x] `__rp_syncLatencyMs` follows the stored sync mode, not the one MI.MAP forces (C2; unreachable (F5a), closed
       with kb1: no plugin binary's list carries a controller app to force one)
-- [ ] MI.OUT decodes about half the NoteOns a song plays; not investigated (LSDj's serial-out node)
+- [x] MI.OUT decodes about half the NoteOns a song plays (LSDj's serial-out node; kb2, <t3>: the capture fed the Game
+      Boy 1s where an Arduinoboy holds its serial input at 0)
 - [ ] The DPF JACK standalone's control handoff runs on JACK's thread at a buffer-size or rate change (C5)
 - [x] A Game Boy's press queue can drop a flood's last release, leaving a button held (C4; kb1, 431e20007)
 - [x] A Game Boy button byte of 8 or more writes past SameBoy's key arrays in a release build (kb1, 431e20007)
@@ -758,9 +761,22 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     the shift), which predates the decoder's move. The decoder adds no split dependence of its own, so the tests
     hold each message to its byte, not to one absolute time shared by every split. A SameBoy matter, its own change
     if wanted.
-  - **MI.OUT decodes about half the NoteOns a song plays.** dsp-bench's MI.OUT window sends 20 NoteOns in 400
-    blocks (9.3 s), where its phrase's eight notes at tempo 128 come to about 40. Likely MI.OUT's rate-dependent
-    decode, one of the defects parity pins ([06-build-test.md](06-build-test.md)); not looked into.
+  - **MI.OUT decoded about half the NoteOns a song plays: fixed with kb2**
+    ([known-bugs-batch-2-design.md](../docs/known-bugs-batch-2-design.md) section 3). The capture clocked LSDj's port
+    as an Arduinoboy does but fed the Game Boy 1s on its serial input, where the Arduinoboy holds it at 0, and LSDj's
+    serial interrupt re-arms the port after every transfer, so the bits clocked between LSDj's writes were 1s, which
+    the decoder reads as start bits: the stream was re-framed by an amount the host rate set (12, 14 and 5 NoteOns,
+    nearly all of them other notes, for the song's 20 at 44.1, 48 and 96 kHz). Fed 0s, `lsdj-midiout` holds the song's
+    exact MIDI at all three rates and dsp-bench's window decodes all 40 NoteOns its phrase plays in 400 blocks;
+    parity's four MI.OUT cases were re-recorded, their MIDI out and work RAM (LSDj's received-byte shadow at `$CC20`,
+    now `$00`) alone. Two things stay. CI decodes no real LSDj's MI.OUT: the fixture-free `HostIo.test.cpp` cases hold
+    the capture's wiring and that mGB's idle line decodes to nothing, and the real decode is the fixture-gated
+    `lsdj-midiout` files' and parity's. And no setting makes LSDj send a clock: its MI.OUT sender (bank 3,
+    `$7ADE`-`$7BBC` in 9.3.3) sends a command and its value for each N, X, Y and Q command and 0x7D and 0x7E at its
+    transport's edges, never 0x7F. The one way a real LSDj reaches the decoder's clock is song data, an N command
+    whose value is 0x7F, which `lsdj-midiout` holds decoding as 0xF8, as an Arduinoboy reads it; the clock path's
+    other cases are the decoder's unit test
+    ([lsdjArduinoboy.test.ts](../packages/lsdj/ts/test/lsdjArduinoboy.test.ts)).
   - **No DAW check records LSDj's host MIDI out.** What MI.OUT and master sync send is held at `Engine::midiOut`
     (the native tests, parity's MIDI-out hashes), never as a DAW records the plugin's `writeMidiEvent`: the Reaper
     legs (`reaper:lsdj-*`) drive LSDj from the DAW and analyse the audio. A leg that records the plugin's MIDI out
@@ -777,19 +793,18 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - `DirectGraph.setPipeline` hands a node its role's config as given, where a store hands the config its
     schema parsed, and a node's settings fill their own defaults: a direct caller's `lsdj-sync` with no `mode`
     is MIDI sync, where the kernel read a missing mode as nothing.
-  - **Readers that decide "is this system LSDj" (or mGB) by a role's presence, not its placement.** In the rare
-    case a ROM path changed between an LSDj and an mGB build, a system holds both `mgb` and `lsdj-sync`, and the
-    pipeline places one (the one the ROM calls for). The graph is right; these readers are not:
-    `trackerIntegrationFor` ([trackerIntegration.ts](../packages/tracker/ts/trackerIntegration.ts), by
-    `markerRole` presence), so the LSDj menu's sync cyclers
-    ([menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts)) edit a left-out `lsdj-sync` on an mGB cart;
-    `hasRole(..., "lsdj-sync")` in
-    [lsdj/ts/ui/hdView.ts](../packages/lsdj/ts/ui/hdView.ts) and
-    [lsdj/ts/ui/overlayView.ts](../packages/lsdj/ts/ui/overlayView.ts) and `projectStore.ts` (which tests
-    `roles.some((r) => r.kind === "lsdj-sync")`); and `projectParameterMap`
-    ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)), which still exposes mGB's CC lanes for an LSDj
-    cart carrying a stale `mgb`. The fix is `placedRole(...)`
-    ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
+  - **Readers that decided "is this system LSDj" (or mGB) by a role's presence: fixed with kb2**
+    ([known-bugs-batch-2-design.md](../docs/known-bugs-batch-2-design.md) section 1). In the rare case a ROM path
+    changed between an LSDj and an mGB build, a system holds both `mgb` and `lsdj-sync`, and the pipeline places one
+    (the one the ROM calls for). The readers now ask what it places: `resolveTracker`
+    ([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts), by its integration's placed `markerRole`;
+    behind the tracker menus, song import, the render's tracker and `projectStore`'s tracker questions), the config
+    rows (`roleRowItems`, [configRows.ts](../packages/retroplug/ui/screens/menu/configRows.ts)) and
+    `projectParameterMap` ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)) through `placedRole`
+    ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)), and the LSDj HD view, the LSDj
+    overlay and the risa overlay through `isPlaced`, over the system view's `placed`. A role whose kind has no node
+    counts as placed when present, and the left-out role stays stored, so a ROM swapped back brings its settings
+    back.
   - **The controller's MI.MAP override changed only the document: closed by C2.** `controllerSyncOverride`
     ([launchpad/ts/control/placer.ts](../packages/launchpad/ts/control/placer.ts), applied in `buildGraph`) runs an LSDj's
     nodes in `midiMap` for a cart reading MidiMap (in `off` for one reading another SYNC), and `serialOutCaptureFor`,
@@ -912,11 +927,12 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **Third-party controller apps are not loadable**: `controllerApps` registers from the product's package list, so an app
     from outside the tree is the downloadable-pack road (design section 11).
   - **One controller link per process**, and per `rp.host.devices/2` name; more than one is a registry change.
-  - **The controller's tick count assumes the default tempo divisor.** The session walks 24 PPQ ticks for the predictor,
-    and an app's sync override sets the cart's sync MODE only: MI.MAP clocks the cart at `24 / tempoDivisor`, and the menu
-    offers 1 to 8, so at divisor 2 and above the predicted playhead (and the quantise) runs that many times ahead of the
-    cart. At the default divisor 1 the two agree. A fix either lets `syncOverride` carry the setting it needs as data,
-    rendered into the cart's sync node, or walks at the divisor the cart is set to.
+  - **The controller's tick count assumed the default tempo divisor: fixed with kb2**
+    ([known-bugs-batch-2-design.md](../docs/known-bugs-batch-2-design.md) section 2). The app's `syncOverride` names
+    the sync node's divisor setting (`divisor`, LSDj's `tempoDivisor`), the placer reads it off the driven cart's node
+    in the pipeline and hands the session `tickDivisor`, and the session walks `24 / tickDivisor` ticks a quarter, as
+    MI.MAP clocks the cart, reading it every block. A quantise of `bar` or `beat` still counts the host's bars, as
+    before: at divisor 2 a cart's phrase lasts two of them.
   - **The store's song poll stays in the shell** (the controller's song refresh, the anchor and the SYNC poll in
     `projectStore.ts`) until F4, which also takes the menu rows.
 - **F4a built: the non-UI registrations**
````

- [ ] **Step 2: fill in the three hashes**

The batch's own commits are the last three before this task's:

```bash
git -C /workspaces/RetroPlug-main/cuttlefish log --format='%h %s' -4
```

Expected: this task's commit is not there yet, so the four lines are, newest first, the three below and then
`docs(kb2): design the LSDj known-bugs batch` (or whatever commit the batch started on, if the design's as-built
section was committed in between). Replace each placeholder with the short hash of the commit whose subject it names:

| Placeholder | Commit subject |
|---|---|
| `<t1>` | `fix(kb2): decide what a system is by what its graph places` |
| `<t2>` | `fix(kb2): walk the MI.MAP predictor at the divisor its cart is set to` |
| `<t3>` | `fix(kb2): feed the Game Boy 0 on MI.OUT's clocked serial input, as an Arduinoboy does` |

Then check that none is left:

```bash
grep -n '<t[0-9]>' /workspaces/RetroPlug-main/cuttlefish/spec/07-remaining-work.md
```

Expected: no output (the `grep` exits 1, which is what "nothing left" looks like).

- [ ] **Step 3: check the docs**

```bash
pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: the same counts as before this task, the one warning being a plan record's pre-existing dead link:

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 106 file(s) clean, 1 warning(s)
```

- [ ] **Step 4: check that no em dash came in**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish diff -- spec AGENTS.md docs | grep '^+' | grep -c $'\u2014'
```

Expected: `0`.

- [ ] **Step 5: commit**

```bash
git -C /workspaces/RetroPlug-main/cuttlefish add -- \
  spec/07-remaining-work.md spec/04-roles-protocol-nodes.md spec/06-build-test.md AGENTS.md \
  docs/lsdj.md docs/modular-graph-phase-b0-design.md docs/lsdj-serial-out-design.md docs/known-bugs-batch-2-design.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(kb2): record the LSDj known-bugs batch

spec/07's known-bugs list ticks the batch's three items with their commits, and its long entries say what is true now:
the readers ask placement (placedRole, isPlaced over the system view's `placed`) and stored roles stay, the session
walks the divisor its cart's sync node is set to, and MI.OUT's capture feeds the Game Boy the 0s an Arduinoboy does.
They also say what stays: CI decodes no real LSDj's MI.OUT, and no LSDj setting sends a clock, the one real-core path
to the decoder's clock being an N command's value of 0x7F. spec/04 describes the readers and the session's divisor,
spec/06 the sync rule's new key and the one parity defect still pinned, which AGENTS.md counts too. docs/lsdj.md
corrects the capture snippet, the fed bit's reason, the 0x7F and 0x80+ rows and the stream it once recorded; the B0
design records the seventh defect fixed and the serial-out design its leftovers closed. The batch's design has its
long prose lines rewrapped.
MSG
```
