# Phase C2: live knobs as system node settings and params - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** A system's live knobs move into the graph: what is worth playing (gain, a NES's expansion volume) becomes a
param of its system node, set live through `setParam`, and everything else (a Game Boy's model, fast boot, highpass,
display group and LSDj serial-out capture; a NES's region, sprite limit, APU latency and accuracy switches) a setting of
it, in the published document. `ConfigField`'s side path and its casts go, and an LSDj mode switch becomes atomic.

**Architecture:** Per [docs/modular-graph-phase-c2-design.md](modular-graph-phase-c2-design.md). node.hpp gains a
`rebuild_key` hook, compared with the key an instance was created with, so the system nodes (keyed on their core) can
prepare statelessly and a settings edit never makes `setGraph` wait. Each system node type's settings struct carries
its setting knobs, every one optional (absent leaves the core as built), enums as the labels the TS role configs spell;
its params are node.hpp params. A node applies what moved at the start of its next block (everything at activation), a
param at its first block and whenever its control stream changes it. `buildGraph` writes the settings (the capture from
the mode the placed `lsdj-sync` node runs, after the controller's override) and params from the stores' role configs
and gain; the stores call `Backend.setParam` for a param and publish for a setting; `Engine::setParam` wraps the graph
host's. Then the old path is deleted and its callers moved.

**Tech stack:** C++20 with Catch2 (`retroplug-graph-test`, `retroplug-engine-graph-test`) and reflect-cpp, the graph
layer of Phases A to C1, TypeScript on txiki (`pnpm test` mock tier, `pnpm test:native` real host).

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/c2-knobs` in this worktree, on top of the design commit 88f8221d: five commits, one per task (Task 6 is the
acceptance run and commits nothing), with GCC 13.3 on linux-x64 (a Ryzen 9 9950X). A code block that creates a file is
that file as the prototype commit left it; one that changes files is the prototype's diff for them, so `git apply` it
from the step's text (or apply it hunk by hunk with the Edit tool). The two golden files Task 3 re-records are not
shown: its steps make them with the commands given (one a script quoted from the prototype's scratch directory), and
check them against the prototype's. Every expected pass, test count and figure is what the prototype's runs printed;
a failing-test step's expected failure is what the missing code produces, as each step says. When this plan was
generated it was replayed in a scratch clone of 88f8221d, every block written or applied in order, the generated files
taken from the prototype, and each task committed with the command given: each commit's tree came out exactly as the
prototype's, and each commit message as its; and on those replayed trees the mock-tier steps (`pnpm test`) were run,
printing the counts quoted. The native steps' counts are the prototype's. The prototype commits, by task:
- Task 1: `d12c7d71` feat(graph): a C++ node's rebuild_key, compared with the key it was created with, so a node that rebuilds on a key can prepare statelessly
- Task 2: `0d270229` feat(engine): system nodes take their knobs as settings, applied at the next block and at activation, and Engine::setParam
- Task 3: `68c118e8` feat(graph): system nodes' knobs ride the document: settings published, gain and expansion volume set live through setParam, the capture from the placed mode
- Task 4: `3cde26dd` refactor(engine): the side path for live knobs is gone: ConfigField, applyConfigField, the three RPCs and the build spec's capture; every caller sets its knob through the store or the document
- Task 5: `3e34f067` docs: the specs say what C2 built: system nodes' settings and params, setParam, the capture from the placed mode

**Where this plan departs from the design.** The design as committed (88f8221d) was written before the prototype; the
plan follows what was built, and Task 5's docs commit lands the design with its Status line, its section 8 questions
answered in place and a section 10, "Found while building". In short: the params land with the TS switch-over (Task 3)
rather than with the settings (Task 2), since a param's default applied at activation would overwrite a built core's
value while TS still sends none; every setting knob is optional, absent leaving the core as built; a range out of
bounds is refused, not clamped; a param reaches its core at the node's first block rather than in `activate`; every
Game Boy node carries its capture, `"off"` included, so an armed capture cannot stick; `expansionVolume` is an integer
param; fast boot is applied before the model, so two edits committed together restart with the new fast boot;
`buildGraph` drops a stored value the node would refuse while `DirectGraph.setCoreConfig` refuses it; and building
found three latent bugs C2 fixes (a loaded project's saved gain, the UI render's carried gain, and a loaded NES's two
accuracy switches, none of which reached the core).

**Ground rules for whoever executes this:**
- Commit after each task (the commit commands are given; Task 6 commits nothing unless a step fails and is fixed,
  in a commit of its own). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";". Some diffs below
  carry emdashes on lines the prototype did not write (context and removed lines of older code): leave those be.
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the
  session itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with
  uncommitted work in the tree. Never touch a submodule.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it.
- The derived bundle C arrays (`build/native/*bundle_data.c`) are never committed.
- Parity holds the engine's output to the bit, and no task re-records it: every system node's knobs reach its core at
  a block's start, where the command ring delivered them. A strict parity failure is a real change to the output: stop
  and find it. The TS graph goldens Task 3 re-records are not parity: they record the documents `buildGraph` writes.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources`; without it their cases skip. Pass it on
  the same command line: a shell `export` does not survive between commands.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter: an exact
  slug or a prefix in the dash form. Run several files as separate commands.
- The sanitizer trees are their own: `build-tsan/` (configured with `-DRETROPLUG_SANITIZE=thread`). Configure it only
  if its `CMakeCache.txt` is missing. The profile build is its own tree too (`build-prof/`, `tools/run-profile.sh`).
- The prototype branch `proto/c2-knobs` must stay until this plan has been executed: Task 3's golden files are checked
  against it.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes. Run the Reaper checks
  one at a time, and before each make sure no Reaper or jackd of an earlier run is left (`pgrep -a reaper;
  pgrep -a jackd`).
- `pnpm check:docs` counts the tracked docs: 64 in the prototype, which had no plan file, so 65 once this plan is
  committed.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/native/src/graph/node.hpp` | the `rebuild_key` hook and its checks (Task 1) |
| `packages/native/src/host/graph/SystemNodes.cpp` | each system node type's settings (Task 2) and params (Task 3), applied at the next block and at activation; `rebuild_key` on the core and stateless prepare |
| `packages/native/src/system/sameboy/SameBoySystem.{hpp,cpp}`, `src/system/mesen/MesenNes{Config,System}.hpp` | value-guarded setters and the enums the settings map to (Task 2); the dead reload flag and the old decode's comments (Task 4) |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `Engine::setParam` (Task 2); `ConfigField` and `applyConfigField` deleted (Task 4) |
| `packages/native/src/host/rpc/*` | `setParam` over RPC (Task 3); the three old RPCs and the build spec's capture deleted (Task 4) |
| `packages/retroplug/src/graph/{buildGraph,controller,directGraph}.ts` | the settings and params in the document, the capture from the placed mode (Task 3); `DirectGraph.setCoreConfig` / `setParam` (Task 4) |
| `packages/retroplug/src/systemsStore.ts`, `src/backend.ts`, `src/realBackend.ts`, `testing/mockBackend.ts` | `setParam` for a param, a publish for a setting (Task 3); the old methods gone (Task 4) |
| `packages/retroplug/test/graph/golden/build-graph{,-roles}.json` | re-recorded by Task 3 |
| `packages/retroplug/test-native/lsdj-capture-settings.test.ts` | new, replacing `lsdj-midiout-build-flag.test.ts` (Task 4) |
| spec/, docs/, AGENTS.md | Task 5 |

---

### Task 1: node.hpp's rebuild_key

Design section 4 ("setGraph never waits on a system node"). A C++ node with `needs_rebuild` cannot declare stateless
prepare, since `needs_rebuild` reads the instance's current settings, which a commit writes; so every settings edit to
such a node makes `setGraph` wait for the audio thread to adopt it. A new hook, `rebuild_key(const Settings&)`, names a
value only a rebuild can change and is compared with the key the instance was created with, which no commit writes, so
a type with it may declare `stateless_prepare`. No existing node changes.

**Files:**
- Modify: `packages/native/src/graph/node.hpp`, `packages/native/test/graph/NodeLayer.test.cpp`, `packages/native/test/graph/SwapStress.test.cpp`, `packages/native/test/graph/TestNodes.hpp`

- [ ] **Step 1: Write the failing tests**

A toy node, `rp.test.keyed` (a string key, stateless prepare), in its own package so the registry's counts of the test
package do not move; cases for its flags, a same-key change swapping (the instance kept, `on_settings` seeing the new
settings), another key rebuilding, the key compared with the one from create even after settings of another key were
committed; and, under an audio thread that owns the host with no block run, three swaps all accepted where a node with
`needs_rebuild` answers busy to the second:

````diff
diff --git a/packages/native/test/graph/NodeLayer.test.cpp b/packages/native/test/graph/NodeLayer.test.cpp
index 92f9b32d..4e4e0ed4 100644
--- a/packages/native/test/graph/NodeLayer.test.cpp
+++ b/packages/native/test/graph/NodeLayer.test.cpp
@@ -192,6 +192,40 @@ TEST_CASE("a node that reshapes answers reshape for a layout change, and a state
     CHECK(rp::node_type<test::Source>()->flags == 0u);  // by default prepare may read the current settings
 }
 
+TEST_CASE("a node that rebuilds on a key compares the key it was created with, so it may prepare statelessly",
+          "[graph][node]") {
+    CHECK(rp::node_type<test::Keyed>()->flags == RP_TYPE_STATELESS_PREPARE);
+    Made m{rp::node_type<test::Keyed>()};
+    make(m, R"({"key":"a","level":1})");
+    rp_error err{};
+    void* prepared = nullptr;
+    void* old = nullptr;
+
+    // The same key swaps, and on_settings sees the new settings.
+    rp_settings louder = json(R"({"key":"a","level":2})");
+    REQUIRE(m.node->vt->prepare_settings(m.node, &louder, &prepared, &err) == RP_PREPARE_SWAP);
+    m.node->vt->commit_settings(m.node, prepared, &old);
+    m.node->vt->release_settings(m.node, old);
+    CHECK(rp::peer_as<test::Keyed>(m.node)->level == 2.0f);
+
+    // Another key rebuilds.
+    rp_settings rekeyed = json(R"({"key":"b","level":2})");
+    CHECK(m.node->vt->prepare_settings(m.node, &rekeyed, &prepared, &err) == RP_PREPARE_REBUILD);
+
+    // The key compared is the one create kept, never the current settings' (what a commit writes): settings with
+    // another key, committed past prepare, leave the answers as they were.
+    m.node->vt->commit_settings(m.node, new test::KeyedSettings{"b", 3.0}, &old);
+    m.node->vt->release_settings(m.node, old);
+    REQUIRE(rp::peer_as<test::Keyed>(m.node)->level == 3.0f);
+    rp_settings back = json(R"({"key":"a","level":4})");
+    REQUIRE(m.node->vt->prepare_settings(m.node, &back, &prepared, &err) == RP_PREPARE_SWAP);
+    m.node->vt->release_settings(m.node, prepared);
+    CHECK(m.node->vt->prepare_settings(m.node, &rekeyed, &prepared, &err) == RP_PREPARE_REBUILD);
+
+    rp_settings bad = json(R"({"key":7})");
+    CHECK(m.node->vt->prepare_settings(m.node, &bad, &prepared, &err) == RP_PREPARE_INVALID);
+}
+
 namespace {
 
 struct PickySettings {
diff --git a/packages/native/test/graph/SwapStress.test.cpp b/packages/native/test/graph/SwapStress.test.cpp
index b4f543b4..f334bab9 100644
--- a/packages/native/test/graph/SwapStress.test.cpp
+++ b/packages/native/test/graph/SwapStress.test.cpp
@@ -255,6 +255,44 @@ TEST_CASE("setGraph waits while a published settings commit is unadopted", "[gra
     rig.host->setAudioThreadOwns(false);
 }
 
+TEST_CASE("setGraph never waits on a node that rebuilds on a key, whose key change rebuilds it", "[graph][swap]") {
+    GraphRig rig({48000.0, 64, rp::graph::Target::Desktop, 50});  // a 50 ms adoption timeout
+    REQUIRE(rig.registry.addPackage(rp::graph::test::keyedPackage()));
+    const auto doc = [](const char* key, int level) {
+        return std::string(R"({"nodes":[{"id":"k","type":"rp.test.keyed","settings":{"key":")") + key +
+               R"(","level":)" + std::to_string(level) + R"(}}],"wires":[]})";
+    };
+    REQUIRE(rig.set(doc("a", 1)).ok);
+    rig.run();
+    rp_node* const before = rig.host->node("k");
+    rig.host->setAudioThreadOwns(true);  // and no block runs until the end, so nothing is adopted
+    // Three swaps committing settings to k, each planned while the one before is unadopted, where a node that
+    // decides with needs_rebuild (rp.test.tunable, above) answers busy to the second.
+    REQUIRE(rig.set(doc("a", 2)).ok);
+    REQUIRE(rig.set(doc("a", 3)).ok);
+    REQUIRE(rig.set(doc("a", 4)).ok);
+    CHECK(rig.host->node("k") == before);
+    rig.run();  // adopts all three, in order
+    const auto& k = rig.node<rp::graph::test::Keyed>("k");
+    CHECK(k.commits == 4);
+    CHECK(k.committed[1] == 2.0);
+    CHECK(k.committed[2] == 3.0);
+    CHECK(k.committed[3] == 4.0);
+    CHECK(k.blocks == 2);  // the same instance throughout
+    CHECK(k.level == 4.0f);
+
+    // Another key is a new instance, its state afresh.
+    REQUIRE(rig.set(doc("b", 5)).ok);
+    rig.run();
+    CHECK(rig.host->node("k") != before);
+    CHECK(rig.node<rp::graph::test::Keyed>("k").blocks == 1);
+    CHECK(rig.node<rp::graph::test::Keyed>("k").commits == 1);
+    CHECK(rig.set(doc("b", 6)).ok);
+    rig.run();
+    CHECK(rig.node<rp::graph::test::Keyed>("k").level == 6.0f);
+    rig.host->setAudioThreadOwns(false);
+}
+
 namespace {
 
 // Holds the audio thread inside a settings commit for as long as `held` is set.
diff --git a/packages/native/test/graph/TestNodes.hpp b/packages/native/test/graph/TestNodes.hpp
index b48c3d55..3c87da3e 100644
--- a/packages/native/test/graph/TestNodes.hpp
+++ b/packages/native/test/graph/TestNodes.hpp
@@ -259,6 +259,45 @@ struct Shape {
     }
 };
 
+// Rebuilds when its settings' key changes and swaps any other change, deciding which with rebuild_key rather than
+// needs_rebuild, so its prepare is stateless. Each block it writes its level; `blocks` is the state a rebuild would
+// lose, and `committed` the level of each settings on_settings saw, in order. Not in testPackage: a test that places
+// it registers keyedPackage.
+struct KeyedSettings {
+    std::string key = "a";
+    double      level = 0.0;
+};
+
+struct Keyed {
+    static constexpr const char* id = "rp.test.keyed";
+    static constexpr bool stateless_prepare = true;
+    using Settings = KeyedSettings;
+    struct { rp::audio_out<rp::mono> out; } outputs;
+
+    static std::string rebuild_key(const Settings& s) { return s.key; }
+
+    float                 level = 0.0f;
+    std::uint32_t         blocks = 0;
+    std::array<double, 8> committed{};
+    std::uint32_t         commits = 0;
+
+    void on_settings(const Settings& s) noexcept {
+        level = static_cast<float>(s.level);
+        if (commits < committed.size()) committed[commits] = s.level;
+        ++commits;
+    }
+    void process(const rp::Block& b) noexcept {
+        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = level;
+        ++blocks;
+    }
+};
+
+inline const rp_package* keyedPackage() {
+    static const rp_node_type* const kTypes[] = {rp::node_type<Keyed>()};
+    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.keyed","version":"1.0.0"})", 1, kTypes};
+    return &kPackage;
+}
+
 inline const rp_package* testPackage() {
     static const rp_node_type* const kTypes[] = {
         rp::node_type<Source>(), rp::node_type<Sink>(),  rp::node_type<Linked>(),  rp::node_type<NoStep>(),
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test`
Expected: FAIL: the new cases fail (the old node.hpp has no `rebuild_key`, so prepare answers a swap where the key moved, and no new instance is made); every other case passes.

- [ ] **Step 3: Add rebuild_key**

`checkHooks` checks the hook's signature, that its key is a copyable, equality-comparable non-void type, and that a
type does not have both it and `needs_rebuild`. The instance holds the key from create as a `std::optional<Key>`,
computed in `createFn` (a throw reported through the error, as `adjust_layout`'s is), and `prepareFn` rebuilds when
the new settings' key differs:

````diff
diff --git a/packages/native/src/graph/node.hpp b/packages/native/src/graph/node.hpp
index 41fe5ca4..93c7150d 100644
--- a/packages/native/src/graph/node.hpp
+++ b/packages/native/src/graph/node.hpp
@@ -28,6 +28,11 @@
 //                                                         among them), append ports or params; never remove or
 //                                                         reorder reflected ones
 //   static bool needs_rebuild(const Settings& from, const Settings& to);
+//   static Key rebuild_key(const Settings&);              a value only a rebuild can change (Key is any copyable
+//                                                         type with ==): prepare compares the new settings' key
+//                                                         with the key the instance was created with, which no
+//                                                         commit writes, so unlike needs_rebuild it allows
+//                                                         stateless_prepare. Never both on one type
 //   static std::size_t extra_memory(const Settings&);     host-supplied tail memory (max_align_t aligned)
 //   void attach(std::span<std::byte>) noexcept;           receives it, at create
 //   void on_settings(const Settings&) noexcept;           at create (control) and each commit (audio): copy,
@@ -47,6 +52,7 @@
 //   static constexpr bool stateless_prepare;              prepare reads nothing a commit writes, so setGraph never
 //                                                         waits for this node's commits (RP_TYPE_STATELESS_PREPARE);
 //                                                         rules out needs_rebuild, which reads the current settings
+//                                                         (rebuild_key does not)
 //
 // process(const rp::Block&) is required, noexcept, and returns void or rp_status. Exceptions never cross
 // the ABI: every control-thread entry catches, and every audio-thread entry is noexcept.
@@ -364,6 +370,17 @@ template <class T> constexpr bool statelessPrepareOf() {
     if constexpr (requires { T::stateless_prepare; }) return T::stateless_prepare; else return false;
 }
 
+// What rebuild_key returns, or NoKey for a type without one. Detected by the call, as needs_rebuild is in prepareFn,
+// so the key the instance keeps and the key prepare compares are found the same way.
+struct NoKey {};
+template <class T> struct RebuildKeyOf { using type = NoKey; };
+template <class T>
+    requires requires(const typename SettingsOf<T>::type& s) { T::rebuild_key(s); }
+struct RebuildKeyOf<T> {
+    using type = std::remove_cvref_t<decltype(T::rebuild_key(std::declval<const typename SettingsOf<T>::type&>()))>;
+};
+template <class T> inline constexpr bool kHasRebuildKey = !std::is_same_v<typename RebuildKeyOf<T>::type, NoKey>;
+
 template <class T> auto& inputsOf(T& t) { if constexpr (requires { t.inputs; }) return t.inputs; else { static NoPorts none; return none; } }
 template <class T> auto& outputsOf(T& t) { if constexpr (requires { t.outputs; }) return t.outputs; else { static NoPorts none; return none; } }
 template <class T> auto& paramsOf(T& t) { if constexpr (requires { t.params; }) return t.params; else { static NoPorts none; return none; } }
@@ -529,6 +546,15 @@ constexpr bool checkHooks() {
     if constexpr (requires { requires kIsFunction<decltype(&T::needs_rebuild)>; })
         static_assert(requires(const S& s) { { T::needs_rebuild(s, s) } -> std::same_as<bool>; },
                       "needs_rebuild must be `static bool needs_rebuild(const Settings&, const Settings&)`");
+    if constexpr (requires { requires kIsFunction<decltype(&T::rebuild_key)>; }) {
+        static_assert(requires(const S& s) { T::rebuild_key(s); },
+                      "rebuild_key must be `static Key rebuild_key(const Settings&)`");
+        using K = typename RebuildKeyOf<T>::type;
+        static_assert(!std::is_void_v<K> && std::copy_constructible<K> && std::equality_comparable<K>,
+                      "rebuild_key's Key must be copyable and compare with ==");
+    }
+    static_assert(!(kHasRebuildKey<T> && requires(const S& s) { T::needs_rebuild(s, s); }),
+                  "a node has needs_rebuild or rebuild_key, not both: the key already decides what rebuilds");
     if constexpr (requires { requires kIsFunction<decltype(&T::extra_memory)>; })
         static_assert(requires(const S& s) { { T::extra_memory(s) } -> std::same_as<std::size_t>; },
                       "extra_memory must be `static std::size_t extra_memory(const Settings&)`");
@@ -574,6 +600,9 @@ struct Instance : rp_node {
     typename SettingsOf<T>::type* settings = nullptr;
     std::string                   layoutJson;  // as created, which prepare compares against; empty if it reshapes
     std::span<std::byte>          extra;
+    // The rebuild_key of the settings it was created with, which prepare compares against (empty without the
+    // hook). A different key rebuilds, so it never changes while the instance lives, and no commit writes it.
+    std::optional<typename RebuildKeyOf<T>::type> key;
 };
 
 inline void copyError(rp_error* err, std::string_view what) {
@@ -712,14 +741,16 @@ rp_status processFn(rp_node* n, const rp_process* p) noexcept {
     }
 }
 
-// Compares against the instance's extra memory and, unless it reshapes, its layout, neither of which changes
-// after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps the
-// layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
+// Compares against the instance's extra memory, its rebuild_key and, unless it reshapes, its layout, none of which
+// changes after create; a node that reshapes never compares layouts, since the planner takes a reshape that keeps
+// the layout as a swap. Only needs_rebuild reads the instance's current settings, which is safe because a host never
 // prepares while a commit for the same instance may be in flight: GraphHost waits for a published commit to be
 // adopted before it plans, but only for commits from types without RP_TYPE_STATELESS_PREPARE (awaitsAdoption). A C++
 // node without stateless_prepare is such a type; a stateless_prepare type (every script node) is not waited for,
 // which is why that flag rules needs_rebuild out. The rule is asserted on the call itself, so no form of
-// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it.
+// needs_rebuild this would call (a template, an overload set, a callable static member) slips past it. A
+// rebuild_key decides the same thing from the key create kept, so a type that rebuilds on a key prepares
+// statelessly.
 template <class T>
 rp_prepare_result prepareFn(rp_node* n, const rp_settings* s, void** prepared, rp_error* err) {
     using S = typename SettingsOf<T>::type;
@@ -738,6 +769,7 @@ rp_prepare_result prepareFn(rp_node* n, const rp_settings* s, void** prepared, r
                           "a stateless_prepare node cannot have needs_rebuild, which reads its current settings");
             rebuild = rebuild || T::needs_rebuild(*inst->settings, *parsed);
         }
+        if constexpr (kHasRebuildKey<T>) rebuild = rebuild || !(T::rebuild_key(*parsed) == *inst->key);
         if (rebuild) return RP_PREPARE_REBUILD;
         *prepared = new S(std::move(*parsed));
         if constexpr (reshapesOf<T>()) return RP_PREPARE_RESHAPE;
@@ -877,15 +909,18 @@ rp_node* createFn(const rp_node_type*, [[maybe_unused]] const rp_host* host, voi
             copyError(err, parsed.error().what());
             return nullptr;
         }
-        // Everything that can throw on the node's behalf (adjust_layout) runs before the instance exists. Only a node
-        // that does not reshape keeps its layout, for prepare to compare against.
+        // Everything that can throw on the node's behalf (adjust_layout, rebuild_key) runs before the instance exists.
+        // Only a node that does not reshape keeps its layout, for prepare to compare against.
         std::string layout;
         if constexpr (reshapesOf<T>()) (void)layoutFor<T>(*parsed);
         else layout = graph::layoutJson(layoutFor<T>(*parsed));
+        std::optional<typename RebuildKeyOf<T>::type> key;
+        if constexpr (kHasRebuildKey<T>) key.emplace(T::rebuild_key(*parsed));
         const std::size_t extra = extraBytes<T>(*parsed);
         inst = new (memory) Instance<T>{};
         inst->vt = vtable<T>();
         inst->layoutJson = std::move(layout);
+        if (key) inst->key.emplace(std::move(*key));
         inst->settings = new S(std::move(*parsed));
         if constexpr (requires(T& t) { t.bind_host(host); }) inst->node.bind_host(host);
         if (extra) {
````

- [ ] **Step 4: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test`
Expected: `All tests passed` for the graph binary.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the graph binary's `All tests passed (1996 assertions in 184 test cases)` and the engine-graph binary's `All tests passed (19253 assertions in 157 test cases)` (the graph binary two cases more).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

- [ ] **Step 5: Commit**

````bash
git add -- packages/native/src/graph/node.hpp packages/native/test/graph/NodeLayer.test.cpp \
  packages/native/test/graph/SwapStress.test.cpp packages/native/test/graph/TestNodes.hpp
git commit -F - <<'EOF'
feat(graph): a C++ node's rebuild_key, compared with the key it was created with, so a node that rebuilds on a key can prepare statelessly
EOF
````

### Task 2: system nodes take their knobs as settings, and Engine::setParam

Design sections 3 and 4. Each system node type's settings carry its setting knobs beside `core`, every one optional
(absent leaves the core as it was built, so the old path keeps working until Task 4 and a hand-written `{"core": n}`
keeps its meaning), applied at the start of the next block and all at activation, value-guarded as
`Engine::applyConfigField` applies them; the types key on `core` and prepare statelessly. `Engine::setParam` wraps the
graph host's. The params themselves land in Task 3, with the TS code that sends them.

**Files:**
- Modify: `packages/native/cmake/tests.cmake`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/graph/SystemNodes.cpp`, `packages/native/src/system/mesen/MesenNesConfig.hpp`, `packages/native/src/system/mesen/MesenNesSystem.hpp`, `packages/native/src/system/sameboy/SameBoySystem.cpp`, `packages/native/src/system/sameboy/SameBoySystem.hpp`, `packages/native/test/engine-graph/EngineGraph.test.cpp`, `packages/native/test/engine-graph/HostIo.test.cpp`, `packages/native/test/engine-graph/SystemNode.test.cpp`

- [ ] **Step 1: Write the failing tests**

Every system node type has the stateless flag, and two knob edits under an owning audio thread with no block run are
both accepted; each Game Boy and NES knob reaches its core at the next block, read back from the core's config; the
value guards, bit for bit against a twin core given the same setter calls at the same block (highpass and the display
group apply in place, a palette alone changes no sample, a model change restarts, the NES switches are inaudible, a
region change resets); knobs present at activation reach the core before its first block, the capture included; a
document with only `{"core": n}` leaves a core built with other config untouched, and a field that was present and is
then dropped leaves the core as it was; ordinals, other spellings and out-of-range values refused; a Game Boy knob
commit allocates nothing on the block thread; an audio thread running while sixty knob edits are published;
`Engine::setParam` (unknown node, unknown param, NaN refused; clamped; frame 0 of the next block); one `setGraph`
moving an mGB's capture and its `rp.lsdj.serial-out` decoder's mode together landing both in the same block; and every
enum knob's labels read against `settingsEnums.ts`'s arrays, whose path the test build is given:

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index f839fa6a..9f786dca 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -75,13 +75,13 @@ retroplug_add_test_binary(
              RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
              RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")
 
-# Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md):
-# each system node renders exactly what its core renders driven directly, its inputs included, the engine places,
-# re-arms and frees what the documents TS builds say, the host I/O nodes and the cart mirror move what the hosts stage
-# and drain, TS nodes run behind the C ABI in the script host, a script node pack loads beside the built-in one into
-# runtimes of its own and reloads, the generation it replaced freed off the audio thread, the protocol script nodes
-# send what the DSP kernel they replaced was recorded sending, and every document in buildGraph's goldens, its role
-# goldens among them, plans on it.
+# Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md, -b3a-design.md, -b3b-design.md,
+# -c2-design.md): each system node renders exactly what its core renders driven directly, its inputs and its settings'
+# knobs included, the engine places, re-arms and frees what the documents TS builds say, the host I/O nodes and the
+# cart mirror move what the hosts stage and drain, TS nodes run behind the C ABI in the script host, a script node
+# pack loads beside the built-in one into runtimes of its own and reloads, the generation it replaced freed off the
+# audio thread, the protocol script nodes send what the DSP kernel they replaced was recorded sending, and every
+# document in buildGraph's goldens, its role goldens among them, plans on it.
 # A hidden bench times a script node per block.
 # Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
 # CountingAllocator.cpp is shared with retroplug-graph-test, for the no-allocation checks. The script tests' bundle
@@ -106,6 +106,7 @@ retroplug_add_test_binary(
              RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
              RP_BUILD_GRAPH_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph.json"
              RP_BUILD_GRAPH_ROLE_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph-roles.json"
+             RP_SETTINGS_ENUMS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/src/settingsEnums.ts"  # the labels a knob takes
              RP_BUILD_TYPE="$<CONFIG>")                 # the bench prints the configuration it was built in
 add_dependencies(retroplug-engine-graph-test retroplug-script-test-bundle)  # the embedded bytecode first
 
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 212f44df..4cc2ab00 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -1,14 +1,18 @@
 // The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5): a built core parks
 // under its id until a document's system node takes it, a document's system nodes and its slots must name the
 // same systems, the tap re-arm rides the swap, a refused document leaves its cores parked, and a core no
-// published document placed is freed; and a build spec reaches the core under the field names TS sends. Driven
-// on a quiescent Engine, so every plan applies as it is published, except where a test owns the drain itself.
+// published document placed is freed; a build spec reaches the core under the field names TS sends; a system node's
+// settings carry its core's knobs beside the core (docs/modular-graph-phase-c2-design.md section 4); and
+// Engine::setParam reaches a param of the running plan at the next block. Driven on a quiescent Engine, so every plan
+// applies as it is published, except where a test owns the drain itself.
 //
 // Run via `pnpm test:plugin engine-graph`.
 
+#include <limits>
 #include <memory>
 #include <optional>
 #include <string>
+#include <utility>
 #include <vector>
 
 #include <catch2/catch_test_macros.hpp>
@@ -435,3 +439,113 @@ TEST_CASE("a render into buffers the planned graph does not write is refused", "
     CHECK(svc.renderAudio(10.0).empty());
     CHECK(svc.renderBlocks(blocks).error.empty());
 }
+
+// ---- knobs (docs/modular-graph-phase-c2-design.md section 4) -----------------------------------------------------
+
+namespace {
+
+// `r` with system `id`'s settings carrying `knobs` (JSON members) beside its core.
+Engine::GraphRequest withKnobs(Engine::GraphRequest r, SystemId id, const std::string& knobs) {
+    const std::string core = R"({"core":)" + std::to_string(id) + "}";
+    const auto at = r.document.find(core);
+    REQUIRE(at != std::string::npos);
+    r.document.replace(at, core.size(), R"({"core":)" + std::to_string(id) + "," + knobs + "}");
+    return r;
+}
+
+// A pack of one node, knobs.level, whose param `level` (-1 to 1, 0 by default) it writes to every frame of its mono
+// output from the frame each change lands at.
+const char* const kLevelPack = R"JS(
+(function () {
+  var nodes = globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || [];
+  var ports = [{ id: "out", dir: "out", kind: "audio", channels: { count: 1, layout: "mono" } }];
+  var params = [{ id: "level", name: "Level", type: "float", min: -1, max: 1, default: 0 }];
+  nodes[nodes.length] = {
+    id: "knobs.level",
+    descriptor: function () { return { id: "knobs.level", ports: ports, params: params, settings: { type: "object" } }; },
+    parseSettings: function (raw) { return raw === undefined ? {} : raw; },
+    layout: function () { return { ports: ports, params: params }; },
+    create: function () {
+      return { process: function (b) {
+        var level = b.control("level"), out = b.audio("out")[0], changes = level.changes, v = level.start, k = 0;
+        for (var f = 0; f < b.frames; f++) {
+          while (k < changes.length && changes[k].frame <= f) v = changes[k++].value;
+          out[f] = v;
+        }
+      } };
+    },
+    reshapes: false,
+    faultOnThrow: false,
+  };
+})();
+)JS";
+
+}  // namespace
+
+TEST_CASE("a system node's settings carry its knobs beside its core, and the engine places it", "[engine-graph][settings]") {
+    Engine e(kRate);
+    REQUIRE(e.parkCore(mgb(1)));
+    REQUIRE(e.parkCore(bliptoaster(2)));
+    auto r = withKnobs(withKnobs(request({{1, kGb}, {2, kNes}}), 1, R"("highpass":"off","serialOutCapture":"listen")"), 2,
+                       R"("removeSpriteLimit":true)");
+    REQUIRE(e.setGraph(r).ok);
+    // Given as the nodes took their cores, before a block.
+    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
+    const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(2));
+    REQUIRE(gb);
+    REQUIRE(nes);
+    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+    CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
+    CHECK(nes->config().removeSpriteLimit);
+
+    // Settings a node refuses refuse the document: the running plan keeps its knobs, and a core it would place stays
+    // parked.
+    REQUIRE(e.parkCore(mgb(3)));
+    const auto refused = e.setGraph(withKnobs(request({{1, kGb}, {2, kNes}, {3, kGb}}), 3, R"("model":"cgb-c")"));
+    CHECK_FALSE(refused.ok);
+    REQUIRE(refused.errors.size() == 1);
+    CHECK(refused.errors[0].code == "settings-invalid");
+    CHECK(refused.errors[0].nodeId == "sys-3");
+    CHECK(e.coreLive(3));
+    CHECK(e.systemCount() == 2);
+}
+
+TEST_CASE("Engine::setParam reaches a param of the running plan at the next block, clamped", "[engine-graph][settings]") {
+    Engine e(kRate);
+    REQUIRE(e.loadScriptPack("knobs", kLevelPack).ok);
+    REQUIRE(e.parkCore(mgb(1)));
+    const rp::host::test::DocExtras extras{{rp::host::test::docNode("level", "knobs.level")},
+                                           {rp::host::test::docWire("level.out", "out.out.2")}};
+    // The Game Boy alone on channel 1, so channel 2 is the toy's.
+    auto r = rp::host::test::graphRequest(rp::host::test::stereoDocument({{1, kGb}}, extras), {1});
+    const auto split = r.document.find(R"({"from":"split-1.out2","to":"out.out.2"})");
+    REQUIRE(split != std::string::npos);
+    r.document.erase(split, std::string(R"({"from":"split-1.out2","to":"out.out.2"},)").size());
+    REQUIRE(e.setGraph(r).ok);
+    std::vector<float> l(256), right(256);
+    const auto block = [&] {
+        float* outs[2] = {l.data(), right.data()};
+        e.processBlock(256, outs, 2);
+        return std::pair{right.front(), right.back()};
+    };
+    CHECK(block() == std::pair{0.0f, 0.0f});
+
+    // Refused: a node or a param the plan does not have, a NaN, and every system node, which has no param yet.
+    CHECK_FALSE(e.setParam("nope", "level", 0.5));
+    CHECK_FALSE(e.setParam("level", "nope", 0.5));
+    CHECK_FALSE(e.setParam("level", "level", std::numeric_limits<double>::quiet_NaN()));
+    CHECK_FALSE(e.setParam("sys-1", "gain", 0.0));
+    CHECK(block() == std::pair{0.0f, 0.0f});
+
+    CHECK(e.setParam("level", "level", 0.5));
+    CHECK(block() == std::pair{0.5f, 0.5f});  // from the first frame of the next block
+    CHECK(e.setParam("level", "level", 5.0));  // clamped to the param's range
+    CHECK(block() == std::pair{1.0f, 1.0f});
+    CHECK(e.setParam("level", "level", -3.0));
+    CHECK(block() == std::pair{-1.0f, -1.0f});
+    CHECK(block() == std::pair{-1.0f, -1.0f});  // and held
+
+    // A published document carrying the node again keeps the value it holds.
+    REQUIRE(e.setGraph(r).ok);
+    CHECK(block() == std::pair{-1.0f, -1.0f});
+}
diff --git a/packages/native/test/engine-graph/HostIo.test.cpp b/packages/native/test/engine-graph/HostIo.test.cpp
index 3a3eaf34..e83b8f69 100644
--- a/packages/native/test/engine-graph/HostIo.test.cpp
+++ b/packages/native/test/engine-graph/HostIo.test.cpp
@@ -4,9 +4,10 @@
 // rp.host.midi-out (each input under the system its settings name, merged by frame, every frame inside its block) and
 // controller-out, rp.n8.cart-mirror feeds the engine's core-byte sink, the Game Boy's serialOut carries MI.OUT, which
 // LSDj's decoder (rp.lsdj.serial-out) sends on as host MIDI inside each block, its master-sync run carried over a
-// change of the engine's rate, and a full queue's drops reach the engine and the harness RPC. A document holds one
-// midi-in, one controller-in, one midi-out and one cart mirror at most, the mirror naming a NES. Driven on a quiescent
-// Engine, and on a NodeRig where a node's calls on the engine are what is checked.
+// change of the engine's rate, one document switching the capture and the decoder's mode together, and a full queue's
+// drops reach the engine and the harness RPC. A document holds one midi-in, one controller-in, one midi-out and one
+// cart mirror at most, the mirror naming a NES. Driven on a quiescent Engine, and on a NodeRig where a node's calls on
+// the engine are what is checked.
 //
 // Run via `pnpm test:plugin engine-graph`.
 
@@ -1104,6 +1105,62 @@ TEST_CASE("what a Game Boy's core captures decodes through rp.lsdj.serial-out in
     CHECK(e.droppedMessages() == 0);
 }
 
+TEST_CASE("one setGraph switches a Game Boy's capture and its decoder's mode together, in the same block",
+          "[engine-graph][host-io][settings]") {
+    // MI.OUT drives mGB's port, and its decoder reads the idle 0xFF bytes as LSDj's clock; master sync only listens,
+    // and mGB, waiting on an external clock, sends nothing. The capture is a setting of the Game Boy's node and the
+    // mode one of its decoder's (docs/modular-graph-phase-c2-design.md section 4), so one document carries both and
+    // they land in one block: the first block of the plan that carries them captures in the new mode and decodes in it.
+    // Moved by two paths, the capture a block behind, master sync would decode a block of MI.OUT's bytes as clocks,
+    // starting a run (docs/lsdj-master-sync-fix-design.md section 10).
+    Engine e(kRate);
+    REQUIRE(e.parkCore(mgb(1)));
+    const auto switched = [](const char* capture, const char* mode) {
+        const DocExtras extras{
+            {docNode("decoder", "rp.lsdj.serial-out", std::string(R"({"mode":")") + mode + R"("})"), loopOut()},
+            {docWire("sys-1.serialOut", "decoder.serialOut"), docWire("decoder.midiOut", "midi-out.in1")}};
+        auto r = graphRequest(stereoDocument({{1, kGb}}, extras), {1});
+        const std::string core = R"({"core":1})";
+        r.document.replace(r.document.find(core), core.size(),
+                           std::string(R"({"core":1,"serialOutCapture":")") + capture + R"("})");
+        return r;
+    };
+    const auto clocksOnly = [&] {
+        for (const auto& m : e.midiOut())
+            if (m.data != std::vector<std::uint8_t>{0xF8}) return false;
+        return !e.midiOut().empty();
+    };
+    REQUIRE(e.setGraph(switched("drive", "midiOut")).ok);
+    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
+    REQUIRE(gb);
+    // mGB opens its port once the boot ROM has run, about 2.5 s in; then every block clocks.
+    int b = 0;
+    while (b < 200 && e.midiOut().empty()) {
+        block(e);
+        ++b;
+    }
+    REQUIRE(b < 200);
+    block(e);
+    REQUIRE(clocksOnly());
+
+    REQUIRE(e.setGraph(switched("listen", "masterSync")).ok);
+    CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);  // the plan is adopted; the capture waits for its block
+    for (int k = 0; k < 20; ++k) {
+        block(e);
+        INFO("block " << k << " of master sync");
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
+        CHECK(gb->serialOutLog_.empty());
+        CHECK(e.midiOut().empty());
+    }
+
+    REQUIRE(e.setGraph(switched("drive", "midiOut")).ok);
+    block(e);  // its first block captures in drive and decodes in MI.OUT
+    CHECK(gb->serialOutLog_.size() > 64);
+    CHECK(clocksOnly());
+    CHECK(e.scriptThrows() == 0);
+    CHECK(e.droppedMessages() == 0);
+}
+
 TEST_CASE("a full output queue's drops reach the engine and the harness facet", "[engine-graph][host-io]") {
     struct Js {
         JSRuntime* rt  = JS_NewRuntime();
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 2bfa507e..97842358 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -4,7 +4,9 @@
 // across block sizes. Both cores power on in the same state (Deterministic.cpp) and hear the same input.
 // The node's inputs (docs/modular-graph-phase-b3a-design.md section 3.1) are held the same way: what reaches a
 // node's input must render what the core call it maps to renders, made directly; and the Game Boy's serialOut
-// must carry what its core captured.
+// must carry what its core captured. So are its settings' knobs (docs/modular-graph-phase-c2-design.md sections 3 and
+// 4): each must reach the core as the core's own setter, called directly at the start of the block of the plan that
+// carries it, or at activation, renders it, and the descriptors must spell the labels the role configs do.
 //
 // There is no GBA case: Mesen's GBA core needs a BIOS image, and no fixture here carries one.
 //
@@ -12,16 +14,21 @@
 
 #include <algorithm>
 #include <array>
+#include <atomic>
+#include <chrono>
 #include <cstdint>
 #include <cstring>
 #include <functional>
 #include <memory>
 #include <string>
+#include <thread>
 #include <vector>
 
 #include <catch2/catch_test_macros.hpp>
 
+#include "../graph/CountingAllocator.hpp"
 #include "NodeRig.hpp"
+#include "graph/CanonicalJson.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
 #include "system/mesen/MesenSmsConfig.hpp"
@@ -787,3 +794,490 @@ TEST_CASE("a system node records what its bytes input feeds the core in the engi
     CHECK(rig.traced[0].frame == 5);
     CHECK(rig.traced[0].data == std::vector<std::uint8_t>{1, 2, 3});
 }
+
+// ---- settings ----------------------------------------------------------------------------------------------------
+// A system node's knobs (docs/modular-graph-phase-c2-design.md sections 3 and 4) are its settings beside `core`: each
+// reaches the core at the start of the first block of the plan that carries it, as the call the command ring made
+// (the core's own value-guarded setter), and everything present at activation, before the core first runs. A knob
+// absent leaves the core as it was. Compared, where the knob is audible, against an identical core given the same
+// call directly at the same block: bit for bit, so the knob landed in that block and restarted nothing the direct
+// call did not.
+
+namespace {
+
+// A system node "sys" of `type` on its core's token, `knobs` its settings after the core (JSON members, or none), its
+// mix on a stereo host pair; a Game Boy's serialOut on a probe.
+std::string knobbed(const char* type, std::uint64_t token, const std::string& knobs = "") {
+    const bool gb = std::string(type) == "rp.sameboy.gb";
+    return R"({"nodes":[{"id":"sys","type":")" + std::string(type) + R"(","settings":{"core":)" + std::to_string(token) +
+           (knobs.empty() ? "" : "," + knobs) + R"(}},)" + (gb ? R"({"id":"probe","type":"rp.test.probe"},)" : "") +
+           R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],"wires":[{"from":"sys.mix","to":"out.out.1-2"})" +
+           (gb ? R"(,{"from":"sys.serialOut","to":"probe.bytes"})" : "") + "]}";
+}
+
+std::string codes(const rp::graph::SetGraphResult& r) {
+    std::string s;
+    for (const auto& e : r.errors) s += (s.empty() ? "" : ",") + e.code;
+    return s;
+}
+
+void run(NodeRig& rig, std::uint32_t frames = 512, int blocks = 1) {
+    Lanes out(2);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    for (int b = 0; b < blocks; ++b) rig.host->processBlock(frames, t, out.ptrs.data(), 2);
+}
+
+// The quoted strings of `export const <name> = [...] as const;` in packages/retroplug/src/settingsEnums.ts.
+std::vector<std::string> tsValues(const std::string& name) {
+    const auto bytes = readFile(RP_SETTINGS_ENUMS_PATH);
+    const std::string ts(bytes.begin(), bytes.end());
+    const std::string head = "export const " + name + " = [";
+    const auto start = ts.find(head);
+    if (start == std::string::npos) return {};
+    const auto end = ts.find("] as const;", start);
+    std::vector<std::string> values;
+    for (auto q = ts.find('"', start); q < end; q = ts.find('"', q + 1)) {
+        const auto close = ts.find('"', q + 1);
+        values.push_back(ts.substr(q + 1, close - q - 1));
+        q = close;
+    }
+    return values;
+}
+
+// The settings schema of a registered type, at its $defs entry.
+rfl::Generic::Object settingsSchema(NodeRig& rig, const char* type) {
+    const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
+    REQUIRE(descriptor);
+    const auto settings = descriptor->to_object()->get("settings")->to_object();
+    REQUIRE(settings);
+    const auto defs = settings->get("$defs")->to_object();
+    REQUIRE(defs);
+    REQUIRE(defs->size() == 1);
+    return *defs->begin()->second.to_object();
+}
+
+// A property's schema: the non-null branch of the anyOf an optional field is.
+rfl::Generic::Object knobSchema(const rfl::Generic::Object& schema, const char* knob) {
+    const auto property = schema.get("properties")->to_object()->get(knob)->to_object();
+    REQUIRE(property);
+    const auto branches = property->get("anyOf")->to_array();
+    REQUIRE(branches);
+    REQUIRE(branches->size() == 2);
+    return *(*branches)[0].to_object();
+}
+
+std::vector<std::string> labelsOf(const rfl::Generic::Object& knob) {
+    std::vector<std::string> labels;
+    const auto values = knob.get("enum")->to_array();
+    REQUIRE(values);
+    for (const auto& v : *values) labels.push_back(*v.to_string());
+    return labels;
+}
+
+}  // namespace
+
+TEST_CASE("every system node prepares statelessly and rebuilds only for a new core, so setGraph never waits on one",
+          "[engine-graph][system-node][settings]") {
+    NodeRig rig(kRate);
+    for (const char* type : {"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"}) {
+        INFO(type);
+        CHECK(rig.registry.find(type)->type->flags == RP_TYPE_STATELESS_PREPARE);
+    }
+    auto built = mgb(1);
+    SameBoySystem* const gb = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    run(rig);
+    rp_node* const before = rig.host->node("sys");
+    rig.host->setAudioThreadOwns(true);  // and no block runs until the end, so nothing is adopted
+    // Two edits, the second planned while the first is unadopted: neither waits (the adoption timeout is 2 s).
+    const auto start = std::chrono::steady_clock::now();
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("dmgPalette":"dmg")")).ok);
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("dmgPalette":"gbl","highpass":"off")")).ok);
+    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500));
+    CHECK(rig.host->node("sys") == before);
+    run(rig);  // adopts both, in order, and the node applies the last
+    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
+    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+    rig.host->setAudioThreadOwns(false);
+}
+
+TEST_CASE("a system node's enum knobs take the labels the role configs spell, and its numbers their ranges",
+          "[engine-graph][system-node][settings]") {
+    NodeRig rig(kRate);
+    const auto gb = settingsSchema(rig, "rp.sameboy.gb");
+    CHECK(labelsOf(knobSchema(gb, "model")) == tsValues("MODEL_VALUES"));
+    CHECK(labelsOf(knobSchema(gb, "highpass")) == tsValues("HIGHPASS_VALUES"));
+    CHECK(labelsOf(knobSchema(gb, "colorCorrection")) == tsValues("COLOR_CORRECTION_VALUES"));
+    CHECK(labelsOf(knobSchema(gb, "dmgPalette")) == tsValues("DMG_PALETTE_VALUES"));
+    CHECK(labelsOf(knobSchema(gb, "serialOutCapture")) == std::vector<std::string>{"off", "listen", "drive"});
+    CHECK(*knobSchema(gb, "fastBoot").get("type")->to_string() == "boolean");
+    const auto nesSchema = settingsSchema(rig, "rp.mesen.nes");
+    CHECK(labelsOf(knobSchema(nesSchema, "region")) == tsValues("REGION_VALUES"));
+    CHECK(labelsOf(knobSchema(nesSchema, "s5bNoise")) == tsValues("CARTRIDGE_ACCURACY_VALUES"));
+    CHECK(labelsOf(knobSchema(nesSchema, "mmc5PhaseReset")) == tsValues("CARTRIDGE_ACCURACY_VALUES"));
+    CHECK(*knobSchema(nesSchema, "removeSpriteLimit").get("type")->to_string() == "boolean");
+    CHECK(tsValues("MODEL_VALUES").size() == 14);  // the reader found them
+    // A range's bounds, each its own allOf branch.
+    const auto bounds = [](const rfl::Generic::Object& knob) {
+        std::vector<double> b;
+        const auto branches = knob.get("allOf")->to_array();
+        REQUIRE(branches);
+        for (const auto& branch : *branches)
+            for (const char* key : {"minimum", "maximum"})
+                if (const auto v = branch.to_object()->get(key)) b.push_back(*v->to_double());
+        return b;
+    };
+    CHECK(bounds(knobSchema(gb, "lightTemperature")) == std::vector<double>{-1.0, 1.0});
+    CHECK(bounds(knobSchema(nesSchema, "apuLatencyMs")) == std::vector<double>{0.25, 6.0});
+    for (const char* type : {"rp.mesen.gba", "rp.mesen.sms"}) {
+        INFO(type);
+        CHECK(settingsSchema(rig, type).get("properties")->to_object()->size() == 1);  // core alone
+    }
+
+    // Anything else is refused with the document, the core as it was: an ordinal, another spelling, a number out of
+    // its range.
+    auto built = mgb(1);
+    SameBoySystem* const core = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    for (const char* refused : {R"("model":9)", R"("model":"CgbC")", R"("model":"cgb-c")", R"("highpass":"removeDCOffset")",
+                                R"("lightTemperature":1.5)", R"("serialOutCapture":"loud")", R"("fastBoot":"yes")"}) {
+        INFO(refused);
+        const auto r = rig.set(knobbed("rp.sameboy.gb", token, refused));
+        CHECK_FALSE(r.ok);
+        CHECK(codes(r) == "settings-invalid");
+    }
+    run(rig);
+    CHECK(core->config_.model == SameBoyModel::DmgB);
+    CHECK(core->config_.highpass == SameBoyHighpass::Accurate);
+    const auto nesToken = rig.park(nes(2, 0));
+    for (const char* refused : {R"("apuLatencyMs":0.2)", R"("apuLatencyMs":7)", R"("region":"PAL")", R"("s5bNoise":1)"}) {
+        INFO(refused);
+        CHECK(codes(rig.set(knobbed("rp.mesen.nes", nesToken, refused))) == "settings-invalid");
+    }
+}
+
+TEST_CASE("each knob reaches its core at the start of the next block of the plan that carries it",
+          "[engine-graph][system-node][settings]") {
+    SECTION("Game Boy") {
+        NodeRig rig(kRate);
+        auto built = mgb(1);
+        SameBoySystem* const gb = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+        run(rig);
+        // The model's restart has its own case below; every other knob here, each off what mgb() built.
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token,
+                                R"("model":"dmgB","fastBoot":true,"highpass":"removeDcOffset","colorCorrection":"modernBalanced",)"
+                                R"("dmgPalette":"mgb","lightTemperature":-0.5,"serialOutCapture":"listen")"))
+                    .ok);
+        // Adopted as it was published (the rig is quiescent), but applied as the next block starts.
+        CHECK(gb->config_.highpass == SameBoyHighpass::Accurate);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Off);
+        run(rig);
+        CHECK(gb->config_.model == SameBoyModel::DmgB);
+        CHECK(gb->config_.fastBoot == true);
+        CHECK(gb->config_.highpass == SameBoyHighpass::RemoveDcOffset);
+        CHECK(gb->config_.colorCorrection == SameBoyColorCorrection::ModernBalanced);
+        CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Mgb);
+        CHECK(gb->config_.lightTemperature == -0.5);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("serialOutCapture":"drive")")).ok);
+        run(rig);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
+    }
+    SECTION("NES") {
+        NodeRig rig(kRate);
+        auto built = nes(1, 0);
+        MesenNesSystem* const core = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token)).ok);
+        run(rig);
+        const std::uint32_t window = core->apuFlushCycleLength();
+        REQUIRE(core->config().s5bNoise == 1);  // MesenNesConfig's defaults, "n8"
+        REQUIRE(core->config().mmc5PhaseReset == 1);
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token,
+                                R"("region":"pal","removeSpriteLimit":true,"apuLatencyMs":3.5,"s5bNoise":"chip","mmc5PhaseReset":"chip")"))
+                    .ok);
+        CHECK(core->config().region == 0);
+        run(rig);
+        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Pal));
+        CHECK(core->config().removeSpriteLimit);
+        CHECK(core->config().apuLatencyMs == 3.5);
+        CHECK(core->apuFlushCycleLength() != window);  // the live window, re-thresholded
+        CHECK(core->config().s5bNoise == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
+        CHECK(core->config().mmc5PhaseReset == static_cast<std::uint32_t>(CartridgeAccuracy::Chip));
+    }
+}
+
+TEST_CASE("a knob restarts or resets its core only where the core's own setter does, in the block the plan lands",
+          "[engine-graph][system-node][settings]") {
+    // The graph core against a direct one given the same setter calls at the same block, bit for bit, and against a
+    // control that hears none: where the knob is audible, the direct core differs from the control from that block on.
+    std::uint64_t heard = kNever;
+
+    SECTION("Game Boy: highpass and the display group apply in place") {
+        NodeRig rig(kRate);
+        auto direct = mgb(1), control = mgb(1);
+        const auto token = rig.park(mgb(1));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
+        // During the boot chime, so the filter is heard; a restart would replay the chime and break the match.
+        CHECK(compare(rig, d, 3.0, once(0.3, [&] {
+                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", token,
+                                          R"("highpass":"off","dmgPalette":"gbl","colorCorrection":"lowContrast","lightTemperature":0.25)"))
+                              .ok);
+                  direct->setHighpass(SameBoyHighpass::Off);
+                  direct->setDmgPalette(SameBoyDmgPalette::Gbl);
+                  direct->setColorCorrection(SameBoyColorCorrection::LowContrast);
+                  direct->setLightTemperature(0.25);
+              }), &c, &heard) == "");
+        CHECK(heard >= static_cast<std::uint64_t>(0.3 * kRate));
+        CHECK(heard != kNever);
+    }
+    SECTION("Game Boy: a palette alone changes no sample") {
+        NodeRig rig(kRate);
+        auto control = mgb(1);
+        const auto token = rig.park(mgb(1));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+        Direct c({control.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, c, 3.0, once(0.3, [&] {
+                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("dmgPalette":"dmg")")).ok);
+              })) == "");
+        CHECK(sounds(c));
+    }
+    SECTION("Game Boy: a model change restarts the core") {
+        NodeRig rig(kRate);
+        auto direct = mgb(1), control = mgb(1);
+        const auto token = rig.park(mgb(1));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"dmgB")")).ok);
+        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, d, 4.0, once(2.0, [&] {
+                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"mgb")")).ok);
+                  direct->setModel(SameBoyModel::Mgb);
+              }), &c, &heard) == "");
+        CHECK(heard >= static_cast<std::uint64_t>(2.0 * kRate));  // the chime again, from the block it landed in
+        CHECK(heard != kNever);
+    }
+    SECTION("NES: the sprite limit, latency and accuracy switches apply in place, a region change resets it") {
+        NodeRig rig(kRate);
+        auto direct = nes(1, 0), control = nes(1, 0);
+        auto viaGraph = nes(1, 0);
+        SystemBase* const graphCore = viaGraph.get();
+        const auto token = rig.park(std::move(viaGraph));
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token)).ok);
+        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
+        auto note = [&, sent = false](std::uint64_t at) mutable {  // as afterBoot, to all three
+            if (sent || at < static_cast<std::uint64_t>(1.5 * kRate)) return;
+            for (SystemBase* s : {graphCore, static_cast<SystemBase*>(direct.get()), static_cast<SystemBase*>(control.get())})
+                noteOn(*s, 0, 60);
+            sent = true;
+        };
+        auto inPlace = once(2.0, [&] {
+            REQUIRE(rig.set(knobbed("rp.mesen.nes", token, R"("removeSpriteLimit":true,"s5bNoise":"chip","mmc5PhaseReset":"chip")")).ok);
+            direct->setRemoveSpriteLimit(true);
+            direct->setS5bNoise(0);
+            direct->setMmc5PhaseReset(0);
+        });
+        auto reset = once(3.0, [&] {
+            REQUIRE(rig.set(knobbed("rp.mesen.nes", token,
+                                    R"("removeSpriteLimit":true,"s5bNoise":"chip","mmc5PhaseReset":"chip","region":"pal")"))
+                        .ok);
+            direct->setRegion(static_cast<std::uint32_t>(NesRegion::Pal));
+        });
+        CHECK(compare(rig, d, 4.0, [&](std::uint64_t at) { note(at); inPlace(at); reset(at); }, &c, &heard) == "");
+        CHECK(heard >= static_cast<std::uint64_t>(3.0 * kRate));  // nothing heard of the switches; the reset is
+        CHECK(heard != kNever);
+    }
+}
+
+TEST_CASE("a system node gives its core every knob its settings carry as it takes the core, before the core first runs",
+          "[engine-graph][system-node][settings]") {
+    SECTION("Game Boy, its capture included") {
+        NodeRig rig(kRate);
+        auto direct = mgb(1);
+        auto built = mgb(1);
+        SameBoySystem* const gb = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"mgb","highpass":"off","serialOutCapture":"drive")")).ok);
+        // Activated as it was planned: no block has run.
+        CHECK(gb->config_.model == SameBoyModel::Mgb);
+        CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
+        direct->setModel(SameBoyModel::Mgb);
+        direct->setHighpass(SameBoyHighpass::Off);
+        direct->setSerialOutCapture(SerialOutCapture::Drive);
+        // From the first block on, the graph core renders and captures what the direct one, given the same calls
+        // before its first block, does: mGB opens its port once the boot ROM has run, about 2.5 s in.
+        Direct d({direct.get()}, 2, {{0, 1}});
+        Lanes graph(2);
+        const rp_transport t{kRate, 120.0, 0.0, 0};
+        std::size_t captured = 0;
+        for (int b = 0; b < 160; ++b) {
+            rig.host->processBlock(1024, t, graph.ptrs.data(), 2);
+            d.block(1024);
+            INFO("block " << b);
+            REQUIRE(std::memcmp(graph.data[0].data(), d.out.data[0].data(), 1024 * sizeof(float)) == 0);
+            std::vector<std::uint8_t> carried, logged;
+            for (const auto& m : rig.node<Probe>("probe").bytes) carried.insert(carried.end(), m.data.begin(), m.data.end());
+            for (const auto& entry : direct->serialOutLog_) logged.push_back(entry.second);
+            REQUIRE(carried == logged);
+            captured += carried.size();
+        }
+        CHECK(captured > 0);
+        CHECK(sounds(d));
+    }
+    SECTION("NES") {
+        NodeRig rig(kRate);
+        auto built = nes(1, 0);
+        MesenNesSystem* const core = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token, R"("region":"dendy","apuLatencyMs":2,"s5bNoise":"chip")")).ok);
+        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Dendy));
+        CHECK(core->config().apuLatencyMs == 2.0);
+        CHECK(core->config().s5bNoise == 0);
+        CHECK(core->config().mmc5PhaseReset == 1);  // absent: as built
+    }
+}
+
+TEST_CASE("a knob absent from a system node's settings leaves its core as it was built, or as the last document left it",
+          "[engine-graph][system-node][settings]") {
+    SECTION("Game Boy") {
+        // Every knob built away from SameBoyConfig's defaults, and the capture armed, as buildCore arms it.
+        const auto build = [] {
+            SameBoyConfig cfg;
+            cfg.model = SameBoyModel::Mgb;
+            cfg.fastBoot = false;
+            cfg.highpass = SameBoyHighpass::RemoveDcOffset;
+            cfg.colorCorrection = SameBoyColorCorrection::ReduceContrast;
+            cfg.dmgPalette = SameBoyDmgPalette::Gbl;
+            cfg.lightTemperature = 0.75;
+            auto sys = std::make_unique<SameBoySystem>(1, cfg, readFile(RP_MGB_ROM_PATH));
+            sys->onActivate(kRate);
+            sys->setSerialOutCapture(SerialOutCapture::Drive);
+            return sys;
+        };
+        NodeRig rig(kRate);
+        auto direct = build();
+        auto built = build();
+        SameBoySystem* const gb = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+        Direct d({direct.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, d, 2.0) == "");  // nothing restarted, nothing re-filtered
+        CHECK(gb->config_.model == SameBoyModel::Mgb);
+        CHECK(gb->config_.fastBoot == false);
+        CHECK(gb->config_.highpass == SameBoyHighpass::RemoveDcOffset);
+        CHECK(gb->config_.colorCorrection == SameBoyColorCorrection::ReduceContrast);
+        CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
+        CHECK(gb->config_.lightTemperature == 0.75);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
+
+        // Present, then dropped: the core keeps what the last document carrying it gave it.
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("highpass":"off","serialOutCapture":"listen")")).ok);
+        run(rig);
+        REQUIRE(gb->config_.highpass == SameBoyHighpass::Off);
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+        run(rig, 512, 4);
+        CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+        CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
+    }
+    SECTION("NES") {
+        MesenNesConfig cfg;
+        cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
+        cfg.region = static_cast<std::uint32_t>(NesRegion::Pal);
+        cfg.removeSpriteLimit = true;
+        cfg.apuLatencyMs = 3.0;
+        cfg.s5bNoise = 0;
+        cfg.mmc5PhaseReset = 0;
+        auto built = std::make_unique<MesenNesSystem>(1, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
+        built->onActivate(kRate);
+        MesenNesSystem* const core = built.get();
+        NodeRig rig(kRate);
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token)).ok);
+        run(rig, 512, 4);
+        CHECK(core->config().region == static_cast<std::uint32_t>(NesRegion::Pal));
+        CHECK(core->config().removeSpriteLimit);
+        CHECK(core->config().apuLatencyMs == 3.0);
+        CHECK(core->config().s5bNoise == 0);
+        CHECK(core->config().mmc5PhaseReset == 0);
+    }
+}
+
+TEST_CASE("a Game Boy node's settings commit, and every knob it applies in place, allocate nothing on the block thread",
+          "[engine-graph][system-node][settings]") {
+    // Only a model's restart allocates, as it did through the command ring. (A NES's block allocates on its own, in
+    // Mesen, and so do its core's setters, which write Mesen's settings, whichever path calls them; the node calls them
+    // as the ring's handler did.)
+    NodeRig rig(kRate);
+    auto built = mgb(1);
+    SameBoySystem* const gb = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    Lanes out(2);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
+    rig.host->setAudioThreadOwns(true);  // so the commit happens in a block, as it does under an audio thread
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token,
+                            R"("model":"dmgB","fastBoot":true,"highpass":"off","colorCorrection":"modernAccurate",)"
+                            R"("dmgPalette":"gbl","lightTemperature":0.5,"serialOutCapture":"listen")"))
+                .ok);
+    rp::test::beginCountingAllocations();
+    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
+    const auto counted = rp::test::endCountingAllocations();
+    rig.host->setAudioThreadOwns(false);
+    CHECK(counted.allocations == 0);
+    CHECK(counted.frees == 0);
+    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);
+    CHECK(gb->serialOutCapture() == SerialOutCapture::Listen);
+}
+
+TEST_CASE("a Game Boy node takes knob edits published while an audio thread runs its blocks", "[engine-graph][system-node][settings]") {
+    // The race check for the settings path under ThreadSanitizer (build-tsan/): each edit is planned on this thread,
+    // never waiting, while the audio thread commits the last one and applies it, and the plans it hands back are freed
+    // here with the settings they held.
+    NodeRig rig(kRate);
+    auto built = mgb(1);
+    SameBoySystem* const gb = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    const char* const palettes[] = {"grey", "dmg", "mgb", "gbl"};
+    const char* const captures[] = {"off", "listen", "drive"};
+    const auto edit = [&](int i) {
+        return knobbed("rp.sameboy.gb", token,
+                       std::string(R"("dmgPalette":")") + palettes[i % 4] + R"(","serialOutCapture":")" + captures[i % 3] +
+                           R"(","highpass":")" + (i % 2 ? "off" : "accurate") + R"(","lightTemperature":)" +
+                           std::to_string((i % 5) * 0.25 - 0.5));
+    };
+    std::atomic<bool> running{true};
+    std::atomic<int>  blocks{0};
+    rig.host->setAudioThreadOwns(true);
+    std::thread audio([&] {
+        Lanes out(2);
+        const rp_transport t{kRate, 120.0, 0.0, 0};
+        while (running.load(std::memory_order_acquire)) {
+            rig.host->processBlock(256, t, out.ptrs.data(), 2);
+            blocks.fetch_add(1, std::memory_order_release);
+        }
+    });
+    int accepted = 0;
+    constexpr int kEdits = 60;
+    for (int i = 0; i < kEdits; ++i) {
+        accepted += rig.set(edit(i)).ok ? 1 : 0;
+        rig.host->reclaim();
+        const int seen = blocks.load(std::memory_order_acquire);
+        while (blocks.load(std::memory_order_acquire) < seen + 2) std::this_thread::yield();  // let a block or two run
+    }
+    running.store(false, std::memory_order_release);
+    audio.join();
+    rig.host->setAudioThreadOwns(false);
+    CHECK(accepted == kEdits);
+    run(rig);  // whatever the audio thread left unapplied
+    CHECK(gb->config_.dmgPalette == SameBoyDmgPalette::Gbl);  // the last edit's, 59
+    CHECK(gb->serialOutCapture() == SerialOutCapture::Drive);
+    CHECK(gb->config_.highpass == SameBoyHighpass::Off);
+    CHECK(gb->config_.lightTemperature == 0.5);
+}
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake -S . -B build && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: FAIL: the build stops at the names this task adds: `Engine::setParam`, `MesenNesSystem::config`, the SameBoy setters (`setModel`, `setHighpass`, ...) and the NES enums. (The prototype's red run, with those names in place and the old system nodes, saw most of the new settings cases fail.)

- [ ] **Step 3: Give the cores value-guarded setters and the enums the settings map to**

SameBoy gains `setModel`, `setHighpass`, `setColorCorrection`, `setDmgPalette` and `setLightTemperature`, each
value-guarded as `applyConfigField` was (which now calls them, unchanged in behaviour, so Task 4 loses nothing when it
deletes it); the NES config gains `NesRegion` (Mesen's ConsoleRegion, value for value) and `CartridgeAccuracy`, and
the system a read-only `config()` for the tests:

````diff
diff --git a/packages/native/src/system/mesen/MesenNesConfig.hpp b/packages/native/src/system/mesen/MesenNesConfig.hpp
index 5ea65609..d76c06fd 100644
--- a/packages/native/src/system/mesen/MesenNesConfig.hpp
+++ b/packages/native/src/system/mesen/MesenNesConfig.hpp
@@ -10,6 +10,23 @@
 // Mirrors SameBoyConfig's shape (embed / rom / sram / savestate) so the system
 // backends handle the concrete configs uniformly.
 
+// The console region, Mesen's ConsoleRegion ordinal for ordinal (Core/Shared/SettingTypes.h, a header this one keeps
+// out of the rest of the codebase), so MesenNesConfig::region holds either.
+enum class NesRegion : std::uint32_t {
+    Auto      = 0,
+    Ntsc      = 1,
+    Pal       = 2,
+    Dendy     = 3,
+    NtscJapan = 4,
+};
+
+// A cartridge-accuracy switch (MesenNesConfig::s5bNoise, mmc5PhaseReset): the chip as documented, or as an
+// Everdrive N8 Pro's FPGA core has it.
+enum class CartridgeAccuracy : std::uint32_t {
+    Chip = 0,
+    N8   = 1,
+};
+
 struct MesenNesConfig {
     // On-disk variant discriminator (`"kind":"nes"`).
     using Tag = rfl::Literal<"nes">;
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 06900e86..7a45ca72 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -110,7 +110,8 @@ public:
 
     void setGainDb(float dB) override;
 
-    // "mesen" system-role knobs (coreRoles.ts), applied live via Engine::applyConfigField.
+    // "mesen" system-role knobs (coreRoles.ts), applied live by the NES's system node (its settings) and by
+    // Engine::applyConfigField. Each but setRemoveSpriteLimit is value-guarded, and that one is a plain flag write.
     // removeSpriteLimit is a live PPU toggle; region reconfigures timing so it forces a reset.
     void setRemoveSpriteLimit(bool on);
     void setRegion(std::uint32_t region);
@@ -136,6 +137,8 @@ public:
     // The live APU flush window in CPU cycles (the mixer's conversion of apuLatencyMs against the region
     // clock). 0 before onActivate. Exposed for tests / introspection.
     std::uint32_t apuFlushCycleLength() const;
+    // The config the setters above keep, read-only. Exposed for tests / introspection.
+    const MesenNesConfig& config() const { return config_; }
 
     // NES native resolution (256x240). Public so callers can construct a
     // FrameBufferTriple-sized read buffer without hard-coding the constant.
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index 8cce43fa..b56ae7fc 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -159,7 +159,7 @@ namespace {
 
 // Extra savestate capacity so a LIVE model switch never outgrows the snapshot slot.
 //
-// A model change rebuilds gb_ in place (Engine::applyConfigField → restartEmulator) while the system
+// A model change rebuilds gb_ in place (setModel → restartEmulator) while the system
 // keeps its id, so the slot allocated at construct has to serve every model the user can cycle to. It
 // cannot be resized on the fly: the publish can run on the audio thread, and SnapshotRegistry sized its
 // shadow slot from our capacity at claim.
@@ -513,6 +513,38 @@ void SameBoySystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
     pendingMidi_.insert(pendingMidi_.end(), events, events + count);
 }
 
+void SameBoySystem::setModel(SameBoyModel model) {
+    if (config_.model == model) return;
+    config_.model = model;
+    // Rebuilds gb_, clearing the savestate (a new model cannot restore an old one) and re-arming the state snapshot
+    // onto the new model. linkPeers_ hold the systems, which survive the restart, so the cable needs no rewiring.
+    restartEmulator();
+}
+
+void SameBoySystem::setHighpass(SameBoyHighpass highpass) {
+    if (config_.highpass == highpass) return;
+    config_.highpass = highpass;
+    applyHighpassMode();  // live: the filter samples its mode every audio frame
+}
+
+void SameBoySystem::setColorCorrection(SameBoyColorCorrection mode) {
+    if (config_.colorCorrection == mode) return;
+    config_.colorCorrection = mode;
+    applyDisplayConfig();
+}
+
+void SameBoySystem::setDmgPalette(SameBoyDmgPalette palette) {
+    if (config_.dmgPalette == palette) return;
+    config_.dmgPalette = palette;
+    applyDisplayConfig();
+}
+
+void SameBoySystem::setLightTemperature(double temperature) {
+    if (config_.lightTemperature == temperature) return;
+    config_.lightTemperature = temperature;
+    applyDisplayConfig();
+}
+
 // TODO: Remove references to old code in comments
 
 void SameBoySystem::applyHighpassMode() {
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index 7b8c5405..7b306d1f 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -105,8 +105,8 @@ public:
 
     // Tear down `gb_` and rebuild it with the current `config_`. Snapshots
     // SRAM through `config_.sram` so it survives the cycle; clears
-    // `config_.savestate` (model-specific). Called from the DSP command
-    // drain when the user changes Model from the menu — the allocations
+    // `config_.savestate` (model-specific). Called by setModel, on the
+    // block thread, when the user changes Model from the menu: the allocations
     // inside `GB_init` are the same ones a fresh Load Project does, so
     // accepting them on the audio thread for an explicit user action is
     // consistent with that precedent.
@@ -114,15 +114,25 @@ public:
 
     // Push `config_.highpass` into the SameBoy core. Safe to call at runtime
     // (the filter samples its mode every audio frame). Called from
-    // `onActivate` and from the SetHighpass command handler.
+    // `onActivate` and from setHighpass.
     void applyHighpassMode();
 
     // Push the whole `config_` display group (colour correction, DMG palette, light temperature,
     // background/object rendering) into the core. Safe to call at runtime — every one lands on the
-    // next rendered frame, so no restart. Called from `onActivate` and from the display config
-    // handlers in Engine::applyConfigField.
+    // next rendered frame, so no restart. Called from `onActivate` and from the display setters below.
     void applyDisplayConfig();
 
+    // The live "sameboy" role knobs, as the Game Boy's system node and Engine::applyConfigField apply them. Each is
+    // value-guarded, a same-value call doing nothing, since a caller may hand over a whole config when one knob
+    // moved: a model change restarts the core (restartEmulator), highpass re-applies the filter mode, and the three
+    // display knobs re-apply the display group, none of those restarting anything. setFastBoot and
+    // setSerialOutCapture are the other two.
+    void setModel(SameBoyModel model);
+    void setHighpass(SameBoyHighpass highpass);
+    void setColorCorrection(SameBoyColorCorrection mode);
+    void setDmgPalette(SameBoyDmgPalette palette);
+    void setLightTemperature(double temperature);
+
     // SystemBase virtuals — see base class for contracts.
     const std::string&        romPath() const override          { return config_.romPath; }
     std::uint32_t             savSuffix() const override         { return config_.savSuffix; }
````

- [ ] **Step 4: The system nodes' settings, and Engine::setParam**

`GameBoySettings` and `NesSettings` (GBA and SMS keep `{core}`); enum knobs are `knobs::Label<E>`, an `rfl::Literal`
made at compile time from the enum's enumerators with their first letter lowered, which is exactly how the TS role
configs spell them, so native has one spelling and the schema lists them; ranges are refused. `on_settings` keeps a
pointer to the committed settings and a pending flag (it allocates nothing); `prepare` applies what moved before the
inputs and `prepareForBlock`, so a linked Game Boy's coupled group applies its knobs too; `activate` applies every
present knob after taking its core. `Engine::setParam` wraps the graph host's at frame 0, and `coreIdOf` no longer
requires `core` to be a system node's only setting:

````diff
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 68c6cb29..a8739744 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -58,11 +58,12 @@ bool isSystemNodeType(const std::string& type) {
     return false;
 }
 
-// The id a system node's settings name its core by, {"core": <id>}; nullopt for anything else.
+// The id a system node's settings name its core by, their "core" (beside the core's knobs, which the node reads:
+// docs/modular-graph-phase-c2-design.md section 3); nullopt for anything else.
 std::optional<SystemId> coreIdOf(const rp::graph::NodeDoc& n) {
     if (!n.settings) return std::nullopt;
     const auto object = n.settings->to_object();
-    if (!object || object->size() != 1) return std::nullopt;
+    if (!object) return std::nullopt;
     const auto core = object->get("core");
     if (!core) return std::nullopt;
     const auto id = core->to_int64();
@@ -277,14 +278,14 @@ Engine::GraphResult Engine::publishGraph(const GraphRequest& request, bool repla
     auto doc = rp::graph::parseGraphDocument(request.document);
     if (!doc) return refuse("", "document", std::string("not a graph document: ") + doc.error().what());
 
-    // The system nodes and the slots name the same systems, each node as sys-<id> with {"core": <id>}.
+    // The system nodes and the slots name the same systems, each node as sys-<id>, its settings {"core": <id>, ...}.
     std::vector<std::pair<SystemId, const rp::graph::NodeDoc*>> systemNodes;
     std::vector<SystemId> named;
     for (const auto& n : doc->nodes) {
         if (!isSystemNodeType(n.type)) continue;
         const auto id = coreIdOf(n);
         if (!id || n.id != "sys-" + std::to_string(*id))
-            return refuse(n.id, "slots", "a system node is sys-<id>, its settings {\"core\": <id>}");
+            return refuse(n.id, "slots", "a system node is sys-<id>, its settings naming its core as {\"core\": <id>}");
         systemNodes.emplace_back(*id, &n);
         named.push_back(*id);
     }
@@ -398,6 +399,10 @@ Engine::GraphResult Engine::publishGraph(const GraphRequest& request, bool repla
     return out;
 }
 
+bool Engine::setParam(std::string_view nodeId, std::string_view paramId, double value) {
+    return graph_->host->setParam(nodeId, paramId, value);
+}
+
 void Engine::drainCommands() { graph_->host->drain(); }
 
 // The script host knows first that an audio thread will run blocks, and last that it has stopped: between the two, a
@@ -925,28 +930,17 @@ void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value)
         return;
     }
 
-    // SameBoy emulator knobs (model / highpass / link group / fast boot).
+    // SameBoy emulator knobs, through the core's own value-guarded setters, which the Game Boy's system node applies
+    // its settings through too.
     auto* sb = dynamic_cast<SameBoySystem*>(sys);
     if (!sb) return;
     switch (static_cast<ConfigField>(field)) {
-        case ConfigField::Model: {
-            const auto m = static_cast<SameBoyModel>(static_cast<std::uint32_t>(value));
-            if (sb->config_.model != m) {
-                sb->config_.model = m;
-                sb->restartEmulator();  // rebuilds gb_, clears the savestate (a new model can't restore an
-                                        // old one), and re-arms the state snapshot onto the new model
-                // linkPeers_ hold the systems, which survive the restart, so the cable needs no rewiring
-            }
+        case ConfigField::Model:
+            sb->setModel(static_cast<SameBoyModel>(static_cast<std::uint32_t>(value)));  // restarts on a change
             break;
-        }
-        case ConfigField::Highpass: {
-            const auto hp = static_cast<SameBoyHighpass>(static_cast<std::uint32_t>(value));
-            if (sb->config_.highpass != hp) {
-                sb->config_.highpass = hp;
-                sb->applyHighpassMode();  // live — the filter samples its mode every audio frame
-            }
+        case ConfigField::Highpass:
+            sb->setHighpass(static_cast<SameBoyHighpass>(static_cast<std::uint32_t>(value)));
             break;
-        }
         case ConfigField::LinkGroup: {
             const auto g = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
             if (sb->config_.linkGroupId != g) {
@@ -965,30 +959,16 @@ void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value)
                                                                                               : SerialOutCapture::Off);
             break;
         }
-        // The display group. Each is value-guarded like the knobs above (the store re-sends the whole
-        // role config on every edit, so an unguarded apply would re-push all five on every keystroke)
-        // and then re-pushes the group, which is a handful of cheap core setters.
-        case ConfigField::ColorCorrection: {
-            const auto cc = static_cast<SameBoyColorCorrection>(static_cast<std::uint32_t>(value));
-            if (sb->config_.colorCorrection != cc) {
-                sb->config_.colorCorrection = cc;
-                sb->applyDisplayConfig();
-            }
+        // The display group: each setter re-pushes the group, a handful of cheap core setters, only when its knob
+        // moved (the store re-sends the whole role config on every edit).
+        case ConfigField::ColorCorrection:
+            sb->setColorCorrection(static_cast<SameBoyColorCorrection>(static_cast<std::uint32_t>(value)));
             break;
-        }
-        case ConfigField::DmgPalette: {
-            const auto p = static_cast<SameBoyDmgPalette>(static_cast<std::uint32_t>(value));
-            if (sb->config_.dmgPalette != p) {
-                sb->config_.dmgPalette = p;
-                sb->applyDisplayConfig();
-            }
+        case ConfigField::DmgPalette:
+            sb->setDmgPalette(static_cast<SameBoyDmgPalette>(static_cast<std::uint32_t>(value)));
             break;
-        }
         case ConfigField::LightTemperature:
-            if (sb->config_.lightTemperature != value) {
-                sb->config_.lightTemperature = value;
-                sb->applyDisplayConfig();
-            }
+            sb->setLightTemperature(value);
             break;
         default:
             break;  // the universal fields (Gain / ReloadOnRomChange) were handled above
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index ada97735..4beb8b61 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -10,6 +10,7 @@
 #include <set>
 #include <span>
 #include <string>
+#include <string_view>
 #include <vector>
 
 #include "system/SystemTypes.hpp"
@@ -236,6 +237,12 @@ public:
     // core still parked is one no document placed, and is freed, and the request is the one a script pack's reload
     // replans (loadScriptPack).
     GraphResult setGraph(const GraphRequest& request);
+    // A live change to a param of a node the last published plan places (GraphHost::setParam): clamped to the param's
+    // range (and rounded for an int, enum or bool), and applied at the start of the next block, flushed inline while
+    // no audio thread runs, on the command ring like every other edit. False for a node or a param that plan does not
+    // have (a node the next publish adds takes its value from the document), a NaN value, a param a wire drives, or a
+    // full ring. Control thread.
+    bool setParam(std::string_view nodeId, std::string_view paramId, double value);
     // The host channels the last published plan writes (the sum of its rp.host.audio-out buses). Control thread.
     std::uint32_t hostWidth() const { return hostWidth_; }
     // Nodes faulted so far, each counted once and bypassed from then on (GraphHost::faultCount). An atomic the
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index a1abc1fe..a7ca634a 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -4,27 +4,125 @@
 #include <array>
 #include <cstring>
 #include <iterator>
+#include <optional>
 #include <span>
 #include <string>
+#include <string_view>
+#include <utility>
 
+#include <rfl/Literal.hpp>
+#include <rfl/Result.hpp>
+#include <rfl/Validator.hpp>
+#include <rfl/enums.hpp>
+#include <rfl/parsing/schema/ValidationType.hpp>
+
+#include "graph/CanonicalJson.hpp"
 #include "graph/node.hpp"
 #include "host/graph/HostServices.hpp"
 #include "system/SystemBase.hpp"
 #include "system/mesen/MesenGbaSystem.hpp"
+#include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
 #include "system/mesen/MesenSmsSystem.hpp"
+#include "system/sameboy/SameBoyConfig.hpp"
 #include "system/sameboy/SameBoySystem.hpp"
 #include "transport/MidiTypes.hpp"
 
 namespace rp::host {
 
-// Named, so its JSON Schema $defs key is the same under every compiler.
-struct SystemNodeSettings {
-    // The system id buildCore parked this node's core under (docs/modular-graph-phase-b2-design.md section
-    // 2.5). A new core is a new id, so it is always a new instance.
+// ---- settings ---------------------------------------------------------------------------------------------------
+
+// The knobs' value types. An enum knob is a label as the role configs spell it (packages/retroplug/src/settingsEnums.ts),
+// which is the name of its C++ enumerator with the first letter lowercased: SameBoyModel::CgbC is "cgbC",
+// SameBoyHighpass::RemoveDcOffset "removeDcOffset", SameBoyModel::Auto "auto". So the enumerators are native's one
+// spelling of every label: Label<E> is the rfl::Literal made of them, which reads exactly those strings (anything else
+// refuses the settings), lists them in the descriptor's schema, and maps back to its enumerator by position (valueOf).
+// Never an ordinal: the document and a saved project carry the same word (docs/modular-graph-phase-c2-design.md
+// section 3).
+namespace knobs {
+
+template <class E, std::size_t I>
+constexpr auto labelAt() {
+    constexpr std::string_view name = rfl::get_enumerator_array<E>()[I].first;
+    std::array<char, name.size() + 1> chars{};
+    for (std::size_t k = 0; k < name.size(); ++k) chars[k] = name[k];
+    if (chars[0] >= 'A' && chars[0] <= 'Z') chars[0] = static_cast<char>(chars[0] - 'A' + 'a');
+    return rfl::internal::StringLiteral<name.size() + 1>(chars);
+}
+
+template <class E, std::size_t... I>
+rfl::Literal<labelAt<E, I>()...> literalOf(std::index_sequence<I...>);
+
+// A number refused outside [R::min, R::max], as a settings value out of its range is refused anywhere (a document is
+// checked, never quietly changed; the store clamps every knob to the same range before it publishes), with the range
+// in the descriptor's schema. The bounds ride a type rather than template values: a floating-point template argument
+// is beyond some of the compilers the build supports.
+template <class R>
+struct Within {
+    template <class T>
+    static rfl::Result<T> validate(T value) noexcept {
+        if (value >= R::min && value <= R::max) return value;  // NaN is refused too
+        return rfl::error("Value expected to be between " + rp::graph::jsNumber(R::min) + " and " +
+                          rp::graph::jsNumber(R::max) + ", but got " + rp::graph::jsNumber(value) + ".");
+    }
+    template <class T>
+    static rfl::parsing::schema::ValidationType to_schema() {
+        using V = rfl::parsing::schema::ValidationType;
+        return V{V::AllOf{.types_ = {V{V::Minimum{.value_ = rfl::Variant<double, int>(R::min)}},
+                                     V{V::Maximum{.value_ = rfl::Variant<double, int>(R::max)}}}}};
+    }
+};
+
+struct LightTemperatureRange {
+    static constexpr double min = -1.0, max = 1.0;  // SameBoy's own: applyDisplayConfig clamps to it
+};
+struct ApuLatencyRange {
+    static constexpr double min = 0.25, max = 6.0;  // milliseconds
+};
+
+template <class E>
+using Label = decltype(literalOf<E>(std::make_index_sequence<rfl::get_enumerator_array<E>().size()>{}));
+
+template <class E>
+E valueOf(const Label<E>& label) {
+    return rfl::get_enumerator_array<E>()[label.value()].second;
+}
+
+using LightTemperature = rfl::Validator<double, Within<LightTemperatureRange>>;
+using ApuLatencyMs = rfl::Validator<double, Within<ApuLatencyRange>>;
+
+}  // namespace knobs
+
+// Each type's settings, named so their JSON Schema $defs keys are the same under every compiler. `core` is the system
+// id buildCore parked the node's core under (docs/modular-graph-phase-b2-design.md section 2.5): a new core is a new
+// id, so it is always a new instance (rebuild_key). Every other field is a knob of that core
+// (docs/modular-graph-phase-c2-design.md section 3), and optional: absent, the node leaves the core as it was built,
+// or as the last document that carried the field left it.
+struct SystemNodeSettings {  // rp.mesen.gba, rp.mesen.sms
     std::uint64_t core = 0;
 };
 
+struct GameBoySettings {
+    std::uint64_t                                       core = 0;
+    std::optional<knobs::Label<SameBoyModel>>           model;
+    std::optional<bool>                                 fastBoot;
+    std::optional<knobs::Label<SameBoyHighpass>>        highpass;
+    std::optional<knobs::Label<SameBoyColorCorrection>> colorCorrection;
+    std::optional<knobs::Label<SameBoyDmgPalette>>      dmgPalette;
+    std::optional<knobs::LightTemperature>              lightTemperature;
+    // What the core captures of its serial port for LSDj's decoder: "listen" for master sync, "drive" for MI.OUT.
+    std::optional<knobs::Label<SerialOutCapture>>       serialOutCapture;
+};
+
+struct NesSettings {
+    std::uint64_t                                  core = 0;
+    std::optional<knobs::Label<NesRegion>>         region;
+    std::optional<bool>                            removeSpriteLimit;
+    std::optional<knobs::ApuLatencyMs>             apuLatencyMs;
+    std::optional<knobs::Label<CartridgeAccuracy>> s5bNoise;
+    std::optional<knobs::Label<CartridgeAccuracy>> mmc5PhaseReset;
+};
+
 namespace {
 
 constexpr std::size_t kMaxOutputs = 16;
@@ -37,19 +135,41 @@ constexpr std::size_t kMaxOutputs = 16;
 //   bytes     bytes   pushCoreBytes(frame, data, size, flush), the flush flag riding RP_MESSAGE_FLUSH
 //   buttons   events  pressButton(button, down), from a 2-byte payload [button, down]
 
-// What every system node shares: the core handoff, the connected-output mask, its inputs, and prepare / step /
-// finish mapped one to one onto the core's block triad. `Self` supplies finish's lanes, and may write output
-// queues after the core has run (writeQueues).
-template <class Self, class Core>
+// Whether a knob is to be applied: present in `to`, and absent from `from`, what was applied last, or another value
+// there. A knob absent from `to` is never applied, so a field a document drops leaves the core as it is.
+inline bool same(bool a, bool b) { return a == b; }
+template <rfl::internal::StringLiteral... L>
+bool same(const rfl::Literal<L...>& a, const rfl::Literal<L...>& b) { return a.value() == b.value(); }
+template <class T, class... V>
+bool same(const rfl::Validator<T, V...>& a, const rfl::Validator<T, V...>& b) { return a.value() == b.value(); }
+template <class F>
+bool moved(const std::optional<F>& to, const std::optional<F>& from) { return to && !(from && same(*from, *to)); }
+
+// What every system node shares: the core handoff, its settings reaching the core, the connected-output mask, its
+// inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` supplies finish's lanes,
+// may write output queues after the core has run (writeQueues), and applies its own knobs (apply).
+//
+// Only a new core rebuilds (rebuild_key): every other setting is a knob the node applies to the core it has, so its
+// prepare reads nothing a commit writes and setGraph never waits on a system node's commit (stateless_prepare). A
+// commit only points `settings` at the new settings (on_settings never allocates); the node applies what moved at the
+// start of its next block, before the core's prepareForBlock, on the block thread as the command ring's knobs were,
+// and everything present once at activation, as it takes its core, before that core first runs. Each knob is
+// value-guarded by the core's own setter, so a palette change re-applies the display group and restarts nothing, while
+// a model change restarts the core and a region change resets it (docs/modular-graph-phase-c2-design.md section 4).
+template <class Self, class Core, class S = SystemNodeSettings>
 struct SystemNode {
-    using Settings = SystemNodeSettings;
-    static bool needs_rebuild(const Settings& a, const Settings& b) { return a.core != b.core; }
+    using Settings = S;
+    static std::uint64_t rebuild_key(const Settings& s) { return s.core; }
+    static constexpr bool stateless_prepare = true;
 
     const HostCores*                       cores = nullptr;
     const EngineSyncTrace*                 trace = nullptr;  // null when the host keeps none
     std::uint64_t                          token = 0;
     Core*                                  core = nullptr;  // owned
     std::array<std::uint8_t, kMaxOutputs>  connected{};
+    const Settings*                        settings = nullptr;  // the committed ones, alive until the next commit
+    Settings                               applied{};           // what the core was given last (none before activation)
+    bool                                   pending = false;     // settings committed and not yet applied
 
     SystemNode() = default;
     SystemNode(const SystemNode&) = delete;
@@ -66,10 +186,15 @@ struct SystemNode {
         cores = static_cast<const HostCores*>(h->get_interface(h, RP_HOST_CORES_INTERFACE));
         trace = static_cast<const EngineSyncTrace*>(h->get_interface(h, RP_ENGINE_SYNC_TRACE_INTERFACE));
     }
-    void on_settings(const Settings& s) noexcept { token = s.core; }
+    void on_settings(const Settings& s) noexcept {
+        settings = &s;
+        token = s.core;
+        pending = true;
+    }
 
-    // The first activation takes the core; later ones (a re-activation) keep it. The factory activated the
-    // core at the engine's rate when it built it, and Engine::setSampleRate re-rates live cores itself.
+    // The first activation takes the core and gives it every knob the settings carry; later ones (a re-activation)
+    // keep it. The factory activated the core at the engine's rate when it built it, and Engine::setSampleRate re-rates
+    // live cores itself.
     bool activate(double, std::uint32_t) {
         if (core) return true;
         if (!cores) return false;
@@ -78,9 +203,19 @@ struct SystemNode {
         // An id naming another kind of core is a host bug (Engine::setGraph checks the kinds before it
         // plans). The engine keeps a snapshot slot for it, as for any core, or takes it back.
         if (!core && built && !cores->released(cores->ctx, built)) delete built;
-        return core != nullptr;
+        if (!core) return false;
+        applySettings();  // against `applied`, which holds none yet, so every knob present
+        return true;
     }
 
+    void applySettings() noexcept {
+        static_cast<Self*>(this)->apply(*settings, applied);
+        applied = *settings;
+        pending = false;
+    }
+    // A core with no knobs of its own (the GBA, the SMS).
+    void apply(const Settings&, const Settings&) noexcept {}
+
     void ports_changed(std::span<const std::uint8_t> mask) noexcept {
         connected.fill(0);
         std::copy_n(mask.begin(), std::min(mask.size(), connected.size()), connected.begin());
@@ -98,9 +233,11 @@ struct SystemNode {
         }
         finish(b);
     }
-    // The inputs are applied here, where process starts too: a linked Game Boy's coupled group prepares every member
-    // (rp.step/1) before any of them steps, and never calls process.
+    // The knobs that moved, then the inputs, are applied here, where process starts too: a linked Game Boy's coupled
+    // group prepares every member (rp.step/1) before any of them steps, and never calls process. Knobs first, as the
+    // command ring's were drained before the block ran: a model change restarts the core before its inputs reach it.
     void prepare(const rp::Block& b) noexcept {
+        if (pending) applySettings();
         applyInputs();
         core->prepareForBlock(info(b));
     }
@@ -177,7 +314,7 @@ struct SystemNode {
 // speed): what does not fit is dropped and counted (GraphHost::droppedMessages), never lost silently.
 constexpr rp::capacity kSerialOutCapacity{.messages = 8256, .bytes = 8256};
 
-struct GameBoy : SystemNode<GameBoy, SameBoySystem> {
+struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
     static constexpr const char* id = "rp.sameboy.gb";
     static constexpr const char* name = "Game Boy";
     static constexpr const char* category = "system";
@@ -229,6 +366,23 @@ struct GameBoy : SystemNode<GameBoy, SameBoySystem> {
         for (rp_node* p : peers)
             if (GameBoy* gb = rp::peer_as<GameBoy>(p)) core->linkPeers_.push_back(gb->core);
     }
+
+    // In the order the store's role config reached the core through the command ring (the model's restart first, with
+    // the fast boot it already had, since that one waits for the next restart), the capture last. The capture lands at
+    // the start of the first block of the plan that carries it, the block rp.lsdj.serial-out first runs in its new
+    // mode, so no byte captured in one mode is decoded in the other.
+    void apply(const Settings& to, const Settings& from) noexcept {
+        if (moved(to.model, from.model)) core->setModel(knobs::valueOf<SameBoyModel>(*to.model));
+        if (moved(to.highpass, from.highpass)) core->setHighpass(knobs::valueOf<SameBoyHighpass>(*to.highpass));
+        if (moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
+        if (moved(to.colorCorrection, from.colorCorrection))
+            core->setColorCorrection(knobs::valueOf<SameBoyColorCorrection>(*to.colorCorrection));
+        if (moved(to.dmgPalette, from.dmgPalette))
+            core->setDmgPalette(knobs::valueOf<SameBoyDmgPalette>(*to.dmgPalette));
+        if (moved(to.lightTemperature, from.lightTemperature)) core->setLightTemperature(to.lightTemperature->value());
+        if (moved(to.serialOutCapture, from.serialOutCapture))
+            core->setSerialOutCapture(knobs::valueOf<SerialOutCapture>(*to.serialOutCapture));
+    }
 };
 
 // Output order: mix, then the tap ports by the mode that drives them.
@@ -237,7 +391,7 @@ constexpr const char* kNesTaps[] = {"pins.pulse",  "pins.tnd",     "pins.expansi
 constexpr std::uint32_t kPinsFirst = 1;      // output index of pins.pulse
 constexpr std::uint32_t kChannelsFirst = 5;  // output index of channels.sq1
 
-struct Nes : SystemNode<Nes, MesenNesSystem> {
+struct Nes : SystemNode<Nes, MesenNesSystem, NesSettings> {
     static constexpr const char* id = "rp.mesen.nes";
     static constexpr const char* name = "NES";
     static constexpr const char* category = "system";
@@ -268,6 +422,19 @@ struct Nes : SystemNode<Nes, MesenNesSystem> {
         for (std::uint32_t k = 0; k < streams; ++k) out[2 * k] = out[2 * k + 1] = b.output(first + k).u.audio.channels[0];
         return 2 * streams;
     }
+
+    // In the order the store's role config reached the core through the command ring. A region change resets the
+    // core (setRegion); the rest apply in place.
+    void apply(const Settings& to, const Settings& from) noexcept {
+        if (moved(to.region, from.region))
+            core->setRegion(static_cast<std::uint32_t>(knobs::valueOf<NesRegion>(*to.region)));
+        if (moved(to.removeSpriteLimit, from.removeSpriteLimit)) core->setRemoveSpriteLimit(*to.removeSpriteLimit);
+        if (moved(to.apuLatencyMs, from.apuLatencyMs)) core->setApuLatencyMs(to.apuLatencyMs->value());
+        if (moved(to.s5bNoise, from.s5bNoise))
+            core->setS5bNoise(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.s5bNoise)));
+        if (moved(to.mmc5PhaseReset, from.mmc5PhaseReset))
+            core->setMmc5PhaseReset(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.mmc5PhaseReset)));
+    }
 };
 
 struct Gba : SystemNode<Gba, MesenGbaSystem> {
````

- [ ] **Step 5: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[settings]"`
Expected: `All tests passed` for the settings cases.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the graph binary's `All tests passed (1996 assertions in 184 test cases)` and the engine-graph binary's `All tests passed (19879 assertions in 168 test cases)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `pnpm test && tools/run-profile.sh unit`
Expected: `# 167 test file(s) passed (jobs=16)`, then `All tests passed (402 assertions in 8 test cases)`.

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[settings]"`
Expected: `All tests passed (630 assertions in 12 test cases)`, no ThreadSanitizer report.

- [ ] **Step 6: Commit**

````bash
git add -- packages/native/cmake/tests.cmake packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/engine/Engine.hpp packages/native/src/host/graph/SystemNodes.cpp \
  packages/native/src/system/mesen/MesenNesConfig.hpp packages/native/src/system/mesen/MesenNesSystem.hpp \
  packages/native/src/system/sameboy/SameBoySystem.cpp packages/native/src/system/sameboy/SameBoySystem.hpp \
  packages/native/test/engine-graph/EngineGraph.test.cpp packages/native/test/engine-graph/HostIo.test.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp
git commit -F - <<'EOF'
feat(engine): system nodes take their knobs as settings, applied at the next block and at activation, and Engine::setParam
EOF
````

### Task 3: TS switches over, and the system nodes' params

Design sections 3, 4 and 5. The system nodes gain their params (`gain` on every one, `expansionVolume` on a NES), here
rather than in Task 2 because a param's default applied at its node's first block would overwrite a built core's value
while TS sent none. `buildGraph` writes every system node's settings and params, the capture from the mode the placed
`lsdj-sync` node runs after the controller's override; the stores call `Backend.setParam` for a param and publish for
a setting; `setParam` crosses the RPC. The two graph golden files are re-recorded. The old methods still exist (Task 4
deletes them), but the stores no longer call them. No parity case moves.

**Files:** 24 files, by step below.
- Modify: 24 files

- [ ] **Step 1: Write the failing tests**

Native: the params' descriptors, a document's values reaching the core before its first block (bit for bit against a
core set directly), `setParam` at the next block, a republish carrying the same value moving nothing, no allocation,
an audio thread racing edits, clamping at the Engine and `setParam` over the QuickJS RPC; and
`BuildGraphGoldens.test.cpp` now fails if a system node's settings name a field its type does not read (node.hpp
ignores such a field, so a misspelt knob would otherwise pass silently). TS: `buildGraph`'s settings and params for
every system type and the capture by mode (`off` under the controller's forced MI.MAP); the goldens' tests reading the
new model fields; the stores' gain and expansion volume calling `setParam` without publishing, a setting edit
publishing, an `lsdj-sync` edit publishing its capture; and, in `test-native`, a project saved at -90 dB loading
silent (it played at full level before: the saved gain never reached the core) and the build-flag file's second case
expecting the capture the document now arms:

````diff
diff --git a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
index c9ffa4af..6e7d0d39 100644
--- a/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
+++ b/packages/native/test/engine-graph/BuildGraphGoldens.test.cpp
@@ -19,10 +19,12 @@
 // and a control surface attached and pressed, so its protocol nodes send into the system inputs they feed and its
 // controller launches into its cart or out of the host. What reaches a system input the engine does not hand back, so
 // only the host's side has a floor: a controller sends its surface something, and one driving a real Game Boy sends
-// the host its launch under the system it resolved. No core here captures its serial out (that is armed only for an
-// LSDj in a MIDI-out mode, by the control plane), so the LSDj decoders send the host nothing, and host MIDI under the
-// controller's system is the controller's. The thirty-three systems under perSystem give
-// rp.host.audio-out 33 buses, past the 32 it takes: those are refused instead.
+// the host its launch under the system it resolved. A Game Boy captures its serial out as its node's settings say
+// (an LSDj in a MIDI-out mode, docs/modular-graph-phase-c2-design.md section 5), but every core here runs mGB, which
+// never plays the serial master, so a decoder's host MIDI is not counted on, and host MIDI under the controller's
+// system is the controller's. The thirty-three systems under perSystem give rp.host.audio-out 33 buses, past the 32 it
+// takes: those are refused instead. And since a C++ node reads only the settings fields it knows, ignoring any other,
+// each system node's settings may name only fields its type's schema lists: a misspelt knob would plan, and do nothing.
 //
 // Run via `pnpm test:plugin engine-graph`.
 
@@ -41,6 +43,7 @@
 #include "NodeRig.hpp"
 #include "graph/CanonicalJson.hpp"
 #include "host/engine/Engine.hpp"
+#include "host/graph/SystemNodes.hpp"
 #include "system/mesen/MesenGbaConfig.hpp"
 #include "system/mesen/MesenGbaSystem.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
@@ -156,6 +159,49 @@ std::unique_ptr<SystemBase> build(const GoldenSystem& s) {
     return core;
 }
 
+// The settings fields each system node type reads, from its descriptor's schema: the properties of its one $defs entry.
+const std::map<std::string, std::set<std::string>>& systemSettingsFields() {
+    static const std::map<std::string, std::set<std::string>> fields = [] {
+        std::map<std::string, std::set<std::string>> out;
+        for (const rp_package* package : {rp::host::sameboyPackage(), rp::host::mesenPackage()})
+            for (std::uint32_t t = 0; t < package->node_type_count; ++t) {
+                const rp_node_type* type = package->node_types[t];
+                const auto descriptor = rp::graph::parseJson(type->descriptor(type));
+                REQUIRE(descriptor);
+                const auto object = descriptor->to_object();
+                const auto id = object->get("id")->to_string();
+                const auto defs = object->get("settings")->to_object()->get("$defs")->to_object();
+                REQUIRE(defs);
+                REQUIRE(defs->size() == 1);
+                const auto properties = defs->begin()->second.to_object()->get("properties").and_then(
+                    [](const rfl::Generic& g) { return g.to_object(); });
+                REQUIRE(properties);
+                for (const auto& [field, _] : *properties) out[*id].insert(field);
+            }
+        return out;
+    }();
+    return fields;
+}
+
+// A settings field a system node of `document` names that its type does not read, or nothing.
+std::optional<std::string> unreadSettingsField(const std::string& document) {
+    const auto doc = rp::graph::parseJson(document);
+    REQUIRE(doc);
+    const auto nodes = doc->to_object()->get("nodes").and_then([](const rfl::Generic& g) { return g.to_array(); });
+    REQUIRE(nodes);
+    for (const auto& node : *nodes) {
+        const auto object = node.to_object();
+        REQUIRE(object);
+        const auto fields = systemSettingsFields().find(*object->get("type")->to_string());
+        if (fields == systemSettingsFields().end()) continue;
+        const auto settings = object->get("settings").and_then([](const rfl::Generic& g) { return g.to_object(); });
+        if (!settings) continue;
+        for (const auto& [field, _] : *settings)
+            if (!fields->second.count(field)) return *object->get("id")->to_string() + "'s settings name " + field + ", which its type does not read";
+    }
+    return std::nullopt;
+}
+
 // rp.host.audio-out takes 32 buses, and perSystem gives each system one.
 bool expectRefused(const GoldenModel& model) { return model.shape.kind == "perSystem" && model.systems.size() > 32; }
 
@@ -277,7 +323,8 @@ FileResult planAll(const char* path, bool driven) {
     for (const GoldenCase& c : goldens.cases) {
         if (!seen.insert(requestKey(c)).second) continue;
         REQUIRE(c.document < goldens.documents.size());
-        documents.insert(c.document);
+        if (documents.insert(c.document).second)
+            if (const auto why = unreadSettingsField(goldens.documents[c.document])) out.wrong.push_back(c.name + ": " + *why);
         if (expectRefused(c.model)) ++out.refused;
         if (const auto why = plan(c, goldens.documents[c.document], driven)) out.wrong.push_back(c.name + ": " + *why);
     }
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 4cc2ab00..5cc84949 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -3,8 +3,9 @@
 // same systems, the tap re-arm rides the swap, a refused document leaves its cores parked, and a core no
 // published document placed is freed; a build spec reaches the core under the field names TS sends; a system node's
 // settings carry its core's knobs beside the core (docs/modular-graph-phase-c2-design.md section 4); and
-// Engine::setParam reaches a param of the running plan at the next block. Driven on a quiescent Engine, so every plan
-// applies as it is published, except where a test owns the drain itself.
+// Engine::setParam reaches a param of the running plan at the next block, a system node's gain and a NES's expansion
+// volume among them, over the RPC as TS calls it. Driven on a quiescent Engine, so every plan applies as it is
+// published, except where a test owns the drain itself.
 //
 // Run via `pnpm test:plugin engine-graph`.
 
@@ -530,11 +531,11 @@ TEST_CASE("Engine::setParam reaches a param of the running plan at the next bloc
     };
     CHECK(block() == std::pair{0.0f, 0.0f});
 
-    // Refused: a node or a param the plan does not have, a NaN, and every system node, which has no param yet.
+    // Refused: a node or a param the plan does not have, and a NaN.
     CHECK_FALSE(e.setParam("nope", "level", 0.5));
     CHECK_FALSE(e.setParam("level", "nope", 0.5));
     CHECK_FALSE(e.setParam("level", "level", std::numeric_limits<double>::quiet_NaN()));
-    CHECK_FALSE(e.setParam("sys-1", "gain", 0.0));
+    CHECK_FALSE(e.setParam("sys-1", "volume", 0.0));
     CHECK(block() == std::pair{0.0f, 0.0f});
 
     CHECK(e.setParam("level", "level", 0.5));
@@ -549,3 +550,92 @@ TEST_CASE("Engine::setParam reaches a param of the running plan at the next bloc
     REQUIRE(e.setGraph(r).ok);
     CHECK(block() == std::pair{-1.0f, -1.0f});
 }
+
+TEST_CASE("Engine::setParam sets a system node's gain and a NES's expansion volume at the next block, clamped",
+          "[engine-graph][settings][params]") {
+    Engine e(kRate);
+    REQUIRE(e.parkCore(mgb(1)));
+    REQUIRE(e.parkCore(bliptoaster(2)));
+    REQUIRE(e.setGraph(request({{1, kGb}, {2, kNes}})).ok);
+    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
+    const auto* nes = dynamic_cast<const MesenNesSystem*>(e.findSystem(2));
+    REQUIRE(gb);
+    REQUIRE(nes);
+    std::vector<float> l(256), r(256);
+    const auto block = [&] {
+        float* outs[2] = {l.data(), r.data()};
+        e.processBlock(256, outs, 2);
+    };
+    block();
+    CHECK(gb->config_.gainDb == 0.0f);
+    CHECK(nes->config().expansionVolume == 100);
+    CHECK(e.setParam("sys-1", "gain", -6.0));
+    CHECK(e.setParam("sys-2", "expansionVolume", 40.0));
+    CHECK(e.setParam("sys-2", "gain", 30.0));  // clamped to +12
+    CHECK(gb->config_.gainDb == 0.0f);         // at the next block, not before
+    block();
+    CHECK(gb->config_.gainDb == -6.0f);
+    CHECK(nes->config().expansionVolume == 40);
+    CHECK(nes->config().gainDb == 12.0f);
+    CHECK_FALSE(e.setParam("sys-1", "expansionVolume", 40.0));  // a NES's alone
+    // A document carrying a value moves it; one dropping the param keeps the live value.
+    auto withParams = request({{1, kGb}, {2, kNes}});
+    const std::string core = R"({"core":1})";
+    const auto at = withParams.document.find(core);
+    REQUIRE(at != std::string::npos);
+    withParams.document.insert(at + core.size(), R"(,"params":{"gain":-20})");
+    REQUIRE(e.setGraph(withParams).ok);
+    block();
+    CHECK(gb->config_.gainDb == -20.0f);
+    REQUIRE(e.setGraph(request({{1, kGb}, {2, kNes}})).ok);
+    block();
+    CHECK(gb->config_.gainDb == -20.0f);
+    CHECK(nes->config().expansionVolume == 40);
+}
+
+TEST_CASE("setParam over the RPC layer takes the node, the param and the value as realBackend sends them", "[engine-graph][params]") {
+    struct Js {
+        JSRuntime* rt  = JS_NewRuntime();
+        JSContext* ctx = JS_NewContext(rt);
+        ~Js() {
+            JS_FreeContext(ctx);
+            JS_FreeRuntime(rt);
+        }
+    } js;  // first, so it outlives the server holding its context
+
+    Engine           e(kRate);
+    SystemFactory    factory;
+    registerCoreBackends(factory);
+    QueuedInvoker    invoker(e);
+    EngineRpcService rpc(e, factory, invoker);
+    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
+    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
+    registerEmulatorRpc(server, rpc);
+    REQUIRE(e.parkCore(mgb(1)));
+    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
+
+    // The call createEmulatorClient makes: true when the engine took it.
+    const auto setParam = [&](const std::string& params) {
+        const std::string src = "({jsonrpc: '2.0', id: 1, method: 'setParam', params: [" + params + "]})";
+        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<setParam>", JS_EVAL_TYPE_GLOBAL);
+        std::optional<rpcpp::QuickJSOut> reply;
+        if (!JS_IsException(message)) reply = server.processMessage(message);
+        JS_FreeValue(js.ctx, message);
+        if (!reply) return false;
+        JSValue answer = reply->materialize(js.ctx);
+        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
+        const bool accepted = JS_IsBool(result) && JS_ToBool(js.ctx, result);
+        JS_FreeValue(js.ctx, result);
+        JS_FreeValue(js.ctx, answer);
+        return accepted;
+    };
+    CHECK(setParam("'sys-1', 'gain', -6.5"));
+    CHECK_FALSE(setParam("'sys-1', 'nope', 0"));
+    CHECK_FALSE(setParam("'sys-9', 'gain', 0"));
+    std::vector<float> l(256), r(256);
+    float* outs[2] = {l.data(), r.data()};
+    e.processBlock(256, outs, 2);
+    const auto* gb = dynamic_cast<const SameBoySystem*>(e.findSystem(1));
+    REQUIRE(gb);
+    CHECK(gb->config_.gainDb == -6.5f);
+}
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index 97842358..e747bca9 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -6,7 +6,9 @@
 // node's input must render what the core call it maps to renders, made directly; and the Game Boy's serialOut
 // must carry what its core captured. So are its settings' knobs (docs/modular-graph-phase-c2-design.md sections 3 and
 // 4): each must reach the core as the core's own setter, called directly at the start of the block of the plan that
-// carries it, or at activation, renders it, and the descriptors must spell the labels the role configs do.
+// carries it, or at activation, renders it, and the descriptors must spell the labels the role configs do. Its params
+// too: each value must reach the core as its setter, called directly before the core's first block or at the start
+// of the block a change lands in, renders it.
 //
 // There is no GBA case: Mesen's GBA core needs a BIOS image, and no fixture here carries one.
 //
@@ -805,12 +807,13 @@ TEST_CASE("a system node records what its bytes input feeds the core in the engi
 
 namespace {
 
-// A system node "sys" of `type` on its core's token, `knobs` its settings after the core (JSON members, or none), its
-// mix on a stereo host pair; a Game Boy's serialOut on a probe.
-std::string knobbed(const char* type, std::uint64_t token, const std::string& knobs = "") {
+// A system node "sys" of `type` on its core's token, `knobs` its settings after the core (JSON members, or none) and
+// `params` its param values (JSON members, or none), its mix on a stereo host pair; a Game Boy's serialOut on a probe.
+std::string knobbed(const char* type, std::uint64_t token, const std::string& knobs = "", const std::string& params = "") {
     const bool gb = std::string(type) == "rp.sameboy.gb";
     return R"({"nodes":[{"id":"sys","type":")" + std::string(type) + R"(","settings":{"core":)" + std::to_string(token) +
-           (knobs.empty() ? "" : "," + knobs) + R"(}},)" + (gb ? R"({"id":"probe","type":"rp.test.probe"},)" : "") +
+           (knobs.empty() ? "" : "," + knobs) + "}" + (params.empty() ? "" : R"(,"params":{)" + params + "}") + "}," +
+           (gb ? R"({"id":"probe","type":"rp.test.probe"},)" : "") +
            R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],"wires":[{"from":"sys.mix","to":"out.out.1-2"})" +
            (gb ? R"(,{"from":"sys.serialOut","to":"probe.bytes"})" : "") + "]}";
 }
@@ -1060,6 +1063,33 @@ TEST_CASE("a knob restarts or resets its core only where the core's own setter d
         CHECK(heard >= static_cast<std::uint64_t>(2.0 * kRate));  // the chime again, from the block it landed in
         CHECK(heard != kNever);
     }
+    SECTION("Game Boy: a fast boot edit and a model edit landing in one block restart it with the new fast boot") {
+        // Two documents published before a block commit together; the restart reads the fast boot, so it applies
+        // first, as the two edits made one block apart would. The other order is audible: off, the boot ROM chimes.
+        const auto build = [] {
+            SameBoyConfig cfg;
+            cfg.model = SameBoyModel::DmgB;
+            cfg.fastBoot = true;
+            auto sys = std::make_unique<SameBoySystem>(1, cfg, readFile(RP_MGB_ROM_PATH));
+            sys->onActivate(kRate);
+            return sys;
+        };
+        NodeRig rig(kRate);
+        auto direct = build(), control = build();
+        const auto token = rig.park(build());
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"dmgB","fastBoot":true)")).ok);
+        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, d, 3.0, once(0.5, [&] {
+                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"dmgB","fastBoot":false)")).ok);
+                  REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("model":"mgb","fastBoot":false)")).ok);
+                  direct->setFastBoot(false);
+                  direct->setModel(SameBoyModel::Mgb);
+                  control->setModel(SameBoyModel::Mgb);
+                  control->setFastBoot(false);
+              }), &c, &heard) == "");
+        CHECK(heard >= static_cast<std::uint64_t>(0.5 * kRate));
+        CHECK(heard != kNever);
+    }
     SECTION("NES: the sprite limit, latency and accuracy switches apply in place, a region change resets it") {
         NodeRig rig(kRate);
         auto direct = nes(1, 0), control = nes(1, 0);
@@ -1281,3 +1311,190 @@ TEST_CASE("a Game Boy node takes knob edits published while an audio thread runs
     CHECK(gb->config_.highpass == SameBoyHighpass::Off);
     CHECK(gb->config_.lightTemperature == 0.5);
 }
+
+// ---- params -----------------------------------------------------------------------------------------------------
+//
+// A system node's params (docs/modular-graph-phase-c2-design.md sections 3 and 4): every type's gain, and a NES's
+// expansion volume. The document's value reaches the core before its first block, and each change (setParam, or a
+// document carrying another value) at the start of the block it lands in, as the core's own setter called directly
+// there renders it, bit for bit; a value the core already has calls nothing.
+
+namespace {
+
+// Each param a registered type declares, as "<id> <type> <min>..<max> <default> <unit>".
+std::vector<std::string> declaredParams(NodeRig& rig, const char* type) {
+    const auto descriptor = rp::graph::parseJson(rig.registry.find(type)->type->descriptor(rig.registry.find(type)->type));
+    REQUIRE(descriptor);
+    const auto params = descriptor->to_object()->get("params")->to_array();
+    REQUIRE(params);
+    std::vector<std::string> out;
+    for (const auto& p : *params) {
+        const auto o = *p.to_object();
+        const auto num = [&](const char* key) { return rp::graph::jsNumber(*o.get(key)->to_double()); };
+        out.push_back(*o.get("id")->to_string() + " " + *o.get("type")->to_string() + " " + num("min") + ".." + num("max") +
+                      " " + num("default") + " " + *o.get("unit")->to_string());
+    }
+    return out;
+}
+
+}  // namespace
+
+TEST_CASE("every system node takes its gain as a param, and a NES its expansion volume", "[engine-graph][system-node][params]") {
+    NodeRig rig(kRate);
+    for (const char* type : {"rp.sameboy.gb", "rp.mesen.gba", "rp.mesen.sms"}) {
+        INFO(type);
+        CHECK(declaredParams(rig, type) == std::vector<std::string>{"gain float -90..12 0 dB"});
+    }
+    CHECK(declaredParams(rig, "rp.mesen.nes") == std::vector<std::string>{"gain float -90..12 0 dB", "expansionVolume int 0..200 100 %"});
+}
+
+TEST_CASE("a system node gives its core the document's params before the core's first block, and each change at the "
+          "start of the block it lands in",
+          "[engine-graph][system-node][params]") {
+    std::uint64_t heard = kNever;
+
+    SECTION("Game Boy") {
+        NodeRig rig(kRate);
+        auto direct = mgb(1), control = mgb(1);
+        auto built = mgb(1);
+        SameBoySystem* const gb = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, "", R"("gain":-12)")).ok);
+        // Planned and activated, but a node reads its params in a block: the core has the value as that block starts.
+        CHECK(gb->config_.gainDb == 0.0f);
+        direct->setGainDb(-12.0f);
+        Direct d({direct.get()}, 2, {{0, 1}}), c({control.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, d, 3.0, once(1.0, [&] {
+                  REQUIRE(rig.host->setParam("sys", "gain", -3.0));
+                  direct->setGainDb(-3.0f);
+              }), &c, &heard) == "");
+        CHECK(heard < static_cast<std::uint64_t>(1.0 * kRate));  // the document's gain, on the boot chime
+        CHECK(gb->config_.gainDb == -3.0f);
+        CHECK(sounds(d));
+    }
+    SECTION("NES, its expansion volume in whole percent") {
+        NodeRig rig(kRate);
+        auto direct = nes(1, 0);
+        auto built = nes(1, 0);
+        MesenNesSystem* const core = built.get();
+        const auto token = rig.park(std::move(built));
+        REQUIRE(rig.set(knobbed("rp.mesen.nes", token, "", R"("gain":-6,"expansionVolume":40)")).ok);
+        direct->setGainDb(-6.0f);
+        direct->setExpansionVolume(40);
+        Direct d({direct.get()}, 2, {{0, 1}});
+        auto edit = once(2.0, [&] {
+            REQUIRE(rig.host->setParam("sys", "expansionVolume", 150.4));  // rounded, as an int param's value is
+            REQUIRE(rig.host->setParam("sys", "gain", 3.0));
+            direct->setExpansionVolume(150);
+            direct->setGainDb(3.0f);
+        });
+        auto note = afterBoot(*direct, *core);
+        CHECK(compare(rig, d, 3.0, [&](std::uint64_t at) { note(at); edit(at); }) == "");
+        CHECK(core->config().gainDb == 3.0f);
+        CHECK(core->config().expansionVolume == 150);
+        CHECK(sounds(d));
+    }
+    SECTION("SMS") {
+        NodeRig rig(kRate);
+        auto direct = sms(1);
+        auto viaGraph = sms(1);
+        SystemBase* const graphCore = viaGraph.get();
+        const auto token = rig.park(std::move(viaGraph));
+        REQUIRE(rig.set(knobbed("rp.mesen.sms", token, "", R"("gain":-9)")).ok);
+        direct->setGainDb(-9.0f);
+        Direct d({direct.get()}, 2, {{0, 1}});
+        CHECK(compare(rig, d, 5.0, smsStart(*direct, *graphCore)) == "");
+        CHECK(sounds(d));
+    }
+}
+
+TEST_CASE("a published param value the core already has changes nothing, and another value lands at the start of its "
+          "plan's first block",
+          "[engine-graph][system-node][params]") {
+    NodeRig rig(kRate);
+    auto direct = mgb(1);
+    const auto token = rig.park(mgb(1));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    Direct d({direct.get()}, 2, {{0, 1}});
+    auto live = once(0.3, [&] {
+        REQUIRE(rig.host->setParam("sys", "gain", -12.0));
+        direct->setGainDb(-12.0f);
+    });
+    // The store's next publish, for whatever reason, carries the value it set live: nothing moves.
+    auto same = once(0.6, [&] { REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("dmgPalette":"dmg")", R"("gain":-12)")).ok); });
+    // A document dropping the param leaves the stream, and the core, where they were.
+    auto dropped = once(0.9, [&] { REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, R"("dmgPalette":"dmg")")).ok); });
+    auto other = once(1.2, [&] {
+        REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, "", R"("gain":-1.5)")).ok);
+        direct->setGainDb(-1.5f);
+    });
+    CHECK(compare(rig, d, 2.0, [&](std::uint64_t at) { live(at); same(at); dropped(at); other(at); }) == "");
+    CHECK(sounds(d));
+}
+
+TEST_CASE("a param edit, live or published, allocates nothing on the block thread", "[engine-graph][system-node][params]") {
+    NodeRig rig(kRate);
+    auto built = mgb(1);
+    SameBoySystem* const gb = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token)).ok);
+    Lanes out(2);
+    const rp_transport t{kRate, 120.0, 0.0, 0};
+    for (int b = 0; b < 8; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
+    rig.host->setAudioThreadOwns(true);  // so each edit lands in a block, as it does under an audio thread
+    REQUIRE(rig.host->setParam("sys", "gain", -6.0));
+    rp::test::beginCountingAllocations();
+    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
+    auto counted = rp::test::endCountingAllocations();
+    CHECK(counted.allocations == 0);
+    CHECK(counted.frees == 0);
+    CHECK(gb->config_.gainDb == -6.0f);
+    REQUIRE(rig.set(knobbed("rp.sameboy.gb", token, "", R"("gain":-3)")).ok);
+    rp::test::beginCountingAllocations();
+    for (int b = 0; b < 2; ++b) rig.host->processBlock(512, t, out.ptrs.data(), 2);
+    counted = rp::test::endCountingAllocations();
+    rig.host->setAudioThreadOwns(false);
+    CHECK(counted.allocations == 0);
+    CHECK(counted.frees == 0);
+    CHECK(gb->config_.gainDb == -3.0f);
+}
+
+TEST_CASE("a system node takes param edits made while an audio thread runs its blocks", "[engine-graph][system-node][params]") {
+    // The race check for the params path under ThreadSanitizer (build-tsan/): live edits and documents carrying a value
+    // from this thread, while the audio thread drains them and the node applies the latest.
+    NodeRig rig(kRate);
+    auto built = nes(1, 0);
+    MesenNesSystem* const core = built.get();
+    const auto token = rig.park(std::move(built));
+    REQUIRE(rig.set(knobbed("rp.mesen.nes", token)).ok);
+    std::atomic<bool> running{true};
+    std::atomic<int>  blocks{0};
+    rig.host->setAudioThreadOwns(true);
+    std::thread audio([&] {
+        Lanes out(2);
+        const rp_transport t{kRate, 120.0, 0.0, 0};
+        while (running.load(std::memory_order_acquire)) {
+            rig.host->processBlock(256, t, out.ptrs.data(), 2);
+            blocks.fetch_add(1, std::memory_order_release);
+        }
+    });
+    int accepted = 0;
+    constexpr int kEdits = 60;
+    for (int i = 0; i < kEdits; ++i) {
+        const double gain = -(i % 30), percent = 100 + i;
+        const std::string values = R"("gain":)" + rp::graph::jsNumber(gain) + R"(,"expansionVolume":)" + rp::graph::jsNumber(percent);
+        const bool ok = i % 10 == 9 ? rig.set(knobbed("rp.mesen.nes", token, "", values)).ok
+                                    : rig.host->setParam("sys", "gain", gain) && rig.host->setParam("sys", "expansionVolume", percent);
+        accepted += ok ? 1 : 0;
+        rig.host->reclaim();
+        const int seen = blocks.load(std::memory_order_acquire);
+        while (blocks.load(std::memory_order_acquire) < seen + 2) std::this_thread::yield();
+    }
+    running.store(false, std::memory_order_release);
+    audio.join();
+    rig.host->setAudioThreadOwns(false);
+    CHECK(accepted == kEdits);
+    run(rig);
+    CHECK(core->config().gainDb == -29.0f);  // the last edit's, 59
+    CHECK(core->config().expansionVolume == 159);
+}
diff --git a/packages/retroplug/test-native/app-settings.test.ts b/packages/retroplug/test-native/app-settings.test.ts
index 2812bedb..0ff21646 100644
--- a/packages/retroplug/test-native/app-settings.test.ts
+++ b/packages/retroplug/test-native/app-settings.test.ts
@@ -1,8 +1,10 @@
 // Phase 1: system settings actually take effect on the LIVE core, driven through the stores. Gain is
-// the crisp proof — a -90 dB edit silences a ringing mGB and 0 dB restores it, so applySystemSetting
-// reaches the running emulator (not the old no-op stub). Then the "sameboy" system-role config
-// (highpass live, model via restart) dispatches through applyRoleConfig without breaking playback.
-// One test / one mGB system: whole-mix RMS needs a single system in the shared native engine.
+// the crisp proof: a -90 dB edit silences a ringing mGB and 0 dB restores it, so the gain param the store
+// sets (setParam) reaches the running emulator. Then the "sameboy" system-role config (highpass live,
+// model via restart) reaches it as its system node's settings, published, without breaking playback. And a
+// loaded project's saved gain is what its core plays at from its first block, which only the published
+// document carries to it (docs/modular-graph-phase-c2-design.md section 5).
+// One mGB system at a time: whole-mix RMS needs a single system in the shared native engine.
 import { test, expect } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
@@ -24,19 +26,23 @@ const rms = (a: Float32Array): number => {
   return a.length ? Math.sqrt(s / a.length) : 0;
 };
 
-test("live gain silences/restores a ringing mGB, and sameboy role config dispatches (through the stores)", () => {
-  const be = createRealBackend();
-  const registry = buildAppRegistry();
-  const recent = new RecentStore(be);
-  const project = new ProjectStore(be, recent, registry);
-  const audio = createAudioDriver();
+// One project for the file: its graph is the engine's (one publisher per Engine), and each test removes what it built.
+const be = createRealBackend();
+const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
+const audio = createAudioDriver();
 
+// Warm a fresh mGB up (GB boot + mGB firmware init), sound the chord, and measure it.
+const chordRms = (): number => {
+  audio.renderAudio(1500);
+  CHORD.forEach((m) => audio.stageMidiIn(m));
+  return rms(audio.renderAudio(500));
+};
+
+test("live gain silences/restores a ringing mGB, and sameboy role config dispatches (through the stores)", () => {
   const id = project.systems.loadMgb()!;
   expect(typeof id).toBe("number");
 
-  audio.renderAudio(1500); // warm up: GB boot + mGB firmware init
-  CHORD.forEach((m) => audio.stageMidiIn(m));
-  const loud = rms(audio.renderAudio(500)); // the chord rings
+  const loud = chordRms(); // the chord rings
 
   // --- live gain: the crisp proof that applySystemSetting reaches the running core. Discard a short
   // settle window after each edit so RMS measures the settled level, not the ~20 ms gain ramp. ---
@@ -64,4 +70,24 @@ test("live gain silences/restores a ringing mGB, and sameboy role config dispatc
   const afterModel = rms(audio.renderAudio(1500));
   console.log(`[app-settings] afterHighpass=${afterHighpass.toFixed(5)} afterModel=${afterModel.toFixed(5)}`);
   expect(afterModel > 0.001).toBeTruthy(); // survived the model-change restart and plays on the new model
+  project.systems.removeSystem(id);
+});
+
+test("a project loaded with a saved gain plays at it from its first block, before any edit", () => {
+  // Nothing but the published document carries the saved gain to a loaded core: it is built at 0 dB.
+  const saved = project.systems.loadMgb()!;
+  expect(project.systems.setGain(saved, -90)).toBeTruthy();
+  const bytes = project.exportBytes()!;
+  project.newProject();
+  expect(project.loadBytes(bytes).kind).toBe("loaded");
+  const [id] = project.systems.systems().map((e) => e.id);
+  expect(project.systems.view()[0].settings.gainDb).toBe(-90);
+  const loaded = chordRms();
+  expect(project.systems.setGain(id, 0)).toBeTruthy();
+  audio.renderAudio(150); // the ramp back up
+  const restored = rms(audio.renderAudio(500));
+  console.log(`[app-settings] loaded at -90 dB=${loaded.toFixed(5)} restored=${restored.toFixed(5)}`);
+  expect(loaded < 0.001).toBeTruthy(); // silent: the core runs at the gain the project saved
+  expect(restored > 0.001).toBeTruthy(); // and it is the gain: at 0 dB the same system rings
+  project.systems.removeSystem(id);
 });
diff --git a/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts b/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
index 3f7f72f4..939e9da3 100644
--- a/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts
@@ -1,8 +1,9 @@
-// A loaded LSDj in a MIDI-out mode captures its serial out through its build alone
-// (docs/modular-graph-phase-b2-design.md section 3): the store adopts it as a project load does, building the core
-// with `serialOutCapture` in the spec, and nothing calls setSerialOutCapture after it. The same system in the same
-// mode, built without that flag, emits nothing, so the flag is what arms the capture. (lsdj-midiout.test.ts proves
-// the decode itself, armed by the live command.)
+// A loaded LSDj in a MIDI-out mode captures its serial out with no live command (docs/modular-graph-phase-b2-design.md
+// section 3): the store adopts it as a project load does, building the core with `serialOutCapture` in the spec, and
+// nothing calls setSerialOutCapture after it. Its Game Boy's node carries the same capture in its settings
+// (docs/modular-graph-phase-c2-design.md section 5), which the node gives the core as it takes it, before the core
+// first runs, so the same system built without the flag captures all the same. (lsdj-midiout.test.ts proves the decode
+// itself.)
 import { test, expect, skip } from "../testing/harness";
 import { bootSession } from "../src/bootSession";
 import { savFrom, type SavInput } from "../src/lsdjSav";
@@ -83,7 +84,7 @@ test("an adopted LSDj in MIDI-out mode is built capturing, and its NoteOns reach
   expect(project.systems.systems().length).toBe(0);
 });
 
-test("the same system built without the flag emits none: the flag is what arms the capture", () => {
+test("the same system built without the flag captures from its node's settings: the document arms it too", () => {
   if (!be.fileExists(ABOY)) skip(`lsdj-midiout-build-flag: aboy LSDj ROM not found at ${ABOY}`);
   const run = playMidiOut((spec) => {
     const bare = { ...spec };
@@ -92,6 +93,7 @@ test("the same system built without the flag emits none: the flag is what arms t
   });
   expect(run.spec.serialOutCapture, "the build flag").toBe(undefined);
   expect(run.liveArms, "setSerialOutCapture calls").toBe(0);
-  expect(run.noteOns).toBe(0);
+  console.log(`[lsdj-midiout-build-flag] armed by its node: ${run.noteOns} NoteOns`);
+  expect(run.noteOns).toBeGreaterThan(0);
   expect(project.systems.systems().length).toBe(0);
 });
diff --git a/packages/retroplug/test/graph/build-graph-goldens.test.ts b/packages/retroplug/test/graph/build-graph-goldens.test.ts
index 5a7a66c2..8b2e4066 100644
--- a/packages/retroplug/test/graph/build-graph-goldens.test.ts
+++ b/packages/retroplug/test/graph/build-graph-goldens.test.ts
@@ -7,7 +7,9 @@
 // each case's model through buildGraph), and says why in its commit. B3a did so for the DSP kernel and the host
 // I/O around the audio (every model gained an empty kernel structure), and B3b for the script nodes that replaced
 // the kernel (every model routes the host's MIDI to all its systems, with no controller and no roles, so each
-// document places the routing node and no protocol node); the audio of every document is the translator's still.
+// document places the routing node and no protocol node), and C2 for the system nodes' knobs (every Game Boy's node
+// carries its serial-out capture, off with no lsdj-sync placed; no model carries a core config or a gain, so no node
+// carries another knob); the audio of every document is the translator's still.
 // Documents with roles and a controller are buildGraph's own record, golden/build-graph-roles.json
 // (build-graph-role-goldens.test.ts). Not every golden document plans: the thirty-three systems under perSystem give
 // rp.host.audio-out 33 buses, past the 32 it takes.
diff --git a/packages/retroplug/test/graph/build-graph-role-goldens.test.ts b/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
index 4b20693f..79a7d497 100644
--- a/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
+++ b/packages/retroplug/test/graph/build-graph-role-goldens.test.ts
@@ -28,6 +28,8 @@ import { outNodeOf, protocolPipeline } from "../../src/graph/protocolNodes";
 import type { GraphController } from "../../src/graph/controller";
 import type { RoleInstance } from "../../src/systemRoles";
 import { MIDI_ROUTING_VALUES, type ControllerTarget } from "../../src/settingsEnums";
+import { RoleRegistry } from "../../src/systemRoles";
+import { registerCoreRoles } from "../../src/coreRoles";
 import golden from "./golden/build-graph-roles.json";
 
 declare const tjs: { env: Record<string, string | undefined>; writeFile(path: string, data: string): Promise<void> };
@@ -94,10 +96,31 @@ const model = (systems: GraphSystem[], over: Partial<GraphModel> = {}): GraphMod
   ...over,
 });
 
+// A core-config role's config as a store holds it, its role's schema parsed (coreRoles.ts), and a gain: what the
+// store's projection hands buildGraph beside the pipeline (SystemsStore.graphSystems), which the system node carries
+// as its settings and params (docs/modular-graph-phase-c2-design.md section 3).
+const CORE_ROLES = new RoleRegistry();
+registerCoreRoles(CORE_ROLES);
+const knobbed = (s: GraphSystem, kind: "sameboy" | "mesen", gainDb: number, over: Record<string, unknown> = {}): GraphSystem => ({
+  ...s,
+  coreConfig: CORE_ROLES.roleType(kind)!.schema.parse(over) as Record<string, unknown>,
+  gainDb,
+});
+
 // Every protocol node type on the system type it is placed for: mGB and an LSDj decoding MI.OUT on Game Boys (the
-// LSDj's serial out read by its decoder after the systems, whose host MIDI out is an input of rp.host.midi-out), the
-// N8 and risa on a NES (which the cart mirrors), smsggdj on an SMS.
-const everyNode = (): GraphSystem[] => [gb(1, [MGB]), gb(2, [lsdj("midiOut")]), nes(3, [N8, RISA]), sms(4, [smsSync("sms")])];
+// LSDj's serial out read by its decoder after the systems, whose host MIDI out is an input of rp.host.midi-out, and
+// captured as its node's settings say), the N8 and risa on a NES (which the cart mirrors), smsggdj on an SMS. Each with
+// its core config and gain, every setting and param away from its default on the first Game Boy and the NES.
+const everyNode = (): GraphSystem[] => [
+  knobbed(gb(1, [MGB]), "sameboy", -6, {
+    model: "dmgB", fastBoot: false, highpass: "removeDcOffset", colorCorrection: "modernBalanced", dmgPalette: "mgb", lightTemperature: 0.5,
+  }),
+  knobbed(gb(2, [lsdj("midiOut")]), "sameboy", 0),
+  knobbed(nes(3, [N8, RISA]), "mesen", -3, {
+    region: "pal", removeSpriteLimit: true, apuLatencyMs: 2, s5bNoise: "chip", mmc5PhaseReset: "chip", expansionVolume: 40,
+  }),
+  knobbed(sms(4, [smsSync("sms")]), "mesen", 1.5),
+];
 
 const CASES: Case[] = [
   // The routing in each mode, and none (a DirectGraph, or a store on its own, before a mode is set: no node's MIDI
diff --git a/packages/retroplug/test/graph/build-graph.test.ts b/packages/retroplug/test/graph/build-graph.test.ts
index 27e79247..6f68854c 100644
--- a/packages/retroplug/test/graph/build-graph.test.ts
+++ b/packages/retroplug/test/graph/build-graph.test.ts
@@ -4,11 +4,15 @@
 // carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
 // Then the script nodes ahead of the systems and the host's I/O around them (docs/modular-graph-phase-b3b-design.md
 // section 4): the MIDI routing, the controller and each role's protocol node, wired as the DSP kernel's ports were; and
-// after the systems, LSDj's serial-out decoder (docs/lsdj-serial-out-design.md section 3).
+// after the systems, LSDj's serial-out decoder (docs/lsdj-serial-out-design.md section 3). And each system node's knobs
+// (docs/modular-graph-phase-c2-design.md section 5): its core config's setting fields and a Game Boy's serial-out
+// capture as settings, its gain and a NES's expansion volume as params.
 import { test, expect } from "../../testing/harness";
 import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "../../src/graph/buildGraph";
 import { protocolPipeline, type LeftOutRole, type ProtocolRole } from "../../src/graph/protocolNodes";
-import { controllerSyncOverride, type GraphController } from "../../src/graph/controller";
+import { controllerSyncOverride, serialOutCaptureFor, type GraphController } from "../../src/graph/controller";
+import { RoleRegistry } from "../../src/systemRoles";
+import { registerCoreRoles } from "../../src/coreRoles";
 import { MIDI_ROUTE_NODE_TYPE } from "../../src/script/nodes/midiRoute";
 import { CONTROLLER_SESSION_NODE_TYPE } from "../../src/script/nodes/controllerSession";
 import { MGB_MIDI_NODE_TYPE } from "../../src/script/nodes/mgbMidi";
@@ -17,7 +21,7 @@ import { RISA_SYNC_NODE_TYPE } from "../../src/script/nodes/risaSync";
 import { SMS_SYNC_NODE_TYPE } from "../../src/script/nodes/smsSync";
 import { LSDJ_SYNC_NODE_TYPE } from "../../src/script/nodes/lsdjSync";
 import { LSDJ_SERIAL_OUT_NODE_TYPE } from "../../src/script/nodes/lsdjSerialOut";
-import type { AudioRouting, MidiRouting } from "../../src/settingsEnums";
+import { LSDJ_MODE_VALUES, type AudioRouting, type MidiRouting } from "../../src/settingsEnums";
 
 const gb = (id: number, linkGroup = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0, pipeline });
 const nes = (id: number, exportMode = 0, pipeline: ProtocolRole[] = []): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode, pipeline });
@@ -85,9 +89,9 @@ test("Stereo sums every system into one pair, in slot order", () => {
   expect(built([gb(1), gb(3)]).lane(0)).toEqual(["split-1.out1", "split-3.out1"]);
 });
 
-test("a system node names its core by its system id", () => {
-  const g = built([gb(4)]);
-  expect(g.node("sys-4")).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { core: 4 } });
+test("a system node names its core by its system id, and a Game Boy's its capture", () => {
+  expect(built([gb(4)]).node("sys-4")).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { core: 4, serialOutCapture: "off" } });
+  expect(built([nes(5)]).node("sys-5")).toEqual({ id: "sys-5", type: "rp.mesen.nes", settings: { core: 5 } });
 });
 
 test("one system on a lane is wired straight in", () => {
@@ -473,3 +477,85 @@ test("the override leaves the model's own pipeline as it was", () => {
   expect(systems[0].pipeline[0].config.mode).toBe("midiSync");
   expect(syncModeOf(built(systems), 1)).toBe("midiSync");
 });
+
+// --- the system nodes' knobs (docs/modular-graph-phase-c2-design.md sections 3 and 5) ----------------------------
+
+// A core-config role's config as a store holds it, its role's schema parsed (coreRoles.ts): every field filled.
+const CORE_ROLES = new RoleRegistry();
+registerCoreRoles(CORE_ROLES);
+const coreConfig = (kind: "sameboy" | "mesen", over: Record<string, unknown> = {}): Record<string, unknown> =>
+  CORE_ROLES.roleType(kind)!.schema.parse(over) as Record<string, unknown>;
+const gba = (id: number): GraphSystem => ({ id, type: "rp.mesen.gba", linkGroup: 0, exportMode: 0, pipeline: [] });
+
+test("each system node's settings carry its core config's setting fields, and its params its gain and a NES's expansion volume", () => {
+  const sameboy = coreConfig("sameboy", { model: "dmgB", highpass: "removeDcOffset", fastBoot: false, colorCorrection: "lowContrast", dmgPalette: "gbl", lightTemperature: -0.25, linkGroupId: 3 });
+  const mesen = coreConfig("mesen", { region: "pal", removeSpriteLimit: true, apuLatencyMs: 2.5, s5bNoise: "chip", expansionVolume: 40, enableFm: false, fifo: "hardware", sdRoot: "/sd" });
+  const g = built([
+    { ...gb(1), coreConfig: sameboy, gainDb: -6 },
+    { ...nes(2), coreConfig: mesen, gainDb: 3 },
+    { ...gba(3), coreConfig: mesen, gainDb: -1.5 },
+    { ...sms(4), coreConfig: mesen, gainDb: 0 },
+  ]);
+  // The labels as the role config spells them; nothing that is wiring (the link group) or construct-time (FM, the FIFO,
+  // the SD root, the export mode, the N8 link) is a setting.
+  expect(g.node("sys-1")).toEqual({
+    id: "sys-1",
+    type: "rp.sameboy.gb",
+    settings: { core: 1, model: "dmgB", fastBoot: false, highpass: "removeDcOffset", colorCorrection: "lowContrast", dmgPalette: "gbl", lightTemperature: -0.25, serialOutCapture: "off" },
+    params: { gain: -6 },
+  });
+  expect(g.node("sys-2")).toEqual({
+    id: "sys-2",
+    type: "rp.mesen.nes",
+    settings: { core: 2, region: "pal", removeSpriteLimit: true, apuLatencyMs: 2.5, s5bNoise: "chip", mmc5PhaseReset: "n8" },
+    params: { gain: 3, expansionVolume: 40 },
+  });
+  expect(g.node("sys-3")).toEqual({ id: "sys-3", type: "rp.mesen.gba", settings: { core: 3 }, params: { gain: -1.5 } });
+  expect(g.node("sys-4")).toEqual({ id: "sys-4", type: "rp.mesen.sms", settings: { core: 4 }, params: { gain: 0 } });
+});
+
+test("a system with no core config or gain carries none: its node leaves its core as it was built", () => {
+  const g = built([gb(1), nes(2), gba(3), sms(4)]);
+  for (const id of [1, 2, 3, 4]) expect(g.node(`sys-${id}`)?.params, `sys-${id}`).toBe(undefined);
+  expect(g.node("sys-2")?.settings).toEqual({ core: 2 });
+  // A config holding only some fields gives the node those alone.
+  expect(built([{ ...nes(2), coreConfig: { region: "dendy" } }]).node("sys-2")).toEqual({ id: "sys-2", type: "rp.mesen.nes", settings: { core: 2, region: "dendy" } });
+});
+
+test("a knob the node would refuse is left out, so one stored config cannot refuse every system's document", () => {
+  // A loaded project keeps its core config as written: a hand edit, a newer build's label, an ordinal from before the
+  // labels. The planner would refuse the whole document for it.
+  const g = built([
+    { ...gb(1), coreConfig: { model: "cgbF", highpass: 2, fastBoot: "yes", colorCorrection: null, dmgPalette: "gbl", lightTemperature: 1.5 }, gainDb: Number.NaN },
+    { ...nes(2), coreConfig: { region: "PAL", apuLatencyMs: 9, removeSpriteLimit: 1, s5bNoise: "n8", expansionVolume: "40" }, gainDb: 999 },
+  ]);
+  expect(g.node("sys-1")).toEqual({ id: "sys-1", type: "rp.sameboy.gb", settings: { core: 1, dmgPalette: "gbl", serialOutCapture: "off" } });
+  // A param out of range is sent: the planner clamps it, as it clamps a live edit.
+  expect(g.node("sys-2")).toEqual({ id: "sys-2", type: "rp.mesen.nes", settings: { core: 2, s5bNoise: "n8" }, params: { gain: 999 } });
+});
+
+test("a Game Boy captures its serial out for the mode its lsdj-sync node runs: drive for MI.OUT, listen for master sync, off otherwise", () => {
+  const capture = (pipeline: ProtocolRole[]) => built([gb(1, 0, pipeline)]).node("sys-1")?.settings?.serialOutCapture;
+  for (const mode of LSDJ_MODE_VALUES)
+    expect(capture([LSDJ(mode)]), mode).toBe(mode === "midiOut" ? "drive" : mode === "masterSync" ? "listen" : "off");
+  expect(capture([])).toBe("off");
+  expect(capture([MGB])).toBe("off");
+  // An lsdj-sync the pipeline leaves out (mGB writes serialIn first): its decoder is not placed, so nothing captures.
+  const leftOut = protocolPipeline([{ kind: "mgb", config: {} }, { kind: "lsdj-sync", config: { mode: "midiOut" } }]);
+  expect(leftOut.map((r) => r.kind)).toEqual(["mgb"]);
+  expect(capture(leftOut)).toBe("off");
+  // Only a Game Boy has one.
+  expect(built([nes(2, 0, [N8])]).node("sys-2")?.settings?.serialOutCapture).toBe(undefined);
+  expect(serialOutCaptureFor([LSDJ("midiOut")], "midiMap")).toBe("off");
+});
+
+test("a cart the controller drives in MI.MAP captures nothing, whatever its stored mode, and its peers keep their own", () => {
+  const captures = (g: ReturnType<typeof built>) => [1, 2].map((id) => g.node(`sys-${id}`)?.settings?.serialOutCapture);
+  const systems = [gb(1, 0, [LSDJ("midiOut")]), gb(2, 0, [LSDJ("masterSync")])];
+  expect(captures(built(systems))).toEqual(["drive", "listen"]);
+  expect(captures(built(systems, "stereo", multi(2), withController()))).toEqual(["off", "listen"]); // forced midiMap
+  expect(captures(built(systems, "stereo", multi(2), withController({ systemId: 2 })))).toEqual(["drive", "off"]);
+  expect(captures(built(systems, "stereo", multi(2), withController({ systemId: 2, cartSync: "Lsdj" })))).toEqual(["drive", "off"]); // forced off
+  // Driving a real Game Boy forces no cart, so none loses its capture.
+  expect(captures(built(systems, "stereo", multi(2), withController({ target: "midiOut" })))).toEqual(["drive", "listen"]);
+});
diff --git a/packages/retroplug/test/system-settings/store.test.ts b/packages/retroplug/test/system-settings/store.test.ts
index 1473084b..11d2f148 100644
--- a/packages/retroplug/test/system-settings/store.test.ts
+++ b/packages/retroplug/test/system-settings/store.test.ts
@@ -1,8 +1,9 @@
 // The systems store carrying per-system settings + roles. A constructed system
 // auto-gets its backend "system" role (SameBoy = model/highpass/…) + universal
-// settings (gain/reload). Editing a SYSTEM role applies to the emulator; editing a
-// FEATURE role is pure TS (behaviour deferred). A fake extension proves the feature
-// path with no LSDj in the core.
+// settings (gain/reload). Editing a SYSTEM role reaches the emulator as its system node's
+// knobs (a setting publishes the document, a param is set live); editing a FEATURE role
+// with no protocol node is pure TS. A fake extension proves the feature path with no LSDj
+// in the core.
 import { test, expect } from "../../testing/harness";
 import { MockBackend } from "../../testing/mockBackend";
 import { SystemsStore } from "../../src/systemsStore";
@@ -10,6 +11,10 @@ import { RoleRegistry } from "../../src/systemRoles";
 import { registerCoreRoles } from "../../src/coreRoles";
 import { z, clampedInt } from "../../src/configSchema";
 import { gbRom, nesRom, sameboyRoleConfig } from "../systems/fixtures";
+import { canonicalJson } from "../../src/graph/canonicalJson";
+
+// `v` as a document carries it: canonical JSON, its keys sorted.
+const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));
 
 // A GB ROM (valid logo for classification) carrying a cartridge title at 0x134.
 function gbRomWithTitle(title: string): Uint8Array {
@@ -52,7 +57,7 @@ test("a SameBoy system auto-gets the sameboy system role + universal settings",
   expect(v.roles).toEqual([{ kind: "sameboy", config: sameboyRoleConfig() }]);
 });
 
-test("a NES system auto-gets the mesen system role; editing region crosses to the emulator", () => {
+test("a NES system auto-gets the mesen system role; editing region crosses to the emulator in the document", () => {
   const { be, store } = newStore();
   be.seed("/roms/g.nes", nesRom());
   const id = store.addSystem("/roms/g.nes") as number;
@@ -62,36 +67,42 @@ test("a NES system auto-gets the mesen system role; editing region crosses to th
 
   expect(store.setRoleConfig(id, "mesen", { region: "pal" })).toBeTruthy(); // PAL
   expect(store.view()[0].roles[0].config.region).toBe("pal");
-  expect(be.applyRoleCalls[be.applyRoleCalls.length - 1]).toEqual({
-    id,
-    kind: "mesen",
-    config: { region: 2, removeSpriteLimit: false, enableFm: true, apuLatencyMs: 1.4, s5bNoise: 1, mmc5PhaseReset: 1, expansionVolume: 100, channelExportMode: 0, sdRoot: "", fifo: 0, fifoDepth: 0, fifoBytesPerSecond: 0, n8Link: 0, n8LinkRttUs: 2200 }, // whole role config crosses (system-category, native-encoded)
-  });
+  // Its node's settings, labels as the role spells them; its params, the gain and the expansion volume.
+  expect(be.graphNode(`sys-${id}`)).toEqual(
+    canon({
+      id: `sys-${id}`,
+      type: "rp.mesen.nes",
+      settings: { core: id, region: "pal", removeSpriteLimit: false, apuLatencyMs: 1.4, s5bNoise: "n8", mmc5PhaseReset: "n8" },
+      params: { gain: 0, expansionVolume: 100 },
+    }),
+  );
+  expect(be.applyRoleCalls.length).toBe(0);
 });
 
-test("setRoleConfig on a system role: updates, clamps, applies to the emulator, dirties", () => {
+test("setRoleConfig on a system role: updates, clamps, publishes its node's settings, dirties", () => {
   const { be, store, changes } = newStore();
   be.seed("/roms/a.gb", gbRom());
   const id = store.addSystem("/roms/a.gb") as number;
   const before = changes();
+  const sent = be.graphRequests.length;
   expect(store.setRoleConfig(id, "sameboy", { model: "sgb" })).toBeTruthy();
   expect(store.view()[0].roles[0].config.model).toBe("sgb");
-  // Native-encoded: every string enum in the role has become its ordinal (model "sgb" → 3,
-  // highpass "accurate" → 1, colorCorrection "disabled" → 0, dmgPalette "grey" → 0), and the
-  // non-enum fields pass through as-is. That mapping is roleConfigForNative's whole job.
-  expect(be.applyRoleCalls[be.applyRoleCalls.length - 1]).toEqual({
-    id,
-    kind: "sameboy",
-    config: {
-      model: 3,
-      highpass: 1,
-      linkGroupId: 0,
+  // The labels as the role config spells them, never ordinals: the document and a saved project carry one word. The
+  // link group is wiring, not a setting.
+  expect(be.graphRequests.length).toBe(sent + 1);
+  expect(be.graphNode(`sys-${id}`)?.settings).toEqual(
+    canon({
+      core: id,
+      model: "sgb",
+      highpass: "accurate",
       fastBoot: true,
-      colorCorrection: 0,
-      dmgPalette: 0,
+      colorCorrection: "disabled",
+      dmgPalette: "grey",
       lightTemperature: 0,
-    },
-  });
+      serialOutCapture: "off",
+    }),
+  );
+  expect(be.applyRoleCalls.length).toBe(0);
   expect(changes()).toBe(before + 1);
   // unknown enum → defaulted, not stored raw
   store.setRoleConfig(id, "sameboy", { model: 99 });
@@ -100,18 +111,27 @@ test("setRoleConfig on a system role: updates, clamps, applies to the emulator,
   expect(store.setRoleConfig(id, "nope", { x: 1 })).toBeFalsy();
 });
 
-test("setGain / setReloadOnRomChange: clamp, update, apply to the emulator", () => {
-  const { be, store } = newStore();
+test("setGain / setReloadOnRomChange: clamp, update; the gain set live on its node, the reload flag TS's alone", () => {
+  const { be, store, changes } = newStore();
   be.seed("/roms/a.gb", gbRom());
   const id = store.addSystem("/roms/a.gb") as number;
+  const before = changes();
+  const sent = be.graphRequests.length;
   expect(store.setGain(id, -6)).toBeTruthy();
   expect(store.view()[0].settings.gainDb).toBe(-6);
   expect(store.setGain(id, -500)).toBeTruthy(); // below -90 → clamped
   expect(store.view()[0].settings.gainDb).toBe(-90);
   store.setReloadOnRomChange(id, true);
   expect(store.view()[0].settings.reloadOnRomChange).toBeTruthy();
-  expect(be.applySettingCalls.map((c) => c.key)).toEqual(["gainDb", "gainDb", "reloadOnRomChange"]);
+  expect(be.paramCalls).toEqual([
+    { nodeId: `sys-${id}`, paramId: "gain", value: -6 },
+    { nodeId: `sys-${id}`, paramId: "gain", value: -90 },
+  ]);
+  expect(be.graphRequests.length).toBe(sent); // no publish
+  expect(be.applySettingCalls.length).toBe(0);
+  expect(changes()).toBe(before + 3); // each dirties
   expect(store.setGain(9999, 0)).toBeFalsy(); // absent id
+  expect(store.setReloadOnRomChange(9999, true)).toBeFalsy();
 });
 
 test("a feature role attaches via the provider; editing it is PURE TS (no emulator apply)", () => {
@@ -124,6 +144,7 @@ test("a feature role attaches via the provider; editing it is PURE TS (no emulat
   expect(store.setRoleConfig(id, "demo-sync", { level: 5 })).toBeTruthy();
   expect(store.view()[0].roles[1].config.level).toBe(5);
   expect(be.applyRoleCalls.length).toBe(0); // feature-role config never crosses to native
+  expect(be.paramCalls.length).toBe(0);
 });
 
 test("no registry → no roles attached (back-compat)", () => {
diff --git a/packages/retroplug/test/systems/build-spec-wire.test.ts b/packages/retroplug/test/systems/build-spec-wire.test.ts
index 269b9703..70f78be1 100644
--- a/packages/retroplug/test/systems/build-spec-wire.test.ts
+++ b/packages/retroplug/test/systems/build-spec-wire.test.ts
@@ -1,7 +1,8 @@
-// The build spec as realBackend.ts puts it on the wire. A loaded LSDj in a MIDI-out mode captures serial out
-// only through `serialOutCapture`, and native skips a field it does not know, so the key must be the one
-// BackendConstructSpec names (the engine-graph Catch2 case decodes that same key on the native side). Driven
-// over a stand-in __rpcSend that records each request, so no native host is needed.
+// The build spec as realBackend.ts puts it on the wire. A loaded LSDj in a MIDI-out mode is built capturing serial
+// out through `serialOutCapture`, and native skips a field it does not know, so the key must be the one
+// BackendConstructSpec names (the engine-graph Catch2 case decodes that same key on the native side). And setParam,
+// how the stores set a gain or a NES's expansion volume live, as EngineRpcService::setParam takes it. Driven over a
+// stand-in __rpcSend that records each request, so no native host is needed.
 import { test, expect } from "../../testing/harness";
 import { createEmulatorClient } from "../../src/realBackend";
 import type { ConstructSpec } from "../../src/backend";
@@ -59,3 +60,10 @@ test("setSerialOutCapture carries the mode, and null for off", () => {
   client.setSerialOutCapture(3, null);
   expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setSerialOutCapture", 3, "listen"], ["setSerialOutCapture", 3, null]]);
 });
+
+test("setParam carries the node, the param and the value, in that order", () => {
+  const { sent, client } = recording();
+  client.setParam("sys-3", "gain", -6.5);
+  client.setParam("sys-4", "expansionVolume", 40);
+  expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setParam", "sys-3", "gain", -6.5], ["setParam", "sys-4", "expansionVolume", 40]]);
+});
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index 498af5d7..79190d4e 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -4,7 +4,10 @@
 // spec carrying serial-out capture; and DirectGraph keeping the B1 translator's model for callers with no store.
 // Each document places the script nodes that feed the systems (docs/modular-graph-phase-b3b-design.md section 4): the
 // MIDI routing, the controller and a protocol node for each role that has one, so an edit to such a role, the MIDI
-// routing or the controller publishes too, and nothing else does.
+// routing or the controller publishes too. And each system node carries its knobs
+// (docs/modular-graph-phase-c2-design.md section 5): an edit of a setting (a core config's setting field, an LSDj's
+// mode and with it its Game Boy's capture) publishes, an edit of a param (a gain, a NES's expansion volume) is set live
+// with no publish, and nothing else publishes.
 import { test, expect } from "../../testing/harness";
 import { MockBackend } from "../../testing/mockBackend";
 import { ProjectStore } from "../../src/projectStore";
@@ -123,26 +126,97 @@ test("a replace takes the slot of the system it replaces", () => {
   expect(sys.systems()[0].built).toEqual({ type: "rp.mesen.nes", exportMode: 0, linkGroup: 0 });
 });
 
-test("a live setting publishes nothing; a link-group change rewires without a re-arm", () => {
+test("a gain or expansion-volume edit sets its param live and publishes nothing", () => {
+  const { be, p, sys } = project();
+  const a = sys.addSystem("/roms/a.gb")!;
+  const c = sys.addSystem("/roms/c.nes")!;
+  const n = be.graphRequests.length;
+  const publishes = countPublishes(p.graph);
+  expect(sys.setGain(a, -6)).toBe(true);
+  expect(sys.setGain(c, -500)).toBe(true); // clamped
+  expect(sys.setRoleConfig(c, "mesen", { expansionVolume: 40 })).toBe(true);
+  expect(sys.setRoleConfig(c, "mesen", { expansionVolume: 40 })).toBe(true); // moves nothing: sets nothing
+  expect(sys.setRoleConfig(c, "mesen", { fifo: "hardware", sdRoot: "/sd" })).toBe(true); // construct-time: a rebuild's
+  expect(publishes.n).toBe(0); // not even a document for the dedupe to drop
+  expect(since(be, n)).toEqual([]);
+  expect(be.paramCalls).toEqual([
+    { nodeId: `sys-${a}`, paramId: "gain", value: -6 },
+    { nodeId: `sys-${c}`, paramId: "gain", value: -90 },
+    { nodeId: `sys-${c}`, paramId: "expansionVolume", value: 40 },
+  ]);
+  // The next document, whatever publishes it, carries the values set live.
+  p.setMidiRouting(MidiRouting.OneChannelPerInstance);
+  expect(be.graphNode(`sys-${a}`)?.params).toEqual(canon({ gain: -6 }));
+  expect(be.graphNode(`sys-${c}`)?.params).toEqual(canon({ gain: -90, expansionVolume: 40 }));
+});
+
+test("a setting edit publishes its system node's settings, without a re-arm; a link-group change rewires", () => {
   const { be, p, sys } = project();
   const a = sys.addSystem("/roms/a.gb")!;
   const b = sys.addSystem("/roms/b.gb")!;
+  const c = sys.addSystem("/roms/c.nes")!;
   const n = be.graphRequests.length;
   const publishes = countPublishes(p.graph);
-  sys.setGain(a, -6);
   sys.setRoleConfig(a, "sameboy", { highpass: "removeDcOffset" });
+  expect(since(be, n)).toEqual([{ slots: [a, b, c] }]);
+  expect(be.graphNode(`sys-${a}`)?.settings?.highpass).toBe("removeDcOffset");
   sys.setRoleConfig(a, "sameboy", { model: "dmgB", dmgPalette: "dmg" });
-  sys.setRoleConfig(a, "sameboy", { linkGroupId: 0 }); // the group it is already in
-  expect(publishes.n).toBe(0); // not even a document for the dedupe to drop
-  expect(since(be, n)).toEqual([]);
-  sys.setRoleConfig(a, "sameboy", { linkGroupId: 1 }); // a group of one is no cable yet: nothing changes
-  expect(publishes.n).toBe(1);
-  expect(since(be, n)).toEqual([]);
+  expect(be.graphNode(`sys-${a}`)?.settings).toEqual(
+    canon({ core: a, model: "dmgB", fastBoot: true, highpass: "removeDcOffset", colorCorrection: "disabled", dmgPalette: "dmg", lightTemperature: 0, serialOutCapture: "off" }),
+  );
+  sys.setRoleConfig(c, "mesen", { region: "pal", expansionVolume: 25 }); // the document carries the param too
+  expect(be.graphNode(`sys-${c}`)?.settings?.region).toBe("pal");
+  expect(be.graphNode(`sys-${c}`)?.params).toEqual(canon({ gain: 0, expansionVolume: 25 }));
+  expect(since(be, n).length).toBe(3);
+  expect(be.paramCalls).toEqual([]);
+  // An edit that moves no setting publishes nothing: the same values, the group it is already in.
+  sys.setRoleConfig(a, "sameboy", { model: "dmgB" });
+  sys.setRoleConfig(a, "sameboy", { linkGroupId: 0 });
+  expect(publishes.n).toBe(3);
+  sys.setRoleConfig(a, "sameboy", { linkGroupId: 1 }); // a group of one is no cable yet: the document is unchanged
+  expect(publishes.n).toBe(4);
+  expect(since(be, n).length).toBe(3);
   sys.setRoleConfig(b, "sameboy", { linkGroupId: 1 });
-  expect(since(be, n)).toEqual([{ slots: [a, b] }]);
+  expect(since(be, n)[3]).toEqual({ slots: [a, b, c] });
   expect(linked(be, `sys-${b}.link`)).toBe(true);
 });
 
+test("no store edit takes the side path the document replaced", () => {
+  const { be, sys } = project();
+  const a = sys.addSystem("/roms/lsdj.gb")!;
+  const c = sys.addSystem("/roms/c.nes")!;
+  sys.setGain(a, -3);
+  sys.setReloadOnRomChange(a, true);
+  sys.setRoleConfig(a, "sameboy", { model: "sgb", lightTemperature: 0.5 });
+  sys.setRoleConfig(a, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
+  sys.setRoleConfig(c, "mesen", { region: "ntsc", expansionVolume: 150, removeSpriteLimit: true });
+  sys.setRoleConfig(c, "mesen", { expansionVolume: 120 });
+  expect(be.log.filter((m) => ["applySystemSetting", "applyRoleConfig", "setSerialOutCapture"].includes(m))).toEqual([]);
+  // reload-on-ROM-change is TS's alone (the file watcher): it reaches no backend at all.
+  expect(sys.view()[0].settings.reloadOnRomChange).toBe(true);
+});
+
+test("a loaded project's saved gain and core config reach its system's node in the load's document", () => {
+  // Nothing else carries a saved gain to a core, which is built at 0 dB: before the document carried it, a loaded
+  // project played at 0 dB until its gain was edited.
+  const { be, p, sys } = project();
+  const a = sys.addSystem("/roms/a.gb")!;
+  const c = sys.addSystem("/roms/c.nes")!;
+  sys.setGain(a, -12);
+  sys.setRoleConfig(a, "sameboy", { model: "mgb" });
+  sys.setRoleConfig(c, "mesen", { expansionVolume: 60, s5bNoise: "chip" });
+  const bytes = p.exportBytes()!;
+  p.newProject();
+  const calls = be.paramCalls.length;
+  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
+  const [la, lc] = sys.systems().map((e) => e.id);
+  expect(be.graphNode(`sys-${la}`)?.params).toEqual(canon({ gain: -12 }));
+  expect(be.graphNode(`sys-${la}`)?.settings?.model).toBe("mgb");
+  expect(be.graphNode(`sys-${lc}`)?.params).toEqual(canon({ gain: 0, expansionVolume: 60 }));
+  expect(be.graphNode(`sys-${lc}`)?.settings?.s5bNoise).toBe("chip");
+  expect(be.paramCalls.length).toBe(calls); // the document, not a live call
+});
+
 test("a duplicate is placed after its source, and inheriting the link group cables the pair", () => {
   const { be, sys } = project();
   const a = sys.addSystem("/roms/a.gb")!;
@@ -340,22 +414,38 @@ test("an adopt outside a load places its core at once", () => {
   expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
 });
 
-test("a system in an LSDj MIDI-out mode is built capturing its serial out: MI.OUT drives, master sync listens", () => {
+test("a system in an LSDj MIDI-out mode captures its serial out as its document says: MI.OUT drives, master sync listens", () => {
   const { be, sys } = project();
   let id = sys.addSystem("/roms/lsdj.gb")!;
   const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
-  const lastLive = () => be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1];
+  const placed = () => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
   expect(lastBuild()).toBe(undefined); // midiSync by default
+  expect(placed()).toBe("off");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
-  expect(lastLive()).toEqual({ id, mode: "drive" }); // live
-  id = sys.reset(id)!; // a rebuild, under a new id: the new core is armed at the build
+  expect(placed()).toBe("drive"); // in the document that hands its decoder the mode
+  id = sys.reset(id)!; // a rebuild, under a new id: the build arms the capture its node carries too
   expect(lastBuild()).toBe("drive");
+  expect(placed()).toBe("drive");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
-  expect(lastLive()).toEqual({ id, mode: "listen" });
+  expect(placed()).toBe("listen");
   id = sys.reset(id)!;
   expect(lastBuild()).toBe("listen");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
-  expect(lastLive()).toEqual({ id, mode: null });
+  expect(placed()).toBe("off");
+  expect(be.serialOutCaptureCalls).toEqual([]);
+});
+
+test("a cart the controller drives in MI.MAP captures nothing, and gets its stored mode's capture back with the controller off", () => {
+  const { be, p, sys } = project();
+  const id = sys.addSystem("/roms/lsdj.gb")!;
+  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
+  const placed = () => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
+  expect(placed()).toBe("drive");
+  p.setController({ enabled: true, app: "lsdj-midimap", target: "system", systemId: id });
+  expect(be.graphNode(`sys-${id}/lsdj-sync`)?.settings?.mode).toBe("midiMap");
+  expect(placed()).toBe("off"); // in the one document that forces the node's mode
+  p.setController({ enabled: false });
+  expect(placed()).toBe("drive");
 });
 
 test("a systems store on its own publishes its systems at the stereo routing", () => {
@@ -392,8 +482,8 @@ test("an edit to a role with a protocol node publishes its settings, without a r
   expect(be.graphNode(`sys-${id}/lsdj-sync`)).toEqual(
     canon({ id: `sys-${id}/lsdj-sync`, type: "rp.lsdj.sync", settings: { mode: LsdjSyncMode.MidiOut, tempoDivisor: 1, autoStart: false } }),
   );
-  // The capture arm is the command it always was, sent before the plan that hands the node the new mode.
-  expect(be.log.lastIndexOf("setSerialOutCapture")).toBeLessThan(be.log.lastIndexOf("setGraph"));
+  // The capture rides the same document, on the Game Boy's node, so the two change in one plan.
+  expect(be.graphNode(`sys-${id}`)?.settings?.serialOutCapture).toBe("drive");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
   expect(since(be, n).length).toBe(1);
 });
@@ -764,15 +854,17 @@ test("a left-out lsdj-sync arms no serial-out capture: not at a build, nor on an
     { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiOut } },
   ];
   const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
-  const captures = (id: number) => be.serialOutCaptureCalls.filter((c) => c.id === id);
+  const placed = (id: number) => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
   let id = warned(() => sys.adopt({ romPath: "/roms/mgb.gb", core: "sameboy", roles })!).result;
   expect(be.protocolNodes(id)).toEqual([`sys-${id}/mgb`]);
   expect(lastBuild()).toBe(undefined); // a load, stored in MI.OUT
+  expect(placed(id)).toBe("off");
   for (const mode of [LsdjSyncMode.MasterSync, LsdjSyncMode.MidiOut]) {
     expect(sys.setRoleConfig(id, "lsdj-sync", { mode })).toBe(true);
-    expect(captures(id)).toEqual([]); // an edit
+    expect(placed(id)).toBe("off"); // an edit
     id = sys.reset(id)!;
     expect(lastBuild()).toBe(undefined); // a rebuild
+    expect(placed(id)).toBe("off");
   }
 });
 
````

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[params]"; pnpm test`
Expected: FAIL: the `[params]` cases fail (the prototype's run counted 15 failed assertions: no system node has a param yet), then the mock tier fails in `systems/store-graph`, `system-settings/store` and `systems/build-spec-wire`, and `graph/build-graph` does not bundle (it imports what this task adds).

- [ ] **Step 3: The params, and setParam over the RPC**

Each system node type declares `gain` (a float param, dB, -90 to 12, default 0) and the NES also `expansionVolume` (an
int param, percent, 0 to 200, default 100: the core takes a whole percent); `prepare` applies a param's value at the
node's first block and whenever its control stream changes it, value-guarded, after the settings and before the inputs
and `prepareForBlock`, allocating nothing (a node gets its control streams only in a block, and its core never runs
before its first, so it plays its first sample at the document's level). `EngineRpcService::setParam` is mounted on
the `emulator` facet:

````diff
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index a7ca634a..636ca27b 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -2,8 +2,10 @@
 
 #include <algorithm>
 #include <array>
+#include <cmath>
 #include <cstring>
 #include <iterator>
+#include <limits>
 #include <optional>
 #include <span>
 #include <string>
@@ -127,6 +129,16 @@ namespace {
 
 constexpr std::size_t kMaxOutputs = 16;
 
+// The params (docs/modular-graph-phase-c2-design.md section 3): what is worth playing, automating or modulating, so
+// each is also a control input, its value in the document and its live edits through Engine::setParam. Every system
+// node has `gain`, in dB over the range the store clamps a gain to (systemSettings.ts GAIN_MIN / GAIN_MAX); a NES has
+// `expansionVolume` too, its cartridge sound chip's level in whole percent of unity, as its core takes it.
+using GainParam = rp::float_param<"Gain", rp::range{-90, 12, 0}, "dB">;
+using ExpansionVolumeParam = rp::int_param<"Expansion volume", rp::range{0, 200, 100}, "%">;
+
+// What a param's last-applied value holds before the core has been given any: no value equals it, NaN included.
+constexpr double kUnapplied = std::numeric_limits<double>::quiet_NaN();
+
 // The inputs every system node has (docs/modular-graph-phase-b3a-design.md section 3.1), each mapped onto the core
 // call SystemBase already has. A type declares them in its `inputs`, under these names.
 //
@@ -145,9 +157,10 @@ bool same(const rfl::Validator<T, V...>& a, const rfl::Validator<T, V...>& b) {
 template <class F>
 bool moved(const std::optional<F>& to, const std::optional<F>& from) { return to && !(from && same(*from, *to)); }
 
-// What every system node shares: the core handoff, its settings reaching the core, the connected-output mask, its
-// inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` supplies finish's lanes,
-// may write output queues after the core has run (writeQueues), and applies its own knobs (apply).
+// What every system node shares: the core handoff, its settings and params reaching the core, the connected-output
+// mask, its inputs, and prepare / step / finish mapped one to one onto the core's block triad. `Self` declares its
+// params (`gain` at least), supplies finish's lanes, may write output queues after the core has run (writeQueues),
+// and applies its own knobs (apply) and any param beyond the gain (applyParams).
 //
 // Only a new core rebuilds (rebuild_key): every other setting is a knob the node applies to the core it has, so its
 // prepare reads nothing a commit writes and setGraph never waits on a system node's commit (stateless_prepare). A
@@ -156,6 +169,14 @@ bool moved(const std::optional<F>& to, const std::optional<F>& from) { return to
 // and everything present once at activation, as it takes its core, before that core first runs. Each knob is
 // value-guarded by the core's own setter, so a palette change re-applies the display group and restarts nothing, while
 // a model change restarts the core and a region change resets it (docs/modular-graph-phase-c2-design.md section 4).
+//
+// A param's value reaches the core at the start of each block whose control stream moves it, before the core's
+// prepareForBlock and after the settings (a model's restart first, then the levels), each value-guarded against the
+// value the core was given last: a publish carrying the value the core already has calls nothing. Every one is given
+// at the node's first block, before the core's first prepareForBlock: a node reads its params only in a block (the
+// host binds its control streams there, and lands the document's values in them as it adopts the plan), and a core a
+// node has taken runs nothing before that, so it plays its first sample at the document's level. The gain is smoothed
+// by the core, as every gain edit is.
 template <class Self, class Core, class S = SystemNodeSettings>
 struct SystemNode {
     using Settings = S;
@@ -170,6 +191,7 @@ struct SystemNode {
     const Settings*                        settings = nullptr;  // the committed ones, alive until the next commit
     Settings                               applied{};           // what the core was given last (none before activation)
     bool                                   pending = false;     // settings committed and not yet applied
+    double                                 gainApplied = kUnapplied;  // the gain param's value the core has, in dB
 
     SystemNode() = default;
     SystemNode(const SystemNode&) = delete;
@@ -233,14 +255,26 @@ struct SystemNode {
         }
         finish(b);
     }
-    // The knobs that moved, then the inputs, are applied here, where process starts too: a linked Game Boy's coupled
-    // group prepares every member (rp.step/1) before any of them steps, and never calls process. Knobs first, as the
-    // command ring's were drained before the block ran: a model change restarts the core before its inputs reach it.
+    // The knobs that moved, the params that moved, then the inputs, are applied here, where process starts too: a
+    // linked Game Boy's coupled group prepares every member (rp.step/1) before any of them steps, and never calls
+    // process. Knobs first, as the command ring's were drained before the block ran: a model change restarts the core
+    // before its inputs reach it.
     void prepare(const rp::Block& b) noexcept {
         if (pending) applySettings();
+        static_cast<Self*>(this)->applyParams();
         applyInputs();
         core->prepareForBlock(info(b));
     }
+
+    // Each param's latest value this block, where it differs from what the core has: a change anywhere in the block
+    // lands at its start. This one for a type whose one param is its gain.
+    void applyParams() noexcept { applyGain(); }
+    void applyGain() noexcept {
+        const double db = static_cast<Self*>(this)->params.gain.latest();
+        if (db != db || db == gainApplied) return;  // NaN reaches a param only over a wire, and moves nothing
+        core->setGainDb(static_cast<float>(db));
+        gainApplied = db;
+    }
     bool step(std::uint32_t target) noexcept { return !core->stepIfBelowTarget(target); }
 
     // finishBlock adds into its lanes, so they start at zero. Every other connected audio output is written too
@@ -333,6 +367,7 @@ struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
         rp::audio_out<rp::stereo>                ch4;
         rp::bytes_out::sized<kSerialOutCapacity> serialOut;
     } outputs;
+    struct { GainParam gain; } params;
     static constexpr std::uint32_t kSerialOut = 5;  // its output index
 
     // The four stems (8 lanes) once any is connected, else the mix (2): finishBlock's two lane counts.
@@ -367,14 +402,16 @@ struct GameBoy : SystemNode<GameBoy, SameBoySystem, GameBoySettings> {
             if (GameBoy* gb = rp::peer_as<GameBoy>(p)) core->linkPeers_.push_back(gb->core);
     }
 
-    // In the order the store's role config reached the core through the command ring (the model's restart first, with
-    // the fast boot it already had, since that one waits for the next restart), the capture last. The capture lands at
-    // the start of the first block of the plan that carries it, the block rp.lsdj.serial-out first runs in its new
-    // mode, so no byte captured in one mode is decoded in the other.
+    // The fast boot first, which waits for the next restart and so is what a model's restart boots with: the edits
+    // that land in one block (several documents published between two blocks commit together) restart the core as if
+    // made one at a time, a fast boot edit before a model edit included. Then the rest, in the order the store's role
+    // config reached the core through the command ring, the capture last. The capture lands at the start of the first
+    // block of the plan that carries it, the block rp.lsdj.serial-out first runs in its new mode, so no byte captured
+    // in one mode is decoded in the other.
     void apply(const Settings& to, const Settings& from) noexcept {
+        if (moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
         if (moved(to.model, from.model)) core->setModel(knobs::valueOf<SameBoyModel>(*to.model));
         if (moved(to.highpass, from.highpass)) core->setHighpass(knobs::valueOf<SameBoyHighpass>(*to.highpass));
-        if (moved(to.fastBoot, from.fastBoot)) core->setFastBoot(*to.fastBoot);
         if (moved(to.colorCorrection, from.colorCorrection))
             core->setColorCorrection(knobs::valueOf<SameBoyColorCorrection>(*to.colorCorrection));
         if (moved(to.dmgPalette, from.dmgPalette))
@@ -402,6 +439,11 @@ struct Nes : SystemNode<Nes, MesenNesSystem, NesSettings> {
         rp::events_in buttons;
     } inputs;
     struct { rp::audio_out<rp::stereo> mix; } outputs;
+    struct {
+        GainParam            gain;
+        ExpansionVolumeParam expansionVolume;
+    } params;
+    double expansionApplied = kUnapplied;  // the expansionVolume param's value the core has
 
     static void adjust_layout(rp::graph::Layout& l, const Settings&) {
         using namespace rp::graph;
@@ -435,6 +477,16 @@ struct Nes : SystemNode<Nes, MesenNesSystem, NesSettings> {
         if (moved(to.mmc5PhaseReset, from.mmc5PhaseReset))
             core->setMmc5PhaseReset(static_cast<std::uint32_t>(knobs::valueOf<CartridgeAccuracy>(*to.mmc5PhaseReset)));
     }
+
+    // The gain, then the expansion volume, which the core's mixer re-reads each buffer: whole percent, as the core
+    // takes it (the planner and setParam round an int param's value; a wire's is clamped and rounded here).
+    void applyParams() noexcept {
+        applyGain();
+        const double percent = params.expansionVolume.latest();
+        if (percent != percent || percent == expansionApplied) return;
+        core->setExpansionVolume(static_cast<std::uint32_t>(std::lround(std::clamp(percent, 0.0, 200.0))));
+        expansionApplied = percent;
+    }
 };
 
 struct Gba : SystemNode<Gba, MesenGbaSystem> {
@@ -448,6 +500,7 @@ struct Gba : SystemNode<Gba, MesenGbaSystem> {
         rp::events_in buttons;
     } inputs;
     struct { rp::audio_out<rp::stereo> mix; } outputs;
+    struct { GainParam gain; } params;
     std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
 };
 
@@ -462,6 +515,7 @@ struct Sms : SystemNode<Sms, MesenSmsSystem> {
         rp::events_in buttons;
     } inputs;
     struct { rp::audio_out<rp::stereo> mix; } outputs;
+    struct { GainParam gain; } params;
     std::size_t lanes(const rp::Block& b, std::array<float*, 2 * kMaxOutputs>& out) const { return mixLanes(b, out); }
 };
 
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index f4e1a385..c901d45f 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,12 +35,13 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: lifecycle / script node packs / snapshot reads / live config / input (14) ---
+// --- emulator: lifecycle / script node packs / live params and config / snapshot reads / input (15) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::buildCore>(e);
     s.template addMethod<&EngineRpcService::setGraph>(e);
     s.template addMethod<&EngineRpcService::loadScriptPack>(e);
+    s.template addMethod<&EngineRpcService::setParam>(e);
     s.template addMethod<&EngineRpcService::applySystemSetting>(e);
     s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
     s.template addMethod<&EngineRpcService::readState>(e);
@@ -124,7 +125,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (88 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (89 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index c64e5421..3f3f0d79 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -169,6 +169,10 @@ RpcLoadScriptPackResult EngineRpcService::loadScriptPack(RpcLoadScriptPackReques
     return out;
 }
 
+bool EngineRpcService::setParam(std::string nodeId, std::string paramId, double value) {
+    return engine_.setParam(nodeId, paramId, value);
+}
+
 // duplicate, reload and remove are TS orchestration (SystemsStore over the registry reads + buildCore, and
 // the documents it publishes): duplicate pulls the source savestate and builds a seeded core; reload pulls
 // SRAM and cold-boots the ROM into the same slot; remove publishes a document without it. Native has no
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index d636425e..c3f416c8 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -36,6 +36,10 @@ public:
     // docs/modular-graph-phase-c1-design.md section 7), while the audio thread runs blocks or not. Refused, changing
     // nothing, for a memory limit that is no whole number of bytes from 0 to 2^53 - 1 (BackendTypes.hpp).
     RpcLoadScriptPackResult     loadScriptPack(RpcLoadScriptPackRequest request);
+    // A live change to a param of a node the running plan places (Engine::setParam), at the start of the next block: a
+    // system node's gain or a NES's expansionVolume, as the store sends an edit of either, with no publish. False for a
+    // node or a param that plan does not have, a NaN, or a full command ring.
+    bool setParam(std::string nodeId, std::string paramId, double value);
     bool applySystemSetting(std::uint32_t id, std::string key, double value);
     bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
     std::optional<rfl::Bytestring> readState(std::uint32_t id);
````

- [ ] **Step 4: buildGraph writes the knobs, and the stores call setParam or publish**

`GraphSystem` gains two optional fields, `coreConfig` (the config of the role whose kind is the core) and `gainDb`;
absent, a system node gets no setting fields and no params, so a model without them builds the document it always did.
`systemKnobs` is the one table of which field is a setting and which a param. Every Game Boy node carries
`serialOutCapture` (`off` included: an absent field leaves the core as it is, so an armed capture would otherwise
stick), computed by `serialOutCaptureFor`, which moves beside the controller's override. A stored value the node would
refuse (an unknown label, a number out of range) is dropped rather than refusing the whole document. The store diffs
`systemKnobs` before and after an edit: a moved setting publishes, a moved param only calls `setParam` (both: a
publish, which carries the param); `graphSystems()` fills the new fields; the UI render's carried gain now reaches its
core through the same document. `Backend.setParam` is added (the mock records each call):

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index d6800bd7..6f5265b7 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -146,10 +146,18 @@ export interface Backend {
   openFileBrowser(opts: FileBrowserOpts): Promise<string | null>;
 
   // --- Live emulator config ----------------------------------------------
-  // TS owns the config; these apply a change to the LIVE emulator. Only EMULATOR
-  // config crosses: the two universal settings + SYSTEM-role config (a backend's own
-  // knobs). Feature-role config never reaches native — its behaviour is the deferred
-  // TS-script future (doc 06), so it stays pure TS config.
+  // TS owns the config. A system's knobs ride the graph document (graph/buildGraph.ts): its core config's setting
+  // fields and its LSDj capture as its node's settings, which an edit publishes, and its gain and a NES's expansion
+  // volume as its node's params, whose live edits go through setParam with no publish
+  // (docs/modular-graph-phase-c2-design.md section 5). The three calls after it are the side path the document
+  // replaced: nothing in the app makes them (native tests and a CLI session do).
+
+  /** A live change to a param of a node the running plan places, applied at the start of the next block, clamped to
+   *  the param's range (and rounded for an int, enum or bool). The store sets a system's `gain` (dB) and a NES's
+   *  `expansionVolume` (percent) on its node, `sys-<id>`, this way; the next document it publishes carries the same
+   *  value. False for a node or a param the running plan does not have (a node the next document places takes its
+   *  value from that document), a NaN, a param a wire drives, or a full command ring. */
+  setParam(nodeId: string, paramId: string, value: number): boolean;
 
   /** Apply a universal per-system setting (`"gainDb"` / `"reloadOnRomChange"`) to the
    *  live emulator. Returns false on failure. */
@@ -160,10 +168,10 @@ export interface Backend {
    *  failure. */
   applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
 
-  /** Set capture of a system's outgoing serial bytes (SerialOutCapture), or null for off. The store calls this when
-   *  a system's `lsdj-sync` mode changes: armed, the Game Boy node's `serialOut` port carries the raw bytes, a wire
-   *  into its rp.lsdj.serial-out node, for the MI.OUT and master-sync decoders. False for a mode native does not know.
-   *  SameBoy-only (a no-op on other cores). */
+  /** Set capture of a system's outgoing serial bytes (SerialOutCapture), or null for off: armed, the Game Boy node's
+   *  `serialOut` port carries the raw bytes, a wire into its rp.lsdj.serial-out node, for the MI.OUT and master-sync
+   *  decoders. A Game Boy node's `serialOutCapture` setting does the same from the document. False for a mode native
+   *  does not know. SameBoy-only (a no-op on other cores). */
   setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean;
 
   // --- Live emulator input ------------------------------------------------
@@ -409,7 +417,7 @@ export type HostBackend = Pick<
 /** Emulator lifecycle / live config / input / snapshot reads — bound by the plugin, UI, CLI, test host. */
 export type EmulatorBackend = Pick<
   Backend,
-  | "buildCore" | "setGraph" | "loadScriptPack" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
+  | "buildCore" | "setGraph" | "loadScriptPack" | "setParam" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
   | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
 >;
 
@@ -829,7 +837,8 @@ export interface ConstructSpec {
    *  boots from these instead of reading `statePath`. Rides as a Uint8Array (rfl::Bytestring). */
   stateBytes?: Uint8Array;
   /** Arm a Game Boy's serial-out capture before the core first runs, as setSerialOutCapture would on a running
-   *  one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. */
+   *  one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. Its node's settings carry a
+   *  capture too, which its activation applies before the core first runs, so the document's wins. */
   serialOutCapture?: SerialOutCapture;
   /** The system's backend ("system"-role) config as JSON, so a loaded non-default setting
    *  (SameBoy model/highpass/…) is applied at CONSTRUCT rather than via a post-build restart
diff --git a/packages/retroplug/src/graph/buildGraph.ts b/packages/retroplug/src/graph/buildGraph.ts
index 0ee6138b..ab2a17c4 100644
--- a/packages/retroplug/src/graph/buildGraph.ts
+++ b/packages/retroplug/src/graph/buildGraph.ts
@@ -5,9 +5,21 @@
 // system that has one, wired between the host's I/O and the systems as the DSP kernel's ports were. After the
 // systems, it places each role's second node where its type names one, which reads what its system sent in the same
 // block: LSDj's serial-out decoder, whose host MIDI goes where the kernel's did (docs/lsdj-serial-out-design.md).
-// Pure, and held to test/graph/golden/build-graph.json, whose audio wiring is the B1 native translator's, so every
-// audio node id, the document order (which is the plan order on ties) and every per-lane sum is the translator's.
-import type { AudioRouting, MidiRouting } from "../settingsEnums";
+// Each system node carries its core's knobs (docs/modular-graph-phase-c2-design.md section 5): its core config's
+// setting fields and, on a Game Boy, the serial-out capture its LSDj's decoder needs, as settings; its gain and a NES's
+// expansion volume as params. Pure, and held to test/graph/golden/build-graph.json, whose audio wiring is the B1
+// native translator's, so every audio node id, the document order (which is the plan order on ties) and every
+// per-lane sum is the translator's.
+import {
+  CARTRIDGE_ACCURACY_VALUES,
+  COLOR_CORRECTION_VALUES,
+  DMG_PALETTE_VALUES,
+  HIGHPASS_VALUES,
+  MODEL_VALUES,
+  REGION_VALUES,
+  type AudioRouting,
+  type MidiRouting,
+} from "../settingsEnums";
 import type { GraphDocument, NodeDoc, WireDoc } from "./graphDocument";
 import {
   outNodeOf,
@@ -16,7 +28,15 @@ import {
   type ProtocolRole,
   type SystemInput,
 } from "./protocolNodes";
-import { controllerSettings, controllerSyncOverride, controllerTarget, NO_SYSTEM, type GraphController } from "./controller";
+import {
+  controllerSettings,
+  controllerSyncOverride,
+  controllerTarget,
+  NO_SYSTEM,
+  serialOutCaptureFor,
+  type GraphController,
+  type SerialOutCaptureMode,
+} from "./controller";
 
 export type SystemNodeType = "rp.sameboy.gb" | "rp.mesen.nes" | "rp.mesen.gba" | "rp.mesen.sms";
 
@@ -38,6 +58,14 @@ export interface GraphSystem {
    *  as that node's settings (graph/protocolNodes.ts protocolPipeline, which leaves out a role with no node and one
    *  whose node would write an input an earlier role's does; ProtocolRole says who parsed the config). */
   pipeline: ProtocolRole[];
+  /** The config of the system's core-config role, the one whose kind is its core (`sameboy` or `mesen`, coreRoles.ts):
+   *  where its node's setting fields and a NES's expansionVolume come from (systemKnobs). Absent for a system with none
+   *  (a DirectGraph's, a store's with no registry, the B1 goldens' models), whose node carries none of them, which
+   *  leaves its core as it was built. */
+  coreConfig?: Readonly<Record<string, unknown>>;
+  /** The system's gain in dB (CommonSettings.gainDb), its node's `gain` param. Absent places no value, which leaves
+   *  the param's stream where it is: at its default, 0 dB, on a new node. */
+  gainDb?: number;
 }
 
 export interface GraphModel {
@@ -85,6 +113,86 @@ const sysNode = (id: number): string => `sys-${id}`;
 /** A system's protocol node: sys-<id>/<role-kind>, the main design's convention. */
 const protocolId = (system: number, kind: string): string => `${sysNode(system)}/${kind}`;
 
+// ---- the system nodes' knobs ----------------------------------------------------------------------------------------
+//
+// A system node's knobs (docs/modular-graph-phase-c2-design.md section 3): the setting fields each type takes from its
+// core config, under the names and in the labels the role config spells them (coreRoles.ts), which are what the C++
+// node reads (SystemNodes.cpp), and the params it takes from the system. A value the node would refuse (another label,
+// a number out of its range, another type) is left out rather than sent: the planner refuses the whole document for
+// one node's settings, and a loaded project keeps its core config as written (SystemsStore.adopt), so a hand edit or a
+// newer build's label would silence every system. Left out, the core keeps what it has. Params are not checked so: the
+// planner clamps a param's value to its range, and rounds an int's, so any finite number is sent.
+
+type Knob = (v: unknown) => boolean;
+const label = (values: readonly string[]): Knob => (v) => typeof v === "string" && values.includes(v);
+const flag: Knob = (v) => typeof v === "boolean";
+const within = (min: number, max: number): Knob => (v) => typeof v === "number" && v >= min && v <= max;
+const finite = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
+
+/** Each system node type's setting fields from its core config, and what the node takes for each. */
+const SETTING_FIELDS: Readonly<Record<SystemNodeType, Readonly<Record<string, Knob>>>> = {
+  "rp.sameboy.gb": {
+    model: label(MODEL_VALUES),
+    fastBoot: flag,
+    highpass: label(HIGHPASS_VALUES),
+    colorCorrection: label(COLOR_CORRECTION_VALUES),
+    dmgPalette: label(DMG_PALETTE_VALUES),
+    lightTemperature: within(-1, 1), // SameBoy's own range
+  },
+  "rp.mesen.nes": {
+    region: label(REGION_VALUES),
+    removeSpriteLimit: flag,
+    apuLatencyMs: within(0.25, 6),
+    s5bNoise: label(CARTRIDGE_ACCURACY_VALUES),
+    mmc5PhaseReset: label(CARTRIDGE_ACCURACY_VALUES),
+  },
+  "rp.mesen.gba": {},
+  "rp.mesen.sms": {},
+};
+
+/** The params each system node type takes from its core config, beside the gain every one takes from the system. */
+const CONFIG_PARAMS: Readonly<Record<SystemNodeType, readonly string[]>> = {
+  "rp.sameboy.gb": [],
+  "rp.mesen.nes": ["expansionVolume"],
+  "rp.mesen.gba": [],
+  "rp.mesen.sms": [],
+};
+
+/** A system node's knobs: what its settings carry beside `core` (and a Game Boy's capture, which buildGraph adds from
+ *  its pipeline), and its param values. */
+export interface SystemKnobs {
+  settings: Record<string, unknown>;
+  params: Record<string, number>;
+}
+
+/** The knobs a system node carries for `system`: its type's setting fields as its core config holds them, each left
+ *  out where absent or one the node would refuse; its gain; and its type's params from its core config. The one place
+ *  that says which field is which: the store reads it too, to tell a setting's edit, which publishes, from a param's,
+ *  which it sets live (SystemsStore.setRoleConfig). */
+export function systemKnobs(system: Pick<GraphSystem, "type" | "coreConfig" | "gainDb">): SystemKnobs {
+  const config: Readonly<Record<string, unknown>> = system.coreConfig ?? {};
+  const settings: Record<string, unknown> = {};
+  for (const [field, takes] of Object.entries(SETTING_FIELDS[system.type] ?? {}))
+    if (takes(config[field])) settings[field] = config[field];
+  const params: Record<string, number> = {};
+  if (finite(system.gainDb)) params.gain = system.gainDb;
+  for (const param of CONFIG_PARAMS[system.type] ?? []) {
+    const value = config[param];
+    if (finite(value)) params[param] = value;
+  }
+  return { settings, params };
+}
+
+/** A system's node: its core's id, its knobs and, on a Game Boy, `capture`. */
+function systemNode(system: GraphSystem, capture: SerialOutCaptureMode | null): NodeDoc {
+  const knobs = systemKnobs(system);
+  const settings: Record<string, unknown> = { core: system.id, ...knobs.settings };
+  if (capture !== null) settings.serialOutCapture = capture;
+  const node: NodeDoc = { id: sysNode(system.id), type: system.type, settings };
+  if (Object.keys(knobs.params).length > 0) node.params = knobs.params;
+  return node;
+}
+
 /** The streams a core reports in `mode` (a NES's export mode; ignored otherwise), as channelLayout() does. */
 function streamsOf(type: SystemNodeType, mode: number): Stream[] {
   const mono = (port: string): Stream => ({ port, stereo: false });
@@ -179,7 +287,7 @@ export function buildGraph(model: GraphModel): BuiltGraph {
     hostMidi.push({ from: "controller.midiOut", system: target });
   }
   // The MI.MAP controller's cart, in the lsdj-sync mode it needs: the settings of that role's two nodes alone (its
-  // rp.lsdj.sync and its serial-out decoder), never the project's.
+  // rp.lsdj.sync and its serial-out decoder), and the capture its Game Boy runs for them, never the project's.
   const sync = controllerSyncOverride(systems, controller);
   // The nodes placed after the systems, in slot order, then pipeline order: each role's second node, and the system
   // output it reads, if any.
@@ -213,7 +321,14 @@ export function buildGraph(model: GraphModel): BuiltGraph {
   }
   const groups = linkGroups(systems);
   const order = sumOrder(systems, groups);
-  for (const i of order) nodes.push({ id: sysNode(systems[i].id), type: systems[i].type, settings: { core: systems[i].id } });
+  // Each Game Boy captures its serial out for the mode its lsdj-sync node runs, the controller's forced one included,
+  // so the capture and the decoder reading it change in one plan: no byte captured in one mode is decoded in the
+  // other.
+  for (const i of order) {
+    const s = systems[i];
+    const forced = sync !== null && sync.systemId === s.id ? sync.mode : null;
+    nodes.push(systemNode(s, s.type === "rp.sameboy.gb" ? serialOutCaptureFor(s.pipeline, forced) : null));
+  }
   for (const g of groups)
     for (let k = 1; k < g.length; ++k) wire(`${sysNode(systems[g[k - 1]].id)}.link`, `${sysNode(systems[g[k]].id)}.link`);
   // After the systems, so each reads its system's output of this block: a Game Boy's serial out, which LSDj's
diff --git a/packages/retroplug/src/graph/controller.ts b/packages/retroplug/src/graph/controller.ts
index fc40a405..985d3595 100644
--- a/packages/retroplug/src/graph/controller.ts
+++ b/packages/retroplug/src/graph/controller.ts
@@ -1,10 +1,13 @@
 // A control-surface session as buildGraph places it (docs/modular-graph-phase-b3b-design.md section 4): what the
-// control plane hands it, the system its launches are for, and the lsdj-sync mode the cart it drives must be in.
-// Pure and registry-free: the publisher (ProjectStore, or a DirectGraph) parses the app's config with the app's own
-// schema before it gets here, so neither buildGraph nor the node it places imports a zod schema.
-import type { ControllerTarget } from "../settingsEnums";
+// control plane hands it, the system its launches are for, and the lsdj-sync mode the cart it drives must be in; and,
+// beside that override, the serial-out capture a Game Boy runs for the mode its lsdj-sync node actually runs
+// (docs/modular-graph-phase-c2-design.md section 5). Pure and registry-free: the publisher (ProjectStore, or a
+// DirectGraph) parses the app's config with the app's own schema before it gets here, so neither buildGraph nor the
+// node it places imports a zod schema.
+import { LsdjSyncMode, type ControllerTarget } from "../settingsEnums";
 import type { RowTicksTable } from "../lsdj/playback/predict";
 import type { ControllerAnchor } from "../lsdj/playback/anchor";
+import type { ProtocolRole } from "./protocolNodes";
 
 /** An enabled controller: the user's choices, and what the control plane derived for it. */
 export interface GraphController {
@@ -69,6 +72,28 @@ export function controllerSyncOverride(
   return { systemId, mode };
 }
 
+/** What a Game Boy captures of its serial out for LSDj's decoder (its node's `serialOutCapture` setting, as the C++
+ *  node spells it): `listen` for master sync, `drive` for MI.OUT, `off` for no capture. */
+export type SerialOutCaptureMode = "off" | "listen" | "drive";
+
+/** The serial-out capture of a Game Boy whose protocol nodes are `pipeline` (protocolPipeline's), for the mode its
+ *  lsdj-sync node runs: `forced`, the controller's override of this system's mode (controllerSyncOverride), when it
+ *  has one, else the role's own. Both LSDj-to-host MIDI modes need one: MI.OUT (midiOut) drives, playing the
+ *  Arduinoboy that clocks LSDj as the serial slave; master sync (masterSync, SYNC=LSDJ) listens to the clock LSDj
+ *  sends as the master. Every other mode is `off`, the forced ones among them (the controller's midiMap and off): the
+ *  capture follows the node that decodes it, so a cart the controller drives in MI.MAP is never clocked under the
+ *  bytes it is sent. And so is a pipeline that places no lsdj-sync, whatever a role left out of it says: what a
+ *  capture delivers is decoded by that role's rp.lsdj.serial-out node, which is not placed, and a driving core would
+ *  clock the serial port under the bytes the placed role writes. */
+export function serialOutCaptureFor(pipeline: readonly ProtocolRole[], forced?: string | null): SerialOutCaptureMode {
+  const role = pipeline.find((r) => r.node === "rp.lsdj.sync");
+  if (!role) return "off";
+  const mode = forced ?? (role.config as { mode?: unknown }).mode;
+  if (mode === LsdjSyncMode.MidiOut) return "drive";
+  if (mode === LsdjSyncMode.MasterSync) return "listen";
+  return "off";
+}
+
 /** The controller node's settings (script/nodes/controllerSession.ts): its system resolved (0 is the node's none),
  *  and the anchor spelt as the node takes it. */
 export function controllerSettings(controller: GraphController, target: number): Record<string, unknown> {
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 6e95449b..0c0f679a 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -109,6 +109,7 @@ export function createEmulatorClient(): EmulatorBackend {
     buildCore: (spec: ConstructSpec, id: number) => (call("buildCore", specParams(spec, id)) as BuiltCore | null) ?? null,
     setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
     loadScriptPack: (request: LoadScriptPackRequest) => call("loadScriptPack", request) as LoadScriptPackResult,
+    setParam: (nodeId, paramId, value) => call("setParam", nodeId, paramId, value) as boolean,
     applySystemSetting: (id, key, value) =>
       call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
     applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
diff --git a/packages/retroplug/src/render/render.ts b/packages/retroplug/src/render/render.ts
index bc12a7c0..0d02d4f6 100644
--- a/packages/retroplug/src/render/render.ts
+++ b/packages/retroplug/src/render/render.ts
@@ -395,8 +395,9 @@ function rolesForRender(carried: RoleInstance[] | undefined, exportMode?: Channe
   return merged.some((r) => r.kind === "mesen") ? merged : [...merged, { kind: "mesen", config: { channelExportMode: exportMode } }];
 }
 
-/** Build the single system; the graph it publishes places its protocol nodes. `seed` (LSDj song bytes) forces
- *  the adopt path; so do carried roles / gain (they can only be applied at construct) and a NES split mode arming
+/** Build the single system; the graph it publishes places its protocol nodes, and its system node the carried gain
+ *  and core config's knobs. `seed` (LSDj song bytes) forces the adopt path; so do carried roles / gain (adopt takes
+ *  them as a load takes a stored system: the roles' construct-time fields reach the build) and a NES split mode arming
  *  construct-time capture; otherwise addSystem auto-detects. */
 function buildSystem(ctx: RenderContext, o: RenderOpts, platform: Platform, seed?: Uint8Array): number {
   let id: number | null;
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 65f875be..9b07cab2 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -10,8 +10,10 @@
 // duplicateSystem / reload orchestration and the DSP list handlers
 // (PluginDSP.cpp:406-458), with every path derived by the pure kernels.
 
-import type { ConstructSpec, ControlPlaneBackend, HostBackend, SerialOutCapture } from "./backend";
-import type { GraphSystem } from "./graph/buildGraph";
+import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
+import { systemKnobs, type GraphSystem } from "./graph/buildGraph";
+import { serialOutCaptureFor } from "./graph/controller";
+import { canonicalJson } from "./graph/canonicalJson";
 import { EngineGraph } from "./engineGraph";
 import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, SEGA_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";
 
@@ -31,9 +33,9 @@ import {
 } from "./systemsList";
 import { type CommonSettings, DEFAULT_COMMON_SETTINGS, clampGain, commonSettingsSchema } from "./systemSettings";
 import type { RoleRegistry, RoleInstance } from "./systemRoles";
-import { placedRole, protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
+import { protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
 import { blipToasterChip, needsChipLabelScan, BLIPTOASTER_CHIP_SCAN_LEN } from "./bliptoaster/romDetect";
-import { roleConfigForNative, LsdjSyncMode } from "./settingsEnums";
+import { roleConfigForNative } from "./settingsEnums";
 
 // How much ROM header to read for the role providers (title lives at 0x134).
 const ROLE_HEADER_LEN = 0x150;
@@ -158,12 +160,18 @@ export class SystemsStore {
     return this.entries.slice();
   }
   /** The systems as the engine's graph places them, in slot order (graph/buildGraph.ts), each with the roles that
-   *  have a protocol node, in order, as its pipeline: the core's config role and the features with no DSP behaviour
-   *  (asset overrides, a ROM's facts) stay out of the document, so a model, palette or asset edit leaves it as it
-   *  was, and so does a role whose kind is already placed or whose node would write an input an earlier role's does
-   *  (one writer per input, graph/protocolNodes.ts). The same with a registry or without one. */
+   *  have a protocol node, in order, as its pipeline, its core-config role's config and its gain: the features with no
+   *  DSP behaviour (asset overrides, a ROM's facts) stay out of the document, so an asset edit leaves it as it was, and
+   *  so does a role whose kind is already placed or whose node would write an input an earlier role's does (one writer
+   *  per input, graph/protocolNodes.ts). The core config reaches the document as its system node's knobs
+   *  (systemKnobs): its setting fields as settings, a NES's expansion volume, like the gain, as a param. The same with
+   *  a registry or without one. */
   graphSystems(): GraphSystem[] {
-    return this.entries.map((e) => ({ id: e.id, ...e.built, pipeline: protocolPipeline(e.roles) }));
+    return this.entries.map((e) => {
+      const coreConfig = e.roles.find((r) => r.kind === e.core)?.config;
+      const system: GraphSystem = { id: e.id, ...e.built, pipeline: protocolPipeline(e.roles), gainDb: e.settings.gainDb };
+      return coreConfig ? { ...system, coreConfig } : system;
+    });
   }
   focused(): number {
     return this.focusedId;
@@ -510,44 +518,66 @@ export class SystemsStore {
 
   // --- per-system settings + roles ----------------------------------------
 
-  /** Set a system's audio gain (dB, clamped); applies to the live emulator. */
+  /** Set a system's audio gain (dB, clamped): its node's `gain` param, set live (backend.setParam, at the next block)
+   *  with no publish. The next document, whatever publishes it, carries the same value. */
   setGain(id: number, db: number): boolean {
-    return this.applySetting(id, "gainDb", clampGain(db));
+    const e = findById(this.entries, id);
+    if (!e) return false;
+    const gainDb = clampGain(db);
+    this.entries = replaceById(this.entries, id, { ...e, settings: { ...e.settings, gainDb } });
+    this.backend.setParam(systemNodeId(id), "gain", gainDb);
+    this.markDirty();
+    return true;
   }
 
-  /** Toggle reload-on-ROM-change; applies to the live emulator. */
+  /** Toggle reload-on-ROM-change: TS's alone, read by the file watcher (fileWatcher.ts), so nothing native hears it. */
   setReloadOnRomChange(id: number, on: boolean): boolean {
-    return this.applySetting(id, "reloadOnRomChange", on);
+    const e = findById(this.entries, id);
+    if (!e) return false;
+    this.entries = replaceById(this.entries, id, { ...e, settings: { ...e.settings, reloadOnRomChange: on } });
+    this.markDirty();
+    return true;
   }
 
-  /** Edit a role's config (validated/clamped via its RoleType). A "system" role's
-   *  config is applied to the live emulator; a "feature" role's config reaches only its
-   *  protocol node, as the node's settings in the next published graph. False when the role is absent. */
+  /** Edit a role's config (validated/clamped via its RoleType). False when the role is absent. What reaches the
+   *  engine is what the published graph carries. A role with a protocol node is placed with its config as that node's
+   *  settings, so its edit publishes (an lsdj-sync's mode among them, and with it the capture its Game Boy's node
+   *  runs). The core-config role (kind === core) is its system node's knobs (systemKnobs): an edit moving a setting
+   *  field publishes, one moving only a param (a NES's expansion volume) sets it live (backend.setParam) with no
+   *  publish, and one moving neither (a construct-time field: the FIFO, the SD root, the N8 link, the export mode, FM)
+   *  leaves the engine as it is until the reset or reload that rebuilds the core from it. A Game Boy's link group is
+   *  wiring, so a change of its value publishes. Any other role (an asset override, a ROM's facts) never reaches the
+   *  engine. */
   setRoleConfig(id: number, roleKind: string, partial: Record<string, unknown>): boolean {
     const e = findById(this.entries, id);
     if (!e) return false;
     const idx = e.roles.findIndex((r) => r.kind === roleKind);
     if (idx < 0) return false;
     const rt = this.registry?.roleType(roleKind);
-    const merged = { ...e.roles[idx].config, ...partial };
+    const was = e.roles[idx].config;
+    const merged = { ...was, ...partial };
     const config = rt ? rt.schema.parse(merged) : merged;
-    if (rt?.category === "system") this.backend.applyRoleConfig(id, roleKind, roleConfigForNative(roleKind, config));
     const roles = e.roles.slice();
     roles[idx] = { kind: roleKind, config };
-    // A Game Boy's link group is the one system-role field that is also wiring: the cable follows it, so only a
-    // change of its value publishes (a model or palette edit leaves the document as it was).
+    // A Game Boy's link group is the one system-role field that is also wiring: the cable follows it, so a change of
+    // its value publishes.
     const linkGroupId = (config as { linkGroupId?: unknown }).linkGroupId;
     const linkGroup = typeof linkGroupId === "number" ? linkGroupId : 0;
     const rewired = roleKind === "sameboy" && e.built.type === "rp.sameboy.gb" && linkGroup !== e.built.linkGroup;
     const built = rewired ? { ...e.built, linkGroup } : e.built;
     this.entries = replaceById(this.entries, id, { ...e, roles, built });
-    // A feature role stays pure TS, EXCEPT lsdj-sync's serial-out capture mode, set on any change to an lsdj-sync the
-    // pipeline places: drive for MI.OUT, listen for master sync, none otherwise (the decoders themselves run in the
-    // role's rp.lsdj.serial-out node, after the Game Boy, which the publish below hands the new mode).
-    if (roleKind === "lsdj-sync") this.syncSerialOutCapture(id);
-    // A role with a protocol node is placed with its config as that node's settings, so its edit publishes; an edit
-    // the document does not carry (a model or palette, an asset override) leaves it as it was.
-    if (rewired || protocolNodeOf(roleKind) !== undefined) this.onGraphChange(false);
+    // The core config's knobs before and after: a setting field that moved is a document of its own, and a param that
+    // moved is set live unless that document carries it anyway.
+    let settingMoved = false;
+    const params: [string, number][] = [];
+    if (roleKind === e.core) {
+      const before = systemKnobs({ type: e.built.type, coreConfig: was });
+      const after = systemKnobs({ type: e.built.type, coreConfig: config });
+      settingMoved = canonicalJson(before.settings) !== canonicalJson(after.settings);
+      for (const [param, value] of Object.entries(after.params)) if (before.params[param] !== value) params.push([param, value]);
+    }
+    if (rewired || settingMoved || protocolNodeOf(roleKind) !== undefined) this.onGraphChange(false);
+    else for (const [param, value] of params) this.backend.setParam(systemNodeId(id), param, value);
     this.markDirty();
     return true;
   }
@@ -555,7 +585,7 @@ export class SystemsStore {
   /** Link a freshly-created instance to the one it was created from: the child joins the parent's link group,
    *  and if the parent is ungrouped (0) the parent is promoted to group 1 so the pair is linked. A no-op unless
    *  BOTH are SameBoy (GB) systems — link groups are a Game Boy link-cable concept. Routes through
-   *  setRoleConfig, so native-apply + dirty + the publish are handled. */
+   *  setRoleConfig, so dirty and the publish are handled. */
   inheritLinkGroup(childId: number, parentId: number): void {
     const parentSb = findById(this.entries, parentId)?.roles.find((r) => r.kind === "sameboy");
     const childSb = findById(this.entries, childId)?.roles.find((r) => r.kind === "sameboy");
@@ -566,19 +596,6 @@ export class SystemsStore {
     this.setRoleConfig(childId, "sameboy", { linkGroupId: target }); // child joins the parent's group
   }
 
-  // A universal-setting change: emulator-apply + update + dirty. False when absent.
-  private applySetting(id: number, key: keyof CommonSettings, value: number | boolean): boolean {
-    const e = findById(this.entries, id);
-    if (!e) return false;
-    this.backend.applySystemSetting(id, key, value);
-    this.entries = replaceById(this.entries, id, {
-      ...e,
-      settings: { ...e.settings, [key]: value } as CommonSettings,
-    });
-    this.markDirty();
-    return true;
-  }
-
   // --- project-load rebuild seam ------------------------------------------
   // clear()/adopt() are QUIET: no dirty/onChange, since a load isn't a user edit
   // (the ProjectStore sets dirty explicitly around load/new).
@@ -955,9 +972,11 @@ export class SystemsStore {
         const rt = this.registry.roleType(r.kind);
         if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
       }
-    // A core is built with its serial-out capture already armed, so no command has to chase it.
-    const capture = serialOutCaptureFor(roles);
-    return capture ? { ...s, serialOutCapture: capture } : s;
+    // A core is built with its serial-out capture already armed, the capture its node's settings carry anyway, from
+    // the mode of the lsdj-sync its pipeline places (graph/controller.ts serialOutCaptureFor): its activation applies
+    // the document's, the controller's MI.MAP override included, before the core first runs.
+    const capture = serialOutCaptureFor(protocolPipeline(roles));
+    return capture !== "off" ? { ...s, serialOutCapture: capture } : s;
   }
 
   // Write a system's live battery to its own `.sav` before its core is destroyed. Every teardown path
@@ -1012,28 +1031,7 @@ export class SystemsStore {
     this.markDirty();
     return id;
   }
-
-  // Set native serial-out capture for a running system from its lsdj-sync mode (serialOutCaptureFor). Called
-  // only when the mode changes (setRoleConfig): a core that is built or rebuilt carries its capture in the build
-  // spec instead (applyConstructHooks). A no-op for a system whose pipeline places no lsdj-sync: a non-LSDj system,
-  // or one whose lsdj-sync the one-writer rule leaves out. Which roles are placed is fixed with the system's role kinds
-  // and their order, which an edit never changes (there is no adding or removing a role), so its core was built with
-  // no capture either, and there is nothing to disarm.
-  private syncSerialOutCapture(id: number): void {
-    const roles = findById(this.entries, id)?.roles;
-    if (!roles || !placedRole(roles, "lsdj-sync")) return;
-    this.backend.setSerialOutCapture(id, serialOutCaptureFor(roles));
-  }
 }
 
-// How a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it. MidiOut
-// (SYNC=MI.OUT) drives, playing the Arduinoboy that clocks LSDj as the serial slave; MasterSync (SYNC=LSDJ) only
-// listens to the clock LSDj sends as the master. Null for no capture, which is also what an lsdj-sync the pipeline
-// leaves out gets, whatever its mode: what a capture delivers is decoded by the role's rp.lsdj.serial-out node, which
-// is not placed, and a driving core would clock the serial port under the bytes the placed role writes.
-function serialOutCaptureFor(roles: RoleInstance[]): SerialOutCapture | null {
-  const mode = (placedRole(roles, "lsdj-sync")?.config as { mode?: LsdjSyncMode } | undefined)?.mode;
-  if (mode === LsdjSyncMode.MidiOut) return "drive";
-  if (mode === LsdjSyncMode.MasterSync) return "listen";
-  return null;
-}
+/** A system's node in the graph documents buildGraph builds, whose params the store sets live. */
+const systemNodeId = (id: number): string => `sys-${id}`;
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 69a52a7e..ca2e2228 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -92,6 +92,8 @@ export class MockBackend implements Backend {
   /** One response per dialog the flow will open, consumed FIFO. `null` = cancel. */
   private browseQueue: (string | null)[] = [];
 
+  /** Every setParam call, in order: the live param edits the stores made (a gain, a NES's expansion volume). */
+  readonly paramCalls: { nodeId: string; paramId: string; value: number }[] = [];
   /** Live-config applies recorded for assertions. */
   readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
   readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];
@@ -622,6 +624,14 @@ export class MockBackend implements Backend {
     return Promise.resolve(this.browseQueue.length ? (this.browseQueue.shift() as string | null) : null);
   }
 
+  // Recorded; accepted for a node the last document sent places, as native accepts one the running plan places (the
+  // mock knows no node's params, so any param id passes).
+  setParam(nodeId: string, paramId: string, value: number): boolean {
+    this.log.push("setParam");
+    this.paramCalls.push({ nodeId, paramId, value });
+    return !Number.isNaN(value) && this.graphNode(nodeId) !== undefined;
+  }
+
   applySystemSetting(id: number, key: string, value: number | boolean): boolean {
     this.log.push("applySystemSetting");
     this.applySettingCalls.push({ id, key, value });
````

- [ ] **Step 5: Re-record the graph goldens**

The B1 golden file is rewritten by mapping each case's model through `buildGraph`, as its test's header
says, and both files are held to the old ones: every model the same but for each system's added `coreConfig` /
`gainDb`, every document the same but for the system nodes' new settings and params. The prototype's scratch script
does both (`rewrite`, `compare`); it imports `buildGraph` from this worktree by path, so keep it in `/tmp/c2-knobs`:

Save the script as `/tmp/c2-knobs/s3-goldens.ts`:

````ts
// Stage 3's golden tooling. `rewrite <build-graph.json>`: re-records golden/build-graph.json's documents by mapping each
// case's model through buildGraph (the procedure build-graph-goldens.test.ts's header gives), keeping its layout, and
// refusing if two cases of one old document now build different ones. `compare <old> <new>`: holds every case of the
// new file to the old one by name: its model the same but for each system's added coreConfig / gainDb, and its
// document the same but for the system nodes' settings fields beyond `core` and their params, which are listed.
import { readFileSync, writeFileSync } from "node:fs";
import { buildGraph } from "../../../workspaces/RetroPlug-main/cuttlefish/packages/retroplug/src/graph/buildGraph";
import { canonicalJson } from "../../../workspaces/RetroPlug-main/cuttlefish/packages/retroplug/src/graph/canonicalJson";

type Node = { id: string; type: string; settings?: Record<string, unknown>; params?: Record<string, unknown> };
type Doc = { nodes: Node[]; wires: unknown[] };
type Case = { name: string; model: { systems: Record<string, unknown>[] } & Record<string, unknown>; document: number; slots: number[]; tapArm: unknown };
type File = { documents: Doc[]; cases: Case[] };

const SYSTEM_TYPES = new Set(["rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"]);
const read = (path: string): File => JSON.parse(readFileSync(path, "utf8"));

function rewrite(path: string): void {
  const file = read(path);
  const next: (string | null)[] = file.documents.map(() => null);
  for (const c of file.cases) {
    const doc = canonicalJson(buildGraph(c.model as never).document);
    if (next[c.document] !== null && next[c.document] !== doc) throw new Error(`${c.name}: document ${c.document} now builds two`);
    next[c.document] = doc;
  }
  if (next.some((d) => d === null)) throw new Error("a document no case names");
  const cases = file.cases.map((c) => canonicalJson(c));
  writeFileSync(path, `{"documents":[\n${next.join(",\n")}\n],"cases":[\n${cases.join(",\n")}\n]}\n`);
  console.log(`rewrote ${next.length} documents of ${file.cases.length} cases in ${path}`);
}

function compare(oldPath: string, newPath: string): void {
  const before = read(oldPath);
  const after = read(newPath);
  const problems: string[] = [];
  const added = new Map<string, number>(); // "<type> settings.<field>" / "<type> params.<id>" -> nodes
  let systemNodes = 0;
  let changedDocs = 0;
  const strip = (m: Case["model"]) => ({ ...m, systems: m.systems.map(({ coreConfig: _c, gainDb: _g, ...rest }) => rest) });
  const names = new Set([...before.cases.map((c) => c.name), ...after.cases.map((c) => c.name)]);
  for (const name of names) {
    const o = before.cases.find((c) => c.name === name);
    const n = after.cases.find((c) => c.name === name);
    if (!o || !n) { problems.push(`${name}: only in ${o ? "old" : "new"}`); continue; }
    if (canonicalJson(strip(o.model)) !== canonicalJson(strip(n.model))) problems.push(`${name}: the model moved beyond coreConfig/gainDb`);
    if (canonicalJson(o.slots) !== canonicalJson(n.slots) || canonicalJson(o.tapArm) !== canonicalJson(n.tapArm)) problems.push(`${name}: slots or tap moved`);
    const od = before.documents[o.document];
    const nd = after.documents[n.document];
    if (canonicalJson(od) !== canonicalJson(nd)) changedDocs++;
    if (canonicalJson(od.wires) !== canonicalJson(nd.wires)) problems.push(`${name}: wires moved`);
    if (od.nodes.length !== nd.nodes.length) { problems.push(`${name}: node count moved`); continue; }
    od.nodes.forEach((on, i) => {
      const nn = nd.nodes[i];
      if (on.id !== nn.id || on.type !== nn.type) { problems.push(`${name}: node ${i} is ${nn.id}, was ${on.id}`); return; }
      if (!SYSTEM_TYPES.has(on.type)) {
        if (canonicalJson(on) !== canonicalJson(nn)) problems.push(`${name}: ${on.id} (${on.type}) moved`);
        return;
      }
      systemNodes++;
      if (canonicalJson(on.settings) !== canonicalJson({ core: on.settings?.core }) || on.params !== undefined)
        problems.push(`${name}: ${on.id} carried more than its core before`);
      if (nn.settings?.core !== on.settings?.core) problems.push(`${name}: ${on.id}'s core moved`);
      for (const k of Object.keys(nn.settings ?? {})) if (k !== "core") added.set(`${on.type} settings.${k}`, (added.get(`${on.type} settings.${k}`) ?? 0) + 1);
      for (const k of Object.keys(nn.params ?? {})) added.set(`${on.type} params.${k}`, (added.get(`${on.type} params.${k}`) ?? 0) + 1);
      for (const k of Object.keys(nn)) if (!["id", "type", "settings", "params"].includes(k)) problems.push(`${name}: ${on.id} gained ${k}`);
    });
  }
  console.log(`${newPath}: ${names.size} cases, ${changedDocs} with a changed document, ${systemNodes} system nodes compared`);
  for (const [k, v] of [...added].sort()) console.log(`  added ${k}: ${v} node(s)`);
  if (problems.length) {
    console.log(`  PROBLEMS (${problems.length}):`);
    for (const p of problems.slice(0, 40)) console.log(`    ${p}`);
    process.exitCode = 1;
  } else console.log("  nothing else moved: every other node, every wire, slot, tap and model field is as it was");
}

const [mode, a, b] = process.argv.slice(2);
if (mode === "rewrite") rewrite(a);
else if (mode === "compare") compare(a, b);
else throw new Error("usage: rewrite <file> | compare <old> <new>");
````

Run: `mkdir -p /tmp/c2-knobs && git show HEAD:packages/retroplug/test/graph/golden/build-graph.json > /tmp/c2-knobs/build-graph.before.json && git show HEAD:packages/retroplug/test/graph/golden/build-graph-roles.json > /tmp/c2-knobs/build-graph-roles.before.json`
Expected: nothing printed (the files as Task 2 left them, for the comparison).

Run: `node_modules/.bin/esbuild /tmp/c2-knobs/s3-goldens.ts --bundle --platform=node --format=esm --outfile=/tmp/c2-knobs/s3-goldens.mjs --log-level=warning && node /tmp/c2-knobs/s3-goldens.mjs rewrite packages/retroplug/test/graph/golden/build-graph.json`
Expected: `rewrote 238 documents of 840 cases in packages/retroplug/test/graph/golden/build-graph.json`.

Run: `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens`
Expected: the role goldens re-recorded, the file passing.

Run: `node /tmp/c2-knobs/s3-goldens.mjs compare /tmp/c2-knobs/build-graph.before.json packages/retroplug/test/graph/golden/build-graph.json && node /tmp/c2-knobs/s3-goldens.mjs compare /tmp/c2-knobs/build-graph-roles.before.json packages/retroplug/test/graph/golden/build-graph-roles.json`
Expected: for build-graph.json, `840 cases, 504 with a changed document, 3780 system nodes compared`, `added rp.sameboy.gb settings.serialOutCapture: 3150 node(s)` and `nothing else moved: every other node, every wire, slot, tap and model field is as it was`; for the role goldens, the Game Boy, NES, GBA and SMS nodes' added settings and params (30 of 37 cases change) and the same last line.

Run: `git diff --stat 68c118e8 -- packages/retroplug/test/graph/golden`
Expected: nothing: both files equal the prototype's byte for byte.

- [ ] **Step 6: Run the tests to see them pass, and parity unchanged**

Run: `cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[params],[settings]"`
Expected: `All tests passed` for those cases.

Run: `pnpm typecheck && pnpm test`
Expected: `packages/retroplug typecheck: Done`, then `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the graph binary's `All tests passed (1996 assertions in 184 test cases)` and the engine-graph binary's `All tests passed (25456 assertions in 175 test cases)`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded. Every parity system node now takes its full core config at activation, and the live edits (gain mid-note, the routing gains, highpass, APU latency, a VRC6's expansion volume) arrive through `setParam` or a publish.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test:node && pnpm test:cli-ts && pnpm check:docs`
Expected: `ℹ pass 12`, `PASS: retroplug-cli TypeScript harness`, `check-doc-links: 65 file(s) clean` (the prototype's 64, and this plan).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1" build-tsan/bin/retroplug-engine-graph-test "[params],[settings]"`
Expected: `All tests passed (715 assertions in 19 test cases)`, no ThreadSanitizer report.

- [ ] **Step 7: Commit**

````bash
git add -- packages/native/src/host/graph/SystemNodes.cpp packages/native/src/host/rpc/BackendRpcRegistration.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/test/engine-graph/BuildGraphGoldens.test.cpp \
  packages/native/test/engine-graph/EngineGraph.test.cpp packages/native/test/engine-graph/SystemNode.test.cpp \
  packages/retroplug/src/backend.ts packages/retroplug/src/graph/buildGraph.ts \
  packages/retroplug/src/graph/controller.ts packages/retroplug/src/realBackend.ts \
  packages/retroplug/src/render/render.ts packages/retroplug/src/systemsStore.ts \
  packages/retroplug/test-native/app-settings.test.ts packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts \
  packages/retroplug/test/graph/build-graph-goldens.test.ts \
  packages/retroplug/test/graph/build-graph-role-goldens.test.ts packages/retroplug/test/graph/build-graph.test.ts \
  packages/retroplug/test/graph/golden/build-graph-roles.json packages/retroplug/test/graph/golden/build-graph.json \
  packages/retroplug/test/system-settings/store.test.ts packages/retroplug/test/systems/build-spec-wire.test.ts \
  packages/retroplug/test/systems/store-graph.test.ts packages/retroplug/testing/mockBackend.ts
git commit -F - <<'EOF'
feat(graph): system nodes' knobs ride the document: settings published, gain and expansion volume set live through setParam, the capture from the placed mode

buildGraph writes each system node's settings and params from what GraphSystem now carries beside its pipeline: the
core-config role's config (coreConfig) and the system's gain (gainDb), both optional. A Game Boy's node takes model,
fastBoot, highpass, colorCorrection, dmgPalette and lightTemperature, a NES's region, removeSpriteLimit, apuLatencyMs,
s5bNoise and mmc5PhaseReset, as the role config spells them (systemKnobs, the one table of which field is which); a
value the node would refuse is left out, since a loaded project keeps its core config as written. Every Game Boy carries
serialOutCapture, from the mode its placed lsdj-sync node runs after the controller's MI.MAP override
(serialOutCaptureFor, moved beside the override in graph/controller.ts): drive for MI.OUT, listen for master sync, off
otherwise. Params: gain on every system node, expansionVolume on a NES.

Native: the system nodes declare gain (float, dB, -90 to 12) and a NES expansionVolume (int, percent, 0 to 200), applied
value-guarded at the start of each block whose stream moves them, and all at the node's first block, before the core's
first prepareForBlock. EngineRpcService::setParam on the emulator facet. A Game Boy applies its fast boot before a model
restart, so edits that commit in one block restart the core as if made one at a time.

The stores: a gain edit and a NES's expansion-volume edit set the param live (Backend.setParam, the mock recording it)
with no publish; an edit moving a core-config setting field publishes; an lsdj-sync edit publishes its capture with the
mode; reloadOnRomChange is TS's alone. No store calls applySystemSetting, applyRoleConfig or setSerialOutCapture. A
loaded project's saved gain, which ran its core at 0 dB until edited, now reaches the core in the load's document.

golden/build-graph.json's documents rewritten by mapping each case's model through buildGraph: every Game Boy node
gains serialOutCapture "off" and nothing else moves. The role goldens re-recorded, the every-node models given core
configs and gains; BuildGraphGoldens.test.cpp also refuses a system node settings field its type does not read.
EOF
````

### Task 4: the old path deleted, its callers moved

Design sections 4 ("Deleted") and 5 ("Callers moved"). `ConfigField`, `DspCommand::SetConfigField`, the invoker's and
the engine's `applyConfigField` with its casts, the three RPCs and `Backend` methods, the build spec's capture and the
dead native reload flag go. Every caller that still used them sets its knob the way the app does: through the store, or
through `DirectGraph.setCoreConfig` / `setParam`, which replace `DirectGraph.applyRoleConfig`. Nothing is lost: every
field `applyConfigField` applied is a system node's setting or param, but the link group, which only `buildCore`'s
facts read.

**Files:** 51 files, by step below.
- Create: `packages/retroplug/test-native/lsdj-capture-settings.test.ts`
- Delete: `packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts`
- Modify: 49 files

- [ ] **Step 1: Give DirectGraph setCoreConfig and setParam, and move the callers**

`DirectGraph.setCoreConfig(id, partial)` takes a core config in labels, as the stores' role spells it, merged over
what it had (setting fields ride the node's settings, `expansionVolume` its params, a Game Boy's `linkGroupId` the
cable), publishes, and refuses (changing nothing) any field its node does not take: an ordinal, a construct-time
field, another core's field, a bad link group. `setParam(id, param, value)` goes live through `backend.setParam` and
is kept in the model. The test-native callers move: the link group through `setCoreConfig(id, { linkGroupId: 1 })`, a
model switch through labels, and the direct capture calls drop (their `setPipeline` already publishes the capture);
`lsdj-midiout-build-flag` becomes `lsdj-capture-settings`, which adopts an LSDj through the store and checks the build
spec carries no capture, the first document placing it carries `drive` or `listen`, and its MIDI reaches the host; the
mock-tier tests check that a knob reaches the engine as a document or a param; the CLI session moves too:

Create `packages/retroplug/test-native/lsdj-capture-settings.test.ts`:

````ts
// A loaded LSDj in a MIDI-out mode captures its serial out from its Game Boy node's settings alone
// (docs/modular-graph-phase-c2-design.md section 4): the store adopts it as a project load does, its core built with
// no capture (a build spec carries none), and the first document that places its node carries `serialOutCapture`,
// which the node gives the core as it takes it, before the core first runs, so the core captures from its first block.
// Each mode once, on the ROM it targets: MI.OUT drives (an Arduinoboy build, its N commands' NoteOns reaching the
// host), master sync listens (a stock LSDj in SYNC=LSDj, its clocks reaching the host, which they would not had its
// capture ever driven it: clocked while stopped, it stops answering START). (lsdj-midiout.test.ts and
// lsdj-mastersync.test.ts prove the decoders themselves, and retroplug-engine-graph-test holds a capture applied at
// activation to one armed on the core directly, bit for bit.)
import { test, expect, skip } from "../testing/harness";
import { bootSession } from "../src/bootSession";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjSyncMode } from "../src/settingsEnums";
import type { GraphDocument } from "../src/graph/graphDocument";

declare const __RESOURCES_DIR__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;

// SYNC=MI.OUT and a phrase of eight notes, each with an `N` (NoteOn) command: once playing, LSDj sends a NoteOn
// per row on its serial out (as lsdj-midiout.test.ts authors it).
const MIDIOUT_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiOut" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [
      {
        notes: [1, 13, 25, 1, 13, 25, 1, 13],
        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
      },
    ],
    instruments: [pulse],
  },
};

// SYNC=LSDj and a one-note phrase: once playing, LSDj is the serial master and sends a byte a 24-PPQN tick (as
// lsdj-mastersync.test.ts authors it).
const MASTER_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [pulse],
  },
};

const session = bootSession();
const { project, audio, backend: be } = session;
project.systems.setBatteryFlush(false); // a remove must not write the song into the fixture ROM's .sav

// What a run saw: whether its build spec carried a capture, the capture its node was first placed with, the status
// bytes its decoder sent while LSDj booted, and those it sent once LSDj played.
type Run = { builtWithCapture: boolean; placed: unknown; boot: number[]; played: number[] };

// Adopt an LSDj in `mode` as a project load adopts each system, recording its build spec and every document published;
// boot it, then play its song. Removed at the end.
function play(romPath: string, song: SavInput, mode: string): Run {
  const buildCore = be.buildCore;
  const setGraph = be.setGraph;
  let builtWithCapture = true;
  const documents: GraphDocument[] = [];
  be.buildCore = (spec, id) => {
    builtWithCapture = "serialOutCapture" in spec;
    return buildCore(spec, id);
  };
  be.setGraph = (request) => {
    documents.push(JSON.parse(request.document) as GraphDocument);
    return setGraph(request);
  };
  let id: number | null = null;
  try {
    const roles = [{ kind: "lsdj-sync", config: { mode } }];
    id = project.systems.adopt({ romPath, roles }, { sramBytes: savFrom(song) });
    expect(typeof id, "adopted").toBe("number");
    const node = `sys-${id}`;
    const placing = documents.find((d) => d.nodes.some((n) => n.id === node));
    const placed = placing?.nodes.find((n) => n.id === node)?.settings?.serialOutCapture;
    audio.renderAudio(6000); // boot to the song screen
    const boot = audio.drainMidiOut().map((m) => m.data[0]);
    audio.pressButton(id!, START, true);
    audio.renderAudio(100);
    audio.pressButton(id!, START, false);
    audio.renderAudio(3000);
    const played = audio.drainMidiOut().map((m) => m.data[0]);
    return { builtWithCapture, placed, boot, played };
  } finally {
    if (id !== null) project.systems.removeSystem(id);
    be.buildCore = buildCore;
    be.setGraph = setGraph;
  }
}

test("an LSDj loaded in MI.OUT is built with no capture, placed driving, and its NoteOns reach the host", () => {
  if (!be.fileExists(ABOY)) skip(`lsdj-capture-settings: aboy LSDj ROM not found at ${ABOY}`);
  const run = play(ABOY, MIDIOUT_SONG, LsdjSyncMode.MidiOut);
  expect(run.builtWithCapture, "a capture in the build spec").toBe(false);
  expect(run.placed, "the capture its node was first placed with").toBe("drive");
  const noteOns = run.played.filter((s) => (s & 0xf0) === 0x90).length;
  console.log(`[lsdj-capture-settings] MI.OUT: ${noteOns} NoteOns`);
  expect(noteOns).toBeGreaterThan(0);
  expect(project.systems.systems().length).toBe(0);
});

test("an LSDj loaded in master sync is built with no capture, placed listening, and its clocks reach the host", () => {
  if (!be.fileExists(LSDJ)) skip(`lsdj-capture-settings: LSDj ROM not found at ${LSDJ}`);
  const run = play(LSDJ, MASTER_SONG, LsdjSyncMode.MasterSync);
  expect(run.builtWithCapture, "a capture in the build spec").toBe(false);
  expect(run.placed, "the capture its node was first placed with").toBe("listen");
  // A stopped master waits on an external clock, which listening never supplies, so its boot sends nothing; and one
  // that was ever clocked would take that clock for another master's and stop answering START.
  expect(run.boot).toEqual([]);
  const clocks = run.played.filter((s) => s === 0xf8).length;
  console.log(`[lsdj-capture-settings] master sync: ${clocks} clocks`);
  expect(clocks).toBeGreaterThan(20);
  expect(project.systems.systems().length).toBe(0);
});
````

````diff
diff --git a/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts b/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
index 5575b32e..ad563165 100644
--- a/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
+++ b/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
@@ -37,8 +37,9 @@ runSession((s) => {
     savPath: null, statePath: null, sramBytes: songSav(sync),
   }, id);
   if (!construct(1, "Lsdj") || !construct(2, "Lsdj")) throw new Error("construct failed");
-  graph.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
-  graph.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
+  // One link group: the cable is wiring, which the document the second edit publishes places.
+  if (!graph.setCoreConfig(1, { linkGroupId: 1 }) || !graph.setCoreConfig(2, { linkGroupId: 1 }))
+    throw new Error("link failed");
 
   s.audio.renderAudio(6000); // reach the song screen from the savs
   s.audio.pressButton(1, START, true); // START on the leader only
diff --git a/packages/retroplug/scripts/skip-baseline.json b/packages/retroplug/scripts/skip-baseline.json
index c810786c..2f21b18a 100644
--- a/packages/retroplug/scripts/skip-baseline.json
+++ b/packages/retroplug/scripts/skip-baseline.json
@@ -48,6 +48,10 @@
    "skipped": 1,
    "total": 1
   },
+  "lsdj-capture-settings": {
+   "skipped": 2,
+   "total": 2
+  },
   "lsdj-codec-corpus": {
    "skipped": 1,
    "total": 1
@@ -84,10 +88,6 @@
    "skipped": 1,
    "total": 1
   },
-  "lsdj-midiout-build-flag": {
-   "skipped": 2,
-   "total": 2
-  },
   "lsdj-old-drift": {
    "skipped": 2,
    "total": 2
diff --git a/packages/retroplug/src/graph/directGraph.ts b/packages/retroplug/src/graph/directGraph.ts
index 829d2350..30256251 100644
--- a/packages/retroplug/src/graph/directGraph.ts
+++ b/packages/retroplug/src/graph/directGraph.ts
@@ -1,24 +1,25 @@
 // Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
 // 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
 // translator kept (add appends, replace keeps the slot, remove shifts later slots down), each system's roles with a
-// protocol node (docs/modular-graph-phase-b3b-design.md section 4), none until set, and the project's MIDI routing
-// and controller, none until set (so no MIDI is routed to any system). It publishes after each call. Use one per
-// process, directGraph(): two publishers with separate EngineGraphs would each drop the other's systems from the
-// document. Sharing a session's EngineGraph (so the session's render calls render what this places) does not merge
-// the two models either: each publish replaces the graph's model with the publisher's own systems. It is safe only
-// while the session's stores place nothing, the two publishing in turn, as test-native/parity/render-blocks.test.ts
-// and cli/sessions/analyze-lsdj-sync.ts do.
+// protocol node (docs/modular-graph-phase-b3b-design.md section 4), none until set, its core config and gain, its
+// node's knobs (docs/modular-graph-phase-c2-design.md section 5), none until set (so its core runs as it was built),
+// and the project's MIDI routing and controller, none until set (so no MIDI is routed to any system). It publishes
+// after each call but setParam, which sets a param live. Use one per process, directGraph(): two publishers with
+// separate EngineGraphs would each drop the other's systems from the document. Sharing a session's EngineGraph (so
+// the session's render calls render what this places) does not merge the two models either: each publish replaces
+// the graph's model with the publisher's own systems. It is safe only while the session's stores place nothing, the
+// two publishing in turn, as test-native/parity/render-blocks.test.ts and cli/sessions/analyze-lsdj-sync.ts do.
 import { createRealBackend } from "../realBackend";
 import type { AudioRouting, MidiRouting } from "../settingsEnums";
 import type { RoleInstance } from "../systemRoles";
 import type { ConstructSpec, EmulatorBackend } from "../backend";
 import { EngineGraph } from "../engineGraph";
 import { parseAppConfig } from "../controller/registry";
-import type { GraphSystem } from "./buildGraph";
+import { systemKnobs, type GraphSystem } from "./buildGraph";
 import type { GraphController } from "./controller";
 import { protocolPipeline, type LeftOutRole } from "./protocolNodes";
 
-export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "applyRoleConfig">;
+export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "setParam">;
 
 export class DirectGraph {
   readonly graph: EngineGraph;
@@ -97,15 +98,47 @@ export class DirectGraph {
     return this.change(() => (this.controller = parsed), false);
   }
 
-  /** Apply a system role's config to the live core. A `sameboy` config's link group is wiring as well: native
-   *  decodes a config without one as group 0, so the cable follows what the core is told. */
-  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
-    const ok = this.backend.applyRoleConfig(id, kind, config);
+  /** Edit system `id`'s core config as the stores' core-config role spells it (`sameboy` or `mesen`, coreRoles.ts: an
+   *  enum as its label, never an ordinal), merged over what this graph gave it before: none at first, so its node
+   *  carries no setting field and its core runs as it was built. Its setting fields ride its node's settings and a
+   *  NES's expansionVolume its params (buildGraph.ts systemKnobs), each reaching the core at the start of the next
+   *  block, and a Game Boy's linkGroupId is its link cable. It publishes, the param included (a document's param value
+   *  reaches the node when it differs from the one last sent). False, changing nothing, when `id` is not placed here,
+   *  for a field its node does not take (a construct-time one, another core's, or a value the node would refuse, an
+   *  ordinal among them, which buildGraph would leave out, the core silently keeping what it has), or when the
+   *  document did not plan. */
+  setCoreConfig(id: number, partial: Record<string, unknown>): boolean {
+    const at = this.systems.findIndex((s) => s.id === id);
+    if (at < 0) return false;
+    const system = this.systems[at];
+    const knobs = systemKnobs({ type: system.type, coreConfig: partial });
+    const group = partial.linkGroupId;
+    const isGroup = typeof group === "number" && Number.isInteger(group) && group >= 0 && group <= 255;
+    const takes = (field: string): boolean =>
+      field in knobs.settings ||
+      field in knobs.params ||
+      (field === "linkGroupId" && system.type === "rp.sameboy.gb" && isGroup);
+    if (!Object.keys(partial).every(takes)) return false;
+    const coreConfig = { ...system.coreConfig, ...partial };
+    const linkGroup = typeof group === "number" ? group : system.linkGroup;
+    return this.change(() => (this.systems[at] = { ...system, coreConfig, linkGroup }), false);
+  }
+
+  /** A live change to a param of system `id`'s node: its `gain` (dB), or a NES's `expansionVolume` (percent), set
+   *  through backend.setParam at the start of the next block with no publish, and kept in the model, so a document
+   *  published later carries the value the node runs, as the store keeps one. False, keeping nothing, when `id` is not
+   *  placed here, for a param no system node has, or when the engine refuses it (a param this node does not have, a
+   *  NaN). */
+  setParam(id: number, param: string, value: number): boolean {
     const at = this.systems.findIndex((s) => s.id === id);
-    if (kind !== "sameboy" || at < 0 || this.systems[at].type !== "rp.sameboy.gb") return ok;
-    const group = typeof config.linkGroupId === "number" ? config.linkGroupId : 0;
-    if (group !== this.systems[at].linkGroup) this.change(() => (this.systems[at] = { ...this.systems[at], linkGroup: group }), false);
-    return ok;
+    if (at < 0 || (param !== "gain" && param !== "expansionVolume")) return false;
+    if (!this.backend.setParam(`sys-${id}`, param, value)) return false;
+    const system = this.systems[at];
+    this.systems[at] =
+      param === "gain"
+        ? { ...system, gainDb: value }
+        : { ...system, coreConfig: { ...system.coreConfig, [param]: value } };
+    return true;
   }
 
   // Apply an edit to the model and publish; a refused document puts the model back.
diff --git a/packages/retroplug/test-native/app-mesen-settings.test.ts b/packages/retroplug/test-native/app-mesen-settings.test.ts
index 13b1fd2f..9f0d2a3a 100644
--- a/packages/retroplug/test-native/app-mesen-settings.test.ts
+++ b/packages/retroplug/test-native/app-mesen-settings.test.ts
@@ -1,8 +1,8 @@
 // C2: the "mesen" system-role knobs (NES Region + Remove Sprite Limit) reach a REAL Mesen core.
 // Two native paths: (1) a non-default region seeded via the construct settings blob boots the core
-// (configureNes consumes it before LoadRom); (2) live setRoleConfig edits cross applyRoleConfig →
-// Engine::applyConfigField → the setters — the live sprite-limit toggle and the region change (which
-// forces emu_->Reset()) both leave the core alive and rendering. (Honest scope: this proves the
+// (configureNes consumes it before LoadRom); (2) live setRoleConfig edits ride the published document as the
+// NES node's settings, which it hands the setters at its next block: the live sprite-limit toggle and the
+// region change (which forces emu_->Reset()) both leave the core alive and rendering. (Honest scope: this proves the
 // round-trip reaches the core and survives both apply modes; the knobs' visible/timing effect isn't
 // asserted headlessly — the TS mock tests carry the semantics.)
 import { test, expect, skip } from "../testing/harness";
diff --git a/packages/retroplug/test-native/app-settings.test.ts b/packages/retroplug/test-native/app-settings.test.ts
index 0ff21646..98d2b755 100644
--- a/packages/retroplug/test-native/app-settings.test.ts
+++ b/packages/retroplug/test-native/app-settings.test.ts
@@ -44,7 +44,7 @@ test("live gain silences/restores a ringing mGB, and sameboy role config dispatc
 
   const loud = chordRms(); // the chord rings
 
-  // --- live gain: the crisp proof that applySystemSetting reaches the running core. Discard a short
+  // --- live gain: the crisp proof that the gain param the store sets reaches the running core. Discard a short
   // settle window after each edit so RMS measures the settled level, not the ~20 ms gain ramp. ---
   expect(project.systems.setGain(id, -90)).toBeTruthy(); // floor → ~silence
   audio.renderAudio(150); // let the gain smoother ramp down (discard the transient)
@@ -58,8 +58,8 @@ test("live gain silences/restores a ringing mGB, and sameboy role config dispatc
   expect(silenced < 0.001).toBeTruthy(); // -90 dB attenuated it to silence
   expect(restored > silenced).toBeTruthy(); // 0 dB brought it back — gain edits reach the live core
 
-  // --- sameboy system-role config: highpass is live, model triggers a restart. Both must dispatch
-  // through applyRoleConfig and leave the system playing. ---
+  // --- sameboy system-role config: highpass is live, model triggers a restart. Both must reach the core
+  // as its node's settings, published, and leave the system playing. ---
   expect(project.systems.setRoleConfig(id, "sameboy", { highpass: "removeDcOffset" })).toBeTruthy(); // RemoveDcOffset, live
   const afterHighpass = rms(audio.renderAudio(500));
   expect(afterHighpass > 0.001).toBeTruthy(); // still ringing after a live filter change
diff --git a/packages/retroplug/test-native/author-sms-rplg.ts b/packages/retroplug/test-native/author-sms-rplg.ts
index a3560d8c..07ddc832 100644
--- a/packages/retroplug/test-native/author-sms-rplg.ts
+++ b/packages/retroplug/test-native/author-sms-rplg.ts
@@ -48,8 +48,8 @@ const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
 // instead of muting it (see sms-fm.test.ts) - but it is kept, for a different reason: with FM off the
 // YM2413 provider contributes nothing to the mix at all, so the drift render measures the PSG
 // metronome alone. The battery's OPTIONS block sets the ROM's own fm_on = 0 to match. Passed as a role
-// config so it lands at CONSTRUCT: configureSms runs before LoadRom, so a later applyRoleConfig would
-// not take.
+// config so it lands at CONSTRUCT: configureSms runs before LoadRom, so a later edit would not take
+// (enableFm is a construct-time field, rebuilt by a reset).
 const id = project.systems.adopt(
   {
     romPath: __SMS_ROM__,
diff --git a/packages/retroplug/test-native/dsp-bench.test.ts b/packages/retroplug/test-native/dsp-bench.test.ts
index 3eb0d440..363be6d2 100644
--- a/packages/retroplug/test-native/dsp-bench.test.ts
+++ b/packages/retroplug/test-native/dsp-bench.test.ts
@@ -14,7 +14,7 @@
 // native Engine is shared per host process).
 import { test, expect, skip } from "../testing/harness";
 import { createAudioDriver } from "../src/audioDriver";
-import type { ConstructSpec, SerialOutCapture } from "../src/backend";
+import type { ConstructSpec } from "../src/backend";
 import { directGraph } from "../src/graph/directGraph";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { createRealBackend } from "../src/realBackend";
@@ -327,15 +327,14 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
     romPath: string,
     song: SavInput,
     mode: string,
-    capture: SerialOutCapture,
     minClocks: number,
   ): void => {
     if (!be.fileExists(romPath)) skip(`dsp-bench: the ${workload} window needs the LSDj ROM at ${romPath}`);
     const id = extra + 1;
     const spec: ConstructSpec = { romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null };
     expect(graph.add({ ...spec, sramBytes: savFrom(song) }, id)).toBeTruthy();
+    // The mode's capture rides the same document, on the Game Boy's node, as a store places it.
     expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode } }])).toBeTruthy();
-    expect(be.setSerialOutCapture(id, capture)).toBeTruthy(); // what the store does for the mode
     audio.renderAudio(6000); // the song screen
     audio.pressButton(id, LSDJ_START, true); // play
     audio.renderAudio(80);
@@ -378,6 +377,6 @@ test("dsp-bench: the script nodes' per-block allocation under an mGB + MIDI work
     expectWindowLive();
   };
   const seconds = (lsdjBlocks * BLOCK_MS) / 1000;
-  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", "listen", 40 * seconds);
-  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", "drive", 2 * seconds);
+  lsdjWindow("lsdj-masterSync", LSDJ_ROM, MASTER_SONG, "masterSync", 40 * seconds);
+  lsdjWindow("lsdj-midiOut", LSDJ_ABOY_ROM, MIDIOUT_SONG, "midiOut", 2 * seconds);
 });
diff --git a/packages/retroplug/test-native/lsdj-mastersync.test.ts b/packages/retroplug/test-native/lsdj-mastersync.test.ts
index c6e328ab..483fb011 100644
--- a/packages/retroplug/test-native/lsdj-mastersync.test.ts
+++ b/packages/retroplug/test-native/lsdj-mastersync.test.ts
@@ -1,14 +1,14 @@
 // LSDj Master Sync (Arduinoboy "Mode 2 — LSDJ as MIDI Master Sync", lsdj-sync mode 8) — the end-to-end
 // functional proof on a real LSDj core. A stock LSDj set to SYNC=LSDj, once playing, self-clocks as the
 // serial MASTER and streams one byte out its link port per MIDI-clock tick. Native captures those bytes
-// (setSerialOutCapture's `listen`: the internal-clock serialStart path, same serialOutLog_ MI.OUT uses, no
-// peer needed, and never `drive`, which would clock a stopped LSDj's wait for another master and turn it
-// slave) and feeds them, in the block it captured them, to the lsdj-sync role's decoder after the Game Boy
-// (rp.lsdj.serial-out), where the mode-8 decoder turns each byte into a 0xF8 clock at its frame (+ a song-row
-// NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows LSDj's tempo. The decoder times
-// the silence on the bytes' own frames and sends every message at a frame of its own, so what it decodes must not
-// depend on how the host cuts its blocks, at either rate, and its clocks lie a tempo period apart whatever the
-// block size: the second case holds it to both.
+// (`listen`, the Game Boy node's capture setting the mode places: the internal-clock serialStart path, same
+// serialOutLog_ MI.OUT uses, no peer needed, and never `drive`, which would clock a stopped LSDj's wait for
+// another master and turn it slave) and feeds them, in the block it captured them, to the lsdj-sync role's
+// decoder after the Game Boy (rp.lsdj.serial-out), where the mode-8 decoder turns each byte into a 0xF8 clock
+// at its frame (+ a song-row NoteOn/0xFA at run start) and a 200 ms silence into 0xFC. So the host follows
+// LSDj's tempo. The decoder times the silence on the bytes' own frames and sends every message at a frame of
+// its own, so what it decodes must not depend on how the host cuts its blocks, at either rate, and its clocks
+// lie a tempo period apart whatever the block size: the second case holds it to both.
 //
 // A playing stock LSDj master emits a TEMPO-LOCKED byte stream (~50/sec = ~130 BPM at 24 PPQN: tens a second,
 // NOT thousands), and a stopped one goes quiet. The thousands-a-second stream once taken for SYNC=LSDj's raw link
@@ -67,8 +67,7 @@ test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte
   }, id)).toBeTruthy();
 
   expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
-  expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
-  expect(be.setSerialOutCapture(id, "listen")).toBeTruthy(); // what the store does for mode 8
+  expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy(); // its Game Boy's node listens, as a store's does
 
   audio.renderAudio(6000); // reach the song screen
   // Nothing from the boot: a stopped LSDj master waits on an external clock, which `listen` never supplies.
@@ -154,7 +153,6 @@ test("Master Sync tells one story at 44.1 and 48 kHz in 1-frame, FL-style ragged
       expect(graph.add({ romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: savFrom(MASTER_SONG) }, id)).toBeTruthy();
       expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
       expect(graph.setPipeline(id, lsdjSync(MASTER_SYNC))).toBeTruthy();
-      expect(be.setSerialOutCapture(id, "listen")).toBeTruthy();
       // Each message with its absolute frame: its block's start, counted from the system's first block, plus its frame.
       const sent: { at: number; data: number[] }[] = [];
       let next = 0; // the absolute frame the next render starts at
diff --git a/packages/retroplug/test-native/lsdj-midiout.test.ts b/packages/retroplug/test-native/lsdj-midiout.test.ts
index 33a86890..48844768 100644
--- a/packages/retroplug/test-native/lsdj-midiout.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout.test.ts
@@ -1,11 +1,11 @@
 // Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) — the full end-to-end functional proof on a
 // real LSDj core. A song authored with SYNC=MI.OUT + N (NoteOn) commands, once playing, emits the
-// Arduinoboy protocol on LSDj's serial-out port; native captures those raw bytes (armed via
-// setSerialOutCapture) and feeds them, in the block it captured them, to the lsdj-sync role's decoder after the
-// Game Boy (rp.lsdj.serial-out), where the mode-7 decoder strips the flag-gated framing + decodes the byte
-// protocol into host MIDI (its midiOut port), each message at the frame of the byte that completes it. We drain
-// that MIDI and assert a transport START (0xFA), a clock stream (0xF8), and NoteOns (0x9x), at frames inside
-// their blocks.
+// Arduinoboy protocol on LSDj's serial-out port; native captures those raw bytes (`drive`, the Game Boy node's
+// capture setting the mode places) and feeds them, in the block it captured them, to the lsdj-sync role's
+// decoder after the Game Boy (rp.lsdj.serial-out), where the mode-7 decoder strips the flag-gated framing +
+// decodes the byte protocol into host MIDI (its midiOut port), each message at the frame of the byte that
+// completes it. We drain that MIDI and assert a transport START (0xFA), a clock stream (0xF8), and NoteOns
+// (0x9x), at frames inside their blocks.
 //
 // This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT
 // is byte 9 (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing — which the raw 8-bit
@@ -69,11 +69,9 @@ test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI its dec
   }, id)).toBeTruthy();
 
   expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
+  // The mode's capture, `drive`, rides the same document on the Game Boy's node, as a store places it.
   expect(graph.setPipeline(id, lsdjSync(MIDIOUT))).toBeTruthy();
 
-  // Arm serial-out capture (what the store does when a system's lsdj-sync mode is MIDIOUT).
-  expect(be.setSerialOutCapture(id, "drive")).toBeTruthy();
-
   audio.renderAudio(6000); // reach the song screen
   audio.drainMidiOut(); // discard any boot-time noise
 
diff --git a/packages/retroplug/test-native/lsdj-sync-negative.test.ts b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
index 27207270..3e6caafd 100644
--- a/packages/retroplug/test-native/lsdj-sync-negative.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
@@ -48,8 +48,8 @@ test("LSDj link-cable sync control: the follower stays silent without SYNC=LSDJ"
   const leader = 1, follower = 2;
   expect(construct(leader, "None")).toBeTruthy();
   expect(construct(follower, "None")).toBeTruthy(); // SYNC=None → no link response
-  expect(graph.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
-  expect(graph.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.setCoreConfig(leader, { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.setCoreConfig(follower, { linkGroupId: 1 })).toBeTruthy();
 
   audio.renderAudio(6000);
 
diff --git a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
index 565bde9f..ae99bcb7 100644
--- a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
@@ -52,10 +52,10 @@ test("LSDj link-cable sync: the follower plays in lockstep with the leader (per-
   const leader = 1, follower = 2;
   expect(construct(leader, "Lsdj")).toBeTruthy();
   expect(construct(follower, "Lsdj")).toBeTruthy(); // same one-note SYNC=LSDJ song
-  // Put both in link group 1 → DirectGraph.applyRoleConfig publishes again, buildGraph wires the cable, and
+  // Put both in link group 1 → DirectGraph.setCoreConfig publishes again, buildGraph wires the cable, and
   // the pair steps as one coupled group.
-  expect(graph.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
-  expect(graph.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.setCoreConfig(leader, { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.setCoreConfig(follower, { linkGroupId: 1 })).toBeTruthy();
 
   audio.renderAudio(6000); // valid savs skip the self-test; still need a few s to the song screen
 
diff --git a/packages/retroplug/test-native/lsdj-wram-seam.test.ts b/packages/retroplug/test-native/lsdj-wram-seam.test.ts
index 23155838..09a7123f 100644
--- a/packages/retroplug/test-native/lsdj-wram-seam.test.ts
+++ b/packages/retroplug/test-native/lsdj-wram-seam.test.ts
@@ -80,7 +80,8 @@ test("readRam publishes live WRAM per block on the emulator facet, and drives th
 // Regression: a live SameBoy model switch RESIZES the WRAM region (CGB 32K ↔ DMG 8K). The snapshot slot
 // carries a length prefix + headroom instead of being pinned to the construct-time size, so readRam tracks
 // the new size every block. (Before the fix, the registry only republished on an EXACT size match, so any
-// resize froze readRam at the pre-switch snapshot forever.) MODEL_VALUES index 1 = "dmgB", 9 = "cgbC".
+// resize froze readRam at the pre-switch snapshot forever.) The switch is the Game Boy node's model setting, a
+// label.
 test("readRam tracks a live model switch that resizes WRAM (CGB 32K → DMG 8K), never freezing", () => {
   const be = createRealBackend();
   if (!be.fileExists(LSDJ)) skip(`lsdj-wram-model-switch: LSDj ROM not found at ${LSDJ}`);
@@ -95,7 +96,7 @@ test("readRam tracks a live model switch that resizes WRAM (CGB 32K → DMG 8K),
   expect(be.readRam(id)!.length).toBe(0x8000); // cgbC default → 32K CGB WRAM
 
   // Switch to a DMG-family model → restartEmulator rebuilds the core with an 8K WRAM region.
-  expect(graph.applyRoleConfig(id, "sameboy", { model: 1 })).toBeTruthy(); // dmgB
+  expect(graph.setCoreConfig(id, { model: "dmgB" })).toBeTruthy(); // its node's model setting
   audio.renderAudio(2000); // a block republishes the (now smaller) region
 
   const dmgRam = be.readRam(id)!;
diff --git a/packages/retroplug/test-native/parity/render-blocks.test.ts b/packages/retroplug/test-native/parity/render-blocks.test.ts
index 3d2673f1..6298db9b 100644
--- a/packages/retroplug/test-native/parity/render-blocks.test.ts
+++ b/packages/retroplug/test-native/parity/render-blocks.test.ts
@@ -187,8 +187,7 @@ test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, t
       ),
     ).toBeTruthy();
     expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
-    expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "midiOut" } }])).toBeTruthy();
-    expect(be.setSerialOutCapture(id, "drive")).toBeTruthy();
+    expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "midiOut" } }])).toBeTruthy(); // and its capture
     render(6000); // reach the song screen; its output is not compared
     const out: Msg[] = [];
     const tap = () => {
diff --git a/packages/retroplug/test-native/sameboy-state-model-switch.test.ts b/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
index 6cb3e1c9..2f267218 100644
--- a/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
+++ b/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
@@ -1,7 +1,7 @@
 // Regression: a live SameBoy model switch must not freeze the published savestate / SRAM.
 //
 // The state snapshot slot is sized, and its region table captured, when the system is BUILT. A model
-// switch rebuilds the core in place (Engine::applyConfigField -> restartEmulator) while keeping the same
+// switch rebuilds the core in place (its system node's model setting -> restartEmulator) while keeping the same
 // SystemId, so before the fix the slot kept describing the dead core: a larger capture overflowed it and
 // publishStateSnapshot skipped forever, freezing readState/readSram at their pre-switch values. Downstream
 // that is real data loss - the SRAM auto-save mirror stops tracking the battery, and a project export
@@ -9,7 +9,8 @@
 //
 // Why the existing coverage missed it: the default model is cgbC and the one live-switch test goes
 // cgbC -> dmgB, which SHRINKS and therefore fits. Seeing the overflow needs a DMG-family model at build
-// and a switch UP. These tests construct on dmgB via the construct-time `settings` blob.
+// and a switch UP. These tests construct on dmgB via the construct-time `settings` blob, which takes a
+// model as its native ordinal, and switch through the node's model setting, which takes it as its label.
 //
 // The WRAM plane was already fixed this way (lsdj-wram-seam.test.ts); this is its state/SRAM twin.
 // MODEL_VALUES indices: 1 = dmgB, 3 = sgb, 9 = cgbC, 12 = agb.
@@ -18,6 +19,7 @@ import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
+import { MODEL_VALUES } from "../src/settingsEnums";
 import { directGraph } from "../src/graph/directGraph";
 
 // Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
@@ -60,6 +62,9 @@ const buildOn = (id: number, model: number): boolean =>
     settings: JSON.stringify({ model }),
   }, id);
 
+/** Switch a built system to `model` (an ordinal, as buildOn takes it) through its node's model setting. */
+const switchTo = (id: number, model: number): boolean => graph.setCoreConfig(id, { model: MODEL_VALUES[model] });
+
 const requireRom = (be: ReturnType<typeof createRealBackend>, name: string): void => {
   if (!be.fileExists(LSDJ)) skip(`${name}: LSDj ROM not found at ${LSDJ}`);
 };
@@ -77,7 +82,7 @@ test("readState tracks a live model switch that GROWS the savestate (dmgB → cg
   expect(be.readRam(id)!.length).toBe(DMG_WRAM); // corroborates the core really booted as DMG
 
   // Switch UP: the CGB core's savestate is exactly RAM_VRAM_DELTA larger than the DMG one it replaces.
-  expect(graph.applyRoleConfig(id, "sameboy", { model: CGB_C })).toBeTruthy();
+  expect(switchTo(id, CGB_C)).toBeTruthy();
   audio.renderAudio(2000);
 
   const cgbState = be.readState(id)!;
@@ -113,7 +118,7 @@ test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB 
   // DMG<->CGB switch this genuinely MOVES Sram.offset. A fix that only widened the slot (without
   // re-reading the region table) would slice here at the stale, too-low offset and hand the SRAM
   // auto-save mirror the SGB section's bytes as if they were the battery.
-  expect(graph.applyRoleConfig(id, "sameboy", { model: SGB })).toBeTruthy();
+  expect(switchTo(id, SGB)).toBeTruthy();
   audio.renderAudio(2000);
 
   const sgbState = be.readState(id)!;
@@ -147,7 +152,7 @@ test("a switched-into model publishes the same state size as one constructed on
     const built = be.readState(fresh)!.length;
     graph.remove(fresh); // a live system is emulated by every later renderAudio in this file
 
-    expect(graph.applyRoleConfig(live, "sameboy", { model: m })).toBeTruthy();
+    expect(switchTo(live, m)).toBeTruthy();
     audio.renderAudio(1600);
     const switched = be.readState(live)!.length;
 
diff --git a/packages/retroplug/test/system-settings/store.test.ts b/packages/retroplug/test/system-settings/store.test.ts
index 11d2f148..85e6a1d6 100644
--- a/packages/retroplug/test/system-settings/store.test.ts
+++ b/packages/retroplug/test/system-settings/store.test.ts
@@ -76,7 +76,6 @@ test("a NES system auto-gets the mesen system role; editing region crosses to th
       params: { gain: 0, expansionVolume: 100 },
     }),
   );
-  expect(be.applyRoleCalls.length).toBe(0);
 });
 
 test("setRoleConfig on a system role: updates, clamps, publishes its node's settings, dirties", () => {
@@ -102,7 +101,6 @@ test("setRoleConfig on a system role: updates, clamps, publishes its node's sett
       serialOutCapture: "off",
     }),
   );
-  expect(be.applyRoleCalls.length).toBe(0);
   expect(changes()).toBe(before + 1);
   // unknown enum → defaulted, not stored raw
   store.setRoleConfig(id, "sameboy", { model: 99 });
@@ -128,7 +126,6 @@ test("setGain / setReloadOnRomChange: clamp, update; the gain set live on its no
     { nodeId: `sys-${id}`, paramId: "gain", value: -90 },
   ]);
   expect(be.graphRequests.length).toBe(sent); // no publish
-  expect(be.applySettingCalls.length).toBe(0);
   expect(changes()).toBe(before + 3); // each dirties
   expect(store.setGain(9999, 0)).toBeFalsy(); // absent id
   expect(store.setReloadOnRomChange(9999, true)).toBeFalsy();
@@ -141,9 +138,11 @@ test("a feature role attaches via the provider; editing it is PURE TS (no emulat
   const kinds = store.view()[0].roles.map((r) => r.kind);
   expect(kinds).toEqual(["sameboy", "demo-sync"]); // system role + provider feature role
 
+  const sent = be.graphRequests.length;
   expect(store.setRoleConfig(id, "demo-sync", { level: 5 })).toBeTruthy();
   expect(store.view()[0].roles[1].config.level).toBe(5);
-  expect(be.applyRoleCalls.length).toBe(0); // feature-role config never crosses to native
+  // Feature-role config never crosses to native: no document, no param.
+  expect(be.graphRequests.length).toBe(sent);
   expect(be.paramCalls.length).toBe(0);
 });
 
diff --git a/packages/retroplug/test/systems/build-spec-wire.test.ts b/packages/retroplug/test/systems/build-spec-wire.test.ts
index 70f78be1..783f24ab 100644
--- a/packages/retroplug/test/systems/build-spec-wire.test.ts
+++ b/packages/retroplug/test/systems/build-spec-wire.test.ts
@@ -1,8 +1,7 @@
-// The build spec as realBackend.ts puts it on the wire. A loaded LSDj in a MIDI-out mode is built capturing serial
-// out through `serialOutCapture`, and native skips a field it does not know, so the key must be the one
-// BackendConstructSpec names (the engine-graph Catch2 case decodes that same key on the native side). And setParam,
-// how the stores set a gain or a NES's expansion volume live, as EngineRpcService::setParam takes it. Driven over a
-// stand-in __rpcSend that records each request, so no native host is needed.
+// The build spec as realBackend.ts puts it on the wire: native skips a field it does not know, so each key must be the
+// one BackendConstructSpec names, and a null path is left out rather than sent as "" (native reads an absent field as
+// nullopt). And setParam, how the stores set a gain or a NES's expansion volume live, as EngineRpcService::setParam
+// takes it. Driven over a stand-in __rpcSend that records each request, so no native host is needed.
 import { test, expect } from "../../testing/harness";
 import { createEmulatorClient } from "../../src/realBackend";
 import type { ConstructSpec } from "../../src/backend";
@@ -38,27 +37,18 @@ const spec = (extra: Partial<ConstructSpec> = {}): ConstructSpec => ({
   ...extra,
 });
 
-test("buildCore carries serialOutCapture under that exact key, each mode as its name", () => {
+test("buildCore carries each field under BackendConstructSpec's name, a null path and an absent seed left out", () => {
   const { sent, client } = recording();
-  client.buildCore(spec({ serialOutCapture: "drive" }), 7);
-  client.buildCore(spec({ serialOutCapture: "listen" }), 8);
+  const sramBytes = new Uint8Array([1, 2, 3]);
+  client.buildCore(spec({ savPath: "/roms/a.sav", sramBytes, settings: "{}" }), 7);
+  client.buildCore(spec(), 8);
   expect(sent.map((r) => r.method)).toEqual(["buildCore", "buildCore"]);
   const params = sent.map((r) => r.params[0] as Record<string, unknown>);
-  expect(params.map((p) => [p.id, p.serialOutCapture])).toEqual([[7, "drive"], [8, "listen"]]);
-});
-
-test("buildCore does not arm the capture for a spec without the field", () => {
-  const { sent, client } = recording();
-  client.buildCore(spec(), 7);
-  expect(sent.length).toBe(1);
-  expect("serialOutCapture" in (sent[0].params[0] as Record<string, unknown>)).toBe(false);
-});
-
-test("setSerialOutCapture carries the mode, and null for off", () => {
-  const { sent, client } = recording();
-  client.setSerialOutCapture(3, "listen");
-  client.setSerialOutCapture(3, null);
-  expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setSerialOutCapture", 3, "listen"], ["setSerialOutCapture", 3, null]]);
+  const named = ["core", "embeddedRom", "id", "platform", "romPath"];
+  expect(Object.keys(params[0]).sort()).toEqual([...named, "savPath", "settings", "sramBytes"]);
+  expect([params[0].id, params[0].savPath, params[0].settings]).toEqual([7, "/roms/a.sav", "{}"]);
+  expect(params[0].sramBytes).toBe(sramBytes); // as the Uint8Array it was: rfl::Bytestring on the native side
+  expect(Object.keys(params[1]).sort()).toEqual(named);
 });
 
 test("setParam carries the node, the param and the value, in that order", () => {
diff --git a/packages/retroplug/test/systems/store-graph.test.ts b/packages/retroplug/test/systems/store-graph.test.ts
index 79190d4e..6d0124dd 100644
--- a/packages/retroplug/test/systems/store-graph.test.ts
+++ b/packages/retroplug/test/systems/store-graph.test.ts
@@ -181,17 +181,19 @@ test("a setting edit publishes its system node's settings, without a re-arm; a l
   expect(linked(be, `sys-${b}.link`)).toBe(true);
 });
 
-test("no store edit takes the side path the document replaced", () => {
+test("a knob edit reaches the engine as a document or a param, and reload-on-ROM-change not at all", () => {
   const { be, sys } = project();
   const a = sys.addSystem("/roms/lsdj.gb")!;
   const c = sys.addSystem("/roms/c.nes")!;
+  const from = be.log.length;
   sys.setGain(a, -3);
   sys.setReloadOnRomChange(a, true);
   sys.setRoleConfig(a, "sameboy", { model: "sgb", lightTemperature: 0.5 });
   sys.setRoleConfig(a, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
   sys.setRoleConfig(c, "mesen", { region: "ntsc", expansionVolume: 150, removeSpriteLimit: true });
   sys.setRoleConfig(c, "mesen", { expansionVolume: 120 });
-  expect(be.log.filter((m) => ["applySystemSetting", "applyRoleConfig", "setSerialOutCapture"].includes(m))).toEqual([]);
+  // The gain and the last expansion volume set live, every other edit a document: nothing else reaches the backend.
+  expect(be.log.slice(from).filter((m) => m !== "setGraph")).toEqual(["setParam", "setParam"]);
   // reload-on-ROM-change is TS's alone (the file watcher): it reaches no backend at all.
   expect(sys.view()[0].settings.reloadOnRomChange).toBe(true);
 });
@@ -417,22 +419,20 @@ test("an adopt outside a load places its core at once", () => {
 test("a system in an LSDj MIDI-out mode captures its serial out as its document says: MI.OUT drives, master sync listens", () => {
   const { be, sys } = project();
   let id = sys.addSystem("/roms/lsdj.gb")!;
-  const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
   const placed = () => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
-  expect(lastBuild()).toBe(undefined); // midiSync by default
-  expect(placed()).toBe("off");
+  expect(placed()).toBe("off"); // midiSync by default
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
   expect(placed()).toBe("drive"); // in the document that hands its decoder the mode
-  id = sys.reset(id)!; // a rebuild, under a new id: the build arms the capture its node carries too
-  expect(lastBuild()).toBe("drive");
+  id = sys.reset(id)!; // a rebuild, under a new id: the document that places it carries the capture its build does not
   expect(placed()).toBe("drive");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MasterSync });
   expect(placed()).toBe("listen");
   id = sys.reset(id)!;
-  expect(lastBuild()).toBe("listen");
+  expect(placed()).toBe("listen");
   sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
   expect(placed()).toBe("off");
-  expect(be.serialOutCaptureCalls).toEqual([]);
+  // No build spec carries a capture: the core runs only once a document places it, and its node arms it first.
+  expect(be.buildCalls.filter((c) => "serialOutCapture" in c)).toEqual([]);
 });
 
 test("a cart the controller drives in MI.MAP captures nothing, and gets its stored mode's capture back with the controller off", () => {
@@ -842,7 +842,7 @@ test("an adopt whose core does not build logs nothing about the system it never
   expect(warnings).toEqual([]);
 });
 
-test("a left-out lsdj-sync arms no serial-out capture: not at a build, nor on an edit", () => {
+test("a left-out lsdj-sync arms no serial-out capture: not where it is placed, nor on an edit", () => {
   // Capture is for the lsdj-sync role's decoder, rp.lsdj.serial-out, which decodes what the core sends (MI.OUT
   // drives, master sync listens). On an mGB cart whose project stored lsdj-sync, mgb wins serialIn and the role's
   // nodes are not placed, so nothing would read the capture, and a driving core would clock the serial port under
@@ -853,18 +853,15 @@ test("a left-out lsdj-sync arms no serial-out capture: not at a build, nor on an
     { kind: "sameboy", config: {} },
     { kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiOut } },
   ];
-  const lastBuild = () => be.buildCalls[be.buildCalls.length - 1].serialOutCapture;
   const placed = (id: number) => be.graphNode(`sys-${id}`)?.settings?.serialOutCapture;
   let id = warned(() => sys.adopt({ romPath: "/roms/mgb.gb", core: "sameboy", roles })!).result;
   expect(be.protocolNodes(id)).toEqual([`sys-${id}/mgb`]);
-  expect(lastBuild()).toBe(undefined); // a load, stored in MI.OUT
-  expect(placed(id)).toBe("off");
+  expect(placed(id)).toBe("off"); // a load, stored in MI.OUT
   for (const mode of [LsdjSyncMode.MasterSync, LsdjSyncMode.MidiOut]) {
     expect(sys.setRoleConfig(id, "lsdj-sync", { mode })).toBe(true);
     expect(placed(id)).toBe("off"); // an edit
     id = sys.reset(id)!;
-    expect(lastBuild()).toBe(undefined); // a rebuild
-    expect(placed(id)).toBe("off");
+    expect(placed(id)).toBe("off"); // a rebuild
   }
 });
 
@@ -890,20 +887,49 @@ test("DirectGraph keeps the B1 model: append, replace in place, remove shifting
   expect(be.buildCalls.some((c) => c.id === 5)).toBe(false);
 });
 
-test("DirectGraph rewires a Game Boy's link group as the core is told it", () => {
+test("DirectGraph's core config: a link group rewires, a setting rides its node, a field it cannot set is refused", () => {
   const be = new MockBackend("/cfg");
   const g = new DirectGraph(be);
   g.add(MGB, 1);
   g.add(MGB, 2);
+  // None until set: the node carries no setting field, so its core runs as it was built.
+  expect(be.graphNode("sys-1")?.settings).toEqual({ core: 1, serialOutCapture: "off" });
   const n = be.graphRequests.length;
-  g.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
-  g.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
-  expect(be.applyRoleCalls.length).toBe(2);
-  expect(since(be, n)).toEqual([{ slots: [1, 2] }]);
+  expect(g.setCoreConfig(1, { linkGroupId: 1 })).toBe(true);
+  expect(g.setCoreConfig(2, { linkGroupId: 1 })).toBe(true);
+  expect(since(be, n)).toEqual([{ slots: [1, 2] }]); // a group of one is no cable: the first sends nothing
+  expect(linked(be, "sys-2.link")).toBe(true);
+  // Merged over what it had: the group stays, the model rides the node's settings as its label.
+  expect(g.setCoreConfig(2, { model: "dmgB", highpass: "off" })).toBe(true);
   expect(linked(be, "sys-2.link")).toBe(true);
-  // A config with no link group decodes natively as group 0, so the cable goes with it.
-  g.applyRoleConfig(2, "sameboy", { model: 1 });
-  expect(linked(be, "sys-2.link")).toBe(false);
+  const sys2 = canon({ core: 2, model: "dmgB", highpass: "off", serialOutCapture: "off" });
+  expect(be.graphNode("sys-2")?.settings).toEqual(sys2);
+  // An ordinal, a construct-time field, another core's field: each refused, changing nothing.
+  const sent = be.graphRequests.length;
+  const refused: Record<string, unknown>[] = [{ model: 1 }, { channelExportMode: "pins" }, { region: "pal" }];
+  refused.push({ linkGroupId: -1 }, { model: "mgb", fifo: 1 }); // the last: one bad field refuses the whole edit
+  for (const partial of refused) expect(g.setCoreConfig(2, partial)).toBe(false);
+  expect(g.setCoreConfig(9, { model: "mgb" })).toBe(false); // not placed
+  expect(be.graphRequests.length).toBe(sent);
+  expect(be.graphNode("sys-2")?.settings?.model).toBe("dmgB");
+  // The next document, another system's edit, carries the config last taken: a refused edit left nothing behind.
+  expect(g.setCoreConfig(1, { model: "mgb" })).toBe(true);
+  expect(be.graphNode("sys-2")?.settings).toEqual(sys2);
+});
+
+test("DirectGraph's params: set live with no publish, and carried by the next document", () => {
+  const be = new MockBackend("/cfg");
+  const g = new DirectGraph(be);
+  g.add(MGB, 1);
+  const n = be.graphRequests.length;
+  expect(g.setParam(1, "gain", -12)).toBe(true);
+  expect(be.paramCalls).toEqual([{ nodeId: "sys-1", paramId: "gain", value: -12 }]);
+  expect(be.graphRequests.length).toBe(n);
+  expect(g.setParam(1, "level", 1)).toBe(false); // no system node's param
+  expect(g.setParam(9, "gain", 0)).toBe(false); // not placed
+  expect(be.paramCalls.length).toBe(1);
+  g.setCoreConfig(1, { highpass: "off" });
+  expect(be.graphNode("sys-1")?.params).toEqual({ gain: -12 });
 });
 
 test("DirectGraph republishes the model a refused edit put back, which frees the core the edit built", () => {
````

Run: `git rm -q packages/retroplug/test-native/lsdj-midiout-build-flag.test.ts`
Expected: nothing printed.

- [ ] **Step 2: Delete the old path**

Native: the enum, the command kind and its payload, both `applyConfigField`s (Engine.cpp no longer includes
SameBoySystem.hpp), the three RPCs and their registration (the emulator facet now 12 methods, the union 86), the build
spec's `serialOutCapture` and `buildCore`'s arming of it, `SystemBase::setRomReload` / `wantsRomReload` with each
core's overrides and its config's `reloadOnRomChange`, and the comments that named them; the Catch2 case that pinned
the build spec's capture field on the wire goes (the goldens' field-name check holds the document's names). TS: the
three methods, the `SerialOutCapture` type and the build spec's field (its description moves onto
`SerialOutCaptureMode`), the store's build-time capture arming, and `roleConfigForNative` documented as the construct
blob's encoder:

````diff
diff --git a/packages/native/src/host/engine/DspCommand.hpp b/packages/native/src/host/engine/DspCommand.hpp
index 40f81a03..cb223482 100644
--- a/packages/native/src/host/engine/DspCommand.hpp
+++ b/packages/native/src/host/engine/DspCommand.hpp
@@ -13,7 +13,7 @@
 struct DspCommand {
     enum class Kind : std::uint8_t {
         None = 0, StageMidi = 3,
-        SetBpm = 7, SetTransport = 8, SetConfigField = 9, PressButton = 10,
+        SetBpm = 7, SetTransport = 8, PressButton = 10,
         SetPpq = 12, StageControllerMidi = 13, WriteRam = 14,
         StageSystemMidi = 15,
         // The NES tap arming every structural change does (docs/modular-graph-phase-b1-design.md section
@@ -30,7 +30,6 @@ struct DspCommand {
         struct { std::uint8_t data[4]; std::uint8_t len; std::uint32_t frame; std::vector<std::uint8_t>* ext; } stageMidi;
         struct { double value; } setBpm;                          // transport as a queued op
         struct { bool value; } setTransport;
-        struct { std::uint32_t id; std::uint8_t field; double value; } setConfigField; // live setting → core
         struct { std::uint32_t id; std::uint8_t button; bool down; } pressButton; // joypad transition → core
         struct { double value; } setPpq;                         // host playhead jump (locate)
         // Host poke into a core's work RAM. Owning payload (a song block is ~7 KB, far past what the
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index a8739744..5de4b2d4 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -13,8 +13,7 @@
 
 #include "system/SystemBase.hpp"
 #include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
-#include "system/sameboy/SameBoySystem.hpp"  // live-apply cast target (model/highpass/gain/…)
-#include "system/mesen/MesenNesSystem.hpp"    // live-apply cast target (NES region / sprite limit)
+#include "system/mesen/MesenNesSystem.hpp"    // the system table's NES, the tap SyncTap arms
 
 #include "transport/FrameBufferTriple.hpp"
 #include "native/core/img/png/lodepng.h"
@@ -603,9 +602,6 @@ void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
         case DspCommand::Kind::SetBpm:          setBpm(cmd.setBpm.value); break;
         case DspCommand::Kind::SetTransport:    setTransport(cmd.setTransport.value); break;
         case DspCommand::Kind::SetPpq:          setPpq(cmd.setPpq.value); break;
-        case DspCommand::Kind::SetConfigField:
-            applyConfigField(core(cmd.setConfigField.id), cmd.setConfigField.field, cmd.setConfigField.value);
-            break;
         case DspCommand::Kind::PressButton:
             if (SystemBase* sys = core(cmd.pressButton.id))
                 sys->pressButton(cmd.pressButton.button, cmd.pressButton.down);  // spread across the block in onProcess
@@ -884,93 +880,3 @@ bool Engine::stageSystemMidi(SystemBase* sys, const std::uint8_t* bytes, std::si
     sys->onMidi(&ev, 1);
     return true;
 }
-
-void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value) {
-    if (!sys) return;
-
-    // The two UNIVERSAL settings apply to every backend through the base interface — they must run
-    // before the SameBoy-only cast below, or they silently no-op on a Mesen (NES/GBA) system even
-    // though its gain slider / reload toggle look live in the UI.
-    switch (static_cast<ConfigField>(field)) {
-        case ConfigField::Gain:
-            sys->setGainDb(static_cast<float>(value));  // live, smoothed
-            return;
-        case ConfigField::ReloadOnRomChange:
-            sys->setRomReload(value != 0.0);  // a UI-thread ROM-watch flag; no core effect
-            return;
-        default:
-            break;
-    }
-
-    // The remaining fields are backend-specific emulator knobs, dispatched by concrete type (a system
-    // is exactly one, so the order of the casts below doesn't matter). Mesen (NES) first.
-    if (auto* mn = dynamic_cast<MesenNesSystem*>(sys)) {
-        switch (static_cast<ConfigField>(field)) {
-            case ConfigField::NesRegion:
-                mn->setRegion(static_cast<std::uint32_t>(value));  // value-guarded; resets the core on change
-                break;
-            case ConfigField::NesRemoveSpriteLimit:
-                mn->setRemoveSpriteLimit(value != 0.0);            // live — the PPU re-reads it per scanline
-                break;
-            case ConfigField::NesApuLatencyMs:
-                mn->setApuLatencyMs(value);                        // live — re-thresholds the APU flush window
-                break;
-            case ConfigField::NesS5bNoise:
-                mn->setS5bNoise(static_cast<std::uint32_t>(value));         // live — read per sample
-                break;
-            case ConfigField::NesMmc5PhaseReset:
-                mn->setMmc5PhaseReset(static_cast<std::uint32_t>(value));   // live — read per register write
-                break;
-            case ConfigField::NesExpansionVolume:
-                mn->setExpansionVolume(static_cast<std::uint32_t>(value));  // live — mixer re-reads per buffer
-                break;
-            default:
-                break;
-        }
-        return;
-    }
-
-    // SameBoy emulator knobs, through the core's own value-guarded setters, which the Game Boy's system node applies
-    // its settings through too.
-    auto* sb = dynamic_cast<SameBoySystem*>(sys);
-    if (!sb) return;
-    switch (static_cast<ConfigField>(field)) {
-        case ConfigField::Model:
-            sb->setModel(static_cast<SameBoyModel>(static_cast<std::uint32_t>(value)));  // restarts on a change
-            break;
-        case ConfigField::Highpass:
-            sb->setHighpass(static_cast<SameBoyHighpass>(static_cast<std::uint32_t>(value)));
-            break;
-        case ConfigField::LinkGroup: {
-            const auto g = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
-            if (sb->config_.linkGroupId != g) {
-                sb->config_.linkGroupId = g;
-                // the cable is wiring: TS republishes the graph with the group (graph/buildGraph.ts)
-            }
-            break;
-        }
-        case ConfigField::FastBoot:
-            sb->setFastBoot(value != 0.0);  // deferred to the next restart
-            break;
-        case ConfigField::SerialOutCapture: {
-            // Live: the capture mode (LSDj MI.OUT or master sync) as its SerialOutCapture value. Out of range is off.
-            const auto mode = static_cast<std::uint32_t>(value);
-            sb->setSerialOutCapture(mode <= static_cast<std::uint32_t>(SerialOutCapture::Drive) ? static_cast<SerialOutCapture>(mode)
-                                                                                              : SerialOutCapture::Off);
-            break;
-        }
-        // The display group: each setter re-pushes the group, a handful of cheap core setters, only when its knob
-        // moved (the store re-sends the whole role config on every edit).
-        case ConfigField::ColorCorrection:
-            sb->setColorCorrection(static_cast<SameBoyColorCorrection>(static_cast<std::uint32_t>(value)));
-            break;
-        case ConfigField::DmgPalette:
-            sb->setDmgPalette(static_cast<SameBoyDmgPalette>(static_cast<std::uint32_t>(value)));
-            break;
-        case ConfigField::LightTemperature:
-            sb->setLightTemperature(value);
-            break;
-        default:
-            break;  // the universal fields (Gain / ReloadOnRomChange) were handled above
-    }
-}
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 4beb8b61..4430bc3f 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -26,23 +26,6 @@ struct DspCommand;
 namespace rp::graph { struct HostCommand; }
 namespace rp::host { struct SystemTable; }
 
-// A per-system config field a control-plane edit applies to the live core (SameBoy today). Carried
-// as a double across the command ring: gain in dB; a bool as 0/1; an enum/int as its integer value.
-enum class ConfigField : std::uint8_t {
-    Gain = 0, ReloadOnRomChange = 1,
-    Model = 2, Highpass = 3, LinkGroup = 4, FastBoot = 5,     // SameBoy
-    NesRegion = 6, NesRemoveSpriteLimit = 7,                  // Mesen (NES)
-    SerialOutCapture = 8,                                     // SameBoy (LSDj MI.OUT, master sync): a SerialOutCapture
-    NesApuLatencyMs = 9,                                      // Mesen (NES) — APU flush window as latency (ms)
-    // SameBoy display group — all live (they land on the next rendered frame), all routed through
-    // SameBoySystem::applyDisplayConfig.
-    ColorCorrection = 10, DmgPalette = 11, LightTemperature = 12,
-    // Mesen (NES) cartridge-accuracy switches — 0 = chip, 1 = Everdrive N8. Both live.
-    NesS5bNoise = 13, NesMmc5PhaseReset = 14,
-    // Mesen (NES) — the cartridge sound chip's level, percent of unity. Live.
-    NesExpansionVolume = 15,
-};
-
 // One message of the host MIDI out (Engine::midiOut), tagged with the system that emitted it.
 struct EngineMidiOut {
     std::uint32_t             system = 0;
@@ -367,9 +350,6 @@ private:
     // Deliberately unguarded beyond bounds: writing a running core's RAM CAN confuse it, because the
     // emulated program has its own invariants over those bytes. That is the point of the feature.
     bool writeRam(SystemBase* sys, std::uint32_t offset, const std::vector<std::uint8_t>& bytes);
-    // Apply a live config edit to a system. Value-guarded per field so a whole-config re-send only acts on
-    // what changed (no spurious model restart).
-    void applyConfigField(SystemBase* sys, std::uint8_t field, double value);
 
     // The NES tap syncTap() armed, and what its export mode was BEFORE we touched it. Restored before
     // every re-resolve so leaving a split mode (or gaining a second system) puts the core back exactly as
diff --git a/packages/native/src/host/engine/EngineInvoker.cpp b/packages/native/src/host/engine/EngineInvoker.cpp
index 73e8e34e..5f213fc6 100644
--- a/packages/native/src/host/engine/EngineInvoker.cpp
+++ b/packages/native/src/host/engine/EngineInvoker.cpp
@@ -77,13 +77,6 @@ void QueuedInvoker::setPpq(double ppq) {
     push(c);
 }
 
-void QueuedInvoker::applyConfigField(SystemId id, std::uint8_t field, double value) {
-    DspCommand c;
-    c.kind = DspCommand::Kind::SetConfigField;
-    c.setConfigField = { static_cast<std::uint32_t>(id), field, value };
-    push(c);  // no heap payload: dropped on a full ring
-}
-
 void QueuedInvoker::pressButton(SystemId id, std::uint8_t button, bool down) {
     DspCommand c;
     c.kind = DspCommand::Kind::PressButton;
diff --git a/packages/native/src/host/engine/EngineInvoker.hpp b/packages/native/src/host/engine/EngineInvoker.hpp
index 7ea77a07..7c2c1b76 100644
--- a/packages/native/src/host/engine/EngineInvoker.hpp
+++ b/packages/native/src/host/engine/EngineInvoker.hpp
@@ -44,7 +44,6 @@ public:
     void setBpm(double bpm);
     void setTransport(bool playing);
     void setPpq(double ppq);
-    void applyConfigField(SystemId id, std::uint8_t field, double value);
     void pressButton(SystemId id, std::uint8_t button, bool down);
     void writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);
 
diff --git a/packages/native/src/host/engine/SnapshotRegistry.hpp b/packages/native/src/host/engine/SnapshotRegistry.hpp
index e229b95a..2f378681 100644
--- a/packages/native/src/host/engine/SnapshotRegistry.hpp
+++ b/packages/native/src/host/engine/SnapshotRegistry.hpp
@@ -33,8 +33,8 @@ class SystemBase;
 // slot is never concurrently freed.
 //
 // That allocation rule is why the state slot is sized to a CEILING rather than to the live savestate:
-// a SameBoy model switch arrives on the audio thread (Engine::applyConfigField) and resizes the core's
-// state, so growing this buffer to match would be an audio-thread realloc racing a control-thread read.
+// a SameBoy model switch arrives on the audio thread (its system node applies its settings there) and resizes the
+// core's state, so growing this buffer to match would be an audio-thread realloc racing a control-thread read.
 // Sizing once, big enough for any model the backend can switch into, removes the need entirely.
 class SnapshotRegistry {
 public:
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index c901d45f..d78269df 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,15 +35,13 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: lifecycle / script node packs / live params and config / snapshot reads / input (15) ---
+// --- emulator: lifecycle / script node packs / live params / snapshot reads / input (12) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::buildCore>(e);
     s.template addMethod<&EngineRpcService::setGraph>(e);
     s.template addMethod<&EngineRpcService::loadScriptPack>(e);
     s.template addMethod<&EngineRpcService::setParam>(e);
-    s.template addMethod<&EngineRpcService::applySystemSetting>(e);
-    s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
     s.template addMethod<&EngineRpcService::readState>(e);
     s.template addMethod<&EngineRpcService::readSram>(e);
     s.template addMethod<&EngineRpcService::readRam>(e);
@@ -52,7 +50,6 @@ void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::getFrame>(e);
     s.template addMethod<&EngineRpcService::pressButton>(e);
     s.template addMethod<&EngineRpcService::stageSystemMidi>(e);
-    s.template addMethod<&EngineRpcService::setSerialOutCapture>(e);
 }
 
 // --- harness: audio render / transport / MIDI + DSP profiling + kit compile + graph drops (22; CLI + tests only) ---
@@ -125,7 +122,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (89 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (86 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index af6a547d..2ac6d569 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -109,9 +109,6 @@ struct BackendConstructSpec {
     // .sav resolution; only the loaded bytes differ. Honoured by SameBoy and by Mesen's NES, GBA and SMS / GG.
     std::optional<rfl::Bytestring> romBytes;
     std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
-    // Arm a Game Boy's serial-out capture before the core first runs, as a later setSerialOutCapture
-    // would: "drive" (LSDj MI.OUT) or "listen" (master sync), absent for off. Ignored by every other core.
-    std::optional<std::string>               serialOutCapture;
 };
 
 // buildCore's answer (docs/modular-graph-phase-b2-design.md section 2.5): the node type the built core is
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 3f3f0d79..73183ae1 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -11,8 +11,6 @@
 
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
-#include "system/mesen/MesenBackend.hpp"
-#include "system/sameboy/SameBoyBackend.hpp"
 #include "system/SystemFactory.hpp"
 
 #include "EmbeddedRoms.hpp"
@@ -74,16 +72,6 @@ SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
     return out;
 }
 
-// A serial-out capture mode as TS names it on the wire (ConstructSpec.serialOutCapture, setSerialOutCapture): absent is
-// off, and an unknown name is no mode at all.
-std::optional<SerialOutCapture> serialOutCaptureMode(const std::optional<std::string>& name) {
-    if (!name) return SerialOutCapture::Off;
-    if (*name == "listen") return SerialOutCapture::Listen;
-    if (*name == "drive") return SerialOutCapture::Drive;
-    std::fprintf(stderr, "[engine] \"%s\" is not a serial-out capture mode\n", name->c_str());
-    return std::nullopt;
-}
-
 } // namespace
 
 EngineRpcService::EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker)
@@ -113,11 +101,8 @@ std::optional<RpcBuiltCore> EngineRpcService::buildCore(BackendConstructSpec spe
         std::fprintf(stderr, "[engine] buildCore: system %u is still live\n", static_cast<unsigned>(id));
         return std::nullopt;
     }
-    const std::optional<SerialOutCapture> capture = serialOutCaptureMode(spec.serialOutCapture);
-    if (!capture) return std::nullopt;
     auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
     if (!sys) return std::nullopt;  // unknown backend / unreadable or unrecognised ROM
-    if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) sb->setSerialOutCapture(*capture);
     RpcBuiltCore facts{rp::host::systemNodeType(sys->kind()), 0, 0};
     if (auto* nes = dynamic_cast<MesenNesSystem*>(sys.get())) facts.exportMode = nes->channelExportMode();
     if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) facts.linkGroup = sb->config_.linkGroupId;
@@ -178,47 +163,6 @@ bool EngineRpcService::setParam(std::string nodeId, std::string paramId, double
 // SRAM and cold-boots the ROM into the same slot; remove publishes a document without it. Native has no
 // bespoke method for any of them.
 
-bool EngineRpcService::applySystemSetting(std::uint32_t id, std::string key, double value) {
-    // A universal per-system setting → the live core, pushed through the invoker (flushed inline when
-    // quiescent, applied on the audio thread while it runs). The store gates existence on its own list.
-    if (key == "gainDb")
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Gain), value);
-    else if (key == "reloadOnRomChange")
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ReloadOnRomChange), value);
-    else
-        return false;  // unknown key
-    return true;
-}
-
-bool EngineRpcService::applyRoleConfig(std::uint32_t id, std::string kind, std::string config) {
-    // A backend "system" role carries live emulator config, keyed by core ("sameboy" | "mesen"). The
-    // store re-sends the WHOLE role config, so each field applies guarded (Engine no-ops an unchanged
-    // one — no spurious model restart / region reset when only a sibling knob moved).
-    if (kind == "sameboy") {
-        const SameBoyRoleConfig c = SameBoyBackend::decodeSameBoyRoleConfig(config);
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Model), static_cast<double>(c.model));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::Highpass), static_cast<double>(c.highpass));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::LinkGroup), static_cast<double>(c.linkGroupId));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::FastBoot), c.fastBoot ? 1.0 : 0.0);
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::ColorCorrection), static_cast<double>(c.colorCorrection));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::DmgPalette), static_cast<double>(c.dmgPalette));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::LightTemperature), c.lightTemperature);
-        return true;
-    }
-    if (kind == "mesen") {
-        // Attaches to any Mesen system; a GBA system casts to null in Engine::applyConfigField → no-op.
-        const MesenNesRoleConfig c = MesenBackend::decodeMesenNesRoleConfig(config);
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRegion), static_cast<double>(c.region));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesRemoveSpriteLimit), c.removeSpriteLimit ? 1.0 : 0.0);
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesApuLatencyMs), c.apuLatencyMs);
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesS5bNoise), static_cast<double>(c.s5bNoise));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesMmc5PhaseReset), static_cast<double>(c.mmc5PhaseReset));
-        invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::NesExpansionVolume), static_cast<double>(c.expansionVolume));
-        return true;
-    }
-    return false;
-}
-
 // Reads of the owned snapshot registry — the control plane reads published copies by id, never the
 // live core, so they're safe while the audio thread runs (no audioRunning_ guard, no findSystem walk).
 std::optional<rfl::Bytestring> EngineRpcService::readState(std::uint32_t id) {
@@ -499,15 +443,6 @@ bool EngineRpcService::stageControllerIn(std::vector<std::uint8_t> bytes) {
     return invoker_.stageControllerMidi(std::move(bytes));  // a pad press is 3 bytes; the ring carries 4 inline
 }
 
-bool EngineRpcService::setSerialOutCapture(std::uint32_t id, std::optional<std::string> mode) {
-    // Control-plane, through the invoker's config path (SameBoy-only in Engine::applyConfigField; a no-op on a
-    // Mesen/GBA system). The store gates existence on its own list, so a known mode is accepted optimistically.
-    const std::optional<SerialOutCapture> capture = serialOutCaptureMode(mode);
-    if (!capture) return false;
-    invoker_.applyConfigField(id, static_cast<std::uint8_t>(ConfigField::SerialOutCapture), static_cast<double>(*capture));
-    return true;
-}
-
 void EngineRpcService::accumulateMidiOut() {
     // Engine::midiOut() holds only the block just rendered (cleared at the top of the next processBlock),
     // so copy it out now. Usually empty (only an armed LSDj MI.OUT system emits) — cheap when so.
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index c3f416c8..b267e30f 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -40,8 +40,6 @@ public:
     // system node's gain or a NES's expansionVolume, as the store sends an edit of either, with no publish. False for a
     // node or a param that plan does not have, a NaN, or a full command ring.
     bool setParam(std::string nodeId, std::string paramId, double value);
-    bool applySystemSetting(std::uint32_t id, std::string key, double value);
-    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
     std::optional<rfl::Bytestring> readState(std::uint32_t id);
     std::optional<rfl::Bytestring> readSram(std::uint32_t id);
     std::optional<rfl::Bytestring> readRam(std::uint32_t id);   // work RAM (WRAM), per-block published copy
@@ -93,10 +91,6 @@ public:
     // The control-surface twin: staged as `controllerIn` rather than musical MIDI, so a headless test
     // can drive a Launchpad app without a Launchpad. Harness-facet only, like stageMidiIn.
     bool            stageControllerIn(std::vector<std::uint8_t> bytes);
-    // Set a SameBoy's serial-out capture: "drive" (LSDj MI.OUT), "listen" (master sync), or absent for off;
-    // false for any other string. Control-plane, via the invoker's config path (ConfigField::SerialOutCapture):
-    // the TS store calls it when a system's lsdj-sync mode changes.
-    bool            setSerialOutCapture(std::uint32_t id, std::optional<std::string> mode);
     // Drain the host MIDI out (LSDj's serial-out decoders, a controller's launches), accumulated across renderAudio
     // blocks since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
     std::vector<RpcMidiOut> drainMidiOut();
diff --git a/packages/native/src/system/SystemBase.hpp b/packages/native/src/system/SystemBase.hpp
index c35f466e..2a42b497 100644
--- a/packages/native/src/system/SystemBase.hpp
+++ b/packages/native/src/system/SystemBase.hpp
@@ -174,12 +174,6 @@ public:
     virtual std::optional<bool> fastBoot() const { return std::nullopt; }
     virtual void                setFastBoot(bool /*on*/) {}
 
-    // "Reload when the ROM file changes on disk" — a UI-thread flag, not a core one.
-    // The control plane watches the path (HostRpcService::drainChangedPaths, over
-    // NativeFileWatcher) and reloads; nothing on the audio thread reads this.
-    virtual bool wantsRomReload() const  { return false; }
-    virtual void setRomReload(bool /*on*/) {}
-
     // Live output gain in dB — a universal per-system setting every backend
     // honours (smoothed at the audio frame). Default no-op so trivial backends /
     // test doubles need not implement it; SameBoy + both Mesen systems override.
@@ -379,7 +373,7 @@ protected:
     // Arms an immediate republish, which refreshes both together within one block instead of up to
     // kStateSnapshotIntervalSec later. Allocates NOTHING: the triple was sized to a ceiling covering
     // every model the backend can switch into, which is exactly what makes this callable from the audio
-    // thread (where Engine::applyConfigField runs while the audio thread owns the Engine).
+    // thread (where a system node applies its settings while the audio thread owns the Engine).
     void rearmStateSnapshot();
 
     // State-snapshot backend hooks (default: unsupported). Overridden by
diff --git a/packages/native/src/system/mesen/MesenBackend.hpp b/packages/native/src/system/mesen/MesenBackend.hpp
index 00e851b2..97c65394 100644
--- a/packages/native/src/system/mesen/MesenBackend.hpp
+++ b/packages/native/src/system/mesen/MesenBackend.hpp
@@ -8,9 +8,10 @@
 
 class SystemBase;
 
-// The TS-owned "mesen" system-role config as it crosses the wire (JSON): the NES emulator knobs the UI
-// edits. Field names match the TS role schema (coreRoles.ts). Decoded both live (applyRoleConfig) and
-// at construct (the settings blob). Defaults mirror MesenNesConfig, so a missing field is a no-op.
+// The TS-owned "mesen" system-role config as it crosses the wire at construct (JSON, the settings blob):
+// the NES emulator knobs the UI edits. Field names match the TS role schema (coreRoles.ts). A live edit
+// reaches the core as its system node's settings and params instead (host/graph/SystemNodes.cpp).
+// Defaults mirror MesenNesConfig, so a missing field is a no-op.
 // (The role attaches to any Mesen system; GBA ignores these NES-only fields.)
 struct MesenNesRoleConfig {
     std::uint32_t region            = 0;      // ConsoleRegion (Auto/Ntsc/Pal/Dendy/NtscJapan)
@@ -64,7 +65,7 @@ public:
                                       double sampleRate) override;
 
     // Parse the TS "mesen" role-config JSON (forward-tolerant: an absent field takes its default).
-    // The single place that JSON is decoded — shared by live applyRoleConfig + the NES construct blob.
+    // The single place that JSON is decoded: the NES construct blob.
     static MesenNesRoleConfig decodeMesenNesRoleConfig(const std::string& json);
 
     // The SMS/GG twin of the above, over the same JSON blob.
diff --git a/packages/native/src/system/mesen/MesenGbaConfig.hpp b/packages/native/src/system/mesen/MesenGbaConfig.hpp
index d1ae607c..fcb207d7 100644
--- a/packages/native/src/system/mesen/MesenGbaConfig.hpp
+++ b/packages/native/src/system/mesen/MesenGbaConfig.hpp
@@ -21,9 +21,6 @@ struct MesenGbaConfig {
 
     bool          embedRom        = true;
     bool          skipBootScreen  = true;
-    // Watch `romPath` on disk; the UI thread reloads the system when the
-    // file's mtime advances. No-op when romPath is empty.
-    bool          reloadOnRomChange = false;
     float         gainDb          = 0.0f;
     std::string   romPath;
     // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
diff --git a/packages/native/src/system/mesen/MesenGbaSystem.hpp b/packages/native/src/system/mesen/MesenGbaSystem.hpp
index b865817d..cc5f9128 100644
--- a/packages/native/src/system/mesen/MesenGbaSystem.hpp
+++ b/packages/native/src/system/mesen/MesenGbaSystem.hpp
@@ -84,8 +84,6 @@ public:
     void                      setSavPath(const std::string& p) override { config_.savPath = p; }
     std::optional<bool>       fastBoot() const override       { return config_.skipBootScreen; }
     void                      setFastBoot(bool on) override;
-    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
-    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
     std::vector<std::uint8_t> saveSramBytes() const override;
     void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
diff --git a/packages/native/src/system/mesen/MesenNesConfig.hpp b/packages/native/src/system/mesen/MesenNesConfig.hpp
index d76c06fd..319fd9c9 100644
--- a/packages/native/src/system/mesen/MesenNesConfig.hpp
+++ b/packages/native/src/system/mesen/MesenNesConfig.hpp
@@ -32,9 +32,6 @@ struct MesenNesConfig {
     using Tag = rfl::Literal<"nes">;
 
     bool          embedRom = true;
-    // Watch `romPath` on disk; the UI thread reloads the system when the
-    // file's mtime advances. No-op when romPath is empty.
-    bool          reloadOnRomChange = false;
     float         gainDb   = 0.0f;
     // TS-owned "mesen" system-role knobs (coreRoles.ts). region = ConsoleRegion
     // (0=Auto,1=Ntsc,2=Pal,3=Dendy,4=NtscJapan) — applied at construct, live edit
diff --git a/packages/native/src/system/mesen/MesenNesSystem.cpp b/packages/native/src/system/mesen/MesenNesSystem.cpp
index 2c75f561..5931a3c9 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.cpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.cpp
@@ -356,7 +356,7 @@ void MesenNesSystem::setApuLatencyMs(double ms) {
     if (config_.apuLatencyMs == ms) return;
     config_.apuLatencyMs = ms;
     // Live: the mixer converts ms→cycles against the region clock and re-thresholds the flush window.
-    // No reset — just a scalar change, safe on the audio thread (arrives via Engine::applyConfigField).
+    // No reset, just a scalar change: safe on the audio thread, where the NES's system node applies it.
     if (nesMixer_) nesMixer_->SetLatencyMs(ms);
 }
 
diff --git a/packages/native/src/system/mesen/MesenNesSystem.hpp b/packages/native/src/system/mesen/MesenNesSystem.hpp
index 7a45ca72..d2236d2a 100644
--- a/packages/native/src/system/mesen/MesenNesSystem.hpp
+++ b/packages/native/src/system/mesen/MesenNesSystem.hpp
@@ -99,8 +99,6 @@ public:
     void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
     const std::string&        savPath() const override        { return config_.savPath; }
     void                      setSavPath(const std::string& p) override { config_.savPath = p; }
-    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
-    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
     std::vector<std::uint8_t> saveSramBytes() const override;
     void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
@@ -110,8 +108,8 @@ public:
 
     void setGainDb(float dB) override;
 
-    // "mesen" system-role knobs (coreRoles.ts), applied live by the NES's system node (its settings) and by
-    // Engine::applyConfigField. Each but setRemoveSpriteLimit is value-guarded, and that one is a plain flag write.
+    // "mesen" system-role knobs (coreRoles.ts), applied live by the NES's system node (its settings, and its
+    // expansionVolume param). Each but setRemoveSpriteLimit is value-guarded, and that one is a plain flag write.
     // removeSpriteLimit is a live PPU toggle; region reconfigures timing so it forces a reset.
     void setRemoveSpriteLimit(bool on);
     void setRegion(std::uint32_t region);
@@ -129,7 +127,7 @@ public:
     // 3 = 5 individual mono); channelLayout() follows it. Value-guarded, and pre-onActivate it only records
     // the mode (onActivate arms from config_). NOT cheap - SetChannelCapture allocates the per-stream blip
     // buffers + resamplers - but it is a user menu action, not a per-block cost, and it runs on the same
-    // audio thread that already absorbs SameBoySystem::restartEmulator from applyConfigField.
+    // audio thread that already absorbs SameBoySystem::restartEmulator from a Game Boy node's model setting.
     void setChannelExportMode(std::uint32_t mode);
     /** The armed per-channel export mode (0 = off). Lets the Engine record what it found before arming,
      *  so it can restore a construct-time (CLI / render) value it did not set. */
diff --git a/packages/native/src/system/mesen/MesenSmsConfig.hpp b/packages/native/src/system/mesen/MesenSmsConfig.hpp
index 29620dbf..638be9a3 100644
--- a/packages/native/src/system/mesen/MesenSmsConfig.hpp
+++ b/packages/native/src/system/mesen/MesenSmsConfig.hpp
@@ -54,9 +54,6 @@ struct MesenSmsConfig {
     bool          enableFm        = true;
 
     bool          embedRom        = true;
-    // Watch `romPath` on disk; the UI thread reloads the system when the
-    // file's mtime advances. No-op when romPath is empty.
-    bool          reloadOnRomChange = false;
     float         gainDb          = 0.0f;
     std::string   romPath;
     // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
diff --git a/packages/native/src/system/mesen/MesenSmsSystem.hpp b/packages/native/src/system/mesen/MesenSmsSystem.hpp
index bfcc8d1e..3ff88bbd 100644
--- a/packages/native/src/system/mesen/MesenSmsSystem.hpp
+++ b/packages/native/src/system/mesen/MesenSmsSystem.hpp
@@ -131,8 +131,6 @@ public:
     void                      setSavSuffix(std::uint32_t s) override { config_.savSuffix = s; }
     const std::string&        savPath() const override        { return config_.savPath; }
     void                      setSavPath(const std::string& p) override { config_.savPath = p; }
-    bool                      wantsRomReload() const override { return config_.reloadOnRomChange; }
-    void                      setRomReload(bool on) override  { config_.reloadOnRomChange = on; }
     std::vector<std::uint8_t> saveSramBytes() const override;
     void                      clearSram() override;
     std::vector<std::uint8_t> saveStateBytes() const override;
diff --git a/packages/native/src/system/sameboy/SameBoyBackend.hpp b/packages/native/src/system/sameboy/SameBoyBackend.hpp
index 0857fd92..52f36b92 100644
--- a/packages/native/src/system/sameboy/SameBoyBackend.hpp
+++ b/packages/native/src/system/sameboy/SameBoyBackend.hpp
@@ -11,10 +11,10 @@
 
 class SameBoySystem;
 
-// The TS-owned "sameboy" system-role config as it crosses the wire (JSON): the emulator knobs the UI
-// edits. Field names match the TS role schema (coreRoles.ts). Decoded both live (applyRoleConfig)
-// and at construct (the settings blob). Defaults mirror the struct + TS defaults, so a missing field
-// is a no-op.
+// The TS-owned "sameboy" system-role config as it crosses the wire at construct (JSON, the settings
+// blob): the emulator knobs the UI edits. Field names match the TS role schema (coreRoles.ts). A live
+// edit reaches the core as its system node's settings instead (host/graph/SystemNodes.cpp). Defaults
+// mirror the struct + TS defaults, so a missing field is a no-op.
 struct SameBoyRoleConfig {
     std::uint32_t model       = static_cast<std::uint32_t>(SameBoyModel::CgbC);
     std::uint32_t highpass    = static_cast<std::uint32_t>(SameBoyHighpass::Accurate);
@@ -44,6 +44,6 @@ public:
                                                        double sampleRate);
 
     // Parse the TS "sameboy" role-config JSON (forward-tolerant: an absent field takes its default).
-    // The single place that JSON is decoded — shared by live applyRoleConfig + the construct blob.
+    // The single place that JSON is decoded: the construct blob.
     static SameBoyRoleConfig decodeSameBoyRoleConfig(const std::string& json);
 };
diff --git a/packages/native/src/system/sameboy/SameBoyConfig.hpp b/packages/native/src/system/sameboy/SameBoyConfig.hpp
index 0a79a248..b967486e 100644
--- a/packages/native/src/system/sameboy/SameBoyConfig.hpp
+++ b/packages/native/src/system/sameboy/SameBoyConfig.hpp
@@ -75,10 +75,6 @@ struct SameBoyConfig {
     SameBoyColorCorrection    colorCorrection = SameBoyColorCorrection::Disabled;
     SameBoyDmgPalette         dmgPalette      = SameBoyDmgPalette::Grey;
     double                    lightTemperature = 0.0;  // -1 cool .. +1 warm, CGB only
-    // When true, the UI thread watches `romPath` and triggers a reload
-    // (preserving current SRAM, dropping savestate) when the file changes.
-    // No-op when `romPath` is empty (embed-only project).
-    bool                      reloadOnRomChange = false;
     // When true (default), saves embed `romBytes` so projects survive ROM
     // file moves. When false, only `romPath` is persisted and the ROM is
     // re-read from disk on load.
diff --git a/packages/native/src/system/sameboy/SameBoySystem.cpp b/packages/native/src/system/sameboy/SameBoySystem.cpp
index b56ae7fc..6db3bf78 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.cpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.cpp
@@ -284,7 +284,7 @@ void SameBoySystem::onActivate(double sampleRate) {
     // Reset the serial shift-register state so a re-activation (e.g. a
     // model-change restart, which routes through onDeactivate→onActivate)
     // doesn't carry over half-shifted bytes. Serial-out capture's mode is set
-    // separately by the host via setSerialOutCapture.
+    // separately, by the system node, via setSerialOutCapture.
     serialIn_.clear();
     serialBitsRemaining_ = 0;
     serialOutByte_ = 0;
diff --git a/packages/native/src/system/sameboy/SameBoySystem.hpp b/packages/native/src/system/sameboy/SameBoySystem.hpp
index 7b306d1f..24b7c8ff 100644
--- a/packages/native/src/system/sameboy/SameBoySystem.hpp
+++ b/packages/native/src/system/sameboy/SameBoySystem.hpp
@@ -122,11 +122,11 @@ public:
     // next rendered frame, so no restart. Called from `onActivate` and from the display setters below.
     void applyDisplayConfig();
 
-    // The live "sameboy" role knobs, as the Game Boy's system node and Engine::applyConfigField apply them. Each is
-    // value-guarded, a same-value call doing nothing, since a caller may hand over a whole config when one knob
-    // moved: a model change restarts the core (restartEmulator), highpass re-applies the filter mode, and the three
-    // display knobs re-apply the display group, none of those restarting anything. setFastBoot and
-    // setSerialOutCapture are the other two.
+    // The live "sameboy" role knobs, as the Game Boy's system node applies its settings. Each is value-guarded, a
+    // same-value call doing nothing, since the node applies every knob its settings carry when it takes the core: a
+    // model change restarts the core (restartEmulator), highpass re-applies the filter mode, and the three display
+    // knobs re-apply the display group, none of those restarting anything. setFastBoot and setSerialOutCapture are
+    // the other two.
     void setModel(SameBoyModel model);
     void setHighpass(SameBoyHighpass highpass);
     void setColorCorrection(SameBoyColorCorrection mode);
@@ -141,8 +141,6 @@ public:
     void                      setSavPath(const std::string& p) override { config_.savPath = p; }
     std::optional<bool>       fastBoot() const override         { return config_.fastBoot; }
     void                      setFastBoot(bool on) override     { config_.fastBoot = on; }
-    bool                      wantsRomReload() const override   { return config_.reloadOnRomChange; }
-    void                      setRomReload(bool on) override    { config_.reloadOnRomChange = on; }
     void                      clearSram() override;
     std::vector<std::uint8_t> saveSramBytes() const override;
     std::vector<std::uint8_t> saveStateBytes() const override;
@@ -168,14 +166,14 @@ public:
     // callback reads this to decide whether to accumulate LSDJ's outgoing bytes.
     bool serialOutCaptureEnabled() const { return serialOutCapture_ != SerialOutCapture::Off; }
     SerialOutCapture serialOutCapture() const { return serialOutCapture_; }
-    // Set serial-out capture's mode. The host drives it through this (the TS
-    // store picks it from a system's lsdj-sync mode: Drive for MI.OUT, Listen for
-    // master sync). Stays set across a model-change restart (onActivate no
-    // longer clears it). A switch drops the half-assembled byte: Drive clocks
-    // continuously, so a switch lands mid-byte 7 times in 8, and the old mode's
-    // bits would leave every later byte straddling two transfers. A same-mode
-    // call keeps them, since the store re-sends the mode on every lsdj-sync
-    // config edit and MI.OUT's partial byte must survive that.
+    // Set serial-out capture's mode. The Game Boy's system node drives it
+    // through this, from its serialOutCapture setting (TS picks it from the mode
+    // the system's lsdj-sync node runs: Drive for MI.OUT, Listen for master
+    // sync). Stays set across a model-change restart (onActivate leaves it). A
+    // switch drops the half-assembled byte: Drive clocks continuously, so a
+    // switch lands mid-byte 7 times in 8, and the old mode's bits would leave
+    // every later byte straddling two transfers. A same-mode call keeps them:
+    // MI.OUT's partial byte must survive a call that changes nothing.
     void setSerialOutCapture(SerialOutCapture mode) {
         if (mode != serialOutCapture_) {
             serialOutByte_ = 0;
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 5cc84949..32a80f07 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -1,11 +1,10 @@
 // The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5): a built core parks
 // under its id until a document's system node takes it, a document's system nodes and its slots must name the
 // same systems, the tap re-arm rides the swap, a refused document leaves its cores parked, and a core no
-// published document placed is freed; a build spec reaches the core under the field names TS sends; a system node's
-// settings carry its core's knobs beside the core (docs/modular-graph-phase-c2-design.md section 4); and
-// Engine::setParam reaches a param of the running plan at the next block, a system node's gain and a NES's expansion
-// volume among them, over the RPC as TS calls it. Driven on a quiescent Engine, so every plan applies as it is
-// published, except where a test owns the drain itself.
+// published document placed is freed; a system node's settings carry its core's knobs beside the core
+// (docs/modular-graph-phase-c2-design.md section 4); and Engine::setParam reaches a param of the running plan at the
+// next block, a system node's gain and a NES's expansion volume among them, over the RPC as TS calls it. Driven on a
+// quiescent Engine, so every plan applies as it is published, except where a test owns the drain itself.
 //
 // Run via `pnpm test:plugin engine-graph`.
 
@@ -339,86 +338,6 @@ TEST_CASE("buildCore and setGraph over the RPC layer", "[engine-graph][b2]") {
     CHECK(e.findSystem(5) != nullptr);
 }
 
-TEST_CASE("a build spec's serialOutCapture crosses the wire under the name TS sends", "[engine-graph][b2]") {
-    // A loaded LSDj in a MIDI-out mode captures serial out only through this field, and reflect-cpp skips a
-    // field it does not know, so a name that drifted between realBackend.ts's specParams and
-    // BackendConstructSpec would drop it with nothing failing. Each mode arrives as itself, and a name that is no
-    // mode refuses the build rather than building a core that captures nothing. The spec is decoded as every host decodes it: a
-    // live JS object, through rpcpp's QuickJS codec, into the registered buildCore.
-    struct Js {
-        JSRuntime* rt  = JS_NewRuntime();
-        JSContext* ctx = JS_NewContext(rt);
-        ~Js() {
-            JS_FreeContext(ctx);
-            JS_FreeRuntime(rt);
-        }
-    } js;  // first, so it outlives the server holding its context
-
-    Engine           e(kRate);
-    SystemFactory    factory;
-    registerCoreBackends(factory);
-    QueuedInvoker    invoker(e);
-    EngineRpcService rpc(e, factory, invoker);
-    rpcpp::QuickJSTransport                                   transport(js.ctx, [](JSContext*, JSValue) {});
-    rpcpp::TypedRpcServer<rpcpp::Empty, rpcpp::QuickJSCodec> server(transport, rpcpp::QuickJSCodec{js.ctx});
-    registerEmulatorRpc(server, rpc);
-
-    // The call specParams makes for an embedded mGB, `extra` appended to the spec. True when it built.
-    const auto buildCore = [&](SystemId id, const std::string& extra) {
-        const std::string src = "({jsonrpc: '2.0', id: 1, method: 'buildCore', params: [{id: " + std::to_string(id) +
-                                ", romPath: '', platform: 'gb', core: 'sameboy', embeddedRom: 'mgb'" + extra + "}]})";
-        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<buildCore>", JS_EVAL_TYPE_GLOBAL);
-        std::optional<rpcpp::QuickJSOut> reply;
-        if (!JS_IsException(message)) reply = server.processMessage(message);
-        JS_FreeValue(js.ctx, message);
-        if (!reply) return false;
-        JSValue answer = reply->materialize(js.ctx);
-        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
-        const bool built = JS_IsObject(result);
-        JS_FreeValue(js.ctx, result);
-        JS_FreeValue(js.ctx, answer);
-        return built;
-    };
-    REQUIRE(buildCore(1, ", serialOutCapture: 'drive'"));
-    REQUIRE(buildCore(2, ", serialOutCapture: 'listen'"));
-    REQUIRE(buildCore(3, ""));
-    CHECK_FALSE(buildCore(4, ", serialOutCapture: 'loud'"));
-    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}, {3, kGb}})).ok);
-    const auto mode = [&](SystemId id) {
-        const auto* sb = dynamic_cast<const SameBoySystem*>(e.findSystem(id));
-        REQUIRE(sb != nullptr);
-        return sb->serialOutCapture();
-    };
-    CHECK(mode(1) == SerialOutCapture::Drive);
-    CHECK(mode(2) == SerialOutCapture::Listen);
-    CHECK(mode(3) == SerialOutCapture::Off);
-    CHECK_FALSE(e.coreLive(4));
-
-    // The live call takes the same names, and null for off; a name that is no mode is refused and changes nothing.
-    const auto setCapture = [&](SystemId id, const std::string& name) {
-        const std::string src = "({jsonrpc: '2.0', id: 2, method: 'setSerialOutCapture', params: [" + std::to_string(id) + ", " +
-                                name + "]})";
-        JSValue message = JS_Eval(js.ctx, src.c_str(), src.size(), "<setSerialOutCapture>", JS_EVAL_TYPE_GLOBAL);
-        std::optional<rpcpp::QuickJSOut> reply;
-        if (!JS_IsException(message)) reply = server.processMessage(message);
-        JS_FreeValue(js.ctx, message);
-        if (!reply) return false;
-        JSValue answer = reply->materialize(js.ctx);
-        JSValue result = JS_GetPropertyStr(js.ctx, answer, "result");
-        const bool accepted = JS_IsBool(result) && JS_ToBool(js.ctx, result);
-        JS_FreeValue(js.ctx, result);
-        JS_FreeValue(js.ctx, answer);
-        invoker.drainInto(e);
-        return accepted;
-    };
-    CHECK(setCapture(3, "'listen'"));
-    CHECK(mode(3) == SerialOutCapture::Listen);
-    CHECK_FALSE(setCapture(3, "'loud'"));
-    CHECK(mode(3) == SerialOutCapture::Listen);
-    CHECK(setCapture(1, "null"));
-    CHECK(mode(1) == SerialOutCapture::Off);
-}
-
 TEST_CASE("a render into buffers the planned graph does not write is refused", "[engine-graph][b2]") {
     // TS sets the shape a render call needs before it makes the call; the render calls only check it.
     Engine           e(kRate);
diff --git a/packages/native/test/engine-graph/SystemNode.test.cpp b/packages/native/test/engine-graph/SystemNode.test.cpp
index e747bca9..03185cad 100644
--- a/packages/native/test/engine-graph/SystemNode.test.cpp
+++ b/packages/native/test/engine-graph/SystemNode.test.cpp
@@ -1174,7 +1174,7 @@ TEST_CASE("a system node gives its core every knob its settings carry as it take
 TEST_CASE("a knob absent from a system node's settings leaves its core as it was built, or as the last document left it",
           "[engine-graph][system-node][settings]") {
     SECTION("Game Boy") {
-        // Every knob built away from SameBoyConfig's defaults, and the capture armed, as buildCore arms it.
+        // Every knob built away from SameBoyConfig's defaults, and the capture armed on the core directly.
         const auto build = [] {
             SameBoyConfig cfg;
             cfg.model = SameBoyModel::Mgb;
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 6f5265b7..2d23999b 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -149,8 +149,7 @@ export interface Backend {
   // TS owns the config. A system's knobs ride the graph document (graph/buildGraph.ts): its core config's setting
   // fields and its LSDj capture as its node's settings, which an edit publishes, and its gain and a NES's expansion
   // volume as its node's params, whose live edits go through setParam with no publish
-  // (docs/modular-graph-phase-c2-design.md section 5). The three calls after it are the side path the document
-  // replaced: nothing in the app makes them (native tests and a CLI session do).
+  // (docs/modular-graph-phase-c2-design.md section 5).
 
   /** A live change to a param of a node the running plan places, applied at the start of the next block, clamped to
    *  the param's range (and rounded for an int, enum or bool). The store sets a system's `gain` (dB) and a NES's
@@ -159,21 +158,6 @@ export interface Backend {
    *  value from that document), a NaN, a param a wire drives, or a full command ring. */
   setParam(nodeId: string, paramId: string, value: number): boolean;
 
-  /** Apply a universal per-system setting (`"gainDb"` / `"reloadOnRomChange"`) to the
-   *  live emulator. Returns false on failure. */
-  applySystemSetting(id: number, key: string, value: number | boolean): boolean;
-
-  /** Apply a SYSTEM-role's (backend) config to the live emulator — native dispatches
-   *  by kind (e.g. `"sameboy"`: model → restart, highpass → live). Returns false on
-   *  failure. */
-  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean;
-
-  /** Set capture of a system's outgoing serial bytes (SerialOutCapture), or null for off: armed, the Game Boy node's
-   *  `serialOut` port carries the raw bytes, a wire into its rp.lsdj.serial-out node, for the MI.OUT and master-sync
-   *  decoders. A Game Boy node's `serialOutCapture` setting does the same from the document. False for a mode native
-   *  does not know. SameBoy-only (a no-op on other cores). */
-  setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean;
-
   // --- Live emulator input ------------------------------------------------
   // The UI→DSP direction for game input: a joypad button transition on the focused system. Native queues
   // it to the core's audio-thread button sink, so it applies live while the instance plays.
@@ -414,10 +398,10 @@ export type HostBackend = Pick<
   | "zip" | "unzip" | "pngEncode" | "pngDecode" | "savFromJson" | "openFileBrowser"
 >;
 
-/** Emulator lifecycle / live config / input / snapshot reads — bound by the plugin, UI, CLI, test host. */
+/** Emulator lifecycle / live params / input / snapshot reads: bound by the plugin, UI, CLI, test host. */
 export type EmulatorBackend = Pick<
   Backend,
-  | "buildCore" | "setGraph" | "loadScriptPack" | "setParam" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
+  | "buildCore" | "setGraph" | "loadScriptPack" | "setParam"
   | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
 >;
 
@@ -798,12 +782,6 @@ export interface FileBrowserOpts {
   directory?: boolean;
 }
 
-/** What a Game Boy's serial-out capture does. `listen`: capture the bytes it clocks out itself (LSDj as master sync's
- *  master). `drive`: that, and clock a transfer waiting on an external clock, capturing its bytes too (the
- *  Arduinoboy's part in LSDj MI.OUT). Only MI.OUT may drive: a stopped LSDj master, clocked, takes the idle bytes for
- *  another master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md). */
-export type SerialOutCapture = "listen" | "drive";
-
 /** What TS hands the native builder: concrete paths only — everything is resolved
  *  TS-side before the call. `savPath`/`statePath` are the exact files to load-from +
  *  auto-save-to / boot-from, or `null` for fresh / cold. */
@@ -836,10 +814,6 @@ export interface ConstructSpec {
   /** Seed savestate bytes (a zip-import blob or a duplicate's captured state). When set, native
    *  boots from these instead of reading `statePath`. Rides as a Uint8Array (rfl::Bytestring). */
   stateBytes?: Uint8Array;
-  /** Arm a Game Boy's serial-out capture before the core first runs, as setSerialOutCapture would on a running
-   *  one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. Its node's settings carry a
-   *  capture too, which its activation applies before the core first runs, so the document's wins. */
-  serialOutCapture?: SerialOutCapture;
   /** The system's backend ("system"-role) config as JSON, so a loaded non-default setting
    *  (SameBoy model/highpass/…) is applied at CONSTRUCT rather than via a post-build restart
    *  that would nuke the just-restored savestate. Omitted for a fresh build (backend defaults
diff --git a/packages/retroplug/src/graph/controller.ts b/packages/retroplug/src/graph/controller.ts
index 985d3595..0ca58995 100644
--- a/packages/retroplug/src/graph/controller.ts
+++ b/packages/retroplug/src/graph/controller.ts
@@ -73,7 +73,11 @@ export function controllerSyncOverride(
 }
 
 /** What a Game Boy captures of its serial out for LSDj's decoder (its node's `serialOutCapture` setting, as the C++
- *  node spells it): `listen` for master sync, `drive` for MI.OUT, `off` for no capture. */
+ *  node spells it), which the node gives the core when it takes it, before the core first runs, and at the start of
+ *  the first block of a plan that changes it. `listen`: capture the bytes it clocks out itself (LSDj as master sync's
+ *  master). `drive`: that, and clock a transfer waiting on an external clock, capturing its bytes too (the
+ *  Arduinoboy's part in LSDj MI.OUT). Only MI.OUT may drive: a stopped LSDj master, clocked, takes the idle bytes for
+ *  another master's clock and stops answering START (docs/lsdj-master-sync-fix-design.md). `off`: no capture. */
 export type SerialOutCaptureMode = "off" | "listen" | "drive";
 
 /** The serial-out capture of a Game Boy whose protocol nodes are `pipeline` (protocolPipeline's), for the mode its
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index 0c0f679a..12f0f4c3 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -70,7 +70,6 @@ const specParams = (spec: ConstructSpec, id: number): Record<string, unknown> =>
   if (spec.sramBytes) p.sramBytes = spec.sramBytes;
   if (spec.stateBytes) p.stateBytes = spec.stateBytes;
   if (spec.settings != null) p.settings = spec.settings;
-  if (spec.serialOutCapture) p.serialOutCapture = spec.serialOutCapture;
   return p;
 };
 
@@ -102,7 +101,7 @@ export function createHostClient(): HostBackend {
   };
 }
 
-/** The emulator lifecycle / live config / input / snapshot-reads facet. Throws if no RPC surface is bound. */
+/** The emulator lifecycle / live params / input / snapshot-reads facet. Throws if no RPC surface is bound. */
 export function createEmulatorClient(): EmulatorBackend {
   const call = makeCall();
   return {
@@ -110,10 +109,6 @@ export function createEmulatorClient(): EmulatorBackend {
     setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
     loadScriptPack: (request: LoadScriptPackRequest) => call("loadScriptPack", request) as LoadScriptPackResult,
     setParam: (nodeId, paramId, value) => call("setParam", nodeId, paramId, value) as boolean,
-    applySystemSetting: (id, key, value) =>
-      call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
-    applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
-    setSerialOutCapture: (id, mode) => call("setSerialOutCapture", id, mode) as boolean,
     pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
     stageSystemMidi: (id, bytes) => call("stageSystemMidi", id, bytes) as boolean,
     readState: (id) => bytesOrNull(call("readState", id)),
diff --git a/packages/retroplug/src/settingsEnums.ts b/packages/retroplug/src/settingsEnums.ts
index 6896212b..75d27b84 100644
--- a/packages/retroplug/src/settingsEnums.ts
+++ b/packages/retroplug/src/settingsEnums.ts
@@ -1,9 +1,10 @@
 // The single source of truth for every string-valued enum setting in a project. Each enum is an
 // index-ordered value tuple whose position EQUALS the native enum integer — the tuple feeds
 // `z.enum(...)` (configSchema.enumField), indexes the menu cyclers' display-name arrays, and drives
-// the string<->int conversion at the native boundary. TS carries the strings everywhere;
-// native (the reflect-cpp role structs) stays numeric, so the boundary encoder here
-// (`roleConfigForNative`) maps back to the ints native decodes.
+// the string<->int conversion at the construct boundary. TS carries the strings everywhere, a system
+// node's settings included (its C++ type reads the labels); the construct blob native decodes (the
+// reflect-cpp role structs) stays numeric, so the boundary encoder here (`roleConfigForNative`) maps
+// back to the ints it reads.
 //
 // A LEAF module: it imports nothing, so the schema, the domain modules (layout/midiRouting/dspRoles),
 // the stores, the menu, and the project migration can all share these values without an import cycle.
@@ -171,10 +172,11 @@ const ROLE_NATIVE_ENUMS: Record<string, Record<string, readonly string[]>> = {
   },
 };
 
-/** Encode a role config for native: replace each string enum field with its native integer (index),
- *  passing every other field (linkGroupId, fastBoot, removeSpriteLimit, …) through unchanged. A role
- *  kind that doesn't cross to native returns its config untouched. Native stays numeric; this is the
- *  one place TS strings become the ints reflect-cpp expects. */
+/** Encode a role config for the construct blob (ConstructSpec.settings): replace each string enum field with
+ *  its native integer (index), passing every other field (linkGroupId, fastBoot, removeSpriteLimit, …) through
+ *  unchanged. A role kind that doesn't cross to native returns its config untouched. The blob stays numeric;
+ *  this is the one place TS strings become the ints reflect-cpp expects. A live edit reaches the core as its
+ *  system node's settings and params instead, in the labels themselves (graph/buildGraph.ts systemKnobs). */
 export function roleConfigForNative(kind: string, config: Record<string, unknown>): Record<string, unknown> {
   const fields = ROLE_NATIVE_ENUMS[kind];
   if (!fields) return config;
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 9b07cab2..11be3cb1 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -12,7 +12,6 @@
 
 import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
 import { systemKnobs, type GraphSystem } from "./graph/buildGraph";
-import { serialOutCaptureFor } from "./graph/controller";
 import { canonicalJson } from "./graph/canonicalJson";
 import { EngineGraph } from "./engineGraph";
 import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, SEGA_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";
@@ -692,7 +691,7 @@ export class SystemsStore {
     // before the build, so a throw here leaves no core behind.
     const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
     const battery = this.detectBattery(romPath, embeddedRom, platform);
-    const built = this.backend.buildCore(spec, id); // a loaded system already in MIDIOUT mode is built capturing
+    const built = this.backend.buildCore(spec, id); // its LSDj capture, if any, comes with the document that places it
     if (!built) return null;
     for (const note of notes) console.warn(note);
     const wasEmpty = this.entries.length === 0;
@@ -972,11 +971,7 @@ export class SystemsStore {
         const rt = this.registry.roleType(r.kind);
         if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
       }
-    // A core is built with its serial-out capture already armed, the capture its node's settings carry anyway, from
-    // the mode of the lsdj-sync its pipeline places (graph/controller.ts serialOutCaptureFor): its activation applies
-    // the document's, the controller's MI.MAP override included, before the core first runs.
-    const capture = serialOutCaptureFor(protocolPipeline(roles));
-    return capture !== "off" ? { ...s, serialOutCapture: capture } : s;
+    return s;
   }
 
   // Write a system's live battery to its own `.sav` before its core is destroyed. Every teardown path
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index ca2e2228..08f8ffa3 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -4,7 +4,7 @@
 // "disk". This is what lets the whole application layer be tested with `tjs run`
 // and nothing else.
 
-import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, SerialOutCapture, TraceLine, ZipEntry } from "../src/backend";
+import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
 import { detectPlatform } from "../src/platform";
 import { savFromJson } from "../src/lsdj";
 import type { GraphDocument, NodeDoc } from "../src/graph/graphDocument";
@@ -94,10 +94,7 @@ export class MockBackend implements Backend {
 
   /** Every setParam call, in order: the live param edits the stores made (a gain, a NES's expansion volume). */
   readonly paramCalls: { nodeId: string; paramId: string; value: number }[] = [];
-  /** Live-config applies recorded for assertions. */
-  readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
-  readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];
-  readonly serialOutCaptureCalls: { id: number; mode: SerialOutCapture | null }[] = [];
+  /** Joypad edges (backend.pressButton), in call order. */
   readonly pressButtonCalls: { id: number; button: number; down: boolean }[] = [];
   /** Control MIDI aimed at one system (backend.stageSystemMidi), in call order. */
   readonly stageSystemMidiCalls: { id: number; bytes: number[] }[] = [];
@@ -632,24 +629,6 @@ export class MockBackend implements Backend {
     return !Number.isNaN(value) && this.graphNode(nodeId) !== undefined;
   }
 
-  applySystemSetting(id: number, key: string, value: number | boolean): boolean {
-    this.log.push("applySystemSetting");
-    this.applySettingCalls.push({ id, key, value });
-    return true;
-  }
-
-  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
-    this.log.push("applyRoleConfig");
-    this.applyRoleCalls.push({ id, kind, config });
-    return true;
-  }
-
-  setSerialOutCapture(id: number, mode: SerialOutCapture | null): boolean {
-    this.log.push("setSerialOutCapture");
-    this.serialOutCaptureCalls.push({ id, mode });
-    return true;
-  }
-
   pressButton(id: number, button: number, down: boolean): boolean {
     this.log.push("pressButton");
     this.pressButtonCalls.push({ id, button, down });
diff --git a/tools/check-doc-links.mjs b/tools/check-doc-links.mjs
index 504d5c80..1dc63149 100755
--- a/tools/check-doc-links.mjs
+++ b/tools/check-doc-links.mjs
@@ -116,7 +116,7 @@ for (const file of files) {
             const context = lines.slice(Math.max(0, i - 2), i + 1).join(" ");
             const ids = new Set();
             for (const t of `${text} ${context}`.matchAll(/`([A-Za-z_][A-Za-z0-9_:.>-]{3,})`/g)) {
-                // Take the last path/namespace segment: `Engine::applyConfigField` -> applyConfigField.
+                // Take the last path/namespace segment: `Engine::setParam` -> setParam.
                 const id = t[1].replace(/^.*[:.]/, "").replace(/[^A-Za-z0-9_]/g, "");
                 if (id.length >= 4) ids.add(id);
             }
````

- [ ] **Step 3: Verify**

Run: `pnpm typecheck && pnpm test`
Expected: `packages/retroplug typecheck: Done`, then `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)`, the graph binary's `All tests passed (1996 assertions in 184 test cases)` and the engine-graph binary's `All tests passed (25435 assertions in 174 test cases)` (the deleted case's one fewer).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, none skipped, nothing re-recorded.

Run: `pnpm test:native parity`
Expected: `# 30 native test file(s) passed (jobs=16)`, with `46 case(s) SKIPPED in 10 file(s)` (every fixture group's cases, as CI runs them) and the skip check clean.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/nonexistent-rp-resources RISA_SRC=/nonexistent-rp-risa pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `136 case(s) SKIPPED in 60 file(s)` and the skip check clean (`lsdj-capture-settings` skips its two cases without the fixtures, as the skip baseline now says).

Run: `pnpm test:node && pnpm test:cli-ts && pnpm check:docs`
Expected: `ℹ pass 12`, `PASS: retroplug-cli TypeScript harness`, `check-doc-links: 65 file(s) clean` (the prototype's 64, and this plan).

Run: `cmake --build build -j$(nproc)`
Expected: a clean build of every target (the plugin, SDL, the CLI), with only the existing warnings (`-Wstringop-overflow` in `MesenNesSystem::pressButton` and `-Wformat-truncation` in `PluginDSP.cpp`).

- [ ] **Step 4: Commit**

````bash
git add -- packages/retroplug/test-native/lsdj-capture-settings.test.ts \
  packages/native/src/host/engine/DspCommand.hpp packages/native/src/host/engine/Engine.cpp \
  packages/native/src/host/engine/Engine.hpp packages/native/src/host/engine/EngineInvoker.cpp \
  packages/native/src/host/engine/EngineInvoker.hpp packages/native/src/host/engine/SnapshotRegistry.hpp \
  packages/native/src/host/rpc/BackendRpcRegistration.hpp packages/native/src/host/rpc/BackendTypes.hpp \
  packages/native/src/host/rpc/EngineRpcService.cpp packages/native/src/host/rpc/EngineRpcService.hpp \
  packages/native/src/system/SystemBase.hpp packages/native/src/system/mesen/MesenBackend.hpp \
  packages/native/src/system/mesen/MesenGbaConfig.hpp packages/native/src/system/mesen/MesenGbaSystem.hpp \
  packages/native/src/system/mesen/MesenNesConfig.hpp packages/native/src/system/mesen/MesenNesSystem.cpp \
  packages/native/src/system/mesen/MesenNesSystem.hpp packages/native/src/system/mesen/MesenSmsConfig.hpp \
  packages/native/src/system/mesen/MesenSmsSystem.hpp packages/native/src/system/sameboy/SameBoyBackend.hpp \
  packages/native/src/system/sameboy/SameBoyConfig.hpp packages/native/src/system/sameboy/SameBoySystem.cpp \
  packages/native/src/system/sameboy/SameBoySystem.hpp packages/native/test/engine-graph/EngineGraph.test.cpp \
  packages/native/test/engine-graph/SystemNode.test.cpp packages/retroplug/cli/sessions/analyze-lsdj-sync.ts \
  packages/retroplug/scripts/skip-baseline.json packages/retroplug/src/backend.ts \
  packages/retroplug/src/graph/controller.ts packages/retroplug/src/graph/directGraph.ts \
  packages/retroplug/src/realBackend.ts packages/retroplug/src/settingsEnums.ts \
  packages/retroplug/src/systemsStore.ts packages/retroplug/test-native/app-mesen-settings.test.ts \
  packages/retroplug/test-native/app-settings.test.ts packages/retroplug/test-native/author-sms-rplg.ts \
  packages/retroplug/test-native/dsp-bench.test.ts packages/retroplug/test-native/lsdj-mastersync.test.ts \
  packages/retroplug/test-native/lsdj-midiout.test.ts packages/retroplug/test-native/lsdj-sync-negative.test.ts \
  packages/retroplug/test-native/lsdj-sync-pattern.test.ts packages/retroplug/test-native/lsdj-wram-seam.test.ts \
  packages/retroplug/test-native/parity/render-blocks.test.ts \
  packages/retroplug/test-native/sameboy-state-model-switch.test.ts \
  packages/retroplug/test/system-settings/store.test.ts packages/retroplug/test/systems/build-spec-wire.test.ts \
  packages/retroplug/test/systems/store-graph.test.ts packages/retroplug/testing/mockBackend.ts \
  tools/check-doc-links.mjs
git commit -F - <<'EOF'
refactor(engine): the side path for live knobs is gone: ConfigField, applyConfigField, the three RPCs and the build spec's capture; every caller sets its knob through the store or the document

Native: ConfigField, DspCommand::SetConfigField, QueuedInvoker::applyConfigField and Engine::applyConfigField with
its casts are deleted (the system nodes apply every field they applied, the link group aside, which only buildCore's
facts read, at build); so are the applySystemSetting, applyRoleConfig and setSerialOutCapture RPCs (the emulator facet
12 methods, the union 86), the build spec's serialOutCapture and buildCore's arming of it (a node arms the capture when
it takes its core, before the core first runs), and SystemBase::setRomReload / wantsRomReload with the configs'
reloadOnRomChange, which nothing native read. The Catch2 case for the build spec's capture on the wire goes with it.

TS: Backend loses the three methods and ConstructSpec.serialOutCapture (realBackend, the mock and the emulator facet
with them); the store no longer arms a capture at a build. DirectGraph loses applyRoleConfig and gains setCoreConfig
(a core config as the role spells it, labels and a Game Boy's link group, merged and published; a field its node does
not take, an ordinal among them, refused) and setParam (live, kept in its model).

Callers moved: the link-group callers (lsdj-sync-pattern, lsdj-sync-negative, the analyze-lsdj-sync session) and the
model switches (lsdj-wram-seam, sameboy-state-model-switch, by label) to setCoreConfig; dsp-bench, lsdj-mastersync,
lsdj-midiout and parity/render-blocks drop their direct capture call, the mode's capture riding the document its
setPipeline publishes. lsdj-midiout-build-flag becomes lsdj-capture-settings: an LSDj loaded in MI.OUT or master
sync is built with no capture, its node first placed with drive or listen, and its NoteOns or clocks reach the host.
EOF
````

### Task 5: the docs

Design section 7. The specs, the modular-graph design, the master-sync fix design's first limit, docs/lsdj.md and
AGENTS.md say what C2 built, and the C2 design lands with its Status line, its section 8 questions answered and a
section 10, "Found while building".

**Files:**
- Modify: `AGENTS.md`, `docs/lsdj-master-sync-fix-design.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-c2-design.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/04-roles-protocol-nodes.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/10-multichannel-audio-out.md`, `spec/11-ui-rendering.md`

- [ ] **Step 1: Update the docs**

spec/01 (the command ring's three kinds of edit, the system nodes' stateless prepare), spec/02 (a new subsection, "The
system nodes' settings and params"; the RPC table's `setParam` row and counts; the deleted command kind and
paragraphs), spec/03 (the Backend table, the stores' mutators, `DirectGraph`), spec/04 (which config reaches the live
core, the capture), spec/06, spec/07 (the capture and MI.MAP items closed, what C2 leaves), spec/10, spec/11,
docs/modular-graph-design.md (5.2's `rebuild_key`, 6.3, 6.5, 7.5, 8), the master-sync design's section 10,
docs/lsdj.md (the samples through `setCoreConfig`), AGENTS.md, and the C2 design:

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 87c218a1..de8350fc 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -37,8 +37,11 @@ The rules below are the parts that don't fit those.
   `-watcher-test` / `-dynparams-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` /
   `-graph-test` / `-engine-graph-test`) as `Catch2::Catch2WithMain`. `retroplug-engine-graph-test`
   holds the engine's graph to the cores it wraps: each system node renders what its core renders
-  driven directly, bit for bit, its inputs included; the engine places, re-arms and frees what the graph
-  documents TS builds say; the host I/O nodes and the N8 cart mirror move what the hosts stage and drain;
+  driven directly, bit for bit, its inputs, settings and params included (C2,
+  [docs/modular-graph-phase-c2-design.md](docs/modular-graph-phase-c2-design.md): each knob reaches its core at the
+  next block and every one at activation, an LSDj's capture switching in the block its decoder first runs in the new
+  mode); the engine places, re-arms and frees what the graph documents TS builds say; the host I/O nodes and the N8
+  cart mirror move what the hosts stage and drain;
   TS nodes run behind the C ABI in the script host (B3a,
   [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
   creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
@@ -58,8 +61,10 @@ The rules below are the parts that don't fit those.
   ([docs/lsdj-serial-out-design.md](docs/lsdj-serial-out-design.md)), and every case that records digests holds a
   second one over its system inputs alone, which the rig gave equal at the commit before the decoder moved, so the
   move changed no system input; and every document in `buildGraph`'s goldens, handed to the engine with real cores,
-  plans and runs with no node faulting, no script node throwing and no message dropped: the B1 translator's matrix a
-  block each (the 33-system `perSystem` ones are refused, as they must be), and `golden/build-graph-roles.json`,
+  plans and runs with no node faulting, no script node throwing, no message dropped and no system node's settings
+  naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing): the
+  B1 translator's matrix a block each (the 33-system `perSystem` ones are refused, as they must be), and
+  `golden/build-graph-roles.json`,
   `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing,
   twenty driven blocks each (the wiring itself is `buildGraph`'s, held by `pnpm test graph` to explicit tables, to the
   B1 translator's goldens and to the role goldens, which `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens`
diff --git a/docs/lsdj-master-sync-fix-design.md b/docs/lsdj-master-sync-fix-design.md
index 7c41da16..bd997e21 100644
--- a/docs/lsdj-master-sync-fix-design.md
+++ b/docs/lsdj-master-sync-fix-design.md
@@ -153,7 +153,8 @@ Each goes through review.
 
 - **A live switch from MI.OUT to master sync** can decode the one block captured under `drive` before the capture
   flips as clocks: master sync sends a NoteOn, 0xFA and clocks for it, then the real run's clocks, or 0xFC 200 ms on
-  if none follow. Accepted, since it needs a user's mode switch on an LSDj set to the other SYNC mode.
+  if none follow. Accepted, since it needs a user's mode switch on an LSDj set to the other SYNC mode. Retired by
+  [Phase C2](modular-graph-phase-c2-design.md): the capture rides the document that hands the decoder its mode.
 - **Only the fixture-only LSDj tests prove `listen` captures a self-clocked byte**
   ([lsdj-mastersync.test.ts](../packages/retroplug/test-native/lsdj-mastersync.test.ts) and the master-sync parity
   cases). CI checks the modes on the wire and that `listen` never clocks (mGB, section 7).
diff --git a/docs/lsdj.md b/docs/lsdj.md
index 715f5ef7..8f97a41a 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -60,8 +60,8 @@ const spec = { romPath: LSDJ, platform: "gb", core: "sameboy",
                embeddedRom: "", savPath: null, statePath: null, sramBytes: sav } as const;
 graph.add(spec, 1);                                      // the leader
 graph.add(spec, 2);                                      // the follower: the same SYNC=LSDJ song
-graph.applyRoleConfig(1, "sameboy", { linkGroupId: 1 }); // link group 1: a group of one is no cable yet
-graph.applyRoleConfig(2, "sameboy", { linkGroupId: 1 }); // the second member: the graph wires the cable
+graph.setCoreConfig(1, { linkGroupId: 1 });              // link group 1: a group of one is no cable yet
+graph.setCoreConfig(2, { linkGroupId: 1 });              // the second member: the graph wires the cable
 
 audio.renderAudio(6000);                                 // MILLISECONDS of emulated audio, not frames
 audio.pressButton(1, 7 /* Start */, true);               // START on the leader only
@@ -92,8 +92,10 @@ The two config surfaces are distinct and easy to confuse:
   the role's two protocol nodes: [lsdjSync.ts](../packages/retroplug/src/script/nodes/lsdjSync.ts) before the
   Game Boy, host to cart, and [lsdjSerialOut.ts](../packages/retroplug/src/script/nodes/lsdjSerialOut.ts)
   after it, MI.OUT and master sync from the cart to the host). A feature role: it is set with the store's
-  `setRoleConfig` and handed to both nodes as their settings in the graph the project publishes, while
-  native's `applyRoleConfig` takes only the `sameboy` and `mesen` system roles (any other kind returns false).
+  `setRoleConfig` and handed to both nodes as their settings in the graph the project publishes, which also
+  gives the Game Boy's own node the serial-out capture that mode needs (below). The core's own knobs, the
+  `sameboy` system role's model, highpass and display group, are that node's settings too, and its link group is
+  the graph's cable (a `DirectGraph` sets them with `setCoreConfig`, in the role's labels).
 
 A test that builds through a `DirectGraph` has no store, so it hands the graph a system's roles and the
 MIDI routing itself, each call a publish of the graph that places their nodes. Until a routing mode is
@@ -103,7 +105,7 @@ set, no host MIDI reaches any system. From
 ```ts
 graph.setMidiRouting("sendToAll");                                                 // host MIDI to every system
 graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: "masterSync" } }]);    // places rp.lsdj.sync and rp.lsdj.serial-out
-be.setSerialOutCapture(id, "listen"); // the store sets it ("drive" for midiOut, "listen" for masterSync); a DirectGraph caller does it itself
+// ...and the same document sets the Game Boy node's capture to "listen" ("drive" for midiOut), as a store's does
 ```
 
 A `DirectGraph` hands each node its config as given, where a store hands the config the role's schema
@@ -580,8 +582,10 @@ mode selected" indicator on the aboy build. Read the SYNC field text (or the
 
 ### Master mode (MI.OUT) verification
 
-Serial-out capture is a mode the store sets from the `lsdj-sync` mode (`serialOutCaptureFor` in
-[systemsStore.ts](../packages/retroplug/src/systemsStore.ts)): `drive` for MI.OUT, `listen` for master sync.
+Serial-out capture is a setting of the Game Boy's node, which `buildGraph` writes for the mode the system's
+`lsdj-sync` node runs (`serialOutCaptureFor` in [graph/controller.ts](../packages/retroplug/src/graph/controller.ts),
+the MI.MAP controller's forced mode included): `drive` for MI.OUT, `listen` for master sync, `off` otherwise. It
+rides the document that hands the decoder its mode, so a mode switch changes both in the same block.
 The captured bytes reach the system's `rp.lsdj.serial-out` node, placed after the Game Boy, over a forward
 wire from the Game Boy node's `serialOut`, in the block the core captured them, each at the frame its core
 completed it, and the decoders send each message in that same block, at its byte's frame:
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 4e1d4b14..8c362949 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1,6 +1,6 @@
 # Design: the modular node-graph engine
 
-**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built, and Phase C1 (the rest of Phases C to E not yet built).
+**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B3b built, and Phases C1 and C2 (the rest of Phases C to E not yet built).
 
 RetroPlug's long-term direction is a modular system in the spirit of Reaktor: emulated consoles (and
 the real hardware they can drive) appear as nodes in a graph alongside DSP effects and routing nodes.
@@ -436,7 +436,10 @@ never calls its `prepare_settings` while a `commit_settings` for the same node m
 may read its current settings there to compare against; `setGraph` waits for such a commit to be adopted first
 (§5.3). A type that declares the flag promises its prepare reads nothing a commit writes, so its commits never
 make `setGraph` wait. Every script type declares it ([B3a §2.4](modular-graph-phase-b3a-design.md)); a C++ node does
-with `static constexpr bool stateless_prepare`, which rules out `needs_rebuild`. The wait is global: one commit to a
+with `static constexpr bool stateless_prepare`, which rules out `needs_rebuild`. A C++ node that rebuilds only on a
+value its creation fixed declares `static Key rebuild_key(const Settings&)` instead: prepare compares the new
+settings' key with the key the instance was created with, which no commit writes, so it may declare the flag (the
+system nodes key on their core, [C2 §4](modular-graph-phase-c2-design.md)). The wait is global: one commit to a
 type without the flag holds back an edit that touches only nodes with it (a gate per instance is possible later). And
 a `node.hpp` node without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag could be derived
 rather than declared.
@@ -887,6 +890,14 @@ Each node handles its own params, so `Engine::applyConfigField` (its `dynamic_ca
 `ConfigField` enum) goes away, and so does `EngineRpcService::applyRoleConfig`'s `"sameboy"` / `"mesen"`
 branch: decoding a node's settings is the node's job.
 
+C2 built the live knobs ([modular-graph-phase-c2-design.md](modular-graph-phase-c2-design.md)) with a stricter split
+than the list above: **a param is what is worth playing, automating or modulating** (every system's gain, a NES's
+expansion volume), and everything else is a setting, highpass and the colour and palette knobs among them, as are a
+NES's accuracy switches and APU latency and a Game Boy's LSDj serial-out capture. A setting reaches the core at the
+start of the first block of the plan that carries it, a param's live edit at the start of the next block, each
+value-guarded by the node; the side path and its casts are gone. The ROM, the seeds, Game Gear mode and the other
+construct-time fields stay in the build spec until C3 builds cores in their nodes.
+
 ### 6.4 System capability interfaces
 
 Today's fat `SystemBase` splits into optional interfaces:
@@ -909,12 +920,12 @@ Extensions only ever see host RPC that routes through those two doors.
 
 | Today | Becomes |
 |---|---|
-| `Engine::applyConfigField` casts | node params and settings |
+| `Engine::applyConfigField` casts | node params and settings (gone in C2) |
 | `Engine::syncSplitPlan` casts | `ports_changed` (§3.5) |
 | `Engine`'s gather of SameBoy `serialOutLog_` | the GB `serialOut` port |
 | `LinkGroup`, `Project::rebuildLinkGroups` | `link` wires, the planner's coupled groups |
 | `BlockRunner` / `OfflineRender` including SameBoy headers | the console-neutral executor |
-| `SystemKind`, `ConfigField`, native `AudioRouting` | deleted |
+| `SystemKind`, `ConfigField`, native `AudioRouting` | deleted (`ConfigField` in C2) |
 
 What remains of `Engine` is the executor, the rings and the registry.
 
@@ -1025,8 +1036,8 @@ B3b ([modular-graph-phase-b3b-design.md](modular-graph-phase-b3b-design.md)) bui
 |---|---|
 | `setGraph(doc)` (built in B2: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)) | `constructSystem`, `removeSystem` (gone in B2; building a core stays a native call, `buildCore`, until Phase C) |
 | `putResource(type, bytes)` -> handle, `releaseResource(handle)` | `setAudioRouting` (gone in B2) |
-| capability calls (memory, SRAM, state, ROM info) addressed by node id | `applyRoleConfig` (Phase C) |
-| `setParam(node, param, value)` | `setSerialOutCapture` (Phase C) |
+| capability calls (memory, SRAM, state, ROM info) addressed by node id | `applyRoleConfig`, `applySystemSetting` (gone in C2: a system's knobs are its node's settings and params) |
+| `setParam(node, param, value)` (built in C2: [modular-graph-phase-c2-design.md](modular-graph-phase-c2-design.md)) | `setSerialOutCapture` (gone in C2: the capture is a Game Boy node's setting) |
 | snapshot reads (unchanged) | `dspSetSystems`, kernel loading (gone in B3a: the kernel's structure became its node's settings in the document, and every Engine embeds its bundle; since B3b each role's node's settings are its config) |
 | host I/O, transport (unchanged) | |
 
@@ -1128,8 +1139,9 @@ B2 is built: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md
 `buildCore`, which builds without placing), `removeSystem` and `setAudioRouting` are gone. Two things
 moved out of B2 as designed: the role-config commands
 (`applySystemSetting`, `applyRoleConfig`, `ConfigField`) stay until Phase C, where the live knobs become
-node params, and the kernel commands stay until B3 deletes the kernel (B3a deleted them). Building a core
-stays a native call (`buildCore`) until Phase C, when system nodes build their own from settings and resources.
+node params (C2 deleted them), and the kernel commands stay until B3 deletes the kernel (B3a deleted them).
+Building a core stays a native call (`buildCore`) until Phase C, when system nodes build their own from settings
+and resources.
 
 B3 is split in two. B3a is built ([modular-graph-phase-b3a-design.md](modular-graph-phase-b3a-design.md)): the
 kernel's input and output became real wires (host I/O nodes, input ports on every system node, the Game Boy's
@@ -1150,6 +1162,13 @@ pack of script node types in a QuickJS runtime pair of its own with the built-in
 reloads while audio runs (`loadScriptPack`, on every host), and a node pays for its own promise jobs and its pack's
 memory limit. Strict parity held bit for bit at every commit.
 
+C2 is built ([modular-graph-phase-c2-design.md](modular-graph-phase-c2-design.md)): a system's live knobs are its
+node's, each a setting in the published document or a param, its value in the document and its live edits through
+`setParam` (§6.3). `node.hpp`'s `rebuild_key` lets the system nodes prepare statelessly, so a knob edit never makes
+`setGraph` wait; `ConfigField`, `Engine::applyConfigField` and its casts, the `applySystemSetting`, `applyRoleConfig`
+and `setSerialOutCapture` RPCs and the build spec's capture are gone; and an LSDj's capture rides the document that
+hands its decoder the mode, so a mode switch is atomic. Strict parity held bit for bit at every commit.
+
 - Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes.
 - `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a preallocated ring (it
   currently allocates on the audio thread).
diff --git a/docs/modular-graph-phase-c2-design.md b/docs/modular-graph-phase-c2-design.md
index 7562ce8b..af8d0ea6 100644
--- a/docs/modular-graph-phase-c2-design.md
+++ b/docs/modular-graph-phase-c2-design.md
@@ -1,6 +1,7 @@
 # Phase C2: live knobs as system node settings and params
 
-**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** designed
+**Date:** 2026-10-30 · **Branch:** `feature/modularize` · **Status:** built: section 8's five commits; what building
+them changed is in the sections it changed, and what it found is section 10
 
 A system's live knobs (a Game Boy's model, highpass and display group, a NES's region and accuracy switches, every
 system's gain) reach its core today through their own path beside the graph: TS calls `applyRoleConfig` /
@@ -63,11 +64,19 @@ The user's, on 2026-10-30:
 | `rp.mesen.nes` | `core`, `region`, `removeSpriteLimit`, `apuLatencyMs`, `s5bNoise`, `mmc5PhaseReset` | `gain`, `expansionVolume` |
 | `rp.mesen.gba`, `rp.mesen.sms` | `core` | `gain` |
 
-- `gain`: dB, -90 to 12, default 0 (the store's `GAIN_MIN` / `GAIN_MAX`). `expansionVolume`: percent of unity, 0 to
-  200, default 100. `serialOutCapture`: `off`, `listen` (master sync) or `drive` (MI.OUT), default `off`.
+- `gain`: dB, -90 to 12, default 0 (the store's `GAIN_MIN` / `GAIN_MAX`). `expansionVolume`: whole percent of unity
+  (an int param, as the core takes it), 0 to 200, default 100. `serialOutCapture`: `off`, `listen` (master sync) or
+  `drive` (MI.OUT), default `off`.
 - **Enums travel as labels**, never ordinals (design section 2.4): the labels the role configs already spell
   (`"cgbC"`, `"removeDcOffset"`, `"n8"`), which are what a `.rplg` saves, so the document and the saved project agree.
-  C++ maps them to its enums; the goldens pin the spelling.
+  C++ maps them to its enums (section 8's first question says how); the goldens pin the spelling. An ordinal, another
+  spelling, or a setting's number out of its range (`lightTemperature` -1 to 1, `apuLatencyMs` 0.25 to 6) refuses the
+  settings, never clamped, as a document's bad value always has; a param's value is clamped, as the planner clamps
+  every param.
+- **Every knob is optional.** Absent, the node leaves its core as it was built, or as the last document that carried
+  the field left it. So a Game Boy's node always carries its capture, `off` included, since one left out would leave an
+  armed capture armed; and a system with no core config (a `DirectGraph`'s until it is given one, the B1 translator
+  goldens' models) carries `core` and its capture alone.
 - **What stays outside C2:** the construct-time fields (SMS `enableFm`, NES `sdRoot`, the FIFO profile and its two
   overrides, the N8 link and its round trip, `channelExportMode`) stay in the build spec until C3 builds cores in their
   nodes; `buildCore`'s facts (the export mode, the link group) stay casts until C4; `linkGroupId` stays a TS-side field
@@ -75,15 +84,21 @@ The user's, on 2026-10-30:
 
 ## 4. The native side
 
-- **Each system node type's settings struct grows its table's fields**, and the types declare their params (node.hpp's
-  `float_param`), so the descriptor lists them and the planner checks and clamps a document's values.
-- **When a value reaches its core.** `on_settings` only copies (node.hpp: never allocate there), so the node applies
-  what moved at the start of its next block, before `prepareForBlock`, and everything once at activation, right after it
-  takes its core and before that core first runs. Each field is value-guarded, as `applyConfigField` was: a palette
-  change reapplies the display group and restarts nothing, a model change restarts the core and a region change resets
-  it, on the block thread as before. A param's control stream gives its latest value each block, applied value-guarded
-  before `prepareForBlock`: a change lands at the block's start (at most a block late, as the command ring's did), and
-  the core smooths gain as it does now.
+- **Each system node type's settings struct grows its table's fields**, each optional (section 3), and the types
+  declare their params (node.hpp's `float_param`, and `int_param` for `expansionVolume`), so the descriptor lists them
+  and the planner checks and clamps a document's values.
+- **When a value reaches its core.** `on_settings` only copies (node.hpp: never allocate there; the node keeps a
+  pointer to the committed settings), so the node applies what moved at the start of its next block, before its inputs
+  and `prepareForBlock`, and every setting present once at activation, right after it takes its core and before that
+  core first runs. Each field is value-guarded, as `applyConfigField` was: a palette change reapplies the display group
+  and restarts nothing, a model change restarts the core and a region change resets it, on the block thread as before.
+  A Game Boy applies its fast boot first, which waits for the next restart and so is what a model's restart boots with,
+  then the rest in `applyRoleConfig`'s order, the capture last (section 10 says why the fast boot moved). A param's
+  control stream gives its latest value each block, applied value-guarded after the settings and before the inputs: a
+  change lands at the block's start (at most a block late, as the command ring's did), and the core smooths gain as it
+  does now. Every param reaches the core at the node's first block, not at activation: a node reads its params only
+  in a block (the host binds its control streams there, landing the document's values as it adopts the plan), and a
+  core runs nothing before its node's first block, so it plays its first sample at the document's level.
 - **The atomic switch.** `serialOutCapture` reaches the Game Boy at the start of the first block of the plan that
   carries it, the block in which `rp.lsdj.serial-out` first runs in the new mode, so no byte captured in one mode is
   decoded in the other.
@@ -94,7 +109,8 @@ The user's, on 2026-10-30:
   thread that may not be running.
 - **`Engine::setParam(nodeId, paramId, value)`** on the control thread, over `GraphHost::setParam`: the command ring,
   flushed inline while quiescent, the value clamped and rounded to the param's range; false for a node or param the
-  running plan does not have (a node the next publish adds takes its value from the document).
+  last published plan does not have (a node the next publish adds takes its value from the document), a NaN, a param
+  a wire drives, or a full ring.
 - **Deleted:** `ConfigField`, `DspCommand::SetConfigField`, `EngineInvoker::applyConfigField` and
   `Engine::applyConfigField` with its casts; the `applySystemSetting`, `applyRoleConfig` and `setSerialOutCapture`
   RPCs; the build spec's `serialOutCapture` and `buildCore`'s arming of it (activation applies it before the core first
@@ -108,17 +124,27 @@ The user's, on 2026-10-30:
   capture is computed from the mode the placed `lsdj-sync` node actually runs, after the controller's MI.MAP override
   (`controllerSyncOverride`): `drive` for MI.OUT, `listen` for master sync, `off` otherwise. `serialOutCaptureFor` moves
   from the store into the graph layer beside the override, which retires the spec/07 item where a forced node and its
-  capture disagreed. The B1 translator goldens and the role goldens are re-recorded, the diff exactly the new settings
-  and params on system nodes.
+  capture disagreed. A setting value the node would refuse (another label, a number out of its range, another type) is
+  left out rather than sent, the core keeping what it has (section 10). `GraphSystem` gains two optional fields,
+  `coreConfig` (the config of the role whose kind is the core) and `gainDb`, which the store's `graphSystems()` fills,
+  and `systemKnobs` is the one table of which core-config field is a setting and which a param. The B1 translator
+  goldens and the role goldens are re-recorded, the diff exactly the new settings and params on system nodes: 161 of
+  the B1 goldens' 238 documents changed, by the capture alone, since their models carry no core config or gain, and 30
+  of the 37 role golden cases.
 - **The stores.** A gain edit, or a NES's expansion volume, updates the store's value and calls
   `backend.setParam("sys-<id>", ...)`, with no publish; the next publish, for whatever reason, carries the same value.
   An edit of a setting field of `sameboy` / `mesen` updates the role config and publishes, as a link-group edit
-  already does. A construct-time field's edit stays as it is (a rebuild through `reset(id)`). An `lsdj-sync` edit
+  already does. The store tells them apart by the system node's knobs before and after the edit (`systemKnobs`): a
+  moved setting publishes, a moved param alone is `setParam`, and an edit moving both publishes, the document carrying
+  the param. A construct-time field's edit stays as it is (a rebuild through `reset(id)`). An `lsdj-sync` edit
   already publishes, and the capture rides that document; `syncSerialOutCapture` goes. `reloadOnRomChange` stays a
   TS-only setting with no backend call.
 - **`Backend`** gains `setParam(nodeId, paramId, value): boolean` (the real backend over its RPC, on the `emulator`
   facet; the mock records each call), and loses `applySystemSetting`, `applyRoleConfig`, `setSerialOutCapture` and the
-  build spec's `serialOutCapture`. `DirectGraph` loses `applyRoleConfig` and gains `setParam`.
+  build spec's `serialOutCapture`. `DirectGraph` loses `applyRoleConfig` and gains `setParam` and `setCoreConfig(id,
+  partial)`: a core config in the labels the store's role spells, merged over what it gave the system before and
+  published (a Game Boy's `linkGroupId` its link cable), which refuses, changing nothing, a field its node does not
+  take (section 8's third question).
 - **Callers moved:** the `test-native` files, mock-tier tests and the CLI session (`analyze-lsdj-sync`) that call the
   old methods set a mode or a knob the way the app does, through the store or the published document. The parity rig
   (`render-blocks`) is among them, and keeps its recorded output.
@@ -133,11 +159,16 @@ The user's, on 2026-10-30:
   atomic switch (one `setGraph` changing an LSDj's mode gives the Game Boy its capture in the block the decoder first
   runs in the new mode); `Engine::setParam` (unknown node or param refused, values clamped, a later publish of the same
   value moving nothing); no allocation on the block thread for the params path; the new cases under ThreadSanitizer.
+  And every system node of every document in `buildGraph`'s goldens names only the settings fields its type's schema
+  lists (section 10).
 - **`pnpm test`:** `buildGraph`'s settings and params for every system type, the capture for each mode and `off` under
   the controller's forced MI.MAP; the store's gain and expansion volume calling `setParam` without publishing, a setting
   edit publishing, an `lsdj-sync` edit publishing its capture; the goldens re-recorded.
 - **`test:native`:** `app-settings` and `app-mesen-settings` drive every knob through the real host; the LSDj and
-  model-switch tests set their modes through the store.
+  model-switch tests set their modes through the store, or a `DirectGraph`'s `setCoreConfig` and `setPipeline`; and
+  `lsdj-capture-settings` (it was `lsdj-midiout-build-flag`, which tested the build spec's capture) holds an LSDj
+  adopted in MI.OUT or master sync to decode from the first document that places it, with no capture in its build
+  spec.
 - **Parity:** knobs reach the core at a block's start, where the command ring delivered them, so strict parity must
   hold unchanged; a case that moves is a finding to explain before anything is re-recorded.
 
@@ -153,22 +184,43 @@ spec/01, spec/02 (the system nodes, the command ring's fields, the RPC table), s
 Five commits, each green on its own:
 
 1. **node.hpp's `rebuild_key`**, with its graph-layer tests; no existing node changes.
-2. **System nodes take settings and params, and `Engine::setParam`**; the old path still works beside them.
-3. **TS switches over:** `buildGraph`'s settings, params and capture, the stores' `setParam` and publishes,
-   `Backend.setParam` over its RPC, the goldens re-recorded.
+2. **System nodes take settings, and `Engine::setParam`**; the old path still works beside them.
+3. **TS switches over, and the system nodes' params:** `buildGraph`'s settings, params and capture, the stores'
+   `setParam` and publishes, `Backend.setParam` over its RPC, the goldens re-recorded. The params landed here rather
+   than in the second commit: a node gives its core its params' values from its first block, so params placed while
+   no document carried a value would have given every core their defaults, over the gain and expansion volume the old
+   path or the build had set.
 4. **The old path deleted:** `ConfigField`, `applyConfigField`, the three RPCs and `Backend` methods, the build spec's
    capture, the reload flag; the `test-native` callers and the CLI session moved.
 5. **The docs.**
 
 As for C1, a prototype is built first in these five commits on a local branch, and the plan is written from it. The
-prototype answers:
+prototype answers (each answer follows its question):
 
 - How C++ maps the role configs' labels to its enums in a settings struct (reflect-cpp's enum names differ in case),
-  without a second spelling.
+  without a second spelling. **From the enumerators' own names, first letter lowercased.** Every label the role
+  configs spell is its C++ enumerator's name with the first letter lowercased (`CgbC` is `"cgbC"`, `RemoveDcOffset`
+  `"removeDcOffset"`, `Auto` `"auto"`), so `knobs::Label<E>` in
+  [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) is an `rfl::Literal` generated at compile time
+  from `rfl::get_enumerator_array<E>()`: it reads exactly those strings (an ordinal, `"CgbC"` or `"cgb-c"` refuses the
+  settings), lists them as the schema's enum and maps one back to its enumerator by position. It builds each name with
+  `rfl::internal::StringLiteral`, an internal reflect-cpp type, so a reflect-cpp bump is to check it. A test reads
+  `settingsEnums.ts` and holds every enum knob's schema list to its TS list (`MODEL_VALUES`, `HIGHPASS_VALUES`,
+  `COLOR_CORRECTION_VALUES`, `DMG_PALETTE_VALUES`, `REGION_VALUES`, `CARTRIDGE_ACCURACY_VALUES`, and `off` / `listen`
+  / `drive`), so the two spellings cannot drift.
 - Whether a model or region change applied at a block's start, rather than in the command ring's drain, moves any
-  parity case (it should not: both run before the block's first step).
+  parity case (it should not: both run before the block's first step). **None moved.** Strict parity held bit for bit
+  at every commit, with nothing re-recorded, its fixture cases among them: every parity system node carries its whole
+  core config from the first document that places it, and the live edits parity makes (a gain mid-note, the routing
+  gains, highpass, APU latency, a VRC6 expansion volume) reach the core through `setParam` or a publish at the block
+  the command ring's did. The node's apply is the core's own setter at that block boundary, which a test holds bit for
+  bit against a core given the same edit directly.
 - Whether every caller of the old methods can set its knob through the store, or a test needs `setParam` or a document
-  of its own.
+  of its own. **Every one could, through the store or the document.** The parity rig and every store-driven caller
+  moved to the store's own calls; the `DirectGraph` callers (two link-group tests, the CLI's `analyze-lsdj-sync`
+  session, the WRAM seam and the model-switch test) needed `DirectGraph.setCoreConfig`, since a model switch must name
+  its label, and the four that set a capture directly needed nothing, `setPipeline` already publishing it.
+  `DirectGraph.setParam` has only mock-tier callers.
 
 ## 9. Left as it is
 
@@ -177,3 +229,59 @@ prototype answers:
 - **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the forced
   one.
 - **The construct-time fields and `buildCore`'s facts:** C3 and C4.
+- What section 10 found and leaves: a NES's settings allocating on the block thread, a settings field a node does not
+  know ignored, and an enum's two spellings on the C++ side.
+
+## 10. Found while building
+
+- **Three latent bugs, which the document's knobs fixed.** Each was proved failing before the switch-over and is
+  pinned now. A loaded project's saved gain never reached its core: nothing applied the stored value until the gain
+  was edited, so a system saved at -90 dB played at 0 dB (`app-settings`: RMS 0.10399 where 0 was due). The UI's
+  System > Render ignored the gain it carried (`render.ts` handed it to `adopt`, which never applied it). And a loaded
+  NES's `s5bNoise` and `mmc5PhaseReset` never reached its core (`MesenBackend::build` does not copy them from the
+  construct blob). Each system node gives its core the document's values by its first block.
+- **The NES core allocates on the block thread on its own**, inside Mesen (8 allocations and frees over two 512-frame
+  blocks), and its sprite-limit and accuracy setters allocate whichever path calls them, as they did on the command
+  ring. So section 6's "no allocation on the block thread" holds for a Game Boy's knobs and for every param, which the
+  tests and a profile build's three windows hold at zero, and not for a NES's settings. Left as it is: Mesen's own.
+- **The fast boot moved ahead of the model.** The prototype applied the Game Boy's knobs in `applyRoleConfig`'s order,
+  model first. Several documents published between two blocks commit together, so a fast boot edit and a model edit
+  landed in one block, and the model's restart booted with the old fast boot (`app-sameboy-display` failed). The fast
+  boot waits for the next restart, so it goes first, and the edits restart the core as if made one at a time
+  (section 4); a test pins the order.
+- **The capture is on every Game Boy node, `off` included** (section 3): an absent knob leaves the core as it is, so a
+  capture left out once a mode stops needing one would stay armed. Every Game Boy node in the B1 goldens gained
+  `"serialOutCapture": "off"`, the only change in their 161 changed documents.
+- **`buildGraph` leaves out a value its node would refuse; `DirectGraph.setCoreConfig` refuses it.** The store's config
+  has been through its role schema, so a value the node would refuse comes from a hand-edited file or a newer build's
+  label, and a loaded project keeps its config as written (`adopt`): sent, it would refuse the whole document and
+  silence every system, so `systemKnobs` leaves it out and that one core keeps what it has. A `DirectGraph` caller is
+  a test or a tool, which should hear of its mistake: `setCoreConfig` refuses an ordinal (`{ model: 1 }`, as two tests
+  passed one before), a construct-time field, another core's field or a link group outside 0 to 255, changing nothing.
+- **`setCoreConfig` merges where `applyRoleConfig` reset.** The old call decoded a partial config with every absent
+  field at its default and applied them all, so `{ linkGroupId: 1 }` also reset the model, highpass, fast boot and
+  display group; `setCoreConfig` leaves an absent field as it was. No caller depended on the reset (every number the
+  moved tests log is unchanged).
+- **A refused document can still have changed a core it took.** Activation runs while a document plans, so the knobs a
+  node gives its core there stay on it when the plan is refused and the core goes back to parked. In the app the next
+  document carries the same knobs, so nothing shows.
+- **A republished document leaves a param's live value alone** unless its value moved from the last document's: the
+  planner lands a document's param value only for a fresh node or a value that differs from the one it last landed. So
+  the stores keep every value they set live in their model, and the next document carries what the node runs. A NES a
+  `DirectGraph` places with no core config gets `expansionVolume` 100 at its first block, its param's default; only one
+  built with another construct-time volume would notice.
+- **A C++ node ignores a settings field it does not know** (node.hpp reads settings with `rfl::DefaultIfMissing` and
+  no `rfl::NoExtraFields`), so a misspelt knob would plan and do nothing. `BuildGraphGoldens.test.cpp` fails on a
+  golden document whose system node names a field its type's schema does not list (it caught a deliberate
+  `"dmgPallete"`). That guard replaced the Catch2 case "a build spec's serialOutCapture crosses the wire under the name
+  TS sends", deleted with the field. Left as it is: a strict reader would refuse the document at plan time instead.
+- **Enum labels have two spellings on the C++ side.** The system nodes' enum settings read the role configs' labels
+  (`"cgbC"`), where node.hpp's `enum_param` labels are kebab-case (`"cgb-c"`): a knob moved from a setting to an enum
+  param would change the word a document carries. Left as it is; no enum knob is a param.
+- **A setting's range rides a type.** `lightTemperature` and `apuLatencyMs` are refused out of range by a validator
+  whose bounds are a type's constants rather than floating-point template arguments, which some of the compilers the
+  build supports lack (older AppleClang). No Mac has compiled it yet.
+- **Nothing was lost with `ConfigField::LinkGroup`.** It wrote a live core's `linkGroupId`, which only `buildCore`'s
+  report reads, at the build; the cable is the document's wiring.
+- **Renamed:** `test-native/lsdj-midiout-build-flag.test.ts`, whose build-spec flag is gone, is
+  `lsdj-capture-settings.test.ts` (section 6); the B2 design, which names the old file, is left as the record it is.
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index 0c19dbf4..57f8f992 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -114,7 +114,7 @@ world currently owns the `Engine`, and therefore who drains the command ring.
         ┌────────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
         │  command ring          │  │  SnapshotRegistry    │  │  release ring        │
         │  GraphHost: plan       │  │  frame/state/SRAM    │  │  GraphHost: the      │
-        │  swaps + DspCommands,  │  │  (owned triple-bufs) │  │  replaced plans      │
+        │  swaps + commands,     │  │  (owned triple-bufs) │  │  replaced plans      │
         │  in push order         │  │                      │  │  (PlanStorage*)      │
         └────────────────────────┘  └──────────────────────┘  └──────────────────────┘
                          │                    ▲                    ▲
@@ -142,43 +142,53 @@ All three are built on the shared-core transport primitives:
 Every control-plane edit lands on ONE ring: the `SpscRing<Command, 256>` inside the Engine's
 [`GraphHost`](../packages/native/src/graph/GraphHost.hpp)
 ([modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md) section 5). An edit
-arrives one of two ways:
+arrives one of three ways:
 
-- **Structure** (which systems run, in what order and wired how, a Game Boy's link cable, the host's
-  output shape, and the script nodes, each with its settings: ahead of the systems the MIDI routing, the
-  controller session and each role's protocol node, and after its Game Boy an LSDj's serial-out decoder) is
-  a graph document TS builds
+- **Structure** (which systems run, in what order and wired how, a Game Boy's link cable, each system
+  node's settings, which are its core's knobs (a Game Boy's model, palette and the serial-out capture its LSDj
+  needs, a NES's region), and its params' values, the host's output shape, and the script nodes, each with its
+  settings: ahead of the systems the MIDI routing, the controller session and each role's protocol node, and
+  after its Game Boy an LSDj's serial-out decoder) is a graph document TS builds
   ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md),
-  [modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)). The stores call
+  [modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md),
+  [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)). The stores call
   [`EngineGraph.publish`](../packages/retroplug/src/engineGraph.ts) once at the end of every edit that
-  changes what the document holds (a structural edit, the config of a role with a protocol node, the MIDI
-  routing, the controller); it builds the document with
+  changes what the document holds (a structural edit, the config of a role with a protocol node, a setting
+  of a system's core config, the MIDI routing, the controller); it builds the document with
   [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) and sends it with the `setGraph` RPC,
   which `EngineRpcService::setGraph` hands straight to `Engine::setGraph`, not through the invoker.
   `Engine::setGraph` builds the plan's `SystemTable` from the request's slots and the live cores
   (parked by `buildCore`, or placed by an earlier plan), and calls `GraphHost::setGraph`, which plans
   the document on the control thread (every layout checked before any instance is created, every new
   instance created and activated) and pushes the plan onto the ring as a plan swap.
+- **A param's live value** (a system's gain, a NES's expansion volume: what is worth playing, automating
+  or modulating) goes through `setParam`, which `EngineRpcService::setParam` hands to `Engine::setParam`, not
+  through the invoker either: `GraphHost::setParam` clamps the value to the param's range and pushes it onto
+  the ring as a param command, for a node the last published plan places, to land at the start of the next
+  block. The next document the stores publish carries the same value, so the two never disagree.
 - **Everything else** (stage host MIDI, controller MIDI or one system's MIDI, set bpm / transport /
-  ppq, apply a config field, press a button, write RAM) goes through the one
+  ppq, press a button, write RAM) goes through the one
   [`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp), which packs it into a POD
   [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes it onto the same ring
   as an opaque host command (`GraphHost::pushHostCommand`).
 
-At drain the ring is applied **in push order**: a plan swap adopts its plan, and a host command goes
-to the Engine's handler, which applies it against the `SystemTable` of the plan current at that
-point. A command pushed after a structural edit therefore sees the new systems and one pushed before
-it sees the old, even when both plans are adopted in the same drain.
+At drain the ring is applied **in push order**: a plan swap adopts its plan, a param command lands in
+its param's stream, and a host command goes to the Engine's handler, which applies it against the
+`SystemTable` of the plan current at that point. A command pushed after a structural edit therefore sees
+the new systems and one pushed before it sees the old, even when both plans are adopted in the same drain.
 
 `setGraph` waits (`GraphHost::waitForCommits`, up to a 2 s timeout, then `busy`) only while a plan it
 published carries a settings commit to a node type whose prepare reads the node's current settings,
 one without `RP_TYPE_STATELESS_PREPARE` (`PlanStorage::awaitsAdoption`): the planner must not ask
 such a node to prepare again while its last commit may still be landing. The commits the engine's
 documents carry are the script nodes', whenever their settings change (a system added or removed, which
-moves the routing node's outputs, a role's config, the MIDI routing, the controller), and the cart
-mirror's, and both prepare statelessly (every script type does); `rp.host.midi-out` swaps or rebuilds,
-statelessly too, and the other C++ nodes take each settings change they meet as a rebuild. So `setGraph` never waits on the audio thread: several plans can sit in the ring at once,
-adopted in push order in one drain. What bounds edits is the ring itself. While no block runs with
+moves the routing node's outputs, a role's config, the MIDI routing, the controller), the system nodes',
+whenever a knob moves (a model, a palette, an LSDj's capture), and the cart mirror's, and all of them prepare
+statelessly: every script type does, and a system node rebuilds only for a new core, which it compares
+against the core it was created with (node.hpp's `rebuild_key`), never against settings a commit writes.
+`rp.host.midi-out` swaps or rebuilds, statelessly too, and the other C++ nodes take each settings change
+they meet as a rebuild. So `setGraph` never waits on the audio thread: several plans can sit in the ring
+at once, adopted in push order in one drain. What bounds edits is the ring itself. While no block runs with
 the audio thread still owning the engine (a host that stops calling process while the plugin stays
 active, as CLAP's `stop_processing` allows), each edit queues one plan, holding its buffers, and once
 the ring is full (255 usable slots, shared with the queued commands) `setGraph` answers `busy`.
@@ -211,25 +221,24 @@ outputs before the handoff.
 
 Producer methods pack a command and push. Lightweight commands are pure POD: `stageMidi` (a message
 of up to 4 bytes rides inline), `stageControllerMidi`, `setBpm`, `setTransport`, `setPpq`,
-`applyConfigField`, `pressButton`; if the ring is full they are simply dropped (a lost edge, never a
-leak). Heavy commands carry an **owning heap payload**: `writeRam`, and a `stageMidi` /
-`stageSystemMidi` longer than 4 bytes, box a `new`-ed buffer (deleted after apply or, if the push
-fails, immediately). `stageControllerMidi` is inline only: a message longer than 4 bytes is refused
+`pressButton`; if the ring is full they are simply dropped (a lost edge, never a leak), as is a
+`setParam`'s, which answers false then. Heavy commands carry an **owning heap payload**: `writeRam`,
+and a `stageMidi` / `stageSystemMidi` longer than 4 bytes, box a `new`-ed buffer (deleted after apply
+or, if the push fails, immediately). `stageControllerMidi` is inline only: a message longer than 4 bytes is refused
 before it is pushed. A staged message lands in the engine's staging at drain, where the plan's
 `rp.host.midi-in` / `controller-in` take it at the top of the next block.
 
-Structure never rides the ring as a command. A `setGraph` request carries a `tap` when its edit
-re-arms the NES channel tap (every edit that builds or drops a core, and every routing change, even to
-the mode already set), and the plan's swap command carries it as a `SyncTap` trailer (dispatched in the
-same pop, right after the plan is adopted), which restores whatever is armed and then arms the tap the
-request names against the plan just adopted
-([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4). The `LinkGroup` config field
-goes both ways: the field reaches the core as a command, and the store republishes the graph with the
-cable. A document that does not plan (refused, or a full ring) changes nothing: a core a node of the
-refused plan had taken goes back to parked, and the store keeps its model. A core parked for it waits
-for the next document that plans, which frees it unless it places it. The store's next publish is sent
-even when its document matches the running one, so the refused document sent again places those cores
-once it plans.
+Structure never rides the ring as a command. A `setGraph` request carries a `tap` when its edit re-arms
+the NES channel tap (every edit that builds or drops a core, and every routing change, even to the mode
+already set), and the plan's swap command carries it as a `SyncTap` trailer (dispatched in the same pop,
+right after the plan is adopted), which restores whatever is armed and then arms the tap the request names
+against the plan just adopted ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4). A
+Game Boy's link group is wiring alone: the store publishes the graph with the cable (the `linkGroupId` a
+core decodes at its build is only what `buildCore` reports back). A document that does not plan (refused,
+or a full ring) changes nothing: a core a node of the refused plan had taken goes back to parked, and the
+store keeps its model. A core parked for it waits for the next document that plans, which frees it unless
+it places it. The store's next publish is sent even when its document matches the running one, so the
+refused document sent again places those cores once it plans.
 
 The handler (`Engine::onHostCommand` → `applyCommand`) switches on `DspCommand::Kind`, applies each
 command and deletes its owning payload. The block drivers (the plugin's `run()`, the test host's
@@ -387,15 +396,16 @@ A pack's audio side runs on **both** threads over its lifetime (the control thre
 guard to the current thread with `JS_UpdateStackTop` before entering JS, or a call from a
 different stack throws a spurious overflow. The control side does the same.
 
-`Engine::processBlock` runs the plan: `rp.host.midi-in` and `rp.host.controller-in` take what the
-hosts staged; the routing node fans the host MIDI out to the systems, the controller session reads the
-surface, and each system's protocol nodes read their routed MIDI (risa and smsggdj read only the
-transport) and send that system node its inputs; the system nodes apply those inputs and run their cores;
-an LSDj's serial-out decoder reads what its Game Boy captured on its link port in that block and decodes
-it to host MIDI, each message at its byte's frame; `rp.n8.cart-mirror` hands a physical N8 what the first
-NES was sent, and `rp.host.midi-out` / `controller-out` hand the host the script nodes' MIDI (merged by
-frame) and control-surface output; the mixing nodes and `rp.host.audio-out` fill the host's buffers. Then
-`registry_.publishSystem(...)` runs for each system: the one place every driver funnels the block
+`Engine::processBlock` runs the plan: `rp.host.midi-in` and `rp.host.controller-in` take what the hosts
+staged; the routing node fans the host MIDI out to the systems, the controller session reads the surface,
+and each system's protocol nodes read their routed MIDI (risa and smsggdj read only the transport) and
+send that system node its inputs; the system nodes give their cores what moved of their settings and
+params, then those inputs, and run them; an LSDj's serial-out decoder reads what its Game Boy captured on
+its link port in that block and decodes it to host MIDI, each message at its byte's frame;
+`rp.n8.cart-mirror` hands a physical N8 what the first NES was sent, and `rp.host.midi-out` /
+`controller-out` hand the host the script nodes' MIDI (merged by frame) and control-surface output; the
+mixing nodes and `rp.host.audio-out` fill the host's buffers. Then `registry_.publishSystem(...)` runs for
+each system: the one place every driver funnels the block
 ([`Engine.cpp`](../packages/native/src/host/engine/Engine.cpp); the full order is in
 [02-native-host.md](02-native-host.md)). The host MIDI out (LSDj's MI.OUT and master sync, the
 controller's launches to a real Game Boy), through `rp.host.midi-out`, is drained after the block by the
@@ -410,9 +420,9 @@ The load-bearing rules. Breaking any one of them reintroduces a data race, a use
 lost mutation.
 
 1. **The control plane and the audio thread never touch each other's memory directly.** All
-   control→audio change is a plan or a `DspCommand` on the one command ring, applied in push order;
-   a command is applied against the plan current at its position in the ring. All audio→control
-   observation is a registry read or a release-ring handback. Nothing else crosses.
+   control→audio change is a plan, a param's value or a `DspCommand` on the one command ring, applied in
+   push order; a command is applied against the plan current at its position in the ring. All
+   audio→control observation is a registry read or a release-ring handback. Nothing else crosses.
 
 2. **`audioThreadOwns_` is the single source of truth for who owns the `Engine`.** It is written
    only on the control thread: set true **before** the audio thread starts, cleared **after** it
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index ae3ef537..df118124 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -129,8 +129,9 @@ does.
 ### `EngineRpcService`: emulator / MIDI / transport
 
 Every mutation here routes through the `QueuedInvoker` except these, which call the `Engine`
-directly on the control thread: `buildCore`, `setGraph` and `loadScriptPack` (`Engine::parkCore` /
-`Engine::setGraph` / `Engine::loadScriptPack`),
+directly on the control thread: `buildCore`, `setGraph`, `loadScriptPack` and `setParam` (`Engine::parkCore` /
+`Engine::setGraph` / `Engine::loadScriptPack` / `Engine::setParam`, which pushes onto the GraphHost's ring
+itself),
 `setSampleRate` (`Engine::setSampleRate`, so it refuses while the audio thread owns the engine or the
 running plan holds a system), the render calls, and the profile calls `dspResetAllocStats`,
 `dspRunGc` and `dspTraceReset` (which the script host refuses while the audio thread owns its
@@ -144,8 +145,7 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 | `buildCore` | `(BackendConstructSpec) → optional<RpcBuiltCore>` | TS-owned `spec.id`; `factory_.build` → `Engine::parkCore`, which claims the registry slot. Returns the core's node type, export mode and link group, or null (below) |
 | `setGraph` | `(RpcSetGraphRequest) → RpcSetGraphResult` | a graph document TS built, its slots and its tap re-arm → `Engine::setGraph` (below) |
 | `loadScriptPack` | `(RpcLoadScriptPackRequest) → RpcLoadScriptPackResult` | a pack of script node types, by its id, its source and the memory limit each of its runtimes is held to (`memoryLimitBytes`, a double, absent or 0 for 64 MiB, refused unless a whole number of bytes) → `Engine::loadScriptPack`, which loads it or, for an id loaded already, reloads it (the script host, below); returns `ok`, `error` (empty on success), the planner's `errors` when a reload's replan refused (empty for a refusal before any replan) and the pack's `types`, each with its descriptor JSON |
-| `applySystemSetting` | `(uint32 id, string key, double value) → bool` | `gainDb` / `reloadOnRomChange` → a `ConfigField` |
-| `applyRoleConfig` | `(uint32 id, string kind, string config) → bool` | `kind` `"sameboy"` or `"mesen"`, false for any other; decodes the whole role config and pushes each field as a `ConfigField` (sameboy: Model/Highpass/LinkGroup/FastBoot/ColorCorrection/DmgPalette/LightTemperature; mesen: NesRegion/NesRemoveSpriteLimit/NesApuLatencyMs/NesS5bNoise/NesMmc5PhaseReset/NesExpansionVolume) |
+| `setParam` | `(string nodeId, string paramId, double value) → bool` | a live change to a param of a node the last published plan places (`Engine::setParam` → `GraphHost::setParam`): clamped to the param's range (rounded for an int, enum or bool) and applied at the start of the next block, on the command ring. The store sends a system's `gain` and a NES's `expansionVolume` to its node, `sys-<id>`, this way, with no publish. False for a node or param that plan does not have (a node the next document places takes its value from that document), a NaN, a param a wire drives, or a full ring |
 | `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry |
 | `readSram` | `(uint32 id) → optional<Bytestring>` | SRAM sliced from the published savestate |
 | `screenshot` | `(uint32 id, string path) → bool` | encodes the registry frame to PNG |
@@ -159,12 +159,14 @@ read of a core's state routes through the `SnapshotRegistry`. No method walks a
 
 The table is a selection. The facets
 ([`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp)) hold 19
-`host` methods, 14 `emulator`, 22 `harness` (the renders, transport and sample rate, staged MIDI in and
+`host` methods, 12 `emulator`, 22 `harness` (the renders, transport and sample rate, staged MIDI in and
 MIDI out, `droppedMessages`, `buildTarget`, the kit compiles and the audio runtimes' profiling calls),
 27 `debug` and
-6 `driver`: 88 in the union. There is no call for what the roles do on the audio thread: their protocol
+6 `driver`: 86 in the union. There is no call for what the roles do on the audio thread: their protocol
 nodes are script node types every Engine embeds, and each node's settings ride the documents `setGraph`
-plans.
+plans. Nor is there one for a system's knobs: they are its system node's settings, in the same documents,
+and its params, whose live edits are `setParam`
+([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)).
 
 `duplicate`, `reload` and `remove` are **not** native methods: they are TS orchestration over
 `buildCore`-with-state, the registry reads and the documents the stores publish
@@ -210,8 +212,7 @@ DTO worth spelling out — it mirrors the TS `ConstructSpec`:
 | `savPath` / `statePath` | `optional<string>` | on-disk seed paths |
 | `sramBytes` / `stateBytes` | `optional<Bytestring>` | zip-import seed bytes (win over the on-disk paths) |
 | `romBytes` | `optional<Bytestring>` | ROM bytes to load instead of reading `romPath` (a patched image, e.g. LSDj asset overrides; honoured by SameBoy and by Mesen's NES, GBA and SMS / Game Gear), `romPath` still travelling for the watcher and `.sav` resolution |
-| `settings` | `optional<string>` | the backend "system"-role config JSON, decoded only by the matching backend |
-| `serialOutCapture` | `optional<string>` | arm a Game Boy's serial-out capture before its first block: `"drive"` (LSDj MI.OUT) or `"listen"` (master sync); any other name refuses the build; ignored by every other core |
+| `settings` | `optional<string>` | the backend "system"-role config JSON, decoded only by the matching backend, at the build: the construct-time fields, and the knobs the core starts with (its node gives it the document's at activation) |
 
 ## `Engine` + `QueuedInvoker`: the mutation path
 
@@ -297,13 +298,9 @@ staging). Then, in order:
 6. `syncTrace_.endBlock()`, then advance `ppq_` only if transport is running (so the protocol nodes'
    `walkTicks` and the cores see the same block-start ppq).
 
-Live config edits land in `Engine::applyConfigField`, dispatched by the core's concrete type and
-**value-guarded per field** so a whole-config re-send only acts on what changed (a `Model` change
-triggers `restartEmulator()` in place, and the cable needs no rewiring because `linkPeers_` holds
-the systems, which survive it; an unchanged one no-ops, avoiding a spurious restart that would nuke
-a restored savestate). It applies at drain against the plan current at that point, finding the core
-by id in that plan's `SystemTable`. `Engine::setSampleRate` re-rates the running plan's cores in slot
-order, then the parked ones, then re-activates the graph's buffers at the new rate;
+A system's live knobs never ride a host command: they are its system node's settings and params (below),
+which the node gives its core at the start of a block. `Engine::setSampleRate` re-rates the running plan's
+cores in slot order, then the parked ones, then re-activates the graph's buffers at the new rate;
 `Engine::setMaxBlockFrames` grows the plan's block size to a host's maximum (never below 8192, the
 largest block the parity matrix holds), and the plugin and the SDL standalone call it before handing
 the engine over.
@@ -355,8 +352,8 @@ queues into a list nothing reads); mGB and LSDj hear MIDI through `serialIn`. Th
 `serialOut` output (bytes): after `finishBlock`, while it is connected, the node copies the bytes its
 core captured on the serial port this block, each a message at the frame the core completed it, a byte
 completed past the block's end at its last frame (the core clears its log at its next
-`prepareForBlock`). Capture runs only while armed (the build-time `serialOutCapture`, or
-`setSerialOutCapture`; TS arms it for LSDj's two MIDI-out modes), in one of two modes
+`prepareForBlock`). Capture runs only while armed, by the node's `serialOutCapture` setting (below; TS
+sets it for LSDj's two MIDI-out modes and `off` otherwise), in one of two modes
 (`SerialOutCapture`): `listen` takes the bytes the Game Boy clocks out itself (LSDj as master sync's
 master), and `drive` also clocks a transfer waiting on an external clock, a bit an audio frame, taking a
 byte every 8 frames while the Game Boy holds the port for one, idle bytes included (the Arduinoboy's part
@@ -386,35 +383,91 @@ atomic and the GraphHost's, `GraphHost::reclaim` names a node the first time its
 `Engine::droppedMessages()` (the harness RPC `droppedMessages`) reads the total, which the parity rig and
 the native goldens run hold at zero.
 
+### The system nodes' settings and params
+
+A system's live knobs are its node's ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp),
+[modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)): each is either a **setting**, in
+the document, which an edit publishes, or a **param**, its value in the document and its live edits through
+`setParam` (above). A param is what is worth playing, automating or modulating, so it is also a control input a
+wire could drive; everything else, display knobs, accuracy switches and latencies among them, is a setting.
+
+| Node | Settings | Params |
+|---|---|---|
+| `rp.sameboy.gb` | `core`, `model`, `fastBoot`, `highpass`, `colorCorrection`, `dmgPalette`, `lightTemperature`, `serialOutCapture` | `gain` |
+| `rp.mesen.nes` | `core`, `region`, `removeSpriteLimit`, `apuLatencyMs`, `s5bNoise`, `mmc5PhaseReset` | `gain`, `expansionVolume` |
+| `rp.mesen.gba`, `rp.mesen.sms` | `core` | `gain` |
+
+- **Values.** `core` is the id `buildCore` parked the core under. Every knob is optional: absent, the node leaves
+  the core as it was built, or as the last document that carried the field left it. An enum is a label as the role
+  configs spell it ([settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)), which is its C++ enumerator's
+  name with the first letter lowercased (`SameBoyModel::CgbC` is `"cgbC"`, `SameBoyHighpass::RemoveDcOffset` is
+  `"removeDcOffset"`): `knobs::Label<E>`, an `rfl::Literal` generated at compile time from reflect-cpp's
+  enumerator list, reads exactly those strings, lists them in the descriptor's schema and maps one back to its
+  enumerator by position, so native has one spelling of each label and a document and a saved `.rplg` carry the
+  same word. An ordinal, another spelling, or a number out of its range (`lightTemperature` -1 to 1,
+  `apuLatencyMs` 0.25 to 6, NaN included) refuses the settings, and with them the document, never clamped. A field
+  the type does not know is ignored (the settings reader takes extra fields), which is why
+  `BuildGraphGoldens.test.cpp` holds every golden document's system nodes to the fields their types' schemas list.
+  `serialOutCapture` is `off`, `listen` (master sync) or `drive` (MI.OUT). `gain` is dB from -90 to 12, default 0;
+  `expansionVolume` an int param, whole percent of unity from 0 to 200, default 100, as the core takes it.
+- **Only a new core rebuilds.** The node keys its rebuild on `core` (node.hpp's `rebuild_key`: prepare compares
+  the new settings' key with the key the instance was created with, which no commit writes) and declares
+  `stateless_prepare`, so every other settings change is a commit that never makes `setGraph` wait for the
+  audio thread.
+- **When a setting reaches the core.** `on_settings` only points the node at the committed settings (it never
+  allocates). At its first activation the node takes its core and gives it every knob present, before that core
+  first runs; after that it gives it what moved (present, and absent from or different in what it gave last) at
+  the start of its next block, in `prepare`, before its inputs and `prepareForBlock`, which a linked Game Boy's
+  coupled group runs for every member before any of them steps. Each goes through the core's own value-guarded
+  setter, so a palette change reapplies the display group and restarts nothing, a model change restarts the core
+  (`restartEmulator`; the cable needs no rewiring because `linkPeers_` holds the systems, which survive it) and a
+  region change resets it. A Game Boy gives `fastBoot` first, which waits for the next restart and so is what a
+  model's restart boots with, then the rest in the role config's order, the capture last. The capture lands at
+  the start of the first block of the plan that carries it, the block `rp.lsdj.serial-out` first runs in its new
+  mode, so no byte captured in one mode is decoded in the other.
+- **When a param reaches the core.** Each param's latest value in the block is given after the settings and
+  before the inputs, value-guarded against the value the core was given last, so a publish carrying the value the
+  core already has calls nothing; a change anywhere in a block lands at its start, and the core smooths gain as
+  it does every gain change. A node reads its params only in a block (the host binds its control streams there,
+  landing the document's values in them as it adopts the plan), so they reach the core at the node's first block,
+  before its first `prepareForBlock`, not in `activate`: a core its node has taken runs nothing before that, so
+  its first sample plays at the document's level. A document without a param leaves its stream where it was: at
+  its default, on a new node.
+- **Allocation.** A Game Boy's knobs and every node's params allocate nothing on the block thread. A NES's settings
+  can: its core allocates inside Mesen in its own blocks, and its sprite-limit and accuracy setters do too,
+  whatever path calls them.
+- **A refused document** can still have given knobs to a core one of its nodes took, since activation runs while
+  it plans: the core goes back to parked with them, for the next document, which in the app carries the same.
+
 ### Graph requests
 
 Structure arrives as a graph document TS builds
 ([`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts),
 [modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)), in two calls:
 
-- **`buildCore(spec)`** builds and activates a core on the control thread (`factory.build`), arms a Game
-  Boy's serial-out capture if the spec asks, and parks it under its system id with `Engine::parkCore`,
-  which claims its snapshot slot (after checking the id is not live: the registry finds a slot by id, first
-  match). It answers with the node type the core is placed as and the two values the graph needs as the
-  core decoded them (a NES's export mode, a Game Boy's link group), or null. It refuses id 0 (the
-  registry's free-slot id) and an id that is still live: parked, or held by a node of any plan not yet
-  reclaimed (it reclaims first, so with no audio thread running every removed core is already gone).
+- **`buildCore(spec)`** builds and activates a core on the control thread (`factory.build`) and parks it
+  under its system id with `Engine::parkCore`, which claims its snapshot slot (after checking the id is
+  not live: the registry finds a slot by id, first match). It answers with the node type the core is
+  placed as and the two values the graph needs as the core decoded them (a NES's export mode, a Game Boy's
+  link group), or null. It refuses id 0 (the registry's free-slot id) and an id that is still live:
+  parked, or held by a node of any plan not yet reclaimed (it reclaims first, so with no audio thread
+  running every removed core is already gone).
 - **`setGraph({ document, slots, tap? })`** (`Engine::setGraph`) first reclaims what the audio thread
   handed back (the GraphHost's plans, the script host's released instances), then checks that the
-  document's system nodes and the slots name the same systems, each node `sys-<id>` with settings
-  `{ "core": <id> }`, of the node type its core is placed as, that a tap arm names one of the slots and
-  a mode from 1 to 3, that there is at most one `rp.host.midi-in` and one `rp.host.controller-in`, and at
-  most one `rp.n8.cart-mirror`, naming one of the slots' NES systems (any of these refuses the document
-  before anything is built); builds the plan's `SystemTable` (a slot-ordered list of
-  `{ id, SystemBase*, MesenNesSystem* }`, the NES pointer for the channel tap, plus the system the cart mirrors,
+  document's system nodes and the slots name the same systems, each node `sys-<id>` with settings naming
+  its core (`"core": <id>`, beside its knobs, which the node reads), of the node type its core is placed
+  as, that a tap arm names one of the slots and a mode from 1 to 3, that there is at most one
+  `rp.host.midi-in` and one `rp.host.controller-in`, and at most one `rp.n8.cart-mirror`, naming one of
+  the slots' NES systems (any of these refuses the document before anything is built); builds the plan's
+  `SystemTable` (a slot-ordered list of `{ id, SystemBase*, MesenNesSystem* }`, the NES pointer for the
+  channel tap, plus the system the cart mirrors,
   [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores;
   turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
   every node's layout from the document (`layout_for` on each node type), runs every layout check, and
-  only then creates and activates the new instances. The table rides with the plan, so the audio
-  thread's lookups always match the plan that is running. Once a document has published, a core still
-  parked is one no document placed, and is freed with its snapshot slot. A refused document changes
-  nothing: a core a node of the refused plan had taken goes back to parked, for a corrected document to
-  place.
+  only then creates and activates the new instances. The table rides with the plan, so the audio thread's
+  lookups always match the plan that is running. Once a document has published, a core still parked is one
+  no document placed, and is freed with its snapshot slot. A refused document changes nothing: a core a
+  node of the refused plan had taken goes back to parked, for a corrected document to place.
 
 The document `buildGraph` writes:
 
@@ -478,13 +531,26 @@ The document `buildGraph` writes:
 - **Cores.** A node takes its core on first activation through the host service `rp.host.cores/1`
   ([HostServices.hpp](../packages/native/src/host/graph/HostServices.hpp)), by the id its settings name,
   and owns it from then on.
+- **The system nodes' knobs** (`systemKnobs` in `buildGraph.ts`, the one table of which core-config field is a
+  setting and which a param, which the store reads too). A system node's settings carry `core`, the setting fields
+  of its system's core-config role (the role whose kind is its core, `sameboy` or `mesen`) as that config spells
+  them, and on a Game Boy `serialOutCapture`, for the mode its `lsdj-sync` node actually runs, after the MI.MAP
+  controller's override (`serialOutCaptureFor`,
+  [graph/controller.ts](../packages/retroplug/src/graph/controller.ts)): `drive` for MI.OUT, `listen` for master
+  sync, and `off` otherwise, a Game Boy with no `lsdj-sync` placed included, since a capture left out would leave an
+  armed one armed. Its params carry the system's `gain` and a NES's `expansionVolume`. A setting value the node
+  would refuse (another label, a number out of range, another type) is left out rather than sent, the core keeping
+  what it has: the planner refuses a whole document for one node's settings, and a loaded project keeps its config
+  as written. A param passes any finite number, which the planner clamps. A system with no core config (a
+  `DirectGraph`'s until `setCoreConfig`, the B1 translator goldens' models) carries `core` and a Game Boy's capture
+  alone.
 
 ### `QueuedInvoker`
 
 [`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp) (in the files
 `EngineInvoker.{hpp,cpp}`) is **the one command path; there is no Direct/Queued fork**. Structure
-arrives as graph requests (above). Every other edit packs into a POD
-[`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes onto the GraphHost's
+arrives as graph requests (above), and a param's value through `setParam`. Every other edit packs into a
+POD [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes onto the GraphHost's
 one `SpscRing<Command, 256>` as an opaque host command, behind any plan already there. The single
 control-thread bit `audioThreadOwns_` (forwarded to `GraphHost::setAudioThreadOwns`) decides who
 drains: while the audio thread owns the `Engine` it drains each block and the producer just pushes;
@@ -502,7 +568,6 @@ cleanly.
 | `StageControllerMidi` | `uint8 data[4]`, `len` | inline only; a message over 4 bytes is refused before it is pushed |
 | `StageSystemMidi` | `id`, `data[4]` / `len` or `ext` | as `StageMidi`, delivered at drain to one system's ingress (and to the cart when the plan's cart mirror names that system), never through the routing node |
 | `SetBpm` / `SetTransport` / `SetPpq` | `double` / `bool` / `double` | inline |
-| `SetConfigField` | `id`, `field`, `value` | inline |
 | `PressButton` | `id`, `button`, `down` | inline |
 | `WriteRam` | `id`, `offset`, `vector<uint8>* bytes` | owning |
 | `SyncTap` | `arm`, `mode`, `id` | inline; rides in the plan's swap command as a trailer for every `setGraph` request carrying a `tap` |
@@ -516,9 +581,8 @@ export mode (either pin split 1, `ChannelSplit` 3, as `buildGraph` decides for a
 single NES), recording what it replaced. TS sends it on every edit that builds or drops a core and on
 every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
 it rides the plan's swap, so no block runs between the two. `armedTap_` holds the core pointer as well
-as the id, so a core that has gone is never restored onto another. The `LinkGroup` config field is the
-one field that is also structure: the invoker pushes it to the core, and the store republishes the
-graph with the cable.
+as the id, so a core that has gone is never restored onto another. A param's value from `setParam` rides
+the same ring as a command of the GraphHost's own, not a `DspCommand`, into its param's control stream.
 
 **Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
 freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`):
@@ -627,8 +691,9 @@ The cores are built **bare** — no feature behaviour is baked into C++. The DSP
 folding the overrides into the ROM in memory at construct, with no audio-thread part. See 04. The system-role config
 schema (`SameBoyRoleConfig` field names) mirrors TS's
 `coreRoles.ts`; [`decodeSameBoyRoleConfig`](../packages/native/src/system/sameboy/SameBoyBackend.cpp#L19)
-reads with `rfl::DefaultIfMissing` (forward-tolerant) and is the single JSON decode point,
-shared by live `applyRoleConfig` and the construct blob. The role model is 04's topic.
+reads with `rfl::DefaultIfMissing` (forward-tolerant) and is the single JSON decode point, for the
+construct blob alone: a live edit reaches the core as its system node's settings (above). The role model is
+04's topic.
 
 ### `MesenBackend`
 
@@ -862,7 +927,7 @@ its **runtime** classes:
 | Shared class | Host use |
 |---|---|
 | [`SystemBase`](../packages/native/src/system/SystemBase.hpp) | the abstract core; owned by its system node in the running plan (`SystemNode<T>`, [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)); drives `onActivate`/`onProcess`/`framebuffer`/`saveStateBytes`/`readStateSnapshot`/`stateRegions`/`enableStateSnapshot`/`pressButton`/`pushSerialIn` |
-| [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; the live-apply `dynamic_cast` target for `applyConfigField`; its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
+| [`SameBoySystem`](../packages/native/src/system/sameboy/SameBoySystem.hpp) | the GB core; its node gives it its knobs through value-guarded setters (`setModel`, `setHighpass`, the display group, `setSerialOutCapture`); its `linkPeers_` are the serial cable, set by its node's `rp.link/1` `set_peers` (the other members of its coupled group, in member order) |
 | [`MesenNesSystem` / `MesenGbaSystem` / `MesenSmsSystem`](../packages/native/src/system/mesen) | the NES, GBA and SMS/GG cores; the NES is the `SyncTap` target |
 | the LSDj kit/sample pipeline (`KitCompiler` / `KitUtil` / `SampleCache` / `Effects`), `util/MinizZip`, `EmbeddedRoms` | backing kit patching, `zip`/`unzip`, and embedded-ROM resolution |
 | `SpscRing` / `FrameBufferTriple` / `MemorySnapshotTriple` | the transport **primitives** the command/release rings and the snapshot registry are built on |
@@ -917,9 +982,9 @@ is one automatable parameter applied post-render across all 8 channels
 
 - [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) — mounts the capability facets onto a server; the single source of truth for the wire surface.
 - [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
-- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, `loadScriptPack`, the command handler, `SyncTap`.
+- [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph (with its script host), the staged host MIDI, the host outputs and the registry; `processBlock`, `setGraph`, `setParam`, `loadScriptPack`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one command path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
-- [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host I/O nodes and the cart mirror), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
+- [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes, with their settings and params), [`HostIoNodes.cpp`](../packages/native/src/host/graph/HostIoNodes.cpp) (the host I/O nodes and the cart mirror), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
 - [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
 - [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
 - [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 53205c6b..8a0d573e 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -48,7 +48,7 @@ The methods, grouped by concern:
 | **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
 | **Emulator lifecycle** | `buildCore(spec, id)` · `setGraph(request)` · `loadScriptPack(request)` | Build/activate a core under a **TS-allocated id** and park it; plan a graph document TS built (which cores run, in what order and wired how). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over the first two. Load a pack of script node types beside the built-in ones, or reload one, while audio runs ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): `{ ok, error, errors, types }`, the pack's types with their descriptors, or why not (the planner's `errors` when a reload's replan refused). Nothing in the app calls it yet, and the mock refuses it |
 | **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path (the one async method) |
-| **Live config** | `applySystemSetting(id,key,value)` · `applyRoleConfig(id,kind,config)` | Apply the two universal settings + a **system-role**'s knobs to the live core (the audio routing is the graph's wiring, not a call) |
+| **Live params** | `setParam(nodeId,paramId,value)` | Set a param of a node the last published plan places, at the start of the next block, clamped to its range: the store's live gain and a NES's expansion volume, on the system's node `sys-<id>`. A system's other knobs are its node's settings in the document `setGraph` plans, and the audio routing is the graph's wiring, so neither is a call ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)) |
 | **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
 | **Live reads** | `readState(id)` · `readSram(id)` · `getFrame(id)` | Pull the latest tear-free snapshot from the `SnapshotRegistry` (see doc 01) — never touches a live core |
 | **Byte codecs** | `zip(entries)` · `unzip(bytes)` | miniz deflate/inflate — the only native part of `.rplg` framing (TS assembles every entry) |
@@ -68,7 +68,7 @@ set of RPC facets is bound behind `__rpcSend` (see doc 01/02).
 ### `ConstructSpec` — the resolved-paths-only build request
 
 `buildCore` takes a `ConstructSpec`
-([backend.ts:800](../packages/retroplug/src/backend.ts#L800)) in which **TS has
+([backend.ts:788](../packages/retroplug/src/backend.ts#L788)) in which **TS has
 already resolved every path, suffix, sibling, and classification** — *native never sees a
 suffix, never looks for a sibling, never classifies.*
 
@@ -77,9 +77,8 @@ suffix, never looks for a sibling, never classifies.*
 | `romPath` / `embeddedRom` | The ROM file to slurp, or a binary-baked marker (e.g. `"mgb"`) with `romPath = ""` |
 | `platform` / `core` | What the ROM targets + which factory builds it — TS classifies via [`platform.ts`](../packages/retroplug/src/platform.ts) |
 | `savPath` / `statePath` | The exact battery file (load-from + auto-save-to) and savestate to boot from, or `null` |
-| `serialOutCapture?` | Arm a Game Boy's serial-out capture before the core first runs: `"drive"` (LSDj MI.OUT) or `"listen"` (master sync) |
 | `sramBytes?` / `stateBytes?` | Seed bytes (zip-import blob, carried battery, role-synthesized sav) that override what native reads from disk; `savPath` stays the auto-save target |
-| `settings?` | The system-role config as JSON, applied **at construct** so a restored savestate isn't nuked by a post-build restart |
+| `settings?` | The system-role config as JSON, applied **at construct** so a restored savestate isn't nuked by a post-build restart: the construct-time fields reach the core only this way, and its knobs again from its node's settings in the document that places it (`roleConfigForNative` encodes it, its enums as ordinals) |
 
 `openFileBrowser` is the odd one out and does **not** ride the RPC bridge: it uses the
 UI-direct hooks `__rp_openFileBrowser` / `__rp_onFileBrowserResult` on the shared context
@@ -109,39 +108,48 @@ The live systems list. `SystemEntry[]`
 model (`{ id, platform, core, romPath, savPath override, savSuffix, embeddedRom, settings,
 roles }`, plus the derived `battery` and `built` facts, never serialized); the private state is
 `entries`, `focusedId`, and `dirty`
-([systemsStore.ts:117-119](../packages/retroplug/src/systemsStore.ts#L117)). `view()`
-([systemsStore.ts:139](../packages/retroplug/src/systemsStore.ts#L139)) projects a
+([systemsStore.ts:119-121](../packages/retroplug/src/systemsStore.ts#L119)). `view()`
+([systemsStore.ts:141](../packages/retroplug/src/systemsStore.ts#L141)) projects a
 `SystemView[]` for the UI, computing the live `focused` and `missing` flags each call.
 
 - **TS owns the system-id counter.** `allocSystemId()` increments a module-scoped
-  `nextSystemId` ([systemsStore.ts:68](../packages/retroplug/src/systemsStore.ts#L68)):
+  `nextSystemId` ([systemsStore.ts:70](../packages/retroplug/src/systemsStore.ts#L70)):
   *"native never allocates."* Ids are opaque handles, one id space per control-plane JS
   context (1:1 with the native `Engine`), and start at **1** because the snapshot registry
   uses 0 as its free-slot sentinel.
-- **`classifyRom`** ([systemsStore.ts:87](../packages/retroplug/src/systemsStore.ts#L87))
+- **`classifyRom`** ([systemsStore.ts:89](../packages/retroplug/src/systemsStore.ts#L89))
   is the one place ROM bytes enter TS — and just the `ROM_SNIFF_LEN` (0x134) header prefix.
-- **Mutators through `Backend`:** `addSystem` (append), `loadRom` (replace focused, or defer
-  to a sibling `<rom>.rplg`), `loadMgb` (embedded), `replaceSystem`, `removeSystem`, plus the
-  per-system `setGain` / `setReloadOnRomChange` (→ `applySystemSetting`) and `setRoleConfig`
-  ([systemsStore.ts:536](../packages/retroplug/src/systemsStore.ts#L536)), where
-  **only a `"system"`-category role's config crosses to the live core** (`applyRoleConfig`);
-  a feature-role's config stays pure TS, but for `lsdj-sync`, whose mode sets the Game Boy's serial-out
-  capture (`setSerialOutCapture`: `"drive"` for MI.OUT, `"listen"` for master sync, null otherwise).
-- **`duplicateSystem`** ([systemsStore.ts:298](../packages/retroplug/src/systemsStore.ts#L298))
+- **Mutators through `Backend`:** `addSystem` (append), `loadRom` (replace focused, or defer to a sibling
+  `<rom>.rplg`), `loadMgb` (embedded), `replaceSystem`, `removeSystem`, plus the per-system `setGain`,
+  `setReloadOnRomChange` and `setRoleConfig`
+  ([systemsStore.ts:550](../packages/retroplug/src/systemsStore.ts#L550)). A gain edit updates the store and
+  sets the system node's `gain` param live (`backend.setParam("sys-<id>", "gain", dB)`) with no publish; the
+  next document, whatever publishes it, carries the same value. `reloadOnRomChange` is TS's alone (the file
+  watcher reads it), so it reaches no backend. An edit of the core-config role (the `"system"`-category role
+  whose kind is the core) is diffed through `systemKnobs`
+  ([graph/buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts), the one table of which field is a
+  setting and which a param): a moved setting field publishes, the node's settings carrying it; a moved
+  param alone (a NES's `expansionVolume`) is set live with `setParam`; and a construct-time field (the FIFO
+  and its overrides, `sdRoot`, the N8 link, `channelExportMode`, SMS `enableFm`) reaches nothing until the
+  `reset(id)` or reload that rebuilds the core from it. A feature-role's config reaches only its protocol
+  node's settings, and for `lsdj-sync` also its Game Boy node's `serialOutCapture` (`"drive"` for MI.OUT,
+  `"listen"` for master sync, `"off"` otherwise), both in the document its edit publishes.
+- **`duplicateSystem`** ([systemsStore.ts:295](../packages/retroplug/src/systemsStore.ts#L295))
   is TS orchestration with **no native duplicate method**: it pulls `readState(id)` (state
   includes SRAM), then `buildCore` seeded with those `stateBytes` under a fresh
   `allocSystemId()`, and appends.
-- **The graph.** Every edit that changes what runs, or what a system's protocol nodes are told, ends by
-  publishing the engine's graph once (`setOnGraphChange` → `ProjectStore.publishGraph` →
-  `EngineGraph.publish`, [engineGraph.ts](../packages/retroplug/src/engineGraph.ts)): the whole
+- **The graph.** Every edit that changes what runs, what a system's protocol nodes are told, or a setting of
+  a system's node, ends by publishing the engine's graph once (`setOnGraphChange` → `ProjectStore.publishGraph`
+  → `EngineGraph.publish`, [engineGraph.ts](../packages/retroplug/src/engineGraph.ts)): the whole
   document, built by [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) from the entries in
-  list order, each entry's `built` facts (what `buildCore` reported, with its live link group) and its
+  list order, each entry's `built` facts (what `buildCore` reported, with its live link group), its
   pipeline (`graphSystems`: its roles that have a protocol node, each with the node type it is placed as
   and its parsed config, by [`graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts)'s
-  table, with or without a registry), the project's audio and MIDI routing and its controller, with a NES
-  tap re-arm when the edit built or dropped a core. So a `setRoleConfig` on a role with a protocol node
-  publishes, one on a role with none (a model, a palette, an asset override) changes no document, and one
-  on a system role publishes only when it moves a Game Boy's link group. A
+  table, with or without a registry), its core-config role's config and its gain (its system node's settings
+  and params), the project's audio and MIDI routing and its controller, with a NES tap re-arm when the edit
+  built or dropped a core. So a `setRoleConfig` on a role with a protocol node publishes, one on the
+  core-config role publishes when it moves a setting field (a model, a palette) or a Game Boy's link group, and
+  one on a role with neither (an asset override) changes no document. A
   `SystemsStore` used without a `ProjectStore` publishes its systems itself, at the default stereo
   routing and with no MIDI routing or controller (so no MIDI is routed). An unchanged document is not sent unless it
   re-arms or the last one did not plan; a refused one leaves the store's model as it is, and is logged
@@ -151,7 +159,7 @@ roles }`, plus the derived `battery` and `built` facts, never serialized); the p
 
 The control plane **reconstructs** a core rather than live-injecting into it (no `GB_reset`, no
 in-place SRAM clear). The private `rebuildInPlace(id, seed)`
-([systemsStore.ts:371](../packages/retroplug/src/systemsStore.ts#L371)) is the shared
+([systemsStore.ts:368](../packages/retroplug/src/systemsStore.ts#L368)) is the shared
 body: it captures the source spec, builds a fresh core under a **new id** that takes the old
 one's slot, and preserves identity + focus
 (`replaceById(entries, id, {...src, id: newId})`; `if (focusedId === id) focusedId = newId`).
@@ -172,7 +180,7 @@ load isn't a user edit): `clear()` tears every system down and, when it had any,
 empty graph;
 `adopt(config, blobs?)` (which publishes the graph that places it, except inside a project load,
 which publishes once its systems are all built)
-([systemsStore.ts:616](../packages/retroplug/src/systemsStore.ts#L616)) reconstructs
+([systemsStore.ts:623](../packages/retroplug/src/systemsStore.ts#L623)) reconstructs
 one system preserving its **exact** `savSuffix`/`savPath`, with `blobs` seeding SRAM/state on
 a zip import. Default roles are derived before the build from a `ROLE_HEADER_LEN` (0x150)
 header prefix via `defaultRoles`, and each role's `onConstruct` hook folds over the spec
@@ -246,8 +254,11 @@ records nothing until it's opened.
 (`{ systems, routing, shape, midiRouting?, controller? }`) to the document `setGraph` plans
 ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md) section 2.1; the node
 order, the routing as wiring and the host shapes are in [02-native-host.md](02-native-host.md)).
-Besides the system nodes and their mixing it places the script nodes: those that feed a system between the
-host's inputs and the systems, and those that read one after the systems, before the host's outputs
+Besides the system nodes, each with its core's knobs as its settings and params (`systemKnobs`, and a Game
+Boy's serial-out capture for the mode its LSDj's nodes run, the controller's forced one included:
+[modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md) section 5), and their mixing, it
+places the script nodes: those that feed a system between the host's inputs and the systems, and those that read
+one after the systems, before the host's outputs
 ([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md) section 4,
 [lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md) section 3,
 [04-roles-protocol-nodes.md](04-roles-protocol-nodes.md)):
@@ -281,7 +292,12 @@ appends, replace keeps the slot, remove shifts later slots down), a system it ad
 roles, and what the script nodes are told is three calls of its own, each a publish: `setPipeline(id,
 roles)` (each role with a protocol node placed as one, its config as given), `setMidiRouting(mode)` and
 `setController(controller)` (its app config parsed by the app's schema, as the project's is). Until
-`setMidiRouting` names a mode, no host MIDI is routed to any system.
+`setMidiRouting` names a mode, no host MIDI is routed to any system. A system it adds has no core config
+either, so its node carries no knob and its core runs as it was built, until `setCoreConfig(id, partial)`,
+a publish, merges fields over what it gave before, in the labels the store's role spells them (a Game Boy's
+`linkGroupId` its link cable), and refuses, changing nothing, any field its node does not take (an ordinal, a
+construct-time field, another core's); `setParam(id, param, value)` sets a `gain` or an `expansionVolume`
+live, as the store does, and keeps it for the next document.
 
 ### RecentStore — [`src/recentStore.ts`](../packages/retroplug/src/recentStore.ts)
 
@@ -592,13 +608,14 @@ processed, so opening a menu mid-hold can't strand a key down. Pinned by
 
 Two concepts touched throughout this doc belong fully to **doc 04**:
 
-- **Roles.** A `RoleInstance = { kind, config }` is what serializes on a `SystemEntry`.
-  A **system-role** (e.g. `"sameboy"`: model/highpass/linkGroup/fastBoot) is the *only*
-  role config that crosses to the live core (via `applyRoleConfig`). **Feature-roles**
-  (LSDj-sync, mGB, the asset overrides) are pure TS whose config **never** gets a C++
-  struct. The control-plane `RoleRegistry` (built by `buildAppRegistry`) attaches feature
-  roles to a system by ROM identity ([`romProviders.ts`](../packages/retroplug/src/romProviders.ts),
-  the TS replacement for the legacy build's native `RomSniffer`, which is gone).
+- **Roles.** A `RoleInstance = { kind, config }` is what serializes on a `SystemEntry`. A **system-role**
+  (e.g. `"sameboy"`: model/highpass/linkGroup/fastBoot) is the *only* role config that reaches the live
+  core: its live fields as its system node's settings and params in the published document, its
+  construct-time ones in the build spec. **Feature-roles** (LSDj-sync, mGB, the asset overrides) are pure TS
+  whose config **never** gets a C++ struct (an `lsdj-sync`'s mode decides its Game Boy node's capture, which
+  does). The control-plane `RoleRegistry` (built by `buildAppRegistry`) attaches feature roles to a system
+  by ROM identity ([`romProviders.ts`](../packages/retroplug/src/romProviders.ts), the TS replacement for
+  the legacy build's native `RomSniffer`, which is gone).
 - **The protocol nodes.** A role whose work is the audio thread's runs as a script node of its own
   ([`src/script/nodes/`](../packages/retroplug/src/script/nodes)), which `buildGraph` places for each
   system that has the role ([`graph/protocolNodes.ts`](../packages/retroplug/src/graph/protocolNodes.ts)),
diff --git a/spec/04-roles-protocol-nodes.md b/spec/04-roles-protocol-nodes.md
index c8c7a718..469dd3b9 100644
--- a/spec/04-roles-protocol-nodes.md
+++ b/spec/04-roles-protocol-nodes.md
@@ -29,12 +29,14 @@ re-explaining them. For the C++ script host the nodes run in, see [02-native-hos
 | Attached by | backend/core kind | ROM identity (a `RomProvider`) |
 | `category` | `"system"` | `"feature"` |
 | Examples today | `sameboy`, `mesen` | `mgb`, `lsdj-sync`, `risa-sync`, `lsdj-assets` |
-| Config reaches the live core? | **yes**: the only role config that crosses, via `applyRoleConfig` | **no**: its config never gets a C++ struct |
-| Where behaviour lives | inside the core (native applies the fields) | its protocol node, when it has one; otherwise the control plane (an asset override is folded into the ROM there) |
+| Config reaches the live core? | **yes**: the only role config that does, its live fields as its system node's settings and params | **no**: its config never gets a C++ struct |
+| Where behaviour lives | inside the core (its system node applies the fields) | its protocol node, when it has one; otherwise the control plane (an asset override is folded into the ROM there) |
 
-The dividing line is load-bearing: **only a system-role's config crosses the RPC boundary into the
-live emulator**. A feature-role's config is pure TS: for a role with a protocol node it travels only as
-that node's settings in the graph document the project publishes (below).
+The dividing line is load-bearing: **only a system-role's config reaches the live emulator**, as its
+system node's knobs in the graph document the project publishes and, for the fields read at construct, the
+build spec. A feature-role's config is pure TS: for a role with a protocol node it travels only as that
+node's settings in the same document (below), and for `lsdj-sync` it also decides one knob of its Game Boy's
+node, the serial-out capture its decoder needs.
 
 ## The role model
 
@@ -126,9 +128,9 @@ ROM in memory the same way.
 [SystemsStore](../packages/retroplug/src/systemsStore.ts) drives the registry:
 `defaultRoles(...)` reads a `ROLE_HEADER_LEN = 0x150` header prefix and calls
 `registry.defaultRoles(...)`
-([systemsStore.ts:769](../packages/retroplug/src/systemsStore.ts#L769)); `applyConstructHooks`
+([systemsStore.ts:789](../packages/retroplug/src/systemsStore.ts#L789)); `applyConstructHooks`
 folds each attached role's `onConstruct` over the spec before the build call
-([systemsStore.ts:951](../packages/retroplug/src/systemsStore.ts#L951)). See
+([systemsStore.ts:967](../packages/retroplug/src/systemsStore.ts#L967)). See
 [03-ts-layer.md](03-ts-layer.md) for the surrounding store idioms.
 
 ## The built-in roles
@@ -138,23 +140,26 @@ takes `registerCoreRoles`, `registerDspRoles`, the asset and marker roles (`lsdj
 `risa-assets`, `bliptoaster`, `bliptoaster-assets`) and `registerRomProviders`.
 
 **System roles** ([coreRoles.ts](../packages/retroplug/src/coreRoles.ts)), keyed by core
-value, ranges mirroring the native enums:
+value, each enum a label mirroring a native enum ([settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)),
+which is what a `.rplg` saves and what the system node reads:
 
 | Role | Config (zod, defaults) |
 |---|---|
-| `sameboy` | `model` 0-13 (CgbC=9), `highpass` 0-2, `linkGroupId` 0-255, `fastBoot` bool (true), plus the display group (`colorCorrection`, `dmgPalette`, `lightTemperature`) |
+| `sameboy` | `model` (`"cgbC"`), `highpass` (`"accurate"`), `linkGroupId` 0-255, `fastBoot` bool (true), plus the display group (`colorCorrection`, `dmgPalette`, `lightTemperature`) |
 | `mesen` | one role for every Mesen platform, so the knobs are per-platform and the menu gates each group. NES: `region`, `removeSpriteLimit`, `apuLatencyMs`, the cartridge-accuracy pair (`s5bNoise` / `mmc5PhaseReset`, both defaulting to `"n8"`), `expansionVolume` (the cartridge sound chip's level, percent of unity: the same value the UI rescales into a connected N8's `master_vol`), `channelExportMode`, `sdRoot`. SMS/GG: `enableFm` |
 
-Not every system-role field is live. `region`, `channelExportMode` and `sdRoot` are read when the core
-is **constructed**, so an edit takes effect only after a rebuild: `setRoleConfig(id, …)` then
-`reset(id)`, which re-sends the stored role config as the construct `settings` blob and hands back a
-new id. `sdRoot` is the host directory the emulated EverDrive N8 SD card maps to (`"/"` on the card),
-for a NES ROM that pulls its data off the cartridge; empty means a per-process scratch directory, not
-the working directory. Reading back what such a ROM sends is `drainCoreBytes` ([09](09-cli-debugging.md)).
-A ROM can also skip the CPU entirely: `CMD_F_FRD_MEM` has the emulated MCU read the open file straight
-into cartridge memory over the PI bus, which `MesenNesSystem` backs for the CHR-RAM window
-(PI `0xC00000`) only. That transfer's duration is modelled from the rate measured on real hardware,
-so the ROM's wait loop spins about as long here as it does on a console.
+Not every system-role field is live. The live ones are the system node's knobs (below);
+`channelExportMode`, `sdRoot`, the FIFO profile and its two overrides, the N8 link and its round trip, and
+SMS `enableFm` are read only when the core is **constructed**, so an edit takes effect only after a
+rebuild: `setRoleConfig(id, …)` then `reset(id)`, which re-sends the stored role config as the construct
+`settings` blob and hands back a new id. (`region` is read at construct too, and is also a knob: a live
+edit resets the core, as switching a console's region would.) `sdRoot` is the host directory the emulated
+EverDrive N8 SD card maps to (`"/"` on the card), for a NES ROM that pulls its data off the cartridge;
+empty means a per-process scratch directory, not the working directory. Reading back what such a ROM sends
+is `drainCoreBytes` ([09](09-cli-debugging.md)). A ROM can also skip the CPU entirely: `CMD_F_FRD_MEM` has
+the emulated MCU read the open file straight into cartridge memory over the PI bus, which `MesenNesSystem`
+backs for the CHR-RAM window (PI `0xC00000`) only. That transfer's duration is modelled from the rate
+measured on real hardware, so the ROM's wait loop spins about as long here as it does on a console.
 
 `fifo` (plus the `fifoDepth` / `fifoBytesPerSecond` overrides) says how faithfully that FIFO behaves
 like the cartridge's. It defaults to `"instant"` (unbounded and delivered immediately, which is what
@@ -183,21 +188,28 @@ project's settings, as it places a protocol node for a role.
 
 ## Which config reaches the live core
 
-Two channels cross the RPC boundary to a running emulator, and no feature-role config uses either:
+A running emulator hears a config edit one of two ways, both the graph's: its system node's settings, in the
+next document the project publishes, or a param of that node, set live with `setParam`
+([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md); how the node gives them to its
+core is [02-native-host.md](02-native-host.md)'s):
 
-| Change | RPC | Reaches |
+| Change | Path | Reaches |
 |---|---|---|
-| the two universal settings (`gainDb`, `reloadOnRomChange`) | `applySystemSetting` | the live core (see [03-ts-layer.md](03-ts-layer.md)) |
-| a **system-role** config edit | `applyRoleConfig(id, kind, config)` | the live core (`sameboy` + `mesen`), minus the construct-time fields noted above |
-| a **feature-role** config edit | *(none to the core)* | only its protocol node, as its settings in the next document the project publishes |
-
-[`SystemsStore.setRoleConfig`](../packages/retroplug/src/systemsStore.ts#L526) merges the
-partial, re-parses through the role schema, and calls `backend.applyRoleConfig` **only when the role's
-`category === "system"`**. On the native side
-[`EngineRpcService::applyRoleConfig`](../packages/native/src/host/rpc/EngineRpcService.cpp#L189)
-accepts `kind == "sameboy"` and `"mesen"`, decodes the whole role config, and pushes each field as a
-guarded config field (an unchanged field is a no-op, so moving `highpass` never triggers a spurious
-model restart).
+| a system's gain (`setGain`) | `setParam("sys-<id>", "gain", dB)`, no publish | the node's `gain` param, at the start of the next block |
+| a **system-role** edit moving a setting field (a model, a palette, a NES's region) | a publish | the node's settings, given to the core at the start of the first block of the plan that carries them |
+| a **system-role** edit moving only a param (a NES's `expansionVolume`) | `setParam`, no publish | the node's param, at the start of the next block |
+| a **system-role** edit moving a construct-time field (noted above) | *(none until a rebuild)* | the core `reset(id)` or a reload builds next |
+| a **feature-role** config edit | a publish, for a role with a protocol node | its protocol node's settings; for `lsdj-sync`, its Game Boy node's capture too |
+| `reloadOnRomChange` | *(none)* | TS's file watcher alone |
+
+[`SystemsStore.setRoleConfig`](../packages/retroplug/src/systemsStore.ts#L550) merges the partial and
+re-parses through the role schema. For the core-config role (its kind is the system's core) it compares the
+system node's knobs before and after (`systemKnobs`,
+[graph/buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts)): a setting field that moved publishes,
+the document carrying any param that moved with it; a param that moved alone goes to `backend.setParam`; an
+edit moving neither reaches nothing. The node value-guards each knob as it gives it to the core, so a document
+that carries every knob again moves only what changed (moving `highpass` never restarts the model). A Game
+Boy's link group publishes as wiring.
 
 A **feature**-role config edit takes a different path: `setRoleConfig` updates TS state and, for a
 role that has a protocol node, publishes the graph (`setOnGraphChange` → `ProjectStore.publishGraph`),
@@ -206,10 +218,13 @@ at the plan swap (`onSettings`), keeping the instance and its state, since every
 An edit to a role with no node (an asset override) changes no document. Then `markDirty` fires the
 systems `onChange` → the project's `onSystemsChange`
 ([projectStore.ts:215](../packages/retroplug/src/projectStore.ts#L215)) for the structure's
-observers. The core is never told, with one exception: an `lsdj-sync` mode change sets the Game Boy's
-serial-out capture (`setSerialOutCapture`), which `rp.lsdj.serial-out` decodes in the two MIDI-out modes:
-`drive` for MI.OUT, where the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where
-LSDj clocks itself.
+observers. The core hears one thing of it: an `lsdj-sync`'s mode decides its Game Boy node's
+`serialOutCapture`, which `rp.lsdj.serial-out` decodes in the two MIDI-out modes: `drive` for MI.OUT, where
+the core plays the Arduinoboy that clocks LSDj, and `listen` for master sync, where LSDj clocks itself; `off`
+in every other mode. It follows the mode the placed node actually runs, the MI.MAP controller's forced one
+included (`serialOutCaptureFor`, [graph/controller.ts](../packages/retroplug/src/graph/controller.ts)), and
+rides the document that hands the decoder its new mode, so the capture and the decoder change in the same
+block and no byte captured in one mode is decoded in the other.
 
 ## The protocol nodes
 
@@ -288,16 +303,13 @@ routing node's outputs follow the systems); a system removed and added again get
   falls in, a mark at or past the block's end carried to the next block; a run carried into a block at another
   host rate has its silence counted again in that rate's frames, kept fractional so that several changes in one
   silence do not compound, and a mark a rise leaves before the block's start goes at its first frame). In every
-  other mode it sends nothing and reads nothing of its input, which is usually empty: the core captures for the
-  two decoded modes alone, but the capture follows the role's stored mode, so a cart stored in MI.OUT or master
-  sync that the MI.MAP controller forces (to MidiMap, or Off) keeps capturing
-  ([07-remaining-work.md](07-remaining-work.md)), and those bytes go unread. Each message goes on `midiOut` at
-  that frame, from one of three arrays made with the instance, one a message length. MI.OUT's bit accumulator and
-  pending command are kept on the instance across mode changes; master sync's state is dropped outside that
-  mode, so coming back opens a fresh run. Its settings are the role's config through the schema
-  `rp.lsdj.sync` takes too
-  ([nodes/lsdjSyncSettings.ts](../packages/retroplug/src/script/nodes/lsdjSyncSettings.ts)); it reads only
-  `mode`.
+  other mode it sends nothing and reads nothing of its input, which is empty: the core captures for the two decoded
+  modes alone, its node's capture following the mode this node runs, so a cart stored in MI.OUT or master sync that
+  the MI.MAP controller forces (to MidiMap, or Off) captures nothing. Each message goes on `midiOut` at that frame,
+  from one of three arrays made with the instance, one a message length. MI.OUT's bit accumulator and pending
+  command are kept on the instance across mode changes; master sync's state is dropped outside that mode, so coming
+  back opens a fresh run. Its settings are the role's config through the schema `rp.lsdj.sync` takes too
+  ([nodes/lsdjSyncSettings.ts](../packages/retroplug/src/script/nodes/lsdjSyncSettings.ts)); it reads only `mode`.
 - **`rp.controller.session`** runs a `ControllerSession` ([controller/session.ts](../packages/retroplug/src/controller/session.ts))
   for the app its settings name (`controller/apps/index.ts`, the apps' runtime, zod-free): the surface's
   traffic and the block's clock in (its tick derived from ppq at 24 PPQN), its LED bytes out on
@@ -372,9 +384,11 @@ After the systems and their link wires, and before the cart mirror and the host
 node a role's type names to run after its system (its `out`), in slot order: `rp.lsdj.serial-out` for an
 `lsdj-sync` on a Game Boy, `sys-<id>/lsdj-sync/out`, with the settings its `rp.lsdj.sync` got (the MI.MAP
 controller's forced mode included), fed `sys-<id>.serialOut` over a forward wire, so it reads what the core
-captured in the same block. It is placed whatever the mode, so a mode switch is a change of settings and
-never of the document, and only on a Game Boy, the one system with a serial out (a hand-edited `lsdj-sync`
-on another system places its `rp.lsdj.sync` alone, and host MIDI out takes no input for it). The rule under
+captured in the same block; the Game Boy's node captures for that same mode (its `serialOutCapture` setting:
+`drive` for MI.OUT, `listen` for master sync, `off` otherwise, and `off` on every Game Boy with no `lsdj-sync`
+placed). It is placed whatever the mode, so a mode switch is a change of settings and never of the document's
+structure, and only on a Game Boy, the one system with a serial out (a hand-edited `lsdj-sync` on another
+system places its `rp.lsdj.sync` alone, and host MIDI out takes no input for it). The rule under
 both: every node reads only nodes placed before it in the document, but over a feedback wire, which orders
 nothing, so the plan runs the nodes, the script nodes among them, in document order. No document `buildGraph`
 builds holds a feedback wire: `test/graph/build-graph-role-goldens.test.ts` holds every wire of its role
@@ -401,7 +415,7 @@ pipeline places (`placedRole`), so a left-out one arms nothing. `DirectGraph.set
 without the later role and can report it through a callback.
 
 **Loaded role configs are parsed.** A loaded protocol role's config is parsed by its `RoleType` schema in `adopt`
-(so the node's settings, the capture the core is built with, the menus and a save all see the parsed one), and a
+(so the node's settings, the capture its Game Boy's node runs, the menus and a save all see the parsed one), and a
 repair is logged once with the ROM and each repaired value. A config the schema refuses outright falls back to
 what the ROM's providers suggest for the role, then to the schema's defaults. The parse drops a key the schema does
 not read, as an edit already did, and names it in the log.
@@ -426,7 +440,8 @@ The rest of the document follows from those nodes:
   from the cart's battery, on the control plane. The `lsdj-sync` nodes of the cart the MI.MAP app drives
   are forced to `midiMap`, or to `off` when the cart's own SYNC setting is not MI.MAP
   (`controllerSyncOverride`), its `rp.lsdj.serial-out` with its `rp.lsdj.sync`, so a forced cart sends no
-  host MIDI. The override is those nodes' settings alone, so nothing reaches the `.rplg`.
+  host MIDI, and its Game Boy's node captures nothing for them. The override is those nodes' settings and that
+  capture alone, so nothing reaches the `.rplg`.
 
 Where nothing produces for a system input, or no controller is enabled, the input or the host I/O node is
 left unwired. The plugin loads nothing for any of this
@@ -485,9 +500,7 @@ Native ([packages/native/src/](../packages/native/src/)):
 - [ScriptHost.hpp](../packages/native/src/host/script/ScriptHost.hpp) /
   [ScriptHost.cpp](../packages/native/src/host/script/ScriptHost.cpp): the script-node runtime the
   protocol nodes run in.
-- [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp): each system node's inputs,
-  applied before its core runs.
+- [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp): each system node's settings, params
+  and inputs, applied before its core runs.
 - [HostIoNodes.cpp](../packages/native/src/host/graph/HostIoNodes.cpp): the host I/O nodes the protocol
   nodes read from and send to, and the N8 cart mirror.
-- [EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp#L189): `applyRoleConfig`
-  (system-role only).
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index f4a1e76d..5bf6ecd0 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -128,7 +128,8 @@ The suite is 30 files: a file per group, and four with no baseline of their own
 - **Scenes use `rig.press` and `rig.writeFile`** for input and files, so a change to the engine under them
   edits only `rig.ts` and `bootSession` (plus `render-blocks.test.ts`, which tests `renderBlocks` itself:
   it builds its systems, their roles and the MIDI routing through a `DirectGraph` sharing the session's
-  graph, with `setPipeline` and `setMidiRouting`, and calls `setSerialOutCapture` directly).
+  graph, with `setPipeline` and `setMidiRouting`, the document `setPipeline` publishes carrying an LSDj's
+  serial-out capture as its Game Boy node's setting, as the stores' does).
 - **Rendering goes through `renderBlocks`**, a harness-facet driver call that repeats `PluginDSP::run`'s
   order for every host block: stage MIDI at its frame, drain the command ring, apply transport,
   `processBlock` at 2/4/6/8 lanes (the host shape `multi(outputs)`, which TS's audio driver sets around
@@ -260,7 +261,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test` | `tjs-cli` | Mock-backend TS suite. |
 | `test:native` | `retroplug-host` | Real-host suite. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs included; the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing and no message dropped, the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph (`retroplug-engine-graph-test`): each system node rendering what its core renders driven directly, bit for bit, its inputs, settings and params included (every knob reaching its core at the next block through its value guard and all of them at activation, `Engine::setParam`, and an LSDj's capture switching in the block its decoder first runs in the new mode: [modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)); the engine placing, re-arming and freeing what the documents TS builds say; the host I/O nodes and `rp.n8.cart-mirror` moving what the hosts stage and drain (the staging sorted and taken with no allocation); TS nodes behind the C ABI in the script host (marshalling, the JS-owned audio copies, promise jobs, reshape, creation at adoption and release at reclaim, a throw's count (a failed write onto what a node holds among them, while one node's prototype setters charge no other, its prototype methods are adopted by none and it replaces no method of the frozen `Block` prototype), the CPU-time call deadline, an audio thread running them while the control thread replans; a profile build adds eight `[profile]` cases, which `tools/run-profile.sh unit` runs); script node packs beside the built-in one ([ScriptPacks.test.cpp](../packages/native/test/engine-graph/ScriptPacks.test.cpp), [modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)), each in runtimes of its own (a getter on `Object.prototype`, a replaced `Array.prototype.push` and a global reaching no other pack's node), its promise jobs and memory limit faulting only its own nodes, loaded, reloaded and freed while an audio thread runs blocks, each refusal changing neither the types a document can name nor the running plan, a reload's replan carrying the running plan's tap as it is, and the control thread's calls into a pack held to the call deadline; the protocol script nodes, in the documents `buildGraph` builds (read from its role goldens, the systems stood in for by probes, the script nodes run in `buildGraph`'s plan order), held to the digests of what the DSP kernel they replaced was recorded sending, block by block, but for the host MIDI out of MI.OUT and master sync, which `rp.lsdj.serial-out` sends at each byte's frame by design, three cases holding that decoder's recorded digests, and every case that records digests a second one over its system inputs alone, equal to what the rig gave at the commit before the decoder moved, so the move changed no system input ([ProtocolNodes.test.cpp](../packages/native/test/engine-graph/ProtocolNodes.test.cpp): every routing, every LSDj sync mode, risa and smsggdj sync, a controller session, its launches behind routed MIDI and its host MIDI out merged by frame with two LSDjs' in an exact interleave, state across structure changes, a throw stopping only its own node, and a block that fills `rp.host.midi-in` reaching an mGB and a NES whole; and, checked directly, a controller's session kept through a system added behind it, and every structure's script nodes running in `buildGraph`'s order), a Game Boy's serial input hashed byte by byte so how a node groups same-frame bytes moves no recorded value; `rp.host.midi-out`'s systems form, each message under its input's system, its inputs merged by frame with ties in input order, and nothing handed out earlier than a frame already handed out or past the block's last frame whatever an input's queue holds (`HostIo.test.cpp`, the guard through a test node that writes its own queue out of order and past the block), and `Engine::setGraph` refusing a second `rp.host.midi-out` as it refuses a second host input or cart mirror; `rp.lsdj.serial-out` on an engine, decoding what an mGB core captures on its link port into host MIDI inside each block and at each byte's frame, the one fixture-free run of a real capture through the real decoder, and sending master sync's stop at the first frame past 200 ms after `Engine::setSampleRate` drops the rate during a run; and every document in `buildGraph`'s goldens planned on the engine with real cores and run for a block with no node faulting (`Engine::faultCount`), no script node throwing, no message dropped and no system node's settings naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing), the 33-system `perSystem` ones refused for having more buses than `rp.host.audio-out` takes; the role goldens (`golden/build-graph-roles.json`, `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing) run twenty blocks each, the transport playing, host MIDI and a control surface's press staged, and a controller must send its surface something and, driving a real Game Boy, the host its launch. A hidden bench in it, `ScriptBench`, times a script node per block, and the protocol nodes in their plan (an LSDj in MIDI sync, and one decoding MI.OUT, its decoder after the systems), at 1, 64 and 512 frames (`build/bin/retroplug-engine-graph-test "[bench]"`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
diff --git a/spec/07-remaining-work.md b/spec/07-remaining-work.md
index c7f37fb2..00a6d9e8 100644
--- a/spec/07-remaining-work.md
+++ b/spec/07-remaining-work.md
@@ -106,7 +106,7 @@ swept as their feature lands or when touched. Known ones:
 
 | Location | Marker |
 |---|---|
-| [host/engine/Engine.cpp](../packages/native/src/host/engine/Engine.cpp) | `fastBoot` takes effect on the next restart, not live |
+| [host/graph/SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) (`GameBoy::apply`) | `fastBoot` takes effect on the next restart, not live |
 
 Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
 [systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
@@ -134,9 +134,6 @@ decodes MI.OUT and master sync in the block the core captured the bytes and send
 frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate nothing, and the profile's
 `apu-render` spans the plan. What is left, in order:
 
-- **The capture mode as a Game Boy node setting** (Phase C, with the other live knobs): carried in the
-  published document rather than through `ConfigField::SerialOutCapture` plus a publish, it would make a mode
-  switch atomic and retire the master-sync design's first accepted limit (its section 10).
 - **Script node packs, built in Phase C1**
   ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): each pack of script node types
   runs in a QuickJS runtime pair of its own, the built-in bundle the first, packs load and reload while audio runs
@@ -178,6 +175,34 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
   - **A record the release ring back cannot take keeps its pack for the host's life**, as it leaks its record rather
     than free it on the audio thread; nothing has filled that ring. A profile build's absolute live heap leaves out
     what the load of any pack loaded beside the built-in one allocated (its deltas are exact).
+- **Live knobs as system node settings and params, built in Phase C2**
+  ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)): each system's knobs are its node's,
+  a setting in the published document (a Game Boy's model, display group and LSDj capture, a NES's region, accuracy
+  switches and APU latency) or a param (every system's gain, a NES's expansion volume), its value in the document and
+  its live edits through `setParam`; node.hpp's `rebuild_key` lets the system nodes prepare statelessly, so a knob
+  edit never makes `setGraph` wait; and the side path (`ConfigField`, `Engine::applyConfigField` and its casts, the
+  `applySystemSetting` / `applyRoleConfig` / `setSerialOutCapture` RPCs, the build spec's capture) is gone. A Game
+  Boy's capture rides the document that hands its decoder the mode, computed from the mode that decoder runs, so an
+  LSDj mode switch is atomic (retiring the master-sync design's first accepted limit, its section 10) and a cart the
+  MI.MAP controller forces captures nothing (closing the B3b item below). Strict parity held bit for bit at every
+  commit. What it leaves (its sections 9 and 10):
+  - **DAW automation of node params.** The plugin's parameters are not mapped onto `setParam`; params make it
+    possible, and it is its own work.
+  - **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the
+    one the MI.MAP controller forces.
+  - **The construct-time fields and `buildCore`'s facts.** SMS `enableFm`, NES `sdRoot`, the FIFO profile and its
+    overrides, the N8 link and its round trip and `channelExportMode` stay in the build spec until C3 builds cores in
+    their nodes; `buildCore`'s report of a NES's export mode and a Game Boy's link group stays a cast until C4.
+  - **A NES's settings allocate on the block thread.** Its core allocates inside Mesen in its own blocks, and its
+    sprite-limit and accuracy setters do too, whatever path calls them (they did on the command ring), so "no
+    allocation" holds for a Game Boy's knobs and every node's params only ([02-native-host.md](02-native-host.md)).
+  - **A system node ignores a settings field it does not know**, as every C++ node does: a misspelt knob plans and
+    does nothing. `BuildGraphGoldens.test.cpp` holds the goldens' system nodes to their schemas' fields; a strict
+    reader would refuse it at plan time instead.
+  - **Enum labels have two spellings on the C++ side.** A system node's enum settings read the role configs' labels,
+    the enumerator's name with its first letter lowercased (`"cgbC"`, `knobs::Label` in
+    [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)), where node.hpp's `enum_param` labels are
+    kebab-case (`"cgb-c"`), so a knob moved from a setting to an enum param would change the word a document carries.
 - **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
   - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
     moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
@@ -216,13 +241,11 @@ frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate noth
     ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)), which still exposes mGB's CC lanes for an LSDj
     cart carrying a stale `mgb`. The fix is `placedRole(...)`
     ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
-  - **The controller's MI.MAP override changes only the document.** `controllerSyncOverride`
+  - **The controller's MI.MAP override changed only the document: closed by C2.** `controllerSyncOverride`
     ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts), applied in `buildGraph`) runs an LSDj's
-    nodes in `midiMap` for a cart reading MidiMap (in `off` for one reading another SYNC), but `serialOutCaptureFor`
-    (`systemsStore.ts`) and `__rp_syncLatencyMs` still follow the stored `lsdj-sync` mode: an LSDj stored in MI.OUT
-    (or master sync) whose nodes the controller forces keeps capture "drive" (or "listen") while its nodes run the
-    forced mode, and its `rp.lsdj.serial-out` leaves those bytes unread. Pre-existing: `kernelProjection` behaved
-    the same.
+    nodes in `midiMap` for a cart reading MidiMap (in `off` for one reading another SYNC), and `serialOutCaptureFor`,
+    beside it since C2, computes the Game Boy's capture from that forced mode, so a forced cart captures nothing. Only
+    `__rp_syncLatencyMs` still follows the stored mode, listed with C2's leftovers above.
   - A few test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`,
     `dsp-sms-sync` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
     a case is harmless but was kept out of B3b, which changed no test's identity it did not have to (the one case
diff --git a/spec/10-multichannel-audio-out.md b/spec/10-multichannel-audio-out.md
index c9b72d0d..4d57d517 100644
--- a/spec/10-multichannel-audio-out.md
+++ b/spec/10-multichannel-audio-out.md
@@ -264,8 +264,8 @@ a gigabyte.
   `channelExportMode` at construct and never selects a split routing, is never disturbed. `buildGraph`
   wires tap ports only when the mode it computes for the core (the routing's arm, else the export mode
   `buildCore` reported) is the one those ports need, so the wiring and the armed tap agree by the time
-  the NES runs. Deliberately NOT a `ConfigField` / `applyRoleConfig` entry: routing
-  is the single owner in a live host.
+  the NES runs. Deliberately NOT a setting of the NES's node (`channelExportMode` stays a construct-time field):
+  routing is the single owner in a live host.
 - **Gating (authority in `buildGraph`).** It wires a split **only** when the routing is a split
   mode and there is exactly one system; both pin modes additionally require it to be a NES, since
   nothing else has output pins. Otherwise it wires the per-system routing (as Stereo) and the layout
diff --git a/spec/11-ui-rendering.md b/spec/11-ui-rendering.md
index f2240db6..8466e4ef 100644
--- a/spec/11-ui-rendering.md
+++ b/spec/11-ui-rendering.md
@@ -84,7 +84,9 @@ so concurrent core construction can't race.
   role's schema DEFAULTS and render a PAL project at NTSC, a Game Boy as the wrong model, a replaced DMC kit
   from the cart's original samples, or an expansion chip at unity whatever the instance's Expansion Volume
   says. `buildSystem` therefore goes through `adopt` whenever roles/gain are carried (the same construct-time
-  path a project load uses), and folds a NES split's `channelExportMode` INTO the carried `mesen` role rather
+  path a project load uses: the roles' construct-time fields reach the build, and the gain and the core
+  config's knobs reach the core as its system node's params and settings in the document that places it), and
+  folds a NES split's `channelExportMode` INTO the carried `mesen` role rather
   than replacing the list. A CLI render carries neither — it has no project to copy and correctly boots the
   ROM as it ships.
 - **Menu** — `System > Render` holds selectors — **Audio Routing** (the split mode: Mix / Channels / Pins,
````

- [ ] **Step 2: Verify**

Run: `pnpm check:docs && pnpm typecheck`
Expected: `check-doc-links: 64 file(s) clean` (65 once this plan is committed), then `packages/retroplug typecheck: Done`.

Run: `git diff HEAD | python3 -c "import sys; print(sum('\u2014' in l for l in sys.stdin if l.startswith('+')))"`
Expected: `0` (no emdash on any added line).

- [ ] **Step 3: Commit**

````bash
git add -- AGENTS.md docs/lsdj-master-sync-fix-design.md docs/lsdj.md docs/modular-graph-design.md \
  docs/modular-graph-phase-c2-design.md spec/01-architecture.md spec/02-native-host.md spec/03-ts-layer.md \
  spec/04-roles-protocol-nodes.md spec/06-build-test.md spec/07-remaining-work.md spec/10-multichannel-audio-out.md \
  spec/11-ui-rendering.md
git commit -F - <<'EOF'
docs: the specs say what C2 built: system nodes' settings and params, setParam, the capture from the placed mode

spec/01 to 07, 10 and 11, docs/lsdj.md, AGENTS.md and the modular-graph design say what Phase C2 built: each system's
live knobs are its node's settings in the published document or its params, set live through setParam; node.hpp's
rebuild_key lets the system nodes prepare statelessly; ConfigField, applyConfigField and the three RPCs are gone; and
an LSDj's capture rides the document from the mode its decoder runs, which retires the master-sync design's first
accepted limit and closes the spec/07 MI.MAP item. The C2 design is marked built, with its prototype questions
answered in place, what building changed in the sections it changed and what it found in a new section 10.
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
Expected: `# 155 native test file(s) passed (jobs=16)`, with `136 case(s) SKIPPED in 60 file(s)` and the skip check clean (`lsdj-capture-settings` skips its two cases there, as the skip baseline says).

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 155 native test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

Run: `pnpm test`
Expected: `# 167 test file(s) passed (jobs=16)`.

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed (jobs=16)` (graph `All tests passed (1996 assertions in 184 test cases)`, engine-graph `All tests passed (25435 assertions in 174 test cases)`).

Run: `pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, with `2 case(s) SKIPPED in 2 file(s): lsdj-hd(1), lsdj-overlay(1)`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:ui`
Expected: `# 39 UI test file(s) passed (jobs=16)`, none skipped.

Run: `pnpm test:node && pnpm typecheck && pnpm check:docs`
Expected: `ℹ pass 12` / `ℹ fail 0`, `packages/retroplug typecheck: Done`, `check-doc-links: 65 file(s) clean` (the prototype's 64, and this plan).

- [ ] **Step 3: The sanitizers**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh thread && RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh address`
Expected: `==> host passed under thread sanitizer`, then `==> host passed under address sanitizer` (with the fixtures, so `dsp-threaded` runs: a system added and removed while the audio thread plays).

Run: `cmake --build build-tsan --target retroplug-engine-graph-test -j$(nproc) && cmake --build build-tsan --target retroplug-graph-test -j$(nproc) && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[protocol-nodes]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[settings]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-engine-graph-test "[params]" && TSAN_OPTIONS="suppressions=$PWD/packages/native/test/sanitizer/tsan.supp halt_on_error=1 second_deadlock_stack=1" build-tsan/bin/retroplug-graph-test "*rebuilds on a key*"`
Expected: `All tests passed (7517 assertions in 11 test cases)`, `All tests passed (657 assertions in 13 test cases)`, `All tests passed (79 assertions in 7 test cases)` and `All tests passed (28 assertions in 2 test cases)` (the two `rebuild_key` cases, chosen by name), no ThreadSanitizer report (the build's `-Wtsan` warnings about `atomic_thread_fence`, from the vendored concurrentqueue, are compile warnings, not a report).

Run: `cmake --build build-asan --target retroplug-engine-graph-test retroplug-graph-test -j$(nproc) && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[settings]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-engine-graph-test "[params]" && ASAN_OPTIONS="abort_on_error=1 detect_leaks=1" build-asan/bin/retroplug-graph-test "*rebuilds on a key*"`
Expected: `All tests passed (657 assertions in 13 test cases)`, `All tests passed (79 assertions in 7 test cases)` and `All tests passed (28 assertions in 2 test cases)`, no AddressSanitizer report.

- [ ] **Step 4: The live hosts**

Each of these one at a time, with nothing rebuilding the plugin meanwhile:

Run: `pnpm reaper:editor-reopen`
Expected: `PASS: the reopened editor still shows the loaded project - close/reopen preserves state.`

Run: `pnpm reaper:editor-autoload`
Expected: `PASS: the editor shows the autoloaded project (control-plane restore reflected in the UI).`

Run: `pnpm reaper:two-instances-load`
Expected: `PASS: a ROM loaded through the second instance's editor left both instances live.` (the prototype's heartbeat counted 192 before the second load and 1117 at the end of the watch, so the UI thread never froze).

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

Run: `R=$PWD; rm -rf /tmp/c2-bt && mkdir -p /tmp/c2-bt/build /tmp/c2-bt/retroplug-cli && cp -r /workspaces/bliptoaster/src /tmp/c2-bt/ && cp /workspaces/bliptoaster/build/*.nes /workspaces/bliptoaster/build/*.dbg /workspaces/bliptoaster/build/*.map /tmp/c2-bt/build/ && cp -r /workspaces/bliptoaster/retroplug-cli/tests /tmp/c2-bt/retroplug-cli/ && (cd /tmp/c2-bt && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/bliptoaster.nes); rm -rf /tmp/c2-bt`
Expected: `PASS: 57/57 test file(s) ok` (BlipToaster at ed0c843; the count follows BlipToaster's own tests).

Run: `R=$PWD; rm -rf /tmp/c2-vj && mkdir -p /tmp/c2-vj/build /tmp/c2-vj/retroplug-cli && cp -r /workspaces/nesvj/src /tmp/c2-vj/ && cp /workspaces/nesvj/build/*.nes /workspaces/nesvj/build/*.dbg /workspaces/nesvj/build/*.map /tmp/c2-vj/build/ && cp -r /workspaces/nesvj/retroplug-cli/tests /tmp/c2-vj/retroplug-cli/ && (cd /tmp/c2-vj && "$R/build/bin/retroplug-cli" test retroplug-cli/tests --rom build/nesvj.nes); rm -rf /tmp/c2-vj`
Expected: `PASS: 19/19 test file(s) ok` (NESVJ at 4783154).

Run: `pnpm test:cli-ts`
Expected: `PASS: retroplug-cli TypeScript harness`.

- [ ] **Step 6: The profile build and the benches, for the record**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-profile.sh stats && tools/run-profile.sh stats nes-bench`
Expected: dsp-bench's three windows, `DSP-BENCH {"profile":"A","cores":1,"warmupBlocks":200,"blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":312064,"maxBlockAllocs":0,"maxBlockBytes":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":16.4}`, `DSP-BENCH {"workload":"lsdj-masterSync","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":476,"clocks":476,"noteOns":0}` and `DSP-BENCH {"workload":"lsdj-midiOut","warmupBlocks":200,"blocks":400,"allocsPerBlock":0,"bytesPerBlock":0,"maxBlockAllocs":0,"maxBlockBytes":0,"midiOut":60,"clocks":40,"noteOns":20}`; then nes-bench's `DSP-BENCH {"core":"mesen","rom":"bliptoaster.nes","profile":"A","blocks":2000,"allocsPerBlock":0,"bytesPerBlock":0,"freesPerBlock":0,"liveHeapDelta":0,"peakLive":310080,"maxBlockAllocs":0,"gcMs":0.07,"gcFreedBytes":0,"xRT":10.51,"usPerBlock":2210.36}`: no allocation in any block of any window. Without `RETROPLUG_RESOURCES_DIR` the LSDj windows skip after the mGB window passes.

Run: `tools/run-profile.sh trace`
Expected: `build-prof/trace.json` written, with `rp.midi.route`, `rp.mgb.midi`, `marshal`, `js-call` and `apu-render` among its span names.

Run: `build/bin/retroplug-engine-graph-test "[bench]" && build/bin/retroplug-graph-test "[bench]"`
Expected: `All tests passed (672 assertions in 1 test case)`, then the planner bench's `All tests passed (156 assertions in 1 test case)`, each with its table. The prototype's run put a pass-through script node at about 0.074 to 0.094 us a block and one passing a message on at 0.186 to 0.197, and the protocol nodes at 0.921, 1.088 and 1.952 us a block in MIDI sync and 0.908, 4.005 and 23.896 in MI.OUT at 1, 64 and 512 frames, every row with 0 JS and 0 native allocations: C2 changes no script node.

Run: `git status --short`
Expected: only ` m deps/sameboy`.

