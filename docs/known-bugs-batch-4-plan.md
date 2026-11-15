# Known bugs batch 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The five entries in [spec/07](../spec/07-remaining-work.md)'s known bugs that
[known-bugs-batch-4-design.md](../docs/known-bugs-batch-4-design.md) settles: a system's tracker is the marker its
ROM earns, a launch's quantise offers the cart's clock and the DAW's, the JACK standalone's lifecycle handoff leaves
the control thread alone, a cart-node handover releases only the notes the leaving node left, and a cart at its menu
is not streamed to; then the documents record the batch.

**Architecture:** Tasks 1 and 2 are the TypeScript control plane. Task 1 has `SystemsStore.suggestedFirst` treat the
tracker marker roles of `TRACKER_INTEGRATIONS` as colliding (the marker today's ROM earns moves ahead of a stale one)
and `resolveTracker` read the first PLACED marker in the system's own role order. Task 2 adds `hostBeat` and `hostBar`
to MI.MAP's `QUANTISE_VALUES`, makes `beat` and `bar` count the cart's clock (`24` and `96` host ticks times the
cart's divisor), and puts that divisor on the app's controller context (`ControllerCtx.tickDivisor`, per block). Task 3
extracts the plugin lifecycle's handoff into `plugin/LifecycleHandoff.hpp` and has `PluginDSP` delegate to it: a
foreign-thread size within the plan is a no-op, and a rate change or an over-plan size parks `run()` until the control
thread's idle applies it. Task 4 gives `CartLink` a `pushMark()` and a marked `releaseNotes(notes, mark)`, has
`rp.n8.nes` record the mark after each of its pushes, and has `N8Link` wait behind the mark and skip a key whose last
note-on is at or past it. Task 5 gives `N8Link` a cart-at-menu state, read from the FPGA config block and seeded at
connect, drops the stream there (and resets the reliable window and the ring), and reports it through `N8CartLink`'s
`connected` and Settings > N8's status row. Task 6 is the documents.

**Tech Stack:** TypeScript with the repo's own TAP runners (`pnpm test`, `pnpm test:native`), the generated package
registries (`packages/retroplug/scripts/lib/packages.mjs`), C++20 with Catch2 (`retroplug-plugin-test`,
`retroplug-n8-test`, `retroplug-engine-graph-test`), DPF's plugin exporter and JACK host under
`deps/dpf.js/deps/dpf` (read only), the N8 pack's Edio/N8Link/N8Host over `FakeN8`, and `pnpm check:docs`.

**Spec:** [known-bugs-batch-4-design.md](../docs/known-bugs-batch-4-design.md). Its section 6 lists the tasks and the
acceptance; the acceptance run is the controller's, after this batch lands.

**Provenance:** every code block below was applied to this worktree at `ccc01889e` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3 (`linux-x64-gnu13`), 32
cores, `RETROPLUG_RESOURCES_DIR` unset (no case here needs a fixture). The plan was revised twice: first after a review
of tasks 3 and 5 (the engine's ownership across a format's lifecycle calls, and the menu guard's one-second read), then
again after a second review of the same two tasks, which found that the guard's discards never advanced the release's
push count (a menu episode left every later release waiting, and a handover cutting the new node's key), that a re-send
could slip past the guard by reading the clock after it (the poll now takes the turn's timestamp), that the handoff's
dropped-MIDI count was written by two threads, and that the one-second case's last assertion could not fail. Those
fixes are Steps 5 to 8 of task 5 and the handful of lines they add to task 3; every block below was then re-applied to
the clean tree from task 3, each test step's red and each implementation step's green re-run, and the wider suites with
them. There is no prototype branch, so afterward the worktree was restored to its starting state and the plan was
replayed by text (`replay.mjs`): each task's blocks applied in order to copies of the touched files at `ccc01889e`, the
result byte-identical to the validated cumulative diff. Every Expected line is quoted from a real run (a count that
moves by a few between runs, such as `retroplug-engine-graph-test`'s assertions, is quoted as one run printed it). A
failing-test step's output is what the code without that step's change produces, quoted as it printed. Task 6 changes
documents only and has no failing test.

**How to apply a step's code.** Each code step gives one `patch` block, a `git apply`-able unified diff against the tree
as the steps before it left it. The blocks are fenced with four backticks, because a docs patch carries three-backtick
lines of its own. Apply one by running this with the block's text (not its fences) between the heredoc's first line and
the closing `PATCH`, or by making the same hunks with the editor:

```
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the step's patch block>
PATCH
```

A patch that creates a file leaves it untracked, which `git status` shows as `??`; the commit's `git add` names it.

**Submodules.** No task touches one: `deps/dpf.js` and its nested checkouts are read only here, and `deps/sameboy` is
dirty from the configure and no step touches it.

**Hardware.** A real NES with an Everdrive N8 Pro, a Zoom L6, a capture card and MIDI interfaces hang off this
container, so nothing in this plan may open one. Every command below that starts the CLI, the test host, a
`pnpm test*` suite, a Catch2 binary or a build under `build/` is written under the guarded wrapper, which preloads the
tracked device guard ([tools/sdl-audio-guard](../tools/sdl-audio-guard)), self-tests it and refuses `/dev/snd`,
`/dev/video*`, `/dev/ttyACM*`, `/dev/ttyUSB*`, `/dev/serial/*`, `/dev/hidraw*` and `/dev/bus/usb/*` before the syscall:

```
timeout -k 10 <seconds> .superpowers/kb4plan/guarded.sh <command...>
```

The wrapper is a work file of this plan's directory (`guarded.sh`, beside this plan in `.superpowers/kb4plan/`, which
is gitignored); it takes any command and returns its exit code, printing what the guard refused, if anything. Use 300
for one test file, 1800 for a whole suite. A refusal it prints is a finding: stop and find what tried to open a device.
N8 tests reach the cart only through `FakeN8` and fake serial transports. Never run `retroplug-sdl`, `retroplug-jack`,
`jackd`, any `sdl:*` or `reaper:*` script, or the verification kits: the controller runs acceptance, and the by-hand
JACK leg of design section 3 is the controller's too.

## Ground rules

These are the repo's rules this batch must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish`. Never switch branches, never `git stash`, never `git reset --hard`,
  never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset, stash or check out a path in it, and never bump a submodule pointer: no task here touches one.
- **Never `rm -rf build`** (or any `build*/`). Build a target with `cmake --build <dir> --target <t> -j$(nproc)`: the
  default is single-threaded.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";".
- **A commit message is a subject line, a blank line, then the body**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution of any kind.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **Never edit an existing `docs/*-plan.md` or design doc.** The batch's own design
  ([known-bugs-batch-4-design.md](../docs/known-bugs-batch-4-design.md)) gains its as-built and execution sections
  from the controller, not from a task here.
- **Only a step's named paths are staged.** `git add` exactly what the step lists; a task's commit is its own
  `git diff` and nothing else.
- **Never send repository content to an external service.**

---
## Task 1: a cart's tracker is the one its ROM earns

The `risa` and `bliptoaster` marker roles have no protocol node, so both count as placed whenever they are stored: a
NES project stored with risa whose path now holds BlipToaster carries both, and `resolveTracker`
([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)) takes the first in PRODUCT order (`risa` is
generated before `bliptoaster`). The load already asks the ROM providers which roles today's file earns
(`SystemsStore.adopt` -> `defaultRoles` -> `withMissingFeatureRoles`, then `suggestedFirst`), and `suggestedFirst`
moves a suggested role ahead of a stale one it COLLIDES with, where "collides" means "writes a system input the other
writes". A marker has no node and writes nothing, so nothing moved.

**Files** (line numbers are the file at `ccc01889e`):

- Modify: `packages/retroplug/src/systemsStore.ts:58-62` (the import), `:1417-1436` (`suggestedFirst`), and a new
  module-level `occupied()` beside `parsedRoleConfig` (`:73-78`).
- Modify: `packages/retroplug/src/trackerRegistry.ts:12-27` (the list's doc, the new `TRACKER_MARKERS`, and
  `resolveTracker`).
- Modify: `packages/retroplug/scripts/lib/packages.mjs:864-865` (the comment the generated list carries), and
  `packages/retroplug/test/packages/fixtures/expected/packages.ts:65-66` (the generator's expected copy).
- Test: `packages/retroplug/test/systems/store-graph.test.ts:28-37` (the imports) and two new cases after
  `:955` (`of two colliding roles neither of which the ROM calls for...`).

**Interfaces:**

- Consumes: `TRACKER_INTEGRATIONS` (already imported by `trackerRegistry.ts`, now also exported as `TRACKER_MARKERS`),
  `protocolWrites(kind)`, `placedKinds(roles)`, `SystemsStore.adopt`'s stored-role path.
- Produces: `TRACKER_MARKERS: ReadonlySet<string>` from `trackerRegistry.ts`; `resolveTracker(roles)` unchanged in
  signature, now order-sensitive; `suggestedFirst` unchanged in signature, now also moves a suggested marker ahead of a
  stale one. Nothing a later task consumes.

### Steps

- [ ] **Step 1: write the failing tests**

Two cases in `store-graph.test.ts`, following the colliding-roles cases above them: one for each swap direction (the
ROM earns BlipToaster over a stored risa, and risa over a stored BlipToaster, the stale marker and its asset overrides
kept), and one for a system that earns neither marker, in both stored orders. That second case iterates what "earns
nothing" can be: a readable iNES image no provider claims, and an embedded image, which is a declaration rather than
detection so there are no bytes to read at all. Its last assertion pins the third form - a stored PATH whose file has
gone - as one the store refuses to place ("could not read ... the file is absent or unreadable"), so an unreadable ROM
never reaches `resolveTracker` and there is no order for it to resolve. The imports gain `resolveTracker` and the
risa fixture.

````patch
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index ec09cffef..fab0407bc 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -28,13 +28,14 @@ import { z } from "zod";
 import { clampedInt } from "@retroplug/core/configFields";
 import type { DefinedNode } from "@retroplug/core/defineNode";
 import { ROLE_NODES, type LeftOutRole } from "../../src/graph/protocolNodes";
+import { resolveTracker } from "../../src/trackerRegistry";
 import { SCRIPT_NODES_GLOBAL } from "@retroplug/core/register";
 import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
 // The production bundle's entry point: importing it registers its node types on SCRIPT_NODES_GLOBAL, as it does in each
 // of the script host's contexts.
 import "../../src/script/bundle";
 import { warned } from "../_warnings";
-import { blipToasterRom, gbRom, lsdjRom, nesRom, smsggdjRom } from "./fixtures";
+import { blipToasterRom, gbRom, lsdjRom, nesRom, risaRom, smsggdjRom } from "./fixtures";
 
 /** The project section the Launchpad pack contributes: a case sets it through the store, as the menu does. */
 const CONTROLLER_SECTION = "controller";
@@ -957,6 +958,70 @@ test("of two colliding roles neither of which the ROM calls for, the first in ro
   expect(warnings.length).toBe(1);
 });
 
+test("of two tracker markers, a loaded system resolves the one its ROM earns, and keeps the stale one", () => {
+  // risa's and BlipToaster's marker roles have no protocol node, so both count as placed whenever they are stored, and
+  // resolveTracker took the first in PRODUCT order: a NES project stored with risa whose path now holds BlipToaster kept
+  // resolving risa whatever the file was. The marker the ROM earns moves ahead of the stale one, as a colliding protocol
+  // role does, and resolveTracker reads the system's own order. Nothing is deleted, so a ROM swapped back swaps back.
+  const { be, sys } = project();
+  be.seed("/roms/bliptoaster.nes", blipToasterRom());
+  be.seed("/roms/risa.nes", risaRom());
+  const mesen: RoleInstance = { kind: "mesen", config: {} };
+  const risa: RoleInstance[] = [{ kind: "risa", config: {} }, { kind: "risa-assets", config: { overrides: [] } }];
+  const blip: RoleInstance[] = [
+    { kind: "bliptoaster", config: {} },
+    { kind: "bliptoaster-assets", config: { overrides: [] } },
+  ];
+  const cases: [string, RoleInstance[], string][] = [
+    ["/roms/bliptoaster.nes", [mesen, ...risa], "bliptoaster"],
+    ["/roms/risa.nes", [mesen, ...blip], "risa"],
+  ];
+  for (const [romPath, roles, earned] of cases) {
+    const { result: id } = warned(() => sys.adopt({ romPath, core: "mesen", roles })!);
+    const stored = sys.view().find((v) => v.id === id)!.roles;
+    expect(resolveTracker(stored)?.id).toBe(earned);
+    // The stale marker and its asset overrides stay on the system, which is what a save then writes.
+    const stale = earned === "bliptoaster" ? "risa" : "bliptoaster";
+    expect(stored.map((r) => r.kind).includes(stale)).toBe(true);
+    sys.removeSystem(id!);
+  }
+});
+
+test("a ROM that earns no tracker marker leaves the stored marker order standing, either way round", () => {
+  // A ROM neither provider recognises earns no marker, so there is nothing to move ahead of anything and the first
+  // marker STORED is the system's tracker. The embedded iteration is the same case with no bytes to read at all: a
+  // declared image is never detected, and the ROM providers earn nothing from it either. (A stored PATH whose file
+  // has gone cannot reach this at all: the store refuses to place a system whose ROM it cannot read, so there is no
+  // order to resolve - the last assertion below is that refusal.)
+  const { be, sys } = project();
+  const mesen: RoleInstance = { kind: "mesen", config: {} };
+  const sameboy: RoleInstance = { kind: "sameboy", config: {} };
+  const orders: RoleInstance[][] = [
+    [{ kind: "risa", config: {} }, { kind: "bliptoaster", config: {} }],
+    [{ kind: "bliptoaster", config: {} }, { kind: "risa", config: {} }],
+  ];
+  be.seed("/roms/plain.nes", nesRom());
+  for (const order of orders) {
+    // Readable bytes no provider claims: classified from its iNES header, earned by nobody.
+    const { result: id } = warned(() =>
+      sys.adopt({ romPath: "/roms/plain.nes", core: "mesen", roles: [mesen, ...order] })!,
+    );
+    expect(resolveTracker(sys.view().find((v) => v.id === id)!.roles)?.id).toBe(order[0].kind);
+    sys.removeSystem(id!);
+    // And nothing to read: the embedded declaration names the platform, and no provider earns a marker.
+    const { result: embedded } = warned(() =>
+      sys.adopt({ core: "sameboy", embeddedRom: "mgb", roles: [sameboy, ...order] })!,
+    );
+    expect(resolveTracker(sys.view().find((v) => v.id === embedded)!.roles)?.id).toBe(order[0].kind);
+    sys.removeSystem(embedded!);
+  }
+  // No file, nothing to earn: the store places nothing, so no later load can resolve a tracker for it either.
+  const { result: absent } = warned(() =>
+    sys.adopt({ romPath: "/roms/gone.sms", core: "mesen", roles: [mesen, ...orders[0]] }),
+  );
+  expect(absent).toBe(null);
+});
+
 test("a collision's outcome survives a save and a load: the ROM's role first and placed, the stale one kept after it", () => {
   const { be, p, sys } = project();
   seedLsdjProject(be, "/proj/collide.rplg", [
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs systems/store-graph
```

Expected: FAIL, both cases at `resolveTracker`, for the reason the task fixes (the first stored marker is whatever
PRODUCT order prefers, here always risa):

```
ok 38 - of two colliding roles neither of which the ROM calls for, the first in role order is placed
not ok 39 - of two tracker markers, a loaded system resolves the one its ROM earns, and keeps the stale one
  ---
  expected "bliptoaster", got "risa"
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:92:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:100:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:32223:45)

  ...
not ok 40 - a ROM that earns no tracker marker leaves the stored marker order standing, either way round
  ---
  expected "bliptoaster", got "risa"
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:92:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:100:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/systems/store-graph.js:32242:80)

  ...
# 54 passed, 0 skipped, 2 failed

# 1/1 test file(s) FAILED: systems/store-graph
```

- [ ] **Step 3: implement it**

The module-level `occupied()` gives `suggestedFirst` one place every tracker marker competes for, beside the system
inputs a role's node writes; `resolveTracker` walks the PLACED kinds in role order instead of the integration list; and
the generated list's comment (in the generator and in its expected copy) says what the order now decides.

````patch
diff --git a/packages/retroplug/scripts/lib/packages.mjs b/packages/retroplug/scripts/lib/packages.mjs
index bf92ea6d6..78645adcc 100644
--- a/packages/retroplug/scripts/lib/packages.mjs
+++ b/packages/retroplug/scripts/lib/packages.mjs
@@ -861,8 +861,8 @@ function renderPackages({ packages, outDir, names, product }) {
     ...contributionLines(app, "romProviders"),
     "];",
     "",
-    "/** The tracker integrations, in product order: a system takes the first whose markerRole its graph places. A marker",
-    " *  has no protocol node, so a system storing two takes the first in this order; a duplicate is refused below. */",
+    "/** The tracker integrations, in product order. A system's tracker is the first of its marker roles that the system's",
+    " *  own role order places (resolveTracker), which a load orders by the ROM's own marker; a duplicate is refused below. */",
     "export const TRACKER_INTEGRATIONS: readonly TrackerIntegration[] = [",
     ...contributionLines(app, "trackerIntegrations"),
     "];",
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 530fe3163..971c773dc 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -56,7 +56,7 @@ import { type CommonSettings, DEFAULT_COMMON_SETTINGS, clampGain, commonSettings
 import { ROLE_HEADER_LEN, type RoleRegistry, type RoleInstance } from "@retroplug/core/systemRoles";
 import type { Host } from "@retroplug/core/host";
 import { placedKinds, protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
-import { resolveSongCatalog, resolveTracker } from "./trackerRegistry";
+import { resolveSongCatalog, resolveTracker, TRACKER_MARKERS } from "./trackerRegistry";
 import type { TrackedSlot, WorkingSongImage } from "@retroplug/tracker/songCatalog";
 
 // Frames the slot watcher waits for a landed carry's queued writes to appear before it trusts the cart
@@ -75,6 +75,15 @@ function parsedRoleConfig(registry: RoleRegistry | undefined, role: RoleInstance
   return rt ? (rt.schema.parse(role.config) as Record<string, unknown>) : role.config;
 }
 
+/** What a role kind competes for in SystemsStore.suggestedFirst: the system inputs its node writes, and - for a cart's
+ *  tracker marker - one place every marker kind shared. Two markers on one system are one ROM swapped for another's, and
+ *  resolveTracker reads the first of them in role order, so they collide the way two nodes writing one input do: the
+ *  marker today's ROM earns is moved ahead of the stale one, never deleted. */
+function occupied(kind: string): readonly string[] {
+  const writes: readonly string[] = protocolWrites(kind);
+  return TRACKER_MARKERS.has(kind) ? [...writes, "tracker-marker"] : writes;
+}
+
 // TS owns the system-id counter (native never allocates). Module-scoped so it's ONE id space per
 // control-plane JS context, which is 1:1 with the native Engine in every host (a plugin instance has
 // its own JS context + Engine; a native test file shares one host + Engine across its cases). Ids are
@@ -1428,8 +1437,8 @@ export class SystemsStore {
     const out = roles.slice();
     for (let i = 0; i < out.length; ++i) {
       if (!wanted.has(out[i].kind)) continue;
-      const writes = protocolWrites(out[i].kind);
-      const collides = (q: RoleInstance) => protocolWrites(q.kind).some((w) => writes.includes(w));
+      const writes = occupied(out[i].kind);
+      const collides = (q: RoleInstance) => occupied(q.kind).some((w) => writes.includes(w));
       const j = out.findIndex((q, k) => k < i && !wanted.has(q.kind) && collides(q));
       if (j >= 0) out.splice(j, 0, ...out.splice(i, 1));
     }
diff --git a/packages/retroplug/src/trackerRegistry.ts b/packages/retroplug/src/trackerRegistry.ts
index 000784b9a..49e0ae5e7 100644
--- a/packages/retroplug/src/trackerRegistry.ts
+++ b/packages/retroplug/src/trackerRegistry.ts
@@ -10,18 +10,28 @@ import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
 import { TRACKER_INTEGRATIONS } from "./generated/packages";
 import { placedKinds } from "./graph/protocolNodes";
 
-/** Every tracker integration, in the product order: a system takes the first one whose markerRole its graph places. A
- *  marker has no protocol node, so it is placed whenever it is stored: a system storing two (a NES project stored with
- *  risa whose ROM is now BlipToaster) takes the first in this order. The generated list, re-exported below as a live
- *  binding. */
+/** Every tracker integration, in the product order. A system takes the first whose markerRole its graph places, in
+ *  the order its OWN roles name them (resolveTracker): of two markers a stored list carries, the load moves the one
+ *  today's ROM earns ahead of the stale one (SystemsStore.suggestedFirst), so this order decides only when the ROM
+ *  earns neither. A marker has no protocol node, so it is placed whenever it is stored. The generated list,
+ *  re-exported below as a live binding. */
 export { TRACKER_INTEGRATIONS };
 
-/** The tracker integration for a system, resolved from its roles: the integration whose markerRole the graph places
- *  (graph/protocolNodes.ts placedKinds). undefined for a non-tracker system, and for one whose stored marker the graph
- *  leaves out: an mGB cart still carrying lsdj-sync after a ROM swap is not an LSDj cart. */
+/** The role kinds a cart's tracker is marked by: one per integration, refused twice over by the generated list. */
+export const TRACKER_MARKERS: ReadonlySet<string> = new Set(TRACKER_INTEGRATIONS.map((t) => t.markerRole));
+
+/** The tracker integration for a system, resolved from its roles: the first of its PLACED markers in role order, as
+ *  graph/protocolNodes.ts placedKinds lists them. undefined for a non-tracker system, and for one whose stored marker
+ *  the graph leaves out: an mGB cart still carrying lsdj-sync after a ROM swap is not an LSDj cart. Role order rather
+ *  than product order, because a stored system can hold two markers (a NES project stored with risa whose path now
+ *  holds BlipToaster) and only the load knows which one the file earns. */
 export function resolveTracker(roles: readonly RoleInstance[]): TrackerIntegration | undefined {
   const placed = placedKinds(roles);
-  return TRACKER_INTEGRATIONS.find((t) => placed.includes(t.markerRole));
+  for (const kind of placed) {
+    const integration = TRACKER_INTEGRATIONS.find((t) => t.markerRole === kind);
+    if (integration) return integration;
+  }
+  return undefined;
 }
 
 /** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
diff --git a/packages/retroplug/test/packages/fixtures/expected/packages.ts b/packages/retroplug/test/packages/fixtures/expected/packages.ts
index 7fa19d684..38e867528 100644
--- a/packages/retroplug/test/packages/fixtures/expected/packages.ts
+++ b/packages/retroplug/test/packages/fixtures/expected/packages.ts
@@ -62,8 +62,8 @@ export const ROM_PROVIDERS: readonly RomProvider[] = [
   ...(app_retroplug.romProviders ?? []),
 ];
 
-/** The tracker integrations, in product order: a system takes the first whose markerRole its graph places. A marker
- *  has no protocol node, so a system storing two takes the first in this order; a duplicate is refused below. */
+/** The tracker integrations, in product order. A system's tracker is the first of its marker roles that the system's
+ *  own role order places (resolveTracker), which a load orders by the ROM's own marker; a duplicate is refused below. */
 export const TRACKER_INTEGRATIONS: readonly TrackerIntegration[] = [
   ...(app_alpha.trackerIntegrations ?? []),
   ...(app_beta.trackerIntegrations ?? []),
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs systems/store-graph
```

Expected: PASS, then the generator's own suite, whose expected copy the step above updated (and which regenerates
`src/generated/packages.ts`, so the new comment reaches the tree that build uses):

```
ok 56 - DirectGraph puts the structure back when its document is refused
# 56 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

```
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs packages/packages-generator
```

Expected: PASS.

```
ok 134 - a package contributing an app and no nodes names no module and still registers its app
# 134 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test
```

Expected: PASS, the whole TypeScript suite with the two new cases in it (tasks 2-5 add cases to files that already
exist, so the file count stands at 243 through the batch):

```
# 243 test file(s) passed (jobs=16)
#   1 case(s) SKIPPED in 1 file(s): lsdj/ui-views(1)
```

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
timeout -k 10 600 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
```

Expected: PASS both (`typecheck: Done`; `check-boundaries: the packages hold their boundaries (0 allowed by
EXCEPTIONS, 0 warning(s))`). The shell imports its own `src/trackerRegistry.ts`, which the boundary rule allows; the
reverse (a package importing the shell) would not be.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/retroplug/src/systemsStore.ts \
  packages/retroplug/src/trackerRegistry.ts packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/test/packages/fixtures/expected/packages.ts \
  packages/retroplug/test/systems/store-graph.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb4): a system's tracker is the marker its ROM earns

The risa and bliptoaster marker roles have no protocol node, so both count as
placed whenever they are stored, and resolveTracker took the first in product
order: a NES project stored with risa whose path now holds BlipToaster kept
resolving risa. Two markers now collide in suggestedFirst the way two writers of
one system input do, so the load moves the marker today's ROM earns ahead of the
stale one, and resolveTracker reads the first placed marker in the system's own
role order. Nothing is deleted: a ROM swapped back swaps the answer back, and a
ROM that earns neither leaves the stored order standing.
MSG
```

---

## Task 2: two clocks for a launch's quantise

MI.MAP's `beat` and `bar` snap a launch to 24 and 96 host ticks
([lsdjMidiMap.ts](../packages/lsdj-launchpad/ts/lsdjMidiMap.ts) `boundaryTick`), but the cart is clocked at
`24 / tickDivisor` ticks a quarter, so at a divisor of 2 an LSDj phrase lasts two host bars and a `bar` launch can
land mid-phrase. The controller session already knows the divisor (its `tickDivisor` setting, read every block from
the cart's own sync node), and the app never sees it.

**Files** (line numbers are the file at `ccc01889e`):

- Modify: `packages/launchpad/ts/session.ts:30-47` (`ControllerCtx`), `:55-73` (`SessionInput`), `:124-142`
  (the ctx built in the constructor), `:156-190` (`update`).
- Modify: `packages/launchpad/ts/controllerSession.ts:110-112` (the block's `frame`), `:214-228` (the clock and the
  frame's fields).
- Modify: `packages/lsdj-launchpad/ts/lsdjMidiMap.ts:33-41` (the ticks and `Quantise`), `:89-96` (`boundaryTick`),
  `:180-190` (`handleInput`'s cue).
- Modify: `packages/lsdj-launchpad/ts/app.ts:18-27` (`QUANTISE_LABELS`), `:33-40` (the schema's comment).
- Modify: `packages/launchpad/ts/scenario/launchpadCore.ts:130-140`, `:160-163` (the scenario's blocks pass a
  divisor); `packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts:57-78`, `:158-175`.
- Modify (with the implementation): `packages/lsdj-launchpad/ts/test/scenario.test.ts:53-84` (`LEAST_HEAP`'s comment
  and the pinned transcript lines).
- Modify (with the implementation, re-recorded): `packages/retroplug/test/menu/recorded-matrix.json` (the quantise
  cycler's steps in the five cases that show it).
- Test: `packages/launchpad/ts/test/session.test.ts:83-110` (the harness's `run`) and a new case after `:280`;
  `packages/lsdj-launchpad/ts/test/app.test.ts:36-48`; `packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts:54-84`
  (the harness) and a new case before `:253`;
  `packages/retroplug/test/script/nodes/controller-session.test.ts` (a new case before `:487`).

**Interfaces:**

- Consumes: `ControllerSessionSettings.tickDivisor` (already read every block by `controllerSessionNode.process`),
  `QUANTISE_VALUES`' consumers (the menu row and the schema's `enumField` in `app.ts`, the scenario, the tests).
- Produces: `ControllerCtx.tickDivisor: number` and `SessionInput.tickDivisor: number` (`@retroplug/launchpad` and its
  `portable` re-export), `Quantise`'s new `hostBeat` and `hostBar` and `QUANTISE_VALUES`' six values in that order.
  Task 6's spec/13 text describes them; no later code depends on them.

### Steps

- [ ] **Step 1: write the failing tests**

Four files. `session.test.ts` gains a case for the divisor on the context (a change reaches a running session, which is
why it rides `SessionInput` and not the constructor) and its harness passes one. `app.test.ts` pins the row's six
values and labels in cycle order. `lsdjMidiMap.test.ts` gains the boundary case at divisors 1, 2 and 3 for `bar`,
`hostBar`, `beat` and `hostBeat` (its harness takes a divisor). `controller-session.test.ts` gains the same at the node,
through the real MI.MAP mode and the predictor.

````patch
diff --git a/packages/launchpad/ts/test/session.test.ts b/packages/launchpad/ts/test/session.test.ts
index ee307e562..14504775b 100644
--- a/packages/launchpad/ts/test/session.test.ts
+++ b/packages/launchpad/ts/test/session.test.ts
@@ -83,7 +83,7 @@ interface Harness {
   seen: ControllerCtx[];
   sent: number[][];
   connect(): void;
-  run(opts?: { tick?: number; transport?: boolean; ticks?: number; input?: number[][] }): void;
+  run(opts?: { tick?: number; transport?: boolean; ticks?: number; tickDivisor?: number; input?: number[][] }): void;
 }
 
 function harness(app: ControllerApp, playback: PlaybackModel = staticModel): Harness {
@@ -102,6 +102,7 @@ function harness(app: ControllerApp, playback: PlaybackModel = staticModel): Har
       tick: opts.tick ?? 0,
       transport: opts.transport ?? false,
       ticks: opts.ticks ?? 0,
+      tickDivisor: opts.tickDivisor ?? 1,
     })),
   };
 }
@@ -277,6 +278,17 @@ test("config reaches the app unchanged", () => {
     config: { quantise: "beat", follow: false },
   });
   device.write(session.connect());
-  device.write(session.update({ input: [], tick: 0, transport: false, ticks: 0 }));
+  device.write(session.update({ input: [], tick: 0, transport: false, ticks: 0, tickDivisor: 1 }));
   expect(seen).toEqual({ quantise: "beat", follow: false });
 });
+
+test("an app reads the cart's tick divisor off its context, and a change reaches a running session", () => {
+  // The divisor is the cart's own clock division (ControllerSessionSettings.tickDivisor): the node reads it every block
+  // and it is deliberately not part of a session's shape, so an app that quantises a launch to the CART's bar needs it
+  // on each update rather than once at construction.
+  const h = harness(noop);
+  h.connect();
+  h.run({ tickDivisor: 2 });
+  h.run({ tickDivisor: 3 });
+  expect(h.seen.map((c) => c.tickDivisor)).toEqual([2, 3]);
+});
diff --git a/packages/lsdj-launchpad/ts/test/app.test.ts b/packages/lsdj-launchpad/ts/test/app.test.ts
index 8039c9d22..fd1727a72 100644
--- a/packages/lsdj-launchpad/ts/test/app.test.ts
+++ b/packages/lsdj-launchpad/ts/test/app.test.ts
@@ -38,6 +38,16 @@ test("every quantise mode the menu offers is one the schema accepts", () => {
   for (const quantise of QUANTISE_VALUES) expect(miMap.config.parse({ quantise }).quantise).toBe(quantise);
 });
 
+test("the quantise row offers both clocks, in the order the setting cycles them", () => {
+  // A bar counts the CART's clock, whose bar at a tempo divisor above 1 is that many DAW bars, so the DAW's own beat and
+  // bar are offered too rather than folded into one meaning.
+  const row = miMap.menu!.find((m) => m.key === "quantise")!;
+  expect(row.choices!.map((c) => c.value)).toEqual(["immediate", "beat", "bar", "hostBeat", "hostBar", "rowEnd"]);
+  expect(row.choices!.map((c) => c.label)).toEqual([
+    "Immediately", "Next beat", "Next bar", "Next host beat", "Next host bar", "End of row",
+  ]);
+});
+
 test("the two halves are joined by id", () => {
   // The generated script bundle registers the runtime and the app's registry holds this half: a document names one id,
   // so the two must name the same app.
diff --git a/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts b/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
index fd04da3f8..dfb797d7a 100644
--- a/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
+++ b/packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts
@@ -57,10 +57,10 @@ class ScriptedModel implements PlaybackModel {
 interface Harness {
   device: FakeLaunchpad;
   sent: number[][];
-  run(opts?: { tick?: number; transport?: boolean; ticks?: number; input?: number[][] }): void;
+  run(opts?: { tick?: number; transport?: boolean; ticks?: number; tickDivisor?: number; input?: number[][] }): void;
 }
 
-function harness(playback: PlaybackModel, config: Record<string, unknown> = {}): Harness {
+function harness(playback: PlaybackModel, config: Record<string, unknown> = {}, tickDivisor = 1): Harness {
   const device = new FakeLaunchpad();
   const sent: number[][] = [];
   const session = new ControllerSession(lsdjMidiMap, {
@@ -76,6 +76,7 @@ function harness(playback: PlaybackModel, config: Record<string, unknown> = {}):
       tick: opts.tick ?? 0,
       transport: opts.transport ?? true,
       ticks: opts.ticks ?? 0,
+      tickDivisor: opts.tickDivisor ?? tickDivisor,
     })),
   };
 }
@@ -217,6 +218,32 @@ test("quantise immediate fires in the same update as the press", () => {
   expect(h.sent).toEqual([[0x90, 5, 100]]);
 });
 
+test("at a tempo divisor of d, a bar launch waits out the CART's bar and a hostBar the DAW's", () => {
+  // MI.MAP clocks the cart at 24 / tickDivisor ticks a quarter, so a cart bar is 96 x d DAW ticks and a cart beat
+  // 24 x d: counting the DAW's 96 at divisor 2 makes a `bar` launch land mid-phrase. The cart's phase is its own
+  // clock, walked from ppq 0. `hostBar` and `hostBeat` keep counting the DAW's, which is all a project at the
+  // default divisor 1 has ever had.
+  const firesAt = (quantise: string, tickDivisor: number): number => {
+    const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
+    const h = harness(model, { quantise, follow: false }, tickDivisor);
+    model.setRows([0, 0, 0, 0]); // something IS playing, so the cue has a boundary to wait for
+    h.run({ tick: 50 });
+    h.run({ tick: 51, input: [h.device.press(0, 5)] });
+    for (let tick = 52; tick <= 51 + 96 * 4; tick++) {
+      h.run({ tick });
+      if (h.sent.length) return tick;
+    }
+    return -1;
+  };
+  expect(firesAt("bar", 1), "bar at 1").toBe(96);
+  expect(firesAt("bar", 2), "bar at 2").toBe(192);
+  expect(firesAt("bar", 3), "bar at 3").toBe(288);
+  expect(firesAt("hostBar", 2), "hostBar at 2").toBe(96);
+  expect(firesAt("hostBar", 3), "hostBar at 3").toBe(96);
+  expect(firesAt("beat", 2), "beat at 2").toBe(96);
+  expect(firesAt("hostBeat", 2), "hostBeat at 2").toBe(72);
+});
+
 test("quantise rowEnd waits for the song to change row, not for a clock boundary", () => {
   const model = new ScriptedModel(new PredictedLsdjModel(fullSong(16)));
   const h = harness(model, { quantise: "rowEnd" });
diff --git a/packages/retroplug/test/script/nodes/controller-session.test.ts b/packages/retroplug/test/script/nodes/controller-session.test.ts
index 405a3bfa8..d5fa304c2 100644
--- a/packages/retroplug/test/script/nodes/controller-session.test.ts
+++ b/packages/retroplug/test/script/nodes/controller-session.test.ts
@@ -489,6 +489,25 @@ test("an app knob changes behaviour on the next block", () => {
   expect(secondLaunchFires({ quantise: "immediate" })).toBe(true);
 });
 
+test("a bar launch counts the CART's bar: the divisor reaches the app on its context", () => {
+  // The cart clocks at 24 / tickDivisor ticks a quarter, so at divisor 2 its bar is 192 host ticks while the host's
+  // stays 96: a `bar` launch cued at host tick 48 must wait for the cart's, and `hostBar` for the host's. The divisor
+  // rides the controller context (ControllerCtx.tickDivisor), read every block, so no rebuild is involved.
+  const firesAt = (quantise: string, tickDivisor: number): number => {
+    const w = new Wired({ appConfig: parsed({ quantise }), tickDivisor });
+    w.run({ ppq: 0, surface: [PRESS_ROW_0] }); // nothing was playing: launched at once, and now it is
+    w.run({ ppq: 2, surface: [press(9)] }); // host tick 48, not on either bar line
+    for (const tick of [96, 192]) {
+      if (at(w.run({ ppq: tick / 24 }).controller, "launches").length) return tick;
+    }
+    return -1;
+  };
+  expect(firesAt("bar", 1)).toBe(96);
+  expect(firesAt("bar", 2)).toBe(192);
+  expect(firesAt("hostBar", 2)).toBe(96);
+  expect(firesAt("hostBar", 3)).toBe(96);
+});
+
 test("a rebuilt session re-takes the device rather than diffing against a surface it never painted", () => {
   const rig = quiet();
   const first = surface(rig.run(still({ connected: true })));
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs launchpad/session
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs lsdj-launchpad/app
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs lsdj-launchpad/lsdjMidiMap
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs script/nodes/controller-session
```

Expected: FAIL in each file, for the reason the task fixes: the context carries no divisor, the row has four values,
and a `bar` at divisor 2 fires at the host's bar (96) instead of the cart's (192).

```
# launchpad/session
not ok 15 - an app reads the cart's tick divisor off its context, and a change reaches a running session
  ---
  expected [2,3], got [null,null]
# 14 passed, 0 skipped, 1 failed

# lsdj-launchpad/app
not ok 6 - the quantise row offers both clocks, in the order the setting cycles them
  expected ["immediate","beat","bar","hostBeat","hostBar","rowEnd"], got ["immediate","beat","bar","rowEnd"]
# 6 passed, 0 skipped, 1 failed

# lsdj-launchpad/lsdjMidiMap
not ok 12 - at a tempo divisor of d, a bar launch waits out the CART's bar and a hostBar the DAW's
  bar at 2: expected 192, got 96
# 21 passed, 0 skipped, 1 failed

# script/nodes/controller-session
not ok 28 - a bar launch counts the CART's bar: the divisor reaches the app on its context
  expected 192, got 96
# 31 passed, 0 skipped, 1 failed
```

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/launchpad/ts/controllerSession.ts b/packages/launchpad/ts/controllerSession.ts
index ddd3da015..f9219a4ec 100644
--- a/packages/launchpad/ts/controllerSession.ts
+++ b/packages/launchpad/ts/controllerSession.ts
@@ -109,7 +109,7 @@ export const controllerSessionNode = defineNode<ControllerSessionSettings>({
     // retained, not dangling; it is read only inside update, and emptied before the next update reads it.
     const input: ArrayLike<number>[] = [];
     // The session's whole input for a block, one object refilled each block so none is built per block.
-    const frame = { input, tick: 0, transport: false, ticks: 0 };
+    const frame = { input, tick: 0, transport: false, ticks: 0, tickDivisor: 1 };
     // The clock the cart is sent, walked here so the predictor can be given the COUNT of ticks a block fired. It is the
     // walk the cart's own protocol node runs (packages/core/ts/ticks.ts), at its resolution: an app's override sets the
     // cart's sync MODE and never its divisor, and MI.MAP clocks the cart at 24 / tempoDivisor ticks a quarter, so the
@@ -213,7 +213,8 @@ export const controllerSessionNode = defineNode<ControllerSessionSettings>({
         // playhead behind the cart for good. Counting the walk gives the predictor exactly the ticks the cart heard.
         clock.begin(b);
         counted = 0;
-        clock.walk(TICKS_PER_QUARTER / (config.tickDivisor >= 1 ? config.tickDivisor : 1), countTick);
+        const divisor = config.tickDivisor >= 1 ? config.tickDivisor : 1;
+        clock.walk(TICKS_PER_QUARTER / divisor, countTick);
         const elapsed = walked;
         walked = counted;
 
@@ -223,6 +224,9 @@ export const controllerSessionNode = defineNode<ControllerSessionSettings>({
         frame.tick = Math.floor(b.transport.ppq * TICKS_PER_QUARTER);
         frame.transport = b.transport.playing;
         frame.ticks = elapsed;
+        // What the cart divides its clock by, on the app's context: a launch quantised to the cart's bar counts in
+        // cart bars, which at a divisor above 1 are several of the host's.
+        frame.tickDivisor = divisor;
         const out = session.update(frame);
         for (let i = 0; i < out.length; i++) b.send("controllerOut", 0, out[i]);
       },
diff --git a/packages/launchpad/ts/scenario/launchpadCore.ts b/packages/launchpad/ts/scenario/launchpadCore.ts
index 7e930f5ac..d531f759a 100644
--- a/packages/launchpad/ts/scenario/launchpadCore.ts
+++ b/packages/launchpad/ts/scenario/launchpadCore.ts
@@ -133,6 +133,8 @@ interface Block {
   transport: boolean;
   /** The ticks the host clocked in the previous block, as walkTicks counted them. */
   ticks: number;
+  /** The cart's clock division this block (ControllerCtx.tickDivisor): no cart here, so 1. */
+  tickDivisor?: number;
   /** The link reports a NEW device before this block, as a pair swapped inside one UI frame does: the host takes it
    *  again, which invalidates the surface so the block repaints everything. */
   reconnect?: boolean;
@@ -160,7 +162,7 @@ blocks.push({ input: [], tick: 480, transport: false, ticks: 0 });
 for (let i = 0; i < blocks.length; i++) {
   launches.length = 0;
   if (blocks[i].reconnect) emit("reconnect " + i, session.connect());
-  const leds = session.update(blocks[i]);
+  const leds = session.update({ ...blocks[i], tickDivisor: blocks[i].tickDivisor ?? 1 });
   if (leds.length > 0 || launches.length > 0) emit("block " + i, { leds, launches: launches.slice() });
 }
 emit("ticks", model.advancedTicks());
diff --git a/packages/launchpad/ts/session.ts b/packages/launchpad/ts/session.ts
index 0c5fbb3d5..017894028 100644
--- a/packages/launchpad/ts/session.ts
+++ b/packages/launchpad/ts/session.ts
@@ -40,6 +40,9 @@ export interface ControllerCtx {
   readonly tick: number;
   /** Ticks elapsed since the previous update; 0 while stopped. */
   readonly ticks: number;
+  /** What the cart the launches drive divides its 24-tick clock by (1 when unknown): a cart bar is 96 times this in the
+   *  host's ticks, which is the unit an app that quantises to the CART's clock counts in. */
+  readonly tickDivisor: number;
   readonly transport: boolean;
   readonly config: Record<string, unknown>;
   /** Persistent scratch, scoped to this session: the contract a DSP role's `ctx.state` had under the kernel. */
@@ -70,6 +73,9 @@ export interface SessionInput {
   transport: boolean;
   /** Ticks the host clocked in the previous block; 0 while it was stopped, and 0 on the first update. */
   ticks: number;
+  /** The cart's clock division this block (ControllerCtx.tickDivisor). It is per block rather than per session: a
+   *  divisor changed while a song plays must reach the running session at its next block, and must not rebuild it. */
+  tickDivisor: number;
 }
 
 export interface SessionOptions {
@@ -135,6 +141,7 @@ export class ControllerSession {
       target: this.target,
       tick: 0,
       ticks: 0,
+      tickDivisor: 1,
       transport: false,
       config: this.config,
       state: this.state,
@@ -176,6 +183,7 @@ export class ControllerSession {
     const ctx = this.ctx;
     ctx.tick = input.tick;
     ctx.ticks = ticks;
+    ctx.tickDivisor = input.tickDivisor;
     ctx.transport = input.transport;
     this.app(ctx);
 
diff --git a/packages/lsdj-launchpad/ts/app.ts b/packages/lsdj-launchpad/ts/app.ts
index 44b66293b..6211a2984 100644
--- a/packages/lsdj-launchpad/ts/app.ts
+++ b/packages/lsdj-launchpad/ts/app.ts
@@ -17,11 +17,15 @@ import { MAX_PAGE, QUANTISE_VALUES, type Quantise } from "./lsdjMidiMap";
 // nothing of it loads at run time.
 import type {} from "@retroplug/launchpad/packageContributions";
 
-/** The row's text per quantise value: the words a player reads, which only this app has. */
+/** The row's text per quantise value: the words a player reads, which only this app has. `beat` and `bar` are the
+ *  CART's clock (one bar is one LSDj phrase, however many host bars the cart's divisor makes it), and the `host` pair
+ *  the DAW's, which a cart at the default divisor 1 hears as the same thing. */
 const QUANTISE_LABELS: Record<Quantise, string> = {
   immediate: "Immediately",
   beat: "Next beat",
   bar: "Next bar",
+  hostBeat: "Next host beat",
+  hostBar: "Next host bar",
   rowEnd: "End of row",
 };
 
@@ -29,9 +33,9 @@ const QUANTISE_LABELS: Record<Quantise, string> = {
 const MI_MAP: ControllerAppContribution = {
   id: "lsdj-midimap",
   label: "LSDj MI.MAP",
-  // quantise: when a pressed pad's launch actually fires. Default `bar` - one bar is also exactly one LSDj phrase
-  // at the factory groove, so launches land where the music does. follow: scroll the 16-row window to keep the
-  // playhead visible. page: which window to start on.
+  // quantise: when a pressed pad's launch actually fires. Default `bar` - one cart bar is also exactly one LSDj phrase
+  // at the factory groove, so launches land where the music does, at whatever clock divisor the cart runs. follow:
+  // scroll the 16-row window to keep the playhead visible. page: which window to start on.
   config: z.object({
     quantise: enumField(QUANTISE_VALUES, "bar"),
     follow: boolField(true),
diff --git a/packages/lsdj-launchpad/ts/lsdjMidiMap.ts b/packages/lsdj-launchpad/ts/lsdjMidiMap.ts
index be65e967f..b8ed2350e 100644
--- a/packages/lsdj-launchpad/ts/lsdjMidiMap.ts
+++ b/packages/lsdj-launchpad/ts/lsdjMidiMap.ts
@@ -31,14 +31,16 @@ export const WINDOW_ROWS = PANE_ROWS * 2;
 /** Song rows addressable by the page control. 256 rows / 16 per page. */
 export const MAX_PAGE = 15;
 
-/** Ticks per quantise unit at 24 PPQN. `bar` assumes 4/4 - the block info carries no time signature, and
- *  96 ticks is also exactly one 16-step LSDj phrase at the factory groove (B2), so it is the right unit
- *  for this tracker whatever the host's meter says. */
+/** Ticks per quantise unit at 24 PPQN, in the HOST's clock. A `bar` assumes 4/4 - the block info carries no time
+ *  signature, and 96 ticks is also exactly one 16-step LSDj phrase at the factory groove (B2), so it is the right unit
+ *  for this tracker whatever the host's meter says. The cart's own beat and bar are these multiplied by its clock
+ *  divisor (ControllerCtx.tickDivisor): it is clocked at 24 / divisor ticks a quarter, so at divisor 2 an LSDj phrase
+ *  lasts two host bars and a `bar` launch that counted the host's would land mid-phrase. */
 const TICKS_PER_BEAT = 24;
 const TICKS_PER_BAR = 96;
 
-export type Quantise = "immediate" | "beat" | "bar" | "rowEnd";
-export const QUANTISE_VALUES = ["immediate", "beat", "bar", "rowEnd"] as const;
+export type Quantise = "immediate" | "beat" | "bar" | "hostBeat" | "hostBar" | "rowEnd";
+export const QUANTISE_VALUES = ["immediate", "beat", "bar", "hostBeat", "hostBar", "rowEnd"] as const;
 
 // Colours. Only the seven palette entries the manual names in prose have known meanings (the full
 // 128-entry table is an image in the PDF), so the app is built from those rather than from invented
@@ -88,9 +90,15 @@ export function followRow(position: PlaybackPosition): number | null {
 const clampPage = (p: number): number => Math.max(0, Math.min(MAX_PAGE, Math.trunc(p) || 0));
 
 /** The tick a launch pressed at `tick` should fire on. Snaps UP to the next boundary, so a press landing
- *  exactly on one fires immediately rather than waiting a whole extra unit. */
-function boundaryTick(tick: number, quantise: Quantise): number {
-  const unit = quantise === "bar" ? TICKS_PER_BAR : TICKS_PER_BEAT;
+ *  exactly on one fires immediately rather than waiting a whole extra unit. `beat` and `bar` count the CART's
+ *  clock, whose phase is its own, walked from ppq 0 (so a host seek does not move it): one cart beat is
+ *  `TICKS_PER_BEAT` host ticks times the divisor it divides its clock by. `hostBeat` and `hostBar` count the host's,
+ *  whatever the cart does. */
+function boundaryTick(tick: number, quantise: Quantise, tickDivisor: number): number {
+  const bar = quantise === "bar" || quantise === "hostBar";
+  const cart = quantise === "beat" || quantise === "bar";
+  const divisor = cart && tickDivisor > 0 ? tickDivisor : 1;
+  const unit = (bar ? TICKS_PER_BAR : TICKS_PER_BEAT) * divisor;
   return Math.ceil(tick / unit) * unit;
 }
 
@@ -180,7 +188,7 @@ function handleInput(
     }
 
     st.pending = row;
-    st.pendingTick = quantise === "rowEnd" ? null : boundaryTick(c.tick, quantise);
+    st.pendingTick = quantise === "rowEnd" ? null : boundaryTick(c.tick, quantise, c.tickDivisor);
     st.pendingFromRow = quantise === "rowEnd" ? current : null;
   }
 }
diff --git a/packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts b/packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts
index c42b3b385..e2262444b 100644
--- a/packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts
+++ b/packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts
@@ -54,10 +54,12 @@ interface Block {
   transport: boolean;
   /** The ticks the host clocked in the previous block, as walkTicks counted them. */
   ticks: number;
+  /** The cart's clock division this block (ControllerCtx.tickDivisor): one cart, at the default divisor. */
+  tickDivisor: number;
 }
 const blocks: Block[] = [
-  { input: [], tick: 0, transport: false, ticks: 0 },
-  { input: [pad(0, 2, 127), pad(0, 2, 0)], tick: 0, transport: false, ticks: 0 }, // stopped: launches at once
+  { input: [], tick: 0, transport: false, ticks: 0, tickDivisor: 1 },
+  { input: [pad(0, 2, 127), pad(0, 2, 0)], tick: 0, transport: false, ticks: 0, tickDivisor: 1 }, // launches at once
 ];
 for (let tick = 0; tick <= 480; tick += 12) {
   const input: number[][] = [];
@@ -65,10 +67,10 @@ for (let tick = 0; tick <= 480; tick += 12) {
   if (tick === 300) input.push(cc(PRO_MK3.buttons.up, 127), cc(PRO_MK3.buttons.up, 0));
   if (tick === 312) input.push(cc(PRO_MK3.buttons.down, 127));
   if (tick === 396) input.push(pad(6, 1, 90)); // row 9, on the right-hand pane
-  blocks.push({ input, tick, transport: true, ticks: 12 });
+  blocks.push({ input, tick, transport: true, ticks: 12, tickDivisor: 1 });
 }
-blocks.push({ input: [], tick: 4000, transport: true, ticks: 0 }); // a seek: the cart was sent no clocks
-blocks.push({ input: [], tick: 480, transport: false, ticks: 0 });
+blocks.push({ input: [], tick: 4000, transport: true, ticks: 0, tickDivisor: 1 }); // a seek: the cart was sent no clocks
+blocks.push({ input: [], tick: 480, transport: false, ticks: 0, tickDivisor: 1 });
 
 for (let i = 0; i < blocks.length; i++) {
   launches.length = 0;
@@ -162,11 +164,11 @@ for (const quantise of QUANTISE_VALUES) {
     if (tick === 60) input.push(cc(PRO_MK3.buttons.session, 127)); // session button: follow on
     if (tick === 90) input.push(cc(PRO_MK3.buttons.down, 127), cc(PRO_MK3.buttons.down, 127));
     if (tick === 120) input.push(pad(7, 7, 127)); // the window's last row
-    leds += s.update({ input, tick, transport: tick > 0, ticks: tick > 0 ? 6 : 0 }).length;
+    leds += s.update({ input, tick, transport: tick > 0, ticks: tick > 0 ? 6 : 0, tickDivisor: 1 }).length;
   }
   emit("quantise " + quantise, { launches: sent, ledMessages: leds });
 }
 const detached = new ControllerSession(lsdjMidiMap, {
   playback: runtime.playbackModel(table), target: nullTarget(LSDJ_MAX_ROW),
 });
-emit("null target session", detached.update({ input: [pad(0, 0, 127)], tick: 0, transport: false, ticks: 0 }).length);
+emit("null target session", detached.update({ input: [pad(0, 0, 127)], tick: 0, transport: false, ticks: 0, tickDivisor: 1 }).length);
diff --git a/packages/lsdj-launchpad/ts/test/scenario.test.ts b/packages/lsdj-launchpad/ts/test/scenario.test.ts
index 3ee6c3c8f..f180ce78b 100644
--- a/packages/lsdj-launchpad/ts/test/scenario.test.ts
+++ b/packages/lsdj-launchpad/ts/test/scenario.test.ts
@@ -49,8 +49,9 @@ const SCENARIO = "../../packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts";
 const PACK_DIR = "../../packages/lsdj-launchpad";
 const OUT = ".test-build/lsdj-launchpad-scenario";
 // The bridge's own heap, measured the way the launchpad core's is, by bisecting --heap-bytes on this tree: the run
-// passes at 144960 bytes and fails at 144896 (the four quantise sessions' tables are live at once), and the budget is
-// 1.25x that. The scenario's figure, NOT the box's 64 KiB arena, which no controller pack claims.
+// passed at 144960 bytes and failed at 144896 with the quantise row's then four modes (four sessions' tables live at
+// once), and the budget is 1.25x that. Six modes fit inside it, which the run below proves. The scenario's figure, NOT
+// the box's 64 KiB arena, which no controller pack claims.
 const LEAST_HEAP = 144960;
 const HEAP = Math.ceil((LEAST_HEAP * 1.25) / 1024) * 1024; // 184320
 
@@ -84,7 +85,7 @@ test("the LSDj bridge runs on MicroQuickJS as on Node, byte for byte", async ()
     "position {", // where the predictor ended, after the bars, the seek and the stop
     "grid [256,4,true,false,false]",
     "layout [0,8,7,31,0,3,0]",
-    "follow [3,null,15,4]",
+    "follow [3,null,15,6]",
     'model playing {"playing":true,"channels":[{"playing":true,"songRow":2}', // a launch, then 150 ticks
     "model edited {", // a row losing its chain, a chain shortened under the ticks already played
     'model anchored {"playing":true,"channels":[{"playing":true,"songRow":3}',
@@ -96,7 +97,9 @@ test("the LSDj bridge runs on MicroQuickJS as on Node, byte for byte", async ()
     "quantise immediate {",
     "quantise beat {",
     "quantise bar {",
-    "quantise rowEnd {", // the four modes: the launches land the same rows, the LED traffic counts differ
+    "quantise hostBeat {",
+    "quantise hostBar {",
+    "quantise rowEnd {", // the six modes: the launches land the same rows, the LED traffic counts differ
     "null target session 1",
   ])
     expect(transcript.includes(part), part).toBe(true);
diff --git a/packages/retroplug/test/menu/recorded-matrix.json b/packages/retroplug/test/menu/recorded-matrix.json
index 96abd0923..2c22638aa 100644
--- a/packages/retroplug/test/menu/recorded-matrix.json
+++ b/packages/retroplug/test/menu/recorded-matrix.json
@@ -12521,6 +12521,20 @@
    "lp-app-quantise": {
     "default": "Launches: Next bar",
     "steps": [
+     {
+      "label": "Launches: Next host beat",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBeat"
+      },
+      "staged": []
+     },
+     {
+      "label": "Launches: Next host bar",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBar"
+      },
+      "staged": []
+     },
      {
       "label": "Launches: End of row",
       "stored": {
@@ -14582,6 +14596,20 @@
    "lp-app-quantise": {
     "default": "Launches: Next bar",
     "steps": [
+     {
+      "label": "Launches: Next host beat",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBeat"
+      },
+      "staged": []
+     },
+     {
+      "label": "Launches: Next host bar",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBar"
+      },
+      "staged": []
+     },
      {
       "label": "Launches: End of row",
       "stored": {
@@ -49600,6 +49628,20 @@
    "lp-app-quantise": {
     "default": "Launches: Next bar",
     "steps": [
+     {
+      "label": "Launches: Next host beat",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBeat"
+      },
+      "staged": []
+     },
+     {
+      "label": "Launches: Next host bar",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBar"
+      },
+      "staged": []
+     },
      {
       "label": "Launches: End of row",
       "stored": {
@@ -52496,6 +52538,20 @@
    "lp-app-quantise": {
     "default": "Launches: Next bar",
     "steps": [
+     {
+      "label": "Launches: Next host beat",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBeat"
+      },
+      "staged": []
+     },
+     {
+      "label": "Launches: Next host bar",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBar"
+      },
+      "staged": []
+     },
      {
       "label": "Launches: End of row",
       "stored": {
@@ -54360,6 +54416,20 @@
    "lp-app-quantise": {
     "default": "Launches: Next bar",
     "steps": [
+     {
+      "label": "Launches: Next host beat",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBeat"
+      },
+      "staged": []
+     },
+     {
+      "label": "Launches: Next host bar",
+      "stored": {
+       "project.controller.appConfig.quantise": "hostBar"
+      },
+      "staged": []
+     },
      {
       "label": "Launches: End of row",
       "stored": {
````

The scenario walks every value of `QUANTISE_VALUES`, six now, so its own test pins the new transcript line
(`follow [3,null,15,6]`, the six-mode count) and the two modes' sections. Its `LEAST_HEAP` comment keeps the bisect it
was measured with (four sessions, 144960 bytes and 144896 the failing side) and says what the six-mode run below proves:
they fit inside 1.25x of it. The recorded menu matrix is re-recorded one named case at a time for the five cases whose
quantise row moved:

```
RP_MENU_MATRIX_UPDATE=nes-launchpad,launchpad-cart-sync,hand-controller,standalone-instance \
  timeout -k 10 900 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs menu/recorded-trees
RP_MENU_MATRIX_UPDATE=standalone-scan \
  timeout -k 10 900 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs menu/recorded-trees
```

Each recorder run rewrites only the cases it names and then reports its own cases against the matrix it loaded before
writing, so run the file once more afterwards to see it settle (the step's Expected below includes that run).

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs launchpad/session
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs lsdj-launchpad/app
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs lsdj-launchpad/lsdjMidiMap
timeout -k 10 300 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs script/nodes/controller-session
timeout -k 10 900 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs lsdj-launchpad/scenario
timeout -k 10 900 .superpowers/kb4plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs menu/recorded-trees
```

Expected: PASS in all six:

```
# 15 passed, 0 skipped, 0 failed
# 7 passed, 0 skipped, 0 failed
# 22 passed, 0 skipped, 0 failed
# 32 passed, 0 skipped, 0 failed
ok 1 - the LSDj bridge runs on MicroQuickJS as on Node, byte for byte
# 105 passed, 0 skipped, 0 failed
```

- [ ] **Step 5: the wider suites this task reaches**

The portable core's proof (the brief's `pnpm check:portable` no longer exists: the device-pack toolchain replaced it,
and `lsdj-launchpad/scenario` is that proof for this package - it bundles the package's whole portable core to ES5 and
scans it for library calls MicroQuickJS lacks, then runs it on `retroplug-mqjs-run` and compares with Node byte for
byte). It is in the step above and passed.

```
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
timeout -k 10 600 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:boundaries
```

Expected: PASS (`# 243 test file(s) passed (jobs=16)`, one skip in `lsdj/ui-views`; `typecheck: Done`;
`check-boundaries: ... 0 warning(s)`).

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/launchpad/ts/session.ts \
  packages/launchpad/ts/controllerSession.ts packages/launchpad/ts/scenario/launchpadCore.ts \
  packages/launchpad/ts/test/session.test.ts packages/lsdj-launchpad/ts/lsdjMidiMap.ts \
  packages/lsdj-launchpad/ts/app.ts packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts \
  packages/lsdj-launchpad/ts/test/lsdjMidiMap.test.ts packages/lsdj-launchpad/ts/test/app.test.ts \
  packages/lsdj-launchpad/ts/test/scenario.test.ts \
  packages/retroplug/test/script/nodes/controller-session.test.ts \
  packages/retroplug/test/menu/recorded-matrix.json
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb4): a bar or beat launch counts the cart's clock, and hostBar/hostBeat the DAW's

MI.MAP snapped beat and bar to 24 and 96 host ticks, but the cart is clocked at
24 / tickDivisor ticks a quarter, so at divisor 2 an LSDj phrase lasts two host
bars and a bar launch could land mid-phrase. beat and bar now count the cart's
clock (24 and 96 host ticks times its divisor, phased from ppq 0), and the new
hostBeat and hostBar keep counting the DAW's; a project at the default divisor 1
behaves exactly as it did. The divisor reaches the app on its controller context,
per block, so a change while a song plays needs no rebuild.
MSG
```

---
## Task 3: the JACK handoff stays on the control thread

DPF's JACK host runs `deactivate()`, the change and `activate()` from JACK's own notification thread for a buffer-size
or sample-rate change (`DistrhoPluginJACK.cpp:352-360` -> `DistrhoPluginInternal.hpp:1215-1253`), and VST2 runs the
same sequence from whichever thread its host dispatches the opcodes on. `PluginDSP` did three things there that are
only safe on the control thread: it flipped the engine's ownership and drained the command and release rings (which
the editor's `uiIdle` reclaims every frame on the control thread), it ran the script host's GC and free, and it ran
`updateLatency` and `updateParameterMap`, which call into the shared QuickJS context the control thread pumps. Nothing
can grow the plan or change the rate while the audio thread owns the engine (`GraphHost::reactivate` refuses,
`GraphHost.cpp:366`), so every size or rate change needs quiescence.

**Files** (line numbers are the file at `ccc01889e`):

- Create: `packages/native/plugin/LifecycleHandoff.hpp` (the policy: stage one adds it carrying today's behaviour so
  the case can hold it; stage two replaces the policy with the two-tier one), and
  `packages/native/test/plugin/LifecycleHandoff.test.cpp` (the cases).
- Modify: `packages/native/cmake/tests.cmake:16-26` (the `retroplug-plugin-test` block: the new source, `Threads`, and
  the comment naming the policy).
- Modify: `packages/native/plugin/PluginDSP.cpp:25-27` (the includes), `:80-112` (the class's base and the member),
  `:259-284` (`getState` / `setState`), `:279-306` (the lifecycle comment and the four overrides), `:306-320`
  (`run()`'s parked gate and the held MIDI), `:385-390` (the idle seam), and a private `HandoffHost`
  implementation beside `bootControlPlane`.

**Interfaces:**

- Consumes: `Engine::kDefaultMaxBlockFrames` (8192), `QueuedInvoker::setAudioThreadOwns` / `drainInto` /
  `reclaimReleased`, `Engine::setMaxBlockFrames` / `setSampleRate`, DPF's `getBufferSize`, `MidiEvent::kDataSize`.
- Produces: `rp::plugin::HandoffHost` (the engine work a lifecycle change needs, control thread only; its defaulted
  seams are `parkOverdue` and `midiDropped`), `rp::plugin::HeldMidi` (a fixed MIDI hold), and
  `rp::plugin::LifecycleHandoff` (constructed with the host, the control thread's id and the plan's maximum;
  `deactivate`, `activate(frames)`, `bufferSizeChanged(frames)`, `sampleRateChanged(rate)`, `beginBlock()`,
  `holdMidi`, `replayHeld`, `idleTick()`, `parked()`, `state()`, and `State` = Running | Requested | Parked |
  Suspended). `PluginDSP` is a private `HandoffHost`. No later task consumes any of it.

### Steps

- [ ] **Step 1: write the failing tests, and the unit that carries today's behaviour**

The policy unit comes first, carrying exactly what `PluginDSP`'s overrides do today (each call runs on whichever thread
made it), so the cases can hold the behaviour they are about; the second stage replaces its policy. The cases drive the
lifecycle from a `std::thread`, as DPF's JACK host does, and record the thread of every engine call: a fitting size
must touch nothing, a rate change must park and then be applied on the control thread with the held MIDI replayed, an
over-plan size must grow the plan there, and the engine's ownership across a format's calls is a case each:

- a control-thread deactivate (as CLAP, VST3 and AU run one) leaves the engine the control thread's, and an idle tick
  then hands it to nobody; a foreign activate after it parks until the tick gives it, with the give and the latency
  report on the control thread alone;
- the very first activate on a foreign thread (DPF's `run()` activates when `fIsActive` is false) parks the same way;
- a foreign deactivate nobody follows is taken back after the grace period and KEPT - take and reclaim, no give, no
  resume - and the foreign activate that comes later asks for the give and gets it from the tick;
- a paired foreign deactivate and activate costs nothing at all;
- the idle tick is the control thread's: called from another (a host's `getState`), it applies nothing and leaves the
  park pending;
- a message a parked block could not hold is counted, and said on the control thread;
- the held buffer's own capacity and drop counting.

The stage-one header carries the `midiDropped` seam (and its default) from the start, so the case that needs it
compiles there; the policy only begins to call it in the second stage.

````patch
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -14,14 +14,18 @@
 
 
 # Pure-C++ Catch2 checks for plugin/ mechanisms that need real JSContexts but no LVGL/txiki host —
-# currently the per-context window-hook routing (ContextTargets.hpp) and the last-good project-state
-# cache (LastGoodState.hpp). Links bare quickjs (qjs) + Catch2.
+# currently the per-context window-hook routing (ContextTargets.hpp), the last-good project-state
+# cache (LastGoodState.hpp) and the lifecycle handoff policy (LifecycleHandoff.hpp). Links bare
+# quickjs (qjs) + Catch2; the handoff's own case drives it from a std::thread, as JACK's callback does.
+find_package(Threads REQUIRED)
 retroplug_add_test_binary(
     NAME     retroplug-plugin-test
     SOURCES  test/plugin/ContextTargets.test.cpp
              test/plugin/LastGoodState.test.cpp
+             test/plugin/LifecycleHandoff.test.cpp
     LIBS     qjs                                        # bare quickjs: JS_* + ArrayBuffer / CFunctionData
-    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/plugin         # ContextTargets.hpp
+             Threads::Threads                           # LifecycleHandoff.test.cpp's std::thread
+    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/plugin         # ContextTargets.hpp, LifecycleHandoff.hpp
              ${RP_QUICKJS_INCLUDE})                     # quickjs.h
 
 # Guards the dynamic-parameter diff classifier in the DPF fork (PluginExporter::reinitParameters —
diff --git a/packages/native/plugin/LifecycleHandoff.hpp b/packages/native/plugin/LifecycleHandoff.hpp
new file mode 100644
--- /dev/null
+++ b/packages/native/plugin/LifecycleHandoff.hpp
@@ -0,0 +1,172 @@
+#pragma once
+
+// The plugin lifecycle's handoff policy: what a buffer-size or sample-rate change does when a host runs it on a thread
+// of its own, and how the engine gets the quiescent moment the change needs.
+//
+// DPF's JACK host runs deactivate(), the change and activate() from JACK's notification callback
+// (DistrhoPluginJACK.cpp's jackBufferSizeCallback / jackSampleRateCallback -> DistrhoPluginInternal.hpp's
+// PluginExporter::setBufferSize / setSampleRate), and VST2 runs the same sequence from whichever thread its host
+// dispatches the block-size and sample-rate opcodes on. NONE of the engine work those callbacks used to do is safe
+// there: the ownership flip and the drains race the editor's per-frame reclaim and its edits on the control thread,
+// and updateLatency / updateParameterMap call into the QuickJS context that thread pumps every frame. PipeWire changes
+// its quantum whenever another client asks for a different latency, so this is routine on a PipeWire desktop, not the
+// rare manual case (spec/13-subsystems.md, the plugin lifecycle's thread rule).
+//
+// The policy splits the work by what the change actually needs, and never touches the engine on a foreign thread:
+//
+//  - **A size the plan already holds.** The Engine's plan starts at Engine::kDefaultMaxBlockFrames (8192) and never
+//    shrinks, and nothing on the control plane can grow it while the audio thread owns the engine
+//    (GraphHost::reactivate refuses). So a foreign buffer-size change within that maximum needs nothing at all: the
+//    engine never leaves the audio thread, so there is no ownership flip, no drain, no reclaim and no JS, and the next
+//    run() drains the ring as usual. That is every PipeWire quantum change.
+//  - **A rate change, or a size above the plan.** Nothing can apply it while the audio thread runs the engine, so
+//    run() PARKS: it writes silence, holds its block's MIDI in a fixed buffer (a note-off is not lost), and touches
+//    nothing. The control thread's idle tick - the one hook both hosts call every frame, the editor's
+//    reclaimReleased - then does the whole quiescent handoff: take the engine back, drain, reclaim, apply the rate and
+//    the size, hand it back, report latency and re-label the CC map.
+//
+// The handshake is a release/acquire flag. run() reads the state with acquire and stores Parked (release) only on
+// ENTRY to a later block, so a block that stored Parked has already declined to touch the engine and the tick, which
+// waits for Parked, cannot overlap one that did. It therefore does not rely on JACK's promise that process does not run
+// during the callback. A foreign deactivate with no activate after it (a VST2 SUSPEND off the main thread) parks too,
+// and DPF guarantees no run() after a deactivate, so the tick may take the engine back for that alone.
+//
+// One mutex serialises the foreign lifecycle calls against the tick; run() never takes it. A host that moves its main
+// thread reads as foreign and takes the deferred path, which is still correct, only slower. A host with no idle tick
+// at all (a VST2 host that changes the rate off its main thread with the editor closed) is covered by getState and
+// setState, which ask the policy to apply a pending park; a park nobody applies is reported once after a second.
+//
+// Nothing here touches DPF, so the policy is unit-tested without it (test/plugin/LifecycleHandoff.test.cpp) and
+// PluginDSP only implements HandoffHost over its engine and forwards its four lifecycle overrides.
+
+#include <algorithm>
+#include <atomic>
+#include <chrono>
+#include <cstddef>
+#include <cstdint>
+#include <cstring>
+#include <mutex>
+#include <thread>
+
+namespace rp::plugin {
+
+// The engine work a lifecycle change needs done, and what the policy may assume about the engine's plan. Every method
+// runs on the CONTROL thread, in the tick that applies a park (or in the lifecycle call itself when that call already
+// came from the control thread).
+class HandoffHost {
+public:
+    virtual ~HandoffHost() = default;
+
+    // Take the engine from the audio thread: the ownership flip and the command ring drained into the engine.
+    // Quiescent only (no run() in flight, and none starting).
+    virtual void takeEngine() = 0;
+
+    // Free the plans the audio thread handed back; returns how many. Quiescent or not.
+    virtual std::size_t reclaimReleases() = 0;
+
+    // Hand the engine to the audio thread: from here every control-plane edit is push-only, drained by run().
+    virtual void giveEngine() = 0;
+
+    // Grow the plan so the host's blocks fit. A shrink is the Engine's to ignore, never this policy's to ask for.
+    virtual void setMaxBlockFrames(std::uint32_t frames) = 0;
+
+    // Re-rate the live cores. Quiescent only (Engine::setSampleRate).
+    virtual void setSampleRate(double rate) = 0;
+
+    // Report the project's latency to the host and re-label the CC map: both call into the shared script context, so
+    // control thread only.
+    virtual void updateControlPlane() = 0;
+
+    // A park this host never got round to applying, said once per park. Nothing to say in a unit test's fake.
+    virtual void parkOverdue(double /*seconds*/) {}
+
+    // MIDI a parked block could not hold (LifecycleHandoff's fixed buffer), said on the CONTROL thread once the block
+    // that parked is done with: the audio thread only counted it, and a log line is not its to write.
+    virtual void midiDropped(std::uint64_t /*count*/) {}
+};
+
+// The MIDI a parked block held, so a note-off the host sent while parked still reaches the cart. Fixed capacity,
+// written by the audio thread only, and it never allocates: past the capacity a message is dropped and counted.
+class HeldMidi {
+public:
+    static constexpr int kCapacity = 256;
+    // DPF's inline MIDI data: a message longer than this (a SysEx) is kept in dataExt, and one of those is not held.
+    static constexpr std::uint32_t kMaxBytes = 4;
+
+    struct Event {
+        std::uint8_t size = 0;
+        std::uint8_t data[kMaxBytes] = {};
+    };
+
+    // Hold one message; false when it did not fit or was not a held size (both counted as dropped).
+    bool push(const std::uint8_t* bytes, std::uint32_t size) {
+        if (bytes == nullptr || size == 0 || size > kMaxBytes || count_ >= kCapacity) {
+            ++dropped_;
+            return false;
+        }
+        Event& e = events_[count_++];
+        e.size = static_cast<std::uint8_t>(size);
+        std::memcpy(e.data, bytes, size);
+        return true;
+    }
+
+    int count() const { return count_; }
+    const Event& at(int i) const { return events_[i]; }
+    void clear() { count_ = 0; }
+    std::uint64_t dropped() const { return dropped_; }
+
+private:
+    Event          events_[kCapacity];
+    int            count_   = 0;
+    std::uint64_t  dropped_ = 0;
+};
+
+// The policy. Construct it on the thread DPF builds the plugin on, which is the control thread it also runs the first
+// activate() on.
+class LifecycleHandoff {
+public:
+    enum class State { Running, Requested, Parked };
+
+    LifecycleHandoff(HandoffHost& host, std::thread::id /*controlThread*/, std::uint32_t /*planMaxBlockFrames*/)
+        : host_(host) {}
+
+    void deactivate() {
+        host_.takeEngine();
+        host_.reclaimReleases();
+    }
+
+    void activate(std::uint32_t frames) {
+        host_.setMaxBlockFrames(frames);
+        host_.giveEngine();
+        host_.updateControlPlane();
+    }
+
+    void bufferSizeChanged(std::uint32_t /*frames*/) {}
+
+    void sampleRateChanged(double rate) { host_.setSampleRate(rate); }
+
+    bool beginBlock() { return true; }
+
+    void holdMidi(const std::uint8_t* bytes, std::uint32_t size) { held_.push(bytes, size); }
+
+    const HeldMidi& held() const { return held_; }
+
+    template <typename Push>
+    void replayHeld(Push&& push) {
+        if (held_.count() == 0) return;
+        for (int i = 0; i < held_.count(); ++i) push(held_.at(i).data, held_.at(i).size);
+        held_.clear();
+    }
+
+    std::size_t idleTick() { return host_.reclaimReleases(); }
+
+    bool parked() const { return false; }
+
+    State state() const { return State::Running; }
+
+private:
+    HandoffHost& host_;
+    HeldMidi     held_;
+};
+
+} // namespace rp::plugin
diff --git a/packages/native/test/plugin/LifecycleHandoff.test.cpp b/packages/native/test/plugin/LifecycleHandoff.test.cpp
new file mode 100644
--- /dev/null
+++ b/packages/native/test/plugin/LifecycleHandoff.test.cpp
@@ -0,0 +1,311 @@
+// The plugin lifecycle's handoff policy (plugin/LifecycleHandoff.hpp): a buffer-size or sample-rate change that a host
+// runs on a thread of its own must not touch the engine there.
+//
+// The format's callbacks are what the policy is driven with - deactivate(), the change, activate() - so every case
+// calls them exactly as DPF's JACK host does: from a std::thread that is not the one the plugin was built on. The fake
+// host records the thread of every engine call, which is the whole assertion: whatever happens must happen on the
+// control thread, or not at all.
+#include <catch2/catch_test_macros.hpp>
+
+#include <chrono>
+#include <cstddef>
+#include <cstdint>
+#include <functional>
+#include <mutex>
+#include <string>
+#include <thread>
+#include <vector>
+
+#include "LifecycleHandoff.hpp"
+
+namespace {
+
+using rp::plugin::HeldMidi;
+using rp::plugin::LifecycleHandoff;
+
+// The engine work the policy may ask for, recorded with the thread it ran on.
+class FakeHost : public rp::plugin::HandoffHost {
+public:
+    struct Call {
+        std::string    what;
+        std::thread::id thread;
+        double         value = 0.0;
+    };
+
+    void takeEngine() override { record("take"); }
+    std::size_t reclaimReleases() override { record("reclaim"); return 0; }
+    void giveEngine() override { record("give"); }
+    void setMaxBlockFrames(std::uint32_t frames) override { record("maxFrames", static_cast<double>(frames)); }
+    void setSampleRate(double rate) override { record("rate", rate); }
+    void updateControlPlane() override { record("control"); }
+    void parkOverdue(double seconds) override { record("overdue", seconds); }
+    void midiDropped(std::uint64_t count) override { record("midiDropped", static_cast<double>(count)); }
+
+    std::vector<Call> calls() {
+        std::lock_guard<std::mutex> guard(mutex_);
+        return calls_;
+    }
+
+    void reset() {
+        std::lock_guard<std::mutex> guard(mutex_);
+        calls_.clear();
+    }
+
+    bool ranOn(std::thread::id thread) {
+        for (const Call& c : calls())
+            if (c.thread == thread) return true;
+        return false;
+    }
+
+    std::vector<std::string> names() {
+        std::vector<std::string> out;
+        for (const Call& c : calls()) out.push_back(c.what);
+        return out;
+    }
+
+private:
+    void record(const char* what, double value = 0.0) {
+        std::lock_guard<std::mutex> guard(mutex_);
+        calls_.push_back({what, std::this_thread::get_id(), value});
+    }
+
+    std::vector<Call> calls_;
+    std::mutex        mutex_;
+};
+
+// Run `body` on a thread of its own, as JACK's notification thread is, and report the id it had.
+std::thread::id onForeignThread(const std::function<void()>& body) {
+    std::thread::id id{};
+    std::thread thread([&] {
+        id = std::this_thread::get_id();
+        body();
+    });
+    thread.join();
+    return id;
+}
+
+} // namespace
+
+TEST_CASE("a foreign buffer-size change within the plan touches nothing at all", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+    host.reset();
+    const std::thread::id foreign = onForeignThread([&] {
+        handoff.deactivate();
+        handoff.bufferSizeChanged(2048);
+        handoff.activate(2048);
+    });
+    REQUIRE(foreign != std::this_thread::get_id());
+    // No ownership flip, no drain, no reclaim, no JS: the engine never left the audio thread, and the next block drains
+    // the ring as usual.
+    REQUIRE(host.calls().empty());
+    REQUIRE_FALSE(handoff.parked());
+    REQUIRE(handoff.beginBlock());
+    REQUIRE(host.calls().empty());
+}
+
+TEST_CASE("a foreign rate change parks the audio thread, and the idle tick applies it there", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+    host.reset();
+    const std::thread::id foreign = onForeignThread([&] { handoff.sampleRateChanged(96000.0); });
+    REQUIRE(foreign != std::this_thread::get_id());
+    REQUIRE_FALSE(host.ranOn(foreign));
+    REQUIRE(host.calls().empty());
+    REQUIRE(handoff.parked());
+
+    // The next block parks: it writes silence and holds its MIDI, and touches neither the engine nor the host.
+    REQUIRE_FALSE(handoff.beginBlock());
+    REQUIRE(handoff.state() == LifecycleHandoff::State::Parked);
+    const std::uint8_t noteOff[3] = {0x80, 0x3C, 0x00};
+    handoff.holdMidi(noteOff, 3);
+    REQUIRE(handoff.held().count() == 1);
+    REQUIRE(host.calls().empty());
+
+    // The control thread's idle does the whole quiescent handoff, and nothing of it ran on JACK's thread.
+    REQUIRE(handoff.idleTick() == 0);
+    const std::vector<std::string> names = host.names();
+    const std::vector<std::string> want = {"take", "reclaim", "rate", "give", "control"};
+    REQUIRE(names == want);
+    REQUIRE_FALSE(host.ranOn(foreign));
+    const std::vector<FakeHost::Call> calls = host.calls();
+    REQUIRE(calls[2].value == 96000.0);
+
+    // The first resumed block runs the engine again, with the held message replayed at frame 0 first.
+    REQUIRE_FALSE(handoff.parked());
+    REQUIRE(handoff.beginBlock());
+    std::vector<std::uint8_t> replayed;
+    handoff.replayHeld([&](const std::uint8_t* bytes, std::uint32_t size) {
+        replayed.insert(replayed.end(), bytes, bytes + size);
+    });
+    const std::vector<std::uint8_t> wantBytes = {0x80, 0x3C, 0x00};
+    REQUIRE(replayed == wantBytes);
+    REQUIRE(handoff.held().count() == 0);
+}
+
+TEST_CASE("a foreign size above the plan parks, and the tick grows the plan for it", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+    host.reset();
+    const std::thread::id foreign = onForeignThread([&] {
+        handoff.deactivate();
+        handoff.bufferSizeChanged(16384);
+        handoff.activate(16384);
+    });
+    REQUIRE(host.calls().empty());
+    REQUIRE(handoff.parked());
+    REQUIRE_FALSE(handoff.beginBlock());
+    REQUIRE(handoff.idleTick() == 0);
+    const std::vector<std::string> names = host.names();
+    const std::vector<std::string> want = {"take", "reclaim", "maxFrames", "give", "control"};
+    REQUIRE(names == want);
+    const std::vector<FakeHost::Call> calls = host.calls();
+    REQUIRE(calls[2].value == 16384.0);
+    REQUIRE_FALSE(host.ranOn(foreign));
+    // The plan now holds a block that long, so the next one is a no-op again.
+    onForeignThread([&] { handoff.bufferSizeChanged(16384); });
+    REQUIRE_FALSE(handoff.parked());
+    REQUIRE(handoff.beginBlock());
+}
+
+TEST_CASE("a control-thread deactivate leaves the engine control-owned, and a foreign activate parks until the tick "
+          "gives it",
+          "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on
+    REQUIRE(host.names() == std::vector<std::string>{"maxFrames", "give", "control"});
+    REQUIRE(handoff.beginBlock());
+    handoff.deactivate();  // and a control-thread deactivate, as CLAP, VST3 and AU do
+    REQUIRE(host.names() == std::vector<std::string>{"maxFrames", "give", "control", "take", "reclaim"});
+    // The engine is the CONTROL thread's again: an idle tick must not hand it to a thread nothing is running on, and
+    // the next block must not drive it either.
+    REQUIRE(handoff.idleTick() == 0);
+    REQUIRE(host.names().size() == 5);
+    REQUIRE_FALSE(handoff.beginBlock());
+
+    // A host that activates on another thread (VST3's setProcessing on the processing thread, DPF's run() auto-activate
+    // when fIsActive is false) gets a park: the give is the control thread's to make, from a quiescent moment.
+    const std::thread::id foreign = onForeignThread([&] { handoff.activate(2048); });
+    REQUIRE_FALSE(host.ranOn(foreign));
+    REQUIRE(handoff.parked());
+    REQUIRE_FALSE(handoff.beginBlock());  // the block parks, so the tick knows no block is in the engine
+    REQUIRE(handoff.idleTick() == 0);
+    const std::vector<std::string> want = {"maxFrames", "give", "control", "take", "reclaim", "give", "control"};
+    REQUIRE(host.names() == want);
+    REQUIRE_FALSE(host.ranOn(foreign));
+    REQUIRE(handoff.beginBlock());
+}
+
+TEST_CASE("the very first activate on a foreign thread parks, and the tick gives it", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    // Nothing has given the engine to an audio thread yet, so this activate must not run the give, the latency report
+    // or the CC map on JACK's thread: it asks for a park, and the tick gives from the control thread.
+    const std::thread::id foreign = onForeignThread([&] { handoff.activate(1024); });
+    REQUIRE_FALSE(host.ranOn(foreign));
+    REQUIRE(host.calls().empty());
+    REQUIRE(handoff.parked());
+    REQUIRE_FALSE(handoff.beginBlock());
+    REQUIRE(handoff.idleTick() == 0);
+    REQUIRE(host.names() == std::vector<std::string>{"give", "control"});
+    REQUIRE_FALSE(host.ranOn(foreign));
+    REQUIRE(handoff.beginBlock());
+}
+
+TEST_CASE("a foreign deactivate nobody follows is taken back and kept; one that pairs costs nothing", "[lifecycle]") {
+    SECTION("deactivate then activate, as VST2's SUSPEND(1) does: nothing moved") {
+        FakeHost host;
+        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+        host.reset();
+        onForeignThread([&] {
+            handoff.deactivate();
+            handoff.activate(1024);
+        });
+        REQUIRE(host.calls().empty());
+        REQUIRE_FALSE(handoff.parked());
+    }
+    SECTION("deactivate alone, as VST2's SUSPEND(0) does off the main thread") {
+        FakeHost host;
+        LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+        handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+        host.reset();
+        const std::thread::id foreign = onForeignThread([&] { handoff.deactivate(); });
+        REQUIRE(host.calls().empty());
+        REQUIRE(handoff.parked());
+        // Not at once: the activate half of a format's pair can arrive a moment later, and cancels this for nothing.
+        REQUIRE(handoff.idleTick() == 0);
+        REQUIRE(host.calls().empty());
+        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // past the grace period
+        // DPF promises no run() after a deactivate, so the tick may take the engine back without waiting for a block -
+        // and KEEPS it: giving it to a thread that will not run it would fill the ring with nobody draining.
+        REQUIRE(handoff.idleTick() == 0);
+        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim"});
+        REQUIRE_FALSE(host.ranOn(foreign));
+        REQUIRE(handoff.parked());
+        REQUIRE_FALSE(handoff.beginBlock());
+
+        // The activate that follows asks for the give, and the tick makes it.
+        onForeignThread([&] { handoff.activate(1024); });
+        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim"});
+        REQUIRE_FALSE(handoff.beginBlock());
+        REQUIRE(handoff.idleTick() == 0);
+        REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "give", "control"});
+        REQUIRE(handoff.beginBlock());
+    }
+}
+
+TEST_CASE("the idle tick is the control thread's: off it, it applies nothing", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    std::size_t ticked = 1;
+    // A host that calls getState off its main thread (VST2's effGetChunk can be) reaches the tick from there: the
+    // reclaim, the handoff and the JS its report runs are all the control thread's, so it is a no-op here.
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+    host.reset();
+    const std::thread::id foreign = onForeignThread([&] {
+        handoff.sampleRateChanged(48000.0);
+        ticked = handoff.idleTick();
+    });
+    REQUIRE(ticked == 0);
+    REQUIRE(host.calls().empty());
+    REQUIRE(handoff.parked());
+    REQUIRE_FALSE(handoff.beginBlock());
+    REQUIRE(handoff.idleTick() == 0);  // and where it belongs it does the whole handoff
+    REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "rate", "give", "control"});
+    REQUIRE_FALSE(host.ranOn(foreign));
+}
+
+TEST_CASE("a message a parked block could not hold is counted, and said on the control thread", "[lifecycle]") {
+    FakeHost host;
+    LifecycleHandoff handoff(host, std::this_thread::get_id(), 8192);
+    handoff.activate(1024);  // as DPF's first activate, on the thread it built the plugin on: the engine is given
+    host.reset();
+    onForeignThread([&] { handoff.sampleRateChanged(48000.0); });
+    REQUIRE_FALSE(handoff.beginBlock());
+    const std::uint8_t sysex[6] = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
+    handoff.holdMidi(sysex, 6);  // longer than DPF's inline data, so not held: counted, and said where a log belongs
+    REQUIRE(handoff.held().count() == 0);
+    REQUIRE(handoff.idleTick() == 0);
+    REQUIRE(host.names() == std::vector<std::string>{"take", "reclaim", "rate", "give", "midiDropped", "control"});
+    REQUIRE(host.calls().back().value == 0.0);  // the control plane's report is the last call, the drop the one before
+    REQUIRE(host.calls()[4].value == 1.0);
+}
+
+TEST_CASE("held MIDI is a fixed buffer: an over-long or over-full message is dropped and counted", "[lifecycle]") {
+    HeldMidi held;
+    const std::uint8_t sysex[6] = {0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7};
+    REQUIRE_FALSE(held.push(sysex, 6));
+    REQUIRE(held.dropped() == 1);
+    const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
+    for (int i = 0; i < HeldMidi::kCapacity; ++i) REQUIRE(held.push(note, 3));
+    REQUIRE_FALSE(held.push(note, 3));
+    REQUIRE(held.dropped() == 2);
+    REQUIRE(held.count() == HeldMidi::kCapacity);
+    held.clear();
+    REQUIRE(held.count() == 0);
+}
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 900 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-plugin-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-plugin-test
```

Expected: FAIL, eight of the fifteen runs, each for the reason the task fixes: the placeholder policy does its engine
work on whichever thread called it, nothing parks, and the idle tick runs wherever it is called from. The first failure
is the ownership case, whose give and latency report ran on JACK's thread, and the foreign pair that should have cost
nothing shows the same:

```
-------------------------------------------------------------------------------
a control-thread deactivate leaves the engine control-owned, and a foreign
activate parks until the tick gives it
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:174
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:187: FAILED:
  REQUIRE( host.names().size() == 5 )
with expansion:
  6 == 5
...
a foreign deactivate nobody follows is taken back and kept; one that pairs
costs nothing
  deactivate then activate, as VST2's SUSPEND(1) does: nothing moved
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:220
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:229: FAILED:
  REQUIRE( host.calls().empty() )
with expansion:
  false
...
the very first activate on a foreign thread parks, and the tick gives it
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:203
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/native/test/plugin/LifecycleHandoff.test.cpp:209: FAILED:
  REQUIRE_FALSE( host.ranOn(foreign) )
with expansion:
  !true
...
test cases:  15 |   7 passed | 8 failed
assertions: 299 | 290 passed | 9 failed
```

- [ ] **Step 3: implement it**

The policy replaces the placeholder: a size within the plan is a no-op on any thread; a rate change or an over-plan
size parks `run()` (silence, its MIDI in a fixed buffer), and the control thread's idle tick does the quiescent
handoff, applies the values, reports latency and re-labels the CC map. `PluginDSP` implements `HandoffHost`, forwards
the four overrides, gates `run()`, replays the held MIDI at frame 0 of the first resumed block, and routes its idle
seam and `getState`/`setState` through the tick. The long comment above the overrides says which formats call them from
where, and points at the header for the argument.

Who owns the engine is a second fact, tracked beside the park state, because a format can move the engine on a thread of
its own: a foreign activate when the engine is the control thread's only requests the give (the tick makes it from a
parked block), a foreign deactivate nothing follows is taken and KEPT after a 250 ms grace period (a paired JACK or VST2
deactivate and activate needs nothing, and the grace is what lets the pair finish first), and a control-thread
deactivate takes and keeps it as every format's did. `idleTick` is the control thread's alone: off it, it applies
nothing and the park stays pending for the thread that owns the engine (it still says an overdue park from there, since
a host that moved its main thread has no other tick, and the header now says so instead of claiming that case is merely
slower). Four smaller ones ride the same step: `beginBlock` moves Requested to Parked with a compare-exchange (never over
a Running an activate just stored); a MIDI message the fixed buffer could not keep is counted on the audio thread and
said by the next control-thread tick, through the host's `midiDropped`, instead of being dropped silently, and the count
is an atomic because parked blocks keep counting after the first one's CAS; a foreign `activate` whose block is longer
than the plan records the size as pending, since DPF does not always announce it through `bufferSizeChanged` (that
returns early on an unchanged size); and the state starts Suspended rather than Running, because nothing has given the
engine to an audio thread before the first activate, so Running from the start would contradict the header's rule that
a block drives the engine only when it is given.

````patch
diff --git a/packages/native/plugin/LifecycleHandoff.hpp b/packages/native/plugin/LifecycleHandoff.hpp
--- a/packages/native/plugin/LifecycleHandoff.hpp
+++ b/packages/native/plugin/LifecycleHandoff.hpp
@@ -10,7 +10,7 @@
 // there: the ownership flip and the drains race the editor's per-frame reclaim and its edits on the control thread,
 // and updateLatency / updateParameterMap call into the QuickJS context that thread pumps every frame. PipeWire changes
 // its quantum whenever another client asks for a different latency, so this is routine on a PipeWire desktop, not the
-// rare manual case (spec/13-subsystems.md, the plugin lifecycle's thread rule).
+// rare manual case (spec/02-native-host.md, the plugin lifecycle's thread rule).
 //
 // The policy splits the work by what the change actually needs, and never touches the engine on a foreign thread:
 //
@@ -25,16 +25,44 @@
 //    reclaimReleased - then does the whole quiescent handoff: take the engine back, drain, reclaim, apply the rate and
 //    the size, hand it back, report latency and re-label the CC map.
 //
+// WHAT OWNS THE ENGINE, which the two tiers above both turn on. There are two facts, and the policy tracks them
+// separately: `engineGiven_` (the audio thread may run it) and the state (whether a block may drive it). A block drives
+// the engine only when the state is Running, and the state is only Running when the engine is given, so the two cannot
+// disagree in that direction. The direction that matters is the other one, and it is what a lifecycle call may not get
+// wrong: a format can move the engine between them on a foreign thread.
+//
+//  - **A foreign activate when the engine is the CONTROL thread's** (a control-thread deactivate before it, or the
+//    plugin's very first activate reaching the audio thread through DPF's run(), which activates when fIsActive is
+//    false, DistrhoPluginInternal.hpp's PluginExporter::run; VST3's setProcessing from the processing thread and VST2's
+//    SUSPEND(1) reach it too). Giving the engine to a thread that is already running, without the quiescent handoff,
+//    would put two threads in the command and release rings. So it REQUESTS the give: the park runs on the next block
+//    and the tick gives from the control thread.
+//  - **A foreign deactivate that nothing follows** (a VST2 SUSPEND(0) off the host's main thread). DPF promises no
+//    run() after a deactivate, so the engine would sit audio-owned with nobody draining: the ring fills and every edit
+//    answers busy. The foreign call parks, and once the deactivate has stood for kDeactivateGrace the tick TAKES the
+//    engine and keeps it (state Suspended: no give, no resume), so an edit applies inline while the plugin is
+//    suspended. The grace period exists because a format's deactivate and activate are two calls, not one: JACK's and
+//    VST2's pairs take the mutex separately, a tick may land between them, and the pair needs nothing at all if it is
+//    allowed to finish. A paired activate cancels the request, so the routine JACK callback costs nothing.
+//  - **A control-thread deactivate** takes the engine and keeps it too, as every format's did before this policy: the
+//    state goes Suspended, and an idle tick must not hand it back to a thread that is not running. The activate that
+//    follows gives it again.
+//
 // The handshake is a release/acquire flag. run() reads the state with acquire and stores Parked (release) only on
 // ENTRY to a later block, so a block that stored Parked has already declined to touch the engine and the tick, which
 // waits for Parked, cannot overlap one that did. It therefore does not rely on JACK's promise that process does not run
-// during the callback. A foreign deactivate with no activate after it (a VST2 SUSPEND off the main thread) parks too,
-// and DPF guarantees no run() after a deactivate, so the tick may take the engine back for that alone.
+// during the callback.
+//
+// One mutex serialises the foreign lifecycle calls against the tick; run() never takes it. The tick itself is the
+// control thread's alone: a host that calls getState from another thread (VST2's effGetChunk can) applies nothing
+// there, and the park waits for a call that belongs to the thread that owns the engine. The thread that owns the engine
+// is the one that constructed the policy, so a host that MOVES its main thread would read as foreign everywhere and
+// never apply a park: DPF's formats all construct and idle on the main thread, which is the premise this rests on,
+// and the one thing the policy can do for a host that does not is say the park is overdue.
 //
-// One mutex serialises the foreign lifecycle calls against the tick; run() never takes it. A host that moves its main
-// thread reads as foreign and takes the deferred path, which is still correct, only slower. A host with no idle tick
-// at all (a VST2 host that changes the rate off its main thread with the editor closed) is covered by getState and
-// setState, which ask the policy to apply a pending park; a park nobody applies is reported once after a second.
+// A host with no idle tick at all (a VST2 host that changes the rate off its main thread with the editor closed) is
+// covered by getState and setState, which ask the policy to apply a pending park; a park nobody applies is reported
+// once after a second, from whichever control-thread entry comes next (there is nothing else that could say it).
 //
 // Nothing here touches DPF, so the policy is unit-tested without it (test/plugin/LifecycleHandoff.test.cpp) and
 // PluginDSP only implements HandoffHost over its engine and forwards its four lifecycle overrides.
@@ -125,32 +153,138 @@
 // activate() on.
 class LifecycleHandoff {
 public:
-    enum class State { Running, Requested, Parked };
+    // Running: no block is parked and the audio thread may drive the engine.
+    // Requested: a park has been asked for and no block has seen it yet.
+    // Parked: a block has stored that it declined to drive the engine, which is the moment the tick may take it.
+    // Suspended: the engine is the control thread's and must stay so until an activate. A format's deactivate, either
+    // thread, leaves this state (see the header): a block parks, and an idle tick does nothing about it.
+    enum class State { Running, Requested, Parked, Suspended };
 
-    LifecycleHandoff(HandoffHost& host, std::thread::id /*controlThread*/, std::uint32_t /*planMaxBlockFrames*/)
-        : host_(host) {}
+    LifecycleHandoff(HandoffHost& host, std::thread::id controlThread, std::uint32_t planMaxBlockFrames)
+        : host_(host), controlThread_(controlThread), planFrames_(planMaxBlockFrames) {}
 
+    // --- the DPF overrides, on whichever thread the format calls them ----------------------------------------------
+
+    // DPF calls this on the control thread before an activate (and a VST2 host may not, when it suspends).
     void deactivate() {
-        host_.takeEngine();
-        host_.reclaimReleases();
+        std::lock_guard<std::mutex> lock(lifecycle_);
+        deactivated_ = true;  // either thread: until an activate, nothing may give the engine back
+        if (onControlThread()) {
+            // Quiescent by the format's contract, and the engine stays this thread's until the activate that follows:
+            // today's behaviour, and what makes an edit apply inline while the plugin is suspended.
+            if (engineGiven_) {
+                host_.takeEngine();
+                host_.reclaimReleases();
+                engineGiven_ = false;
+            }
+            state_.store(State::Suspended, std::memory_order_release);
+            return;
+        }
+        // Foreign: DPF promises no run() after this until an activate, so the engine can be taken back here - but not
+        // at once, because the activate half of the format's pair may be a moment behind and needs nothing at all.
+        deactivatedAt_ = Clock::now();
+        requestParkLocked();
     }
 
+    // `frames` is the host's buffer size at this activation (DPF's getBufferSize), the plan's first input.
     void activate(std::uint32_t frames) {
+        std::lock_guard<std::mutex> lock(lifecycle_);
+        deactivated_ = false;  // an activate pairs a foreign deactivate, if there was one: nothing to take back
+        if (!onControlThread()) {
+            // A block longer than the plan has to grow it, and nothing may do that while the audio thread runs the
+            // engine. DPF usually announces it through bufferSizeChanged first, but not always: setBufferSize returns
+            // early on an unchanged size, so a host above the plan can reach only this call. Recorded before the
+            // engine check: DPF's run() auto-activates a host's first block on the audio thread, engine not yet given.
+            const bool overPlan = frames > planFrames_;
+            if (overPlan) {
+                hasPendingSize_ = true;
+                pendingFrames_  = std::max(pendingFrames_, frames);
+            }
+            if (!engineGiven_ || overPlan) {
+                // The engine is the control thread's (a deactivate before this, or this host's first activate): giving
+                // it to a thread that is already running without the quiescent handoff would put two threads in the
+                // rings. Ask for the give; the next block parks and the tick makes it (and grows the plan first).
+                requestParkLocked();
+                return;
+            }
+            // Audio-owned, so the engine never moved: the format's pair needs nothing, unless a size or a rate really
+            // is pending, and then the park is already requested.
+            if (!hasPendingSize_ && !hasPendingRate_) resumeLocked();
+            return;
+        }
+        // On the control thread this is quiescent whatever happened before it: DPF runs deactivate first, and a
+        // format that skips it still promised no run() before activate.
+        if (engineGiven_) {
+            host_.takeEngine();
+            host_.reclaimReleases();
+        }
+        planFrames_ = std::max(planFrames_, frames);
         host_.setMaxBlockFrames(frames);
+        applyPendingLocked();
         host_.giveEngine();
+        engineGiven_ = true;
+        // Running before the two JS calls below, so a block that starts while they run drives an engine that is
+        // already handed over and applied (run() never takes the mutex, so nothing but the state gates it).
+        resumeLocked();
+        reportDroppedLocked();
         host_.updateControlPlane();
     }
 
-    void bufferSizeChanged(std::uint32_t /*frames*/) {}
+    void bufferSizeChanged(std::uint32_t frames) {
+        std::lock_guard<std::mutex> lock(lifecycle_);
+        if (onControlThread()) {
+            planFrames_ = std::max(planFrames_, frames);
+            hasPendingSize_ = false;
+            host_.setMaxBlockFrames(frames);
+            return;
+        }
+        // Tier 1: the plan already holds a block this long, so nothing has to move. The next run() gets the block and
+        // the ring drains as usual; the plan is untouched because the engine never left the audio thread.
+        if (frames <= planFrames_) return;
+        hasPendingSize_ = true;
+        pendingFrames_  = frames;
+        requestParkLocked();
+    }
+
+    void sampleRateChanged(double rate) {
+        std::lock_guard<std::mutex> lock(lifecycle_);
+        if (onControlThread()) {
+            hasPendingRate_ = false;
+            host_.setSampleRate(rate);
+            return;
+        }
+        hasPendingRate_ = true;
+        pendingRate_    = rate;
+        requestParkLocked();
+    }
 
-    void sampleRateChanged(double rate) { host_.setSampleRate(rate); }
+    // --- the audio thread ----------------------------------------------------------------------------------------
 
-    bool beginBlock() { return true; }
+    // True when run() may drive the engine for this block. False while a park is requested, applied or suspended: the
+    // caller writes silence and holds its MIDI, and touches nothing. Stores Parked on entry, so a tick that sees Parked
+    // knows this block and every block before it have finished with the engine; the exchange only ever moves
+    // Requested -> Parked, so it cannot overwrite a Running a control-thread activate has just stored.
+    bool beginBlock() {
+        State state = state_.load(std::memory_order_acquire);
+        if (state == State::Running) return true;
+        if (state == State::Requested) {
+            State expected = State::Requested;
+            state_.compare_exchange_strong(expected, State::Parked, std::memory_order_release,
+                                           std::memory_order_acquire);
+        }
+        return false;
+    }
 
-    void holdMidi(const std::uint8_t* bytes, std::uint32_t size) { held_.push(bytes, size); }
+    // Hold one message for the resumed block. Audio thread, parked blocks only. A message the fixed buffer cannot keep
+    // is counted here and said by the control thread's next tick (see HandoffHost::midiDropped).
+    void holdMidi(const std::uint8_t* bytes, std::uint32_t size) {
+        if (!held_.push(bytes, size)) heldDropped_.fetch_add(1, std::memory_order_relaxed);
+    }
 
     const HeldMidi& held() const { return held_; }
 
+    // Visit the MIDI a park held, as frame 0 of the block that resumes (a note-off is not lost), and clear it. Called
+    // by the first block beginBlock() lets through, so on the audio thread.
     template <typename Push>
     void replayHeld(Push&& push) {
         if (held_.count() == 0) return;
@@ -158,15 +292,157 @@
         held_.clear();
     }
 
-    std::size_t idleTick() { return host_.reclaimReleases(); }
+    // --- the control thread's idle ----------------------------------------------------------------------------------
 
-    bool parked() const { return false; }
+    // The editor's per-frame tick: reclaim what the audio thread released, and apply the handoff a park has left
+    // pending. Returns how many plans it freed, for the idle reclaim probe. The CONTROL thread's alone: every engine
+    // call it makes, and the JS in the report, belongs to the thread that owns the engine, so a call from another
+    // thread applies nothing and leaves the park pending for this one. It does say an overdue park, from any thread: a
+    // host that moved its main thread has no other tick, and a park nobody applies is worth a log line wherever it is
+    // noticed.
+    std::size_t idleTick() {
+        if (!onControlThread()) {
+            std::lock_guard<std::mutex> lock(lifecycle_);
+            noteOverdueLocked();
+            return 0;
+        }
+        std::lock_guard<std::mutex> lock(lifecycle_);
+        noteOverdueLocked();
+        const State state = state_.load(std::memory_order_acquire);
+        if (state == State::Running) return host_.reclaimReleases();
+        if (state == State::Suspended) return 0;  // the engine is this thread's already, and stays so until an activate
+        if (deactivated_) {
+            // The format says no run() is coming, but its activate may still: give it kDeactivateGrace to arrive,
+            // and then take the engine back and keep it (see the header).
+            if (state == State::Requested && Clock::now() - deactivatedAt_ < kDeactivateGrace) return 0;
+            return suspendLocked();
+        }
+        // A park for a rate or a size, with the engine still the audio thread's: a block must have seen the park
+        // before the tick touches the engine.
+        if (state != State::Parked) return 0;
+        return handoffLocked();
+    }
 
-    State state() const { return State::Running; }
+    bool parked() const { return state_.load(std::memory_order_acquire) != State::Running; }
+
+    State state() const { return state_.load(std::memory_order_acquire); }
 
 private:
-    HandoffHost& host_;
-    HeldMidi     held_;
+    using Clock = std::chrono::steady_clock;
+
+    // How long a foreign deactivate may stand before the tick takes the engine back and keeps it. Long enough for the
+    // activate half of a JACK or VST2 pair, which are two calls and not one, and short enough that a suspended plugin
+    // gets its inline edits back well inside a UI frame's worth of time.
+    static constexpr std::chrono::milliseconds kDeactivateGrace{250};
+
+    bool onControlThread() const { return std::this_thread::get_id() == controlThread_; }
+
+    void requestParkLocked() {
+        const State state = state_.load(std::memory_order_relaxed);
+        if (state == State::Parked) return;  // already parked: the tick applies what it finds, whenever it comes
+        if (state == State::Running) overdueReported_ = false;
+        parkedAt_ = Clock::now();
+        state_.store(State::Requested, std::memory_order_release);
+    }
+
+    void resumeLocked() {
+        state_.store(State::Running, std::memory_order_release);
+        overdueReported_ = false;
+    }
+
+    // The quiescent handoff, on the control thread. Only ever reached from Parked (the audio thread has finished with
+    // the engine: it stored Parked with release, this read it with acquire).
+    std::size_t handoffLocked() {
+        std::size_t freed = 0;
+        if (engineGiven_) {
+            host_.takeEngine();
+            freed = host_.reclaimReleases();
+        }
+        applyPendingLocked();
+        host_.giveEngine();
+        engineGiven_ = true;
+        resumeLocked();
+        reportDroppedLocked();
+        host_.updateControlPlane();
+        return freed;
+    }
+
+    // The engine's the control thread's from here (a foreign deactivate nothing followed): take it if it is still the
+    // audio thread's, reclaim what that thread handed back, and KEEP it. No give, no resume: a hand-off to a thread
+    // that is not running would fill the ring with nobody draining, and the activate that comes later asks for it.
+    std::size_t suspendLocked() {
+        std::size_t freed = 0;
+        if (engineGiven_) {
+            host_.takeEngine();
+            freed = host_.reclaimReleases();
+            engineGiven_ = false;
+        }
+        state_.store(State::Suspended, std::memory_order_release);
+        return freed;
+    }
+
+    void applyPendingLocked() {
+        if (hasPendingRate_) {
+            host_.setSampleRate(pendingRate_);
+            hasPendingRate_ = false;
+        }
+        if (hasPendingSize_) {
+            planFrames_ = std::max(planFrames_, pendingFrames_);
+            host_.setMaxBlockFrames(pendingFrames_);
+            hasPendingSize_ = false;
+            pendingFrames_  = 0;
+        }
+    }
+
+    // A message a parked block could not hold, said on this thread: the audio thread only counted it.
+    void reportDroppedLocked() {
+        if (heldDropped_.load(std::memory_order_relaxed) == 0) return;
+        const std::uint64_t dropped = heldDropped_.exchange(0, std::memory_order_relaxed);
+        host_.midiDropped(dropped);
+    }
+
+    // A park nobody applied within a second: a host with no idle tick and no state save. Said once, so a stuck plugin
+    // cannot flood the log, and only from a later control-thread entry - nothing else could say it.
+    void noteOverdueLocked() {
+        // Only a park that is WAITING is overdue: a suspended plugin is exactly where a deactivate left it, and its
+        // engine is the control thread's, so nothing is stuck.
+        const State state = state_.load(std::memory_order_relaxed);
+        if (overdueReported_ || (state != State::Requested && state != State::Parked)) return;
+        const double seconds = std::chrono::duration<double>(Clock::now() - parkedAt_).count();
+        if (seconds < 1.0) return;
+        overdueReported_ = true;
+        host_.parkOverdue(seconds);
+    }
+
+    HandoffHost&               host_;
+    const std::thread::id      controlThread_;
+    std::mutex                 lifecycle_;
+
+    // Written on the control thread under lifecycle_ and by run() (Requested -> Parked only), read on the audio thread
+    // without the mutex: an atomic, so a park that lands mid-block is seen by the next one and never torn. Suspended to
+    // begin with, because nothing has given the engine to an audio thread yet: a block that ran before an activate
+    // would be driving an engine nobody handed over. The first control-thread activate gives it, as it does from
+    // anywhere else.
+    std::atomic<State>         state_{State::Suspended};
+
+    // Control thread under lifecycle_.
+    std::uint32_t  planFrames_;
+    bool           engineGiven_      = false;  // the audio thread may run the engine
+    bool           deactivated_      = false;  // the format deactivated and no activate has come yet
+    bool           hasPendingSize_   = false;
+    std::uint32_t  pendingFrames_    = 0;
+    bool           hasPendingRate_   = false;
+    double         pendingRate_      = 0.0;
+    Clock::time_point deactivatedAt_{};
+    Clock::time_point parkedAt_{};
+    bool           overdueReported_  = false;
+    // Counted by parked blocks on the audio thread (several may run after the first one's CAS, and only that first
+    // store is what the tick waits for), read and cleared by the control thread: a count is a count, so relaxed.
+    std::atomic<std::uint64_t> heldDropped_{0};
+
+    // Audio thread throughout: the parked blocks write it, and the first resumed block reads and clears it. The
+    // state's release/acquire pair orders them.
+    HeldMidi       held_;
 };
 
 } // namespace rp::plugin
diff --git a/packages/native/plugin/PluginDSP.cpp b/packages/native/plugin/PluginDSP.cpp
--- a/packages/native/plugin/PluginDSP.cpp
+++ b/packages/native/plugin/PluginDSP.cpp
@@ -25,6 +25,7 @@
 
 #include "PluginShared.hpp"     // SharedDSP handoff to the editor
 #include "LastGoodState.hpp"   // keeps a failed project serialization from reporting "empty"
+#include "LifecycleHandoff.hpp" // the lifecycle policy: a foreign size/rate change parks instead of racing here
 
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
@@ -77,7 +78,7 @@
     std::string   shortName;
 };
 
-class PluginDSP : public Plugin {
+class PluginDSP : public Plugin, private rp::plugin::HandoffHost {
     // Control-plane runtime (main-thread only): the txiki context + the backend service graph it drives
     // over __rpcSend. The plugin owns the Engine and drives it per audio block from run() directly (it
     // never routes audio through RPC), so it holds svc_.engine + the ONE invoker plus the host + emulator
@@ -89,6 +90,11 @@
     // RETROPLUG_IDLE_RECLAIM_TEST: inert unless `tools/run-standalone.sh` arms it. Holds the editor's idle (the only
     // per-frame hook the plugin has) to the idle tick it owes the engine, which no in-process test can reach.
     retroplug::IdleReclaimProbe idleProbe_;
+    // What a host's buffer-size / sample-rate callback may do, and what run() does while one is pending: see
+    // LifecycleHandoff.hpp. Built on the thread DPF builds the plugin on, which is the thread it also runs the first
+    // activate() on. mutable because getState (DPF's signature is const) is one of the seams that applies a pending
+    // park, for a host that never calls the editor's idle.
+    mutable rp::plugin::LifecycleHandoff handoff_{*this, std::this_thread::get_id(), Engine::kDefaultMaxBlockFrames};
     // The device providers of the packages this product lists (rp_product_providers.hpp, generated): the N8 pack's,
     // the same one the SDL standalone starts, which owns the physical Everdrive N8 Pro's streaming link, its n8.cfg in
     // svc_.host's config dir and the cart link the engine hands rp.n8.nes by name. Nothing in run() pushes at it: the
@@ -258,6 +264,9 @@
     }
     String getState(const char* key) const override {
         if (std::strcmp(key, "project") != 0) return String();
+        // A host that changed the rate off its main thread and never called the editor's idle left a park pending, and
+        // everything below reads the script context: apply it here, on this (the control) thread.
+        handoff_.idleTick();
         const std::string& out =
             lastGoodProject_.offer(const_cast<PluginDSP*>(this)->callGlobal("__rp_saveProjectB64", nullptr));
         // Log the first substitution only: the host calls this on every save AND on editor connect, so
@@ -268,6 +277,7 @@
     }
     void setState(const char* key, const char* value) override {
         if (std::strcmp(key, "project") != 0) return;
+        handoff_.idleTick();    // the same seam getState is, before a load reaches the script context
         callGlobal("__rp_loadProjectB64", value ? value : "");
         updateLatency();        // the loaded project's sync mode determines the compensable latency
         updateParameterMap();   // ...and its ROMs determine what the DAW's CC slots are called
@@ -276,38 +286,41 @@
     // --- audio lifecycle: DPF owns the audio thread and calls run(), so the plugin drives the Engine
     // directly here (mirrors AudioDriverRpcService's loop, minus the spawned thread). ---
     //
-    // activate and sampleRateChanged can re-activate the graph, so they assume DPF calls them on the control
-    // thread, as it does for CLAP, VST3 and AU. JACK does not: a buffer-size or sample-rate change runs deactivate(),
-    // the change, then activate() from its notification callback (DistrhoPluginJACK.cpp's jackBufferSizeCallback /
-    // jackSampleRateCallback -> setBufferSize(n, true)), and VST2 runs the same setBufferSize(n, true) from its own
-    // block-size opcode, on whichever thread its host calls that from. On a thread beside the main one, this is not a
-    // no-op to wave through: deactivate hands the engine back there (the ring's flush, the ownership flip, the drain,
-    // the release ring) with the editor's idle reclaiming and editing on this thread, which is spec/07's "The JACK
-    // standalone's control handoff" known bug, and only activate's setMaxBlockFrames is a no-op for a block within the
-    // plan's maximum - which is why that is the only part ever noticed. The hazard predates the graph engine. (The
-    // output shape is no longer set here: the control plane builds the graph for the plugin's outputs at boot.)
-    void activate() override {
-        // Hand the Engine to the DPF audio thread: from here every control-plane edit is push-only,
-        // drained by run() each block. (Set BEFORE the host starts calling run(); the quiescent path
-        // flushed every push, so the command ring is empty at this handoff.) Then report PDC latency.
-        svc_.engine.setMaxBlockFrames(getBufferSize());
-        svc_.invoker.setAudioThreadOwns(true);
-        updateLatency();
-        updateParameterMap();
-    }
-    void deactivate() override {
-        // DPF guarantees no run() during/after deactivate, so the ring has a single accessor again. Take
-        // the Engine back, apply any commands the last block didn't drain (no lost mutation), and reclaim
-        // the plans the audio thread handed back, freeing each core only they held (and its snapshot slot).
-        svc_.invoker.setAudioThreadOwns(false);
-        svc_.invoker.drainInto(svc_.engine);
-        svc_.invoker.reclaimReleased();
-    }
-    void sampleRateChanged(double newSampleRate) override { svc_.engine.setSampleRate(newSampleRate); }
+    // The four overrides below are the FORMAT's callbacks, and only some formats run them on the control thread. CLAP,
+    // VST3 and AU do. JACK does not: a buffer-size or sample-rate change runs deactivate(), the change and activate()
+    // from its notification callback (DistrhoPluginJACK.cpp's jackBufferSizeCallback / jackSampleRateCallback ->
+    // PluginExporter::setBufferSize(n, true)), and VST2 runs the same sequence from whichever thread its host
+    // dispatches the block-size and sample-rate opcodes on. Doing the engine work there races the editor's per-frame
+    // reclaim and its edits, and updateLatency / updateParameterMap would call into the QuickJS context this thread
+    // pumps every frame (spec/02-native-host.md's plugin lifecycle thread rule; PipeWire changes its quantum whenever
+    // any client asks for a different latency, so it is routine). So all four only RECORD what the format asked for and
+    // hand it to the policy: a size the plan already holds costs nothing, and a rate change or an over-plan size parks
+    // run() until this thread's idle tick (the editor's per-frame reclaimReleased) applies it quiescently.
+    // LifecycleHandoff.hpp is the whole argument, including what a format's activate and deactivate mean for who owns
+    // the engine.
+    void activate() override { handoff_.activate(getBufferSize()); }
+    void deactivate() override { handoff_.deactivate(); }
+    void bufferSizeChanged(uint32_t newBufferSize) override { handoff_.bufferSizeChanged(newBufferSize); }
+    void sampleRateChanged(double newSampleRate) override { handoff_.sampleRateChanged(newSampleRate); }
 
     // --- the per-block loop (replaces AudioDriverRpcService::audioLoop) ---
     void run(const float**, float** outputs, uint32_t frames,
              const MidiEvent* midiEvents, uint32_t midiEventCount) override {
+        // A park comes before anything that reaches the engine or the script context: this block is silence, its MIDI
+        // is held for the first resumed one, and nothing here allocates or takes a lock the audio thread would.
+        if (!handoff_.beginBlock()) {
+            holdMidi(midiEvents, midiEventCount);
+            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
+                std::memset(outputs[c], 0, frames * sizeof(float));
+            return;
+        }
+
+        // What a park held, at frame 0 of this first resumed block, before the block's own messages: a note-off the
+        // host sent while parked must reach the cart before anything struck after it.
+        handoff_.replayHeld([this](const std::uint8_t* bytes, std::uint32_t size) {
+            svc_.engine.stageMidi(0, std::span<const std::uint8_t>(bytes, size));
+        });
+
         // Transport from the host. The Engine advances its own PPQ from bpm/transport, so pass those
         // two only (pushing host ppq too would double-count).
         double bpm = 120.0;
@@ -384,6 +397,41 @@
     }
 
 private:
+    // --- what the lifecycle policy may do to the engine (rp::plugin::HandoffHost), on the control thread only -------
+    void takeEngine() override {
+        // DPF guarantees no run() while the engine is taken: flip the ownership, then apply any commands the last block
+        // did not drain (no lost mutation).
+        svc_.invoker.setAudioThreadOwns(false);
+        svc_.invoker.drainInto(svc_.engine);
+    }
+    std::size_t reclaimReleases() override { return svc_.reclaimOnIdle(); }
+    void giveEngine() override { svc_.invoker.setAudioThreadOwns(true); }
+    void setMaxBlockFrames(std::uint32_t frames) override { svc_.engine.setMaxBlockFrames(frames); }
+    void setSampleRate(double rate) override { svc_.engine.setSampleRate(rate); }
+    void updateControlPlane() override {
+        updateLatency();
+        updateParameterMap();
+    }
+    void parkOverdue(double seconds) override {
+        d_stderr("[retroplug] a lifecycle handoff stayed parked for %.1f s: no idle tick and no state save applied it",
+                 seconds);
+    }
+    void midiDropped(std::uint64_t count) override {
+        d_stderr("[retroplug] a parked block dropped %llu MIDI message(s): they did not fit the hold",
+                 static_cast<unsigned long long>(count));
+    }
+
+    // Copy a parked block's MIDI into the policy's fixed buffer (LifecycleHandoff.hpp): a message longer than DPF's
+    // inline data is not held, and past the buffer a message is dropped and counted rather than allocated for.
+    void holdMidi(const MidiEvent* events, uint32_t count) {
+        for (uint32_t i = 0; i < count; ++i) {
+            const MidiEvent& e = events[i];
+            if (e.size < 1) continue;
+            const std::uint8_t* bytes = e.size <= MidiEvent::kDataSize ? e.data : e.dataExt;
+            if (bytes != nullptr) handoff_.holdMidi(bytes, e.size);
+        }
+    }
+
     // Bring up the control-plane runtime: bind __rpcSend, eval the bundle, autoload.
     void bootControlPlane() {
         if (!host_.init()) { d_stderr("[retroplug] TjsHostRuntime init failed"); return; }
@@ -431,7 +479,9 @@
         // The editor's per-frame hand-back, through the one idle seam both hosts call (the SDL standalone's loop is the
         // other). The probe rides it so a host that stops calling it goes red in its own smoke.
         shared_.reclaimReleased  = [this] {
-            const std::size_t freed = svc_.reclaimOnIdle();
+            // The idle tick also applies a lifecycle handoff a foreign callback parked for, which is why the policy's
+            // tick is the one seam (see LifecycleHandoff.hpp).
+            const std::size_t freed = handoff_.idleTick();
             idleProbe_.tick(freed);
             return freed;
         };
````

- [ ] **Step 4: run it, and see it pass**

```
timeout -k 10 900 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-plugin-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-plugin-test
```

Expected: PASS, the whole binary (its `ContextTargets` and `LastGoodState` cases beside the new ones; Catch2 counts
each leaf section as a run, hence fifteen):

```
===============================================================================
All tests passed (363 assertions in 15 test cases)
```

- [ ] **Step 5: the plugin targets this task compiles**

`PluginDSP.cpp` is `retroplug-dsp`, which every format links. Build all four formats that compile it, and do not run
the JACK standalone (the by-hand leg is the controller's):

```
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-vst2 -j$(nproc)
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-vst3 -j$(nproc)
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-clap -j$(nproc)
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-jack -j$(nproc)
```

Expected: PASS each (`Built target retroplug-vst2`, `...vst3`, `...clap`, `...jack`; the JACK target builds
`build/bin/retroplug` and is not started).

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/native/plugin/LifecycleHandoff.hpp \
  packages/native/plugin/PluginDSP.cpp packages/native/cmake/tests.cmake \
  packages/native/test/plugin/LifecycleHandoff.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb4): a host's lifecycle change is applied on the control thread, not JACK's

DPF's JACK host runs deactivate, the change and activate from its notification
thread, and VST2 from whichever thread its host dispatches the opcodes on.
PluginDSP did the engine work there: the ownership flip and the drains race the
editor's per-frame reclaim and edits, the script host's GC and free ran on two
threads, and updateLatency/updateParameterMap called into the QuickJS context the
control thread pumps. The policy is now a unit of its own
(plugin/LifecycleHandoff.hpp): a foreign buffer-size change within the plan is a
no-op, and a rate change or an over-plan size parks run(), which writes silence
and holds its MIDI until the control thread's idle applies the change
quiescently and the first resumed block replays what was held. Who owns the
engine is tracked beside that: an activate on a foreign thread while it is the
control thread's only asks for the give, a deactivate nothing follows is taken
and kept after a short grace period, and a control-thread deactivate keeps it as
every format's did, so no block ever drives an engine another thread holds.
MSG
```

---

## Task 4: a handover releases only the old node's notes

When a cart node is destroyed its held notes are released, and `N8Link` took the release's turn as everything pushed so
far (`releaseAfter_ = chunksPushed_`), the node taking over's first blocks among them. In a plugin whose audio thread
owns the engine the publish is adopted by a block and the replaced plan is reclaimed later, so by then the new node has
pushed: a note it starts on a key the old one held is cut by the old one's release. A per-key holder count would be
wrong (a node forwards the DAW's duplicate note-ons, so a count would rise and never come down), so the link keeps, per
key, the index of the chunk whose note-on it last wrote, and a release waits behind the LEAVING node's own mark.

**Files** (line numbers are the file at `ccc01889e`):

- Modify: `packages/n8/native/src/n8/CartLink.hpp:88-130` (`pushMark()` and the marked `releaseNotes` overload, both
  defaulted).
- Modify: `packages/n8/native/src/n8/desktop/N8CartLink.hpp:27-38` (`pushMark`), `:72-84` (both `releaseNotes` forms
  visible, and the marked override).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp:139-160` (`releaseNotes(notes, mark)`, `chunksPushed()`),
  `:216-224` (`lastOn_`).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.cpp:1-12` (the include), `:90-100` (connect resets the table),
  `:145-180` (`releaseNotes`, `writeReleases`, and the message-boundary gate it waits for), `:380-400`
  (`writeChunk`'s tracker, and the byte-stream shape it follows).
- Modify: `packages/n8/native/src/n8/N8Node.cpp:68-100` (the mark and the destructor), `:120-160` (`process`'s bytes
  loop, `pushMidi`), `:180-195` (`payOwed`).
- Test: `packages/n8/native/test/N8CartJobs.test.cpp:1018-1045` (a `lastLogIndex` helper) and a new
  `[cart-release]` case after `:1116` with its own SysEx section; `packages/native/test/engine-graph/N8Adapters.test.cpp:63-90` (a
  `withCartNode` helper and `lastLogIndex`), the setup at `:164-186`, and a new section before `:210`.

**Interfaces:**

- Consumes: `rp::n8::CartNotes`, `noteEvent` (`CartLink.hpp`), `N8Link`'s ring and `chunksPushed_` / `chunksWritten_`,
  `Engine::reclaim` and `setAudioThreadOwns` (the engine case plays the audio thread with the bit set, as the plugin
  does).
- Produces: `CartLink::pushMark() const` (0 by default), `CartLink::releaseNotes(notes, mark)` (forwarding by default),
  `N8Link::chunksPushed()`, `N8Link::releaseNotes(const NoteSet&, std::uint64_t)`, `N8Nes::mark`, and `N8Link`'s
  byte-stream shape (`noteWireBytes` / `atMessageBoundary`), which keeps a release at a message boundary and is bounded
  so a stream that died mid-message still releases. Task 5 changes `N8Link` again and keeps all of it.

### Steps

- [ ] **Step 1: write the failing tests, and the interface they call**

`CartLink`'s two new virtuals are defaulted (`pushMark()` answers 0, the marked `releaseNotes` forwards), and the
adapter makes both forms visible, so the cases compile against today's behaviour and fail on it. Three sections in the
raw, the pending and the framed order, and a fourth for the design's SysEx claim: a node's BYTES port can hand over
half a SysEx, and a note-off written between the halves is payload, not a message.

````patch
diff --git a/packages/n8/native/src/n8/CartLink.hpp b/packages/n8/native/src/n8/CartLink.hpp
index 2ef9aeea0..de37803ea 100644
--- a/packages/n8/native/src/n8/CartLink.hpp
+++ b/packages/n8/native/src/n8/CartLink.hpp
@@ -123,12 +123,27 @@ public:
     // removal) and the device reports into its record no more, stopping at the next point that leaves the device
     // coherent. Nothing when it holds no job.
     virtual void abandonJobs(const void* owner) { (void)owner; }
+    // [audio] How many chunks this link has taken from its producer since the connect. Only the audio thread pushes, so a
+    // node reads it exactly after its own push, and that reading is the mark a handover's release waits behind
+    // (releaseNotes(notes, mark)): the pushes a node taking over made between the swap and the leaving node's destroy are
+    // NOT among them. A link that cannot tell the pushes apart answers 0, and its releaseNotes(notes, mark) falls back
+    // to the one-argument form.
+    virtual std::uint64_t pushMark() const { return 0; }
+
     // [control] The node is leaving the plan while the cart holds `notes` (it pushed their note-ons and no note-offs):
     // the device sends each a note-off from its own thread, after every byte pushed before this call, so no console is
     // left holding a tone no node will ever release. The node's destroy calls it, on the control thread after the plan
     // swap, where it may not push itself: the audio thread (and the node taking over) may still be pushing. A link with
     // no device, or one not streaming, drops them; nothing a node does depends on whether they went.
     virtual void releaseNotes(const CartNotes& notes) { (void)notes; }
+    // [control] As releaseNotes, but waiting behind `mark` (pushMark() after the caller's own last push) rather than
+    // behind every push so far. A cart node's destroy runs after the plan swap, so the node taking over has already
+    // pushed, and a release that waited for THOSE would cut a key it struck. A link that cannot tell the pushes apart
+    // takes the one-argument form.
+    virtual void releaseNotes(const CartNotes& notes, std::uint64_t mark) {
+        (void)mark;
+        releaseNotes(notes);
+    }
 
     // There is deliberately no error string here: why a cart would not open is the host UI's (it reads N8Host's
     // lastError through Settings > N8), not something a document's node can act on. Nor an acquire or a release: a
diff --git a/packages/n8/native/src/n8/desktop/N8CartLink.hpp b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
index 0b9bac1ac..62252ffbe 100644
--- a/packages/n8/native/src/n8/desktop/N8CartLink.hpp
+++ b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
@@ -72,6 +72,9 @@ public:
         return taken;
     }
     void abandonJobs(const void* owner) override { host_.abandonCartJobs(owner); }
+    // Both releaseNotes forms are visible on this concrete type (a hidden overload would make a caller's marked
+    // release, the one a handover makes, fail to compile).
+    using rp::n8::CartLink::releaseNotes;
     // [control] N8Link::releaseNotes: the serial thread writes their note-offs behind what was already pushed.
     void releaseNotes(const rp::n8::CartNotes& notes) override {
         N8Link::NoteSet set;
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
index f2ed65ad7..032f6b4a2 100644
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -1030,6 +1030,13 @@ rp::n8::CartNotes heldNotes() {
     return notes;
 }
 
+// Where `entry` last appears in the cart's log, or -1 when it never does.
+std::ptrdiff_t lastLogIndex(FakeN8& dev, const std::string& entry) {
+    std::lock_guard<std::mutex> l(dev.mu);
+    const auto it = std::find(dev.log.rbegin(), dev.log.rend(), entry);
+    return it == dev.log.rend() ? -1 : static_cast<std::ptrdiff_t>(dev.log.rend() - it - 1);
+}
+
 // Where `entry` first appears in the cart's log, or the log's length when it never does.
 std::ptrdiff_t logIndex(FakeN8& dev, const std::string& entry) {
     std::lock_guard<std::mutex> l(dev.mu);
@@ -1114,3 +1121,92 @@ TEST_CASE("the notes a leaving cart node held get their note-offs from the seria
         CHECK(rig.dev.count("fifo:80 3C 00") == 1);
     }
 }
+
+TEST_CASE("a handover releases only the notes the leaving node left, not a key the new one struck",
+          "[n8][cart-release]") {
+    // A cart node's destroy runs on the control thread after the plan swap, so the node taking over has already pushed.
+    // Its release therefore waits behind the LEAVING node's own last push (CartLink::pushMark) instead of behind
+    // everything pushed so far, and a key the new node struck since is skipped: a per-key holder count would be wrong
+    // (a node forwards the DAW's duplicate note-ons, so a count would rise and never come down), which is why the link
+    // keeps the index of the chunk whose note-on it last wrote for each key.
+    SECTION("raw: the new node struck one of the keys the old one held") {
+        Rig rig;
+        rig.connect();
+        rig.push({0x90, 0x3C, 0x7F});  // the leaving node, key 60
+        rig.push({0x90, 0x43, 0x7F});  // ... and key 67
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 43 7F") == 1; }));
+        const std::uint64_t mark = rig.cart.pushMark();
+        rig.push({0x90, 0x3C, 0x7F});  // the node taking over, on a key the old one held
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 2; }));
+        rig.cart.releaseNotes(heldNotes(), mark);
+        // Key 67 is the leaving node's alone, so its note-off goes; key 60 is the new node's, so none is written.
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
+        std::this_thread::sleep_for(std::chrono::milliseconds(30));
+        CHECK(rig.dev.count("fifo:80 3C 00") == 0);
+        CHECK(rig.dev.count("fifo:81 43 00") == 1);
+        CHECK(rig.cart.connected());
+    }
+    SECTION("raw: the new node's note-on is still waiting for its release time when the release runs") {
+        Rig rig;
+        rig.connect();
+        rig.push({0x90, 0x3C, 0x7F});
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+        const std::uint64_t mark = rig.cart.pushMark();
+        // 200 ms out at 48 kHz: the node taking over's note-on is still in the ring, so the release goes out ahead of
+        // it, and the wire reads on, off, on - the key sounds, as the new node meant it to.
+        const Bytes on{0x90, 0x3C, 0x7F};
+        rig.cart.push(9600, on.data(), on.size(), false, 48000.0);
+        rig.cart.releaseNotes(heldNotes(), mark);
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 2; }));
+        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
+        CHECK(lastLogIndex(rig.dev, "fifo:80 3C 00") < lastLogIndex(rig.dev, "fifo:90 3C 7F"));
+        CHECK(rig.dev.count("fifo:81 43 00") == 1);
+    }
+    SECTION("raw: a release waits for a SysEx the bytes port split across chunks") {
+        Rig rig;
+        rig.connect();
+        // A node's bytes port can hand over half a SysEx: the cart's parser is inside a message until the F7, so a
+        // note-off written between the halves is payload, not a message, and the key it meant to release stays down.
+        rig.push({0xF0, 0x7E, 0x7F});  // the first half: F0 and two of its data bytes
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F") == 1; }));
+        rig.cart.releaseNotes(heldNotes());
+        std::this_thread::sleep_for(std::chrono::milliseconds(30));
+        CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // held: the message is not finished
+        rig.push({0x09, 0x01, 0xF7});                // and its tail, which completes it
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
+        CHECK(lastLogIndex(rig.dev, "fifo:09 01 F7") < lastLogIndex(rig.dev, "fifo:80 3C 00"));
+        CHECK(rig.dev.count("fifo:81 43 00") == 1);
+    }
+    SECTION("framed: the reliable link delivers the same order") {
+        Rig rig;
+        rig.dev.menu    = false;
+        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
+        rig.connect();
+        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+        rig.push({0x90, 0x3C, 0x7F});
+        rig.push({0x90, 0x43, 0x7F});
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 2;
+        }));
+        const std::uint64_t mark = rig.cart.pushMark();
+        rig.push({0x90, 0x3C, 0x7F});
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 3;
+        }));
+        rig.cart.releaseNotes(heldNotes(), mark);
+        REQUIRE(waitUntil([&] {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            return rig.dev.applied.size() == 4;
+        }));
+        std::this_thread::sleep_for(std::chrono::milliseconds(30));
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        REQUIRE(rig.dev.applied.size() == 4);  // and the second note for key 60 never comes
+        CHECK(rig.dev.applied[3] == Bytes{0x81, 0x43, 0x00});
+        for (const Bytes& m : rig.dev.applied) {
+            const bool cutsKey60 = (m[0] & 0xF0) == 0x80 && m[1] == 0x3C;
+            CHECK_FALSE(cutsKey60);
+        }
+    }
+}
diff --git a/packages/native/test/engine-graph/N8Adapters.test.cpp b/packages/native/test/engine-graph/N8Adapters.test.cpp
index c6ca8ec49..2a976ddaa 100644
--- a/packages/native/test/engine-graph/N8Adapters.test.cpp
+++ b/packages/native/test/engine-graph/N8Adapters.test.cpp
@@ -69,6 +69,15 @@ Engine::GraphRequest withCart(std::vector<DocSystem>& systems) {
                                                            docWire("midi-in.midi", "cart.midi")}}),
                         {1});
 }
+// The same document with the cart node under another id: the node taking over across a replan, so the one it replaces
+// is destroyed later, at reclaim, while the new one is already pushing.
+Engine::GraphRequest withCartNode(std::vector<DocSystem>& systems, const std::string& id) {
+    return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in"),
+                                                           docNode(id.c_str(), "rp.n8.nes")},
+                                                          {docWire("midi-in.midi", "sys-1.midi"),
+                                                           docWire("midi-in.midi", id + ".midi")}}),
+                        {1});
+}
 Engine::GraphRequest withoutCart(std::vector<DocSystem>& systems) {
     return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in")},
                                                           {docWire("midi-in.midi", "sys-1.midi")}}),
@@ -106,6 +115,13 @@ std::ptrdiff_t logIndex(FakeN8& dev, const std::string& entry) {
     return std::find(dev.log.begin(), dev.log.end(), entry) - dev.log.begin();
 }
 
+// Where `entry` last appears in the cart's log, or -1 when it never does.
+std::ptrdiff_t lastLogIndex(FakeN8& dev, const std::string& entry) {
+    std::lock_guard<std::mutex> l(dev.mu);
+    const auto it = std::find(dev.log.rbegin(), dev.log.rend(), entry);
+    return it == dev.log.rend() ? -1 : static_cast<std::ptrdiff_t>(dev.log.rend() - it - 1);
+}
+
 // The provider a case started, stopped as a product stops it: the engine's plans first, while the link is installed,
 // then the provider. A holder, so a REQUIRE that fails mid-case still stops it, in that order, before the FakeN8 whose
 // port its serial thread polls goes out of scope.
@@ -155,6 +171,12 @@ TEST_CASE("the cart's production adapters behind a real Engine: a note, then the
         host.connect(true);
         REQUIRE(host.link().isConnected());
         host.link().setLookaheadMs(0);
+        {
+            // A game, not the menu: the note this case stages first has to reach the cart, and a menu is where a
+            // streamed message does not go.
+            std::lock_guard<std::mutex> l(dev.mu);
+            dev.menu = false;
+        }
 
         // A note staged at the engine reaches the cart's FIFO, through the node, N8CartLink and N8Link.
         e.stageMidi(0, Bytes{0x90, 60, 100});
@@ -234,6 +256,34 @@ TEST_CASE("the cart's production adapters behind a real Engine: a note, then the
             REQUIRE(host.link().isConnected());
             provider.stop();
         }
+        SECTION("a cart node taking over keeps the key it struck, though the one it replaced held it") {
+            // The audio thread owns the engine, as it does in the plugin: a publish is adopted by a block, not by
+            // setGraph, and the plan it replaced stays alive until a reclaim. So the node taking over pushes its first
+            // block BEFORE the node it replaced is destroyed, which is the handover this case guards. (Quiescent, the
+            // publish itself would free the replaced plan, and the release would go out before the new node pushed -
+            // correct, but not the timing that cuts a key.)
+            struct Owned {
+                Engine& e;
+                explicit Owned(Engine& engine) : e(engine) { e.setAudioThreadOwns(true); }
+                ~Owned() { e.setAudioThreadOwns(false); }
+                Owned(const Owned&)            = delete;
+                Owned& operator=(const Owned&) = delete;
+            } owned(e);
+            REQUIRE(e.setGraph(withCartNode(systems, "cart-2")).ok);
+            e.stageMidi(0, Bytes{0x90, 60, 100});
+            e.drainCommands();  // the audio thread's drain: the swap is adopted and the staged note pushed
+            block(e);
+            REQUIRE(waitUntil([&] { return dev.count("fifo:90 3C 64") == 2; }));
+            // The replaced plan, and the node still holding key 60, are freed here, with the new node's push already
+            // among what the link has taken.
+            e.reclaim();
+            std::this_thread::sleep_for(std::chrono::milliseconds(30));
+            const std::ptrdiff_t off = lastLogIndex(dev, "fifo:80 3C 00");
+            const std::ptrdiff_t on  = lastLogIndex(dev, "fifo:90 3C 64");
+            CHECK(on >= 0);
+            CHECK(off < on);  // what the cart last heard about key 60 is the new node's note-on
+            provider.stop();
+        }
         SECTION("a host's teardown with a note held: the plans first, then the links") {
             // A product's order: the plans first, while the link is installed, so the node's destroy hands the note
             // it holds (60) to the link and releases its handle there; then the providers. The note-off reaches the
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[cart-release]"
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-engine-graph-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-engine-graph-test "[n8-adapters]"
```

Expected: FAIL, for the reason the task fixes: the leaving node's release goes out after the new node's note-on and
cuts it (raw and framed), and at the engine level the last thing the cart heard about key 60 is that note-off.

```
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1145: FAILED:
  CHECK( rig.dev.count("fifo:80 3C 00") == 0 )
with expansion:
  1 == 0
...
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1162: FAILED:
  CHECK( lastLogIndex(rig.dev, "fifo:80 3C 00") < lastLogIndex(rig.dev, "fifo:90 3C 7F") )
with expansion:
  3 < 2
...
test cases:  2 |  1 passed | 1 failed
assertions: 60 | 56 passed | 4 failed
```

The fourth failure is the SysEx section, which is the case for the design's second claim:

```
a handover releases only the notes the leaving node left, not a key the new one
struck
  raw: a release waits for a SysEx the bytes port split across chunks
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1165
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1174: FAILED:
  CHECK( rig.dev.count("fifo:80 3C 00") == 0 )
with expansion:
  1 == 0
```

And at the engine level:

```
/workspaces/RetroPlug-main/cuttlefish/packages/native/test/engine-graph/N8Adapters.test.cpp:284: FAILED:
  CHECK( off < on )
with expansion:
  3 < 2
test cases: 1 | 0 passed | 1 failed
assertions: 56 | 55 passed | 1 failed
```

- [ ] **Step 3: implement it**

The adapter forwards the mark, `N8Link` waits behind `max(releaseAfter_, mark)` and skips a key whose last written
note-on is at or past it, the serial thread keeps that per-key index in `writeChunk` (which `drainForJob` uses too), and
the node records the mark after each of its pushes and hands it over from its destroy.

Two things ride the same step, because they are the same batch of release bytes being written. The mark a batch is
judged against is copied under `jobMutex_` beside the notes it belongs to: `releaseNotes` on the control thread can move
the field while the loop writes, and a data race there would let a newer mark decide about an older batch, so the
skipped key of a second release could be applied to the first. And the batch only goes at a message boundary: the serial
thread follows the shape of the bytes it has actually written (a status's outstanding data bytes, and an open SysEx until
its F7) and holds the release while a message is unfinished, bounded at 250 ms so a stream that died mid-SysEx still
releases. The framed path needs none of that - the sender holds a split SysEx and frames whole messages, so the bytes the
cart parses never leave a message open - and the tracker feeds what each path really writes, raw bytes or frames.

````patch
diff --git a/packages/n8/native/src/n8/N8Node.cpp b/packages/n8/native/src/n8/N8Node.cpp
index af73f1eef..416e58020 100644
--- a/packages/n8/native/src/n8/N8Node.cpp
+++ b/packages/n8/native/src/n8/N8Node.cpp
@@ -68,6 +68,10 @@ struct N8Nes {
     // note-off lost there would leave the console holding the note for good. What is still sounding when the node
     // leaves the plan is handed to the link (the destructor).
     CartNotes sounding, owed;
+    // The link's push count after this node's last push (CartLink::pushMark). Its destroy hands it over with the notes
+    // it still holds: a node taking over has pushed since the plan swap, and a release waiting behind everything pushed
+    // would cut a key it struck.
+    std::uint64_t mark = 0;
 
     void bind_host(const rp_host* h) noexcept {
         devices = static_cast<const rp_host_devices*>(h->get_interface(h, RP_HOST_DEVICES_INTERFACE));
@@ -91,7 +95,7 @@ struct N8Nes {
         // turned off, the NES removed, the drive moving, a project closed while the DAW plays) would sound until a
         // power-cycle. Not pushed here: this runs on the control thread after the swap, and the audio thread, or the
         // node taking over (whose books start empty), may be pushing at the ring now.
-        if (sounding.any()) cart->releaseNotes(sounding);
+        if (sounding.any()) cart->releaseNotes(sounding, mark);
         // A job this node submitted that is still running fails now, and the link reports into its record no more, so
         // the engine may free the record; the device stops at its next safe point.
         cart->abandonJobs(this);
@@ -146,7 +150,9 @@ struct N8Nes {
         }
         for (const rp_message& m : inputs.bytes.messages()) {
             const auto data = inputs.bytes.data(m);
-            if (!data.empty()) cart->push(m.frame, data.data(), data.size(), (m.flags & RP_MESSAGE_FLUSH) != 0, rate);
+            if (data.empty()) continue;
+            cart->push(m.frame, data.data(), data.size(), (m.flags & RP_MESSAGE_FLUSH) != 0, rate);
+            mark = cart->pushMark();
         }
     }
 
@@ -166,6 +172,7 @@ struct N8Nes {
     // books never read a count that another thread moves.
     void pushMidi(std::uint32_t frame, const std::uint8_t* data, std::size_t size, double rate) noexcept {
         const bool      sent = cart->push(frame, data, size, false, rate);
+        mark                 = cart->pushMark();
         const NoteEvent note = noteEvent(data, size);
         if (note.on && sent) {
             sounding.set(note.index);
@@ -188,6 +195,7 @@ struct N8Nes {
             const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (note / 128)),
                                          static_cast<std::uint8_t>(note % 128), 0};
             if (!cart->push(0, off, sizeof off, false, rate)) return;
+            mark = cart->pushMark();
             owed.reset(note);
             sounding.reset(note);
         }
diff --git a/packages/n8/native/src/n8/desktop/N8CartLink.hpp b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
index 62252ffbe..e1c115a44 100644
--- a/packages/n8/native/src/n8/desktop/N8CartLink.hpp
+++ b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
@@ -35,6 +35,8 @@ public:
         return false;
     }
     bool          connected() const override { return host_.link().isConnected(); }
+    // [audio] N8Link::chunksPushed: the queue's own count, which the node reads after each of its pushes.
+    std::uint64_t pushMark() const override { return host_.link().chunksPushed(); }
     // Every push of this adapter that lost its bytes, plus the chunks a stream stopped after a job found in its ring
     // (N8Link::chunksDiscarded: pushes that reached the ring after the pause's drain, whose caller the link cannot
     // tell). A node keeps its own books on push()'s answer; this is the count for a caller that reads the link.
@@ -82,6 +84,14 @@ public:
             if (notes.test(i)) set.set(i);
         host_.link().releaseNotes(set);
     }
+    // [control] The same, waiting behind the mark the leaving node recorded: a key the node taking over struck since is
+    // its own, and its note-off would cut it.
+    void releaseNotes(const rp::n8::CartNotes& notes, std::uint64_t mark) override {
+        N8Link::NoteSet set;
+        for (std::size_t i = 0; i < notes.size(); ++i)
+            if (notes.test(i)) set.set(i);
+        host_.link().releaseNotes(set, mark);
+    }
 
 private:
     N8Host&                    host_;
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
index 87a08c0f8..db3b419cb 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -7,6 +7,8 @@
 #include <cstring>
 #include <stdexcept>
 
+#include "n8/CartLink.hpp"  // NoteEvent: the release tracker's books
+
 namespace retroplug {
 
 namespace {
@@ -22,6 +24,9 @@ std::int64_t nowNs() {
 // How long a cart may say nothing while a frame is outstanding before it is taken for one acknowledging nothing: the
 // pause's bounded wait for acks, and the silence after which a config read may go ahead of an outstanding frame.
 constexpr std::int64_t kAckWaitNs = 250'000'000;
+// How long a message the cart's parser is still inside may hold a leaving node's release: long enough for the next
+// chunk of a SysEx the bytes port split, short enough that a stream that died mid-message still releases its notes.
+constexpr std::int64_t kMidMessageNs = 250'000'000;
 }  // namespace
 
 N8Link::N8Link(PortFactory factory) : factory_(std::move(factory)) {}
@@ -93,6 +98,7 @@ bool N8Link::connect(const std::string& port) {
     // connection, then start the consumer thread BEFORE allowing producers.
     { TimedChunk drop; while (ring_.tryPop(drop)) {} }
     chunksWritten_ = chunksPushed_.load(std::memory_order_acquire);  // the ring is empty: nothing pushed is still to go
+    std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);     // a fresh connection's cart holds no note of ours
     bytesForwarded_.store(0, std::memory_order_relaxed);
     controlRetryAtNs_ = 0;  // a prior connection's failed control write does not hold this one's first turn back
     backChannelQuiet_.store(true, std::memory_order_relaxed);
@@ -142,30 +148,47 @@ bool N8Link::push(std::uint32_t sampleOffset, const std::uint8_t* data, std::siz
 }
 
 void N8Link::releaseNotes(const NoteSet& notes) {
+    releaseNotes(notes, chunksPushed_.load(std::memory_order_acquire));
+}
+
+void N8Link::releaseNotes(const NoteSet& notes, std::uint64_t mark) {
     if (notes.none()) return;
     std::lock_guard<std::mutex> l(jobMutex_);
     if (!acceptingJobs_) return;  // no serial thread to write them: the link is down
     releasePending_ |= notes;
-    // Everything pushed so far, which the caller's own pushes are among: a cart node's destroy runs after the plan swap
-    // that ordered its last block before it.
-    releaseAfter_ = chunksPushed_.load(std::memory_order_acquire);
+    // Behind the caller's own last push (the mark it recorded), not behind every push so far: a cart node's destroy
+    // runs after the plan swap, so the node taking over has already pushed, and a release that waited for those would
+    // go out behind a note-on it has to precede. Several leaving nodes' releases take the latest mark: a note-on
+    // between two of their marks belongs to a node that is leaving too.
+    releaseAfter_ = std::max(releaseAfter_, mark);
     releaseWanted_.store(true, std::memory_order_release);
 }
 
 void N8Link::writeReleases(bool stopping) {
     if (!releaseWanted_.load(std::memory_order_acquire)) return;
-    NoteSet notes;
+    // Between two messages only: a note-off written inside one the bytes port split across chunks is not a message the
+    // cart's parser sees (a SysEx above all - a note-off inside an F0..F7 run is payload). A stream left mid-message
+    // holds it only until kMidMessageNs, so a node that dies mid-SysEx still gets its notes released.
+    if (!stopping && !atMessageBoundary(nowNs())) return;
+    NoteSet      notes;
+    std::uint64_t after = 0;
     {
         std::lock_guard<std::mutex> l(jobMutex_);
         // A chunk pushed before the release is still to go (a note-on waiting for its release time would otherwise
         // follow its own note-off and sound for good). A stopping link writes no more chunks, so it does not wait.
         if (!stopping && chunksWritten_ < releaseAfter_) return;
         notes = releasePending_;
+        // The mark THIS batch was taken at, copied under the lock that writes it: releaseNotes on the control thread
+        // can move the field while the loop below runs, and a newer mark must not decide about an older batch.
+        after = releaseAfter_;
         releasePending_.reset();
         releaseWanted_.store(false, std::memory_order_relaxed);
     }
     for (std::size_t i = 0; i < notes.size(); ++i) {
         if (!notes.test(i)) continue;
+        // A key whose last note-on went out at or past the mark is the node that took over's, and a note-off here would
+        // cut it. (A key it struck and released again is kNoChunk, so this is only ever the safe direction.)
+        if (lastOn_[i] != kNoChunk && lastOn_[i] >= after) continue;
         TimedChunk off;
         off.len     = 3;
         off.data[0] = static_cast<std::uint8_t>(0x80 | (i / 128));
@@ -377,6 +400,15 @@ void N8Link::streamLoop() {
 }
 
 void N8Link::writeChunk(const TimedChunk& chunk) {
+    // The tracker's books, before the bytes go: this chunk's index is chunksWritten_, which the caller bumps after this
+    // returns. A note-on is what a handover's release must not cut; a note-off leaves the key unheld. Only the first
+    // message of a chunk is read (noteEvent), which is every message a node pushes: pushMidi sends one at a time. A
+    // multi-message push is the BYTES port's, and its notes are not in this table, so a handover through that port
+    // cannot tell the new node's key from the old one's.
+    const rp::n8::NoteEvent note = rp::n8::noteEvent(chunk.data, chunk.len);
+    if (note.on) lastOn_[note.index] = chunksWritten_;
+    else if (note.off) lastOn_[note.index] = kNoChunk;
+    const std::int64_t now = nowNs();
     if (rel_) {
         std::vector<std::uint8_t> out;
         std::vector<int> seqs;
@@ -384,14 +416,64 @@ void N8Link::writeChunk(const TimedChunk& chunk) {
             if (seq >= 0) seqs.push_back(seq);
             out.insert(out.end(), d, d + k);
         });
-        writeFramed(out, seqs, nowNs(), false);
+        // What the cart's parser sees on this path is frames, which never leave a message open: the sender holds a
+        // split SysEx until its F7 and frames whole messages (transport/N8ReliableLink.hpp).
+        noteWireBytes(out.data(), out.size(), now);
+        writeFramed(out, seqs, now, false);
     } else {
-        if (writeLog_) writeLog_(chunk.data, chunk.len, nowNs(), false);
+        noteWireBytes(chunk.data, chunk.len, now);
+        if (writeLog_) writeLog_(chunk.data, chunk.len, now, false);
         check(edio_->fifoWR(chunk.data, chunk.len));
         bytesForwarded_.fetch_add(chunk.len, std::memory_order_relaxed);
     }
 }
 
+void N8Link::noteWireBytes(const std::uint8_t* data, std::size_t n, std::int64_t now) {
+    for (std::size_t i = 0; i < n; ++i) {
+        const std::uint8_t b = data[i];
+        if (wireSysex_) {
+            if (b == 0xF7) wireSysex_ = false;
+            continue;
+        }
+        if (wireExpect_ > 0 && b < 0x80) {
+            --wireExpect_;
+            continue;
+        }
+        if (b < 0x80) continue;  // data with no status behind it: nothing of ours is open
+        if (b == 0xF0) {
+            wireSysex_ = true;
+            continue;
+        }
+        wireExpect_ = wireDataBytes(b);
+    }
+    const bool open = wireSysex_ || wireExpect_ > 0;
+    if (!open) wireOpenSinceNs_ = 0;
+    else if (wireOpenSinceNs_ == 0) wireOpenSinceNs_ = now;
+}
+
+// One complete MIDI message's data bytes, from its status: 2 for most channel messages, 1 for programme and channel
+// pressure, and 0 for the System Common ones that carry none (a real-time byte is one on its own, and F0 is the SysEx
+// open this tracker follows instead).
+std::size_t N8Link::wireDataBytes(std::uint8_t status) {
+    switch (status & 0xF0) {
+        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
+        case 0xC0: case 0xD0: return 1;
+        default: break;
+    }
+    switch (status) {
+        case 0xF1: case 0xF3: return 1;
+        case 0xF2: return 2;
+        default: return 0;
+    }
+}
+
+bool N8Link::atMessageBoundary(std::int64_t now) const {
+    if (!wireSysex_ && wireExpect_ == 0) return true;
+    // A stream left open this long is one nothing is going to finish (a node that died mid-SysEx): its notes are
+    // released anyway, since a tone held on the console is the worse of the two.
+    return wireOpenSinceNs_ != 0 && now - wireOpenSinceNs_ >= kMidMessageNs;
+}
+
 void N8Link::drainForJob(TimedChunk& chunk, bool& have) {
     // What was pushed before the pause goes out first, each chunk at its own release time: at most the lookahead and a
     // block away, and a note-off among it must reach the cart.
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
index d26485067..e00128147 100644
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -123,6 +123,12 @@ public:
     // leaving the plan releases what it held: its destroy runs on the control thread, which may not push into the audio
     // thread's ring.
     void          releaseNotes(const NoteSet& notes);
+    // [control] As releaseNotes, waiting behind `mark` instead of behind every push so far: what a cart node hands over
+    // from its destroy, which runs after the plan swap and so after the node taking over has pushed. A key whose last
+    // note-on was written at or past the mark is the new node's, and is skipped rather than cut.
+    void          releaseNotes(const NoteSet& notes, std::uint64_t mark);
+    // [audio] Every chunk push() has put in the ring since the connect: the mark a node records after its own push.
+    std::uint64_t chunksPushed() const { return chunksPushed_.load(std::memory_order_acquire); }
     bool          connect(const std::string& port);  // open + handshake + spawn the serial thread
     // Stop + join the serial thread, close the port. With a paused job running the join waits for the job's current
     // step to end (see PausedJob): a caller on a UI thread (Settings > N8's Connect and port rows) should not offer it
@@ -183,6 +189,11 @@ private:
     void streamLoop();                                   // serial thread: the probe, then stream until stopped
     void writeChunk(const TimedChunk& chunk);            // serial thread: one chunk out, framed when reliable
     void writeReleases(bool stopping);                   // serial thread: releaseNotes' note-offs, once they may go
+    // serial thread: where the BYTE stream the cart is reading stands, so a release never goes between a status and its
+    // data (see atMessageBoundary). wireDataBytes is one message's data bytes from its status.
+    void        noteWireBytes(const std::uint8_t* data, std::size_t n, std::int64_t now);
+    static std::size_t wireDataBytes(std::uint8_t status);
+    bool        atMessageBoundary(std::int64_t now) const;
     void runJob(TimedChunk& chunk, bool& have);          // serial thread: pause, run the pending job, resume
     void drainForJob(TimedChunk& chunk, bool& have);     // serial thread: what was pushed before the pause goes out
     void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
@@ -216,6 +227,17 @@ private:
     // read when it was asked, so it goes out behind everything pushed before it.
     std::atomic<std::uint64_t> chunksPushed_{0};
     std::uint64_t              chunksWritten_ = 0;
+    // Per key (channel * 128 + note), the index of the chunk whose note-on the serial thread last wrote, or kNoChunk:
+    // cleared by a note-off it writes. It is what tells a release for a key the node taking over struck (at or past the
+    // mark it waits behind) from one for a key nothing holds any more. Serial thread only, reset at each connect.
+    static constexpr std::uint64_t kNoChunk = ~std::uint64_t{0};
+    static constexpr std::size_t   kNoteKeys = 16 * 128;
+    std::uint64_t              lastOn_[kNoteKeys] = {};
+    // The shape of the byte stream the cart has been sent (noteWireBytes): the data bytes still owed by the message
+    // open, whether a SysEx is one of them, and when that began (0: at a message boundary). Serial thread only.
+    int                        wireExpect_    = 0;
+    bool                       wireSysex_     = false;
+    std::int64_t               wireOpenSinceNs_ = 0;
     // The earliest moment OnControl runs again after a throw (see the loop in streamLoop), and a wake a caller asked
     // for since the last quiet turn: the first is serial-thread state, the second is stored from any thread.
     std::int64_t               controlRetryAtNs_ = 0;
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[cart-release]"
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-engine-graph-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-engine-graph-test "[n8-adapters]"
```

Expected: PASS both:

```
All tests passed (66 assertions in 2 test cases)
All tests passed (56 assertions in 1 test case)
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 600 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test
timeout -k 10 900 .superpowers/kb4plan/guarded.sh build/bin/retroplug-engine-graph-test
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-device-mode-test -j$(nproc)
timeout -k 10 600 .superpowers/kb4plan/guarded.sh build/bin/retroplug-device-mode-test
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8
```

Expected: PASS, the two whole binaries, the device-mode binary and the SDK's n8 files:

```
All tests passed (1566 assertions in 113 test cases)
test cases:     342 |     331 passed | 11 skipped
assertions: 1702522 | 1702522 passed |  0 skipped
```

```
All tests passed (1504408 assertions in 96 test cases)
# 3 native test file(s) passed (jobs=16)
```

`retroplug-device-mode-test` is here because this step's interface is a virtual `CartLink` gains: that binary compiles
the desktop `pico/n8-host/BoxCartLink.cpp` and the device-mode `N8Node.cpp` against it, which no other target does
(`packages/native/cmake/tests.cmake`).

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8/native/src/n8/CartLink.hpp \
  packages/n8/native/src/n8/N8Node.cpp packages/n8/native/src/n8/desktop/N8CartLink.hpp \
  packages/n8/native/src/n8/desktop/N8Link.hpp packages/n8/native/src/n8/desktop/N8Link.cpp \
  packages/n8/native/test/N8CartJobs.test.cpp \
  packages/native/test/engine-graph/N8Adapters.test.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb4): a cart-node handover releases only the notes the leaving node left

A cart node's destroy runs after the plan swap, so the node taking over has
already pushed, and a release that waited behind every push cut a key it struck.
CartLink gains a push mark and a marked releaseNotes, rp.n8.nes records the mark
after each of its pushes and hands it over, and the serial thread keeps the index
of the chunk whose note-on it last wrote per key: a key whose last note-on is at
or past the mark is the new node's and is skipped, so the old node's other notes
are still released and the same holds framed. A release also waits for a message
boundary now, so one cannot land inside a SysEx the bytes port split across
chunks, and the mark a batch is judged against is copied under the lock that
writes it.
MSG
```

---
## Task 5: a cart at its menu is not streamed to

The N8 menu reads its commands over the same FIFO a game reads MIDI from, so a data pair inside a message is one of its
commands (`0x2A 0x73` is `*s`: start an app), and a stream into a menu also keeps a job's menu probe from finding a
quiet turn. Where the cart runs is readable without touching its bus: the FPGA config block's map_idx is 255 at the
menu (`N8Host.cpp`'s `runningMapper` and `kMenuMapper`), a system-region read.

**Files** (line numbers are the file at `ccc01889e`):

- Modify: `packages/n8-wire/native/src/transport/N8ReliableLink.hpp:129-140` (`progressOverdue(now)`, the window-progress
  question, beside the sender's own timeout).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.hpp:54-64` (`MenuCheck`), `:88-100` (`setMenuCheck`), `:150-160`
  (`atMenu`), `:200-215` (the new private methods), `:230-262` (the state).
- Modify: `packages/n8/native/src/n8/desktop/N8Link.cpp:1-30` (the constants), `:88-135` (connect seeds it; push drops
  at the menu; `releaseNotes` refuses there), `:160-190` (`writeReleases` and `serviceLink`'s split), `:250-380` (the
  guard, and `streamLoop`'s turn), `:405-425` (`drainForJob`).
- Modify: `packages/n8/native/src/n8/desktop/N8Host.hpp:28-40` (`N8ConfigDto.atMenu`), `packages/n8/native/src/n8/desktop/N8Host.cpp:79-85`
  (the read's timeout), `:160-230` (the menu check installed beside `OnControl`), `:230-245` (`getConfig`).
- Modify: `packages/n8/native/src/n8/desktop/N8CartLink.hpp:36-40` (`connected`).
- Modify: `packages/n8/ts/index.ts:39-46`, `:186-199` (`N8LinkConfig.atMenu`, read tolerantly);
  `packages/n8/ts/ui/index.ts:50-62` (the status row).
- Test: `packages/n8/native/test/N8CartJobs.test.cpp` (the `[menu-guard]` cases: the headline one, a job under a
  stream into the menu, leaving the menu, the one-second read, and a job taken while the ring holds a push; and the
  rewrites the guard forces; then, in Step 5, the menu-episode `[cart-release]` case with its two sections, the clock
  one, and the chattering-menu `[menu-guard]` one); `packages/n8/native/test/FakeN8.hpp` gains the chatter: one that
  never goes quiet (`chatterMs`), one that lands ahead of the host's next read (`chatterGarbles`, Step 5), and one that
  fails the flush after a boot (`failFlushAtGame`, Step 7) - its `fifoByte` already parses the menu's `*` commands off
  the FIFO and boots on `*s`, and its `peek` already answers map_idx 255 while the menu shows, which is what the red
  cases and the guard both read;
  `packages/n8/native/test/N8CartLink.test.cpp:201-230` (no guard where a case counts the wire);
  `packages/n8/native/test/N8Provider.test.cpp:295-318`; `packages/native/test/engine-graph/N8Adapters.test.cpp` (the
  menu sections); `packages/n8/ts/test/link-client.test.ts:34-62`; `packages/retroplug/test/menu/n8-sd-ops.test.ts:60-90`
  and a new status case; `packages/retroplug/test-ui/ui-services.test.ts:66-80`,
  `packages/retroplug/test/menu/recorded-cases.ts:100-115`, `packages/retroplug/test/services.test.ts:78-95` (the
  fixture configs gain the field).

**Interfaces:**

- Consumes: `N8Host::runningMapper` and `kMenuMapper`, `Edio::timeouts()`, `N8ReliableSender`'s `outstanding` /
  `reset` / `progressOverdue`, `N8Link`'s ring, `jobMutex_`, `chunksDiscarded_`.
- Produces: `N8Link::MenuCheck` and `setMenuCheck`, `N8Link::reportMenuState(bool)` (serial thread; what N8Host's own
  menu probe feeds it), `N8Link::atMenu()`, `N8ConfigDto::atMenu`, `N8LinkConfig.atMenu` (TS). Nothing later consumes
  them.

### Steps

- [ ] **Step 1: write the failing tests**

Five `[menu-guard]` cases in `N8CartJobs.test.cpp`, beside the other cart cases and using the same `Rig`, and they
use only the API that exists before the fix, so they compile and fail on behaviour:

- the headline: a cart at its menu (the fake's default) refuses the push and counts it, nothing boots, the FIFO never
  sees the bytes, and a cart node reads disconnected; a game booted by hand streams again and reads connected; and once
  the guard has read the menu during a stream, what is pushed goes nowhere. It waits out the connect-time link probe
  (300 ms) before pushing, so the failing run shows the cart READING the bytes - the menu boots on the `0x2A 0x73`
  inside the message - rather than the test outrunning the link;
- leaving the menu re-probes: connect at the menu with a ROM that speaks the link, start a game by hand, and the frames
  begin (today the link stays raw for the connection's life);
- a job under a 200-message-a-second stream into the menu: a framed link whose game reset keeps a stale window that is
  re-sent and re-stamped every turn, so no turn was ever quiet for the probe; the job must be accepted promptly once the
  guard has the menu;
- a menu that never goes quiet: a menu that chatters on the back-channel keeps the quiet rule fresh, so only the hold's
  one-second bound can end it - the trip is awaited, its `linkResends() == 0` is asserted there (the frame was held on
  every turn of the wait, not re-sent into the menu), and then the FIFO writes are counted before and after another
  300 ms, which a re-send would add to (the fake's `frameSeqs` cannot say it: its menu branch returns before it reads a
  frame);
- a job taken while the ring holds a push, right after the console resets: the probe's own read is the guard's chance
  to see the reset, and nothing may be written into the menu after it.

`Rig` gains `connectGame()` (a game before the connect, which is where the guard seeds its state) in this step, and
`FakeN8` gains `chatterMs`.

````patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -52,6 +52,7 @@
     bool                     linkDeaf = false;      // ... until it stops answering (it read no frame and acks nothing)
     int                      linkAckDelayMs = 0;    // how long the game takes to write a frame's ack back
     bool                     linkDamaged = false;   // every frame reaches the game damaged, and it says so each time
+    int                      chatterMs = 0;          // writes a byte on the back-channel this often (0: never)
     int                      refuseExpVolWrites = 0; // the next N expansion-volume writes fail in the port
     int                      expVolAttempts = 0;    // every expansion-volume write the host made
     int                      fileDelayMs = 0;       // likewise each SD file write
@@ -127,6 +128,16 @@
         std::lock_guard<std::mutex> l(dev_.mu);
         failure_.clear();
         releaseAcks(true);  // whatever the game has had time to write since the last read
+        // A menu that never goes quiet: one byte whenever the cart owes the host no reply, which the link reads as it
+        // having said something (its quiet rule stamps the last heard on ANY byte). Only between replies, so a config
+        // read's answer is never garbled by one.
+        if (dev_.chatterMs > 0 && out_.empty()) {
+            const auto now = std::chrono::steady_clock::now();
+            if (now >= chatterDue_) {
+                out_.push_back(0xB0);  // a status byte the ack parser skips
+                chatterDue_ = now + std::chrono::milliseconds(dev_.chatterMs);
+            }
+        }
         std::size_t i = 0;
         while (i < n && !out_.empty()) {
             b[i++] = out_.front();
@@ -452,6 +463,7 @@
     std::size_t              fileLeft_ = 0, blockLeft_ = 0;
     Bytes                    menu_;
     std::deque<PendingAck>   acks_;  // what the game owes the link, in the order it will write it
+    std::chrono::steady_clock::time_point chatterDue_{};  // the next unsolicited back-channel byte
     std::string              menuPath_;
     enum { Idle, WantSync, WantSeq, InMsg, WantSum } link_ = Idle;
     std::uint8_t             next_ = 0, seq_ = 0;
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -84,6 +84,15 @@
         REQUIRE(host.link().isConnected());
         host.link().setLookaheadMs(0);
     }
+    // Connect on a cart running a game (the fake's config block then names mapper 0): the menu reads the FIFO as its
+    // own commands, so a streamed message goes nowhere there and a case that pushes MIDI while it streams starts here.
+    void connectGame() {
+        {
+            std::lock_guard<std::mutex> l(dev.mu);
+            dev.menu = false;
+        }
+        connect();
+    }
     bool submit(const JobRequest& r, Job& job, std::string* error = nullptr) {
         // As Engine::submitDeviceJob hands it over: the request as C, the record as its rp_job handle, and the report
         // interface over that record (FakeJobs.hpp's, a host's own, as the engine's is its own).
@@ -1210,3 +1219,176 @@
         }
     }
 }
+
+TEST_CASE("a cart at its menu is not streamed to: its FIFO is a command parser there", "[n8][menu-guard]") {
+    // The N8 menu reads the same FIFO a game reads MIDI from, so a data pair inside a message - 0x2A 0x73, say - is its
+    // '*s': start an app. The guard reads where the cart runs from the FPGA config block (a system-region read: no bus,
+    // no FIFO), and drops rather than streams while that says menu.
+    Rig rig;
+    rig.connect();  // at its menu: the fake's default
+    // The connect-time link probe holds the serial thread for up to 100 ms, and a push waits in the ring behind it:
+    // wait it out, so a failing run shows the cart reading the bytes rather than the test outrunning the link.
+    std::this_thread::sleep_for(std::chrono::milliseconds(300));
+    const Bytes on{0x90, 0x2A, 0x73};
+    CHECK_FALSE(rig.cart.push(0, on.data(), on.size(), false, 48000.0));
+    std::this_thread::sleep_for(std::chrono::milliseconds(50));
+    CHECK(rig.dev.menu);  // nothing booted it
+    CHECK(rig.dev.booted.empty());
+    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
+    CHECK_FALSE(rig.cart.connected());  // attached, streaming nothing: what a cart node reads
+
+    // A game booted by hand is streamed to, and reads connected again.
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = false;  // the user started a game from the cart's own browser
+    }
+    REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
+    rig.push({0x90, 0x2A, 0x73});
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 2A 73") == 1; }));
+    // And once the guard has read the menu during a stream, what is pushed goes nowhere and is counted: the cart reads
+    // disconnected the moment the read lands, which is how this waits for it.
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = true;
+    }
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
+    const std::uint64_t dropped = rig.cart.dropped();
+    rig.push({0x90, 0x2A, 0x73});
+    CHECK(rig.cart.dropped() == dropped + 1);
+    std::this_thread::sleep_for(std::chrono::milliseconds(30));
+    CHECK(rig.dev.count("fifo:90 2A 73") == 1);
+}
+
+TEST_CASE("a job submitted while the ring holds a push, right after the console resets to its menu, writes nothing "
+          "into the menu",
+          "[n8][menu-guard]") {
+    // The job's own menu probe reads the config block, and that read is the guard's chance to see the reset: it must
+    // trip before the pause delivers what the ring still holds, or the menu reads a note-on as its own commands.
+    Rig rig;
+    rig.connectGame();
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = true;  // the console's reset button, a moment before the submission
+        rig.dev.files[kGameBattery] = saveOf(1024, 13);
+    }
+    // Still in the ring when the job is taken: a lookahead this long is what the pause's drain would deliver first.
+    rig.host.link().setLookaheadMs(200);
+    rig.push({0x91, 0x41, 0x7F});
+    Job job;
+    REQUIRE(rig.submit(battery("dump"), job));
+    REQUIRE(waitUntil([&] { return job.finished(); }));
+    CHECK(job.state.load() == Job::Done);
+    CHECK(job.result == saveOf(1024, 13));
+    std::this_thread::sleep_for(std::chrono::milliseconds(30));
+    CHECK(rig.dev.count("fifo:91 41 7F") == 0);  // the ring it held went nowhere
+}
+
+TEST_CASE("leaving the menu re-probes the link, so a game the user starts by hand streams framed", "[n8][menu-guard]") {
+    Rig rig;
+    rig.dev.linkRom = true;  // BlipToaster, which answers the probe
+    rig.connect();           // at its menu: the probe finds nothing, and the link stays raw
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = false;  // the user started a game from the cart's own browser
+        rig.dev.cfg[0] = 69;
+        rig.dev.cfg[2] = 0;
+    }
+    // The guard's next read sees a game, and leaving the menu runs the link probe again: the frames start.
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+}
+
+TEST_CASE("a job submitted under a stream into the menu is accepted, and the stream is dropped there",
+          "[n8][menu-guard]") {
+    // The design's starvation case: a framed link whose game reset to its menu keeps a stale window that is re-sent and
+    // re-stamped on every turn, so at about 200 messages a second no turn was quiet for the probe. Once the guard has
+    // the menu, nothing is framed, the window is reset and every turn is quiet.
+    Rig rig;
+    rig.dev.menu    = false;
+    rig.dev.linkRom = true;
+    rig.dev.cfg[0]  = 69;
+    rig.dev.cfg[2]  = 0;
+    rig.dev.files[kGameBattery] = saveOf(1024, 9);
+    rig.connect();
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = true;  // the console's reset button: the game is gone, and the link is still framed
+    }
+    std::atomic<bool> streaming{true};
+    std::thread feeder([&] {
+        while (streaming.load(std::memory_order_relaxed)) {
+            rig.push({0x90, 0x3C, 0x7F});
+            std::this_thread::sleep_for(std::chrono::milliseconds(5));  // about 200 messages a second
+        }
+    });
+    Job job;
+    const auto t0       = std::chrono::steady_clock::now();
+    const bool accepted = rig.submit(battery("dump"), job);
+    const auto waited =
+        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
+    streaming.store(false, std::memory_order_relaxed);
+    feeder.join();
+    CHECK(accepted);
+    CHECK(waited < 900);  // a turn was quiet for the read, rather than the probe waiting out its deadline
+    REQUIRE(waitUntil([&] { return job.finished(); }));
+    CHECK(job.state.load() == Job::Done);
+    CHECK(job.result == saveOf(1024, 9));
+    CHECK_FALSE(rig.cart.connected());  // the menu: attached, streaming nothing
+}
+
+TEST_CASE("a menu that never goes quiet is read anyway after about a second", "[n8][menu-guard]") {
+    using Clock = std::chrono::steady_clock;
+    Rig rig;
+    rig.dev.menu    = false;
+    rig.dev.linkRom = true;
+    rig.dev.cfg[0]  = 69;
+    rig.dev.cfg[2]  = 0;
+    rig.connect();
+    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
+    rig.push({0x90, 0x3C, 0x7F});
+    REQUIRE(waitUntil([&] {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        return rig.dev.applied.size() == 1;
+    }));
+    // The console resets to its menu, and the menu chatters on the back-channel: the quiet rule stamps the last heard
+    // on ANY byte, so the turn never goes quiet and only the hold's bound can end it.
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu      = true;
+        rig.dev.chatterMs = 40;
+    }
+    rig.push({0x90, 0x3E, 0x7F});  // an unacknowledged frame: this is what a re-send would take into the menu
+    // A second message pushed while the hold waits for its read, a lookahead out so it is still in the ring (or in
+    // hand) at the trip: the trip drops it, and nothing may write it afterwards. It is what the FIFO count below is for
+    // - a frame the fake's menu branch never reads cannot show it.
+    rig.host.link().setLookaheadMs(1100);
+    rig.push({0x90, 0x41, 0x7F});
+    rig.host.link().setLookaheadMs(0);
+    const auto t0 = Clock::now();
+    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 4000));  // the trip, which is the read landing
+    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
+    CHECK(waited >= 900);    // it waited out the bound rather than reading on a hunch with a frame in flight
+    CHECK(waited < 2500);
+    // The frame was held, not re-sent, on every turn of the wait: the menu would have read a re-send as a command.
+    CHECK(rig.host.link().linkResends() == 0);
+    // And nothing more goes to the cart's FIFO after the trip. The fake's frameSeqs cannot say this (its menu branch
+    // returns before it reads a frame), so this counts what the host WROTE, which a re-send would add to.
+    const std::size_t writes = rig.dev.count("fifo:");
+    std::this_thread::sleep_for(std::chrono::milliseconds(300));
+    CHECK(rig.dev.count("fifo:") == writes);  // nothing more was written after the trip
+    CHECK(rig.dev.menu);
+    CHECK(rig.dev.booted.empty());
+}
````

- [ ] **Step 2: run it, and see it fail**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[menu-guard]"
```

Expected: FAIL, all five, for the reason the task fixes: the push into the menu is accepted and counts nothing, the
cart reads connected there, the menu boots on the bytes it was given, the link never re-probes, and nothing a job or a
stream pushes at a real game after that boot arrives as the game's stream. The headline case's first failures are the
boot itself - `CHECK( rig.dev.menu )` is false because the cart read `0x2A 0x73` as `*s` - which is the design's
headline demonstrated, not assumed:

```
a cart at its menu is not streamed to: its FIFO is a command parser there
-------------------------------------------------------------------------------
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1223
...............................................................................

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1233: FAILED:
  CHECK_FALSE( rig.cart.push(0, on.data(), on.size(), false, 48000.0) )
with expansion:
  !true

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1235: FAILED:
  CHECK( rig.dev.menu )
with expansion:
  false

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1237: FAILED:
  CHECK( rig.dev.count("fifo:90 2A 73") == 0 )
with expansion:
  1 == 0

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1238: FAILED:
  CHECK_FALSE( rig.cart.connected() )
with expansion:
  !true

/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1254: FAILED:
  REQUIRE( waitUntil([&] { return !rig.cart.connected(); }) )
with expansion:
  false
...
===============================================================================
test cases:  5 |  0 passed | 5 failed
assertions: 36 | 27 passed | 9 failed
```

`CHECK( rig.dev.menu )` is false there: the fake read the pushed `0x2A 0x73` as `*s` and booted a game. The four
other cases fail earlier in the same way - the link never re-probes, the menu's stream keeps the FIFO busy, and the
job's pause writes the ring into it.

- [ ] **Step 3: implement it**

`N8Link` keeps the cart's at-menu state, seeded by `N8Host` at connect and re-read between chunks: every 250 ms on a
turn quiet for a read (raw, framed and idle, or already at the menu), and at once when the framed window has STOPPED
MOVING - the oldest frame unacknowledged past the link's own timeout, which is the console reset. That second read is
what the hold is for: the stream's new chunks, its re-sends and its pending releases wait for the answer, because a cart
at its menu would read every one of them as its commands. The wait is bounded at a second, because the quiet rule is
not: a menu that chatters on the back-channel stamps the last heard on any byte, so the hold would never see a quiet
turn. At the bound the input is flushed and the read made anyway. A read that answers "a game" while the window is
stopped is taken at its word for the next second, so a game whose acks are merely damaged keeps its re-sends (one read a
second, not one a turn); a read that does not answer holds the next one off for a second, so a port that keeps timing out
does not cost the stream 100 ms of every 250. At the menu `push` answers false and counts, the ring (each turn, not only
the trip's), the reliable window and any pending release are discarded, and nothing is written; the connect-time probe is
skipped there too (its sync is bytes a menu parses), and leaving the menu re-probes the link. The adapter's `connected`
is `isConnected() && !atMenu()`, the status DTO carries the state, and Settings > N8's row says "At the N8 menu (not
streaming)". A chunk the loop has already taken from the ring is dropped only when the cart is at its menu: during a
pre-read hold it is kept, because the cart may be a game that has merely been quiet for an ack bound, and its note-offs
must still go.

The job probe feeds the guard: `N8Host`'s `OnControl` reads the same config block for a submission, and reports the
menu answer through `reportMenuState(true)`, so the guard has the menu before the pause's drain can write the ring
into it. Only a menu answer: the probe's read is not flushed first, as the guard's own are, so a chattering cart's byte
could make a menu read as a game, and only the guard's flushed read may re-enable the stream (re-check 2's G2). (A
link with no `MenuCheck` has no guard, and its probe answers feed nothing.)

One timestamp per turn: the guard judges `menuHold(turnNs, quiet)` and the re-sends it may hold are polled at that same
`turnNs` (`serviceLinkResends(turnNs)`), not at a clock read of their own. The guard's threshold and the poll's timeout
are the same instant for a frame nothing re-sent, so a second reading would let a turn just short of the threshold
re-send on the far side of it, into the menu.

The same patch rewrites the cases the guard forces: a case that streams MIDI now connects to a game first
(`connectGame()`), an assertion of `rig.cart.connected()` at the menu is now false, the dump case's notes go out before
the console goes to its menu, the reset case asserts the stale window is discarded rather than re-sent (`linkOutstanding() == 0`,
`linkResends() == 0`, and the one frame the game read never written again), and the fake's
"a game's link with the map saying menu" combination - which the guard correctly refuses - is exercised with the guard
off in the pause and sequence cases, which are about the pause, not the menu (that is the only way the pause's sequence
machinery is reachable now). The probe's own wait is restored as its own section there, guard off, since the case that
used to cover it is now the menu's; and the link that never goes quiet is a game whose acks keep arriving, which is what
"never quiet" means now that a stalled window is held.

````patch
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -142,19 +142,27 @@
 
 }  // namespace
 
-TEST_CASE("a dump job pauses streaming on the port it has, delivers what was pushed first, reads the battery file "
-          "with progress, and resumes before it completes", "[n8][cart-jobs]") {
+TEST_CASE("a dump job pauses streaming on the port it has, reads the battery file with progress, and resumes before "
+          "it completes", "[n8][cart-jobs]") {
     Rig rig;
     const Bytes save = saveOf(3000, 7);
     rig.dev.files[kGameBattery] = save;
     rig.dev.fileReadDelayMs = 20;
-    rig.connect();
+    rig.connectGame();
 
     // A note pushed 20 ms ahead and then a job at once: the note goes out at its own time, before the first read.
     rig.host.link().setLookaheadMs(20);
     rig.push({0x90, 0x3C, 0x7F});
     rig.push({0x80, 0x3C, 0x00});
     rig.host.link().setLookaheadMs(0);
+    // Both notes are out before the console goes to its menu, which is the only place the SD card is reachable: the
+    // guard reads that state between chunks, and from then on the stream is dropped rather than read as commands.
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.menu = true;
+    }
+    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
     Job job;
     REQUIRE(rig.submit(battery("dump"), job));
     REQUIRE(rig.host.cartJobBusy());
@@ -170,8 +178,10 @@
     CHECK(job.result == save);
     CHECK(job.total.load() == save.size());     // a block a step, in bytes
     CHECK(job.progress.load() == save.size());
-    // Completed after streaming resumed, on the one port the link opened.
-    CHECK(rig.cart.connected());
+    // Completed after streaming resumed, on the one port the link opened. The cart reads disconnected at its menu: what
+    // a cart node shows there, since the FIFO is a command parser, not a MIDI stream.
+    CHECK(rig.host.link().isConnected());
+    CHECK_FALSE(rig.cart.connected());
     CHECK_FALSE(rig.host.cartJobBusy());
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
@@ -187,9 +197,13 @@
     CHECK(rig.dev.count("file-rd") == (save.size() + 511) / 512);  // one 512-byte block a step
     CHECK(rig.dev.count("srm-") == 0);                              // the cart's own battery: never
     CHECK(rig.dev.count("fifo:90 40") == 0);  // the push the pause dropped never reached the cart
+    CHECK(rig.cart.dropped() == 1);            // ...counted, and nothing else was lost
 
+    // And a message pushed now goes nowhere: the guard has the menu, so the push is dropped and counted.
     rig.push({0x91, 0x41, 0x7F});
-    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:91 41 7F") == 1; }));
+    CHECK(rig.cart.dropped() == 2);  // the pause's push, and this one
+    std::this_thread::sleep_for(std::chrono::milliseconds(30));
+    CHECK(rig.dev.count("fifo:91 41 7F") == 0);
 }
 
 TEST_CASE("a restore job writes the battery file, creating the folder, for the game's next boot", "[n8][cart-jobs]") {
@@ -211,7 +225,8 @@
         CHECK(rig.dev.files["EDN8/gamedata/fresh.nes/bram.srm"] == save);
     }
     CHECK(rig.dev.count("srm-") == 0);
-    CHECK(rig.cart.connected());
+    CHECK(rig.host.link().isConnected());
+    CHECK_FALSE(rig.cart.connected());  // the menu: attached, streaming nothing
 
     // A dump reads back what the restore wrote.
     Job back;
@@ -267,7 +282,9 @@
     REQUIRE(waitUntil([&] { return missing.finished(); }));
     CHECK(missing.state.load() == Job::Failed);
     CHECK(missing.error == "no battery file for never.nes (EDN8/gamedata/never.nes/bram.srm)");
-    CHECK(rig.cart.connected());
+    CHECK(rig.host.link().isConnected());
+    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
+    CHECK_FALSE(rig.cart.connected());  // the menu, once the guard has read it
 }
 
 TEST_CASE("one cart job at a time, none while disconnected, and an operation the cart does not have is refused",
@@ -381,6 +398,9 @@
     // pause), and it is what this case pins: the pause keeps the sequence state.
     rig.dev.cfg[0] = 0xFF;  // map_idx 255: the menu, which the job's own first step checks again
     rig.dev.cfg[2] = 0;
+    // This combination - a game's link with the config block saying the menu - is the fake's, and one the guard
+    // refuses: it seeds the state at connect. The case is the pause's sequence state, so the guard is off.
+    rig.host.link().setMenuCheck({});
     rig.connect();
     REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
 
@@ -407,7 +427,7 @@
     CHECK(rig.host.link().reliable());
 }
 
-TEST_CASE("the menu probe waits out an ack the game still owes, so its config read sees only its reply",
+TEST_CASE("the menu guard reads the cart's state between chunks, so a streamed frame never reaches a menu",
           "[n8][cart-jobs]") {
     using Clock = std::chrono::steady_clock;
     const auto ms = [](Clock::time_point t) {
@@ -435,6 +455,9 @@
         cfgTail(rig.dev);
         rig.dev.linkAckDelayMs = 60;
         rig.dev.files[kGameBattery] = saveOf(1024, 3);
+        // The fake's inconsistent combination - a game's link with the config block saying the menu - is one the guard
+        // refuses, and it seeds that state at connect: off, for this section, which is the probe's own wait.
+        rig.host.link().setMenuCheck({});
         rig.connect();
         REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
 
@@ -456,6 +479,30 @@
         CHECK(rig.dev.applied == std::vector<Bytes>{Bytes{0x90, 0x3C, 0x7F}});
     }
 
+    SECTION("a frame pushed at the menu goes nowhere, and the job's probe still reads it") {
+        Rig rig;
+        rig.dev.linkRom = true;  // a game's link, stale after the console went back to its menu
+        cfgTail(rig.dev);
+        rig.dev.files[kGameBattery] = saveOf(1024, 3);
+        rig.connect();  // at the menu: the guard reads that off the config block before anything may stream
+        // A message pushed now would be read as one of the menu's commands, so it is dropped rather than framed, and
+        // nothing is left outstanding for a re-send to carry into the menu.
+        rig.push({0x90, 0x3C, 0x7F});
+        CHECK(rig.host.link().linkOutstanding() == 0);
+        Job job;
+        const auto t0 = Clock::now();
+        REQUIRE(rig.submit(battery("dump"), job));
+        CHECK(ms(t0) < 1000);  // nothing outstanding to wait out: the menu owes no ack
+        REQUIRE(waitUntil([&] { return job.finished(); }));
+        CHECK(job.state.load() == Job::Done);
+        CHECK(job.result == saveOf(1024, 3));
+        CHECK(rig.host.link().linkOutstanding() == 0);
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        CHECK(rig.dev.frameSeqs.empty());  // no frame was ever written, so none was re-sent
+        CHECK(rig.dev.syncs == 0);
+        CHECK(rig.dev.applied.empty());
+    }
+
     SECTION("the volume's Auto read waits for it too") {
         Rig rig;
         rig.dev.menu = false;
@@ -562,7 +609,8 @@
     const std::uint32_t progressAtAbandon = job.progress.load();
 
     REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
-    CHECK(rig.cart.connected());                      // nothing booted: streaming resumed to the menu, as it was
+    CHECK(rig.host.link().isConnected());             // nothing booted: the link is up, at the menu it resumed to
+    CHECK_FALSE(rig.cart.connected());                // ...which a cart node reads as disconnected
     CHECK(rig.dev.count("file-wr") < 8);              // it stopped early
     CHECK(job.progress.load() == progressAtAbandon);  // and reported nothing more into the record
     {
@@ -606,7 +654,8 @@
     rig.cart.abandonJobs(&rig.owner);
     REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
     CHECK(job.failure.load() == Job::Removed);
-    CHECK(rig.cart.connected());
+    CHECK(rig.host.link().isConnected());
+    CHECK_FALSE(rig.cart.connected());  // the menu the abandoned upload left the cart at
     std::lock_guard<std::mutex> l(rig.dev.mu);
     CHECK(rig.dev.menu);
     CHECK(rig.dev.booted.empty());
@@ -655,7 +704,8 @@
         rig.dev.files[kGameBattery] = saveOf(1024, 11);
     }
     rig.host.connect(true);
-    REQUIRE(rig.cart.connected());
+    REQUIRE(rig.host.link().isConnected());
+    REQUIRE_FALSE(rig.cart.connected());  // the menu: connected reads 0 there
     Job after;
     std::string why;
     CHECK_FALSE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "header"), after, &why));
@@ -683,6 +733,8 @@
         // case for why that combination is the fake's, not a real cart's).
         rig.dev.cfg[0] = 0xFF;
         rig.dev.cfg[2] = 0;
+        // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
+        rig.host.link().setMenuCheck({});
         rig.connect();
         REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
         {
@@ -708,6 +760,8 @@
         rig.dev.linkRom = true;
         rig.dev.cfg[0] = 0xFF;
         rig.dev.cfg[2] = 0;
+        // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
+        rig.host.link().setMenuCheck({});
         rig.connect();
         REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
         {
@@ -744,17 +798,25 @@
         return rig.dev.applied.size() == 1;  // the game read it and has acknowledged it
     }));
 
-    // The console's reset button: the N8 goes back to its menu with USB still up, so the link and its sequence state
-    // stay, and the menu acknowledges nothing (it skips every byte that is not one of its commands). The next frame is
-    // outstanding for good, re-sent whenever the cart says nothing for the link's timeout.
+    // The console's reset button: the N8 goes back to its menu with USB still up, and the menu acknowledges nothing (it
+    // skips every byte that is not one of its commands). The guard reads that between chunks - the frame below is due
+    // to be re-sent, the cart has been silent, so it looks - and the stale window is reset there rather than re-sent
+    // into a command parser.
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
         rig.dev.menu = true;
     }
     rig.push({0x80, 0x3C, 0x00});
-    REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
-    // The probe reads once the cart has been silent for the drain's bound since that frame went out, the menu answers,
-    // and the job runs after the pause's own bounded wait for the same ack.
+    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
+    CHECK(rig.host.link().linkOutstanding() == 0);  // the stale frame went with the window
+    CHECK(rig.host.link().linkResends() == 0);      // held, not re-sent into the menu
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        // The fake's menu branch returns before it reads a frame, so this says only that no frame reached a GAME after
+        // the reset; the linkResends() line above is what says none was written into the menu.
+        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
+    }
+    // The probe reads the menu and the job runs; the pause's own drain finds nothing to deliver, which is the point.
     Job job;
     std::string why;
     const auto t0 = Clock::now();
@@ -763,8 +825,8 @@
     REQUIRE(waitUntil([&] { return job.finished(); }));
     CHECK(job.state.load() == Job::Done);
     CHECK(job.result == saveOf(1024, 5));
-    REQUIRE(waitUntil([&] { return rig.cart.connected(); }));  // streaming resumed to the menu
-    CHECK(rig.host.link().linkOutstanding() == 1);              // still owed: the menu will never acknowledge it
+    CHECK(rig.host.link().isConnected());
+    CHECK_FALSE(rig.cart.connected());  // the menu, where streaming stays off
 }
 
 TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a config block that did not answer",
@@ -775,20 +837,25 @@
     rig.dev.linkRom = true;
     rig.dev.cfg[0] = 69;
     rig.dev.cfg[2] = 0;
+    rig.dev.linkAckDelayMs = 30;  // a game that answers, but not instantly
     rig.connect();
     REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
-    {
-        std::lock_guard<std::mutex> l(rig.dev.mu);
-        rig.dev.linkDamaged = true;
-    }
-    // Every copy of the frame reaches the game damaged and is reported at once, so it is re-sent at once: the cart
-    // never stops acknowledging and the frame never stops being outstanding, so no turn is quiet for a read.
-    rig.push({0x90, 0x3C, 0x7F});
-    REQUIRE(waitUntil([&] { return rig.host.link().linkResends() >= 3; }));
+    // A stream the game keeps acknowledging: every turn has an ack in the last ack bound, so no turn is quiet for a
+    // config read, and the cart is answering as a game (its window moves), so the guard has nothing to look for either.
+    std::atomic<bool> streaming{true};
+    std::thread feeder([&] {
+        while (streaming.load(std::memory_order_relaxed)) {
+            rig.push({0x90, 0x3C, 0x7F});
+            std::this_thread::sleep_for(std::chrono::milliseconds(5));
+        }
+    });
+    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // acks in flight before the probe asks
     Job job;
     std::string why;
     const auto t0 = Clock::now();
     CHECK_FALSE(rig.submit(battery("dump"), job, &why));
+    streaming.store(false, std::memory_order_relaxed);
+    feeder.join();
     CHECK(why == "the reliable link never went quiet for the cart's config read: the cart kept acknowledging the "
                  "stream");
     CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() >= 900);  // its deadline
@@ -876,7 +943,8 @@
     CHECK(job.state.load() == Job::Failed);
     CHECK(job.error.rfind("the battery file EDN8/gamedata/game.nes/bram.srm was replaced and is incomplete: restore "
                           "again (", 0) == 0);
-    CHECK(rig.cart.connected());
+    CHECK(rig.host.link().isConnected());
+    CHECK_FALSE(rig.cart.connected());  // the menu
     {
         std::lock_guard<std::mutex> l(rig.dev.mu);
         CHECK(rig.dev.filesOpen == 0);                 // closed on the failure path
@@ -1058,7 +1126,7 @@
           "[n8][cart-release]") {
     SECTION("raw: after a note-on still waiting for its release time") {
         Rig rig;
-        rig.connect();
+        rig.connectGame();
         // The note-on is due 200 ms out (a lookahead and a late frame), so it is still in the ring when the node's
         // destroy hands over its books: its note-off must not overtake it.
         const Bytes on{0x90, 0x3C, 0x7F};
@@ -1088,11 +1156,11 @@
         CHECK(rig.dev.applied[1] == Bytes{0x80, 0x3C, 0x00});
         CHECK(rig.dev.applied[2] == Bytes{0x81, 0x43, 0x00});
     }
-    SECTION("a job's pause: none while it holds the port, all on the first streaming turn after it") {
+    SECTION("a job's pause: nothing is written while the cart is at its menu") {
         Rig rig;
         rig.dev.files[kGameBattery] = saveOf(3000, 7);
         rig.dev.fileReadDelayMs     = 20;
-        rig.connect();
+        rig.connect();  // the menu, where a battery job runs
         Job job;
         REQUIRE(rig.submit(battery("dump"), job));
         REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
@@ -1101,17 +1169,14 @@
         CHECK(rig.dev.count("fifo:8") == 0);
         REQUIRE(waitUntil([&] { return job.finished(); }));
         CHECK(job.state.load() == Job::Done);
-        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
-        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
         CHECK(rig.dev.count("file-rd") == 6);
-        std::lock_guard<std::mutex> l(rig.dev.mu);
-        const auto lastRead = std::find(rig.dev.log.rbegin(), rig.dev.log.rend(), "file-rd");
-        const auto release  = std::find(rig.dev.log.rbegin(), rig.dev.log.rend(), "fifo:80 3C 00");
-        CHECK(release < lastRead);  // reversed: the note-off is the later entry
+        // The release was dropped with the ring when the guard read the menu: a menu holds no notes, and its FIFO is a
+        // command parser. Nothing of it reached the cart, before or after the job.
+        CHECK(rig.dev.count("fifo:8") == 0);
     }
     SECTION("a disconnect writes a release still waiting before the port closes; a link that is down drops one") {
         Rig rig;
-        rig.connect();
+        rig.connectGame();
         // A note-on due a second out never goes (the disconnect throws the ring away), and the release does not wait
         // for it: a note-off for a note that never sounded is harmless, a held one left behind is not.
         const Bytes on{0x90, 0x3C, 0x7F};
@@ -1125,7 +1190,7 @@
         // Down: nothing runs to write it, and the next connect does not either (the console it reaches may have been
         // power-cycled since, and a cart node that still drives it keeps its own books).
         rig.cart.releaseNotes(heldNotes());
-        rig.connect();
+        rig.connectGame();
         std::this_thread::sleep_for(std::chrono::milliseconds(30));
         CHECK(rig.dev.count("fifo:80 3C 00") == 1);
     }
@@ -1140,7 +1205,7 @@
     // keeps the index of the chunk whose note-on it last wrote for each key.
     SECTION("raw: the new node struck one of the keys the old one held") {
         Rig rig;
-        rig.connect();
+        rig.connectGame();
         rig.push({0x90, 0x3C, 0x7F});  // the leaving node, key 60
         rig.push({0x90, 0x43, 0x7F});  // ... and key 67
         REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 43 7F") == 1; }));
@@ -1157,7 +1222,7 @@
     }
     SECTION("raw: the new node's note-on is still waiting for its release time when the release runs") {
         Rig rig;
-        rig.connect();
+        rig.connectGame();
         rig.push({0x90, 0x3C, 0x7F});
         REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
         const std::uint64_t mark = rig.cart.pushMark();
@@ -1173,7 +1238,7 @@
     }
     SECTION("raw: a release waits for a SysEx the bytes port split across chunks") {
         Rig rig;
-        rig.connect();
+        rig.connectGame();
         // A node's bytes port can hand over half a SysEx: the cart's parser is inside a message until the F7, so a
         // note-off written between the halves is payload, not a message, and the key it meant to release stays down.
         rig.push({0xF0, 0x7E, 0x7F});  // the first half: F0 and two of its data bytes
@@ -1392,3 +1457,4 @@
     CHECK(rig.dev.menu);
     CHECK(rig.dev.booted.empty());
 }
+
diff --git a/packages/n8-wire/native/src/transport/N8ReliableLink.hpp b/packages/n8-wire/native/src/transport/N8ReliableLink.hpp
--- a/packages/n8-wire/native/src/transport/N8ReliableLink.hpp
+++ b/packages/n8-wire/native/src/transport/N8ReliableLink.hpp
@@ -129,6 +129,15 @@
     }
 
     std::size_t outstanding() const { return window_.size(); }
+    // Whether the window has stopped MOVING: the oldest outstanding frame first went out at least timeoutNs ago, and
+    // nothing has acknowledged it since (an ack that advances the window makes a newer frame the oldest, with a newer
+    // first send). This is not the re-send question above, which measures from the last thing the cart said: a
+    // back-channel that chatters keeps that fresh for ever, and a transport that must look before it re-sends - a cart
+    // at its menu reads a stale frame as one of its commands - needs the progress, not the silence.
+    bool progressOverdue(std::int64_t nowNs) const {
+        const std::int64_t first = oldestFirstSentNs();
+        return first != kNever && nowNs - first >= timing_.timeoutNs;
+    }
     // When the cart last said anything on the back-channel (an ack or not), and when the oldest outstanding frame first
     // went out (a re-send does not move it): with both far enough behind, a cart is not acknowledging anything, so a
     // transport may read a reply off the same channel. kNever while nothing is outstanding or that frame has not left;
diff --git a/packages/n8/native/src/n8/desktop/N8CartLink.hpp b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
--- a/packages/n8/native/src/n8/desktop/N8CartLink.hpp
+++ b/packages/n8/native/src/n8/desktop/N8CartLink.hpp
@@ -34,7 +34,10 @@
         dropped_.fetch_add(1, std::memory_order_relaxed);
         return false;
     }
-    bool          connected() const override { return host_.link().isConnected(); }
+    // What a cart NODE reads as connected: the link is up AND the cart is running a game. A menu is attached and
+    // streaming nothing (its FIFO is a command parser there), so a node treats it as disconnected and owes its notes no
+    // further - the existing disconnected rule - which is what "at the menu" means to the graph.
+    bool          connected() const override { return host_.link().isConnected() && !host_.link().atMenu(); }
     // [audio] N8Link::chunksPushed: the queue's own count, which the node reads after each of its pushes.
     std::uint64_t pushMark() const override { return host_.link().chunksPushed(); }
     // Every push of this adapter that lost its bytes, plus the chunks a stream stopped after a job found in its ring
diff --git a/packages/n8/native/src/n8/desktop/N8Host.cpp b/packages/n8/native/src/n8/desktop/N8Host.cpp
--- a/packages/n8/native/src/n8/desktop/N8Host.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Host.cpp
@@ -79,6 +79,10 @@
 // a control thread (the DAW's) from waiting out the read's own 2 s timeout to learn it.
 constexpr int kMenuProbeMs = 1000;
 
+// How long the menu guard's own read of the config block may wait for the cart: it runs between chunks on the serial
+// thread, so a cart that stopped answering must cost the stream a fraction of the read timeout (2 s), not all of it.
+constexpr std::uint32_t kMenuReadMs = 100;
+
 // Why a battery job refuses a running game: the battery file lives on the SD card, which only the menu's file API
 // reaches (a running game has no menu), and the cart's own battery is never touched over USB (on the real cart one
 // read of it wedged a running BlipToaster, and one at the menu left the menu deaf to its commands).
@@ -184,6 +188,22 @@
     // A value a param changed while the link is up: the serial thread owns the port, so it writes the standing value
     // on its own turn, between MIDI chunks, and marks the generation it wrote. A throw leaves the generation behind,
     // so the next turn tries again (N8Link catches it: a config write must not take the MIDI link down).
+    // Where the cart runs, for the link's menu guard (N8Link::MenuCheck): the config block's map_idx, 255 while the N8
+    // OS's own menu shows. A system-region read, so it touches neither the cart's bus nor a running game's FIFO, and it
+    // takes a short timeout: it is made between chunks, and a cart silent for two seconds per turn would stall the
+    // stream. -1 (the block did not answer) leaves the link's state as it was.
+    link_.setMenuCheck([](Edio& edio) {
+        // The short timeout is this call's, not the Edio's from here on: a scope guard puts the table back however the
+        // read ends, a throw from the port included, so no later call inherits a 100 ms reply timeout.
+        struct ReadMs {
+            rp::n8::Timeouts& table;
+            rp::n8::Timeouts  saved;
+            ~ReadMs() { table = saved; }
+        } restore{edio.timeouts(), edio.timeouts()};
+        restore.table.readMs = kMenuReadMs;
+        const int mapper = runningMapper(edio);
+        return mapper < 0 ? -1 : (mapper == kMenuMapper ? 1 : 0);
+    });
     link_.setOnControl([this](Edio& edio, bool backChannelQuiet) {
         // The config block a submission's refusal reads (probeMenu). Taken here, between chunks, for the reason the
         // volume is: a system-region read on the thread that owns the port, putting no byte in a running game's FIFO.
@@ -197,7 +217,13 @@
         // covers a cart that keeps acknowledging.
         const unsigned probe = menuProbeGeneration_.load(std::memory_order_acquire);
         if (backChannelQuiet && probe != menuProbeApplied_.load(std::memory_order_relaxed)) {
-            menuProbeIndex_.store(runningMapper(edio), std::memory_order_relaxed);
+            const int mapper = runningMapper(edio);
+            menuProbeIndex_.store(mapper, std::memory_order_relaxed);
+            // The same read tells the guard the cart is at its menu, and it does so before the submission that asked
+            // for it can hand over a job: a pause's drain must not write the ring into a menu the probe has just seen.
+            // Only that way round: this read is not flushed first, as the guard's own are, so a chattering cart's byte
+            // could make a menu read as a game, and only the guard's flushed read may re-enable the stream.
+            if (mapper == kMenuMapper) link_.reportMenuState(true);
             menuProbeApplied_.store(probe, std::memory_order_release);
         }
         const unsigned generation = expVolGeneration_.load(std::memory_order_acquire);
@@ -219,6 +245,7 @@
     c.ports        = lister_ ? lister_() : std::vector<N8PortDto>{};
     c.selectedPort = port_;
     c.connected    = link_.isConnected();
+    c.atMenu       = link_.atMenu();
     c.enabled      = enabled_;
     c.lookaheadMs  = link_.lookaheadMs();
     c.bytes        = link_.bytesForwarded();
diff --git a/packages/n8/native/src/n8/desktop/N8Host.hpp b/packages/n8/native/src/n8/desktop/N8Host.hpp
--- a/packages/n8/native/src/n8/desktop/N8Host.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Host.hpp
@@ -27,6 +27,9 @@
     std::string            selectedPort;
     bool                   connected = false;
     bool                   enabled = false;
+    // The cart is at its menu (N8Link::atMenu): the link is up, and the stream is dropped rather than delivered, since
+    // a menu reads the FIFO as commands. Settings > N8 says so, and a cart node's connected reads 0.
+    bool                   atMenu = false;
     int                    lookaheadMs = 0;
     std::uint64_t          bytes = 0;
     std::string            error;
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -13,6 +13,14 @@
 
 namespace {
 using steady = std::chrono::steady_clock;
+// How often the cart's at-menu state is read on a quiet turn while streaming (raw, framed and idle, or already at the
+// menu); how long a stream whose window has stopped moving waits for a quiet turn before it is read anyway (a menu that
+// chatters on the back-channel keeps the quiet rule fresh for ever, so only this bound ends it); and how long a read
+// that did not answer holds the next one off, so a port that keeps timing out costs the stream 100 ms a second, not
+// 100 ms in every 250. The bound is also the job probe's own deadline (N8Host's kMenuProbeMs).
+constexpr std::int64_t kMenuReadNs = 250'000'000;
+constexpr std::int64_t kMenuHoldNs = 1'000'000'000;
+constexpr std::int64_t kMenuReadFailNs = 1'000'000'000;
 // RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
 bool traceOn() {
     static const bool on = std::getenv("RP_N8LINK_TRACE") != nullptr;
@@ -99,6 +107,19 @@
     { TimedChunk drop; while (ring_.tryPop(drop)) {} }
     chunksWritten_ = chunksPushed_.load(std::memory_order_acquire);  // the ring is empty: nothing pushed is still to go
     std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);     // a fresh connection's cart holds no note of ours
+    // The cart's at-menu state, on the fresh port and before any producer may push: a menu reads the FIFO as commands,
+    // so the state decides whether anything may be streamed at all. A check that fails to answer leaves the state as it
+    // was (not the menu), which streams as the link always did.
+    atMenu_.store(false, std::memory_order_relaxed);
+    menuHoldSince_  = 0;
+    menuReadDueNs_  = 0;
+    menuGameUntilNs_ = 0;
+    if (menuCheck_) {
+        try {
+            atMenu_.store(menuCheck_(*edio_) == 1, std::memory_order_relaxed);
+        } catch (const std::exception&) {
+        }
+    }
     bytesForwarded_.store(0, std::memory_order_relaxed);
     controlRetryAtNs_ = 0;  // a prior connection's failed control write does not hold this one's first turn back
     backChannelQuiet_.store(true, std::memory_order_relaxed);
@@ -115,6 +136,11 @@
 void N8Link::disconnect() {
     running_.store(false, std::memory_order_release);
     if (thread_.joinable()) thread_.join();  // the serial thread stops touching edio_ before we reset it
+    // Where the OLD cart ran says nothing about the next one: a status read while the link is down must not keep saying
+    // "at the menu" for a port that has been closed.
+    atMenu_.store(false, std::memory_order_relaxed);
+    menuHoldSince_   = 0;
+    menuGameUntilNs_ = 0;
     rel_.reset();
     reliable_.store(false, std::memory_order_release);
     connected_.store(false, std::memory_order_release);
@@ -124,7 +150,9 @@
 
 bool N8Link::push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate) {
     if (n == 0) return true;
-    if (!connected_.load(std::memory_order_acquire)) {
+    if (!connected_.load(std::memory_order_acquire) || atMenu_.load(std::memory_order_relaxed)) {
+        // The menu reads the same FIFO a game takes MIDI from, so a message is a command there: dropped, not held (a
+        // game booted later starts fresh, and a console reset's game held the notes, not this one).
         pushesDropped_.fetch_add(1, std::memory_order_relaxed);
         return false;
     }
@@ -152,7 +180,7 @@
 }
 
 void N8Link::releaseNotes(const NoteSet& notes, std::uint64_t mark) {
-    if (notes.none()) return;
+    if (notes.none() || atMenu_.load(std::memory_order_relaxed)) return;  // a menu holds no notes of ours
     std::lock_guard<std::mutex> l(jobMutex_);
     if (!acceptingJobs_) return;  // no serial thread to write them: the link is down
     releasePending_ |= notes;
@@ -166,9 +194,12 @@
 
 void N8Link::writeReleases(bool stopping) {
     if (!releaseWanted_.load(std::memory_order_acquire)) return;
-    // Between two messages only: a note-off written inside one the bytes port split across chunks is not a message the
-    // cart's parser sees (a SysEx above all - a note-off inside an F0..F7 run is payload). A stream left mid-message
-    // holds it only until kMidMessageNs, so a node that dies mid-SysEx still gets its notes released.
+    // At the menu nothing goes, stopping or not: a menu holds no notes of ours, and its FIFO reads a note-off as its
+    // own commands.
+    if (atMenu_.load(std::memory_order_relaxed)) return;
+    // And between two messages only: a note-off written inside one the bytes port split across chunks is not a message
+    // the cart's parser sees (a SysEx above all - a note-off inside an F0..F7 run is payload). A stream left
+    // mid-message holds it only until kMidMessageNs, so a node that dies mid-SysEx still gets its notes released.
     if (!stopping && !atMessageBoundary(nowNs())) return;
     NoteSet      notes;
     std::uint64_t after = 0;
@@ -264,7 +295,14 @@
     bytesForwarded_.fetch_add(out.size(), std::memory_order_relaxed);
 }
 
-void N8Link::serviceLink() {
+void N8Link::serviceLink(std::int64_t now) {
+    serviceLinkAcks();
+    serviceLinkResends(now);
+}
+
+// Acks in, and nothing out: the quiet test a turn's read is given must see what the cart has actually said, and a
+// reply read before it can have an ack land inside.
+void N8Link::serviceLinkAcks() {
     const std::int64_t now = nowNs();
     std::uint8_t buf[64];
     const std::size_t n = readPort(buf, sizeof buf, 0);   // whatever the cart has said: never waits
@@ -274,6 +312,13 @@
         std::fprintf(stderr, "\n");
     }
     if (n) rel_->onBackChannel(buf, n, now);
+}
+
+// Whatever the reliable link is due to re-send at `now`, out. Held while the menu guard looks (menuHold): a cart at its
+// menu reads a stale frame as a command. `now` is the turn's timestamp, the one the guard judged: the poll's timeout
+// and the guard's own threshold are the same instant for a frame that was never re-sent, so a second reading of the
+// clock here would let a turn just short of the threshold re-send on the far side of it, into the menu.
+void N8Link::serviceLinkResends(std::int64_t now) {
     std::vector<std::uint8_t> out;
     std::vector<int> seqs;
     rel_->poll(now, [&](const std::uint8_t* d, std::size_t k, int seq) {
@@ -286,6 +331,107 @@
     linkRttMinNs_.store(rel_->rttMinNs(), std::memory_order_relaxed);
 }
 
+// The cart's at-menu state for this turn (see MenuCheck). Two things ask for a read: the cadence, on a turn quiet
+// enough for one; and a framed window that has STOPPED MOVING - the oldest frame unacknowledged past the link's own
+// timeout - which is the console reset, and whose re-send a menu would read as one of its commands. The second holds
+// the stream until the read answers. A cart that chatters cannot postpone that for ever: the quiet rule stamps the last
+// heard on any byte, so its bound, not the silence, is what ends it (kMenuHoldNs), after a flush of what it said.
+bool N8Link::menuHold(std::int64_t now, bool quiet) {
+    if (!menuCheck_) return false;
+    if (atMenu_.load(std::memory_order_relaxed)) {
+        menuHoldSince_ = 0;
+        // Still look, every cadence: a game the user starts from the cart's own browser is only visible in the config
+        // block, and nothing is outstanding at a menu, so a turn is always quiet for the read.
+        if (now >= menuReadDueNs_) readMenuState(now);
+        return atMenu_.load(std::memory_order_relaxed);  // nothing new goes out while it says menu
+    }
+    if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
+        const bool forced = menuHoldSince_ != 0 && now - menuHoldSince_ >= kMenuHoldNs;
+        if (quiet || forced) {
+            if (!quiet) flushPort();  // the read's answer must not begin with what the chatter left in the input
+            const bool answered = readMenuState(now);
+            menuHoldSince_ = 0;
+            if (atMenu_.load(std::memory_order_relaxed)) return true;
+            // The cart answered as a game: its acks are merely lost or damaged, and the stream goes on as it always
+            // did, at one read a second rather than one a turn.
+            if (answered) menuGameUntilNs_ = now + kMenuHoldNs;
+            return false;
+        }
+        if (menuHoldSince_ == 0) menuHoldSince_ = now;
+        return true;  // hold the re-send until the guard has looked
+    }
+    menuHoldSince_ = 0;
+    if (quiet && now >= menuReadDueNs_) readMenuState(now);
+    return atMenu_.load(std::memory_order_relaxed);
+}
+
+// One read of where the cart runs. True when it answered; a read that did not leaves the state as it was and holds the
+// next one off for a second (see kMenuReadFailNs).
+bool N8Link::readMenuState(std::int64_t now) {
+    menuReadDueNs_ = now + kMenuReadNs;
+    int answer = -1;
+    try {
+        answer = menuCheck_(*edio_);
+    } catch (const std::exception&) {
+        return false;  // the port's failure is the stream's to report at its next write; the state stands
+    }
+    if (answer < 0) {
+        menuReadDueNs_ = now + kMenuReadFailNs;
+        return false;
+    }
+    reportMenuState(answer != 0);
+    return true;
+}
+
+void N8Link::reportMenuState(bool menu) {
+    // A link with no MenuCheck has no guard - a fake device's, or a case that turned it off on purpose - so there is
+    // nothing for a probe's answer to feed.
+    if (!menuCheck_) return;
+    if (menu == atMenu_.load(std::memory_order_relaxed)) return;
+    atMenu_.store(menu, std::memory_order_relaxed);
+    if (menu)
+        enterMenu();
+    else
+        leaveMenu();
+}
+
+void N8Link::enterMenu() {
+    // Drop, not hold: a console reset left the game that held these notes, the menu holds none, and a game booted later
+    // starts fresh. The ring, the reliable window and any pending release go with it.
+    discardRing();
+    if (rel_) {
+        rel_->reset();
+        reliable_.store(false, std::memory_order_release);
+        linkOutstanding_.store(0, std::memory_order_relaxed);
+    }
+    {
+        std::lock_guard<std::mutex> l(jobMutex_);
+        releasePending_.reset();
+        releaseAfter_ = 0;
+        releaseWanted_.store(false, std::memory_order_relaxed);
+    }
+    menuHoldSince_   = 0;
+    menuGameUntilNs_ = 0;
+    if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
+}
+
+void N8Link::leaveMenu() {
+    if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
+    menuHoldSince_   = 0;
+    menuGameUntilNs_ = 0;
+    // A game started from the cart's own browser may speak the reliable link, and the connect-time probe could not have
+    // told: the frames it would carry start from a sync here.
+    if (linkAllowed_) probeLink();
+}
+
+void N8Link::discardRing() {
+    TimedChunk drop;
+    while (ring_.tryPop(drop)) {
+        chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
+        pushesDropped_.fetch_add(1, std::memory_order_relaxed);
+    }
+}
+
 bool N8Link::quietForRead(std::int64_t now) const {
     // Nothing outstanding, so no ack is owed (an ack the ROM repeats for a frame it already acknowledged is the one
     // exception, see OnControl). Or a cart that has said nothing for the pause's ack bound since the oldest outstanding
@@ -325,7 +471,8 @@
 
 void N8Link::streamLoop() {
     try {
-        if (linkAllowed_) probeLink();
+        // Not into a menu: the seed above knows where the cart runs, and the probe's sync is bytes its parser reads.
+        if (linkAllowed_ && !atMenu_.load(std::memory_order_relaxed)) probeLink();
     } catch (const std::exception& e) {
         setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
         connected_.store(false, std::memory_order_release);
@@ -336,12 +483,15 @@
     bool have = false;
     while (running_.load(std::memory_order_acquire)) {
         try {
-            // Acks in, re-sends out, before anything else this turn: the control turn below can read a reply, and an
-            // ack that answered an outstanding frame must be behind us before it does.
-            if (rel_) serviceLink();
+            // Acks in first, then the quiet test, then the menu guard, then the re-sends it may hold: the control turn
+            // below can read a reply, so an ack that answered an outstanding frame must be behind us before it does,
+            // and the guard's own read must not have one land inside it.
+            if (rel_) serviceLinkAcks();
             const std::int64_t turnNs = nowNs();
             const bool         quiet  = quietForRead(turnNs);
             backChannelQuiet_.store(quiet, std::memory_order_relaxed);
+            const bool menuHeld = menuHold(turnNs, quiet);
+            if (rel_ && !menuHeld) serviceLinkResends(turnNs);
             // A setting that lives in an atomic (see OnControl) is applied between chunks, on the thread that owns the
             // port. Best-effort, like the connect-time write: a config register must not take the MIDI link down. A
             // wake is spent only on a quiet turn, the only kind its caller's read can use: a busy turn whose write
@@ -359,12 +509,32 @@
                 }
             }
             // A leaving cart node's held notes (releaseNotes), once what was pushed before them has gone: on a
-            // streaming turn, never inside a job's pause, which only the job's own traffic may use.
-            writeReleases(false);
+            // streaming turn, never inside a job's pause, which only the job's own traffic may use, and never at a
+            // menu, which reads them as commands.
+            if (!menuHeld) writeReleases(false);
+            // Jobs still run at the menu: the battery files and an upload are exactly what its file API is for.
             if (jobPending_.load(std::memory_order_acquire)) {
                 runJob(chunk, have);
                 continue;
             }
+            if (atMenu_.load(std::memory_order_relaxed)) {
+                // The cart is at its menu, so nothing the ring holds may go - now or when a game boots later - and the
+                // trip's own discard cannot see a push that raced it. Drop it here, and keep dropping until a game.
+                if (have) {
+                    have = false;
+                    chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
+                    pushesDropped_.fetch_add(1, std::memory_order_relaxed);
+                }
+                discardRing();
+                std::this_thread::sleep_for(std::chrono::microseconds(200));
+                continue;
+            }
+            if (menuHeld) {
+                // A re-send is due and the guard has not been able to look yet. The chunk in hand is NOT lost: the cart
+                // may be a game that has been quiet for an ack bound, and its note-off must still go.
+                std::this_thread::sleep_for(std::chrono::microseconds(200));
+                continue;
+            }
             if (!have) have = ring_.tryPop(chunk);
             if (!have) {
                 std::this_thread::sleep_for(std::chrono::microseconds(200));  // ring empty
@@ -475,10 +645,17 @@
 }
 
 void N8Link::drainForJob(TimedChunk& chunk, bool& have) {
+    // At the menu nothing the ring still holds may go: the cart reads it as commands. The job's own transfers use the
+    // port directly, which is what a menu is for.
+    if (atMenu_.load(std::memory_order_relaxed)) {
+        have = false;
+        discardRing();
+        return;
+    }
     // What was pushed before the pause goes out first, each chunk at its own release time: at most the lookahead and a
     // block away, and a note-off among it must reach the cart.
     while (running_.load(std::memory_order_acquire)) {
-        if (rel_) serviceLink();
+        if (rel_) serviceLink(nowNs());
         if (!have) have = ring_.tryPop(chunk);
         if (!have) break;
         const std::int64_t now = nowNs();
@@ -496,7 +673,7 @@
     if (!rel_) return;
     const std::int64_t until = nowNs() + kAckWaitNs;
     while (running_.load(std::memory_order_acquire) && rel_->outstanding() > 0 && nowNs() < until) {
-        serviceLink();
+        serviceLink(nowNs());
         std::this_thread::sleep_for(std::chrono::microseconds(200));
     }
 }
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -54,6 +54,12 @@
     // keeps refusing a write cannot take the link's turns from the MIDI stream (a caller that cannot wait that out, a
     // submission's probe, asks for a quiet turn of its own through wakeControl).
     using OnControl = std::function<void(rp::n8::Edio&, bool backChannelQuiet)>;
+    // What the cart runs, read on the serial thread (and at connect, before the thread starts): 1 at the N8 menu, 0 in
+    // a game, -1 when the FPGA config block did not answer (the last state then stands). A menu reads the same FIFO a
+    // game takes MIDI from, so a streamed message can be read as one of its '*' commands (0x2A 0x73 is '*s'), which is
+    // why the link drops rather than streams while this says menu. N8Host installs one whose read has a short timeout:
+    // it runs between chunks, and a cart that stopped answering must not cost the stream two seconds a turn.
+    using MenuCheck = std::function<int(rp::n8::Edio&)>;
 
     // What a paused job tells the link once it has run: resume streaming, or stop it (a ROM boot: the old stream is
     // stale, and a running game has no menu to come back to). Stop closes the port, as a disconnect does.
@@ -88,6 +94,14 @@
     // Set once by the owner before any connect; connect() runs it on the caller's thread, while the link is the only
     // thing on the port.
     void          setOnConnected(OnConnected fn) { onConnected_ = std::move(fn); }
+    // Set once by the owner before any connect, like OnConnected: read at each connect (to seed the state) and on quiet
+    // turns while streaming. Null on a link with no cart behind it (N8CartLink.test.cpp's fake device), which then
+    // streams whatever it is given, as it always did.
+    void          setMenuCheck(MenuCheck fn) { menuCheck_ = std::move(fn); }
+    // [serial thread] What a config-block read said about where the cart runs (a MenuCheck's answer, or N8Host's own
+    // menu probe): the guard takes it as its state at once, so a probe made for a submission trips the guard before
+    // that job's pause drains the ring into a menu.
+    void          reportMenuState(bool menu);
     void          setOnControl(OnControl fn) { onControl_ = std::move(fn); }
     // [any] Run OnControl at the next quiet turn, whatever the wait a failed config write armed: the wake stands
     // through turns that are not quiet, and is spent by the first that is. A submission's menu probe is why this
@@ -141,6 +155,9 @@
         setError(why);
     }
     bool          isConnected() const { return connected_.load(std::memory_order_acquire); }
+    // [any] Whether the cart is at its menu, as the serial thread last read it: pushes are dropped there, and a cart
+    // node's connected reads 0 (N8CartLink).
+    bool          atMenu() const { return atMenu_.load(std::memory_order_relaxed); }
     std::string   portName() const;
     std::uint64_t bytesForwarded() const { return bytesForwarded_.load(std::memory_order_relaxed); }
     // Pushes that lost their bytes: the link was down or a job held it, the ring was full, or a stopped stream threw
@@ -187,6 +204,14 @@
 
     void serialLoop();
     void streamLoop();                                   // serial thread: the probe, then stream until stopped
+    // serial thread: the cart's at-menu state for this turn (see MenuCheck). True while the stream must put no new
+    // chunk, re-send or release on the wire: the cart is at its menu, or the window has stopped moving and the guard
+    // has not been able to look yet. The turn's quietForRead is the second argument.
+    bool        menuHold(std::int64_t now, bool quiet);
+    bool        readMenuState(std::int64_t now);                // serial thread: read it; false when it did not answer
+    void        enterMenu();                                    // serial thread: discard what is held, reset the link
+    void        leaveMenu();                                    // serial thread: probe the link again
+    void        discardRing();                                   // serial thread: drop what the ring holds, counted
     void writeChunk(const TimedChunk& chunk);            // serial thread: one chunk out, framed when reliable
     void writeReleases(bool stopping);                   // serial thread: releaseNotes' note-offs, once they may go
     // serial thread: where the BYTE stream the cart is reading stands, so a release never goes between a status and its
@@ -198,7 +223,11 @@
     void drainForJob(TimedChunk& chunk, bool& have);     // serial thread: what was pushed before the pause goes out
     void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
     void probeLink();                                    // serial thread: sync + listen for an ack
-    void serviceLink();                                  // serial thread: read acks, send re-sends
+    void serviceLink(std::int64_t now);                  // serial thread: read acks, send re-sends
+    void serviceLinkAcks();                              // serial thread: the cart's back-channel, read
+    // serial thread: whatever the reliable link is due to re-send at `now`, the turn's own timestamp: the same one the
+    // guard judged, so a re-send can never slip past the hold by re-reading the clock a moment later.
+    void serviceLinkResends(std::int64_t now);
     bool quietForRead(std::int64_t now) const;           // serial thread: may a reply be read now (see OnControl)
     void writeFramed(std::vector<std::uint8_t>& out, const std::vector<int>& seqs, std::int64_t now, bool resend);
     void setError(const std::string& msg);
@@ -215,6 +244,8 @@
     PortFactory                factory_;
     OnConnected                onConnected_;  // set before the first connect; never mutated afterwards
     OnControl                  onControl_;    // likewise
+    MenuCheck                  menuCheck_;    // likewise
+    std::atomic<bool>          atMenu_{false};  // the serial thread writes it, any thread reads it
     SpscRing<TimedChunk, 1024> ring_;  // audio producer -> serial consumer
     std::atomic<std::int64_t>  lookaheadNs_{0};
     std::atomic<bool>          connected_{false};
@@ -241,6 +272,11 @@
     // The earliest moment OnControl runs again after a throw (see the loop in streamLoop), and a wake a caller asked
     // for since the last quiet turn: the first is serial-thread state, the second is stored from any thread.
     std::int64_t               controlRetryAtNs_ = 0;
+    // The next periodic menu read, and when a re-send the guard must look before began to wait for a quiet turn (0:
+    // not waiting). Serial thread.
+    std::int64_t               menuReadDueNs_ = 0;
+    std::int64_t               menuHoldSince_ = 0;
+    std::int64_t               menuGameUntilNs_ = 0;  // a read answered "a game" here: no hold before this moment
     std::atomic<bool>          controlWanted_{false};
     std::atomic<bool>          backChannelQuiet_{true};  // the last turn's quietForRead, for other threads
 
diff --git a/packages/n8/native/test/N8CartLink.test.cpp b/packages/n8/native/test/N8CartLink.test.cpp
--- a/packages/n8/native/test/N8CartLink.test.cpp
+++ b/packages/n8/native/test/N8CartLink.test.cpp
@@ -201,6 +201,7 @@
     FakeDevice dev;
     N8Host     host(factoryFor(dev), noPorts(), tempCfgDir());
     host.link().setLinkAllowed(false);  // no probe: the wire below is the pushes alone
+    host.link().setMenuCheck({});       // nor a menu guard: its own config reads would be among the wire this counts
     REQUIRE(host.link().connect("fake"));
     N8CartLink link(host);
     REQUIRE(link.connected());
diff --git a/packages/n8/native/test/N8Provider.test.cpp b/packages/n8/native/test/N8Provider.test.cpp
--- a/packages/n8/native/test/N8Provider.test.cpp
+++ b/packages/n8/native/test/N8Provider.test.cpp
@@ -301,7 +301,9 @@
     provider->restore(self);
     CHECK(n8.getConfig().connected);
     CHECK(n8.getConfig().lookaheadMs == 0);
-    CHECK(cart->connected());
+    CHECK(n8.getConfig().atMenu);        // the fake cart sits at its menu: the guard says so
+    CHECK_FALSE(cart->connected());      // ...which a cart node reads as disconnected
+    CHECK(n8.link().isConnected());      // the port is up all the same
 
     // The facet's service is the provider's, over the same host.
     auto* rpc = static_cast<rp::n8::N8Rpc*>(provider->rpc_service(self));
@@ -311,7 +313,7 @@
     void* const installed = registry.link;
     provider->destroy(self);
     CHECK(registry.cleared == std::vector<void*>{installed});
-    CHECK(registry.connectedAtClear == std::vector<bool>{true});  // read through the link, still streaming
+    CHECK(registry.connectedAtClear == std::vector<bool>{false});  // read through the link: at the menu, so not streaming
 }
 
 TEST_CASE("the N8 provider refuses another ABI, and one whose link the registry refuses clears nothing",
diff --git a/packages/n8/ts/index.ts b/packages/n8/ts/index.ts
--- a/packages/n8/ts/index.ts
+++ b/packages/n8/ts/index.ts
@@ -40,6 +40,7 @@
   ports: N8Port[]; // available serial ports
   selectedPort: string; // "" = none chosen (connect auto-picks the attached N8)
   connected: boolean; // the serial link is open + handshaken
+  atMenu: boolean; // the cart is at its menu: the link is up, and nothing is streamed to it (its FIFO is a command parser)
   enabled: boolean; // the user's "stream to the N8" toggle
   lookaheadMs: number; // timed-release latency the serial thread applies
   bytes: number; // bytes forwarded since connect (status)
@@ -191,6 +192,7 @@
     ports,
     selectedPort: typeof c.selectedPort === "string" ? c.selectedPort : "",
     connected: !!c.connected,
+    atMenu: !!c.atMenu,
     enabled: !!c.enabled,
     lookaheadMs: typeof c.lookaheadMs === "number" ? c.lookaheadMs : 0,
     bytes: typeof c.bytes === "number" ? c.bytes : typeof c.bytes === "bigint" ? Number(c.bytes) : 0,
diff --git a/packages/n8/ts/test/link-client.test.ts b/packages/n8/ts/test/link-client.test.ts
--- a/packages/n8/ts/test/link-client.test.ts
+++ b/packages/n8/ts/test/link-client.test.ts
@@ -36,6 +36,7 @@
     ports: [{ port: "/dev/ttyACM0", isN8: true }, { port: "/dev/ttyS0", isN8: false }],
     selectedPort: "/dev/ttyACM0",
     connected: true,
+    atMenu: false,
     enabled: true,
     lookaheadMs: 10,
     bytes: 1234,
@@ -52,6 +53,7 @@
     ],
     selectedPort: "",
     connected: false,
+    atMenu: false,
     enabled: false,
     lookaheadMs: 0,
     bytes: 5,
diff --git a/packages/n8/ts/ui/index.ts b/packages/n8/ts/ui/index.ts
--- a/packages/n8/ts/ui/index.ts
+++ b/packages/n8/ts/ui/index.ts
@@ -53,9 +53,13 @@
   }
   const portName = (n: number) => (n === 0 ? "" : portValues[n - 1] ?? cfg.selectedPort);
   const laIdx = Math.max(0, N8_LOOKAHEADS.indexOf(cfg.lookaheadMs));
+  // The cart at its menu is attached and streaming nothing: its FIFO reads MIDI as the menu's own '*' commands, so the
+  // link drops rather than delivers, and the status says where the cart is instead of counting bytes that are not going.
   const status = cfg.enabled
     ? cfg.connected
-      ? `Streaming (${cfg.bytes} bytes)`
+      ? cfg.atMenu
+        ? "At the N8 menu (not streaming)"
+        : `Streaming (${cfg.bytes} bytes)`
       : cfg.error
         ? `Error: ${cfg.error}`
         : "Connecting..."
diff --git a/packages/native/test/engine-graph/N8Adapters.test.cpp b/packages/native/test/engine-graph/N8Adapters.test.cpp
--- a/packages/native/test/engine-graph/N8Adapters.test.cpp
+++ b/packages/native/test/engine-graph/N8Adapters.test.cpp
@@ -167,16 +167,16 @@
         std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
         withRoms(e, systems, roms);
         REQUIRE(e.setGraph(withCart(systems)).ok);
-        host.setPort("/dev/fake");
-        host.connect(true);
-        REQUIRE(host.link().isConnected());
-        host.link().setLookaheadMs(0);
         {
             // A game, not the menu: the note this case stages first has to reach the cart, and a menu is where a
-            // streamed message does not go.
+            // streamed message does not go. Set before the connect, which is where the link's guard reads it.
             std::lock_guard<std::mutex> l(dev.mu);
             dev.menu = false;
         }
+        host.setPort("/dev/fake");
+        host.connect(true);
+        REQUIRE(host.link().isConnected());
+        host.link().setLookaheadMs(0);
 
         // A note staged at the engine reaches the cart's FIFO, through the node, N8CartLink and N8Link.
         e.stageMidi(0, Bytes{0x90, 60, 100});
@@ -206,11 +206,22 @@
                 dev.menu = true;
             }
 
-            // A dump at the menu is taken; a replan without the cart node abandons it at reclaim (Removed, through the
-            // engine's record), and the note the node still held (62) gets its note-off once streaming resumes.
+            // A note the node holds while the cart is still a game, then the console's menu for the dump: the guard
+            // reads it between chunks, and from then on nothing is streamed.
             e.stageMidi(0, Bytes{0x90, 62, 100});
             block(e);
             REQUIRE(waitUntil([&] { return dev.count("fifo:90 3E 64") == 1; }));
+            {
+                std::lock_guard<std::mutex> l(dev.mu);
+                dev.menu = true;
+            }
+            // The guard reads the cart's state between chunks, so give it the read before anything is expected of the
+            // menu: a message pushed in the window before it is a message the menu may read.
+            REQUIRE(waitUntil([&] { return host.link().atMenu(); }));
+            // A dump at the menu is taken; a replan without the cart node abandons it at reclaim (Removed, through the
+            // engine's record), and the note the node still held (62) is dropped with the stream: the menu reads the
+            // note-off as one of its own commands. Nothing sounds for it either way: the console is in its menu, not in
+            // the game that started the note.
             const auto dump = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump", "game.nes"));
             REQUIRE(dump.id != 0);
             REQUIRE(waitUntil([&] { return dev.count("file-rd") >= 1; }));
@@ -222,9 +233,9 @@
             CHECK_FALSE(removed->ok);
             CHECK(removed->error == "node cart left the plan before the job completed");
             REQUIRE(waitUntil([&] { return !host.cartJobBusy(); }));
-            REQUIRE(waitUntil([&] { return dev.count("fifo:80 3E 00") == 1; }));
-            CHECK(dev.count("file-rd") < 8);  // it stopped early
-            CHECK(logIndex(dev, "file-rd") < logIndex(dev, "fifo:80 3E 00"));
+            std::this_thread::sleep_for(std::chrono::milliseconds(30));
+            CHECK(dev.count("fifo:80 3E 00") == 0);  // at the menu nothing was written for it
+            CHECK(dev.count("file-rd") < 8);         // it stopped early
             CHECK(host.link().isConnected());
 
             // The node again, and an upload through it: the menu installs and boots the image, streaming stops, and the
diff --git a/packages/retroplug/test-ui/ui-services.test.ts b/packages/retroplug/test-ui/ui-services.test.ts
--- a/packages/retroplug/test-ui/ui-services.test.ts
+++ b/packages/retroplug/test-ui/ui-services.test.ts
@@ -70,6 +70,7 @@
     ports: [{ port: "/dev/ttyACM0", isN8: true }],
     selectedPort: n8State.selectedPort,
     connected: n8State.connected,
+    atMenu: false,
     enabled: n8State.enabled,
     lookaheadMs: 10,
     bytes: 0,
diff --git a/packages/retroplug/test/menu/n8-sd-ops.test.ts b/packages/retroplug/test/menu/n8-sd-ops.test.ts
--- a/packages/retroplug/test/menu/n8-sd-ops.test.ts
+++ b/packages/retroplug/test/menu/n8-sd-ops.test.ts
@@ -65,6 +65,7 @@
   ports: { port: string; isN8: boolean }[];
   selectedPort: string;
   connected: boolean;
+  atMenu: boolean;
   enabled: boolean;
   lookaheadMs: number;
   bytes: number;
@@ -77,6 +78,7 @@
   ports: [{ port: "/dev/ttyACM0", isN8: true }],
   selectedPort: "/dev/ttyACM0",
   connected: true,
+  atMenu: false,
   enabled: true,
   lookaheadMs: 10,
   bytes: 0,
@@ -453,3 +455,24 @@
     env.restore();
   }
 });
+
+test("the status row says where the cart is: streaming, or at its menu with nothing going to it", () => {
+  // A cart at its menu is attached and streaming nothing: its FIFO reads MIDI as the menu's own '*' commands, so the
+  // native link drops it. The row says the menu rather than counting bytes the cart is not hearing.
+  const streaming = installN8({ bytes: 1234 });
+  try {
+    const be = new MockBackend("/cfg");
+    const stores = composeAppStores({ backend: be });
+    expect(findItem(n8Submenu(stores, be), "n8-status")?.label).toBe("Status: Streaming (1234 bytes)");
+  } finally {
+    streaming.restore();
+  }
+  const atMenu = installN8({ atMenu: true, bytes: 1234 });
+  try {
+    const be = new MockBackend("/cfg");
+    const stores = composeAppStores({ backend: be });
+    expect(findItem(n8Submenu(stores, be), "n8-status")?.label).toBe("Status: At the N8 menu (not streaming)");
+  } finally {
+    atMenu.restore();
+  }
+});
diff --git a/packages/retroplug/test/menu/recorded-cases.ts b/packages/retroplug/test/menu/recorded-cases.ts
--- a/packages/retroplug/test/menu/recorded-cases.ts
+++ b/packages/retroplug/test/menu/recorded-cases.ts
@@ -104,6 +104,7 @@
   ports: [{ port: "/dev/ttyACM0", isN8: true }],
   selectedPort: "/dev/ttyACM0",
   connected: true,
+  atMenu: false,
   enabled: true,
   lookaheadMs: 10,
   bytes: 0,
diff --git a/packages/retroplug/test/services.test.ts b/packages/retroplug/test/services.test.ts
--- a/packages/retroplug/test/services.test.ts
+++ b/packages/retroplug/test/services.test.ts
@@ -80,7 +80,16 @@
 test("the N8 card poll repaints only when the job's snapshot moves, and a job does move it", () => {
   const { host } = rig();
   setN8LinkClient({
-    linkConfig: () => ({ ports: [], selectedPort: "", connected: false, enabled: false, lookaheadMs: 10, bytes: 0, error: "" }),
+    linkConfig: () => ({
+      ports: [],
+      selectedPort: "",
+      connected: false,
+      atMenu: false,
+      enabled: false,
+      lookaheadMs: 10,
+      bytes: 0,
+      error: "",
+    }),
     setPort: () => {},
     connect: () => {},
     setLookahead: () => {},
````

- [ ] **Step 4: run it, and see it pass**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[menu-guard]"
timeout -k 10 600 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test
```

Expected: PASS, the five cases and then the whole binary with every rewritten case in it:

```
All tests passed (46 assertions in 5 test cases)
...
All tests passed (1638 assertions in 118 test cases)
```

- [ ] **Step 5: write the cases the guard's discards and its reads need, and see them fail**

Three cases, added to `N8CartJobs.test.cpp` beside the ones they belong with, and one knob in `FakeN8`: chatter that
lands ahead of a reply. Each fails against the code Step 3 just wrote, for its own reason, which is what Step 6 fixes:

- **a menu episode does not leave a release waiting behind the chunks it dropped.** The guard drops the ring, and a
  release waits for the serial thread to have taken that many chunks off it (`chunksWritten_` reaching the push count it
  was asked at). If a dropped chunk is not counted, every later release on the connection waits for chunks nobody will
  push: the console holds a tone until the link ends, and a handover reads the new node's note-on as older than the mark
  it waits behind, so the old node's release cuts the new node's key. Two sections, one per half: a node removed with no
  successor gets its note-offs written, and a handover after the trip still keeps the new node's key.
- **a clock between a message's halves is not a message boundary.** A real-time byte (0xF8 and up) may stand between any
  two bytes of a message. Read as a status it looks like the message ended, and a leaving node's note-off written there
  becomes the message's payload.
- **a menu whose chatter lands ahead of its answers is read as a menu, not as a game.** A cart that talks while the host
  is not reading leaves bytes in the input; a config-block reply read behind them is garbled, and map_idx 255 with one
  status byte in front of it reads as a game - which would leave the guard streaming into the menu.

````patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -53,6 +53,7 @@
     int                      linkAckDelayMs = 0;    // how long the game takes to write a frame's ack back
     bool                     linkDamaged = false;   // every frame reaches the game damaged, and it says so each time
     int                      chatterMs = 0;          // writes a byte on the back-channel this often (0: never)
+    bool                     chatterGarbles = false; // ... left for the host's next read, as one said between reads is
     int                      refuseExpVolWrites = 0; // the next N expansion-volume writes fail in the port
     int                      expVolAttempts = 0;    // every expansion-volume write the host made
     int                      fileDelayMs = 0;       // likewise each SD file write
@@ -129,9 +130,11 @@
         failure_.clear();
         releaseAcks(true);  // whatever the game has had time to write since the last read
         // A menu that never goes quiet: one byte whenever the cart owes the host no reply, which the link reads as it
-        // having said something (its quiet rule stamps the last heard on ANY byte). Only between replies, so a config
-        // read's answer is never garbled by one.
-        if (dev_.chatterMs > 0 && out_.empty()) {
+        // having said something (its quiet rule stamps the last heard on ANY byte). It is handed back in this read, so
+        // a reply the cart owes is never garbled by one - unless chatterGarbles, which leaves it in the input for the
+        // next read instead, as a cart that talks while the host is not reading does: the reply is then behind it, and
+        // a flush of the input is the only thing that gets it out of the way.
+        if (dev_.chatterMs > 0 && !dev_.chatterGarbles && out_.empty()) {
             const auto now = std::chrono::steady_clock::now();
             if (now >= chatterDue_) {
                 out_.push_back(0xB0);  // a status byte the ack parser skips
@@ -143,6 +146,13 @@
             b[i++] = out_.front();
             out_.pop_front();
         }
+        if (dev_.chatterMs > 0 && dev_.chatterGarbles) {
+            const auto now = std::chrono::steady_clock::now();
+            if (now >= chatterDue_) {
+                out_.push_front(0xB0);  // said while the host was not reading: the next read meets it first
+                chatterDue_ = now + std::chrono::milliseconds(dev_.chatterMs);
+            }
+        }
         return i;
     }
     void flushInput() override {
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -1285,6 +1285,88 @@
     }
 }
 
+TEST_CASE("a menu episode does not leave a release waiting behind the chunks it dropped", "[n8][cart-release]") {
+    // The guard drops the ring at the menu, and a release waits until the serial thread has taken that many chunks off
+    // it (chunksWritten_ reaching the push count it was asked at). A chunk the guard drops is a chunk gone, so the
+    // count has to include it: otherwise every later release on this connection waits for chunks nobody will push - the
+    // console holding a tone until the link ends - and every later handover reads the new node's note-on as older than
+    // the mark it waits behind, which is the key-cutting bug this release exists to stop.
+    SECTION("raw: the leaving node's last push never went, so its release waits for a count that includes it") {
+        Rig rig;
+        rig.connectGame();
+        // Still in the ring when the console goes to its menu: the lookahead keeps it there, and the trip drops it.
+        rig.host.link().setLookaheadMs(1000);
+        rig.push({0x90, 0x3C, 0x7F});
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.menu = true;  // the menu, and the chunk in flight is dropped with the ring
+        }
+        REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
+        REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
+        // A game booted by hand from the cart's own browser, which is the state read back a game again.
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.menu   = false;
+            rig.dev.cfg[0] = 69;
+            rig.dev.cfg[2] = 0;
+        }
+        REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
+        rig.host.link().setLookaheadMs(0);
+        rig.push({0x90, 0x3E, 0x7F});  // one chunk written after the trip, so the ring is empty again
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3E 7F") == 1; }));
+        // The node that pushed it leaves with no successor (the setting off, the system removed): its release waits for
+        // every push, and every push has gone, dropped or written, so the note-offs go.
+        rig.cart.releaseNotes(heldNotes());
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
+        CHECK(rig.dev.count("fifo:81 43 00") == 1);
+    }
+    SECTION("raw: a handover after the trip still keeps the new node's key") {
+        Rig rig;
+        rig.connectGame();
+        rig.host.link().setLookaheadMs(1000);
+        rig.push({0x90, 0x3C, 0x7F});  // the leaving node, key 60, still in the ring at the trip
+        const std::uint64_t mark = rig.cart.pushMark();
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.menu = true;
+        }
+        REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
+        {
+            std::lock_guard<std::mutex> l(rig.dev.mu);
+            rig.dev.menu   = false;
+            rig.dev.cfg[0] = 69;
+            rig.dev.cfg[2] = 0;
+        }
+        REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
+        rig.host.link().setLookaheadMs(0);
+        rig.push({0x90, 0x3C, 0x7F});  // the node taking over, on the key the old one held
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
+        rig.cart.releaseNotes(heldNotes(), mark);
+        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));  // the old node's other key went
+        std::this_thread::sleep_for(std::chrono::milliseconds(30));
+        CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // and the new node's is still down
+        CHECK(rig.cart.connected());
+    }
+}
+
+TEST_CASE("a clock between a message's halves is not a message boundary", "[n8][cart-release]") {
+    // A real-time byte (0xF8 and up) may stand between any two bytes of a message without ending it, so the tracker of
+    // what the cart's parser is inside must step over one. Read as a status it would look like a boundary, and a
+    // leaving node's note-off written there would land inside the message as its payload.
+    Rig rig;
+    rig.connectGame();
+    rig.push({0x90, 0x3C});  // half a note-on: the parser is inside a message
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C") == 1; }));
+    rig.push({0xF8});  // a clock
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F8") == 1; }));
+    rig.cart.releaseNotes(heldNotes());
+    std::this_thread::sleep_for(std::chrono::milliseconds(30));
+    CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // held: the message is not finished
+    rig.push({0x7F});                             // its tail, which completes it
+    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
+    CHECK(rig.dev.count("fifo:81 43 00") == 1);
+}
+
 TEST_CASE("a cart at its menu is not streamed to: its FIFO is a command parser there", "[n8][menu-guard]") {
     // The N8 menu reads the same FIFO a game reads MIDI from, so a data pair inside a message - 0x2A 0x73, say - is its
     // '*s': start an app. The guard reads where the cart runs from the FPGA config block (a system-region read: no bus,
@@ -1458,3 +1540,21 @@
     CHECK(rig.dev.booted.empty());
 }
 
+TEST_CASE("a menu whose chatter lands ahead of its answers is read as a menu, not as a game", "[n8][menu-guard]") {
+    // The guard's read of the config block takes the reply's first bytes as they come. A cart that talks while the host
+    // is not reading leaves bytes in the input, and a reply read behind them is garbled: map_idx 255 with one status
+    // byte in front of it reads as a game, which would leave the guard streaming into the menu. Flushing the input
+    // before the read is what keeps the answer the cart's own.
+    Rig rig;
+    rig.dev.chatterMs      = 40;  // one byte whenever the cart owes the host no reply
+    rig.dev.chatterGarbles = true;  // ... left in the input for the next read, as one said between reads is
+    rig.connect();                  // at its menu: the fake's default, so the connect-time read seeds "menu"
+    REQUIRE(rig.host.link().atMenu());
+    rig.push({0x90, 0x2A, 0x73});  // '*s' if the menu ever read it
+    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // several read cadences
+    CHECK(rig.host.link().atMenu());
+    CHECK_FALSE(rig.cart.connected());
+    CHECK(rig.dev.menu);
+    CHECK(rig.dev.booted.empty());
+    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
+}
````

- [ ] **Step 6: run them, and see them fail**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[cart-release]"
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[menu-guard]"
```

Expected: FAIL, the three new cases and nothing else. The cart's release never goes (`waitUntil` false: `chunksWritten_`
never reaches the push count), the handover cuts the key the new node struck (`1 == 0`), the clock opens the release
gate (`1 == 0`), and the guard reads the chattering menu as a game and leaves it (`atMenu()` false):

```
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1364: FAILED:
  CHECK( rig.dev.count("fifo:80 3C 00") == 0 )
with expansion:
  1 == 0
...
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1320: FAILED:
  REQUIRE( waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }) )
with expansion:
  false
...
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1347: FAILED:
  CHECK( rig.dev.count("fifo:80 3C 00") == 0 )
with expansion:
  1 == 0
...
test cases:  4 |  2 passed | 2 failed
assertions: 86 | 83 passed | 3 failed
```

```
/workspaces/RetroPlug-main/cuttlefish/packages/n8/native/test/N8CartJobs.test.cpp:1546: FAILED:
  REQUIRE( rig.host.link().atMenu() )
with expansion:
  false
...
test cases:  6 |  5 passed | 1 failed
assertions: 49 | 48 passed | 1 failed
```

- [ ] **Step 7: count the discards, flush before every read, and step over real-time bytes**

Four fixes ride this step, and they are all in `N8Link`, plus the two cases whose `flushInput` count the guard's own
read now shares (below).

- **Every chunk taken off the ring counts as written** (`discardRing`, the at-menu chunk in hand, a job drain's at-menu
  drop), because a release's mark is a push count and a dropped chunk has still gone; `lastOn_` is reset at the menu,
  which holds no notes of ours.
- **`readMenuState` flushes the input on every path**, and the connect-time seed flushes before its read too: the reply
  must not begin with what the cart already said, or a menu's chatter reads as a game. The forced read's flush moves
  into it.
- **The overdue branch's failed read backs off for the failure bound**, not just the answered-as-a-game one: a window
  that keeps timing out then costs the stream one read a second, not 100 ms of every turn.
- **The message-boundary tracker steps over 0xF8 and up**, and resets at connect and on entering the menu, so a
  real-time byte is not a boundary and a connection that ended mid-SysEx does not leave the gate open for ever.

The guard's own read flushes `FakeN8`'s input now, which is what the two cases that count `flushInput` calls around a
job must account for: the dump case arms its failure while the job is inside its run (the job owns the port, so no
other flush can take it), and the upload case uses `FakeN8.failFlushAtGame`, a flush after the boot, rather than a
count.

````patch
diff --git a/packages/n8/native/test/FakeN8.hpp b/packages/n8/native/test/FakeN8.hpp
--- a/packages/n8/native/test/FakeN8.hpp
+++ b/packages/n8/native/test/FakeN8.hpp
@@ -60,6 +60,7 @@
     int                      fileReadDelayMs = 0;   // likewise each SD file read block
     bool                     failNextWrite = false; // the next SD file write answers its first block with an error
     int                      failOnFlushInput = 0;  // the Nth flushInput from now fails, as a port that just died does
+    bool                     failFlushAtGame = false; // every flush after the cart booted fails (an upload's last)
     bool                     failNextRead = false;  // the next SD file read block answers an error
     bool                     failNextList = false;  // the next directory load answers a disk error
     int                      filesOpen = 0;         // files opened on the card and not yet closed
@@ -159,7 +160,10 @@
         std::lock_guard<std::mutex> l(dev_.mu);
         failure_.clear();
         out_.clear();
-        if (dev_.failOnFlushInput > 0 && --dev_.failOnFlushInput == 0) failure_ = "fake port: flushInput failed";
+        if (dev_.failFlushAtGame && !dev_.booted.empty())
+            failure_ = "fake port: flushInput failed";
+        else if (dev_.failOnFlushInput > 0 && --dev_.failOnFlushInput == 0)
+            failure_ = "fake port: flushInput failed";
     }
     std::string failure() const override {
         std::lock_guard<std::mutex> l(dev_.mu);
diff --git a/packages/n8/native/test/N8CartJobs.test.cpp b/packages/n8/native/test/N8CartJobs.test.cpp
--- a/packages/n8/native/test/N8CartJobs.test.cpp
+++ b/packages/n8/native/test/N8CartJobs.test.cpp
@@ -563,11 +563,17 @@
           "[n8][cart-jobs]") {
     Rig rig;
     rig.dev.files[kGameBattery] = saveOf(1024, 3);
+    rig.dev.fileReadDelayMs     = 20;  // long enough to arm the failure while the job is inside its run
     rig.connect();
-    // The flush after the job throws: the transfer itself completed, so its completion is the one that was decided.
-    rig.dev.failOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after it ran
     Job job;
     REQUIRE(rig.submit(battery("dump"), job));
+    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
+    // The flush after the job throws: the transfer itself completed, so its completion is the one that was decided. No
+    // other flush can take it: the job owns the port while it runs, so the stream's own reads (and their flushes) wait.
+    {
+        std::lock_guard<std::mutex> l(rig.dev.mu);
+        rig.dev.failOnFlushInput = 1;
+    }
     REQUIRE(waitUntil([&] { return job.finished(); }));
     CHECK(job.state.load() == Job::Done);
     CHECK(job.result == saveOf(1024, 3));
@@ -579,7 +585,9 @@
 TEST_CASE("an upload whose trailing flush throws still closes the port it booted from", "[n8][cart-jobs]") {
     Rig rig;
     rig.connect();
-    rig.dev.failOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after the boot
+    // The flush after the boot throws: the boot itself is decided, so its completion is the one that was decided (the
+    // port stays closed after it, so every later flush would fail too).
+    rig.dev.failFlushAtGame = true;
     Job job;
     REQUIRE(rig.submit(uploadOf(inesImage(8)), job));
     REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));
diff --git a/packages/n8/native/src/n8/desktop/N8Link.cpp b/packages/n8/native/src/n8/desktop/N8Link.cpp
--- a/packages/n8/native/src/n8/desktop/N8Link.cpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.cpp
@@ -107,6 +107,11 @@
     { TimedChunk drop; while (ring_.tryPop(drop)) {} }
     chunksWritten_ = chunksPushed_.load(std::memory_order_acquire);  // the ring is empty: nothing pushed is still to go
     std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);     // a fresh connection's cart holds no note of ours
+    // Nor is a prior connection's message still open on this one's wire: a port that ended mid-SysEx would otherwise
+    // hold the release gate open until an F7 that is never coming.
+    wireExpect_      = 0;
+    wireSysex_       = false;
+    wireOpenSinceNs_ = 0;
     // The cart's at-menu state, on the fresh port and before any producer may push: a menu reads the FIFO as commands,
     // so the state decides whether anything may be streamed at all. A check that fails to answer leaves the state as it
     // was (not the menu), which streams as the link always did.
@@ -116,6 +121,9 @@
     menuGameUntilNs_ = 0;
     if (menuCheck_) {
         try {
+            // What the cart said after the handshake is not this read's reply: a flush keeps a chattering cart's byte
+            // out of the block's first byte, which would read as a game (see readMenuState).
+            flushPort();
             atMenu_.store(menuCheck_(*edio_) == 1, std::memory_order_relaxed);
         } catch (const std::exception&) {
         }
@@ -348,13 +356,13 @@
     if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
         const bool forced = menuHoldSince_ != 0 && now - menuHoldSince_ >= kMenuHoldNs;
         if (quiet || forced) {
-            if (!quiet) flushPort();  // the read's answer must not begin with what the chatter left in the input
             const bool answered = readMenuState(now);
             menuHoldSince_ = 0;
             if (atMenu_.load(std::memory_order_relaxed)) return true;
             // The cart answered as a game: its acks are merely lost or damaged, and the stream goes on as it always
-            // did, at one read a second rather than one a turn.
-            if (answered) menuGameUntilNs_ = now + kMenuHoldNs;
+            // did, at one read a second rather than one a turn. A read that did NOT answer waits the same (its own
+            // failure bound), so a window that keeps timing out costs the stream one read a second, not 100 ms a turn.
+            menuGameUntilNs_ = now + (answered ? kMenuHoldNs : kMenuReadFailNs);
             return false;
         }
         if (menuHoldSince_ == 0) menuHoldSince_ = now;
@@ -369,6 +377,16 @@
 // next one off for a second (see kMenuReadFailNs).
 bool N8Link::readMenuState(std::int64_t now) {
     menuReadDueNs_ = now + kMenuReadNs;
+    // Whatever the cart has already said goes first: the reply to this read must not begin with bytes a chattering cart
+    // left in the input, which would take the place of the reply's own (map_idx 255 behind one status byte reads as a
+    // game, and a game re-enables the stream into the menu). Every path flushes, not only the forced read: a raw link
+    // has nothing else draining its input between turns.
+    try {
+        flushPort();
+    } catch (const std::exception&) {
+        menuReadDueNs_ = now + kMenuReadFailNs;  // as a read that did not answer: the state stands, and the next waits
+        return false;
+    }
     int answer = -1;
     try {
         answer = menuCheck_(*edio_);
@@ -397,8 +415,13 @@
 
 void N8Link::enterMenu() {
     // Drop, not hold: a console reset left the game that held these notes, the menu holds none, and a game booted later
-    // starts fresh. The ring, the reliable window and any pending release go with it.
+    // starts fresh. The ring, the reliable window, any pending release and the books on what the cart's parser was
+    // inside go with it: a menu's FIFO is a command parser, not the game's stream.
     discardRing();
+    std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);  // a menu holds no note of ours to skip a release for
+    wireExpect_      = 0;
+    wireSysex_       = false;
+    wireOpenSinceNs_ = 0;
     if (rel_) {
         rel_->reset();
         reliable_.store(false, std::memory_order_release);
@@ -427,6 +450,9 @@
 void N8Link::discardRing() {
     TimedChunk drop;
     while (ring_.tryPop(drop)) {
+        // Taken off the ring is gone: a release's mark is a push count, so a dropped chunk counts as written, or every
+        // later release would wait for one nobody will ever push.
+        ++chunksWritten_;
         chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
         pushesDropped_.fetch_add(1, std::memory_order_relaxed);
     }
@@ -522,6 +548,7 @@
                 // trip's own discard cannot see a push that raced it. Drop it here, and keep dropping until a game.
                 if (have) {
                     have = false;
+                    ++chunksWritten_;  // gone with the rest of the ring: a release's mark counts pushes, not writes
                     chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
                     pushesDropped_.fetch_add(1, std::memory_order_relaxed);
                 }
@@ -601,6 +628,9 @@
 void N8Link::noteWireBytes(const std::uint8_t* data, std::size_t n, std::int64_t now) {
     for (std::size_t i = 0; i < n; ++i) {
         const std::uint8_t b = data[i];
+        // A real-time byte (0xF8 and up) may stand between any two bytes of a message and changes nothing: read as a
+        // status it would look like the message ended, and a release written there would be its payload.
+        if (b >= 0xF8) continue;
         if (wireSysex_) {
             if (b == 0xF7) wireSysex_ = false;
             continue;
@@ -623,7 +653,7 @@
 
 // One complete MIDI message's data bytes, from its status: 2 for most channel messages, 1 for programme and channel
 // pressure, and 0 for the System Common ones that carry none (a real-time byte is one on its own, and F0 is the SysEx
-// open this tracker follows instead).
+// open this tracker follows instead; noteWireBytes steps over real-time bytes, so none of them arrives here).
 std::size_t N8Link::wireDataBytes(std::uint8_t status) {
     switch (status & 0xF0) {
         case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
@@ -648,6 +678,7 @@
     // At the menu nothing the ring still holds may go: the cart reads it as commands. The job's own transfers use the
     // port directly, which is what a menu is for.
     if (atMenu_.load(std::memory_order_relaxed)) {
+        if (have) ++chunksWritten_;  // a chunk taken off the ring and dropped has still gone (see discardRing)
         have = false;
         discardRing();
         return;
diff --git a/packages/n8/native/src/n8/desktop/N8Link.hpp b/packages/n8/native/src/n8/desktop/N8Link.hpp
--- a/packages/n8/native/src/n8/desktop/N8Link.hpp
+++ b/packages/n8/native/src/n8/desktop/N8Link.hpp
@@ -257,15 +257,20 @@
     // wrote (its own, reset to the first at each connect): a release waits until the second reaches what the first
     // read when it was asked, so it goes out behind everything pushed before it.
     std::atomic<std::uint64_t> chunksPushed_{0};
+    // Every chunk TAKEN off the ring, written or dropped (the menu's discards, the at-menu chunk in hand, a job
+    // drain's), because a release's mark is a push count: a chunk that was dropped has still gone, and a release that
+    // waited for it would wait for ever. Serial thread only, reset to the first at each connect.
     std::uint64_t              chunksWritten_ = 0;
     // Per key (channel * 128 + note), the index of the chunk whose note-on the serial thread last wrote, or kNoChunk:
     // cleared by a note-off it writes. It is what tells a release for a key the node taking over struck (at or past the
-    // mark it waits behind) from one for a key nothing holds any more. Serial thread only, reset at each connect.
+    // mark it waits behind) from one for a key nothing holds any more. Serial thread only, reset at each connect and
+    // whenever the cart is at its menu, which holds no notes of ours.
     static constexpr std::uint64_t kNoChunk = ~std::uint64_t{0};
     static constexpr std::size_t   kNoteKeys = 16 * 128;
     std::uint64_t              lastOn_[kNoteKeys] = {};
     // The shape of the byte stream the cart has been sent (noteWireBytes): the data bytes still owed by the message
-    // open, whether a SysEx is one of them, and when that began (0: at a message boundary). Serial thread only.
+    // open, whether a SysEx is one of them, and when that began (0: at a message boundary). Serial thread only, reset
+    // at each connect and at the menu, whose stream is a command parser's, not ours.
     int                        wireExpect_    = 0;
     bool                       wireSysex_     = false;
     std::int64_t               wireOpenSinceNs_ = 0;
````

- [ ] **Step 8: run them all, and see them pass**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-n8-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[cart-release]"
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test "[menu-guard]"
timeout -k 10 600 .superpowers/kb4plan/guarded.sh build/bin/retroplug-n8-test
```

Expected: PASS, the new cases with the ones beside them and then the whole binary:

```
All tests passed (87 assertions in 4 test cases)
All tests passed (54 assertions in 6 test cases)
...
All tests passed (1670 assertions in 121 test cases)
```

- [ ] **Step 9: the wider suites this task reaches**

```
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-engine-graph-test -j$(nproc)
timeout -k 10 300 .superpowers/kb4plan/guarded.sh build/bin/retroplug-engine-graph-test "[n8-adapters]"
timeout -k 10 900 .superpowers/kb4plan/guarded.sh build/bin/retroplug-engine-graph-test
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh cmake --build build --target retroplug-device-mode-test -j$(nproc)
timeout -k 10 600 .superpowers/kb4plan/guarded.sh build/bin/retroplug-device-mode-test
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:n8
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test:native n8
timeout -k 10 1800 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish test
timeout -k 10 1200 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish typecheck
```

Expected: PASS all of them:

```
All tests passed (56 assertions in 1 test case)
test cases:     342 |     331 passed | 11 skipped
assertions: 1702512 | 1702512 passed |  0 skipped
All tests passed (1504408 assertions in 96 test cases)
# 2 plugin test binary(ies) passed (jobs=16)
# 3 native test file(s) passed (jobs=16)
# 243 test file(s) passed (jobs=16)
typecheck: Done
```

(`retroplug-engine-graph-test`'s assertion count moves by a few between runs - a timing case asserts per iteration - so
that line is one run's, not a constant.)

`retroplug-device-mode-test` is here for the same reason as in task 4: this step changes `N8Node.cpp`, which the
device-mode build compiles, and its link's virtuals.

- [ ] **Step 10: commit**


```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/n8-wire/native/src/transport/N8ReliableLink.hpp \
  packages/n8/native/src/n8/desktop/N8Link.hpp packages/n8/native/src/n8/desktop/N8Link.cpp \
  packages/n8/native/src/n8/desktop/N8Host.hpp packages/n8/native/src/n8/desktop/N8Host.cpp \
  packages/n8/native/src/n8/desktop/N8CartLink.hpp packages/n8/native/test/N8CartJobs.test.cpp \
  packages/n8/native/test/FakeN8.hpp packages/n8/native/test/N8CartLink.test.cpp \
  packages/n8/native/test/N8Provider.test.cpp \
  packages/native/test/engine-graph/N8Adapters.test.cpp packages/n8/ts/index.ts \
  packages/n8/ts/ui/index.ts packages/n8/ts/test/link-client.test.ts \
  packages/retroplug/test-ui/ui-services.test.ts packages/retroplug/test/menu/n8-sd-ops.test.ts \
  packages/retroplug/test/menu/recorded-cases.ts packages/retroplug/test/services.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(kb4): a cart at its menu is not streamed to, and reads disconnected there

The N8 menu reads its commands over the same FIFO a game reads MIDI from, so
streamed MIDI could be read as one of them (0x2A 0x73 is *s: start an app), and
the stream also kept a job's menu probe from ever finding a quiet turn. The link
now keeps the cart's at-menu state, read from the FPGA config block (a
system-region read) and seeded at connect: every 250 ms on a quiet turn, and at
once when the framed window stops moving, which is a console reset. A menu that
never goes quiet is read anyway after a second, since the quiet rule stamps the
last heard on any byte. At the menu pushes are dropped and counted, the ring,
the reliable window and any pending release are discarded, nothing is written,
and a cart node's connected reads 0, so Settings > N8 says where the cart is. The
job probe's own read feeds the same state, so a pause cannot drain the ring into
a menu it has just seen. Jobs still run there, since the card is reachable only
at the menu.
MSG
```

---

## Task 6: the documents

No test fails for a document, so this task is one step (plus the link check and the commit). It removes the four fixed
entries from [spec/07](../spec/07-remaining-work.md)'s known bugs and turns the fifth into two lines under Before merge -
the hardware check the quiet rule rests on, and the residual window the guard cannot close - adds the menu guard and
`connected` to the N8 bridge's section and the quantise's two clocks to the controller role's in
[spec/13](../spec/13-subsystems.md), fixes [spec/06](../spec/06-build-test.md)'s line about node-less markers, and
states the plugin lifecycle's thread rule in [spec/02](../spec/02-native-host.md), which owns that topic. The residual
is stated in the terms the code has, not the ones the design guessed at: framed, the hold starts when the window has
STOPPED MOVING (the oldest frame unacknowledged past the link's timeout, not a silent link), what reaches the menu is
every frame written in that wait (re-sends included, not one frame in flight), a read that answers "a game" is trusted
for a second (so a reset inside it streams until the trust expires), and a chattering menu now costs a second's delay
and a read it may garble rather than a refused probe. The comment
above the plugin's lifecycle overrides was task 3's, and the design's as-built and execution sections are the
controller's.

**Files** (line numbers are the file at `ccc01889e`):

- Modify: `spec/07-remaining-work.md:30-36` (the checklist group and its five lines), `:217-276` (the five sections),
  and two new sections under `## Before merge` (`:104-110`): `### The N8 menu's back-channel` and
  `### The N8 menu's residual window`, each with its checklist line in that group.
- Modify: `spec/06-build-test.md:156-162` (the product order's paragraph).
- Modify: `spec/13-subsystems.md:104-134` (the N8 bridge), `:190-205` (the controller role).
- Modify: `spec/02-native-host.md:786-798` (the block drivers and the lifecycle).

### Steps

- [ ] **Step 1: write the documents**

````patch
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index f4aea81d6..4b871323a 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -496,11 +496,14 @@ volume reaches it, and only on a turn quiet for the read: nothing outstanding on
 for 250 ms since its oldest outstanding frame first went out, as one at its menu is), so a transfer a running game
 will refuse is refused without pausing the stream and dropping the block's notes; the submitting thread waits for that
 read, up to its 1 s bound. The cart node keeps books on the notes the cart holds: it re-sends at frame 0 the note-offs
-a pause dropped, and its destroy hands what is still sounding to the link (`CartLink::releaseNotes`), whose serial
-thread writes their note-offs behind every byte pushed before the call (a disconnect writes them before the port
-closes; a link that is down drops them), so a node leaving the plan mid-note leaves no tone on the console once its
-plan is reclaimed: within a frame of the swap where the host has an idle tick (the plugin with its editor open, the
-SDL standalone), and at the next publish or deactivate in a plugin whose editor is closed. The engine keeps at most
+a pause dropped, and its destroy hands what is still sounding to the link (`CartLink::releaseNotes`), with the push
+count it recorded after its own last push (`pushMark`): the serial thread writes the note-offs behind THAT mark, and
+skips a key whose last note-on went out at or past it, so the node taking over keeps a key it struck while the old one
+leaving is released (a disconnect writes them before the port closes; a link that is down drops them, and a cart at its
+menu holds no notes of ours, so they are dropped there too). A node leaving the plan mid-note therefore leaves no tone
+on the console once its plan is reclaimed: within a frame of the swap where the host has an idle tick (the plugin with
+its editor open, the SDL standalone), and at the next publish or deactivate in a plugin whose editor is closed. The
+engine keeps at most
 `kMaxJobs` (64) outstanding, engine-wide back-pressure that leaves room for every completion, and a request names a
 capability from the one reserved id space (`rp.system.memory/1`, `rp.system.sram/1`, `rp.system.rom/1`, in
 `rp_jobs.h`), so the engine asks a node only by an id it knows and never casts an answer.
@@ -788,9 +791,36 @@ commands' payloads unapplied.
 The block drivers are the same shape: the plugin's
 [`run()`](../packages/native/plugin/PluginDSP.cpp) (stage the DAW's MIDI → `drainInto` → set
 bpm/transport direct → `processBlock`), `audioLoop` for the test host, and `renderBlocks` for the
-harness. The plugin's [`activate` / `deactivate`](../packages/native/plugin/PluginDSP.cpp) flip the
-ownership bit around DPF's lifecycle (`activate` first sets the block size),
-draining and reclaiming on the way out.
+harness.
+
+**The plugin's lifecycle thread rule.** `activate`, `deactivate`, `bufferSizeChanged` and `sampleRateChanged` are the
+FORMAT's callbacks, and only some formats call them on the control thread: CLAP, VST3 and AU do, while DPF's JACK host
+runs them from JACK's notification callback and VST2 from whichever thread its host dispatches the block-size and
+sample-rate opcodes on. Doing the engine work there would race the editor's per-frame reclaim and edits, and the
+latency/parameter-map calls would reach the shared QuickJS context the control thread pumps. So the plugin's overrides
+only record what the format asked for, and
+[`LifecycleHandoff`](../packages/native/plugin/LifecycleHandoff.hpp) decides, in two tiers. A buffer size the plan
+already holds - `Engine::kDefaultMaxBlockFrames` (8192) and up, and the plan never shrinks - is a no-op on any thread:
+the engine never leaves the audio thread, so there is nothing to drain or reclaim. A rate change, or a size above the
+plan, parks `run()`: it writes silence and holds its block's MIDI in a fixed buffer, touching nothing. The control
+thread's idle tick (the editor's per-frame `reclaimReleased`) then does the whole quiescent handoff there - take the
+engine, drain, reclaim, apply the rate and the size, hand it back, report latency and re-label the CC map - and the
+first resumed block replays the held MIDI at frame 0. `run()` reads the state with acquire and stores Parked only on
+entry to a later block, so the tick never overlaps a block that touched the engine, and the foreign callbacks are
+serialised against it by one mutex `run()` never takes. `getState` and `setState` also apply a pending park, for a
+host with no idle tick, and a park longer than a second is logged once. `retroplug-plugin-test`'s `LifecycleHandoff`
+cases drive the policy from a `std::thread`, as JACK's callback does.
+
+Who owns the engine is tracked separately from whether a block may drive it, because a format can move it between the
+two on a thread of its own. A foreign `activate` when the engine is the control thread's - after a control-thread
+`deactivate`, or the plugin's very first, which DPF's `run()` reaches by activating when `fIsActive` is false - only
+requests the give: the next block parks and the tick gives the engine from the control thread, because handing it to a
+thread that is already running would put two threads in the command and release rings. A foreign `deactivate` that no
+`activate` follows (a VST2 `SUSPEND(0)` off the host's main thread) is taken back once it has stood for 250 ms and
+KEPT - the state goes suspended, no give and no resume, so an edit applies inline while the plugin is suspended - and
+the activate that comes later asks for the give. The grace period is what lets a paired JACK or VST2 sequence cost
+nothing, since its two calls are not one. A control-thread `deactivate` takes and keeps the engine too, as every
+format's did before the policy.
 
 ## `SnapshotRegistry`: the read door
 
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index adc4f9a4e..0c833b74c 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -156,10 +156,13 @@ the lists: its 21 packages hold the plugin's 15. The desktop's run also writes t
 extends) and `tsconfig.paths.json` (the same entries plus the shell's own aliases, from `tsconfig.aliases.json`: a
 child's `paths` REPLACE its parent's, so the shell's program needs one file carrying both). The product list is the
 **dependency order and the registration order both** (a package before the ones that use it, the shell last), and only a
-ROM provider's place in it is behaviour, and a tracker integration's for a system storing two node-less markers ([a
-known bug](07-remaining-work.md#a-tracker-marker-with-no-protocol-node)): the providers run in that order, and the first
-to match a ROM fixes a system's role-list order. A role's kind, a kit format's id and a tracker integration's marker
-role are otherwise looked up rather than sequenced (one package's roles interleave another's without consequence), so
+ROM provider's place in it is behaviour: the providers run in that order, and the first to match a ROM fixes a system's
+role-list order. A tracker integration's marker role is looked up, not sequenced: a load moves the marker today's ROM
+earns ahead of a stale one, and `resolveTracker` takes the first placed marker in the SYSTEM's own role order, so the
+product order decides only when the ROM earns neither (`SystemsStore.suggestedFirst`,
+[trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)). A role's kind, a kit format's id and a tracker
+integration's marker role are otherwise looked up rather than sequenced (one package's roles interleave another's
+without consequence), so
 for those a duplicate is the hazard: the generated module refuses a role kind, a kit format id or a marker role declared
 twice, each with its own message. The generator itself refuses a list that puts a package before a dependency, a
 dependency the product does not carry whose contribution would be lost, a dependency on the shell, a duplicate product
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index 8552cb351..461e420c8 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -9,6 +9,8 @@ than work.
 ## Checklist
 
 **Before merge**
+- [ ] [The N8 menu's back-channel](#the-n8-menus-back-channel)
+- [ ] [The N8 menu's residual window](#the-n8-menus-residual-window)
 - [ ] [The Launchpad real-device checklist](#the-launchpad-real-device-checklist)
 - [ ] [The Launchpad's Windows items on a real device](#the-launchpads-windows-items-on-a-real-device)
 - [ ] [The overlay strip placement on a real host](#the-overlay-strip-placement-on-a-real-host)
@@ -27,13 +29,6 @@ than work.
 - [ ] [A config section paired with its UI service](#a-config-section-paired-with-its-ui-service)
 - [ ] [F5a's other leftovers](#f5as-other-leftovers)
 
-**Known bugs**
-- [ ] [A tracker marker with no protocol node](#a-tracker-marker-with-no-protocol-node)
-- [ ] [The JACK standalone's control handoff](#the-jack-standalones-control-handoff)
-- [ ] [A cart-node handover can cut a key](#a-cart-node-handover-can-cut-a-key)
-- [ ] [The N8 menu, unverified on a cart](#the-n8-menu-unverified-on-a-cart)
-- [ ] [A bar or beat quantise counts host bars](#a-bar-or-beat-quantise-counts-host-bars)
-
 **Engine**
 - [ ] [DAW automation of node params](#daw-automation-of-node-params)
 - [ ] [P3: cores borrow their pinned ROM](#p3-cores-borrow-their-pinned-rom)
@@ -98,6 +93,29 @@ than work.
 
 What `feature/modularize` owes before it merges to main.
 
+### The N8 menu's back-channel
+
+The menu guard reads the cart's FPGA config block to tell a menu (map_idx 255) from a game, and drops the stream there
+([spec/13](13-subsystems.md#the-everdrive-n8-bridge)). The quiet rule that decides when that read may be made assumes
+the menu writes nothing unprompted on the back-channel: unsolicited bytes keep the quiet rule's `lastHeardNs` fresh, so
+a menu with stale frames reads as one that never goes quiet, and the guard only looks when the hold's one-second bound
+trips it. That bound now always ends the wait, so the cost is a second's delay and a read the chatter may garble, not a
+probe that is refused. Confirm on a cart at its menu, with nothing streamed to it, that the back-channel stays silent,
+and that a job submitted right after a power-cycle to the menu is accepted, and promptly.
+
+### The N8 menu's residual window
+
+A cart at its menu is dropped rather than streamed to, but the guard knows where the cart runs only when a read lands,
+so a little streamed traffic can reach the menu before that: raw, up to one read cadence - 250 ms of MIDI - and framed,
+every frame written before the hold, which is the whole window the link waits before it reads (its own 100 ms timeout,
+re-sent on each turn of it). Two smaller ones go with them: a read that answers "a game" while the window is stopped is
+trusted for a second, so a reset inside that second streams to the menu until it expires, and the same second is what a
+read that does not answer waits behind. The framed case cannot be made airtight from this side: the first frames after a
+reset go out before anything can know a menu came back, which is why the batch's framed reset case asserts what the
+guard does promise - nothing after the trip - rather than that nothing arrived. A config read before every chunk would
+close both windows, at a 100 ms USB round trip between messages.
+([spec/13](13-subsystems.md#the-everdrive-n8-bridge).)
+
 ### The Launchpad real-device checklist
 
 Design section 8's ten steps, about ten minutes on the desk with the Pro MK3 and LSDj in a ROM, have never run; they
@@ -214,61 +232,6 @@ load-sensitive; contribution checks run at load (a malformed pack takes the cont
 registry has no unregister); and a project service is tied to `@retroplug/tracker` and the editor's song watch. ([F5a
 design](../docs/modular-graph-phase-f5a-design.md) sections 12 and 14.)
 
-## Known bugs
-
-### A tracker marker with no protocol node
-
-The `risa` and `bliptoaster` marker roles have no protocol node, so they count as placed whenever stored: a NES project
-storing both (a risa project whose ROM is now BlipToaster) carries both markers, and `resolveTracker`
-([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)) takes the first in product order. ([Known bugs
-batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)
-
-### The JACK standalone's control handoff
-
-The DPF JACK standalone's control handoff can run on JACK's thread. A JACK buffer-size or sample-rate change runs
-`PluginDSP::deactivate`, the change and then `activate` from DPF's notification callback (`DistrhoPluginJACK.cpp`'s
-`jackBufferSizeCallback` / `jackSampleRateCallback` -> `setBufferSize(n, true)`), not on the main thread every other
-control path of the plugin runs on, while the editor's `uiIdle` reclaims on that main thread every frame. So
-`deactivate` hands the engine back on JACK's thread: `setAudioThreadOwns(false)` clears the ownership bit (every push
-then flushes inline on whichever thread makes it), `drainInto` drains the command ring there, `reclaimReleased` takes
-the release ring there, and the script host's reclaim runs a pack's GC and free there - a second consumer of the ring,
-and two threads in `JS_RunGC` / `freePack` is undefined behaviour. `activate` can likewise grow the plan on that thread
-when the new buffer exceeds the plan's maximum (`setMaxBlockFrames` -> `GraphHost::reactivate`, which replans and
-publishes) and re-declare the DAW parameters there. The fix: marshal that whole handoff (the flush, the ownership flip,
-the drain, the release-ring reclaim, the script GC and free, and activate's replan and parameter re-declaration) to the
-control thread, or make a buffer-size or rate change a message that thread applies, so the quiescent engine has one
-owner. Rare and manual: a JACK buffer-size change in the standalone; CLAP, VST3 and AU call deactivate and activate on
-the host's main thread, and VST2 runs `setBufferSize(n, true)` from its block-size opcode on whichever thread its host
-calls that from ([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), above `activate`). Only `activate`'s
-`setMaxBlockFrames` is a no-op for a block within the plan's maximum, which is why the path went unnoticed; C5's
-per-frame reclaim widened its window from an edit to any frame. (Found after C5; no design records it. [C5
-design](../docs/modular-graph-phase-c5-design.md) section 10 records the per-frame reclaim that widened its window.)
-
-### A cart-node handover can cut a key
-
-A handover can cut a key the new cart node strikes before the old one is reclaimed: the old node's held notes are
-released behind every byte pushed when its destroy runs, the new node's first blocks among them, so a note the new node
-starts on a key the old one still held, between the swap and the reclaim, is released with them. Where an idle tick
-reclaims that is a frame or so; in a plugin whose editor is closed it runs to the next publish, the editor opening or a
-deactivate. ([C5 design](../docs/modular-graph-phase-c5-design.md) section 11.)
-
-### The N8 menu, unverified on a cart
-
-Three behaviours of a cart at its menu are reasoned, not seen. MIDI streamed to a cart at its menu reaches the menu's
-own command parser, which takes commands over the same FIFO, so a data pair such as `0x2A 0x73` (`*s`) can ask it to
-start an app; it holds for raw MIDI after a connect at the menu and for framed MIDI after a console reset, where the
-stale window is re-sent every 100 ms until it re-syncs. The quiet rule assumes the menu writes nothing unprompted on the
-back-channel: unsolicited bytes would keep `lastHeardNs` fresh and a menu with stale frames would read as never quiet.
-And a heavy stream into a menu can starve the probe: the window's 48-frame re-sync re-stamps the oldest frame's first
-send, so past about 190 messages a second (48 per 250 ms) no turn is quiet and a submission is refused. ([C5
-design](../docs/modular-graph-phase-c5-design.md) section 11.)
-
-### A bar or beat quantise counts host bars
-
-A quantise of `bar` or `beat` still counts the host's bars: at a tempo divisor of 2 a cart's phrase lasts two of them.
-It may be the behaviour wanted; decide, then fix it or state it in [spec/13](13-subsystems.md#the-controller-role).
-([Known bugs batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)
-
 ## Engine
 
 ### DAW automation of node params
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index f727fb51f..81fa5c7d8 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -123,6 +123,28 @@ Everdrive N8 Pro's cart FIFO, so the console itself plays. One-way, host to cart
   `rom.upload` leaves streaming stopped until the host reconnects. The CLI's n8 tools keep their own raw serial path
   (the battery dump among them, the one cart-memory read left); the capabilities deliberately offer no peek, poke,
   sniffer or FIFO reads.
+- **A cart at its menu is not streamed to.** The menu takes commands over the same FIFO a game reads MIDI from, so a
+  data pair inside a message (`0x2A 0x73` is `*s`: start an app) can reach it. `N8Link` keeps the cart's at-menu state,
+  read from the FPGA config block's map_idx (255 at the menu; a system-region read that touches neither the cart's bus
+  nor a game's FIFO) - seeded at connect, and re-read about every 250 ms on a turn quiet for a read, with a 100 ms
+  timeout, and at once when the framed window has STOPPED MOVING (the oldest frame unacknowledged past the link's own
+  100 ms timeout, which is the console reset), whose new frames and re-sends wait for that read and at most a second.
+  Every guard read flushes the input first, so a chattering cart's bytes cannot garble the block; a job's menu probe
+  reads unflushed, so it may trip the guard but never clear it. While it says menu, `push` answers false and counts, the
+  ring, the reliable window and any pending note release are discarded, and nothing is written; leaving the menu re-runs
+  the link probe. Jobs still run there (the SD card is reachable only at the menu), and a cart node's `connected` reads
+  0, so its owed note-offs follow the existing disconnected rule. Settings > N8's status row says "At the N8 menu (not
+  streaming)".
+  - **The residual window, in its two bounds.** The guard knows the cart's state only when a read lands, so bytes that
+    go out before it are not held back. Raw, that is up to one read cadence, 250 ms of streamed MIDI. Framed, the read
+    follows the quiet rule or the hold's bound once the window has stood still for the link's timeout, so what reaches
+    the menu is every frame written in that wait, re-sends included. The framed reset case therefore cannot be made
+    airtight from this side - the first frames after a reset are written before anything can know a menu came back -
+    and a read per chunk, which would put a 100 ms round trip between every message, is the only full fix
+    ([spec/07](07-remaining-work.md#the-n8-menus-residual-window)). A second's worth of that window is trusted: a read
+    that answers "a game" while the window is stopped suppresses the next look for a second, so a reset inside that
+    second streams until it expires. The hardware check the quiet rule rests on is
+    [spec/07](07-remaining-work.md#the-n8-menus-back-channel).
 - **Prove a change:** `pnpm test:n8` (protocol, no hardware; `packages/n8/native/test`), the `NesN8FifoTiming` and
   `N8Host` Catch2 cases, `pnpm test n8` and `pnpm test:native n8-session` and `pnpm test:native n8-sdk` (`connectN8`
   over the fake N8); for the box, `pnpm device:box`, then the rig's `loop.sh`, `stress.sh` and `faults.sh` by hand, each
@@ -184,9 +206,15 @@ The apps themselves are contributions (`controllerApps`, in two halves joined by
 control plane's half is `definePackage({ controllerApps })` (`id`, `label`, the app's config schema), which the
 kind's `accept` files in the pack's own `ts/registry.ts` table, the one `parseAppConfig` reads.
 
+- **MI.MAP's quantise, in two clocks.** The app's `quantise` knob has four timed values beside `immediate` and
+  `rowEnd`: `beat` and `bar` count the CART's clock, and `hostBeat` and `hostBar` the DAW's. The cart is clocked at
+  24 / tickDivisor ticks a quarter, so its beat is 24 x divisor DAW ticks and its bar 96 x divisor (one LSDj phrase),
+  phased from ppq 0; the host values keep 24 and 96 whatever the cart does. The divisor reaches the app on its
+  controller context (`ControllerCtx.tickDivisor`, read every block from the session's settings, so a change needs no
+  rebuild). A project at the default divisor 1 behaves exactly as it always has.
 - **Prove a change:** `pnpm test controller`, `pnpm test script/nodes/controller-session`,
-  `pnpm test graph/build-graph`, and `pnpm test:plugin` (`retroplug-launchpad-test` for the transport the
-  session's surface reads).
+  `pnpm test graph/build-graph`, `pnpm test lsdj-launchpad`, and `pnpm test:plugin` (`retroplug-launchpad-test` for
+  the transport the session's surface reads).
 
 ### Pico firmware (`pico/`)
 
````

- [ ] **Step 2: check the links**

```
timeout -k 10 600 .superpowers/kb4plan/guarded.sh pnpm -C /workspaces/RetroPlug-main/cuttlefish check:docs
```

Expected: clean, with the one warning the tree already carries (`docs/launchpad-plan.md:299`, a plan record's dead
link). A new file a task creates must be staged before this runs, since the checker refuses a link to an untracked
target: task 3's `LifecycleHandoff.hpp` is committed by then, and a run against an uncommitted tree needs
`git add -N` on it first (`ERROR spec/02-native-host.md:801 target is untracked` without):

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 115 file(s) clean, 1 warning(s)
```

- [ ] **Step 3: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add spec/02-native-host.md spec/06-build-test.md \
  spec/07-remaining-work.md spec/13-subsystems.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(kb4): the batch's four specs: the menu guard, the two clocks, the handoff rule

spec/07 loses the four known bugs this batch fixed, and its fifth entry
becomes two lines: confirm on a cart that a menu writes nothing unprompted on
the back-channel, which the guard's quiet rule assumes, and the residual window
the guard cannot close (a read per chunk is the only full fix).
spec/13 describes the N8 link's at-menu state and a cart node's connected, the
residual in its two bounds, and MI.MAP's two clocks; spec/06 says what a
node-less marker's order decides now; spec/02 states the plugin lifecycle's
thread rule, the ownership across a format's calls, and what a handover's release
waits behind.
MSG
```
