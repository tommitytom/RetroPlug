# Phase C1: script node packs, each in its own runtimes - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** The script host isolates, loads and replaces script node types by the **pack**: each pack runs in a QuickJS
runtime pair of its own (a control side for settings and layout, an audio side for its instances), the built-in bundle
is the first pack (`rp.script`), a pack can be loaded or reloaded from source while audio runs, and a node is charged
for its own promise jobs and its pack's memory limit, so a buggy node fails alone.

**Architecture:** Per [docs/modular-graph-phase-c1-design.md](modular-graph-phase-c1-design.md). `ScriptHost` holds
packs; everything per runtime (the `Block` class, the atoms, `Object.prototype`'s address, the types) is a pack's, and
everything shared between blocks (the call deadline, the counters, the release rings) stays the host's. A pack is built
entirely on the control thread and reached by the block thread only through an instance in a plan published after it,
so loading one never touches the audio thread; it joins the shared interrupt handler and counters only once its load
is done. A node's promise jobs run at a checkpoint after each of its calls, under that call's deadline. The control side
has a deadline of its own. A reload swaps the node registry's entries for a pack's new generation and replans the
running document, all or nothing; a retired generation is freed on the control thread once its last instance record
comes back. `Engine::loadScriptPack` is the entry point, `loadScriptPack` its RPC and `Backend` method.

**Tech stack:** C++20 with Catch2 (`retroplug-engine-graph-test`, `retroplug-graph-test`), the script host's bare
QuickJS (quickjs-ng, inside the `deps/dpf.js` submodule, which is not patched), TypeScript on txiki (`pnpm test` mock
tier, `pnpm test:native` real host), the graph layer of Phases A to B3b.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/c1-packs` in this worktree, on top of the design commit d55e34c9: five commits, one per task (Task 6 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A code block that creates a file is
that file as the prototype commit left it; one that changes files is the prototype's diff for them, so `git apply` it
from the step's text (or apply it hunk by hunk with the Edit tool). Every expected pass, test count and figure is what
the prototype's runs printed. A failing-test step's expected failure is what the missing code produces: for the C++
tasks a build that stops at the names the task adds (the prototype checked its red states with those names alone in
place, as each step says). When this plan was generated it was replayed in a scratch clone of d55e34c9, every block
written or applied in order and each task committed with the command given: each commit's tree came out exactly as the
prototype's, and each commit message as its; and on those replayed trees the mock-tier steps (`pnpm test`) were run,
printing the counts quoted. The native steps' counts are the prototype's. The prototype commits, by task:
- Task 1: `4a022854` refactor(script-host): a script host holds packs, each its own runtime pair; the built-in bundle is the first
- Task 2: `4978cabe` feat(script-host): a node's promise jobs run after its own call and are charged to it; each pack has a memory limit
- Task 3: `292e5462` feat(engine): Engine::loadScriptPack loads a pack of script node types into runtimes of its own while the engine runs
- Task 4: `a2914067` feat(engine): a pack reloads while the engine runs: its nodes rebuild on the new code, a refused replan rolls back, and a retired generation is freed off the audio thread
- Task 5: `9b5e7f5e` feat(backend): loadScriptPack over RPC and in the TS Backend, a test:native case, and the docs for script node packs

**Where this plan departs from the design.** The design as committed (d55e34c9) was written before the prototype; the
plan follows what was built, and Task 5's docs commit lands the design with its Status line, the controller's two
decisions in its section 2, section 11's questions answered in place, and a section 13, "Found while building", which
lists every departure. In short: ids under `rp.` are the app's, so a loaded pack may not take one; the control side's
calls into a pack (its load, its settings checks) are held to a deadline of their own, so a pack that hangs there is
refused rather than freezing the host's thread; a pack joins the shared interrupt handler and allocation counters only
once its load is done (the race Task 3's ThreadSanitizer run found otherwise), so a loaded pack's load, and a freed
generation's frees, are counted in no audio-side counter; the memory limit is enforced by the host's own allocator
(QuickJS's `JS_SetMemoryLimit` refuses before the allocator is called and keeps no record of a refusal JS caught); a
compile error carries its line only; a released node's leftover promise jobs are no node's; a refused reload's
generation is freed once its records come back; a reload whose running document places none of the pack's types skips
its replan; and a pack the size of the production bundle, compiled from source, keeps its debug information, so it
costs about 760 KB rather than the design's estimated 590.

**Ground rules for whoever executes this:**
- Commit after each task (the commit commands are given; Task 6 commits nothing unless a step fails and is fixed,
  in a commit of its own). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the
  session itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with
  uncommitted work in the tree. Never touch a submodule: `deps/dpf.js` (QuickJS lives inside it) is not patched by
  this plan.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- Parity holds the engine's output to the bit, and no task re-records anything: no production node uses a promise, and
  the built-in pack runs the same code in the same order. A strict parity failure is a real change to the output: stop
  and find it.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources`; without it their cases skip. Pass it on
  the same command line: a shell `export` does not survive between commands.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter: an exact
  slug (`parity/lsdj-modes`, `script-packs`) or a prefix in the dash form. Run several files as separate commands.
- The sanitizer trees are their own: `build-tsan/` (configured with `-DRETROPLUG_SANITIZE=thread`) and `build-asan/`
  (`-DRETROPLUG_SANITIZE=address`). Configure one only if its `CMakeCache.txt` is missing (`cmake -S . -B build-tsan
  -DRETROPLUG_SANITIZE=thread`); building a Catch2 binary there by name rebuilds what it needs. The profile build is
  its own tree too (`build-prof/`, `tools/run-profile.sh unit|stats`).
- A new C++ source (Task 3's test file) needs `cmake -S . -B build` before the build sees it.
- The prototype branch `proto/c1-packs` must stay until this plan has been executed.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks
  one at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper;
  pgrep -a jackd`).
- `pnpm check:docs` counts the tracked docs: 62 in the prototype, which had no plan file, so 63 once this plan is
  committed.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/host/script/ScriptHost.{hpp,cpp}` | packs, each its own runtime pair (Task 1); jobs charged at a checkpoint after each call, the per-pack memory limit (Task 2); `loadPack`, the namespace rule, the control-side deadline, `join` (Task 3); generations, record counts, retiring, discarding and freeing (Task 4); the memory-limit load message (Task 5) |
| `packages/native/test/engine-graph/ScriptNodes.test.cpp`, `ScriptRig.hpp` | the jobs and memory rules on one pack (Task 2) |
| `packages/native/src/host/dsp/ScriptCompiler.{hpp,cpp}` | the compile error's text and line; the parser's stack held to the script host's (Task 3) |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `loadScriptPack`, `scriptPacks`, `nodeTypeCount`, `setScriptCallDeadline` (Task 3); reload: the last published request, the replan and its rollback, the registry dropping freed generations (Task 4) |
| `packages/native/src/graph/NodeRegistry.{hpp,cpp}` | the registry's threading note (Task 3); `replacePackage`, `restorePackage`, `dropRetired` (Task 4) |
| `packages/native/test/engine-graph/ScriptPacks.test.cpp` | new: packs over an Engine (Tasks 3 and 4) |
| `packages/native/test/graph/Registry.test.cpp` | the registry's swap, undo and drop (Task 4) |
| `packages/native/src/host/rpc/{BackendTypes.hpp,EngineRpcService.{hpp,cpp},BackendRpcRegistration.hpp}` | `loadScriptPack` over RPC, on the `emulator` facet (Task 5) |
| `packages/retroplug/src/backend.ts`, `realBackend.ts`, `testing/mockBackend.ts` | `Backend.loadScriptPack`; the mock refuses (Task 5) |
| `packages/retroplug/test-native/script-packs.test.ts` | new: packs through the real host (Task 5) |
| spec/, docs/, AGENTS.md, `src/graph/defineNode.ts` | Tasks 2 and 5 |

---

### Task 1: packs inside the script host, the built-in bundle the only one

Design section 4. `ScriptHost` holds one control runtime and one audio runtime today, and with them everything per
runtime: the `Block` class and its frozen prototype, the atoms the block thread names properties by, the address of
`Object.prototype`, the types and their package, the garbage flag. All of it moves into a pack, and the host holds its
packs; the embedded bundle loads into the first, `rp.script`, as `load` always did. What every block shares stays the
host's: the call deadline, the allocation counters (every pack's audio allocator counts into them), the release rings,
the throw and live counts, the profile's windows. No behaviour changes, so no test is written first: the 123
engine-graph cases, strict parity and the ThreadSanitizer pass are this task's test.

**Files:**
- Modify: `packages/native/src/host/script/ScriptHost.cpp`, `packages/native/src/host/script/ScriptHost.hpp`

- [ ] **Step 1: Check the starting point**

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (18011 assertions in 123 test cases)` (the design commit's).

- [ ] **Step 2: Move the per-runtime state into a pack**

`ScriptHost::Pack` (declared in the header beside `Type` and `Instance`, defined in the .cpp) holds a pack's id,
generation, manifest and `rp_package`, both runtimes and contexts, the `Block` class, the atoms, the address of
`Object.prototype`, its types and its garbage flag. `makePack` / `makeRuntimes` make a pack's runtimes as the
constructor made the host's (the counted allocator, the stack limits, the interrupt handler with the host as its
opaque, the frozen `Block` class), `freeRuntimes` frees them, and `loadInto` is `load`'s old body, loading into a
given pack. A `Type` names its pack, and the Glue reaches the audio side through `packOf(in)`; the audio context's
opaque is the pack (a `Block` method finds its pack's class through it, and the host through `pack.host`). The span
labels come from a host-level map from type id to label, so a type id registered again keeps its label (the built-in
types keep 16 up in bundle order). The block thread never iterates the host's pack list, the one exception,
`endBlock`'s promise jobs, being Task 2's to remove:

````diff
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index df3217ef..738fe066 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -1,6 +1,7 @@
 #include "host/script/ScriptHost.hpp"
 
 #include <algorithm>
+#include <array>
 #include <cassert>
 #include <chrono>
 #include <cstdio>
@@ -41,7 +42,7 @@ namespace {
 using graph::PortKind;
 
 constexpr const char* kNodesGlobal = "__rp_scriptNodes";  // packages/retroplug/src/script/register.ts
-constexpr const char* kPackageManifest = R"({"id":"rp.script"})";
+constexpr const char* kBuiltInPack = "rp.script";          // the built-in pack's id, and its package's
 
 // The property names the block thread reads and writes, as atoms made once, so naming one allocates nothing.
 enum Atom : std::size_t {
@@ -52,16 +53,16 @@ constexpr const char* kAtomNames[kAtomCount] = {"frames", "transport", "sampleRa
                                                 "frame",  "data",      "flags",      "start",      "changes",      "value",
                                                 "length", "fault",     "process",    "onSettings", "portsChanged", "activate"};
 
-// How the host makes a property on an object it made, or a slot of an array JS holds that is not the array's own: an own
-// data property, writable, enumerable and configurable as a plain assignment would make it, a failure thrown. A define
-// never walks the prototype chain, so in the one realm every script node shares, an accessor one node (or the bundle)
-// hung on Object.prototype or Array.prototype never runs for another's; and a JS_SetProperty onto a property that is
-// the object's own finds it there and walks no further either. A node takes slots back as a matter of course (a shift,
-// a splice, a shorter length), so whether a slot is the array's own is asked at each write (Glue::putSlot). A field the
-// host fills stays its object's own unless the node deletes it, which nothing a node should do needs, so those writes
-// are plain sets, and one onto a field the node deleted walks the prototype chain: asking at every field write as well
-// would cost each script node's call about 12 ns a block (5 fields; 14% of an empty pass-through's), and about 4 ns
-// more a message.
+// How the host makes a property on an object it made, or a slot of an array JS holds that is not the array's own: an
+// own data property, writable, enumerable and configurable as a plain assignment would make it, a failure thrown. A
+// define never walks the prototype chain, so in the realm every script node of a pack shares, an accessor one node (or
+// the bundle) hung on Object.prototype or Array.prototype never runs for another's; and a JS_SetProperty onto a
+// property that is the object's own finds it there and walks no further either. A node takes slots back as a matter of
+// course (a shift, a splice, a shorter length), so whether a slot is the array's own is asked at each write
+// (Glue::putSlot). A field the host fills stays its object's own unless the node deletes it, which nothing a node
+// should do needs, so those writes are plain sets, and one onto a field the node deleted walks the prototype chain:
+// asking at every field write as well would cost each script node's call about 12 ns a block (5 fields; 14% of an empty
+// pass-through's), and about 4 ns more a message.
 constexpr int kDefine = JS_PROP_C_W_E | JS_PROP_THROW;
 
 // `text` into out[0..capacity), NUL-terminated, cut where it does not fit at the end of a character: a cut by bytes
@@ -292,13 +293,38 @@ std::unique_ptr<Layout> buildLayout(const std::string& json, std::string& why) {
 struct ScriptHost::Type {
     rp_node_type  abi{};
     ScriptHost*   host = nullptr;
+    Pack*         pack = nullptr;  // whose runtimes its definitions, and so its instances, live in
     std::string   id;
     std::string   descriptor;
     bool          reshapes = false;
     bool          faultOnThrow = false;
     std::uint32_t spanLabel = DSP_SPAN_SCRIPT_NODE;  // its nodes' process spans' label (DspSpanLabel), set by the load
-    JSValue       control = JS_UNDEFINED;  // the definition, in the control context
-    JSValue       audio = JS_UNDEFINED;    // and in the audio context
+    JSValue       control = JS_UNDEFINED;  // the definition, in its pack's control context
+    JSValue       audio = JS_UNDEFINED;    // and in its audio context
+};
+
+// A pack (docs/modular-graph-phase-c1-design.md section 4): an id, its two runtimes and their contexts, loaded with its
+// bundle, and the types that bundle registered, in a package of its own. Everything the block thread holds per audio
+// runtime is the pack's, made with it: the Block class and its frozen prototype, the atoms it names properties by, the
+// address of Object.prototype. Made and loaded on the control thread; the block thread reaches it only through an
+// instance of one of its types.
+struct ScriptHost::Pack {
+    ScriptHost*   host = nullptr;   // how a Block method, which finds the pack as its context's opaque, finds the host
+    std::string   id;
+    std::uint32_t generation = 1;   // its load's count
+    std::string   manifest;         // its package's: {"id":"<id>"}
+    JSRuntime*    controlRt = nullptr;
+    JSContext*    control = nullptr;
+    JSRuntime*    audioRt = nullptr;
+    JSContext*    audio = nullptr;
+    JSClassID     blockClass = 0;   // the Block object's class, in the audio runtime
+    std::array<JSAtom, kAtomCount> atoms{};  // the names the block thread reads and writes, in the audio runtime
+    const void*   objectProto = nullptr;     // the audio context's Object.prototype, by address
+    std::vector<std::unique_ptr<Type>> types;
+    std::vector<const rp_node_type*>   typeList;
+    rp_package    package{};
+    // Its load, or a release of one of its instances, since its last collect (whoever released it).
+    bool          garbage = false;
 };
 
 // One script node instance, behind its shim. Allocated by create and deleted on the control thread; its JS state is
@@ -339,15 +365,18 @@ struct Shim : rp_node {
 
 }  // namespace
 
-// Everything the ABI tables, the Block object's methods and the runtime's allocator reach the host through.
+// Everything the ABI tables, the Block object's methods and the runtimes' allocator reach the host through. What an
+// instance's JS lives in is reached through its type's pack, and what a Block method runs in through its context's.
 struct ScriptHost::Glue {
-    static Type&     typeOf(const rp_node_type* t) { return *static_cast<Type*>(t->ctx); }
-    static Instance& instanceOf(rp_node* n) { return *static_cast<Shim*>(n)->in; }
-    static ScriptHost& hostOf(JSContext* ctx) { return *static_cast<ScriptHost*>(JS_GetContextOpaque(ctx)); }
-    static JSAtom    atom(ScriptHost& h, Atom a) { return h.atoms_[a]; }
+    static Type&       typeOf(const rp_node_type* t) { return *static_cast<Type*>(t->ctx); }
+    static Instance&   instanceOf(rp_node* n) { return *static_cast<Shim*>(n)->in; }
+    static Pack&       packOf(Instance& in) { return *in.type->pack; }
+    static Pack&       packOf(JSContext* audio) { return *static_cast<Pack*>(JS_GetContextOpaque(audio)); }
+    static ScriptHost& hostOf(JSContext* audio) { return *packOf(audio).host; }
+    static JSAtom      atom(const Pack& p, Atom a) { return p.atoms[a]; }
 
     // ---- the call deadline ---------------------------------------------------------------------------------------
-    // While one lives, what the block thread runs in the audio runtime may use the deadline's worth of the thread's CPU
+    // While one lives, what the block thread runs in an audio runtime may use the deadline's worth of the thread's CPU
     // time, and the interrupt handler stops it after: QuickJS throws an uncatchable error, which the call returns as a
     // throw. Arming one reads no clock: the handler takes the baseline at its first poll inside the call and compares at
     // the later ones, so a call too short to reach a poll costs nothing (and one that does goes uncounted for up to
@@ -396,7 +425,7 @@ struct ScriptHost::Glue {
         return in.expiries >= 2;
     }
 
-    // ---- the audio runtime's allocator: the platform's, counted ------------------------------------------------
+    // ---- the audio runtimes' allocator: the platform's, counted -----------------------------------------------
 #ifdef RETROPLUG_PROFILE
     // A profile build keeps the live heap and its peak too, in usable bytes, as QuickJS's own accounting (jsUsable)
     // reads a block.
@@ -457,9 +486,9 @@ struct ScriptHost::Glue {
             copyError(err, "settings encoding " + std::to_string(s->encoding) + " is not supported; a script node reads JSON");
             return false;
         }
-        ScriptHost& h = *t.host;
-        JSContext* ctx = h.control_;
-        JS_UpdateStackTop(h.controlRt_);  // QuickJS guards the stack from where it was last entered: re-anchor it here
+        Pack& pack = *t.pack;
+        JSContext* ctx = pack.control;
+        JS_UpdateStackTop(pack.controlRt);  // QuickJS guards the stack from where it was last entered: re-anchor here
         const std::string text = s && s->data && s->size ? std::string(s->data, s->size) : std::string("{}");
         JSValue raw = JS_ParseJSON(ctx, text.c_str(), text.size(), "<settings>");
         JSValue parsed = JS_IsException(raw) ? JS_EXCEPTION : callMethod(ctx, t.control, "parseSettings", 1, &raw);
@@ -469,7 +498,7 @@ struct ScriptHost::Glue {
         JS_FreeValue(ctx, lay);
         JS_FreeValue(ctx, parsed);
         JS_FreeValue(ctx, raw);
-        h.drainControlJobs();
+        drainControlJobs(pack);
         return ok;
     }
 
@@ -541,7 +570,7 @@ struct ScriptHost::Glue {
         Instance& in = instanceOf(n);
         ScriptHost& h = *in.type->host;
         if (!ensureLive(h, in)) return;
-        JSContext* ctx = h.audio_;
+        JSContext* ctx = packOf(in).audio;
         if (!JS_IsFunction(ctx, in.portsChanged)) return;
         const Deadline call(h);
         JSValue mask = JS_NewArray(ctx);
@@ -560,7 +589,8 @@ struct ScriptHost::Glue {
         Instance& in = instanceOf(n);
         ScriptHost& h = *in.type->host;
         if (!ensureLive(h, in) || in.faultNext) return RP_FAULT;
-        JSContext* ctx = h.audio_;
+        const Pack& pack = packOf(in);
+        JSContext* ctx = pack.audio;
         // The whole block, not just the call: a node may hang a setter on what the block path fills in.
         const Deadline call(h);
         traceBegin(h, in.type->spanLabel);
@@ -568,9 +598,9 @@ struct ScriptHost::Glue {
         ++in.blockStamp;
         // Every audio port's copies first, the inputs' samples in them: a node may read views it kept from a block
         // before, so the copy cannot wait for it to ask.
-        if (!readyAudio(h, ctx, in, *block)) return cannotRun(h, in, *block);
+        if (!readyAudio(pack, ctx, in, *block)) return cannotRun(h, in, *block);
         in.running = block;
-        if (!fillBlock(h, ctx, in, *block)) {
+        if (!fillBlock(pack, ctx, in, *block)) {
             in.running = nullptr;
             return cannotRun(h, in, *block);
         }
@@ -587,7 +617,7 @@ struct ScriptHost::Glue {
             fault = in.type->faultOnThrow || expired;
         } else if (JS_IsString(result)) {
             const JSAtom said = JS_ValueToAtom(ctx, result);
-            fault = said == atom(h, kFault);
+            fault = said == atom(pack, kFault);
             JS_FreeAtom(ctx, said);
         }
         JS_FreeValue(ctx, result);
@@ -611,13 +641,13 @@ struct ScriptHost::Glue {
     // The block's length and transport, onto the Block object and its transport, whose fields createJs defined, so each
     // write finds its own property (one the node deleted is the exception, kDefine). False, with the exception pending,
     // when a write fails: a setter the node hung on one threw, or it froze one.
-    static bool fillBlock(ScriptHost& h, JSContext* ctx, Instance& in, const rp_process& block) {
+    static bool fillBlock(const Pack& p, JSContext* ctx, Instance& in, const rp_process& block) {
         const rp_transport& t = *block.transport;
-        return JS_SetProperty(ctx, in.block, atom(h, kFrames), JS_NewUint32(ctx, block.frames)) >= 0 &&
-               JS_SetProperty(ctx, in.transport, atom(h, kSampleRate), JS_NewFloat64(ctx, t.sample_rate)) >= 0 &&
-               JS_SetProperty(ctx, in.transport, atom(h, kTempo), JS_NewFloat64(ctx, t.tempo)) >= 0 &&
-               JS_SetProperty(ctx, in.transport, atom(h, kPpq), JS_NewFloat64(ctx, t.ppq)) >= 0 &&
-               JS_SetProperty(ctx, in.transport, atom(h, kPlaying), JS_NewBool(ctx, (t.flags & RP_TRANSPORT_PLAYING) != 0)) >= 0;
+        return JS_SetProperty(ctx, in.block, atom(p, kFrames), JS_NewUint32(ctx, block.frames)) >= 0 &&
+               JS_SetProperty(ctx, in.transport, atom(p, kSampleRate), JS_NewFloat64(ctx, t.sample_rate)) >= 0 &&
+               JS_SetProperty(ctx, in.transport, atom(p, kTempo), JS_NewFloat64(ctx, t.tempo)) >= 0 &&
+               JS_SetProperty(ctx, in.transport, atom(p, kPpq), JS_NewFloat64(ctx, t.ppq)) >= 0 &&
+               JS_SetProperty(ctx, in.transport, atom(p, kPlaying), JS_NewBool(ctx, (t.flags & RP_TRANSPORT_PLAYING) != 0)) >= 0;
     }
 
     // A process call's js-call span (a profile build), closed as the call returns whatever happened in JS. Every span
@@ -657,7 +687,7 @@ struct ScriptHost::Glue {
         auto* next = static_cast<Settings*>(prepared);
         Settings* prev = in.settings;
         const bool live = ensureLive(h, in);  // a fresh instance is made from the settings it was created with first
-        JSContext* ctx = h.audio_;
+        JSContext* ctx = packOf(in).audio;
         if (next->layout) {
             if (next->layout->json != in.layout->json) {
                 freeLayoutJs(ctx, *in.layout);
@@ -690,7 +720,7 @@ struct ScriptHost::Glue {
     // ---- the block thread: an instance's JS ------------------------------------------------------------------------
     static bool ensureLive(ScriptHost& h, Instance& in) {
         if (in.state == Instance::State::Pending) createJs(h, in);
-        else JS_UpdateStackTop(h.audioRt_);
+        else JS_UpdateStackTop(packOf(in).audioRt);
         return in.state == Instance::State::Live;
     }
 
@@ -700,8 +730,9 @@ struct ScriptHost::Glue {
     // of memory making what the host hands it. What the host makes here holds every field it will write as its own
     // (kDefine).
     static void createJs(ScriptHost& h, Instance& in) {
-        JSContext* ctx = h.audio_;
-        JS_UpdateStackTop(h.audioRt_);
+        const Pack& pack = packOf(in);
+        JSContext* ctx = pack.audio;
+        JS_UpdateStackTop(pack.audioRt);
         bindLayout(ctx, *in.layout);
         const Deadline call(h);
         // The settings as the control side's parseSettings returned them, in their JSON form (the class comment).
@@ -718,7 +749,7 @@ struct ScriptHost::Glue {
         JSValue object = JS_IsException(cc) ? JS_EXCEPTION : callMethod(ctx, in.type->audio, "create", 1, &cc);
         JS_FreeValue(ctx, cc);
         if (JS_IsException(object)) return failCreate(h, in);
-        JSValue process = method(h, ctx, object, kProcess);
+        JSValue process = method(pack, ctx, object, kProcess);
         if (!JS_IsFunction(ctx, process)) {
             if (!JS_IsException(process)) JS_ThrowTypeError(ctx, "create returned no process function");
             JS_FreeValue(ctx, process);
@@ -730,18 +761,18 @@ struct ScriptHost::Glue {
         JSValue hooks[3] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
         bool ok = true;
         for (int k = 0; k < 3 && ok; ++k) {
-            JSValue fn = method(h, ctx, object, names[k]);
+            JSValue fn = method(pack, ctx, object, names[k]);
             ok = !JS_IsException(fn);
             if (JS_IsFunction(ctx, fn)) hooks[k] = fn;
             else JS_FreeValue(ctx, fn);
         }
         // The Block object and its transport, with every field the block path fills (fillBlock), so no write there
         // reaches the Block class's prototype or Object.prototype unless the node deletes the field (kDefine).
-        JSValue block = ok ? JS_NewObjectClass(ctx, h.blockClass_) : JS_EXCEPTION;
+        JSValue block = ok ? JS_NewObjectClass(ctx, pack.blockClass) : JS_EXCEPTION;
         JSValue transport = JS_IsException(block) ? JS_EXCEPTION : JS_NewObject(ctx);
-        ok = !JS_IsException(transport) && defineFields(h, ctx, transport, {kSampleRate, kTempo, kPpq, kPlaying}) &&
-             JS_DefinePropertyValue(ctx, block, atom(h, kTransport), JS_DupValue(ctx, transport), kDefine) >= 0 &&
-             defineFields(h, ctx, block, {kFrames});
+        ok = !JS_IsException(transport) && defineFields(pack, ctx, transport, {kSampleRate, kTempo, kPpq, kPlaying}) &&
+             JS_DefinePropertyValue(ctx, block, atom(pack, kTransport), JS_DupValue(ctx, transport), kDefine) >= 0 &&
+             defineFields(pack, ctx, block, {kFrames});
         if (!ok) {
             for (JSValue v : {transport, block, hooks[0], hooks[1], hooks[2], process, object}) JS_FreeValue(ctx, v);
             return failCreate(h, in);
@@ -766,9 +797,9 @@ struct ScriptHost::Glue {
 
     // Each of `fields` onto `obj`, an object the host made and JS has not yet seen, as its own data property, undefined
     // until the block path fills it. False, with the exception pending, when JS is out of memory.
-    static bool defineFields(ScriptHost& h, JSContext* ctx, JSValueConst obj, std::initializer_list<Atom> fields) {
+    static bool defineFields(const Pack& p, JSContext* ctx, JSValueConst obj, std::initializer_list<Atom> fields) {
         for (const Atom a : fields)
-            if (JS_DefinePropertyValue(ctx, obj, atom(h, a), JS_UNDEFINED, kDefine) < 0) return false;
+            if (JS_DefinePropertyValue(ctx, obj, atom(p, a), JS_UNDEFINED, kDefine) < 0) return false;
         return true;
     }
 
@@ -785,17 +816,17 @@ struct ScriptHost::Glue {
     // cannot see past, recurses into it until that throws a RangeError), and the walk by kMethodHops (plain objects
     // never form a cycle, and a chain of more than that is refused, with a RangeError). Undefined when nothing on the
     // way has it; JS_EXCEPTION, with it pending, when a getter or a Proxy's trap throws or the read is refused.
-    static JSValue method(ScriptHost& h, JSContext* ctx, JSValueConst object, Atom name) {
+    static JSValue method(const Pack& p, JSContext* ctx, JSValueConst object, Atom name) {
         if (!JS_IsObject(object)) return JS_UNDEFINED;
         JSValue at = JS_DupValue(ctx, object);
-        for (int hops = 0; JS_IsObject(at) && JS_VALUE_GET_PTR(at) != h.objectProto_; ++hops) {
+        for (int hops = 0; JS_IsObject(at) && JS_VALUE_GET_PTR(at) != p.objectProto; ++hops) {
             if (hops == kMethodHops || JS_IsProxy(at)) {
                 JS_FreeValue(ctx, at);
-                if (hops < kMethodHops) return JS_GetProperty(ctx, object, atom(h, name));
+                if (hops < kMethodHops) return JS_GetProperty(ctx, object, atom(p, name));
                 return JS_ThrowRangeError(ctx, "the prototype chain is longer than %d objects", kMethodHops);
             }
             JSPropertyDescriptor d;
-            const int own = JS_GetOwnProperty(ctx, &d, at, atom(h, name));
+            const int own = JS_GetOwnProperty(ctx, &d, at, atom(p, name));
             if (own != 0) {
                 JS_FreeValue(ctx, at);
                 if (own < 0) return JS_EXCEPTION;
@@ -831,9 +862,10 @@ struct ScriptHost::Glue {
     }
 
     static bool callActivate(ScriptHost& h, Instance& in) {
-        JSContext* ctx = h.audio_;
+        const Pack& pack = packOf(in);
+        JSContext* ctx = pack.audio;
         if (!JS_IsFunction(ctx, in.activate)) return true;
-        JS_UpdateStackTop(h.audioRt_);
+        JS_UpdateStackTop(pack.audioRt);
         const Deadline call(h);
         JSValueConst args[2] = {JS_NewFloat64(ctx, in.sampleRate), JS_NewUint32(ctx, in.maxFrames)};
         JSValue result = JS_Call(ctx, in.activate, in.object, 2, args);
@@ -845,12 +877,13 @@ struct ScriptHost::Glue {
         return ok;
     }
 
-    // Counted, and the first one kept for reclaim to log; the exception is cleared. A call the deadline stopped counts
-    // as a throw, and says so.
-    static void recordThrow(ScriptHost& h, Instance& in, const char* where) { recordThrow(h, in.type->id.c_str(), where); }
+    // Counted, and the first one kept for reclaim to log; the exception, pending in `ctx` (the context the call ran
+    // in), is cleared. A call the deadline stopped counts as a throw, and says so.
+    static void recordThrow(ScriptHost& h, Instance& in, const char* where) {
+        recordThrow(h, packOf(in).audio, in.type->id.c_str(), where);
+    }
     // `type` null: no one node's (a promise job).
-    static void recordThrow(ScriptHost& h, const char* type, const char* where) {
-        JSContext* ctx = h.audio_;
+    static void recordThrow(ScriptHost& h, JSContext* ctx, const char* type, const char* where) {
         JSValue thrown = JS_GetException(ctx);
         h.throws_.fetch_add(1, std::memory_order_relaxed);
         int unclaimed = 0;
@@ -946,7 +979,7 @@ struct ScriptHost::Glue {
     // setter it hung on a slot threw, or ran past the deadline, or it froze the array), which process then fails as the
     // node's throw. Every view of that port is then made again next block, so the array is never left holding a view of
     // a copy the host has since replaced.
-    static bool readyAudio(ScriptHost& h, JSContext* ctx, Instance& in, const rp_process& b) {
+    static bool readyAudio(const Pack& pack, JSContext* ctx, Instance& in, const rp_process& b) {
         Layout& l = *in.layout;
         for (std::size_t i = 0; i < l.ports.size(); ++i) {
             if (l.ports[i].kind != PortKind::Audio) continue;
@@ -991,7 +1024,7 @@ struct ScriptHost::Glue {
                 if (l.ports[i].side == Side::Input) std::memcpy(data, v.channels[c], std::size_t(b.frames) * sizeof(float));
             }
             if (count != p.viewCount || p.viewFrames != b.frames) {
-                if (JS_SetProperty(ctx, p.value, atom(h, kLength), JS_NewUint32(ctx, count)) < 0) return stale();
+                if (JS_SetProperty(ctx, p.value, atom(pack, kLength), JS_NewUint32(ctx, count)) < 0) return stale();
                 p.viewCount = count;
                 p.viewFrames = b.frames;
             }
@@ -1031,7 +1064,7 @@ struct ScriptHost::Glue {
     // ---- the Block object ----------------------------------------------------------------------------------------
     // The instance whose process call is running with this block, or null with a TypeError thrown.
     static Instance* running(JSContext* ctx, JSValueConst self) {
-        auto* in = static_cast<Instance*>(JS_GetOpaque(self, hostOf(ctx).blockClass_));
+        auto* in = static_cast<Instance*>(JS_GetOpaque(self, packOf(ctx).blockClass));
         if (!in || !in->running) {
             JS_ThrowTypeError(ctx, "a block is used only during its node's process call");
             return nullptr;
@@ -1066,12 +1099,14 @@ struct ScriptHost::Glue {
     // (defineFields), so a fill's writes onto it, and its read of a message's data, reach Object.prototype only for a
     // field the node deleted (kDefine). The pool is read only below `pooled`, where every slot is one the host defined,
     // so no read walks to a getter on Array.prototype either. JS_EXCEPTION, with it pending, when JS is out of memory.
-    static JSValue element(ScriptHost& h, JSContext* ctx, PortJs& p, std::uint32_t i, std::initializer_list<Atom> fields) {
+    static JSValue element(const Pack& pack, JSContext* ctx, PortJs& p, std::uint32_t i,
+                           std::initializer_list<Atom> fields) {
         if (i < p.pooled) return JS_GetPropertyUint32(ctx, p.pool, i);
         assert(i == p.pooled && "a fill makes its elements in order");
         JSValue e = JS_NewObject(ctx);
         if (JS_IsException(e)) return e;
-        if (!defineFields(h, ctx, e, fields) || JS_DefinePropertyValueUint32(ctx, p.pool, i, JS_DupValue(ctx, e), kDefine) < 0) {
+        if (!defineFields(pack, ctx, e, fields) ||
+            JS_DefinePropertyValueUint32(ctx, p.pool, i, JS_DupValue(ctx, e), kDefine) < 0) {
             JS_FreeValue(ctx, e);
             return JS_EXCEPTION;
         }
@@ -1102,25 +1137,25 @@ struct ScriptHost::Glue {
 
     // One message's frame, data and flags onto its element. False, with the exception pending, when JS is out of memory
     // or a read or write there fails.
-    static bool fillMessage(ScriptHost& h, JSContext* ctx, const PortJs& p, JSValueConst e, const rp_message& m) {
-        if (JS_SetProperty(ctx, e, atom(h, kFrame), JS_NewUint32(ctx, m.frame)) < 0) return false;
-        JSValue data = JS_GetProperty(ctx, e, atom(h, kData));
+    static bool fillMessage(const Pack& pack, JSContext* ctx, const PortJs& p, JSValueConst e, const rp_message& m) {
+        if (JS_SetProperty(ctx, e, atom(pack, kFrame), JS_NewUint32(ctx, m.frame)) < 0) return false;
+        JSValue data = JS_GetProperty(ctx, e, atom(pack, kData));
         if (JS_IsException(data)) return false;
         const bool same = viewing(ctx, data, p.payload, m.offset, m.size);
         JS_FreeValue(ctx, data);
         if (!same) {
             JSValueConst args[3] = {p.payload, JS_NewUint32(ctx, m.offset), JS_NewUint32(ctx, m.size)};
             JSValue view = JS_NewTypedArray(ctx, 3, args, JS_TYPED_ARRAY_UINT8);
-            if (JS_IsException(view) || JS_SetProperty(ctx, e, atom(h, kData), view) < 0) return false;
+            if (JS_IsException(view) || JS_SetProperty(ctx, e, atom(pack, kData), view) < 0) return false;
         }
-        return JS_SetProperty(ctx, e, atom(h, kFlags), JS_NewUint32(ctx, m.flags)) >= 0;
+        return JS_SetProperty(ctx, e, atom(pack, kFlags), JS_NewUint32(ctx, m.flags)) >= 0;
     }
 
     // An events or bytes input as a reused Message[]: the payloads copied into one JS-owned buffer (grown, never
     // shrunk), each message's data a Uint8Array over its bytes there, made again only when its place moves. False, with
     // the exception pending, when JS is out of memory or a write onto what the node holds fails (a setter it hung on a
     // message threw, or it froze the array or a message), which messages() then throws as the node's.
-    static bool fillMessages(ScriptHost& h, JSContext* ctx, PortJs& p, const rp_queue& q) {
+    static bool fillMessages(const Pack& pack, JSContext* ctx, PortJs& p, const rp_queue& q) {
         if (JS_IsUndefined(p.value)) {
             JSValue pool = JS_NewArray(ctx), value = JS_NewArray(ctx);
             if (JS_IsException(pool) || JS_IsException(value)) {
@@ -1148,26 +1183,26 @@ struct ScriptHost::Glue {
             if (bytes && q.bytes_used) std::memcpy(bytes, q.bytes, q.bytes_used);
         }
         for (std::uint32_t i = 0; i < q.count; ++i) {
-            JSValue e = element(h, ctx, p, i, {kFrame, kData, kFlags});
+            JSValue e = element(pack, ctx, p, i, {kFrame, kData, kFlags});
             if (JS_IsException(e)) return false;
-            if (!fillMessage(h, ctx, p, e, q.items[i])) {
+            if (!fillMessage(pack, ctx, p, e, q.items[i])) {
                 JS_FreeValue(ctx, e);
                 return false;
             }
             if (putSlot(ctx, p.value, i, e) < 0) return false;
         }
-        return JS_SetProperty(ctx, p.value, atom(h, kLength), JS_NewUint32(ctx, q.count)) >= 0;
+        return JS_SetProperty(ctx, p.value, atom(pack, kLength), JS_NewUint32(ctx, q.count)) >= 0;
     }
 
     // A control input or a param as a reused { start, changes }. False, with the exception pending, when JS is out of
     // memory or a write onto what the node holds fails (a setter it hung on the stream or a change threw, or it froze
     // one), which control() then throws as the node's.
-    static bool fillControl(ScriptHost& h, JSContext* ctx, PortJs& p, const rp_control_queue& q) {
+    static bool fillControl(const Pack& pack, JSContext* ctx, PortJs& p, const rp_control_queue& q) {
         if (JS_IsUndefined(p.value)) {
             JSValue items = JS_NewArray(ctx), pool = JS_NewArray(ctx), value = JS_NewObject(ctx);
             const bool made = !JS_IsException(items) && !JS_IsException(pool) && !JS_IsException(value) &&
-                              JS_DefinePropertyValue(ctx, value, atom(h, kChanges), JS_DupValue(ctx, items), kDefine) >= 0 &&
-                              defineFields(h, ctx, value, {kStart});
+                              JS_DefinePropertyValue(ctx, value, atom(pack, kChanges), JS_DupValue(ctx, items), kDefine) >= 0 &&
+                              defineFields(pack, ctx, value, {kStart});
             if (!made) {  // all made again next time
                 for (JSValue v : {items, pool, value}) JS_FreeValue(ctx, v);
                 return false;
@@ -1177,18 +1212,18 @@ struct ScriptHost::Glue {
             p.value = value;
             p.pooled = 0;
         }
-        if (JS_SetProperty(ctx, p.value, atom(h, kStart), JS_NewFloat64(ctx, q.start)) < 0) return false;
+        if (JS_SetProperty(ctx, p.value, atom(pack, kStart), JS_NewFloat64(ctx, q.start)) < 0) return false;
         for (std::uint32_t i = 0; i < q.count; ++i) {
-            JSValue e = element(h, ctx, p, i, {kFrame, kValue});
+            JSValue e = element(pack, ctx, p, i, {kFrame, kValue});
             if (JS_IsException(e)) return false;
-            if (JS_SetProperty(ctx, e, atom(h, kFrame), JS_NewUint32(ctx, q.changes[i].frame)) < 0 ||
-                JS_SetProperty(ctx, e, atom(h, kValue), JS_NewFloat64(ctx, q.changes[i].value)) < 0) {
+            if (JS_SetProperty(ctx, e, atom(pack, kFrame), JS_NewUint32(ctx, q.changes[i].frame)) < 0 ||
+                JS_SetProperty(ctx, e, atom(pack, kValue), JS_NewFloat64(ctx, q.changes[i].value)) < 0) {
                 JS_FreeValue(ctx, e);
                 return false;
             }
             if (putSlot(ctx, p.items, i, e) < 0) return false;
         }
-        return JS_SetProperty(ctx, p.items, atom(h, kLength), JS_NewUint32(ctx, q.count)) >= 0;
+        return JS_SetProperty(ctx, p.items, atom(pack, kLength), JS_NewUint32(ctx, q.count)) >= 0;
     }
 
     // block.messages(port): an events or bytes input.
@@ -1201,7 +1236,7 @@ struct ScriptHost::Glue {
         if (!p) return JS_EXCEPTION;
         if (p->stamp != in->blockStamp) {
             traceBegin(hostOf(ctx), DSP_SPAN_MARSHAL);
-            const bool filled = fillMessages(hostOf(ctx), ctx, *p, *in->running->inputs[found->index].u.queue);
+            const bool filled = fillMessages(packOf(*in), ctx, *p, *in->running->inputs[found->index].u.queue);
             traceEnd(hostOf(ctx));
             if (!filled) return JS_EXCEPTION;
             p->stamp = in->blockStamp;
@@ -1221,7 +1256,7 @@ struct ScriptHost::Glue {
             const rp_control_queue& q =
                 found->side == Side::Param ? *in->running->params[found->index] : *in->running->inputs[found->index].u.control;
             traceBegin(hostOf(ctx), DSP_SPAN_MARSHAL);
-            const bool filled = fillControl(hostOf(ctx), ctx, *p, q);
+            const bool filled = fillControl(packOf(*in), ctx, *p, q);
             traceEnd(hostOf(ctx));
             if (!filled) return JS_EXCEPTION;
             p->stamp = in->blockStamp;
@@ -1297,77 +1332,99 @@ struct ScriptHost::Glue {
 
 // ---- ScriptHost ------------------------------------------------------------------------------------------------
 
-ScriptHost::ScriptHost(Binder binder, void* user) {
-    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, 0, nullptr};
-    controlRt_ = JS_NewRuntime();
-    control_ = JS_NewContext(controlRt_);
+ScriptHost::ScriptHost(Binder binder, void* user) : binder_(binder), binderUser_(user) {
+    // The fixed labels' names (a profile build's: traceName names nothing in any other); a pack's load names its
+    // types'.
+    for (const auto& [label, name] : {std::pair{DSP_SPAN_SCRIPT_NODE, "script-node"}, std::pair{DSP_SPAN_MARSHAL, "marshal"},
+                                      std::pair{DSP_SPAN_JSCALL, "js-call"}, std::pair{DSP_SPAN_APU, "apu-render"},
+                                      std::pair{DSP_SPAN_PUBLISH, "state-publish"}})
+        traceName(label, name);
+    packs_.push_back(makePack(kBuiltInPack));  // loaded by load
+}
+
+ScriptHost::~ScriptHost() {
+    assert(!audioOwns_.load() && "destroy a ScriptHost only once no audio thread runs its blocks");
+    releaseQueued();
+    assert(live_.load() == 0 && "every shim goes before its script host");
+    for (auto& pack : packs_) freeRuntimes(*pack);
+}
+
+ScriptHost::Chunk ScriptHost::embeddedBundle() { return {rp_script_bundle, rp_script_bundle_size}; }
+
+const rp_package* ScriptHost::package() const { return &packs_.front()->package; }
+
+std::unique_ptr<ScriptHost::Pack> ScriptHost::makePack(const std::string& id) {
+    auto pack = std::make_unique<Pack>();
+    pack->host = this;
+    pack->id = id;
+    pack->manifest = R"({"id":")" + id + R"("})";
+    pack->package = rp_package{RP_ABI_VERSION, pack->manifest.c_str(), 0, nullptr};
+    makeRuntimes(*pack);
+    return pack;
+}
+
+void ScriptHost::makeRuntimes(Pack& pack) {
+    pack.controlRt = JS_NewRuntime();
+    pack.control = JS_NewContext(pack.controlRt);
     const JSMallocFunctions counted{&Glue::jsCalloc, &Glue::jsMalloc, &Glue::jsFree, &Glue::jsRealloc, &Glue::jsUsable};
-    audioRt_ = JS_NewRuntime2(&counted, &counters_);  // QuickJS copies the functions and keeps the opaque
-    audio_ = JS_NewContext(audioRt_);
-    JS_SetContextOpaque(audio_, this);
+    pack.audioRt = JS_NewRuntime2(&counted, &counters_);  // QuickJS copies the functions and keeps the opaque
+    pack.audio = JS_NewContext(pack.audioRt);
+    JS_SetContextOpaque(pack.audio, &pack);  // what a Block method runs in: Glue::packOf
     // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the block
-    // thread runs held to the call deadline.
-    JS_SetMaxStackSize(controlRt_, kStackBytes);
-    JS_SetMaxStackSize(audioRt_, kStackBytes);
-    JS_SetInterruptHandler(audioRt_, &Glue::interrupt, this);
+    // thread runs held to the call deadline, whose state every pack shares: the handler's opaque is the host.
+    JS_SetMaxStackSize(pack.controlRt, kStackBytes);
+    JS_SetMaxStackSize(pack.audioRt, kStackBytes);
+    JS_SetInterruptHandler(pack.audioRt, &Glue::interrupt, this);
 
     // Object.prototype, which a node's methods are never read from (Glue::method): by address, as the context holds it
     // for its life and no script can replace it.
-    JSValue plain = JS_NewObject(audio_);
-    JSValue objectProto = JS_GetPrototype(audio_, plain);
-    objectProto_ = JS_VALUE_GET_PTR(objectProto);
-    JS_FreeValue(audio_, objectProto);
-    JS_FreeValue(audio_, plain);
-
-    // The Block object: one per instance, its methods on the class prototype, the instance its opaque. Every node
-    // reaches that prototype (Object.getPrototypeOf(block)), so it and its methods are frozen: no node replaces, removes
-    // or shadows a method there, or on one of the functions, for another.
-    JS_NewClassID(audioRt_, &blockClass_);
+    JSContext* ctx = pack.audio;
+    JSValue plain = JS_NewObject(ctx);
+    JSValue objectProto = JS_GetPrototype(ctx, plain);
+    pack.objectProto = JS_VALUE_GET_PTR(objectProto);
+    JS_FreeValue(ctx, objectProto);
+    JS_FreeValue(ctx, plain);
+
+    // The Block object: one per instance, its methods on the class prototype, the instance its opaque. Every node of
+    // the pack reaches that prototype (Object.getPrototypeOf(block)), so it and its methods are frozen: no node
+    // replaces, removes or shadows a method there, or on one of the functions, for another. A class id is its runtime's
+    // own (JS_NewClassID takes the runtime, and the pack's id is 0 until then), so each pack's has its own Block class.
+    JS_NewClassID(pack.audioRt, &pack.blockClass);
     JSClassDef block{};
     block.class_name = "Block";
-    JS_NewClass(audioRt_, blockClass_, &block);
-    JSValue proto = JS_NewObject(audio_);
+    JS_NewClass(pack.audioRt, pack.blockClass, &block);
+    JSValue proto = JS_NewObject(ctx);
     const auto bindFrozen = [&](JSValueConst on, const char* name, JSCFunction* fn, int length, int flags) {
-        JSValue f = JS_NewCFunction(audio_, fn, name, length);
-        JS_FreezeObject(audio_, f);
-        JS_DefinePropertyValueStr(audio_, on, name, f, flags);
+        JSValue f = JS_NewCFunction(ctx, fn, name, length);
+        JS_FreezeObject(ctx, f);
+        JS_DefinePropertyValueStr(ctx, on, name, f, flags);
     };
     bindFrozen(proto, "messages", &Glue::messages, 1, JS_PROP_C_W_E);
     bindFrozen(proto, "send", &Glue::send, 4, JS_PROP_C_W_E);
     bindFrozen(proto, "control", &Glue::control, 1, JS_PROP_C_W_E);
     bindFrozen(proto, "emit", &Glue::emit, 3, JS_PROP_C_W_E);
     bindFrozen(proto, "audio", &Glue::audio, 1, JS_PROP_C_W_E);
-    JS_FreezeObject(audio_, proto);
-    JS_SetClassProto(audio_, blockClass_, proto);
-    static_assert(kAtomCount <= std::tuple_size_v<decltype(atoms_)>, "atoms_ holds every atom the block thread names");
-    for (std::size_t a = 0; a < kAtomCount; ++a) atoms_[a] = JS_NewAtom(audio_, kAtomNames[a]);
-
-    // The fixed labels' names (a profile build's: traceName names nothing in any other); load names the types'.
-    for (const auto& [label, name] : {std::pair{DSP_SPAN_SCRIPT_NODE, "script-node"}, std::pair{DSP_SPAN_MARSHAL, "marshal"},
-                                      std::pair{DSP_SPAN_JSCALL, "js-call"}, std::pair{DSP_SPAN_APU, "apu-render"},
-                                      std::pair{DSP_SPAN_PUBLISH, "state-publish"}})
-        traceName(label, name);
+    JS_FreezeObject(ctx, proto);
+    JS_SetClassProto(ctx, pack.blockClass, proto);
+    for (std::size_t a = 0; a < kAtomCount; ++a) pack.atoms[a] = JS_NewAtom(ctx, kAtomNames[a]);
 
-    if (binder) {
-        binder(user, control_, false);
-        binder(user, audio_, true);
+    if (binder_) {
+        binder_(binderUser_, pack.control, false);
+        binder_(binderUser_, pack.audio, true);
     }
 }
 
-ScriptHost::~ScriptHost() {
-    assert(!audioOwns_.load() && "destroy a ScriptHost only once no audio thread runs its blocks");
-    releaseQueued();
-    assert(live_.load() == 0 && "every shim goes before its script host");
-    freeTypes();
-    for (std::size_t a = 0; a < kAtomCount; ++a) JS_FreeAtom(audio_, atoms_[a]);
-    JS_FreeContext(audio_);
-    JS_FreeRuntime(audioRt_);
-    JS_FreeContext(control_);
-    JS_FreeRuntime(controlRt_);
+void ScriptHost::freeRuntimes(Pack& pack) {
+    freeTypes(pack);
+    for (std::size_t a = 0; a < kAtomCount; ++a) JS_FreeAtom(pack.audio, pack.atoms[a]);
+    JS_FreeContext(pack.audio);
+    JS_FreeRuntime(pack.audioRt);
+    JS_FreeContext(pack.control);
+    JS_FreeRuntime(pack.controlRt);
+    pack.audio = pack.control = nullptr;
+    pack.audioRt = pack.controlRt = nullptr;
 }
 
-ScriptHost::Chunk ScriptHost::embeddedBundle() { return {rp_script_bundle, rp_script_bundle_size}; }
-
 bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
     // Refused without touching what a first load registered: a registry may hold its types already.
     if (loaded_) {
@@ -1375,14 +1432,20 @@ bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
         return false;
     }
     loaded_ = true;
+    return loadInto(*packs_.front(), chunks, error);
+}
+
+bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error) {
     auto fail = [&](std::string why) {
-        freeTypes();
+        freeTypes(pack);
         if (error) *error = std::move(why);
         return false;
     };
+    JSContext* const control = pack.control;
+    JSContext* const audio = pack.audio;
     for (const bool audioSide : {false, true}) {
-        JSContext* ctx = audioSide ? audio_ : control_;
-        JS_UpdateStackTop(audioSide ? audioRt_ : controlRt_);
+        JSContext* ctx = audioSide ? audio : control;
+        JS_UpdateStackTop(audioSide ? pack.audioRt : pack.controlRt);
         for (std::size_t i = 0; i < chunks.size(); ++i) {
             JSValue code = JS_ReadObject(ctx, chunks[i].data(), chunks[i].size(), JS_READ_OBJ_BYTECODE);
             JSValue result = JS_IsException(code) ? JS_EXCEPTION : JS_EvalFunction(ctx, code);  // consumes `code`
@@ -1390,16 +1453,16 @@ bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
             JS_FreeValue(ctx, result);
         }
         // What the bundle's top level queued runs now, in this context's runtime: no block has begun.
-        runJobs(audioSide ? audioRt_ : controlRt_, kJobsPerBlock, [](JSContext* c) { clearException(c); });
+        runJobs(audioSide ? pack.audioRt : pack.controlRt, kJobsPerBlock, [](JSContext* c) { clearException(c); });
     }
     // What the load leaves in cycles in the audio runtime, its top level's and the descriptors' below, goes at the first
     // quiescent reclaim, rather than waiting for a block's threshold GC. (The production bundle left about 144 KiB while
     // zod and the DSP kernel were in it; it has left none since B3b.)
-    garbage_ = true;
+    pack.garbage = true;
     // The definitions, in the same order in both contexts (the same bundle ran in each).
     JSValue lists[2];
     for (const bool audioSide : {false, true}) {
-        JSContext* ctx = audioSide ? audio_ : control_;
+        JSContext* ctx = audioSide ? audio : control;
         JSValue global = JS_GetGlobalObject(ctx);
         lists[audioSide] = JS_GetPropertyStr(ctx, global, kNodesGlobal);
         JS_FreeValue(ctx, global);
@@ -1412,83 +1475,92 @@ bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
             JS_FreeValue(control, v[0]);
             JS_FreeValue(audio, v[1]);
         }
-    } owned{control_, audio_, lists};
+    } owned{control, audio, lists};
     if (JS_IsUndefined(lists[0])) return true;  // a bundle that registers nothing
     std::int64_t count = 0;
-    if (!JS_IsArray(lists[0]) || JS_GetLength(control_, lists[0], &count) < 0)
+    if (!JS_IsArray(lists[0]) || JS_GetLength(control, lists[0], &count) < 0)
         return fail(std::string(kNodesGlobal) + " is not an array of node definitions");
     for (std::int64_t i = 0; i < count; ++i) {
         auto t = std::make_unique<Type>();
         t->host = this;
-        t->control = JS_GetPropertyUint32(control_, lists[0], static_cast<std::uint32_t>(i));
-        t->audio = JS_GetPropertyUint32(audio_, lists[1], static_cast<std::uint32_t>(i));
-        types_.push_back(std::move(t));
-        Type& type = *types_.back();
+        t->pack = &pack;
+        t->control = JS_GetPropertyUint32(control, lists[0], static_cast<std::uint32_t>(i));
+        t->audio = JS_GetPropertyUint32(audio, lists[1], static_cast<std::uint32_t>(i));
+        pack.types.push_back(std::move(t));
+        Type& type = *pack.types.back();
         const std::string at = "definition #" + std::to_string(i);
-        JSValue id = JS_GetPropertyStr(control_, type.control, "id");
-        const char* text = JS_IsString(id) ? JS_ToCString(control_, id) : nullptr;
+        JSValue id = JS_GetPropertyStr(control, type.control, "id");
+        const char* text = JS_IsString(id) ? JS_ToCString(control, id) : nullptr;
         if (text) {
             type.id = text;
-            JS_FreeCString(control_, text);
+            JS_FreeCString(control, text);
         }
-        JS_FreeValue(control_, id);
+        JS_FreeValue(control, id);
         if (type.id.empty()) return fail(at + " has no id");
-        JSValue audioId = JS_GetPropertyStr(audio_, type.audio, "id");
-        const char* audioText = JS_IsString(audioId) ? JS_ToCString(audio_, audioId) : nullptr;
+        JSValue audioId = JS_GetPropertyStr(audio, type.audio, "id");
+        const char* audioText = JS_IsString(audioId) ? JS_ToCString(audio, audioId) : nullptr;
         const bool same = audioText && type.id == audioText;
-        if (audioText) JS_FreeCString(audio_, audioText);
-        JS_FreeValue(audio_, audioId);
+        if (audioText) JS_FreeCString(audio, audioText);
+        JS_FreeValue(audio, audioId);
         if (!same) return fail(at + " (" + type.id + ") is not the same in both contexts: its id differs");
-        JSValue d = callMethod(control_, type.control, "descriptor", 0, nullptr);
-        const bool described = !JS_IsException(d) && stringify(control_, d, type.descriptor);
-        JS_FreeValue(control_, d);
-        drainControlJobs();
-        if (!described) return fail(type.id + ": its descriptor: " + exceptionText(control_, true));
+        JSValue d = callMethod(control, type.control, "descriptor", 0, nullptr);
+        const bool described = !JS_IsException(d) && stringify(control, d, type.descriptor);
+        JS_FreeValue(control, d);
+        drainControlJobs(pack);
+        if (!described) return fail(type.id + ": its descriptor: " + exceptionText(control, true));
         // The audio side's must be the same: the control side lays out and checks what the audio side then runs.
         std::string audioDescriptor;
-        JSValue ad = callMethod(audio_, type.audio, "descriptor", 0, nullptr);
-        const bool audioDescribed = !JS_IsException(ad) && stringify(audio_, ad, audioDescriptor);
-        JS_FreeValue(audio_, ad);
-        if (!audioDescribed) return fail(type.id + ": its descriptor, in the audio context: " + exceptionText(audio_, true));
+        JSValue ad = callMethod(audio, type.audio, "descriptor", 0, nullptr);
+        const bool audioDescribed = !JS_IsException(ad) && stringify(audio, ad, audioDescriptor);
+        JS_FreeValue(audio, ad);
+        if (!audioDescribed) return fail(type.id + ": its descriptor, in the audio context: " + exceptionText(audio, true));
         if (audioDescriptor != type.descriptor)
             return fail(at + " (" + type.id + ") is not the same in both contexts: its descriptor differs, so the bundle "
                              "defines it by which context it runs in");
         for (const auto& [flag, key] : {std::pair{&type.reshapes, "reshapes"}, std::pair{&type.faultOnThrow, "faultOnThrow"}}) {
-            JSValue v = JS_GetPropertyStr(control_, type.control, key);
-            *flag = JS_IsBool(v) && JS_ToBool(control_, v);
-            JS_FreeValue(control_, v);
+            JSValue v = JS_GetPropertyStr(control, type.control, key);
+            *flag = JS_IsBool(v) && JS_ToBool(control, v);
+            JS_FreeValue(control, v);
         }
         type.abi = rp_node_type{type.id.c_str(), &Glue::descriptor, &Glue::layoutFor, &Glue::queryMemory,
                                 &Glue::create, &Glue::destroy, RP_TYPE_STATELESS_PREPARE, &type};
     }
-    runJobs(audioRt_, kJobsPerBlock, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
-    // Each type's span label, in registration order, named by its id (DspSpanLabel); past the name table's bound, a type
-    // keeps the shared script-node label.
-    for (std::size_t i = 0; i < types_.size() && DSP_SPAN_TYPE_BASE + i < kDspSpanLabels; ++i) {
-        types_[i]->spanLabel = DSP_SPAN_TYPE_BASE + static_cast<std::uint32_t>(i);
-        traceName(types_[i]->spanLabel, types_[i]->id);
-    }
-    typeList_.clear();
-    for (const auto& t : types_) typeList_.push_back(&t->abi);
-    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, static_cast<std::uint32_t>(typeList_.size()), typeList_.data()};
+    runJobs(pack.audioRt, kJobsPerBlock, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
+    // Each type's span label, in registration order (spanLabelFor).
+    for (const auto& t : pack.types) t->spanLabel = spanLabelFor(t->id);
+    pack.typeList.clear();
+    for (const auto& t : pack.types) pack.typeList.push_back(&t->abi);
+    pack.package = rp_package{RP_ABI_VERSION, pack.manifest.c_str(), static_cast<std::uint32_t>(pack.typeList.size()),
+                              pack.typeList.data()};
     return true;
 }
 
-void ScriptHost::freeTypes() {
-    for (auto& t : types_) {
-        JS_FreeValue(control_, t->control);
-        JS_FreeValue(audio_, t->audio);
+// A type id's label: the one it was given at its first registration, else the next one, named by its id, while the name
+// table has room; past it, the shared script-node label.
+std::uint32_t ScriptHost::spanLabelFor(const std::string& typeId) {
+    if (const auto at = spanLabels_.find(typeId); at != spanLabels_.end()) return at->second;
+    if (nextSpanLabel_ >= kDspSpanLabels) return DSP_SPAN_SCRIPT_NODE;
+    const std::uint32_t label = nextSpanLabel_++;
+    spanLabels_.emplace(typeId, label);
+    traceName(label, typeId);
+    return label;
+}
+
+void ScriptHost::freeTypes(Pack& pack) {
+    for (auto& t : pack.types) {
+        JS_FreeValue(pack.control, t->control);
+        JS_FreeValue(pack.audio, t->audio);
     }
-    types_.clear();
-    typeList_.clear();
-    package_ = rp_package{RP_ABI_VERSION, kPackageManifest, 0, nullptr};
+    pack.types.clear();
+    pack.typeList.clear();
+    pack.package = rp_package{RP_ABI_VERSION, pack.manifest.c_str(), 0, nullptr};
 }
 
 void ScriptHost::setAudioThreadOwns(bool owns) {
     audioOwns_.store(owns, std::memory_order_release);
     if (owns) return;
     releaseQueued();
-    collect();  // the runtime is this thread's again: what the audio thread's releases left in cycles goes now
+    collect();  // the runtimes are this thread's again: what the audio thread's releases left in cycles goes now
 }
 
 void ScriptHost::setCallDeadline(std::chrono::milliseconds limit) {
@@ -1522,12 +1594,14 @@ void ScriptHost::beginBlock() {
     }
 }
 
-// The promise jobs the block's JS queued (its process calls, and whatever the adoption before it ran), bounded so a
-// reaction that queues another every time cannot hold the block, and held to one call deadline. A job that fails (a
-// queueMicrotask callback that threw, the deadline, or JS out of memory: a promise reaction's own throw only rejects
-// its promise) is counted as a node's throw is, and one the deadline stopped leaves the rest for the next block's end.
+// The promise jobs the block's JS queued (its process calls, and whatever the adoption before it ran), each pack's
+// bounded so a reaction that queues another every time cannot hold the block, and held to one call deadline. A job
+// that fails (a queueMicrotask callback that threw, the deadline, or JS out of memory: a promise reaction's own throw
+// only rejects its promise) is counted as a node's throw is, and one the deadline stopped leaves the rest of its pack's
+// for the next block's end. This reads the pack list on the block thread, which nothing else there does (packs_).
 void ScriptHost::endBlock() {
-    if (JS_IsJobPending(audioRt_)) runAudioJobs();
+    for (const auto& pack : packs_)
+        if (JS_IsJobPending(pack->audioRt)) runAudioJobs(*pack);
 #ifdef RETROPLUG_PROFILE
     // The allocation window's per-block counts, the promise jobs included.
     const ScriptAllocations now = audioAllocations();
@@ -1537,22 +1611,22 @@ void ScriptHost::endBlock() {
 #endif
 }
 
-void ScriptHost::runAudioJobs() {
-    JS_UpdateStackTop(audioRt_);
+void ScriptHost::runAudioJobs(Pack& pack) {
+    JS_UpdateStackTop(pack.audioRt);
     const Glue::Deadline call(*this);
     for (int n = 0; n < kJobsPerBlock; ++n) {
         JSContext* ctx = nullptr;
-        const int r = JS_ExecutePendingJob(audioRt_, &ctx);
+        const int r = JS_ExecutePendingJob(pack.audioRt, &ctx);
         if (r == 0) return;
         if (r > 0) continue;
-        Glue::recordThrow(*this, nullptr, "a promise job");
+        Glue::recordThrow(*this, ctx, nullptr, "a promise job");
         if (interrupted_) break;
     }
-    if (JS_IsJobPending(audioRt_)) jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
+    if (JS_IsJobPending(pack.audioRt)) jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
 }
 
-void ScriptHost::drainControlJobs() {
-    if (JS_IsJobPending(controlRt_)) runJobs(controlRt_, kJobsPerBlock, [](JSContext* ctx) { clearException(ctx); });
+void ScriptHost::drainControlJobs(Pack& pack) {
+    if (JS_IsJobPending(pack.controlRt)) runJobs(pack.controlRt, kJobsPerBlock, [](JSContext* ctx) { clearException(ctx); });
 }
 
 void ScriptHost::drainReleased() {
@@ -1612,17 +1686,20 @@ void ScriptHost::releaseQueued() {
 
 // Quiescent only, and never by a block: a GC's cost is the whole heap's, which a block cannot afford. QuickJS's own
 // threshold GC may still run inside one when JS allocates past it; this is what keeps a released instance's cycles
-// from waiting for that.
+// from waiting for that. Each pack's runtime is collected only when something has left garbage in it.
 void ScriptHost::collect() {
-    if (!garbage_) return;
-    garbage_ = false;
-    JS_UpdateStackTop(audioRt_);
-    JS_RunGC(audioRt_);
+    for (const auto& pack : packs_) {
+        if (!pack->garbage) continue;
+        pack->garbage = false;
+        JS_UpdateStackTop(pack->audioRt);
+        JS_RunGC(pack->audioRt);
+    }
 }
 
 void ScriptHost::releaseJs(Instance& in) {
-    JSContext* ctx = audio_;
-    JS_UpdateStackTop(audioRt_);
+    Pack& pack = Glue::packOf(in);
+    JSContext* ctx = pack.audio;
+    JS_UpdateStackTop(pack.audioRt);
     if (in.layout) Glue::freeLayoutJs(ctx, *in.layout);
     if (!JS_IsUndefined(in.block)) JS_SetOpaque(in.block, nullptr);  // a block JS kept is no longer this instance's
     for (JSValue* v : {&in.object, &in.process, &in.onSettings, &in.portsChanged, &in.activate, &in.block, &in.transport}) {
@@ -1631,7 +1708,7 @@ void ScriptHost::releaseJs(Instance& in) {
     }
     if (in.state == Instance::State::Live) live_.fetch_sub(1, std::memory_order_relaxed);
     in.state = Instance::State::Released;
-    garbage_ = true;
+    pack.garbage = true;
 }
 
 ScriptAllocations ScriptHost::audioAllocations() const {
@@ -1675,16 +1752,19 @@ bool ScriptHost::resetAllocStats(bool disableAutoGc) {
     maxBlockAllocs_.store(0, std::memory_order_relaxed);
     maxBlockBytes_.store(0, std::memory_order_relaxed);
     // QuickJS checks its threshold only as it makes an object, so SIZE_MAX means a collection never starts by itself.
-    if (disableAutoGc) JS_SetGCThreshold(audioRt_, static_cast<std::size_t>(-1));
+    if (disableAutoGc)
+        for (const auto& pack : packs_) JS_SetGCThreshold(pack->audioRt, static_cast<std::size_t>(-1));
     return true;
 }
 
 DspGcResult ScriptHost::runGc() {
     if (refuseWhileOwned("a GC pass")) return {};  // enabled false
-    JS_UpdateStackTop(audioRt_);
     const std::int64_t before = counters_.liveBytes.load(std::memory_order_relaxed);
     const auto t0 = std::chrono::steady_clock::now();
-    JS_RunGC(audioRt_);
+    for (const auto& pack : packs_) {
+        JS_UpdateStackTop(pack->audioRt);
+        JS_RunGC(pack->audioRt);
+    }
     DspGcResult r;
     r.enabled = true;
     r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index 0e15e278..3f572d8c 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -1,6 +1,5 @@
 #pragma once
 
-#include <array>
 #include <atomic>
 #include <chrono>
 #include <cstddef>
@@ -8,6 +7,7 @@
 #include <memory>
 #include <span>
 #include <string>
+#include <unordered_map>
 #include <utility>
 #include <vector>
 
@@ -20,9 +20,9 @@ struct JSContext;
 
 namespace rp::host {
 
-// What the audio-side runtime's heap has done since the host was made, cumulative: every block, adoption and
-// release that entered JS. A test reads it either side of a stretch of blocks. Written only by the thread that runs
-// blocks; any thread may read it, a read on another thread lagging a running audio thread.
+// What the audio-side runtimes' heaps have done since the host was made, summed over every pack, cumulative: every
+// block, adoption and release that entered JS. A test reads it either side of a stretch of blocks. Written only by the
+// thread that runs blocks; any thread may read it, a read on another thread lagging a running audio thread.
 struct ScriptAllocations {
     std::uint64_t allocs = 0;    // malloc and calloc calls
     std::uint64_t reallocs = 0;
@@ -30,14 +30,14 @@ struct ScriptAllocations {
     std::uint64_t bytes = 0;     // asked for, by allocs and growing reallocs
 };
 
-// ---- profiling the audio runtime (spec/08-profiling.md) ---------------------------------------------------------
+// ---- profiling the audio runtimes (spec/08-profiling.md) --------------------------------------------------------
 // Real only in a RETROPLUG_PROFILE build; in any other every field is 0 and `enabled` false. They cross the harness
 // RPCs (dspAllocStats, dspRunGc, dspTrace) as they are, so their field names are what packages/retroplug/src/
-// audioDriver.ts reads. "dsp" is the DSP (audio) thread, whose JS the script host's audio runtime runs.
+// audioDriver.ts reads. "dsp" is the DSP (audio) thread, whose JS the script host's audio runtimes run.
 
-// The allocation window: what the audio runtime did since the last resetAllocStats(). `liveBytesDelta` is the net
-// change of its live heap (flat ~0: refcount churn; rising: a leak). The block counts are per Engine block, each
-// measured from its beginBlock to its endBlock, so a benchmark reads them once per window.
+// The allocation window: what the audio runtimes did since the last resetAllocStats(), every pack's summed.
+// `liveBytesDelta` is the net change of their live heap (flat ~0: refcount churn; rising: a leak). The block counts are
+// per Engine block, each measured from its beginBlock to its endBlock, so a benchmark reads them once per window.
 struct DspAllocStats {
     bool          enabled            = false;
     std::uint64_t allocCalls         = 0;
@@ -51,7 +51,8 @@ struct DspAllocStats {
     std::uint64_t maxBlockAllocBytes = 0;
 };
 
-// One self-driven collection pass (JS_RunGC): `freedBytes` ~0 proves the runtime accumulates no reference cycles.
+// One self-driven collection pass (JS_RunGC of every pack's audio runtime): `freedBytes` ~0 proves they accumulate no
+// reference cycles.
 struct DspGcResult {
     bool         enabled    = false;
     double       ms         = 0.0;
@@ -67,11 +68,12 @@ struct DspTraceSpan {
 };
 
 // The span labels. A script node's process call is labelled by its type, so a profile shows each type's cost, as the
-// DSP kernel's per-role spans did inside its one node until B3b made each role a node of its own: the bundle's types
-// take DSP_SPAN_TYPE_BASE up, in the order it registered them, each named by its id. The label is the type's, not the
-// node's, as the kernel's role spans were per role kind; two systems' nodes of one type still show as two spans a
-// block (each its own process call, in plan order). A type's label is fixed at the load, where one per node id would
-// come and go with every document, and a node's create is never told its id.
+// DSP kernel's per-role spans did inside its one node until B3b made each role a node of its own: the packs' types
+// take DSP_SPAN_TYPE_BASE up, in the order they were registered, each named by its id. The label is the type's, not
+// the node's, as the kernel's role spans were per role kind; two systems' nodes of one type still show as two spans a
+// block (each its own process call, in plan order). A type id's label is given at its first registration and kept for
+// the host's life, so one registered again has the label it had, where one per node id would come and go with every
+// document, and a node's create is never told its id.
 enum DspSpanLabel : std::uint32_t {
     DSP_SPAN_SCRIPT_NODE = 0,   // ScriptHost: the process call of a script node whose type has no label of its own (a
                                 //   bundle registering more types than kDspSpanLabels leaves room for)
@@ -82,44 +84,51 @@ enum DspSpanLabel : std::uint32_t {
                                 //   mixing, the host outputs), holding its script nodes' process spans
     DSP_SPAN_PUBLISH     = 4,   // Engine: the state pump to the control plane (the per-block frame copy, and the timed
                                 //   savestate and SRAM republish, so spiky)
-    DSP_SPAN_TYPE_BASE   = 16,  // ScriptHost: the process call of a script node of the bundle's first type, and up
+    DSP_SPAN_TYPE_BASE   = 16,  // ScriptHost: the process call of a script node of the first type registered, and up
 };
 
 // Every label a trace names is below this: the fixed ones, then one per script node type (the production bundle's are
-// in packages/retroplug/src/script/bundle.ts), so the name table is bounded whatever a bundle registers.
+// in packages/retroplug/src/script/bundle.ts), so the name table is bounded whatever the packs register.
 inline constexpr std::uint32_t kDspSpanLabels = 1024;
 
 // The script host (docs/modular-graph-phase-b3a-design.md section 2): every TS node one Engine runs, behind the
-// same C ABI as a native node. A bundle registers its node definitions (defineNode results, appended to the global
-// __rp_scriptNodes: packages/retroplug/src/script/register.ts), and the host registers an rp_node_type for each in
-// one package, "rp.script", whose instances are native shims.
+// same C ABI as a native node. It holds them in packs (docs/modular-graph-phase-c1-design.md section 4), the unit it
+// isolates and loads: a pack has an id, a bundle that registers its node definitions (defineNode results, appended to
+// the global __rp_scriptNodes: packages/retroplug/src/script/register.ts), and a package of its own, named by its id,
+// in which the host registers an rp_node_type for each definition, whose instances are native shims. The host holds
+// one pack, the built-in one, "rp.script", which load fills with a bundle (an Engine's is embeddedBundle()).
 //
-// Two bare QuickJS runtimes, each loaded with the same bundle and sharing no JS value:
+// A pack is two bare QuickJS runtimes of its own, each loaded with its bundle, sharing no JS value with each other or
+// with another pack's:
 //   - the CONTROL side, entered only by the control thread, answers layout_for and prepare_settings: the
 //     definition's own parseSettings checks a settings object, and its layout gives the ports, from the settings
 //     alone. It never holds an instance.
-//   - the AUDIO side holds every instance and is entered only by the thread that runs blocks (the audio thread, or
-//     the control thread while setAudioThreadOwns(false)). A shim's create and activate run on the control thread
-//     and only allocate the shim; the JS instance is created on the block thread at the first call the shim gets
-//     there, which is the ports_changed the planner sends every fresh instance when its plan is adopted, so before
-//     that plan's first process. A shim's destroy (control thread) queues the instance's release, which beginBlock
-//     performs at the start of a later block (at most kReleasesPerBlock a block), or does it there and then while no
-//     audio thread runs; the record it frees then goes back to the control thread to be deleted (reclaim), so the
-//     audio thread frees nothing native. What a released instance leaves in cycles is collected by a GC at two
-//     points no block is running: the reclaim that ends a quiescent operation which released some (the Engine's
-//     after every setGraph: one GC however many went), and the audio thread handing the runtime back
-//     (setAudioThreadOwns(false)). The first reclaim after the load (the top of the first setGraph) takes what the bundle's
-//     load left in cycles too, whether or not an instance released anything. Never by a
-//     block, so while an audio thread owns the runtime (a live session) those cycles wait for QuickJS's own threshold
-//     GC, which runs inside whichever block's JS allocates past it, or for the audio thread to hand the runtime back.
+//   - the AUDIO side holds every instance of the pack's types and is entered only by the thread that runs blocks (the
+//     audio thread, or the control thread while setAudioThreadOwns(false)), which reaches it only through an instance.
+//     A shim's create and activate run on the control thread and only allocate the shim; the JS instance is created on
+//     the block thread at the first call the shim gets there, which is the ports_changed the planner sends every fresh
+//     instance when its plan is adopted, so before that plan's first process. A shim's destroy (control thread) queues
+//     the instance's release, which beginBlock performs at the start of a later block (at most kReleasesPerBlock a
+//     block), or does it there and then while no audio thread runs; the record it frees then goes back to the control
+//     thread to be deleted (reclaim), so the audio thread frees nothing native. What a released instance leaves in
+//     cycles is collected by a GC of its pack's audio runtime at two points no block is running: the reclaim that ends
+//     a quiescent operation which released some (the Engine's after every setGraph: one GC a pack however many went),
+//     and the audio thread handing the runtimes back (setAudioThreadOwns(false)). The first reclaim after a pack's load
+//     (for the built-in one, the top of the first setGraph) takes what its bundle's load left in cycles too, whether or
+//     not an instance released anything. Never by a block, so while an audio thread owns the runtimes (a live session)
+//     those cycles wait for QuickJS's own threshold GC, which runs inside whichever block's JS allocates past it in
+//     that runtime, or for the audio thread to hand the runtimes back.
+// What every pack shares stays the host's: the call deadline (one call runs at a time on the block thread), the
+// allocation counters (summed over every pack's audio runtime, so audioAllocations and the profile's allocation window
+// mean the whole audio side), the release rings, the throw count and the first throw, and the profile's span labels.
 //
-// Both runtimes' JS stacks are held to kStackBytes, under the smallest thread that may enter one: 512 KiB is a macOS
+// Every runtime's JS stack is held to kStackBytes, under the smallest thread that may enter one: 512 KiB is a macOS
 // secondary thread's default, which a DAW's audio or control thread may have and a render job's std::thread does
 // (no host here sizes its threads), so 256 KiB leaves the caller's frames half. Loading the production bundle needs
 // 4 KiB of it; the test bundle with its toys, planned (their settings' parse, refusal and JSON schema, measured while
 // those were zod's, before B3b), 40 KiB, 64 under TSan and 96 under ASan.
 //
-// Every call into the audio runtime from the block thread is held to a deadline (setCallDeadline) of the calling
+// Every call into an audio runtime from the block thread is held to a deadline (setCallDeadline) of the calling
 // thread's CPU time, so a stall that is not the node's (preemption, a stopped process, a debugger or a VM pause) never
 // trips it. A call that runs past it is stopped (QuickJS's interrupt, which JS cannot catch) and counted as a throw.
 // A node with faultOnThrow then faults; one without runs again next block (design section 4.5), and faults only if
@@ -143,8 +152,8 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // block allocates nothing native, and a node written with indexed loops nothing in JS either. What a block hands out
 // is good for that process call: keep copies.
 //
-// The promise jobs a block's JS queues run at the end of the block (endBlock), at most kJobsPerBlock of them, so a
-// reaction that queues another every time cannot hold the block: the rest wait for the next block's end.
+// The promise jobs a block's JS queues run at the end of the block (endBlock), at most kJobsPerBlock of each pack's,
+// so a reaction that queues another every time cannot hold the block: the rest wait for the next block's end.
 //
 // A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
 // (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch); a
@@ -153,7 +162,7 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // block path makes onto what the node holds that fails (a setter the node hung there threw or ran past the deadline, or
 // it froze the object) is the node's throw too: messages() and control() throw it, and on its audio views, the Block
 // object or its transport, written before process is called, it is a throw in process whose block does not run (its
-// audio outputs silent). Every node shares the one realm, so the host keeps its writes, and its reads of a node's
+// audio outputs silent). Every node of a pack shares its realm, so the host keeps its writes, and its reads of a node's
 // methods, off the prototypes they all share. Each field it fills it makes the object's own data property when it makes
 // the object (a define, which never walks the prototype chain), and each slot of an array it writes as the slot is at
 // that write: set when it is the array's own, defined when it is not (never made, or taken back by the node with a
@@ -167,28 +176,29 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // Proxy, fails the create rather than holding the block thread, where the deadline has no JS to stop. And it freezes
 // the Block prototype every node reaches, with its methods, so no node replaces one for another: assigning one on a
 // node's own Block (`block.send = f`) throws in the strict bundle, where defining it there still works. That is the
-// host's part only: what nodes can do to each other through the realm they share (an accessor on a prototype another's
-// own code reads through, a global it calls) it does not cover, and one context per node is what would close that
-// (spec/07-remaining-work.md).
+// host's part only: what the nodes of a pack can do to each other through the realm they share (an accessor on a
+// prototype another's own code reads through, a global it calls) it does not cover. Only nodes of different packs are
+// kept apart, each pack's realm in runtimes of its own (docs/modular-graph-phase-c1-design.md section 12).
 //
 // Settings reach the audio side as their JSON form: the control side's parseSettings checks them, and what it
-// returns crosses as JSON (the two runtimes share no value), so a transform's result that JSON cannot hold (a Map, a
-// Date, undefined) arrives as JSON spells it. The audio side never runs the schema, so it does no validation work at
+// returns crosses as JSON (a pack's two runtimes share no value), so a transform's result that JSON cannot hold (a
+// Map, a Date, undefined) arrives as JSON spells it. The audio side never runs the schema, so it does no validation work at
 // a swap (and no node's schema is zod's: since B3b the bundle holds none, graph/settingsSchema.ts's instead).
 //
-// A profile build (RETROPLUG_PROFILE, spec/08-profiling.md) profiles the audio runtime: its allocator keeps the live
-// and peak heap, an allocation window counts per block, and a trace window records nested wall-time spans. Every
-// script node's process is a span labelled by its type (DspSpanLabel), holding a marshal span for the audio copies in
-// and the transport and a js-call span for its JS, inside which each input it reads is a marshal span too. Only the
-// host opens spans, and it closes each on every way out of the call (a throw, the deadline, a write onto what the node
-// holds refused before its JS runs). JS adds none: the DSP kernel's roles were the only JS that did, through globals a
-// profile build bound into the audio context, and with each role a node of its own the host's spans attribute per
-// role without them.
+// A profile build (RETROPLUG_PROFILE, spec/08-profiling.md) profiles the audio runtimes: their allocator keeps the
+// live and peak heap, every pack's summed, an allocation window counts per block, and a trace window records nested
+// wall-time spans. Every script node's process is a span labelled by its type (DspSpanLabel), holding a marshal span
+// for the audio copies in and the transport and a js-call span for its JS, inside which each input it reads is a
+// marshal span too. Only the host opens spans, and it closes each on every way out of the call (a throw, the deadline,
+// a write onto what the node holds refused before its JS runs). JS adds none: the DSP kernel's roles were the only JS
+// that did, through globals a profile build bound into the audio context, and with each role a node of its own the
+// host's spans attribute per role without them.
 class ScriptHost {
 public:
     using Chunk = std::span<const std::uint8_t>;
-    // Globals a host binds into one of the contexts before any bundle runs in it (the native tests' probes),
-    // `audioSide` saying which; called once for each, on the constructing thread.
+    // Globals a host binds into one of a pack's contexts before any bundle runs in it (the native tests' probes),
+    // `audioSide` saying which; called once for each of every pack's two, on the thread that makes the pack (the
+    // constructing thread, for the built-in one).
     using Binder = void (*)(void* user, JSContext* ctx, bool audioSide);
 
     explicit ScriptHost(Binder binder = nullptr, void* user = nullptr);
@@ -202,25 +212,27 @@ public:
     static Chunk embeddedBundle();
 
     // ---- control thread ------------------------------------------------------------------------------
-    // Run a bundle (global-code bytecode, in chunks run in order) in both contexts, then read the definitions it
-    // registered. Once, before anything plans with the package: a second call is refused, and leaves what the first
-    // registered as it is. False, with a reason, when a chunk fails to load or a definition cannot be read or is not
-    // the same in both contexts (its id or its descriptor differs: a bundle that branches on which context it runs
-    // in). One bad definition refuses the whole package: it then has no types, so every document naming any of the
-    // bundle's is refused (unknown-type).
+    // Load the built-in pack: run a bundle (global-code bytecode, in chunks run in order) in both of its contexts, then
+    // read the definitions it registered. Once, before anything plans with the package: a second call is refused, and
+    // leaves what the first registered as it is. False, with a reason, when a chunk fails to load or a definition
+    // cannot be read or is not the same in both contexts (its id or its descriptor differs: a bundle that branches on
+    // which context it runs in). One bad definition refuses the whole package: it then has no types, so every document
+    // naming any of the bundle's is refused (unknown-type).
     bool load(std::span<const Chunk> chunks, std::string* error = nullptr);
-    // The script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a load).
-    const rp_package* package() const { return &package_; }
+    // The built-in pack's script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a
+    // load).
+    const rp_package* package() const;
     // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
     // queued instance there and then, and collects what they left.
     void setAudioThreadOwns(bool owns);
-    // How much of the calling thread's CPU time one call into the audio runtime may use before it is stopped
+    // How much of the calling thread's CPU time one call into an audio runtime may use before it is stopped
     // (kDefaultCallDeadline until set). Set it before an audio thread runs blocks.
     void setCallDeadline(std::chrono::milliseconds limit);
     static constexpr std::chrono::milliseconds kDefaultCallDeadline{2000};
     // Delete the records the block thread has released, and log the first throw once. While quiescent, also collect
-    // what instances released since the last left in cycles, and, the first time after the load, what the bundle's load
-    // left (whether or not anything was released): call it after an operation that may release some.
+    // what instances released since the last left in cycles, and, the first time after a pack's load, what its bundle's
+    // load left (whether or not anything was released), in each pack that has some: call it after an operation that
+    // may release some.
     void reclaim();
     // The first throw's type and message ("" until one), once the block thread has kept it.
     std::string firstThrow() const;
@@ -231,8 +243,8 @@ public:
     // going at once is spread over blocks, and a full ring still drains in 16).
     void beginBlock();
     static constexpr std::uint32_t kReleasesPerBlock = 16;
-    // At the end of every block that ran the graph: the promise jobs its JS queued, at most kJobsPerBlock of them,
-    // then the allocation window's per-block counts (a profile build).
+    // At the end of every block that ran the graph: the promise jobs its JS queued, at most kJobsPerBlock of each
+    // pack's, then the allocation window's per-block counts (a profile build).
     void endBlock();
     static constexpr int kJobsPerBlock = 256;
     static constexpr std::size_t kStackBytes = 256 * 1024;  // each runtime's JS stack (the class comment)
@@ -253,11 +265,11 @@ public:
     std::uint32_t     jobsCutShort() const { return jobsCutShort_.load(std::memory_order_relaxed); }
 
     // ---- profiling (spec/08-profiling.md): real only in a RETROPLUG_PROFILE build --------------------------------
-    // The allocation window, opened by resetAllocStats, which can pin the audio runtime's auto-GC off from then on (so
-    // a window collects nothing); runGc runs and times one collection there. allocStats never refuses (a benchmark
-    // reads its `enabled` to know a profile host), and read on another thread while blocks run lags them.
+    // The allocation window, opened by resetAllocStats, which can pin every pack's audio runtime's auto-GC off from
+    // then on (so a window collects nothing); runGc runs and times one collection of each. allocStats never refuses (a
+    // benchmark reads its `enabled` to know a profile host), and read on another thread while blocks run lags them.
     DspAllocStats allocStats() const;
-    // These refuse, doing nothing, while an audio thread owns the runtime (setAudioThreadOwns): resetAllocStats and
+    // These refuse, doing nothing, while an audio thread owns the runtimes (setAudioThreadOwns): resetAllocStats and
     // traceReset return false and runGc a result with enabled false (the first refusal said once on stderr), and the
     // trace reads, traceSpans and traceNames, return nothing, silently. The check is sound because these and
     // setAudioThreadOwns are called on the same control thread: an audio thread starts only after that thread has set
@@ -266,7 +278,7 @@ public:
     DspGcResult   runGc();
     // The trace window. traceReset clears it and arms (or disarms) it, quiescent, as are the reads; the block thread
     // records, and only while it is armed, so a window armed before an audio thread starts records (and grows) until
-    // that thread hands the runtime back. spanBegin and spanEnd bracket a span; spanSince records one from `t0`, a
+    // that thread hands the runtimes back. spanBegin and spanEnd bracket a span; spanSince records one from `t0`, a
     // traceNow(), to now. traceName names a label below kDspSpanLabels, and ignores one past it.
     void                      spanBegin(std::uint32_t label);
     void                      spanEnd();
@@ -278,12 +290,13 @@ public:
     std::vector<DspTraceSpan> traceSpans() const;
     std::vector<std::string>  traceNames() const;
 
-    // Defined in ScriptHost.cpp: a script type, and one instance's record.
+    // Defined in ScriptHost.cpp: a pack, a script type, and one instance's record.
+    struct Pack;
     struct Type;
     struct Instance;
 
 private:
-    struct Glue;  // the ABI tables, the Block object's methods and the audio runtime's allocator
+    struct Glue;  // the ABI tables, the Block object's methods and the audio runtimes' allocator
     struct Counters {
         std::atomic<std::uint64_t> allocs{0}, reallocs{0}, frees{0}, bytes{0};
         // The live heap and its peak, in usable bytes: kept by a profile build only.
@@ -291,30 +304,37 @@ private:
         std::atomic<std::uint64_t> peakBytes{0};
     };
 
+    // control thread: a pack named `id` with its two runtimes made (makeRuntimes), loaded with nothing yet
+    std::unique_ptr<Pack> makePack(const std::string& id);
+    void makeRuntimes(Pack& pack);  // control thread: its runtimes and contexts, set up as the class comment says
+    void freeRuntimes(Pack& pack);  // control thread or quiescent: its types first (freeTypes), then its runtimes
+    // control thread: run a bundle in both of the pack's contexts, read and check its definitions, and label its types
+    bool loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error);
+    void freeTypes(Pack& pack);
+    std::uint32_t spanLabelFor(const std::string& typeId);  // control thread: a type id's label (DspSpanLabel)
     void releaseJs(Instance& in);   // the block thread, or quiescent
     void retire(Instance* in);      // control thread: a destroyed shim's record
     void releaseQueued();           // quiescent: everything queued, released and deleted
     void drainReleased();           // control thread: the records handed back, deleted, and the backlog moved on
-    void collect();                 // quiescent: a GC of the audio runtime, when the load or a release has left
-                                    //   garbage since the last
-    void drainControlJobs();        // control thread: the promise jobs a call into the control context queued
-    void runAudioJobs();            // the block thread: endBlock's promise jobs
-    void freeTypes();
+    void collect();                 // quiescent: a GC of each pack's audio runtime whose load or releases have left
+                                    //   garbage since its last
+    static void drainControlJobs(Pack& pack);  // control thread: the jobs a call into its control context queued
+    void runAudioJobs(Pack& pack);  // the block thread: endBlock's promise jobs, one pack's
 
-    // counters_ is the audio runtime's allocator opaque, so it is declared (and made) before it.
+    // counters_ is every audio runtime's allocator opaque, so it is declared (and made) before them.
     Counters                     counters_;
-    JSRuntime*                   controlRt_ = nullptr;
-    JSContext*                   control_ = nullptr;
-    JSRuntime*                   audioRt_ = nullptr;
-    JSContext*                   audio_ = nullptr;
-    std::uint32_t                blockClass_ = 0;  // the JSClassID of the Block object, in the audio runtime
-    std::array<std::uint32_t, 24> atoms_{};        // JSAtoms the block thread names properties by, in the audio runtime
-    const void*                  objectProto_ = nullptr;  // the audio context's Object.prototype, by address
-
-    std::vector<std::unique_ptr<Type>> types_;
-    std::vector<const rp_node_type*>   typeList_;
-    rp_package                         package_{};
+    Binder                       binder_ = nullptr;
+    void*                        binderUser_ = nullptr;
+    // Every pack, the built-in one first. Made, loaded and read on the control thread (and quiescent, by releaseQueued,
+    // collect and the profile calls); the block thread reaches a pack only through an instance of one of its types,
+    // never through this list. endBlock is the one exception: it runs every pack's promise jobs, which is sound while
+    // the list is fixed from the constructor on, as it is with the built-in pack the only one.
+    std::vector<std::unique_ptr<Pack>> packs_;
     bool                               loaded_ = false;  // load has been called
+    // Each type id's span label (DspSpanLabel), given at its first registration in any pack and kept, and the next one
+    // to give (control thread).
+    std::unordered_map<std::string, std::uint32_t> spanLabels_;
+    std::uint32_t                                  nextSpanLabel_ = DSP_SPAN_TYPE_BASE;
 
     std::atomic<bool>          audioOwns_{false};
     // destroy -> the block thread, and the released records back. A full release ring spills into the backlog (the
@@ -327,7 +347,6 @@ private:
     Instance*                  backlogTail_ = nullptr;
     std::atomic<std::uint32_t> leakedRecords_{0};
     std::uint32_t              leaksReported_ = 0;
-    bool                       garbage_ = false;  // the load, or a release, since the last collect (whoever releases)
 
     // The call deadline: its length in CPU nanoseconds (set on the control thread), and while a call runs (armed_), the
     // thread's CPU time at the first poll in it (-1 until then) and whether the interrupt stopped it (the block thread's).
````

- [ ] **Step 3: Run the tests: nothing moves**

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (18011 assertions in 123 test cases)` (unchanged).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `tools/run-profile.sh unit`
Expected: `All tests passed (132 assertions in 6 test cases)`.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[script]"`
Expected: `All tests passed (803 assertions in 49 test cases)`, no ThreadSanitizer report.

- [ ] **Step 4: Commit**

````bash
git add -- packages/native/src/host/script/ScriptHost.cpp packages/native/src/host/script/ScriptHost.hpp
git commit -F - <<'EOF'
refactor(script-host): a script host holds packs, each its own runtime pair; the built-in bundle is the first

What the script host held per runtime is a pack's now (docs/modular-graph-phase-c1-design.md section 4): its id and
generation, its manifest and package, its control and audio runtimes and contexts, the Block class, the atoms, the
address of Object.prototype, its types and its garbage flag. makeRuntimes and freeRuntimes make and free a pack's
runtimes as the constructor and destructor did; loadInto runs a bundle in a given pack, and load loads the built-in
pack, rp.script, through it, its contract unchanged. A Type names its pack, and the Glue reaches the audio side
through the instance's type's pack, a Block method through its context's (the audio context's opaque is the pack).

The call deadline, the counters, the release rings, the throw count and first throw and the profile windows stay the
host's, shared by every pack; collect, runGc and the auto-GC pin act on every pack. Span labels come from a host-level
map from type id to label, given in registration order, so the built-in types keep 16 up. endBlock runs every pack's
jobs, the one place the block thread reads the pack list. No behaviour change.
EOF
````

### Task 2: a node pays for its own jobs and memory

Design section 6. A node's promise jobs run at a checkpoint after each call the block thread makes into it, under that
call's deadline, charged to it; each pack's audio runtime is held to a memory limit (64 MiB by default) by the host's
allocator, and a call during which its pack refused an allocation faults its node. No production node uses a promise
or comes near the limit, so parity cannot move.

**Files:**
- Modify: `packages/native/src/host/script/ScriptHost.cpp`, `packages/native/src/host/script/ScriptHost.hpp`, `packages/native/test/engine-graph/ScriptNodes.test.cpp`, `packages/native/test/engine-graph/ScriptRig.hpp`, `packages/retroplug/src/graph/defineNode.ts`

- [ ] **Step 1: Write the failing tests**

The four `[jobs]` and deadline cases of `ScriptNodes.test.cpp` encode today's rule (every job at the block's end,
charged to no node); each moves to the new one: a reaction runs between its node's `process` and the next node's; an
endless chain is cut short at a checkpoint's bound (`kJobsPerCheckpoint`), charged to its node as a throw, and faults
it on the second cut short in a row; a failing `queueMicrotask` callback is its node's throw, named by its type, and
faults a `faultOnThrow` node at once; a reaction that never returns is its node's call stopped by the deadline. Two
cases are new: a node's leftover chain run at a pack-mate's checkpoint is charged to it, never to the pack-mate, and
to no node once it is removed (`[pack-mate]`); and with the toys loaded under a 16 MiB limit, a node that allocates
past it faults at once whether or not it catches the error, while a pack-mate that allocates nothing runs on
(`[memory]`). The rig passes the limit through:

````diff
diff --git a/packages/native/test/engine-graph/ScriptNodes.test.cpp b/packages/native/test/engine-graph/ScriptNodes.test.cpp
index 3debfb8b..61d43423 100644
--- a/packages/native/test/engine-graph/ScriptNodes.test.cpp
+++ b/packages/native/test/engine-graph/ScriptNodes.test.cpp
@@ -1,16 +1,18 @@
-// The script-node runtime (docs/modular-graph-phase-b3a-design.md section 2): TS nodes behind the same C ABI as
-// native ones, one runtime per engine. Toy nodes, written here as JS source on the authoring layer the test bundle
-// exposes (ScriptRig.hpp), are planned and run beside the graph tests' C++ sources and sinks: what reaches them
-// through Block, what they send, the control-side context laying their ports out from settings, a node kept across
-// a reshape or swapped or rebuilt, where their instances are made and released, a throw (a refused write onto what a
-// node holds among them, while the accessors one hangs on the shared prototypes charge no other, a method only
-// Object.prototype holds is adopted by none, and none replaces a method of the frozen Block prototype), the block path's
-// allocations, and a running audio thread under all of it. The embedded production bundle and an Engine built
-// around a toy bundle close it. A RETROPLUG_PROFILE build (build-prof/, which tools/run-profile.sh configures) adds the
-// trace window's cases, [profile]: a script node's process span labelled by its type and closed every way a call ends,
-// a label per type in load order, the profile calls refused while an audio thread owns the runtime, the types past
-// the label table's bound, the embedded bundle's types labelled in the order the docs quote, and an Engine's apu-render
-// spanning its plan's whole run, its script nodes' process spans inside it.
+// The script-node runtime (docs/modular-graph-phase-b3a-design.md section 2): TS nodes behind the same C ABI as native
+// ones, run in their pack's runtimes (here the built-in pack's, the test bundle and its toys loaded into it). Toy
+// nodes, written here as JS source on the authoring layer the test bundle exposes (ScriptRig.hpp), are planned and run
+// beside the graph tests' C++ sources and sinks: what reaches them through Block, what they send, the control-side
+// context laying their ports out from settings, a node kept across a reshape or swapped or rebuilt, where their
+// instances are made and released, a throw (a refused write onto what a node holds among them, while the accessors one
+// hangs on the shared prototypes charge no other, a method only Object.prototype holds is adopted by none, and none
+// replaces a method of the frozen Block prototype), a node's promise jobs run and charged to it at the checkpoint after
+// its call (a pack-mate never charged for them) and its pack's memory limit faulting it, the block path's allocations,
+// and a running audio thread under all of it. The embedded production bundle and an Engine built around a toy bundle
+// close it. A RETROPLUG_PROFILE build (build-prof/, which tools/run-profile.sh configures) adds the trace window's
+// cases, [profile]: a script node's process span labelled by its type and closed every way a call ends, a label per
+// type in load order, the profile calls refused while an audio thread owns the runtime, the types past the label
+// table's bound, the embedded bundle's types labelled in the order the docs quote, and an Engine's apu-render spanning
+// its plan's whole run, its script nodes' process spans inside it.
 //
 // Run via `pnpm test:plugin engine-graph`; the profile cases, build-prof/bin/retroplug-engine-graph-test "[profile]".
 
@@ -749,24 +751,43 @@ TEST_CASE("a view a script node keeps in module state outlives its instance as i
     CHECK(rig.scripts.throwCount() == 0);
 }
 
-TEST_CASE("a promise reaction a script node queues runs at the end of its block, and its memory is given back", "[script][jobs]") {
+TEST_CASE("a promise reaction a script node queues runs right after its own process, before the next node's, and its "
+          "memory is given back",
+          "[script][jobs]") {
     ScriptRig rig(R"JS(
 rp.register([rp.defineNode({
   id: "rp.toy.async",
-  ports: { reactions: rp.control.out() },
+  ports: { reactions: rp.control.out(), out: rp.events.out() },
   create: () => {
     let ran = 0;
     return {
       process(b) {
         b.emit("reactions", 0, ran);
-        Promise.resolve(1).then(() => { ++ran; });
+        if (ran < 2) __probe("process async");
+        Promise.resolve(1).then(() => { if (ran < 2) __probe("reaction async"); ++ran; });
       },
     };
   },
+}), rp.defineNode({
+  id: "rp.toy.after",  // fed by the async node, so it runs after it
+  ports: { in: rp.events.in() },
+  create: () => {
+    let n = 0;
+    return { process() { if (++n <= 2) __probe("process after"); } };
+  },
 })]);
 )JS");
-    REQUIRE(rig.set(doc(node("a", "rp.toy.async") + "," + node("p", "rp.test.probe"), wire("a.reactions", "p.control"))).ok);
-    rig.run(64, 4);
+    REQUIRE(rig.set(doc(node("a", "rp.toy.async") + "," + node("z", "rp.toy.after") + "," + node("p", "rp.test.probe"),
+                        wire("a.reactions", "p.control") + "," + wire("a.out", "z.in")))
+                .ok);
+    rig.run(64, 2);
+    std::vector<std::string> order;
+    for (const auto& [tag, thread] : rig.log.calls)
+        if (tag.find("async") != std::string::npos || tag.find("after") != std::string::npos) order.push_back(tag);
+    // Each block: the node's process, its reaction at the checkpoint after it, then the next node's process.
+    CHECK(order == std::vector<std::string>{"process async", "reaction async", "process after", "process async",
+                                            "reaction async", "process after"});
+    rig.run(64, 2);
     CHECK(rig.node<Probe>("p").controlAtZero == 3.0);  // each block sees every reaction the blocks before it queued
     const auto before = rig.scripts.audioAllocations();
     rig.run(64, 1000);
@@ -776,14 +797,16 @@ rp.register([rp.defineNode({
     INFO("1000 blocks: " << after.allocs - before.allocs << " allocations, " << after.frees - before.frees << " frees");
     CHECK(held <= 16);  // what a reaction allocates is freed once it has run: nothing grows with the blocks
     CHECK(rig.scripts.throwCount() == 0);
+    CHECK(rig.scripts.jobsCutShort() == 0);
 }
 
-TEST_CASE("promise reactions that queue another every time run a bounded number a block, the rest at the next block's end",
-          "[script][jobs]") {
-    ScriptRig rig(R"JS(
+// A promise chain that queues another link every time, started in its first block, and a reaction that throws, which
+// rejects its promise rather than failing its job; and, in the same pack, a node that does nothing but faults on any
+// throw it is charged, fed by the chain's node so it runs after it.
+const char* const kChain = R"JS(
 rp.register([rp.defineNode({
   id: "rp.toy.chain",
-  ports: { links: rp.control.out() },
+  ports: { links: rp.control.out(), out: rp.events.out() },
   create: () => {
     let links = 0, started = false;
     const link = () => { ++links; Promise.resolve().then(link); };
@@ -794,29 +817,91 @@ rp.register([rp.defineNode({
       },
     };
   },
+}), rp.defineNode({
+  id: "rp.toy.strict-mate",
+  faultOnThrow: true,
+  ports: { in: rp.events.in(), out: rp.events.out() },
+  create: () => {
+    let n = 0;
+    return { process(b) { b.send("out", 0, [++n & 0x7f]); } };
+  },
 })]);
-)JS");
+)JS";
+
+TEST_CASE("an endless promise chain is cut short after a checkpoint's bound, charged to its node as a throw, and faults it "
+          "on the second cut short in a row",
+          "[script][jobs]") {
+    ScriptRig rig(kChain);
     REQUIRE(rig.set(doc(node("c", "rp.toy.chain") + "," + node("p", "rp.test.probe"), wire("c.links", "p.control"))).ok);
     rig.run();
     CHECK(rig.node<Probe>("p").controlAtZero == 0.0);
+    // Its first checkpoint ran the first link's 256 jobs: 255 more links and the reaction that threw, which rejects its
+    // promise rather than failing the job. The rest are cut short: its throw, but the first in a row faults nothing.
     CHECK(rig.scripts.jobsCutShort() == 1);
+    CHECK(rig.scripts.throwCount() == 1);
+    CHECK_FALSE(rig.host->isFaulted("c"));
     rig.run();
-    // The link process ran, then the block's 256 jobs: 255 more links and the reaction that threw, which rejects its
-    // promise rather than failing the job.
-    CHECK(rig.node<Probe>("p").controlAtZero == 1.0 + (ScriptHost::kJobsPerBlock - 1));
-    rig.run();
-    CHECK(rig.node<Probe>("p").controlAtZero == 1.0 + (ScriptHost::kJobsPerBlock - 1) + ScriptHost::kJobsPerBlock);
-    CHECK(rig.scripts.jobsCutShort() == 3);
-    CHECK(rig.scripts.throwCount() == 0);
+    CHECK(rig.scripts.jobsCutShort() == 2);
+    CHECK(rig.scripts.throwCount() == 2);
+    CHECK(rig.host->isFaulted("c"));  // cut short again, at its next checkpoint: faulted there, so this block is silent
+    CHECK(rig.node<Probe>("p").controlAtZero == 0.0);
+    rig.run(64, 3);  // bypassed: no call, so no checkpoint, and the chain waits in its pack
+    CHECK(rig.scripts.jobsCutShort() == 2);
+    CHECK(rig.scripts.throwCount() == 2);
     rig.reclaim();  // says so, once
+    INFO(rig.scripts.firstThrow());
+    CHECK(rig.scripts.firstThrow().find("rp.toy.chain's promise jobs did not settle in the " +
+                                        std::to_string(ScriptHost::kJobsPerCheckpoint) + " a checkpoint runs") == 0);
+}
+
+TEST_CASE("a node's leftover promise chain is charged to it at a pack-mate's checkpoint, never to the pack-mate, and to no "
+          "node once it is removed",
+          "[script][jobs][pack-mate]") {
+    ScriptRig rig(kChain);
+    const std::string mate = node("m", "rp.toy.strict-mate") + "," + node("pm", "rp.test.probe");
+    REQUIRE(rig.set(doc(node("c", "rp.toy.chain") + "," + mate, wire("c.out", "m.in") + "," + wire("m.out", "pm.events"))).ok);
+    rig.run();
+    // The chain's own checkpoint cut it short once; the strict mate's, after it, ran 256 more charged to the chain, and
+    // cut it short a second time in a row: the chain faults at its next block, and the mate, which faults on any throw
+    // it is charged, runs on.
+    CHECK(rig.scripts.jobsCutShort() == 2);
+    CHECK(rig.scripts.throwCount() == 2);
+    CHECK_FALSE(rig.host->isFaulted("c"));
+    CHECK_FALSE(rig.host->isFaulted("m"));
+    for (int n = 2; n <= 5; ++n) {
+        rig.run();
+        INFO("block " << n);
+        CHECK(rig.host->isFaulted("c"));
+        CHECK_FALSE(rig.host->isFaulted("m"));
+        REQUIRE(rig.node<Probe>("pm").events.size() == 1);
+        CHECK(rig.node<Probe>("pm").events[0].data == bytes({n}));
+        // The chain goes on at the mate's checkpoint, 256 jobs a block, each run cut short and charged to the chain.
+        CHECK(rig.scripts.jobsCutShort() == static_cast<std::uint32_t>(n + 1));
+        CHECK(rig.scripts.throwCount() == static_cast<std::uint32_t>(n + 1));
+    }
+    // Removing the chain's node frees nothing in the runtime its closures live in, so the chain goes on, charged to no
+    // node: cut short, counted, and no one's throw.
+    REQUIRE(rig.set(doc(mate, wire("m.out", "pm.events"))).ok);
+    const std::uint32_t throws = rig.scripts.throwCount(), cut = rig.scripts.jobsCutShort();
+    for (int n = 6; n <= 9; ++n) {
+        rig.run();
+        INFO("block " << n);
+        CHECK_FALSE(rig.host->isFaulted("m"));
+        REQUIRE(rig.node<Probe>("pm").events.size() == 1);
+        CHECK(rig.node<Probe>("pm").events[0].data == bytes({n}));
+    }
+    CHECK(rig.scripts.throwCount() == throws);
+    CHECK(rig.scripts.jobsCutShort() > cut);
+    rig.reclaim();
+    CHECK(rig.scripts.firstThrow().find("rp.toy.chain") == 0);
 }
 
-TEST_CASE("a queueMicrotask callback that throws fails its job, which is counted; a promise reaction's throw only rejects its "
-          "promise",
+TEST_CASE("a queueMicrotask callback that throws is its node's throw, named by its type, and faults a faultOnThrow node; a "
+          "promise reaction's throw only rejects its promise",
           "[script][jobs]") {
     ScriptRig rig(R"JS(
-rp.register([rp.defineNode({
-  id: "rp.toy.microtask",
+const microtask = (id, extra) => rp.defineNode({
+  id, ...extra,
   ports: { out: rp.events.out() },
   create: () => {
     let n = 0;
@@ -829,11 +914,12 @@ rp.register([rp.defineNode({
       },
     };
   },
-})]);
+});
+rp.register([microtask("rp.toy.microtask", {}), microtask("rp.toy.strict-microtask", { faultOnThrow: true })]);
 )JS");
     REQUIRE(rig.set(doc(node("m", "rp.toy.microtask") + "," + node("p", "rp.test.probe"), wire("m.out", "p.events"))).ok);
     rig.run();
-    CHECK(rig.scripts.throwCount() == 1);  // at the block's end, as no one node's
+    CHECK(rig.scripts.throwCount() == 1);  // its own, at the checkpoint after its process
     CHECK_FALSE(rig.host->isFaulted("m"));
     rig.run();
     CHECK(rig.scripts.throwCount() == 1);
@@ -841,7 +927,70 @@ rp.register([rp.defineNode({
     CHECK(rig.node<Probe>("p").events[0].data == bytes({2}));
     rig.reclaim();
     INFO(rig.scripts.firstThrow());
-    CHECK(rig.scripts.firstThrow().find("a promise job threw: Error: from a microtask") == 0);
+    CHECK(rig.scripts.firstThrow().find("rp.toy.microtask threw in a promise job: Error: from a microtask") == 0);
+    REQUIRE(rig.set(doc(node("s", "rp.toy.strict-microtask") + "," + node("q", "rp.test.probe"), wire("s.out", "q.events"))).ok);
+    rig.run();
+    CHECK(rig.scripts.throwCount() == 2);
+    CHECK(rig.host->isFaulted("s"));  // at once: the block whose job failed is silent
+    CHECK(rig.node<Probe>("q").events.empty());
+}
+
+// Each allocates 1 MiB at a time on its second block, up to 64 MiB, and sends its block count every block: `hog`
+// catches the error a refused allocation throws, `glutton` lets it go. The idler allocates nothing.
+const char* const kHogs = R"JS(
+const hog = (id, catches) => rp.defineNode({
+  id,
+  ports: { out: rp.events.out() },
+  create: () => {
+    let n = 0;
+    return {
+      process(b) {
+        b.send("out", 0, [++n]);
+        if (n !== 2) return;
+        const kept = [];
+        if (!catches) { for (let k = 0; k < 64; ++k) kept.push(new Float64Array(1 << 17)); return; }
+        try { for (let k = 0; k < 64; ++k) kept.push(new Float64Array(1 << 17)); } catch (e) { __probe("caught " + id); }
+      },
+    };
+  },
+});
+rp.register([hog("rp.toy.hog", true), hog("rp.toy.glutton", false), rp.defineNode({
+  id: "rp.toy.idler",
+  ports: { out: rp.events.out() },
+  create: () => { let n = 0; return { process(b) { b.send("out", 0, [++n]); } }; },
+})]);
+)JS";
+
+TEST_CASE("a script node that allocates past its pack's memory limit faults at once, whether or not it catches the error, "
+          "and a node of the same pack that allocates nothing runs on",
+          "[script][memory]") {
+    constexpr std::size_t kLimit = std::size_t{16} << 20;
+    ScriptRig rig(kHogs, 256, 48000.0, kLimit);
+    REQUIRE(rig.set(doc(node("h", "rp.toy.hog") + "," + node("g", "rp.toy.glutton") + "," + node("i", "rp.toy.idler") + "," +
+                            node("ph", "rp.test.probe") + "," + node("pg", "rp.test.probe") + "," + node("pi", "rp.test.probe"),
+                        wire("h.out", "ph.events") + "," + wire("g.out", "pg.events") + "," + wire("i.out", "pi.events")))
+                .ok);
+    rig.run();
+    CHECK(rig.node<Probe>("ph").events.size() == 1);
+    CHECK(rig.node<Probe>("pg").events.size() == 1);
+    rig.run();  // both hogs run out
+    CHECK(rig.log.count("caught rp.toy.hog") == 1);
+    CHECK(rig.host->isFaulted("h"));  // at once, though it caught the error: the block is silent
+    CHECK(rig.host->isFaulted("g"));
+    CHECK(rig.node<Probe>("ph").events.empty());
+    CHECK(rig.node<Probe>("pg").events.empty());
+    CHECK(rig.scripts.throwCount() == 2);  // each hog's: the refusal the one caught, the throw the other let go
+    for (int n = 2; n <= 4; ++n) {
+        if (n > 2) rig.run();
+        INFO("block " << n);
+        CHECK_FALSE(rig.host->isFaulted("i"));
+        REQUIRE(rig.node<Probe>("pi").events.size() == 1);
+        CHECK(rig.node<Probe>("pi").events[0].data == bytes({n}));
+    }
+    CHECK(rig.scripts.throwCount() == 2);
+    rig.reclaim();
+    INFO(rig.scripts.firstThrow());
+    CHECK(rig.scripts.firstThrow().find("rp.toy.hog went past its pack's 16 MiB memory limit in process") == 0);
 }
 
 TEST_CASE("a script node's payloads grow their buffer when a block's outgrow it, and one JS detached is replaced",
@@ -1839,8 +1988,8 @@ TEST_CASE("a call that runs to its end between two expiries starts the count aga
     }
 }
 
-TEST_CASE("an endless recursion in a script node throws and it runs on, and a promise reaction that never returns is stopped at "
-          "its block's end",
+TEST_CASE("an endless recursion in a script node throws and it runs on, and a promise reaction that never returns is its "
+          "node's call stopped by the deadline",
           "[script][deadline]") {
     ScriptRig rig(R"JS(
 rp.register([rp.defineNode({
@@ -1848,25 +1997,30 @@ rp.register([rp.defineNode({
   ports: { out: rp.events.out() },
   create: () => { const down = (n) => down(n + 1) + 1; return { process(b) { b.send("out", 0, [2]); down(0); } }; },
 }), rp.defineNode({
-  id: "rp.toy.slowjob",  // a promise reaction that never returns, once
+  id: "rp.toy.slowjob",  // a promise reaction that never returns, every block
   ports: { out: rp.events.out() },
-  create: () => {
-    let queued = false;
-    return { process(b) { if (!queued) { queued = true; Promise.resolve().then(() => { for (;;) {} }); } } };
-  },
+  create: () => ({ process(b) { b.send("out", 0, [1]); Promise.resolve().then(() => { for (;;) {} }); } }),
 })]);
 )JS");
     rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
-    REQUIRE(rig.set(doc(node("j", "rp.toy.slowjob"), "")).ok);
+    REQUIRE(rig.set(doc(node("j", "rp.toy.slowjob") + "," + node("pj", "rp.test.probe"), wire("j.out", "pj.events"))).ok);
     rig.run();
+    // Its reaction runs at the checkpoint after its process, under the same deadline, which stops it: the node's call
+    // stopped, counted once, and it runs again next block (no faultOnThrow), what its process sent standing.
     CHECK(rig.scripts.throwCount() == 1);
-    CHECK_FALSE(rig.host->isFaulted("j"));  // a job is no one node's
+    CHECK_FALSE(rig.host->isFaulted("j"));
+    CHECK(rig.node<Probe>("pj").events.size() == 1);
+    rig.run();
+    CHECK(rig.scripts.throwCount() == 2);
+    CHECK(rig.host->isFaulted("j"));  // stopped again: bypassed, within two deadlines, as a process that hangs is
+    CHECK(rig.node<Probe>("pj").events.empty());
     rig.reclaim();
     INFO(rig.scripts.firstThrow());
-    CHECK(rig.scripts.firstThrow().find("a promise job ran past the 50 ms call deadline (CPU time), and was stopped") == 0);
+    CHECK(rig.scripts.firstThrow().find("rp.toy.slowjob ran past the 50 ms call deadline in a promise job (CPU time), and was "
+                                        "stopped") == 0);
     REQUIRE(rig.set(doc(node("d", "rp.toy.deep") + "," + node("p", "rp.test.probe"), wire("d.out", "p.events"))).ok);
     rig.run(64, 3);
-    CHECK(rig.scripts.throwCount() == 4);  // a stack overflow is an ordinary throw: counted, and it runs on
+    CHECK(rig.scripts.throwCount() == 5);  // a stack overflow is an ordinary throw: counted, and it runs on
     CHECK_FALSE(rig.host->isFaulted("d"));
     CHECK(rig.node<Probe>("p").events.size() == 1);
 }
diff --git a/packages/native/test/engine-graph/ScriptRig.hpp b/packages/native/test/engine-graph/ScriptRig.hpp
index 8cf390ca..ddc13637 100644
--- a/packages/native/test/engine-graph/ScriptRig.hpp
+++ b/packages/native/test/engine-graph/ScriptRig.hpp
@@ -158,8 +158,9 @@ struct ScriptRig {
     std::vector<float*>                   outPointers;
     rp_transport                          transport{48000.0, 120.0, 0.0, 0};
 
-    // The test bundle, then `toys` (JS source; empty for none).
-    explicit ScriptRig(const std::string& toys = "", std::uint32_t maxFrames = 256, double sampleRate = 48000.0) {
+    // The test bundle, then `toys` (JS source; empty for none), its pack held to `memoryLimit` (0: the host's default).
+    explicit ScriptRig(const std::string& toys = "", std::uint32_t maxFrames = 256, double sampleRate = 48000.0,
+                       std::size_t memoryLimit = 0) {
         std::vector<std::uint8_t> toyBytecode;
         std::vector<ScriptHost::Chunk> chunks{scriptTestBundle()};
         if (!toys.empty()) {
@@ -167,7 +168,7 @@ struct ScriptRig {
             chunks.emplace_back(toyBytecode);
         }
         std::string error;
-        if (!scripts.load(chunks, &error)) throw std::runtime_error("the toy bundle does not load: " + error);
+        if (!scripts.load(chunks, &error, memoryLimit)) throw std::runtime_error("the toy bundle does not load: " + error);
         for (const rp_package* p : {graph::corePackage(), graph::hostPackage(), graph::test::testPackage(),
                                     test::testPackage(), scripts.package()})
             if (!registry.addPackage(p, &error)) throw std::runtime_error("a package is refused: " + error);
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[jobs],[memory],[deadline]"`
Expected: FAIL: the build stops, since the tests name `ScriptHost::kJobsPerCheckpoint` and pass `load` a memory limit, neither of which exists yet. The prototype's red run gave the old code those two names alone (the constant, and a limit argument `load` ignored): then the six cases this step changed or added fail and every other passes.

- [ ] **Step 3: Charge jobs and memory to the node**

One deadline spans a call (the create a fresh instance makes first included), the hook or `process`, and its
checkpoint; `expiryFaults` is evaluated once after the checkpoint, over the call and the node's own jobs, so a
`process` that returns cannot reset the count before its hung reaction is stopped. The checkpoint charges an owner:
the pack's remembered owner while its queue holds a node's cut-short leftovers, else the node being called. A released
node's leftovers become no node's. `endBlock` runs no jobs. Each pack's audio allocator (`Pack::Heap`) keeps the
pack's live usable bytes against its limit and a pointer to the host's shared counters, refuses past the limit and
notes it; a call during which its pack refused one faults its node (a refusal JS caught is counted as its throw, with
a fixed text). `defineNode.ts`'s comment on when promise jobs run follows:

````diff
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index 738fe066..1255d7f3 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -77,6 +77,19 @@ std::size_t copyText(char* out, std::size_t capacity, std::string_view text) {
     return n;
 }
 
+// Text written into a fixed buffer piece by piece, each piece cut at a character where it does not fit (copyText).
+struct TextOut {
+    char*       out;
+    std::size_t capacity;
+    std::size_t n = 0;
+    void add(std::string_view text) { n += copyText(out + n, capacity - n, text); }
+    void number(long long value) {
+        char digits[24];
+        std::snprintf(digits, sizeof digits, "%lld", value);
+        add(digits);
+    }
+};
+
 void copyError(rp_error* err, std::string_view what) {
     if (err) copyText(err->message, sizeof err->message, what);
 }
@@ -309,6 +322,16 @@ struct ScriptHost::Type {
 // address of Object.prototype. Made and loaded on the control thread; the block thread reaches it only through an
 // instance of one of its types.
 struct ScriptHost::Pack {
+    // Its audio runtime's allocator opaque: the host's counters, which every pack's runtime adds to, and its own live
+    // heap and the limit an allocation may not take it past (design section 6). Written by whichever thread runs the
+    // runtime (the block thread, or the control thread while it loads or no audio thread runs).
+    struct Heap {
+        Counters*   counters = nullptr;
+        std::size_t live = 0;      // usable bytes
+        std::size_t limit = kDefaultMemoryLimit;
+        bool        refused = false;  // an allocation was refused since this was last read (Glue::refusedSince)
+    };
+
     ScriptHost*   host = nullptr;   // how a Block method, which finds the pack as its context's opaque, finds the host
     std::string   id;
     std::uint32_t generation = 1;   // its load's count
@@ -325,6 +348,12 @@ struct ScriptHost::Pack {
     rp_package    package{};
     // Its load, or a release of one of its instances, since its last collect (whoever released it).
     bool          garbage = false;
+    Heap          heap;
+    // Whose promise jobs are pending while a checkpoint has left some (the block thread's): the instance they are
+    // charged to until the queue empties, or, `jobsOrphaned`, no node, the one they were charged to having been
+    // released (Glue::chargeJobs).
+    Instance*     jobsOwner = nullptr;
+    bool          jobsOrphaned = false;
 };
 
 // One script node instance, behind its shim. Allocated by create and deleted on the control thread; its JS state is
@@ -343,6 +372,7 @@ struct ScriptHost::Instance {
     State                   state = State::Pending;
     bool                    faultNext = false;  // a hook threw under faultOnThrow, or expired twice in a row: fault next block
     std::uint8_t            expiries = 0;       // its calls the deadline stopped in a row (expiryFaults)
+    std::uint8_t            cutShorts = 0;      // checkpoints in a row that cut its promise jobs short (Glue::chargeJobs)
     Settings*               settings = nullptr;
     std::unique_ptr<Layout> layout;
     JSValue                 object = JS_UNDEFINED;  // the NodeInstance its definition's create returned
@@ -411,12 +441,13 @@ struct ScriptHost::Glue {
         return 1;
     }
 
-    // After one of a node's calls: whether the deadline stopping it faults the node. Under faultOnThrow the first expiry
-    // does; without, only a second in a row (design section 4.5: one throw and the next block runs again), and a call
-    // that ran to its end clears the count. So a node that hangs still faults within two deadlines, and one long call
-    // costs a block rather than the session, which a fault would: it outlives a reshape.
-    static bool expiryFaults(const ScriptHost& h, Instance& in) {
-        if (!h.interrupted_) {
+    // After one of a node's calls: whether the deadline stopping it (`stopped`: the call, or a promise job charged to
+    // the node at its checkpoint) faults the node. Under faultOnThrow the first expiry does; without, only a second in
+    // a row (design section 4.5: one throw and the next block runs again), and a call that ran to its end, its jobs
+    // with it, clears the count. So a node that hangs, or whose reaction does, still faults within two deadlines, and
+    // one long call costs a block rather than the session, which a fault would: it outlives a reshape.
+    static bool expiryFaults(Instance& in, bool stopped) {
+        if (!stopped) {
             in.expiries = 0;
             return false;
         }
@@ -425,10 +456,14 @@ struct ScriptHost::Glue {
         return in.expiries >= 2;
     }
 
-    // ---- the audio runtimes' allocator: the platform's, counted -----------------------------------------------
+    // ---- the audio runtimes' allocator: the platform's, counted and held to each pack's limit ------------------ Each
+    // pack's runtime has its own opaque (Pack::Heap), which keeps its live heap in usable bytes, as QuickJS's own
+    // accounting (jsUsable) reads it, in every build: an allocation that would take it past the pack's limit is refused
+    // (null, which QuickJS throws as out of memory in the call that asked) and the refusal noted, for the call's
+    // checkpoint to fault its node. The limit is the host's to enforce rather than QuickJS's (JS_SetMemoryLimit), which
+    // checks its own before calling the allocator and so would refuse without the host ever seeing it.
 #ifdef RETROPLUG_PROFILE
-    // A profile build keeps the live heap and its peak too, in usable bytes, as QuickJS's own accounting (jsUsable)
-    // reads a block.
+    // A profile build keeps the live heap of every pack's runtime summed, and its peak, in the host's counters.
     static void grew(Counters& c, std::int64_t by) {
         const std::int64_t live = c.liveBytes.load(std::memory_order_relaxed) + by;
         c.liveBytes.store(live, std::memory_order_relaxed);
@@ -436,42 +471,59 @@ struct ScriptHost::Glue {
             c.peakBytes.store(static_cast<std::uint64_t>(live), std::memory_order_relaxed);
     }
 #endif
-    static void allocated(Counters& c, [[maybe_unused]] void* p, std::size_t size) {
-        bump(c.allocs);
-        bump(c.bytes, size);
+    // Whether `heap` has room for `more` bytes; when not, the refusal is noted.
+    static bool admits(Pack::Heap& heap, std::size_t more) {
+        if (more <= heap.limit && heap.live <= heap.limit - more) return true;
+        heap.refused = true;
+        return false;
+    }
+    static void allocated(Pack::Heap& heap, void* p, std::size_t size) {
+        const std::size_t usable = usableSize(p);
+        heap.live += usable;
+        bump(heap.counters->allocs);
+        bump(heap.counters->bytes, size);
 #ifdef RETROPLUG_PROFILE
-        grew(c, static_cast<std::int64_t>(usableSize(p)));
+        grew(*heap.counters, static_cast<std::int64_t>(usable));
 #endif
     }
     static void* jsCalloc(void* opaque, std::size_t count, std::size_t size) {
+        auto& heap = *static_cast<Pack::Heap*>(opaque);
+        if (!admits(heap, count * size)) return nullptr;  // QuickJS has checked count * size for overflow
         void* p = std::calloc(count, size);
-        if (p) allocated(*static_cast<Counters*>(opaque), p, count * size);
+        if (p) allocated(heap, p, count * size);
         return p;
     }
     static void* jsMalloc(void* opaque, std::size_t size) {
+        auto& heap = *static_cast<Pack::Heap*>(opaque);
+        if (!admits(heap, size)) return nullptr;
         void* p = std::malloc(size);
-        if (p) allocated(*static_cast<Counters*>(opaque), p, size);
+        if (p) allocated(heap, p, size);
         return p;
     }
     static void jsFree(void* opaque, void* p) {
         if (p) {
-            auto& c = *static_cast<Counters*>(opaque);
-            bump(c.frees);
+            auto& heap = *static_cast<Pack::Heap*>(opaque);
+            const std::size_t usable = usableSize(p);
+            heap.live -= usable;
+            bump(heap.counters->frees);
 #ifdef RETROPLUG_PROFILE
-            grew(c, -static_cast<std::int64_t>(usableSize(p)));
+            grew(*heap.counters, -static_cast<std::int64_t>(usable));
 #endif
         }
         std::free(p);
     }
     static void* jsRealloc(void* opaque, void* p, std::size_t size) {
+        auto& heap = *static_cast<Pack::Heap*>(opaque);
         const std::size_t before = p ? usableSize(p) : 0;
+        if (size > before && !admits(heap, size - before)) return nullptr;  // p left as it was, as a failed realloc does
         void* q = std::realloc(p, size);
         if (q || size == 0) {
-            auto& c = *static_cast<Counters*>(opaque);
-            bump(c.reallocs);
-            if (size > before) bump(c.bytes, size - before);
+            const std::size_t after = q ? usableSize(q) : 0;
+            heap.live = heap.live - before + after;
+            bump(heap.counters->reallocs);
+            if (size > before) bump(heap.counters->bytes, size - before);
 #ifdef RETROPLUG_PROFILE
-            grew(c, static_cast<std::int64_t>(q ? usableSize(q) : 0) - static_cast<std::int64_t>(before));
+            grew(*heap.counters, static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before));
 #endif
         }
         return q;
@@ -569,10 +621,14 @@ struct ScriptHost::Glue {
     static void portsChanged(rp_node* n, const std::uint8_t* connected, std::uint32_t count) {
         Instance& in = instanceOf(n);
         ScriptHost& h = *in.type->host;
+        const Deadline call(h);  // the create a fresh instance makes first, the hook, and the jobs they queue
+        const bool fresh = in.state == Instance::State::Pending;
         if (!ensureLive(h, in)) return;
         JSContext* ctx = packOf(in).audio;
-        if (!JS_IsFunction(ctx, in.portsChanged)) return;
-        const Deadline call(h);
+        if (!JS_IsFunction(ctx, in.portsChanged)) {
+            if (fresh) hookDone(h, in, false, "create");  // what its create queued, at a checkpoint of its own
+            return;
+        }
         JSValue mask = JS_NewArray(ctx);
         for (std::uint32_t i = 0; i < count && !JS_IsException(mask); ++i)
             if (JS_DefinePropertyValueUint32(ctx, mask, i, JS_NewBool(ctx, connected[i] != 0), kDefine) < 0) {
@@ -588,11 +644,12 @@ struct ScriptHost::Glue {
     static rp_status process(rp_node* n, const rp_process* block) {
         Instance& in = instanceOf(n);
         ScriptHost& h = *in.type->host;
+        // The whole block, not just the call: the create a fresh instance makes first, the block path (a node may hang
+        // a setter on what it fills in), the call, and the jobs they queue.
+        const Deadline call(h);
         if (!ensureLive(h, in) || in.faultNext) return RP_FAULT;
         const Pack& pack = packOf(in);
         JSContext* ctx = pack.audio;
-        // The whole block, not just the call: a node may hang a setter on what the block path fills in.
-        const Deadline call(h);
         traceBegin(h, in.type->spanLabel);
         traceBegin(h, DSP_SPAN_MARSHAL);
         ++in.blockStamp;
@@ -609,33 +666,38 @@ struct ScriptHost::Glue {
         JSValue result = JS_Call(ctx, in.process, in.object, 1, &in.block);
         jsCallEnd(h);
         in.running = nullptr;
-        const bool threw = JS_IsException(result), expired = expiryFaults(h, in);
+        const bool threw = JS_IsException(result), stopped = h.interrupted_;
         if (threw) recordThrow(h, in, "process");  // first: the copy out may meet a detached buffer, which throws too
         copyAudioOut(ctx, in, *block);             // what it wrote up to a throw stands, as its sends do
-        bool fault = false;
-        if (threw) {
-            fault = in.type->faultOnThrow || expired;
-        } else if (JS_IsString(result)) {
-            const JSAtom said = JS_ValueToAtom(ctx, result);
-            fault = said == atom(pack, kFault);
-            JS_FreeAtom(ctx, said);
+        bool said = false;                         // it answered "fault"
+        if (!threw && JS_IsString(result)) {
+            const JSAtom answer = JS_ValueToAtom(ctx, result);
+            said = answer == atom(pack, kFault);
+            JS_FreeAtom(ctx, answer);
         }
         JS_FreeValue(ctx, result);
+        // Its jobs after the copy out, so the block's audio is what process wrote, inside its process span (a profile
+        // build), after its js-call span.
+        const Settled jobs = checkpoint(h, in, threw, "process");
+        const bool expired = expiryFaults(in, stopped || jobs.stopped);
         processSpanEnd(h);
-        return fault ? RP_FAULT : RP_OK;
+        return (threw && in.type->faultOnThrow) || expired || said || jobs.fault ? RP_FAULT : RP_OK;
     }
 
     // A block whose marshalling failed before its process could be called, with the exception pending: JS out of
     // memory, a write onto what the node holds refused (readyAudio's onto its views, fillBlock's onto the Block and its
     // transport), or the deadline stopping a setter it hung there, whose uncatchable error is the throw kept. Counted as
-    // a throw in process, its audio outputs silent, and faulted as such a throw would fault it.
+    // a throw in process, its audio outputs silent, and faulted as such a throw would fault it. What the setter queued
+    // runs at the checkpoint after it.
     static rp_status cannotRun(ScriptHost& h, Instance& in, const rp_process& block) {
-        const bool expired = expiryFaults(h, in);
+        const bool stopped = h.interrupted_;
         recordThrow(h, in, "process");
         silenceAudio(in, block);
         traceEnd(h);  // marshal
+        const Settled jobs = checkpoint(h, in, true, "process");
+        const bool expired = expiryFaults(in, stopped || jobs.stopped);
         processSpanEnd(h);
-        return in.type->faultOnThrow || expired ? RP_FAULT : RP_OK;
+        return in.type->faultOnThrow || expired || jobs.fault ? RP_FAULT : RP_OK;
     }
 
     // The block's length and transport, onto the Block object and its transport, whose fields createJs defined, so each
@@ -686,6 +748,8 @@ struct ScriptHost::Glue {
         ScriptHost& h = *in.type->host;
         auto* next = static_cast<Settings*>(prepared);
         Settings* prev = in.settings;
+        const Deadline call(h);  // the create a fresh instance makes first, onSettings, and the jobs they queue
+        const bool fresh = in.state == Instance::State::Pending;
         const bool live = ensureLive(h, in);  // a fresh instance is made from the settings it was created with first
         JSContext* ctx = packOf(in).audio;
         if (next->layout) {
@@ -700,8 +764,11 @@ struct ScriptHost::Glue {
         }
         in.settings = next;
         *old = prev;
-        if (!live || !JS_IsFunction(ctx, in.onSettings)) return;
-        const Deadline call(h);
+        if (!live) return;
+        if (!JS_IsFunction(ctx, in.onSettings)) {
+            if (fresh) hookDone(h, in, false, "create");  // what its create queued, at a checkpoint of its own
+            return;
+        }
         JSValue settings = JS_ParseJSON(ctx, next->json.c_str(), next->json.size(), "<settings>");
         JSValue result = JS_IsException(settings) ? JS_EXCEPTION : JS_Call(ctx, in.onSettings, in.object, 1, &settings);
         hookDone(h, in, JS_IsException(result), "onSettings");
@@ -728,13 +795,13 @@ struct ScriptHost::Glue {
     // throws or returns no process, when reading its process or a hook throws (a getter or a Proxy trap the node
     // defined for one, or a prototype chain too long or cycling through a Proxy, as method says), or when JS runs out
     // of memory making what the host hands it. What the host makes here holds every field it will write as its own
-    // (kDefine).
+    // (kDefine). It runs inside the deadline of the call that reached the pending instance (ensureLive's caller), whose
+    // checkpoint runs the jobs it queues.
     static void createJs(ScriptHost& h, Instance& in) {
         const Pack& pack = packOf(in);
         JSContext* ctx = pack.audio;
         JS_UpdateStackTop(pack.audioRt);
         bindLayout(ctx, *in.layout);
-        const Deadline call(h);
         // The settings as the control side's parseSettings returned them, in their JSON form (the class comment).
         JSValue settings = JS_ParseJSON(ctx, in.settings->json.c_str(), in.settings->json.size(), "<settings>");
         JSValue cc = JS_IsException(settings) ? JS_EXCEPTION : JS_NewObject(ctx);
@@ -789,10 +856,12 @@ struct ScriptHost::Glue {
         h.live_.fetch_add(1, std::memory_order_relaxed);
     }
 
-    // A create that failed, with its exception pending: counted, and the node faults at its first block.
+    // A create that failed, with its exception pending: counted, what it queued run at a checkpoint (charged to it, as
+    // nothing else is to be), and the node faults at its first block.
     static void failCreate(ScriptHost& h, Instance& in) {
         recordThrow(h, in, "create");
         in.state = Instance::State::Failed;
+        checkpoint(h, in, true, "create");
     }
 
     // Each of `fields` onto `obj`, an object the host made and JS has not yet seen, as its own data property, undefined
@@ -877,54 +946,197 @@ struct ScriptHost::Glue {
         return ok;
     }
 
+    // A throw counted, and the first one's text kept for reclaim to log, written by `write` into a fixed buffer (the
+    // block thread allocates nothing native there).
+    template <class Write>
+    static void countThrow(ScriptHost& h, Write&& write) {
+        h.throws_.fetch_add(1, std::memory_order_relaxed);
+        int unclaimed = 0;
+        if (!h.firstThrowState_.compare_exchange_strong(unclaimed, 1, std::memory_order_acq_rel)) return;
+        TextOut text{h.firstThrow_, sizeof h.firstThrow_};
+        write(text);
+        h.firstThrowState_.store(2, std::memory_order_release);
+    }
+
     // Counted, and the first one kept for reclaim to log; the exception, pending in `ctx` (the context the call ran
     // in), is cleared. A call the deadline stopped counts as a throw, and says so.
     static void recordThrow(ScriptHost& h, Instance& in, const char* where) {
         recordThrow(h, packOf(in).audio, in.type->id.c_str(), where);
     }
-    // `type` null: no one node's (a promise job).
+    // `type` null: no one node's (a promise job a release ran, or one whose node is gone).
     static void recordThrow(ScriptHost& h, JSContext* ctx, const char* type, const char* where) {
         JSValue thrown = JS_GetException(ctx);
-        h.throws_.fetch_add(1, std::memory_order_relaxed);
-        int unclaimed = 0;
-        if (h.firstThrowState_.compare_exchange_strong(unclaimed, 1, std::memory_order_acq_rel)) {
-            char* const out = h.firstThrow_;
-            std::size_t n = 0;
-            const auto add = [&](std::string_view text) { n += copyText(out + n, sizeof h.firstThrow_ - n, text); };
-            add(type ? type : where);
+        countThrow(h, [&](TextOut& t) {
+            t.add(type ? type : where);
             if (h.interrupted_) {
-                char ms[24];
-                std::snprintf(ms, sizeof ms, "%lld",
-                              static_cast<long long>(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000));
-                add(" ran past the ");
-                add(ms);
-                add(" ms call deadline");
+                t.add(" ran past the ");
+                t.number(static_cast<long long>(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000));
+                t.add(" ms call deadline");
                 if (type) {
-                    add(" in ");
-                    add(where);
+                    t.add(" in ");
+                    t.add(where);
                 }
-                add(" (CPU time), and was stopped: ");
+                t.add(" (CPU time), and was stopped: ");
             } else if (type) {
-                add(" threw in ");
-                add(where);
-                add(": ");
+                t.add(" threw in ");
+                t.add(where);
+                t.add(": ");
             } else {
-                add(" threw: ");
+                t.add(" threw: ");
             }
-            describe(ctx, thrown, false, out + n, sizeof h.firstThrow_ - n);
-            h.firstThrowState_.store(2, std::memory_order_release);
-        }
+            describe(ctx, thrown, false, t.out + t.n, t.capacity - t.n);
+        });
         JS_FreeValue(ctx, thrown);
     }
 
-    // After a hook (onSettings and portsChanged at a plan swap, activate at a re-activation): a throw is counted, and
-    // under faultOnThrow, or when it is the deadline's second expiry in a row (expiryFaults), the node faults at its
-    // next block, since it may hold half of what the hook would have set.
+    // After a hook (onSettings and portsChanged at a plan swap, activate at a re-activation), or a create no hook
+    // follows: a throw is counted, then its checkpoint runs, and the node faults at its next block, since it may hold
+    // half of what the hook would have set, under faultOnThrow, when the deadline stopped it or its jobs a second time
+    // in a row (expiryFaults), or when its checkpoint faults it.
     static void hookDone(ScriptHost& h, Instance& in, bool threw, const char* where) {
-        const bool expired = expiryFaults(h, in);
-        if (!threw) return;
-        recordThrow(h, in, where);
-        if (in.type->faultOnThrow || expired) in.faultNext = true;
+        const bool stopped = h.interrupted_;
+        if (threw) recordThrow(h, in, where);
+        const Settled jobs = checkpoint(h, in, threw, where);
+        const bool expired = expiryFaults(in, stopped || jobs.stopped);
+        if ((threw && in.type->faultOnThrow) || expired || jobs.fault) in.faultNext = true;
+    }
+
+    // ---- the checkpoint after a call: its pack's promise jobs, and its memory (design section 6) ----------------
+    // What a checkpoint charged to a node.
+    struct Settled {
+        bool stopped = false;  // the deadline stopped one of its jobs: its call counts as stopped (expiryFaults)
+        bool fault = false;    // it faults now: an allocation its pack refused in its call or its jobs, a job of its
+                               //   that failed under faultOnThrow, or its jobs cut short a second time in a row
+    };
+
+    // Whether `pack` refused an allocation since this was last asked; the note is cleared.
+    static bool refusedSince(Pack& pack) { return std::exchange(pack.heap.refused, false); }
+
+    // After a call the block thread made into `in` (create, onSettings, portsChanged, activate or process), inside the
+    // call's deadline, which spans the call and its jobs. An allocation its pack refused during the call faults it,
+    // whether or not JS caught the error (`threw`: the call threw, so its throw, the refusal's error or another, is
+    // counted; else the refusal is counted as its throw). Then the jobs pending in its pack run (chargeJobs), charged
+    // to the pack's remembered owner when it has one, else to `in`: so its own, and only those, while no other node's
+    // are pending. What falls to `in` is returned, for its call to fold in as its own (process faults at once, a hook
+    // at the next block); what falls to another node, whose leftovers these are, faults that one at its next block
+    // (chargeOther). Once the deadline has stopped the call, nothing runs: what it left waits for the pack's next
+    // checkpoint, still charged to it. A call into a pack with nothing pending costs one list test here.
+    static Settled checkpoint(ScriptHost& h, Instance& in, bool threw, const char* where) {
+        Pack& pack = packOf(in);
+        Settled self;
+        if (refusedSince(pack)) {
+            if (!threw) recordRefusal(h, pack, in.type->id.c_str(), where);
+            self.fault = true;
+        }
+        if (!JS_IsJobPending(pack.audioRt)) return self;
+        Instance* const owner = pack.jobsOwner ? pack.jobsOwner : pack.jobsOrphaned ? nullptr : &in;
+        if (h.interrupted_) {
+            pack.jobsOwner = owner;
+            return self;
+        }
+        const Settled jobs = chargeJobs(h, pack, owner);
+        if (owner == &in) {
+            self.stopped = jobs.stopped;
+            self.fault = self.fault || jobs.fault;
+        } else if (owner) {
+            chargeOther(*owner, jobs);
+        }
+        return self;
+    }
+
+    // The jobs pending in `pack`, at most kJobsPerCheckpoint, charged to `owner` (null: no node's, the node they were
+    // charged to having been released). A job that fails (a queueMicrotask callback that threw, or JS out of memory) is
+    // the owner's throw, and faults a faultOnThrow owner; one the deadline stops ends the run and counts as the owner's
+    // call stopped; an allocation the pack refused in one faults the owner (and is its throw when JS caught it). Jobs
+    // still pending after the bound are cut short: counted (jobsCutShort), the owner's throw, and its second cut short
+    // in a row faults it. QuickJS cannot drop a job, so the pack remembers the owner while any are pending, and its
+    // later checkpoints charge them to the same node, never to a pack-mate; one that empties the queue forgets the
+    // owner and its count. A promise reaction's own throw only rejects its promise: the job runs to its end.
+    static Settled chargeJobs(ScriptHost& h, Pack& pack, Instance* owner) {
+        Settled charged;
+        const char* const type = owner ? owner->type->id.c_str() : nullptr;
+        for (int n = 0; n < kJobsPerCheckpoint; ++n) {
+            JSContext* ctx = nullptr;
+            const int r = JS_ExecutePendingJob(pack.audioRt, &ctx);
+            if (r == 0) break;
+            const bool refused = refusedSince(pack);
+            if (r < 0) recordThrow(h, ctx, type, "a promise job");
+            else if (refused) recordRefusal(h, pack, type, "a promise job");
+            if (owner && (refused || (r < 0 && owner->type->faultOnThrow))) charged.fault = true;
+            if (r < 0 && h.interrupted_) {
+                charged.stopped = true;
+                break;
+            }
+        }
+        if (!JS_IsJobPending(pack.audioRt)) {
+            pack.jobsOwner = nullptr;
+            pack.jobsOrphaned = false;
+            if (owner) owner->cutShorts = 0;
+            return charged;
+        }
+        pack.jobsOwner = owner;
+        pack.jobsOrphaned = !owner;
+        if (charged.stopped) return charged;  // the deadline's, not the bound's: the rest wait, uncounted
+        h.jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
+        if (owner) {
+            recordCutShort(h, type);
+            if (owner->cutShorts < 2) ++owner->cutShorts;
+            if (owner->cutShorts >= 2) charged.fault = true;
+        }
+        return charged;
+    }
+
+    // What a checkpoint charged to a node other than the one whose call it followed, the owner of the leftovers it ran:
+    // it faults at its next block when they fault it, or when the deadline stopping one of them is its second expiry in
+    // a row (expiryFaults; a run the deadline did not stop leaves its count alone, its own calls being what clear it).
+    static void chargeOther(Instance& owner, const Settled& jobs) {
+        const bool expired = jobs.stopped && expiryFaults(owner, true);
+        if (jobs.fault || expired) owner.faultNext = true;
+    }
+
+    // A release's (releaseJs), after the instance's JS is freed: what its pack has pending runs there, under a deadline
+    // of its own, charged to the pack's owner when it has one (never the released instance, which releaseJs has let go
+    // of), else to no node.
+    static void releaseJobs(ScriptHost& h, Pack& pack) {
+        const Deadline call(h);
+        Instance* const owner = pack.jobsOwner;
+        const Settled jobs = chargeJobs(h, pack, owner);
+        if (owner) chargeOther(*owner, jobs);
+    }
+
+    // An owner's jobs cut short (chargeJobs): its throw, with no exception behind it.
+    static void recordCutShort(ScriptHost& h, const char* type) {
+        countThrow(h, [&](TextOut& t) {
+            t.add(type);
+            t.add("'s promise jobs did not settle in the ");
+            t.number(kJobsPerCheckpoint);
+            t.add(" a checkpoint runs");
+        });
+    }
+
+    // An allocation `pack` refused that JS caught, so no throw stands for it: counted as the node's throw (`type`;
+    // null: no node's), which faults it.
+    static void recordRefusal(ScriptHost& h, const Pack& pack, const char* type, const char* where) {
+        countThrow(h, [&](TextOut& t) {
+            constexpr std::size_t kMiB = std::size_t{1} << 20;
+            t.add(type ? type : where);
+            t.add(" went past its pack's ");
+            if (pack.heap.limit % kMiB == 0) {
+                t.number(static_cast<long long>(pack.heap.limit / kMiB));
+                t.add(" MiB");
+            } else {
+                t.number(static_cast<long long>(pack.heap.limit));
+                t.add("-byte");
+            }
+            t.add(" memory limit");
+            if (type) {
+                t.add(" in ");
+                t.add(where);
+                t.add(" (an allocation was refused), and was faulted");
+            } else {
+                t.add(" (an allocation was refused)");
+            }
+        });
     }
 
     static void bindLayout(JSContext* ctx, Layout& l) {
@@ -1367,7 +1579,8 @@ void ScriptHost::makeRuntimes(Pack& pack) {
     pack.controlRt = JS_NewRuntime();
     pack.control = JS_NewContext(pack.controlRt);
     const JSMallocFunctions counted{&Glue::jsCalloc, &Glue::jsMalloc, &Glue::jsFree, &Glue::jsRealloc, &Glue::jsUsable};
-    pack.audioRt = JS_NewRuntime2(&counted, &counters_);  // QuickJS copies the functions and keeps the opaque
+    pack.heap.counters = &counters_;
+    pack.audioRt = JS_NewRuntime2(&counted, &pack.heap);  // QuickJS copies the functions and keeps the opaque
     pack.audio = JS_NewContext(pack.audioRt);
     JS_SetContextOpaque(pack.audio, &pack);  // what a Block method runs in: Glue::packOf
     // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the block
@@ -1425,17 +1638,24 @@ void ScriptHost::freeRuntimes(Pack& pack) {
     pack.audioRt = pack.controlRt = nullptr;
 }
 
-bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error) {
+bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error, std::size_t memoryLimit) {
     // Refused without touching what a first load registered: a registry may hold its types already.
     if (loaded_) {
         if (error) *error = "a script host loads one bundle";
         return false;
     }
     loaded_ = true;
-    return loadInto(*packs_.front(), chunks, error);
+    Pack& pack = *packs_.front();
+    pack.heap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;  // before the load, whose allocations it holds
+    return loadInto(pack, chunks, error);
 }
 
 bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error) {
+    // An allocation the load was refused fails it (the chunk throws out of memory); no node's call is to be faulted.
+    struct Forget {
+        Pack* pack;
+        ~Forget() { pack->heap.refused = false; }
+    } forget{&pack};
     auto fail = [&](std::string why) {
         freeTypes(pack);
         if (error) *error = std::move(why);
@@ -1453,7 +1673,7 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
             JS_FreeValue(ctx, result);
         }
         // What the bundle's top level queued runs now, in this context's runtime: no block has begun.
-        runJobs(audioSide ? pack.audioRt : pack.controlRt, kJobsPerBlock, [](JSContext* c) { clearException(c); });
+        runJobs(audioSide ? pack.audioRt : pack.controlRt, kJobsPerCheckpoint, [](JSContext* c) { clearException(c); });
     }
     // What the load leaves in cycles in the audio runtime, its top level's and the descriptors' below, goes at the first
     // quiescent reclaim, rather than waiting for a block's threshold GC. (The production bundle left about 144 KiB while
@@ -1525,7 +1745,7 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         type.abi = rp_node_type{type.id.c_str(), &Glue::descriptor, &Glue::layoutFor, &Glue::queryMemory,
                                 &Glue::create, &Glue::destroy, RP_TYPE_STATELESS_PREPARE, &type};
     }
-    runJobs(pack.audioRt, kJobsPerBlock, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
+    runJobs(pack.audioRt, kJobsPerCheckpoint, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
     // Each type's span label, in registration order (spanLabelFor).
     for (const auto& t : pack.types) t->spanLabel = spanLabelFor(t->id);
     pack.typeList.clear();
@@ -1594,14 +1814,8 @@ void ScriptHost::beginBlock() {
     }
 }
 
-// The promise jobs the block's JS queued (its process calls, and whatever the adoption before it ran), each pack's
-// bounded so a reaction that queues another every time cannot hold the block, and held to one call deadline. A job
-// that fails (a queueMicrotask callback that threw, the deadline, or JS out of memory: a promise reaction's own throw
-// only rejects its promise) is counted as a node's throw is, and one the deadline stopped leaves the rest of its pack's
-// for the next block's end. This reads the pack list on the block thread, which nothing else there does (packs_).
+// The block's promise jobs have run already, each at the checkpoint after the call that queued it.
 void ScriptHost::endBlock() {
-    for (const auto& pack : packs_)
-        if (JS_IsJobPending(pack->audioRt)) runAudioJobs(*pack);
 #ifdef RETROPLUG_PROFILE
     // The allocation window's per-block counts, the promise jobs included.
     const ScriptAllocations now = audioAllocations();
@@ -1611,22 +1825,8 @@ void ScriptHost::endBlock() {
 #endif
 }
 
-void ScriptHost::runAudioJobs(Pack& pack) {
-    JS_UpdateStackTop(pack.audioRt);
-    const Glue::Deadline call(*this);
-    for (int n = 0; n < kJobsPerBlock; ++n) {
-        JSContext* ctx = nullptr;
-        const int r = JS_ExecutePendingJob(pack.audioRt, &ctx);
-        if (r == 0) return;
-        if (r > 0) continue;
-        Glue::recordThrow(*this, ctx, nullptr, "a promise job");
-        if (interrupted_) break;
-    }
-    if (JS_IsJobPending(pack.audioRt)) jobsCutShort_.fetch_add(1, std::memory_order_relaxed);
-}
-
 void ScriptHost::drainControlJobs(Pack& pack) {
-    if (JS_IsJobPending(pack.controlRt)) runJobs(pack.controlRt, kJobsPerBlock, [](JSContext* ctx) { clearException(ctx); });
+    if (JS_IsJobPending(pack.controlRt)) runJobs(pack.controlRt, kJobsPerCheckpoint, [](JSContext* ctx) { clearException(ctx); });
 }
 
 void ScriptHost::drainReleased() {
@@ -1648,9 +1848,9 @@ void ScriptHost::reclaim() {
         leaksReported_ = leaked;
     }
     if (!jobsLogged_ && jobsCutShort() > 0) {
-        std::fprintf(stderr, "[script] a block's promise jobs outran the %d a block runs; the rest ran at the next block's end "
-                             "(said once; jobsCutShort() counts them)\n",
-                     kJobsPerBlock);
+        std::fprintf(stderr, "[script] a script node's promise jobs outran the %d a checkpoint runs; the rest wait for its pack's "
+                             "next checkpoint, still charged to it (said once; jobsCutShort() counts them)\n",
+                     kJobsPerCheckpoint);
         jobsLogged_ = true;
     }
     if (!firstThrowLogged_ && firstThrowState_.load(std::memory_order_acquire) == 2) {
@@ -1709,6 +1909,13 @@ void ScriptHost::releaseJs(Instance& in) {
     if (in.state == Instance::State::Live) live_.fetch_sub(1, std::memory_order_relaxed);
     in.state = Instance::State::Released;
     pack.garbage = true;
+    // Never left its pack's owner, its record being deleted after this: what it left pending is no node's now.
+    if (pack.jobsOwner == &in) {
+        pack.jobsOwner = nullptr;
+        pack.jobsOrphaned = true;
+    }
+    if (JS_IsJobPending(pack.audioRt)) Glue::releaseJobs(*this, pack);
+    pack.heap.refused = false;  // what freeing it was refused is no node's to fault
 }
 
 ScriptAllocations ScriptHost::audioAllocations() const {
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index 3f572d8c..f27be898 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -121,6 +121,7 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // What every pack shares stays the host's: the call deadline (one call runs at a time on the block thread), the
 // allocation counters (summed over every pack's audio runtime, so audioAllocations and the profile's allocation window
 // mean the whole audio side), the release rings, the throw count and the first throw, and the profile's span labels.
+// What is a pack's own: its promise-job queue, its memory limit, its cycle GC and its quiescent collection.
 //
 // Every runtime's JS stack is held to kStackBytes, under the smallest thread that may enter one: 512 KiB is a macOS
 // secondary thread's default, which a DAW's audio or control thread may have and a render job's std::thread does
@@ -130,11 +131,13 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 //
 // Every call into an audio runtime from the block thread is held to a deadline (setCallDeadline) of the calling
 // thread's CPU time, so a stall that is not the node's (preemption, a stopped process, a debugger or a VM pause) never
-// trips it. A call that runs past it is stopped (QuickJS's interrupt, which JS cannot catch) and counted as a throw.
-// A node with faultOnThrow then faults; one without runs again next block (design section 4.5), and faults only if
-// its next call is stopped too, so a node that hangs is bypassed within two deadlines and one that ran long once is
-// not. A call that runs to its end starts the count again. A deadline fault is a fault like any other: it latches on
-// the instance and outlives reshapes, and only a rebuild (or the node removed and added again) clears it.
+// trips it. One deadline spans a call into a node, the create a fresh instance makes first, and the promise jobs they
+// queue (below). A call that runs past it is stopped (QuickJS's interrupt, which JS cannot catch) and counted as a
+// throw. A node with faultOnThrow then faults; one without runs again next block (design section 4.5), and faults only
+// if its next call is stopped too, so a node that hangs is bypassed within two deadlines and one that ran long once is
+// not. A call that runs to its end, its jobs with it, starts the count again. A deadline fault is a fault like any
+// other: it latches on the instance and outlives reshapes, and only a rebuild (or the node removed and added again)
+// clears it.
 //
 // Every type declares RP_TYPE_STATELESS_PREPARE: its prepare_settings reads the settings it is handed, the
 // definition and what the instance was created with, never anything a commit writes. A type that reshapes answers
@@ -152,8 +155,24 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // block allocates nothing native, and a node written with indexed loops nothing in JS either. What a block hands out
 // is good for that process call: keep copies.
 //
-// The promise jobs a block's JS queues run at the end of the block (endBlock), at most kJobsPerBlock of each pack's,
-// so a reaction that queues another every time cannot hold the block: the rest wait for the next block's end.
+// A node pays for its own promise jobs and memory (docs/modular-graph-phase-c1-design.md section 6). The jobs a call
+// into its JS queues (its create, onSettings, portsChanged, activate or process) run at a checkpoint right after the
+// call, inside its deadline, at most kJobsPerCheckpoint of them, and are charged to that node: a job that fails (a
+// queueMicrotask callback that threw, or JS out of memory) is its throw, and faults it under faultOnThrow; one the
+// deadline stops counts as its call stopped (the rule above); and a chain still pending after the bound is cut short,
+// its throw, the second cut short in a row faulting it, as two expiries do. QuickJS cannot drop a pending job, so a
+// chain cut short stays queued and its pack remembers whose it is until the queue empties: its later checkpoints (the
+// next calls into any of its nodes) run the rest charged to that node, never to a pack-mate. A release runs what its
+// pack has pending, charged to that remembered owner when it is another node, else to no node, as is what a released
+// owner leaves (a throw counted, faulting no one). A fault charged to the node whose process this is returns RP_FAULT;
+// one after a hook, or charged to another node of the pack whose leftovers ran, faults that node at its next block. A
+// promise reaction's own throw only rejects its promise, which is the node's to handle.
+//
+// Each pack's audio runtime is held to a memory limit (kDefaultMemoryLimit, unless its load says otherwise) by the
+// host's allocator, which refuses an allocation past it, so QuickJS throws out of memory in the call that asked, and
+// notes the refusal on the pack: a call, or its jobs, during which its pack refused one faults its node at once (a
+// refusal JS caught counted as the node's throw), since a pack at its limit is one none of its nodes can rely on.
+// Another pack is untouched. The control side's runtimes keep no limit: they hold no instance.
 //
 // A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
 // (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch); a
@@ -213,12 +232,15 @@ public:
 
     // ---- control thread ------------------------------------------------------------------------------
     // Load the built-in pack: run a bundle (global-code bytecode, in chunks run in order) in both of its contexts, then
-    // read the definitions it registered. Once, before anything plans with the package: a second call is refused, and
-    // leaves what the first registered as it is. False, with a reason, when a chunk fails to load or a definition
-    // cannot be read or is not the same in both contexts (its id or its descriptor differs: a bundle that branches on
-    // which context it runs in). One bad definition refuses the whole package: it then has no types, so every document
-    // naming any of the bundle's is refused (unknown-type).
-    bool load(std::span<const Chunk> chunks, std::string* error = nullptr);
+    // read the definitions it registered, its audio runtime held to `memoryLimit` bytes (0: kDefaultMemoryLimit) from
+    // then on, the load included. Once, before anything plans with the package: a second call is refused, and leaves
+    // what the first registered as it is. False, with a reason, when a chunk fails to load (or runs out of memory) or a
+    // definition cannot be read or is not the same in both contexts (its id or its descriptor differs: a bundle that
+    // branches on which context it runs in). One bad definition refuses the whole package: it then has no types, so
+    // every document naming any of the bundle's is refused (unknown-type).
+    bool load(std::span<const Chunk> chunks, std::string* error = nullptr, std::size_t memoryLimit = kDefaultMemoryLimit);
+    // What a pack's audio runtime may hold, in usable bytes, unless its load says otherwise (the class comment).
+    static constexpr std::size_t kDefaultMemoryLimit = std::size_t{64} << 20;
     // The built-in pack's script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a
     // load).
     const rp_package* package() const;
@@ -243,25 +265,28 @@ public:
     // going at once is spread over blocks, and a full ring still drains in 16).
     void beginBlock();
     static constexpr std::uint32_t kReleasesPerBlock = 16;
-    // At the end of every block that ran the graph: the promise jobs its JS queued, at most kJobsPerBlock of each
-    // pack's, then the allocation window's per-block counts (a profile build).
+    // At the end of every block that ran the graph: the allocation window's per-block counts (a profile build). Its
+    // promise jobs ran at its calls' checkpoints.
     void endBlock();
-    static constexpr int kJobsPerBlock = 256;
+    // The most of its pack's pending promise jobs a checkpoint after a call into a node runs (the class comment).
+    static constexpr int kJobsPerCheckpoint = 256;
     static constexpr std::size_t kStackBytes = 256 * 1024;  // each runtime's JS stack (the class comment)
     // How many plain objects reading a node's process or a hook walks (what its create returned, then the prototypes
     // it inherits from, short of Object.prototype) before it refuses, failing the create (the class comment).
     static constexpr int kMethodHops = 1024;
 
     // ---- any thread ------------------------------------------------------------------------------------------
-    // Throws caught in a script node (process, and its create, onSettings, portsChanged and activate) so far, and the
-    // jobs their blocks queued that failed: a queueMicrotask callback that threw, or a job the deadline stopped or JS
-    // ran out of memory in. A promise reaction's own throw only rejects its promise, which is the node's to handle, so
-    // it is not counted.
+    // Throws caught in a script node (process, and its create, onSettings, portsChanged and activate) so far; the jobs
+    // their calls queued that failed: a queueMicrotask callback that threw, or a job the deadline stopped or JS ran out
+    // of memory in (each charged to its node, or to no node for one a release ran); the checkpoints that cut a node's
+    // jobs short; and the allocations a pack refused that JS caught. A promise reaction's own throw only rejects its
+    // promise, which is the node's to handle, so it is not counted.
     std::uint32_t     throwCount() const { return throws_.load(std::memory_order_relaxed); }
     ScriptAllocations audioAllocations() const;
     // JS instances created and not yet released.
     std::uint32_t     liveInstances() const { return live_.load(std::memory_order_relaxed); }
-    // Blocks whose promise jobs outran kJobsPerBlock, leaving some for the next block; reclaim says so once.
+    // Checkpoints (and releases) whose pack's promise jobs outran kJobsPerCheckpoint, leaving some for its next one;
+    // reclaim says so once.
     std::uint32_t     jobsCutShort() const { return jobsCutShort_.load(std::memory_order_relaxed); }
 
     // ---- profiling (spec/08-profiling.md): real only in a RETROPLUG_PROFILE build --------------------------------
@@ -319,16 +344,14 @@ private:
     void collect();                 // quiescent: a GC of each pack's audio runtime whose load or releases have left
                                     //   garbage since its last
     static void drainControlJobs(Pack& pack);  // control thread: the jobs a call into its control context queued
-    void runAudioJobs(Pack& pack);  // the block thread: endBlock's promise jobs, one pack's
 
-    // counters_ is every audio runtime's allocator opaque, so it is declared (and made) before them.
+    // Every audio runtime's allocator opaque (its pack's Heap) points at counters_, so it is declared (and made) first.
     Counters                     counters_;
     Binder                       binder_ = nullptr;
     void*                        binderUser_ = nullptr;
     // Every pack, the built-in one first. Made, loaded and read on the control thread (and quiescent, by releaseQueued,
     // collect and the profile calls); the block thread reaches a pack only through an instance of one of its types,
-    // never through this list. endBlock is the one exception: it runs every pack's promise jobs, which is sound while
-    // the list is fixed from the constructor on, as it is with the built-in pack the only one.
+    // never through this list.
     std::vector<std::unique_ptr<Pack>> packs_;
     bool                               loaded_ = false;  // load has been called
     // Each type id's span label (DspSpanLabel), given at its first registration in any pack and kept, and the next one
diff --git a/packages/retroplug/src/graph/defineNode.ts b/packages/retroplug/src/graph/defineNode.ts
index eea3515c..7afb522c 100644
--- a/packages/retroplug/src/graph/defineNode.ts
+++ b/packages/retroplug/src/graph/defineNode.ts
@@ -157,11 +157,13 @@ export function startValue(stream: ControlStream): number {
 // - A feedback input carries last block's messages at their frames in that block, so a frame can lie at or past
 //   this block's end. No production node reads one: LSDj's serial-out decoder, the one that reads a system's output,
 //   takes its Game Boy's serialOut over a forward wire, each byte at its frame in this block.
-// - Promise reactions and queueMicrotask callbacks run at the end of the block, after every node's process: at
-//   most 256 a block (the rest at the next block's end), under one call deadline between them. Every Block method
-//   throws inside one, as in a hook: a block exists only during its node's process call. A reaction's own throw
-//   only rejects its promise and is not counted; a queueMicrotask callback's is counted as a throw, charged to no
-//   node, so it never faults one.
+// - Promise reactions and queueMicrotask callbacks run right after the call that queued them (create, onSettings,
+//   portsChanged, activate or process), before the next node's, inside that call's deadline, and are charged to the
+//   node: at most 256 after a call, the rest of a chain that queues another every time left for the next call into
+//   a node of its pack and still charged to it; a node whose jobs are cut short twice in a row faults. Every Block
+//   method throws inside one, as in a hook: a block exists only during its node's process call. A reaction's own
+//   throw only rejects its promise and is not counted; a queueMicrotask callback's is counted as the node's throw,
+//   and faults a faultOnThrow node, and one the deadline stops counts as the node's call stopped.
 //
 // What a block hands out (the arrays, the views, the message objects) is valid only during the process call it is
 // handed to: the same objects are refilled for the next block, or made again, so read them through the block every
````

- [ ] **Step 4: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[jobs],[memory],[deadline]"`
Expected: `All tests passed` for those cases.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (18091 assertions in 125 test cases)` (two cases more: the pack-mate's and the memory limit's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `tools/run-profile.sh unit && tools/run-profile.sh stats`
Expected: `All tests passed (132 assertions in 6 test cases)`, then dsp-bench's `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":312112,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.64}`, `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":476,"clocks":476,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`: no allocation in any block (the allocator's added `malloc_usable_size` call costs about 4 ns an empty script call and 12 one that passes a message on, in `[bench]`).

Run: `pnpm typecheck && pnpm test`
Expected: `packages/retroplug typecheck: Done`, then `# 167 test file(s) passed (jobs=16)`.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[script]"`
Expected: `All tests passed (883 assertions in 51 test cases)`, no ThreadSanitizer report.

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/src/host/script/ScriptHost.cpp packages/native/src/host/script/ScriptHost.hpp \
  packages/native/test/engine-graph/ScriptNodes.test.cpp packages/native/test/engine-graph/ScriptRig.hpp \
  packages/retroplug/src/graph/defineNode.ts
git commit -F - <<'EOF'
feat(script-host): a node's promise jobs run after its own call and are charged to it; each pack has a memory limit

After every call the block thread makes into a node (portsChanged, process, onSettings, activate, and the create the
first of them makes), inside the same deadline, a checkpoint runs the node's pack's pending jobs, at most
kJobsPerCheckpoint (256), charged to the pack's remembered owner or else to the node: a failing job is its throw and
faults it under faultOnThrow, a job the deadline stops is its call stopped (expiryFaults' rule, now evaluated over the
call and its jobs), and a chain still pending after the bound is cut short, its throw, the second in a row faulting
it. The pack remembers the owner until its queue empties, so a pack-mate's checkpoint charges leftovers to the node
that left them (its faultNext); a release runs what is pending charged to that owner when it is another node, else to
no node, and a released owner's leftovers are no node's from then on. endBlock no longer runs jobs.

Each pack's audio runtime has its own allocator opaque holding its live usable bytes and a limit (kDefaultMemoryLimit,
64 MiB; load takes one for the built-in pack): past it the allocator returns null and notes the refusal, and a call
(or its jobs) during which its pack refused one faults its node at once, whether or not JS caught the error.
EOF
````

### Task 3: loading a pack

Design sections 4, 5 ("Load") and 7's Engine half. `Engine::loadScriptPack(id, source, memoryLimit)` compiles a pack's
source on the control thread, loads it into runtimes of its own, and adds its package to the Engine's node registry,
while an audio thread runs other packs' nodes. A loaded pack's id is a plain id outside `rp.` (the app's), and its type
ids are under it. The control side's calls into a pack (its load, its settings checks) are held to a deadline of their
own, so a pack that never returns there is refused instead of holding the control thread.

**Files:**
- Create: `packages/native/test/engine-graph/ScriptPacks.test.cpp`
- Modify: `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`, `packages/native/src/graph/NodeRegistry.hpp`, `packages/native/src/host/dsp/ScriptCompiler.cpp`, `packages/native/src/host/dsp/ScriptCompiler.hpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/script/ScriptHost.cpp`, `packages/native/src/host/script/ScriptHost.hpp`

- [ ] **Step 1: Write the failing tests**

A new engine-graph file drives packs through an Engine whose built-in pack is the production bundle; each toy pack is
inline JS that defines its nodes as plain objects in the shape `defineNode` makes and pushes them onto
`globalThis.__rp_scriptNodes`, so it needs no authoring layer. Its `[script][packs]` cases: a pack's node beside the
built-in routing in one document, the load's `types` listing its descriptors; isolation (one pack's getter on
`Object.prototype`, replaced `Array.prototype.push` and global seen by its own probe, not by another pack's, the
built-in routing unaffected); every refusal changing nothing (a non-plain id, `rp.script`, `rp` and ids under it, a
type outside the namespace, a compile error at its line, an unreadable definition, a collision between two user packs
that the registry refuses); a pack's memory blow-up faulting only its node; a never-settling chain faulting only its
node; a load and placement while a `std::thread` drives blocks; the control side's deadline (a load that never returns
refused; a `parseSettings` or `layout` that never returns refusing the documents that place it while the running plan
plays on, quiescent and under an audio thread); and, on a `ScriptRig`, the binder reaching a loaded pack's contexts
and its load counted in no audio-side counter. The test binary lists it:

Create `packages/native/test/engine-graph/ScriptPacks.test.cpp`:

````cpp
// Script node packs (docs/modular-graph-phase-c1-design.md sections 4 to 7): a pack Engine::loadScriptPack loads runs
// beside the built-in pack's nodes in one document, in runtimes of its own, so what one pack does to its realm (a
// getter on Object.prototype, Array.prototype.push replaced, a global) no node of another pack sees, its memory limit
// and its promise jobs fault only its own nodes, and it loads while an audio thread runs blocks. Its id (never one of
// the app's, "rp" and under), its types' namespace, its source and its definitions are checked, and a refusal changes
// nothing. The control thread's calls into a pack, its load and its settings checks, are held to the call deadline on
// a clock of their own. Driven on an Engine, whose built-in pack is the production bundle, the toy packs inline JS
// source; and on a ScriptRig, where the host's binder, its allocation counts and the built-in pack's deadline are what
// is checked.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[packs]"`.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphDocs.hpp"
#include "ScriptRig.hpp"
#include "graph/Descriptor.hpp"
#include "host/engine/Engine.hpp"
#include "host/script/ScriptHost.hpp"

using rp::host::ScriptHost;
using rp::host::test::DocExtras;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::graphRequest;
using rp::host::test::stereoDocument;

namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 256;

// What every toy pack's source starts with. The production bundle, which is every Engine's built-in pack, carries no
// authoring layer, so a toy defines its nodes as plain objects in the shape defineNode makes
// (packages/retroplug/src/graph/defineNode.ts: id, descriptor(), parseSettings, layout, create and the flags) and
// appends them to the global the script host reads (packages/retroplug/src/script/register.ts). A node's ports are its
// layout whatever its settings.
const std::string kToy = R"JS(
var toy = (function () {
  var nodes = globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || [];
  return {
    define: function (id, ports, create) {
      var layout = { ports: ports, params: [] };
      nodes[nodes.length] = {  // not a push: a pack may replace Array.prototype.push
        id: id,
        descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
        parseSettings: function (raw) { return raw === undefined ? {} : raw; },
        layout: function () { return layout; },
        create: create,
        reshapes: false,
        faultOnThrow: false,
      };
    },
    input: function (id) { return { id: id, dir: "in", kind: "events" }; },
    output: function (id) { return { id: id, dir: "out", kind: "events" }; },
  };
})();
)JS";

// A toy pack's source: kToy, then `body`.
std::string toys(const std::string& body) { return kToy + body; }

// A node, `id`, that sends each message it gets on with its second byte moved by `by`.
std::string transposer(const std::string& id, int by) {
    return "toy.define('" + id + "', [toy.input('in'), toy.output('out')], function () {\n"
           "  return { process: function (b) {\n"
           "    var ms = b.messages('in');\n"
           "    for (var i = 0; i < ms.length; i++) {\n"
           "      var d = ms[i].data;\n"
           "      b.send('out', ms[i].frame, [d[0], d[1] + (" + std::to_string(by) + "), d[2]]);\n"
           "    }\n"
           "  } };\n"
           "});\n";
}

// A node, `id`, that sends its block count every block, and on its second runs `second` (JS statements, with `kept`
// an array it holds for its life).
std::string counter(const std::string& id, const std::string& second) {
    return "toy.define('" + id + "', [toy.output('out')], function () {\n"
           "  var n = 0, kept = [];\n"
           "  return { process: function (b) {\n"
           "    b.send('out', 0, [++n & 0x7f]);\n"
           "    if (n === 2) { " + second + " }\n"
           "  } };\n"
           "});\n";
}

// A document of `nodes` and `wires` beside the host's stereo pair, placing no system.
Engine::GraphRequest document(std::vector<std::string> nodes, std::vector<std::string> wires) {
    return graphRequest(stereoDocument({}, DocExtras{std::move(nodes), std::move(wires)}), {});
}

// rp.host.midi-out with an input per system in `systems`, in1 up, each sending under its system.
std::string midiOut(const std::string& systems) {
    return docNode("midi-out", "rp.host.midi-out", R"({"systems":[)" + systems + "]}");
}

// The built-in pack's MIDI routing, in `mode`, with an output per system in `systems` (sys-<id>).
std::string routing(const std::string& mode, const std::string& systems) {
    return docNode("routing", "rp.midi.route", R"({"mode":")" + mode + R"(","systems":[)" + systems + "]}");
}

struct Out {
    std::uint32_t             system = 0, frame = 0;
    std::vector<std::uint8_t> data;
    auto operator<=>(const Out&) const = default;
};
using Staged = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;

// One block, `staged` its host MIDI: what the plan sent out, sorted (rp.host.midi-out merges by frame; within one, the
// order is not what these cases hold).
std::vector<Out> block(Engine& e, const Staged& staged = {}) {
    for (const auto& [frame, data] : staged) e.stageMidi(frame, data);
    std::vector<float> l(kFrames), r(kFrames);
    e.processBlock(kFrames, l.data(), r.data());
    std::vector<Out> out;
    for (const auto& m : e.midiOut()) out.push_back({m.system, m.frame, m.data});
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Out> sorted(std::vector<Out> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// `id` loaded from `source`, required to load (and why not said when it does not).
Engine::ScriptPackResult load(Engine& e, const std::string& id, const std::string& source, std::size_t memoryLimit = 0) {
    auto r = e.loadScriptPack(id, source, memoryLimit);
    UNSCOPED_INFO("loading " << id << ": " << r.error);
    REQUIRE(r.ok);
    return r;
}

// `request` placed, required to plan (and the planner's errors said when it does not).
void place(Engine& e, const Engine::GraphRequest& request) {
    const auto r = e.setGraph(request);
    for (const auto& err : r.errors)
        UNSCOPED_INFO(err.code << " at " << err.nodeId << "." << err.port << ": " << err.message);
    REQUIRE(r.ok);
}

// The engine's packs, each as "<id>#<generation>".
std::vector<std::string> packIds(const Engine& e) {
    std::vector<std::string> ids;
    for (const auto& p : e.scriptPacks()) ids.push_back(p.id + "#" + std::to_string(p.generation));
    return ids;
}

const std::vector<std::uint8_t> kNote{0x90, 60, 100};

}  // namespace

TEST_CASE("a pack's nodes run beside the built-in pack's in one document, and its load lists its types with their "
          "descriptors",
          "[script][packs]") {
    Engine e(kRate);
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1"});
    const std::size_t builtIn = e.nodeTypeCount();
    const auto loaded = load(e, "test.shift", toys(transposer("test.shift.up", 12) + transposer("test.shift.down", -12)));
    CHECK(loaded.error.empty());
    CHECK(loaded.errors.empty());
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.shift#1"});
    CHECK(e.nodeTypeCount() == builtIn + 2);
    // Its types in registration order, each with its descriptor as the registry holds it: canonical, every field
    // spelled (the name, never given, is the id).
    REQUIRE(loaded.types.size() == 2);
    CHECK(loaded.types[0].id == "test.shift.up");
    CHECK(loaded.types[1].id == "test.shift.down");
    for (const auto& t : loaded.types) {
        INFO(t.descriptor);
        const auto d = rp::graph::parseDescriptor(t.descriptor);
        REQUIRE(d);
        CHECK(d->id == t.id);
        CHECK(d->name == t.id);
        REQUIRE(d->layout.ports.size() == 2);
        CHECK(d->layout.ports[0].id == "in");
        CHECK(d->layout.ports[0].dir == rp::graph::PortDir::In);
        CHECK(d->layout.ports[0].kind == rp::graph::PortKind::Events);
        CHECK(d->layout.ports[1].id == "out");
        CHECK(d->layout.ports[1].dir == rp::graph::PortDir::Out);
        CHECK(t.descriptor == rp::graph::descriptorJson(*d));
    }

    // Host MIDI through the built-in routing, by channel: channel 0 to system 1 through the pack's node, which moves its
    // note up an octave, and channel 1 to system 2 straight, its channel rewritten to 0 (the routing's own job).
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("midiChannelToInstance", "1,2"),
                       docNode("up", "test.shift.up"), midiOut("1,2")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "up.in"),
                       docWire("up.out", "midi-out.in1"), docWire("routing.sys-2", "midi-out.in2")}));
    CHECK(block(e, {{3, {0x90, 60, 100}}, {5, {0x91, 64, 90}}}) ==
          sorted({{1, 3, {0x90, 72, 100}}, {2, 5, {0x90, 64, 90}}}));
    CHECK(block(e).empty());
    CHECK(block(e, {{0, {0x80, 61, 0}}}) == std::vector<Out>{{1, 0, {0x80, 73, 0}}});
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("what one pack does to its realm reaches no node of another: a getter on Object.prototype, Array.prototype.push "
          "replaced, a global",
          "[script][packs]") {
    // Each pack's probe sends what it sees as a SysEx: whether a property only pack A's getter answers is there,
    // whether a push pushes, and whether pack A's global is set.
    const std::string probe = R"JS(
var probe = function (id) {
  toy.define(id, [toy.output("out")], function () {
    return { process: function (b) {
      var getter = ({}).onlyAGetterAnswers === undefined ? 0 : 1;
      var a = [], pushed = a.push(5);
      var push = pushed === 1 && a.length === 1 && a[0] === 5 ? 1 : 0;
      var global = typeof globalThis.leakedByA === "undefined" ? 0 : 1;
      b.send("out", 0, [0xF0, getter, push, global, 0xF7]);
    } };
  });
};
)JS";
    Engine e(kRate);
    load(e, "test.a", toys(probe + R"JS(
probe("test.a.probe");
Object.defineProperty(Object.prototype, "onlyAGetterAnswers", { get: function () { return 7; }, configurable: true });
Array.prototype.push = function () { return -1; };
globalThis.leakedByA = 42;
)JS"));
    load(e, "test.b", toys(probe + R"JS(probe("test.b.probe");)JS"));
    // The built-in routing beside them, placed after both loads, by channel: its create builds its rewritten views with
    // a push, so a realm pack A had reached would leave it none to send channel 1's note with.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("midiChannelToInstance", "3,4"),
                       docNode("a", "test.a.probe"), docNode("b", "test.b.probe"), midiOut("1,2,4")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("a.out", "midi-out.in1"),
                       docWire("b.out", "midi-out.in2"), docWire("routing.sys-4", "midi-out.in3")}));
    for (int n = 0; n < 3; ++n) {
        INFO("block " << n);
        // Pack A's probe sees all three in its own realm; pack B's, none.
        CHECK(block(e, {{7, {0x91, 64, 100}}}) ==
              sorted({{1, 0, {0xF0, 1, 0, 1, 0xF7}}, {2, 0, {0xF0, 0, 1, 0, 0xF7}}, {4, 7, {0x90, 64, 100}}}));
    }
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

namespace {

// `id` from `source` refused, `why` in the error, changing nothing: no pack added, the registry as it was, and a
// document naming `type` (one the source defines, or would) refused as naming no type.
void refused(Engine& e, const std::string& id, const std::string& source, const std::string& why, const std::string& type) {
    const auto packsBefore = packIds(e);
    const std::size_t typesBefore = e.nodeTypeCount();
    const auto r = e.loadScriptPack(id, source);
    INFO(id << ": " << r.error);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find(why) != std::string::npos);
    CHECK(r.types.empty());
    CHECK(r.errors.empty());
    CHECK(packIds(e) == packsBefore);
    CHECK(e.nodeTypeCount() == typesBefore);
    if (type.empty()) return;
    const auto placed = e.setGraph(document({docNode("n", type)}, {}));
    CHECK_FALSE(placed.ok);
    REQUIRE_FALSE(placed.errors.empty());
    CHECK(placed.errors[0].code == "unknown-type");
}

}  // namespace

TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's, one of the app's, one loaded, a type "
          "outside its namespace, a compile error, a definition that cannot be read, one the registry refuses",
          "[script][packs][refused]") {
    Engine e(kRate);
    refused(e, "test pack", toys(transposer("test pack.up", 12)), "must be non-empty, with no whitespace", "test pack.up");
    refused(e, "rp.script", toys(transposer("rp.script.up", 12)), "rp.script is the built-in pack, which is not reloadable",
            "rp.script.up");
    // "rp" and the ids under it are the app's to give: its built-in pack's and its native packages' (rp.core, rp.host,
    // ...), and a pack named "rp.midi" would hold the built-in rp.midi.route's namespace.
    for (const std::string app : {"rp", "rp.mine", "rp.core", "rp.midi"}) {
        INFO(app);
        refused(e, app, toys(transposer(app + ".up", 1)), "is the app's to give: \"rp\" and every id under \"rp.\"",
                app + ".up");
    }

    load(e, "test.once", toys(transposer("test.once.first", 1)));
    refused(e, "test.once", toys(transposer("test.once.second", 2)), "pack \"test.once\" is already loaded",
            "test.once.second");

    // A type id is the pack's id, a dot and a name: not another prefix, the bare id, or the id and a dot.
    for (const std::string stray : {"test.other.stray", "test.nsx.stray", "test.ns", "test.ns."}) {
        INFO(stray);
        refused(e, "test.ns", toys(transposer("test.ns.fine", 1) + transposer(stray, 2)),
                "(" + stray + ") is outside pack \"test.ns\": its type ids are \"test.ns.<name>\"", "test.ns.fine");
    }

    // The compile error's line, in the source as given (no toy prelude here).
    refused(e, "test.syntax", "var a = 1;\nvar b = 2;\nvar c = ;\n", "the source does not compile: line 3: SyntaxError: ",
            "test.syntax.up");
    refused(e, "test.syntax",
            toys("toy.define('test.syntax.up', [toy.output('out')], function () {\n  return { process: function (b) {\n"),
            "SyntaxError", "test.syntax.up");

    // A definition with no descriptor: the pack's other types go with it.
    refused(e, "test.nodesc", toys(transposer("test.nodesc.fine", 1) + R"JS(
globalThis.__rp_scriptNodes[globalThis.__rp_scriptNodes.length] = {
  id: "test.nodesc.bare", parseSettings: function (r) { return r || {}; }, layout: function () { return { ports: [] }; },
  create: function () { return { process: function () {} }; },
};
)JS"),
            "test.nodesc.bare: its descriptor: the definition has no descriptor", "test.nodesc.fine");
    // A top level that throws.
    refused(e, "test.throws", toys(transposer("test.throws.up", 1) + "throw new Error('not today');"),
            "chunk 0: Error: not today", "test.throws.up");

    // What the registry refuses, once the pack is built: a descriptor the planner could not use, and a type another
    // pack holds, which two packs' namespaces can both hold where one's id runs on into the other's ("test.nest" and
    // "test.nest.x" can both name "test.nest.x.up"). The pack goes; the type stays the first pack's.
    refused(e, "test.baddesc", toys(R"JS(
toy.define("test.baddesc.odd", [{ id: "in", dir: "in", kind: "smoke" }], function () {
  return { process: function () {} };
});
)JS"),
            "the registry refused the pack's types: ", "test.baddesc.odd");
    load(e, "test.nest", toys(transposer("test.nest.x.up", 1)));
    refused(e, "test.nest.x", toys(transposer("test.nest.x.up", 2)), "node type \"test.nest.x.up\" is already registered",
            "");
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("nest", "test.nest.x.up"), midiOut("1")},
                      {docWire("midi-in.midi", "nest.in"), docWire("nest.out", "midi-out.in1")}));
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, {0x90, 61, 100}}});  // the first pack's: a semitone, not two

    // Each id refused once its pack was built loads clean now: neither the script host nor the registry kept it.
    load(e, "test.ns", toys(transposer("test.ns.fine", 1)));
    load(e, "test.syntax", toys(transposer("test.syntax.up", 1)));
    load(e, "test.nodesc", toys(transposer("test.nodesc.fine", 1)));
    load(e, "test.baddesc", toys(transposer("test.baddesc.odd", 1)));
    load(e, "test.nest.x", toys(transposer("test.nest.x.down", -1)));
    // And an id JSON must escape is its package's id as it is: its manifest spells it as JSON does.
    const auto quoted = load(e, "test\"q", toys(transposer("test\"q.up", 1)));
    REQUIRE(quoted.types.size() == 1);
    CHECK(quoted.types[0].id == "test\"q.up");
    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.once#1", "test.nest#1", "test.ns#1",
                                                 "test.syntax#1", "test.nodesc#1", "test.baddesc#1", "test.nest.x#1",
                                                 "test\"q#1"});

    // The pack that was loaded when a second load of its id was refused runs its own code.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("once", "test.once.first"), midiOut("1")},
                      {docWire("midi-in.midi", "once.in"), docWire("once.out", "midi-out.in1")}));
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, {0x90, 61, 100}}});
    CHECK(e.scriptThrows() == 0);
}

TEST_CASE("a pack's node that allocates past its pack's memory limit faults, while another pack's node and a built-in one "
          "run on",
          "[script][packs][memory]") {
    Engine e(kRate);
    // Pack A held to 16 MiB, its node reaching for 32 on its second block; pack B at the default, its node taking 24 and
    // keeping them, more than pack A may hold.
    constexpr std::size_t kLimit = std::size_t{16} << 20;
    const auto mebibytes = [](int n) {
        return "for (var k = 0; k < " + std::to_string(n) + "; k++) kept[k] = new Float64Array(1 << 17);";
    };
    load(e, "test.hog", toys(counter("test.hog.grows", mebibytes(32))), kLimit);
    load(e, "test.big", toys(counter("test.big.holds", mebibytes(24))));
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "3"), docNode("hog", "test.hog.grows"),
                       docNode("big", "test.big.holds"), midiOut("1,2,3")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("hog.out", "midi-out.in1"),
                       docWire("big.out", "midi-out.in2"), docWire("routing.sys-3", "midi-out.in3")}));
    CHECK(block(e, {{4, kNote}}) == sorted({{1, 0, {1}}, {2, 0, {1}}, {3, 4, kNote}}));
    // The hog runs out: faulted at once, so its block is silent, its throw the out-of-memory error it let go.
    CHECK(block(e, {{4, kNote}}) == sorted({{2, 0, {2}}, {3, 4, kNote}}));
    CHECK(e.faultCount() == 1);
    CHECK(e.scriptThrows() == 1);
    for (std::uint8_t n = 3; n <= 6; ++n) {
        INFO("block " << int{n});
        CHECK(block(e, {{4, kNote}}) == sorted({{2, 0, {n}}, {3, 4, kNote}}));
    }
    CHECK(e.faultCount() == 1);
    CHECK(e.scriptThrows() == 1);
}

TEST_CASE("a pack's node whose promise chain never settles faults after its two cut shorts, while another pack's node, its "
          "own promises settling, is charged nothing",
          "[script][packs][jobs]") {
    Engine e(kRate);
    load(e, "test.chain", toys(R"JS(
toy.define("test.chain.endless", [toy.output("out")], function () {
  var n = 0, started = false;
  var link = function () { Promise.resolve().then(link); };
  return { process: function (b) {
    b.send("out", 0, [++n & 0x7f]);
    if (!started) { started = true; link(); }
  } };
});
)JS"));
    // Each block it sends its count and what its last block's reaction settled, which ran at the checkpoint after it.
    load(e, "test.calm", toys(R"JS(
toy.define("test.calm.settles", [toy.output("out")], function () {
  var n = 0, settled = 0;
  return { process: function (b) {
    b.send("out", 0, [++n & 0x7f, settled & 0x7f]);
    Promise.resolve(n).then(function (v) { settled = v; });
  } };
});
)JS"));
    // The chain's node first, so the other's checkpoint follows its leftovers in every block: a pack-mate's would run them.
    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("chain", "test.chain.endless"),
                       docNode("calm", "test.calm.settles"), routing("sendToAll", "3"), midiOut("1,2,3")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("chain.out", "midi-out.in1"),
                       docWire("calm.out", "midi-out.in2"), docWire("routing.sys-3", "midi-out.in3")}));
    CHECK(block(e, {{9, kNote}}) == sorted({{1, 0, {1}}, {2, 0, {1, 0}}, {3, 9, kNote}}));
    CHECK(e.scriptThrows() == 1);  // the chain cut short once: its throw
    CHECK(e.faultCount() == 0);
    CHECK(block(e, {{9, kNote}}) == sorted({{2, 0, {2, 1}}, {3, 9, kNote}}));  // cut short again: faulted, its block silent
    CHECK(e.scriptThrows() == 2);
    CHECK(e.faultCount() == 1);
    for (std::uint8_t n = 3; n <= 8; ++n) {
        INFO("block " << int{n});
        CHECK(block(e, {{9, kNote}}) == sorted({{2, 0, {n, static_cast<std::uint8_t>(n - 1)}}, {3, 9, kNote}}));
    }
    CHECK(e.scriptThrows() == 2);
    CHECK(e.faultCount() == 1);
}

TEST_CASE("a pack loads while an audio thread runs blocks, and the plan that places its node runs it on that thread",
          "[script][packs][threads]") {
    Engine e(kRate);
    place(e, document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "1"), midiOut("1")},
                      {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1")}));
    e.setAudioThreadOwns(true);
    // The audio thread: each block stages a note, which the built-in routing's script node passes on, and counts the
    // blocks whose MIDI out holds it as it was (system 1) and moved up an octave (system 2), which only the pack's node
    // does.
    std::atomic<bool> running{true};
    std::atomic<int>  moved{0}, plain{0};
    std::thread       audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.stageMidi(0, kNote);
            e.processBlock(kFrames, l.data(), r.data());
            for (const auto& m : e.midiOut()) {
                if (m.system == 2 && m.data == std::vector<std::uint8_t>{0x90, 72, 100}) moved.fetch_add(1);
                if (m.system == 1 && m.data == kNote) plain.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    const auto waitFor = [](const std::atomic<int>& count, int n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (count.load() < n && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return count.load() >= n;
    };
    const bool started = waitFor(plain, 10);  // the routing's JS runs in the built-in pack's audio runtime meanwhile
    // The pack's top level runs past QuickJS's interrupt poll many times over in each runtime, while the block thread
    // is making calls into the built-in pack's: a handler polled in the load would share the deadline state of those
    // calls (ThreadSanitizer's case), which is why a pack joins it only once loaded.
    const auto loaded =
        e.loadScriptPack("test.live", toys("for (var i = 0; i < 300000; i++) {}\n" + transposer("test.live.up", 12)));
    INFO(loaded.error);
    const auto request = document({docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", "1,2"),
                                   docNode("up", "test.live.up"), midiOut("1,2")},
                                  {docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1"),
                                   docWire("routing.sys-2", "up.in"), docWire("up.out", "midi-out.in2")});
    bool placed = false;
    std::string refusal;
    for (int attempt = 0; loaded.ok && !placed && attempt < 1000; ++attempt) {
        const auto r = e.setGraph(request);
        placed = r.ok;
        if (!r.ok) {
            refusal = r.errors.empty() ? "" : r.errors[0].code + ": " + r.errors[0].message;
            if (refusal.rfind("busy", 0) != 0) break;  // busy is the ring full, which the audio thread drains: again
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        e.reclaim();
    }
    const bool ran = placed && waitFor(moved, 10);
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(started);
    REQUIRE(loaded.ok);
    INFO(refusal);
    REQUIRE(placed);
    CHECK(ran);
    // And on this thread, quiescent again, the same plan.
    CHECK(block(e, {{6, kNote}}) == sorted({{1, 6, kNote}, {2, 6, {0x90, 72, 100}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

namespace {

// Three types whose settings check never returns, in parseSettings, in layout, and a third that is fine. Their
// descriptors need neither, so the pack loads, and only a document placing one of the first two asks.
const std::string kStuck = R"JS(
var idle = function () { return { process: function () {} }; };
toy.define("test.stuck.settings", [toy.input("in"), toy.output("out")], idle);
toy.define("test.stuck.layout", [toy.input("in"), toy.output("out")], idle);
var defs = globalThis.__rp_scriptNodes;
defs[defs.length - 2].parseSettings = function () { for (;;) {} };
defs[defs.length - 1].layout = function () { for (;;) {} };
)JS" + transposer("test.stuck.fine", 12);

// The running plan: host MIDI through the built-in routing to the host MIDI out, under system 1, and to `type`, if
// given, whose output goes out under system 2.
Engine::GraphRequest routedTo(const std::string& type = "") {
    const std::string systems = type.empty() ? "1" : "1,2";
    std::vector<std::string> nodes{docNode("midi-in", "rp.host.midi-in"), routing("sendToAll", systems),
                                   midiOut(systems)};
    std::vector<std::string> wires{docWire("midi-in.midi", "routing.midiIn"), docWire("routing.sys-1", "midi-out.in1")};
    if (!type.empty()) {
        nodes.push_back(docNode("node", type));
        wires.push_back(docWire("routing.sys-2", "node.in"));
        wires.push_back(docWire("node.out", "midi-out.in2"));
    }
    return document(std::move(nodes), std::move(wires));
}

// `request` refused because the node's settings check ran past a 50 ms deadline in `where`.
void stoppedCheck(Engine& e, const Engine::GraphRequest& request, const std::string& type, const std::string& where) {
    const auto r = e.setGraph(request);
    CHECK_FALSE(r.ok);
    REQUIRE(r.errors.size() == 1);
    INFO(r.errors[0].message);
    CHECK(r.errors[0].code == "settings-invalid");
    CHECK(r.errors[0].nodeId == "node");
    CHECK(r.errors[0].message == type + "'s " + where + " ran past the 50 ms call deadline (CPU time), and was stopped");
}

constexpr const char* kLoadStopped = "its load ran past the 50 ms call deadline (CPU time), and was stopped";

}  // namespace

TEST_CASE("the control thread's calls into a pack are held to the call deadline: a load that never returns is refused, "
          "and a settings check that never returns refuses its document while the running plan runs on",
          "[script][packs][deadline]") {
    Engine e(kRate);
    e.setScriptCallDeadline(std::chrono::milliseconds(50));
    place(e, routedTo());
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    const auto packsBefore = packIds(e);
    const std::size_t typesBefore = e.nodeTypeCount();
    const auto hung = e.loadScriptPack("test.hang", toys(transposer("test.hang.up", 1) + "for (;;) {}"));
    INFO(hung.error);
    CHECK_FALSE(hung.ok);
    CHECK(hung.error.rfind(kLoadStopped, 0) == 0);
    CHECK(packIds(e) == packsBefore);
    CHECK(e.nodeTypeCount() == typesBefore);
    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});

    load(e, "test.stuck", toys(kStuck));
    stoppedCheck(e, routedTo("test.stuck.settings"), "test.stuck.settings", "parseSettings");
    stoppedCheck(e, routedTo("test.stuck.layout"), "test.stuck.layout", "layout");
    for (int n = 0; n < 3; ++n) CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, kNote}});
    // A pack whose call was stopped is no worse for it: its fine type plans and runs.
    place(e, routedTo("test.stuck.fine"));
    CHECK(block(e, {{2, kNote}}) == sorted({{1, 2, kNote}, {2, 2, {0x90, 72, 100}}}));
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("the control side's deadline keeps its own clock while an audio thread is making calls into another pack",
          "[script][packs][deadline][threads]") {
    Engine e(kRate);
    e.setScriptCallDeadline(std::chrono::milliseconds(50));
    place(e, routedTo());
    e.setAudioThreadOwns(true);
    std::atomic<bool> running{true};
    std::atomic<int>  plain{0}, moved{0};
    std::thread       audio([&] {
        std::vector<float> l(kFrames), r(kFrames);
        while (running.load()) {
            e.drainCommands();
            e.stageMidi(0, kNote);
            e.processBlock(kFrames, l.data(), r.data());
            for (const auto& m : e.midiOut()) {
                if (m.system == 1 && m.data == kNote) plain.fetch_add(1);
                if (m.system == 2 && m.data == std::vector<std::uint8_t>{0x90, 72, 100}) moved.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    const auto waitFor = [](const std::atomic<int>& count, int n) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (count.load() < n && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return count.load() >= n;
    };
    const bool started = waitFor(plain, 10);
    // Each stopped call runs its 50 ms on this thread while the block thread's routing calls run on theirs, each on its
    // own clock: a block's call ends well inside its deadline, and the load and the check are stopped.
    const auto hung = e.loadScriptPack("test.hang", toys(transposer("test.hang.up", 1) + "for (;;) {}"));
    const auto stuck = e.loadScriptPack("test.stuck", toys(kStuck));
    const auto refusedPlan = e.setGraph(routedTo("test.stuck.settings"));
    const int afterRefusal = plain.load();
    const bool ranOn = waitFor(plain, afterRefusal + 10);  // the running plan, as it was
    bool placed = false;
    for (int attempt = 0; stuck.ok && !placed && attempt < 1000; ++attempt) {
        const auto r = e.setGraph(routedTo("test.stuck.fine"));
        placed = r.ok;
        if (!r.ok && (r.errors.empty() || r.errors[0].code != "busy")) break;
        if (!r.ok) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        e.reclaim();
    }
    const bool ranFine = placed && waitFor(moved, 10);
    running = false;
    audio.join();
    e.setAudioThreadOwns(false);
    e.reclaim();
    CHECK(started);
    INFO(hung.error);
    CHECK_FALSE(hung.ok);
    CHECK(hung.error.rfind(kLoadStopped, 0) == 0);
    REQUIRE(stuck.ok);
    CHECK_FALSE(refusedPlan.ok);
    REQUIRE(refusedPlan.errors.size() == 1);
    CHECK(refusedPlan.errors[0].message ==
          "test.stuck.settings's parseSettings ran past the 50 ms call deadline (CPU time), and was stopped");
    CHECK(ranOn);
    CHECK(placed);
    CHECK(ranFine);
    CHECK(e.scriptThrows() == 0);
    CHECK(e.faultCount() == 0);
}

TEST_CASE("the call deadline holds every pack's control side, the built-in one's too, and a loaded pack's audio runtime "
          "while its load owns it",
          "[script][packs][deadline]") {
    using rp::host::test::compileToys;
    // The built-in pack's settings check: the test bundle and the stuck toys loaded as the rig's one bundle.
    rp::host::test::ScriptRig rig(toys(kStuck));
    rig.scripts.setCallDeadline(std::chrono::milliseconds(50));
    const auto placed = rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},)"
                                R"({"id":"node","type":"test.stuck.settings"}],)"
                                R"("wires":[{"from":"feed.midi","to":"node.in"}]})");
    CHECK_FALSE(placed.ok);
    REQUIRE(placed.errors.size() == 1);
    CHECK(placed.errors[0].message ==
          "test.stuck.settings's parseSettings ran past the 50 ms call deadline (CPU time), and was stopped");

    // A loaded pack's load, stopped wherever it hangs: its audio runtime's top level (the control side's loaded fine),
    // a job its top level queued, a definition's descriptor. None is added.
    const std::vector<std::pair<std::string, std::string>> hangs{
        {"test.audiohang", transposer("test.audiohang.up", 1) + "if (__audioSide) for (;;) {}"},
        {"test.jobhang", transposer("test.jobhang.up", 1) + "Promise.resolve().then(function () { for (;;) {} });"},
        {"test.deschang",
         transposer("test.deschang.up", 1) +
             "var d = globalThis.__rp_scriptNodes;\nd[d.length - 1].descriptor = function () { for (;;) {} };"},
    };
    for (const auto& [id, body] : hangs) {
        INFO(id);
        const auto bytecode = compileToys(toys(body));
        const ScriptHost::Chunk chunk = bytecode;
        std::string error;
        CHECK(rig.scripts.loadPack(id, {&chunk, 1}, 0, &error) == nullptr);
        INFO(error);
        CHECK(error.rfind(kLoadStopped, 0) == 0);
        CHECK(rig.scripts.packs().size() == 1);
    }

    // The built-in pack's load, stopped as a loaded pack's is: its package then holds no type.
    ScriptHost host;
    host.setCallDeadline(std::chrono::milliseconds(50));
    const auto hang = compileToys(toys(transposer("test.builtin.up", 1) + "for (;;) {}"));
    const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), hang};
    std::string error;
    CHECK_FALSE(host.load(chunks, &error));
    INFO(error);
    CHECK(error.rfind(kLoadStopped, 0) == 0);
    CHECK(host.package()->node_type_count == 0);
}

TEST_CASE("a pack is bound by the host's binder in both its contexts, and what its load allocates is in no audio-side "
          "count, where its nodes' are",
          "[script][packs]") {
    using rp::host::test::Feed;
    using rp::host::test::Probe;
    rp::host::test::ScriptRig rig;
    const auto before = rig.scripts.audioAllocations();
    const auto bytecode = rp::host::test::compileToys(toys(R"JS(
__probe("loaded " + (__audioSide ? "audio" : "control"));
var junk = [];
for (var k = 0; k < 64; k++) junk[k] = { k: k };
toy.define("test.bound.pass", [toy.input("in"), toy.output("out")], function () {
  return { process: function (b) {
    var ms = b.messages("in");
    for (var i = 0; i < ms.length; i++) b.send("out", ms[i].frame, ms[i].data);
  } };
});
)JS"));
    const ScriptHost::Chunk chunk = bytecode;
    std::string error;
    const rp_package* package = rig.scripts.loadPack("test.bound", {&chunk, 1}, 0, &error);
    INFO(error);
    REQUIRE(package != nullptr);
    CHECK(rig.log.count("loaded control") == 1);
    CHECK(rig.log.count("loaded audio") == 1);
    const auto loaded = rig.scripts.audioAllocations();
    CHECK(loaded.allocs == before.allocs);  // the load's runtimes, its top level and its GC: the control thread's own
    CHECK(loaded.frees == before.frees);
    CHECK(loaded.bytes == before.bytes);
    const auto packs = rig.scripts.packs();
    REQUIRE(packs.size() == 2);
    CHECK(packs[0].id == "rp.script");
    CHECK(packs[1].id == "test.bound");
    CHECK(packs[1].generation == 1);
    CHECK(package->node_type_count == 1);
    CHECK(std::string(package->manifest_json) == R"({"id":"test.bound"})");

    REQUIRE(rig.registry.addPackage(package, &error));
    REQUIRE(rig.set(R"({"nodes":[{"id":"feed","type":"rp.test.feed"},{"id":"pass","type":"test.bound.pass"},)"
                    R"({"id":"probe","type":"rp.test.probe"}],)"
                    R"("wires":[{"from":"feed.midi","to":"pass.in"},{"from":"pass.out","to":"probe.events"}]})")
                .ok);
    rig.node<Feed>("feed").next[Feed::Midi] = {{2, {0x90, 1, 2}}};
    rig.run();
    REQUIRE(rig.node<Probe>("probe").events.size() == 1);
    CHECK(rig.node<Probe>("probe").events[0].data == std::vector<std::uint8_t>{0x90, 1, 2});
    CHECK(rig.scripts.audioAllocations().allocs > loaded.allocs);  // its node's create and first block, on the block thread
    CHECK(rig.scripts.throwCount() == 0);
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 40158fd5..f5b5f26e 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -78,8 +78,9 @@ retroplug_add_test_binary(
 # Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md):
 # each system node renders exactly what its core renders driven directly, its inputs included, the engine places,
 # re-arms and frees what the documents TS builds say, the host I/O nodes and the cart mirror move what the hosts stage
-# and drain, TS nodes run behind the C ABI in the script host, the protocol script nodes send what the DSP kernel they
-# replaced was recorded sending, and every document in buildGraph's goldens, its role goldens among them, plans on it.
+# and drain, TS nodes run behind the C ABI in the script host, a script node pack loads beside the built-in one into
+# runtimes of its own, the protocol script nodes send what the DSP kernel they replaced was recorded sending, and every
+# document in buildGraph's goldens, its role goldens among them, plans on it.
 # A hidden bench times a script node per block.
 # Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
 # CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
@@ -91,6 +92,7 @@ retroplug_add_test_binary(
              test/engine-graph/EngineGraph.test.cpp
              test/engine-graph/HostIo.test.cpp
              test/engine-graph/ScriptNodes.test.cpp
+             test/engine-graph/ScriptPacks.test.cpp
              test/engine-graph/ProtocolNodes.test.cpp
              test/engine-graph/ScriptBench.test.cpp    # hidden: run with "[bench]"
              test/engine-graph/BuildGraphGoldens.test.cpp
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake -S . -B build && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops in `ScriptPacks.test.cpp`: `Engine` has no `loadScriptPack`, `scriptPacks`, `nodeTypeCount` or `setScriptCallDeadline`, and `ScriptHost` no `loadPack` or `packs`, yet. (The prototype's red runs, made with the rest of this task in place but each rule taken out in turn, saw the refusals' case fail 7 of its 26 checks without the `rp.` rule, and the deadline cases hang until a 30-second `timeout` killed them without the control-side deadline.)

- [ ] **Step 3: Give the compiler's error**

`dsp::compileToBytecode` said nothing on a compile error; it now hands back the error's text with its line (QuickJS
puts no position on the error object, and the column its stack gives is unreliable, often 1), its parser held to the
script host's 256 KiB stack, since it compiles user source on the Engine's control thread:

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index a182fcd1..beafec60 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -152,7 +152,7 @@ add_library(retroplug-backend STATIC
     src/system/CoreBackends.cpp             # registerCoreBackends: mounts sameboy + mesen on a factory
     src/system/sameboy/SameBoyBackend.cpp   # SameBoy builder — the only place SameBoyConfig is constructed
     src/system/mesen/MesenBackend.cpp       # Mesen builder — NES + GBA (HLE BIOS), dispatched on platform
-    src/host/dsp/ScriptCompiler.cpp         # bare-QuickJS bytecode compiler (the engine-graph tests' toy nodes)
+    src/host/dsp/ScriptCompiler.cpp         # bare-QuickJS bytecode compiler (script packs' source, the tests' toys)
     src/host/script/ScriptHost.cpp          # the script-node runtime: TS nodes behind the C ABI, two bare contexts
     src/host/render/RenderHost.cpp          # bare-QuickJS render host (own Engine, runs the worker bundle)
     src/host/render/RenderJobRegistry.cpp   # background render jobs — one worker thread each, cancellable
diff --git a/packages/native/src/host/dsp/ScriptCompiler.cpp b/packages/native/src/host/dsp/ScriptCompiler.cpp
index 3352f5b9..81293b75 100644
--- a/packages/native/src/host/dsp/ScriptCompiler.cpp
+++ b/packages/native/src/host/dsp/ScriptCompiler.cpp
@@ -1,31 +1,89 @@
 #include "host/dsp/ScriptCompiler.hpp"
 
+#include <cstdlib>
+#include <string_view>
+
 extern "C" {
 #include "quickjs.h"
 }
 
+#include "host/script/ScriptHost.hpp"  // kStackBytes
+
 namespace dsp {
 
-std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source) {
+namespace {
+
+// The name the compiled code goes by: what a syntax error's position names first in its stack ("    at <script>:3:1").
+constexpr std::string_view kName = "<script>";
+
+// `v` as text ("" when it has none), its exception cleared.
+std::string text(JSContext* ctx, JSValueConst v) {
+    std::size_t n = 0;
+    const char* s = JS_ToCStringLen(ctx, &n, v);
+    if (!s) {
+        JS_FreeValue(ctx, JS_GetException(ctx));
+        return {};
+    }
+    std::string out(s, n);
+    JS_FreeCString(ctx, s);
+    return out;
+}
+
+// The pending compile error, as the header says: "Name: message", after "line L: " when the error's stack names a place
+// in the source. QuickJS puts no position on the error object itself (no lineNumber, no columnNumber): the parser
+// writes it as the stack's first frame, so it is read from there.
+std::string compileError(JSContext* ctx) {
+    JSValue thrown = JS_GetException(ctx);
+    std::string out = text(ctx, thrown);
+    if (out.empty()) out = "the source does not compile";
+    JSValue stack = JS_IsError(thrown) ? JS_GetPropertyStr(ctx, thrown, "stack") : JS_UNDEFINED;
+    const std::string frames = JS_IsString(stack) ? text(ctx, stack) : std::string();
+    const std::string at = "at " + std::string(kName) + ":";
+    if (const auto p = frames.find(at); p != std::string::npos) {
+        const char* digits = frames.c_str() + p + at.size();
+        char* end = nullptr;
+        const long line = std::strtol(digits, &end, 10);
+        if (end != digits && line > 0) out = "line " + std::to_string(line) + ": " + out;
+    }
+    JS_FreeValue(ctx, stack);
+    JS_FreeValue(ctx, thrown);
+    return out;
+}
+
+}  // namespace
+
+std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source, std::string* error) {
+    const auto fail = [&](std::string why) -> std::optional<std::vector<std::uint8_t>> {
+        if (error) *error = std::move(why);
+        return std::nullopt;
+    };
     JSRuntime* rt = JS_NewRuntime();
-    if (!rt) return std::nullopt;
+    if (!rt) return fail("no QuickJS runtime could be made");
+    // The parser recurses on the calling thread, which may be the Engine's control thread: held to the script host's
+    // stack, under the smallest thread that may call it (ScriptHost's class comment), so deeply nested source is a
+    // compile error rather than an overflowed stack.
+    JS_SetMaxStackSize(rt, rp::host::ScriptHost::kStackBytes);
     JSContext* ctx = JS_NewContext(rt);
     if (!ctx) {
         JS_FreeRuntime(rt);
-        return std::nullopt;
+        return fail("no QuickJS context could be made");
     }
 
     std::optional<std::vector<std::uint8_t>> result;
-
-    // ES5 global code, compile-only → a JS_TAG_FUNCTION_BYTECODE object.
-    JSValue fn = JS_Eval(ctx, source.c_str(), source.size(), "<dsp-script>",
-                         JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
-    if (!JS_IsException(fn)) {
+    // Global code, compile-only: a JS_TAG_FUNCTION_BYTECODE object.
+    JSValue fn =
+        JS_Eval(ctx, source.c_str(), source.size(), kName.data(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
+    if (JS_IsException(fn)) {
+        fail(compileError(ctx));
+    } else {
         std::size_t len = 0;
         std::uint8_t* buf = JS_WriteObject(ctx, &len, fn, JS_WRITE_OBJ_BYTECODE);
         if (buf) {
             result = std::vector<std::uint8_t>(buf, buf + len);
             js_free(ctx, buf);
+        } else {
+            JS_FreeValue(ctx, JS_GetException(ctx));
+            fail("the bytecode could not be written (out of memory)");
         }
     }
     JS_FreeValue(ctx, fn);
diff --git a/packages/native/src/host/dsp/ScriptCompiler.hpp b/packages/native/src/host/dsp/ScriptCompiler.hpp
index c79d9d93..9cc2fd5a 100644
--- a/packages/native/src/host/dsp/ScriptCompiler.hpp
+++ b/packages/native/src/host/dsp/ScriptCompiler.hpp
@@ -5,16 +5,19 @@
 #include <string>
 #include <vector>
 
-// Compile JS source, as global code, to QuickJS bytecode on a transient, bare QuickJS context. The
-// engine-graph tests compile their toy script nodes with it (ScriptRig.hpp, ProtocolNodes.test.cpp), and
-// load the bytecode into a ScriptHost as a bundle chunk. (It compiled the scripts the control plane
-// sent the native DSP runtime, through the compileScript RPC, until both were deleted.)
+// Compile JS source, as global code, to QuickJS bytecode on a transient, bare QuickJS runtime. Engine::loadScriptPack
+// compiles a script node pack's source with it, on the control thread, and loads the bytecode into the pack's runtimes
+// as its one chunk (docs/modular-graph-phase-c1-design.md section 4); the engine-graph tests compile their toy script
+// nodes with it (ScriptRig.hpp, ProtocolNodes.test.cpp), and load the bytecode into a ScriptHost as a bundle chunk.
 //
-// Returns the bytecode bytes (JS_WriteObject / JS_WRITE_OBJ_BYTECODE) or nullopt on a compile
-// error. The bytecode is version-locked to this exact qjs-ng build, which is fine: the
-// compiler and the script host's runtimes are the same `qjs`.
+// Returns the bytecode bytes (JS_WriteObject / JS_WRITE_OBJ_BYTECODE), or nullopt on a compile error, whose text goes
+// into *error when it is given: QuickJS's own, after the line it was found on when QuickJS says (a syntax error does),
+// as "line 3: SyntaxError: unexpected token in expression: ';'". Its parser records a column as well, but the one a
+// syntax error carries is wherever the parser last set it (the line's first column, as often as not), so it is left
+// out. The bytecode is version-locked to this exact qjs-ng build, which is fine: the compiler and the script host's
+// runtimes are the same `qjs`; and it is why a pack arrives as source, never as bytecode.
 namespace dsp {
 
-std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source);
+std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source, std::string* error = nullptr);
 
 } // namespace dsp
````

- [ ] **Step 4: Load a pack into runtimes of its own**

`ScriptHost::canLoadPack` refuses an id before anything is built (not plain, `rp.script`, `rp` or under `rp.`, loaded
already); `loadPack` builds a new pack's runtimes on the control thread, sets its memory limit, runs its chunks in
both runtimes and reads its definitions, every type id required under the pack's id, collects what the load left, and
only then `join`s the pack to the shared interrupt handler and counters: installed before, the handler races the block
thread's deadline state (ThreadSanitizer reported it in every run of the threads case). Until it joins, a pack's
allocations go to a private counter, so a loaded pack's load is counted in no audio-side counter. Every pack's
runtimes start with the control side's interrupt handler, which keeps its own deadline state (`ControlDeadline`); the
whole load is one call under it, and so is each settings check. `discardPack` frees a pack nothing placed, and
`packs()` lists them. A manifest's id is written as a JSON string:

````diff
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index 1255d7f3..86b980b3 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -25,6 +25,7 @@ extern "C" {
 #include "quickjs.h"
 }
 
+#include "graph/CanonicalJson.hpp"
 #include "graph/Descriptor.hpp"
 #include "graph/Queue.hpp"
 
@@ -427,18 +428,54 @@ struct ScriptHost::Glue {
         Deadline& operator=(const Deadline&) = delete;
     };
 
-    // QuickJS asks once every 10000 of its polls (at each call and each jump back in a loop) whether to stop.
+    // QuickJS asks once every 10000 of its polls (at each call and each jump back in a loop) whether to stop: the
+    // block thread's clock in an audio runtime that has joined, the control thread's everywhere else (controlInterrupt).
     static int interrupt(JSRuntime*, void* opaque) {
         auto& h = *static_cast<ScriptHost*>(opaque);
-        if (!h.armed_) return 0;
+        return expired(h, h.armed_, h.cpuStart_, h.interrupted_) ? 1 : 0;
+    }
+    // Whether the armed call one thread's clock times has used up the deadline: the baseline is taken at the call's
+    // first poll, and `interrupted` set once it has.
+    static bool expired(const ScriptHost& h, bool armed, std::int64_t& cpuStart, bool& interrupted) {
+        if (!armed) return false;
         const std::int64_t now = threadCpuNs();
-        if (h.cpuStart_ < 0) {
-            h.cpuStart_ = now;
-            return 0;
+        if (cpuStart < 0) {
+            cpuStart = now;
+            return false;
+        }
+        if (now - cpuStart < h.callDeadlineNs_.load(std::memory_order_relaxed)) return false;
+        interrupted = true;
+        return true;
+    }
+
+    // ---- the control thread's deadline ---------------------------------------------------------------------------
+    // The same length, of the control thread's own CPU time, on a clock of its own, held to every call it makes into a
+    // pack's JS: a load (the chunks in both runtimes, the jobs they queue, the definitions read) and a settings check.
+    // Never the block thread's clock: the block thread may be inside a call into another pack meanwhile.
+    struct ControlDeadline {
+        ScriptHost& h;
+        bool        nested;
+        explicit ControlDeadline(ScriptHost& host) : h(host), nested(host.controlArmed_) {
+            if (nested) return;
+            h.controlArmed_ = true;
+            h.controlCpuStart_ = -1;
+            h.controlInterrupted_ = false;
         }
-        if (now - h.cpuStart_ < h.callDeadlineNs_.load(std::memory_order_relaxed)) return 0;
-        h.interrupted_ = true;
-        return 1;
+        ~ControlDeadline() {
+            if (!nested) h.controlArmed_ = false;
+        }
+        ControlDeadline(const ControlDeadline&) = delete;
+        ControlDeadline& operator=(const ControlDeadline&) = delete;
+    };
+    // A pack's control runtime's handler for its life, and its audio runtime's until join.
+    static int controlInterrupt(JSRuntime*, void* opaque) {
+        auto& h = *static_cast<ScriptHost*>(opaque);
+        return expired(h, h.controlArmed_, h.controlCpuStart_, h.controlInterrupted_) ? 1 : 0;
+    }
+    // What a call the control thread's deadline stopped says, `what` the call.
+    static std::string stopped(const ScriptHost& h, const std::string& what) {
+        return what + " ran past the " + std::to_string(h.callDeadlineNs_.load(std::memory_order_relaxed) / 1000000) +
+               " ms call deadline (CPU time), and was stopped";
     }
 
     // After one of a node's calls: whether the deadline stopping it (`stopped`: the call, or a promise job charged to
@@ -539,18 +576,34 @@ struct ScriptHost::Glue {
             return false;
         }
         Pack& pack = *t.pack;
+        ScriptHost& h = *t.host;
         JSContext* ctx = pack.control;
         JS_UpdateStackTop(pack.controlRt);  // QuickJS guards the stack from where it was last entered: re-anchor here
+        // Its parseSettings, its layout and the jobs they queue, as one call: one the deadline stops refuses the
+        // settings, saying so, so the planner refuses the document and the running plan stands.
+        const ControlDeadline call(h);
         const std::string text = s && s->data && s->size ? std::string(s->data, s->size) : std::string("{}");
         JSValue raw = JS_ParseJSON(ctx, text.c_str(), text.size(), "<settings>");
         JSValue parsed = JS_IsException(raw) ? JS_EXCEPTION : callMethod(ctx, t.control, "parseSettings", 1, &raw);
         JSValue lay = JS_IsException(parsed) ? JS_EXCEPTION : callMethod(ctx, t.control, "layout", 1, &parsed);
-        const bool ok = !JS_IsException(lay) && stringify(ctx, parsed, settings) && stringify(ctx, lay, layout);
-        if (!ok) copyError(err, exceptionText(ctx, true));
+        bool ok = !JS_IsException(lay) && stringify(ctx, parsed, settings) && stringify(ctx, lay, layout);
+        if (!ok && h.controlInterrupted_) {
+            clearException(ctx);
+            const char* where = JS_IsException(parsed) ? "parseSettings"
+                                : JS_IsException(lay)    ? "layout"
+                                                         : "settings check";
+            copyError(err, stopped(h, t.id + "'s " + where));
+        } else if (!ok) {
+            copyError(err, exceptionText(ctx, true));
+        }
         JS_FreeValue(ctx, lay);
         JS_FreeValue(ctx, parsed);
         JS_FreeValue(ctx, raw);
         drainControlJobs(pack);
+        if (ok && h.controlInterrupted_) {
+            ok = false;
+            copyError(err, stopped(h, t.id + "'s promise jobs"));
+        }
         return ok;
     }
 
@@ -1551,7 +1604,7 @@ ScriptHost::ScriptHost(Binder binder, void* user) : binder_(binder), binderUser_
                                       std::pair{DSP_SPAN_JSCALL, "js-call"}, std::pair{DSP_SPAN_APU, "apu-render"},
                                       std::pair{DSP_SPAN_PUBLISH, "state-publish"}})
         traceName(label, name);
-    packs_.push_back(makePack(kBuiltInPack));  // loaded by load
+    packs_.push_back(makePack(kBuiltInPack, counters_));  // loaded by load
 }
 
 ScriptHost::~ScriptHost() {
@@ -1565,12 +1618,15 @@ ScriptHost::Chunk ScriptHost::embeddedBundle() { return {rp_script_bundle, rp_sc
 
 const rp_package* ScriptHost::package() const { return &packs_.front()->package; }
 
-std::unique_ptr<ScriptHost::Pack> ScriptHost::makePack(const std::string& id) {
+std::unique_ptr<ScriptHost::Pack> ScriptHost::makePack(const std::string& id, Counters& counters) {
     auto pack = std::make_unique<Pack>();
     pack->host = this;
     pack->id = id;
-    pack->manifest = R"({"id":")" + id + R"("})";
+    // Spelled as JSON spells the id: a plain id may hold a quote or a backslash, which written raw would make the
+    // manifest unreadable, or name another package.
+    pack->manifest = R"({"id":)" + graph::jsonString(id) + "}";
     pack->package = rp_package{RP_ABI_VERSION, pack->manifest.c_str(), 0, nullptr};
+    pack->heap.counters = &counters;  // before its runtime is made, which allocates
     makeRuntimes(*pack);
     return pack;
 }
@@ -1579,15 +1635,16 @@ void ScriptHost::makeRuntimes(Pack& pack) {
     pack.controlRt = JS_NewRuntime();
     pack.control = JS_NewContext(pack.controlRt);
     const JSMallocFunctions counted{&Glue::jsCalloc, &Glue::jsMalloc, &Glue::jsFree, &Glue::jsRealloc, &Glue::jsUsable};
-    pack.heap.counters = &counters_;
     pack.audioRt = JS_NewRuntime2(&counted, &pack.heap);  // QuickJS copies the functions and keeps the opaque
     pack.audio = JS_NewContext(pack.audioRt);
     JS_SetContextOpaque(pack.audio, &pack);  // what a Block method runs in: Glue::packOf
-    // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the block
-    // thread runs held to the call deadline, whose state every pack shares: the handler's opaque is the host.
+    // Both runtimes' JS stacks, inside the smallest thread that enters either (the class comment), and what the control
+    // thread runs in either held to its deadline: the audio runtime's handler is the control thread's until join gives
+    // it the block thread's.
     JS_SetMaxStackSize(pack.controlRt, kStackBytes);
     JS_SetMaxStackSize(pack.audioRt, kStackBytes);
-    JS_SetInterruptHandler(pack.audioRt, &Glue::interrupt, this);
+    JS_SetInterruptHandler(pack.controlRt, &Glue::controlInterrupt, this);
+    JS_SetInterruptHandler(pack.audioRt, &Glue::controlInterrupt, this);
 
     // Object.prototype, which a node's methods are never read from (Glue::method): by address, as the context holds it
     // for its life and no script can replace it.
@@ -1647,7 +1704,75 @@ bool ScriptHost::load(std::span<const Chunk> chunks, std::string* error, std::si
     loaded_ = true;
     Pack& pack = *packs_.front();
     pack.heap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;  // before the load, whose allocations it holds
-    return loadInto(pack, chunks, error);
+    const bool loaded = loadInto(pack, chunks, error);
+    join(pack);  // counted in the shared counters from the start: no audio thread runs before the host is loaded
+    return loaded;
+}
+
+bool ScriptHost::canLoadPack(const std::string& id, std::string* error) const {
+    const auto refuse = [&](std::string why) {
+        if (error) *error = std::move(why);
+        return false;
+    };
+    if (!graph::isPlainId(id))
+        return refuse("pack id " + graph::jsonString(id) +
+                      " must be non-empty, with no whitespace or control characters");
+    if (id == kBuiltInPack) return refuse(std::string(kBuiltInPack) + " is the built-in pack, which is not reloadable");
+    if (id == "rp" || id.rfind("rp.", 0) == 0)
+        return refuse("pack id " + graph::jsonString(id) +
+                      " is the app's to give: \"rp\" and every id under \"rp.\" name its own packages (its built-in "
+                      "script pack, its native ones)");
+    for (const auto& pack : packs_)
+        if (pack->id == id) return refuse("pack " + graph::jsonString(id) + " is already loaded");
+    return true;
+}
+
+const rp_package* ScriptHost::loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
+                                       std::string* error) {
+    if (!canLoadPack(id, error)) return nullptr;
+    // What its audio runtime allocates until it joins, the load's own, counted apart: this is the control thread, and
+    // the block thread may be counting into the shared counters, which have one writer at a time.
+    Counters loading;
+    auto pack = makePack(id, loading);
+    pack->heap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;  // before the load, whose allocations it holds
+    if (!loadInto(*pack, chunks, error)) {
+        freeRuntimes(*pack);  // counted in `loading` still
+        return nullptr;
+    }
+    // What the load left in cycles goes now, while the pack is still this thread's alone: once a plan places one of
+    // its nodes, its audio runtime is the block thread's, and no block collects.
+    JS_UpdateStackTop(pack->audioRt);
+    JS_RunGC(pack->audioRt);
+    pack->garbage = false;
+    join(*pack);
+    packs_.push_back(std::move(pack));
+    return &packs_.back()->package;
+}
+
+void ScriptHost::discardPack(const rp_package* package) {
+    const auto at =
+        std::find_if(packs_.begin() + 1, packs_.end(), [&](const auto& p) { return &p->package == package; });
+    assert(at != packs_.end() && "discardPack takes a pack loadPack returned, never the built-in one");
+    if (at == packs_.end()) return;
+    freeRuntimes(**at);
+    packs_.erase(at);
+}
+
+std::vector<ScriptHost::PackInfo> ScriptHost::packs() const {
+    std::vector<PackInfo> out;
+    for (const auto& pack : packs_) out.push_back({pack->id, pack->generation});
+    return out;
+}
+
+// The pack's audio runtime joins what every pack shares: what the block thread runs in it held to the call deadline,
+// whose state every pack shares (the handler's opaque is the host), and its allocations counted with every other
+// pack's (and, in a profile build, its auto-GC pinned off with theirs). Not before its load is done, which runs on the
+// control thread while an audio thread may be making a call into another pack: a handler polled in the load would read
+// and write that call's deadline state, and its allocations would be a second writer of the counters.
+void ScriptHost::join(Pack& pack) {
+    JS_SetInterruptHandler(pack.audioRt, &Glue::interrupt, this);  // in place of the control thread's
+    pack.heap.counters = &counters_;
+    if (gcPinned_) JS_SetGCThreshold(pack.audioRt, static_cast<std::size_t>(-1));
 }
 
 bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error) {
@@ -1656,8 +1781,16 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         Pack* pack;
         ~Forget() { pack->heap.refused = false; }
     } forget{&pack};
+    // The whole load is one call, held to the control thread's deadline: a top level, a job or a definition that runs
+    // past it refuses the pack, saying so.
+    const Glue::ControlDeadline call(*this);
     auto fail = [&](std::string why) {
         freeTypes(pack);
+        // What a read that failed left pending (the deadline's error among them) goes with the load: the built-in pack
+        // keeps its runtimes, and nothing is to find it there.
+        clearException(pack.control);
+        clearException(pack.audio);
+        if (controlInterrupted_) why = Glue::stopped(*this, "its load") + ": " + why;
         if (error) *error = std::move(why);
         return false;
     };
@@ -1674,10 +1807,14 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         }
         // What the bundle's top level queued runs now, in this context's runtime: no block has begun.
         runJobs(audioSide ? pack.audioRt : pack.controlRt, kJobsPerCheckpoint, [](JSContext* c) { clearException(c); });
+        if (controlInterrupted_)
+            return fail(std::string("a promise job its top level queued in the ") + (audioSide ? "audio" : "control") +
+                        " context");
     }
     // What the load leaves in cycles in the audio runtime, its top level's and the descriptors' below, goes at the first
-    // quiescent reclaim, rather than waiting for a block's threshold GC. (The production bundle left about 144 KiB while
-    // zod and the DSP kernel were in it; it has left none since B3b.)
+    // quiescent reclaim (the built-in pack's) or before loadPack returns (a loaded pack's), rather than waiting for a
+    // block's threshold GC. (The production bundle left about 144 KiB while zod and the DSP kernel were in it; it has
+    // left none since B3b.)
     pack.garbage = true;
     // The definitions, in the same order in both contexts (the same bundle ran in each).
     JSValue lists[2];
@@ -1717,6 +1854,14 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         }
         JS_FreeValue(control, id);
         if (type.id.empty()) return fail(at + " has no id");
+        // A loaded pack's types are named under its id (the built-in pack's predate the rule: rp.midi.route, ...), so
+        // its namespace meets another's only where one pack's id runs on into another's ("a" beside "a.b", or "rp.midi"
+        // beside the built-in rp.midi.route), and there the registry refuses the type a second pack would take.
+        if (pack.id != kBuiltInPack &&
+            (type.id.size() <= pack.id.size() + 1 || type.id.compare(0, pack.id.size(), pack.id) != 0 ||
+             type.id[pack.id.size()] != '.'))
+            return fail(at + " (" + type.id + ") is outside pack " + graph::jsonString(pack.id) +
+                        ": its type ids are " + graph::jsonString(pack.id + ".<name>"));
         JSValue audioId = JS_GetPropertyStr(audio, type.audio, "id");
         const char* audioText = JS_IsString(audioId) ? JS_ToCString(audio, audioId) : nullptr;
         const bool same = audioText && type.id == audioText;
@@ -1744,8 +1889,11 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         }
         type.abi = rp_node_type{type.id.c_str(), &Glue::descriptor, &Glue::layoutFor, &Glue::queryMemory,
                                 &Glue::create, &Glue::destroy, RP_TYPE_STATELESS_PREPARE, &type};
+        // A read the deadline stopped that left no exception behind (a getter on a flag): refused all the same.
+        if (controlInterrupted_) return fail(at + " (" + type.id + ")");
     }
     runJobs(pack.audioRt, kJobsPerCheckpoint, [](JSContext* c) { clearException(c); });  // what the descriptors queued there
+    if (controlInterrupted_) return fail("a promise job its descriptors queued");
     // Each type's span label, in registration order (spanLabelFor).
     for (const auto& t : pack.types) t->spanLabel = spanLabelFor(t->id);
     pack.typeList.clear();
@@ -1959,8 +2107,10 @@ bool ScriptHost::resetAllocStats(bool disableAutoGc) {
     maxBlockAllocs_.store(0, std::memory_order_relaxed);
     maxBlockBytes_.store(0, std::memory_order_relaxed);
     // QuickJS checks its threshold only as it makes an object, so SIZE_MAX means a collection never starts by itself.
-    if (disableAutoGc)
+    if (disableAutoGc) {
+        gcPinned_ = true;  // a pack loaded later joins pinned
         for (const auto& pack : packs_) JS_SetGCThreshold(pack->audioRt, static_cast<std::size_t>(-1));
+    }
     return true;
 }
 
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index f27be898..77fd8652 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -95,16 +95,20 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // same C ABI as a native node. It holds them in packs (docs/modular-graph-phase-c1-design.md section 4), the unit it
 // isolates and loads: a pack has an id, a bundle that registers its node definitions (defineNode results, appended to
 // the global __rp_scriptNodes: packages/retroplug/src/script/register.ts), and a package of its own, named by its id,
-// in which the host registers an rp_node_type for each definition, whose instances are native shims. The host holds
-// one pack, the built-in one, "rp.script", which load fills with a bundle (an Engine's is embeddedBundle()).
+// in which the host registers an rp_node_type for each definition, whose instances are native shims. The first pack is
+// the built-in one, "rp.script", which load fills with a bundle (an Engine's is embeddedBundle()); loadPack loads
+// more beside it, each under an id of its own that names its types: every type id a loaded pack registers is the
+// pack's id, a dot and more, so no pack takes another's type ids (where two namespaces meet, one id running on into
+// another's, "a" and "a.b", the registry refuses the type the second would take).
 //
 // A pack is two bare QuickJS runtimes of its own, each loaded with its bundle, sharing no JS value with each other or
 // with another pack's:
 //   - the CONTROL side, entered only by the control thread, answers layout_for and prepare_settings: the
 //     definition's own parseSettings checks a settings object, and its layout gives the ports, from the settings
 //     alone. It never holds an instance.
-//   - the AUDIO side holds every instance of the pack's types and is entered only by the thread that runs blocks (the
-//     audio thread, or the control thread while setAudioThreadOwns(false)), which reaches it only through an instance.
+//   - the AUDIO side holds every instance of the pack's types and, once the pack is loaded (below), is entered only by
+//     the thread that runs blocks (the audio thread, or the control thread while setAudioThreadOwns(false)), which
+//     reaches it only through an instance.
 //     A shim's create and activate run on the control thread and only allocate the shim; the JS instance is created on
 //     the block thread at the first call the shim gets there, which is the ports_changed the planner sends every fresh
 //     instance when its plan is adopted, so before that plan's first process. A shim's destroy (control thread) queues
@@ -113,11 +117,19 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 //     thread to be deleted (reclaim), so the audio thread frees nothing native. What a released instance leaves in
 //     cycles is collected by a GC of its pack's audio runtime at two points no block is running: the reclaim that ends
 //     a quiescent operation which released some (the Engine's after every setGraph: one GC a pack however many went),
-//     and the audio thread handing the runtimes back (setAudioThreadOwns(false)). The first reclaim after a pack's load
-//     (for the built-in one, the top of the first setGraph) takes what its bundle's load left in cycles too, whether or
-//     not an instance released anything. Never by a block, so while an audio thread owns the runtimes (a live session)
-//     those cycles wait for QuickJS's own threshold GC, which runs inside whichever block's JS allocates past it in
-//     that runtime, or for the audio thread to hand the runtimes back.
+//     and the audio thread handing the runtimes back (setAudioThreadOwns(false)). The first reclaim after the built-in
+//     pack's load (an Engine's first setGraph, at its top) takes what its bundle's load left in cycles too, whether or
+//     not an instance released anything; a loaded pack's load collects its own before loadPack returns. Never by a
+//     block, so while an audio thread owns the runtimes (a live session) those cycles wait for QuickJS's own threshold
+//     GC, which runs inside whichever block's JS allocates past it in that runtime, or for the audio thread to hand the
+//     runtimes back.
+// A pack is made and loaded on the control thread, its audio side included, so loadPack runs while an audio thread
+// runs blocks in the other packs: the block thread first reaches a pack through an instance of one of its types, in a
+// plan published after its load, and the plan swap's ring (release on push, acquire on adopt) carries the whole built
+// pack with it. Until its load is done, a pack's audio runtime touches nothing the block thread writes: it joins what
+// every pack shares (the call deadline's interrupt handler, the allocation counters) as its load ends (join). So what
+// a loaded pack's load allocates is left out of the allocation counters, which count the audio side's blocks,
+// adoptions and releases; the built-in pack's, at the host's construction, comes before any and is in them.
 // What every pack shares stays the host's: the call deadline (one call runs at a time on the block thread), the
 // allocation counters (summed over every pack's audio runtime, so audioAllocations and the profile's allocation window
 // mean the whole audio side), the release rings, the throw count and the first throw, and the profile's span labels.
@@ -138,6 +150,13 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // not. A call that runs to its end, its jobs with it, starts the count again. A deadline fault is a fault like any
 // other: it latches on the instance and outlives reshapes, and only a rebuild (or the node removed and added again)
 // clears it.
+// The control thread's calls into a pack are held to the same length of its own CPU time, on a clock of its own (the
+// block thread may be inside a call into another pack meanwhile, so the two share no deadline state): a load, one call
+// (its chunks in both runtimes, the jobs they queue, its definitions read), and a settings check (parseSettings and
+// layout, for layout_for, prepare_settings and create, and the jobs they queue). A pack's control runtime has the
+// control thread's handler for its life, its audio runtime until join gives it the block thread's. A load the deadline
+// stops refuses the pack; a settings check it stops refuses the settings, saying so, so the planner refuses the
+// document and the running plan stands.
 //
 // Every type declares RP_TYPE_STATELESS_PREPARE: its prepare_settings reads the settings it is handed, the
 // definition and what the instance was created with, never anything a commit writes. A type that reshapes answers
@@ -234,21 +253,46 @@ public:
     // Load the built-in pack: run a bundle (global-code bytecode, in chunks run in order) in both of its contexts, then
     // read the definitions it registered, its audio runtime held to `memoryLimit` bytes (0: kDefaultMemoryLimit) from
     // then on, the load included. Once, before anything plans with the package: a second call is refused, and leaves
-    // what the first registered as it is. False, with a reason, when a chunk fails to load (or runs out of memory) or a
-    // definition cannot be read or is not the same in both contexts (its id or its descriptor differs: a bundle that
-    // branches on which context it runs in). One bad definition refuses the whole package: it then has no types, so
-    // every document naming any of the bundle's is refused (unknown-type).
+    // what the first registered as it is. False, with a reason, when a chunk fails to load (or runs out of memory), the
+    // load runs past the call deadline, or a definition cannot be read or is not the same in both contexts (its id or its
+    // descriptor differs: a bundle that branches on which context it runs in). One bad definition refuses the whole
+    // package: it then has no types, so every document naming any of the bundle's is refused (unknown-type).
     bool load(std::span<const Chunk> chunks, std::string* error = nullptr, std::size_t memoryLimit = kDefaultMemoryLimit);
     // What a pack's audio runtime may hold, in usable bytes, unless its load says otherwise (the class comment).
     static constexpr std::size_t kDefaultMemoryLimit = std::size_t{64} << 20;
     // The built-in pack's script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a
     // load).
     const rp_package* package() const;
+    // Load a pack beside the built-in one (docs/modular-graph-phase-c1-design.md sections 4 and 5): a new pack named
+    // `id`, its two runtimes made for it, runs a bundle (global-code bytecode, in chunks run in order) in both, its
+    // audio runtime held to `memoryLimit` bytes (0: kDefaultMemoryLimit), the load included, and its definitions are
+    // read and checked as load checks the built-in pack's. Callable while an audio thread runs blocks (the class
+    // comment). Its package, named by `id`, for NodeRegistry::addPackage, valid for the host's life unless discarded;
+    // null, with a reason, when canLoadPack refuses the id (nothing is built), or, the pack freed there and then, when
+    // a chunk fails to load (or runs out of memory), the load runs past the call deadline, a definition cannot be read
+    // or is not the same in both contexts, or a type id is not the pack's id, a dot and more ("user.synth.osc" in
+    // "user.synth").
+    const rp_package* loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
+                               std::string* error = nullptr);
+    // Whether loadPack takes a pack named `id`: a plain id (graph::isPlainId), not the built-in pack's ("rp.script",
+    // which is not reloadable), not otherwise the app's ("rp" and every id under "rp.", where its native packages are
+    // named too), and none loaded already. False, with the reason.
+    bool canLoadPack(const std::string& id, std::string* error = nullptr) const;
+    // Free a pack loadPack returned whose package no registry took (a NodeRegistry refused it), so nothing has made an
+    // instance of its types: its runtimes, there and then. Never the built-in pack.
+    void discardPack(const rp_package* package);
+    // A pack loaded: its id and its generation (its load's count).
+    struct PackInfo {
+        std::string   id;
+        std::uint32_t generation = 0;
+    };
+    // Every pack loaded, the built-in one first, then in load order.
+    std::vector<PackInfo> packs() const;
     // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
     // queued instance there and then, and collects what they left.
     void setAudioThreadOwns(bool owns);
-    // How much of the calling thread's CPU time one call into an audio runtime may use before it is stopped
-    // (kDefaultCallDeadline until set). Set it before an audio thread runs blocks.
+    // How much of the calling thread's CPU time one call into a pack's JS may use before it is stopped, on either side
+    // (kDefaultCallDeadline until set: the class comment). Set it before an audio thread runs blocks.
     void setCallDeadline(std::chrono::milliseconds limit);
     static constexpr std::chrono::milliseconds kDefaultCallDeadline{2000};
     // Delete the records the block thread has released, and log the first throw once. While quiescent, also collect
@@ -291,8 +335,9 @@ public:
 
     // ---- profiling (spec/08-profiling.md): real only in a RETROPLUG_PROFILE build --------------------------------
     // The allocation window, opened by resetAllocStats, which can pin every pack's audio runtime's auto-GC off from
-    // then on (so a window collects nothing); runGc runs and times one collection of each. allocStats never refuses (a
-    // benchmark reads its `enabled` to know a profile host), and read on another thread while blocks run lags them.
+    // then on, a pack loaded later's too (so a window collects nothing); runGc runs and times one collection of each.
+    // allocStats never refuses (a benchmark reads its `enabled` to know a profile host), and read on another thread
+    // while blocks run lags them.
     DspAllocStats allocStats() const;
     // These refuse, doing nothing, while an audio thread owns the runtimes (setAudioThreadOwns): resetAllocStats and
     // traceReset return false and runGc a result with enabled false (the first refusal said once on stderr), and the
@@ -329,12 +374,17 @@ private:
         std::atomic<std::uint64_t> peakBytes{0};
     };
 
-    // control thread: a pack named `id` with its two runtimes made (makeRuntimes), loaded with nothing yet
-    std::unique_ptr<Pack> makePack(const std::string& id);
+    // control thread: a pack named `id` with its two runtimes made (makeRuntimes), loaded with nothing yet, its audio
+    // runtime's allocations counted in `counters` until it joins
+    std::unique_ptr<Pack> makePack(const std::string& id, Counters& counters);
     void makeRuntimes(Pack& pack);  // control thread: its runtimes and contexts, set up as the class comment says
     void freeRuntimes(Pack& pack);  // control thread or quiescent: its types first (freeTypes), then its runtimes
-    // control thread: run a bundle in both of the pack's contexts, read and check its definitions, and label its types
+    // control thread: run a bundle in both of the pack's contexts, read and check its definitions (a loaded pack's
+    // type ids in its namespace), and label its types
     bool loadInto(Pack& pack, std::span<const Chunk> chunks, std::string* error);
+    // control thread, its load done and before any plan can place its types: the pack's audio runtime joins what every
+    // pack shares (the class comment)
+    void join(Pack& pack);
     void freeTypes(Pack& pack);
     std::uint32_t spanLabelFor(const std::string& typeId);  // control thread: a type id's label (DspSpanLabel)
     void releaseJs(Instance& in);   // the block thread, or quiescent
@@ -350,10 +400,12 @@ private:
     Binder                       binder_ = nullptr;
     void*                        binderUser_ = nullptr;
     // Every pack, the built-in one first. Made, loaded and read on the control thread (and quiescent, by releaseQueued,
-    // collect and the profile calls); the block thread reaches a pack only through an instance of one of its types,
-    // never through this list.
+    // collect and the profile calls), so it grows (loadPack) and shrinks (discardPack) there, while an audio thread
+    // runs blocks; the block thread reaches a pack only through an instance of one of its types, never through this
+    // list.
     std::vector<std::unique_ptr<Pack>> packs_;
     bool                               loaded_ = false;  // load has been called
+    bool                               gcPinned_ = false;  // resetAllocStats pinned the audio runtimes' auto-GC off
     // Each type id's span label (DspSpanLabel), given at its first registration in any pack and kept, and the next one
     // to give (control thread).
     std::unordered_map<std::string, std::uint32_t> spanLabels_;
@@ -377,6 +429,11 @@ private:
     bool                       armed_ = false;
     std::int64_t               cpuStart_ = -1;
     bool                       interrupted_ = false;
+    // The control thread's own: its calls into a pack's JS (a load, a settings check) run while the block thread may be
+    // inside one into another pack, so the two clocks share nothing but the length.
+    bool                       controlArmed_ = false;
+    std::int64_t               controlCpuStart_ = -1;
+    bool                       controlInterrupted_ = false;
 
     std::atomic<std::uint32_t> throws_{0};
     std::atomic<std::uint32_t> live_{0};
````

- [ ] **Step 5: Load a pack through the Engine**

`Engine::loadScriptPack` checks the id, compiles, loads, and adds the pack's package to the registry (a pack the
registry refuses is discarded), and answers each type's id and canonical descriptor. The registry's threading note
says what makes a load between two `setGraph` calls safe: the registry is read only when planning, on the control
thread, and nothing on the audio thread looks a type up:

````diff
diff --git a/packages/native/src/graph/NodeRegistry.hpp b/packages/native/src/graph/NodeRegistry.hpp
index 2311a170..f13488ca 100644
--- a/packages/native/src/graph/NodeRegistry.hpp
+++ b/packages/native/src/graph/NodeRegistry.hpp
@@ -15,8 +15,10 @@ namespace rp::graph {
 // trust boundary for a pack's metadata: everything the host will later call or read is checked here.
 //
 // Entries are never moved or removed, so an Entry* stays valid for the registry's life (a planned instance
-// keeps one). Not thread-safe: addPackage must not overlap any other call, so a registry shared by several
-// hosts is filled before any of them plans; after that, concurrent find() is safe.
+// keeps one), whatever is added after. Not thread-safe: addPackage must not overlap any other call, so a registry
+// shared by several hosts is filled before any of them plans; after that, concurrent find() is safe. One host may add
+// packages between its plans on the thread that plans (an Engine's script node packs: Engine::loadScriptPack), since
+// planning is the only reader: an executor runs a plan's instances and never looks a type up.
 class NodeRegistry {
 public:
     struct Entry {
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index de00804e..83c97737 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -19,10 +19,12 @@
 #include "native/core/img/png/lodepng.h"
 
 #include "graph/CanonicalJson.hpp"
+#include "graph/Descriptor.hpp"
 #include "graph/GraphHost.hpp"
 #include "graph/HostNodes.hpp"
 #include "graph/Queue.hpp"
 #include "graph/nodes/CoreNodes.hpp"
+#include "host/dsp/ScriptCompiler.hpp"
 #include "host/engine/DspCommand.hpp"
 #include "host/graph/HostIoNodes.hpp"
 #include "host/graph/HostServices.hpp"
@@ -103,8 +105,13 @@ std::span<const rp::host::ScriptHost::Chunk> embeddedScripts() {
 // before the core handoff and the engine services (the host I/O, the sync trace) its nodes call into, and before the
 // script host its script nodes release their instances through.
 struct Engine::Graph {
-    // First, so it goes last: the script nodes the host destroys hand their instances back to it.
+    // First, so it goes last: the script nodes the host destroys hand their instances back to it, and its packs' types
+    // are the registry's script entries.
     rp::host::ScriptHost                   script;
+    // Filled at construction and by loadScriptPack, on the control thread, which is where every plan is made too
+    // (setGraph, and reactivate, quiescent), so an addPackage never overlaps a find (NodeRegistry's threading rule).
+    // Nothing on the audio thread reads it: a plan's instances carry their entries, which only the control thread reads
+    // (to destroy a node, as it frees the plan that held it), and an entry never moves.
     rp::graph::NodeRegistry                registry;
     rp::graph::ResourceTable               resources;
     rp::host::HostCores                    cores{};
@@ -121,7 +128,7 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
       pendingMidi_(rp::host::kHostMidiInMessages, rp::host::kHostMidiInBytes),
       pendingControllerMidi_(rp::host::kControllerInMessages, rp::host::kControllerInBytes) {
     graph_ = std::make_unique<Graph>();
-    // The script node types first: the bundle runs in both of the script host's contexts, and its definitions become
+    // The script node types first: the bundle runs in both of the built-in pack's contexts, and its definitions become
     // one more package. One that will not load registers no type, so a document naming one is refused (unknown-type).
     std::string why;
     if (!graph_->script.load(scriptBundle, &why))
@@ -386,6 +393,43 @@ std::uint32_t Engine::droppedMessages() const { return graph_->host->droppedMess
 
 std::uint32_t Engine::scriptThrows() const { return graph_->script.throwCount(); }
 
+Engine::ScriptPackResult Engine::loadScriptPack(const std::string& id, const std::string& source,
+                                                std::size_t memoryLimit) {
+    ScriptPackResult out;
+    rp::host::ScriptHost& script = graph_->script;
+    // The id first: a refused one is refused whatever its source says, and costs no compile.
+    if (!script.canLoadPack(id, &out.error)) return out;
+    std::string why;
+    const auto bytecode = dsp::compileToBytecode(source, &why);
+    if (!bytecode) {
+        out.error = "the source does not compile: " + why;
+        return out;
+    }
+    const rp::host::ScriptHost::Chunk chunk{*bytecode};
+    const rp_package* package = script.loadPack(id, {&chunk, 1}, memoryLimit, &out.error);
+    if (!package) return out;
+    // Checked here as every package is: a descriptor the planner could not use, or a package or type id the registry
+    // holds already (a pack named as a native package, or one whose namespace holds another's: "a" and "a.b" can both
+    // name a type "a.b.c"). Refused, the pack goes: nothing placed it.
+    if (!graph_->registry.addPackage(package, &why)) {
+        script.discardPack(package);
+        out.error = "the registry refused the pack's types: " + why;
+        return out;
+    }
+    for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
+        const auto* entry = graph_->registry.find(package->node_types[k]->id);
+        out.types.push_back({entry->descriptor.id, rp::graph::descriptorJson(entry->descriptor)});
+    }
+    out.ok = true;
+    return out;
+}
+
+void Engine::setScriptCallDeadline(std::chrono::milliseconds limit) { graph_->script.setCallDeadline(limit); }
+
+std::vector<rp::host::ScriptHost::PackInfo> Engine::scriptPacks() const { return graph_->script.packs(); }
+
+std::size_t Engine::nodeTypeCount() const { return graph_->registry.size(); }
+
 rp::host::DspAllocStats Engine::dspAllocStats() const { return graph_->script.allocStats(); }
 bool Engine::resetDspAllocStats(bool disableAutoGc) { return graph_->script.resetAllocStats(disableAutoGc); }
 rp::host::DspGcResult Engine::dspRunGc() { return graph_->script.runGc(); }
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 8665654f..7025bf30 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -1,6 +1,7 @@
 #pragma once
 
 #include <atomic>
+#include <chrono>
 #include <cstdint>
 #include <functional>
 #include <map>
@@ -60,7 +61,8 @@ struct EngineFrame {
 
 // The core of the host: the graph the systems run on (docs/modular-graph-phase-b1-design.md), the script host its TS
 // nodes run in, the MIDI routing, the controller and each role's protocol node among them
-// (docs/modular-graph-phase-b3b-design.md section 2), the staged MIDI and the snapshot registry. Structure reaches it
+// (docs/modular-graph-phase-b3b-design.md section 2), and the types of any script node pack loaded beside them
+// (docs/modular-graph-phase-c1-design.md), the staged MIDI and the snapshot registry. Structure reaches it
 // as graph documents TS builds (docs/modular-graph-phase-b2-design.md), planned on the control thread; every other
 // edit is a command on the GraphHost's one ring, applied at the next drain. A removed core is freed on the control
 // thread, with the plan that last ran it. Transport and ppq are plain members, set per block by whoever drives it; its
@@ -245,6 +247,36 @@ public:
     // Throws caught in the script nodes so far (ScriptHost::throwCount): any thread, as faultCount.
     std::uint32_t scriptThrows() const;
 
+    // --- script node packs (docs/modular-graph-phase-c1-design.md section 7), control thread ---
+    struct ScriptPackType {
+        std::string id;
+        std::string descriptor;  // as the registry holds it: canonical JSON, every field spelled
+    };
+    struct ScriptPackResult {
+        bool                        ok = false;
+        std::string                 error;   // why not, when not
+        std::vector<GraphError>     errors;  // the planner's, when a replan refused (a load makes no plan: none)
+        std::vector<ScriptPackType> types;   // the pack's, in registration order
+    };
+    // Load a pack of script node types named `id`: `source` is global code (the IIFE a bundler makes, appending its
+    // defineNode results to __rp_scriptNodes as the built-in bundle does: packages/retroplug/src/script/register.ts),
+    // compiled here and run in runtimes of the pack's own (ScriptHost::loadPack), its audio runtime held to
+    // `memoryLimit` bytes (0: ScriptHost::kDefaultMemoryLimit); then its types join the registry, so a document can
+    // place them from the next setGraph on. Callable while an audio thread runs blocks: the pack is built here, and the
+    // block thread meets it only in a plan that places one of its nodes. Refused, changing nothing, with the reason in
+    // `error`: an id ScriptHost::canLoadPack refuses (checked first, before the source is compiled: one of the app's,
+    // "rp" and under, among them), a compile error (its line), a pack ScriptHost::loadPack refuses (a definition it
+    // cannot read, a type id outside the pack's namespace, a load past the call deadline), or one the registry refuses
+    // (a descriptor it cannot use, a type id another pack holds already).
+    ScriptPackResult loadScriptPack(const std::string& id, const std::string& source, std::size_t memoryLimit = 0);
+    // How much of the calling thread's CPU time one call into a script pack's JS may use, on either side
+    // (ScriptHost::setCallDeadline): set it before an audio thread runs blocks.
+    void setScriptCallDeadline(std::chrono::milliseconds limit);
+    // The script host's packs, the built-in one first (ScriptHost::packs).
+    std::vector<rp::host::ScriptHost::PackInfo> scriptPacks() const;
+    // The node types a document can name: the registry's, native and script.
+    std::size_t nodeTypeCount() const;
+
 private:
     struct Graph;
     // Host MIDI staged for the next block: each message's frame and its place in one flat arena of bytes. Both keep
````

- [ ] **Step 6: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[packs]"`
Expected: `All tests passed (318 assertions in 10 test cases)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (18409 assertions in 135 test cases)` (ten cases more, the new file's).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `pnpm typecheck && tools/run-profile.sh unit`
Expected: `packages/retroplug typecheck: Done`, then `All tests passed (132 assertions in 6 test cases)`.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[script]"`
Expected: `All tests passed (1201 assertions in 61 test cases)`, no ThreadSanitizer report.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[packs]"`
Expected: `All tests passed (318 assertions in 10 test cases)`, no ThreadSanitizer report (the prototype ran it three times).

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/test/engine-graph/ScriptPacks.test.cpp packages/native/CMakeLists.txt \
  packages/native/cmake/tests.cmake packages/native/src/graph/NodeRegistry.hpp \
  packages/native/src/host/dsp/ScriptCompiler.cpp packages/native/src/host/dsp/ScriptCompiler.hpp \
  packages/native/src/host/engine/Engine.cpp packages/native/src/host/engine/Engine.hpp \
  packages/native/src/host/script/ScriptHost.cpp packages/native/src/host/script/ScriptHost.hpp
git commit -F - <<'EOF'
feat(engine): Engine::loadScriptPack loads a pack of script node types into runtimes of its own while the engine runs

ScriptHost::loadPack builds a pack beside the built-in one on the control thread: its id checked first (a plain id,
not rp.script, not "rp" or under "rp.", which are the app's, not loaded already), its bundle run in two runtimes made
for it, its definitions read as the built-in pack's are, every type id its id, a dot and a name, its memory limit set
before the load, and what the load left in cycles collected there and then. A refused pack is freed at once. Its audio
runtime joins what every pack shares (the block thread's call deadline handler, the allocation counters) only once
loaded, so a load beside a running audio thread neither touches the deadline state of a call the block thread is
making nor writes the shared counters. A pack's manifest spells its id as JSON does. discardPack frees a pack whose
package a registry refused; packs() lists the packs.

The control thread's calls into a pack are held to the call deadline on a clock of its own: a load (its chunks in both
runtimes, the jobs they queue, its definitions read) and a settings check (parseSettings and layout, and their jobs).
A stopped load refuses the pack; a stopped check refuses the settings, so the planner refuses the document and the
running plan stands. The built-in pack's control side has it too.

dsp::compileToBytecode gives a compile error's text with the line QuickJS found it on, and holds the parser to the
script host's stack. Engine::loadScriptPack compiles a pack's source, loads it, adds its package to the registry
(discarding the pack when the registry refuses it) and returns its types with their canonical descriptors.

ScriptPacks.test.cpp: a pack's nodes beside the built-in ones in one document, isolation of a polluted realm, each
refusal changing nothing, a pack's memory limit and promise jobs faulting only its own node, a load and placement
under a running audio thread, the control-side deadline quiescent and under an audio thread (ThreadSanitizer catches
either side's handler reading the other's clock), and the binder, allocation counts and built-in pack's deadline on a
ScriptRig.
EOF
````

### Task 4: reload and freeing

Design section 5 ("Reload", "Freeing a retired generation"). Loading an id already loaded builds its next generation
beside the old; the registry swaps the pack's entries for the new ones (the old entries kept valid, since planned
instances point at them); the Engine replans the request it last published, without re-arming the tap, so every node of
the pack rebuilds on the new code under the same id while every other node keeps its instance. A refused replan undoes
the swap and returns the planner's errors, the old code running on. A retired generation is freed on the control thread
once the last of its instance records comes back from the block thread.

**Files:**
- Modify: `packages/native/cmake/tests.cmake`, `packages/native/src/graph/NodeRegistry.cpp`, `packages/native/src/graph/NodeRegistry.hpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/script/ScriptHost.cpp`, `packages/native/src/host/script/ScriptHost.hpp`, `packages/native/test/engine-graph/ScriptPacks.test.cpp`, `packages/native/test/graph/Registry.test.cpp`

- [ ] **Step 1: Write the failing tests**

`Registry.test.cpp` gains the swap (the new entry found, the old pointer still valid), the undo (the old entry back by
identity), two swaps in a row, the drop, and eight refusals that change nothing. `ScriptPacks.test.cpp` gains
`[packs][reload]` cases: a reload's nodes run the new code while another pack's node and a built-in one keep their
instances and their state; a type the reload adds places; a reload refused (the new settings check refusing the
document's settings, a placed type dropped) changes nothing; fifty reloads of a placed pack leave one generation and
the instance count steady; under an audio thread, an old generation retired until its records come back, then freed by
a reclaim; a reload's replan leaving the running tap as it is; and a `[profile]` case: a type's span label kept across
reloads, the trace's name table not growing, and each freed generation's share off the live heap. The refusal case
loses its "already loaded" check, which is now a reload:

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index f5b5f26e..f839fa6a 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -79,8 +79,9 @@ retroplug_add_test_binary(
 # each system node renders exactly what its core renders driven directly, its inputs included, the engine places,
 # re-arms and frees what the documents TS builds say, the host I/O nodes and the cart mirror move what the hosts stage
 # and drain, TS nodes run behind the C ABI in the script host, a script node pack loads beside the built-in one into
-# runtimes of its own, the protocol script nodes send what the DSP kernel they replaced was recorded sending, and every
-# document in buildGraph's goldens, its role goldens among them, plans on it.
+# runtimes of its own and reloads, the generation it replaced freed off the audio thread, the protocol script nodes
+# send what the DSP kernel they replaced was recorded sending, and every document in buildGraph's goldens, its role
+# goldens among them, plans on it.
 # A hidden bench times a script node per block.
 # Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
 # CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
diff --git a/packages/native/test/engine-graph/ScriptPacks.test.cpp b/packages/native/test/engine-graph/ScriptPacks.test.cpp
index 9766ca55..898643b8 100644
--- a/packages/native/test/engine-graph/ScriptPacks.test.cpp
+++ b/packages/native/test/engine-graph/ScriptPacks.test.cpp
@@ -3,8 +3,11 @@
 // getter on Object.prototype, Array.prototype.push replaced, a global) no node of another pack sees, its memory limit
 // and its promise jobs fault only its own nodes, and it loads while an audio thread runs blocks. Its id (never one of
 // the app's, "rp" and under), its types' namespace, its source and its definitions are checked, and a refusal changes
-// nothing. The control thread's calls into a pack, its load and its settings checks, are held to the call deadline on
-// a clock of their own. Driven on an Engine, whose built-in pack is the production bundle, the toy packs inline JS
+// nothing. Loaded again, it reloads: its placed nodes rebuild on the new code while every other node keeps its instance
+// and its state (the running plan's tap included), a reload whose replan is refused changes nothing, and the generation
+// a reload replaced is freed on the control thread once its records are back, within the call when no audio thread
+// runs. The control thread's calls into a pack, its load and its settings checks, are held to the call deadline on a
+// clock of their own. Driven on an Engine, whose built-in pack is the production bundle, the toy packs inline JS
 // source; and on a ScriptRig, where the host's binder, its allocation counts and the built-in pack's deadline are what
 // is checked.
 //
@@ -15,6 +18,8 @@
 #include <chrono>
 #include <compare>
 #include <cstdint>
+#include <cstdlib>
+#include <memory>
 #include <string>
 #include <thread>
 #include <utility>
@@ -23,10 +28,13 @@
 #include <catch2/catch_test_macros.hpp>
 
 #include "GraphDocs.hpp"
+#include "NodeRig.hpp"
 #include "ScriptRig.hpp"
 #include "graph/Descriptor.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/script/ScriptHost.hpp"
+#include "system/mesen/MesenNesConfig.hpp"
+#include "system/mesen/MesenNesSystem.hpp"
 
 using rp::host::ScriptHost;
 using rp::host::test::DocExtras;
@@ -272,8 +280,8 @@ void refused(Engine& e, const std::string& id, const std::string& source, const
 
 }  // namespace
 
-TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's, one of the app's, one loaded, a type "
-          "outside its namespace, a compile error, a definition that cannot be read, one the registry refuses",
+TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's, one of the app's, a type outside its "
+          "namespace, a compile error, a definition that cannot be read, one the registry refuses",
           "[script][packs][refused]") {
     Engine e(kRate);
     refused(e, "test pack", toys(transposer("test pack.up", 12)), "must be non-empty, with no whitespace", "test pack.up");
@@ -287,10 +295,6 @@ TEST_CASE("a pack refused changes nothing: a non-plain id, the built-in pack's,
                 app + ".up");
     }
 
-    load(e, "test.once", toys(transposer("test.once.first", 1)));
-    refused(e, "test.once", toys(transposer("test.once.second", 2)), "pack \"test.once\" is already loaded",
-            "test.once.second");
-
     // A type id is the pack's id, a dot and a name: not another prefix, the bare id, or the id and a dot.
     for (const std::string stray : {"test.other.stray", "test.nsx.stray", "test.ns", "test.ns."}) {
         INFO(stray);
@@ -343,14 +347,8 @@ toy.define("test.baddesc.odd", [{ id: "in", dir: "in", kind: "smoke" }], functio
     const auto quoted = load(e, "test\"q", toys(transposer("test\"q.up", 1)));
     REQUIRE(quoted.types.size() == 1);
     CHECK(quoted.types[0].id == "test\"q.up");
-    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.once#1", "test.nest#1", "test.ns#1",
-                                                 "test.syntax#1", "test.nodesc#1", "test.baddesc#1", "test.nest.x#1",
-                                                 "test\"q#1"});
-
-    // The pack that was loaded when a second load of its id was refused runs its own code.
-    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("once", "test.once.first"), midiOut("1")},
-                      {docWire("midi-in.midi", "once.in"), docWire("once.out", "midi-out.in1")}));
-    CHECK(block(e, {{2, kNote}}) == std::vector<Out>{{1, 2, {0x90, 61, 100}}});
+    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.nest#1", "test.ns#1", "test.syntax#1",
+                                                 "test.nodesc#1", "test.baddesc#1", "test.nest.x#1", "test\"q#1"});
     CHECK(e.scriptThrows() == 0);
 }
 
@@ -726,3 +724,364 @@ toy.define("test.bound.pass", [toy.input("in"), toy.output("out")], function ()
     CHECK(rig.scripts.audioAllocations().allocs > loaded.allocs);  // its node's create and first block, on the block thread
     CHECK(rig.scripts.throwCount() == 0);
 }
+
+namespace {
+
+// A node, `id`, that sends [tag, n] every block, n its block count: which code runs, and whether its instance, and so
+// its count, carried on.
+std::string tally(const std::string& id, int tag) {
+    return "toy.define('" + id + "', [toy.output('out')], function () {\n"
+           "  var n = 0;\n"
+           "  return { process: function (b) { b.send('out', 0, [" + std::to_string(tag) + ", ++n & 0x7f]); } };\n"
+           "});\n";
+}
+
+// The pack `id` at `generation` among `packs`, or null.
+const ScriptHost::PackInfo* findPack(const std::vector<ScriptHost::PackInfo>& packs, const std::string& id,
+                                     std::uint32_t generation) {
+    for (const auto& p : packs)
+        if (p.id == id && p.generation == generation) return &p;
+    return nullptr;
+}
+
+// Until `done()`, for at most ten seconds (the block thread is someone else); whether it came.
+template <class Done>
+bool waitUntil(Done&& done) {
+    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
+    while (!done() && std::chrono::steady_clock::now() < deadline)
+        std::this_thread::sleep_for(std::chrono::milliseconds(1));
+    return done();
+}
+
+}  // namespace
+
+TEST_CASE("a pack reloads while its nodes run: they rebuild on its new code, fresh, while another pack's node and a "
+          "built-in one keep their instances and their state, and the old generation is freed within the call",
+          "[script][packs][reload]") {
+    Engine e(kRate);
+    e.setControllerConnected(true);
+    load(e, "test.keep", toys(tally("test.keep.count", 1)));
+    load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2)));
+    // Beside them the built-in controller session, which takes the control surface (Programmer mode, a full paint) on
+    // the block it first sees one attached, and only then: a session made again takes it again.
+    const auto request = [](const std::string& session) {
+        return document({docNode("midi-in", "rp.host.midi-in"), docNode("shift", "test.code.shift"),
+                         docNode("keep", "test.keep.count"), docNode("count", "test.code.count"), midiOut("1,2,3"),
+                         docNode("surface-in", "rp.host.controller-in"), docNode(session, "rp.controller.session"),
+                         docNode("surface-out", "rp.host.controller-out")},
+                        {docWire("midi-in.midi", "shift.in"), docWire("shift.out", "midi-out.in1"),
+                         docWire("keep.out", "midi-out.in2"), docWire("count.out", "midi-out.in3"),
+                         docWire("surface-in.midi", session + ".controllerIn"),
+                         docWire("surface-in.connected", session + ".controllerConnected"),
+                         docWire(session + ".controllerOut", "surface-out.midi")});
+    };
+    place(e, request("session"));
+    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {1, 1}}, {3, 0, {2, 1}}}));
+    CHECK_FALSE(e.controllerOut().empty());
+    for (std::uint8_t n = 2; n <= 3; ++n) {
+        INFO("block " << int{n});
+        CHECK(block(e) == sorted({{2, 0, {1, n}}, {3, 0, {2, n}}}));
+        CHECK(e.controllerOut().empty());
+    }
+    const std::uint32_t instances = e.scriptInstances();
+    const std::size_t types = e.nodeTypeCount();
+
+    const auto reloaded = load(e, "test.code", toys(transposer("test.code.shift", -12) + tally("test.code.count", 3)));
+    CHECK(reloaded.error.empty());
+    CHECK(reloaded.errors.empty());
+    REQUIRE(reloaded.types.size() == 2);
+    CHECK(reloaded.types[0].id == "test.code.shift");
+    CHECK(reloaded.types[1].id == "test.code.count");
+    // No audio thread runs, so the replan released the old generation's nodes there and then, and the generation went
+    // within the call: its runtimes, and its entries in the registry.
+    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.keep#1", "test.code#2"});
+    CHECK(e.retiredNodeTypeCount() == 0);
+    CHECK(e.nodeTypeCount() == types);
+    CHECK(e.scriptInstances() == instances);
+    const auto packs = e.scriptPacks();
+    REQUIRE(packs.size() == 3);
+    CHECK_FALSE(packs[2].retired);
+    CHECK(packs[2].records == 2);
+    CHECK(packs[1].records == 1);
+    CHECK(packs[0].records == 1);  // the session
+
+    // The pack's nodes on the new code, counting from 1 again; the other pack's count carrying on, unbroken; the
+    // session not taking the surface again.
+    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 48, 100}}, {2, 0, {1, 4}}, {3, 0, {3, 1}}}));
+    CHECK(e.controllerOut().empty());
+    CHECK(block(e) == sorted({{2, 0, {1, 5}}, {3, 0, {3, 2}}}));
+    CHECK(e.controllerOut().empty());
+    // Where a session made again does take it: the same document with the session under another id.
+    place(e, request("session2"));
+    CHECK(block(e) == sorted({{2, 0, {1, 6}}, {3, 0, {3, 3}}}));
+    CHECK_FALSE(e.controllerOut().empty());
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.faultCount() == 0);
+}
+
+namespace {
+
+// A reload of `id` from `source` refused, changing nothing: `why` in its error and the planner's first error `code` at
+// `node` (no planner error when `code` is empty: refused before any replan), no types listed, and the packs, the
+// registry and the script instances as they were.
+void refusedReload(Engine& e, const std::string& id, const std::string& source, const std::string& why,
+                   const std::string& code = "", const std::string& node = "") {
+    const auto packsBefore = packIds(e);
+    const std::size_t typesBefore = e.nodeTypeCount();
+    const std::uint32_t instances = e.scriptInstances();
+    const auto r = e.loadScriptPack(id, source);
+    INFO(id << ": " << r.error);
+    CHECK_FALSE(r.ok);
+    CHECK(r.error.find(why) != std::string::npos);
+    CHECK(r.types.empty());
+    if (code.empty()) {
+        CHECK(r.errors.empty());
+    } else {
+        REQUIRE_FALSE(r.errors.empty());
+        CHECK(r.errors[0].code == code);
+        CHECK(r.errors[0].nodeId == node);
+        CHECK(r.error.find(r.errors[0].message) != std::string::npos);
+    }
+    CHECK(packIds(e) == packsBefore);
+    CHECK(e.nodeTypeCount() == typesBefore);
+    CHECK(e.retiredNodeTypeCount() == 0);
+    CHECK(e.scriptInstances() == instances);
+}
+
+}  // namespace
+
+TEST_CASE("a reload refused changes nothing, the old code running on with its state: the new settings check refusing "
+          "the document's settings, a type the document places gone, a type another pack holds, a source that does not "
+          "load; and a type a reload adds places",
+          "[script][packs][reload]") {
+    Engine e(kRate);
+    load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2)));
+    load(e, "test.code.sub", toys(tally("test.code.sub.x", 5)));
+    place(e, document({docNode("midi-in", "rp.host.midi-in"), docNode("shift", "test.code.shift"),
+                       docNode("count", "test.code.count"), midiOut("1,2")},
+                      {docWire("midi-in.midi", "shift.in"), docWire("shift.out", "midi-out.in1"),
+                       docWire("count.out", "midi-out.in2")}));
+    // The old code, its instances kept: up an octave, and [2, n] with n carrying on.
+    std::uint8_t n = 0;
+    const auto oldCodeRuns = [&] {
+        ++n;
+        CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {2, n}}}));
+    };
+    oldCodeRuns();
+
+    // Every type kept, but test.code.shift's settings check wants a gain, which the document's settings do not give.
+    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7) + tally("test.code.count", 3) + R"JS(
+globalThis.__rp_scriptNodes[0].parseSettings = function (raw) {
+  if (!raw || typeof raw.gain !== "number") throw new Error("test.code.shift takes a gain");
+  return raw;
+};
+)JS"),
+                  "test.code.shift takes a gain", "settings-invalid", "shift");
+    oldCodeRuns();
+    // test.code.count gone.
+    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7)),
+                  "no node type \"test.code.count\" is registered", "unknown-type", "count");
+    oldCodeRuns();
+    // A type in its namespace that the pack whose id runs on into it holds (test.code.sub's).
+    refusedReload(e, "test.code",
+                  toys(transposer("test.code.shift", 7) + tally("test.code.count", 3) + tally("test.code.sub.x", 6)),
+                  "the registry refused the pack's types: node type \"test.code.sub.x\" is already registered");
+    oldCodeRuns();
+    // Refused before it is a generation: a compile error, a top level that throws, a type outside its namespace.
+    refusedReload(e, "test.code", "var x = ;", "the source does not compile: line 1: SyntaxError");
+    refusedReload(e, "test.code", toys(transposer("test.code.shift", 7) + "throw new Error('not this one');"),
+                  "chunk 0: Error: not this one");
+    refusedReload(e, "test.code", toys(transposer("test.other.shift", 7)), "is outside pack \"test.code\"");
+    oldCodeRuns();
+    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.code#1", "test.code.sub#1"});
+
+    // One that adds a type: the placed nodes rebuild on it (the count from 1 again), and the new type places.
+    const auto added = load(e, "test.code", toys(transposer("test.code.shift", 12) + tally("test.code.count", 2) +
+                                                tally("test.code.extra", 9)));
+    REQUIRE(added.types.size() == 3);
+    CHECK(added.types[2].id == "test.code.extra");
+    CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.code.sub#1", "test.code#2"});
+    CHECK(block(e, {{3, kNote}}) == sorted({{1, 3, {0x90, 72, 100}}, {2, 0, {2, 1}}}));
+    place(e, document({docNode("count", "test.code.count"), docNode("extra", "test.code.extra"), midiOut("2,3")},
+                      {docWire("count.out", "midi-out.in1"), docWire("extra.out", "midi-out.in2")}));
+    CHECK(block(e) == sorted({{2, 0, {2, 2}}, {3, 0, {9, 1}}}));
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.faultCount() == 0);
+}
+
+TEST_CASE("fifty reloads of a placed pack, blocks run between them, leave one generation, the instance count steady "
+          "and the registry as it was",
+          "[script][packs][reload]") {
+    Engine e(kRate);
+    load(e, "test.loop", toys(tally("test.loop.count", 0)));
+    place(e, document({docNode("count", "test.loop.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
+    CHECK(block(e) == std::vector<Out>{{1, 0, {0, 1}}});
+    const std::uint32_t instances = e.scriptInstances();
+    const std::size_t types = e.nodeTypeCount();
+    for (int k = 1; k <= 50; ++k) {
+        INFO("reload " << k);
+        load(e, "test.loop", toys(tally("test.loop.count", k)));
+        const auto tag = static_cast<std::uint8_t>(k);
+        CHECK(block(e) == std::vector<Out>{{1, 0, {tag, 1}}});
+        CHECK(block(e) == std::vector<Out>{{1, 0, {tag, 2}}});
+        CHECK(packIds(e) == std::vector<std::string>{"rp.script#1", "test.loop#" + std::to_string(k + 1)});
+        CHECK(e.scriptInstances() == instances);
+        CHECK(e.nodeTypeCount() == types);
+        CHECK(e.retiredNodeTypeCount() == 0);
+    }
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.faultCount() == 0);
+}
+
+TEST_CASE("under an audio thread, a reload's old generation is retired until its records come back, then freed by a "
+          "reclaim on the control thread, and reloads in a row free every generation they replace",
+          "[script][packs][reload][threads]") {
+    Engine e(kRate);
+    load(e, "test.live", toys(tally("test.live.count", 0)));
+    place(e, document({docNode("count", "test.live.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
+    e.setAudioThreadOwns(true);
+    // The audio thread: blocks, each noting the tag of the code its node ran.
+    std::atomic<bool> running{true};
+    std::atomic<int>  lastTag{-1};
+    std::thread       audio([&] {
+        std::vector<float> l(kFrames), r(kFrames);
+        while (running.load()) {
+            e.drainCommands();
+            e.processBlock(kFrames, l.data(), r.data());
+            for (const auto& m : e.midiOut())
+                if (m.system == 1 && !m.data.empty()) lastTag.store(m.data[0]);
+            std::this_thread::sleep_for(std::chrono::microseconds(50));
+        }
+    });
+    const bool started = waitUntil([&] { return lastTag.load() == 0; });
+    // A reload, tried again while its replan finds the ring busy (which the audio thread drains).
+    const auto reload = [&](int tag) {
+        Engine::ScriptPackResult r;
+        for (int attempt = 0; attempt < 1000; ++attempt) {
+            r = e.loadScriptPack("test.live", toys(tally("test.live.count", tag)));
+            if (r.ok || r.errors.empty() || r.errors[0].code != "busy") break;
+            std::this_thread::sleep_for(std::chrono::milliseconds(1));
+            e.reclaim();
+        }
+        return r;
+    };
+    const auto first = reload(1);
+    // The plan holding the old generation's node is the audio thread's until it adopts the replan and hands the old
+    // one back, so the generation is retired, its record still out, and its entries still in the registry.
+    const auto held = e.scriptPacks();
+    const ScriptHost::PackInfo* old = findPack(held, "test.live", 1);
+    const std::size_t retiredTypes = e.retiredNodeTypeCount();
+    // A reclaim frees the plan handed back, which queues its node's release; a later block releases it; and the reclaim
+    // after that deletes the record, and frees the generation with it.
+    const auto onlyCurrent = [&] {
+        e.reclaim();
+        const auto packs = e.scriptPacks();
+        return packs.size() == 2 && !packs[1].retired && packs[1].records == 1 && e.retiredNodeTypeCount() == 0;
+    };
+    const bool freedFirst = waitUntil(onlyCurrent);
+    const bool ranFirst = waitUntil([&] { return lastTag.load() == 1; });
+    // Twenty in a row, a reclaim between each, so each frees what it can while the next is loaded and planned.
+    bool reloaded = true;
+    for (int k = 2; k <= 21; ++k) {
+        reloaded = reload(k).ok && reloaded;
+        e.reclaim();
+        std::this_thread::sleep_for(std::chrono::milliseconds(1));
+    }
+    const bool freedAll = waitUntil(onlyCurrent);
+    const bool ranLast = waitUntil([&] { return lastTag.load() == 21; });
+    running = false;
+    audio.join();
+    e.setAudioThreadOwns(false);
+    e.reclaim();
+    CHECK(started);
+    INFO(first.error);
+    REQUIRE(first.ok);
+    REQUIRE(old != nullptr);
+    CHECK(old->retired);
+    CHECK(old->records == 1);
+    CHECK(retiredTypes == 1);
+    CHECK(freedFirst);
+    CHECK(ranFirst);
+    CHECK(reloaded);
+    CHECK(freedAll);
+    CHECK(ranLast);
+    CHECK(packIds(e).size() == 2);
+    CHECK(e.scriptInstances() == 1);
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.faultCount() == 0);
+}
+
+TEST_CASE("a reload's replan carries the running plan's tap as it is: neither restored nor armed again",
+          "[script][packs][reload]") {
+    using rp::host::test::DocSystem;
+    using rp::host::test::kNesType;
+    using rp::host::test::rearming;
+    Engine e(kRate);
+    MesenNesConfig cfg;
+    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
+    auto core = std::make_unique<MesenNesSystem>(1, cfg, rp::host::test::readFile(RP_BLIPTOASTER_ROM_PATH));
+    core->onActivate(kRate);
+    auto* nes = core.get();
+    REQUIRE(e.parkCore(std::move(core)));
+    load(e, "test.tap", toys(tally("test.tap.count", 1)));
+    const auto request = graphRequest(stereoDocument({DocSystem{1, kNesType}},
+                                                     DocExtras{{docNode("count", "test.tap.count"), midiOut("1")},
+                                                               {docWire("count.out", "midi-out.in1")}}),
+                                      {1});
+    place(e, rearming(request, 1, 1));  // the tap armed to mode 1 from the core's 0
+    CHECK(nes->channelExportMode() == 1);
+    CHECK(block(e) == std::vector<Out>{{1, 0, {1, 1}}});
+    // Moved behind the engine's back, so a replan that restored the tap (to 0) or armed it again (to 1) would show.
+    nes->setChannelExportMode(2);
+    load(e, "test.tap", toys(tally("test.tap.count", 2)));
+    CHECK(block(e) == std::vector<Out>{{1, 0, {2, 1}}});  // the pack's node rebuilt on the new code
+    CHECK(nes->channelExportMode() == 2);
+    // And the engine still holds what it armed: a re-arm with nothing to arm restores the mode the core had before.
+    place(e, rearming(request));
+    CHECK(nes->channelExportMode() == 0);
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.faultCount() == 0);
+}
+
+#ifdef RETROPLUG_PROFILE
+TEST_CASE("a type keeps its span label across its pack's reloads, the trace's name table does not grow, and what each "
+          "freed generation's realm kept leaves the live heap with it",
+          "[script][packs][reload][profile][trace]") {
+    // A tally that, on its first block, keeps 32 KiB in its realm (a global), which outlives its instance and goes only
+    // with its pack's runtimes.
+    const auto source = [](int tag) {
+        return toys("toy.define('test.loop.count', [toy.output('out')], function () {\n"
+                    "  var n = 0;\n"
+                    "  return { process: function (b) {\n"
+                    "    if (n === 0) globalThis.kept = new Float64Array(4096);\n"
+                    "    b.send('out', 0, [" + std::to_string(tag) + ", ++n & 0x7f]);\n"
+                    "  } };\n"
+                    "});\n");
+    };
+    Engine e(kRate);
+    load(e, "test.loop", source(0));
+    place(e, document({docNode("count", "test.loop.count"), midiOut("1")}, {docWire("count.out", "midi-out.in1")}));
+    // The label of the node's process spans over one traced block, named by the trace's name table.
+    const auto traced = [&] {
+        REQUIRE(e.dspTraceReset(true));
+        block(e);
+        const auto names = e.dspTraceNames();
+        std::vector<std::uint32_t> labels;
+        for (const auto& span : e.dspTrace())
+            if (span.label < names.size() && names[span.label] == "test.loop.count") labels.push_back(span.label);
+        REQUIRE(e.dspTraceReset(false));
+        REQUIRE(labels.size() == 1);
+        return std::pair{labels[0], names.size()};
+    };
+    const auto [label, names] = traced();
+    CHECK(label >= rp::host::DSP_SPAN_TYPE_BASE);
+    REQUIRE(e.resetDspAllocStats(false));  // the allocation window, from one generation's realm holding its 32 KiB
+    for (int k = 1; k <= 50; ++k) {
+        INFO("reload " << k);
+        load(e, "test.loop", source(k));
+        CHECK(traced() == std::pair{label, names});
+    }
+    // Fifty generations each kept 32 KiB, and each took it with it: the live heap is where one generation's was.
+    CHECK(std::abs(e.dspAllocStats().liveBytesDelta) < 16 * 1024);
+}
+#endif
diff --git a/packages/native/test/graph/Registry.test.cpp b/packages/native/test/graph/Registry.test.cpp
index 5df5446c..86d391b8 100644
--- a/packages/native/test/graph/Registry.test.cpp
+++ b/packages/native/test/graph/Registry.test.cpp
@@ -169,6 +169,141 @@ TEST_CASE("addPackage checks everything the host will later call or read", "[gra
     CHECK(refusal(newer, {&future}) == "registered");
 }
 
+namespace {
+
+// The versions of a package "p" the replacement cases swap: p.a in each (its name telling which), p.b in the first, p.c
+// in the second.
+const char* aOne(const rp_node_type*) { return R"({"id":"p.a","name":"A one"})"; }
+const char* aTwo(const rp_node_type*) { return R"({"id":"p.a","name":"A two"})"; }
+const char* aThree(const rp_node_type*) { return R"({"id":"p.a","name":"A three"})"; }
+const char* bOne(const rp_node_type*) { return R"({"id":"p.b"})"; }
+const char* cTwo(const rp_node_type*) { return R"({"id":"p.c"})"; }
+
+// One version of a package: its types, held where the package's array points, and the package.
+struct Version {
+    std::vector<rp_node_type>        types;
+    std::vector<const rp_node_type*> list;
+    rp_package                       package{};
+    Version(const char* manifest, std::vector<rp_node_type> t) : types(std::move(t)) {
+        for (const auto& type : types) list.push_back(&type);
+        package = rp_package{RP_ABI_VERSION, manifest, static_cast<std::uint32_t>(list.size()), list.data()};
+    }
+    Version(const Version&) = delete;
+    Version& operator=(const Version&) = delete;
+};
+
+}  // namespace
+
+TEST_CASE("a package is replaced by a new version of itself: its old entries stay valid until it is put back as it "
+          "was, or they are dropped",
+          "[graph][registry]") {
+    NodeRegistry reg;
+    REQUIRE(reg.addPackage(test::testPackage()));
+    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
+    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo), typeWith("p.c", &cTwo)});
+    const Version three(R"({"id":"p"})", {typeWith("p.a", &aThree)});
+    REQUIRE(reg.addPackage(&one.package));
+    CHECK(reg.hasPackage("p"));
+    const auto* a1 = reg.find("p.a");
+    const auto* b1 = reg.find("p.b");
+    REQUIRE(a1);
+    REQUIRE(b1);
+    const std::size_t total = reg.size();
+
+    // The swap: p.a is a new entry (the planner rebuilds an instance whose entry changed), p.b is gone, p.c is found,
+    // and what a planned instance of the first version holds is still there to read.
+    std::string error;
+    CHECK(reg.replacePackage("p", &two.package, &error) == &one.package);
+    INFO(error);
+    const auto* a2 = reg.find("p.a");
+    REQUIRE(a2);
+    CHECK(a2 != a1);
+    CHECK(a2->type == &two.types[0]);
+    CHECK(a2->descriptor.name == "A two");
+    CHECK(reg.find("p.b") == nullptr);
+    REQUIRE(reg.find("p.c"));
+    CHECK(reg.find("p.c")->package == "p");
+    CHECK(a1->descriptor.name == "A one");
+    CHECK(a1->type == &one.types[0]);
+    CHECK(b1->descriptor.id == "p.b");
+    CHECK(reg.size() == total);
+    CHECK(reg.retiredSize() == 2);
+
+    // Undone: the first version's entries found again, the very ones, and the second's gone.
+    CHECK(reg.restorePackage(&one.package));
+    CHECK(reg.find("p.a") == a1);
+    CHECK(reg.find("p.b") == b1);
+    CHECK(reg.find("p.c") == nullptr);
+    CHECK(reg.retiredSize() == 0);
+    CHECK_FALSE(reg.restorePackage(&one.package));  // retired no more
+
+    // Two swaps in a row retire two versions; only the last can be undone, and each is dropped on its own.
+    CHECK(reg.replacePackage("p", &two.package) == &one.package);
+    const auto* a2again = reg.find("p.a");
+    CHECK(reg.replacePackage("p", &three.package) == &two.package);
+    CHECK(reg.find("p.a")->descriptor.name == "A three");
+    CHECK(reg.find("p.c") == nullptr);
+    CHECK(reg.retiredSize() == 4);
+    CHECK_FALSE(reg.restorePackage(&one.package));  // the second version replaced it, not the third
+    CHECK(reg.restorePackage(&two.package));
+    CHECK(reg.find("p.a") == a2again);
+    CHECK(reg.find("p.c")->descriptor.id == "p.c");
+    CHECK(reg.retiredSize() == 2);
+    CHECK(reg.dropRetired(&one.package));
+    CHECK_FALSE(reg.dropRetired(&one.package));
+    CHECK(reg.retiredSize() == 0);
+    CHECK(reg.find("p.a") == a2again);
+    CHECK(reg.replacePackage("p", &three.package) == &two.package);
+    CHECK(reg.dropRetired(&two.package));
+    CHECK(reg.retiredSize() == 0);
+    CHECK(reg.find("p.a")->type == &three.types[0]);
+    CHECK(reg.size() == total - 1);  // p.a alone, where the first version had p.b too
+}
+
+TEST_CASE("a replacement refused changes nothing: a bad descriptor, another package's id, a type id another package "
+          "holds, a package not registered, a version held already",
+          "[graph][registry]") {
+    NodeRegistry reg;
+    REQUIRE(reg.addPackage(test::testPackage()));
+    const Version one(R"({"id":"p"})", {typeWith("p.a", &aOne), typeWith("p.b", &bOne)});
+    const Version two(R"({"id":"p"})", {typeWith("p.a", &aTwo)});
+    REQUIRE(reg.addPackage(&one.package));
+    REQUIRE(reg.replacePackage("p", &two.package) == &one.package);
+    const auto* a = reg.find("p.a");
+    const auto* source = reg.find("rp.test.source");
+    const std::size_t total = reg.size();
+
+    const auto refused = [&](const char* id, const rp_package* next, const std::string& why) {
+        INFO(why);
+        std::string error;
+        CHECK(reg.replacePackage(id, next, &error) == nullptr);
+        CHECK(error.find(why) == 0);
+        CHECK(reg.find("p.a") == a);
+        CHECK(reg.find("p.b") == nullptr);
+        CHECK(reg.find("rp.test.source") == source);
+        CHECK(reg.size() == total);
+        CHECK(reg.retiredSize() == 2);
+    };
+    const Version bad(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("p.x", &badDescriptor)});
+    refused("p", &bad.package, "node type \"p.x\": ");
+    const Version other(R"({"id":"rp.test"})", {typeWith("p.a", &aThree)});
+    refused("p", &other.package,
+            "package \"rp.test\" cannot replace package \"p\": a new version keeps its package's id");
+    const Version stray(R"({"id":"q"})", {typeWith("p.a", &aThree)});
+    refused("p", &stray.package, "package \"q\" cannot replace package \"p\"");
+    const Version taker(R"({"id":"p"})", {typeWith("p.a", &aThree), typeWith("rp.test.source", &sourceDescriptor)});
+    refused("p", &taker.package, "node type \"rp.test.source\" is already registered");
+    refused("q", &stray.package, "package \"q\" is not registered, so it has no version to replace");
+    refused("p", &two.package, "that version of package \"p\" is registered already");
+    refused("p", &one.package, "that version of package \"p\" is registered already");  // retired, not yet dropped
+    refused("p", nullptr, "null package");
+
+    // A package's own type ids are the new version's to take again; the replacement that keeps every one goes through.
+    const Version three(R"({"id":"p"})", {typeWith("p.a", &aThree)});
+    CHECK(reg.replacePackage("p", &three.package) == &two.package);
+    CHECK(reg.find("p.a")->descriptor.name == "A three");
+}
+
 TEST_CASE("endpoints split at the first dot; resource refs parse", "[graph][document]") {
     const auto e = parseEndpoint("gb1.pins.pulse");
     REQUIRE(e);
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops: `NodeRegistry` has no `replacePackage`, `restorePackage`, `dropRetired`, `hasPackage` or `retiredSize`, and `Engine` no `retiredNodeTypeCount` or `scriptInstances`, yet. (The prototype's red run, with those names in place, saw all five new Engine cases fail at the reload: `pack ... is already loaded`.)

- [ ] **Step 3: Swap, undo and drop a package's entries**

The validation `addPackage` does is factored out and shared. `replacePackage` returns the swapped-out version's
package, the key for the other two calls, or refuses changing nothing; the old entries are retired as `std::map` node
handles (`extract`), so every entry keeps its address, and `restorePackage` re-inserts the same ones, so a plan
holding them still matches and nothing rebuilds:

````diff
diff --git a/packages/native/src/graph/NodeRegistry.cpp b/packages/native/src/graph/NodeRegistry.cpp
index 8ba6787b..8ef8a4f9 100644
--- a/packages/native/src/graph/NodeRegistry.cpp
+++ b/packages/native/src/graph/NodeRegistry.cpp
@@ -1,5 +1,6 @@
 #include "graph/NodeRegistry.hpp"
 
+#include <algorithm>
 #include <vector>
 
 #include <rfl/from_generic.hpp>
@@ -29,7 +30,8 @@ rfl::Result<std::string> packageId(const rp_package* p) {
 
 }  // namespace
 
-bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
+bool NodeRegistry::check(const rp_package* package, std::string_view replacing, std::string& pkg,
+                         std::vector<Entry>& added, std::string* error) const {
     auto fail = [&](std::string why) {
         if (error) *error = std::move(why);
         return false;
@@ -41,12 +43,20 @@ bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
                     std::to_string(RP_ABI_VERSION));
     const auto id = packageId(package);
     if (!id) return fail(id.error().what());
-    const std::string& pkg = *id;
-    if (packages_.count(pkg)) return fail("package " + q(pkg) + " is already registered");
+    pkg = *id;
+    if (replacing.empty()) {
+        if (packages_.count(pkg)) return fail("package " + q(pkg) + " is already registered");
+    } else {
+        if (pkg != replacing)
+            return fail("package " + q(pkg) + " cannot replace package " + q(replacing) +
+                        ": a new version keeps its package's id");
+        if (!packages_.count(pkg))
+            return fail("package " + q(pkg) + " is not registered, so it has no version to replace");
+        if (holds(package)) return fail("that version of package " + q(pkg) + " is registered already");
+    }
     if (package->node_type_count > 0 && !package->node_types)
         return fail("package " + q(pkg) + " lists " + std::to_string(package->node_type_count) + " node types but no array");
 
-    std::vector<Entry> added;
     for (std::uint32_t i = 0; i < package->node_type_count; ++i) {
         const rp_node_type* type = package->node_types[i];
         const std::string at = "package " + q(pkg) + " node type #" + std::to_string(i);
@@ -65,19 +75,92 @@ bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
         auto descriptor = parseDescriptor(json);
         if (!descriptor) return fail(where + ": " + descriptor.error().what());
         if (descriptor->id != type->id) return fail(where + " describes itself as " + q(descriptor->id));
-        if (find(type->id)) return fail(where + " is already registered");
+        // A replacement may take its own package's type ids again; never another package's.
+        if (const Entry* held = find(type->id); held && held->package != replacing)
+            return fail(where + " is already registered");
         for (const auto& a : added)
             if (a.descriptor.id == descriptor->id) return fail(where + " appears twice in package " + q(pkg));
         added.push_back(Entry{type, std::move(*descriptor), pkg});
     }
+    return true;
+}
+
+bool NodeRegistry::holds(const rp_package* source) const {
+    for (const auto& [id, version] : packages_)
+        if (version.source == source) return true;
+    return std::any_of(retired_.begin(), retired_.end(), [&](const Retired& r) { return r.source == source; });
+}
+
+std::vector<NodeRegistry::Retired>::iterator NodeRegistry::retiredVersion(const rp_package* source) {
+    return std::find_if(retired_.begin(), retired_.end(), [&](const Retired& r) { return r.source == source; });
+}
+
+bool NodeRegistry::addPackage(const rp_package* package, std::string* error) {
+    std::string pkg;
+    std::vector<Entry> added;
+    if (!check(package, {}, pkg, added, error)) return false;
+    Version& version = packages_[pkg];
+    version.source = package;
+    for (auto& e : added) {
+        version.types.push_back(e.descriptor.id);
+        std::string key = e.descriptor.id;
+        entries_.emplace(std::move(key), std::move(e));
+    }
+    return true;
+}
+
+const rp_package* NodeRegistry::replacePackage(std::string_view id, const rp_package* next, std::string* error) {
+    std::string pkg;
+    std::vector<Entry> added;
+    if (!check(next, id, pkg, added, error)) return nullptr;
+    Version& current = packages_.find(pkg)->second;
+    Retired old{pkg, current.source, next, {}};
+    for (const auto& type : current.types) old.entries.push_back(entries_.extract(type));
+    current.source = next;
+    current.types.clear();
     for (auto& e : added) {
+        current.types.push_back(e.descriptor.id);
         std::string key = e.descriptor.id;
         entries_.emplace(std::move(key), std::move(e));
     }
-    packages_.insert(pkg);
+    const rp_package* const previous = old.source;
+    retired_.push_back(std::move(old));
+    return previous;
+}
+
+bool NodeRegistry::restorePackage(const rp_package* previous) {
+    const auto at = retiredVersion(previous);
+    if (at == retired_.end()) return false;
+    const auto pkg = packages_.find(at->package);
+    if (pkg == packages_.end() || pkg->second.source != at->replacedBy) return false;  // not its package's last
+    // Each of its type ids is free once the version that replaced it goes, unless another package took one since.
+    for (const auto& e : at->entries)
+        if (const auto taken = entries_.find(e.key()); taken != entries_.end() && taken->second.package != at->package)
+            return false;
+    for (const auto& type : pkg->second.types) entries_.erase(type);
+    pkg->second.source = previous;
+    pkg->second.types.clear();
+    for (auto& e : at->entries) {
+        pkg->second.types.push_back(e.key());
+        entries_.insert(std::move(e));  // the node itself, so the Entry is where it was
+    }
+    retired_.erase(at);
     return true;
 }
 
+bool NodeRegistry::dropRetired(const rp_package* retired) {
+    const auto at = retiredVersion(retired);
+    if (at == retired_.end()) return false;
+    retired_.erase(at);
+    return true;
+}
+
+std::size_t NodeRegistry::retiredSize() const {
+    std::size_t n = 0;
+    for (const auto& r : retired_) n += r.entries.size();
+    return n;
+}
+
 const NodeRegistry::Entry* NodeRegistry::find(std::string_view typeId) const {
     const auto it = entries_.find(typeId);
     return it != entries_.end() ? &it->second : nullptr;
diff --git a/packages/native/src/graph/NodeRegistry.hpp b/packages/native/src/graph/NodeRegistry.hpp
index f13488ca..4679db1c 100644
--- a/packages/native/src/graph/NodeRegistry.hpp
+++ b/packages/native/src/graph/NodeRegistry.hpp
@@ -1,9 +1,9 @@
 #pragma once
 
 #include <map>
-#include <set>
 #include <string>
 #include <string_view>
+#include <vector>
 
 #include "graph/Descriptor.hpp"
 #include "graph/rp_node.h"
@@ -14,11 +14,18 @@ namespace rp::graph {
 // and (later) downloaded ones go through the same addPackage, so there is one loading path, and it is the
 // trust boundary for a pack's metadata: everything the host will later call or read is checked here.
 //
-// Entries are never moved or removed, so an Entry* stays valid for the registry's life (a planned instance
-// keeps one), whatever is added after. Not thread-safe: addPackage must not overlap any other call, so a registry
-// shared by several hosts is filled before any of them plans; after that, concurrent find() is safe. One host may add
-// packages between its plans on the thread that plans (an Engine's script node packs: Engine::loadScriptPack), since
-// planning is the only reader: an executor runs a plan's instances and never looks a type up.
+// A package may be replaced by a new version of itself (replacePackage: a script node pack's reload,
+// docs/modular-graph-phase-c1-design.md section 5), and that is the only way an entry stops being found. An entry is
+// never moved, and never freed while anything may hold it: a planned instance keeps an Entry* (and destroys its node
+// through it), so the entries a replacement swaps out are retired, kept where they are, until their version is put back
+// (restorePackage, when what the replacement was for is refused: the same Entry objects found again, so a plan that
+// holds them still matches them and nothing is rebuilt) or dropped (dropRetired, once nothing holds one). A version is
+// named by the rp_package it was registered from, which its owner holds anyway (a script pack's generation is a package
+// of its own, alive as long as the generation is): so whoever frees a version names it by what it frees, and no token
+// has to be kept beside it. Not thread-safe: no call may overlap another, so a registry shared by several hosts is
+// filled before any of them plans, and after that, concurrent find() is safe. One host may change it between its plans
+// on the thread that plans (an Engine's script node packs: Engine::loadScriptPack adds and replaces, its reclaim
+// drops), since planning is the only reader: an executor runs a plan's instances and never looks a type up.
 class NodeRegistry {
 public:
     struct Entry {
@@ -35,12 +42,55 @@ public:
     // its flags: every bit is a relaxation (rp_node.h), so one this host does not know is ignored.
     bool addPackage(const rp_package* package, std::string* error = nullptr);
 
+    // Swap the entries of package `id` for those of `next`, a new version of it, checked as addPackage checks a
+    // package. Every type of the new version gets an Entry of its own, a type id it shares with the old one included,
+    // so the planner, which compares an instance's entry with the one it finds, rebuilds every instance of the package;
+    // a type the new version lacks is found no more. The old version's entries are retired, still valid for whatever
+    // holds one. The rp_package the swapped-out version was registered from, the key restorePackage and dropRetired
+    // take; null, with a reason, changing nothing, when `next` would be refused by addPackage (a type id another
+    // package holds among the reasons), its manifest names another package than `id`, no package `id` is registered, or
+    // `next` is a version the registry holds already (`id`'s current one, or one retired).
+    const rp_package* replacePackage(std::string_view id, const rp_package* next, std::string* error = nullptr);
+    // Undo the replacement that retired `previous`, which must be the last one made to its package: its entries are
+    // found again, the same Entry objects, and those of the version that replaced it are dropped, so nothing may hold
+    // one (a plan made with them was refused, and freed with its instances). False, changing nothing, when `previous`
+    // is no version a package's last replacement retired, or a type id it held is another package's since.
+    bool restorePackage(const rp_package* previous);
+    // A retired version's entries dropped: nothing may hold one any longer. False when `retired` is no retired version.
+    bool dropRetired(const rp_package* retired);
+
+    bool         hasPackage(std::string_view id) const { return packages_.count(id) != 0; }
     const Entry* find(std::string_view typeId) const;
-    std::size_t  size() const { return entries_.size(); }
+    std::size_t  size() const { return entries_.size(); }  // the entries find finds
+    std::size_t  retiredSize() const;                      // the entries retired and not yet put back or dropped
 
 private:
-    std::map<std::string, Entry, std::less<>> entries_;  // node-based: an Entry never moves
-    std::set<std::string, std::less<>>        packages_;
+    using Entries = std::map<std::string, Entry, std::less<>>;  // node-based: an Entry never moves
+    // A package registered: the rp_package its entries came from, and their type ids.
+    struct Version {
+        const rp_package*        source = nullptr;
+        std::vector<std::string> types;
+    };
+    // A version a replacement swapped out: its entries, extracted from the map whole (a node handle keeps its element
+    // where it is, so each Entry stays at its address), its package, where it came from and what replaced it.
+    struct Retired {
+        std::string                     package;
+        const rp_package*               source = nullptr;
+        const rp_package*               replacedBy = nullptr;
+        std::vector<Entries::node_type> entries;
+    };
+
+    // The entries `package` would register, checked, and its id; `replacing` names the package a replacement replaces
+    // (empty for an addition), whose own type ids the new version may take again.
+    bool check(const rp_package* package, std::string_view replacing, std::string& id, std::vector<Entry>& entries,
+               std::string* error) const;
+    // Whether the registry holds the version `source` came from: a package's current one, or one retired.
+    bool holds(const rp_package* source) const;
+    std::vector<Retired>::iterator retiredVersion(const rp_package* source);  // the retired version `source` came from
+
+    Entries                                      entries_;
+    std::map<std::string, Version, std::less<>>  packages_;
+    std::vector<Retired>                         retired_;
 };
 
 }  // namespace rp::graph
````

- [ ] **Step 4: Generations, record counts and freeing in the script host**

`loadPack` on an id already loaded builds its next generation. Each pack counts its instance records (made in the
shim's create, deleted where the control thread deletes them, so a plain count) and stands current, retired or
discarded; a retired or discarded pack with no record left is freed (`freeIdle`) from `retirePack`, `discardPack`,
`reclaim` and `setAudioThreadOwns(false)`: its callback first, while its types exist (the Engine's registry drops
their entries there), then `leave` (`join`'s reverse), then its runtimes. A refused reload's generation may have
records out under an audio thread (the fresh instances the refused plan made), so it waits for them as a retired one
does. Freeing on the control thread is safe because the last record came back through the release ring, and no JS
value crosses packs:

````diff
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index 86b980b3..3b032bac 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -355,6 +355,16 @@ struct ScriptHost::Pack {
     // released (Glue::chargeJobs).
     Instance*     jobsOwner = nullptr;
     bool          jobsOrphaned = false;
+    // The control thread's. The records of its types' instances alive (made by create, deleted by deleteRecord), and
+    // whether it stands: a generation a reload replaced is retired, one a refused reload built is discarded, and either
+    // is freed once its records are 0 (freeIdle).
+    std::uint32_t records = 0;
+    enum class Standing : std::uint8_t { Current, Retired, Discarded };
+    Standing      standing = Standing::Current;
+    // Its live heap as it joined, which the shared counters never had (a loaded pack's load is counted apart; the
+    // built-in pack's is not, but it is freed only with the host): what they have of its live heap is the rest, which a
+    // profile build takes off them once it is freed (settleFreedLive).
+    std::size_t   joinedLive = 0;
 };
 
 // One script node instance, behind its shim. Allocated by create and deleted on the control thread; its JS state is
@@ -644,6 +654,7 @@ struct ScriptHost::Glue {
         auto* shim = new (memory) Shim{};
         shim->vt = &kVtable;
         shim->in = in.release();
+        ++type.pack->records;  // the pack is freed only once every record of its types is back (freeIdle)
         return shim;
     }
 
@@ -1722,18 +1733,22 @@ bool ScriptHost::canLoadPack(const std::string& id, std::string* error) const {
         return refuse("pack id " + graph::jsonString(id) +
                       " is the app's to give: \"rp\" and every id under \"rp.\" name its own packages (its built-in "
                       "script pack, its native ones)");
-    for (const auto& pack : packs_)
-        if (pack->id == id) return refuse("pack " + graph::jsonString(id) + " is already loaded");
     return true;
 }
 
 const rp_package* ScriptHost::loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
                                        std::string* error) {
     if (!canLoadPack(id, error)) return nullptr;
+    // An id loaded already gets its next generation: one more than the newest held (a retired one waiting for its
+    // records, or a discarded one, among them).
+    std::uint32_t generation = 1;
+    for (const auto& held : packs_)
+        if (held->id == id) generation = std::max(generation, held->generation + 1);
     // What its audio runtime allocates until it joins, the load's own, counted apart: this is the control thread, and
     // the block thread may be counting into the shared counters, which have one writer at a time.
     Counters loading;
     auto pack = makePack(id, loading);
+    pack->generation = generation;
     pack->heap.limit = memoryLimit ? memoryLimit : kDefaultMemoryLimit;  // before the load, whose allocations it holds
     if (!loadInto(*pack, chunks, error)) {
         freeRuntimes(*pack);  // counted in `loading` still
@@ -1749,21 +1764,89 @@ const rp_package* ScriptHost::loadPack(const std::string& id, std::span<const Ch
     return &packs_.back()->package;
 }
 
+ScriptHost::Packs::iterator ScriptHost::findPack(const rp_package* package) {
+    return std::find_if(packs_.begin() + 1, packs_.end(), [&](const auto& p) { return &p->package == package; });
+}
+
+void ScriptHost::retirePack(const rp_package* package) {
+    const auto at = findPack(package);
+    assert(at != packs_.end() && "retirePack takes a pack loadPack returned, never the built-in one");
+    if (at == packs_.end()) return;
+    (*at)->standing = Pack::Standing::Retired;
+    if ((*at)->records == 0) freePack(at);
+}
+
 void ScriptHost::discardPack(const rp_package* package) {
-    const auto at =
-        std::find_if(packs_.begin() + 1, packs_.end(), [&](const auto& p) { return &p->package == package; });
+    const auto at = findPack(package);
     assert(at != packs_.end() && "discardPack takes a pack loadPack returned, never the built-in one");
     if (at == packs_.end()) return;
-    freeRuntimes(**at);
-    packs_.erase(at);
+    (*at)->standing = Pack::Standing::Discarded;
+    if ((*at)->records == 0) freePack(at);
+}
+
+void ScriptHost::setPackFreed(PackFreed freed, void* user) {
+    packFreed_ = freed;
+    packFreedUser_ = user;
 }
 
 std::vector<ScriptHost::PackInfo> ScriptHost::packs() const {
     std::vector<PackInfo> out;
-    for (const auto& pack : packs_) out.push_back({pack->id, pack->generation});
+    for (const auto& pack : packs_)
+        out.push_back({pack->id, pack->generation, pack->standing != Pack::Standing::Current, pack->records});
     return out;
 }
 
+// Every retired or discarded pack no record of whose types is alive: the block thread holds nothing of it (the class
+// comment), so it goes here, on the control thread, whether or not an audio thread runs.
+void ScriptHost::freeIdle() {
+    for (auto at = packs_.begin() + 1; at != packs_.end();) {
+        if ((*at)->standing != Pack::Standing::Current && (*at)->records == 0) at = freePack(at);
+        else ++at;
+    }
+}
+
+// A retired generation's package named to the callback while its types still stand (what holds its retired entries
+// drops them), then its audio runtime leaves what every pack shares and its runtimes go, with the pack.
+ScriptHost::Packs::iterator ScriptHost::freePack(Packs::iterator at) {
+    Pack& pack = **at;
+    if (pack.standing == Pack::Standing::Retired && packFreed_) packFreed_(packFreedUser_, &pack.package);
+    Counters freeing;  // what freeing it frees, counted apart (leave)
+    leave(pack, freeing);
+    freeRuntimes(pack);
+    at = packs_.erase(at);
+    settleFreedLive();
+    return at;
+}
+
+// join's reverse, for a pack about to be freed on the control thread: its frees counted apart, since the block thread
+// may be writing the shared counters (a profile build notes what it had added to their live heap, for settleFreedLive),
+// and the control thread's interrupt handler in place of the block thread's, whose deadline state it must not touch.
+// Both runtimes' stacks anchored here: the audio one was last entered on the block thread.
+void ScriptHost::leave(Pack& pack, Counters& counters) {
+#ifdef RETROPLUG_PROFILE
+    freedLive_ += static_cast<std::int64_t>(pack.heap.live) - static_cast<std::int64_t>(pack.joinedLive);
+#endif
+    pack.heap.counters = &counters;
+    JS_SetInterruptHandler(pack.audioRt, &Glue::controlInterrupt, this);
+    JS_UpdateStackTop(pack.audioRt);
+    JS_UpdateStackTop(pack.controlRt);
+}
+
+// A profile build's live heap in the shared counters, without what freed packs had added to it since they joined:
+// taken off once no audio thread runs, when this thread is their one writer.
+void ScriptHost::settleFreedLive() {
+#ifdef RETROPLUG_PROFILE
+    if (freedLive_ == 0 || audioOwns_.load(std::memory_order_acquire)) return;
+    Glue::grew(counters_, -freedLive_);
+    freedLive_ = 0;
+#endif
+}
+
+void ScriptHost::deleteRecord(Instance* in) {
+    --in->type->pack->records;
+    delete in;
+}
+
 // The pack's audio runtime joins what every pack shares: what the block thread runs in it held to the call deadline,
 // whose state every pack shares (the handler's opaque is the host), and its allocations counted with every other
 // pack's (and, in a profile build, its auto-GC pinned off with theirs). Not before its load is done, which runs on the
@@ -1772,6 +1855,7 @@ std::vector<ScriptHost::PackInfo> ScriptHost::packs() const {
 void ScriptHost::join(Pack& pack) {
     JS_SetInterruptHandler(pack.audioRt, &Glue::interrupt, this);  // in place of the control thread's
     pack.heap.counters = &counters_;
+    pack.joinedLive = pack.heap.live;
     if (gcPinned_) JS_SetGCThreshold(pack.audioRt, static_cast<std::size_t>(-1));
 }
 
@@ -1928,6 +2012,8 @@ void ScriptHost::setAudioThreadOwns(bool owns) {
     audioOwns_.store(owns, std::memory_order_release);
     if (owns) return;
     releaseQueued();
+    freeIdle();  // a generation whose last records were still queued
+    settleFreedLive();
     collect();  // the runtimes are this thread's again: what the audio thread's releases left in cycles goes now
 }
 
@@ -1942,7 +2028,7 @@ void ScriptHost::retire(Instance* in) {
     drainReleased();
     if (!audioOwns_.load(std::memory_order_acquire)) {
         releaseJs(*in);
-        delete in;
+        deleteRecord(in);
         return;
     }
     if (!backlogHead_ && releases_.tryPush(in)) return;
@@ -1979,7 +2065,7 @@ void ScriptHost::drainControlJobs(Pack& pack) {
 
 void ScriptHost::drainReleased() {
     Instance* in = nullptr;
-    while (released_.tryPop(in)) delete in;
+    while (released_.tryPop(in)) deleteRecord(in);
     while (backlogHead_) {  // the next read first: once pushed, a record is the block thread's until it comes back
         Instance* next = backlogHead_->backlogNext;
         if (!releases_.tryPush(backlogHead_)) break;
@@ -2007,6 +2093,7 @@ void ScriptHost::reclaim() {
                      firstThrow_);
         firstThrowLogged_ = true;
     }
+    freeIdle();  // a retired generation whose last record just came back
     if (!audioOwns_.load(std::memory_order_acquire)) collect();  // quiescent: what this thread's releases left in cycles
 }
 
@@ -2020,16 +2107,16 @@ void ScriptHost::releaseQueued() {
     Instance* in = nullptr;
     while (releases_.tryPop(in)) {
         releaseJs(*in);
-        delete in;
+        deleteRecord(in);
     }
     for (Instance* b = backlogHead_; b;) {
         Instance* next = b->backlogNext;
         releaseJs(*b);
-        delete b;
+        deleteRecord(b);
         b = next;
     }
     backlogHead_ = backlogTail_ = nullptr;
-    while (released_.tryPop(in)) delete in;
+    while (released_.tryPop(in)) deleteRecord(in);
 }
 
 // Quiescent only, and never by a block: a GC's cost is the whole heap's, which a block cannot afford. QuickJS's own
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index 77fd8652..c7a71a2d 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -130,6 +130,23 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 // every pack shares (the call deadline's interrupt handler, the allocation counters) as its load ends (join). So what
 // a loaded pack's load allocates is left out of the allocation counters, which count the audio side's blocks,
 // adoptions and releases; the built-in pack's, at the host's construction, comes before any and is in them.
+// A pack loaded again is reloaded (docs/modular-graph-phase-c1-design.md section 5): loadPack on an id loaded builds
+// its next generation beside the current one, as a load builds a pack, under the same id and namespace, and the caller
+// settles which stands once it knows whether a plan placing the new one's nodes was published: retirePack retires the
+// old generation, discardPack the new one. Either way that generation's instances go the way every released instance
+// goes, and a pack counts the records of its types' instances alive (create makes them, and drainReleased,
+// releaseQueued and retire's quiescent path delete them, all on the control thread, so a plain count). A retired or
+// discarded generation whose count is 0 is freed by the call that finds it so (retirePack, discardPack, reclaim,
+// setAudioThreadOwns(false)), on the control thread: its runtimes and contexts, its types' definitions and its atoms,
+// with the pack-freed callback told a retired one's package first (an Engine's registry drops its retired entries
+// then). That holds while an audio thread runs blocks in other packs: the last record came back through the release
+// ring after the block thread released its JS (or was released on this thread while none ran), so the block thread
+// holds nothing of the pack, no plan it may yet adopt places one of its nodes (that plan would hold a record), and no
+// JS value crosses packs, so no other heap points into its. Its audio runtime first leaves what every pack shares
+// (leave, join's reverse): what freeing it frees is kept out of the allocation counters, as a loaded pack's load is,
+// since the block thread may be writing them. So a whole generation's heap is freed off the audio thread, where an
+// instance's release frees its JS on it. A record the release ring back could not take, leaked rather than freed on the
+// audio thread, leaks its pack with it. The built-in pack is never retired.
 // What every pack shares stays the host's: the call deadline (one call runs at a time on the block thread), the
 // allocation counters (summed over every pack's audio runtime, so audioAllocations and the profile's allocation window
 // mean the whole audio side), the release rings, the throw count and the first throw, and the profile's span labels.
@@ -263,42 +280,61 @@ public:
     // The built-in pack's script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a
     // load).
     const rp_package* package() const;
-    // Load a pack beside the built-in one (docs/modular-graph-phase-c1-design.md sections 4 and 5): a new pack named
-    // `id`, its two runtimes made for it, runs a bundle (global-code bytecode, in chunks run in order) in both, its
-    // audio runtime held to `memoryLimit` bytes (0: kDefaultMemoryLimit), the load included, and its definitions are
-    // read and checked as load checks the built-in pack's. Callable while an audio thread runs blocks (the class
-    // comment). Its package, named by `id`, for NodeRegistry::addPackage, valid for the host's life unless discarded;
-    // null, with a reason, when canLoadPack refuses the id (nothing is built), or, the pack freed there and then, when
-    // a chunk fails to load (or runs out of memory), the load runs past the call deadline, a definition cannot be read
-    // or is not the same in both contexts, or a type id is not the pack's id, a dot and more ("user.synth.osc" in
-    // "user.synth").
+    // Load a pack beside the built-in one (docs/modular-graph-phase-c1-design.md sections 4 and 5): a pack named `id`,
+    // its two runtimes made for it, runs a bundle (global-code bytecode, in chunks run in order) in both, its audio
+    // runtime held to `memoryLimit` bytes (0: kDefaultMemoryLimit), the load included, and its definitions are read and
+    // checked as load checks the built-in pack's. Callable while an audio thread runs blocks (the class comment). On an
+    // id loaded already it builds the pack's next generation (one more than the newest of the id held), beside the
+    // current one, until retirePack or discardPack settles which goes. Its package, named by `id`, for the registry
+    // (NodeRegistry::addPackage, or replacePackage for a new generation), valid until the pack is freed; null, with a
+    // reason, when canLoadPack refuses the id (nothing is built), or, the pack freed there and then, when a chunk fails
+    // to load (or runs out of memory), the load runs past the call deadline, a definition cannot be read or is not the
+    // same in both contexts, or a type id is not the pack's id, a dot and more ("user.synth.osc" in "user.synth").
     const rp_package* loadPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
                                std::string* error = nullptr);
     // Whether loadPack takes a pack named `id`: a plain id (graph::isPlainId), not the built-in pack's ("rp.script",
-    // which is not reloadable), not otherwise the app's ("rp" and every id under "rp.", where its native packages are
-    // named too), and none loaded already. False, with the reason.
+    // which is not reloadable), and not otherwise the app's ("rp" and every id under "rp.", where its native packages
+    // are named too). False, with the reason.
     bool canLoadPack(const std::string& id, std::string* error = nullptr) const;
-    // Free a pack loadPack returned whose package no registry took (a NodeRegistry refused it), so nothing has made an
-    // instance of its types: its runtimes, there and then. Never the built-in pack.
+    // Retire the generation `package` names, which a reload replaced: a plan placing the new generation's nodes in
+    // place of its own is published (or none placed its own). Freed at once when no record of its types' instances is
+    // alive, else by the call that finds the last one back (the class comment), the pack-freed callback told its
+    // package first either way. Never the built-in pack.
+    void retirePack(const rp_package* package);
+    // Free a pack loadPack returned that never stood: a registry refused its package, or the replan of the reload that
+    // built it was refused. There and then when no record of its types' instances is alive; else (instances a refused
+    // plan made, whose releases the block thread has yet to hand back) by the call that finds the last one back. No
+    // registry holds its entries, so the pack-freed callback is not told. Never the built-in pack.
     void discardPack(const rp_package* package);
-    // A pack loaded: its id and its generation (its load's count).
+    // Told, on the control thread, of each retired generation as it is freed, before its types go: `user` as given, and
+    // its package, whose retired entries whatever holds them drops then (an Engine's registry:
+    // NodeRegistry::dropRetired). Never by the destructor, which frees every pack it holds.
+    using PackFreed = void (*)(void* user, const rp_package* package);
+    void setPackFreed(PackFreed freed, void* user);
+    // A pack held: its id, its generation (its load's count: 1, then one more each reload), whether it is a retired or
+    // discarded one waiting for its records, and how many records of its types' instances are alive.
     struct PackInfo {
         std::string   id;
         std::uint32_t generation = 0;
+        bool          retired = false;
+        std::uint32_t records = 0;
     };
-    // Every pack loaded, the built-in one first, then in load order.
+    // Every pack held, the built-in one first, then in load order: a retired or discarded generation among them until
+    // it is freed.
     std::vector<PackInfo> packs() const;
     // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
-    // queued instance there and then, and collects what they left.
+    // queued instance there and then, frees each retired or discarded pack whose last records that brings back, and
+    // collects what they left.
     void setAudioThreadOwns(bool owns);
     // How much of the calling thread's CPU time one call into a pack's JS may use before it is stopped, on either side
     // (kDefaultCallDeadline until set: the class comment). Set it before an audio thread runs blocks.
     void setCallDeadline(std::chrono::milliseconds limit);
     static constexpr std::chrono::milliseconds kDefaultCallDeadline{2000};
-    // Delete the records the block thread has released, and log the first throw once. While quiescent, also collect
-    // what instances released since the last left in cycles, and, the first time after a pack's load, what its bundle's
-    // load left (whether or not anything was released), in each pack that has some: call it after an operation that
-    // may release some.
+    // Delete the records the block thread has released, free each retired or discarded pack whose last records that
+    // brings back (the class comment), and log the first throw once. While quiescent, also collect what instances
+    // released since the last left in cycles, and, the first time after a pack's load, what its bundle's load left
+    // (whether or not anything was released), in each pack that has some: call it after an operation that may release
+    // some.
     void reclaim();
     // The first throw's type and message ("" until one), once the block thread has kept it.
     std::string firstThrow() const;
@@ -385,7 +421,16 @@ private:
     // control thread, its load done and before any plan can place its types: the pack's audio runtime joins what every
     // pack shares (the class comment)
     void join(Pack& pack);
+    // control thread, no record of its types alive: the pack's audio runtime leaves what every pack shares, its frees
+    // counted in `counters` from then on
+    void leave(Pack& pack, Counters& counters);
     void freeTypes(Pack& pack);
+    using Packs = std::vector<std::unique_ptr<Pack>>;
+    Packs::iterator findPack(const rp_package* package);  // a pack loadPack returned, by its package, or end
+    void freeIdle();                    // control thread: every retired or discarded pack with no record alive, freed
+    Packs::iterator freePack(Packs::iterator at);  // control thread: one of those, freed; the pack after it
+    void settleFreedLive();             // control thread: a profile build's live heap, past freed packs, once quiescent
+    void deleteRecord(Instance* in);    // control thread: a record its release is done with, off its pack's count
     std::uint32_t spanLabelFor(const std::string& typeId);  // control thread: a type id's label (DspSpanLabel)
     void releaseJs(Instance& in);   // the block thread, or quiescent
     void retire(Instance* in);      // control thread: a destroyed shim's record
@@ -400,12 +445,18 @@ private:
     Binder                       binder_ = nullptr;
     void*                        binderUser_ = nullptr;
     // Every pack, the built-in one first. Made, loaded and read on the control thread (and quiescent, by releaseQueued,
-    // collect and the profile calls), so it grows (loadPack) and shrinks (discardPack) there, while an audio thread
-    // runs blocks; the block thread reaches a pack only through an instance of one of its types, never through this
-    // list.
-    std::vector<std::unique_ptr<Pack>> packs_;
+    // collect and the profile calls), so it grows (loadPack) and shrinks (freePack) there, while an audio thread runs
+    // blocks; the block thread reaches a pack only through an instance of one of its types, never through this list.
+    Packs                              packs_;
     bool                               loaded_ = false;  // load has been called
     bool                               gcPinned_ = false;  // resetAllocStats pinned the audio runtimes' auto-GC off
+    PackFreed                          packFreed_ = nullptr;
+    void*                              packFreedUser_ = nullptr;
+#ifdef RETROPLUG_PROFILE
+    // What freed packs had added to the live heap in counters_ since they joined, taken off it once no audio thread
+    // runs (settleFreedLive): the control thread's.
+    std::int64_t                       freedLive_ = 0;
+#endif
     // Each type id's span label (DspSpanLabel), given at its first registration in any pack and kept, and the next one
     // to give (control thread).
     std::unordered_map<std::string, std::uint32_t> spanLabels_;
````

- [ ] **Step 5: Reload through the Engine**

The Engine keeps the request `setGraph` last published. A load of an id the registry holds swaps its entries and
replans that request with `rearmTap = false` (which sends no tap trailer, so the running tap is left as it is), skips
the replan when that document places none of the old generation's types, retires the old generation on success, and on
a refusal restores the entries, discards the new generation and returns the planner's errors. A replan neither frees a
parked core nor replaces the request it replans:

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 83c97737..7db338a8 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -1,6 +1,7 @@
 #include "host/engine/Engine.hpp"
 
 #include <algorithm>
+#include <cassert>
 #include <cstddef>
 #include <cstdint>
 #include <cstdio>
@@ -92,6 +93,26 @@ std::uint32_t hostWidthOf(const rp::graph::GraphDocument& doc) {
     return 0;
 }
 
+// Whether `document` places a node of one of `package`'s types. A document that does not parse places nothing; one
+// published always parses.
+bool placesAny(const std::string& document, const rp_package& package) {
+    const auto doc = rp::graph::parseGraphDocument(document);
+    if (!doc) return false;
+    for (const auto& n : doc->nodes)
+        for (std::uint32_t k = 0; k < package.node_type_count; ++k)
+            if (n.type == package.node_types[k]->id) return true;
+    return false;
+}
+
+// The planner's errors as one line: the first, and how many more.
+std::string describeErrors(const std::vector<Engine::GraphError>& errors) {
+    if (errors.empty()) return "no reason given";
+    const auto& e = errors.front();
+    std::string text = e.code + " at " + e.nodeId + (e.port.empty() ? "" : "." + e.port) + ": " + e.message;
+    if (errors.size() > 1) text += " (and " + std::to_string(errors.size() - 1) + " more)";
+    return text;
+}
+
 // The embedded script-node bundle (src/script/bundle.ts) every Engine loads unless it is built with another.
 std::span<const rp::host::ScriptHost::Chunk> embeddedScripts() {
     static const rp::host::ScriptHost::Chunk chunks[] = {rp::host::ScriptHost::embeddedBundle()};
@@ -108,10 +129,14 @@ struct Engine::Graph {
     // First, so it goes last: the script nodes the host destroys hand their instances back to it, and its packs' types
     // are the registry's script entries.
     rp::host::ScriptHost                   script;
-    // Filled at construction and by loadScriptPack, on the control thread, which is where every plan is made too
-    // (setGraph, and reactivate, quiescent), so an addPackage never overlaps a find (NodeRegistry's threading rule).
-    // Nothing on the audio thread reads it: a plan's instances carry their entries, which only the control thread reads
-    // (to destroy a node, as it frees the plan that held it), and an entry never moves.
+    // Filled at construction and by loadScriptPack (a pack's package added, or a reload's swapped in, and swapped back
+    // when its replan is refused), and a reload's retired entries dropped as the script host frees their generation
+    // (its pack-freed callback, from the reclaims), all on the control thread, which is where every plan is made too
+    // (setGraph, and reactivate, quiescent), so no change overlaps a find (NodeRegistry's threading rule). Nothing on
+    // the audio thread reads it: a plan's instances carry their entries, which only the control thread reads (to
+    // destroy a node, as it frees the plan that held it), and an entry never moves, nor is dropped while an instance
+    // holds it: a retired generation is freed only once no record of its types' instances is alive, each one's planned
+    // instance gone first.
     rp::graph::NodeRegistry                registry;
     rp::graph::ResourceTable               resources;
     rp::host::HostCores                    cores{};
@@ -131,6 +156,10 @@ Engine::Engine(double sampleRate, std::span<const rp::host::ScriptHost::Chunk> s
     // The script node types first: the bundle runs in both of the built-in pack's contexts, and its definitions become
     // one more package. One that will not load registers no type, so a document naming one is refused (unknown-type).
     std::string why;
+    // A retired generation's entries go as the script host frees it, on whichever reclaim frees it.
+    graph_->script.setPackFreed(
+        [](void* ctx, const rp_package* package) { static_cast<Graph*>(ctx)->registry.dropRetired(package); },
+        graph_.get());
     if (!graph_->script.load(scriptBundle, &why))
         std::fprintf(stderr, "[engine] the script-node bundle did not load (%s); no script node type is registered\n", why.c_str());
     // The routing, the controller and the protocol nodes are the script package's (src/script/nodes/).
@@ -231,7 +260,9 @@ bool Engine::parkCore(std::unique_ptr<SystemBase> core) {
     return true;
 }
 
-Engine::GraphResult Engine::setGraph(const GraphRequest& request) {
+Engine::GraphResult Engine::setGraph(const GraphRequest& request) { return publishGraph(request, false); }
+
+Engine::GraphResult Engine::publishGraph(const GraphRequest& request, bool replan) {
     GraphResult out;
     auto refuse = [&](const std::string& node, const char* code, std::string message) {
         out.errors.push_back({node, "", code, std::move(message)});
@@ -352,6 +383,8 @@ Engine::GraphResult Engine::setGraph(const GraphRequest& request) {
     if (!result.ok) return out;
     out.ok = true;
     hostWidth_ = hostWidthOf(*doc);
+    if (replan) return out;  // a core parked since the document was published is for TS's next one
+    lastRequest_ = request;
     // Each node the document names took its core when it was activated: a core still parked is one no
     // document placed.
     for (auto& [id, core] : parked_) {
@@ -397,6 +430,7 @@ Engine::ScriptPackResult Engine::loadScriptPack(const std::string& id, const std
                                                 std::size_t memoryLimit) {
     ScriptPackResult out;
     rp::host::ScriptHost& script = graph_->script;
+    rp::graph::NodeRegistry& registry = graph_->registry;
     // The id first: a refused one is refused whatever its source says, and costs no compile.
     if (!script.canLoadPack(id, &out.error)) return out;
     std::string why;
@@ -408,16 +442,52 @@ Engine::ScriptPackResult Engine::loadScriptPack(const std::string& id, const std
     const rp::host::ScriptHost::Chunk chunk{*bytecode};
     const rp_package* package = script.loadPack(id, {&chunk, 1}, memoryLimit, &out.error);
     if (!package) return out;
-    // Checked here as every package is: a descriptor the planner could not use, or a package or type id the registry
-    // holds already (a pack named as a native package, or one whose namespace holds another's: "a" and "a.b" can both
-    // name a type "a.b.c"). Refused, the pack goes: nothing placed it.
-    if (!graph_->registry.addPackage(package, &why)) {
-        script.discardPack(package);
-        out.error = "the registry refused the pack's types: " + why;
-        return out;
+    // A pack id canLoadPack takes names no native package (those are the app's, "rp" and under), so one the registry
+    // holds is a pack loaded before, and this is its reload (docs/modular-graph-phase-c1-design.md section 5).
+    if (!registry.hasPackage(id)) {
+        // Checked here as every package is: a descriptor the planner could not use, or a type id the registry holds
+        // already (one whose namespace holds another's: "a" and "a.b" can both name a type "a.b.c"). Refused, the pack
+        // goes: nothing placed it.
+        if (!registry.addPackage(package, &why)) {
+            script.discardPack(package);
+            out.error = "the registry refused the pack's types: " + why;
+            return out;
+        }
+    } else {
+        // The new generation's entries in place of the old one's, checked as an addition is. The old entries stay valid
+        // for the running plan's instances, which no longer match them, so a replan of the document last published
+        // rebuilds the pack's nodes on the new code, under the same node ids, and keeps every other node's instance.
+        // Its tap is carried as it is: a request that does not re-arm leaves the tap the running plan has, neither
+        // restored nor armed again. A document that places none of the pack's nodes would plan as it runs, so it is not
+        // replanned.
+        const rp_package* previous = registry.replacePackage(id, package, &why);
+        if (!previous) {
+            script.discardPack(package);
+            out.error = "the registry refused the pack's types: " + why;
+            return out;
+        }
+        if (lastRequest_ && placesAny(lastRequest_->document, *previous)) {
+            GraphRequest replan = *lastRequest_;
+            replan.rearmTap = false;
+            GraphResult planned = publishGraph(replan, true);
+            if (!planned.ok) {
+                // Nothing has changed: the old entries are found again, the same ones, so the running plan's instances
+                // still match them, and the new generation goes.
+                [[maybe_unused]] const bool restored = registry.restorePackage(previous);
+                assert(restored && "the swap is the package's last: the replan's reclaim drops only older generations");
+                script.discardPack(package);
+                out.error =
+                    "the running document does not plan with the reloaded pack: " + describeErrors(planned.errors);
+                out.errors = std::move(planned.errors);
+                return out;
+            }
+        }
+        // Freed with its runtimes once the plan holding its nodes is handed back and their records come back: at once
+        // when no audio thread runs, as the replan released them there and then.
+        script.retirePack(previous);
     }
     for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
-        const auto* entry = graph_->registry.find(package->node_types[k]->id);
+        const auto* entry = registry.find(package->node_types[k]->id);
         out.types.push_back({entry->descriptor.id, rp::graph::descriptorJson(entry->descriptor)});
     }
     out.ok = true;
@@ -429,6 +499,9 @@ void Engine::setScriptCallDeadline(std::chrono::milliseconds limit) { graph_->sc
 std::vector<rp::host::ScriptHost::PackInfo> Engine::scriptPacks() const { return graph_->script.packs(); }
 
 std::size_t Engine::nodeTypeCount() const { return graph_->registry.size(); }
+std::size_t Engine::retiredNodeTypeCount() const { return graph_->registry.retiredSize(); }
+
+std::uint32_t Engine::scriptInstances() const { return graph_->script.liveInstances(); }
 
 rp::host::DspAllocStats Engine::dspAllocStats() const { return graph_->script.allocStats(); }
 bool Engine::resetDspAllocStats(bool disableAutoGc) { return graph_->script.resetAllocStats(disableAutoGc); }
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 7025bf30..89a62e94 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -233,7 +233,8 @@ public:
     // than 1 to 3 or a system not in the slots, the document holds a second rp.host.midi-in, rp.host.controller-in,
     // rp.host.midi-out or rp.n8.cart-mirror, or its mirror names anything but one of its NES systems. Refused at all,
     // every core stays where it was: one a node of the refused plan took goes back to parked. Once it has published, a
-    // core still parked is one no document placed, and is freed.
+    // core still parked is one no document placed, and is freed, and the request is the one a script pack's reload
+    // replans (loadScriptPack).
     GraphResult setGraph(const GraphRequest& request);
     // The host channels the last published plan writes (the sum of its rp.host.audio-out buses). Control thread.
     std::uint32_t hostWidth() const { return hostWidth_; }
@@ -268,6 +269,17 @@ public:
     // "rp" and under, among them), a compile error (its line), a pack ScriptHost::loadPack refuses (a definition it
     // cannot read, a type id outside the pack's namespace, a load past the call deadline), or one the registry refuses
     // (a descriptor it cannot use, a type id another pack holds already).
+    // An id loaded already is reloaded, all or nothing (docs/modular-graph-phase-c1-design.md section 5): its next
+    // generation is built as a load builds a pack, the registry swaps the pack's entries for its, and the request
+    // setGraph last published is planned again without re-arming the tap (the running plan's stays as it is), so every
+    // node of the pack is rebuilt on the new code, its JS state not carried over, while every other node keeps its
+    // instance; a core parked since that request stays parked. When the replan is refused (a node's settings the new
+    // code refuses, a type the document places gone, the ring busy) the registry is swapped back, the new generation
+    // freed, and the planner's errors are returned in `errors`, summed up in `error`: the old code runs on, its
+    // instances untouched. On success `types` are the new generation's, and the old one is retired, freed with its
+    // runtimes once its nodes' records come back (within the call when no audio thread runs; else by the reclaim that
+    // finds them back, its registry entries dropped with it). A document that places none of the pack's nodes is not
+    // replanned: it would plan as it runs.
     ScriptPackResult loadScriptPack(const std::string& id, const std::string& source, std::size_t memoryLimit = 0);
     // How much of the calling thread's CPU time one call into a script pack's JS may use, on either side
     // (ScriptHost::setCallDeadline): set it before an audio thread runs blocks.
@@ -276,6 +288,11 @@ public:
     std::vector<rp::host::ScriptHost::PackInfo> scriptPacks() const;
     // The node types a document can name: the registry's, native and script.
     std::size_t nodeTypeCount() const;
+    // The registry's entries a reload swapped out whose generation the script host has not yet freed.
+    std::size_t retiredNodeTypeCount() const;
+    // Script node instances whose JS is made and not yet released (ScriptHost::liveInstances): any thread, as
+    // faultCount.
+    std::uint32_t scriptInstances() const;
 
 private:
     struct Graph;
@@ -307,6 +324,9 @@ private:
     // handler: the tail on the block thread, and the control thread's readers. The handler is handed its plan's
     // table instead, and threads that through.
     const rp::host::SystemTable& currentTable() const;
+    // setGraph, or with `replan` a script pack's reload planning the request last published again, which leaves the
+    // parked cores and the request kept as they are.
+    GraphResult publishGraph(const GraphRequest& request, bool replan);
     void applyCommand(DspCommand& c, const rp::host::SystemTable& table);
     void syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table);
     SystemBase* takeCore(std::uint64_t id);
@@ -387,6 +407,8 @@ private:
     // gives its core up again, the plan refused, puts it back in parked_ rather than freeing it.
     bool                                            inSetGraph_ = false;
     std::set<SystemId>                              takenInSetGraph_;
+    // The request setGraph last published (the empty document's, from construction), which a reload replans.
+    std::optional<GraphRequest>                     lastRequest_;
 
     // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
     // liveCores_) as they go.
````

- [ ] **Step 6: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[registry]" && cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[packs]"`
Expected: `All tests passed` for the registry's cases, then for the packs'.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the graph binary's `All tests passed (1894 assertions in 178 test cases)` (two cases more) and the engine-graph binary's `All tests passed (18886 assertions in 140 test cases)` (five cases more; the `[profile]` one is built only in the profile tree).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 154 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm typecheck && tools/run-profile.sh unit`
Expected: `packages/retroplug typecheck: Done`, then `All tests passed (390 assertions in 7 test cases)` (one case more: the span label's).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[packs]"`
Expected: `All tests passed (795 assertions in 15 test cases)`, no ThreadSanitizer report (the prototype ran it three times).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[script]"`
Expected: `All tests passed (1678 assertions in 66 test cases)`, no ThreadSanitizer report.

Run: `cmake --build build-tsan --target retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-graph-test "[registry]"`
Expected: `All tests passed (209 assertions in 5 test cases)`, no ThreadSanitizer report.

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[packs]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[script]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[registry]"`
Expected: `All tests passed (795 assertions in 15 test cases)`, `All tests passed (1678 assertions in 66 test cases)` and `All tests passed (209 assertions in 5 test cases)`, no AddressSanitizer report: freeing a whole generation is what it checks (the prototype's mutation that freed a retired pack with its records out was caught as a heap-use-after-free in `~Instance`).

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/cmake/tests.cmake packages/native/src/graph/NodeRegistry.cpp \
  packages/native/src/graph/NodeRegistry.hpp packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/engine/Engine.hpp packages/native/src/host/script/ScriptHost.cpp \
  packages/native/src/host/script/ScriptHost.hpp packages/native/test/engine-graph/ScriptPacks.test.cpp \
  packages/native/test/graph/Registry.test.cpp
git commit -F - <<'EOF'
feat(engine): a pack reloads while the engine runs: its nodes rebuild on the new code, a refused replan rolls back, and a retired generation is freed off the audio thread

NodeRegistry::replacePackage swaps one package's entries for a new version's, checked as addPackage checks a package
(same package id, a type id another package holds refused): every type of the new version gets a new Entry, so the
planner rebuilds each instance of the package, and the old entries are retired, extracted from the map as node handles
so each stays at its address. restorePackage puts the last swap back, the same Entry objects found again, and
dropRetired drops a retired version. A version is keyed by the rp_package it came from, which a script pack's
generation owns for its life, so whoever frees a version names it by what it frees and no token is kept beside it.

ScriptHost::loadPack on an id loaded builds the next generation beside the current one; retirePack retires the old one,
discardPack a refused new one. A pack counts the records of its types' instances alive (create makes them; the control
thread deletes them), and a retired or discarded pack whose count is 0 is freed on the control thread by the call that
finds it so (retirePack, discardPack, reclaim, setAudioThreadOwns(false)): the last record came back through the
release ring, so the block thread holds nothing of it, and no JS value crosses packs. Its audio runtime first leaves
what every pack shares: its frees are counted apart, as a loaded pack's load is, and a profile build takes what the
generation had added to the live heap off it once quiescent. The pack-freed callback names a retired generation's
package before its types go, and the Engine's registry drops its retired entries there. packs() reports each pack's
retired flag and record count. The built-in pack is never retired.

Engine::loadScriptPack on an id loaded swaps the registry's entries and replans the request setGraph last published
with rearmTap false, so the running tap is neither restored nor armed again and a core parked since stays parked; a
document placing none of the pack's nodes is not replanned. A refused replan swaps the registry back, discards the new
generation and returns the planner's errors (in `errors`, summed up in `error`), the old instances untouched; a
successful one retires the old generation, freed within the call when no audio thread runs.

ScriptPacks.test.cpp: a reload rebuilding a pack's nodes on new code while another pack's counting node and the
built-in controller session keep their instances and state; refused reloads (settings the new code refuses, a type
gone, a type another pack holds, a source that does not load) changing nothing, and a reload adding a placeable type;
fifty quiescent reloads leaving one generation and a steady instance count; a reload and twenty more under an audio
thread, the old generation retired until its record comes back, then freed by a reclaim; the tap carried across a
reload's replan; and, in a profile build, a type's span label and the live heap held across fifty reloads.
Registry.test.cpp: swap, undo by identity, drop, and refused swaps changing nothing.
EOF
````

### Task 5: loadScriptPack over RPC and in the TS Backend, a test:native case, and the docs

Design sections 7 and 10. `EngineRpcService::loadScriptPack` on the `emulator` facet beside `setGraph`, so every host
has it, and `Backend.loadScriptPack` in TS (the mock refuses: it has no JS engine to run a pack in). Nothing in the app
calls it yet. Then the specs, AGENTS.md and the design doc, which gains its Status line, the controller's two decisions,
section 11's questions answered and a "Found while building" section.

**Files:** 21 files, by step below.
- Create: `packages/retroplug/test-native/script-packs.test.ts`
- Modify: 20 files

- [ ] **Step 1: Write the failing test**

A `test:native` file drives packs through the real host, its toy pack a string in the test: a load answers its types;
a pack node between the built-in routing and the host MIDI nodes sends exactly the MIDI its code makes; a reload
changes that node's output while the routing's stays as it was, and a type the reload adds places; the refusals before
any replan (a compile error at its line, a type outside the namespace, `rp.script`, `rp.mine`, a limit that is no
whole number, a limit too small for the load, a top level that throws); and a refused reload returning the planner's
errors with the old code running on. No fixtures, so CI runs it:

Create `packages/retroplug/test-native/script-packs.test.ts`:

````ts
// Script node packs through the real host (docs/modular-graph-phase-c1-design.md section 7): Backend.loadScriptPack
// over the RPC surface every host mounts, into the Engine this host runs. A pack loads and lists its types with their
// descriptors; its node, placed in a document beside the built-in pack's MIDI routing and the host MIDI nodes and
// driven with host MIDI in, sends the MIDI out its code makes; reloaded, that node runs the new code while the
// routing's own output is as it was; and each refusal comes back in the result's shape, changing nothing. The toy packs
// are source strings whose nodes are plain objects in the shape defineNode makes, appended to the global the script
// host reads (packages/retroplug/src/script/register.ts), as the engine-graph tests' toys are
// (packages/native/test/engine-graph/ScriptPacks.test.cpp, which holds packs to their isolation, jobs, memory and
// freeing). No fixtures, so CI runs every case. One Engine serves the file and a pack stays loaded (there is no
// unload), so each case loads ids of its own.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { LoadScriptPackRequest, LoadScriptPackResult } from "../src/backend";
import type { TypeDescriptor } from "../src/graph/descriptor";
import { toGraphJson } from "../src/graph/graphDocument";

const be = createRealBackend();
// No graph of its own: it renders whatever this file last published, which places the host's stereo pair.
const audio = createAudioDriver();

// What every toy pack's source starts with: `toy.define(id, ports, create)` appends a definition whose layout is
// `ports` whatever its settings, and `toy.events(id, dir)` is an events port.
const TOY = `
var toy = (function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  return {
    define: function (id, ports, create) {
      var layout = { ports: ports, params: [] };
      nodes.push({
        id: id,
        descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
        parseSettings: function (raw) { return raw === undefined ? {} : raw; },
        layout: function () { return layout; },
        create: create,
        reshapes: false,
        faultOnThrow: false,
      });
    },
    events: function (id, dir) { return { id: id, dir: dir, kind: "events" }; },
  };
})();
`;

/** A node `id` that sends each message it gets on, its second byte moved by `by`. */
const transposer = (id: string, by: number): string => `
toy.define(${JSON.stringify(id)}, [toy.events("in", "in"), toy.events("out", "out")], function () {
  return { process: function (b) {
    var ms = b.messages("in");
    for (var i = 0; i < ms.length; i++) {
      var d = ms[i].data;
      b.send("out", ms[i].frame, [d[0], d[1] + (${by}), d[2]]);
    }
  } };
});
`;

/** `request` loaded, required to load (its error said when it does not). */
function load(request: LoadScriptPackRequest): LoadScriptPackResult {
  const r = be.loadScriptPack(request);
  expect(r.ok, `loading ${request.id}: ${r.error}`).toBe(true);
  expect(r.error).toBe("");
  expect(r.errors).toEqual([]);
  return r;
}

/** `request` refused before any replan, `why` in its error, with no planner errors and no types. */
function refused(request: LoadScriptPackRequest, why: string): void {
  const r = be.loadScriptPack(request);
  expect(r.ok, request.id).toBe(false);
  expect(r.error.includes(why), `${request.id}: ${JSON.stringify(why)} in ${JSON.stringify(r.error)}`).toBe(true);
  expect(r.errors, request.id).toEqual([]);
  expect(r.types, request.id).toEqual([]);
}

/** Host MIDI through the built-in routing by channel, to systems 1 and 2 (no system is placed: the routing names its
 *  outputs by id), channel 0 through the node `shift` of type `type` and channel 1 straight on (its channel rewritten
 *  to 0, the routing's own work), both to the host MIDI out under their systems, beside the host's stereo pair. */
function place(type: string): void {
  const document = toGraphJson({
    nodes: [
      { id: "midi-in", type: "rp.host.midi-in" },
      { id: "routing", type: "rp.midi.route", settings: { mode: "midiChannelToInstance", systems: [1, 2] } },
      { id: "shift", type },
      { id: "midi-out", type: "rp.host.midi-out", settings: { systems: [1, 2] } },
      { id: "out", type: "rp.host.audio-out", settings: { buses: [1, 1] } },
    ],
    wires: [
      { from: "midi-in.midi", to: "routing.midiIn" },
      { from: "routing.sys-1", to: "shift.in" },
      { from: "shift.out", to: "midi-out.in1" },
      { from: "routing.sys-2", to: "midi-out.in2" },
    ],
  });
  const r = be.setGraph({ document, slots: [] });
  expect(r.ok, `placing ${type}: ${JSON.stringify(r.errors)}`).toBe(true);
}

type Out = { system: number; frame: number; data: number[] };

/** One 256-frame block with `midi` as its host MIDI: the host MIDI out, by system and then frame. */
function drive(midi: { frame: number; bytes: number[] }[]): Out[] {
  const { midiOut } = audio.renderBlocks(2, [{ frames: 256, midi }]);
  return midiOut
    .map((m) => ({ system: m.system, frame: m.frame, data: Array.from(m.data) }))
    .sort((a, b) => a.system - b.system || a.frame - b.frame);
}

// A note on channel 0 (to the pack's node) and one on channel 1 (straight on).
const NOTES = [
  { frame: 3, bytes: [0x90, 60, 100] },
  { frame: 5, bytes: [0x91, 64, 90] },
];

test("a pack loads and lists its types, each with its descriptor", () => {
  const r = load({
    id: "test.list",
    source: TOY + transposer("test.list.up", 12) + transposer("test.list.down", -12),
    memoryLimitBytes: 32 * 1024 * 1024,
  });
  // In the order its source registered them, each descriptor the canonical JSON the registry holds (the name, never
  // given, is the id), so whatever builds documents for the pack knows its ports.
  expect(r.types.map((t) => t.id)).toEqual(["test.list.up", "test.list.down"]);
  for (const t of r.types) {
    const d = JSON.parse(t.descriptor) as TypeDescriptor;
    expect(d.id).toBe(t.id);
    expect(d.name).toBe(t.id);
    expect(d.ports.map((p) => `${p.dir} ${p.kind} ${p.id}`), t.id).toEqual(["in events in", "out events out"]);
  }
});

test("a pack's node runs beside the built-in routing, and the host MIDI out is what its code makes", () => {
  load({ id: "test.shift", source: TOY + transposer("test.shift.up", 12) });
  place("test.shift.up");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  expect(drive([])).toEqual([]);
  expect(drive([{ frame: 0, bytes: [0x80, 61, 0] }])).toEqual([{ system: 1, frame: 0, data: [0x80, 73, 0] }]);
});

test("a reload rebuilds the placed node on the new code, and the routing's output is as it was", () => {
  load({ id: "test.reload", source: TOY + transposer("test.reload.shift", 12) });
  place("test.reload.shift");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  // The same id loaded again is its reload: the engine replans the document it runs, with no setGraph from here, and
  // the new generation may add a type.
  const r = load({
    id: "test.reload",
    source: TOY + transposer("test.reload.shift", 24) + transposer("test.reload.more", 1),
  });
  expect(r.types.map((t) => t.id)).toEqual(["test.reload.shift", "test.reload.more"]);
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 84, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  // And the type it added places.
  place("test.reload.more");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 61, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
});

test("a refusal before any replan says why, with no planner errors, and changes nothing", () => {
  // A compile error, at its line.
  refused(
    { id: "test.syntax", source: "var a = 1;\nvar b = 2;\nvar c = ;\n" },
    "the source does not compile: line 3: SyntaxError: ",
  );
  // A type id outside the pack's namespace.
  refused(
    { id: "test.ns", source: TOY + transposer("test.ns.fine", 1) + transposer("test.other.stray", 2) },
    '(test.other.stray) is outside pack "test.ns": its type ids are "test.ns.<name>"',
  );
  // The built-in pack, and another id that is the app's to give.
  refused(
    { id: "rp.script", source: TOY + transposer("rp.script.up", 1) },
    "rp.script is the built-in pack, which is not reloadable",
  );
  refused({ id: "rp.mine", source: TOY + transposer("rp.mine.up", 1) }, `pack id "rp.mine" is the app's to give`);
  // A memory limit that is no whole number of bytes, refused before the id is looked at.
  refused({ id: "test.limit", source: TOY, memoryLimitBytes: 1.5 }, "memoryLimitBytes must be a whole number of bytes");
  refused({ id: "test.limit", source: TOY, memoryLimitBytes: -1 }, "memoryLimitBytes must be a whole number of bytes");
  // One too small for the load: the limit holds the pack's whole audio runtime, its intrinsics included.
  refused(
    { id: "test.limit", source: TOY + transposer("test.limit.up", 1), memoryLimitBytes: 64 * 1024 },
    "(the load went past the pack's 65536-byte memory limit)",
  );
  // A top level that throws.
  refused({ id: "test.throws", source: TOY + "throw new Error('not today');" }, "chunk 0: Error: not today");

  // None of them loaded anything: a document naming a type one would have defined is refused as naming no type, and
  // each id loads cleanly once its source is good.
  const named = toGraphJson({ nodes: [{ id: "n", type: "test.ns.fine" }], wires: [] });
  const r = be.setGraph({ document: named, slots: [] });
  expect(r.ok).toBe(false);
  expect(r.errors.map((e) => `${e.code} at ${e.nodeId}`)).toEqual(["unknown-type at n"]);
  load({ id: "test.syntax", source: TOY + transposer("test.syntax.up", 1) });
  load({ id: "test.ns", source: TOY + transposer("test.ns.fine", 1) });
  load({ id: "test.limit", source: TOY, memoryLimitBytes: 0 });
});

/** A reload of `id` from `source` refused by its replan of the running document, which places it as node "shift":
 *  the planner's one error, `code` at that node, with `message` in it, summed up in `error`, and no types. */
function replanRefused(id: string, source: string, code: string, message: string): void {
  const r = be.loadScriptPack({ id, source });
  expect(r.ok, id).toBe(false);
  expect(r.types).toEqual([]);
  expect(r.errors.map((e) => `${e.code} at ${e.nodeId}`)).toEqual([`${code} at shift`]);
  expect(r.errors[0].message.includes(message), r.errors[0].message).toBe(true);
  const summary = `the running document does not plan with the reloaded pack: ${code} at shift: `;
  expect(r.error.startsWith(summary), r.error).toBe(true);
}

test("a reload whose replan is refused returns the planner's errors, and the old code runs on", () => {
  load({ id: "test.keep", source: TOY + transposer("test.keep.shift", 12) });
  place("test.keep.shift");
  const before = [
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ];
  expect(drive(NOTES)).toEqual(before);

  // A new generation without the type the document places.
  replanRefused("test.keep", TOY + transposer("test.keep.other", 1), "unknown-type", "test.keep.shift");
  expect(drive(NOTES)).toEqual(before);

  // One whose parseSettings refuses the settings the document gives the node.
  const lenient = "parseSettings: function (raw) { return raw === undefined ? {} : raw; }";
  expect(TOY.includes(lenient)).toBe(true);
  const strict = TOY.replace(lenient, 'parseSettings: function () { throw new Error("no settings will do"); }');
  replanRefused("test.keep", strict + transposer("test.keep.shift", 24), "settings-invalid", "no settings will do");
  expect(drive(NOTES)).toEqual(before);

  // The pack still reloads once its code plans.
  load({ id: "test.keep", source: TOY + transposer("test.keep.shift", -12) });
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 48, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
});
````

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test:native script-packs`
Expected: FAIL: every case fails at its first `loadScriptPack` call, which the backend does not have yet.

- [ ] **Step 3: Mount loadScriptPack on the emulator facet**

The request and result types sit beside `setGraph`'s; `memoryLimitBytes` crosses as an optional double that C++ checks
to be a whole number from 0 to 2^53 - 1 (an integer field would turn 1.5 into 1 and -1 into a huge limit), and `error`
is always a string, empty when `ok`. A load refused by its memory limit says so (the limit holds the whole audio
runtime, its intrinsics included, and QuickJS throws `null` once it cannot make its out-of-memory error):

````diff
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 89a62e94..a82e58ad 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -267,8 +267,8 @@ public:
     // block thread meets it only in a plan that places one of its nodes. Refused, changing nothing, with the reason in
     // `error`: an id ScriptHost::canLoadPack refuses (checked first, before the source is compiled: one of the app's,
     // "rp" and under, among them), a compile error (its line), a pack ScriptHost::loadPack refuses (a definition it
-    // cannot read, a type id outside the pack's namespace, a load past the call deadline), or one the registry refuses
-    // (a descriptor it cannot use, a type id another pack holds already).
+    // cannot read, a type id outside the pack's namespace, a load past the call deadline or the memory limit), or one
+    // the registry refuses (a descriptor it cannot use, a type id another pack holds already).
     // An id loaded already is reloaded, all or nothing (docs/modular-graph-phase-c1-design.md section 5): its next
     // generation is built as a load builds a pack, the registry swaps the pack's entries for its, and the request
     // setGraph last published is planned again without re-arming the tap (the running plan's stays as it is), so every
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index 3e35c120..f4e1a385 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,11 +35,12 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: lifecycle / snapshot reads / live config / input (13) ---
+// --- emulator: lifecycle / script node packs / snapshot reads / live config / input (14) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::buildCore>(e);
     s.template addMethod<&EngineRpcService::setGraph>(e);
+    s.template addMethod<&EngineRpcService::loadScriptPack>(e);
     s.template addMethod<&EngineRpcService::applySystemSetting>(e);
     s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
     s.template addMethod<&EngineRpcService::readState>(e);
@@ -123,7 +124,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (87 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (88 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index 6d3e2c83..ddde23e9 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -134,3 +134,22 @@ struct RpcSetGraphRequest {
 };
 struct RpcGraphError { std::string nodeId; std::string port; std::string code; std::string message; };
 struct RpcSetGraphResult { bool ok; std::vector<RpcGraphError> errors; };
+
+// loadScriptPack (docs/modular-graph-phase-c1-design.md section 7): a pack of script node types, by its id, its source
+// (global code, as Engine::loadScriptPack takes it) and its audio runtime's memory limit in bytes, absent or 0 for the
+// default (64 MiB). The limit is a double so that one which is no whole number of bytes is refused, where an integer
+// field would read it truncated (1.5 as 1, -1 as a limit past any heap). The result is the Engine's: `error` empty on
+// success, `errors` the planner's when a reload's replan refused (empty for a refusal before any replan), and `types`
+// the pack's on success, in registration order, each with its descriptor as the registry holds it.
+struct RpcLoadScriptPackRequest {
+    std::string           id;
+    std::string           source;
+    std::optional<double> memoryLimitBytes;
+};
+struct RpcScriptPackType { std::string id; std::string descriptor; };
+struct RpcLoadScriptPackResult {
+    bool                           ok;
+    std::string                    error;
+    std::vector<RpcGraphError>     errors;
+    std::vector<RpcScriptPackType> types;
+};
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 803c985b..c64e5421 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -145,6 +145,30 @@ RpcSetGraphResult EngineRpcService::setGraph(RpcSetGraphRequest request) {
     return out;
 }
 
+RpcLoadScriptPackResult EngineRpcService::loadScriptPack(RpcLoadScriptPackRequest request) {
+    RpcLoadScriptPackResult out{false, {}, {}, {}};
+    // The limit arrives as the JS number it was (BackendTypes.hpp says why): a whole number of bytes a double holds
+    // exactly, or refused.
+    std::size_t limit = 0;
+    if (request.memoryLimitBytes) {
+        constexpr double kLargest = 9007199254740991.0;  // 2^53 - 1, JS's Number.MAX_SAFE_INTEGER
+        const double bytes = *request.memoryLimitBytes;
+        if (!(bytes >= 0.0 && bytes <= kLargest && std::floor(bytes) == bytes)) {
+            char text[64];
+            std::snprintf(text, sizeof text, "%g", bytes);
+            out.error = std::string("memoryLimitBytes must be a whole number of bytes, 0 for the default: not ") + text;
+            return out;
+        }
+        limit = static_cast<std::size_t>(bytes);
+    }
+    const Engine::ScriptPackResult result = engine_.loadScriptPack(request.id, request.source, limit);
+    out.ok = result.ok;
+    out.error = result.error;
+    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
+    for (const auto& t : result.types) out.types.push_back({t.id, t.descriptor});
+    return out;
+}
+
 // duplicate, reload and remove are TS orchestration (SystemsStore over the registry reads + buildCore, and
 // the documents it publishes): duplicate pulls the source savestate and builds a seeded core; reload pulls
 // SRAM and cold-boots the ROM into the same slot; remove publishes a document without it. Native has no
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 280178f7..b7d34ed6 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -32,6 +32,10 @@ public:
     std::optional<RpcBuiltCore> buildCore(BackendConstructSpec spec);
     // Plan a document TS built (docs/modular-graph-phase-b2-design.md section 2.5).
     RpcSetGraphResult           setGraph(RpcSetGraphRequest request);
+    // Load a pack of script node types, or reload one loaded already (Engine::loadScriptPack,
+    // docs/modular-graph-phase-c1-design.md section 7), while the audio thread runs blocks or not. Refused, changing
+    // nothing, for a memory limit that is no whole number of bytes from 0 to 2^53 - 1 (BackendTypes.hpp).
+    RpcLoadScriptPackResult     loadScriptPack(RpcLoadScriptPackRequest request);
     bool applySystemSetting(std::uint32_t id, std::string key, double value);
     bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
     std::optional<rfl::Bytestring> readState(std::uint32_t id);
diff --git a/packages/native/src/host/script/ScriptHost.cpp b/packages/native/src/host/script/ScriptHost.cpp
index 3b032bac..92882e59 100644
--- a/packages/native/src/host/script/ScriptHost.cpp
+++ b/packages/native/src/host/script/ScriptHost.cpp
@@ -1875,6 +1875,16 @@ bool ScriptHost::loadInto(Pack& pack, std::span<const Chunk> chunks, std::string
         clearException(pack.control);
         clearException(pack.audio);
         if (controlInterrupted_) why = Glue::stopped(*this, "its load") + ": " + why;
+        // A load the memory limit refused an allocation says so: the limit holds the whole audio runtime, its
+        // intrinsics included, and once it cannot even make its out-of-memory error QuickJS throws null, which says
+        // nothing.
+        if (pack.heap.refused) {
+            constexpr std::size_t kMiB = std::size_t{1} << 20;
+            why += " (the load went past the pack's " +
+                   (pack.heap.limit % kMiB == 0 ? std::to_string(pack.heap.limit / kMiB) + " MiB"
+                                                : std::to_string(pack.heap.limit) + "-byte") +
+                   " memory limit)";
+        }
         if (error) *error = std::move(why);
         return false;
     };
diff --git a/packages/native/src/host/script/ScriptHost.hpp b/packages/native/src/host/script/ScriptHost.hpp
index c7a71a2d..762c3809 100644
--- a/packages/native/src/host/script/ScriptHost.hpp
+++ b/packages/native/src/host/script/ScriptHost.hpp
@@ -206,9 +206,11 @@ inline constexpr std::uint32_t kDspSpanLabels = 1024;
 //
 // Each pack's audio runtime is held to a memory limit (kDefaultMemoryLimit, unless its load says otherwise) by the
 // host's allocator, which refuses an allocation past it, so QuickJS throws out of memory in the call that asked, and
-// notes the refusal on the pack: a call, or its jobs, during which its pack refused one faults its node at once (a
-// refusal JS caught counted as the node's throw), since a pack at its limit is one none of its nodes can rely on.
-// Another pack is untouched. The control side's runtimes keep no limit: they hold no instance.
+// notes the refusal on the pack. The limit holds the whole runtime, its intrinsics (about 90 KB) and the pack's loaded
+// code with them, so one too small for those refuses the load, which says so. Once the pack is loaded, a call, or its
+// jobs, during which its pack refused one faults its node at once (a refusal JS caught counted as the node's throw),
+// since a pack at its limit is one none of its nodes can rely on. Another pack is untouched. The control side's
+// runtimes keep no limit: they hold no instance.
 //
 // A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
 // (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch); a
````

- [ ] **Step 4: Add it to the TS Backend**

Its doc comment says a reload refused as `busy` (the command ring full) may be retried, and that `errors` is empty for
a refusal before any replan:

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 37efff72..24489302 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -102,7 +102,8 @@ export interface Backend {
   // looks for a sibling, never classifies. No ROM/SRAM bytes cross for construction:
   // native slurps the paths it's given. Which cores run, in what order and wired how is
   // the graph TS builds (graph/buildGraph.ts); duplicate, reload and remove are TS
-  // orchestration over these two calls (SystemsStore), with no native method of their own.
+  // orchestration over buildCore and setGraph (SystemsStore), with no native method of their own.
+  // loadScriptPack adds script node types beside the built-in ones, for a document to place.
 
   /** Build + activate an emulator core from a spec under the TS-allocated `id` (TS owns the id counter;
    *  native never mints one), and park it for the next graph document to place
@@ -117,6 +118,20 @@ export interface Backend {
    *  refused document changes nothing; its errors name the node at fault. */
   setGraph(request: SetGraphRequest): SetGraphResult;
 
+  /** Load a pack of script node types into the engine, or reload one loaded already, while audio runs or not
+   *  (docs/modular-graph-phase-c1-design.md section 7). `source` is global code, the IIFE a bundler makes, appending
+   *  its `defineNode` results to `__rp_scriptNodes` (script/register.ts) as the built-in bundle does; each type id is
+   *  the pack's id, a dot and a name, and the id is none of the app's ("rp" and every id under "rp."). A load lists the
+   *  pack's types with their descriptor JSON, and a document can place them from the next `setGraph`. A reload
+   *  rebuilds every node of the pack the running document places on the new code, its JS state not carried over, and
+   *  every other node runs on as it was. Refused, nothing changes and the old code runs on, `error` saying why:
+   *  `errors` holds the planner's errors when a reload's replan refused, and is empty when the refusal came before any
+   *  replan (the id, a compile error, the load, the registry). A replan refused `busy` (the engine's command ring
+   *  full, or a settings change not yet adopted) says nothing against the pack: the call may be retried.
+   *  `memoryLimitBytes` holds the pack's audio runtime to that many bytes (absent or 0: 64 MiB). Nothing in the app
+   *  calls it yet; the mock backend refuses it. */
+  loadScriptPack(request: LoadScriptPackRequest): LoadScriptPackResult;
+
   // --- File dialog --------------------------------------------------------
 
   /** Open an OS file browser; resolves to the picked absolute path, or `null` on
@@ -390,7 +405,7 @@ export type HostBackend = Pick<
 /** Emulator lifecycle / live config / input / snapshot reads — bound by the plugin, UI, CLI, test host. */
 export type EmulatorBackend = Pick<
   Backend,
-  | "buildCore" | "setGraph" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
+  | "buildCore" | "setGraph" | "loadScriptPack" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
   | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
 >;
 
@@ -728,6 +743,29 @@ export interface SetGraphResult {
   errors: GraphError[];
 }
 
+/** A pack of script node types for `loadScriptPack`: its id, its source (global code) and its audio runtime's memory
+ *  limit, a whole number of bytes (absent or 0: the default, 64 MiB). */
+export interface LoadScriptPackRequest {
+  id: string;
+  source: string;
+  memoryLimitBytes?: number;
+}
+
+/** One of a pack's node types: its id and its descriptor, the canonical JSON the engine's registry holds. */
+export interface ScriptPackType {
+  id: string;
+  descriptor: string;
+}
+
+/** What `loadScriptPack` did: `error` is empty when `ok`; `errors` are the planner's when a reload's replan refused;
+ *  `types` are the pack's when `ok`, in the order its source registered them. */
+export interface LoadScriptPackResult {
+  ok: boolean;
+  error: string;
+  errors: GraphError[];
+  types: ScriptPackType[];
+}
+
 /** Presentation for an OS file dialog: its title + the glob patterns to show. */
 export interface FileBrowserOpts {
   title: string;
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index dba2a8f1..6e95449b 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -13,7 +13,7 @@
 // recomposes them into the full `Backend`. openFileBrowser is the one async method and rides
 // a UI-direct native hook rather than the RPC bridge (see below).
 
-import type { ApuState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, ControlPlaneBackend, CoreTransportStats, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "./backend";
+import type { ApuState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, LoadScriptPackRequest, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, ControlPlaneBackend, CoreTransportStats, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "./backend";
 import type { OpenSerialPort, SerialClient, SerialPortInfo } from "./n8/transport";
 import { savFromJson as savFromJsonTs } from "./lsdj";
 import { makeCall } from "./rpcClient";
@@ -108,6 +108,7 @@ export function createEmulatorClient(): EmulatorBackend {
   return {
     buildCore: (spec: ConstructSpec, id: number) => (call("buildCore", specParams(spec, id)) as BuiltCore | null) ?? null,
     setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
+    loadScriptPack: (request: LoadScriptPackRequest) => call("loadScriptPack", request) as LoadScriptPackResult,
     applySystemSetting: (id, key, value) =>
       call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
     applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index fd9f5388..69a52a7e 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -4,7 +4,7 @@
 // "disk". This is what lets the whole application layer be tested with `tjs run`
 // and nothing else.
 
-import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, SerialOutCapture, TraceLine, ZipEntry } from "../src/backend";
+import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, SerialOutCapture, TraceLine, ZipEntry } from "../src/backend";
 import { detectPlatform } from "../src/platform";
 import { savFromJson } from "../src/lsdj";
 import type { GraphDocument, NodeDoc } from "../src/graph/graphDocument";
@@ -315,6 +315,13 @@ export class MockBackend implements Backend {
     return { ok: true, errors: [] };
   }
 
+  // Script packs need a JS engine to run their nodes in, which the mock does not have: it refuses every one, before
+  // any replan, as native refuses a pack it cannot load.
+  loadScriptPack(): LoadScriptPackResult {
+    this.log.push("loadScriptPack");
+    return { ok: false, error: "the mock backend runs no script packs", errors: [], types: [] };
+  }
+
   /** The system the build `call` displaced from its slot: the id that held that slot in the document before
    *  the first one placing `call.id`. Undefined for a build that was appended, or never placed. */
   replacedBy(call: { id: number }): number | undefined {
````

- [ ] **Step 5: Run the tests to see them pass**

Run: `pnpm test:native script-packs`
Expected: `# 5 passed, 0 skipped, 0 failed`, then `# 1 native test file(s) passed (jobs=16)`.

Run: `pnpm typecheck && pnpm test`
Expected: `packages/retroplug typecheck: Done`, then `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:node && pnpm test:cli-ts`
Expected: `ℹ pass 12`, then `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: Update the docs**

spec/01 section 6 (a runtime pair per pack), spec/02's RPC table and script host (packs, load and reload, the freeing,
jobs at a checkpoint, the memory limit, the control-side deadline), spec/03's Backend table, spec/04 where it says
what the script runtime is, spec/06's `test:plugin` row, spec/07 section 5 (the item built, what it leaves), spec/08
(jobs no longer in `endBlock`), AGENTS.md (the packs file; seven `[profile]` cases), the modular-graph design's
sections 4.9 and 8, and the C1 design itself:

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 16ef8e92..f0cb54b4 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -41,7 +41,12 @@ The rules below are the parts that don't fit those.
   documents TS builds say; the host I/O nodes and the N8 cart mirror move what the hosts stage and drain;
   TS nodes run behind the C ABI in the script host (B3a,
   [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
-  creation and release on the block thread, throws; the protocol script nodes (B3b: the MIDI routing, a
+  creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
+  built-in one (C1, [docs/modular-graph-phase-c1-design.md](docs/modular-graph-phase-c1-design.md),
+  `test/engine-graph/ScriptPacks.test.cpp`) run in runtimes of their own, what one does to its realm reaching no
+  other pack's node and its jobs and memory limit faulting only its own, and load, reload and free while an audio
+  thread runs blocks, a refused one changing nothing and a reload's replan carrying the running plan's tap as it is;
+  the protocol script nodes (B3b: the MIDI routing, a
   controller session and each role's protocol node, in the documents `buildGraph` builds, read from its role goldens
   and run in its plan order, `test/engine-graph/ProtocolNodes.test.cpp`) send exactly the digests the DSP kernel they
   replaced was recorded sending in the cases it runs, a throw stopping only its own node the one difference those
@@ -58,7 +63,7 @@ The rules below are the parts that don't fit those.
   twenty driven blocks each (the wiring itself is `buildGraph`'s, held by `pnpm test graph` to explicit tables, to the
   B1 translator's goldens and to the role goldens, which `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens`
   re-records). A hidden bench times a script node per block, and the protocol nodes' plan, at 1, 64 and 512 frames
-  (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds six `[profile]` cases
+  (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds seven `[profile]` cases
   (`tools/run-profile.sh unit`).
   `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
   plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 362469ae..c0983976 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1,6 +1,6 @@
 # Design: the modular node-graph engine
 
-**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built (Phases C to E not yet built).
+**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built, and Phase C1 (the rest of Phases C to E not yet built).
 
 RetroPlug's long-term direction is a modular system in the spirit of Reaktor: emulated consoles (and
 the real hardware they can drive) appear as nodes in a graph alongside DSP effects and routing nodes.
@@ -609,7 +609,9 @@ export const lsdjSync = defineNode({
 The host wraps each TS node instance in the C table. One script runtime per engine holds every TS node,
 and each node's `process` calls into it. On the desktop that is B3a's script host
 ([ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp), [B3a §2](modular-graph-phase-b3a-design.md)),
-with a second, control-side runtime that answers `layout_for` and `prepare_settings` and holds no instance.
+with a second, control-side runtime that answers `layout_for` and `prepare_settings` and holds no instance. Since C1
+that pair is per script node pack ([modular-graph-phase-c1-design.md](modular-graph-phase-c1-design.md)): the built-in
+bundle is the first pack, and each pack loaded beside it holds its own nodes in runtimes of its own.
 
 | Platform | Script runtime |
 |---|---|
@@ -1141,6 +1143,11 @@ the per-role graph, and strict parity held bit for bit at every commit.
 
 ### Phase C: nodes own their knowledge
 
+C1 is built ([modular-graph-phase-c1-design.md](modular-graph-phase-c1-design.md)): the script host holds packs, each
+pack of script node types in a QuickJS runtime pair of its own with the built-in bundle the first, a pack loads and
+reloads while audio runs (`loadScriptPack`, on every host), and a node pays for its own promise jobs and its pack's
+memory limit. Strict parity held bit for bit at every commit.
+
 - Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes.
 - `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a preallocated ring (it
   currently allocates on the audio thread).
diff --git a/docs/modular-graph-phase-c1-design.md b/docs/modular-graph-phase-c1-design.md
index 60aaecce..a71eecb6 100644
--- a/docs/modular-graph-phase-c1-design.md
+++ b/docs/modular-graph-phase-c1-design.md
@@ -1,6 +1,7 @@
 # Phase C1: script node packs, each in its own runtimes
 
-**Date:** 2026-10-29 · **Branch:** `feature/modularize` · **Status:** designed
+**Date:** 2026-10-29 · **Branch:** `feature/modularize` · **Status:** built: section 11's five commits; what
+building them changed is in the sections it changed, and what it found is section 13
 
 Every script node an Engine runs shares one QuickJS realm and one promise-job queue, so one node's bug can reach
 another: a getter it hangs on `Object.prototype`, a global it replaces, a promise chain that never settles, a heap it
@@ -61,6 +62,17 @@ The user's, on 2026-10-29:
 - **A pack's promise jobs run after each call into one of its nodes**, charged to that node, and hitting the pack's
   memory limit faults the node whose call hit it (section 6).
 
+Two more, the controller's, taken while building to keep to the user's intent:
+
+- **`rp` and every id under `rp.` are the app's to give** (its built-in pack's and its native packages': `rp.core`,
+  `rp.host` and the rest), so no loaded pack may take one (section 4). Namespacing alone keeps a pack off another's
+  type ids, but a pack named `rp.midi` would hold the namespace of the built-in `rp.midi.route`, and one named after
+  a native package, its types': a type the app adds there later could collide with a user's.
+- **The control thread's calls into a pack are held to the call deadline too**, on a clock of their own (section 4).
+  The design held only the block thread's, but a pack's top level, `parseSettings`, `layout` or descriptor that never
+  returns would hang the control thread that loads the pack or plans a document placing it, which is a buggy node
+  failing everything rather than alone.
+
 ## 3. What a context costs
 
 Measured with a throwaway program (`/tmp/c1meas/meas.cpp`) linked against the build's own `libqjs.a` (Release,
@@ -80,44 +92,74 @@ usable bytes, averaged over 200:
 | A cycle GC of one runtime holding one | | 0.06 ms |
 
 So a context per node costs 140 MB or more at a thousand script nodes, each made on the block thread, and a shared
-heap's GC grows with every node in it. A runtime per pack costs about 300 KB a pack, made off the block thread, and
-each pack's GC covers its own heap.
+heap's GC grows with every node in it. A pack is two runtimes, a control one and an audio one, each with its first
+context and its code loaded: about 180 KB (twice 90: 21 for a runtime, 69 for its first context) and twice what its
+code loads to, so about 590 KB for a pack the size of the production bundle (twice 296), made off the block thread,
+and each pack's GC covers its own heap. The built-in pack's two runtimes are the two the script host had, so it costs
+nothing new.
+
+Measured in the Engine once built (Release, through `test:native`, the same machine): a toy pack of two nodes holds
+about 101 KB in its audio runtime (the allocator's usable bytes) and 107 KB in its control runtime (QuickJS's own
+count), about 210 KB the pair, and `loadScriptPack` takes 0.3 to 0.45 ms from source, its compile included (a reload
+that replans, 0.5 to 0.6 ms). The production bundle's source loaded as a pack, its type ids renamed into the pack's
+namespace, holds about 366 KB in its audio runtime and 391 KB in its control one, 760 KB the pair, loaded in 4.4 to
+5.3 ms: more than twice 296 KB, because a pack compiled from source keeps its debug information (its lines, which a
+thrown error's stack names), where the built-in bundle is compiled stripped (`tjsc -s`).
 
 ## 4. Packs in the script host
 
 `ScriptHost` stops being one pair of runtimes and becomes a holder of packs.
 
-- **A pack** is an id, a generation (its load's count: 1, then 1 more each reload), a control runtime and context, an
-  audio runtime and context, its types, and its own `rp_package` (manifest `{"id": "<pack id>"}`). Everything the
-  audio side holds per runtime today is the pack's: the `Block` class and its frozen prototype, the atoms the block
-  thread names properties by, the address of `Object.prototype`, the interrupt handler (with the host as its opaque).
-  A `Type` names its pack, and an `Instance` reaches its runtime through its type.
+- **A pack** is an id, a generation (its load's count: 1, then one more than the newest of its id the host still
+  holds, section 5), a control runtime and context, an audio runtime and context, its types, and its own
+  `rp_package` (manifest `{"id": "<pack id>"}`, the id written as a JSON string, since a plain id may hold `"` or
+  `\`). Everything the audio side holds per runtime today is the pack's: the `Block` class and its frozen prototype,
+  the atoms the block thread names properties by, the address of `Object.prototype`, the interrupt handler (with the
+  host as its opaque). The audio context's opaque is its pack, through which a `Block` method finds its pack's class
+  and the host. A `Type` names its pack, and an `Instance` reaches its runtime through its type.
 - **The built-in pack** is today's bundle, as pack `rp.script`: the same type ids, the same code, loaded at the Engine's
-  construction as now (`ScriptHost::load` keeps its signature and loads this pack). The binder a test host passes binds
-  into every pack's two contexts.
+  construction as now (`ScriptHost::load` keeps its signature, a defaulted memory limit added, and loads this pack).
+  The binder a test host passes binds into every pack's two contexts.
 - **A loaded pack** (`loadScriptPack`, section 7) is checked before anything is built:
-  1. Its id is a plain id (the registry's `isPlainId`) and not `rp.script`, which is not reloadable.
+  1. Its id is a plain id (the registry's `isPlainId`), not `rp.script`, which is not reloadable, and not otherwise
+     the app's: `rp` and every id under `rp.` (section 2). The id is checked first, before the source is compiled.
   2. Each type id starts with the pack id and a dot (`user.mysynth.osc` in `user.mysynth`), so a pack can never
-     shadow a built-in type or another pack's, and a reload can never collide with anything.
+     shadow a built-in type or another pack's. Where one pack's id runs on into another's (`a` and `a.b` can both
+     name `a.b.c`), the registry refuses the type the second would take, and the load or reload that brings it.
   3. Its source is global code, the IIFE a bundler makes, appending `defineNode` results to `__rp_scriptNodes`
      ([register.ts](../packages/retroplug/src/script/register.ts)), as the built-in bundle does. It is compiled to
      bytecode on the control thread by `dsp::compileToBytecode`
-     ([ScriptCompiler.hpp](../packages/native/src/host/dsp/ScriptCompiler.hpp)), which now returns the compile
-     error's text and position rather than nothing, and run in both of the pack's runtimes by today's load path, with
-     its checks: the same ids and descriptors in both, and one definition that cannot be read refusing the whole pack.
-     Bytecode is locked to one qjs build, so a download or a reload cannot ship it; the built-in pack keeps its
-     embedded bytecode.
+     ([ScriptCompiler.hpp](../packages/native/src/host/dsp/ScriptCompiler.hpp)), which returns the compile error's
+     text and its line (`line 3: SyntaxError: ...`; section 13 says why only the line) rather than nothing, its parser
+     held to the script host's 256 KiB stack rather than QuickJS's 1 MiB, and run in both of the pack's runtimes by
+     today's load path, with its checks: the same ids and descriptors in both, and one definition that cannot be read
+     refusing the whole pack. Bytecode is locked to one qjs build, so a download or a reload cannot ship it; the
+     built-in pack keeps its embedded bytecode.
 - **Shared across packs:** the call deadline's state (one call runs at a time on the block thread); the allocation
   counters, summed over every pack's audio runtime, so `audioAllocations`, the profile's allocation window and the
   zero-allocation benches keep their meaning, "the audio side"; the release rings (a record knows its pack); the
   throw count and first throw; and the profile's span labels, one per type id, given at a type id's first
   registration and kept by its reloads, so the built-in types' labels stay 16 up in bundle order
-  ([spec/08-profiling.md](../spec/08-profiling.md)) and a dev loop's reloads do not use up the name table.
+  ([spec/08-profiling.md](../spec/08-profiling.md)) and a dev loop's reloads do not use up the name table. A loaded
+  pack's audio runtime joins the deadline's handler and the allocation counters only once its load is done: the load
+  runs on the control thread while the block thread may be inside a call into another pack, so a handler polled in the
+  load would touch that call's deadline state, and the load's allocations would be a second writer of counters that
+  have one (section 13). So what a loaded pack's load allocates, and what freeing a generation frees (section 5), are
+  counted apart, never in `audioAllocations`; the built-in pack's load, at construction, comes before any block and is
+  counted. A pack loaded after the profile's allocation window pinned the auto-GC off joins with its own pinned too.
 - **Per pack:** its memory limit (section 6), its promise-job queue, its cycle GC, and its quiescent collection
   (`collect` runs per pack that has garbage).
-
-The built-in pack's memory and per-call cost are today's; each loaded pack adds about 300 KB; each call into a node
-gains one job-pending check (a list-empty test, section 6).
+- **The control thread's calls into a pack** are held to the call deadline's length of its own CPU time, on a clock of
+  its own (section 2), since the block thread may be inside a call into another pack meanwhile: a load is one call
+  (both runtimes' chunks, the jobs they queue, its definitions read), and so is a settings check (`parseSettings` and
+  `layout` with their jobs, for `layout_for`, `prepare_settings` and `create`). A load it stops refuses the pack; a
+  settings check it stops refuses the settings, saying so, so the planner refuses the document and the running plan
+  stands. A pack's control runtime keeps that handler for its life, its audio runtime until it joins.
+
+The built-in pack's memory is today's; each loaded pack adds two runtimes, about 180 KB and twice what its code loads
+to (section 3). Each call into a node gains its checkpoint (a job-pending check when nothing is queued), and each
+allocation and free in an audio runtime a `malloc_usable_size` (section 11's third question): B3a's `[bench]` moved by
+about 4 ns an empty call and 12 one that passes a message on (section 9).
 
 ## 5. Loading, reloading and freeing
 
@@ -125,29 +167,50 @@ All of it runs on the control thread. The block thread enters a pack only throug
 adopted, so a pack is the control thread's until a plan placing one of its nodes is published, and the plan swap's
 ring (release on push, acquire on adopt) carries the whole built pack with it.
 
-- **Load** (a new id): the pack's two runtimes are made, its source compiled and run in both, and its definitions
-  read; then its package is added to the Engine's node registry. A document can place its nodes from then on.
+- **Load** (a new id): the id is checked, the source compiled, the pack's two runtimes made and its code run in both,
+  its definitions read, what the load left in cycles collected, and its audio runtime joined to what packs share
+  (section 4); then its package is added to the Engine's node registry. A document can place its nodes from then on.
+  A package the registry refuses (a descriptor the planner could not use, a type id another pack holds) frees the
+  pack there and then: nothing placed it.
 - **Reload** (an id already loaded), all or nothing, as `setGraph` is:
-  1. The new generation is built as a load builds, beside the old one.
-  2. The registry swaps the package's entries for the new generation's. The old entries stay valid: planned instances
-     point at them (`NodeRegistry` keeps them until told to drop them, rather than never removing an entry).
-  3. The Engine replans its current document (the last request it published, its tap carried as it is). The planner
-     already rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/native/src/graph/Planner.cpp)
+  1. The new generation is built as a load builds, beside the old one. Its number is one more than the newest of its
+     id the host still holds, so a refused reload freed at once uses no number and one waiting for its records does.
+  2. The registry swaps the package's entries for the new generation's (`NodeRegistry::replacePackage`, keyed by each
+     generation's `rp_package`, which the generation owns for its life). The old entries stay valid: planned instances
+     point at them. The registry retires them, each keeping its address, until told to drop them (`dropRetired`),
+     rather than never removing an entry, and can put the same ones back (`restorePackage`). The swap is checked as an
+     addition is, and refused, changing nothing, for a new version naming another package or a type id another
+     package holds.
+  3. The Engine replans its current document: the last request `setGraph` published, with its tap carried as it is
+     (the request re-arms no tap, so its swap carries none: section 11's second question). The planner already
+     rebuilds an instance whose registry entry changed ([Planner.cpp](../packages/native/src/graph/Planner.cpp)
      `reconcile`), so every node of the pack gets a fresh instance of the new code under the same node id, its JS state
-     not carried over, while every other node keeps its instance.
+     not carried over, while every other node keeps its instance. A document that places none of the old
+     generation's types would plan as it runs, so it is not replanned. The replan leaves a core parked since that
+     request parked (TS may have built it for the next document), and the request stays the one `setGraph` published.
   4. A replan that is refused (the new generation rejects a node's settings, or no longer has a type the document
      places) swaps the registry back to the old entries, frees the new generation, and returns the planner's errors.
-     Nothing has changed: the old plan's instances still match the old entries, so none is rebuilt.
-
-  With no document published yet, a reload is steps 1 and 2. A reload may add or remove types; removing one the
-  document still places is refused by step 4.
+     Nothing has changed: the old plan's instances still match the old entries, so none is rebuilt. The new
+     generation is freed there and then while no audio thread runs; under one, the refused plan's fresh instances have
+     releases queued to the block thread, so the generation is marked discarded and freed once their records come
+     back. A replan refused as `busy` (the command ring full, or a settings commit not yet adopted) says nothing
+     against the pack: the caller may try again.
+
+  With no document placing the pack's nodes (the empty one the Engine publishes at construction among them), a
+  reload is steps 1 and 2. A reload may add or remove types; removing one the document still places is refused by
+  step 4.
 - **Freeing a retired generation.** A replaced generation is retired, and its instances go the way every released
   instance goes today: each shim is destroyed when the plan that held it is reclaimed, its JS state freed on the block
   thread at the top of a later block, and its record handed back to the control thread to be deleted. A pack counts its
-  records (the control thread makes and deletes them: a plain count). Once a retired pack's count reaches 0 the block
-  thread holds nothing of it, and the next `reclaim` frees its runtimes and has the registry drop its entries, on the
-  control thread: a whole generation's heap freed off the audio thread, where today's releases free JS state on it.
-  While no audio thread runs, releases happen there and then, so a reload frees the old generation within the call.
+  records (the control thread makes and deletes them: a plain count). Once a retired or discarded pack's count is 0
+  the block thread holds nothing of it, and the call that finds it so (`retirePack` itself, `discardPack`, `reclaim`,
+  or `setAudioThreadOwns(false)`) frees it on the control thread: the pack-freed callback is told a retired
+  generation's package first (the Engine's has the registry drop that package's retired entries), then its audio
+  runtime leaves what packs share (the join's reverse, so its frees are counted apart), then its runtimes go. A whole
+  generation's heap is freed off the audio thread, where today's releases free JS state on it. While no audio thread
+  runs, releases happen there and then, so a reload frees the old generation within the call. A record the release
+  ring back could not take, leaked rather than freed on the audio thread, keeps its pack for the host's life. A
+  profile build takes what a freed generation added to the live heap off the shared count once no audio thread runs.
 - **Not in C1:** unloading a pack (a dev loop reloads, and a pack nothing places costs only its runtimes), and loading a
   pack into more than one Engine: a render job's Engine (or a second one) has the built-in pack only. Loading a project's
   packs into every Engine that runs it belongs with whatever first places user nodes in projects; until then none does.
@@ -155,10 +218,12 @@ ring (release on push, acquire on adopt) carries the whole built pack with it.
 ## 6. A node pays for its own jobs and memory
 
 **Promise jobs** run after the call that queued them, not at the block's end. After every call the block thread makes
-into a node (`create`, `onSettings`, `portsChanged`, `activate`, `process`), the host runs its pack's pending jobs, at
-most 256 (today's `kJobsPerBlock`, now a bound a call), under the same deadline as the call: one deadline spans the call and its jobs. A pack's
-queue is empty between two calls into its nodes, so every job that runs is charged to the node whose call queued it
-(or whose jobs did). `endBlock` no longer runs jobs. No built-in node uses a promise, so parity cannot move.
+into a node (`create`, `onSettings`, `portsChanged`, `activate`, `process`), the host runs its pack's pending jobs at a
+checkpoint, at most 256 (`kJobsPerCheckpoint`, which was `kJobsPerBlock`), under the same deadline as the call: one
+deadline spans the call, the `create` a fresh instance's first call makes before it, and the checkpoint after them. A
+pack's queue is empty between two calls into its nodes, so every job that runs is charged to the node whose call
+queued it (or whose jobs did). `endBlock` no longer runs jobs, and in `process` the checkpoint runs after the node's
+audio outputs are copied out. No built-in node uses a promise, so parity cannot move.
 
 | | Today | C1 |
 |---|---|---|
@@ -172,33 +237,52 @@ until its queue empties: they run at the pack's later checkpoints (the next call
 to the node that left them, so a pack-mate is never charged for them (while they are pending, a pack-mate's own job
 failures are charged to that node too, which errs towards the innocent). A faulted node's chain can still run up to
 256 jobs a checkpoint, since rebuilding or removing the node frees nothing in the runtime its closures live in; a
-reload of its pack, which frees that runtime, ends it. The cost stays bounded and stays in its pack.
+reload of its pack, which frees that runtime, ends it. The cost stays bounded and stays in its pack. Each of those
+checkpoints that cuts the chain short again still counts as the faulted node's throw. Leftovers that a pack-mate's
+checkpoint ran and the deadline stopped count against the node that left them as its own call stopped would: it
+faults at its next block on the second in a row, or the first under `faultOnThrow`; a run not stopped leaves its count
+alone. Once the node that left them is released, they are no node's until the queue empties: a later checkpoint runs
+them charged to no node, never to the pack-mate whose call it follows, and one that cuts them short counts in
+`jobsCutShort` only.
 
 Two more queue a job outside any one node's call, and are charged as follows: a release (`beginBlock` freeing a
-destroyed node's JS) runs its pack's jobs there, charged to no node, since the node is gone; and a
-`FinalizationRegistry` callback is queued by whichever call's GC found its target, so it runs as that call's job. No
-built-in node uses either.
+destroyed node's JS) runs its pack's jobs there, charged to the node that left them when that is another node, else to
+no node, since the node is gone; and a `FinalizationRegistry` callback is queued by whichever call's GC found its
+target, so it runs as that call's job. No built-in node uses either.
 
 **Memory.** Each pack's audio runtime is held to its memory limit (64 MiB unless the load says otherwise): an
-allocation that would pass it is refused, and QuickJS throws out of memory in the call that made it. The host learns
-that its pack refused one (section 11 leaves to the prototype how), and a call (or its jobs) during which its pack
-refused one faults the node at once, whether or not the node caught the error: a pack at its limit is one none of its
-nodes can rely on. Its heap is
-freed when it is rebuilt or removed, or its pack reloaded. Other packs, the built-in one among them, are untouched. The
-control side's runtimes keep no limit: they hold no instance.
+allocation that would pass it is refused, and QuickJS throws out of memory in the call that made it. The host's own
+allocator enforces it (section 11's third question): each pack's audio runtime allocates through an opaque of its
+pack's holding its live bytes and its limit, which refuses an allocation past the limit and notes the refusal on the
+pack. A call (or its jobs) during which its pack refused one faults the node at once, whether or not the node caught
+the error: a pack at its limit is one none of its nodes can rely on. A refusal JS caught is counted as the node's
+throw, saying the node "went past its pack's N MiB memory limit"; an uncaught one is the out-of-memory throw already
+counted. A refusal while a release frees a node's JS, or while the pack loads, faults no node. The limit holds the
+whole runtime, its intrinsics and its loaded code included, so a limit too small for those refuses the load, which
+says so. A node's heap is freed when it is rebuilt or removed, or its pack reloaded. Other packs, the built-in one
+among them, are untouched. The control side's runtimes keep no limit: they hold no instance.
 
 Faults latch per instance as now, and a reload clears them, every instance of the pack being fresh. The first throw's
 logged text already names the node's type, so a dev loop sees which of its nodes broke.
 
 ## 7. The surface
 
-- **The Engine** (control thread): `loadScriptPack(id, source, memoryLimit)` returns `ok`, `error` with the planner's
-  structured errors when it was a reload's replan that refused, and on success `types`: each type's id and descriptor
-  JSON, so whatever builds documents for the pack knows its ports. `memoryLimit` 0 is the default.
-- **RPC:** `EngineRpcService::loadScriptPack` on the `emulator` facet beside `setGraph`, so every host has it (the
-  plugin, SDL, the CLI, the Node addon, the test host): a user developing nodes in a DAW needs a reload there.
-- **TS:** `Backend.loadScriptPack({ id, source, memoryLimitBytes? })` with that result type. The mock backend refuses
-  (`"the mock backend runs no script packs"`): it has no JS engine to run one in.
+- **The Engine** (control thread): `loadScriptPack(id, source, memoryLimit)` returns `ok`; `error`, why not, as text
+  (for a reload's refused replan, "the running document does not plan with the reloaded pack: " and the planner's
+  first error, with how many more); `errors`, the planner's structured errors when it was a reload's replan that
+  refused, empty for a refusal before any replan (the id, the compile, the load, the registry); and on success
+  `types`: each type's id and its descriptor as the registry holds it (canonical JSON, every field spelled), in the
+  order the source registered them, so whatever builds documents for the pack knows its ports. `memoryLimit` 0 is the
+  default.
+- **RPC:** `EngineRpcService::loadScriptPack(RpcLoadScriptPackRequest) -> RpcLoadScriptPackResult`
+  ([BackendTypes.hpp](../packages/native/src/host/rpc/BackendTypes.hpp)) on the `emulator` facet beside `setGraph`,
+  so every host has it (the plugin, SDL, the CLI, the Node addon, the test host): a user developing nodes in a DAW
+  needs a reload there. The limit crosses as a double, `memoryLimitBytes`, refused unless it is a whole number of
+  bytes from 0 to 2^53 - 1: an integer field would read whatever number came, 1.5 as 1 and -1 as a limit past any
+  heap.
+- **TS:** `Backend.loadScriptPack({ id, source, memoryLimitBytes? })` returns `LoadScriptPackResult`, `{ ok, error,
+  errors, types }`, field for field the RPC's (`error` empty when `ok`). The mock backend refuses (`"the mock backend
+  runs no script packs"`): it has no JS engine to run one in.
 - Nothing in the app calls it yet; the authoring project will.
 
 ## 8. A pack's UI half
@@ -226,8 +310,9 @@ bugs), belong with the authoring and package-split work.
   - a second pack's nodes run beside the built-in ones', placed in one document;
   - isolation: one pack hangs a getter on `Object.prototype`, replaces `Array.prototype.push` and a global, and a probe
     node in another pack, reading through each, sees nothing changed;
-  - the refusals: a non-plain id, `rp.script`, a type outside the pack's namespace, a compile error (its text and
-    position), a definition that cannot be read;
+  - the refusals: a non-plain id, `rp.script`, one of the app's ids (`rp`, `rp.mine`, `rp.core`, `rp.midi`), a type
+    outside the pack's namespace, a compile error (its text and line), a definition that cannot be read, a type
+    another pack's namespace holds;
   - reload: the pack's nodes run the new code, a node of another pack (and a built-in one) keeps its instance and its
     JS state across it, a type added by the reload places, and a refused reload (a type the document places removed,
     settings the new code rejects) returns the planner's errors with the old code still running;
@@ -236,16 +321,25 @@ bugs), belong with the authoring and package-split work.
   - jobs: a node's failing `queueMicrotask` callback is its throw and faults it under `faultOnThrow`; a job the deadline
     stops counts as its call stopped; a never-settling chain faults its node after two cut-short checkpoints while a
     pack-mate is never charged; a built-in node's counts stay 0 throughout;
-  - memory: a node that allocates past its pack's limit faults, and nodes of other packs keep running.
+  - memory: a node that allocates past its pack's limit faults, and nodes of other packs keep running;
+  - the control thread's deadline: a load that never returns refused, a settings check that never returns refusing its
+    document while the running plan runs on, its clock its own while an audio thread makes calls into another pack;
+  - a load under an audio thread driving blocks, and a reload under one, its old generation freed by a later reclaim;
+  - a reload's replan carrying the running plan's tap as it is, and a type's span label kept across fifty reloads.
 - **The ThreadSanitizer and AddressSanitizer passes** (`tools/run-sanitizer.sh`) cover reloading under a running audio
   thread.
-- **`test:native`**, a `script-packs` case through the real host: `loadScriptPack`, a document placing a pack node beside
-  the built-in ones, a reload whose node's output changes, and the error shapes.
+- **`test:native`**, a `script-packs` case through the real host
+  ([script-packs.test.ts](../packages/retroplug/test-native/script-packs.test.ts)): `loadScriptPack`, a document
+  placing a pack node beside the built-in ones, a reload whose node's output changes, and the error shapes (a compile
+  error at its line, a type outside the namespace, `rp.script`, an id of the app's, a memory limit that is no whole
+  number or too small for the load, a refused reload with the planner's errors). No fixtures, so CI runs it.
 - **Unchanged and re-run:** strict parity (all 197 cases, the fixtures present, `RP_FAIL_ON_SKIP=1`), with no
   re-record; the four suites; `test:plugin`; the zero-allocation benches.
-- **Measured** and recorded here: a pack's memory and load time in the Engine, and the script node call cost before and
-  after (B3a's `[bench]`, `build/bin/retroplug-engine-graph-test "[bench]"`), expected to differ by about one
-  job-pending check a call.
+- **Measured**: a pack's memory and load time in the Engine (section 3), and the script node call cost before and after
+  (B3a's `[bench]`, `build/bin/retroplug-engine-graph-test "[bench]"`, two runs each, before and after the second
+  commit): an empty pass-through 0.083 to 0.092 us a call before, 0.088 to 0.096 after; one passing a message on 0.178
+  to 0.188 before, 0.193 to 0.202 after; the protocol nodes' plan at 1 frame 0.84 to 0.855 before, 0.872 to 0.898
+  after. The profile's `allocsPerBlock` and `maxBlockAllocs` stay 0 in all three of its windows.
 
 ## 10. Docs
 
@@ -254,7 +348,10 @@ bugs), belong with the authoring and package-split work.
 the node), [spec/04-roles-protocol-nodes.md](../spec/04-roles-protocol-nodes.md) where it says what the script runtime
 is, [spec/07-remaining-work.md](../spec/07-remaining-work.md) section 5 (the item closed, and what section 5 and section
 11 here leave), [defineNode.ts](../packages/retroplug/src/graph/defineNode.ts)'s comment on when promise jobs run, and
-AGENTS.md's description of `retroplug-engine-graph-test`.
+AGENTS.md's description of `retroplug-engine-graph-test`. Writing them found three more: the `test:plugin` row of
+[spec/06-build-test.md](../spec/06-build-test.md), [spec/08-profiling.md](../spec/08-profiling.md) (the audio runtimes
+summed, a type's label across reloads, where the jobs run in the trace), and
+[modular-graph-design.md](modular-graph-design.md) sections 4.9 and 8.
 
 ## 11. Landing
 
@@ -268,16 +365,28 @@ Five commits, each green on its own (the engine-graph tests, `test:plugin`, stri
 5. **The RPC and TS surface, the `test:native` case, and the docs.**
 
 As for B3b and LSDj's serial-out node, a prototype is built first in these five commits on a local branch, and the plan
-is written from it; the prototype is where the questions below are answered.
+is written from it; the prototype is where the questions below are answered (each answer follows its question).
 
 - Whether QuickJS-ng's per-runtime class ids, atoms and interrupt handler need anything beyond moving today's setup
-  into a pack (`JS_NewClassID` takes the runtime).
+  into a pack (`JS_NewClassID` takes the runtime). **Nothing more.** `JS_NewClassID(rt, &id)` takes the next id from the
+  runtime's own counter when `id` is 0, so each pack's `Block` class starts at 0 in its own runtime; atoms and the
+  interrupt handler are each runtime's already. (A test binder that keeps one class id for every pack's probe works
+  only because every pack allocates its classes in the same order, `Block` first.)
 - What the replan's request carries for the SyncTap, so a reload neither drops nor re-arms a tap the running plan has.
+  **No tap at all.** The replan is the request `setGraph` last published with its re-arm cleared, so its swap carries
+  no trailer and the tap code never runs: the armed tap stays as it is. A case arms a bliptoaster NES to mode 1, moves
+  its mode to 2 behind the engine's back, and finds it still 2 after a reload (neither restored nor armed again),
+  then 0 after a later restore request, so the armed record survived.
 - Where the memory limit is enforced: by the host's own allocator, which sees every allocation the pack's runtime
   makes and so knows when it refused one (at the cost of keeping each pack's live bytes in every build, where only a
   profile build keeps the live heap today), or by QuickJS's `JS_SetMemoryLimit`, which checks before the allocator is
-  called, so the refusal has to be read from QuickJS after the call.
-- That the 64 MiB default holds every test bundle's toys.
+  called, so the refusal has to be read from QuickJS after the call. **The host's allocator.** `JS_SetMemoryLimit`
+  refuses inside `js_malloc_rt` before the allocator is called, and QuickJS keeps no record of a refusal JS caught, so
+  there is nothing to read after the call. The cost is one `malloc_usable_size` an allocation and one a free in every
+  build (QuickJS already calls it once each), and the call cost section 9 records.
+- That the 64 MiB default holds every test bundle's toys. **It does**, with room: the largest any engine-graph test's
+  pack reached was 2.55 MB, across 546 packs, and the production bundle's in a whole `test:native` run 0.43 MB (a
+  temporary probe of each pack's peak). Only the toy built to pass a 16 MiB limit came near one.
 
 ## 12. Left as it is
 
@@ -290,3 +399,52 @@ is written from it; the prototype is where the questions below are answered.
   8).
 - **The script nodes' own call cost** (about 0.08 us a node a block empty, B3a) and MicroQuickJS readiness
   (`defineNode` keeps to ES2015's library), both unchanged.
+
+## 13. Found while building
+
+- **A pack joined what packs share too early.** The prototype first gave a pack's audio runtime the block thread's
+  interrupt handler and the shared allocation counters as its runtimes were made, before its load. ThreadSanitizer
+  found a data race in `Glue::interrupt` called from `loadPack` (three runs of three) once a toy's top level ran long
+  enough to reach QuickJS's interrupt poll (300000 iterations), the block thread inside a call into another pack, so
+  the join moved to the load's end (section 4). What the load allocates goes to a counter of its own, so a profile
+  build's absolute live heap leaves out what a live-loaded pack's load left: a constant offset, every delta exact.
+- **A compile error has a line and nothing finer.** QuickJS puts no position on the error object, and the first frame
+  of its stack (`at <script>:L:C`) gives a column that is often 1 whatever the column was, so `compileToBytecode`
+  says `line 3: SyntaxError: ...` (section 4). Its compiled code's name is `<script>`.
+- **Where namespaces meet, the registry is the backstop.** `a` and `a.b` can both name a type `a.b.c`, a pack named
+  `rp.midi` could define the built-in `rp.midi.route`, and one named `rp.core` takes a native package's name. The
+  registry refuses each type already registered, and the pack goes with it; the `rp.` reservation (section 2) then
+  put the app's namespace out of reach before anything is built.
+- **The control thread had no deadline**, so a pack whose top level, `parseSettings`, `layout` or descriptor never
+  returns hung whichever thread loaded it or planned it (section 2). A call the control deadline stops leaves the
+  pack's control runtime usable; a control-side promise chain that never settles is stopped at its drain's deadline,
+  and the rest of it waits in that runtime for the pack's next settings check.
+- **A refused replan's generation cannot always be freed at once.** Under an audio thread the refused plan's fresh
+  instances have their releases queued to the block thread, so the new generation is marked discarded and freed by the
+  call that finds its last record back, with no pack-freed callback: the registry dropped its entries when it put the
+  old ones back (section 5).
+- **A replan must leave the parked cores alone.** A reload between a `buildCore` and the `setGraph` that places its
+  core would otherwise free the core TS had just parked, as a published document frees the parked cores it does not
+  place; so a replan leaves them parked and the request it replans stays the last one `setGraph` published.
+- **A record the release ring back cannot take keeps its pack for the host's life.** It is leaked rather than freed on
+  the audio thread, as before C1, and its pack's count never reaches 0. Left as it is: the ring back is twice the
+  release ring's depth, and nothing has filled it.
+- **The records' count is what makes freeing safe.** Freeing a retired pack with records out (a mutation) is an
+  AddressSanitizer heap-use-after-free in `~Instance`, reading its dropped registry entry. Skipping the counters'
+  detachment when a generation is freed leaves ThreadSanitizer silent, since relaxed atomics lose updates rather than
+  race. And a profile build has to take a freed generation's share off the live heap: fifty reloads leave it within
+  16 bytes of where it was, where without that it rose by 1.67 MB.
+- **A memory limit too small for the runtime itself refuses every load, and QuickJS said `null`.** The limit holds the
+  audio runtime's intrinsics (about 90 KB) and its loaded code, so 64 KiB refuses a two-node toy's load (256 KiB loads
+  it), and once QuickJS cannot make its out-of-memory error it throws `null`: the load said only `chunk 0: null`. The
+  fifth commit has a load the limit refused say so, `(the load went past the pack's 65536-byte memory limit)`, and
+  `test:native` holds it.
+- **A pack compiled from source keeps its debug information.** The built-in bundle is compiled stripped (`tjsc -s`);
+  `compileToBytecode` keeps the lines a thrown error's stack names, which a dev loop wants, so the production bundle's
+  source loaded as a pack holds about 366 KB in its audio runtime, where the stripped bundle loaded to 296 (section
+  3). Whether a downloaded pack's code is compiled stripped is left to the pack-format work.
+- **Running out of memory faults sooner than a throw did.** An allocation refused at the limit in `process`, or while
+  its inputs are made JS values, faults the node at once, where it had been a throw the node ran on from; a refusal
+  while a release frees a node's JS, or while a pack loads, faults no node (section 6).
+- **A plain id can hold `"` or `\`,** so a pack's manifest writes its id as a JSON string; an id `test"q` broke the
+  manifest built by concatenation.
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index a55056ff..fb8256cb 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -89,7 +89,7 @@ goes through the command ring; all audio→control observation goes through the
 
 | | **Control plane** (main / UI thread) | **Audio thread** (DPF `run()` / spawned `audioThread_`) |
 |---|---|---|
-| QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | the script host's bare audio context ([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp), owned by the Engine's graph) |
+| QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | the script host's bare audio runtimes, one per pack ([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp), owned by the Engine's graph) |
 | Owns | the RPC services + their facets, the TS stores and the graph they publish (`EngineGraph`), the editor | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp): the running plan (its system nodes, each owning its core, and its script nodes, the MIDI routing and each role's protocol node among them, whose JS instances live in the script host), **while running** |
 | Does | issues mutation *requests*; plans structure into graph documents (the script nodes' settings checked in the script host's control side); reads snapshots by id; frees replaced plans and the cores only they held | drains the command ring, adopting plans in order; runs the plan each block; publishes snapshots |
 | Never | dereferences a live `SystemBase` while audio runs; frees a core a running plan holds | builds or frees a core or a plan, or blocks |
@@ -338,36 +338,50 @@ This is the teardown counterpart to `drainInto`, which *applies* pending command
 ## 6. The QuickJS runtimes
 
 A host runs one control-plane runtime, and every `Engine` it holds has a **script host** of its own
-with two more ([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp),
-[modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 2). (A UI
+with two more for each **script node pack** it holds: the built-in one, and any loaded beside it
+([`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp),
+[modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md) section 2,
+[modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)). (A UI
 render job adds a bare QuickJS `RenderHost` with an Engine, so a script host, of its own,
 [11-ui-rendering.md](11-ui-rendering.md); the Node-API host runs its control plane in V8.) None is
 shared, and no JS value passes between any two:
 
-| | **Control-plane runtime** | **Script host: control side** | **Script host: audio side** |
+| | **Control-plane runtime** | **A pack's control side** | **A pack's audio side** |
 |---|---|---|---|
 | Type | txiki full host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | bare QuickJS, no txiki | bare QuickJS, no txiki |
-| Owner | the host (plugin / test host / UI harness) | the Engine's graph | the Engine's graph |
-| Runs | the TS control-plane bundle: stores, `Backend` adapter, `__rp_*` globals, editor | each script type's `parseSettings` and `layout`, for the planner's `layout_for` and `prepare_settings`; it never holds an instance | every script node instance (its `create`, `process`, `onSettings`, `portsChanged`, `activate`): the MIDI routing, the controller session and each role's protocol node |
-| Thread | control plane only | the thread that plans (`setGraph`) | whichever thread drives the block |
+| Owner | the host (plugin / test host / UI harness) | the Engine's script host, one per pack | the Engine's script host, one per pack |
+| Runs | the TS control-plane bundle: stores, `Backend` adapter, `__rp_*` globals, editor | its pack's types' `parseSettings` and `layout`, for the planner's `layout_for` and `prepare_settings`; it never holds an instance | every instance of its pack's types (its `create`, `process`, `onSettings`, `portsChanged`, `activate`): for the built-in pack, the MIDI routing, the controller session and each role's protocol node |
+| Thread | control plane only | the thread that plans (`setGraph`) and loads packs (`loadScriptPack`) | whichever thread drives the block, once its pack is loaded (the load runs on the control thread) |
 | Data | full JS objects over `__rpcSend` | settings JSON in, layout JSON out | the plan's queues and control streams, and copies of its audio buffers, through one reused `Block` object per instance; a JSValue never crosses out |
 
-Both script runtimes are loaded at the Engine's construction with the same script-node bundle
-([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)), compiled to bytecode at build time and
-embedded in `retroplug-backend`, so every host's Engines have the same node types with no boot call.
-Each script type is a node type of the package `rp.script`, and each script node of a plan is a
-native shim the planner creates, activates and destroys like any node; its JS instance is made on the
-block thread when its plan is adopted, before that plan's first `process`, and released there at the
-start of a later block once the shim is destroyed (at once while no audio thread runs). The
-production bundle's eight are the MIDI routing, a protocol node for each role that has one (mGB, the N8
-FIFO, risa, smsggdj and LSDj sync), the controller session and LSDj's serial-out decoder: each takes its
-settings from the document, and its inputs and outputs are real wires from the host I/O nodes and to each
-system node's inputs, or, for the decoder, from its Game Boy's output. Native owns no role logic; in
-particular, **the drift-exact PPQ tick clock lives entirely in JS** (`walkTicks`). The nodes and the role
+The built-in pack, `rp.script`, is the first: both its runtimes are loaded at the Engine's construction
+with the script-node bundle ([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)), compiled to
+bytecode at build time and embedded in `retroplug-backend`, so every host's Engines have the same node
+types with no boot call. Each of a pack's script types is a node type of the package named by the
+pack's id, and each script node of a plan is a native shim the planner creates, activates and destroys
+like any node; its JS instance is made on the block thread when its plan is adopted, before that plan's
+first `process`, and released there at the start of a later block once the shim is destroyed (at once
+while no audio thread runs). The production bundle's eight are the MIDI routing, a protocol node for each
+role that has one (mGB, the N8 FIFO, risa, smsggdj and LSDj sync), the controller session and LSDj's
+serial-out decoder: each takes its settings from the document, and its inputs and outputs are real
+wires from the host I/O nodes and to each system node's inputs, or, for the decoder, from its Game Boy's
+output. Native owns no role logic; in particular, **the drift-exact PPQ tick clock lives entirely in JS**
+(`walkTicks`). The nodes and the role
 model are specified in [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md); the runtime under them in
 [02-native-host.md](02-native-host.md).
 
-The audio side runs on **both** threads over its lifetime (the control thread on the pull-path
+More packs load at runtime, while audio runs or not: `loadScriptPack`, on the `emulator` facet every host
+mounts, compiles a pack's source on the control thread and loads it into two runtimes made for it, its type
+ids under its own id (never `rp` or an id under it, which are the app's), and its types join the registry for
+the next document to place. Loaded again, a pack reloads: the running document is planned again, so its
+nodes rebuild on the new code while every other node keeps its instance, all or nothing, and the old
+generation's runtimes are freed on the control thread once the block thread holds nothing of it. A pack
+isolates its nodes from every other pack's (a prototype or a global one patches, a heap it fills) but not from
+each other; its promise jobs run after each call into one of its nodes, charged to that node, and its audio
+runtime is held to a memory limit (64 MiB by default). Each loaded pack costs two runtimes, about 180 KB and
+twice what its code loads to. Nothing in the app loads one yet.
+
+A pack's audio side runs on **both** threads over its lifetime (the control thread on the pull-path
 `renderAudio`, the audio thread once running), so every entry re-anchors QuickJS's stack-overflow
 guard to the current thread with `JS_UpdateStackTop` before entering JS, or a call from a
 different stack throws a spurious overflow. The control side does the same.
@@ -419,14 +433,16 @@ lost mutation.
    there by the planner. A replaced plan goes back through the release ring and is destroyed, with
    any core only it held, on the control thread. Adopting a plan inside a block is a pointer swap
    and the calls its plan carries (settings commits, `ports_changed`). The block thread does allocate
-   and free in the script host's audio runtime: a script node's JS instance is created there when its
-   plan is adopted (and a commit's settings are parsed into JS for its `onSettings`), and released at
-   the start of a later block once its shim is destroyed (16 a block at most). The native record
+   and free in the script host's audio runtimes: a script node's JS instance is created in its pack's
+   when its plan is adopted (and a commit's settings are parsed into JS for its `onSettings`), and
+   released at the start of a later block once its shim is destroyed (16 a block at most). The native record
    behind a released instance goes back to the control thread to be deleted (`ScriptHost::reclaim`),
    as do the settings a commit replaced, and what released instances leave in cycles is collected
-   while no block runs (a quiescent reclaim, or the audio thread handing the runtime back). A heavy
-   `create` is therefore an xrun risk at a swap, accepted as the price of one runtime that only the
-   block thread enters.
+   while no block runs (a quiescent reclaim, or the audio thread handing the runtimes back). A heavy
+   `create` is therefore an xrun risk at a swap, accepted as the price of runtimes that only the
+   block thread enters. A pack's runtimes themselves are made, loaded and freed on the control thread:
+   a pack loaded, or a reload's new generation, before any plan places one of its nodes, and a
+   generation a reload retired once no record of its nodes is alive.
 
 5. **Leak before you block or free in the render loop.** A full release ring leaks a plan (and the
    cores only it held) rather than free it on the audio thread: real-time safety is never traded for
@@ -437,14 +453,14 @@ lost mutation.
    both on the control thread, both outside the window in which the audio thread publishes to that
    slot. `claim` publishes the slot id last; `release` clears it first.
 
-7. **A JSValue never crosses out of the script host's audio runtime, and no plan memory crosses in.**
+7. **A JSValue never crosses out of a pack's audio runtime, and no plan memory crosses in.**
    A script node reaches the plan only through its shim: its input messages are copied into JS-owned
    buffers, its audio ports are JS-owned copies (an input's samples copied in before `process`, an
    output's copied out after it), and what it sends is written straight into the plan's output queues.
    So nothing a node keeps, transfers or edits can reach a buffer another node reads or a plan the
    control thread has freed. A protocol node's sends are its output ports. The runtime is separate
-   from the control-plane runtime and from the script host's control side, and is re-anchored to the
-   calling thread on every entry.
+   from the control-plane runtime, from its pack's control side and from every other pack's runtimes,
+   and is re-anchored to the calling thread on every entry.
 
 8. **TypeScript owns identity, orchestration and structure; native never mints or decides.** System
    ids are allocated by TS and passed into `buildCore`; native returns what it built, never an id
@@ -477,8 +493,8 @@ lost mutation.
 | [`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts) / [`engineGraph.ts`](../packages/retroplug/src/engineGraph.ts) | the engine's graph as TS builds it from the stores, and its publisher (the `ProjectStore`'s, or a `DirectGraph`'s or bare `SystemsStore`'s own) |
 | [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) / [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) / [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp) | the cores as nodes (each owning its core), the host I/O nodes and the cart mirror, and the host services they ask for |
 | [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishSystem/read/release |
-| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), the staged host MIDI, the host outputs and the registry; `parkCore` / `setGraph`, `processBlock`, the command handler, `SyncTap` |
-| [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in two bare QuickJS contexts, and the audio runtime's profiling |
+| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph (with the script host its TS nodes run in), the staged host MIDI, the host outputs and the registry; `parkCore` / `setGraph` / `loadScriptPack`, `processBlock`, the command handler, `SyncTap` |
+| [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp) | the script-node runtime: TS nodes behind the C ABI in packs, each two bare QuickJS runtimes of its own (the built-in pack first, more loaded and reloaded at runtime), and the audio runtimes' profiling |
 | [`nodes/`](../packages/retroplug/src/script/nodes) / [`bundle.ts`](../packages/retroplug/src/script/bundle.ts) | the script nodes buildGraph places (the MIDI routing, each role's protocol node, the controller session), and the script-node bundle every Engine embeds |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 5937977f..3959d032 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -129,7 +129,8 @@ does.
 ### `EngineRpcService`: emulator / MIDI / transport
 
 Every mutation here routes through the `QueuedInvoker` except these, which call the `Engine`
-directly on the control thread: `buildCore` and `setGraph` (`Engine::parkCore` / `Engine::setGraph`),
+directly on the control thread: `buildCore`, `setGraph` and `loadScriptPack` (`Engine::parkCore` /
+`Engine::setGraph` / `Engine::loadScriptPack`),
 `setSampleRate` (`Engine::setSampleRate`, so it refuses while the audio thread owns the engine or the
 running plan holds a system), the render calls, and the profile calls `dspResetAllocStats`,
 `dspRunGc` and `dspTraceReset` (which the script host refuses while the audio thread owns its
@@ -142,6 +143,7 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 |---|---|---|
 | `buildCore` | `(BackendConstructSpec) → optional<RpcBuiltCore>` | TS-owned `spec.id`; `factory_.build` → `Engine::parkCore`, which claims the registry slot. Returns the core's node type, export mode and link group, or null (below) |
 | `setGraph` | `(RpcSetGraphRequest) → RpcSetGraphResult` | a graph document TS built, its slots and its tap re-arm → `Engine::setGraph` (below) |
+| `loadScriptPack` | `(RpcLoadScriptPackRequest) → RpcLoadScriptPackResult` | a pack of script node types, by its id, its source and its audio runtime's memory limit (`memoryLimitBytes`, a double, absent or 0 for 64 MiB, refused unless a whole number of bytes) → `Engine::loadScriptPack`, which loads it or, for an id loaded already, reloads it (the script host, below); returns `ok`, `error` (empty on success), the planner's `errors` when a reload's replan refused (empty for a refusal before any replan) and the pack's `types`, each with its descriptor JSON |
 | `applySystemSetting` | `(uint32 id, string key, double value) → bool` | `gainDb` / `reloadOnRomChange` → a `ConfigField` |
 | `applyRoleConfig` | `(uint32 id, string kind, string config) → bool` | `kind` `"sameboy"` or `"mesen"`, false for any other; decodes the whole role config and pushes each field as a `ConfigField` (sameboy: Model/Highpass/LinkGroup/FastBoot/ColorCorrection/DmgPalette/LightTemperature; mesen: NesRegion/NesRemoveSpriteLimit/NesApuLatencyMs/NesS5bNoise/NesMmc5PhaseReset/NesExpansionVolume) |
 | `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry |
@@ -157,10 +159,10 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 
 The table is a selection. The facets
 ([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 19
-`host` methods, 13 `emulator`, 22 `harness` (the renders, transport and sample rate, staged MIDI in and
+`host` methods, 14 `emulator`, 22 `harness` (the renders, transport and sample rate, staged MIDI in and
 MIDI out, `droppedMessages`, `buildTarget`, the kit compiles and the audio runtime's profiling calls),
 27 `debug` and
-6 `driver`: 87 in the union. There is no call for what the roles do on the audio thread: their protocol
+6 `driver`: 88 in the union. There is no call for what the roles do on the audio thread: their protocol
 nodes are script node types every Engine embeds, and each node's settings ride the documents `setGraph`
 plans.
 
@@ -227,7 +229,8 @@ block, so staging allocates nothing until a block stages more), the host outputs
 snapshot registry, the transport fields, and the graph the systems run on: the `ScriptHost` its script
 nodes (the MIDI routing, the controller session and each role's protocol node) run in, a `NodeRegistry`
 holding the node packages (`rp.core`,
-`rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`), a `ResourceTable`, the
+`rp.host`, `rp.sameboy`, `rp.mesen`, `rp.host.io`, `rp.n8`, `rp.script`, and each script node pack
+`loadScriptPack` loaded), a `ResourceTable`, the
 [`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, the cores `buildCore`
 parked until a document places them, and every live core by id. Every op runs "now, on the calling
 thread"; it holds **no locks**: transport and the ppq clock are plain members mutated only by
@@ -283,10 +286,10 @@ staging). Then, in order:
    5. **The mixing nodes**, `rp.core.split` and `rp.core.add`: the routing, as wiring (below).
    6. **`rp.host.audio-out`**, which copies its buses into the host's buffers (it reaches them
       through `rp_host.get_interface("rp.host.audio-buffers/1")`).
-4. `ScriptHost::endBlock()`: the promise jobs the block's script nodes queued, at most 256 (the rest
-   wait for the next block's end), then, in a profile build, the allocation window's block counts; a
-   profile build closes the block's `apu-render` span, the plan's whole run with the script nodes' spans
-   inside it, just before ([08-profiling.md](08-profiling.md)).
+4. `ScriptHost::endBlock()`: in a profile build, the allocation window's block counts (the script
+   nodes' promise jobs ran at each call's checkpoint, inside the plan's run); a profile build closes the
+   block's `apu-render` span, the plan's whole run with the script nodes' spans inside it, just before
+   ([08-profiling.md](08-profiling.md)).
    What was staged and no node of the plan took (a plan with no `rp.host.midi-in` or `controller-in`)
    is dropped, not carried into the next block.
 5. `registry_.publishSystem(...)` for each system of the plan, in slot order: copy its
@@ -645,23 +648,55 @@ protocol node) but understands nothing about roles. The nodes and what they do a
 ([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)); this doc covers the C++ runtime under them
 (docs/modular-graph-phase-b3a-design.md section 2).
 
-[`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp) owns **two bare QuickJS
-runtimes** (no txiki; see [01-architecture.md](01-architecture.md)), one pair per Engine, sharing no
-JS value: a control-side one that only checks settings and lays out a node's ports, and the
-audio-side one that holds every instance and runs `process`.
+[`ScriptHost`](../packages/native/src/host/script/ScriptHost.hpp) holds **packs**, the unit it
+isolates, loads and replaces ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)).
+A pack has an id, a bundle that registers its node definitions, a package of its own named by its id, and
+**two bare QuickJS runtimes** of its own (no txiki; see [01-architecture.md](01-architecture.md)), sharing
+no JS value with each other or with another pack's: a control-side one that only checks settings and lays
+out a node's ports, and an audio-side one that holds every instance of the pack's types and runs `process`.
 
-- **The bundle.** Both run the script-node bundle
+- **The built-in pack.** The first pack, `rp.script`, runs the script-node bundle
   ([`bundle.ts`](../packages/retroplug/src/script/bundle.ts)), esbuilt to a global-code IIFE and
   compiled to bytecode at build time by the `retroplug-script-bundle` target
   ([bundles.cmake](../packages/native/cmake/bundles.cmake)), whose derived C array is compiled into
   `retroplug-backend`: every Engine loads it at construction, so every host has the same node types
-  with no boot call. The bundle appends its `defineNode` results to the global `__rp_scriptNodes`
-  ([register.ts](../packages/retroplug/src/script/register.ts)); the host reads that list in both
-  runtimes, requires the same id and descriptor at each index, and registers one `rp_node_type` per
-  definition in the package `rp.script`, all served by the same functions, each type telling itself
-  apart by its `ctx`. A bundle that does not load, or one definition that cannot be read, registers no
-  type, logged, and every document naming one is refused (`unknown-type`); a second load is refused.
-  The native tests build an Engine with toy bundles of their own instead.
+  with no boot call. A bundle appends its `defineNode` results to the global `__rp_scriptNodes`
+  ([register.ts](../packages/retroplug/src/script/register.ts)); the host reads that list in both of the
+  pack's runtimes, requires the same id and descriptor at each index, and registers one `rp_node_type` per
+  definition in the pack's package, all served by the same functions, each type telling itself apart by
+  its `ctx`. A bundle that does not load, or one definition that cannot be read, registers no type, logged,
+  and every document naming one is refused (`unknown-type`); the built-in pack is loaded once, and never
+  reloaded. The native tests build an Engine with toy bundles of their own instead.
+- **Loading a pack** (`Engine::loadScriptPack`, the `loadScriptPack` RPC above) runs on the control thread,
+  while an audio thread runs blocks or not. The id is checked first: a plain id, and none of the app's (`rp`
+  and every id under `rp.`, its built-in pack's and its native packages'). The source, global code as the
+  built-in bundle is, is compiled by `ScriptCompiler` (below) and run in two runtimes made for the pack, under
+  the checks the built-in pack's load makes and one more: every type id is the pack's id, a dot and a name
+  (`user.synth.osc` in `user.synth`). Then the pack's package joins the registry, which refuses a type id
+  another package holds (one pack's id can run on into another's, `a` and `a.b`), a refusal that frees the
+  pack. A document can place its nodes from the next `setGraph`. The block thread first meets a pack through
+  an instance in a plan it adopts, so the plan swap's ring carries the whole built pack across; a pack's audio
+  runtime joins what packs share (the call deadline's interrupt handler, the allocation counters) only once
+  its load is done, so what a loaded pack's load allocates is counted apart from `audioAllocations`.
+- **Reloading one.** The same id loaded again builds the pack's next generation beside the current one (one
+  more than the newest of its id still held), and the registry swaps the package's entries for the new
+  generation's, keeping the old ones valid for the instances that hold them. The Engine plans the request
+  `setGraph` last published again, with no tap re-arm (the running plan's tap stays as it is, neither
+  restored nor armed again) and its parked cores left parked; the planner rebuilds every instance whose
+  registry entry changed, so each of the pack's nodes gets a fresh instance of the new code under its node
+  id, its JS state not carried over, and every other node keeps its instance. A document that places none
+  of the pack's nodes is not planned again. All or nothing: a replan the planner refuses (a node's settings
+  the new code refuses, a type the document places gone, or `busy`) puts the old entries back and discards
+  the new generation, and the result carries the planner's errors, the old code running on.
+- **Freeing a generation.** A pack counts the records of its types' instances alive (made and deleted on the
+  control thread, so a plain count). A generation a reload retired, or one a refused replan discarded, is
+  freed by the control-thread call that finds its count 0 (`retirePack`, `discardPack`, `reclaim`,
+  `setAudioThreadOwns(false)`): the registry drops a retired one's entries first (the pack-freed callback),
+  then its audio runtime leaves what packs share, so its frees are counted apart, then its runtimes go. So a
+  whole generation's heap is freed off the audio thread, while it runs blocks in other packs: the block
+  thread holds nothing of a pack none of whose records is alive. With no audio thread running, a reload frees
+  the old generation within the call. A record the release ring back could not take, leaked rather than
+  freed on the audio thread, keeps its pack for the host's life.
 - **Settings and layout** (control side, the planning thread). `layout_for` and `prepare_settings` run
   the definition's `parseSettings` and its `layout` on the settings alone, so the planner can check a
   whole document before it creates anything. Every script type declares `RP_TYPE_STATELESS_PREPARE`,
@@ -682,9 +717,10 @@ audio-side one that holds every instance and runs `process`.
   queued on a 256-slot SPSC ring (a control-side backlog behind it) that `beginBlock` drains at the
   top of later blocks, 16 a block, freeing the JS state on the block thread, and the record comes back
   on a second ring for `reclaim` (control thread) to delete, so the audio thread frees nothing native.
-  What released instances, and the bundle's load, leave in cycles is collected while no block runs: at
-  the reclaim that ends a quiescent operation (the Engine's after every `setGraph`) and when the audio
-  thread hands the runtime back.
+  What released instances, and the built-in bundle's load, leave in cycles is collected while no block
+  runs, by a GC of each pack's audio runtime that has some: at the reclaim that ends a quiescent operation
+  (the Engine's after every `setGraph`) and when the audio thread hands the runtimes back. A loaded pack's
+  load collects its own before it returns.
 - **The block.** A node's `process` gets one `Block` object, the same every block: `messages(port)` a
   reused array whose payloads are copied into one JS-owned buffer per port, `control(id)` a reused
   `{ start, changes }`, `audio(port)` a reused `Float32Array[]` over the instance's own JS-owned copy of
@@ -695,9 +731,27 @@ audio-side one that holds every instance and runs `process`.
   or writes can reach a buffer another node reads or a plan the control thread frees. Ports are found
   through atoms made when the instance or its new layout is bound, so a warm block allocates nothing
   native, and a node written with indexed loops nothing in JS either. So **a `JSValue` never crosses
-  out**. The promise jobs a block's JS queues run at its end (`endBlock`), at most 256. The
-  **drift-exact PPQ tick clock lives entirely in JS** (`walkTicks`, which each clocking protocol node
-  runs); native passes only the block's transport in.
+  out**. The **drift-exact PPQ tick clock lives entirely in JS** (`walkTicks`, which each clocking
+  protocol node runs); native passes only the block's transport in.
+- **Promise jobs** run at a checkpoint right after each call the block thread makes into a node (its
+  `create`, `onSettings`, `portsChanged`, `activate` or `process`, after the audio outputs are copied
+  out), at most 256 (`kJobsPerCheckpoint`), inside the call's deadline, and are charged to that node: a
+  job that fails (a `queueMicrotask` callback that threw, or JS out of memory) is its throw, and faults
+  it under `faultOnThrow`; one the deadline stops counts as its call stopped; and a chain still pending
+  after the bound is cut short, its throw (`jobsCutShort` counts it), the second in a row faulting it.
+  QuickJS cannot drop a pending job, so a chain cut short stays queued, and its pack remembers whose it
+  is until the queue empties: its later checkpoints (the next calls into any of the pack's nodes) run the
+  rest charged to that node, never to a pack-mate, and once that node is released the rest is no node's.
+  A release runs what its pack has pending, charged likewise. Only a reload, which frees the pack's
+  runtime, ends a faulted node's chain; it runs, bounded, in its own pack.
+- **Memory.** Each pack's audio runtime is held to a memory limit (64 MiB unless its load says
+  otherwise) by the host's allocator, which refuses an allocation past it, so QuickJS throws out of
+  memory in the call that asked, and notes the refusal on the pack. A call, or its jobs, during which
+  its pack refused one faults its node at once (a refusal JS caught counted as the node's throw), since
+  a pack at its limit is one none of its nodes can rely on; a refusal during a release or a load faults
+  no node. The limit holds the whole runtime, its intrinsics (about 90 KB) and its code included, so a
+  limit too small for those refuses the load, which says so. Another pack is untouched, and the control
+  side's runtimes keep no limit: they hold no instance.
 - **A throw** in `process` is caught and counted, the block's output up to it stands, and the node runs
   again next block; a definition that says `faultOnThrow`, or a `process` that returns `"fault"`,
   faults instead (bypassed from then on), and a `faultOnThrow` node whose `onSettings`,
@@ -709,8 +763,8 @@ audio-side one that holds every instance and runs `process`.
   fails (a setter the node hung there threw or ran past the deadline, or it froze the object) is the
   node's throw too: `messages()` and `control()` throw it, and on its audio views, the `Block` or its
   transport, written before `process` is called, it is a throw in `process` whose block does not run.
-  Every node shares one realm, so the host keeps its writes, and its reads of a node's methods, off the
-  shared prototypes: each field it fills is the object's own data property from when it makes the object
+  Every node of a pack shares its realm, so the host keeps its writes, and its reads of a node's methods,
+  off the prototypes they share: each field it fills is the object's own data property from when it makes the object
   (a define, which never walks the prototype chain), and each array slot is set when it is the array's
   own at that write and defined when it is not (never made, or taken back by a shift, a splice or a
   shorter length). So only the node's own setters run there, never an accessor another node (or the
@@ -722,26 +776,35 @@ audio-side one that holds every instance and runs `process`.
   (its `get` trap, or its target's chain), so a wrapper or a test double works; a chain longer than
   `ScriptHost::kMethodHops` (1024) plain objects, or one that cycles back through a Proxy, fails the
   `create` rather than holding the block thread. The `Block` prototype every node reaches is frozen with
-  its methods, so no node replaces one for another: assigning one on a node's own `Block` (`block.send = f`) throws in the strict bundle,
-  where `Object.defineProperty` on it still works. That is the host's part only: what nodes can do to
-  each other through the shared realm (an accessor on a prototype another's own code reads through, a
-  global it calls) is left to one context per node ([07-remaining-work.md](07-remaining-work.md)).
-- **The call deadline.** Every call the block thread makes into a node's JS (and `endBlock`'s promise
-  jobs, together) is held to a deadline of the calling thread's CPU time (2 s by default,
-  `setCallDeadline`), so a stall that is not the node's own never trips it. A call that runs past it is
-  stopped and counted as a throw: a `faultOnThrow` node faults, any other runs again and faults only if
-  its next call is stopped too. Both runtimes' JS stacks are held to 256 KiB, under a macOS secondary
-  thread's 512 KiB.
+  its methods, so no node replaces one for another: assigning one on a node's own `Block`
+  (`block.send = f`) throws in the strict bundle, where `Object.defineProperty` on it still works. That
+  is the host's part only, and it guards against the nodes of one pack: what they can do to each other
+  through the realm they share (an accessor on a prototype another's own code reads through, a global it
+  calls) is left as it is, since one author wrote them all. Nodes of different packs share no realm.
+- **The call deadline.** Every call the block thread makes into a node's JS, with the `create` a fresh
+  instance's first call makes and the promise jobs its checkpoint runs, is held to one deadline of the
+  calling thread's CPU time (2 s by default, `setCallDeadline`), so a stall that is not the node's own
+  never trips it. A call that runs past it is stopped and counted as a throw: a `faultOnThrow` node
+  faults, any other runs again and faults only if its next call is stopped too. The control thread's
+  calls into a pack are held to the same length of its own CPU time, on a clock of its own: a load is one
+  call (its chunks in both runtimes, the jobs they queue, its definitions read), and so is a settings
+  check (`parseSettings` and `layout` with their jobs). A load the deadline stops refuses the pack; a
+  settings check it stops refuses the settings, so the planner refuses the document and the running plan
+  stands. Every runtime's JS stack is held to 256 KiB, under a macOS secondary thread's 512 KiB.
 
 Every entry into a runtime re-anchors with `JS_UpdateStackTop` because the driving thread differs
 (the control thread on the pull path, the audio thread while running), and QuickJS's stack-overflow
 guard is calibrated against the thread that last entered it. A profile build profiles the audio
-runtime there ([08-profiling.md](08-profiling.md)).
+runtimes there, every pack's summed ([08-profiling.md](08-profiling.md)).
 
 [`ScriptCompiler`](../packages/native/src/host/dsp/ScriptCompiler.hpp#L18) (`dsp::compileToBytecode`)
-compiles a script to QuickJS bytecode on a **transient, bare context**: the engine-graph tests use
-it to build their toy script nodes. The bytecode is version-locked to this exact qjs-ng build, which
-is fine because the compiler and the script host are the same `qjs`.
+compiles a script to QuickJS bytecode on a **transient, bare context**: `Engine::loadScriptPack` compiles a
+pack's source with it, and the engine-graph tests their toy script nodes. A compile error comes back as its
+text and line (`line 3: SyntaxError: ...`; QuickJS gives no finer position), and the parser is held to the
+script host's 256 KiB stack, since the source is a user's and the thread the Engine's control thread. The
+bytecode keeps its debug information (the built-in bundle's is stripped at build time) and is
+version-locked to this exact qjs-ng build, which is fine because the compiler and the script host are the
+same `qjs`.
 
 ## Wrapped shared-core classes
 
@@ -808,13 +871,13 @@ is one automatable parameter applied post-render across all 8 channels
 
 - [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
 - [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
-- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, the command handler, `SyncTap`.
+- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, `loadScriptPack`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
 - [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host I/O nodes and the cart mirror), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
 - [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
 - [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
 - [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
-- [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime + the tests' compiler; [`src/script/`](../packages/retroplug/src/script) (`bundle.ts`, `register.ts`, `nodes/`): the bundle every Engine embeds, how it registers its nodes, and the script nodes themselves.
+- [`ScriptHost.hpp`](../packages/native/src/host/script/ScriptHost.hpp) / [`.cpp`](../packages/native/src/host/script/ScriptHost.cpp), [`ScriptCompiler.hpp`](../packages/native/src/host/dsp/ScriptCompiler.hpp): the script-node runtime and its packs, + the compiler a pack's source and the tests' toys go through; [`src/script/`](../packages/retroplug/src/script) (`bundle.ts`, `register.ts`, `nodes/`): the bundle every Engine embeds, how it registers its nodes, and the script nodes themselves.
 - [`HostRpcService.cpp`](../packages/native/src/host/rpc/HostRpcService.cpp), [`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp), [`DebugRpcService.cpp`](../packages/native/src/host/rpc/DebugRpcService.cpp), [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp) — the RPC services behind the facets.
 - [`plugin/PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp), [`plugin/PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp), [`src/main.cpp`](../packages/native/src/main.cpp), [`test/ui/UiHarness.hpp`](../packages/native/test/ui/UiHarness.hpp) — the three hosts.
 - [`CMakeLists.txt`](../packages/native/CMakeLists.txt), [`plugin/DistrhoPluginInfo.h`](../packages/native/plugin/DistrhoPluginInfo.h) — build wiring + plugin identity.
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 86f247e1..1a5dee90 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -46,7 +46,7 @@ The methods, grouped by concern:
 |---|---|---|
 | **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
 | **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
-| **Emulator lifecycle** | `buildCore(spec, id)` · `setGraph(request)` | Build/activate a core under a **TS-allocated id** and park it; plan a graph document TS built (which cores run, in what order and wired how). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over the two |
+| **Emulator lifecycle** | `buildCore(spec, id)` · `setGraph(request)` · `loadScriptPack(request)` | Build/activate a core under a **TS-allocated id** and park it; plan a graph document TS built (which cores run, in what order and wired how). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over the first two. Load a pack of script node types beside the built-in ones, or reload one, while audio runs ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): `{ ok, error, errors, types }`, the pack's types with their descriptors, or why not (the planner's `errors` when a reload's replan refused). Nothing in the app calls it yet, and the mock refuses it |
 | **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path (the one async method) |
 | **Live config** | `applySystemSetting(id,key,value)` · `applyRoleConfig(id,kind,config)` | Apply the two universal settings + a **system-role**'s knobs to the live core (the audio routing is the graph's wiring, not a call) |
 | **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index 3e9fe95b..8704cdf9 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -343,9 +343,12 @@ or the node removed and added again, clears it).
 **Settings without zod.** A node's settings are a zod-free schema
 ([graph/settingsSchema.ts](../packages/retroplug/src/graph/settingsSchema.ts): `s.object`, `s.enum`,
 `s.int`, `s.bool`, `s.number`, `s.string`, `s.array`, `s.json`), each builder parsing and writing its JSON
-Schema as its zod twin did, so the bundle every Engine loads into both of its script runtimes holds no zod
-(78 KB, where the kernel's was 660 KB, most of it zod). `defineNode` takes any schema with `safeParse` and
-`toJSONSchema`, so a zod schema still serves on the control plane. The role's own zod schema stays on the
+Schema as its zod twin did, so the bundle every Engine loads as its built-in script pack, `rp.script`, into
+both of that pack's runtimes, holds no zod (78 KB, where the kernel's was 660 KB, most of it zod). A pack
+loaded beside it ([02-native-host.md](02-native-host.md), the script host) runs in runtimes of its own, so
+nothing its nodes do to their realm, their promise jobs or their heap reaches the protocol nodes.
+`defineNode` takes any schema with `safeParse` and `toJSONSchema`, so a zod schema still serves on the
+control plane. The role's own zod schema stays on the
 control plane: the store parses a role's config with it, and that parsed config is the node's settings.
 [bundle-no-zod.test.ts](../packages/retroplug/test/script/bundle-no-zod.test.ts), a `pnpm test` case (not a
 build gate), fails on a bundle that pulls a zod module, or a role or config module, back in, or that passes 200 KB.
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index c00f5ebe..6c9f55fb 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -260,7 +260,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test` | `tjs-cli` | Mock-backend TS suite. |
 | `test:native` | `retroplug-host` | Real-host suite. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds six `[profile]` cases, which `tools/run-profile.sh unit` runs); the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds seven `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing nothing, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
@@ -339,7 +339,7 @@ directly: `tools/run-sanitizer.sh <thread|address> [slug]`. It configures a sepa
 drives it via `run-native-tests.mjs` with `RETROPLUG_HOST` pointed at the instrumented
 binary. Default slugs are `dsp-threaded` + `dsp-lifecycle`. Thread mode reuses the Catch2 seqlock
 suppression file ([tsan.supp](../packages/native/test/sanitizer/tsan.supp)); a clean run **is** the
-proof that the script host's audio runtime + cores are touched only by the audio thread. Any finding aborts
+proof that the script host's audio runtimes + cores are touched only by the audio thread. Any finding aborts
 the host → nonzero exit.
 
 ## Build gotchas
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index f4c75694..f4a5ce82 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -137,20 +137,26 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
 - **The capture mode as a Game Boy node setting** (Phase C, with the other live knobs): carried in the
   published document rather than through `ConfigField::SerialOutCapture` plus a publish, it would make a mode
   switch atomic and retire the master-sync design's first accepted limit (its section 10).
-- **One QuickJS context per script node, decided before user-written nodes.** Every script node runs in
-  the audio runtime's one context, so they share a realm: the script host keeps its own writes and its
-  reads of a node's methods off the shared prototypes ([02-native-host.md](02-native-host.md)), but one
-  node's code can still change what another's reads through (an accessor on `Object.prototype` or
-  `Array.prototype`, a global, a definition in `__rp_scriptNodes`), and a field a node deletes puts the
-  host's next write of it back on the chain. Promise jobs are charged to no node either:
-  `ScriptHost::runAudioJobs` runs every node's at the block's end under one deadline and counts a failed
-  one as nobody's throw, so a node that queues a never-ending reaction every block burns a full deadline
-  (2 s of CPU) every block and is never bypassed, as a `process` that hangs would be. A context per node
-  in the same runtime (its own intrinsics and prototypes) closes all of it, at a per-context memory cost,
-  and names the node a job ran in (`JS_ExecutePendingJob` reports the job's context); decide it before
-  nodes users write (Phase C, the Reaktor-scale aim). B3b made the project's nodes many, not one: eight is the
-  number of node types, and a project places the routing node, a controller when enabled, one node per protocol
-  role, and a second for each LSDj on a Game Boy, its serial-out decoder.
+- **Script node packs, built in Phase C1**
+  ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): each pack of script node types
+  runs in a QuickJS runtime pair of its own, the built-in bundle the first, packs load and reload while audio runs
+  (`loadScriptPack`, on every host), and a node pays for its own promise jobs (run at a checkpoint after each call
+  into it) and its pack's memory limit. Nothing in the app loads a pack yet. What it leaves (its sections 5, 12 and
+  13):
+  - **Isolation inside a pack.** Nodes of one pack share its realm, one author having written them all: one node's
+    code can still change what another's reads through (an accessor on `Object.prototype` or `Array.prototype`, a
+    global, a definition in `__rp_scriptNodes`), and a field a node deletes puts the host's next write of it back
+    on the chain ([02-native-host.md](02-native-host.md)). Freezing a pack's intrinsics after its load (an SES-style
+    lockdown) would close it, at the price of code that writes over an inherited property.
+  - **A node's state across a reload.** A reloaded pack's nodes restart clean; a `snapshot` / `restore` pair would
+    carry it.
+  - **The authoring loop, the pack format and downloads, a pack's UI half** (the design's section 8), **unloading a
+    pack**, and **loading one into more than one Engine**: a render job's Engine has the built-in pack only. A pack
+    compiled from source keeps its debug information, about a quarter more memory than stripped code; whether a
+    downloaded pack's is stripped is the pack format's to say.
+  - **A record the release ring back cannot take keeps its pack for the host's life**, as it leaks its record rather
+    than free it on the audio thread; nothing has filled that ring. A profile build's absolute live heap leaves out
+    what a pack loaded while audio runs allocated in its load (its deltas are exact).
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
diff --git a/spec/08-profiling.md b/spec/08-profiling.md
index 4ee8807c..91602529 100644
--- a/spec/08-profiling.md
+++ b/spec/08-profiling.md
@@ -214,8 +214,11 @@ past `renderAudio`'s 1024-frame block).
 
 ## 8. What was built
 
-- **Native allocator instrumentation**, on the script host's audio runtime
-  ([ScriptHost.hpp/.cpp](../packages/native/src/host/script/ScriptHost.cpp)): the counting
+- **Native allocator instrumentation**, on the script host's audio runtimes, every pack's summed
+  ([ScriptHost.hpp/.cpp](../packages/native/src/host/script/ScriptHost.cpp); a pack loaded beside the built-in
+  one, [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md), counts its load and its
+  freeing apart, so a profile build's absolute live heap leaves out what a pack loaded while audio runs
+  allocated in its load, its deltas exact): the counting
   `JSMallocFunctions` installed via `JS_NewRuntime2` (`js_malloc_usable_size` returns the real platform
   size so QuickJS's own accounting stays valid), a per-Engine-block allocation bracket (`beginBlock` to
   `endBlock`: native aggregation → no per-block RPC), and `allocStats()` /
@@ -231,8 +234,9 @@ past `renderAudio`'s 1024-frame block).
   nodes' few microseconds beside the cores') and `state-publish`. A span is stored as it closes, so a block's
   `apu-render` follows the spans inside it in the list; the trace-event JSON gives each span its start and
   length, so nothing that reads it depends on that order. The labels are fixed: `script-node` (0),
-  `marshal`, `js-call`, `apu-render`, `state-publish` (1 to 4), then the bundle's types from
-  `DSP_SPAN_TYPE_BASE` (16) up, in the order it registered them, each named by its id (the production
+  `marshal`, `js-call`, `apu-render`, `state-publish` (1 to 4), then the packs' types from
+  `DSP_SPAN_TYPE_BASE` (16) up, in the order they were first registered, each named by its id and keeping its
+  label across its pack's reloads, so a dev loop's reloads do not use up the table (the production
   bundle's eight are 16 to 23: `rp.midi.route`, `rp.mgb.midi`, `rp.n8.midi-to-fifo`, `rp.risa.sync`,
   `rp.sms.sync`, `rp.lsdj.sync`, `rp.controller.session` and `rp.lsdj.serial-out`, registered last so the
   others kept their labels). By type rather than node id, as the DSP kernel's per-role spans were per kind
@@ -241,8 +245,8 @@ past `renderAudio`'s 1024-frame block).
   Every label is below `kDspSpanLabels` (1024), so the name table stays bounded: a type past it is traced
   as `script-node`. JS adds no spans of its own (the kernel's roles were the only JS that did, through span
   globals that went with them), so a span is always closed by the host, every way a call ends. The promise
-  jobs a block's JS queued sit in no span: they run in `endBlock`, after `apu-render` closes and before
-  `state-publish`. `dspTraceReset` / `dspTrace` / `dspTraceNames` read it, and `tools/run-profile.sh trace`
+  jobs a node's call queues have no span of their own: they run at the checkpoint after the call, which for
+  `process` lies inside its process span, after its `js-call`. `dspTraceReset` / `dspTrace` / `dspTraceNames` read it, and `tools/run-profile.sh trace`
   writes it as Chrome trace-event JSON.
 - **Introspection RPCs** — `dspAllocStats` / `dspResetAllocStats` / `dspRunGc` on the harness facet
   (`Engine` → `EngineRpcService`, reached directly through `engine_` in the quiescent `renderAudio`
````

- [ ] **Step 7: Verify**

Run: `pnpm check:docs`
Expected: `check-doc-links: 62 file(s) clean` (63 once this plan is committed).

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the engine-graph binary's `All tests passed (18886 assertions in 140 test cases)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)` (one file more: `script-packs`), with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `(git diff HEAD; git diff --no-index /dev/null packages/retroplug/test-native/script-packs.test.ts) | python3 -c "import sys; print(sum('\u2014' in l for l in sys.stdin if l.startswith('+')))"`
Expected: `0` (no emdash on any added line, the new test file's included).

- [ ] **Step 8: Commit**

````bash
git add -- packages/retroplug/test-native/script-packs.test.ts AGENTS.md docs/modular-graph-design.md \
  docs/modular-graph-phase-c1-design.md packages/native/src/host/engine/Engine.hpp \
  packages/native/src/host/rpc/BackendRpcRegistration.hpp packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/src/host/script/ScriptHost.cpp packages/native/src/host/script/ScriptHost.hpp \
  packages/retroplug/src/backend.ts packages/retroplug/src/realBackend.ts packages/retroplug/testing/mockBackend.ts \
  spec/01-architecture.md spec/02-native-host.md spec/03-ts-layer.md spec/04-roles-protocol-nodes.md \
  spec/06-build-test.md spec/07-remaining-work.md spec/08-profiling.md
git commit -F - <<'EOF'
feat(backend): loadScriptPack over RPC and in the TS Backend, a test:native case, and the docs for script node packs

EngineRpcService::loadScriptPack, on the emulator facet beside setGraph (14 methods, 88 in the union), carries
Engine::loadScriptPack: an RpcLoadScriptPackRequest (id, source, memoryLimitBytes as an optional double, refused
unless a whole number of bytes from 0 to 2^53 - 1, since an integer field would read 1.5 as 1 and -1 as no limit)
and an RpcLoadScriptPackResult (ok, error, the planner's errors, the pack's types with their descriptor JSON).
Backend.loadScriptPack matches it field for field (LoadScriptPackRequest / LoadScriptPackResult / ScriptPackType),
realBackend sends it as setGraph sends its request, and the mock backend refuses ("the mock backend runs no script
packs"). The doc comment says errors is empty for a refusal before any replan and that a replan refused busy may be
retried. Nothing in the app calls it.

A load the pack's memory limit refused now says so: the limit holds the whole audio runtime, its intrinsics
included, and once QuickJS cannot make its out-of-memory error it throws null, so a limit of 64 KiB refused a toy's
load as "chunk 0: null"; it reads "chunk 0: null (the load went past the pack's 65536-byte memory limit)".

test-native/script-packs.test.ts, through the real host and fixture-free: a pack's types and descriptors; its node
beside the built-in routing and the host MIDI nodes, its MIDI out what its code makes; a reload whose node's output
changes while the routing's does not, and a type it adds placing; the refusals before any replan (a compile error at
its line, a type outside the namespace, rp.script, an id of the app's, a limit that is no whole number or too small
for the load, a top level that throws), each changing nothing; and a refused reload (a type the document places
gone, settings the new code refuses) returning the planner's errors with the old code running on.

Docs: the design's status, the controller's two decisions (rp. reserved, the control-side deadline), what building
changed in sections 4 to 7 and 9, the cost of a pack as two runtimes (about 180 KB and twice its code; measured in
the Engine), section 11's four questions answered, and a new section 13, Found while building. spec/01 section 6,
spec/02's RPC table and script host, spec/03's Backend table, spec/04, spec/06, spec/07 section 5, spec/08,
AGENTS.md (the packs file; seven [profile] cases) and modular-graph-design.md sections 4.9 and 8.
EOF
````

### Task 6: Verify everything

The acceptance run, on the final tree. Nothing to commit unless a step fails and is fixed; a fix is its own commit,
named for what it fixes. Every Expected is what the prototype's acceptance run printed on its final tree (some harness
PASS lines print an emdash; they are quoted here with " - " in its place).

- [ ] **Step 1: The tree and the build**

Run: `git status --short && cmake --build build -j$(nproc)`
Expected: only ` m deps/sameboy`, then a clean build (the plugin's existing `-Wformat-truncation` warning in `PluginDSP.cpp` only if it recompiles).

- [ ] **Step 2: The suites**

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, and `git status` shows no baseline changed.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s): parity/blocks-tiny-lsdj(9), parity/blocks-tiny-lsdj-master(6), parity/blocks-tiny-risa(3), parity/controller(1), parity/gba(2), parity/lsdj-link(6), parity/lsdj-modes(8), parity/lsdj-sync(8), parity/render-blocks(1), parity/risa(2)`, as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `136 case(s) SKIPPED in 60 file(s)` and the skip check clean (`script-packs` runs here: it needs no fixture).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test`
Expected: `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)` (graph `All tests passed (1894 assertions in 178 test cases)`, engine-graph `All tests passed (18886 assertions in 140 test cases)`).

Run: `pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): lsdj-hd(1), lsdj-overlay(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:node && pnpm typecheck && pnpm check:docs`
Expected: `ℹ pass 12` / `ℹ fail 0`, `packages/retroplug typecheck: Done`, `check-doc-links: 63 file(s) clean` (the prototype's 62, and this plan).

- [ ] **Step 3: The sanitizers**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh thread && RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh address`
Expected: `==> host passed under thread sanitizer`, then `==> host passed under address sanitizer` (with the fixtures, so `dsp-threaded` runs: a system added and removed while the audio thread plays).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && cmake --build build-tsan --target retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[protocol-nodes]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[script]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[packs]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-graph-test "[registry]"`
Expected: `All tests passed (7517 assertions in 11 test cases)`, `All tests passed (1678 assertions in 66 test cases)`, `All tests passed (795 assertions in 15 test cases)` and `All tests passed (209 assertions in 5 test cases)`, no ThreadSanitizer report (the build's `-Wtsan` warnings about `atomic_thread_fence`, from the vendored concurrentqueue, are compile warnings, not a report).

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[script]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[packs]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "[registry]"`
Expected: `All tests passed (1678 assertions in 66 test cases)`, `All tests passed (795 assertions in 15 test cases)` and `All tests passed (209 assertions in 5 test cases)`, no AddressSanitizer report.

- [ ] **Step 4: The live hosts**

Each of these one at a time, with nothing rebuilding the plugin meanwhile:

Run: `pnpm reaper:editor-reopen`
Expected: `PASS: the reopened editor still shows the loaded project - close/reopen preserves state.`

Run: `pnpm reaper:editor-autoload`
Expected: `PASS: the editor shows the autoloaded project (control-plane restore reflected in the UI).`

Run: `pnpm reaper:two-instances-load`
Expected: `PASS: a ROM loaded through the second instance's editor left both instances live.` (the prototype's heartbeat counted 191 before the second load and 1102 at the end of the watch, so the UI thread never froze).

Run: `pnpm reaper:params`
Expected: `PASS: the host re-read the parameter info - the CC slots are now mGB's map, count unchanged at 2725.`

Run: `pnpm reaper:params-clap`
Expected: `PASS: the host re-read the parameter info - the CC slots are now mGB's map, count unchanged at 644.`

Then the whole Reaper leg at once. Its fixture step authors the LSDj fixtures from `../resources/roms/lsdj/...`, which
does not exist from this worktree (the sibling resources are at `/workspaces/resources`), and `RP_SUITE_NO_BUILD`
skips the build AND every fixture. So author the fixtures by hand with explicit ROM paths, make sure no Reaper or
jackd of yours is left, and run the suite without its own build or fixture step. The plugin build in it is the one the
first step made.

Run: `node tools/author-rplg.js && node tools/author-nes-rplg.js && node tools/author-risa-rplg.js /workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes && node tools/author-sms-rplg.js && node tools/author-sms-rplg.js resources/roms/smsggdj_v0_45.gg && node tools/author-lsdj-rplg.js midi-metro /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js midi-drift /workspaces/resources/roms/lsdj/lsdj9_4_2.gb && node tools/author-lsdj-rplg.js arduinoboy-metro /workspaces/resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb`
Expected: an `authored build/<name>.rplg.zip` line for each (eight).

Run: `pgrep -a reaper; pgrep -a jackd; RP_SUITE_NO_BUILD=1 pnpm reaper:all`
Expected: no leftover process listed, then `ALL 15 REAPER CHECKS PASSED`. If a render fails, run that one check alone once (its own `pnpm reaper:<name>`) and report both results: concurrency in this container has flaked before.

Run: `pnpm sdl:smoke && pnpm sdl:pipewire`
Expected: `SDL smoke OK`, then `SDL PipeWire check: 12 passed, 0 failed` (the wide-output leg ran; where wireplumber will not start it prints `SKIPPED: wide-output checks need wireplumber` and fewer checks pass).

- [ ] **Step 5: The consumers, against this tree's CLI**

Their suites read `src/` and write beside the ROMs, so each runs from a scratch copy and leaves its repository
untouched:

Run: `R=$PWD; rm -rf /tmp/c1-bt && mkdir -p /tmp/c1-bt/build /tmp/c1-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/c1-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/c1-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/c1-bt/retroplug-cli/ && (cd /tmp/c1-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); rm -rf /tmp/c1-bt`
Expected: `PASS: 57/57 test file(s) ok` (BlipToaster at ed0c843; the count follows BlipToaster's own tests).

Run: `R=$PWD; rm -rf /tmp/c1-vj && mkdir -p /tmp/c1-vj/build /tmp/c1-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/c1-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/c1-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/c1-vj/retroplug-cli/ && (cd /tmp/c1-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); rm -rf /tmp/c1-vj`
Expected: `PASS: 19/19 test file(s) ok` (NESVJ at 4783154).

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: The profile build and the benches, for the record**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats && tools/run-profile.sh stats nes-bench`
Expected: dsp-bench's three windows, `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":312000,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.31}`, `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":476,"clocks":476,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`; then nes-bench's `DSP-BENCH {"core":"mesen","rom":"bliptoaster.nes","profile":"A","blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":310080,"maxBlockAllocs":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":10.42,"usPerBlock":2229.39}`: no allocation in any block of any window. Without `RETROPLUG_RESOURCES_DIR` the LSDj windows skip after the mGB window passes.

Run: `tools/run-profile.sh trace`
Expected: `build-prof/trace.json` written, with `rp.midi.route`, `rp.mgb.midi`, `marshal`, `js-call` and `apu-render` among its span names.

Run: `build/bin/retroplug-engine-graph-test "[bench]" && build/bin/retroplug-graph-test "[bench]"`
Expected: `All tests passed (672 assertions in 1 test case)`, then the planner bench's `All tests passed (156 assertions in 1 test case)`, each with its table. The prototype's three runs put an empty pass-through script node at 0.086 to 0.096 us a block and one passing a message on at 0.178 to 0.204 (the design commit's: 0.083 to 0.092 and 0.178 to 0.188; the difference is each call's checkpoint, a job-pending test, since a steady block allocates nothing), and the protocol nodes at 0.906, 1.093 and 1.953 us a block in MIDI sync and 0.921, 3.963 and 24.112 in MI.OUT at 1, 64 and 512 frames, every row with 0 JS and 0 native allocations.

Run: `git status --short`
Expected: only ` m deps/sameboy`.

