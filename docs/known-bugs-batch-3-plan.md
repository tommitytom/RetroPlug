# Known bugs, batch 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The eight entries from [spec/07](../../spec/07-remaining-work.md)'s known bugs that
[known-bugs-batch-3-design.md](../../docs/known-bugs-batch-3-design.md) settles: the type stripper's bundle builds from
any build directory, a settings edit to an empty project stops dirtying it, a visit to the audio menu starts a fresh
draft, an Apply the config refuses leaves the device alone and says why, Settings > MIDI reads the stream's own state
every poll, each hardware MIDI output is written from a thread of its own, one session pump reports what the old hosts
dropped, and `tjs.exit` unwinds so a normal exit runs the teardown; then the documents record the batch.

**Architecture:** Tasks 1-5 are the TypeScript control plane and one native hook. Task 1 gives
`tools/build-ts-stripper.mjs` an `absWorkingDir` and moves its two side files beside its output. Task 2 puts a
`isPristine()` and a `markSettingsChanged()` in `ProjectStore` and an early return in `setSection`. Task 3 gives the
audio menu an `onOpen` hook that calls the `resetAudioDraft` it already has. Task 4 turns
`UserConfigStore.setSection`'s boolean into `writeSection(): "written" | "unchanged" | "refused"`, makes
`ConfigHost.setSection` answer, and has `applyAudioDraft` gate the device on it and show a row. Task 5 adds a no-probe
`MidiDeviceHost.state()` (`__rp_midiState` natively) that the midi service's poll merges into its cached listing
while the menu is open. Task 6 adds `rp::midi::PortWriter` to the midi package (a preallocated SPSC ring and a writer
thread per hardware output, or an inline write) and a `MidiStream` mode, the SDL standalone being the one `Threaded`
consumer. Task 7 adds `packages/native/src/host/SessionPump.{hpp,cpp}`, which both hosts' mains drive one turn at a
time. Task 8 patches the txiki fork with an embedder exit hook (`TJS_SetExitHook`/`TJS_StopRequested`/`TJS_ExitTaken`)
and moves both hosts onto it, so a session's `tjs.exit` unwinds to `main` and its exit is terminal (a module whose
evaluation rejected with the exit's own marker counts as evaluated, which is what keeps the CLI's `--version` and
`--help` from reading as failed sessions). Task 9 is the docs.

**Tech Stack:** TypeScript with the repo's own TAP runners (`pnpm test`, `pnpm test:native`, `pnpm test:ui`), C++20 with
Catch2 (`retroplug-midi-test` through `pnpm test:plugin`), the two hosts (`retroplug-cli`, `retroplug-host`) and their
shared `SessionPump`, the nested txiki fork under `deps/dpf.js/deps/lv_binding_js/deps/txiki` (one patched commit, its
pointer bumped up the chain), `tools/run-cli-ts-tests.sh` for the real binary's exit, and
`tools/run-sanitizer.sh`'s ThreadSanitizer and AddressSanitizer legs.

**Spec:** [known-bugs-batch-3-design.md](../../docs/known-bugs-batch-3-design.md). Its section 9 lists the tasks and the
acceptance; the acceptance run is the controller's, after this batch lands.

**Provenance:** every code block below was applied to this worktree at `080c0c0ba` in this plan's order, test first and
then the implementation, and built and run before the plan was written: linux-x64, GCC 13.3 (`linux-x64-gnu13`), 32
cores, with the sibling fixture tree at `/workspaces/resources`. There is no prototype branch, so afterward the worktree
was restored to its starting state and the plan was replayed by text (`replay.mjs`): each task's patches applied in
order to copies of the touched files at `080c0c0ba`, the result byte-identical to the validated cumulative diff. Every
Expected line is quoted from a real run. A failing-test step's output is what the code without that step's change
produces, quoted as it printed. The one task with no failing test is Task 9, which changes documents only.

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

**Submodules.** Task 8 is the only task that touches one, and it says how, in order: a commit in the txiki fork, a
pointer bump in `lv_binding_js`, one in `deps/dpf.js`, then this repository's own. Nothing is pushed: the user pushes
their own work, and until the fork's commit is pushed CI cannot check out the RetroPlug one. `deps/sameboy` is dirty
from the configure and no step touches it.

**Hardware.** A real NES with an Everdrive N8 Pro, a Zoom L6, a capture card and MIDI interfaces hang off this
container, so nothing in this plan may open one. Every command below that starts the CLI, the test host, a
`pnpm test*` suite, a Catch2 binary or `tools/run-cli-ts-tests.sh` is written under the guarded wrapper, which preloads
the tracked device guard ([tools/sdl-audio-guard](../../tools/sdl-audio-guard)), self-tests it and refuses
`/dev/snd`, `/dev/video*`, `/dev/ttyACM*`, `/dev/ttyUSB*`, `/dev/serial/*`, `/dev/hidraw*` and `/dev/bus/usb/*` before
the syscall:

```
timeout -k 10 <seconds> .superpowers/kb3plan/guarded.sh <command...>
```

The wrapper is a work file of this plan's directory (`guarded.sh`, beside this plan in `.superpowers/kb3plan/`, which
is gitignored); it takes any command and returns its exit code, printing what the guard refused, if anything. Use 300
for one test file, 1800 for a whole suite. A refusal it prints is a finding: stop and find what tried to open a device.
MIDI is reached only through `FakeMidiBackend`, `FakePorts` and `RP_MIDI_PORT_ALLOW`. Never run `retroplug-sdl`, any
`sdl:*` or `reaper:*` script, or the verification kits: the controller runs acceptance.

## Ground rules

These are the repo's rules this batch must keep. They are not optional and no step repeats them.

- **Never leave the worktree**: prefix git with `git -C /workspaces/RetroPlug-main/cuttlefish` and pnpm with
  `pnpm -C /workspaces/RetroPlug-main/cuttlefish`. Never switch branches, never `git stash`, never `git reset --hard`,
  never push.
- **`deps/sameboy` is modified in a fresh tree and that is expected** (its tracked patches are applied at configure).
  Never stage, reset, stash or check out a path in it. No submodule is touched except by Task 8's named steps, each in
  its own commit, and never a pointer bump of `deps/sameboy`.
- **Never `rm -rf build`** (or any `build*/`). Build a target with `cmake --build <dir> --target <t> -j$(nproc)`: the
  default is single-threaded.
- **The em dash (U+2014) never appears** in code, comments, docs or commit messages. Use " - ", ":" or ";".
- **A commit message is a subject line, a blank line, then the body**, committed with `git commit -F -`. No
  `Co-Authored-By`, no "generated with" line, no attribution of any kind.
- **Match the surrounding code**: names, comment density and wrapping (comments explain WHY, in full sentences, at
  about 116-120 columns, and say what IS, not change history).
- **Never edit an existing `docs/*-plan.md` or design doc.** The batch's own design
  ([known-bugs-batch-3-design.md](../../docs/known-bugs-batch-3-design.md)) gains its as-built and execution sections
  from the controller, not from a task here.
- **Only a step's named paths are staged.** `git add` exactly what the step lists; a task's commit is its own
  `git diff` and nothing else.
- **Never send repository content to an external service.**
- **Fixture runs**: a native run that needs the LSDj, risa or render fixtures is given
  `RETROPLUG_RESOURCES_DIR=/workspaces/resources` on the same command line; CI runs the fixture-free cases alone
  (`RP_FAIL_ON_SKIP=1` with the fixtures is the serial kit's, the controller's).

---

## Task 1: the stripper bundle resolves from the repository

`tools/build-ts-stripper.mjs` sets no esbuild `absWorkingDir`, so its `typescript-5` alias resolves from the directory
the script is run in - which is CMake's binary directory, not the repository - and a build directory outside the
repository has no `node_modules` above it. It also writes its stub and its generated entry into the repository's
`build/native` whatever the output path is. The fix is `absWorkingDir: REPO` plus `nodePaths: [REPO/node_modules]`
(the entry no longer sits under the repository either, so `ts-blank-space`, which it imports by name, is looked for
there too), and the two side files written beside the output file.

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `tools/build-ts-stripper.mjs:32-38` (the stub's path), `:46-52` (the entry's), `:95-100` (the esbuild call).
- Create: `packages/retroplug/test/script/build-ts-stripper.test.ts` (107 lines).

**Interfaces:**

- Consumes: `esbuild`'s `absWorkingDir` and `nodePaths` options; `REPO`, which the script already resolves.
- Produces: nothing another task uses. The stub and entry land in `dirname(outFile)` from here on, which is what
  `packages/native/CMakeLists.txt`'s `retroplug-ts-stripper-bundle` step already passes as one path.

### Steps

- [ ] **Step 1: write the failing test**

The whole file: a Node child (esbuild is not a txiki module) chdirs to a fresh directory outside the repository, imports
the script with an output path there, and reports what it built and what the repository's `build/native` looked like
before and after.

````patch
diff --git a/packages/retroplug/test/script/build-ts-stripper.test.ts b/packages/retroplug/test/script/build-ts-stripper.test.ts
new file mode 100644
index 000000000..7fd0cf4ef
--- /dev/null
+++ b/packages/retroplug/test/script/build-ts-stripper.test.ts
@@ -0,0 +1,107 @@
+// The TypeScript type stripper's bundle is built by CMake from whatever directory the build runs in
+// (packages/native/CMakeLists.txt's retroplug-ts-stripper-bundle), and a build directory need not be inside the
+// repository: `cmake -S <repo> -B /tmp/rpbuild` is a normal configure. esbuild resolves an alias's value, and every
+// bare import, from its absWorkingDir, which defaults to the process's own directory, so without one the typescript-5
+// alias has no node_modules above it there and the build stops. The stub and the generated entry are build products of
+// this script too, so they belong beside the output file rather than in the repository's build/native whatever the
+// output path is.
+//
+// The build is Node's (esbuild is not a txiki module), so this runs it in a node child, which chdirs to a fresh
+// directory outside the repository before importing the script, as bundle-protocol.test.ts runs its builds.
+import { test, expect } from "@retroplug/testing";
+
+// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-protocol.test.ts declares them.
+interface ByteStream {
+  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
+}
+declare const tjs: {
+  env: Record<string, string | undefined>;
+  spawn(
+    args: string[],
+    opts: { stdout: "pipe"; stderr: "pipe" },
+  ): {
+    stdout: ByteStream | null;
+    stderr: ByteStream | null;
+    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
+  };
+};
+
+async function text(stream: ByteStream | null): Promise<string> {
+  if (!stream) return "";
+  const chunks: Uint8Array[] = [];
+  const reader = stream.getReader();
+  for (;;) {
+    const { done, value } = await reader.read();
+    if (done) break;
+    if (value) chunks.push(value);
+  }
+  let total = 0;
+  for (const c of chunks) total += c.length;
+  const all = new Uint8Array(total);
+  let at = 0;
+  for (const c of chunks) {
+    all.set(c, at);
+    at += c.length;
+  }
+  return new TextDecoder().decode(all);
+}
+
+/** What one run reports: the bundle's size and whether it defines the one global, whether the stub and the entry
+ *  landed beside it, and the two files' stamps in the repository's build/native before and after (null when absent). */
+interface Run {
+  work: string;
+  bytes: number;
+  defines: boolean;
+  stub: boolean;
+  entry: boolean;
+  before: { stub: string | null; entry: string | null };
+  after: { stub: string | null; entry: string | null };
+}
+
+/** Run tools/build-ts-stripper.mjs once from a fresh directory outside the repository, writing its output there. */
+async function runOutsideRepo(): Promise<{ run: Run | null; err: string; status: number }> {
+  const script = [
+    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
+    // The suite runs from packages/retroplug; the repository is two levels up.
+    'const repo = path.resolve(process.cwd(), "../..");',
+    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-stripper-"));',
+    'const out = path.join(work, "out", "stripper.js");',
+    'const stamp = (name) => { try { const s = fs.statSync(path.join(repo, "build/native", name));',
+    '  return s.size + ":" + s.mtimeMs; } catch { return null; } };',
+    'const snap = () => ({ stub: stamp("ts-stripper-stub.js"), entry: stamp("ts-stripper-entry.mjs") });',
+    "const before = snap();",
+    // cwd is what esbuild's default absWorkingDir is, and the script is outside it on purpose.
+    "process.chdir(work);",
+    'const real = path.join(repo, "tools/build-ts-stripper.mjs");',
+    // The output path is the script's only argument.
+    "process.argv = [process.argv[0], real, out];",
+    // The script's own progress line is not this driver's output, which is the JSON below.
+    "console.log = () => {};",
+    "import(url.pathToFileURL(real).href).then(() => {",
+    "  const made = fs.existsSync(out) ? fs.readFileSync(out, 'utf8') : '';",
+    "  const run = { work, bytes: made.length, defines: made.includes('__stripTypes'),",
+    "    stub: fs.existsSync(path.join(work, 'out/ts-stripper-stub.js')),",
+    "    entry: fs.existsSync(path.join(work, 'out/ts-stripper-entry.mjs')), before, after: snap() };",
+    "  process.stdout.write(JSON.stringify(run));",
+    // The scratch tree is this run's, and a run leaves nothing behind: the parent has what it needs in the JSON above.
+    "  fs.rmSync(work, { recursive: true, force: true });",
+    "}).catch((e) => { process.stderr.write(String((e && e.stack) || e)); process.exit(1); });",
+  ].join("");
+  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
+  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
+  if (status.term_signal || status.exit_status !== 0) return { run: null, err, status: status.exit_status };
+  return { run: JSON.parse(out) as Run, err, status: 0 };
+}
+
+test("the stripper builds from a directory outside the repository, and writes its stub and entry beside its output", async () => {
+  const { run, err } = await runOutsideRepo();
+  if (!run) throw new Error(`the stripper did not build from outside the repository: ${err}`);
+  // The bundle really was built, not the two side files alone: it is the minified iife that defines __stripTypes.
+  expect(run.bytes > 1_000_000).toBe(true);
+  expect(run.defines).toBe(true);
+  expect(run.stub).toBe(true);
+  expect(run.entry).toBe(true);
+  // Nothing in the repository's build/native moved: the two files are this run's, and this run's alone.
+  expect(run.after).toEqual(run.before);
+  console.log(`# the stripper built outside the repository, into ${run.work}, and removed it`);
+});
````

- [ ] **Step 2: run it, and see it fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs script/build-ts-stripper
```

Expected: FAIL, with esbuild failing to resolve the alias from the child's working directory (the test file itself
bundles and runs; only the stripper's own build stops):

```
not ok 1 - the stripper builds from a directory outside the repository, and writes its stub and entry beside its output
  ---
  the stripper did not build from outside the repository: ✘ [ERROR] Could not resolve "typescript-5" (originally "typescript")

      ../../workspaces/RetroPlug-main/cuttlefish/node_modules/.pnpm/ts-blank-space@0.9.0/node_modules/ts-blank-space/out/index.js:5:18:
        5 │ import tslib from "typescript";
          ╵                   ~~~~~~~~~~~~

    The path "typescript" was remapped to "typescript-5" using the alias feature, which then couldn't be resolved. Keep in mind that import path aliases are resolved in the current working directory.

    You can mark the path "typescript-5" as external to exclude it from the bundle, which will remove this error and leave the unresolved path in the bundle.

  Error: Build failed with 1 error:
  ...

# 0 passed, 0 skipped, 1 failed

# 1/1 test file(s) FAILED: script/build-ts-stripper
```

- [ ] **Step 3: implement it**

````patch
diff --git a/tools/build-ts-stripper.mjs b/tools/build-ts-stripper.mjs
index 33fcddecc..cbf2a7cca 100644
--- a/tools/build-ts-stripper.mjs
+++ b/tools/build-ts-stripper.mjs
@@ -32,7 +32,10 @@ mkdirSync(dirname(outFile), { recursive: true });
 
 // TypeScript's package references these four node builtins from its system/host layer. ts-blank-space
 // only ever reaches the PARSER, so they are never called; stub them so the bundle stays runtime-free.
-const stub = resolve(REPO, "build/native/ts-stripper-stub.js");
+//
+// The stub and the entry are written beside the OUTPUT rather than into the repository's build/native: the output path
+// is CMake's to choose, and a build directory outside the repository must leave the repository alone.
+const stub = resolve(dirname(outFile), "ts-stripper-stub.js");
 writeFileSync(stub, "export default {};\n");
 
 // The entry, written inline so the wrapper and its one critical detail live next to the explanation.
@@ -46,7 +49,7 @@ writeFileSync(stub, "export default {};\n");
 // reachable in this parser-only bundle. Note that TypeScript's node.pos includes LEADING TRIVIA - it
 // starts where the previous token ended - so it must be advanced past whitespace and comments first,
 // or an offending statement preceded by a comment block reports the comment's position instead.
-const entry = resolve(REPO, "build/native/ts-stripper-entry.mjs");
+const entry = resolve(dirname(outFile), "ts-stripper-entry.mjs");
 writeFileSync(
   entry,
   `import tsBlankSpace from "ts-blank-space";
@@ -95,6 +98,13 @@ globalThis.__stripTypes = (source, filename) => {
 buildSync({
   entryPoints: [entry],
   bundle: true,
+  // The alias below resolves its value from here, as every bare import does once the entry is outside the repository
+  // (tools/build-ui.js sets the same): the working directory is the build directory (CMake runs this from the
+  // package's own, under whatever build tree it is), so without one a build outside the repository finds neither.
+  absWorkingDir: REPO,
+  // The entry with the output has no node_modules above it, so ts-blank-space, which it imports by name, is looked for
+  // here as well.
+  nodePaths: [resolve(REPO, "node_modules")],
   format: "iife", // global code: JS_ReadObject + JS_EvalFunction, no module loader involved
   platform: "neutral",
   mainFields: ["module", "main"],
````

- [ ] **Step 4: run it, and see it pass**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs script/build-ts-stripper
```

Expected: PASS, and the test's own line names the directory it built into (and removed: a run leaves nothing in
`/tmp` behind):

```
# script/build-ts-stripper
# the stripper built outside the repository, into /tmp/rp-stripper-175Ste, and removed it
TAP version 13
1..1
ok 1 - the stripper builds from a directory outside the repository, and writes its stub and entry beside its output
# 1 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test
```

Expected: PASS, the whole TypeScript suite with the new file in it (tasks 2-5 add cases to files that already
exist, so the file count stands at 243 through the batch):

```
# 243 test file(s) passed (jobs=16)
#   1 case(s) SKIPPED in 1 file(s): lsdj/ui-views(1)
```

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add tools/build-ts-stripper.mjs packages/retroplug/test/script/build-ts-stripper.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): build the type stripper from the repository, wherever the build directory is

tools/build-ts-stripper.mjs set no esbuild absWorkingDir, so the typescript-5 alias it maps resolved from the directory
CMake runs it in - the build directory - and a build outside the repository has no node_modules above it. It also wrote
its stub and its generated entry into the repository's build/native whatever the output path was, so a build elsewhere
still left two files behind. Set absWorkingDir to the repository (and nodePaths, since the entry no longer sits under
it), and write the two side files beside the output file.
MSG
```

---

## Task 2: an empty project takes settings edits without dirtying

`ProjectStore.setSection` marks the project dirty unconditionally, and so do `setEnumSetting` and `setZoom`. On the
start screen the Settings > MIDI picks and the Project submenu's Layout, Zoom, MIDI Routing and Audio Routing all reach
them, so the close guard and the Load guard ask to save a project with no systems, in every product - and opening a ROM
starts a new project anyway (`openRom` calls `newProject`), so the prompt protects nothing. A settings edit to a
pristine project (no systems, no path, no name) now fires the change callback and dirties nothing; a pathless project
WITH systems still dirties. `setSection` also returns early when the merged section equals the current one, which closes
the class of no-op patches that dirty (a cycler landing on its current value).

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `packages/retroplug/src/projectStore.ts:294-304` (the doc), `:332-339` (the early return and
  `markSettingsChanged`), `:615-621` (`setZoom`), `:865-874` (`setEnumSetting`, then the two new private methods).
- Modify: `packages/core/ts/host.ts:217-224` (`ProjectHost.setSection`'s contract).
- Test: `packages/retroplug/test/project/store.test.ts:8-13` (the import), `:346-349` (the three cases),
  `packages/retroplug/test-ui/midi-settings.test.ts:43-48` (the close guard asks nothing).

**Interfaces:**

- Consumes: `this.systems.systems()`, `this.path`, `this.projectName`, `JSON.stringify` over the parsed sections,
  `hasUnsavedChanges(backend, project)` (`src/unsavedChanges.ts`).
- Produces: two private methods on `ProjectStore`, `isPristine(): boolean` and `markSettingsChanged(): void`; no
  signature change, so no later task consumes anything new. `ProjectHost.setSection`'s doc says what it now does.

### Steps

- [ ] **Step 1: write the failing tests**

Three cases in `store.test.ts` (a pick, Layout and Zoom leave an empty project clean and still fire the observer; adding
a system then dirties and the save carries the pick; a pathless project with a system still dirties, and so does a
saved one on a section edit; an unchanged patch neither dirties nor publishes) and one assertion in
`midi-settings.test.ts` (the standalone's close guard answers "no veto" after a start-screen pick).

````patch
diff --git a/packages/retroplug/test-ui/midi-settings.test.ts b/packages/retroplug/test-ui/midi-settings.test.ts
index e50a14071..0b27907e6 100644
--- a/packages/retroplug/test-ui/midi-settings.test.ts
+++ b/packages/retroplug/test-ui/midi-settings.test.ts
@@ -43,6 +43,12 @@ test("Settings > MIDI: the checklist picks ports into the project, 'All devices'
   expect(labelOf("Arturia KeyStep 32")).toBe("[x] Arturia KeyStep 32");
   expect(fake.applied.at(-1)!.selection.inputs).toEqual(["Arturia KeyStep 32"]);
 
+  // The pick is a settings edit to a project with no systems and no file, so it is not unsaved work: the close goes
+  // through, with no prompt to save a project that holds only the pick.
+  const closeGuard = g as { __rp_onCloseRequested?: () => boolean };
+  expect(closeGuard.__rp_onCloseRequested?.()).toBe(false);
+  expect(ui.findByTextContaining("Unsaved changes")).toBe(null);
+
   // The output side mirrors to a chosen port; Left/Right do nothing on a checklist row, Enter toggles it.
   expect(navTo("Deluge")).toBeTruthy();
   ui.tapKey(Key.Enter);
diff --git a/packages/retroplug/test/project/store.test.ts b/packages/retroplug/test/project/store.test.ts
index c8a572e0d..a07acc2d8 100644
--- a/packages/retroplug/test/project/store.test.ts
+++ b/packages/retroplug/test/project/store.test.ts
@@ -8,6 +8,7 @@ import { RecentStore } from "../../src/recentStore";
 import { ProjectStore } from "../../src/projectStore";
 import { buildAppRegistry } from "../../src/appHost";
 import { K_PROJECT } from "../../src/projectConfig";
+import { hasUnsavedChanges } from "../../src/unsavedChanges";
 import type { SystemLayout } from "../../src/settingsEnums";
 import { gbRom, gbRomBattery, lsdjRom } from "../systems/fixtures";
 import { savFrom, type SavInput } from "@retroplug/lsdj";
@@ -346,3 +347,57 @@ test("setFocus: fires the focus observer for a re-render but does NOT mark the p
   expect(project.systems.setFocus(999999)).toBeFalsy(); // unknown id → no-op, no notify
   expect(focusNotifies).toBe(1);
 });
+
+test("a settings edit on an empty untitled project is not a project change, but still fires the observer", () => {
+  const { be, project } = newProject();
+  let changes = 0;
+  project.setOnChange(() => changes++);
+  // A pick on the start screen (Settings > MIDI), and the four settings the Project submenu offers there: none of
+  // them is a change a prompt should save, because opening a ROM starts a new project anyway.
+  project.setSection("midi", { allInputs: true });
+  expect(project.section<{ allInputs: boolean }>("midi").allInputs).toBe(true);
+  expect(project.setLayout("grid")).toBe(true);
+  expect(project.setZoom(4)).toBe(true);
+  expect(project.isDirty()).toBe(false);
+  expect(hasUnsavedChanges(be, project)).toBe(false);
+  expect(changes).toBe(3); // the observer still hears every edit: the rows repaint
+  // Adding a system makes it a project, and the pick is part of it.
+  be.seed("/proj/a.gb", gbRom());
+  project.systems.addSystem("/proj/a.gb");
+  expect(project.isDirty()).toBe(true);
+  expect(project.save("/proj/song.rplg")).toBe(true);
+  const saved = JSON.parse(be.readText("/proj/song.rplg")!);
+  expect(saved.settings.midi.allInputs).toBe(true);
+});
+
+test("a pathless project with a system still dirties, and a saved one dirties on a section edit", () => {
+  const { be, project } = newProject();
+  be.seed("/proj/a.gb", gbRom());
+  project.systems.addSystem("/proj/a.gb");
+  project.save("/proj/song.rplg");
+  expect(project.isDirty()).toBe(false);
+  project.setSection("midi", { allInputs: true }); // a saved project: the edit is unsaved, and the prompt says so
+  expect(project.isDirty()).toBe(true);
+  expect(hasUnsavedChanges(be, project)).toBe(true);
+  // The same for an unsaved project that holds a system: no path, but a project.
+  const other = newProject();
+  other.be.seed("/proj/b.gb", gbRom());
+  other.project.systems.addSystem("/proj/b.gb");
+  other.project.setZoom(4);
+  expect(other.project.isDirty()).toBe(true);
+});
+
+test("a section patch that changes nothing dirties nothing and publishes nothing", () => {
+  const { be, project } = newProject();
+  be.seed("/proj/a.gb", gbRom());
+  project.systems.addSystem("/proj/a.gb");
+  project.save("/proj/song.rplg");
+  let changes = 0;
+  project.setOnChange(() => changes++);
+  let systemNotifies = 0;
+  project.addOnSystemsChange(() => systemNotifies++);
+  project.setSection("midi", {}); // a cycler landing on its current value
+  expect(project.isDirty()).toBe(false);
+  expect(changes).toBe(0);
+  expect(systemNotifies).toBe(0);
+});
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs project/store
```

Expected: FAIL. Cases 21 and 23 fail on `isDirty()`, and 22 - the guard that a pathless project with a system still
dirties - passes before the fix, which is the point of it:

```
ok 20 - setFocus: fires the focus observer for a re-render but does NOT mark the project dirty
not ok 21 - a settings edit on an empty untitled project is not a project change, but still fires the observer
  ---
  expected false, got true
  ...
ok 22 - a pathless project with a system still dirties, and a saved one dirties on a section edit
not ok 23 - a section patch that changes nothing dirties nothing and publishes nothing
  ---
  expected false, got true
  ...
# 21 passed, 0 skipped, 2 failed

# 1/1 test file(s) FAILED: project/store
```

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-ui-tests.mjs midi-settings
```

Expected: FAIL at the new assertion (line 424 of the bundle is `expect(closeGuard.__rp_onCloseRequested?.()).toBe(false)`).
The UI test binary EMBEDS the UI bundle, so it must be rebuilt first, or the run tests the previous build:

```
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-ui-test -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-ui-tests.mjs midi-settings
```

```
not ok 1 - Settings > MIDI: the checklist picks ports into the project, 'All devices' is a choice, Rescan reopens
  ---
  expected false, got true
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.ui-build/midi-settings.js:86:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.ui-build/midi-settings.js:94:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.ui-build/midi-settings.js:424:53)

  ...
# 0 passed, 0 skipped, 1 failed

# 1/1 UI test file(s) FAILED: midi-settings
```

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/core/ts/host.ts b/packages/core/ts/host.ts
index e6806614b..2dd714c2e 100644
--- a/packages/core/ts/host.ts
+++ b/packages/core/ts/host.ts
@@ -217,8 +217,10 @@ export interface ViewsHost {
 export interface ProjectHost {
   /** The section as its contributor parses it (raw JSON for a key no package contributes). */
   section<T = Record<string, unknown>>(key: string): T;
-  /** Apply a patch through the contributor's `merge`, mark the project dirty, publish the graph and notify. THROWS when
-   *  the contributor refuses the patch, and does nothing for a section stamped newer than its contributor. */
+  /** Apply a patch through the contributor's `merge`, publish the graph and notify. An edit to an empty project (no
+   *  systems, no path, no name) is not unsaved work, so it does not mark it dirty: there is nothing a prompt could
+   *  save. THROWS when the contributor refuses the patch, and does nothing for a section stamped newer than its
+   *  contributor or for a patch that changes nothing. */
   setSection(key: string, patch: Readonly<Record<string, unknown>>): void;
   /** What a project service published (the controller cart's polled SYNC), or undefined for no such service. */
   serviceState<T = unknown>(key: string): T | undefined;
diff --git a/packages/retroplug/src/projectStore.ts b/packages/retroplug/src/projectStore.ts
index 81a9a8827..c25e86443 100644
--- a/packages/retroplug/src/projectStore.ts
+++ b/packages/retroplug/src/projectStore.ts
@@ -294,11 +294,12 @@ export class ProjectStore {
     return readableProjectSection<T>(this.projectSettings, key, this.sections);
   }
 
-  /** Apply a patch to a section through its contributor's `merge`, then mark the project dirty, publish the graph and
+  /** Apply a patch to a section through its contributor's `merge`, then mark the project changed, publish the graph and
    *  notify: a section's values are what the graph's placers place (the controller node is placed from this one at each
    *  publish), so a change that was never published would leave the audio thread running the previous session. A patch
    *  the contributor refuses THROWS, nothing written. A section stamped newer than its contributor is left alone, with
-   *  one warning naming the key and both versions: this build must not rewrite what it cannot read. */
+   *  one warning naming the key and both versions: this build must not rewrite what it cannot read. A patch that
+   *  changes nothing is not an edit: it neither dirties, nor publishes, nor notifies. */
   setSection(key: string, patch: Readonly<Record<string, unknown>>): void {
     assertSectionKey(key);
     // Writing a block no package of this product contributes would create a key E5 then keeps in every save forever,
@@ -332,8 +333,12 @@ export class ProjectStore {
         ? contributor.merge(current, patch)
         : contributor.schema.parse({ ...(isObject(current) ? current : {}), ...patch })
       : { ...(isObject(current) ? current : {}), ...patch };
+    // A patch that lands on the values already there is not an edit (a cycler landing on its current value, a service
+    // re-applying the saved section), so it dirties nothing and publishes nothing: the audio thread is already running
+    // it. Both sides went through the same parse, so their key order is the same and the comparison is exact.
+    if (JSON.stringify(next) === JSON.stringify(current)) return;
     this.projectSettings = { ...this.projectSettings, sections: { ...this.projectSettings.sections, [key]: next } };
-    this.markDirty();
+    this.markSettingsChanged();
     this.publishGraph(false);
     this.notifySystemsChange();
   }
@@ -615,7 +620,7 @@ export class ProjectStore {
   setZoom(n: number): boolean {
     if (n < 0 || n > ZOOM_MAX) return false; // out of range → reject
     this.projectSettings = { ...this.projectSettings, zoom: n };
-    this.markDirty();
+    this.markSettingsChanged();
     return true;
   }
 
@@ -865,10 +870,27 @@ export class ProjectStore {
   private setEnumSetting(key: "layout" | "midiRouting" | "audioRouting", value: string): boolean {
     if (!(SETTING_VALUES[key] as readonly string[]).includes(value)) return false; // unknown → reject
     this.projectSettings = { ...this.projectSettings, [key]: value } as ProjectSettings;
-    this.markDirty();
+    this.markSettingsChanged();
     return true;
   }
 
+  /** A project with nothing of its own yet: no systems, no file, no name. The start screen's settings rows edit such a
+   *  project (Settings > MIDI's picks, Layout, Zoom, the MIDI and audio routings), and a prompt to save one protects
+   *  nothing: opening a ROM starts a new project anyway (openRom calls newProject, which resets the settings), and a
+   *  close that saved one would write a .rplg holding only settings. The edit itself is kept, so it applies to the audio
+   *  and MIDI streams as it lies and reaches the project a system is later added to. A pathless project WITH systems is
+   *  a project: it is what the close guard exists for. */
+  private isPristine(): boolean {
+    return this.systems.systems().length === 0 && this.path === "" && this.projectName === "";
+  }
+
+  /** A settings edit landed: unsaved work in a project, an edit alone in an empty one. The change callback fires either
+   *  way, so what shows a setting repaints. */
+  private markSettingsChanged(): void {
+    if (this.isPristine()) this.onChangeCb();
+    else this.markDirty();
+  }
+
   /** Publish the whole graph: the systems as they stand, wired by the current audio-routing mode, with their
    *  script nodes. Called on set / load / reset, every one re-arming the NES tap, as a routing change always has, even
    *  to the mode already set. */
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs project/store
```

```
ok 21 - a settings edit on an empty untitled project is not a project change, but still fires the observer
ok 22 - a pathless project with a system still dirties, and a saved one dirties on a section edit
ok 23 - a section patch that changes nothing dirties nothing and publishes nothing
# 23 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

```
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-ui-test -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-ui-tests.mjs midi-settings
```

```
ok 1 - Settings > MIDI: the checklist picks ports into the project, 'All devices' is a choice, Rescan reopens
# 1 passed, 0 skipped, 0 failed

# 1 UI test file(s) passed (jobs=16)
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test
timeout -k 10 600 .superpowers/kb3plan/guarded.sh pnpm test:ui
```

Expected: PASS. `pnpm test` at the tip of the batch prints `243 test file(s) passed`; `pnpm test:ui` prints
`48 UI test file(s) passed`. The store's dirty-flag rule reaches the close guard's own cases (`close-guard`,
`working-song-smsggdj` in `test-ui`), which the same run covers.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/retroplug/src/projectStore.ts packages/core/ts/host.ts \
    packages/retroplug/test/project/store.test.ts packages/retroplug/test-ui/midi-settings.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): take a settings edit to an empty project without dirtying it

setSection, setEnumSetting and setZoom marked the project dirty unconditionally, so a start-screen settings edit (a
Settings > MIDI pick, Layout, Zoom, either routing) made the close guard and the Load guard ask to save a project with
no systems, no path and no name - which nothing protects, since opening a ROM starts a new project anyway. A pristine
project's settings edit now fires the change callback and dirties nothing, and a pathless project WITH systems still
dirties. setSection also returns early when the merged section equals the current one, so a no-op patch neither
dirties, publishes nor notifies.
MSG
```

---

## Task 3: a visit to the audio menu starts a fresh draft

`resetAudioDraft` has no production caller and the audio menu has no open hook, so the sibling-backend cache lasts
until the backend in force changes ("More drivers..." vanishes for the session, and a sibling that answered nothing is
never asked again) and an unapplied draft survives every later visit and project load. The MIDI package already has the
seam: `audioMenu` gains `onOpen: () => resetAudioDraft()`. An unapplied draft is discarded when the container is
collapsed and reopened (the user's decision, design section 3); `knownDrivers`' backend-in-force invalidation stays for
an Apply to another backend.

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `packages/audio/ts/ui/audioDraft.ts:52-58` (the reset's doc says who calls it), `:90-97` (the reset clears
  the visit's per-backend cache).
- Modify: `packages/audio/ts/ui/index.ts:11-17` (the import), `:115-124` (`audioMenu.onOpen`).
- Test: `packages/audio/ts/test/audioDraft.test.ts:5-14` (imports), `:16-22` (the harness's config stub),
  `:56-58` (the two cases).

**Interfaces:**

- Consumes: `resetAudioDraft`, `audioDescription`, `moreDriversAvailable`, `fetchMoreDrivers`, `audioDriverChoices`,
  `getAudioDraft`, `setAudioDraft`, `audioDraftDirty` (`audioDraft.ts`), and the `UiMenu.onOpen` seam the MIDI package's
  menu uses (`packages/midi/ts/ui/index.ts`).
- Produces: `audioMenu.onOpen` (`packages/audio/ts/ui/index.ts`), which Task 4 reads again because the refusal row it
  adds is cleared by the same visit; no new type.

### Steps

- [ ] **Step 1: write the failing tests**

Two cases in `audioDraft.test.ts`, built over the package's real menu contribution and a fake audio device: a sibling
that answered nothing is asked again after `onOpen` and "More drivers..." is back; a staged block size is gone after
`onOpen`, so the row shows the saved value and Apply is disabled. The within-a-visit "asked once" case stands.

````patch
diff --git a/packages/audio/ts/test/audioDraft.test.ts b/packages/audio/ts/test/audioDraft.test.ts
index 6880afa31..baab03c7e 100644
--- a/packages/audio/ts/test/audioDraft.test.ts
+++ b/packages/audio/ts/test/audioDraft.test.ts
@@ -5,10 +5,16 @@
 import { test, expect } from "@retroplug/testing";
 import type { AudioBackendDescription } from "@retroplug/core/host";
 import { FakeAudioDevice, DEFAULT_BACKENDS } from "../testSupport/fakeDevice";
-import { audioDescription, fetchMoreDrivers, moreDriversAvailable, resetAudioDraft } from "../ui/audioDraft";
+import audioUi from "../ui/index";
+import {
+  audioDescription, audioDraftDirty, audioDriverChoices, fetchMoreDrivers, getAudioDraft, moreDriversAvailable,
+  resetAudioDraft, setAudioDraft,
+} from "../ui/audioDraft";
 
 /** A host over the fake's globals: `audioDescription` is the one read the rows make, and a sibling's drivers come from
- *  the hook the native host binds (the fake counts those calls, which is what "asked once" is proved by). */
+ *  the hook the native host binds (the fake counts those calls, which is what "asked once" is proved by). The config
+ *  stub is the `audio` section the draft seeds from and is what a file that took the defaults holds. */
+const SAVED = { backend: "auto", driver: "", device: "", sampleRate: 48000, blockSize: 512, outChannels: 2 };
 const hostOver = (fake: FakeAudioDevice) =>
   ({
     audioDevice: {
@@ -16,6 +22,7 @@ const hostOver = (fake: FakeAudioDevice) =>
       backendDrivers: (id: string) =>
         (globalThis as unknown as { __rp_audioBackendDrivers: (b: string) => unknown }).__rp_audioBackendDrivers(id),
     },
+    config: { section: () => SAVED, setSection: () => true },
   }) as never;
 
 /** SDL with no driver it can list, which is what a refused (pinned) sibling answers. */
@@ -56,3 +63,59 @@ test("a run that pins a backend offers no step: the hook refuses, and the This-r
     resetAudioDraft();
   }
 });
+
+/** The audio package's own menu contribution: the builder the shell's Audio container renders, whose `onOpen` is the
+ *  moment a visit begins. */
+const audioMenu = audioUi.menus!.find((menu) => menu.placement === "settings.audio")!;
+
+/** The rows the shell would render, built by the real builder over the fake. */
+const rowsOf = (host: unknown) =>
+  audioMenu.build(host as never, null, { systems: [], assetRom: () => null, romPatterns: [] });
+
+test("a visit asks a sibling that answered nothing again, and 'More drivers...' comes back", () => {
+  const fake = new FakeAudioDevice({ backends: SILENT_SIBLING }).install();
+  resetAudioDraft();
+  try {
+    const host = hostOver(fake);
+    expect(moreDriversAvailable(audioDescription(host)!)).toBe(true);
+    // The first visit asks, gets nothing, and the step goes away for the rest of the visit.
+    expect(fetchMoreDrivers(host, audioDescription(host)!)).toBe(true);
+    expect(fake.backendDriversCalls).toEqual(["sdl"]);
+    expect(moreDriversAvailable(audioDescription(host)!)).toBe(false);
+    // Collapsing and reopening the container is a new visit: the question is asked afresh, and a library that had
+    // nothing to list last time can now be seen to have something.
+    audioMenu.onOpen!(host as never);
+    expect(moreDriversAvailable(audioDescription(host)!)).toBe(true);
+    fake.description.backends[1].drivers = [{ id: "pipewire", label: "pipewire", devices: { names: ["Speakers"], defaultName: "Speakers" } }];
+    expect(fetchMoreDrivers(host, audioDescription(host)!)).toBe(true);
+    expect(fake.backendDriversCalls).toEqual(["sdl", "sdl"]);
+    const labels = audioDriverChoices(host, audioDescription(host)!).map((choice) => choice.label);
+    expect(labels.includes("SDL: pipewire")).toBe(true);
+  } finally {
+    fake.uninstall();
+    resetAudioDraft();
+  }
+});
+
+test("a visit starts a fresh draft: a staged block size is gone, and Apply with it", () => {
+  const fake = new FakeAudioDevice().install();
+  resetAudioDraft();
+  try {
+    const host = hostOver(fake);
+    expect(getAudioDraft(host).blockSize).toBe(512);
+    setAudioDraft({ blockSize: 256 });
+    expect(audioDraftDirty(host)).toBe(true);
+    expect(rowsOf(host).find((row) => row.id === "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
+    expect(rowsOf(host).find((row) => row.id === "audio-apply")!.disabled).toBeFalsy();
+    // The container is collapsed and reopened: the unapplied draft is discarded, so the rows show the device that is
+    // running and Apply has nothing to commit.
+    audioMenu.onOpen!(host as never);
+    expect(getAudioDraft(host).blockSize).toBe(512);
+    expect(audioDraftDirty(host)).toBe(false);
+    expect(rowsOf(host).find((row) => row.id === "audio-block")!.label).toBe("Block Size: 512 (10.7 ms)");
+    expect(rowsOf(host).find((row) => row.id === "audio-apply")!.disabled).toBeTruthy();
+  } finally {
+    fake.uninstall();
+    resetAudioDraft();
+  }
+});
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs audio/audioDraft
```

Expected: FAIL, on the seam that does not exist yet (`audioMenu.onOpen` is `undefined`, so calling it is "not a
function"):

```
ok 1 - a sibling that answers nothing is asked once, and the step goes away
ok 2 - a run that pins a backend offers no step: the hook refuses, and the This-run row says why
not ok 3 - a visit asks a sibling that answered nothing again, and 'More drivers...' comes back
  ---
  not a function
  ...
not ok 4 - a visit starts a fresh draft: a staged block size is gone, and Apply with it
  ---
  not a function
  ...
# 2 passed, 0 skipped, 2 failed

# 1/1 test file(s) FAILED: audio/audioDraft
```

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/audio/ts/ui/audioDraft.ts b/packages/audio/ts/ui/audioDraft.ts
index 5158fffdc..0f27ea03f 100644
--- a/packages/audio/ts/ui/audioDraft.ts
+++ b/packages/audio/ts/ui/audioDraft.ts
@@ -52,7 +52,9 @@ export function audioDraftDirty(host: Pick<UiHost, "config">): boolean {
   return JSON.stringify(getAudioDraft(host)) !== JSON.stringify(savedAudio(host));
 }
 
-/** Drop any pending edits back to the saved value, and forget the sibling lists fetched for this visit. */
+/** Drop any pending edits back to the saved value, and forget the sibling lists fetched for this visit. The audio
+ *  menu's `onOpen` runs it, so every visit starts from what the device is running and from a sibling no step of the
+ *  new visit has asked about yet. */
 export function resetAudioDraft(): void {
   draft = null;
   siblingDrivers.clear();
@@ -90,8 +92,9 @@ export interface AudioDriverChoice {
   readonly driver: string;
 }
 
-/** Sibling backends' drivers, fetched only by an explicit step on the Driver row, and for this visit: the cache is
- *  dropped whenever the backend in force moves (the user applied another one), so a later visit asks afresh. */
+/** Sibling backends' drivers, fetched only by an explicit step on the Driver row, and for this visit: resetAudioDraft
+ *  (the menu's open hook) drops the cache, and so does a move of the backend in force (the user applied another one),
+ *  so a later visit asks afresh. */
 const siblingDrivers = new Map<string, readonly AudioDriverDescription[]>();
 /** The siblings this visit has ASKED about, separately from what each answered: a backend with no usable driver (a
  *  pinned run, where the hook refuses; a library with nothing to list) answers `[]`, and that answer is final - counting
diff --git a/packages/audio/ts/ui/index.ts b/packages/audio/ts/ui/index.ts
index 2ebec8b4c..3e3420da4 100644
--- a/packages/audio/ts/ui/index.ts
+++ b/packages/audio/ts/ui/index.ts
@@ -11,7 +11,7 @@ import { defineUi, type MenuBuilder, type UiHost, type UiService } from "@retrop
 import { AUDIO_OUT_CHANNELS } from "../configSection";
 import {
   applyAudioDraft, applySavedAudio, audioDescription, audioDevices, audioDraftDirty, audioDriverChoices,
-  fetchMoreDrivers, getAudioDraft, moreDriversAvailable, setAudioDraft, subscribeAudioDraft,
+  fetchMoreDrivers, getAudioDraft, moreDriversAvailable, resetAudioDraft, setAudioDraft, subscribeAudioDraft,
 } from "./audioDraft";
 
 export * from "./audioDraft";
@@ -115,10 +115,14 @@ function audioSettingsChildren(host: UiHost): MenuItem[] {
   ];
 }
 
-/** The Settings > Audio rows, in the shell's Audio container. */
+/** The Settings > Audio rows, in the shell's Audio container. Opening that container begins a VISIT, which is what the
+ *  draft and the sibling cache are scoped to: an unapplied draft is discarded, and a sibling that answered nothing is
+ *  asked again. The shell's open hook is the only moment a package hears that the user asked for the rows, and a
+ *  render is the wrong moment to reset anything a row is showing. */
 const audioMenu: MenuBuilder = {
   placement: "settings.audio",
   build: (host) => audioSettingsChildren(host),
+  onOpen: () => resetAudioDraft(),
 };
 
 /** What the service has done this mount: the first apply (the startup rendezvous's word), and the status it last
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs audio/audioDraft
```

```
ok 3 - a visit asks a sibling that answered nothing again, and 'More drivers...' comes back
ok 4 - a visit starts a fresh draft: a staged block size is gone, and Apply with it
# 4 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test
```

Expected: PASS, `243 test file(s) passed`. The shell's own Cases for the audio rows are `menu/leaves.test.ts`, which the
same run covers; Task 4 touches both files again.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/audio/ts/ui/audioDraft.ts packages/audio/ts/ui/index.ts \
    packages/audio/ts/test/audioDraft.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): start a fresh audio draft on every visit to the menu

resetAudioDraft had no production caller and the audio menu had no open hook, so an unapplied draft survived every
later visit and project load, and the sibling-backend cache lasted until the backend in force changed: "More drivers..."
vanished for the session and a sibling that answered nothing was never asked again. Give the menu the onOpen seam the
MIDI package already uses, calling the reset. A re-fetch re-asks the library, which cannot show a device PortAudio did
not see at its initialisation; that limit is the library's.
MSG
```

---

## Task 4: a refused config write leaves the device alone

`applyAudioDraft` writes the section and then calls `audioDevice.apply` regardless; `ConfigHost.setSection` returns
`void` and the shell drops `UserConfigStore.setSection`'s answer (`packages/retroplug/ui/host/uiHost.ts`). Under a
read-only config (a newer RetroPlug's) or a failed write the device switches, then the rows snap back to the saved
values and nothing says why. The store's `false` cannot gate the device as it stands: it also means "nothing changed",
and an Apply with a clean draft is the muted device's retry. So the store gains a three-way `writeSection`, the host's
`setSection` gains a boolean (whether the file holds the patch), and `applyAudioDraft` on a refusal reaches no device,
keeps the draft and the Apply row live, and shows why.

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `packages/retroplug/src/userConfigStore.ts:136-150` (`writeSection`, and `setSection` over it),
  `:156-161, :166-171` (the atomic write's answer), `:275-289, :283-289, :329-344` (the refusals: the latch, a failed
  write, a stamped-newer section).
- Modify: `packages/retroplug/ui/host/uiHost.ts:81-87` (the shell binding answers).
- Modify: `packages/core/ts/host.ts:242-249` (`ConfigHost.setSection` returns a boolean).
- Modify: `packages/audio/ts/ui/audioDraft.ts:17-22` (the refusal row's text and its state), `:52-74` (the Apply's gate).
- Modify: `packages/audio/ts/ui/index.ts:11-17, :73-78, :107-115` (the row is rendered where the draft rows are, and the
  visit clears it).
- Test: `packages/retroplug/test/config/user-config.test.ts:276-278` (`writeSection`'s answers), and the cases in
  `packages/retroplug/test/config/config-sections.test.ts:179-186` and
  `packages/retroplug/test/menu/leaves.test.ts:14-19, :1698-1700, :1746-1782` (the refusal's Apply case and the failed
  write's, which is the other way `writeSection` answers "refused").

**Interfaces:**

- Consumes: `UserConfigStore`'s read-only latch and its atomic write (`writeFileAtomic`), the sections' merge,
  `audioDraftDirty` / `getAudioDraft` / `setAudioDraft` (Task 3's file), the `audio-apply` row's `disabled`.
- Produces: `UserConfigStore.writeSection(key, patch): "written" | "unchanged" | "refused"`, with
  `setSection(key, patch): boolean` kept as `=== "written"` for its existing callers; `ConfigHost.setSection(key, patch):
  boolean` (`@retroplug/core/host`), which is a breaking change of the interface's return type and only the shell binds
  it (the fake hosts in the tests return `true`). `applyAudioDraft` returns nothing new: it writes no device call on a
  refusal and sets the draft's `notSaved` flag, which the audio rows render as
  "Not saved: config.json could not be written". No later task consumes it.

### Steps

- [ ] **Step 1: write the failing tests**

Three files. `user-config.test.ts` gains a case with the three answers (a fresh value is `written`, a repeat is
`unchanged`, the read-only latch, a failed atomic write and a stamped-newer section are `refused`). `config-sections`
changes its newer-stamp case to the new answer. `menu/leaves.test.ts` gains the Apply cases: under a newer-stamped
config and under a failing `writeFileAtomic`, Apply reaches no device, keeps the staged value and the Apply row, shows
the row, and leaves the file's bytes alone; a muted device with a clean draft on a writable config still re-applies (the
unchanged write is not a refusal).

````patch
diff --git a/packages/retroplug/test/config/config-sections.test.ts b/packages/retroplug/test/config/config-sections.test.ts
index b1a49b532..b98522432 100644
--- a/packages/retroplug/test/config/config-sections.test.ts
+++ b/packages/retroplug/test/config/config-sections.test.ts
@@ -179,8 +179,9 @@ test("a section stamped newer reads its defaults, refuses a patch once, and is w
     store.load();
     // A reader gets what it can honestly type: the contributor's defaults, not a block it could not parse.
     expect(store.section("versioned")).toEqual({ knob: "a", size: 0 });
-    // The writer keeps the block, so two refused patches warn once and change nothing.
-    expect(store.setSection("versioned", { knob: "b" })).toBeFalsy();
+    // The writer keeps the block, so two refused patches warn once and change nothing. A refused patch is a REFUSAL,
+    // not a no-op: a caller that gates a device on the answer must not read it as "already there".
+    expect(store.writeSection("versioned", { knob: "b" })).toBe("refused");
     expect(store.setSection("versioned", { knob: "c" })).toBeFalsy();
     expect(warnings.lines.filter((line) => line.includes("versioned")).length).toBe(1);
     // A change to the shell's own fields still writes the file, and the block goes back exactly as it came.
diff --git a/packages/retroplug/test/config/user-config.test.ts b/packages/retroplug/test/config/user-config.test.ts
index f0185499d..4decc0827 100644
--- a/packages/retroplug/test/config/user-config.test.ts
+++ b/packages/retroplug/test/config/user-config.test.ts
@@ -276,3 +276,27 @@ test("a write that fails is not reported as saved, and is not adopted", () => {
   be.writeFileAtomic = realWrite;
   expect(store.setSection(CONTROL_SURFACE, { input: "LP in" })).toBeTruthy();
 });
+
+test("writeSection answers written, unchanged or refused, which the boolean setSection cannot", () => {
+  const { be, store } = newStore();
+  store.load();
+  expect(store.writeSection(CONTROL_SURFACE, { input: "LP in" })).toBe("written");
+  // The same patch again changes nothing: not a failure, and not a write either. An Apply that re-sends the saved
+  // choice (the muted device's retry) lands here.
+  expect(store.writeSection(CONTROL_SURFACE, { input: "LP in" })).toBe("unchanged");
+  // A failed atomic write is a refusal: the change is not adopted, and the file is as it was.
+  const before = be.readText(CONFIG);
+  const realWrite = be.writeFileAtomic.bind(be);
+  be.writeFileAtomic = () => false; // e.g. Windows: another process holds config.json open
+  expect(store.writeSection(CONTROL_SURFACE, { input: "LP out" })).toBe("refused");
+  be.writeFileAtomic = realWrite;
+  expect(be.readText(CONFIG)).toBe(before);
+  expect(store.section<{ input: string }>(CONTROL_SURFACE).input).toBe("LP in");
+  // A newer build's file landing mid-session latches the store read-only, and every write is refused.
+  const newer = JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 1, ...DEFAULT_USER_CONFIG });
+  be.seed(CONFIG, newer);
+  expect(store.writeSection(CONTROL_SURFACE, { input: "LP out" })).toBe("refused");
+  expect(be.readText(CONFIG)).toBe(newer);
+  // setSection is the one answer a caller that only acts on a real write wants.
+  expect(store.setSection(CONTROL_SURFACE, { input: "LP out" })).toBe(false);
+});
diff --git a/packages/retroplug/test/menu/leaves.test.ts b/packages/retroplug/test/menu/leaves.test.ts
index 9606a56e9..2a5aea7ed 100644
--- a/packages/retroplug/test/menu/leaves.test.ts
+++ b/packages/retroplug/test/menu/leaves.test.ts
@@ -14,6 +14,8 @@ import { savFrom, loadSongToWorking, type SavInput } from "@retroplug/lsdj";
 import { lsdjSongCatalog } from "@retroplug/lsdj/lsdjSongCatalog";
 import { FakeAudioDevice } from "@retroplug/audio/testSupport/fakeDevice";
 import { FakeMidiDevice } from "@retroplug/midi/testSupport/fakeDevice";
+import { DEFAULT_USER_CONFIG } from "../../src/userConfig";
+import { USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
 
 // A leaf's onSelect fires a FileSelection call fire-and-forget; flush the microtask chain it kicks off
 // (openFileBrowser resolve → pairing → the store mutation / runLoad .then). A handful of turns settles it.
@@ -1696,3 +1698,114 @@ test("a muted device keeps Apply live, and the audio service repaints when the s
     delete g.__rp_isStandalone;
   }
 });
+
+test("Settings -> Audio: an Apply the config refuses leaves the device alone, keeps the draft, and says why", async () => {
+  const fake = new FakeAudioDevice().install();
+  const g = globalThis as { __rp_isStandalone?: boolean };
+  g.__rp_isStandalone = true;
+  const { resetAudioDraft } = await import("@retroplug/audio/ui");
+  resetAudioDraft();
+
+  // A config.json a newer RetroPlug wrote: this build loads it read-only (one console line) and must never rewrite it.
+  const be = new MockBackend("/cfg");
+  const newer = JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA + 1, ...DEFAULT_USER_CONFIG });
+  be.seed("/cfg/config.json", newer);
+  const stores = composeAppStores({ backend: be, notify: () => {} });
+  const audioItems = () =>
+    submenuChildren(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings"), "set-audio");
+
+  // A staged block size is a real change, so Apply is live.
+  findItem(audioItems(), "audio-block")!.onCycle!(-1);
+  let items = audioItems();
+  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
+  expect(findItem(items, "audio-apply")!.disabled).toBeFalsy();
+
+  // Apply under the refusing file: the device is not touched, the draft stays (so the rows still show what was asked
+  // and Apply is still live to retry), a row says why, and the file is byte-identical.
+  findItem(items, "audio-apply")!.onSelect!();
+  expect(fake.applied.length).toBe(0);
+  items = audioItems();
+  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
+  expect(findItem(items, "audio-apply")!.disabled).toBeFalsy();
+  expect(findItem(items, "audio-not-saved")!.label).toBe("Not saved: config.json could not be written");
+  expect(be.readText("/cfg/config.json")).toBe(newer);
+
+  // A writable file again: the retry lands, reaches the device, and clears the row.
+  be.seed("/cfg/config.json", JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG }));
+  findItem(items, "audio-apply")!.onSelect!();
+  expect(fake.applied.at(-1)!.blockSize).toBe(256);
+  items = audioItems();
+  expect(findItem(items, "audio-not-saved")).toBe(undefined);
+  expect(findItem(items, "audio-apply")!.disabled).toBe(true);
+
+  resetAudioDraft();
+  fake.uninstall();
+  delete g.__rp_isStandalone;
+});
+
+test("Settings -> Audio: an Apply whose write FAILS leaves the device alone, keeps the draft, and says why", async () => {
+  const fake = new FakeAudioDevice().install();
+  const g = globalThis as { __rp_isStandalone?: boolean };
+  g.__rp_isStandalone = true;
+  const { resetAudioDraft } = await import("@retroplug/audio/ui");
+  resetAudioDraft();
+
+  // A writable config.json whose WRITE then fails: on Windows another process holding the file open without
+  // FILE_SHARE_DELETE fails the atomic rename, which is the other way the store answers "refused".
+  const be = new MockBackend("/cfg");
+  const saved = JSON.stringify({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG });
+  be.seed("/cfg/config.json", saved);
+  const stores = composeAppStores({ backend: be, notify: () => {} });
+  const audioItems = () =>
+    submenuChildren(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings"), "set-audio");
+  findItem(audioItems(), "audio-block")!.onCycle!(-1);
+  const realWrite = be.writeFileAtomic.bind(be);
+  be.writeFileAtomic = () => false; // the rename failed: the change is not on disk
+
+  // The device is not touched (it would run something the next launch will not open), the draft and Apply stay, the
+  // row says why, and the file is byte-identical.
+  findItem(audioItems(), "audio-apply")!.onSelect!();
+  expect(fake.applied.length).toBe(0);
+  let items = audioItems();
+  expect(findItem(items, "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
+  expect(findItem(items, "audio-apply")!.disabled).toBeFalsy();
+  expect(findItem(items, "audio-not-saved")!.label).toBe("Not saved: config.json could not be written");
+  expect(be.readText("/cfg/config.json")).toBe(saved);
+
+  // The file takes writes again: the retry lands on it, reaches the device, and clears the row.
+  be.writeFileAtomic = realWrite;
+  findItem(items, "audio-apply")!.onSelect!();
+  expect(fake.applied.at(-1)!.blockSize).toBe(256);
+  items = audioItems();
+  expect(findItem(items, "audio-not-saved")).toBe(undefined);
+  expect(findItem(items, "audio-apply")!.disabled).toBe(true);
+
+  resetAudioDraft();
+  fake.uninstall();
+  delete g.__rp_isStandalone;
+});
+
+test("Settings -> Audio: a muted device with a clean draft re-applies, and the no-op write is not a refusal", async () => {
+  const fake = new FakeAudioDevice({ status: { error: "no device" } }).install();
+  const g = globalThis as { __rp_isStandalone?: boolean };
+  g.__rp_isStandalone = true;
+  const { resetAudioDraft } = await import("@retroplug/audio/ui");
+  resetAudioDraft();
+
+  const be = new MockBackend("/cfg");
+  const stores = composeAppStores({ backend: be, notify: () => {} });
+  const audioItems = () =>
+    submenuChildren(submenuChildren(buildStartMenu(ctxOf(stores)).items, "start-settings"), "set-audio");
+  resetAudioDraft(); // the saved section, which is what the draft already is: no change staged
+
+  const items = audioItems();
+  expect(findItem(items, "audio-apply")!.disabled).toBeFalsy(); // enabled while muted, even with a clean draft
+  findItem(items, "audio-apply")!.onSelect!();
+  expect(fake.applied.length).toBe(1); // the retry reached the device
+  expect(fake.applied.at(-1)!.blockSize).toBe(512);
+  expect(findItem(audioItems(), "audio-not-saved")).toBe(undefined);
+
+  resetAudioDraft();
+  fake.uninstall();
+  delete g.__rp_isStandalone;
+});
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs config/user-config
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs config/config-sections
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs menu/leaves
```

Expected: the first two fail on `writeSection` not existing ("not a function"), and `menu/leaves` fails on the Apply
that reaches the device under a config that refused the write:

```
not ok 24 - writeSection answers written, unchanged or refused, which the boolean setSection cannot
  ---
  not a function
  ...
# 23 passed, 0 skipped, 1 failed
```

```
not ok 5 - a section stamped newer reads its defaults, refuses a patch once, and is written back as it came
  ---
  not a function
  ...
# 14 passed, 0 skipped, 1 failed
```

```
ok 70 - a muted device keeps Apply live, and the audio service repaints when the status moves
not ok 71 - Settings -> Audio: an Apply the config refuses leaves the device alone, keeps the draft, and says why
  ---
  expected 0, got 1
  ...
not ok 72 - Settings -> Audio: an Apply whose write FAILS leaves the device alone, keeps the draft, and says why
  ---
  expected 0, got 1
  ...
ok 73 - Settings -> Audio: a muted device with a clean draft re-applies, and the no-op write is not a refusal
# 71 passed, 0 skipped, 2 failed

# 1/1 test file(s) FAILED: menu/leaves
```

(Case 73 passes before the fix: it is the guard that an `unchanged` write is not a refusal. Cases 71 and 72 fail on
the same line - `expect(fake.applied.length).toBe(0)` - because the device was applied past a refused write, once
under the read-only latch and once under a failed atomic write; Task 4's step 4 shows all three green.)

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/audio/ts/ui/audioDraft.ts b/packages/audio/ts/ui/audioDraft.ts
index 0f27ea03f..f865709de 100644
--- a/packages/audio/ts/ui/audioDraft.ts
+++ b/packages/audio/ts/ui/audioDraft.ts
@@ -17,6 +17,9 @@ import { AUDIO_SECTION_KEY, type AudioSection } from "../configSection";
 export type AudioDraft = AudioDeviceChoice;
 
 let draft: AudioDraft | null = null;
+/** The last Apply the config file refused, for the rows to say why, or "" when none did. Session state beside the
+ *  draft, cleared the same ways (a landed Apply, a new visit). */
+let notSaved = "";
 const notify = createSubscribable();
 const emit = notify.emit;
 export const subscribeAudioDraft = notify.subscribe;
@@ -52,23 +55,39 @@ export function audioDraftDirty(host: Pick<UiHost, "config">): boolean {
   return JSON.stringify(getAudioDraft(host)) !== JSON.stringify(savedAudio(host));
 }
 
-/** Drop any pending edits back to the saved value, and forget the sibling lists fetched for this visit. The audio
- *  menu's `onOpen` runs it, so every visit starts from what the device is running and from a sibling no step of the
- *  new visit has asked about yet. */
+/** Drop any pending edits back to the saved value, forget the sibling lists fetched for this visit, and clear a
+ *  refusal the last visit met. The audio menu's `onOpen` runs it, so every visit starts from what the device is running,
+ *  from a sibling no step of the new visit has asked about yet, and from no stale warning. */
 export function resetAudioDraft(): void {
   draft = null;
+  notSaved = "";
   siblingDrivers.clear();
   askedBackends.clear();
   cachedFor = "";
   emit();
 }
 
+/** The row's text while the last Apply was refused by the config file, or "" (nothing to say). */
+export function audioNotSaved(): string {
+  return notSaved;
+}
+
 /** Commit the draft: write the section (the file, through the contributor's parse) and hand the same choice to the
- *  device, then re-seed from what was saved. The status the host returns is what the muted row shows. */
+ *  device, then re-seed from what was saved. The status the host returns is what the muted row shows. A file that
+ *  REFUSES the write leaves the device alone and the draft where it is: applying it anyway runs something the next
+ *  launch will not open, and the rows would then describe a device that is not the one playing. Keeping the draft keeps
+ *  the rows showing what was asked, with Apply still live to retry once the file takes it. */
 export function applyAudioDraft(host: UiHost): void {
   if (!host.audioDevice) return;
   const choice = getAudioDraft(host);
-  host.config.setSection(AUDIO_SECTION_KEY, { ...choice });
+  // "Holds the patch" is written OR unchanged (a muted device's retry re-sends the saved choice), so a no-op write must
+  // not read as a refusal.
+  if (!host.config.setSection(AUDIO_SECTION_KEY, { ...choice })) {
+    notSaved = "Not saved: config.json could not be written";
+    emit();
+    return;
+  }
+  notSaved = "";
   const status = host.audioDevice.apply(choice);
   // The device may clamp or round (or fall back to the null device); the section keeps the CHOICE, which is what a
   // later launch opens, and the draft follows it rather than what the driver happened to obtain.
diff --git a/packages/audio/ts/ui/index.ts b/packages/audio/ts/ui/index.ts
index 3e3420da4..685e60a73 100644
--- a/packages/audio/ts/ui/index.ts
+++ b/packages/audio/ts/ui/index.ts
@@ -11,7 +11,8 @@ import { defineUi, type MenuBuilder, type UiHost, type UiService } from "@retrop
 import { AUDIO_OUT_CHANNELS } from "../configSection";
 import {
   applyAudioDraft, applySavedAudio, audioDescription, audioDevices, audioDraftDirty, audioDriverChoices,
-  fetchMoreDrivers, getAudioDraft, moreDriversAvailable, resetAudioDraft, setAudioDraft, subscribeAudioDraft,
+  audioNotSaved, fetchMoreDrivers, getAudioDraft, moreDriversAvailable, resetAudioDraft, setAudioDraft,
+  subscribeAudioDraft,
 } from "./audioDraft";
 
 export * from "./audioDraft";
@@ -73,6 +74,7 @@ function audioSettingsChildren(host: UiHost): MenuItem[] {
   const devName = (n: number) => (n === 0 ? "" : devices.names[n - 1] ?? cfg.device);
   const dirty = audioDraftDirty(host);
   const muted = described.status.error;
+  const notSaved = audioNotSaved();
   return [
     // The cyclers stage a pending value only: the label tracks the draft, the device is unchanged until Apply. Picking
     // a driver resets the device, because a device name is not valid across backends.
@@ -107,6 +109,9 @@ function audioSettingsChildren(host: UiHost): MenuItem[] {
     ...(muted
       ? [separator("audio-sep-status"), infoRow("audio-status", `Muted: ${muted}`)]
       : []),
+    // An Apply the file refused: the rows still show the staged choice (nothing was applied), and this says why the
+    // device did not move. Cleared by the next Apply that lands, or the next visit.
+    ...(notSaved ? [infoRow("audio-not-saved", notSaved)] : []),
     // A run's flags win over the saved section (E12), so the rows say which choice is NOT the one running rather than
     // showing a saved value the device is not using.
     ...(runOverride(described)
diff --git a/packages/core/ts/host.ts b/packages/core/ts/host.ts
index 2dd714c2e..25ec72503 100644
--- a/packages/core/ts/host.ts
+++ b/packages/core/ts/host.ts
@@ -242,8 +242,11 @@ export interface ConfigHost {
    *  refused, not answered. */
   section<T = Record<string, unknown>>(key: string): T;
   /** Apply a patch through the contributor's `merge`, written through a re-read of the file (two processes may share
-   *  it). THROWS when it refuses the patch; does nothing for a section stamped newer than its contributor. */
-  setSection(key: string, patch: Readonly<Record<string, unknown>>): void;
+   *  it), and answer whether the file HOLDS the patch: true for a patch it now carries or already carried, false when
+   *  it refused it (a file a newer build wrote, which reads read-only; a failed write; a block stamped newer than its
+   *  contributor). A caller that changes something outside the file on the strength of a write - a device, a link - goes
+   *  ahead on true and does nothing on false. THROWS when it refuses the patch's shape. */
+  setSection(key: string, patch: Readonly<Record<string, unknown>>): boolean;
 }
 
 /** One choice of the audio device: the `audio` config section, exactly as it is saved - the backend and driver by
diff --git a/packages/retroplug/src/userConfigStore.ts b/packages/retroplug/src/userConfigStore.ts
index bb5e73a54..0ae4d8aa3 100644
--- a/packages/retroplug/src/userConfigStore.ts
+++ b/packages/retroplug/src/userConfigStore.ts
@@ -136,15 +136,28 @@ export class UserConfigStore {
   }
 
   /** Apply a patch to a section through its contributor's `merge`, written through a re-read of the file (two
-   *  processes may share it). THROWS when the package has no such section or the contributor refuses the patch, and
-   *  does nothing for a block stamped newer than its contributor (one warning, and the block stays byte-identical).
-   *  The stamp check and the merge run over the RE-READ block, inside the store's apply callback: the block's truth is
-   *  the file, and another process may have moved it since this one read it. */
+   *  processes may share it), and answer whether the file HOLDS it. THROWS when the package has no such section or the
+   *  contributor refuses the patch; a block stamped newer than its contributor is a refusal (one warning, and the block
+   *  stays byte-identical). This is `writeSection(...) === "written"`: a caller that only distinguishes saved from not
+   *  saved wants `writeSection`, because "unchanged" is a satisfied patch, not a failure. */
   setSection(key: string, patch: Readonly<Record<string, unknown>>): boolean {
+    return this.writeSection(key, patch) === "written";
+  }
+
+  /** Apply a patch to a section through its contributor's `merge`, written through a re-read of the file (two
+   *  processes may share it), and say how it landed - three answers, because a caller cannot act on two: `"written"`
+   *  is a patch the file now holds, `"unchanged"` is a patch that changes nothing (a row re-sending the saved choice: a
+   *  caller that gates something on the file, such as a device, must do it), and `"refused"` is a file that would not
+   *  take it (the read-only latch a newer build's file sets, a failed atomic write, a block stamped newer than its
+   *  contributor). THROWS when the package has no such section or the contributor refuses the patch. The stamp check
+   *  and the merge run over the RE-READ block, inside the store's apply callback: the block's truth is the file, and
+   *  another process may have moved it since this one read it. */
+  writeSection(key: string, patch: Readonly<Record<string, unknown>>): "written" | "unchanged" | "refused" {
     assertConfigSectionKey(key);
     const entry = configSectionEntryOf(key, this.configSections);
     if (!entry) throw new Error(`${key} is not a config section this product's packages contribute`);
-    return this.apply((model) => {
+    let stampedNewer = false;
+    const answer = this.apply((model) => {
       const stored = model.sections[key];
       const version = entry.section.version;
       const stamped = version !== undefined && isObject(stored) ? readSectionVersion(stored) : 0;
@@ -156,6 +169,7 @@ export class UserConfigStore {
               "untouched. A newer build wrote it.",
           );
         }
+        stampedNewer = true;
         return model; // unchanged, so the commit writes nothing and the newer block stays byte-identical
       }
       const current = stored === undefined ? configSectionDefaults(entry) : stored;
@@ -166,6 +180,7 @@ export class UserConfigStore {
       putConfigSection(sections, key, next);
       return { ...model, sections, written: [...(model.written ?? []), key] };
     });
+    return stampedNewer ? "refused" : answer;
   }
   defaultZoom(): number {
     return this.current.defaultZoom;
@@ -275,7 +290,7 @@ export class UserConfigStore {
    *  session wrote - because serialising a contributed block from its parsed value drops every field this build does not
    *  know, and config.json is shared with builds of other ages (the verbatim-keep rule). */
   private update(fn: (cfg: UserConfig) => UserConfig): boolean {
-    return this.apply((model) => ({ ...model, core: fn(model.core) }));
+    return this.apply((model) => ({ ...model, core: fn(model.core) })) === "written";
   }
 
   /** Apply `fn` to the config AS IT IS ON DISK, then commit. The file is shared - the DAW plugin, another standalone -
@@ -283,7 +298,7 @@ export class UserConfigStore {
    *  copy wrote that whole stale copy back: one Connect here reverted every setting the plugin had changed since, and
    *  the plugin's own watcher then adopted the reverts live. This is why a section is patched over the re-read copy
    *  too: a package's block and the shell's fields come from the one file. */
-  private apply(fn: (model: ConfigModel) => ConfigModel): boolean {
+  private apply(fn: (model: ConfigModel) => ConfigModel): "written" | "unchanged" | "refused" {
     this.syncFromDisk();
     return this.commit(fn(this.model()));
   }
@@ -329,16 +344,17 @@ export class UserConfigStore {
     return `${this.backend.configDir()}/${CONFIG_FILE}`;
   }
 
-  // Adopt `next` if it differs from the current config: persist atomically + notify.
-  private commit(next: ConfigModel): boolean {
-    if (this.readOnly) return false; // a newer build wrote this file; ours is not the version that gets to edit it
+  // Adopt `next` if it differs from the current config: persist atomically, notify, and say how it landed.
+  private commit(next: ConfigModel): "written" | "unchanged" | "refused" {
+    // A newer build wrote this file; ours is not the version that gets to edit it.
+    if (this.readOnly) return "refused";
     const after = this.serialize(next);
-    if (after === this.serialize()) return false; // genuine no-op
+    if (after === this.serialize()) return "unchanged"; // genuine no-op: the file already holds it
     // Adopted only once it is on disk. A failed write (on Windows, another process holding the file open without
     // FILE_SHARE_DELETE fails the rename) must not show the change all session and lose it at the next launch.
-    if (!this.backend.writeFileAtomic(this.filePath(), enc.encode(after))) return false;
+    if (!this.backend.writeFileAtomic(this.filePath(), enc.encode(after))) return "refused";
     this.adopt(next);
     this.onChange();
-    return true;
+    return "written";
   }
 }
diff --git a/packages/retroplug/ui/host/uiHost.ts b/packages/retroplug/ui/host/uiHost.ts
index 250683e9f..4629c82ef 100644
--- a/packages/retroplug/ui/host/uiHost.ts
+++ b/packages/retroplug/ui/host/uiHost.ts
@@ -81,7 +81,8 @@ export function createUiHost(stores: AppStores, peers: UiHostPeers): UiHost {
     } satisfies ProjectHost,
     config: {
       section: (key) => stores.userConfig.section(key),
-      setSection: (key, patch) => void stores.userConfig.setSection(key, patch),
+      // "Does the file hold it": a patch that changed nothing counts, a refusal does not (see writeSection).
+      setSection: (key, patch) => stores.userConfig.writeSection(key, patch) !== "refused",
     } satisfies ConfigHost,
     // The two device groups exist only where the host binds their hooks (the SDL standalone): a DAW-hosted editor and
     // the UI harness have no audio device or MIDI stream of their own, so their rows are not drawn at all rather than
````

- [ ] **Step 4: run them, and see them pass**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs config/user-config
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs config/config-sections
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs menu/leaves
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs audio/audioDraft
```

```
ok 24 - writeSection answers written, unchanged or refused, which the boolean setSection cannot
# 24 passed, 0 skipped, 0 failed
```

```
# 15 passed, 0 skipped, 0 failed
```

```
ok 71 - Settings -> Audio: an Apply the config refuses leaves the device alone, keeps the draft, and says why
ok 72 - Settings -> Audio: an Apply whose write FAILS leaves the device alone, keeps the draft, and says why
ok 73 - Settings -> Audio: a muted device with a clean draft re-applies, and the no-op write is not a refusal
# 73 passed, 0 skipped, 0 failed
```

```
# 4 passed, 0 skipped, 0 failed
```

- [ ] **Step 5: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test
timeout -k 10 1200 .superpowers/kb3plan/guarded.sh pnpm typecheck
```

Expected: PASS. `pnpm test` prints `243 test file(s) passed`; `pnpm typecheck` walks every package and the plugin
product's tree and prints `Done` for each, which is what holds the `ConfigHost.setSection` signature change to the
shell's one binding and to the fakes the tests declare.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/retroplug/src/userConfigStore.ts packages/retroplug/ui/host/uiHost.ts \
    packages/core/ts/host.ts packages/audio/ts/ui/audioDraft.ts packages/audio/ts/ui/index.ts \
    packages/retroplug/test/config/user-config.test.ts packages/retroplug/test/config/config-sections.test.ts \
    packages/retroplug/test/menu/leaves.test.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): keep the device alone when the config refuses an Apply

applyAudioDraft wrote the section and called audioDevice.apply regardless, and the shell dropped the store's answer, so
under a read-only config a newer RetroPlug wrote - or a failed atomic write - the device switched and the rows then
snapped back to the saved values with nothing said. setSection's boolean could not gate it: false also means "nothing
changed", and an Apply with a clean draft is the muted device's retry. writeSection answers written, unchanged or
refused; the host's setSection returns whether the file holds the patch; and a refusal reaches no device, keeps the
draft and the Apply row live, and shows the row that says why. The Launchpad's config writes keep ignoring the answer:
its applyLink re-reads the saved choice, so a refusal claims nothing new.
MSG
```

---

## Task 5: Settings > MIDI reads the stream's state every poll

The checklist renders from a cached `describe()` - which probes RtMidi, so it runs only on the service's edges. A
Launchpad claim or release changes the stream's opened lists and its reserved pair but none of those edges, and on the
start screen the MIDI container never closes (the start menu's `onClose` is a no-op), so a claimed port stays an
ordinary "[x]" pick: tapping it turns All devices off, and with All devices off it saves the Launchpad's port into the
project. The fix is a member-read `state()` on the device, bound natively beside `jsMidiDescribe`, which the service's
poll merges into its cached listing while its menu is open, through the same `host.menuOpen !== false` gate the rows key
is built with: with the container collapsed the poll makes no native call at all, which is what a handheld needs.

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `packages/core/ts/host.ts:351-359` (`MidiState`, `MidiDeviceHost.state()`).
- Modify: `packages/midi/ts/ui/index.ts:320-326` (the poll's merge).
- Modify: `packages/midi/ts/testSupport/fakeDevice.ts:5-10, :56-62, :88-94` (the fake's hook).
- Modify: `packages/native/sdl/hooks.cpp:183-189` (`__rp_midiState` beside `jsMidiDescribe`), `:372-378` (its binding).
- Modify: `packages/retroplug/ui/host/nativeHooks.ts:61-67`, `packages/retroplug/ui/host/uiHost.ts:109-115` (the
  declaration and the binding).
- Test: `packages/midi/ts/test/midiService.test.ts:35-41` (the rig's `state`), `:53-59` (the device), `:408-411` (the
  case).

**Interfaces:**

- Consumes: `MidiDescription`'s `openedInputs` / `openedOutputs` / `reserved`, `MidiStream::openedInputs()` /
  `openedOutputs()` / `reservedInput()` / `reservedOutput()`, the service's `listing` cache and `rowsKey`.
- Produces: `MidiState` and `MidiDeviceHost.state?(): MidiState | null` (`@retroplug/core/host`); the global
  `__rp_midiState()` (`nativeHooks.ts`, `hooks.cpp`); `FakeMidiDevice` implementing it. No later task uses it. The port
  lists stay the last describe's: a claim changes what is open, not what is listed.

### Steps

- [ ] **Step 1: write the failing test**

One case over the fake: a claim made with no edge repaints once, the claimed port's row reads "(control surface)" and no
pick row names it, the describe count does not move; the release mirrors it; each poll that changed nothing reports
`false`.

````patch
diff --git a/packages/midi/ts/test/midiService.test.ts b/packages/midi/ts/test/midiService.test.ts
index bcb153298..d46e8d3b4 100644
--- a/packages/midi/ts/test/midiService.test.ts
+++ b/packages/midi/ts/test/midiService.test.ts
@@ -35,6 +35,9 @@ interface Rig {
 function rig(overrides: {
   describe?: () => MidiDescription;
   throwOnApply?: boolean;
+  /** The stream's own state read (opened ports and the reserved pair), as the host's `state()` answers it: separate
+   *  from `describe`, which probes. */
+  state?: () => Pick<MidiDescription, "openedInputs" | "openedOutputs" | "reserved">;
   /** What the device does with an apply: a fake whose ports OPEN, so a case can hold what the rows read afterwards. */
   onApply?: (selection: MidiSelection, mode: string) => void;
 } = {}): Rig {
@@ -53,6 +56,7 @@ function rig(overrides: {
     transport: () => ({ playing: false, external: false, bpm: 120, localBpm: mockSection.clockBpm }),
     setTransport: (playing) => transportSet.push(playing),
     setClockBpm: (bpm) => clock.push(bpm),
+    ...(overrides.state ? { state: overrides.state } : {}),
   };
   const host = {
     midiDevice: device,
@@ -408,3 +412,43 @@ test("a host with no MIDI stream (a DAW's editor) contributes no row at all", ()
   expect(menu("settings.midi").build(host, null, { systems: [], assetRom: () => null, romPatterns: [] })).toEqual([]);
   expect(menu("instance.clock").build(host, null, { systems: [], assetRom: () => null, romPatterns: [] })).toEqual([]);
 });
+
+test("a control surface's claim repaints the checklist with no edge, and reads no port list to do it", () => {
+  // The Launchpad claims its pair from ITS service's poll: the stream closes the claimed input and its reserved pair
+  // moves, and none of this service's edges does (no section byte, no project revision, no container opening). The
+  // stream's own state answers it every poll - member reads, no probe - so the rows follow a claim, and a release.
+  let opened = ["Launchpad Pro MK3", "Arturia KeyStep 32"];
+  let reserved = { input: "", output: "" };
+  const r = rig({
+    describe: countingDescribe(),
+    state: () => ({ openedInputs: opened, openedOutputs: [], reserved }),
+  });
+  const svc = service();
+  svc.start!(r.host);
+  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] }); // the user picked the pad before the claim
+  const build = () => menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
+  describes = 0;
+  svc.poll!.run(r.host); // the mount's apply, against the machine as it is
+  const listed = describes;
+  expect(build().find((row) => row.id === "midi-input-0")!.label).toBe("[x] Launchpad Pro MK3");
+
+  // The claim lands. No edge fires: the poll sees it through the stream's state, repaints once, and asks for no listing.
+  opened = ["Arturia KeyStep 32"];
+  reserved = { input: "Launchpad Pro MK3", output: "" };
+  expect(svc.poll!.run(r.host)).toBe(true);
+  expect(r.applied.length).toBe(1); // a claim is not a project change: nothing is re-applied
+  expect(describes).toBe(listed);
+  const rows = build();
+  expect(rows.find((row) => row.id === "midi-reserved-in")!.label).toBe("[ ] Launchpad Pro MK3 (control surface)");
+  expect(rows.some((row) => row.id.startsWith("midi-input-") && row.label.includes("Launchpad Pro MK3"))).toBe(false);
+  expect(svc.poll!.run(r.host)).toBe(false); // once, not every frame
+
+  // The release mirrors it: the pick comes back, and the held row goes.
+  opened = ["Launchpad Pro MK3", "Arturia KeyStep 32"];
+  reserved = { input: "", output: "" };
+  expect(svc.poll!.run(r.host)).toBe(true);
+  const after = build();
+  expect(after.some((row) => row.id === "midi-reserved-in")).toBe(false);
+  expect(after.find((row) => row.id === "midi-input-0")!.label).toBe("[x] Launchpad Pro MK3");
+  expect(svc.poll!.run(r.host)).toBe(false);
+});
````

- [ ] **Step 2: run it, and see it fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs midi/midiService
```

Expected: FAIL with the description-only cache. The claim changes nothing the poll looks at, so the poll reports no
move (`expected true, got false`):

```
ok 20 - a host with no MIDI stream (a DAW's editor) contributes no row at all
not ok 21 - a control surface's claim repaints the checklist with no edge, and reads no port list to do it
  ---
  expected true, got false
      at fail (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/midi/midiService.js:86:15)
      at toBe (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/midi/midiService.js:94:84)
      at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.test-build/midi/midiService.js:849:37)

  ...
# 20 passed, 0 skipped, 1 failed

# 1/1 test file(s) FAILED: midi/midiService
```

(The fake, `MidiState` in the host contract and `fakeDevice.ts` are part of this step's patch, so the case compiles and
fails on the behaviour, not on a missing symbol.)

- [ ] **Step 3: implement it**

````patch
diff --git a/packages/core/ts/host.ts b/packages/core/ts/host.ts
index 25ec72503..9860714a0 100644
--- a/packages/core/ts/host.ts
+++ b/packages/core/ts/host.ts
@@ -351,10 +351,19 @@ export interface TransportState {
   localBpm: number;
 }
 
+/** The stream's own state, as its member reads answer it: the ports it has open and the pair a control surface holds.
+ *  No probe of the machine (that is `describe`), so a poll may read this every frame. The port LISTS stay the last
+ *  describe's, because a claim changes what is open, not what the machine offers. */
+export type MidiState = Pick<MidiDescription, "openedInputs" | "openedOutputs" | "reserved">;
+
 /** The MIDI device the standalone owns: its stream and the transport the clock drives. A host with no MIDI device
  *  (the plugin, whose DAW routes MIDI) does not offer the group. */
 export interface MidiDeviceHost {
   describe(): MidiDescription | null;
+  /** The stream's state right now, or null on a host that cannot say (an older binding, a stream that is closed).
+   *  Member reads only: a control surface's claim or release moves this without raising any of the service's edges (a
+   *  section byte, a project revision, the container opening), which is why the rows read it on every poll. */
+  state?(): MidiState | null;
   /** Apply a selection to the stream. `mode` is the stream's: "diff" keeps the ports an unchanged selection already
    *  holds open (a project load), "reopen" lets every port go before opening it again (the Rescan row and a re-pick,
    *  the one remedy for a port that was unplugged). */
diff --git a/packages/midi/ts/ui/index.ts b/packages/midi/ts/ui/index.ts
index 644f02825..09f4838c0 100644
--- a/packages/midi/ts/ui/index.ts
+++ b/packages/midi/ts/ui/index.ts
@@ -320,6 +320,13 @@ const midiService: UiService = {
         lastClock = bpm;
         dev.setClockBpm(bpm);
       }
+      // What moved with no edge: a control surface's claim or release, which the Launchpad's own service makes while
+      // this container is open, closes the claimed input and moves the reserved pair. `state` is the stream's members,
+      // not a listing, so this costs the machine nothing; what is LISTED stays the last describe's, because a claim
+      // changes what is open, not what the machine offers. Read only while the menu is open - the same gate the rows
+      // key is built with below - so a collapsed container pays no native call per frame on a handheld.
+      const state = host.menuOpen !== false ? dev.state?.() ?? null : null;
+      if (state && listing?.dev === dev && listing.value) listing = { dev, value: { ...listing.value, ...state } };
       // The rows follow the listing, the section and the transport: a poll repaints only when one of them moved.
       const rows = rowsKey(listing?.value ?? null, section, dev.transport(), host.menuOpen !== false);
       const moved = rows !== lastRows;
diff --git a/packages/midi/ts/testSupport/fakeDevice.ts b/packages/midi/ts/testSupport/fakeDevice.ts
index 12b97d260..40133a350 100644
--- a/packages/midi/ts/testSupport/fakeDevice.ts
+++ b/packages/midi/ts/testSupport/fakeDevice.ts
@@ -5,6 +5,7 @@ import type { MidiDescription, MidiSelection, TransportState } from "@retroplug/
 
 interface MidiHooks {
   __rp_midiDescribe?: () => MidiDescription | null;
+  __rp_midiState?: () => Pick<MidiDescription, "openedInputs" | "openedOutputs" | "reserved"> | null;
   __rp_midiApply?: (selection: MidiSelection, mode: "diff" | "reopen") => void;
   __rp_getTransport?: () => Partial<TransportState>;
   __rp_setTransport?: (playing: boolean) => void;
@@ -56,6 +57,15 @@ export class FakeMidiDevice {
 
   install(): this {
     globals.__rp_midiDescribe = () => (this.open ? this.description : null);
+    // What the stream holds, with no listing: the hook a poll reads every frame, which is how a claim made while the
+    // menu is open reaches the rows.
+    globals.__rp_midiState = () => (this.open
+      ? {
+        openedInputs: [...this.description.openedInputs],
+        openedOutputs: [...this.description.openedOutputs],
+        reserved: { ...this.description.reserved },
+      }
+      : null);
     globals.__rp_midiApply = (selection, mode) => {
       this.applied.push({
         selection: { ...selection, inputs: [...selection.inputs], outputs: [...selection.outputs] },
@@ -88,6 +98,7 @@ export class FakeMidiDevice {
 
   uninstall(): void {
     delete globals.__rp_midiDescribe;
+    delete globals.__rp_midiState;
     delete globals.__rp_midiApply;
     delete globals.__rp_getTransport;
     delete globals.__rp_setTransport;
diff --git a/packages/native/sdl/hooks.cpp b/packages/native/sdl/hooks.cpp
index ba8dfb74b..cc39224f9 100644
--- a/packages/native/sdl/hooks.cpp
+++ b/packages/native/sdl/hooks.cpp
@@ -183,6 +183,23 @@ JSValue jsMidiDescribe(JSContext* ctx, JSValueConst, int, JSValueConst*) {
     return o;
 }
 
+// __rp_midiState(): { openedInputs, openedOutputs, reserved: { input, output } } - what the stream itself holds, with no
+// probe of the machine. jsMidiDescribe above asks the backend for the port lists, which on ALSA creates and destroys an
+// RtMidi client that every other sequencer client sees announced; a poll that runs every frame (Settings > MIDI's rows
+// follow a control surface's claim made while the container is open) may only read members. So the lists stay the last
+// describe's and this answers what moved since.
+JSValue jsMidiState(JSContext* ctx, JSValueConst, int, JSValueConst*) {
+    JSValue o = JS_NewObject(ctx);
+    if (!g_app) return o;
+    setStringArray(ctx, o, "openedInputs", g_app->midi.openedInputs());
+    setStringArray(ctx, o, "openedOutputs", g_app->midi.openedOutputs());
+    JSValue reserved = JS_NewObject(ctx);
+    JS_SetPropertyStr(ctx, reserved, "input", JS_NewString(ctx, g_app->midi.reservedInput().c_str()));
+    JS_SetPropertyStr(ctx, reserved, "output", JS_NewString(ctx, g_app->midi.reservedOutput().c_str()));
+    JS_SetPropertyStr(ctx, o, "reserved", reserved);
+    return o;
+}
+
 /** The selection object's `names` list, as the strings a row wrote. */
 std::vector<std::string> stringListOf(JSContext* ctx, JSValueConst value) {
     std::vector<std::string> out;
@@ -372,6 +389,7 @@ void installWindowHooks(JSContext* ctx) {
     // The MIDI stream (Settings > MIDI and the control surface's claim). RtMidi is compiled into retroplug-sdl, so the
     // seam always exists here; the plugin never binds these, so its rows and its transport are absent.
     bind("__rp_midiDescribe", jsMidiDescribe, 0);
+    bind("__rp_midiState", jsMidiState, 0);
     bind("__rp_midiApply", jsMidiApply, 2);
     // The OS-native file dialog — only where a desktop helper exists (zenity/kdialog/…). Where none does
     // (the muOS handheld, a bare container) we DON'T bind it: useFileBrowser sees no nativeHook and the
diff --git a/packages/retroplug/ui/host/nativeHooks.ts b/packages/retroplug/ui/host/nativeHooks.ts
index 49da08a29..9a8d77b69 100644
--- a/packages/retroplug/ui/host/nativeHooks.ts
+++ b/packages/retroplug/ui/host/nativeHooks.ts
@@ -61,6 +61,9 @@ export interface RpHooks {
   // The MIDI stream: the ports this machine lists and what actually opened, the selection applied by exact name, and
   // the transport the MIDI Clock submenu drives. The selection is the project's; the stream is the host's.
   __rp_midiDescribe?: () => MidiDescription | null;
+  // The stream's own state (what is open, and the pair a control surface holds): member reads, no probe, so a per-frame
+  // poll may call it.
+  __rp_midiState?: () => Pick<MidiDescription, "openedInputs" | "openedOutputs" | "reserved"> | null;
   __rp_midiApply?: (selection: MidiSelection, mode: "diff" | "reopen") => void;
   __rp_getTransport?: () => Partial<TransportState>;
   __rp_setTransport?: (playing: boolean) => void;
diff --git a/packages/retroplug/ui/host/uiHost.ts b/packages/retroplug/ui/host/uiHost.ts
index 4629c82ef..724735833 100644
--- a/packages/retroplug/ui/host/uiHost.ts
+++ b/packages/retroplug/ui/host/uiHost.ts
@@ -109,6 +109,8 @@ function midiDeviceHost(): MidiDeviceHost | undefined {
   if (typeof native.__rp_midiDescribe !== "function") return undefined;
   return {
     describe: () => native.__rp_midiDescribe?.() ?? null,
+    // Absent on a host that predates the hook (or has no stream): the service then follows the edges alone.
+    state: native.__rp_midiState ? () => native.__rp_midiState?.() ?? null : undefined,
     apply: (selection, mode = "diff") => native.__rp_midiApply?.(selection, mode),
     transport: () => {
       const t = native.__rp_getTransport?.();
````

- [ ] **Step 4: run it, and see it pass**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-tests.mjs midi/midiService
```

```
ok 21 - a control surface's claim repaints the checklist with no edge, and reads no port list to do it
# 21 passed, 0 skipped, 0 failed

# 1 test file(s) passed (jobs=16)
```

- [ ] **Step 5: the wider suites this task reaches**

```
cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-sdl retroplug-ui-test -j$(nproc)
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test
timeout -k 10 600 .superpowers/kb3plan/guarded.sh pnpm test:ui
```

Expected: PASS, and the SDL standalone links the new hook (`native/sdl/hooks.cpp` is the only product that binds
`__rp_midi*`), the UI suite still passing the rows this poll paints:

```
[100%] Built target retroplug-sdl
...
[100%] Built target retroplug-ui-test
```

Never RUN `retroplug-sdl`: building it is the check.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/core/ts/host.ts packages/midi/ts/ui/index.ts \
    packages/midi/ts/testSupport/fakeDevice.ts packages/native/sdl/hooks.cpp \
    packages/midi/ts/test/midiService.test.ts packages/retroplug/ui/host/nativeHooks.ts packages/retroplug/ui/host/uiHost.ts
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): read the MIDI stream's state every poll

Settings > MIDI's checklist rendered from a cached describe(), and describe() probes RtMidi, so it runs only on the
service's edges. A control surface's claim changes the stream's opened lists and its reserved pair but fires none of
them, and on the start screen the container never closes, so a claimed port stayed an ordinary pick: tapping it turned
All devices off, and with All devices off it saved the pad's port into the project. The host now offers state(), member
reads only, and the service's poll merges it into the cached listing while its menu is open, so the rows key moves once
and the menu repaints. The port lists stay the last describe's: a claim changes what is open, not what is listed.
MSG
```

---

## Task 6: a writer thread per hardware MIDI output

Only the SDL standalone sends from its audio callback (`packages/native/sdl/audioLoop.cpp`), and a port's send may block
until the bytes are on the wire: WinMM's SysEx path allocates and sleeps until they leave it (a 538-byte Launchpad
repaint is about 170 ms at DIN rate), ALSA's grows its encode buffer and prints its warnings from the caller, CoreMIDI
makes an OS call. The fix is `rp::midi::PortWriter`: one hardware output, a preallocated SPSC ring of length-prefixed
records (a push is all or nothing, so a SysEx is never torn), a writer thread, and a wake the audio thread gives without
a lock or an allocation. `MidiStream` takes a mode; the SDL standalone is the one `Threaded` consumer, and the CLI's and
the test host's facet stays `Inline` (their caller is the JS thread, and a script relies on a returned `midiSend` being
on the wire). The virtual output is inline in both.

The wake's four operations on the counter and `sleeping_` are `seq_cst`, for the reason `MidiStream::retire` gives for
its own two: the wake is the store-buffering (Dekker) shape, the writer writes `sleeping_` and then re-reads the
counter while the producer bumps the counter and then reads `sleeping_`, and under acquire/release both store-load
pairs may be reordered - the writer would then sleep with a record queued (a Note Off delayed until the next message),
which x86 hides and AArch64 (the handheld and Pi builds) does not. The producer EXCHANGES `sleeping_` for false rather
than reading it, so only the push that finds the writer still asleep pays the futex, once per sleep rather than once
per message. `RETROPLUG_MIDI_LOG`'s out line moves with the write and names the port, because a `Threaded` stream has
one writer per output and two ports sending the same bytes must not print the same line twice.

Two TDD cycles, because the class and the mode are two seams: the writer's own cases, then the stream's. The five
stream cases whose contract the two modes share (the exclusive port's re-pick, a close under a send in flight, an
output swap under a send, a diff's close before its open, a swap behind back-to-back sends) run in BOTH modes through
`GENERATE`, reading what a port received only after its writer has drained, so the ThreadSanitizer leg exercises the
ring and the flush with a real concurrent sender, not only the writer's own cases.

**Files** (line numbers are the file at `080c0c0ba`):

- Create: `packages/midi/native/src/midi/PortWriter.hpp` (130 lines), `.../PortWriter.cpp` (133 lines),
  `packages/midi/native/test/PortWriter.test.cpp` (250 lines), `.../test/AllocationCounter.hpp` (34 lines: the
  global `operator new` PortWriter.test.cpp defines, shared with the stream's own allocation case).
- Modify: `docs/sdl-standalone.md:66` (`RETROPLUG_MIDI_LOG`'s out line: written where the write happens, and naming
  the port in `Threaded` mode).
- Modify: `packages/midi/native/CMakeLists.txt:25-30` (the library's sources), `:55-60` (the test binary's).
- Modify: `packages/midi/native/src/midi/MidiStream.hpp:25-38` (the class comment), `:46-51` (the include), `:67-73`
  (the constructor), `:134-139` (`droppedOutputCount`), `:142-158` (`OutputSet`), `:158-164` (the log's doc), `:183-200`
  (`mode_`, `hwOut_`).
- Modify: `packages/midi/native/src/midi/MidiStream.cpp:21-29` (the constructor), `:208-226` (the open), `:327-341` (the
  dump's site), `:347-360` (the send).
- Modify: `packages/native/sdl/app.hpp:199-206` (the one `Threaded` stream).
- Test: `packages/midi/native/test/MidiStream.test.cpp:15-21` (the includes), `:279-306` (the exclusive port's re-pick),
  `:344-370` (a close under a send in flight), `:566-598` (an output swap under a send), `:600-622` (the diff's close
  before its open), `:624-655` (a swap behind back-to-back sends): the five gain `GENERATE` over both modes, the
  threaded cases and the allocation case are new at the end of the file.

**Interfaces:**

- Consumes: `MidiPort`, `MidiBackend` (`midi/MidiBackend.hpp`), `FakeMidiBackend`'s `sendDelayMs`, `onSend`,
  `sendsStarted`/`sendsFinished`, `sentTo`, `sendsAfterClose`, `MidiStream`'s epoch handshake and control-thread
  ownership of the ports.
- Produces: `rp::midi::OutputMode { Inline, Threaded }` and `rp::midi::PortWriter` (constructor
  `(std::unique_ptr<MidiPort>, std::string name, Options, LogSink)`, `bool push(const std::uint8_t*, std::size_t)`,
  `void stop()`, `std::uint64_t dropped() const`, `const std::string& name() const`, `kDefaultRingBytes`,
  `kFlushDeadline`); `MidiStream(MidiBackend&, OutputMode mode = OutputMode::Inline)` and
  `std::uint64_t MidiStream::droppedOutputCount() const`. No later task uses them; the SDL app is the only consumer.

### Steps

- [ ] **Step 1: write the failing writer tests**

`PortWriter.test.cpp`, the whole file (7 cases over `FakeMidiBackend`), and the test binary's source list. Nothing else:
the writer and the mode do not exist yet.

````patch
diff --git a/packages/midi/native/CMakeLists.txt b/packages/midi/native/CMakeLists.txt
index 168610b61..abfa98a7a 100644
--- a/packages/midi/native/CMakeLists.txt
+++ b/packages/midi/native/CMakeLists.txt
@@ -55,6 +56,7 @@ target_include_directories(retroplug-midi-test-support INTERFACE ${CMAKE_CURRENT
 retroplug_add_test_binary(
     NAME     retroplug-midi-test
     SOURCES  test/MidiPortSelect.test.cpp
+             test/PortWriter.test.cpp
              test/MidiStream.test.cpp
     LIBS     retroplug-midi               # the interface and the stream
              retroplug-midi-test-support)  # midi/FakeMidiBackend.hpp
diff --git a/packages/midi/native/test/AllocationCounter.hpp b/packages/midi/native/test/AllocationCounter.hpp
new file mode 100644
index 000000000..638943a6c
--- /dev/null
+++ b/packages/midi/native/test/AllocationCounter.hpp
@@ -0,0 +1,34 @@
+#pragma once
+
+// A test-only replacement of the global allocation functions, so a case can prove a Threaded push allocates nothing on
+// the caller's thread (docs/known-bugs-batch-3-design.md section 6): the audio callback may not allocate, and what it
+// calls is PortWriter::push, through MidiStream::send.
+//
+// The count is per THREAD, and only for the thread that armed it: the writer thread's own allocations (the log string,
+// the fake's records) are its own business, and a case that counted them would measure the fake.
+//
+// The operator new/delete replacements themselves are defined ONCE, in PortWriter.test.cpp, and cover the whole
+// retroplug-midi-test binary; this header is how the stream's own case (MidiStream.test.cpp) arms and reads the count.
+
+#include <cstddef>
+
+namespace rp::midi::test {
+
+extern thread_local bool        countAllocations;
+extern thread_local std::size_t allocations;
+
+/** Counts this thread's allocations for as long as it lives: what a case wraps the pushes it measures in. */
+class AllocationCount {
+public:
+    AllocationCount() {
+        allocations      = 0;
+        countAllocations = true;
+    }
+    ~AllocationCount() { countAllocations = false; }
+    AllocationCount(const AllocationCount&)            = delete;
+    AllocationCount& operator=(const AllocationCount&) = delete;
+
+    std::size_t value() const { return allocations; }
+};
+
+}  // namespace rp::midi::test
diff --git a/packages/midi/native/test/PortWriter.test.cpp b/packages/midi/native/test/PortWriter.test.cpp
new file mode 100644
index 000000000..ec56ef3b2
--- /dev/null
+++ b/packages/midi/native/test/PortWriter.test.cpp
@@ -0,0 +1,250 @@
+// Unit-tests PortWriter, one hardware output and the thread that writes to it (docs/known-bugs-batch-3-design.md
+// section 6), over FakeMidiBackend: a send that blocks in the port does not block the caller, a slow port holds nothing
+// else, a full ring drops whole messages and counts them, a stop flushes what is queued before the port goes and
+// abandons the rest at the deadline, a push allocates nothing, and the Inline mode is the old write on the caller's
+// thread. No MIDI system and no hardware: the backend is the fake in midi/FakeMidiBackend.hpp, and the ports are the
+// fake's own.
+//
+// The ring and the flush are what ThreadSanitizer is for: build this target in build-tsan/ too (tools/run-sanitizer.sh's
+// midi leg), as MidiStream.test.cpp says of its own swap and close cases.
+
+#include <catch2/catch_test_macros.hpp>
+
+#include <algorithm>
+#include <atomic>
+#include <chrono>
+#include <cstddef>
+#include <cstdint>
+#include <cstdlib>
+#include <functional>
+#include <memory>
+#include <new>
+#include <string>
+#include <thread>
+#include <vector>
+
+#include "AllocationCounter.hpp"  // the global operator new this file defines, and the count MidiStream.test.cpp reads
+#include "midi/FakeMidiBackend.hpp"
+#include "midi/PortWriter.hpp"
+
+using rp::midi::FakeMidiBackend;
+using rp::midi::OutputMode;
+using rp::midi::PortWriter;
+
+namespace {
+
+using Bytes = std::vector<std::uint8_t>;
+using Clock = std::chrono::steady_clock;
+
+std::vector<std::uint8_t> bytes(std::size_t n, std::uint8_t first = 0x90) {
+    std::vector<std::uint8_t> out(n);
+    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::uint8_t>(first + i);
+    return out;
+}
+
+/** Wait until `done()` or the deadline passes; false on the deadline. A case never sleeps a fixed time for something a
+ *  thread does, so a slow runner costs a longer wait rather than a flake. */
+bool eventually(const std::function<bool()>& done, std::chrono::milliseconds within = std::chrono::milliseconds(5000)) {
+    const auto deadline = Clock::now() + within;
+    while (Clock::now() < deadline) {
+        if (done()) return true;
+        std::this_thread::sleep_for(std::chrono::milliseconds(1));
+    }
+    return done();
+}
+
+/** A backend with two output ports, and the writer a case opens over the first. */
+struct Rig {
+    FakeMidiBackend backend;
+    Rig() { backend.outputs = {"Synth out", "Interface out"}; }
+    std::unique_ptr<PortWriter> open(const PortWriter::Options& options = {}) {
+        return std::make_unique<PortWriter>(backend.openOutput("Synth out"), "Synth out", options,
+                                            [](const std::string&) {});
+    }
+};
+
+}  // namespace
+
+// ---- the caller does not wait ----------------------------------------------------------------------------------
+
+TEST_CASE("a threaded push returns at once while the port takes 200 ms a message, and the bytes arrive whole, in order") {
+    Rig     rig;
+    auto&   backend     = rig.backend;
+    backend.sendDelayMs = 200;
+    auto writer         = rig.open();
+    // A control surface's repaint: 538 bytes, the size the WinMM SysEx path blocks on for about 170 ms at DIN rate.
+    const Bytes sysex = bytes(538, 0xF0);
+    const Bytes note  = {0x90, 0x3C, 0x7F};
+
+    const auto t0 = Clock::now();
+    REQUIRE(writer->push(sysex.data(), sysex.size()));
+    REQUIRE(writer->push(note.data(), note.size()));
+    REQUIRE(writer->push(note.data(), note.size()));
+    REQUIRE(writer->push(note.data(), note.size()));
+    const auto elapsed = Clock::now() - t0;
+    // Three 200 ms writes are queued behind the first: the caller waited for none of them. The bound is loose on
+    // purpose (a macOS or Windows CI runner stalls a thread for tens of milliseconds), and the count says the property
+    // outright: not all four writes had finished when the pushes returned.
+    CHECK(elapsed < std::chrono::milliseconds(150));
+    CHECK(backend.sendsFinished.load() < 4);
+    REQUIRE(eventually([&] { return backend.sendsFinished.load() == 4; }));
+    // Every byte, in the order it was pushed, each message whole.
+    const std::vector<Bytes> sent = backend.sentTo("Synth out");
+    REQUIRE(sent.size() == 4);
+    CHECK(sent[0] == sysex);
+    CHECK(sent[1] == note);
+    CHECK(sent[2] == note);
+    CHECK(sent[3] == note);
+    CHECK(writer->dropped() == 0);
+    writer->stop();
+}
+
+TEST_CASE("a slow port does not delay a fast one: each output has its own writer") {
+    Rig   rig;
+    auto& backend = rig.backend;
+    // One port answers slowly (a device that has to wake), the other at once: the delay is the fake's per-send hook, so
+    // it holds only the writer whose port it is.
+    backend.onSend = [](FakeMidiBackend&, const std::string& name, const std::uint8_t*, std::size_t) {
+        if (name == "Synth out") std::this_thread::sleep_for(std::chrono::milliseconds(1000));
+    };
+    // MidiStream picks the ports it opens; here two writers are built directly, which is what the stream does per
+    // output, so the isolation is this class's and not the stream's.
+    auto slow = std::make_unique<PortWriter>(backend.openOutput("Synth out"), "Synth out", PortWriter::Options{},
+                                             [](const std::string&) {});
+    auto fast = std::make_unique<PortWriter>(backend.openOutput("Interface out"), "Interface out", PortWriter::Options{},
+                                             [](const std::string&) {});
+    const Bytes note = {0x90, 0x3C, 0x7F};
+    const auto  t0   = Clock::now();
+    REQUIRE(slow->push(note.data(), note.size()));
+    REQUIRE(fast->push(note.data(), note.size()));
+    // The fast port's write FINISHED while the slow one is still inside its own (one send of the two has completed).
+    REQUIRE(eventually(
+        [&] { return !backend.sentTo("Interface out").empty() && backend.sendsFinished.load() < 2; },
+        std::chrono::milliseconds(800)));
+    CHECK(Clock::now() - t0 < std::chrono::milliseconds(800));
+    slow->stop();
+    fast->stop();
+    CHECK(backend.sentTo("Synth out").size() == 1);
+}
+
+// ---- Inline, the mode the CLI's facet and the test host use ---------------------------------------------------
+
+TEST_CASE("an inline push is the write, on the caller's thread") {
+    Rig     rig;
+    auto&   backend     = rig.backend;
+    backend.sendDelayMs = 100;
+    auto               writer = rig.open({OutputMode::Inline, false, PortWriter::kDefaultRingBytes});
+    const Bytes        note   = {0x90, 0x3C, 0x7F};
+    const auto         t0     = Clock::now();
+    REQUIRE(writer->push(note.data(), note.size()));
+    // "The push returned" is "it is on the wire": that is what a script relying on midiSend before tjs.exit wants.
+    CHECK(Clock::now() - t0 >= std::chrono::milliseconds(100));
+    CHECK(backend.sentTo("Synth out") == std::vector<Bytes>{note});
+    // No thread was started, so none is left to stop with: the close is the port's.
+    writer->stop();
+    CHECK(backend.sendsAfterClose() == 0);
+    CHECK(!writer->push(note.data(), note.size()));  // a stopped writer takes nothing, whichever mode it is
+}
+
+// ---- overflow -------------------------------------------------------------------------------------------------
+
+TEST_CASE("a full ring drops the newest WHOLE message, counts it, and never tears one") {
+    Rig   rig;
+    auto& backend       = rig.backend;
+    backend.sendDelayMs = 300;  // the first message sits in the port while the ring fills
+    // A ring of four 40-byte records: the fifth cannot fit while the first is in flight.
+    auto          writer = rig.open(PortWriter::Options{OutputMode::Threaded, false, 4 * (sizeof(std::uint32_t) + 40)});
+    const Bytes   first  = bytes(40);
+    REQUIRE(writer->push(first.data(), first.size()));
+    REQUIRE(eventually([&] { return backend.sendsStarted.load() == 1; }, std::chrono::milliseconds(2000)));
+    // The port is inside the first send, so the ring fills: fill it until it refuses, and count the refusals - what it
+    // takes is exact, what it does not is dropped WHOLE.
+    int refused = 0;
+    for (int i = 0; i < 12; ++i)
+        if (!writer->push(first.data(), first.size())) ++refused;
+    CHECK(refused > 0);
+    CHECK(writer->dropped() == static_cast<std::uint64_t>(refused));
+    writer->stop();  // flushes what it took, up to the deadline
+    // Every message that arrived is whole: a fragment is what an all-or-nothing push exists to prevent.
+    const std::vector<Bytes> sent = backend.sentTo("Synth out");
+    for (const Bytes& message : sent) CHECK(message == first);
+    CHECK(sent.size() >= 1);
+}
+
+// ---- stopping -------------------------------------------------------------------------------------------------
+
+TEST_CASE("a stop flushes what queued before the port goes, and nothing is sent after it") {
+    Rig   rig;
+    auto& backend       = rig.backend;
+    backend.sendDelayMs = 20;
+    auto          writer  = rig.open();
+    const Bytes   note    = {0x90, 0x3C, 0x7F};
+    constexpr int kQueued = 8;
+    for (int i = 0; i < kQueued; ++i) REQUIRE(writer->push(note.data(), note.size()));
+    writer->stop();
+    // Every queued message went out before the close (a dropped Note Off is a stuck note), and none after it.
+    CHECK(backend.sentTo("Synth out").size() == kQueued);
+    CHECK(backend.sendsAfterClose() == 0);
+    // A stopped writer takes nothing more: a push is refused rather than reaching a closed port.
+    CHECK(!writer->push(note.data(), note.size()));
+}
+
+TEST_CASE("a wedged port's stop returns by its deadline, and abandons the rest of the backlog") {
+    Rig   rig;
+    auto& backend       = rig.backend;
+    backend.sendDelayMs = 200;  // each write takes 200 ms: the whole backlog would be 4 seconds
+    auto        writer  = rig.open();
+    const Bytes note    = {0x90, 0x3C, 0x7F};
+    for (int i = 0; i < 20; ++i) REQUIRE(writer->push(note.data(), note.size()));
+    const auto t0 = Clock::now();
+    writer->stop();
+    const auto elapsed = Clock::now() - t0;
+    // The deadline, plus the one write that was already inside the port when the stop landed, plus slack.
+    CHECK(elapsed < PortWriter::kFlushDeadline + std::chrono::milliseconds(200 + 500));
+    CHECK(backend.sendsAfterClose() == 0);
+    // It gave up on the rest: fewer than the twenty, and more than none (the flush sent what it could).
+    const std::size_t sent = backend.sentTo("Synth out").size();
+    CHECK(sent >= 1);
+    CHECK(sent < 20);
+}
+
+// ---- the audio thread allocates nothing -----------------------------------------------------------------------
+
+namespace rp::midi::test {
+
+// The header declares these; this TU owns them, and the operator new/delete replacements below cover the whole test
+// binary (MidiStream.test.cpp's Threaded-send case reads the same count).
+thread_local bool        countAllocations = false;
+thread_local std::size_t allocations      = 0;
+
+}  // namespace rp::midi::test
+
+void* operator new(std::size_t size) {
+    if (rp::midi::test::countAllocations) ++rp::midi::test::allocations;
+    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
+    throw std::bad_alloc();
+}
+void* operator new[](std::size_t size) { return ::operator new(size); }
+void  operator delete(void* p) noexcept { std::free(p); }
+void  operator delete[](void* p) noexcept { std::free(p); }
+void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
+void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }
+
+TEST_CASE("a threaded push allocates nothing on the caller's thread") {
+    Rig   rig;
+    auto& backend       = rig.backend;
+    backend.sendDelayMs = 50;  // the ring is being drained while the pushes land
+    auto        writer  = rig.open();
+    const Bytes sysex   = bytes(538, 0xF0);
+    const Bytes note    = {0x90, 0x3C, 0x7F};
+    const auto  t0      = Clock::now();
+    rp::midi::test::AllocationCount count;
+    for (int i = 0; i < 64; ++i) {
+        writer->push(sysex.data(), sysex.size());
+        writer->push(note.data(), note.size());
+    }
+    const bool quiet = count.value() == 0;
+    CHECK(quiet);
+    CHECK(Clock::now() - t0 < std::chrono::milliseconds(150));  // and it did not wait for the port either
+    writer->stop();
+}
````

- [ ] **Step 2: run them, and see them fail**

```
timeout -k 10 600 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-midi-test -j$(nproc)
```

Expected: FAIL to compile, because the header does not exist:

```
[100%] Building CXX object packages/midi/CMakeFiles/retroplug-midi-test.dir/test/PortWriter.test.cpp.o
/workspaces/RetroPlug-main/cuttlefish/packages/midi/native/test/PortWriter.test.cpp:28:10: fatal error: midi/PortWriter.hpp: No such file or directory
   28 | #include "midi/PortWriter.hpp"
gmake[3]: *** [packages/midi/CMakeFiles/retroplug-midi-test.dir/build.make:90: packages/midi/CMakeFiles/retroplug-midi-test.dir/test/PortWriter.test.cpp.o] Error 1
gmake[2]: *** [CMakeFiles/Makefile2:5287: packages/midi/CMakeFiles/retroplug-midi-test.dir/all] Error 2
gmake: *** [Makefile:1971: retroplug-midi-test] Error 2
```

- [ ] **Step 3: implement the writer**

````patch
diff --git a/packages/midi/native/CMakeLists.txt b/packages/midi/native/CMakeLists.txt
index 168610b61..abfa98a7a 100644
--- a/packages/midi/native/CMakeLists.txt
+++ b/packages/midi/native/CMakeLists.txt
@@ -25,6 +25,7 @@ find_package(Threads REQUIRED)
 # --- retroplug-midi: the interface, the RtMidi backend, the port rules and the stream
 retroplug_package_library(retroplug-midi
     ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/MidiStream.cpp
+    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/PortWriter.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/RtMidiBackend.cpp)
 target_include_directories(retroplug-midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
 target_link_libraries(retroplug-midi
diff --git a/packages/midi/native/src/midi/PortWriter.cpp b/packages/midi/native/src/midi/PortWriter.cpp
new file mode 100644
index 000000000..eaafa472a
--- /dev/null
+++ b/packages/midi/native/src/midi/PortWriter.cpp
@@ -0,0 +1,133 @@
+#include "midi/PortWriter.hpp"
+
+#include <cstdio>
+#include <cstring>
+#include <utility>
+
+namespace rp::midi {
+
+PortWriter::PortWriter(std::unique_ptr<MidiPort> port, std::string name, Options options, LogSink log)
+    : port_(std::move(port)),
+      name_(std::move(name)),
+      options_(options),
+      log_(std::move(log)),
+      ring_(options.ringBytes < kHeader * 2 ? kHeader * 2 : options.ringBytes),
+      scratch_(ring_.size()) {
+    if (options_.mode == OutputMode::Threaded) worker_ = std::thread([this] { run(); });
+}
+
+PortWriter::~PortWriter() { stop(); }
+
+bool PortWriter::push(const std::uint8_t* data, std::size_t n) {
+    if (!port_ || !data || n == 0) return false;
+    if (options_.mode == OutputMode::Inline) {
+        // The caller's thread is the one that waits, which is the point of the mode: "the push returned" is "it is on
+        // the wire".
+        write(data, n);
+        return true;
+    }
+    const std::size_t need = kHeader + n;
+    if (need > ring_.size()) {  // bigger than any ring there is: it could never fit, so it is dropped whole
+        dropped_.fetch_add(1, std::memory_order_relaxed);
+        return false;
+    }
+    const std::uint64_t w = write_.load(std::memory_order_relaxed);
+    const std::uint64_t r = read_.load(std::memory_order_acquire);
+    if (need > ring_.size() - (w - r)) {  // full: drop the NEWEST whole message, never part of one
+        dropped_.fetch_add(1, std::memory_order_relaxed);
+        return false;
+    }
+    const auto length = static_cast<std::uint32_t>(n);
+    copyIn(reinterpret_cast<const std::uint8_t*>(&length), kHeader, w);
+    copyIn(data, n, w + kHeader);
+    write_.store(w + need, std::memory_order_release);
+    // The wake is the store-buffering (Dekker) shape MidiStream::retire describes: the producer bumps the counter and
+    // then reads `sleeping_`, while the writer writes `sleeping_` and then re-reads the counter. Under acquire/release
+    // both store-load pairs may be reordered, and the writer then sleeps with a record queued - a Note Off delayed until
+    // the next message - which x86 hides and AArch64 (the handheld and Pi builds) does not. The four operations on the
+    // two locations are therefore seq_cst, which is free on x86. `exchange` rather than `load` is what bounds the
+    // syscalls: only the push that finds the writer still asleep notifies it, so a block of messages costs at most one
+    // futex wake, where a load that sees a writer awake-but-not-yet-running would notify once per message.
+    wake_.fetch_add(1, std::memory_order_seq_cst);
+    if (sleeping_.exchange(false, std::memory_order_seq_cst)) wake_.notify_one();
+    return true;
+}
+
+void PortWriter::stop() {
+    if (!port_) return;  // already stopped
+    stopping_.store(true, std::memory_order_release);
+    if (worker_.joinable()) {
+        wake_.fetch_add(1, std::memory_order_seq_cst);
+        wake_.notify_all();
+        worker_.join();  // the writer flushes for up to kFlushDeadline (plus the send already in flight) and returns
+    }
+    port_.reset();  // closed now, and no push can reach it: the stream unpublishes and waits sends out before stopping
+}
+
+void PortWriter::run() {
+    bool                                  flushing = false;
+    std::chrono::steady_clock::time_point deadline{};
+    for (;;) {
+        if (stopping_.load(std::memory_order_acquire) && !flushing) {
+            flushing = true;
+            deadline = std::chrono::steady_clock::now() + kFlushDeadline;
+        }
+        if (queued()) {
+            if (flushing && std::chrono::steady_clock::now() >= deadline) return;  // the rest is discarded
+            sendOne();
+            continue;
+        }
+        if (flushing) return;  // the flush has nothing left to send
+        // Nothing to do: publish that this thread is going to sleep, then look once more, so a push that landed
+        // between the check above and the store below is not missed, and wait for the next one.
+        const std::uint32_t seen = wake_.load(std::memory_order_acquire);
+        sleeping_.store(true, std::memory_order_seq_cst);
+        if (wake_.load(std::memory_order_seq_cst) == seen && !stopping_.load(std::memory_order_acquire))
+            wake_.wait(seen, std::memory_order_seq_cst);
+        sleeping_.store(false, std::memory_order_seq_cst);
+    }
+}
+
+bool PortWriter::queued() const { return read_.load(std::memory_order_acquire) != write_.load(std::memory_order_acquire); }
+
+void PortWriter::sendOne() {
+    const std::uint64_t r      = read_.load(std::memory_order_relaxed);
+    std::uint32_t       length = 0;
+    copyOut(reinterpret_cast<std::uint8_t*>(&length), kHeader, r);
+    copyOut(scratch_.data(), length, r + kHeader);
+    read_.store(r + kHeader + length, std::memory_order_release);
+    write(scratch_.data(), length);
+}
+
+void PortWriter::copyIn(const std::uint8_t* from, std::size_t n, std::uint64_t at) {
+    const std::size_t size = ring_.size();
+    const std::size_t pos  = static_cast<std::size_t>(at % size);
+    const std::size_t head = n < size - pos ? n : size - pos;
+    std::memcpy(ring_.data() + pos, from, head);
+    if (head < n) std::memcpy(ring_.data(), from + head, n - head);
+}
+
+void PortWriter::copyOut(std::uint8_t* to, std::size_t n, std::uint64_t at) const {
+    const std::size_t size = ring_.size();
+    const std::size_t pos  = static_cast<std::size_t>(at % size);
+    const std::size_t head = n < size - pos ? n : size - pos;
+    std::memcpy(to, ring_.data() + pos, head);
+    if (head < n) std::memcpy(to + head, ring_.data(), n - head);
+}
+
+void PortWriter::write(const std::uint8_t* data, std::size_t n) {
+    if (options_.log) {
+        // The port is in the line: a Threaded stream has one writer per output, and two ports sending the same bytes is
+        // not something a dump should hide (an Inline stream keeps its one line, which covers every destination).
+        std::string line = "MIDI out (" + name_ + "): ";
+        char        hex[4];
+        for (std::size_t i = 0; i < n; ++i) {
+            std::snprintf(hex, sizeof(hex), "%02X ", data[i]);
+            line += hex;
+        }
+        log_(line);
+    }
+    port_->send(data, n);
+}
+
+}  // namespace rp::midi
diff --git a/packages/midi/native/src/midi/PortWriter.hpp b/packages/midi/native/src/midi/PortWriter.hpp
new file mode 100644
index 000000000..ce8ef4c77
--- /dev/null
+++ b/packages/midi/native/src/midi/PortWriter.hpp
@@ -0,0 +1,130 @@
+#pragma once
+
+// One hardware MIDI output and the thread that writes to it (docs/known-bugs-batch-3-design.md section 6). A port's
+// send may BLOCK until the bytes are on the wire: WinMM's SysEx path allocates and sleeps while midiOutUnprepareHeader
+// reports MIDIERR_STILLPLAYING, so a 538-byte Launchpad repaint is about 170 ms at DIN rate; ALSA's grows its encode
+// buffer, and CoreMIDI makes an OS call. The SDL standalone sends from its audio callback (one `send` per queued
+// message, after the block is rendered), so the write has to leave the callback: the callback copies the message into a
+// preallocated ring and returns, and a writer thread per hardware output does the blocking.
+//
+// THREADING. The ring is SPSC: one producer (the audio thread, which is send()'s only caller) and one consumer (this
+// object's thread). A push is ALL OR NOTHING - a record is its length header and its bytes, and a record that does not
+// fit is dropped whole and counted - so a SysEx is never torn and never interleaved with the message after it. Every
+// byte both sides touch is inside the preallocated ring and a scratch buffer of the same size, so neither the push nor
+// the writer allocates.
+//
+// The wake is a C++20 atomic wait (futex on Linux, WakeByAddressSingle on Windows, __ulock_wait on macOS, which libc++
+// provides from macOS 11 and this repository pins no older deployment target: the release build's runner is
+// macos-15). The producer bumps a counter and, ONLY when the writer has said it is sleeping, notifies it, exchanging
+// `sleeping_` for false as it does (a notify per message would be a syscall per message for a writer that is usually
+// awake anyway, and the exchange is what makes a block's worth of messages cost one wake). The four operations on the
+// counter and `sleeping_` are seq_cst for the reason MidiStream::retire gives for its own two: this is the
+// store-buffering (Dekker) shape, the writer writes `sleeping_` and then re-reads the counter while the producer bumps
+// the counter and then reads `sleeping_`, and under acquire/release both store-load pairs may be reordered - the writer
+// would then sleep with a record queued, which x86 hides and AArch64 (the handheld and Pi builds) does not. In the one
+// total order either the exchange sees the writer's `true` and notifies, or the writer's re-read sees the bumped
+// counter. stop() bumps the counter seq_cst and notifies all as well, so a writer waiting with an empty ring is still
+// joined.
+//
+// STOPPING. A port that leaves the selection, or the stream closing, stops its writer with a FLUSH: what a push has
+// queued is sent before the port goes, because a dropped Note Off is a stuck note. The flush is bounded by
+// kFlushDeadline, after which the rest is discarded; the send already inside the port cannot be interrupted (WinMM
+// gives no way to cancel a write in progress), so a close can take that long on top. Nothing may push after stop()
+// begins: the stream unpublishes the set and waits the send in flight out before it stops a writer, which is the order
+// its epoch handshake already keeps for the ports' lifetime.
+//
+// An Inline writer is the same object with no thread: push() writes to the port on the caller's thread, which is what
+// the CLI's facet and the test host want (their caller is the JS thread, and a script that returns from `midiSend` and
+// then exits relies on the bytes having gone out). One class, so the ownership, the dropping and the close order are
+// the same in both modes.
+
+#include <atomic>
+#include <chrono>
+#include <cstddef>
+#include <cstdint>
+#include <functional>
+#include <memory>
+#include <string>
+#include <thread>
+#include <vector>
+
+#include "midi/MidiBackend.hpp"
+
+namespace rp::midi {
+
+/** How a stream hands a message to its hardware outputs: on the caller's thread (Inline) or on a writer thread of its
+ *  own per output (Threaded). The virtual output is inline in both: a DAW or an aconnect route gains no latency. */
+enum class OutputMode { Inline, Threaded };
+
+class PortWriter {
+public:
+    /** The ring's size. A DIN port drains at about 3125 bytes a second, so this is over twenty seconds of the busiest
+     *  traffic a control surface makes; a bigger ring would only add latency to a flush. */
+    static constexpr std::size_t kDefaultRingBytes = 64 * 1024;
+    /** How long stop() keeps sending what is queued. A Note Off left in the ring is a stuck note; a port that is
+     *  wedged must not hold the control thread's close for longer than this. */
+    static constexpr std::chrono::milliseconds kFlushDeadline{500};
+
+    /** The line sink: the stream's own `emit`, so a writer's log line carries the same prefix and reaches the same
+     *  sink an embedder installed. Called from the writer's thread. */
+    using LogSink = std::function<void(const std::string&)>;
+
+    struct Options {
+        OutputMode  mode      = OutputMode::Threaded;
+        bool        log       = false;  // RETROPLUG_MIDI_LOG: dump each message as it is written
+        std::size_t ringBytes = kDefaultRingBytes;
+    };
+
+    /** Take ownership of an OPEN port and, in Threaded mode, start the writer. `name` is the port's, for the log. */
+    PortWriter(std::unique_ptr<MidiPort> port, std::string name, Options options, LogSink log);
+    ~PortWriter();
+    PortWriter(const PortWriter&)            = delete;
+    PortWriter& operator=(const PortWriter&) = delete;
+
+    /** Queue one message. Threaded: all or nothing into the ring, no lock and no allocation. Inline: written to the
+     *  port on this thread. False when the message did not go (it was dropped whole because the ring was full, or the
+     *  port had already stopped), so a caller may count it; `dropped()` carries the ring's own count. */
+    bool push(const std::uint8_t* data, std::size_t n);
+
+    /** Send what is queued (up to kFlushDeadline), join the writer and close the port. No push may come afterwards.
+     *  Idempotent, and safe on a writer whose port never opened. */
+    void stop();
+
+    /** Whole messages dropped because the ring was full, since this writer opened. Read on the control thread, as
+     *  MidiStream::droppedCount() is. */
+    std::uint64_t      dropped() const { return dropped_.load(std::memory_order_relaxed); }
+    const std::string& name() const { return name_; }
+
+private:
+    static constexpr std::size_t kHeader = sizeof(std::uint32_t);  // a record is its length, then its bytes
+
+    void run();                                                  // the writer thread, until stop() and an empty ring
+    void sendOne();                                              // one queued record: write it and free its space
+    bool queued() const;
+    void copyIn(const std::uint8_t* from, std::size_t n, std::uint64_t at);
+    void copyOut(std::uint8_t* to, std::size_t n, std::uint64_t at) const;
+    void write(const std::uint8_t* data, std::size_t n);  // one port send, logged when the dump is on
+
+    std::unique_ptr<MidiPort> port_;  // closed by stop()
+    std::string               name_;
+    Options                   options_;
+    LogSink                   log_;
+
+    // The ring, read by the writer and written by the producer. `write_` and `read_` count BYTES since the writer
+    // opened (never wrapped), and a byte's place in the buffer is its index modulo the size: a record may straddle the
+    // end, which copyIn/copyOut handle in two pieces.
+    std::vector<std::uint8_t>  ring_;
+    std::vector<std::uint8_t>  scratch_;  // the writer's contiguous copy of one record, so a send never sees two pieces
+    std::atomic<std::uint64_t> write_{0};
+    std::atomic<std::uint64_t> read_{0};
+    std::atomic<std::uint64_t> dropped_{0};
+
+    // The wake (see the header comment).
+    std::atomic<std::uint32_t> wake_{0};
+    std::atomic<bool>          sleeping_{false};
+    std::atomic<bool>          stopping_{false};
+
+    std::thread worker_;  // Inline: never started
+};
+
+}  // namespace rp::midi
````

This block is the writer and the CMake hunk that puts `PortWriter.cpp` into `retroplug-midi`, and nothing else: the
stream is untouched, so its own cases still pass and the next step's green run covers both.

- [ ] **Step 4: build and run the writer's cases**

```
timeout -k 10 600 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-midi-test -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-midi-test
```

Expected: PASS. The stream's own 49 cases and the writer's 7, with the class's own counts (291 assertions):

```
===============================================================================
All tests passed (291 assertions in 56 test cases)
```

- [ ] **Step 5: write the failing threaded-stream tests**

`MidiStream.test.cpp` gains four threaded cases and one allocation case - a threaded `send` returns while the port is
still writing and every byte arrives in order; the stream's dump is written by the writer, naming the port; a swap
with a backlog flushes before the close and sends nothing after it; a full ring's drops are counted on the control
thread; and a `Threaded` `send` allocates nothing on the audio callback (the design asks for `send`, not `push`) - and
the five existing concurrency cases gain a `GENERATE` over both modes: each reads `sentTo`/`sendsFinished` only after
`settled(...)`, and the two whose contract really differs say so per mode (a close flushes a `Threaded` ring where an
`Inline` close abandons it, and a swap behind back-to-back sends is bounded by one send there and by the writer's flush
deadline here). The two cases that time the fake's own slow write keep that delay Inline-only: under `Threaded`
`sendDelayMs` is 0, because what those cases say there is the push against retire, flush and close (not a slow port),
and a millisecond per send is the platform's sleep granularity off Linux - Windows' is about 15.6 ms - so a backlog
paced that way cannot be drained inside any deadline. The swap case also paces its Threaded audio thread about 200 us
per push, so the swaps really land under the sender instead of racing its start, and its assertion is `swaps > 2` in
both modes with no `records()` count that a thread-start race could make or break.

````patch
diff --git a/packages/midi/native/test/MidiStream.test.cpp b/packages/midi/native/test/MidiStream.test.cpp
index 045e82f..24b7996 100644
--- a/packages/midi/native/test/MidiStream.test.cpp
+++ b/packages/midi/native/test/MidiStream.test.cpp
@@ -9,18 +9,21 @@
 // been waited out.
 
 #include <catch2/catch_test_macros.hpp>
+#include <catch2/generators/catch_generators.hpp>
 
 #include <atomic>
 #include <algorithm>
 #include <chrono>
 #include <cstdint>
 #include <cstdlib>
+#include <mutex>
 #include <set>
 #include <string>
 #include <thread>
 #include <utility>
 #include <vector>
 
+#include "AllocationCounter.hpp"  // the global operator new PortWriter.test.cpp defines, and its per-thread count
 #include "midi/FakeMidiBackend.hpp"
 #include "midi/MidiStream.hpp"
 
@@ -109,6 +112,18 @@ std::vector<std::uint8_t> bytes(int a, int b, int c = -1) {
     return out;
 }
 
+/** Wait until `count` sends have FINISHED: Inline has done them by the time the send returns, and Threaded's writers
+ *  do them after it. Waiting for "as many started as finished" is not enough - that reads 0 == 0 before the writer has
+ *  begun - so the count is the caller's, which is what the case knows it sent. */
+bool settled(const FakeMidiBackend& backend, const int count) {
+    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
+    while (std::chrono::steady_clock::now() < deadline) {
+        if (backend.sendsFinished.load() == count) return true;
+        std::this_thread::sleep_for(std::chrono::milliseconds(1));
+    }
+    return backend.sendsFinished.load() == count;
+}
+
 }  // namespace
 
 // ---- what a selection opens ------------------------------------------------------------------------------------
@@ -280,10 +295,14 @@ TEST_CASE("an exclusive port: a re-pick must let the port go before it is opened
     // A WinMM port's model, over the fake: a second open of a name this stream still holds is
     // refused. A re-pick that opened before it closed therefore lost the device it meant to heal - the output went
     // silent for good, and the event log says so (a refused open, then a close, then nothing).
+    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
     Machine          machine;
     FakeMidiBackend& backend = machine.backend;
     backend.exclusivePorts   = true;
-    MidiStream stream(backend);
+    // The virtual output is inline in both modes and the fake counts its sends too: off, so a send is one hardware
+    // write, which is what this case waits for.
+    backend.virtualPorts = false;
+    MidiStream stream(backend, mode);
     stream.setInputs({"Keys in"}, false);
     stream.setOutputs({"Synth out"});
     REQUIRE(stream.open());
@@ -296,11 +315,13 @@ TEST_CASE("an exclusive port: a re-pick must let the port go before it is opened
     CHECK(backend.listening.count("Keys in") == 1);
     const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
     stream.send(note, 3);
+    REQUIRE(settled(backend, 1));  // Threaded: the port is read once the writer has written what the push queued
     CHECK(backend.sentTo("Synth out").size() == 1);
     // A diff keeps the port and never asks for it again, so it is not refused either.
     stream.setOutputs({"Synth out", "Interface out"});
     CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth out", "Interface out"});
-    stream.send(note, 3);
+    stream.send(note, 3);  // this one fans out to both ports
+    REQUIRE(settled(backend, 3));
     CHECK(backend.sentTo("Interface out").size() == 1);
 }
 
@@ -344,10 +365,18 @@ TEST_CASE("a close under a send in flight frees no port the send is using, nor t
     // A device loss is one reason a product closes the stream while audio still renders; the close publishes the end of
     // the set and waits the send out, so the ports (and the virtual output, whose sends the same bracket covers) are
     // never freed under a caller still inside one.
+    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
     Machine          machine;
     FakeMidiBackend& backend = machine.backend;
-    backend.sendDelayMs      = 1;
-    MidiStream stream(backend);
+    // INLINE: a slow port, so a send is still inside the fake when the close lands, and the virtual output on, because
+    // this case is about the epoch bracket covering its send too. THREADED: no delay, because the pushes only have to
+    // reach the ring (the subject is push against retire and flush, not a slow port) and a millisecond per send is the
+    // platform's sleep granularity off Linux (Windows' is about 15.6 ms), not a millisecond; and the virtual output off,
+    // because its send stays inline there and the fake's delay would pace this caller, leaving the ring holding only a
+    // handful of messages - what this mode has to say is the hardware port's flush.
+    backend.sendDelayMs  = mode == rp::midi::OutputMode::Inline ? 1 : 0;
+    backend.virtualPorts = mode == rp::midi::OutputMode::Inline;
+    MidiStream stream(backend, mode);
     stream.setOutputs({"Synth out"});
     REQUIRE(stream.open());
     constexpr int     kSends = 200;
@@ -363,8 +392,14 @@ TEST_CASE("a close under a send in flight frees no port the send is using, nor t
     stream.close();
     audio.join();
     CHECK(backend.sendsAfterClose() == 0);
-    // Whatever had not been sent by the close went nowhere at all: no port was left published to receive it.
-    CHECK(backend.sendsStarted.load() < kSends);
+    // Inline: whatever had not been sent by the close went nowhere at all, because no port was left published to receive
+    // it. Threaded: the close FLUSHES the ring before the port goes instead (a dropped Note Off is a stuck note), so
+    // every push that had returned when it landed reaches the port.
+    if (mode == rp::midi::OutputMode::Inline) {
+        CHECK(backend.sendsStarted.load() < kSends);
+    } else {
+        CHECK(backend.sendsStarted.load() == kSends);
+    }
 }
 
 // ---- the reservation and the scan's held edge ------------------------------------------------------------------
@@ -569,16 +604,28 @@ TEST_CASE("an output swap under a send in flight: every send lands on one live p
     // than only a crash - and this case runs in build-tsan/ as well.
     Machine          machine;
     FakeMidiBackend& backend = machine.backend;
-    backend.sendDelayMs   = 1;      // a write that takes a moment, as a WinMM bulk SysEx does
-    backend.virtualPorts  = false;  // the swap is the hardware set's: no virtual output to count as well
-    MidiStream stream(backend);
+    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
+    // Inline: a write that takes a moment, as a WinMM bulk SysEx does, so the sender is still inside one under most of
+    // the swaps. Threaded: no delay, because the pushes only have to reach the ring (the subject is push against retire
+    // and flush, not a slow port) and a millisecond per send is the platform's sleep granularity off Linux (Windows' is
+    // about 15.6 ms), not a millisecond there.
+    backend.sendDelayMs  = mode == rp::midi::OutputMode::Inline ? 1 : 0;
+    backend.virtualPorts = false;  // the swap is the hardware set's: no virtual output to count as well
+    MidiStream stream(backend, mode);
     stream.setOutputs({"Synth out"});  // an output is selected before the audio thread starts, as it is in a run
     REQUIRE(stream.open());
     constexpr int kSends = 300;
     std::atomic<bool> done{false};
     std::thread audio([&] {
         const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
-        for (int i = 0; i < kSends; ++i) stream.send(note, 3);
+        for (int i = 0; i < kSends; ++i) {
+            stream.send(note, 3);
+            // Threaded: the pushes are otherwise microseconds and would all be over before the control thread's second
+            // swap, so the count below would rest on a thread-start race. Pacing the sender makes the swaps land under
+            // it, which is what the case is about; inline, the fake's own write already paces it.
+            if (mode == rp::midi::OutputMode::Threaded)
+                std::this_thread::sleep_for(std::chrono::microseconds(200));
+        }
         done.store(true);
     });
     int swaps = 0;
@@ -588,10 +635,13 @@ TEST_CASE("an output swap under a send in flight: every send lands on one live p
         ++swaps;
     }
     audio.join();
+    // Threaded: every push returned before its writer wrote, so the counts are read once the last writer has drained.
+    REQUIRE(settled(backend, kSends));
+    // Both modes: many swaps landed while the sender was sending (inline it is inside a fake write, threaded the pushes
+    // are paced), and every send still reached exactly one live port - no send was lost and none reached a freed one.
     CHECK(swaps > 2);
     CHECK(backend.sendsStarted.load() == kSends);   // every send reached a port: none went to no output at all
     CHECK(backend.sendsFinished.load() == kSends);
-    CHECK(backend.records() >= 2);                  // the swap really opened new ports under the sender
     CHECK(backend.sendsAfterClose() == 0);
     CHECK(backend.totalReceived() == static_cast<std::size_t>(kSends));
 }
@@ -606,7 +656,8 @@ TEST_CASE("a diff closes what it drops before it opens what it adds: a renamed d
     backend.outputs          = {"Synth 1", "Synth 2"};
     backend.exclusivePorts   = true;
     backend.deviceOf         = {{"Synth 1", "synth"}, {"Synth 2", "synth"}};  // one device, two enumerations
-    MidiStream stream(backend);
+    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
+    MidiStream stream(backend, mode);
     stream.setOutputs({"Synth 1"});
     REQUIRE(stream.open());
     backend.events.clear();
@@ -620,18 +671,20 @@ TEST_CASE("a diff closes what it drops before it opens what it adds: a renamed d
     CHECK(stream.openedOutputs() == std::vector<std::string>{"Synth 2"});
 }
 
-TEST_CASE("a swap behind back-to-back long sends waits out one send, never the run of them") {
-    // A WinMM bulk SysEx blocks about 170 ms inside send(), and a block may send several. The wait must be bounded by
-    // the ONE send in flight - the set the swap published is what the next send reads - so a reconfigure waits one
-    // send, not the stream of them. Waiting "while the epoch is odd" instead chases every send that follows: this case
-    // measured hundreds of milliseconds on a 32-thread box and never returned under `taskset -c 0` (the swap spins out
-    // the whole run). The bound is a quarter of the run, loose enough for a loaded runner and tight enough to fail that
-    // wait.
+TEST_CASE("a swap behind back-to-back long sends waits a bounded time: one send inline, the flush's deadline threaded") {
+    // A WinMM bulk SysEx blocks about 170 ms inside send(), and a block may send several. INLINE: the wait must be
+    // bounded by the ONE send in flight - the set the swap published is what the next send reads - so a reconfigure
+    // waits one send, not the stream of them. Waiting "while the epoch is odd" instead chases every send that follows:
+    // this case measured hundreds of milliseconds on a 32-thread box and never returned under `taskset -c 0` (the swap
+    // spins out the whole run). The bound is a quarter of the run, loose enough for a loaded runner and tight enough to
+    // fail that wait. THREADED: the wait is the writer's stop, which flushes what the ring holds up to kFlushDeadline and
+    // discards the rest - bounded the same way, by a constant rather than by the backlog's length.
+    const rp::midi::OutputMode mode = GENERATE(rp::midi::OutputMode::Inline, rp::midi::OutputMode::Threaded);
     Machine          machine;
     FakeMidiBackend& backend = machine.backend;
     backend.sendDelayMs      = 20;
     backend.virtualPorts     = false;  // the swap is the hardware set's
-    MidiStream stream(backend);
+    MidiStream stream(backend, mode);
     stream.setOutputs({"Synth out"});
     REQUIRE(stream.open());
     constexpr int kSends = 40;  // 800 ms of unbroken sends
@@ -645,12 +698,18 @@ TEST_CASE("a swap behind back-to-back long sends waits out one send, never the r
     const auto waited =
         std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
     audio.join();
-    // One in-flight send plus slack: a quarter of the run (ten sends' time), because a ThreadSanitizer run inside the
-    // parallel kit, or a loaded macOS runner, stretches a 20 ms send and the scheduling around it (a CI run measured 92
-    // ms against the four sends this was). Chasing the run would wait out most of its 800 ms.
-    CHECK(waited < kSends * backend.sendDelayMs / 4);
+    // Inline: one in-flight send plus slack, a quarter of the run (ten sends' time), because a ThreadSanitizer run inside
+    // the parallel kit, or a loaded macOS runner, stretches a 20 ms send and the scheduling around it (a CI run measured
+    // 92 ms against the four sends this was). Chasing the run would wait out most of its 800 ms. Threaded: the writer's
+    // flush deadline plus the send it was already inside, the same invariant with a constant bound.
+    if (mode == rp::midi::OutputMode::Inline) {
+        CHECK(waited < kSends * backend.sendDelayMs / 4);
+        CHECK(backend.totalReceived() == static_cast<std::size_t>(kSends));
+    } else {
+        CHECK(waited < rp::midi::PortWriter::kFlushDeadline.count() + 500);
+        CHECK(backend.totalReceived() >= 1);  // the flush sent what the deadline allowed, and discarded the rest
+    }
     CHECK(backend.sendsAfterClose() == 0);
-    CHECK(backend.totalReceived() == static_cast<std::size_t>(kSends));
 }
 
 TEST_CASE("every message a delivery thread queued is delivered before its port's destructor returns") {
@@ -802,3 +861,158 @@ TEST_CASE("the allow list hides a port from the stream's list and from its opens
     CHECK(backend.listening.count("Zoom L6:1 20:0") == 0);
     CHECK(backend.totalReceived() == 0);  // the filtered output never even opened
 }
+
+// ---- the stream in Threaded mode ------------------------------------------------------------------------------
+//
+// What the audio callback sees when the stream writes from a thread of its own: a send that returns before the port has
+// written, the per-message dump written by the writer rather than the callback, a swap that FLUSHES a backlog before the
+// port goes, and drops counted per port. The cases that pin a contract the two modes share run in BOTH (a GENERATE at
+// the top of each, reading what a port received only once its writer has drained), so the concurrency cases the design
+// asks to hold in both modes - the exclusive port's re-pick, a close under a send in flight, an output swap under a
+// send, the diff that closes before it opens, and a swap behind back-to-back sends - are what the ThreadSanitizer leg
+// runs in Threaded mode too.
+
+TEST_CASE("a threaded send returns while the port is still writing, and every byte arrives in order") {
+    // The SDL standalone's audio callback: it drains the Engine's MIDI out once per block, and a WinMM SysEx write waits
+    // for the bytes to leave the wire (about 170 ms for a 538-byte repaint at DIN rate). The callback queues and moves
+    // on; the writer thread does the waiting.
+    Machine          machine;
+    FakeMidiBackend& backend = machine.backend;
+    backend.sendDelayMs      = 200;
+    // The virtual output is INLINE in both modes, so a delay on it is the caller's: it is not what this case is about.
+    backend.virtualPorts = false;
+    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
+    stream.setOutputs({"Synth out"});
+    REQUIRE(stream.open());
+    const std::vector<std::uint8_t> sysex(538, 0xF0);
+    const std::uint8_t              note[3] = {0x90, 0x3C, 0x7F};
+    const auto                      t0      = std::chrono::steady_clock::now();
+    stream.send(sysex.data(), sysex.size());
+    for (int i = 0; i < 3; ++i) stream.send(note, 3);
+    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(150));  // it waited for no write at all
+    // And the port receives them whole, in the order the block sent them.
+    REQUIRE(settled(backend, 4));
+    const std::vector<std::vector<std::uint8_t>> sent = backend.sentTo("Synth out");
+    REQUIRE(sent.size() == 4);
+    CHECK(sent[0] == sysex);
+    CHECK(sent[1] == std::vector<std::uint8_t>(note, note + 3));
+    CHECK(stream.droppedOutputCount() == 0);
+    stream.close();
+}
+
+TEST_CASE("a threaded stream's dump is written by the writer, not the audio callback") {
+    // RETROPLUG_MIDI_LOG's per-message line moved with the send: building it is an allocation, and send() runs on the
+    // audio thread. The line still carries the prefix and the bytes a harness greps.
+    Machine          machine;
+    FakeMidiBackend& backend = machine.backend;
+    backend.sendDelayMs      = 0;
+    backend.virtualPorts     = false;
+    // The sink runs on the WRITER's thread (that is the point: send() is the audio callback), so the case's own
+    // collection is guarded: ThreadSanitizer runs this file too.
+    std::mutex               linesMutex;
+    std::vector<std::string> lines;
+    auto                     linesCopy = [&] {
+        std::lock_guard<std::mutex> lk(linesMutex);
+        return lines;
+    };
+    const char*              was = std::getenv("RETROPLUG_MIDI_LOG");
+    std::string              kept;
+    const bool               had = was != nullptr;
+    if (had) kept = was;
+#if defined(_WIN32)
+    _putenv_s("RETROPLUG_MIDI_LOG", "1");
+#else
+    setenv("RETROPLUG_MIDI_LOG", "1", 1);
+#endif
+    {
+        MidiStream stream(backend, rp::midi::OutputMode::Threaded);
+        stream.setLogSink([&](const std::string& line) {
+            if (line.find("MIDI out (Synth out): 90") == std::string::npos) return;
+            std::lock_guard<std::mutex> lk(linesMutex);
+            lines.push_back(line);
+        });
+        stream.setOutputs({"Synth out"});
+        REQUIRE(stream.open());
+        const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
+        stream.send(note, 3);
+        REQUIRE(settled(backend, 1));
+        // The writer's line, which names the port: a Threaded stream has one writer per output, and two ports sending
+        // the same bytes is not something a dump should hide. The prefix and the bytes are what a harness greps.
+        const std::vector<std::string> got = linesCopy();
+        REQUIRE(got.size() == 1);
+        CHECK(got[0] == "[retroplug-sdl] MIDI out (Synth out): 90 3C 7F ");
+        stream.close();
+    }
+#if defined(_WIN32)
+    _putenv_s("RETROPLUG_MIDI_LOG", had ? kept.c_str() : "");
+#else
+    if (had) setenv("RETROPLUG_MIDI_LOG", kept.c_str(), 1);
+    else unsetenv("RETROPLUG_MIDI_LOG");
+#endif
+}
+
+TEST_CASE("a threaded stream's swap with a backlog flushes it before the port closes, and sends nothing after") {
+    // A port leaving the selection (a project load, a pick, the stream closing) stops its writer with a flush: a dropped
+    // Note Off is a stuck note. Nothing reaches the port after it closes, which is what the stream's epoch handshake and
+    // the writer's stop are ordered to guarantee.
+    Machine          machine;
+    FakeMidiBackend& backend = machine.backend;
+    backend.sendDelayMs      = 20;
+    backend.virtualPorts     = false;  // the hardware set's flush is the subject; the virtual send stays inline
+    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
+    stream.setOutputs({"Synth out"});
+    REQUIRE(stream.open());
+    const std::uint8_t note[3]  = {0x90, 0x3C, 0x7F};
+    constexpr int      kQueued  = 8;
+    for (int i = 0; i < kQueued; ++i) stream.send(note, 3);
+    stream.setOutputs({});  // the port leaves: published away, waited out, flushed, joined, closed
+    CHECK(backend.sentTo("Synth out").size() == kQueued);
+    CHECK(backend.sendsAfterClose() == 0);
+    // The stream is still usable, and no hardware port is left to receive: the next message goes nowhere on the wire.
+    CHECK(stream.openedOutputs().empty());
+    stream.send(note, 3);
+    CHECK(settled(backend, kQueued));  // nothing more is written: the count stands
+    CHECK(backend.sentTo("Synth out").size() == kQueued);
+    stream.close();
+}
+
+TEST_CASE("a Threaded stream's send allocates nothing on the audio callback") {
+    // The design's case is send()'s, not push()'s: the audio callback calls MidiStream::send, so that is where an
+    // allocation would matter (PortWriter.test.cpp counts push's own, and the count is the same global operator new,
+    // armed for this thread alone, so the writer's own allocations are not measured).
+    Machine          machine;
+    FakeMidiBackend& backend = machine.backend;
+    backend.sendDelayMs      = 50;  // the ring is being drained while the block's messages land
+    backend.virtualPorts     = false;
+    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
+    stream.setOutputs({"Synth out"});
+    REQUIRE(stream.open());
+    const std::vector<std::uint8_t> sysex(538, 0xF0);
+    const std::uint8_t              note[3] = {0x90, 0x3C, 0x7F};
+    {
+        rp::midi::test::AllocationCount count;
+        for (int i = 0; i < 64; ++i) {
+            stream.send(sysex.data(), sysex.size());
+            stream.send(note, 3);
+        }
+        const bool quiet = count.value() == 0;
+        CHECK(quiet);
+    }
+    stream.close();
+}
+
+TEST_CASE("a threaded stream counts the messages a full ring drops") {
+    // The count is per port and read on the control thread, as droppedCount() is for the receive ring: a port that is
+    // not draining must not make a lost message invisible.
+    Machine          machine;
+    FakeMidiBackend& backend = machine.backend;
+    backend.sendDelayMs      = 300;
+    backend.virtualPorts     = false;
+    MidiStream stream(backend, rp::midi::OutputMode::Threaded);
+    stream.setOutputs({"Synth out"});
+    REQUIRE(stream.open());
+    const std::vector<std::uint8_t> repaint(1024, 0xF0);  // a big message, so a few fills the ring
+    for (int i = 0; i < 400; ++i) stream.send(repaint.data(), repaint.size());
+    CHECK(stream.droppedOutputCount() > 0);
+    stream.close();
+}
````

- [ ] **Step 6: run them, and see them fail**

```
timeout -k 10 600 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-midi-test -j$(nproc)
```

Expected: FAIL to compile, because the stream has no mode and no drop count yet:

```
/workspaces/RetroPlug-main/cuttlefish/packages/midi/native/test/MidiStream.test.cpp:298:21: error: 'OutputMode' in namespace 'rp::midi' does not name a type
/workspaces/RetroPlug-main/cuttlefish/packages/midi/native/test/MidiStream.test.cpp:305:32: error: 'mode' was not declared in this scope; did you mean 'mode_t'?
/workspaces/RetroPlug-main/cuttlefish/packages/midi/native/test/MidiStream.test.cpp:377:46: error: 'rp::midi::OutputMode' has not been declared
...
```

- [ ] **Step 7: implement the stream's mode, and say in the standalone's doc where the dump is written**

The block is the stream's mode and, with it, `docs/sdl-standalone.md`'s sentence about `RETROPLUG_MIDI_LOG`: the
out line now names the port and is written by the writer, so the doc that said "dumps in + out bytes" says where.

````patch
diff --git a/packages/midi/native/src/midi/MidiStream.cpp b/packages/midi/native/src/midi/MidiStream.cpp
index 0bf9ae4bf..ab919a676 100644
--- a/packages/midi/native/src/midi/MidiStream.cpp
+++ b/packages/midi/native/src/midi/MidiStream.cpp
@@ -21,8 +21,8 @@ bool contains(const std::vector<std::string>& names, const std::string& name) {
 
 }  // namespace
 
-MidiStream::MidiStream(MidiBackend& backend)
-    : backend_(&backend), clientName_(backend.clientName()), filterName_(backend.familyName()) {}
+MidiStream::MidiStream(MidiBackend& backend, OutputMode mode)
+    : backend_(&backend), mode_(mode), clientName_(backend.clientName()), filterName_(backend.familyName()) {}
 
 MidiStream::~MidiStream() { close(); }
 
@@ -208,7 +208,16 @@ void MidiStream::applyOutputs(Apply mode) {
         bool opened = false;
         for (int attempt = 0; attempt < 2 && !opened; ++attempt) {
             try {
-                hwOut_.emplace_back(name, backend_->openOutput(name));
+                // The port opens OUTSIDE the writer: an exclusive port that refuses throws before a thread exists, so
+                // the retry below costs no thread start and no flush.
+                PortWriter::Options options;
+                options.mode = mode_;
+                // An Inline stream dumps in send() (one line per message, its destinations together); a Threaded one
+                // cannot build a string on the audio thread, so its dump is the writer's, one line per port.
+                options.log = mode_ == OutputMode::Threaded && log_.load();
+                hwOut_.emplace_back(name, std::make_unique<PortWriter>(
+                                              backend_->openOutput(name), name, options,
+                                              [this](const std::string& line) { emit(line); }));
                 next.push_back(name);
                 emit("MIDI out: connected hardware port '" + name + "'");
                 opened = true;
@@ -327,7 +336,10 @@ void MidiStream::poll(std::vector<Message>& out) {
 
 void MidiStream::send(const std::uint8_t* data, std::size_t n) {
     if (!data || n == 0) return;
-    if (log_.load()) {
+    // The per-message dump: an Inline stream writes it here, as it always has (its caller is the CLI's JS thread, and
+    // one line covers every destination). A THREADED stream's dump is each writer's, because building a string is an
+    // allocation and this is the audio callback.
+    if (log_.load() && mode_ == OutputMode::Inline) {
         std::string line = "MIDI out: ";
         char        hex[4];
         for (std::size_t i = 0; i < n; ++i) {
@@ -347,7 +359,8 @@ void MidiStream::send(const std::uint8_t* data, std::size_t n) {
     OutputSet* set = outputs_.load(std::memory_order_seq_cst);
     if (set) {
         if (set->virtualPort) set->virtualPort->send(data, n);
-        for (auto& port : set->ports) port->send(data, n);
+        // Threaded: a copy into each writer's ring and, at most, one wake. Inline: the port's own send, on this thread.
+        for (PortWriter* writer : set->ports) writer->push(data, n);
     }
     epoch_.fetch_add(1, std::memory_order_seq_cst);  // even: done, and everything above happens-before a retire
 }
diff --git a/packages/midi/native/src/midi/MidiStream.hpp b/packages/midi/native/src/midi/MidiStream.hpp
index 818cb332a..738d26db2 100644
--- a/packages/midi/native/src/midi/MidiStream.hpp
+++ b/packages/midi/native/src/midi/MidiStream.hpp
@@ -25,14 +25,19 @@
 // a selection change publishes a new set naming the same virtual port without reopening it, so a DAW or an aconnect
 // user connected to "RetroPlug Out" keeps that connection across a Settings > MIDI change. The hardware ports are the
 // CONTROL thread's (hwOut_), and a published set only names them: a port leaving the selection is unpublished, waited
-// out, and only then closed, which is also what lets a name be reopened at all where a port is exclusive (Windows).
+// out, and only then closed, which is also what lets a name be reopened at all where a port is exclusive (Windows). A
+// hardware port lives inside its PortWriter, which in Threaded mode writes from a thread of its own: the swap order
+// above is what makes that safe (the set stops naming the writer and the send in flight is waited out before the writer
+// is STOPPED, which flushes what it queued, joins the thread and closes the port).
 //
 // The filters judge a listing by the backend's FAMILY (MidiBackend::familyName), not by the name this stream publishes
 // under: a session's stream is "RetroPlug N8" and a new name for it would otherwise turn the standalone's virtual output
 // and a claimed Launchpad's link into devices it may open (the E11 "every input" default makes that reachable).
 //
 // Logging: every line the harnesses grep goes through one sink, by default the "[retroplug-sdl] " voice to stderr that
-// the app has always written (the CLI prints it too), so a test asserts the very bytes that reach the console.
+// the app has always written (the CLI prints it too), so a test asserts the very bytes that reach the console. An Inline
+// stream's per-message out dump is written by send() as it always was; a Threaded stream's is written by the writer that
+// actually sends it, because the audio thread must not build it.
 
 #include <array>
 #include <atomic>
@@ -46,6 +51,7 @@
 #include <vector>
 
 #include "midi/MidiBackend.hpp"
+#include "midi/PortWriter.hpp"
 
 namespace rp::midi {
 
@@ -67,7 +73,7 @@ public:
      *  to it, and poll() then never grows a vector on the audio thread. */
     static constexpr std::size_t kCapacity = 256;
 
-    explicit MidiStream(MidiBackend& backend);
+    explicit MidiStream(MidiBackend& backend, OutputMode mode = OutputMode::Inline);
     ~MidiStream();
     MidiStream(const MidiStream&)            = delete;
     MidiStream& operator=(const MidiStream&) = delete;
@@ -134,6 +140,14 @@ public:
      *  thread stopped draining, and any transport clock in what was lost is simply gone, which downstream reads as a
      *  tempo that is too slow. */
     std::uint64_t droppedCount() const { return dropped_.load(std::memory_order_relaxed); }
+    /** Whole messages a hardware output's writer dropped because its ring was full - always 0 in a healthy run.
+     *  Threaded streams only (an Inline one has no ring): a port that is not draining must not make a lost message
+     *  invisible. Read on the control thread, as droppedCount() is. */
+    std::uint64_t droppedOutputCount() const {
+        std::uint64_t n = 0;
+        for (const auto& entry : hwOut_) n += entry.second->dropped();
+        return n;
+    }
 
     /** Test-only: push a message into the input ring as if a port had received it, for a headless self-test that has no
      *  MIDI hardware. Not used in normal operation. */
@@ -142,11 +156,12 @@ public:
 private:
     /** What send() reaches, immutable once published: the hardware outputs this set NAMES, and the virtual output
      *  (owned by open(), carried here only so a send reads ONE pointer and close() can wait sends out before freeing
-     *  it). It owns no port: hwOut_ is the hardware ports' owner, so a port can be unpublished, waited out and closed
-     *  before the same name is opened again. One control-thread allocation per change, nothing on the audio path. */
+     *  it). It owns no port: hwOut_ is the hardware ports' (and their writers') owner, so one can be unpublished,
+     *  waited out and stopped before the same name is opened again. One control-thread allocation per change, nothing
+     *  on the audio path. */
     struct OutputSet {
-        std::vector<MidiPort*> ports;
-        MidiPort*              virtualPort = nullptr;
+        std::vector<PortWriter*> ports;
+        MidiPort*                virtualPort = nullptr;
     };
 
     void applyInputs(Apply mode);
@@ -158,6 +173,7 @@ private:
     void emit(const std::string& line);  // one log line, prefix included, through sink_ or stderr
 
     MidiBackend* backend_;  // non-const on purpose: a listing probes the system, and listInputs() is const
+    OutputMode   mode_ = OutputMode::Inline;  // where a hardware output's send happens (see OutputMode)
     std::string  clientName_ = "RetroPlug";  // copied from the backend at open(): the name this stream PUBLISHES under
     std::string  filterName_ = "RetroPlug";  // the backend's FAMILY, copied at open(): what the filters judge a port by
     LogSink      sink_;                      // empty: stderr
@@ -183,7 +199,10 @@ private:
     // The hardware ports, name and owner together, the CONTROL thread's alone: it opens them, and it closes one only
     // after the published set has stopped naming it and the send in flight has been waited out.
     std::vector<std::pair<std::string, std::unique_ptr<MidiPort>>> hwIn_;
-    std::vector<std::pair<std::string, std::unique_ptr<MidiPort>>> hwOut_;
+    // An output and its writer (which owns the port): the CONTROL thread's, as the inputs above are. In Threaded mode a
+    // writer is a thread, so one is stopped (flushed, joined, closed) only once the published set has stopped naming it
+    // and the send in flight has been waited out.
+    std::vector<std::pair<std::string, std::unique_ptr<PortWriter>>> hwOut_;
     std::atomic<OutputSet*>                                       outputs_{nullptr};  // published for send()
     std::atomic<std::uint32_t>              epoch_{0};          // odd while a send is in flight
 
diff --git a/packages/native/sdl/app.hpp b/packages/native/sdl/app.hpp
index efe2603ed..731252ea7 100644
--- a/packages/native/sdl/app.hpp
+++ b/packages/native/sdl/app.hpp
@@ -199,7 +199,9 @@ struct AppState {
     SDL_Texture*  texture = nullptr;
     rp::audio::AudioHost* audio = nullptr;     // the device, its reconfigure and its muted fallback (main owns it)
     retroplug::GamepadManager gamepad;
-    rp::midi::MidiStream midi{rp::midi::systemMidi()};  // in/out; drained into the Engine on the audio thread each block
+    rp::midi::MidiStream midi{rp::midi::systemMidi(), rp::midi::OutputMode::Threaded};  // in (drained on the audio
+    // thread each block) and out (each hardware port's send on that port's own writer: a WinMM SysEx blocks until the
+    // bytes are on the wire, and this is the audio callback) - midi/PortWriter.hpp
     std::vector<rp::midi::MidiStream::Message> midiScratch;  // reused per block; reserved to the ring size in main()
     std::atomic<std::uint64_t> midiStaged{0};             // count staged into the Engine (headless self-test)
     MidiClockSync clockSync;                              // MIDI-clock → BPM/transport (audio thread only)
diff --git a/docs/sdl-standalone.md b/docs/sdl-standalone.md
index 6507d42bc..f20e9335f 100644
--- a/docs/sdl-standalone.md
+++ b/docs/sdl-standalone.md
@@ -63,7 +63,9 @@ to the selected hardware output port, if any. It sends each message at once, its
 (RtMidi's out takes no timestamp), where the plugin hands the DAW each at its frame; scheduling it is the
 standalone's own work ([07-remaining-work.md](../spec/07-remaining-work.md)). Hardware-verified: LSDj
 Master-Sync clock (`0xF8`) reaches `aseqdump`.
-`RETROPLUG_MIDI_LOG` dumps in + out bytes.
+`RETROPLUG_MIDI_LOG` dumps in + out bytes. Its out line is written where the write happens: inline on
+the caller's thread for a facet whose caller is the JS thread, and by each hardware output's writer in the
+standalone's `Threaded` stream, whose line names the port (two ports sending the same bytes are two lines).
 
 ### MIDI device selection: ✅ DONE (per project, since F5b)
 `Settings > MIDI` edits a PROJECT's `midi` section, not a machine-wide setting: an inputs checklist plus "All
````

- [ ] **Step 7b: the threaded close case waits for its pushes, not a timer (review R3)**

"A close under a send in flight" closes 5 ms after its audio thread starts. Inline, that is the point: a send is still
inside the slow fake when the close lands. Threaded, the case counts every push the flush must deliver
(`sendsStarted == kSends`), and a push made after the close goes nowhere; with no fake delay nearly every push may wake
the writer, so on a loaded box or under the thread sanitizer 200 pushes can outlast 5 ms and the count comes up short.
The plan review traced one unexplained ThreadSanitizer-leg failure to this. In Threaded mode the case now waits for the
audio thread's last push before it closes, which keeps the equality and makes it deterministic; Inline is unchanged.

````patch
diff --git a/packages/midi/native/test/MidiStream.test.cpp b/packages/midi/native/test/MidiStream.test.cpp
index 24b7996d7..7bf16d5e0 100644
--- a/packages/midi/native/test/MidiStream.test.cpp
+++ b/packages/midi/native/test/MidiStream.test.cpp
@@ -381,20 +381,30 @@ TEST_CASE("a close under a send in flight frees no port the send is using, nor t
     REQUIRE(stream.open());
     constexpr int     kSends = 200;
     std::atomic<bool> started{false};
+    std::atomic<bool> pushed{false};
     std::thread       audio([&] {
         const std::uint8_t note[3] = {0x90, 0x3C, 0x7F};
         started.store(true);
         for (int i = 0; i < kSends; ++i) stream.send(note, 3);
+        pushed.store(true);
     });
     while (!started.load()) std::this_thread::yield();
-    // A few sends in, so one is in flight when the close lands.
-    std::this_thread::sleep_for(std::chrono::milliseconds(5));
+    if (mode == rp::midi::OutputMode::Inline) {
+        // A few sends in, so one is in flight when the close lands.
+        std::this_thread::sleep_for(std::chrono::milliseconds(5));
+    } else {
+        // Every push made before the close, so the count below does not rest on the pushes beating a timer: on a loaded
+        // box (or under the thread sanitizer) 200 pushes, each of which may wake the writer, can outlast any fixed
+        // sleep, and a push after the close goes nowhere. The writer may still be draining the ring when the close
+        // lands.
+        while (!pushed.load()) std::this_thread::yield();
+    }
     stream.close();
     audio.join();
     CHECK(backend.sendsAfterClose() == 0);
     // Inline: whatever had not been sent by the close went nowhere at all, because no port was left published to receive
     // it. Threaded: the close FLUSHES the ring before the port goes instead (a dropped Note Off is a stuck note), so
-    // every push that had returned when it landed reaches the port.
+    // every push, all of which had returned when it landed, reaches the port.
     if (mode == rp::midi::OutputMode::Inline) {
         CHECK(backend.sendsStarted.load() < kSends);
     } else {
````

- [ ] **Step 8: build and run them, three times**

```
timeout -k 10 600 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-midi-test -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-midi-test
timeout -k 10 300 .superpowers/kb3plan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-midi-test
timeout -k 10 300 .superpowers/kb3plan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/bin/retroplug-midi-test
```

Expected: PASS, identically each time (the writer cases time threads, so a single green run says less than three):

```
All tests passed (344 assertions in 61 test cases)
```

three times over.

- [ ] **Step 9: the wider suites this task reaches**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:plugin
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs midi-facet
```

Expected: PASS. `pnpm test:plugin` builds and runs all sixteen Catch2 binaries (`retroplug-midi-test` prints
`All tests passed (344 assertions in 61 test cases)`; the run ends `# 16 plugin test binary(ies) passed`).
`midi-facet` passes unchanged, which is the proof the facet stayed `Inline` (`# 5 passed, 0 skipped, 0 failed`).

Then the ThreadSanitizer leg of the midi binary, which is what the ring and the flush are for (the environment goes
INLINE on the guarded command, so it reaches the one run and no later step):

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build-tsan -j$(nproc) --target retroplug-midi-test
timeout -k 10 300 .superpowers/kb3plan/guarded.sh env \
    TSAN_OPTIONS="suppressions=/workspaces/RetroPlug-main/cuttlefish/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" \
    /workspaces/RetroPlug-main/cuttlefish/build-tsan/bin/retroplug-midi-test
```

Expected: PASS, three times over, and no `WARNING: ThreadSanitizer` line in any of them:

```
All tests passed (344 assertions in 61 test cases)
```

- [ ] **Step 10: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/midi/native/src/midi/PortWriter.hpp packages/midi/native/src/midi/PortWriter.cpp \
    packages/midi/native/test/PortWriter.test.cpp packages/midi/native/CMakeLists.txt \
    packages/midi/native/src/midi/MidiStream.hpp packages/midi/native/src/midi/MidiStream.cpp \
    packages/midi/native/test/MidiStream.test.cpp packages/native/sdl/app.hpp packages/midi/native/test/AllocationCounter.hpp \
    docs/sdl-standalone.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(kb3): write each hardware MIDI output from its own thread

The SDL standalone drains the Engine's MIDI out from its audio callback, and a port's send may block until the bytes are
on the wire: WinMM's SysEx path allocates and sleeps until they leave it (a 538-byte Launchpad repaint is about 170 ms
at DIN rate), ALSA's grows its encode buffer and prints from the caller, CoreMIDI makes an OS call. PortWriter owns one
hardware output, a preallocated SPSC ring of length-prefixed records (a push is all or nothing, so a SysEx is never
torn) and a writer thread, and the callback only copies and, at most, wakes it. MidiStream takes a mode: the standalone
is Threaded, the CLI's and the test host's facet stays Inline because their caller is the JS thread and a script relies
on a returned midiSend being on the wire. A full ring drops the newest whole message and counts it per port; a retire or
a close stops the writer with a bounded flush, since a dropped Note Off is a stuck note. The wake's four operations on
the counter and the sleeping flag are seq_cst, because that is the same store-buffering shape the stream's retire
documents and AArch64 does not hide it, and the producer exchanges the flag so a wake is one futex per sleep rather
than one per message. The stream's five concurrency cases now run in both modes, so the thread sanitizer exercises the
ring and the flush with a real concurrent sender.
MSG
```

---

## Task 7: one session pump for both hosts

The test host (`packages/native/src/main.cpp`) pumps txiki without looking for a pending exception or a stop request,
so a module body's throw ends as "tests did not complete (tjs.exit never called)" with no reason, and a kept-alive
file's throw runs to the 30-minute deadline. Its own `tjs.exit` override fails under txiki (frozen `tjs`) and leaves a
TypeError pending for the whole run, so a pump must clear it. The CLI already has the policy; this task moves it into
one helper both hosts drive, and each host keeps its own loop: the CLI's SIGINT handling and unbounded keep-alive, the
test host's bounded spin and its deadline.

**Files** (line numbers are the file at `080c0c0ba`):

- Create: `packages/native/src/host/SessionPump.hpp` (78 lines), `.../SessionPump.cpp` (90 lines; task 8 grows
  both, with the exit hook), `packages/retroplug/test-native/session-pump.test.ts` (185 lines).
- Modify: `packages/native/CMakeLists.txt:141-147` (`retroplug-host`'s sources), `:262-268` (`retroplug-cli`'s).
- Modify: `packages/native/src/main.cpp:39-44` (the include), `:266-274` (the refused override's comment),
  `:283-288` (the pump, before the module is evaluated), `:291-308` (the two loops and the verdict).
- Modify: `packages/native/cli/main.cpp:34-39` (the include), `:80-87` (the flag's comment), `:275-282` (the refused
  override), `:319-356` (the pump, before the module is evaluated) and `:359-381` (the loops).

**Interfaces:**

- Consumes: `TjsHostRuntime` (`dpfjs/host/TjsHostRuntime.hpp`: `pump()`, `context()`), `TJS_GetRuntime`,
  `JS_HasException`, `JS_GetException`, `tjs_dump_error`, the runtime's `stop.async_cb`.
- Produces: `rp::host::SessionPump`, constructed from a `TjsHostRuntime&`, with
  `bool pumpOnce()`, `bool threw() const`, `bool stopRequested() const`. Task 8 extends it with
  `bool exitTaken() const`, `int exitCode() const` and a static exit hook; nothing else uses it.

### Steps

- [ ] **Step 1: write the failing test**

`packages/retroplug/test-native/session-pump.test.ts`, the whole file: four children of the same bundle, selected by
`RP_PUMP_CHILD` - a module body's throw, a microtask job's throw with a sibling timer asking for a clean exit, a throw
in a timer created before the first `test()`, and a throw in a kept-alive session. The spawn rules are in the file's
header and are load-bearing: a child never registers a case (every `test()` sits inside `if (CHILD === undefined)`), a
child refuses to spawn (`RP_SPAWN_DEPTH`), and every child has a watchdog.

````patch
diff --git a/packages/retroplug/test-native/session-pump.test.ts b/packages/retroplug/test-native/session-pump.test.ts
new file mode 100644
index 000000000..7f3581950
--- /dev/null
+++ b/packages/retroplug/test-native/session-pump.test.ts
@@ -0,0 +1,159 @@
+// The test host's session pump (docs/known-bugs-batch-3-design.md section 7): a throw that nothing caught - in the
+// module body, in a microtask job, in a timer made before the first test(), or in a timer while the session is kept
+// alive - ends the run at once with the error on stderr and exit 1. Before the pump, the host drove txiki's loop
+// without looking for the pending exception or the stop request txiki raises with it, so a module body's throw ended as
+// "tests did not complete (tjs.exit never called)" with no reason, and a kept-alive file's throw ran to the 30-minute
+// deadline.
+//
+// Each case is a CHILD process: the throw has to happen before the harness installs its own handlers, and a parent
+// cannot read its own stderr. The child is this same bundle (__SELF_BUNDLE__), selected by an environment variable, as
+// midi-facet.test.ts runs its renamed-session child.
+//
+// WHY THE SPAWN RULES ARE HERE (a run once fork-bombed this machine): a child that reaches this file's test()
+// registrations spawns children of its own, and each of those spawns more, so one run can leave thousands of hosts
+// behind. Three rules, each enforced below:
+//   1. a child NEVER registers a case: every test() in this file is inside `if (CHILD === undefined)`, and the child
+//      branches end the module body (a throw) or the process (tjs.exit, then an unreachable throw);
+//   2. a spawned child REFUSES to spawn: the parent passes RP_SPAWN_DEPTH=1, and spawnChild() throws on sight if it
+//      finds it already set;
+//   3. every child has a watchdog (SIGTERM after a few seconds), so a host that keeps pumping cannot hold the parent.
+import { test, expect } from "@retroplug/testing";
+
+// Just enough of txiki's spawn and a child's pipe, declared locally as midi-facet.test.ts declares them.
+interface ByteStream {
+  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
+}
+declare const tjs: {
+  env: Record<string, string | undefined>;
+  exePath: string;
+  exit(code: number): void;
+  spawn(
+    args: string[],
+    opts: { env?: Record<string, string | undefined>; stdout: "pipe"; stderr: "pipe" },
+  ): {
+    stdout: ByteStream | null;
+    stderr: ByteStream | null;
+    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
+    kill(signal: string): void;
+  };
+};
+declare const __SELF_BUNDLE__: string;
+declare const __rp_keepAlive: () => void;
+// The runtime's globals, declared locally as parity/group.ts declares them: this file bundles for txiki, which has no DOM
+// lib in the package's tsconfig.
+declare function setTimeout(fn: () => void, ms: number): number;
+declare function clearTimeout(id: number): void;
+declare function queueMicrotask(fn: () => void): void;
+
+async function drain(stream: ByteStream | null): Promise<string> {
+  if (!stream) return "";
+  const chunks: Uint8Array[] = [];
+  const reader = stream.getReader();
+  for (;;) {
+    const { done, value } = await reader.read();
+    if (done) break;
+    if (value) chunks.push(value);
+  }
+  let total = 0;
+  for (const c of chunks) total += c.length;
+  const all = new Uint8Array(total);
+  let at = 0;
+  for (const c of chunks) {
+    all.set(c, at);
+    at += c.length;
+  }
+  return new TextDecoder().decode(all);
+}
+
+/** One child's run: its exit status, its two pipes, and how long it took. A child that does NOT end on its own is
+ *  killed at `timeoutMs` and reported as it was found: that is the shape of the bug (a kept-alive session pumped to a
+ *  deadline), and a validation run must not wait it out. */
+async function child(what: string, timeoutMs = 20000): Promise<{ status: number; out: string; err: string; ms: number }> {
+  if (tjs.env.RP_SPAWN_DEPTH) throw new Error("a spawned child may not spawn children (RP_SPAWN_DEPTH is set)");
+  const started = Date.now();
+  const proc = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
+    env: { ...tjs.env, RP_PUMP_CHILD: what, RP_SPAWN_DEPTH: "1" },
+    stdout: "pipe",
+    stderr: "pipe",
+  });
+  const watchdog = setTimeout(() => proc.kill("SIGTERM"), timeoutMs);
+  const [out, err, status] = await Promise.all([drain(proc.stdout), drain(proc.stderr), proc.wait()]);
+  clearTimeout(watchdog);
+  return { status: status.exit_status, out, err, ms: Date.now() - started };
+}
+
+// ---- the children ---------------------------------------------------------------------------------------------
+//
+// Selected by RP_PUMP_CHILD and run BEFORE the first test() below, which is the point: a module body's throw and a
+// timer created here are what the host used to drop. Each branch either throws out of the module body or ends the
+// process, so no case is registered in a child (rule 1 at the top of the file).
+const CHILD = tjs.env.RP_PUMP_CHILD;
+if (CHILD === "module") {
+  // A throw in the module body itself, before any test() exists: what a file's top-level setup does when it fails.
+  throw new Error("boom in the module body");
+} else if (CHILD === "microtask") {
+  // A job of the drain the runtime runs itself: txiki stops the loop and leaves the reason pending.
+  queueMicrotask(() => {
+    throw new Error("boom in a microtask");
+  });
+  // The same loop turn's sibling asks for a clean exit, which must NOT hide the error above. A second, not a few
+  // milliseconds: an instrumented build's turn is slower than such a timer's deadline, and the timer would then fire
+  // inside uv_run BEFORE the job drain - the child exiting 0 with the error never having run, which says nothing about
+  // the host. A second is longer than any turn. (While this task is the tree's tip the timer would C-exit(0), which is
+  // why it is already a second here: the child must not race the fix to be its own witness.)
+  setTimeout(() => tjs.exit(0), 1000);
+} else if (CHILD === "timer") {
+  // Created before this file's first test(), so the harness's own wrappers are not around it yet.
+  setTimeout(() => {
+    throw new Error("boom in a timer");
+  }, 0);
+} else if (CHILD === "kept-alive") {
+  __rp_keepAlive();
+  setTimeout(() => {
+    throw new Error("boom while kept alive");
+  }, 0);
+  // Only reachable if the throw was dropped: the host would then pump to its 30-minute deadline, and this case holds
+  // the child to a few seconds by exiting cleanly (a red run says "exit 0", not "hung").
+  setTimeout(() => tjs.exit(0), 8000);
+}
+if (CHILD !== undefined) {
+  // Nothing more to do in a child: its cases below are guarded out, and its end is the throw it scheduled or its own
+  // tjs.exit. (This is rule 1's other half: a child never reaches a test() registration.)
+}
+
+// ---- the cases -------------------------------------------------------------------------------------------------
+
+if (CHILD === undefined) {
+  test("a throw in a module body ends the run with its reason, not with 'tests did not complete'", async () => {
+    __rp_keepAlive(); // the child outlasts the host's bounded spin
+    const run = await child("module");
+    expect(run.status).toBe(1);
+    expect(run.err.includes("boom in the module body")).toBe(true);
+    expect(run.err.includes("tests did not complete")).toBe(false);
+  });
+
+  test("a throw in a microtask job ends the run, even with a sibling timer asking for a clean exit", async () => {
+    __rp_keepAlive();
+    const run = await child("microtask");
+    // The sibling's tjs.exit(0) must not be what the session ends as: the harness never ran, and the job threw.
+    expect(run.status).toBe(1);
+    expect(run.err.includes("boom in a microtask")).toBe(true);
+  });
+
+  test("a throw in a timer made before the first test() ends the run with its reason", async () => {
+    __rp_keepAlive();
+    const run = await child("timer");
+    expect(run.status).toBe(1);
+    expect(run.err.includes("boom in a timer")).toBe(true);
+    expect(run.err.includes("tests did not complete")).toBe(false);
+  });
+
+  test("a kept-alive session that throws ends at once, with its reason", async () => {
+    __rp_keepAlive();
+    const run = await child("kept-alive", 15000);
+    expect(run.status).toBe(1);
+    expect(run.err.includes("boom while kept alive")).toBe(true);
+    // Not the deadline's 30 minutes, nor the child's own 8-second fallback.
+    expect(run.ms < 5000).toBe(true);
+  });
+}
````

- [ ] **Step 2: run it, and see it fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs session-pump
```

Expected: FAIL, all four. Each child ends with the host's own verdict and no reason (or, for the kept-alive one, with
exit 0 after its own 8-second fallback), which is the bug:

```
not ok 1 - a throw in a module body ends the run with its reason, not with 'tests did not complete'
  ---
  expected true, got false
  ...
not ok 2 - a throw in a microtask job ends the run, even with a sibling timer asking for a clean exit
  ---
  expected true, got false
  ...
not ok 3 - a throw in a timer made before the first test() ends the run with its reason
  ---
  expected true, got false
  ...
not ok 4 - a kept-alive session that throws ends at once, with its reason
  ---
  expected 1, got 0
  ...
# 0 passed, 0 skipped, 4 failed

# 1/1 native test file(s) FAILED: session-pump
```

The children themselves, run by hand at that state, say what the assertions are about (exit 1 with no reason, then exit
0 after 8 s). Each of these is a whole command:

```
timeout -k 10 60 .superpowers/kb3plan/guarded.sh env RP_PUMP_CHILD=module RP_SPAWN_DEPTH=1 \
    build/bin/retroplug-host packages/retroplug/.native-build/session-pump.js
tests did not complete (tjs.exit never called)      (exit 1)
timeout -k 10 60 .superpowers/kb3plan/guarded.sh env RP_PUMP_CHILD=microtask RP_SPAWN_DEPTH=1 \
    build/bin/retroplug-host packages/retroplug/.native-build/session-pump.js
tests did not complete (tjs.exit never called)      (exit 1)
timeout -k 10 60 .superpowers/kb3plan/guarded.sh env RP_PUMP_CHILD=timer RP_SPAWN_DEPTH=1 \
    build/bin/retroplug-host packages/retroplug/.native-build/session-pump.js
tests did not complete (tjs.exit never called)      (exit 1)
timeout -k 10 60 .superpowers/kb3plan/guarded.sh env RP_PUMP_CHILD=kept-alive RP_SPAWN_DEPTH=1 \
    build/bin/retroplug-host packages/retroplug/.native-build/session-pump.js
(nothing printed)                                   (exit 0, after 8 s)
```

- [ ] **Step 3: implement it**

The helper, then both hosts onto it. The test host deletes nothing of its `tjs.exit` override yet (Task 8 does, when
the hook replaces it); the pump clears the TypeError that override leaves.

````patch
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index ef009a1a9..fe565fdb3 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -141,7 +141,7 @@ set_target_properties(retroplug-backend PROPERTIES
 
 # retroplug-host: the test host — the backend lib + a main() that evals a TS bundle over the
 # __rpcSend bridge. Output binary lands in build/bin/.
-add_executable(retroplug-host src/main.cpp)
+add_executable(retroplug-host src/main.cpp src/host/SessionPump.cpp)
 
 # The txiki host it runs its control plane on (src/main.cpp): its own library, not the backend's.
 target_link_libraries(retroplug-host PRIVATE retroplug-backend retroplug-tjs-runtime)
@@ -262,7 +262,7 @@ add_dependencies(retroplug-cli-sdk-assets ${RP_BUNDLE_desktop_GENERATOR})
 # retroplug-cli: the standalone runner — the backend lib + a main() that evals either the COMPILED-IN
 # dispatcher bundle (commands + help) or, when argv[1] is a `.js`, a session file by path. No Node at
 # runtime. Output binary lands in build/bin/.
-add_executable(retroplug-cli cli/main.cpp ${CLI_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C} ${SDK_ASSETS_C} ${SDK_MODULE_C})
+add_executable(retroplug-cli cli/main.cpp src/host/SessionPump.cpp ${CLI_BUNDLE_C} ${TS_STRIPPER_BUNDLE_C} ${SDK_ASSETS_C} ${SDK_MODULE_C})
 
 target_link_libraries(retroplug-cli PRIVATE retroplug-backend retroplug-tjs-runtime)
 retroplug_product(retroplug-cli PACKAGES ${RP_DESKTOP_PACKAGES} PROVIDERS n8 launchpad midi)
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 767987926..8c9314bdf 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -34,6 +34,7 @@
 #include "codecs/QuickJSCodec.h"
 #include "transports/QuickJSTransport.h"
 #include "host/HostServices.hpp"
+#include "host/SessionPump.hpp"
 #include "host/QuickJsGlobals.hpp"
 #include "host/devices/ProviderHost.hpp"  // the rp_device_host this product starts its providers with
 #include "rp_product_providers.hpp"       // generated, in this product's own include dir: its providers and their facets
@@ -80,8 +81,8 @@ ExitState g_exit;
 // Set when txiki asks its loop to stop. Every such request on the session runtime is an error path (an exception in a
 // callback the runtime invoked, a job that failed, a rejection nothing cancelled), and the request survives where the
 // exception may not: a sibling callback in the same loop turn that throws and catches anything replaces, and so clears,
-// the pending exception, and the error would be lost with the session running on. See pumpOnce.
-bool g_stopRequested = false;
+// the pending exception, and the error would be lost with the session running on. host/SessionPump keeps the flag and
+// the check (both hosts run on it); this launcher keeps its own loop around it.
 
 JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
     int code = 0;
@@ -275,8 +276,11 @@ int main(int argc, char** argv) try {
             tjsObj = JS_NewObject(ctx);
             JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
         }
-        if (JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1)) < 0)
+        if (JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1)) < 0) {
+            // Under txiki this set never takes (its `tjs` is frozen), and the TypeError it leaves pending is cleared by
+            // the SessionPump's constructor below, not here: one place for both hosts.
             JS_FreeValue(ctx, JS_GetException(ctx));
+        }
         JS_FreeValue(ctx, tjsObj);
     }
 
@@ -319,38 +323,27 @@ int main(int argc, char** argv) try {
     // throw in an async module body, a rejection nothing cancelled, a throw in a timer or another callback it invoked)
     // into a pending exception and a request to stop ITS loop; this launcher drives the loop itself, so unless it looks
     // the error is dropped: a plain session would say only that it did not complete, a kept-alive one would pump for
-    // ever (wedging any `retroplug-cli test` run it is part of), and a timer's throw would be lost. The stop request is
-    // an async handle whose callback stops txiki's loop; it is taken over to be seen as well, since it survives a sibling
-    // callback that throws and catches clearing the exception. A test file's rejections and timer throws reach its
-    // harness first (packages/testing/ts/harness.ts records them and fails the case); anything else (a throw in a
-    // queueMicrotask job, say) ends the file here, with no TAP, and the runner fails it. The replacement body mirrors
-    // txiki's uv__stop (src/vm.c), which is `uv_stop(&qrt->loop)`; the flag is all it adds.
-    TJS_GetRuntime(ctx)->stop.async_cb = [](uv_async_t* handle) {
-        g_stopRequested = true;
-        uv_stop(handle->loop);
-    };
-    bool       threw    = false;
-    const auto pumpOnce = [&] {
-        host.pump();
-        if (JS_HasException(ctx)) {
-            tjs_dump_error(ctx);
-            threw = true;
-        } else if (g_stopRequested) {
-            std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
-            threw = true;
-        }
-    };
+    // ever (wedging any `retroplug-cli test` run it is part of), and a timer's throw would be lost. A test file's
+    // rejections and timer throws reach its harness first (packages/testing/ts/harness.ts records them and fails the
+    // case); anything else (a throw in a queueMicrotask job, say) ends the file here, with no TAP, and the runner fails
+    // it. host/SessionPump owns the stop takeover, the exception check and the uncaught-setup error, and this launcher
+    // keeps its own SIGINT and keep-alive handling around it.
+    rp::host::SessionPump pump(host);
+    // A Ctrl-C handed to the session's own __rp_onInterrupt hook can throw. tjs_dump_error has already taken that
+    // exception (so the pump sees none pending), and this flag carries the verdict: a hook that throws ends the session,
+    // as an async one that rejects does.
+    bool hookThrew = false;
 
     // Drive the job loop. ES modules evaluate async in QuickJS, so the module body (the dispatcher + the tool)
     // runs during these pumps. A normal command runs to completion + calls tjs.exit within the bounded
     // backstop; a long-running session calls __rp_keepAlive() during setup, which drops us into the unbounded
     // loop below so it can react to MIDI until tjs.exit or Ctrl-C.
-    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive && !threw; ++i) pumpOnce();
+    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive && !pump.threw(); ++i) pump.pumpOnce();
 
-    if (g_keepAlive && !threw) {
+    if (g_keepAlive && !pump.threw()) {
         // Long-running session (e.g. a live MIDI bridge): pump until it exits, throws or Ctrl-C. The 250us cadence
         // matches the native bridge loop; the session's own setInterval poll fires as the loop pumps.
-        while (!g_exit.set && !threw) {
+        while (!g_exit.set && !pump.threw() && !hookThrew) {
             if (g_sigint) {
                 if (JS_IsUndefined(g_onInterrupt)) break;
                 // Hand the first Ctrl-C to the session; a second one gets the default action.
@@ -359,23 +352,23 @@ int main(int argc, char** argv) try {
                 JSValue r = JS_Call(ctx, g_onInterrupt, JS_UNDEFINED, 0, nullptr);
                 if (JS_IsException(r)) {
                     tjs_dump_error(ctx);
-                    threw = true;  // a hook that throws ends the session, as an async one that rejects does
+                    hookThrew = true;
                 }
                 JS_FreeValue(ctx, r);
                 JS_FreeValue(ctx, g_onInterrupt);
                 g_onInterrupt = JS_UNDEFINED;
             }
-            pumpOnce();
+            pump.pumpOnce();
             std::this_thread::sleep_for(std::chrono::microseconds(250));
         }
-    } else if (!threw && !g_exit.set && !g_sigint) {
+    } else if (!pump.threw() && !hookThrew && !g_exit.set && !g_sigint) {
         std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
         return 1;
     }
     JS_FreeValue(ctx, g_onInterrupt);
     g_onInterrupt = JS_UNDEFINED;
     // A session that threw has said what (pumpOnce) and fails, kept alive or not.
-    if (threw) return 1;
+    if (pump.threw() || hookThrew) return 1;
     // A Ctrl-C (g_sigint) without tjs.exit is a clean exit 0 for a session that registered no interrupt hook; one
     // that did ends the process itself (the render tool exits 1).
     return g_exit.set ? g_exit.code : 0;
diff --git a/packages/native/src/host/SessionPump.cpp b/packages/native/src/host/SessionPump.cpp
new file mode 100644
index 000000000..13f337edb
--- /dev/null
+++ b/packages/native/src/host/SessionPump.cpp
@@ -0,0 +1,46 @@
+#include "host/SessionPump.hpp"
+
+#include <cstdio>
+
+namespace rp::host {
+namespace {
+
+/** Set by the stop takeover below. It is a namespace-level flag rather than a member because the async callback is a
+ *  plain function pointer with one argument, the handle, and because there is one session runtime for a process's life
+ *  (the same reason the CLI kept its exit state in a TU-local before this). */
+bool g_stopRequested = false;
+
+}  // namespace
+
+SessionPump::SessionPump(TjsHostRuntime& host) : host_(&host) {
+    // txiki's own callback (src/vm.c) is `uv_stop(&qrt->loop)`; the flag is all this adds.
+    TJS_GetRuntime(host.context())->stop.async_cb = [](uv_async_t* handle) {
+        g_stopRequested = true;
+        uv_stop(handle->loop);
+    };
+    // The refused `tjs.exit` override: both hosts set their own so a session's exit code is recorded rather than the
+    // process ending inside the call, and txiki freezes `tjs` before an embedder can wrap it, so the set fails with a
+    // TypeError (JS_PROP_THROW) that STAYS PENDING on the context for the whole run. It is not the session's error - a
+    // pump that took it for one would fail the run on its first turn - so it is cleared here, once. From section 8's
+    // exit hook on the override is deleted (it never takes) and this is the belt for a host that still sets one.
+    if (JS_HasException(host_->context())) JS_FreeValue(host_->context(), JS_GetException(host_->context()));
+}
+
+SessionPump::~SessionPump() = default;
+
+bool SessionPump::pumpOnce() {
+    JSContext* ctx = host_->context();
+    host_->pump();
+    if (JS_HasException(ctx)) {
+        tjs_dump_error(ctx);
+        threw_ = true;
+    } else if (g_stopRequested) {
+        std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
+        threw_ = true;
+    }
+    return threw_;
+}
+
+bool SessionPump::stopRequested() const { return g_stopRequested; }
+
+}  // namespace rp::host
diff --git a/packages/native/src/host/SessionPump.hpp b/packages/native/src/host/SessionPump.hpp
new file mode 100644
index 000000000..69c32142a
--- /dev/null
+++ b/packages/native/src/host/SessionPump.hpp
@@ -0,0 +1,49 @@
+#pragma once
+
+// One session runtime's loop, shared by the two hosts that drive txiki themselves: retroplug-cli and retroplug-host
+// (docs/known-bugs-batch-3-design.md section 7). txiki turns a throw nothing caught - in a timer or another callback it
+// invoked, in a microtask job, in an async module body - into a pending exception and a request to stop ITS loop. A host
+// that drives that loop by hand (TjsHostRuntime::pump) has to look for both, or the error is dropped: a batch session
+// would say only that it did not complete, and a kept-alive one would pump until its deadline with no reason printed.
+//
+// The stop request is an async handle on the runtime whose callback stops txiki's loop (src/vm.c's uv__stop). It is
+// TAKEN OVER here - the callback keeps that one line and records that it fired - because the request outlives the
+// exception it came with: a sibling callback in the same loop turn that throws and catches anything replaces, and so
+// clears, the pending exception, and the loop request is then the only witness that something threw. Every request on
+// the session runtime is that kind of error path (a callback's throw, a failed job, an unhandled rejection nothing
+// cancelled); nothing benign stops the main runtime's loop.
+//
+// Each host keeps its own LOOP - the CLI's SIGINT handling and unbounded keep-alive, the test host's bounded spin and
+// its deadline - and asks this for one turn of it.
+
+#include "dpfjs/host/TjsHostRuntime.hpp"  // the shared txiki/QuickJS host (+ tjs.h/quickjs.h)
+
+namespace rp::host {
+
+class SessionPump {
+public:
+    /** Install the stop takeover on `host`'s runtime and clear what a refused `tjs.exit` override left pending. One
+     *  pump per session runtime, which is one per process in both hosts. Constructed BEFORE the module is evaluated and
+     *  AFTER the host's globals are installed. */
+    explicit SessionPump(TjsHostRuntime& host);
+    ~SessionPump();
+    SessionPump(const SessionPump&)            = delete;
+    SessionPump& operator=(const SessionPump&) = delete;
+
+    /** One turn of the loop: run what is ready, then report whether the session ended in an ERROR. An exception is
+     *  pending: it is dumped to stderr here. A stop was requested with none pending: reported here, in the launcher's
+     *  words, because a sibling callback cleared the error. Latched: once the session threw, every later turn says so. */
+    bool pumpOnce();
+
+    /** Whether the session has thrown since this pump was made (what a loop's condition tests). */
+    bool threw() const { return threw_; }
+
+    /** Whether txiki has asked its loop to stop since startup. */
+    bool stopRequested() const;
+
+private:
+    TjsHostRuntime* host_;
+    bool            threw_ = false;
+};
+
+}  // namespace rp::host
diff --git a/packages/native/src/main.cpp b/packages/native/src/main.cpp
index ab080b92f..964b11076 100644
--- a/packages/native/src/main.cpp
+++ b/packages/native/src/main.cpp
@@ -39,6 +39,7 @@
 #include "host/HostServices.hpp"
 #include "launchpad/harness/HarnessSurfaceLink.hpp"
 #include "host/QuickJsGlobals.hpp"
+#include "host/SessionPump.hpp"
 #include "host/devices/ProviderHost.hpp"
 #include "launchpad/desktop/LaunchpadProvider.hpp"
 #include "launchpad/desktop/LaunchpadRpc.hpp"
@@ -266,9 +267,11 @@ int main(int argc, char** argv) try {
         });
     });
 
-    // globalThis.tjs.exit — the harness sets the exit code through it. Override
-    // any txiki-provided exit so we record the code and return it (rather than terminating
-    // mid-pump and losing it).
+    // globalThis.tjs.exit - the harness sets the exit code through it. An override that RECORDS the code and returns
+    // (rather than terminating mid-pump) is installed here; under txiki it does not take (txiki freezes `tjs` before an
+    // embedder can wrap it, so the set fails with a TypeError it leaves pending, and the pump clears that once it is
+    // constructed). Section 8 of docs/known-bugs-batch-3-design.md gives txiki an exit hook that both hosts reach
+    // through, and deletes this attempt.
     {
         JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
         if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
@@ -283,6 +286,10 @@ int main(int argc, char** argv) try {
 
     JS_FreeValue(ctx, global);
 
+    // The session pump: the stop takeover and txiki's error check, one turn at a time (host/SessionPump.hpp). It also
+    // clears the TypeError the refused `tjs.exit` override above left pending, which is not the session's error.
+    rp::host::SessionPump pump(host);
+
     const std::string code = slurp(bundlePath);
     const int rc = host.evalModuleBuffer(code.data(), code.size(), bundlePath.c_str());
     if (rc != 0) {
@@ -291,18 +298,22 @@ int main(int argc, char** argv) try {
     }
 
     // Drive the job loop until the harness's microtask + tests run and call tjs.exit
-    // (ES modules evaluate async in QuickJS, so the module body itself runs here). Bounded.
-    for (int i = 0; i < 20000 && !g_exit.set; ++i) host.pump();
+    // (ES modules evaluate async in QuickJS, so the module body itself runs here). Bounded, and ended at once by a throw
+    // nothing caught (pumpOnce prints it).
+    for (int i = 0; i < 20000 && !g_exit.set && !pump.threw(); ++i) pump.pumpOnce();
 
     // A test that awaits real work - a parity recording runs each case alone in a child process - says so
     // with __rp_keepAlive(), and is pumped until it exits, bounded by the wall clock. Asking is required:
     // txiki keeps a handle of its own alive, so "the loop still has work" would also hold a hung test.
     const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
-    while (g_keepAlive && !g_exit.set && std::chrono::steady_clock::now() < deadline) {
-        host.pump();
+    while (g_keepAlive && !g_exit.set && !pump.threw() && std::chrono::steady_clock::now() < deadline) {
+        pump.pumpOnce();
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
     }
 
+    // A session that threw has said what, and fails (the test host's `tjs.exit` override never takes under txiki, so a
+    // thrown file used to run on to the deadline or the bounded spin and say only that it did not complete).
+    if (pump.threw()) return 1;
     if (!g_exit.set) {
         std::fprintf(stderr, "tests did not complete (tjs.exit never called)\n");
         return 1;
````

- [ ] **Step 4: build and run it, and see it pass**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-host retroplug-cli -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs session-pump
```

```
ok 1 - a throw in a module body ends the run with its reason, not with 'tests did not complete'
ok 2 - a throw in a microtask job ends the run, even with a sibling timer asking for a clean exit
ok 3 - a throw in a timer made before the first test() ends the run with its reason
ok 4 - a kept-alive session that throws ends at once, with its reason
# 4 passed, 0 skipped, 0 failed

# 1 native test file(s) passed (jobs=16)
```

and the children, by hand, print their reasons and exit 1:

```
timeout -k 10 60 .superpowers/kb3plan/guarded.sh env RP_PUMP_CHILD=module RP_SPAWN_DEPTH=1 \
    build/bin/retroplug-host packages/retroplug/.native-build/session-pump.js
Error: boom in the module body
    at <anonymous> (/workspaces/RetroPlug-main/cuttlefish/packages/retroplug/.native-build/session-pump.js:236:13)

(exit 1)
```

and the same for `microtask` (`Error: boom in a microtask`), `timer` (`Error: boom in a timer`) and `kept-alive`
(`Error: boom while kept alive`), each exit 1 and the kept-alive one at once.

- [ ] **Step 5: the wider suites this task reaches**

The whole native suite, which is where a pump that turns a file red would show (this run is also task 8's baseline):

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:native
```

Expected: PASS, 177 files (176 plus the new one):

```
# 177 native test file(s) passed (jobs=16)
#   129 case(s) SKIPPED in 59 file(s): ...
```

The CLI moved onto the helper unchanged in behaviour, which `pnpm test:cli-ts` holds through the real binary:

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:cli-ts
```

```
PASS: retroplug-cli TypeScript harness
```

And the sanitizer's host legs (`tools/run-sanitizer.sh thread` runs them, then the three Catch2 binaries):

```
timeout -k 10 2400 .superpowers/kb3plan/guarded.sh tools/run-sanitizer.sh thread
```

```
==> running native test 'dsp-threaded' under thread sanitizer
# 0 passed, 1 skipped, 0 failed
==> running native test 'dsp-lifecycle' under thread sanitizer
# 1 passed, 0 skipped, 0 failed
...
==> host passed under thread sanitizer
```

Address mode is Task 8's, not this one's: what it can break is the teardown a normal exit now runs, which does not
exist until the exit unwinds. Its acceptance there rebuilds the instrumented host first, because a stale
`build-asan/bin/retroplug-host` would run none of the new path, and passes `RETROPLUG_HOST` and `ASAN_OPTIONS` inline
on the guarded command so they reach one run and no later step.

- [ ] **Step 6: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add packages/native/src/host/SessionPump.hpp packages/native/src/host/SessionPump.cpp \
    packages/retroplug/test-native/session-pump.test.ts packages/native/CMakeLists.txt \
    packages/native/src/main.cpp packages/native/cli/main.cpp
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
fix(kb3): report an uncaught error from one session pump for both hosts

The test host pumped txiki without looking for a pending exception or the stop request txiki raises with it, so a module
body's throw ended as "tests did not complete (tjs.exit never called)" with no reason and a kept-alive file's throw ran
to the 30-minute deadline; its tjs.exit override never takes under txiki (frozen tjs) and left a TypeError pending for
the whole run. Move the CLI's policy into one SessionPump both hosts drive a turn at a time - the stop takeover, the
exception dump, the cleared-error report, and one clearing of that refused override's TypeError - and leave each host
its own loop: the CLI's SIGINT handling and unbounded keep-alive, the test host's bounded spin and its deadline.
MSG
```

---

## Task 8: `tjs.exit` returns to the launcher

txiki freezes `tjs` inside `TJS_NewRuntime`, before an embedder can wrap it, and `tjs_exit` (`src/mod_os.c`) calls C
`exit()` from inside the JS call, so `main`'s stack never unwinds: the CLI's `Teardown` (`engine.clearGraph()`, then
`rp_stop_providers`) never runs on a normal exit, and a Launchpad a CLI session claimed never gets its close bytes and
stays in Programmer mode with its front panel locked. An error thrown in the same loop turn as a `tjs.exit(0)` is lost
too (exit 0, nothing printed). This task patches the fork with an exit HOOK: with one set, `tjs.exit` records the
status, stops the loop and throws an uncatchable error, which unwinds try, catch, finally, async functions and promise
jobs, so both hosts' mains return and their teardown runs. A host with no hook (the plugin, the SDL standalone, the
stock `tjs`) keeps `exit()` unchanged. The submodule pointer bumps that carry the fork are this task's last step, in
order, and nothing is pushed.

TWO CYCLES, because the hook alone is not the whole fix. The CLI's compiled-in dispatcher calls `tjs.exit` from inside
its own module body (`cli/cli.ts` for `--version`, `--help`, the bare index, an unknown command and a tool's `--help`;
`cli/session.ts`'s `runSession` for a synchronous tool), and the module's evaluation returns a PROMISE that txiki's
bytecode evaluator checks: with the hook installed and nothing else, every one of those exits rejects that promise with
the hook's uncatchable marker, so the launcher dumps an `InternalError`, prints "session eval failed" and exits 1 where
it must exit 0 or 2. So cycle 1 installs the hook and proves the teardown and the same-turn error; cycle 2 makes the
exit TERMINAL - `exit_taken`, honoured where the module promise and every JS entry point are checked - and proves that
the dispatcher's own exits, and a job queued behind an exit, are the exit's unwinding and not a failure.

**Files** (line numbers are the file at `080c0c0ba`, or at the fork's `e39158c` for the txiki files):

- Modify (txiki, `deps/dpf.js/deps/lv_binding_js/deps/txiki`): `src/tjs.h:38-50` (the hook's type and the three
  declarations), `src/private.h:85-90` (the runtime's four fields), `src/mod_os.c:31-38` (`tjs_exit`),
  `src/vm.c:859-866` (`TJS_Stop`, the accessors), `:903-923` (`TJS_EvalModuleContent`'s load event), `:691-711`
  (`tjs__drain_microtasks`, `tjs__execute_jobs`), `:714-790` (the rejection dispatch), `src/eval.c:26-68`
  (`tjs__eval_bytecode`: the promise's rejection and the `error:` path), `src/utils.c:184-197`
  (`tjs_call_handler`).
- Modify (deps/dpf.js, branch `reflectcpp-0.26`): `src/dpfjs/host/TjsHostRuntime.cpp:107-124`
  (`evalModuleBytecode`'s load event).
- Modify: `packages/native/src/host/SessionPump.hpp:15-21, :32-56` (the exit's accessors and the hook),
  `.../SessionPump.cpp:13-75` (the hook, the constructor, `pumpOnce`, `stopRequested`).
- Modify: `packages/native/src/main.cpp:54-74` (the override goes, the `ExitTrace` seam arrives), `:180-204` (the
  scripted device's exit trace), `:274-291` (the pump's exit), `:296-321` (the loops and the verdict).
- Modify: `packages/native/cli/main.cpp:71-98` (the exit state goes), `:216-222` (the teardown trace), `:254-265` (the
  override goes), `:276-300` (the pump before the module), `:306-312` (the eval-failed belt), `:313-329`
  (`hookThrew`), `:340-357` (the verdict).
- Modify: `packages/retroplug/test-native/session-pump.test.ts:17-24, :79-86, :105-116, :123-136, :149-156, :170-184`
  (the launchpad child and its case).
- Modify: `tools/run-cli-ts-tests.sh:503-510, :512-519, :556-562` (section 6h).

**Interfaces:**

- Consumes: txiki's `TJSRuntime` (`private.h`), `JS_HasException`, `JS_ThrowInternalError`, `JS_GetException`,
  `JS_Throw`, `JS_SetUncatchableError`, `JS_NewObject` and `JS_DefinePropertyValueStr` (the fork's vendored quickjs),
  `uv_stop`, and the pump Task 7 built.
- Produces: in the fork, `typedef bool (*TJSExitHook)(TJSRuntime*, int status, bool error_pending, void* opaque)`,
  `void TJS_SetExitHook(TJSRuntime*, TJSExitHook, void*)`, `bool TJS_StopRequested(TJSRuntime*)` and
  `bool TJS_ExitTaken(TJSRuntime*)`; in this repository, `SessionPump::exitTaken()`, `SessionPump::exitCode()` and its
  two private static hooks; the environment knob `RETROPLUG_TEARDOWN_TRACE` (the CLI prints one line, the test host
  prints the scripted Pro MK3's messages and a closing line), which `tools/run-cli-ts-tests.sh`'s section 6h and the
  launchpad child read. No later task consumes them.

### Steps

- [ ] **Step 1: patch the txiki fork with the exit hook**

The hook's mechanism, one commit: `tjs_exit` asks the runtime's hook first, and a hook that answers true makes it stop
the loop and throw an uncatchable error instead of calling `exit()`; `TJS_Stop` records the request as well as sending
the async handle (a loop that is being stopped never services the handle); `TJS_ExitTaken` reports that an exit has been
taken; the module's `load` event is skipped once one has.

```
git -C /workspaces/RetroPlug-main/cuttlefish/deps/dpf.js/deps/lv_binding_js/deps/txiki apply - <<'PATCH'
<the block's text>
PATCH
```

````patch
diff --git a/src/mod_os.c b/src/mod_os.c
index 8c589b3..7b58306 100644
--- a/src/mod_os.c
+++ b/src/mod_os.c
@@ -33,6 +33,25 @@ static JSValue tjs_exit(JSContext *ctx, JSValue this_val, int argc, JSValue *arg
     if (JS_ToInt32(ctx, &status, argv[0])) {
         status = -1;
     }
+    TJSRuntime *qrt = TJS_GetRuntime(ctx);
+    if (qrt != NULL && qrt->exit_hook != NULL &&
+        qrt->exit_hook(qrt, status, JS_HasException(ctx), qrt->exit_hook_opaque)) {
+        /* The embedder owns this exit: it has recorded the status and returns from its own loop, so nothing is printed
+         * here and the process does not end inside the call. What unwinds the JS stack is the UNCATCHABLE error below:
+         * it passes through try, catch and finally, out of async functions and out of promise jobs, so the embedder's
+         * frame runs its teardown. exit_taken is what keeps the rest of the runtime quiet until then - a promise job, a
+         * timer callback or the module's load event must not run JS after the session has exited - and the stop
+         * request is recorded here as well, since the loop is being stopped and its async handle may never be
+         * serviced. */
+        qrt->exit_taken = true;
+        qrt->stop_requested = true;
+        uv_stop(&qrt->loop);
+        JS_ThrowInternalError(ctx, "tjs.exit(%d) taken by the embedder's exit hook", status);
+        JSValue exc = JS_GetException(ctx);
+        JS_SetUncatchableError(ctx, exc);
+        JS_Throw(ctx, exc); /* takes ownership */
+        return JS_EXCEPTION;
+    }
     /* Reset TTY state (if it had changed) before exiting. */
     uv_tty_reset_mode();
     exit(status);
diff --git a/src/private.h b/src/private.h
index 678f221..1ed8ba7 100644
--- a/src/private.h
+++ b/src/private.h
@@ -85,6 +85,17 @@ struct TJSRuntime {
     bool is_worker;
     bool freeing;
     bool draining_microtasks;
+    /* Set where a stop is REQUESTED: on the runtime's own JS thread (a callback that threw, an embedder's exit hook),
+     * or by an owner that is about to join it (tjs_worker_terminate). Read by TJS_StopRequested from the thread that
+     * drives the loop - an embedder's, for the runtime it owns - so no read races a write: a worker runtime's flag is
+     * written by whichever thread asks it to stop and read by nobody, since an embedder drives no worker's loop. */
+    bool stop_requested;
+    /* An embedder's exit hook (tjs.h) and its opaque, NULL for the stock exit(). */
+    TJSExitHook exit_hook;
+    void *exit_hook_opaque;
+    /* Set when the hook TAKES an exit: from then on nothing runs JS on this runtime (see TJS_ExitTaken). Written and
+     * read on the JS thread the runtime belongs to. */
+    bool exit_taken;
 #ifdef TJS_HAVE_WASM
     struct {
         bool initialized;
diff --git a/src/tjs.h b/src/tjs.h
index 9bc6aba..1558506 100644
--- a/src/tjs.h
+++ b/src/tjs.h
@@ -34,6 +34,18 @@ extern "C" {
 
 typedef struct TJSRuntime TJSRuntime;
 
+/* An embedder's exit hook: what tjs.exit(status) does instead of ending the process. It runs on the JS thread, inside
+ * the JS call, with the exit's status and whether an exception was already PENDING when exit was called (a sibling
+ * callback in the same loop turn that threw: that error is the session's, and the embedder reports it).
+ *
+ * The hook's job is to RECORD. The unwinding is txiki's: when the hook answers true, tjs.exit stops the loop and throws
+ * an UNCATCHABLE error, so the JS stack unwinds through try/catch/finally, out of async functions and out of promise
+ * jobs, and the embedder returns from its own loop and runs its teardown. An embedder that installs no hook - the stock
+ * runtime, and the hosts whose exit is the process's own - keeps the old behaviour, where exit() ends the process from
+ * inside the call and no caller's stack unwinds. A hook that answers false declines the exit, and the old behaviour is
+ * what runs. */
+typedef bool (*TJSExitHook)(TJSRuntime *qrt, int status, bool error_pending, void *opaque);
+
 typedef struct TJSRunOptions {
     int mem_limit;
     size_t stack_size;
@@ -48,6 +60,16 @@ JSContext *TJS_GetJSContext(TJSRuntime *qrt);
 TJSRuntime *TJS_GetRuntime(JSContext *ctx);
 int TJS_Run(TJSRuntime *qrt);
 void TJS_Stop(TJSRuntime *qrt);
+/* Whether a stop has been asked for since this runtime was made: by TJS_Stop (an error path, or an embedder's exit
+ * hook). The async handle that carries the request is not serviced when the loop is already being stopped, so an
+ * embedder driving the loop by hand has to see the request here, in the same turn. */
+bool TJS_StopRequested(TJSRuntime *qrt);
+/* Give this runtime an exit hook (see TJSExitHook above). NULL restores the stock exit(). */
+void TJS_SetExitHook(TJSRuntime *qrt, TJSExitHook hook, void *opaque);
+/* Whether an embedder's exit hook has taken a tjs.exit since this runtime was made. Once it has, nothing else runs JS:
+ * the session has exited, and a promise job, a timer callback or the module's load event would be code running after
+ * the exit's own unwinding. */
+bool TJS_ExitTaken(TJSRuntime *qrt);
 
 #ifdef __cplusplus
 } /* extern "C" */
diff --git a/src/vm.c b/src/vm.c
index 673f63f..5d9fc13 100644
--- a/src/vm.c
+++ b/src/vm.c
@@ -858,9 +858,28 @@ int TJS_Run(TJSRuntime *qrt) {
 
 void TJS_Stop(TJSRuntime *qrt) {
     CHECK_NOT_NULL(qrt);
+    /* Recorded here, not only in the async callback: a loop that is being stopped returns from uv_run without
+     * servicing the handle, and an embedder driving the loop by hand has to see the request in the same turn. */
+    qrt->stop_requested = true;
     uv_async_send(&qrt->stop);
 }
 
+bool TJS_StopRequested(TJSRuntime *qrt) {
+    CHECK_NOT_NULL(qrt);
+    return qrt->stop_requested;
+}
+
+void TJS_SetExitHook(TJSRuntime *qrt, TJSExitHook hook, void *opaque) {
+    CHECK_NOT_NULL(qrt);
+    qrt->exit_hook = hook;
+    qrt->exit_hook_opaque = opaque;
+}
+
+bool TJS_ExitTaken(TJSRuntime *qrt) {
+    CHECK_NOT_NULL(qrt);
+    return qrt->exit_taken;
+}
+
 uv_loop_t *TJS_GetLoop(TJSRuntime *qrt) {
     return &qrt->loop;
 }
@@ -913,8 +932,11 @@ JSValue TJS_EvalModuleContent(JSContext *ctx,
         ret = JS_EvalFunction(ctx, ret);
     }
 
-    /* Emit 'load' event. */
-    if (!JS_IsException(ret) && is_main) {
+    /* Emit 'load' event - unless an embedder's exit hook took an exit during the module body: the session has ended,
+     * and a listener is code running after its exit. */
+    TJSRuntime *qrt = TJS_GetRuntime(ctx);
+    bool exit_taken = qrt != NULL && qrt->exit_taken;
+    if (!JS_IsException(ret) && is_main && !exit_taken) {
         static char emit_load[] = "globalThis.dispatchEvent(new Event('load'));";
         JSValue ret1 = JS_Eval(ctx, emit_load, strlen(emit_load), "<global>", JS_EVAL_TYPE_GLOBAL);
         if (JS_IsException(ret1)) {
````

What it changes: `tjs_exit` gives a hook the status and whether an exception was already pending, and a hook that
answers true (`taken`) gets an uncatchable `InternalError` thrown from inside the JS call, so the JS stack unwinds -
through try, catch and finally, out of async functions and out of promise jobs - and the embedder's own frame runs its
teardown. The hook answers `bool` and `tjs_exit` HONOURS it: false is a declined exit, and the stock `exit()` below it
is what happens then. `stop_requested` is a plain `bool` with a stated invariant: it is written on the runtime's own JS
thread (a callback's throw, an embedder's hook) or by an owner that is about to join it (`tjs_worker_terminate`), and
read from the thread that drives the loop; a worker runtime's flag is read by nobody, because an embedder drives no
worker's loop. That is why it is not an atomic: `private.h` is also compiled as C++, where `<stdatomic.h>` is not
portable.

- [ ] **Step 2: write the failing tests (cycle 1: the teardown and the same-turn error)**

Three places. `tools/run-cli-ts-tests.sh` gains section 6h, whose first four checks are cycle 1's: a normal
`tjs.exit(0)` still exits 0 AND runs the teardown (a trace line under `RETROPLUG_TEARDOWN_TRACE=1`), `tjs.exit(3)`
still exits 3, two 0 ms timers in one turn - the first throwing, the second asking for a clean exit - exit 1 with the
text, and the same with a SIBLING callback clearing the exception first, which exits 1 with the cleared-error line.
`test-native/session-pump.test.ts` gains a child that claims the scripted Pro MK3 and exits, whose parent asserts the
close bytes arrived. The two SEAMS the cases read arrive here too, not with the code they prove: the CLI's
`RETROPLUG_TEARDOWN_TRACE` line in `~Teardown`, and the test host's `ExitTrace`. Without them a red could not tell "the
teardown did not run" from "there is nothing to see".

````patch
diff --git a/packages/retroplug/test-native/session-pump.test.ts b/packages/retroplug/test-native/session-pump.test.ts
index 7f3581950..3df2ae0bf 100644
--- a/packages/retroplug/test-native/session-pump.test.ts
+++ b/packages/retroplug/test-native/session-pump.test.ts
@@ -17,7 +17,14 @@
 //   2. a spawned child REFUSES to spawn: the parent passes RP_SPAWN_DEPTH=1, and spawnChild() throws on sight if it
 //      finds it already set;
 //   3. every child has a watchdog (SIGTERM after a few seconds), so a host that keeps pumping cannot hold the parent.
+//
+// The last case is the exit's own: txiki's tjs.exit used to call C exit() from inside the JS call, so main's stack never
+// unwound and the launchpad provider's disconnect never wrote the close bytes - a pad a CLI session claimed stayed in
+// Programmer mode with its front panel locked (docs/known-bugs-batch-3-design.md section 8).
 import { test, expect } from "@retroplug/testing";
+import { createLaunchpadClient } from "@retroplug/launchpad/client";
+import { exitToLiveMode, PRO_MK3 } from "@retroplug/launchpad";
+import { makeCall } from "../src/rpcClient";
 
 // Just enough of txiki's spawn and a child's pipe, declared locally as midi-facet.test.ts declares them.
 interface ByteStream {
@@ -72,7 +79,9 @@ async function child(what: string, timeoutMs = 20000): Promise<{ status: number;
   if (tjs.env.RP_SPAWN_DEPTH) throw new Error("a spawned child may not spawn children (RP_SPAWN_DEPTH is set)");
   const started = Date.now();
   const proc = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
-    env: { ...tjs.env, RP_PUMP_CHILD: what, RP_SPAWN_DEPTH: "1" },
+    // RETROPLUG_TEARDOWN_TRACE: the host says what its teardown ran (packages/native/src/main.cpp's ExitTrace), which is
+    // the only way a parent can see the close bytes a claimed Launchpad got on the way out.
+    env: { ...tjs.env, RP_PUMP_CHILD: what, RP_SPAWN_DEPTH: "1", RETROPLUG_TEARDOWN_TRACE: "1" },
     stdout: "pipe",
     stderr: "pipe",
   });
@@ -115,6 +124,13 @@ if (CHILD === "module") {
   // Only reachable if the throw was dropped: the host would then pump to its 30-minute deadline, and this case holds
   // the child to a few seconds by exiting cleanly (a red run says "exit 0", not "hung").
   setTimeout(() => tjs.exit(0), 8000);
+} else if (CHILD === "launchpad") {
+  // A session that CLAIMS the scripted Pro MK3 and then exits normally: the provider's teardown is what writes the
+  // close bytes (exitToLiveMode), and the host prints them under RETROPLUG_TEARDOWN_TRACE.
+  const client = createLaunchpadClient(makeCall());
+  const held = client.claim("LPProMK3 MIDI", "LPProMK3 MIDI", Uint8Array.from(exitToLiveMode(PRO_MK3)));
+  if (!held.connected) throw new Error(`the scripted Launchpad was not claimed: ${held.error}`);
+  tjs.exit(0);
 }
 if (CHILD !== undefined) {
   // Nothing more to do in a child: its cases below are guarded out, and its end is the throw it scheduled or its own
@@ -156,4 +172,14 @@ if (CHILD === undefined) {
     // Not the deadline's 30 minutes, nor the child's own 8-second fallback.
     expect(run.ms < 5000).toBe(true);
   });
+
+  test("a normal exit runs the teardown: a claimed Launchpad gets its close bytes", async () => {
+    __rp_keepAlive();
+    const run = await child("launchpad", 15000);
+    expect(run.status).toBe(0);
+    // The close bytes the claim recorded, written by the provider's disconnect on the way out, and the line that says the
+    // provider stack stopped: neither ran when tjs.exit ended the process inside the JS call.
+    expect(run.err.includes("teardown: launchpad out: F0 00 20 29 02 0E 0E 00 F7")).toBe(true);
+    expect(run.err.includes("teardown: providers stopped")).toBe(true);
+  });
 }
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index cab773b99..efd9094a4 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -556,6 +556,40 @@ wait "$pid"
 check "a throwing interrupt hook ends a kept-alive session with exit 1" 1 $?
 contains "...and prints the error" "$out" "hook threw"
 
+# 6h. tjs.exit returns to the launcher. txiki's exit called C exit() from inside the JS call, so main's frame (the
+#     Teardown, the providers, the runtime) never unwound on a NORMAL exit - the launchpad a session claimed never got
+#     its close bytes - and an error thrown in the same turn as a tjs.exit(0) was lost with exit 0. With txiki's exit
+#     hook the exit unwinds like a thrown session or a Ctrl-C, so the same teardown runs, and the hook records what was
+#     pending when exit was called. RETROPLUG_TEARDOWN_TRACE=1 makes the teardown say it ran.
+EX="$TMP/exit"
+mkdir -p "$EX"
+printf '%s\n' 'tjs.exit(0);' >"$EX/normal.js"
+out="$TMP/run-exit-normal.log"
+RETROPLUG_TEARDOWN_TRACE=1 timeout 20 "$CLI" "$EX/normal.js" >"$out" 2>&1
+check "a normal tjs.exit(0) still exits 0" 0 $?
+contains "...and runs the launcher's teardown first" "$out" "teardown: graph cleared, providers stopped"
+printf '%s\n' 'tjs.exit(3);' >"$EX/three.js"
+out="$TMP/run-exit-three.log"
+timeout 20 "$CLI" "$EX/three.js" >"$out" 2>&1
+check "tjs.exit(3) still exits 3" 3 $?
+#     Two 0 ms timers in one loop turn: the first throws, the second asks for a clean exit. The hook is told an exception
+#     was already pending, so the session fails with that error's text instead of exiting 0 silently.
+printf '%s\n' 'setTimeout(() => {' '  throw new Error("thrown before the exit");' '}, 0);' \
+	'setTimeout(() => tjs.exit(0), 0);' >"$EX/same-turn.js"
+out="$TMP/run-exit-same-turn.log"
+timeout 20 "$CLI" "$EX/same-turn.js" >"$out" 2>&1
+check "an error thrown in the same turn as tjs.exit(0) exits 1" 1 $?
+contains "...and prints the error" "$out" "thrown before the exit"
+#     An error a SIBLING callback cleared in the same turn: the exception is gone, so the exit is the one place that can
+#     still know something threw. txiki exposes the stop request, and the hook asks it.
+printf '%s\n' 'setTimeout(() => {' '  throw new Error("lost before the exit");' '}, 0);' \
+	'setTimeout(() => {' '  try {' '    JSON.parse("{");' '  } catch {}' '}, 0);' \
+	'setTimeout(() => tjs.exit(0), 0);' >"$EX/same-turn-cleared.js"
+out="$TMP/run-exit-cleared.log"
+timeout 20 "$CLI" "$EX/same-turn-cleared.js" >"$out" 2>&1
+check "an error a sibling cleared in the same turn as tjs.exit(0) exits 1" 1 $?
+contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
+
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
 for f in pass.test fail.test helper; do
diff --git a/packages/native/src/main.cpp b/packages/native/src/main.cpp
index 964b11076..90673768d 100644
--- a/packages/native/src/main.cpp
+++ b/packages/native/src/main.cpp
@@ -192,6 +192,27 @@ int main(int argc, char** argv) try {
     launchpadRig.outputs = {"LPProMK3 MIDI", "MIDISPORT 2x2 Port A", "Midi Through:Midi Through Port-0 14:1",
                             "RetroPlug:RetroPlug Out 128:1", "RetroPlug Launchpad:LPProMK3 MIDI 129:1"};
     launchpadRig.answering = {"LPProMK3 MIDI"};  // the interface's port is listed and answers nothing
+    // RETROPLUG_TEARDOWN_TRACE=1: say what this host's teardown ran. Declared AFTER the rig and BEFORE the providers, so
+    // its destructor runs after theirs (destruction is the reverse of declaration): the close bytes a claimed Launchpad
+    // is owed (exitToLiveMode) have been written by then, and a case can hold them where it could not before - a session
+    // that ended through txiki's own exit() never unwound main's frame, so this ran for nothing
+    // (test-native/session-pump.test.ts's last case).
+    struct ExitTrace {
+        rp::launchpad::test::Rig& rig;
+        ~ExitTrace() {
+            if (std::getenv("RETROPLUG_TEARDOWN_TRACE") == nullptr) return;
+            for (const auto& message : rig.sentTo("LPProMK3 MIDI")) {
+                std::string line = "teardown: launchpad out:";
+                for (const std::uint8_t byte : message) {
+                    char hex[4];
+                    std::snprintf(hex, sizeof(hex), " %02X", byte);
+                    line += hex;
+                }
+                std::fprintf(stderr, "%s\n", line.c_str());
+            }
+            std::fprintf(stderr, "teardown: providers stopped\n");
+        }
+    } exitTrace{launchpadRig};
     rp::host::ProviderHost launchpadHost{svc.engine, launchpadConfigDir.path.string(),
                                          rp::host::ProviderHost::Calls{}, /*installLinks=*/false};
     struct SurfaceProvider {
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 8c9314bdf..5574f6457 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -231,6 +231,11 @@ int main(int argc, char** argv) try {
         ~Teardown() {
             engine.clearGraph();
             rp_stop_providers(providers);
+            // RETROPLUG_TEARDOWN_TRACE: say the exit ran. Before the exit hook below, this line was never reached on a
+            // normal exit - tjs.exit() ended the process from inside the JS call, so main's frame never unwound - and a
+            // claimed Launchpad kept its Programmer mode. tools/run-cli-ts-tests.sh's section 6h asserts it.
+            if (std::getenv("RETROPLUG_TEARDOWN_TRACE") != nullptr)
+                std::fprintf(stderr, "teardown: graph cleared, providers stopped\n");
         }
     } teardown{svc.engine, providers};
     // The two facets the plugin and the SDL standalone deliberately do NOT mount, so they are not
````

- [ ] **Step 3: run them, and see them fail**

```
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs session-pump
timeout -k 10 600 .superpowers/kb3plan/guarded.sh tools/run-cli-ts-tests.sh
```

Expected: both fail. The child exits 0 with nothing printed after the claim (no teardown ran), and the CLI's normal
exit prints no teardown line and loses the same-turn error:

```
ok 4 - a kept-alive session that throws ends at once, with its reason
not ok 5 - a normal exit runs the teardown: a claimed Launchpad gets its close bytes
  ---
  expected true, got false
  ...
# 4 passed, 0 skipped, 1 failed

# 1/1 native test file(s) FAILED: session-pump
```

```
FAIL - ...and runs the launcher's teardown first (output did not contain: teardown: graph cleared, providers stopped)
FAIL - an error thrown in the same turn as tjs.exit(0) exits 1 (expected exit 1, got 0)
FAIL - ...and prints the error (output did not contain: thrown before the exit)
FAIL - an error a sibling cleared in the same turn as tjs.exit(0) exits 1 (expected exit 1, got 0)
FAIL - ...and says why (output did not contain: a callback threw, and its error was cleared before it could be reported)
FAIL: 5 check(s) failed
```

(The child's own run at that state is the shape of the bug: `RP_PUMP_CHILD=launchpad` prints nothing and exits 0.)

- [ ] **Step 4: implement cycle 1: the pump takes the exit**

The pump installs the hook and reports the exit; both hosts stop installing the dead override and take their exit code
from the pump.

````patch
diff --git a/packages/native/src/host/SessionPump.hpp b/packages/native/src/host/SessionPump.hpp
index 69c32142a..bfd48ddee 100644
--- a/packages/native/src/host/SessionPump.hpp
+++ b/packages/native/src/host/SessionPump.hpp
@@ -11,10 +11,18 @@
 // exception it came with: a sibling callback in the same loop turn that throws and catches anything replaces, and so
 // clears, the pending exception, and the loop request is then the only witness that something threw. Every request on
 // the session runtime is that kind of error path (a callback's throw, a failed job, an unhandled rejection nothing
-// cancelled); nothing benign stops the main runtime's loop.
+// cancelled); nothing benign stops the main runtime's loop. TJS_StopRequested asks txiki itself as well, which is the
+// form that survives a request made in the turn the loop is already stopping (the handle is then never serviced).
 //
 // Each host keeps its own LOOP - the CLI's SIGINT handling and unbounded keep-alive, the test host's bounded spin and
 // its deadline - and asks this for one turn of it.
+//
+// THE EXIT (section 8) is the pump's too: txiki freezes `tjs` before an embedder can wrap it, and its tjs.exit called C
+// exit() from inside the JS call, so a session that exited normally ended the process with main's frame - the launcher's
+// Teardown, the providers, the runtime - never unwinding (a claimed Launchpad never got its close bytes). The pump
+// installs txiki's exit HOOK instead: tjs.exit records its status here and throws an uncatchable error, so the session
+// unwinds like a thrown one and both hosts' mains return. A host that installs no hook (the plugin, the SDL standalone)
+// keeps the old exit.
 
 #include "dpfjs/host/TjsHostRuntime.hpp"  // the shared txiki/QuickJS host (+ tjs.h/quickjs.h)
 
@@ -22,28 +30,49 @@ namespace rp::host {
 
 class SessionPump {
 public:
-    /** Install the stop takeover on `host`'s runtime and clear what a refused `tjs.exit` override left pending. One
-     *  pump per session runtime, which is one per process in both hosts. Constructed BEFORE the module is evaluated and
-     *  AFTER the host's globals are installed. */
+    /** Install the stop takeover and the exit hook on `host`'s runtime, and clear what a refused `tjs.exit` override
+     *  left pending. One pump per session runtime, which is one per process in both hosts. Constructed BEFORE the
+     *  module is evaluated and AFTER the host's globals are installed. */
     explicit SessionPump(TjsHostRuntime& host);
+    /** Leave the runtime a hook that records nothing, so a `tjs.exit` reached during the teardown does not follow a
+     *  pointer to this object. */
     ~SessionPump();
     SessionPump(const SessionPump&)            = delete;
     SessionPump& operator=(const SessionPump&) = delete;
 
     /** One turn of the loop: run what is ready, then report whether the session ended in an ERROR. An exception is
      *  pending: it is dumped to stderr here. A stop was requested with none pending: reported here, in the launcher's
-     *  words, because a sibling callback cleared the error. Latched: once the session threw, every later turn says so. */
+     *  words, because a sibling callback cleared the error. An exit the hook took is a clean end instead (unless an
+     *  error was pending when it was asked for, which is one). Latched: once the session threw, every later turn says
+     *  so. */
     bool pumpOnce();
 
     /** Whether the session has thrown since this pump was made (what a loop's condition tests). */
     bool threw() const { return threw_; }
 
+    /** Whether a session's tjs.exit was TAKEN by the hook: the loop is stopping and main unwinds, so the host's teardown
+     *  runs. Read the code with exitCode(). */
+    bool exitTaken() const { return exitTaken_; }
+
+    /** The status the session exited with (0 unless tjs.exit was given one). */
+    int exitCode() const { return exitCode_; }
+
     /** Whether txiki has asked its loop to stop since startup. */
     bool stopRequested() const;
 
 private:
+    /** txiki's exit hook (TJSExitHook, tjs.h): record the status, dump the error that was already pending so the exit
+     *  does not swallow it, and say the exit was taken (which makes tjs.exit unwind instead of ending the process). The
+     *  FIRST exit wins: a second one may not rewrite the code or the verdict. */
+    static bool onExit(TJSRuntime* qrt, int status, bool errorPending, void* opaque);
+    /** What ~SessionPump leaves in the hook's place: it records nothing and still unwinds. Not nullptr, which would put
+     *  the stock exit() back in the middle of the hosts' teardown. */
+    static bool onExitAbandoned(TJSRuntime* qrt, int status, bool errorPending, void* opaque);
+
     TjsHostRuntime* host_;
-    bool            threw_ = false;
+    bool            threw_     = false;
+    bool            exitTaken_ = false;
+    int             exitCode_  = 0;
 };
 
 }  // namespace rp::host
diff --git a/packages/native/src/host/SessionPump.cpp b/packages/native/src/host/SessionPump.cpp
index 13f337edb..b627b2af0 100644
--- a/packages/native/src/host/SessionPump.cpp
+++ b/packages/native/src/host/SessionPump.cpp
@@ -13,34 +13,78 @@ bool g_stopRequested = false;
 }  // namespace
 
 SessionPump::SessionPump(TjsHostRuntime& host) : host_(&host) {
-    // txiki's own callback (src/vm.c) is `uv_stop(&qrt->loop)`; the flag is all this adds.
-    TJS_GetRuntime(host.context())->stop.async_cb = [](uv_async_t* handle) {
+    // txiki's own callback (src/vm.c) is `uv_stop(&qrt->loop)`; the flag is all this adds. TJS_Stop records the same
+    // request itself (TJS_StopRequested), which is what catches one made in the turn the loop is already stopping: the
+    // async handle is then never serviced.
+    JSContext* ctx = host.context();
+    TJS_GetRuntime(ctx)->stop.async_cb = [](uv_async_t* handle) {
         g_stopRequested = true;
         uv_stop(handle->loop);
     };
-    // The refused `tjs.exit` override: both hosts set their own so a session's exit code is recorded rather than the
+    // The exit hook: what a session's tjs.exit does instead of ending the process here (section 8).
+    TJS_SetExitHook(TJS_GetRuntime(ctx), &SessionPump::onExit, this);
+    // The refused `tjs.exit` override: a host may still set one so a session's exit code is recorded rather than the
     // process ending inside the call, and txiki freezes `tjs` before an embedder can wrap it, so the set fails with a
     // TypeError (JS_PROP_THROW) that STAYS PENDING on the context for the whole run. It is not the session's error - a
-    // pump that took it for one would fail the run on its first turn - so it is cleared here, once. From section 8's
-    // exit hook on the override is deleted (it never takes) and this is the belt for a host that still sets one.
-    if (JS_HasException(host_->context())) JS_FreeValue(host_->context(), JS_GetException(host_->context()));
+    // pump that took it for one would fail the run on its first turn - so it is cleared here, once. Both hosts deleted
+    // their override when they moved onto the hook; this is the belt for one that still sets one.
+    if (JS_HasException(ctx)) JS_FreeValue(ctx, JS_GetException(ctx));
 }
 
-SessionPump::~SessionPump() = default;
+SessionPump::~SessionPump() {
+    // The hook's opaque is this object, and the runtime outlives it (both mains declare the pump after the host and
+    // before the runtime's teardown): leave a hook that records nothing rather than a pointer to what is being
+    // destroyed. A tjs.exit reached from a provider's late callback then still unwinds instead of calling exit() in the
+    // middle of the teardown.
+    TJS_SetExitHook(TJS_GetRuntime(host_->context()), &SessionPump::onExitAbandoned, nullptr);
+}
+
+bool SessionPump::onExit(TJSRuntime* qrt, int status, bool errorPending, void* opaque) {
+    auto* self = static_cast<SessionPump*>(opaque);
+    if (self->exitTaken_) return true;  // the FIRST exit wins: a second may not rewrite the code or the verdict
+    self->exitTaken_ = true;
+    self->exitCode_  = status;
+    if (errorPending) {
+        // A sibling callback in this turn threw, and the session asked to exit with that error still pending. txiki is
+        // about to replace it with the exit's own uncatchable marker, so it is dumped HERE: the session fails (exit 1)
+        // with what was actually thrown, instead of exiting 0 and never printing it.
+        tjs_dump_error(TJS_GetJSContext(qrt));
+        self->threw_ = true;
+    } else if (self->stopRequested()) {
+        // A sibling callback threw and another one's try/catch cleared the exception before this exit was asked for, so
+        // no exception is pending. txiki's stop request survives where the exception does not, and the takeover's
+        // callback has not run yet (the loop is stopping), so this is the earlier request: the session failed, and this
+        // says so in the words the CLI's own cleared-error path uses.
+        std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
+        self->threw_ = true;
+    }
+    return true;  // taken: tjs.exit unwinds rather than ending the process
+}
+
+bool SessionPump::onExitAbandoned(TJSRuntime*, int, bool, void*) { return true; }
 
 bool SessionPump::pumpOnce() {
     JSContext* ctx = host_->context();
     host_->pump();
+    if (exitTaken_ && JS_HasException(ctx)) {
+        // The exit's uncatchable error IS the unwinding, not a failure: the hook has already decided (threw_, from what
+        // was pending when exit was called), so the marker is cleared and the turn reports the clean end.
+        JS_FreeValue(ctx, JS_GetException(ctx));
+    }
+    if (threw_) return true;
+    if (exitTaken_) return false;  // a clean exit: the loop is stopping and the host returns exitCode()
     if (JS_HasException(ctx)) {
         tjs_dump_error(ctx);
         threw_ = true;
-    } else if (g_stopRequested) {
+    } else if (stopRequested()) {
         std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
         threw_ = true;
     }
     return threw_;
 }
 
-bool SessionPump::stopRequested() const { return g_stopRequested; }
+bool SessionPump::stopRequested() const {
+    return g_stopRequested || TJS_StopRequested(TJS_GetRuntime(host_->context()));
+}
 
 }  // namespace rp::host
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 5574f6457..634e9ff61 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -71,27 +71,12 @@ bool endsWithJs(const char* s) {
     return n >= 3 && std::strcmp(s + n - 3, ".js") == 0;
 }
 
-// The exit code a replacement globalThis.tjs.exit() would record. Under txiki that replacement never takes (txiki's
-// `tjs` is frozen, so the set fails and txiki's own exit ends the process), so `set` stays false and the loops below
-// end on their own conditions; it is kept for a host whose `tjs` is not frozen. One host per process,
-// single-threaded, so a TU-local is the right mechanism (mirrors src/main.cpp).
-struct ExitState { int code = 0; bool set = false; };
-ExitState g_exit;
-
 // Set when txiki asks its loop to stop. Every such request on the session runtime is an error path (an exception in a
 // callback the runtime invoked, a job that failed, a rejection nothing cancelled), and the request survives where the
 // exception may not: a sibling callback in the same loop turn that throws and catches anything replaces, and so clears,
 // the pending exception, and the error would be lost with the session running on. host/SessionPump keeps the flag and
 // the check (both hosts run on it); this launcher keeps its own loop around it.
 
-JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
-    int code = 0;
-    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
-    g_exit.code = code;
-    g_exit.set = true;
-    return JS_UNDEFINED;
-}
-
 // A long-running session (a live MIDI bridge) calls globalThis.__rp_keepAlive() during setup to opt out of
 // the bounded batch pump: the launcher then pumps until tjs.exit or Ctrl-C, so the session can react to MIDI
 // indefinitely. Set on the single JS thread; read by the pump loop.
@@ -269,25 +254,11 @@ int main(int argc, char** argv) try {
         JS_SetPropertyStr(ctx, ns, "args", args);
     });
 
-    // globalThis.tjs.exit — the session sets the process exit code through it. Override any
-    // txiki-provided exit so we record the code and return it (rather than terminating mid-pump). With txiki present
-    // the override does not take: txiki's `tjs` is frozen, so the set fails and txiki's own exit, which ends the
-    // process with the code, stays in place, and a session exits through it. The failed set leaves an exception
-    // pending, cleared here so the pump's check below does not take it for the session's.
-    {
-        JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
-        if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
-            JS_FreeValue(ctx, tjsObj);
-            tjsObj = JS_NewObject(ctx);
-            JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
-        }
-        if (JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1)) < 0) {
-            // Under txiki this set never takes (its `tjs` is frozen), and the TypeError it leaves pending is cleared by
-            // the SessionPump's constructor below, not here: one place for both hosts.
-            JS_FreeValue(ctx, JS_GetException(ctx));
-        }
-        JS_FreeValue(ctx, tjsObj);
-    }
+    // globalThis.tjs.exit is txiki's own, and this launcher installs nothing over it: txiki freezes `tjs` before an
+    // embedder can wrap it, and the refused set was a TypeError left pending for the whole run. A session's exit reaches
+    // main through txiki's exit HOOK, which the session pump installs below (host/SessionPump.hpp): the status is
+    // recorded and the JS stack unwinds instead of the process ending inside the call, so the Teardown above - the graph,
+    // the providers, the runtimes - runs on a NORMAL exit as it already did for a thrown session and for Ctrl-C.
 
     // globalThis.__rp_keepAlive — a long-running session (MIDI bridge) calls it to switch the pump from the
     // bounded batch backstop to run-until-exit (see the pump loop below).
@@ -305,6 +276,24 @@ int main(int argc, char** argv) try {
 
     JS_FreeValue(ctx, global);
 
+    // The session pump, BEFORE any session code runs: a `.js` file's body is evaluated below and may call tjs.exit on its
+    // very first line, and the exit HOOK it takes (host/SessionPump.hpp, tjs.h) has to be installed by then. It also
+    // clears a refused `tjs.exit` override's pending TypeError and takes over the loop's stop request.
+    //
+    // Its pumpOnce is one turn of the loop, and the session's end when something threw that nothing caught. txiki turns
+    // such an error (a throw in an async module body, a rejection nothing cancelled, a throw in a timer or another
+    // callback it invoked) into a pending exception and a request to stop ITS loop; this launcher drives the loop itself,
+    // so unless it looks the error is dropped: a plain session would say only that it did not complete, a kept-alive one
+    // would pump for ever (wedging any `retroplug-cli test` run it is part of), and a timer's throw would be lost. A test
+    // file's rejections and timer throws reach its harness first (packages/testing/ts/harness.ts records them and fails
+    // the case); anything else (a throw in a queueMicrotask job, say) ends the file here, with no TAP, and the runner
+    // fails it. This launcher keeps its own SIGINT and keep-alive handling around it.
+    rp::host::SessionPump pump(host);
+    // A Ctrl-C handed to the session's own __rp_onInterrupt hook can throw. tjs_dump_error has already taken that
+    // exception (so the pump sees none pending), and this flag carries the verdict: a hook that throws ends the session,
+    // as an async one that rejects does.
+    bool hookThrew = false;
+
     // A `.js` path is slurped + evaled; otherwise run the compiled-in dispatcher (it reads the command
     // from the args we exposed above and prints help / routes it).
     int rc;
@@ -324,31 +313,16 @@ int main(int argc, char** argv) try {
 
     std::signal(SIGINT, onSigint);
 
-    // One turn of the loop, and the session's end when something threw that nothing caught. txiki turns such an error (a
-    // throw in an async module body, a rejection nothing cancelled, a throw in a timer or another callback it invoked)
-    // into a pending exception and a request to stop ITS loop; this launcher drives the loop itself, so unless it looks
-    // the error is dropped: a plain session would say only that it did not complete, a kept-alive one would pump for
-    // ever (wedging any `retroplug-cli test` run it is part of), and a timer's throw would be lost. A test file's
-    // rejections and timer throws reach its harness first (packages/testing/ts/harness.ts records them and fails the
-    // case); anything else (a throw in a queueMicrotask job, say) ends the file here, with no TAP, and the runner fails
-    // it. host/SessionPump owns the stop takeover, the exception check and the uncaught-setup error, and this launcher
-    // keeps its own SIGINT and keep-alive handling around it.
-    rp::host::SessionPump pump(host);
-    // A Ctrl-C handed to the session's own __rp_onInterrupt hook can throw. tjs_dump_error has already taken that
-    // exception (so the pump sees none pending), and this flag carries the verdict: a hook that throws ends the session,
-    // as an async one that rejects does.
-    bool hookThrew = false;
-
     // Drive the job loop. ES modules evaluate async in QuickJS, so the module body (the dispatcher + the tool)
     // runs during these pumps. A normal command runs to completion + calls tjs.exit within the bounded
     // backstop; a long-running session calls __rp_keepAlive() during setup, which drops us into the unbounded
     // loop below so it can react to MIDI until tjs.exit or Ctrl-C.
-    for (int i = 0; i < 20000 && !g_exit.set && !g_sigint && !g_keepAlive && !pump.threw(); ++i) pump.pumpOnce();
+    for (int i = 0; i < 20000 && !pump.exitTaken() && !g_sigint && !g_keepAlive && !pump.threw(); ++i) pump.pumpOnce();
 
     if (g_keepAlive && !pump.threw()) {
         // Long-running session (e.g. a live MIDI bridge): pump until it exits, throws or Ctrl-C. The 250us cadence
         // matches the native bridge loop; the session's own setInterval poll fires as the loop pumps.
-        while (!g_exit.set && !pump.threw() && !hookThrew) {
+        while (!pump.exitTaken() && !pump.threw() && !hookThrew) {
             if (g_sigint) {
                 if (JS_IsUndefined(g_onInterrupt)) break;
                 // Hand the first Ctrl-C to the session; a second one gets the default action.
@@ -366,7 +340,7 @@ int main(int argc, char** argv) try {
             pump.pumpOnce();
             std::this_thread::sleep_for(std::chrono::microseconds(250));
         }
-    } else if (!pump.threw() && !hookThrew && !g_exit.set && !g_sigint) {
+    } else if (!pump.threw() && !hookThrew && !pump.exitTaken() && !g_sigint) {
         std::fprintf(stderr, "session did not complete (tjs.exit never called)\n");
         return 1;
     }
@@ -376,7 +350,7 @@ int main(int argc, char** argv) try {
     if (pump.threw() || hookThrew) return 1;
     // A Ctrl-C (g_sigint) without tjs.exit is a clean exit 0 for a session that registered no interrupt hook; one
     // that did ends the process itself (the render tool exits 1).
-    return g_exit.set ? g_exit.code : 0;
+    return pump.exitTaken() ? pump.exitCode() : 0;
 } catch (const std::exception& e) {
     std::fprintf(stderr, "error: %s\n", e.what());
     return 1;
diff --git a/packages/native/src/main.cpp b/packages/native/src/main.cpp
index 90673768d..4566542af 100644
--- a/packages/native/src/main.cpp
+++ b/packages/native/src/main.cpp
@@ -54,20 +54,6 @@ using BackendRpcServer = rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec
 
 namespace {
 
-// The exit code the harness reports through globalThis.tjs.exit(). One host per process,
-// single-threaded, so a TU-local is the right mechanism (mirrors TestHarness.cpp's
-// g_activeImpl pattern).
-struct ExitState { int code = 0; bool set = false; };
-ExitState g_exit;
-
-JSValue jsExit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
-    int code = 0;
-    if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
-    g_exit.code = code;
-    g_exit.set = true;
-    return JS_UNDEFINED;
-}
-
 // Set by globalThis.__rp_keepAlive(): the test awaits work that outlasts the bounded spin in main().
 bool g_keepAlive = false;
 
@@ -288,27 +274,17 @@ int main(int argc, char** argv) try {
         });
     });
 
-    // globalThis.tjs.exit - the harness sets the exit code through it. An override that RECORDS the code and returns
-    // (rather than terminating mid-pump) is installed here; under txiki it does not take (txiki freezes `tjs` before an
-    // embedder can wrap it, so the set fails with a TypeError it leaves pending, and the pump clears that once it is
-    // constructed). Section 8 of docs/known-bugs-batch-3-design.md gives txiki an exit hook that both hosts reach
-    // through, and deletes this attempt.
-    {
-        JSValue tjsObj = JS_GetPropertyStr(ctx, global, "tjs");
-        if (JS_IsUndefined(tjsObj) || JS_IsNull(tjsObj)) {
-            JS_FreeValue(ctx, tjsObj);
-            tjsObj = JS_NewObject(ctx);
-            JS_SetPropertyStr(ctx, global, "tjs", JS_DupValue(ctx, tjsObj));
-        }
-        JS_SetPropertyStr(ctx, tjsObj, "exit", JS_NewCFunction(ctx, jsExit, "exit", 1));
-        JS_FreeValue(ctx, tjsObj);
-    }
+    // globalThis.tjs.exit is txiki's own, and this host installs nothing over it: txiki freezes `tjs` before an embedder
+    // can wrap it, and the refused set was a TypeError left pending for the whole run. A session's exit reaches main
+    // through txiki's exit HOOK, which the session pump installs below (host/SessionPump.hpp): it records the status and
+    // unwinds instead of ending the process inside the JS call, so main's frame - the providers, the engine, the runtime
+    // - is torn down on the way out.
     JS_SetPropertyStr(ctx, global, "__rp_keepAlive", JS_NewCFunction(ctx, jsKeepAlive, "__rp_keepAlive", 0));
 
     JS_FreeValue(ctx, global);
 
-    // The session pump: the stop takeover and txiki's error check, one turn at a time (host/SessionPump.hpp). It also
-    // clears the TypeError the refused `tjs.exit` override above left pending, which is not the session's error.
+    // The session pump: the stop takeover, txiki's error check one turn at a time, and the exit hook that lets a normal
+    // exit unwind (host/SessionPump.hpp).
     rp::host::SessionPump pump(host);
 
     const std::string code = slurp(bundlePath);
@@ -320,26 +296,25 @@ int main(int argc, char** argv) try {
 
     // Drive the job loop until the harness's microtask + tests run and call tjs.exit
     // (ES modules evaluate async in QuickJS, so the module body itself runs here). Bounded, and ended at once by a throw
-    // nothing caught (pumpOnce prints it).
-    for (int i = 0; i < 20000 && !g_exit.set && !pump.threw(); ++i) pump.pumpOnce();
+    // nothing caught (pumpOnce prints it) or by a session's exit (the hook records it; the loop stops).
+    for (int i = 0; i < 20000 && !pump.exitTaken() && !pump.threw(); ++i) pump.pumpOnce();
 
     // A test that awaits real work - a parity recording runs each case alone in a child process - says so
     // with __rp_keepAlive(), and is pumped until it exits, bounded by the wall clock. Asking is required:
     // txiki keeps a handle of its own alive, so "the loop still has work" would also hold a hung test.
     const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
-    while (g_keepAlive && !g_exit.set && !pump.threw() && std::chrono::steady_clock::now() < deadline) {
+    while (g_keepAlive && !pump.exitTaken() && !pump.threw() && std::chrono::steady_clock::now() < deadline) {
         pump.pumpOnce();
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
     }
 
-    // A session that threw has said what, and fails (the test host's `tjs.exit` override never takes under txiki, so a
-    // thrown file used to run on to the deadline or the bounded spin and say only that it did not complete).
+    // A session that threw has said what and fails; one that exited says with which code.
     if (pump.threw()) return 1;
-    if (!g_exit.set) {
+    if (!pump.exitTaken()) {
         std::fprintf(stderr, "tests did not complete (tjs.exit never called)\n");
         return 1;
     }
-    return g_exit.code;
+    return pump.exitCode();
 } catch (const std::exception& e) {
     std::fprintf(stderr, "error: %s\n", e.what());
     return 1;
````

The hook's three decisions, in order: the FIRST exit wins (a second may not rewrite the code or the verdict); an error
pending when `exit` was called is dumped HERE, because txiki is about to replace it with the marker, so a same-turn
throw makes the exit 1 WITH its text; and when no exception is pending but a stop had been requested, the error was
cleared by a sibling callback and the session still failed, in the words the CLI's own cleared-error path uses. The
pump's `pumpOnce` treats the marker as the clean end (it is the unwinding, and the hook has already decided), and
`~SessionPump` leaves the runtime a hook that records nothing and still unwinds instead of a pointer to a destroyed
pump.

- [ ] **Step 5: build and run them, and see them pass**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-host retroplug-cli -j$(nproc)
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs session-pump
timeout -k 10 600 .superpowers/kb3plan/guarded.sh tools/run-cli-ts-tests.sh
```

```
ok 5 - a normal exit runs the teardown: a claimed Launchpad gets its close bytes
# 5 passed, 0 skipped, 0 failed

# 1 native test file(s) passed (jobs=16)
```

```
ok   - a normal tjs.exit(0) still exits 0
ok   - ...and runs the launcher's teardown first
ok   - tjs.exit(3) still exits 3
ok   - an error thrown in the same turn as tjs.exit(0) exits 1
ok   - an error a sibling cleared in the same turn as tjs.exit(0) exits 1
PASS: retroplug-cli TypeScript harness
```

- [ ] **Step 6: write the failing tests (cycle 2: the dispatcher's own exits)**

Section 6h gains the rest: `--version` exits 0 with an EMPTY stderr, `--help` the same, the bare call and an unknown
command exit 2 with no "session eval failed", a synchronous tool (`lsdj-rom info` with no argument, which is
`runSession`'s own `tjs.exit(1)`) keeps its exit code and prints its usage with neither "session eval failed" nor the
exit's marker, and a `.js` session that queues a microtask and then exits prints nothing (no JS runs once an exit has
been taken). Two helpers come with them: `quiet` (a stderr that must be empty) and `absent` (a string that must NOT be
in the output). `normal.js`'s log must contain no marker either. The five dispatcher calls run under `timeout 20` like
their neighbours, so a hang there cannot hold the whole harness. The Ctrl-C legs gain one more: a kept-alive session
whose `__rp_onInterrupt` hook ENDS the session itself (`tjs.exit(5)`, as a live bridge that releases its devices before
exiting does) must exit 5, with no marker in its log - the hook is the session's own code, so its exit is the exit and
not a thrown hook. It is red at this step because the keep-alive loop treats every exception out of the hook's
`JS_Call` as a throw, and the marker is what `tjs.exit` now raises.

````patch
diff --git a/tools/run-cli-ts-tests.sh b/tools/run-cli-ts-tests.sh
index efd9094..a5a4275 100755
--- a/tools/run-cli-ts-tests.sh
+++ b/tools/run-cli-ts-tests.sh
@@ -52,6 +52,25 @@ contains_line() { # contains_line <label> <haystack-file> <exact line> - a resul
 	fi
 }
 
+quiet() { # quiet <label> <stderr-file> - nothing was written to it
+	if [ ! -s "$2" ]; then
+		echo "ok   - $1"
+	else
+		echo "FAIL - $1 (stderr was not empty)"
+		sed 's/^/       | /' "$2" >&2
+		fails=$((fails + 1))
+	fi
+}
+absent() { # absent <label> <haystack-file> <needle> - and it is NOT in there
+	if grep -qF -- "$3" "$2"; then
+		echo "FAIL - $1 (output contained: $3)"
+		sed 's/^/       | /' "$2" >&2
+		fails=$((fails + 1))
+	else
+		echo "ok   - $1"
+	fi
+}
+
 # Each case gets its own build dir so nothing is inherited between them.
 TMP="$(mktemp -d)"
 trap 'rm -rf "$TMP"' EXIT
@@ -555,6 +574,19 @@ kill -INT "$pid" 2>/dev/null
 wait "$pid"
 check "a throwing interrupt hook ends a kept-alive session with exit 1" 1 $?
 contains "...and prints the error" "$out" "hook threw"
+#     A hook that ENDS the session itself (release the devices, then exit with the code a wrapper expects): the session
+#     ends with the hook's code, and the marker that carried the unwinding out of tjs.exit is not an error to report.
+printf '%s\n' 'globalThis.__rp_keepAlive();' \
+	'globalThis.__rp_onInterrupt(() => {' '  tjs.exit(5);' '});' \
+	'setTimeout(() => console.log("ready"), 0);' >"$UC/ka-exit.js"
+out="$TMP/run-ka-exit.log"
+timeout 20 "$CLI" "$UC/ka-exit.js" >"$out" 2>&1 &
+pid=$!
+wait_ready "$out"
+kill -INT "$pid" 2>/dev/null
+wait "$pid"
+check "an interrupt hook that calls tjs.exit ends the session with its code" 5 $?
+absent "...and never shows the exit's marker" "$out" "taken by the embedder's exit hook"
 
 # 6h. tjs.exit returns to the launcher. txiki's exit called C exit() from inside the JS call, so main's frame (the
 #     Teardown, the providers, the runtime) never unwound on a NORMAL exit - the launchpad a session claimed never got
@@ -589,6 +621,36 @@ out="$TMP/run-exit-cleared.log"
 timeout 20 "$CLI" "$EX/same-turn-cleared.js" >"$out" 2>&1
 check "an error a sibling cleared in the same turn as tjs.exit(0) exits 1" 1 $?
 contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
+absent "...and the exit's own marker never reaches the log" "$out" "taken by the embedder's exit hook"
+#     The DISPATCHER's own synchronous exits. cli/cli.ts calls tjs.exit from the module body for --version, the bare
+#     index, an unknown command and a tool's --help, and cli/session.ts's runSession calls it for a synchronous tool
+#     (lsdj-rom info with no argument falls into tjs.exit(1)). With the hook each of those exits unwinds through the
+#     module's promise, and none of them is a failed session: the marker IS the unwinding, not an error.
+timeout 20 "$CLI" --version >"$EX/version.out" 2>"$EX/version.err"
+check "--version exits 0" 0 $?
+quiet "...and says nothing on stderr" "$EX/version.err"
+timeout 20 "$CLI" --help >"$EX/help.out" 2>"$EX/help.err"
+check "--help exits 0" 0 $?
+quiet "...and says nothing on stderr" "$EX/help.err"
+timeout 20 "$CLI" >"$EX/bare.out" 2>"$EX/bare.err"
+check "a bare call exits 2" 2 $?
+absent "...and does not report a failed session" "$EX/bare.err" "session eval failed"
+timeout 20 "$CLI" no-such-command >"$EX/unknown.out" 2>"$EX/unknown.err"
+check "an unknown command exits 2" 2 $?
+absent "...and does not report a failed session" "$EX/unknown.err" "session eval failed"
+timeout 20 "$CLI" lsdj-rom info >"$EX/tool.out" 2>"$EX/tool.err"
+check "a synchronous tool's own exit code stands (lsdj-rom info with no argument: 1)" 1 $?
+contains "...and prints its usage" "$EX/tool.err" "usage: lsdj-rom info"
+absent "...and does not report a failed session" "$EX/tool.err" "session eval failed"
+absent "...and never shows the exit's marker" "$EX/tool.err" "taken by the embedder's exit hook"
+#     An exit with a microtask queued behind it: nothing may run JS once the exit has been taken, so the job never runs
+#     (before the fork honoured that, it did, and its side effects were the session's last word).
+printf '%s\n' 'setTimeout(() => {' '  Promise.resolve().then(() => console.log("ran after exit"));' \
+	'  tjs.exit(0);' '}, 0);' >"$EX/after-exit.js"
+out="$TMP/run-exit-after.log"
+timeout 20 "$CLI" "$EX/after-exit.js" >"$out" 2>&1
+check "a clean exit after a queued microtask still exits 0" 0 $?
+absent "...and the job queued behind it never ran" "$out" "ran after exit"
 
 # 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
 #    emitted .js points at the right line of the .ts.
````

- [ ] **Step 7: run them and see them fail**

```
timeout -k 10 600 .superpowers/kb3plan/guarded.sh tools/run-cli-ts-tests.sh
```

Expected: FAIL, and this is exactly the review's C1: the dispatcher's synchronous exits now unwind through the module
promise, and the bytecode evaluator dumps the marker and reports a failed evaluation. By hand, at this state:

```
$ timeout -k 10 30 .superpowers/kb3plan/guarded.sh build/bin/retroplug-cli --version
v0.8.0
InternalError: tjs.exit(0) taken by the embedder's exit hook
    at exit (native)
    at exitProcess (<unnamed>:28782:12)
    at <anonymous> (<unnamed>:33072:3)

session eval failed
(exit 1)
```

and the script's thirteen failures - the dispatcher's eleven, then the Ctrl-C hook's two:

```
FAIL - an interrupt hook that calls tjs.exit ends the session with its code (expected exit 5, got 1)
FAIL - ...and never shows the exit's marker (output contained: taken by the embedder's exit hook)
       | ready
       | InternalError: tjs.exit(5) taken by the embedder's exit hook
       |     at exit (native)
       |     at <anonymous> (<unnamed>/ka-exit.js:3:7)
FAIL - --version exits 0 (expected exit 0, got 1)
FAIL - ...and says nothing on stderr (stderr was not empty)
FAIL - --help exits 0 (expected exit 0, got 1)
FAIL - ...and says nothing on stderr (stderr was not empty)
FAIL - a bare call exits 2 (expected exit 2, got 1)
FAIL - ...and does not report a failed session (output contained: session eval failed)
FAIL - an unknown command exits 2 (expected exit 2, got 1)
FAIL - ...and does not report a failed session (output contained: session eval failed)
FAIL - ...and does not report a failed session (output contained: session eval failed)
FAIL - ...and never shows the exit's marker (output contained: taken by the embedder's exit hook)
FAIL - ...and the job queued behind it never ran (output contained: ran after exit)
FAIL: 13 check(s) failed
```

(The hook's own failure is the review's R1: the keep-alive loop takes the marker out of the hook's `JS_Call` for a
throw, so a session that ends itself from its Ctrl-C hook exits 1 and says why - with the exit's marker, which no user
of `__rp_onInterrupt` should ever see.)

- [ ] **Step 8: implement cycle 2: the exit is terminal**

Three patches, one per repository: the header's `git -C /workspaces/RetroPlug-main/cuttlefish apply` is right for the
third alone (the fork's and `deps/dpf.js`'s hunks are rooted at their own trees), so each block goes in against the
repository it belongs to, with the block's text (fences excluded) between the heredoc's first line and its `PATCH`:

```
git -C /workspaces/RetroPlug-main/cuttlefish/deps/dpf.js/deps/lv_binding_js/deps/txiki apply - <<'PATCH'
<the first block>
PATCH
git -C /workspaces/RetroPlug-main/cuttlefish/deps/dpf.js apply - <<'PATCH'
<the second block>
PATCH
git -C /workspaces/RetroPlug-main/cuttlefish apply - <<'PATCH'
<the third block>
PATCH
```

The fork's: `exit_taken` is honoured where the module's promise is checked (a rejection that is the exit
counts as evaluated), and every JS entry point the loop can still reach - the microtask drain, the unhandled-rejection
dispatch, a timer's handler - returns without calling JS once an exit has been taken. `deps/dpf.js`'s: the bytecode
module's own `load` event is skipped for the same reason. The CLI's: the "session eval failed" line asks the pump first,
and a Ctrl-C hook whose `tjs.exit` threw the marker is the exit rather than a thrown hook (its exception is cleared, so
the process ends with the hook's own code and prints nothing).

````patch
diff --git a/src/eval.c b/src/eval.c
index 8925572..6a22bcc 100644
--- a/src/eval.c
+++ b/src/eval.c
@@ -52,6 +52,15 @@ int tjs__eval_bytecode(JSContext *ctx, const uint8_t *buf, size_t buf_len, bool
             // It's a promise!
             if (promise_state == JS_PROMISE_REJECTED) {
                 JSValue res = JS_PromiseResult(ctx, val);
+                /* A module the embedder's exit hook ended rejects with the hook's uncatchable marker: that rejection is
+                 * the EXIT unwinding, not a failed evaluation, so nothing is printed and the module counts as
+                 * evaluated. The marker is freed with the promise it is held in. */
+                if (TJS_ExitTaken(TJS_GetRuntime(ctx))) {
+                    JS_FreeValue(ctx, res);
+                    JS_FreeValue(ctx, val);
+
+                    return 0;
+                }
                 tjs_dump_error1(ctx, res);
                 JS_FreeValue(ctx, res);
                 JS_FreeValue(ctx, val);
@@ -66,6 +75,14 @@ int tjs__eval_bytecode(JSContext *ctx, const uint8_t *buf, size_t buf_len, bool
     return 0;
 
 error:
+    if (TJS_ExitTaken(TJS_GetRuntime(ctx))) {
+        /* The module's evaluation threw the exit hook's uncatchable marker. That is what every one of the compiled-in
+         * dispatcher's own exits is - --version, --help, the bare index, an unknown command, a tool's --help - and a
+         * synchronous runSession tool's return too. It is the EXIT unwinding, not an evaluation failure: the marker is
+         * freed and the module counts as evaluated, so the embedder returns from its loop with the hook's status. */
+        JS_FreeValue(ctx, JS_GetException(ctx));
+        return 0;
+    }
     tjs_dump_error(ctx);
     return -1;
 }
diff --git a/src/utils.c b/src/utils.c
index 6553d6b..4b0fcef 100644
--- a/src/utils.c
+++ b/src/utils.c
@@ -183,16 +183,17 @@ void tjs_dump_error1(JSContext *ctx, JSValue exception_val) {
 
 void tjs_call_handler(JSContext *ctx, JSValue func, int argc, JSValue *argv) {
     JSValue ret, func1;
+    /* A handler (a timer, say) that comes due after the embedder's exit hook took an exit does not run: the session has
+     * ended, and the loop is only finishing the iteration it is in. */
+    TJSRuntime *qrt = TJS_GetRuntime(ctx);
+    CHECK_NOT_NULL(qrt);
+    if (qrt->exit_taken) return;
     /* 'func' might be destroyed when calling itself (if it frees the
        handler), so must take extra care */
     func1 = JS_DupValue(ctx, func);
     ret = JS_Call(ctx, func1, JS_UNDEFINED, argc, argv);
     JS_FreeValue(ctx, func1);
-    if (JS_IsException(ret)) {
-        TJSRuntime *qrt = TJS_GetRuntime(ctx);
-        CHECK_NOT_NULL(qrt);
-        TJS_Stop(qrt);
-    }
+    if (JS_IsException(ret)) TJS_Stop(qrt);
     JS_FreeValue(ctx, ret);
     tjs__drain_microtasks(ctx);
 }
diff --git a/src/vm.c b/src/vm.c
index 5d9fc13..8146940 100644
--- a/src/vm.c
+++ b/src/vm.c
@@ -692,6 +692,13 @@ void tjs__drain_microtasks(JSContext *ctx) {
     JSContext *ctx1;
     int err;
 
+    /* Nothing runs JS once the embedder's exit hook has taken an exit: the session has ended, and a job queued behind
+     * the exit would be code running after its unwinding - its side effects the session's last word, and an error it
+     * threw replacing the exit's own marker. */
+    TJSRuntime *qrt0 = TJS_GetRuntime(ctx);
+    CHECK_NOT_NULL(qrt0);
+    if (qrt0->exit_taken) return;
+
     for (;;) {
         err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
         if (err <= 0) {
@@ -715,6 +722,8 @@ void tjs__execute_jobs(JSContext *ctx) {
      * (e.g. Promise.reject(x).catch(fn)) don't spuriously fire events. */
     TJSRuntime *qrt = TJS_GetRuntime(ctx);
     CHECK_NOT_NULL(qrt);
+    /* An exit ends the session: no unhandledrejection event is dispatched after it either (its listener is JS). */
+    if (qrt->exit_taken) return;
     struct list_head *el, *el1;
     list_for_each_safe(el, el1, &qrt->pending_rejections) {
         TJSPendingRejection *pr = list_entry(el, TJSPendingRejection, link);
````

````patch
diff --git a/src/dpfjs/host/TjsHostRuntime.cpp b/src/dpfjs/host/TjsHostRuntime.cpp
index 5b1042f..645c1c2 100644
--- a/src/dpfjs/host/TjsHostRuntime.cpp
+++ b/src/dpfjs/host/TjsHostRuntime.cpp
@@ -112,6 +112,12 @@ int TjsHostRuntime::evalModuleBytecode(const std::uint8_t* bytecode, std::size_t
     if (tjs__eval_bytecode(ctx_, bytecode, len, true) != 0)
         return -1;
 
+    // A module the embedder's exit hook ended counts as evaluated above, and no JS may run after the exit: not even the
+    // load event below, whose listeners are the session's code. The hook's status is what the embedder returns to its
+    // loop with.
+    if (TJS_ExitTaken(TJS_GetRuntime(ctx_)))
+        return 0;
+
     // Use globalThis rather than `window`: upstream txiki dropped the
     // window/global/self browser-compat aliases, and the host runtime (CLI)
     // does not install them. dispatchEvent lives on globalThis either way.
````

````patch
diff --git a/packages/native/cli/main.cpp b/packages/native/cli/main.cpp
index 634e9ff..0d52e37 100644
--- a/packages/native/cli/main.cpp
+++ b/packages/native/cli/main.cpp
@@ -306,7 +306,9 @@ int main(int argc, char** argv) try {
     } else {
         rc = host.evalModuleBytecode(rp_cli_bundle, rp_cli_bundle_size);
     }
-    if (rc != 0) {
+    // A session that took its exit through the hook has already unwound: an evaluation path that still reports a
+    // failure for it (one this launcher does not know about) must not print "session eval failed" over a clean exit.
+    if (rc != 0 && !pump.exitTaken()) {
         std::fprintf(stderr, "session eval failed\n");
         return 1;
     }
@@ -330,8 +332,16 @@ int main(int argc, char** argv) try {
                 std::signal(SIGINT, SIG_DFL);
                 JSValue r = JS_Call(ctx, g_onInterrupt, JS_UNDEFINED, 0, nullptr);
                 if (JS_IsException(r)) {
-                    tjs_dump_error(ctx);
-                    hookThrew = true;
+                    // The hook may have ENDED the session itself (a live bridge that releases its devices and then
+                    // calls tjs.exit(130), say): the exception is then the exit hook's uncatchable marker - the
+                    // unwinding, not an error - so it is cleared, no marker is printed, and the process ends with the
+                    // hook's own code. Anything else out of the hook is a throw, and ends the session as one.
+                    if (pump.exitTaken()) {
+                        JS_FreeValue(ctx, JS_GetException(ctx));
+                    } else {
+                        tjs_dump_error(ctx);
+                        hookThrew = true;
+                    }
                 }
                 JS_FreeValue(ctx, r);
                 JS_FreeValue(ctx, g_onInterrupt);
````

- [ ] **Step 9: build and run them, and see them pass**

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build --target retroplug-host retroplug-cli -j$(nproc)
timeout -k 10 600 .superpowers/kb3plan/guarded.sh tools/run-cli-ts-tests.sh
timeout -k 10 300 .superpowers/kb3plan/guarded.sh node packages/retroplug/scripts/run-native-tests.mjs session-pump
```

Expected: PASS, section 6h included, and session-pump still 5 of 5:

```
ok   - an interrupt hook that calls tjs.exit ends the session with its code
ok   - ...and never shows the exit's marker
ok   - --version exits 0
ok   - ...and says nothing on stderr
ok   - --help exits 0
ok   - a bare call exits 2
ok   - an unknown command exits 2
ok   - a synchronous tool's own exit code stands (lsdj-rom info with no argument: 1)
ok   - ...and the job queued behind it never ran
PASS: retroplug-cli TypeScript harness
```

111 `ok` lines, no `FAIL`. By hand: `--version` exits 0 with 7 bytes on stdout and an empty stderr, the bare call exits
2, `no-such-command` exits 2, `lsdj-rom info` exits 1 printing only `ERROR: usage: lsdj-rom info <rom> [--json]`, and
the kept-alive session whose hook calls `tjs.exit(5)` (driven with the script's own `wait_ready`/`kill -INT` legs)
exits 5 with nothing on stderr.

- [ ] **Step 10: the wider suites this task reaches, and the new surface it opens**

Every one of the test host's 177 files now runs the full teardown on exit for the first time (the audio driver service,
`clearGraph`, the three providers, the Engine's render pool), so this is the acceptance:

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:native
```

```
# 177 native test file(s) passed (jobs=16)
#   129 case(s) SKIPPED in 59 file(s): ...
```

Per-file wall time against the pre-batch run (`.superpowers/kb3plan/native-timings-before.json`, 199 common files): the
runner records each file in `packages/retroplug/.test-timings/native.json`, and no file regressed materially. The
whole suite under AddressSanitizer with leak detection, which is what a teardown that runs for real can break. **Rebuild
the instrumented host FIRST**: the binary was last built in Task 7, and running the suite against it would exercise none
of this task's teardown for all but one file (and would fail the launchpad child on the old exit). The build runs
outside the guard (a build opens no device; its instrumented codegen tools refuse a preload), and every variable is
passed INLINE on the guarded command so nothing leaks into a later step:

```
cmake --build /workspaces/RetroPlug-main/cuttlefish/build-asan -j$(nproc) --target retroplug-host
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh env \
    RETROPLUG_HOST=/workspaces/RetroPlug-main/cuttlefish/build-asan/bin/retroplug-host \
    ASAN_OPTIONS=abort_on_error=1:detect_leaks=1:verify_asan_link_order=0 \
    node packages/retroplug/scripts/run-native-tests.mjs
```

```
# 177 native test file(s) passed (jobs=16)
```

and the ThreadSanitizer legs (the host slugs, then the three Catch2 binaries the script builds under TSan, the midi
package's stream cases in both modes among them):

```
timeout -k 10 2400 .superpowers/kb3plan/guarded.sh tools/run-sanitizer.sh thread
```

```
==> host passed under thread sanitizer
```

`pnpm test:cli-ts` again, the stock `tjs` binary (no hook: the old exit), and the whole build (the plugin and the SDL
standalone link txiki and install no hook):

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:cli-ts
mkdir -p /workspaces/RetroPlug-main/cuttlefish/.superpowers/kb3exec
printf 'tjs.exit(4);\n' > /workspaces/RetroPlug-main/cuttlefish/.superpowers/kb3exec/tjsexit.js
timeout -k 10 60 .superpowers/kb3plan/guarded.sh /workspaces/RetroPlug-main/cuttlefish/build/dpfjs/deps/lv_binding_js/deps/txiki/tjs run /workspaces/RetroPlug-main/cuttlefish/.superpowers/kb3exec/tjsexit.js; echo "stock tjs exit=$?"
timeout -k 10 2400 .superpowers/kb3plan/guarded.sh cmake --build /workspaces/RetroPlug-main/cuttlefish/build -j$(nproc)
```

```
PASS: retroplug-cli TypeScript harness
stock tjs exit=4
[100%] Built target retroplug-sdl
```

Never RUN `retroplug-sdl`: building it is the check. `deps/dpf.js`'s change needs `build/dpfjs`'s own txiki and dpf-widgets
rebuilt, which the target above does.

- [ ] **Step 11: the fork's branch, the three pointer bumps, then this repository's commit**

Nothing is pushed. Each of the next three commits is in the submodule above it, in this order, and each of the three
repositories gets a LOCAL branch of its own, so a commit never lands on a detached HEAD (the fork is detached at
`e39158c`) and `deps/dpf.js`'s `reflectcpp-0.26` (which `feature/web` also bumps) is not moved:

```
T=/workspaces/RetroPlug-main/cuttlefish/deps/dpf.js/deps/lv_binding_js/deps/txiki
git -C "$T" switch -c retroplug-exit-hook
git -C "$T" add src/tjs.h src/private.h src/mod_os.c src/eval.c src/utils.c src/vm.c
git -C "$T" commit -F - <<'MSG'
feat: an embedder exit hook, so tjs.exit can unwind instead of ending the process

tjs.exit called C exit() from inside the JS call, so an embedder driving the loop by hand never got its frame back: the
process's atexit handlers and static destructors ran, and nothing else did, so a launcher's teardown (a claimed control
surface's close bytes among it) never ran on a normal exit, and an error thrown in the same turn was lost with the exit.

TJS_SetExitHook installs a hook: tjs.exit then tells it the status and whether an exception was already pending, stops
the loop, and throws an uncatchable error so the JS stack unwinds - try, catch, finally, async functions and promise
jobs - and the embedder returns from its own loop. The exit is terminal: TJS_ExitTaken reports it, and once it is taken
the microtask drain, the unhandled-rejection dispatch, a timer's handler and a module's load event all return without
running JS, while a module whose promise rejects with the exit's own marker counts as evaluated rather than failed. With
no hook the old exit() is the whole body, so a runtime that wants the process to end still gets exactly that.
TJS_StopRequested exposes a stop asked for in the turn the loop is being stopped, which the async handle cannot report.
MSG

L=/workspaces/RetroPlug-main/cuttlefish/deps/dpf.js/deps/lv_binding_js
git -C "$L" switch -c retroplug-exit-hook
git -C "$L" add deps/txiki
git -C "$L" commit -F - <<'MSG'
chore: txiki with the embedder exit hook

The hook is what lets a launcher's tjs.exit unwind to its own frame instead of ending the process inside the call.
MSG

D=/workspaces/RetroPlug-main/cuttlefish/deps/dpf.js
git -C "$D" switch -c retroplug-exit-hook
git -C "$D" add deps/lv_binding_js src/dpfjs/host/TjsHostRuntime.cpp
git -C "$D" commit -F - <<'MSG'
chore: lv_binding_js with txiki's embedder exit hook, and no load event after an exit

The hook is txiki's; the host change is what matches it. evalModuleBytecode fires the bytecode module's synthetic load
event after evaluating it, and once an exit has been taken that event's listeners are JS running after the session
ended: the module counts as evaluated and main unwinds.
MSG
```

Then this repository's commit:

```
git -C /workspaces/RetroPlug-main/cuttlefish add deps/dpf.js packages/native/src/host/SessionPump.hpp \
    packages/native/src/host/SessionPump.cpp packages/native/src/main.cpp packages/native/cli/main.cpp \
    packages/retroplug/test-native/session-pump.test.ts tools/run-cli-ts-tests.sh
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
feat(kb3): let a session's exit unwind, and run the teardown

txiki freezes tjs before an embedder can wrap it, and its tjs.exit called C exit() from inside the JS call, so main's
frame never unwound: the CLI's Teardown and rp_stop_providers never ran on a normal exit, a Launchpad a session claimed
stayed in Programmer mode with its front panel locked, and an error thrown in the same turn as tjs.exit(0) was lost with
exit 0. The fork now has an embedder exit hook: with one set, tjs.exit records the status, stops the loop and throws an
uncatchable error, which unwinds through try, catch, finally, async functions and promise jobs, so both hosts return
from their own loop and their teardown runs - the path a thrown session and Ctrl-C already took. The hook is told what
was pending when exit was called, so a same-turn error makes the exit 1 and is printed, and it asks txiki's stop flag,
so an error a sibling callback's try/catch cleared still fails the session. The exit is terminal: nothing runs JS once
it has been taken, and a module whose evaluation rejected with the exit's own marker (every one of the CLI's compiled-in
dispatcher's synchronous exits is one) counts as evaluated, so a help print or a version print is not a failed session.
Hosts that install no hook (the plugin, the SDL standalone) keep exit() unchanged. Bumps the nested pointer chain from
the fork up to deps/dpf.js.
MSG
```

---

## Task 9: the documents record the batch

No behaviour: [spec/07](../../spec/07-remaining-work.md) loses the eight known-bug entries and their checklist lines,
and the specs that describe what changed gain it - spec/13 the MIDI stream's writer and the exit's close bytes,
spec/14 what a session's exit runs, that a same-turn error is reported, that a Ctrl-C hook which exits is the exit,
and the promise-executor limit that lets code after `new Promise(() => tjs.exit())` still run, spec/02 what the test
host is now (the
shared pump, the exit hook, the code back from `main`), spec/06 the midi test binary's cases and the fork's patch in
the dpf.js seam and why address mode runs outside the guard's build, plus spec/00's package row. spec/02's bullet
still describes the dead `tjs.exit` override and links the spec/07 entry this task deletes, and `pnpm check:docs`
cannot catch it (its link checker skips `#heading` fragments). There is no failing test for a documents task; its
check is `pnpm check:docs` (which fails on a link to a file that does not exist) and `pnpm typecheck` for the code
it names.

**Files** (line numbers are the file at `080c0c0ba`):

- Modify: `spec/07-remaining-work.md:32-46` (eight checklist lines), `:271-345` (eight entries).
- Modify: `spec/13-subsystems.md:154-160` (the Launchpad's exit), `:214-219` (`retroplug-sdl`'s threaded MIDI out).
- Modify: `spec/14-hosts-harnesses.md:215-226` (the same-turn error is reported), `:233-235` (the exit section it adds).
- Modify: `spec/06-build-test.md:74-80` (`retroplug-midi-test`'s cases), `:987-992` (the fork's one patch),
  `:1119-1124` (address mode under the device guard).
- Modify: `spec/02-native-host.md:86-89` (the test host's bullet: the pump, the hook, the exit code, and no link to
  the spec/07 entry spec/07 loses).
- Modify: `spec/00-overview.md:97-103` (the midi package's row).

**Interfaces:**

- Consumes: the anchors the other specs already have, and the paths the eight tasks created.
- Produces: nothing. Every link it adds resolves; the entry it removes is the one spec/14 used to cite for a lost exit,
  which is why the two edits are in one patch.

### Steps

- [ ] **Step 1: apply the docs patch**

````patch
diff --git a/spec/00-overview.md b/spec/00-overview.md
index f033f14..5b22857 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -97,7 +97,7 @@ system routed to one pair by its audio routing.
 | [`packages/sameboy/`](../packages/sameboy) / [`packages/mesen/`](../packages/mesen) | The consoles, each beside its TS: `SameBoySystem` and the Game Boy node (`rp.sameboy.gb`); the NES, GBA and SMS/GG systems, their roles, the emulated N8 FIFO, their build functions and nodes (`rp.mesen.nes` / `.gba` / `.sms`). Their TS is the console descriptor the shell classifies a ROM with and places a system by (its detectors' rank, extensions, settings, split modes, link cable and real cart), plus their roles, catalogs and protocol nodes. `mesen` and `n8` both declare `n8-wire` (the reliable link's header and the expansion-volume range) and `system`; `sameboy` declares `system`. | The products that list them. |
 | [`packages/host-io/`](../packages/host-io) | The host's own I/O as node types (`rp.host.midi-in` / `rp.host.midi-out`) and the engine service they read, `rp.engine.host-io/3`, with the staging bounds the engine sizes from. | Every desktop product. |
 | [`packages/audio/`](../packages/audio) / [`packages/audio-portaudio/`](../packages/audio-portaudio) / [`packages/audio-sdl/`](../packages/audio-sdl) | The SDL standalone's audio: the backend contract (`AudioBackend` / `AudioStream`), the null device and `AudioHost` (the one open/start/stop/reconfigure sequence, the muted fallback with a status, render-ahead, the no-pull watchdog), plus config.json's `audio` section and the Settings > Audio rows; and PortAudio and SDL2 audio as backends of their own, linked only by a product whose `AUDIO_BACKENDS` names them (`retroplug-sdl` does; the CLI, the headless host and the Node addon name none). | `retroplug-sdl`; the packages' TS half by the desktop products. |
-| [`packages/midi/`](../packages/midi) | The host's MIDI: `MidiBackend` / `MidiPort`, RtMidi under one `systemMidi()`, `FakeMidiBackend`, the one copy of the port filters and `MidiStream` (the receive ring, the output set swapped with no audio stop, the control surface's reservation and the scan's held edge); the project's `midi` section, the Settings > MIDI rows and the MIDI Clock submenu; and `MidiRpcService` as the provider a product starts by naming `midi`. | The desktop products (`retroplug-sdl` uses the stream directly and starts no facet); the plugin lists neither this nor the audio packages. |
+| [`packages/midi/`](../packages/midi) | The host's MIDI: `MidiBackend` / `MidiPort`, RtMidi under one `systemMidi()`, `FakeMidiBackend`, the one copy of the port filters, `MidiStream` (the receive ring, the output set swapped with no audio stop, the control surface's reservation and the scan's held edge) and `PortWriter`, one hardware output's writer thread (or an inline write, for the hosts whose caller is the JS thread); the project's `midi` section, the Settings > MIDI rows and the MIDI Clock submenu; and `MidiRpcService` as the provider a product starts by naming `midi`. | The desktop products (`retroplug-sdl` uses the stream directly, in its `Threaded` mode, and starts no facet); the plugin lists neither this nor the audio packages. |
 | [`packages/node-sdk/`](../packages/node-sdk) | The node SDK (E1): the C ABI (`rp_node.h`), `node.hpp` built for the desktop or, with `RP_DEVICE`, for the RP2350, the descriptor and canonical-JSON support library, the fixed-capacity settings types (`rp::text`, `rp::list`, `rp::choice`), the msgpack settings decode and converter, and `SpscRing`. A node pack depends on this alone. | Shared by every package. |
 | [`packages/graph-runtime/`](../packages/graph-runtime) | The executor (`retroplug-graph-exec`, no heap, no exceptions), the planner, the registry, resources and `GraphHost` (`retroplug-graph`), the plan blob's `exportPlan`, loader and `retroplug-export-plan` tool (E3), and `device-check/`, the RP2350 image that proves the portable nodes link under a heap-and-exception trap and, when flashed, that a plan loads and runs. | Shared by every host. |
 | [`packages/launchpad/`](../packages/launchpad) / [`packages/lsdj-launchpad/`](../packages/lsdj-launchpad) | The Launchpad pack: the protocol, the session and its `rp.controller.session` node, the port scan and profiles, the provider and the facet, the app registry, the `controllerApps` contribution kind, the project's `controller` section, its service and the graph placer that places them - and MI.MAP, the app that drives LSDj through it (the bridge beside the pack, which imports it and `lsdj`, never the reverse). | The desktop products alone: the plugin's `plugin` list carries neither, so a DAW host is handed no control surface. |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 13fef62..dc48311 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -83,10 +83,12 @@ All three hosts compose the services, mount their facets on an rpcpp server over
   ([`SharedDSP`](../packages/native/plugin/PluginShared.hpp#L21)),
   so the UI reaches the backend through the already-bound `__rpcSend` — no second RPC bridge.
   The UI and window-geometry seams are 03's topic.
-- **Native test host**: [`main.cpp:107-151`](../packages/native/src/main.cpp#L107-L151) binds the same namespace, tries
-  to install a `globalThis.tjs.exit` hook that records the exit code (it does not take: txiki's `tjs` is frozen, so the
-  exit is txiki's own, [spec/07](07-remaining-work.md#the-test-host-drops-an-uncaught-error)), evals a TS bundle, and
-  pumps the job loop until the harness reports TAP. This is the `test:native` runner (`retroplug-host`).
+- **Native test host**: [`main.cpp:107-151`](../packages/native/src/main.cpp#L107-L151) binds the same namespace,
+  constructs the shared [`SessionPump`](../packages/native/src/host/SessionPump.cpp) before it evals a TS bundle (the
+  stop takeover, the uncaught-error report, and txiki's exit hook, so a session's `tjs.exit` unwinds to `main` and the
+  exit code comes back from it: [spec/14](14-hosts-harnesses.md#a-sessions-exit)), pumps the job loop until the harness
+  reports TAP, and returns that code once its own teardown has run. This is the `test:native` runner
+  (`retroplug-host`).
 - **Headless UI-test host**: [`UiHarness`](../packages/native/test/ui/UiHarness.hpp#L30)
   is the plugin's control-plane bring-up **minus the audio thread, plus a display**. Because
   there is no audio thread, its
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index f37b842..911a0c9 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -74,7 +74,7 @@ CMakeLists of [node-sdk](../packages/node-sdk/native/CMakeLists.txt),
 | `retroplug-cp-bundle` / `-ui-bundle` / `-cli-bundle` / `-ts-stripper-bundle` / `-render-worker-bundle` / `-script-bundle` / `-script-test-bundle` / `-cli-sdk-assets` | custom | `build/native/*bundle_data.c`: control-plane / React-UI / CLI / TS-stripper / render-worker / script-node bytecode (the script nodes every Engine loads, [packages/native/cmake/bundles.cmake](../packages/native/cmake/bundles.cmake); the test twin's toy nodes, embedded in `retroplug-engine-graph-test` alone) and the CLI SDK assets, embedded in their consumers. | cp: yes; rest: via dependents |
 | `retroplug-example-session` | custom | The CLI example sessions under `build/cli/`. | yes |
 | `retroplug-render-host-test` | executable | A manual smoke of engine jobs on `rp::host::OfflineJobs` ([11-ui-rendering.md](11-ui-rendering.md)): `<job-json>` runs one render (and is `cmp`-identical to `retroplug-cli render`), `--parallel <j>...` runs N concurrently on one pool, `--cancel <j>` cancels mid-render. Not a CI step (`[engine-jobs]` carries that coverage). `EXCLUDE_FROM_ALL`. | no |
-| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` / `-audio-host-test` | executable | Fourteen of the sixteen Catch2 binaries `pnpm test:plugin` builds and runs (the other two are the N8 pack's, the next row). Each is made by `retroplug_add_test_binary` ([cmake/test-binary.cmake](../cmake/test-binary.cmake)) and links `Catch2::Catch2WithMain`. `EXCLUDE_FROM_ALL`; the console packages', the midi package's and the launchpad pack's are declared in their own `native/CMakeLists.txt` (`retroplug-midi-test` holds `MidiStream` over `FakeMidiBackend`, the port filters and the ring; `retroplug-audio-host-test` holds `AudioHost` over `FakeAudioBackend` and the run-override rule). | no |
+| `retroplug-plugin-test` / `-dynparams-test` / `-classid-test` / `-audio-test` / `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test` / `-audio-host-test` | executable | Fourteen of the sixteen Catch2 binaries `pnpm test:plugin` builds and runs (the other two are the N8 pack's, the next row). Each is made by `retroplug_add_test_binary` ([cmake/test-binary.cmake](../cmake/test-binary.cmake)) and links `Catch2::Catch2WithMain`. `EXCLUDE_FROM_ALL`; the console packages', the midi package's and the launchpad pack's are declared in their own `native/CMakeLists.txt` (`retroplug-midi-test` holds `MidiStream` and `PortWriter` over `FakeMidiBackend`, the port filters and the ring - a slow port's write off the caller's thread, a slow port not delaying a fast one, a full ring dropping whole messages, a stop's flush and its deadline, and neither a push nor a threaded `send` allocating, all of them in the thread sanitizer leg of [run-sanitizer.sh](../tools/run-sanitizer.sh) too, and with the five concurrency cases (the exclusive port's re-pick, a close under a send in flight, an output swap under a send, a diff's close before its open, a swap behind back-to-back sends) run in BOTH output modes through `GENERATE`; `retroplug-audio-host-test` holds `AudioHost` over `FakeAudioBackend` and the run-override rule). | no |
 | `retroplug-n8-test` / `-serial-test` / `-n8-hwtest` | executable | The Everdrive N8 pack's protocol and serial checks (`packages/n8/native/test/`), the first two `pnpm test:n8`'s; `-hwtest` needs the physical console and links no Catch2 (`NO_CATCH2`). `EXCLUDE_FROM_ALL`. | no |
 
 The plugin's identity is vendor-owned (not the DPF example namespace):
@@ -987,6 +987,17 @@ nested `deps/dpf.js` submodule (documented in its own README), consumed through
 4. **Use of `${DPFJS_PATH}`**: the shell's `retroplug-tjs-runtime` library compiles the shared txiki host `TjsHostRuntime.cpp` and links `tjs`; the backend takes the bare QuickJS headers (`quickjs.h`) and links `qjs`; the plugin embeds the generic `dpf-widgets/generic/LVGL.cpp` and links `dpfjs::core` + `lvgl-js-native` + `retroplug-tjs-runtime`. All in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.txt).
 5. **React resolution** — the UI ([ui/main.tsx](../packages/retroplug/ui/main.tsx)) is bundled by the shared [tools/build-ui.js](../tools/build-ui.js), which resolves `react`/`react-reconciler`/`lvgljs-ui` from dpf.js's own pnpm workspace under `deps/dpf.js/deps/lv_binding_js/node_modules`.
 
+**The txiki fork's one addition.** The nested `deps/dpf.js/deps/lv_binding_js/deps/txiki` is a fork of `tommitytom/txiki.js`
+(MSVC fixes on top of upstream), and RetroPlug carries one patch of its own there: the embedder exit hook
+(`TJS_SetExitHook` / `TJS_StopRequested` / `TJS_ExitTaken`, `src/tjs.h`, `src/private.h`, `src/mod_os.c`,
+`src/eval.c`, `src/utils.c`, `src/vm.c`), which is what lets a session's `tjs.exit` unwind instead of ending the
+process inside the call ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)); `deps/dpf.js`'s own
+`TjsHostRuntime.cpp` matches it, skipping the bytecode module's `load` event once an exit has been taken (the module
+counts as evaluated, and no JS runs after the exit). A pointer bump reaches all of that in three commits - txiki, then
+`dpf.js/deps/lv_binding_js`, then `deps/dpf.js` - because each nested submodule records the one below it;
+nothing is pushed without the owner's word, and until the fork's commit is pushed CI cannot check out the RetroPlug one.
+With no hook installed (the plugin, the SDL standalone, the stock `tjs`) the exit is upstream's `exit()`.
+
 The shared C++ contract (`SystemBase`, the capabilities, the build request) is `retroplug-system`'s, and each console's
 core is its package's (`retroplug-sameboy`, `retroplug-mesen`); the transport primitives the cores and the host share
 are `system`'s too (`packages/system/native/src/transport/`), while the shell keeps `SampleCache`, `FileSync` and
@@ -1119,6 +1130,31 @@ and runs `retroplug-launchpad-test`, `retroplug-midi-test` and `retroplug-audio-
 the native slugs. `retroplug-device-mode-test`'s `[mutation]` bounds checks bite only under `build-asan`, which is
 run by hand: no CI or `test:plugin` leg builds that binary there.
 
+**Address mode under the device guard.** A harness that can reach a device runs under
+[tools/sdl-audio-guard](../tools/sdl-audio-guard) with the guard in `LD_PRELOAD`, and an ASan-instrumented binary refuses
+to start with a preload ahead of its runtime (`ASan runtime does not come first in initial library list`). Two
+consequences: the configure and build steps run OUTSIDE the guard (a build opens no device; the instrumented codegen
+tools it runs trip the same check), and the runs set
+`ASAN_OPTIONS=abort_on_error=1:detect_leaks=1:verify_asan_link_order=0`, which is ASan's own switch for a deliberate
+preload order. The run is still guarded in the sense that matters: the guard's interposers are loaded, and its
+self-test passes before the binary starts. `tools/run-sanitizer.sh address` cannot be used unchanged under the guard for
+that reason: run its two steps by hand.
+
+```
+# 1. configure and build, OUTSIDE the guard (a build opens no device; its instrumented codegen tools trip the check)
+cmake -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DRETROPLUG_SANITIZE=address \
+    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
+cmake --build build-asan -j$(nproc) --target retroplug-host
+
+# 2. run guarded, with ASan's link-order check off for the deliberate preload
+TMP="$(mktemp -d)"; REPO_DIR="$PWD"
+. tools/sdl-audio-guard/guard-env.sh "$TMP/guard"          # (it PROVES the guard is live, and exits if not)
+export NOSND_LOG="$TMP/nosnd.log"
+export RETROPLUG_HOST="$PWD/build-asan/bin/retroplug-host"
+export ASAN_OPTIONS="abort_on_error=1:detect_leaks=1:verify_asan_link_order=0"
+node packages/retroplug/scripts/run-native-tests.mjs dsp-lifecycle
+```
+
 ## Build gotchas
 
 - **`deps/sameboy` is patched at configure, twice.** `cmake/sameboy.cmake` applies two tracked patches idempotently
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index d2db999..1072ba3 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -32,15 +32,7 @@ than work.
 - [ ] [The JACK standalone's control handoff](#the-jack-standalones-control-handoff)
 - [ ] [A cart-node handover can cut a key](#a-cart-node-handover-can-cut-a-key)
 - [ ] [The N8 menu, unverified on a cart](#the-n8-menu-unverified-on-a-cart)
-- [ ] [The stripper bundle outside the repository](#the-stripper-bundle-outside-the-repository)
 - [ ] [A bar or beat quantise counts host bars](#a-bar-or-beat-quantise-counts-host-bars)
-- [ ] [MidiStream::send blocks the audio callback](#midistreamsend-blocks-the-audio-callback)
-- [ ] [A MIDI pick dirties the untitled project](#a-midi-pick-dirties-the-untitled-project)
-- [ ] [Settings > MIDI misses a claim made while open](#settings--midi-misses-a-claim-made-while-open)
-- [ ] [The audio draft's sibling-backend cache](#the-audio-drafts-sibling-backend-cache)
-- [ ] [An Apply past a read-only config](#an-apply-past-a-read-only-config)
-- [ ] [A normal CLI exit skips the teardown](#a-normal-cli-exit-skips-the-teardown)
-- [ ] [The test host drops an uncaught error](#the-test-host-drops-an-uncaught-error)
 
 **Engine**
 - [ ] [DAW automation of node params](#daw-automation-of-node-params)
@@ -271,75 +263,12 @@ And a heavy stream into a menu can starve the probe: the window's 48-frame re-sy
 send, so past about 190 messages a second (48 per 250 ms) no turn is quiet and a submission is refused. ([C5
 design](../docs/modular-graph-phase-c5-design.md) section 11.)
 
-### The stripper bundle outside the repository
-
-`retroplug-ts-stripper-bundle` fails in a build directory outside the repository:
-[tools/build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) runs in the binary directory and sets no esbuild
-`absWorkingDir`, so its `typescript-5` alias resolves from there (unlike [tools/build-ui.js](../tools/build-ui.js),
-which sets it to the repo root). ([B3a design](../docs/modular-graph-phase-b3a-design.md) section 10.)
-
 ### A bar or beat quantise counts host bars
 
 A quantise of `bar` or `beat` still counts the host's bars: at a tempo divisor of 2 a cart's phrase lasts two of them.
 It may be the behaviour wanted; decide, then fix it or state it in [spec/13](13-subsystems.md#the-controller-role).
 ([Known bugs batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)
 
-### MidiStream::send blocks the audio callback
-
-`MidiStream::send` runs every selected hardware output's send inline on the audio callback, so a WinMM bulk SysEx of
-about 170 ms (a controller app's LED traffic) blocks the audio thread. The fix is a writer thread per hardware output,
-as the Launchpad link has. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)
-
-### A MIDI pick dirties the untitled project
-
-A MIDI pick on the start screen dirties the empty untitled project (`ProjectStore.setSection` marks it dirty
-unconditionally), so the close guard asks to save a project holding only the pick; exempt a section-only change to an
-empty, pathless project. (Found by the final review that [F5b design](../docs/modular-graph-phase-f5b-design.md) section
-14 summarises; recorded only here.)
-
-### Settings > MIDI misses a claim made while open
-
-The row's reserved pair is a `describe()` snapshot, so Settings > MIDI's checklist does not notice a claim made while
-its container is open; a cheap, no-probe `state()` read on `MidiDeviceHost` would fix it. (Found by the final review
-that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)
-
-### The audio draft's sibling-backend cache
-
-The audio draft's sibling-backend cache is per backend change, not per visit
-([audioDraft.ts](../packages/audio/ts/ui/audioDraft.ts) caches by the described backend). (Found by the final review
-that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)
-
-### An Apply past a read-only config
-
-`UserConfigStore.setSection` answers false when it leaves a read-only config alone, but the UI host's
-`config.setSection` drops that answer (`void`, [uiHost.ts](../packages/retroplug/ui/host/uiHost.ts)) and
-`applyAudioDraft` calls `audioDevice.apply` regardless, so an Apply still reaches the device. (Found by the final review
-that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)
-
-### A normal CLI exit skips the teardown
-
-`retroplug-cli`'s override of `tjs.exit` never takes: txiki's `tjs` is frozen, so the set in
-[cli/main.cpp](../packages/native/cli/main.cpp) fails (the launcher clears the exception it leaves) and a session's
-`tjs.exit` is txiki's own, which ends the process at once. So `g_exit` is never set and the launcher's `Teardown`
-(`engine.clearGraph()`, then `rp_stop_providers`) never runs on a normal exit: the plans are not reclaimed while the
-providers' links are alive, and the providers are not stopped. The fix needs an exit the launcher sees (a `tjs.exit`
-wrapped before txiki freezes `tjs`, or an exit hook) and a look at what each provider's stop does at a process's end.
-The same exit also loses an error: one thrown in the same loop turn as a `tjs.exit(0)` is never reported, since the
-process ends inside txiki's timers phase, before the launcher looks, and exits 0 with nothing printed.
-([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 2.)
-
-### The test host drops an uncaught error
-
-The test host (`retroplug-host`, [src/main.cpp](../packages/native/src/main.cpp)) swallows a top-level throw: it
-pumps txiki's loop without looking for a pending exception or a stop request, so an error nothing caught outside the
-harness's reach (a module body's throw before its first `test()`, a timer created before it) is dropped, and the file
-ends as "tests did not complete (tjs.exit never called)" with no reason, which hides a fixture script's real error (the
-renders' fixtures meet it), or with a kept-alive one only when its 30-minute deadline runs out. Its `tjs.exit` override
-does not take either. The fix is the CLI's `pumpOnce` (which handles this fault), with the failed `tjs.exit` set's
-pending exception cleared as the CLI clears it (`cli/main.cpp`), then a whole `pnpm test:native` run for the strays
-it surfaces. ([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 3; [D3
-design](../docs/modular-graph-phase-d3-design.md) section 10.)
-
 ## Engine
 
 ### DAW automation of node params
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index 23245fd..069c824 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -154,7 +154,11 @@ reaches a surface, and it names no launchpad provider.
   `launchpad` and `lsdj` and which `launchpad` never imports. The provider is `rp_launchpad_provider`, and the facet
   is TS's only door to a port, through `createLaunchpadClient(call)` in `@retroplug/launchpad/client`.
   `ControllerLink`'s writer thread drains the send ring and a UI queue of its own, so `send()` waits for no write in
-  flight: a push takes a lock the writer holds only for a swap. The pack's portable core is held to MicroQuickJS by the
+  flight: a push takes a lock the writer holds only for a swap. A session's EXIT is where the close bytes go out: the
+  provider's destroy disconnects the link, which writes the device's `exitToLiveMode` bytes (a pad left in Programmer
+  mode has its front panel locked) before the ports close. That is the CLI's teardown path, so it runs on a normal
+  `tjs.exit` as it does after a thrown session or Ctrl-C ([spec/14](14-hosts-harnesses.md#retroplug-cli-as-a-test-harness)).
+  The pack's portable core is held to MicroQuickJS by the
   device-pack toolchain's scenario mode (byte-identical transcripts under Node and `retroplug-mqjs-run`), but it
   declares no `script` member, so the scenarios are the proof and no box image carries it.
 - **Prove a change:** `retroplug-launchpad-test` (one of the sixteen `pnpm test:plugin` binaries),
@@ -214,6 +218,17 @@ submodules. On macOS the artifact binary is build-machine-only: the `SDL2.framew
 bundles, and `retroplug-sdl` is a bare executable, so shipping it would need a framework copy and an `@executable_path`
 rpath.
 
+MIDI OUT LEAVES THE AUDIO CALLBACK here, and only here: this app drains the Engine's MIDI out from the audio callback
+([audioLoop.cpp](../packages/native/sdl/audioLoop.cpp)), and a port's send can block until the bytes are on the wire
+(WinMM's SysEx path waits for a 538-byte Launchpad repaint, about 170 ms at DIN rate), so its `MidiStream` runs in
+`Threaded` mode. Each selected hardware output then has a `PortWriter` of its own
+([PortWriter.hpp](../packages/midi/native/src/midi/PortWriter.hpp)): `send` copies the message into a preallocated ring
+(all or nothing, so a SysEx is never torn) and wakes a thread that does the blocking write, and a message that does not
+fit is dropped whole and counted (`droppedOutputCount()`). A port leaving the selection, or the stream closing, stops
+its writer with a bounded FLUSH, because a dropped Note Off is a stuck note. The CLI's and the test host's MIDI facet
+stays `Inline` - the caller there is the JS thread, and a script relies on a returned `midiSend` being on the wire - and
+the virtual output is inline in both modes.
+
 At startup it holds MIDI and audio until the UI has reported: the control surface's `restored()` (only when it
 started the launchpad provider) and the `audio` section's apply. The wait is bounded in time, at 2000 ms
 (`kAudioBoundMs`) while the audio apply has not landed and 500 ms once it has, after which it goes on without what has
diff --git a/spec/14-hosts-harnesses.md b/spec/14-hosts-harnesses.md
index 5d061af..76f55d0 100644
--- a/spec/14-hosts-harnesses.md
+++ b/spec/14-hosts-harnesses.md
@@ -215,12 +215,14 @@ the pumps run out, and a script that waits on either calls it once, up front, an
 declared whole in [cli/sdk-types.d.ts](../packages/retroplug/cli/sdk-types.d.ts), which `cli/sdkSurfaceCheck.ts` holds
 to the implementation by type identity, not mutual assignability (which passes a dropped `readonly`).
 
-**An error nothing caught.** It ends a session, kept alive or not, with exit 1 and its text, when the launcher sees it before the session exits (an
-error thrown in the same loop turn as a `tjs.exit(0)` is lost: [spec/07](07-remaining-work.md#a-normal-cli-exit-skips-the-teardown)). txiki turns one (a throw in
+**An error nothing caught.** It ends a session, kept alive or not, with exit 1 and its text, including one thrown in the
+same loop turn as a `tjs.exit(0)`: txiki's exit hook is told what was pending when `exit` was called, and the launcher
+prints it before the unwinding ([A session's exit](#a-sessions-exit)). txiki turns one (a throw in
 an async module body, a rejection nothing cancelled, a throw in a timer) into a pending exception and a request to stop
 its loop, and the launcher ([packages/native/cli/main.cpp](../packages/native/cli/main.cpp)) drives that loop itself, so
-after every pump it looks for both: the exception, which it prints, and the stop request, which it takes over because it
-survives a sibling callback's try/catch clearing the exception. In a test file the harness
+after every pump it looks for both: the exception, which it prints, and the stop request, which it both takes over and
+asks txiki for (`TJS_StopRequested`, the flag set where a stop is asked for, since the handle is not serviced once the
+loop is being stopped) because it survives a sibling callback's try/catch clearing the exception. In a test file the harness
 ([packages/testing/ts/harness.ts](../packages/testing/ts/harness.ts), the one `pnpm test`, `test:native` and `test:ui`
 run too) takes over first, from its first `test()`: it records a rejection nothing handled, or a throw in a timer
 callback, prints it to stderr at once, fails the running case at once (each case is raced against an abort, so one
@@ -233,3 +235,23 @@ handled a loop turn late counts as unhandled, as in Node. A synchronous throw in
 ends it with exit 1 too, as an async one that rejects does. A session holding a device releases it on that path:
 `launchpad-probe` runs each timer body so that a throw reports, leaves Programmer mode and closes its ports before
 exiting 1. [tools/run-cli-ts-tests.sh](../tools/run-cli-ts-tests.sh) checks each of these through the real binary.
+
+**A session's exit.** `tjs.exit` is txiki's own and stays frozen (`src/js/core/index.js` freezes the global before
+`TJS_NewRuntime` returns), so a host does not wrap it: both hosts that drive the loop themselves install txiki's exit
+HOOK instead (`TJS_SetExitHook` in the fork at `deps/dpf.js/deps/lv_binding_js/deps/txiki/src/mod_os.c`, through
+[SessionPump](../packages/native/src/host/SessionPump.cpp)). With a hook, `tjs.exit(status)` records the status, stops
+the loop and throws an UNCATCHABLE error, so the JS stack unwinds - through try/catch/finally, out of async functions
+and promise jobs - and `main` returns: the CLI's `Teardown` (`engine.clearGraph()`, then `rp_stop_providers`) runs,
+`~AudioDriverRpcService` stops the audio thread, the providers stop in reverse order (a claimed Launchpad's link writes
+its close bytes and gives the device back), and the runtime is freed. That is the path a thrown session and Ctrl-C
+already took. A Ctrl-C hook that ends the session itself (`tjs.exit(130)` from `__rp_onInterrupt`, say) is the exit and
+not a thrown hook: the process ends with the hook's own code, and the exit's unwinding is not reported as an error. A
+host with no hook (the plugin, the SDL standalone, the stock `tjs` binary) keeps the old `exit()`: the process ends
+inside the call and no caller's stack unwinds. One limit is the engine's, not the hook's: quickjs-ng's promise
+constructor takes an executor's exception with `JS_GetException` and without the uncatchable check, so code after
+`new Promise(() => tjs.exit(0))` still runs synchronously until control returns to the C caller; nothing runs after
+that. `RETROPLUG_TEARDOWN_TRACE=1` makes the CLI print
+`teardown: graph cleared, providers stopped`, and the test host print every message its scripted Pro MK3 was sent plus
+`teardown: providers stopped`, which is how
+[test-native/session-pump.test.ts](../packages/retroplug/test-native/session-pump.test.ts) holds a claimed Launchpad's
+close bytes from a child process's exit.
````

- [ ] **Step 2: check the links and the tree**

```
git -C /workspaces/RetroPlug-main/cuttlefish add -N spec/00-overview.md spec/02-native-host.md spec/06-build-test.md \
    spec/07-remaining-work.md spec/13-subsystems.md spec/14-hosts-harnesses.md \
    packages/midi/native/src/midi/PortWriter.hpp packages/midi/native/src/midi/PortWriter.cpp \
    packages/midi/native/test/PortWriter.test.cpp packages/midi/native/test/AllocationCounter.hpp \
    packages/native/src/host/SessionPump.hpp packages/native/src/host/SessionPump.cpp \
    packages/retroplug/test-native/session-pump.test.ts
pnpm check:docs
```

Expected: PASS. The links spec/14 and spec/06 add name files this batch creates, and the checker treats an untracked
target as one that will not exist in a fresh clone, so the new files must be in the index (`git add -N` is enough;
the step-3 commit does it for real):

```
WARN  docs/launchpad-plan.md:299  dead link in a plan record  ../packages/retroplug/ui/screens/menu/midiDevices.ts

check-doc-links: 113 file(s) clean, 1 warning(s)
```

The one warning is a plan record's, and plan records are never edited.

```
git -C /workspaces/RetroPlug-main/cuttlefish diff --stat -- spec/
```

Expected: six files, the deletions in spec/07 and additions in the other five, and no line the batch did not
mean to touch:

```
 spec/00-overview.md        |  2 +-
 spec/02-native-host.md     | 10 ++++---
 spec/06-build-test.md      | 38 ++++++++++++++++++++++++-
 spec/07-remaining-work.md  | 71 ----------------------------------------------
 spec/13-subsystems.md      | 17 ++++++++++-
 spec/14-hosts-harnesses.md | 30 +++++++++++++++++---
 6 files changed, 86 insertions(+), 82 deletions(-)
```

- [ ] **Step 3: commit**

```
git -C /workspaces/RetroPlug-main/cuttlefish add spec/00-overview.md spec/02-native-host.md spec/06-build-test.md \
    spec/07-remaining-work.md spec/13-subsystems.md spec/14-hosts-harnesses.md
git -C /workspaces/RetroPlug-main/cuttlefish commit -F - <<'MSG'
docs(kb3): record the batch in spec/07, spec/13, spec/14, spec/06 and spec/02

spec/07 loses the eight entries and their checklist lines the batch settles. spec/13 gains what the SDL standalone's
MIDI out does (a writer per hardware output) and what a claimed Launchpad is owed on the way out. spec/14 says what a
session's exit runs, now that the exit hook makes main's frame unwind, and that a same-turn error is reported. spec/06
names PortWriter's cases in the midi test binary, describes the fork's one patch and the pointer order that carries it,
and records why address mode runs outside the device guard's build and with verify_asan_link_order off. spec/00's midi
row names PortWriter, and spec/02's test-host bullet stops describing a tjs.exit override that never took: the host
runs the shared pump and txiki's exit hook, and returns the code main got back.
MSG
```

---

## The whole batch, at the tip

After Task 9, with all nine commits, these are the checks the batch's own acceptance rests on (the controller runs the
verification kits: the serial kit, the parallel kit and the guarded SDL legs, none of which this plan runs):

```
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test            # 243 test file(s) passed, 1 skipped case
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:native     # 177 native test file(s) passed
timeout -k 10 600  .superpowers/kb3plan/guarded.sh pnpm test:ui         # 48 UI test file(s) passed
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:plugin     # 16 plugin test binary(ies) passed
timeout -k 10 1800 .superpowers/kb3plan/guarded.sh pnpm test:cli-ts     # PASS: retroplug-cli TypeScript harness
timeout -k 10 1200 .superpowers/kb3plan/guarded.sh pnpm typecheck       # Done, every package and the plugin product
pnpm check:docs                                                          # 113 file(s) clean, 1 warning
pnpm check:boundaries                                                    # 0 allowed by EXCEPTIONS
```

No run in this plan opens an audio, MIDI, serial or video device: the guard refuses those paths before the syscall and
the runs above logged no refusal.

**Windows.** Everything here was validated on `linux-x64-gnu13` only. Task 8's new C++ (the pump, the hook, the
writer's threads, a test binary's global `operator new`) and the whole native suite's teardown path are `/W4 /WX` code on
our own targets, and the three submodule commits are what a Windows CI run needs before it can check this batch out at
all: the controller's acceptance therefore includes the `windows-2025-vs2026` legs (`test.bat`, never bare
`pnpm test*`) once the fork's commit is pushed. A CI-only MSVC warning is fixed, not explained away.
