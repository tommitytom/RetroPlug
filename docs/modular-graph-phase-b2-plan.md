# Modular graph engine, Phase B2: TS owns the graph - implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** the TS stores build the engine's graph document and send one per edit, building a core is a native
call the document names by system id, TS owns the host shape, and the native translator and the
`constructSystem` / `removeSystem` / `setAudioRouting` RPCs are deleted, bit-identical to the B0 parity
baseline.

**Architecture:** Per [docs/modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md). A pure
`buildGraph(model)` (a port of the B1 translator, held to goldens the translator writes before it goes) turns
the systems in slot order, the routing and the host shape into a document, its slot order and the NES tap it
arms. One `EngineGraph` per control-plane context sends it (`setGraph`), skipping an unchanged document unless a
tap re-arm is due; the `ProjectStore` owns it, the stores publish once at the end of every structural edit, and
a `DirectGraph` keeps the B1 model for callers with no store. Natively, `buildCore` builds a core and parks it
under its system id, and `Engine::setGraph` builds the system table from the request's slots, turns its tap
into the swap's `SyncTap` trailer, plans, and frees any core no document placed. The live knobs and the kernel
commands keep their paths (Phase C and B3).

**Tech stack:** TypeScript on txiki (`pnpm test` mock tier, `pnpm test:native` real host), C++20 (CMake),
Catch2, the graph layer of Phases A and B1.

**Provenance:** every code block below was built and run before this plan was written, on the scratch branch
`proto/b2` in this worktree, a commit per task (two for Tasks 5 and 8), with GCC 13.3 on linux-x64. A code block that creates a file
is that file as the prototype commit left it; one that changes files is the prototype's diff for them, so
`git apply` it from the step's text (or apply it hunk by hunk with the Edit tool). Every expected pass, test
count and benchmark figure is what that run printed. A failing-test step's expected failure is what the missing code produces
(a module that does not resolve, a member that does not exist): those tests were written against the
prototype after its code, so their failures were not recorded runs. The prototype commits, in the order they
were made (Task 5's store tests were written last, and Task 8 is two commits):
- Task 1: `4bf38881` test(engine-graph): the translator's documents as goldens for the TS builder
- Task 2: `657eea4a` feat(graph): buildGraph, the engine's document built in TS
- Task 3: `cda78000` feat(engine): buildCore and setGraph, the native half of TS-built graphs
- Task 4: `6adb7785` feat(graph): EngineGraph and DirectGraph; buildCore and setGraph on the Backend
- Task 5: `6ade70fc` feat(graph): the flip - the stores publish the engine's graph, TS owns the host shape
- Task 6: `42151037` refactor(engine): delete the translator and the structural RPCs TS no longer sends
- Task 7: `79cb0581` test(graph): a planner benchmark at 1,000 and 10,000 nodes
- Task 8: `85811904` docs: the engine with TS owning the graph, and what building it found
- Task 8: `fbbfcedd` docs(engine): a comment still naming constructSystem
- Task 5: `01c70215` test(systems): what each store edit publishes, and DirectGraph's model

**Ground rules for whoever executes this:**
- Commit after each task (the commit commands are given). No `Co-Authored-By` or "generated with" trailer.
- Do not push, and do not open a PR.
- Never use an emdash character in code, comments, docs or commit messages; use " - ", ":" or ";".
- Run every command from the worktree root with absolute paths or a subshell (`(cd x && ...)`); never `cd` the
  session itself.
- Don't `rm -rf build` to fix CMake; investigate. Never use bare `git stash`, and never switch branches with
  uncommitted work in the tree. A new CMake source needs a reconfigure first: `cmake -S . -B build`.
- `deps/sameboy` shows as modified after any configure. That is the tracked audio patch: expected, leave it.
- No parity baseline is re-recorded in B2: every task leaves every baseline passing unchanged. A parity failure
  is a real change to the audio; stop and find it.
- The fixture groups need `RETROPLUG_RESOURCES_DIR=/workspaces/resources`; without it their cases skip. Pass it
  on the same command line: a shell `export` does not survive between commands.
- `node packages/retroplug/scripts/run-native-tests.mjs` (what `pnpm test:native` runs) takes ONE filter; run
  several files as separate commands, or the whole suite.
- While a Reaper check runs it uses `build/bin`: do not rebuild the plugin until it finishes.

---

## File structure

| Path | Responsibility |
|---|---|
| `packages/retroplug/src/graph/buildGraph.ts` | the document from the systems (slot order), the routing and the host shape; its slots; the NES tap it arms |
| `packages/retroplug/test/graph/golden/build-graph.json` | the B1 translator's documents for 540 models, which `buildGraph` is held to |
| `packages/retroplug/src/engineGraph.ts` | one per context: the shape, the last document sent, `publish` / `setShape` / `withShape` |
| `packages/retroplug/src/graph/directGraph.ts` | the B1 model (add / replace / remove / routing / link group) for callers with no store, and the process's one |
| `packages/retroplug/src/{systemsStore,projectStore,systemsList}.ts` | each entry's built facts; one publish per edit; a load publishing its clear, then once |
| `packages/retroplug/src/{audioDriver,bootSession,pluginControlPlane}.ts` | the render calls' shapes, `__rp_setHostOutputs` |
| `packages/retroplug/src/{backend,realBackend}.ts`, `testing/mockBackend.ts` | `buildCore` / `setGraph` in, the three structural calls out |
| `packages/native/src/host/engine/Engine.{hpp,cpp}` | `parkCore`, `setGraph` (table from slots, tap trailer, freeing unplaced cores), the host width, the empty first graph |
| `packages/native/src/host/rpc/{EngineRpcService,BackendTypes,BackendRpcRegistration}.*` | `buildCore` / `setGraph` RPCs, the render width guard |
| `packages/native/src/host/engine/{EngineInvoker,DspCommand}.*` | the invoker without structure; `SyncTap` as `{ arm, mode, id }` |
| `packages/native/{plugin/PluginDSP.cpp,sdl/main.cpp,src/host/rpc/AudioDriverRpcService.cpp}` | the hosts tell TS their outputs instead of setting a shape |
| `packages/native/test/engine-graph/{EngineGraph.test.cpp,GraphDocs.hpp}` | the engine's side of TS-built graphs; documents spelled as `buildGraph` writes them |
| `packages/native/test/graph/PlannerBench.test.cpp` | how `setGraph` scales (hidden; `"[bench]"`) |
| deleted | `src/host/graph/Translator.*`, `test/engine-graph/Translator{,Plan,Goldens}.test.cpp` |

---
### Task 1: The translator's documents as goldens

Before anything replaces the translator, record what it builds, so the TS builder can be held to it case for
case (design section 7.1). The generator runs the B1 translator over 15 system sets (empty; each kind alone;
the NES built in each export mode; linked Game Boys around an unlinked one; two link groups; a group of one;
nine mixed systems; seventeen Game Boys, which chain the adds; non-contiguous ids), each under all six routings
and six host shapes, and writes one JSON file: the distinct documents, and per case the model, the document's
index, the slot order and the tap `Engine::syncTap` would arm. Core tokens are normalized to system ids, the
form `buildGraph` writes. Real cores are built only where the translator reads them (a linked Game Boy's link
group, a NES's build mode); everything else is a stand-in reporting its kind.

**Files:**
- Create: `packages/native/test/engine-graph/TranslatorGoldens.test.cpp`
- Modify: `packages/native/cmake/tests.cmake` (`retroplug-engine-graph-test`)
- Create (generated): `packages/retroplug/test/graph/golden/build-graph.json`

- [ ] **Step 1: Write the generator**

Create `packages/native/test/engine-graph/TranslatorGoldens.test.cpp`:

````cpp
// The translator's documents as goldens for B2's TS builder (docs/modular-graph-phase-b2-design.md section
// 7.1): every model in a matrix of system sets, routings and host shapes, with the document the translator
// builds for it, its slot order, and the NES tap the engine's SyncTap would arm. The TS test
// (packages/retroplug/test/graph/build-graph-goldens.test.ts) holds buildGraph to the same file, so the two
// builders agree on every case before the translator is deleted.
//
// This test fails when the translator's output drifts from the committed file; RP_GOLDENS_UPDATE=1 rewrites
// it instead. Run via `pnpm test:plugin engine-graph`.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "graph/CanonicalJson.hpp"
#include "host/graph/Translator.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::AudioRouting;
using rp::host::HostShape;
using rp::host::Translator;
using rp::host::test::readFile;

namespace {

// A core that reports a kind and nothing else: the translator reads a Game Boy's link group and a NES's build
// mode from the real cores, so those cases build real ones, and every other system can be one of these.
class StandIn final : public SystemBase {
public:
    StandIn(SystemId id, SystemKind k) : SystemBase(id), kind_(k) {}
    SystemKind kind() const override { return kind_; }
    void onActivate(double) override {}
    void onSampleRateChanged(double) override {}
    std::vector<ChannelStream> channelLayout() const override { return {{"Mix", true}}; }
    void finishBlock(const AudioBlockInfo&, float* const*, std::size_t) override {}

private:
    SystemKind kind_;
};

struct Sys {
    const char*   type;           // the node type buildGraph places it as
    std::uint8_t  linkGroup = 0;  // Game Boys only
    std::uint32_t exportMode = 0; // NES only
    SystemId      id = 0;         // 0: the next id in slot order
};

std::unique_ptr<SystemBase> build(const Sys& s, SystemId id) {
    const std::string type = s.type;
    if (type == "rp.sameboy.gb") {
        if (s.linkGroup == 0) return std::make_unique<StandIn>(id, SystemKind::SameBoy);
        SameBoyConfig cfg;
        cfg.linkGroupId = s.linkGroup;
        return std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    }
    if (type == "rp.mesen.nes") {
        if (s.exportMode == 0) return std::make_unique<StandIn>(id, SystemKind::MesenNes);
        MesenNesConfig cfg;
        cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
        cfg.channelExportMode = s.exportMode;
        return std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    }
    if (type == "rp.mesen.gba") return std::make_unique<StandIn>(id, SystemKind::MesenGba);
    return std::make_unique<StandIn>(id, SystemKind::MesenSms);
}

struct Set {
    const char*      name;
    std::vector<Sys> systems;
};

const char* const kGb = "rp.sameboy.gb";
const char* const kNes = "rp.mesen.nes";
const char* const kGba = "rp.mesen.gba";
const char* const kSms = "rp.mesen.sms";

std::vector<Set> sets() {
    std::vector<Sys> seventeen(17, Sys{kGb});
    return {
        {"empty", {}},
        {"gb", {{kGb}}},
        {"nes", {{kNes}}},
        {"nes-built-pins", {{kNes, 0, 1}}},
        {"nes-built-pins-ref", {{kNes, 0, 2}}},
        {"nes-built-channels", {{kNes, 0, 3}}},
        {"gba", {{kGba}}},
        {"sms", {{kSms}}},
        {"gb-nes", {{kGb}, {kNes}}},
        {"linked-around-unlinked", {{kGb, 1}, {kGb}, {kGb, 1}}},
        {"two-link-groups", {{kGb, 1}, {kGb, 2}, {kGb, 1}, {kGb, 2}, {kNes}}},
        {"group-of-one", {{kGb, 3}, {kNes}}},
        {"nine-mixed", {{kGb}, {kNes}, {kGba}, {kSms}, {kGb, 1}, {kGb, 1}, {kGb}, {kNes, 0, 3}, {kGb}}},
        {"seventeen", seventeen},
        {"sparse-ids", {{kGb, 0, 0, 3}, {kNes, 0, 0, 7}, {kGb, 0, 0, 12}}},
    };
}

struct Routing {
    AudioRouting mode;
    const char*  name;  // the TS value (settingsEnums.ts AUDIO_ROUTING_VALUES)
};
const Routing kRoutings[] = {{AudioRouting::Stereo, "stereo"},
                             {AudioRouting::TwoPerInstance, "twoPerInstance"},
                             {AudioRouting::OnePerInstance, "onePerInstance"},
                             {AudioRouting::ChannelSplit, "channelSplit"},
                             {AudioRouting::PinSplit, "pinSplit"},
                             {AudioRouting::StereoPinSplit, "stereoPinSplit"}};

struct Shape {
    HostShape   shape;
    const char* json;   // the TS HostShape
    const char* label;  // for the case name
};
const Shape kShapes[] = {{{HostShape::Kind::Multi, 2}, R"({"channels":2,"kind":"multi"})", "multi2"},
                         {{HostShape::Kind::Multi, 4}, R"({"channels":4,"kind":"multi"})", "multi4"},
                         {{HostShape::Kind::Multi, 6}, R"({"channels":6,"kind":"multi"})", "multi6"},
                         {{HostShape::Kind::Multi, 8}, R"({"channels":8,"kind":"multi"})", "multi8"},
                         {{HostShape::Kind::PerSystem, 0}, R"({"kind":"perSystem"})", "perSystem"},
                         {{HostShape::Kind::PerChannel, 0}, R"({"kind":"perChannel"})", "perChannel"}};

// What Engine::syncTap arms for this routing and table: the pins (1) or the channels (3) of a lone NES.
std::string tapArm(AudioRouting routing, const rp::host::SystemTable& table) {
    const bool pins = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
    const bool split = pins || routing == AudioRouting::ChannelSplit;
    if (!split || table.slots.size() != 1 || table.slots[0].core->kind() != SystemKind::MesenNes) return "null";
    return R"({"id":)" + std::to_string(table.slots[0].id) + R"(,"mode":)" + (pins ? "1" : "3") + "}";
}

// The document with each core token replaced by its system's id, the form buildGraph writes. Tokens are
// handed out in slot order from 1, and each appears exactly once, as {"core":<token>}.
std::string normalized(std::string doc, const std::vector<SystemId>& ids) {
    for (std::size_t k = ids.size(); k-- > 0;) {
        const std::string from = R"({"core":)" + std::to_string(k + 1) + "}";
        const std::string to = R"({"core":)" + std::to_string(ids[k]) + "}";
        const auto at = doc.find(from);
        REQUIRE(at != std::string::npos);
        doc.replace(at, from.size(), to);
    }
    auto canonical = rp::graph::canonicalizeJson(doc);
    REQUIRE(canonical);
    return *canonical;
}

std::string generate() {
    std::vector<std::string> documents;
    std::vector<std::string> cases;
    for (const Set& set : sets()) {
        Translator t([](const std::string&, std::shared_ptr<const rp::host::SystemTable>, bool) { return true; },
                     [](SystemBase*) {});
        std::vector<SystemId> ids;
        std::string systems = "[";
        SystemId next = 1;
        for (const Sys& s : set.systems) {
            const SystemId id = s.id ? s.id : next;
            next = id + 1;
            ids.push_back(id);
            REQUIRE(t.add(build(s, id)));
            if (systems.size() > 1) systems += ",";
            systems += R"({"exportMode":)" + std::to_string(s.exportMode) + R"(,"id":)" + std::to_string(id) +
                       R"(,"linkGroup":)" + std::to_string(s.linkGroup) + R"(,"type":")" + s.type + R"("})";
        }
        systems += "]";
        for (const Shape& shape : kShapes) {
            REQUIRE(t.setShape(shape.shape));
            for (const Routing& routing : kRoutings) {
                REQUIRE(t.setRouting(routing.mode));
                const std::string doc = normalized(t.documentJson(), ids);
                std::size_t index = 0;
                while (index < documents.size() && documents[index] != doc) ++index;
                if (index == documents.size()) documents.push_back(doc);
                const rp::host::SystemTable table = t.table();
                std::string slots = "[";
                for (std::size_t i = 0; i < table.slots.size(); ++i)
                    slots += (i ? "," : "") + std::to_string(table.slots[i].id);
                slots += "]";
                cases.push_back(R"({"document":)" + std::to_string(index) + R"(,"model":{"routing":")" + routing.name +
                                R"(","shape":)" + shape.json + R"(,"systems":)" + systems + R"(},"name":")" + set.name +
                                "/" + routing.name + "/" + shape.label + R"(","slots":)" + slots +
                                R"(,"tapArm":)" + tapArm(routing.mode, table) + "}");
            }
        }
    }
    std::string out = "{\"documents\":[\n";
    for (std::size_t i = 0; i < documents.size(); ++i) out += documents[i] + (i + 1 < documents.size() ? ",\n" : "\n");
    out += "],\"cases\":[\n";
    for (std::size_t i = 0; i < cases.size(); ++i) out += cases[i] + (i + 1 < cases.size() ? ",\n" : "\n");
    return out + "]}\n";
}

}  // namespace

TEST_CASE("the translator's documents match the goldens buildGraph is held to", "[engine-graph][translator][goldens]") {
    const std::string text = generate();
    if (const char* u = std::getenv("RP_GOLDENS_UPDATE"); u && std::string(u) == "1") {
        std::FILE* f = std::fopen(RP_TRANSLATOR_GOLDENS_PATH, "wb");
        REQUIRE(f);
        REQUIRE(std::fwrite(text.data(), 1, text.size(), f) == text.size());
        std::fclose(f);
        SUCCEED("rewrote " RP_TRANSLATOR_GOLDENS_PATH);
        return;
    }
    const auto bytes = readFile(RP_TRANSLATOR_GOLDENS_PATH);
    REQUIRE(std::string(bytes.begin(), bytes.end()) == text);
}
````

- [ ] **Step 2: Register it**

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 1a1ccefe..244330cf 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -83,11 +83,13 @@ retroplug_add_test_binary(
              test/engine-graph/SystemNode.test.cpp
              test/engine-graph/Translator.test.cpp
              test/engine-graph/TranslatorPlan.test.cpp
+             test/engine-graph/TranslatorGoldens.test.cpp
     LIBS     retroplug-backend
     INCLUDES ${RP_QUICKJS_INCLUDE}
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
-             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms")
+             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
+             RP_TRANSLATOR_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph.json")
 
 # Guards the native file watcher (NativeFileWatcher over efsw): config.json + bindings/ recursive
 # detection, registered-ROM parent-dir watches, and the unregistered-file filter.
````

- [ ] **Step 3: Run it to verify it fails**

Run: `mkdir -p packages/retroplug/test/graph/golden && cmake -S . -B build > /dev/null && cmake --build build --target retroplug-engine-graph-test -j$(nproc) && build/bin/retroplug-engine-graph-test "[goldens]"`
Expected: FAIL at the last `REQUIRE` (no golden file yet, so the comparison is against an empty string).

- [ ] **Step 4: Write the goldens, then check them**

Run: `RP_GOLDENS_UPDATE=1 build/bin/retroplug-engine-graph-test "[goldens]"`
Expected: `All tests passed (2949 assertions in 1 test case)`.

Run: `grep -c '"name"' packages/retroplug/test/graph/golden/build-graph.json && build/bin/retroplug-engine-graph-test "[goldens]"`
Expected: `540`, then `All tests passed (2947 assertions in 1 test case)`. The file is about 355 KB.

- [ ] **Step 5: Commit**

```bash
git add packages/native/test/engine-graph/TranslatorGoldens.test.cpp packages/native/cmake/tests.cmake packages/retroplug/test/graph/golden/build-graph.json
git commit -m "test(engine-graph): the translator's documents as goldens for the TS builder"
```

---
### Task 2: `buildGraph`

The engine's document built in TS (design section 2.1): a line-for-line port of `Translator::documentJson`,
with the tap arm `Engine::syncTap` computes. The goldens hold it to the translator; the wiring tests are the
translator's own tests, carried over as explicit tables.

**Files:**
- Create: `packages/retroplug/test/graph/build-graph-goldens.test.ts`, `packages/retroplug/test/graph/build-graph.test.ts`
- Create: `packages/retroplug/src/graph/buildGraph.ts`

- [ ] **Step 1: Write the failing tests**

Create `packages/retroplug/test/graph/build-graph-goldens.test.ts`:

````ts
// buildGraph against the B1 native translator, case for case: golden/build-graph.json is what the translator
// built for every model in a matrix of system sets, routings and host shapes (written by
// packages/native/test/engine-graph/TranslatorGoldens.test.cpp while it existed). Node order, wire order and
// settings must all agree: document order is plan order on ties, and wire order is each add's input order.
import { test, expect } from "../../testing/harness";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { buildGraph, type GraphModel, type TapArm } from "../../src/graph/buildGraph";
import golden from "./golden/build-graph.json";

interface GoldenCase {
  name: string;
  model: GraphModel;
  document: number;
  slots: number[];
  tapArm: TapArm | null;
}

const goldens = golden as unknown as { documents: unknown[]; cases: GoldenCase[] };

test("the goldens cover the matrix", () => {
  expect(goldens.cases.length).toBe(540);
});

test("buildGraph builds the translator's document, slots and tap for every golden model", () => {
  const wrong: string[] = [];
  for (const c of goldens.cases) {
    const built = buildGraph(c.model);
    if (canonicalJson(built.document) !== canonicalJson(goldens.documents[c.document])) wrong.push(`${c.name}: document`);
    if (canonicalJson(built.slots) !== canonicalJson(c.slots)) wrong.push(`${c.name}: slots`);
    if (canonicalJson(built.tapArm) !== canonicalJson(c.tapArm)) wrong.push(`${c.name}: tapArm`);
  }
  expect(wrong).toEqual([]);
});
````

Create `packages/retroplug/test/graph/build-graph.test.ts`:

````ts
// buildGraph's wiring, as explicit tables (docs/modular-graph-phase-b2-design.md section 7.2). Each host channel
// must be fed what the routers before B1 added into it, in their order, and systems must run in the order that
// engine stepped them (unlinked by slot, then each link group). These are the B1 translator's own tests,
// carried over when buildGraph replaced it; build-graph-goldens.test.ts holds the two builders to each other.
import { test, expect } from "../../testing/harness";
import { buildGraph, type GraphModel, type GraphSystem, type HostShape } from "../../src/graph/buildGraph";
import type { AudioRouting } from "../../src/settingsEnums";

const gb = (id: number, linkGroup = 0): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup, exportMode: 0 });
const nes = (id: number, exportMode = 0): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode });
const multi = (channels: number): HostShape => ({ kind: "multi", channels });

function built(systems: GraphSystem[], routing: AudioRouting = "stereo", shape: HostShape = multi(2)) {
  const model: GraphModel = { systems, routing, shape };
  const { document, slots, tapArm } = buildGraph(model);
  const from = (to: string): string => document.wires.find((w) => w.to === to)?.from ?? "";
  // What feeds host channel `c` (0-based), in order: the one endpoint wired straight in, or every input of
  // the rp.core.add chain that sums into it.
  const expand = (add: string, out: string[]): void => {
    for (let k = 1; ; ++k) {
      const input = from(`${add}.in${k}`);
      if (!input) return;
      if (input.startsWith("lane-")) expand(input.slice(0, input.indexOf(".")), out);
      else out.push(input);
    }
  };
  const lane = (c: number): string[] => {
    const src = from(`out.out.${c + 1}`);
    if (!src.startsWith("lane-")) return src ? [src] : [];
    const out: string[] = [];
    expand(src.slice(0, src.indexOf(".")), out);
    return out;
  };
  const nodeOrder = document.nodes.map((n) => n.id);
  const buses = document.nodes.find((n) => n.id === "out")?.settings?.buses;
  return { document, slots, tapArm, from, lane, nodeOrder, buses };
}

test("the empty project is the kernel and the host's outputs", () => {
  const g = built([]);
  expect(g.nodeOrder).toEqual(["kernel", "out"]);
  expect(g.buses).toEqual([1, 1]);
  expect(g.slots).toEqual([]);
});

test("Stereo sums every system into one pair, in slot order", () => {
  const g = built([gb(1), nes(2), gb(3)]);
  expect(g.nodeOrder).toEqual(["kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.lane(0)).toEqual(["split-1.out1", "split-2.out1", "split-3.out1"]);
  expect(g.lane(1)).toEqual(["split-1.out2", "split-2.out2", "split-3.out2"]);
  expect(built([gb(1), gb(3)]).lane(0)).toEqual(["split-1.out1", "split-3.out1"]);
});

test("a system node names its core by its system id", () => {
  const g = built([gb(4)]);
  expect(g.document.nodes[1]).toEqual({ id: "sys-4", type: "rp.sameboy.gb", settings: { core: 4 } });
});

test("one system on a lane is wired straight in", () => {
  const g = built([gb(1)]);
  expect(g.lane(0)).toEqual(["split-1.out1"]);
  expect(g.from("out.out.1")).toBe("split-1.out1");
});

test("OnePerInstance puts L then R on one lane, and wraps past the outputs", () => {
  const g = built([gb(1), gb(2), gb(3)], "onePerInstance");
  // Flat, never (L0 + R0) + (L2 + R2): float addition is not associative.
  expect(g.lane(0)).toEqual(["split-1.out1", "split-1.out2", "split-3.out1", "split-3.out2"]);
  expect(g.lane(1)).toEqual(["split-2.out1", "split-2.out2"]);
});

test("TwoPerInstance gives slot i the pair 2i mod N", () => {
  const g = built([gb(1), gb(2), gb(3), gb(4), gb(5)], "twoPerInstance", multi(4));
  expect(g.lane(0)).toEqual(["split-1.out1", "split-3.out1", "split-5.out1"]);
  expect(g.lane(1)).toEqual(["split-1.out2", "split-3.out2", "split-5.out2"]);
  expect(g.lane(2)).toEqual(["split-2.out1", "split-4.out1"]);
  expect(g.lane(3)).toEqual(["split-2.out2", "split-4.out2"]);
});

test("a lane fed by more than 16 contributions chains its adds", () => {
  const ids = Array.from({ length: 20 }, (_, i) => i + 1);
  const g = built(ids.map((id) => gb(id)));
  expect(g.lane(0)).toEqual(ids.map((id) => `split-${id}.out1`));
  expect(g.from("lane-0-1.in1")).toBe("lane-0.out");
});

test("linked Game Boys run after the unlinked ones, each group in slot order", () => {
  const g = built([gb(1, 1), gb(2), gb(3, 1)]);
  expect(g.nodeOrder).toEqual(["kernel", "sys-2", "sys-1", "sys-3", "split-2", "split-1", "split-3", "lane-0", "lane-1", "out"]);
  expect(g.from("sys-3.link")).toBe("sys-1.link");
  expect(g.lane(0)).toEqual(["split-2.out1", "split-1.out1", "split-3.out1"]);
  // Unlinking one of two dissolves the group.
  const apart = built([gb(1, 1), gb(2), gb(3)]);
  expect(apart.nodeOrder).toEqual(["kernel", "sys-1", "sys-2", "sys-3", "split-1", "split-2", "split-3", "lane-0", "lane-1", "out"]);
  expect(apart.from("sys-3.link")).toBe("");
});

test("only Game Boys link: a link group on any other system is ignored", () => {
  const other: GraphSystem = { id: 2, type: "rp.mesen.nes", linkGroup: 1, exportMode: 0 };
  const g = built([gb(1, 1), other]);
  expect(g.nodeOrder.slice(1, 3)).toEqual(["sys-1", "sys-2"]);
  expect(g.document.wires.some((w) => w.to.endsWith(".link"))).toBe(false);
});

test("ChannelSplit fans a Game Boy's stems, or falls back to Stereo on a narrow host", () => {
  const g = built([gb(1)], "channelSplit", multi(8));
  for (let k = 0; k < 4; ++k) {
    const stem = `stem-1-${k}`;
    expect(g.from(`${stem}.in`)).toBe(`sys-1.ch${k + 1}`);
    expect(g.lane(2 * k)).toEqual([`${stem}.out1`]);
    expect(g.lane(2 * k + 1)).toEqual([`${stem}.out2`]);
  }
  const narrow = built([gb(1)], "channelSplit", multi(4));
  expect(narrow.lane(0)).toEqual(["split-1.out1"]);
  expect(narrow.lane(2)).toEqual([]);
});

test("a single NES splits into channels, pins, or pins on pairs", () => {
  const channels = built([nes(1)], "channelSplit", multi(8));
  expect(channels.lane(0)).toEqual(["sys-1.channels.sq1"]);
  expect(channels.lane(4)).toEqual(["sys-1.channels.dmc"]);
  expect(channels.lane(5)).toEqual([]);
  const pins = built([nes(1)], "pinSplit", multi(8));
  expect(pins.lane(0)).toEqual(["sys-1.pins.pulse"]);
  expect(pins.lane(2)).toEqual(["sys-1.pins.expansion"]);
  const pairs = built([nes(1)], "stereoPinSplit", multi(8));
  expect(pairs.lane(2)).toEqual(["sys-1.pins.tnd"]);  // each pin mirrored onto both lanes of its pair
  expect(pairs.lane(3)).toEqual(["sys-1.pins.tnd"]);
  // Too narrow for its lanes: the mix feeds the host as in Stereo (the tap is still armed, the narrow-host defect).
  expect(built([nes(1)], "stereoPinSplit", multi(4)).lane(0)).toEqual(["split-1.out1"]);
  // Two systems: every split renders as Stereo.
  expect(built([nes(1), gb(2)], "channelSplit", multi(8)).lane(0)).toEqual(["split-1.out1", "split-2.out1"]);
});

test("every host lane of a lone NES is fed as the split routers fed it", () => {
  // Stream k lands on lane (stride * k) mod N, with stride 1 for mono streams (2 under StereoPinSplit, which
  // mirrors the mono stem onto both lanes of its pair); unused lanes stay empty.
  const lanes = (routing: AudioRouting) => {
    const g = built([nes(1)], routing, multi(8));
    return Array.from({ length: 8 }, (_, c) => g.lane(c));
  };
  expect(lanes("channelSplit")).toEqual([
    ["sys-1.channels.sq1"], ["sys-1.channels.sq2"], ["sys-1.channels.tri"], ["sys-1.channels.noise"],
    ["sys-1.channels.dmc"], [], [], [],
  ]);
  expect(lanes("pinSplit")).toEqual([["sys-1.pins.pulse"], ["sys-1.pins.tnd"], ["sys-1.pins.expansion"], [], [], [], [], []]);
  expect(lanes("stereoPinSplit")).toEqual([
    ["sys-1.pins.pulse"], ["sys-1.pins.pulse"], ["sys-1.pins.tnd"], ["sys-1.pins.tnd"],
    ["sys-1.pins.expansion"], ["sys-1.pins.expansion"], [], [],
  ]);
});

test("PerSystem and PerChannel lay the shapes the render calls want", () => {
  const perSystem = built([gb(1), nes(2)], "stereo", { kind: "perSystem" });
  expect(perSystem.from("out.out.1-2")).toBe("sys-1.mix");
  expect(perSystem.from("out.out.3-4")).toBe("sys-2.mix");
  expect(perSystem.buses).toEqual([2, 2]);
  // ChannelSplit arms the lone NES's five channels, and PerChannel lays the armed streams.
  const perChannel = built([nes(2)], "channelSplit", { kind: "perChannel" });
  expect(perChannel.from("out.out.1")).toBe("sys-2.channels.sq1");
  expect(perChannel.from("out.out.9")).toBe("sys-2.channels.dmc");
  expect(perChannel.buses).toEqual(Array(10).fill(1));
  // With no arm, the streams are the ones the core was built with.
  expect(built([nes(2, 1)], "stereo", { kind: "perChannel" }).buses).toEqual(Array(6).fill(1));
  expect(built([gb(1)], "stereo", { kind: "perChannel" }).buses).toEqual([2, 2, 2, 2]);
});

test("systems are placed by slot, not by their place in the sum order, past a link group", () => {
  // Slots 0 and 2 are linked, slot 1 is not: the sum order is slot 1, 0, 2, so a placement by sum position
  // would put the unlinked system on the first pair.
  const systems = [gb(1, 1), gb(2), gb(3, 1)];
  const two = built(systems, "twoPerInstance", multi(8));
  expect(two.nodeOrder[1]).toBe("sys-2");
  expect([0, 1, 2, 3, 4, 5].map((c) => two.lane(c))).toEqual([
    ["split-1.out1"], ["split-1.out2"], ["split-2.out1"], ["split-2.out2"], ["split-3.out1"], ["split-3.out2"],
  ]);
  const one = built(systems, "onePerInstance", multi(8));
  expect([0, 1, 2].map((c) => one.lane(c))).toEqual([
    ["split-1.out1", "split-1.out2"], ["split-2.out1", "split-2.out2"], ["split-3.out1", "split-3.out2"],
  ]);
  const perSystem = built(systems, "stereo", { kind: "perSystem" });
  expect(["out.out.1-2", "out.out.3-4", "out.out.5-6"].map(perSystem.from)).toEqual(["sys-1.mix", "sys-2.mix", "sys-3.mix"]);
  expect(perSystem.slots).toEqual([1, 2, 3]);
});

test("the tap is armed for a lone NES under a split routing, and for nothing else", () => {
  expect(built([nes(5)], "pinSplit").tapArm).toEqual({ id: 5, mode: 1 });
  expect(built([nes(5)], "stereoPinSplit").tapArm).toEqual({ id: 5, mode: 1 });
  expect(built([nes(5)], "channelSplit").tapArm).toEqual({ id: 5, mode: 3 });
  expect(built([nes(5)], "stereo").tapArm).toBe(null);
  expect(built([gb(5)], "channelSplit").tapArm).toBe(null);
  expect(built([nes(5), nes(6)], "pinSplit").tapArm).toBe(null);
});
````

- [ ] **Step 2: Run them to verify they fail**

Run: `pnpm test graph/build-graph`
Expected: both files fail to bundle: `../../src/graph/buildGraph` cannot be resolved.

- [ ] **Step 3: Write `buildGraph`**

Create `packages/retroplug/src/graph/buildGraph.ts`:

````ts
// The engine's graph as TS builds it (docs/modular-graph-phase-b2-design.md section 2.1): the systems in slot
// order, the project's audio routing and the host's output shape, turned into the document setGraph plans.
// Pure, and held to the B1 native translator's output by test/graph/golden/build-graph.json, so every node
// id, the document order (which is the plan order on ties) and every per-lane sum is the translator's.
import type { AudioRouting } from "../settingsEnums";
import type { GraphDocument, NodeDoc, WireDoc } from "./graphDocument";

export type SystemNodeType = "rp.sameboy.gb" | "rp.mesen.nes" | "rp.mesen.gba" | "rp.mesen.sms";

/** The output shape a document is built for: the host's own outputs routed by the project's mode (multi),
 *  each system on its own pair (perSystem), or one system's streams on a pair each (perChannel). */
export type HostShape = { kind: "multi"; channels: number } | { kind: "perSystem" } | { kind: "perChannel" };

export interface GraphSystem {
  /** The TS system id. */
  id: number;
  /** The node type its core is placed as, as buildCore reported it. */
  type: SystemNodeType;
  /** Game Boys only (ignored for any other type); 0 = unlinked. */
  linkGroup: number;
  /** NES only: the channel export mode its core was built with. */
  exportMode: number;
}

export interface GraphModel {
  /** Slot order. */
  systems: readonly GraphSystem[];
  routing: AudioRouting;
  shape: HostShape;
}

/** The NES tap a lone NES under a split routing has armed: its pins (1) or its five channels (3). */
export interface TapArm {
  id: number;
  mode: 1 | 3;
}

export interface BuiltGraph {
  document: GraphDocument;
  /** System ids in slot order: the engine's system table, which its tail still walks while the kernel is sealed. */
  slots: number[];
  /** What the engine's SyncTap arms on an edit that re-arms, or null for nothing (a restore only). */
  tapArm: TapArm | null;
}

/** rp.core.add's input limit (CoreNodes.cpp): a longer sum chains adds, continuing the same left-to-right sum. */
const ADD_INPUTS = 16;

interface Stream {
  port: string;
  stereo: boolean;
}

const sysNode = (id: number): string => `sys-${id}`;

/** The streams a core reports in `mode` (a NES's export mode; ignored otherwise), as channelLayout() does. */
function streamsOf(type: SystemNodeType, mode: number): Stream[] {
  const mono = (port: string): Stream => ({ port, stereo: false });
  if (type === "rp.sameboy.gb") return ["ch1", "ch2", "ch3", "ch4"].map((port) => ({ port, stereo: true }));
  if (type === "rp.mesen.nes") {
    if (mode === 1) return ["pins.pulse", "pins.tnd", "pins.expansion"].map(mono);
    if (mode === 2) return ["pins.pulse", "pins.tnd", "pins.expansion", "pins.mixref"].map(mono);
    if (mode === 3) return ["channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"].map(mono);
  }
  return [{ port: "mix", stereo: true }];
}

/** rp.host.audio-out's port for the bus starting at host channel `first` (0-based), `n` channels wide. */
const outPort = (first: number, n: number): string => `out.out.${first + 1}${n === 1 ? "" : `-${first + n}`}`;

const isPins = (r: AudioRouting): boolean => r === "pinSplit" || r === "stereoPinSplit";
const isSplit = (r: AudioRouting): boolean => isPins(r) || r === "channelSplit";

/** Game Boys with a nonzero link group, bucketed in slot order; a group of fewer than two is dissolved.
 *  Indices into `systems`. */
function linkGroups(systems: readonly GraphSystem[]): number[][] {
  const groups: { group: number; members: number[] }[] = [];
  systems.forEach((s, i) => {
    if (s.type !== "rp.sameboy.gb" || s.linkGroup === 0) return;
    const g = groups.find((e) => e.group === s.linkGroup);
    if (g) g.members.push(i);
    else groups.push({ group: s.linkGroup, members: [i] });
  });
  return groups.filter((g) => g.members.length >= 2).map((g) => g.members);
}

/** The order systems are summed and stepped in: unlinked by slot, then each link group (by first appearance)
 *  with its members by slot. */
function sumOrder(systems: readonly GraphSystem[], groups: number[][]): number[] {
  const linked = new Set(groups.flat());
  const order = systems.map((_, i) => i).filter((i) => !linked.has(i));
  for (const g of groups) order.push(...g);
  return order;
}

/** The tap the engine arms for this model: a split routing with exactly one system, and that one a NES. */
function tapArmOf(model: GraphModel): TapArm | null {
  const only = model.systems.length === 1 ? model.systems[0] : undefined;
  if (!isSplit(model.routing) || !only || only.type !== "rp.mesen.nes") return null;
  return { id: only.id, mode: isPins(model.routing) ? 1 : 3 };
}

/** The export mode a NES has once the engine's SyncTap has armed or restored it. */
function nesMode(model: GraphModel, s: GraphSystem): number {
  const arm = tapArmOf(model);
  return arm && arm.id === s.id ? arm.mode : s.exportMode;
}

export function buildGraph(model: GraphModel): BuiltGraph {
  const { systems, routing, shape } = model;
  const nodes: NodeDoc[] = [];
  const wires: WireDoc[] = [];
  const wire = (from: string, to: string) => wires.push({ from, to });
  // A stereo endpoint as its two mono halves, through an rp.core.split named `name`.
  const halves = (from: string, name: string): [string, string] => {
    nodes.push({ id: name, type: "rp.core.split" });
    wire(from, `${name}.in`);
    return [`${name}.out1`, `${name}.out2`];
  };

  nodes.push({ id: "kernel", type: "rp.dsp.kernel" });
  const groups = linkGroups(systems);
  const order = sumOrder(systems, groups);
  for (const i of order) nodes.push({ id: sysNode(systems[i].id), type: systems[i].type, settings: { core: systems[i].id } });
  for (const g of groups)
    for (let k = 1; k < g.length; ++k) wire(`${sysNode(systems[g[k - 1]].id)}.link`, `${sysNode(systems[g[k]].id)}.link`);

  const buses: number[] = [];
  if (shape.kind === "perSystem") {
    // Slot i's mix on channels 2i and 2i + 1.
    systems.forEach((s, i) => {
      wire(`${sysNode(s.id)}.mix`, outPort(2 * i, 2));
      buses.push(2);
    });
  } else if (shape.kind === "perChannel") {
    // One system's stream s on channels 2s and 2s + 1; a mono stream fills only 2s.
    if (systems.length === 1) {
      const s = systems[0];
      streamsOf(s.type, nesMode(model, s)).forEach((stream, k) => {
        if (stream.stereo) {
          wire(`${sysNode(s.id)}.${stream.port}`, outPort(2 * k, 2));
          buses.push(2);
        } else {
          wire(`${sysNode(s.id)}.${stream.port}`, outPort(2 * k, 1));
          buses.push(1, 1);
        }
      });
    }
  } else {
    const n = shape.channels;
    const lanes: string[][] = Array.from({ length: n }, () => []);
    const addTo = (lane: number, from: string) => {
      if (lane < n) lanes[lane].push(from);
    };
    // A split holds for one system, the pin splits only on a NES, and only with room for its lanes.
    let splitValid = isSplit(routing) && systems.length === 1 && !(isPins(routing) && systems[0].type !== "rp.mesen.nes");
    let streams: Stream[] = [];
    let stride = 2;
    if (splitValid) {
      streams = streamsOf(systems[0].type, nesMode(model, systems[0]));
      const allMono = streams.every((s) => !s.stereo);
      const pairPerMono = allMono && routing === "stereoPinSplit";
      stride = allMono && !pairPerMono ? 1 : 2;
      splitValid = stride * streams.length <= n;
    }
    if (splitValid) {
      // The split: stream k on lane (stride * k) mod n and, for a stereo stream, the next. A mono stream on a
      // pair (StereoPinSplit) fills both lanes.
      const s = systems[0];
      streams.forEach((stream, k) => {
        const l = (stride * k) % n;
        const from = `${sysNode(s.id)}.${stream.port}`;
        if (stream.stereo) {
          const [lo, ro] = halves(from, `stem-${s.id}-${k}`);
          addTo(l, lo);
          addTo((l + stride - 1) % n, ro);
        } else {
          addTo(l, from);
          if (stride === 2) addTo((l + 1) % n, from);
        }
      });
    } else {
      // Multi-out placement, every split that fell back included: each system's L then R, in sum order.
      for (const i of order) {
        const s = systems[i];
        const [lo, ro] = halves(`${sysNode(s.id)}.mix`, `split-${s.id}`);
        if (routing === "onePerInstance") {
          addTo(i % n, lo);
          addTo(i % n, ro);
        } else if (routing === "twoPerInstance") {
          const p = (2 * i) % n;
          addTo(p, lo);
          addTo((p + 1) % n, ro);
        } else {
          addTo(0, lo);
          addTo(1, ro);
        }
      }
    }
    // Each host channel: one contribution wired straight in, several summed by rp.core.add in order.
    for (let c = 0; c < n; ++c) {
      buses.push(1);
      const inputs = lanes[c];
      if (inputs.length === 0) continue;
      if (inputs.length === 1) {
        wire(inputs[0], outPort(c, 1));
        continue;
      }
      let prev = "";
      let at = 0;
      for (let j = 0; at < inputs.length; ++j) {
        const name = `lane-${c}${j ? `-${j}` : ""}`;
        const ins: string[] = prev ? [`${prev}.out`] : [];
        while (ins.length < ADD_INPUTS && at < inputs.length) ins.push(inputs[at++]);
        nodes.push({ id: name, type: "rp.core.add", settings: { inputs: ins.length } });
        ins.forEach((from, k) => wire(from, `${name}.in${k + 1}`));
        prev = name;
      }
      wire(`${prev}.out`, outPort(c, 1));
    }
  }
  nodes.push({ id: "out", type: "rp.host.audio-out", settings: { buses } });
  return { document: { nodes, wires }, slots: systems.map((s) => s.id), tapArm: tapArmOf(model) };
}
````

- [ ] **Step 4: Run them to verify they pass**

Run: `pnpm test graph/build-graph && pnpm test graph/build-graph-goldens`
Expected: `15 passed, 0 skipped, 0 failed`, then `2 passed, 0 skipped, 0 failed`.

- [ ] **Step 5: Prove the golden test can fail**

Run: `cp packages/retroplug/src/graph/buildGraph.ts /tmp/bg.bak && sed -i 's/const pairPerMono = allMono \&\& routing === "stereoPinSplit";/const pairPerMono = false;/' packages/retroplug/src/graph/buildGraph.ts && pnpm test graph/build-graph-goldens; cp /tmp/bg.bak packages/retroplug/src/graph/buildGraph.ts && git status --short packages/retroplug/src/graph/`
Expected: `not ok 2`, listing 12 `.../stereoPinSplit/multi4|6|8: document` cases (the NES sets); the copy then
puts `buildGraph.ts` back as it was.

- [ ] **Step 6: Typecheck and commit**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

```bash
git add packages/retroplug/src/graph/buildGraph.ts packages/retroplug/test/graph/build-graph.test.ts packages/retroplug/test/graph/build-graph-goldens.test.ts
git commit -m "feat(graph): buildGraph, the engine's document built in TS"
```

---
### Task 3: `buildCore` and `setGraph`, natively, beside the old path

The native half (design section 2.5), added beside the translator, which keeps working: nothing calls the new
pair yet. `Engine::parkCore` parks a built core under its id and refuses a live one; `Engine::setGraph` checks
the slots against the document's system nodes, builds the table from the live cores, turns the request's tap
into the swap's `SyncTap` trailer, and frees what no document placed. `SyncTap` changes payload from a routing
value to `{ arm, mode, id }`, and the translator's own publishes compute it from their routing and table, so
the old path arms exactly as before. The engine records the host width of every published plan (the render
guard of Task 5 reads it), and `setSampleRate` re-rates the running plan's cores, which serves both paths.

**Files:**
- Create: `packages/native/test/engine-graph/EngineGraph.test.cpp`
- Modify: `packages/native/cmake/tests.cmake`
- Modify: `packages/native/src/host/engine/DspCommand.hpp`, `packages/native/src/host/graph/SystemNodes.hpp`, `packages/native/src/host/graph/SystemNodes.cpp`, `packages/native/src/host/engine/Engine.hpp`, `packages/native/src/host/engine/Engine.cpp`, `packages/native/src/host/rpc/BackendTypes.hpp`, `packages/native/src/host/rpc/EngineRpcService.hpp`, `packages/native/src/host/rpc/EngineRpcService.cpp`, `packages/native/src/host/rpc/BackendRpcRegistration.hpp`

- [ ] **Step 1: Write the failing test**

Create `packages/native/test/engine-graph/EngineGraph.test.cpp`:

````cpp
// The engine's side of graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5): a built core parks
// under its id until a document's system node takes it, a document's system nodes and its slots must name the
// same systems, the system table follows the slots, the tap re-arm rides the swap, and a core no published
// document placed is freed. Driven on a quiescent Engine, so every plan applies as it is published.
//
// Run via `pnpm test:plugin engine-graph`.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "host/engine/Engine.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::test::readFile;

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<SystemBase> mgb(SystemId id) {
    SameBoyConfig cfg;
    cfg.model = SameBoyModel::DmgB;
    auto s = std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

std::unique_ptr<SystemBase> bliptoaster(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto s = std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

// Build a core, claim its snapshot slot and park it, as buildCore does.
bool park(Engine& e, std::unique_ptr<SystemBase> core) {
    if (!e.registry().claim(core->id(), *core)) return false;
    return e.parkCore(std::move(core));
}

struct Sys {
    SystemId    id;
    const char* type;
};

// What buildGraph writes for these systems summed to a stereo pair.
std::string stereoDoc(const std::vector<Sys>& systems) {
    std::string nodes = R"({"id":"kernel","type":"rp.dsp.kernel"})";
    std::string wires;
    std::string left, right;
    for (const Sys& s : systems) {
        const std::string id = std::to_string(s.id);
        nodes += R"(,{"id":"sys-)" + id + R"(","type":")" + s.type + R"(","settings":{"core":)" + id + "}}";
    }
    for (const Sys& s : systems) {
        const std::string id = std::to_string(s.id);
        nodes += R"(,{"id":"split-)" + id + R"(","type":"rp.core.split"})";
        wires += std::string(wires.empty() ? "" : ",") + R"({"from":"sys-)" + id + R"(.mix","to":"split-)" + id + R"(.in"})";
    }
    for (std::size_t c = 0; c < 2 && !systems.empty(); ++c) {
        const std::string half = c ? "out2" : "out1";
        const std::string out = R"("out.out.)" + std::to_string(c + 1) + "\"";
        if (systems.size() == 1) {
            wires += R"(,{"from":"split-)" + std::to_string(systems[0].id) + "." + half + R"(","to":)" + out + "}";
            continue;
        }
        const std::string lane = "lane-" + std::to_string(c);
        nodes += R"(,{"id":")" + lane + R"(","type":"rp.core.add","settings":{"inputs":)" + std::to_string(systems.size()) + "}}";
        for (std::size_t k = 0; k < systems.size(); ++k)
            wires += R"(,{"from":"split-)" + std::to_string(systems[k].id) + "." + half + R"(","to":")" + lane + ".in" +
                     std::to_string(k + 1) + "\"}";
        wires += R"(,{"from":")" + lane + R"(.out","to":)" + out + "}";
    }
    nodes += R"(,{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}})";
    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
}

Engine::GraphRequest request(const std::vector<Sys>& systems) {
    Engine::GraphRequest r;
    r.document = stereoDoc(systems);
    for (const Sys& s : systems) r.slots.push_back(s.id);
    return r;
}

const char* const kGb = "rp.sameboy.gb";
const char* const kNes = "rp.mesen.nes";

}  // namespace

TEST_CASE("a parked core is placed by the document that names it, in slot order", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));
    auto r = request({{2, kGb}, {1, kGb}});
    const auto result = e.setGraph(r);
    REQUIRE(result.ok);
    CHECK(e.systemCount() == 2);
    CHECK(e.findSystem(1) != nullptr);
    CHECK(e.findSystem(2) != nullptr);
    CHECK(e.coreLive(1));
    // Rendering runs the plan: the table the tail walks is the slots'.
    std::vector<float> l(256), rr(256);
    e.processBlock(256, l.data(), rr.data());
    CHECK(e.hostWidth() == 2);
}

TEST_CASE("a core under an id still live is refused", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    CHECK_FALSE(e.parkCore(mgb(1)));  // parked
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK_FALSE(e.parkCore(mgb(1)));  // held by a node
    // Once a document drops it and the plan that ran it is reclaimed, the id is free again.
    REQUIRE(e.setGraph(request({})).ok);
    e.reclaim();
    CHECK_FALSE(e.coreLive(1));
    CHECK(park(e, mgb(1)));
}

TEST_CASE("the slots and the system nodes must name the same systems", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));

    auto missing = request({{1, kGb}, {2, kGb}});
    missing.slots = {1};
    auto result = e.setGraph(missing);
    CHECK_FALSE(result.ok);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].code == "slots");

    auto twice = request({{1, kGb}});
    twice.slots = {1, 1};
    CHECK(e.setGraph(twice).errors.at(0).code == "slots");

    auto renamed = request({{1, kGb}});
    const auto at = renamed.document.find(R"("sys-1","type")");
    REQUIRE(at != std::string::npos);
    renamed.document.replace(at, 7, R"("gb-1")");
    CHECK(e.setGraph(renamed).errors.at(0).code == "slots");

    auto unbuilt = request({{9, kGb}});
    result = e.setGraph(unbuilt);
    CHECK(result.errors.at(0).code == "unknown-core");
    CHECK(result.errors.at(0).nodeId == "sys-9");

    // Nothing was published, so the cores are still parked and nothing runs.
    CHECK(e.systemCount() == 0);
    CHECK(e.coreLive(1));
}

TEST_CASE("a published document frees every parked core it does not place", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(park(e, mgb(2)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    CHECK(e.coreLive(1));
    CHECK_FALSE(e.coreLive(2));
    // Its snapshot slot went with it, so the id builds again.
    CHECK(park(e, mgb(2)));
}

TEST_CASE("a refused document leaves the running plan and the parked cores alone", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, mgb(1)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(park(e, mgb(2)));
    auto bad = request({{1, kGb}, {2, kGb}});
    bad.document.replace(bad.document.find("rp.core.add"), 11, "rp.core.nope");
    CHECK_FALSE(e.setGraph(bad).ok);
    CHECK(e.systemCount() == 1);
    CHECK(e.coreLive(2));
}

TEST_CASE("the tap re-arm rides the swap: restore, then arm", "[engine-graph][b2]") {
    Engine e(kRate);
    REQUIRE(park(e, bliptoaster(1)));
    auto* nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    CHECK(nes == nullptr);  // parked, not yet placed

    auto armPins = request({{1, kNes}});
    armPins.rearmTap = true;
    armPins.arm = true;
    armPins.armId = 1;
    armPins.armMode = 1;
    REQUIRE(e.setGraph(armPins).ok);
    nes = dynamic_cast<MesenNesSystem*>(e.findSystem(1));
    REQUIRE(nes != nullptr);
    CHECK(nes->channelExportMode() == 1);

    // No re-arm: the tap is left as it is.
    REQUIRE(e.setGraph(request({{1, kNes}})).ok);
    CHECK(nes->channelExportMode() == 1);

    // A re-arm with nothing to arm restores the mode the core had.
    auto restore = request({{1, kNes}});
    restore.rearmTap = true;
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);

    // Channels, then a re-arm to the same mode, which restores and arms again.
    auto armChannels = request({{1, kNes}});
    armChannels.rearmTap = true;
    armChannels.arm = true;
    armChannels.armId = 1;
    armChannels.armMode = 3;
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(armChannels).ok);
    CHECK(nes->channelExportMode() == 3);
    REQUIRE(e.setGraph(restore).ok);
    CHECK(nes->channelExportMode() == 0);
}

TEST_CASE("a sample-rate change re-rates the running plan's cores", "[engine-graph][b2]") {
    Engine e(kRate);
    auto core = mgb(1);
    auto* raw = static_cast<SameBoySystem*>(core.get());
    REQUIRE(park(e, std::move(core)));
    REQUIRE(e.setGraph(request({{1, kGb}})).ok);
    REQUIRE(raw->sampleRate_ == kRate);
    e.setSampleRate(44100.0);
    CHECK(raw->sampleRate_ == 44100.0);
}
````

Register it:

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 244330cf..8e982f6f 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -84,6 +84,7 @@ retroplug_add_test_binary(
              test/engine-graph/Translator.test.cpp
              test/engine-graph/TranslatorPlan.test.cpp
              test/engine-graph/TranslatorGoldens.test.cpp
+             test/engine-graph/EngineGraph.test.cpp
     LIBS     retroplug-backend
     INCLUDES ${RP_QUICKJS_INCLUDE}
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
````

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S . -B build > /dev/null && cmake --build build --target retroplug-engine-graph-test -j$(nproc)`
Expected: compile errors: `Engine` has no `parkCore`, `setGraph`, `coreLive`, `hostWidth` or `GraphRequest`.

- [ ] **Step 3: Implement it**

````diff
diff --git a/packages/native/src/host/engine/DspCommand.hpp b/packages/native/src/host/engine/DspCommand.hpp
index 7c7c2d11..d0d328af 100644
--- a/packages/native/src/host/engine/DspCommand.hpp
+++ b/packages/native/src/host/engine/DspCommand.hpp
@@ -17,8 +17,9 @@ struct DspCommand {
         SetBpm = 7, SetTransport = 8, SetConfigField = 9, PressButton = 10,
         SetPpq = 12, StageControllerMidi = 13, WriteRam = 14,
         StageSystemMidi = 15,
-        // The NES tap arming every structural change does (design section 3.2): carried in its plan's
-        // swap, or alone for an edit that plans nothing (a remove or replace of an id already gone).
+        // The NES tap arming every structural change does (docs/modular-graph-phase-b1-design.md section
+        // 3.2): restore whatever is armed, then arm `id` to `mode` if `arm`. Carried in its plan's swap, or
+        // alone for an edit that plans nothing (a remove or replace of an id already gone).
         SyncTap = 16,
     };
 
@@ -45,7 +46,7 @@ struct DspCommand {
         // the whole message (audio thread deletes) and `len` is 0 - the same shape as stageMidi, because a
         // settings message is a SysEx and a rig block does not fit in four bytes.
         struct { std::uint32_t id; std::uint8_t data[4]; std::uint8_t len; std::vector<std::uint8_t>* ext; } stageSystemMidi;
-        struct { std::uint8_t routing; } syncTap;                 // the rp::host::AudioRouting it arms for
+        struct { bool arm; std::uint8_t mode; std::uint32_t id; } syncTap;  // what it arms after the restore
     };
 
     DspCommand() : kind(Kind::None), stageMidi{{0, 0, 0, 0}, 0, nullptr} {}
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 546bf968..0ba15744 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -1,8 +1,11 @@
 #include "host/engine/Engine.hpp"
 
+#include <algorithm>
 #include <cstddef>
+#include <cstdint>
 #include <cstdio>
 #include <cstring>
+#include <optional>
 
 #include "system/SystemBase.hpp"
 #include "system/SystemTypes.hpp"   // AudioBlockInfo, SystemId
@@ -33,13 +36,61 @@ const std::vector<DspRuntime::KeyIn>    kNoKeys;
 // the audio thread, which must not take a static's init guard or register its destructor.
 const rp::host::SystemTable kNoSystems;
 
-DspCommand syncTapFor(rp::host::AudioRouting routing) {
+DspCommand syncTapCommand(bool arm, SystemId id, std::uint32_t mode) {
     DspCommand c;
     c.kind = DspCommand::Kind::SyncTap;
-    c.syncTap = { static_cast<std::uint8_t>(routing) };
+    c.syncTap = { arm, static_cast<std::uint8_t>(mode), static_cast<std::uint32_t>(id) };
     return c;
 }
 
+// What the translator's routing arms against `table`, the systems of the plan it rides with: the pins (1) or
+// the five channels (3) of a lone NES under a split routing; a restore only otherwise.
+DspCommand syncTapFor(rp::host::AudioRouting routing, const rp::host::SystemTable& table) {
+    using rp::host::AudioRouting;
+    const bool pins  = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
+    const bool split = pins || routing == AudioRouting::ChannelSplit;
+    if (!split || table.slots.size() != 1 || !table.slots.front().nes) return syncTapCommand(false, 0, 0);
+    return syncTapCommand(true, table.slots.front().id, pins ? 1u : 3u);
+}
+
+constexpr const char* kSystemNodeTypes[] = {"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"};
+
+bool isSystemNodeType(const std::string& type) {
+    for (const char* t : kSystemNodeTypes)
+        if (type == t) return true;
+    return false;
+}
+
+// The id a system node's settings name its core by, {"core": <id>}; nullopt for anything else.
+std::optional<SystemId> coreIdOf(const rp::graph::NodeDoc& n) {
+    if (!n.settings) return std::nullopt;
+    const auto object = n.settings->to_object();
+    if (!object || object->size() != 1) return std::nullopt;
+    const auto core = object->get("core");
+    if (!core) return std::nullopt;
+    const auto id = core->to_int();
+    if (!id || *id <= 0 || *id > static_cast<std::int64_t>(UINT32_MAX)) return std::nullopt;
+    return static_cast<SystemId>(*id);
+}
+
+// The host channels a document's rp.host.audio-out writes: the sum of its buses.
+std::uint32_t hostWidthOf(const rp::graph::GraphDocument& doc) {
+    for (const auto& n : doc.nodes) {
+        if (n.type != "rp.host.audio-out" || !n.settings) continue;
+        const auto object = n.settings->to_object();
+        if (!object) return 0;
+        const auto buses = object->get("buses");
+        if (!buses) return 0;
+        const auto array = buses->to_array();
+        if (!array) return 0;
+        std::uint32_t width = 0;
+        for (const auto& b : *array)
+            if (const auto v = b.to_int()) width += static_cast<std::uint32_t>(*v);
+        return width;
+    }
+    return 0;
+}
+
 } // namespace
 
 // The Engine's graph: the node packages, the GraphHost running the plan, and the translator that turns
@@ -61,9 +112,12 @@ Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
     for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::sameboyPackage(),
                                 rp::host::mesenPackage(), rp::host::dspPackage()})
         graph_->registry.addPackage(p);
-    graph_->cores = {this,
-                     [](void* ctx, std::uint64_t token) { return static_cast<Engine*>(ctx)->graph_->translator->take(token); },
-                     [](void* ctx, SystemBase* core) { static_cast<Engine*>(ctx)->registry_.release(core->id()); }};
+    graph_->cores = {this, [](void* ctx, std::uint64_t token) { return static_cast<Engine*>(ctx)->takeCore(token); },
+                     [](void* ctx, SystemBase* core) {
+                         auto* self = static_cast<Engine*>(ctx);
+                         self->registry_.release(core->id());
+                         self->liveCores_.erase(core->id());
+                     }};
     graph_->kernel = {this, [](void* ctx, std::uint32_t frames) { static_cast<Engine*>(ctx)->runKernelStage(frames); }};
     rp::graph::HostConfig config;
     config.sampleRate = sampleRate;
@@ -83,13 +137,14 @@ Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
             // tap land in one drain or not at all. routing() already holds the mode this edit set.
             rp::graph::HostCommand trailer{};
             if (rearmsTap) {
-                const DspCommand c = syncTapFor(graph_->translator->routing());
+                const DspCommand c = syncTapFor(graph_->translator->routing(), *table);
                 std::memcpy(trailer.bytes, &c, sizeof c);
             }
             const auto result = graph_->host->setGraph(*doc, std::move(table), rearmsTap ? &trailer : nullptr);
             for (const auto& e : result.errors)
                 std::fprintf(stderr, "[engine] a graph was refused (%s) at %s.%s: %s\n", e.code.c_str(), e.nodeId.c_str(),
                              e.port.c_str(), e.message.c_str());
+            if (result.ok) hostWidth_ = hostWidthOf(*doc);
             return result.ok;
         },
         [this](SystemBase* core) { registry_.release(core->id()); });
@@ -108,7 +163,86 @@ rp::host::Translator& Engine::translator() { return *graph_->translator; }
 
 bool Engine::pushCommand(const DspCommand& c) { return graph_->host->pushHostCommand(&c, sizeof c); }
 
-bool Engine::pushSyncTap() { return pushCommand(syncTapFor(graph_->translator->routing())); }
+bool Engine::pushSyncTap() {
+    // Behind every plan already pushed, so the table it is applied against is the last one published: the
+    // translator's own.
+    return pushCommand(syncTapFor(graph_->translator->routing(), graph_->translator->table()));
+}
+
+SystemBase* Engine::takeCore(std::uint64_t token) {
+    const auto it = parked_.find(static_cast<SystemId>(token));
+    if (token <= UINT32_MAX && it != parked_.end()) {
+        SystemBase* core = it->second.release();
+        parked_.erase(it);
+        return core;
+    }
+    return graph_->translator->take(token);
+}
+
+bool Engine::parkCore(std::unique_ptr<SystemBase> core) {
+    const SystemId id = core->id();
+    if (coreLive(id)) {
+        std::fprintf(stderr, "[engine] a core was built under id %u, which is still live; it is dropped\n", static_cast<unsigned>(id));
+        registry_.release(id);
+        return false;
+    }
+    liveCores_[id] = core.get();
+    parked_[id] = std::move(core);
+    return true;
+}
+
+Engine::GraphResult Engine::setGraph(const GraphRequest& request) {
+    GraphResult out;
+    auto refuse = [&](const std::string& node, const char* code, std::string message) {
+        out.errors.push_back({node, "", code, std::move(message)});
+        return out;
+    };
+    auto doc = rp::graph::parseGraphDocument(request.document);
+    if (!doc) return refuse("", "document", std::string("not a graph document: ") + doc.error().what());
+
+    // The system nodes and the slots name the same systems, each node as sys-<id> with {"core": <id>}.
+    std::vector<SystemId> named;
+    for (const auto& n : doc->nodes) {
+        if (!isSystemNodeType(n.type)) continue;
+        const auto id = coreIdOf(n);
+        if (!id || n.id != "sys-" + std::to_string(*id))
+            return refuse(n.id, "slots", "a system node is sys-<id>, its settings {\"core\": <id>}");
+        named.push_back(*id);
+    }
+    std::vector<SystemId> slots = request.slots;
+    std::sort(named.begin(), named.end());
+    std::sort(slots.begin(), slots.end());
+    if (std::adjacent_find(slots.begin(), slots.end()) != slots.end() || slots != named)
+        return refuse("", "slots", "the slots and the document's system nodes name different systems");
+
+    auto table = std::make_shared<rp::host::SystemTable>();
+    for (const SystemId id : request.slots) {
+        const auto it = liveCores_.find(id);
+        if (it == liveCores_.end())
+            return refuse("sys-" + std::to_string(id), "unknown-core", "no core is built under id " + std::to_string(id));
+        table->slots.push_back(
+            {id, it->second, dynamic_cast<SameBoySystem*>(it->second), dynamic_cast<MesenNesSystem*>(it->second)});
+    }
+
+    rp::graph::HostCommand trailer{};
+    if (request.rearmTap) {
+        const DspCommand c = syncTapCommand(request.arm, request.armId, request.armMode);
+        std::memcpy(trailer.bytes, &c, sizeof c);
+    }
+    const auto result = graph_->host->setGraph(*doc, std::move(table), request.rearmTap ? &trailer : nullptr);
+    for (const auto& e : result.errors) out.errors.push_back({e.nodeId, e.port, e.code, e.message});
+    if (!result.ok) return out;
+    out.ok = true;
+    hostWidth_ = hostWidthOf(*doc);
+    // Each node the document names took its core when it was activated: a core still parked is one no
+    // document placed.
+    for (auto& [id, core] : parked_) {
+        registry_.release(id);
+        liveCores_.erase(id);
+    }
+    parked_.clear();
+    return out;
+}
 
 void Engine::drainCommands() { graph_->host->drain(); }
 
@@ -137,8 +271,9 @@ void Engine::setMaxBlockFrames(std::uint32_t frames) {
 
 void Engine::setSampleRate(double sr) {
     sampleRate_ = sr;
-    // Every live core re-rated in place, in slot order; then the plan's buffers at the new rate.
-    for (SystemBase* core : graph_->translator->cores()) core->onSampleRateChanged(sr);
+    // Every live core re-rated in place, in the running plan's slot order; then the plan's buffers at the new
+    // rate. Quiescent, so the running plan is the one last published.
+    for (const auto& e : currentTable().slots) e.core->onSampleRateChanged(sr);
     std::string why;
     if (!graph_->host->reactivate(sr, graph_->maxFrames, &why))
         std::fprintf(stderr, "[Engine] could not re-activate the graph at %g Hz: %s\n", sr, why.c_str());
@@ -221,7 +356,7 @@ void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
             delete cmd.writeRam.bytes;
             break;
         case DspCommand::Kind::SyncTap:
-            syncTap(static_cast<rp::host::AudioRouting>(cmd.syncTap.routing), table);
+            syncTap(cmd.syncTap.arm, cmd.syncTap.id, cmd.syncTap.mode, table);
             break;
         default:
             break;
@@ -229,25 +364,22 @@ void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
 }
 
 // The NES tap arming at every structural change, against `table`, the plan just adopted (design section 3.2):
-// restore whatever was armed, then arm the mode the routing wants. It runs even when the wanted mode is
+// restore whatever was armed, then arm `id` to `mode` if `arm`. It runs even when the wanted mode is
 // unchanged, and each run resets the capture streams. A core that has gone (removed, or replaced under
 // its id or a new one) is not restored, and its record is dropped.
-void Engine::syncTap(rp::host::AudioRouting routing, const rp::host::SystemTable& table) {
+void Engine::syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table) {
     if (armedTap_.active) {
         if (const auto* e = table.find(armedTap_.id); e && e->nes == armedTap_.core)
             e->nes->setChannelExportMode(armedTap_.prevMode);
         armedTap_ = {};
     }
-    using rp::host::AudioRouting;
-    const bool pins  = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
-    const bool split = pins || routing == AudioRouting::ChannelSplit;
-    if (!split || table.slots.size() != 1) return;
-    MesenNesSystem* nes = table.slots.front().nes;
+    if (!arm) return;
+    const auto* e = table.find(id);
+    MesenNesSystem* nes = e ? e->nes : nullptr;
     if (!nes) return;
-    const std::uint32_t want = pins ? 1u : 3u;
-    if (nes->channelExportMode() != want) {
+    if (nes->channelExportMode() != mode) {
         armedTap_ = { true, nes->id(), nes, nes->channelExportMode() };
-        nes->setChannelExportMode(want);
+        nes->setChannelExportMode(mode);
     }
 }
 
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index 69c6cb59..c0ff77ca 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -3,6 +3,7 @@
 #include <atomic>
 #include <cstdint>
 #include <functional>
+#include <map>
 #include <memory>
 #include <optional>
 #include <string>
@@ -169,6 +170,32 @@ public:
     // a SyncTap behind everything pushed so far. A plan's own SyncTap rides in its swap. Control thread.
     bool pushSyncTap();
 
+    // --- graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5), control thread ---
+    // Park a built core under its id until the system node a document names it in takes it. False, with the
+    // core freed, when a core under that id is still live (parked, or held by a node of any plan).
+    bool parkCore(std::unique_ptr<SystemBase> core);
+    bool coreLive(SystemId id) const { return liveCores_.count(id) != 0; }
+    struct GraphRequest {
+        std::string           document;  // a graph document, as toGraphJson writes it
+        std::vector<SystemId> slots;     // the document's systems, in slot order
+        bool                  rearmTap = false;  // carry a SyncTap in the swap: restore, then arm if `arm`
+        bool                  arm = false;
+        SystemId              armId = 0;
+        std::uint32_t         armMode = 0;
+    };
+    struct GraphError {
+        std::string nodeId, port, code, message;
+    };
+    struct GraphResult {
+        bool                    ok = false;
+        std::vector<GraphError> errors;
+    };
+    // Plan `request` and publish it with its system table (built from the slots and the live cores) and its
+    // SyncTap. Once it has published, a core still parked is one no document placed, and is freed.
+    GraphResult setGraph(const GraphRequest& request);
+    // The host channels the last published plan writes (the sum of its rp.host.audio-out buses). Control thread.
+    std::uint32_t hostWidth() const { return hostWidth_; }
+
 private:
     struct Graph;
     // The running plan's systems (an empty table before the first plan), for everything but the host-command
@@ -177,7 +204,8 @@ private:
     const rp::host::SystemTable& currentTable() const;
     void runKernelStage(std::uint32_t frames);     // rp.dsp.kernel's body
     void applyCommand(DspCommand& c, const rp::host::SystemTable& table);
-    void syncTap(rp::host::AudioRouting routing, const rp::host::SystemTable& table);
+    void syncTap(bool arm, SystemId id, std::uint32_t mode, const rp::host::SystemTable& table);
+    SystemBase* takeCore(std::uint64_t token);
     static const void* graphInterface(void* ctx, const char* id);
     static void onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const void* hostData, bool discard);
 
@@ -241,6 +269,13 @@ private:
     std::atomic<std::uint32_t> oversizedFrames_{0};
     std::uint32_t              oversizedReported_ = 0;
 
-    // Last, so it goes first: its system nodes release their snapshot slots into registry_ as they go.
+    // Cores buildCore built, waiting for the node a document names them in (control thread), and every core
+    // alive anywhere by id: parked, or owned by a system node, until that node frees it.
+    std::map<SystemId, std::unique_ptr<SystemBase>> parked_;
+    std::map<SystemId, SystemBase*>                 liveCores_;
+    std::uint32_t                                   hostWidth_ = 2;
+
+    // Last, so it goes first: its system nodes release their snapshot slots into registry_ (and leave
+    // liveCores_) as they go.
     std::unique_ptr<Graph> graph_;
 };
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index 044b2dea..c90c9ba6 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -193,6 +193,16 @@ struct Sms : SystemNode<Sms, MesenSmsSystem> {
 
 }  // namespace
 
+const char* systemNodeType(SystemKind kind) {
+    switch (kind) {
+        case SystemKind::SameBoy: return GameBoy::id;
+        case SystemKind::MesenNes: return Nes::id;
+        case SystemKind::MesenGba: return Gba::id;
+        case SystemKind::MesenSms: return Sms::id;
+    }
+    return "";
+}
+
 const rp_package* sameboyPackage() {
     static const rp_node_type* const kTypes[] = {rp::node_type<GameBoy>()};
     static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.sameboy","version":"1.0.0"})",
diff --git a/packages/native/src/host/graph/SystemNodes.hpp b/packages/native/src/host/graph/SystemNodes.hpp
index dc3df8cd..65cbbeaa 100644
--- a/packages/native/src/host/graph/SystemNodes.hpp
+++ b/packages/native/src/host/graph/SystemNodes.hpp
@@ -1,6 +1,7 @@
 #pragma once
 
 #include "graph/rp_node.h"
+#include "system/SystemTypes.hpp"
 
 namespace rp::host {
 
@@ -10,4 +11,7 @@ namespace rp::host {
 const rp_package* sameboyPackage();
 const rp_package* mesenPackage();
 
+// The node type a core of this kind is placed as (rp.sameboy.gb, rp.mesen.nes, ...).
+const char* systemNodeType(SystemKind kind);
+
 }  // namespace rp::host
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index 2afcd6fb..d80b0475 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,10 +35,12 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: lifecycle / snapshot reads / live config / input (14) ---
+// --- emulator: lifecycle / snapshot reads / live config / input (16) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::constructSystem>(e);
+    s.template addMethod<&EngineRpcService::buildCore>(e);
+    s.template addMethod<&EngineRpcService::setGraph>(e);
     s.template addMethod<&EngineRpcService::removeSystem>(e);
     s.template addMethod<&EngineRpcService::applySystemSetting>(e);
     s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
@@ -131,7 +133,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (90 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (92 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index a703f8b5..be7a5101 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -109,4 +109,27 @@ struct BackendConstructSpec {
     // .sav resolution; only the loaded bytes differ. Honoured by the SameBoy backend (GB-only feature).
     std::optional<rfl::Bytestring> romBytes;
     std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
+    // Arm a Game Boy's serial-out capture (LSDj MI.OUT) before the core first runs, as a later
+    // setSerialOutCapture would. Ignored by every other core.
+    std::optional<bool>                      serialOutCapture;
 };
+
+// buildCore's answer (docs/modular-graph-phase-b2-design.md section 2.5): the node type the built core is
+// placed as, and the two values the graph needs, as the core decoded them.
+struct RpcBuiltCore {
+    std::string   type;        // rp.sameboy.gb / rp.mesen.nes / rp.mesen.gba / rp.mesen.sms
+    std::uint32_t exportMode;  // NES only: the channel export mode it was built with (0 otherwise)
+    std::uint32_t linkGroup;   // Game Boys only: its link group (0 otherwise)
+};
+
+// setGraph: a document TS built, its systems in slot order, and the NES tap re-arm its edit makes. `tap`
+// present re-arms (restore, then arm `tap.arm` if it is there); absent, the plan touches no tap.
+struct RpcTapArm { std::uint32_t id; std::uint32_t mode; };
+struct RpcTapRequest { std::optional<RpcTapArm> arm; };
+struct RpcSetGraphRequest {
+    std::string                  document;
+    std::vector<std::uint32_t>   slots;
+    std::optional<RpcTapRequest> tap;
+};
+struct RpcGraphError { std::string nodeId; std::string port; std::string code; std::string message; };
+struct RpcSetGraphResult { bool ok; std::vector<RpcGraphError> errors; };
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 7748b8b3..1cd3aebb 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -4,6 +4,7 @@
 
 #include <algorithm>
 #include <cmath>
+#include <cstdio>
 #include <cstddef>
 #include <fstream>
 #include <iterator>
@@ -22,6 +23,8 @@
 #include "system/SystemTypes.hpp"
 #include "system/sameboy/SameBoyConfig.hpp"
 #include "system/sameboy/SameBoySystem.hpp"
+#include "system/mesen/MesenNesSystem.hpp"
+#include "host/graph/SystemNodes.hpp"
 
 #include "kit/KitCompiler.hpp"       // rp::kit::KitCompiler (generic, compileKit) — heavy, so only in this TU
 #include "lsdj/LsdjKitCodec.hpp"     // rp::lsdj::LsdjKitCodec (the LSDJ nibble/16KB-bank codec)
@@ -98,6 +101,49 @@ bool EngineRpcService::constructSystem(BackendConstructSpec spec) {
     return true;
 }
 
+std::optional<RpcBuiltCore> EngineRpcService::buildCore(BackendConstructSpec spec) {
+    // A core in a plan the audio thread has already handed back is freed first, so the id check sees it gone.
+    invoker_.reclaimReleased();
+    const SystemId id = spec.id;
+    if (engine_.coreLive(id)) {
+        std::fprintf(stderr, "[engine] buildCore: system %u is still live\n", static_cast<unsigned>(id));
+        return std::nullopt;
+    }
+    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
+    if (!sys) return std::nullopt;  // unknown backend / unreadable or unrecognised ROM
+    if (spec.serialOutCapture.value_or(false))
+        if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) sb->setSerialOutCapture(true);
+    // Seed the snapshot slot from the just-built core, so a read right after the build works with no block run.
+    if (!engine_.registry().claim(id, *sys)) return std::nullopt;  // pool full
+    RpcBuiltCore facts{rp::host::systemNodeType(sys->kind()), 0, 0};
+    if (auto* nes = dynamic_cast<MesenNesSystem*>(sys.get())) facts.exportMode = nes->channelExportMode();
+    if (auto* sb = dynamic_cast<SameBoySystem*>(sys.get())) facts.linkGroup = sb->config_.linkGroupId;
+    if (!engine_.parkCore(std::move(sys))) return std::nullopt;
+    return facts;
+}
+
+RpcSetGraphResult EngineRpcService::setGraph(RpcSetGraphRequest request) {
+    Engine::GraphRequest r;
+    r.document = std::move(request.document);
+    r.slots.assign(request.slots.begin(), request.slots.end());
+    if (request.tap) {
+        r.rearmTap = true;
+        if (request.tap->arm) {
+            r.arm = true;
+            r.armId = request.tap->arm->id;
+            r.armMode = request.tap->arm->mode;
+        }
+    }
+    const Engine::GraphResult result = engine_.setGraph(r);
+    RpcSetGraphResult out{result.ok, {}};
+    for (const auto& e : result.errors) {
+        std::fprintf(stderr, "[engine] a graph was refused (%s) at %s.%s: %s\n", e.code.c_str(), e.nodeId.c_str(),
+                     e.port.c_str(), e.message.c_str());
+        out.errors.push_back({e.nodeId, e.port, e.code, e.message});
+    }
+    return out;
+}
+
 // duplicate + reload are TS orchestration now (SystemsStore over the registry reads + constructSystem-
 // with-state): duplicate pulls the source savestate and builds a seeded core; reload pulls SRAM and
 // cold-boots the ROM with replaceId. Native has no bespoke method for either — that deletes the
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 9fa76e66..887563f1 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -28,6 +28,11 @@ public:
     // --- emulator lifecycle / reads ---
     // (duplicate + reload live in the TS SystemsStore as constructSystem-with-state orchestration.)
     bool constructSystem(BackendConstructSpec spec);   // TS-owned id in spec.id; returns "did it build"
+    // Build a core and park it under spec.id for the next document to place (spec.replaceId is not read):
+    // the core's facts, or null when it did not build or a core under that id is still live.
+    std::optional<RpcBuiltCore> buildCore(BackendConstructSpec spec);
+    // Plan a document TS built (docs/modular-graph-phase-b2-design.md section 2.5).
+    RpcSetGraphResult           setGraph(RpcSetGraphRequest request);
     bool removeSystem(std::uint32_t id);
     bool applySystemSetting(std::uint32_t id, std::string key, double value);
     bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
````

- [ ] **Step 4: Run it to verify it passes**

Run: `cmake --build build -j$(nproc) && build/bin/retroplug-engine-graph-test "[b2]"`
Expected: `All tests passed (58 assertions in 7 test cases)`.

- [ ] **Step 5: The old path is unchanged**

Run: `pnpm test:plugin`
Expected: `# 10 plugin test binary(ies) passed`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 24 native test file(s) passed`.

- [ ] **Step 6: Commit**

```bash
git add -A packages/native/src packages/native/test packages/native/cmake
git commit -m "feat(engine): buildCore and setGraph, the native half of TS-built graphs"
```

---
### Task 4: `EngineGraph`, `DirectGraph`, and the new calls on the `Backend`

The TS plumbing, still changing nothing (design sections 2.2 and 2.4): `buildCore` and `setGraph` on the
`Backend` (real and mock), `EngineGraph` with its unit tests, and `DirectGraph` first written as a pass-through
over today's calls, so the 46 files that build systems without stores can move onto it while their behaviour
stays exactly as it was. The mock treats a built core as live from the build on (its snapshot slot is claimed
at the build natively, so its reads answer at once), and `setGraph` makes the slots the live set.

**Files:**
- Create: `packages/retroplug/test/graph/engine-graph.test.ts`, `packages/retroplug/src/engineGraph.ts`, `packages/retroplug/src/graph/directGraph.ts`
- Modify: `packages/retroplug/src/backend.ts`, `packages/retroplug/src/realBackend.ts`, `packages/retroplug/testing/mockBackend.ts`
- Modify: the 46 direct callers (listed in Step 6)

- [ ] **Step 1: Write the failing `EngineGraph` test**

Create `packages/retroplug/test/graph/engine-graph.test.ts`:

````ts
// EngineGraph: what reaches setGraph, and when (docs/modular-graph-phase-b2-design.md section 2.2).
import { test, expect } from "../../testing/harness";
import { EngineGraph } from "../../src/engineGraph";
import type { SetGraphRequest, SetGraphResult } from "../../src/backend";
import type { GraphSystem } from "../../src/graph/buildGraph";

class FakeEngine {
  readonly requests: SetGraphRequest[] = [];
  refuse = false;
  setGraph(request: SetGraphRequest): SetGraphResult {
    this.requests.push(request);
    return this.refuse ? { ok: false, errors: [{ nodeId: "out", port: "", code: "bad", message: "refused" }] } : { ok: true, errors: [] };
  }
}

const gb = (id: number): GraphSystem => ({ id, type: "rp.sameboy.gb", linkGroup: 0, exportMode: 0 });
const nes = (id: number): GraphSystem => ({ id, type: "rp.mesen.nes", linkGroup: 0, exportMode: 0 });
const buses = (r: SetGraphRequest): unknown =>
  (JSON.parse(r.document) as { nodes: { id: string; settings?: { buses?: number[] } }[] }).nodes.find((n) => n.id === "out")?.settings?.buses;

test("a publish sends the document and its slots; an unchanged one is skipped", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(e.requests[0].slots).toEqual([1, 2]);
  expect(e.requests[0].tap).toBe(undefined);
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(1);
  expect(g.publish([gb(2)], "stereo")).toBe(true);
  expect(e.requests.length).toBe(2);
});

test("a re-arm is sent even when the document is unchanged, with the arm the routing wants", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo", { rearmTap: true });
  g.publish([gb(1)], "stereo", { rearmTap: true });
  expect(e.requests.length).toBe(2);
  expect(e.requests[1].tap).toEqual({});  // a restore only
  g.publish([nes(4)], "pinSplit", { rearmTap: true });
  expect(e.requests[2].tap).toEqual({ arm: { id: 4, mode: 1 } });
  g.publish([nes(4)], "channelSplit", { rearmTap: true });
  expect(e.requests[3].tap).toEqual({ arm: { id: 4, mode: 3 } });
});

test("a refused document keeps what was there before it", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo");
  e.refuse = true;
  expect(g.publish([gb(1), gb(2)], "stereo")).toBe(false);
  e.refuse = false;
  // The last document that planned is still the one-system one, so publishing it again sends nothing.
  g.publish([gb(1)], "stereo");
  expect(e.requests.length).toBe(2);
  // And a shape change rebuilds the systems that planned, not the refused ones.
  g.setShape({ kind: "perSystem" });
  expect(e.requests[2].slots).toEqual([1]);
});

test("a shape change rebuilds the last published systems at the new shape", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1), gb(2)], "twoPerInstance");
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);
  expect(buses(e.requests[1])).toEqual([1, 1, 1, 1]);
  expect(e.requests[1].tap).toBe(undefined);  // a shape change never re-arms
  expect(g.setShape({ kind: "multi", channels: 4 })).toBe(true);  // unchanged: nothing sent
  expect(e.requests.length).toBe(2);
  expect(g.setShape({ kind: "multi", channels: 0 })).toBe(false);
  expect(e.requests.length).toBe(2);
  e.refuse = true;
  expect(g.setShape({ kind: "perSystem" })).toBe(false);
  expect(g.shape).toEqual({ kind: "multi", channels: 4 });
});

test("withShape runs at a shape and puts the one before it back", () => {
  const e = new FakeEngine();
  const g = new EngineGraph(e);
  g.publish([gb(1)], "stereo");
  const inside = g.withShape({ kind: "perSystem" }, () => g.shape);
  expect(inside).toEqual({ kind: "perSystem" });
  expect(g.shape).toEqual({ kind: "multi", channels: 2 });
  expect(buses(e.requests[e.requests.length - 1])).toEqual([1, 1]);
  e.refuse = true;
  let ran = false;
  expect(g.withShape({ kind: "perChannel" }, () => (ran = true))).toBe(null);
  expect(ran).toBe(false);
});
````

- [ ] **Step 2: Run it to verify it fails**

Run: `pnpm test graph/engine-graph`
Expected: the file fails to bundle: `../../src/engineGraph` cannot be resolved.

- [ ] **Step 3: The `Backend` calls**

````diff
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index f524be6b..79d88851 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -18,6 +18,7 @@
 // belongs in the application layer, not here.
 
 import type { Platform, Core } from "./platform";
+import type { SystemNodeType, TapArm } from "./graph/buildGraph";
 
 export interface Backend {
   // --- Filesystem ---------------------------------------------------------
@@ -114,6 +115,19 @@ export interface Backend {
   /** Drop system `id`. Returns false when it isn't present. */
   removeSystem(id: number): boolean;
 
+  /** Build + activate an emulator core from a spec under the TS-allocated `id`, and park it for the next
+   *  graph document to place (docs/modular-graph-phase-b2-design.md section 2.5). Nothing runs it until a
+   *  `setGraph` document names it as `sys-<id>`; a document published without it frees it. Returns what the
+   *  core must be placed as, or null when it did not build (an unreadable ROM, an unknown format, a full
+   *  snapshot registry) or a core under `id` is still live. `spec.replaceId` is not read: the document
+   *  says what a core replaces. */
+  buildCore(spec: ConstructSpec, id: number): BuiltCore | null;
+
+  /** Plan and publish a graph document TS built (graph/buildGraph.ts): the engine's systems, their wiring
+   *  and the host's outputs, with the systems in slot order and the NES tap re-arm its edit makes. A
+   *  refused document changes nothing; its errors name the node at fault. */
+  setGraph(request: SetGraphRequest): SetGraphResult;
+
   // --- File dialog --------------------------------------------------------
 
   /** Open an OS file browser; resolves to the picked absolute path, or `null` on
@@ -394,7 +408,7 @@ export type HostBackend = Pick<
 /** Emulator lifecycle / live config / input / snapshot reads — bound by the plugin, UI, CLI, test host. */
 export type EmulatorBackend = Pick<
   Backend,
-  | "constructSystem" | "removeSystem" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
+  | "constructSystem" | "removeSystem" | "buildCore" | "setGraph" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
   | "setAudioRouting" | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
 >;
 
@@ -699,6 +713,34 @@ export const MemoryRegion = {
   ExtWorkRam: 8,
 } as const;
 
+/** What `buildCore` built: the node type its core is placed as, and the two values the graph needs, as the
+ *  core decoded them (a NES's channel export mode, a Game Boy's link group; 0 for every other core). */
+export interface BuiltCore {
+  type: SystemNodeType;
+  exportMode: number;
+  linkGroup: number;
+}
+
+/** A graph document for `setGraph`: `toGraphJson`'s text, its systems in slot order, and `tap` when the edit
+ *  re-arms the NES tap (restore whatever is armed, then arm `tap.arm` if given). */
+export interface SetGraphRequest {
+  document: string;
+  slots: number[];
+  tap?: { arm?: TapArm };
+}
+
+export interface GraphError {
+  nodeId: string;
+  port: string;
+  code: string;
+  message: string;
+}
+
+export interface SetGraphResult {
+  ok: boolean;
+  errors: GraphError[];
+}
+
 /** Presentation for an OS file dialog: its title + the glob patterns to show. */
 export interface FileBrowserOpts {
   title: string;
@@ -751,6 +793,9 @@ export interface ConstructSpec {
   /** Seed savestate bytes (a zip-import blob or a duplicate's captured state). When set, native
    *  boots from these instead of reading `statePath`. Rides as a Uint8Array (rfl::Bytestring). */
   stateBytes?: Uint8Array;
+  /** Arm a Game Boy's serial-out capture (LSDj MI.OUT) before the core first runs, as setSerialOutCapture
+   *  would on a running one. Omitted (off) for every system without an lsdj-sync role in a capturing mode. */
+  serialOutCapture?: boolean;
   /** The system's backend ("system"-role) config as JSON, so a loaded non-default setting
    *  (SameBoy model/highpass/…) is applied at CONSTRUCT rather than via a post-build restart
    *  that would nuke the just-restored savestate. Omitted for a fresh build (backend defaults
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index d68c6c96..dd6ba94d 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -13,7 +13,7 @@
 // recomposes them into the full `Backend`. openFileBrowser is the one async method and rides
 // a UI-direct native hook rather than the RPC bridge (see below).
 
-import type { ApuState, Backend, BreakHitBatch, BreakInfo, Breakpoint, CallFrame, ConstructSpec, ControlPlaneBackend, CoreTransportStats, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "./backend";
+import type { ApuState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, ControlPlaneBackend, CoreTransportStats, CpuRegister, DebugBackend, DebugEvent, DisasmLine, EmulatorBackend, ExpansionAudioState, FileBrowserOpts, FrameData, HostBackend, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "./backend";
 import type { OpenSerialPort, SerialClient, SerialPortInfo } from "./n8/transport";
 import { savFromJson as savFromJsonTs } from "./lsdj";
 import { makeCall } from "./rpcClient";
@@ -71,6 +71,7 @@ const specParams = (spec: ConstructSpec, id: number): Record<string, unknown> =>
   if (spec.sramBytes) p.sramBytes = spec.sramBytes;
   if (spec.stateBytes) p.stateBytes = spec.stateBytes;
   if (spec.settings != null) p.settings = spec.settings;
+  if (spec.serialOutCapture) p.serialOutCapture = true;
   return p;
 };
 
@@ -108,6 +109,8 @@ export function createEmulatorClient(): EmulatorBackend {
   return {
     constructSystem: (spec: ConstructSpec, id: number) => call("constructSystem", specParams(spec, id)) as boolean,
     removeSystem: (id) => call("removeSystem", id) as boolean,
+    buildCore: (spec: ConstructSpec, id: number) => (call("buildCore", specParams(spec, id)) as BuiltCore | null) ?? null,
+    setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
     applySystemSetting: (id, key, value) =>
       call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
     applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 3797a14e..ed098f57 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -4,7 +4,7 @@
 // "disk". This is what lets the whole application layer be tested with `tjs run`
 // and nothing else.
 
-import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
+import type { ApuState, ApuSquareState, Backend, BreakHitBatch, BuiltCore, SetGraphRequest, SetGraphResult, BreakInfo, Breakpoint, CallFrame, ConstructSpec, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, TraceLine, ZipEntry } from "../src/backend";
 import { detectPlatform } from "../src/platform";
 import { savFromJson } from "../src/lsdj";
 
@@ -41,6 +41,31 @@ interface MockSystem {
   restoredFromBytes: boolean;
 }
 
+function mockSystem(spec: ConstructSpec): MockSystem {
+  return {
+    romPath: spec.romPath,
+    embeddedRom: spec.embeddedRom,
+    savPath: spec.savPath,
+    statePath: spec.statePath,
+    restoredFromBytes: spec.sramBytes !== undefined || spec.stateBytes !== undefined,
+  };
+}
+
+// The node type native places a core of this platform as.
+function nodeTypeFor(spec: ConstructSpec): BuiltCore["type"] {
+  if (spec.embeddedRom || spec.platform === "gb") return "rp.sameboy.gb";
+  if (spec.platform === "nes") return "rp.mesen.nes";
+  if (spec.platform === "gba") return "rp.mesen.gba";
+  return "rp.mesen.sms";
+}
+
+// A number field of the construct-time settings blob, as native decodes it (0 when absent).
+function settingsNumber(spec: ConstructSpec, key: string): number {
+  if (!spec.settings) return 0;
+  const v = (JSON.parse(spec.settings) as Record<string, unknown>)[key];
+  return typeof v === "number" ? v : 0;
+}
+
 export class MockBackend implements Backend {
   private files = new Map<string, Uint8Array>();
   private dir: string;
@@ -55,6 +80,12 @@ export class MockBackend implements Backend {
   /** Every ConstructSpec passed to constructSystem, in order — lets tests assert
    *  the CONCRETE paths TS resolved (savPath/statePath/replaceId). */
   readonly constructCalls: ConstructSpec[] = [];
+  /** Every buildCore call, in order, with the id it built under. */
+  readonly buildCalls: (ConstructSpec & { id: number })[] = [];
+  /** Every setGraph request, in order: the documents TS published, their slots and their tap re-arms. */
+  readonly graphRequests: SetGraphRequest[] = [];
+  /** Cores buildCore built that no published document has placed yet. */
+  private parked = new Map<number, MockSystem>();
   // NOTE: duplicate/reload no longer hit the backend — they are TS orchestration over constructSystem,
   // so their effect shows up in constructCalls (with stateBytes/sramBytes + replaceId), not a dedicated log.
 
@@ -267,31 +298,52 @@ export class MockBackend implements Backend {
   constructSystem(spec: ConstructSpec, id: number): boolean {
     this.log.push("constructSystem");
     this.constructCalls.push(spec);
-    // Build only when a real ROM is available: an embedded marker, TS-supplied effective ROM bytes
-    // (romBytes wins, as native does), or the on-disk romPath — classified to a known format.
-    //
-    // The SMS/GG carve-out mirrors MesenBackend's gate, and has to: the Sega magic is optional, so
-    // headerless homebrew reaches construct with bytes that classify as "unknown". Native accepts those
-    // (rejecting only bytes that are positively another platform), and a stricter mock would fail every
-    // headerless-ROM store test against a backend that would really have built it.
-    if (!spec.embeddedRom) {
-      const bytes = spec.romBytes ?? this.files.get(this.canonicalize(spec.romPath));
-      if (!bytes) return false;
-      const fmt = detectPlatform(bytes);
-      const sega = spec.platform === "sms" || spec.platform === "gg";
-      if (fmt === "unknown" ? !sega : sega && fmt !== "sms" && fmt !== "gg") return false;
-    }
+    if (!this.buildable(spec)) return false;
     if (spec.replaceId !== undefined) this.systems.delete(spec.replaceId); // swap in place
-    this.systems.set(id, {  // TS owns the id counter; the mock just records under the given id
-      romPath: spec.romPath,
-      embeddedRom: spec.embeddedRom,
-      savPath: spec.savPath,
-      statePath: spec.statePath,
-      restoredFromBytes: spec.sramBytes !== undefined || spec.stateBytes !== undefined,
-    });
+    this.systems.set(id, mockSystem(spec)); // TS owns the id counter; the mock just records under the given id
     return true;
   }
 
+  buildCore(spec: ConstructSpec, id: number): BuiltCore | null {
+    this.log.push("buildCore");
+    this.buildCalls.push({ ...spec, id });
+    if (!this.buildable(spec) || this.systems.has(id) || this.parked.has(id)) return null;
+    this.parked.set(id, mockSystem(spec));
+    return { type: nodeTypeFor(spec), exportMode: settingsNumber(spec, "channelExportMode"), linkGroup: settingsNumber(spec, "linkGroupId") };
+  }
+
+  setGraph(request: SetGraphRequest): SetGraphResult {
+    this.log.push("setGraph");
+    this.graphRequests.push(request);
+    // The slots become the live systems: a parked core is placed, a live one the document drops goes, and a
+    // core still parked once the document is published is freed, as native does.
+    const live = new Map<number, MockSystem>();
+    for (const id of request.slots) {
+      const sys = this.systems.get(id) ?? this.parked.get(id);
+      if (!sys) return { ok: false, errors: [{ nodeId: `sys-${id}`, port: "", code: "unknown-core", message: `no core is built under id ${id}` }] };
+      live.set(id, sys);
+    }
+    this.systems = live;
+    this.parked.clear();
+    return { ok: true, errors: [] };
+  }
+
+  // Build only when a real ROM is available: an embedded marker, TS-supplied effective ROM bytes (romBytes
+  // wins, as native does), or the on-disk romPath, classified to a known format.
+  //
+  // The SMS/GG carve-out mirrors MesenBackend's gate, and has to: the Sega magic is optional, so headerless
+  // homebrew reaches construct with bytes that classify as "unknown". Native accepts those (rejecting only
+  // bytes that are positively another platform), and a stricter mock would fail every headerless-ROM store
+  // test against a backend that would really have built it.
+  private buildable(spec: ConstructSpec): boolean {
+    if (spec.embeddedRom) return true;
+    const bytes = spec.romBytes ?? this.files.get(this.canonicalize(spec.romPath));
+    if (!bytes) return false;
+    const fmt = detectPlatform(bytes);
+    const sega = spec.platform === "sms" || spec.platform === "gg";
+    return fmt === "unknown" ? sega : !sega || fmt === "sms" || fmt === "gg";
+  }
+
   // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram + constructSystem —
   // the mock has no bespoke method; those store paths land as constructCalls (restoredFromBytes).
````

- [ ] **Step 4: `EngineGraph`**

Create `packages/retroplug/src/engineGraph.ts`:

````ts
// The engine's graph as one control-plane context publishes it (docs/modular-graph-phase-b2-design.md section
// 2.2): the systems and routing the last publish gave it, the host's output shape, and the canonical text of
// the document that last planned. Whoever builds systems (the ProjectStore, or a DirectGraph for callers with no
// store) publishes through it, and the host shape is an edit of its own. One per native Engine: two publishers
// with separate EngineGraphs would each skip a document the other had already replaced.
import { buildGraph, type GraphSystem, type HostShape } from "./graph/buildGraph";
import { toGraphJson } from "./graph/graphDocument";
import type { Backend, SetGraphRequest } from "./backend";
import type { AudioRouting } from "./settingsEnums";

export type GraphBackend = Pick<Backend, "setGraph">;

const sameShape = (a: HostShape, b: HostShape): boolean =>
  a.kind === b.kind && (a.kind !== "multi" || b.kind !== "multi" || a.channels === b.channels);

export class EngineGraph {
  private shape_: HostShape = { kind: "multi", channels: 2 };
  private systems: readonly GraphSystem[] = [];
  private routing: AudioRouting = "stereo";
  /** The canonical text of the last document that planned; null before the first. */
  private sent: string | null = null;

  constructor(private readonly backend: GraphBackend) {}

  get shape(): HostShape {
    return this.shape_;
  }

  /** Build and send the graph for these systems (slot order) and this routing, at the current shape. Skipped
   *  when the document is unchanged and no re-arm is due; a re-arm is sent even then, since its plan carries
   *  the tap trailer. False when the planner refused it: the engine, and this graph, keep what they had. */
  publish(systems: readonly GraphSystem[], routing: AudioRouting, opts: { rearmTap?: boolean } = {}): boolean {
    return this.send(systems, routing, this.shape_, opts.rearmTap === true);
  }

  /** Rebuild the last published systems at another output shape. A no-op when it is the current one; false,
   *  keeping the shape before it, for a document the planner refuses (more systems than a perSystem render
   *  has buses for) or a multi shape with no channels, which no placement can divide by. */
  setShape(shape: HostShape): boolean {
    if (sameShape(shape, this.shape_)) return true;
    if (shape.kind === "multi" && !(shape.channels >= 1)) return false;
    return this.send(this.systems, this.routing, shape, false);
  }

  /** Run `fn` at `shape`, then put the shape before it back. Null, without running `fn`, when the shape does
   *  not plan. */
  withShape<T>(shape: HostShape, fn: () => T): T | null {
    const before = this.shape_;
    if (!this.setShape(shape)) return null;
    try {
      return fn();
    } finally {
      this.setShape(before);
    }
  }

  private send(systems: readonly GraphSystem[], routing: AudioRouting, shape: HostShape, rearmTap: boolean): boolean {
    const built = buildGraph({ systems, routing, shape });
    const document = toGraphJson(built.document);
    if (document !== this.sent || rearmTap) {
      const request: SetGraphRequest = { document, slots: built.slots };
      if (rearmTap) request.tap = built.tapArm ? { arm: built.tapArm } : {};
      const result = this.backend.setGraph(request);
      if (!result.ok) {
        for (const e of result.errors)
          console.warn(`[graph] the engine refused a document (${e.code}) at ${e.nodeId}${e.port ? "." + e.port : ""}: ${e.message}`);
        return false;
      }
      this.sent = document;
    }
    this.systems = systems.slice();
    this.routing = routing;
    this.shape_ = shape;
    return true;
  }
}
````

Run: `pnpm test graph/engine-graph`
Expected: `5 passed, 0 skipped, 0 failed`.

- [ ] **Step 5: `DirectGraph`, over today's calls**

Create `packages/retroplug/src/graph/directGraph.ts` (Task 5 rewrites its internals onto `buildCore` /
`setGraph`):

````ts
// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down) and publishes after each
// call. Use one per process, directGraph(), or one sharing a session's EngineGraph: two publishers with separate
// models would each drop the other's systems from the document.
import { createRealBackend } from "../realBackend";
import { audioRoutingToIndex, type AudioRouting } from "../settingsEnums";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import { EngineGraph } from "../engineGraph";

export type DirectBackend = Pick<
  EmulatorBackend,
  "constructSystem" | "removeSystem" | "setAudioRouting" | "applyRoleConfig" | "buildCore" | "setGraph"
>;

export class DirectGraph {
  readonly graph: EngineGraph;

  constructor(
    private readonly backend: DirectBackend,
    graph?: EngineGraph,
  ) {
    this.graph = graph ?? new EngineGraph(backend);
  }

  /** Build a core under `id` and append it. False when it did not build. */
  add(spec: ConstructSpec, id: number): boolean {
    return this.backend.constructSystem(spec, id);
  }

  /** Build a core under `id` in `oldId`'s slot. False when it did not build or `oldId` is absent. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    return this.backend.constructSystem({ ...spec, replaceId: oldId }, id);
  }

  /** Drop system `id`; later slots shift down. */
  remove(id: number): boolean {
    return this.backend.removeSystem(id);
  }

  setRouting(routing: AudioRouting): boolean {
    return this.backend.setAudioRouting(audioRoutingToIndex(routing));
  }

  /** Apply a system role's config to the live core. A `sameboy` config's link group is wiring as well. */
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
    return this.backend.applyRoleConfig(id, kind, config);
  }
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
````

- [ ] **Step 6: Move the direct callers onto it**

Every `be.constructSystem(` becomes `graph.add(`, `be.removeSystem(` `graph.remove(`, and
`be.applyRoleConfig(` `graph.applyRoleConfig(`, with `const graph = directGraph();` at module scope; a few
helpers lose the `be` parameter they only used for those calls. The whole change:

````diff
diff --git a/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts b/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
index 0123bdd3..ecf1e41f 100644
--- a/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
+++ b/packages/retroplug/cli/sessions/analyze-lsdj-sync.ts
@@ -7,6 +7,10 @@
 import { runSession, hostArgs } from "../session";
 import { encodeWav } from "../wav";
 import { savFrom, type SongSettings } from "../../src/lsdjSav";
+import { directGraph } from "../../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 const START = 7; // GameboyButton::Start
 
@@ -28,13 +32,13 @@ runSession((s) => {
 
   // Link-cable sync is pure GB serial ferrying over the link cable: construct both cores directly,
   // link them, and drive; no DSP kernel needed.
-  const construct = (id: number, sync: SongSettings["syncMode"]) => s.backend.constructSystem({
+  const construct = (id: number, sync: SongSettings["syncMode"]) => graph.add({
     romPath, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: songSav(sync),
   }, id);
   if (!construct(1, "Lsdj") || !construct(2, "Lsdj")) throw new Error("construct failed");
-  s.backend.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
-  s.backend.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
+  graph.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
+  graph.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
 
   s.audio.renderAudio(6000); // reach the song screen from the savs
   s.audio.pressButton(1, START, true); // START on the leader only
diff --git a/packages/retroplug/test-native/app-cores.test.ts b/packages/retroplug/test-native/app-cores.test.ts
index 5569332c..9e9d769d 100644
--- a/packages/retroplug/test-native/app-cores.test.ts
+++ b/packages/retroplug/test-native/app-cores.test.ts
@@ -11,6 +11,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { RecentStore } from "../src/recentStore";
 import { ProjectStore } from "../src/projectStore";
 import { buildAppRegistry } from "../src/appHost";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __REPO_RESOURCES_DIR__: string;
@@ -98,10 +102,10 @@ function buildsWithoutCore(rom: string, platform: string): void {
     statePath: null,
   };
   const id = 900 + platform.length; // outside the store's allocator, distinct per case
-  expect(be.constructSystem(spec as never, id)).toBeTruthy();
+  expect(graph.add(spec as never, id)).toBeTruthy();
   createAudioDriver().renderAudio(500);
   expect(be.getFrame(id)?.published ?? false).toBeTruthy(); // a real core, not a nullptr swallowed
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 }
 
 test("a wire spec that omits `core` still reaches the right backend, for every platform", () => {
diff --git a/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts b/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
index 6004d398..6c86e5f3 100644
--- a/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
+++ b/packages/retroplug/test-native/bliptoaster-rom-cli.test.ts
@@ -11,6 +11,10 @@ import { blipToasterRomTool } from "../cli/sessions/bliptoaster-rom";
 import { BlipToasterRom } from "../src/bliptoaster/rom";
 import { bankToModel, isBankPopulated, decodeThemeFromRom } from "../src/risa/rom";
 import { encodeWav } from "../cli/wav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 // The base build (FME-7, taken for kit banking alone — no expansion audio): 16 switchable kit banks, 16 baked
 // themes, 4 CHR fonts. Built by a bare `make` in the bliptoaster checkout; these legs SKIP cleanly if absent.
@@ -82,7 +86,7 @@ test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-
   }
   expect([...be.readFile(BLIPTOASTER_ROM)!]).toEqual([...base.bytes()]); // source untouched
 
-  expect(be.constructSystem({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 40)).toBeTruthy();
+  expect(graph.add({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 40)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(40) != null).toBeTruthy();
   console.log(`[bliptoaster-rom] import-kit replaced bank ${SLOT} (${before} -> HATS); patched ROM boots`);
@@ -211,14 +215,14 @@ test("a settings-patched ROM boots, and the core comes up in the theme the block
   const nes = copyRom(be, "/tmp/rp-em-setboot.nes");
   blipToasterRomTool.run(s, ["settings", nes, "--theme", "11"]); // MTRX: bg $0F, text $2A
 
-  expect(be.constructSystem({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 44)).toBeTruthy();
+  expect(graph.add({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 44)).toBeTruthy();
   audio.renderAudio(1000); // ~1 s: past the ROM's boot, so the palette has been written
   // The proof that the byte reached the hardware, not just the file: $3F00/$3F01 hold theme 11's two roles.
   const pal = be.getPpuState(44).paletteRam;
   expect(pal[0]).toBe(0x0f);
   expect(pal[1]).toBe(0x2a);
   console.log(`[bliptoaster-rom] settings --theme 11 -> the core boots with $3F00/$3F01 = 0F/2A`);
-  expect(be.removeSystem(44)).toBeTruthy(); // renderAudio drives EVERY live system — see frameOf below
+  expect(graph.remove(44)).toBeTruthy(); // renderAudio drives EVERY live system — see frameOf below
 });
 
 // The committed resources ROM is a real artifact predating the two screen fields, so it is the honest fixture
@@ -273,12 +277,12 @@ test("a ROM predating the screen fields is reported as not reading them, and ref
 // cost 24 s and 31 s for 2 s of audio apiece, because they were dragging their predecessors along.
 // Removed, they are a few seconds each. The pixels are copied out first, so the frame outlives the core.
 function frameOf(be: ReturnType<typeof createRealBackend>, audio: ReturnType<typeof createAudioDriver>, rom: string, id: number): Uint8Array {
-  expect(be.constructSystem({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, id)).toBeTruthy();
+  expect(graph.add({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, id)).toBeTruthy();
   audio.renderAudio(2000); // ~2 s: past boot and the first drawn page
   const frame = be.getFrame(id);
   expect(frame != null && frame.published).toBeTruthy();
   const pixels = new Uint8Array(frame!.pixels);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
   return pixels;
 }
 
@@ -323,9 +327,9 @@ test("bliptoaster-rom patch realizes a mixed manifest (build kit + import theme
   const rom = BlipToasterRom.fromBytes(be.readFile("/tmp/rp-em-pt-out.nes")!);
   expect(rom.kits().some((k) => k.slot === 4 && k.name === "PT")).toBeTruthy();
 
-  expect(be.constructSystem({ romPath: "/tmp/rp-em-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 41)).toBeTruthy();
+  expect(graph.add({ romPath: "/tmp/rp-em-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 41)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(41) != null).toBeTruthy();
   console.log(`[bliptoaster-rom] patch: built kit 4 + theme 0 + font 0; patched ROM boots`);
-  expect(be.removeSystem(41)).toBeTruthy();
+  expect(graph.remove(41)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/bliptoaster-rom.test.ts b/packages/retroplug/test-native/bliptoaster-rom.test.ts
index 868995e5..e8083d81 100644
--- a/packages/retroplug/test-native/bliptoaster-rom.test.ts
+++ b/packages/retroplug/test-native/bliptoaster-rom.test.ts
@@ -13,6 +13,10 @@ import { bootSession } from "../cli/session";
 import { Timeline, renderTimeline } from "../cli/timeline";
 import { BlipToasterRom } from "../src/bliptoaster/rom";
 import { blipToasterInfo, isBlipToasterRomHeader, BLIPTOASTER_MARKER } from "../src/bliptoaster/romDetect";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 // The COMMITTED ROM, so every leg below runs everywhere — no fixture guard, no skip.
@@ -52,7 +56,7 @@ test("constructSystem romBytes boots a BlipToaster (Mesen) system over a nonexis
 
   const bytes = be.readFile(BLIPTOASTER_ROM)!;
   // romPath points at nothing; only romBytes can boot this. (Pre-fix Mesen slurps the path -> nullptr.)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: "/definitely/not/here/nope.nes", platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: bytes,
   }, 11)).toBeTruthy();
@@ -79,7 +83,7 @@ test("a kit + font patched into the ROM in memory boots (the on-disk .nes is unt
   rom.setChrFontSlot(0, font);
   const effective = rom.bytes();
 
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: effective,
   }, 12)).toBeTruthy();
@@ -103,7 +107,7 @@ test("the baked theme sets the background palette; a theme override changes it",
   // re-stage that first ran this test). The claim is that the PPU palette follows the baked record,
   // which is what comparing the two asserts — and it stays true of any build.
   const bakedTheme = BlipToasterRom.fromBytes(base).getTheme(0)!;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: base,
   }, 13)).toBeTruthy();
@@ -120,7 +124,7 @@ test("the baked theme sets the background palette; a theme override changes it",
   rec[1] = 0x11; // text
   rom.setTheme(0, rec, t.nameBytes);
 
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: BLIPTOASTER_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: rom.bytes(),
   }, 14)).toBeTruthy();
diff --git a/packages/retroplug/test-native/dsp-lifecycle.test.ts b/packages/retroplug/test-native/dsp-lifecycle.test.ts
index c9f45bce..93ea0089 100644
--- a/packages/retroplug/test-native/dsp-lifecycle.test.ts
+++ b/packages/retroplug/test-native/dsp-lifecycle.test.ts
@@ -10,6 +10,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import type { ConstructSpec } from "../src/backend";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 
@@ -31,7 +35,7 @@ test("add + remove a system while the audio thread plays, via the command + rele
   // --- setup (quiescent): one mGB + the kernel routing so notes can reach it ---
   // TS owns the id counter now; a test calling the backend directly picks its own (fresh host per file).
   const a = 1;
-  expect(be.constructSystem(MGB, a)).toBeTruthy();
+  expect(graph.add(MGB, a)).toBeTruthy();
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
   expect(dsp.setSystems(routingMgb([a]))).toBeTruthy();
 
@@ -51,7 +55,7 @@ test("add + remove a system while the audio thread plays, via the command + rele
 
   // --- ADD a second mGB while running → built on the control thread, planned in on the audio thread ---
   const b = 2;
-  expect(be.constructSystem(MGB, b)).toBeTruthy();
+  expect(graph.add(MGB, b)).toBeTruthy();
   audio.sleepMs(30); // let the audio thread adopt the plan that adds it
   expect(audio.systemCount()).toBe(2);
   expect(dsp.setSystems(routingMgb([a, b]))).toBeTruthy();
@@ -59,7 +63,7 @@ test("add + remove a system while the audio thread plays, via the command + rele
   const twoSys = energyWindow(120);
 
   // --- REMOVE the first while running → planned out on the audio thread, freed with its old plan ---
-  expect(be.removeSystem(a)).toBeTruthy();
+  expect(graph.remove(a)).toBeTruthy();
   audio.sleepMs(30);
   expect(audio.systemCount()).toBe(1);
   expect(be.readState(a) !== null, "a's slot before the reclaim").toBeTruthy(); // its old plan still holds it
diff --git a/packages/retroplug/test-native/dsp-lsdj-midisync.test.ts b/packages/retroplug/test-native/dsp-lsdj-midisync.test.ts
index bb8a155a..b1495acf 100644
--- a/packages/retroplug/test-native/dsp-lsdj-midisync.test.ts
+++ b/packages/retroplug/test-native/dsp-lsdj-midisync.test.ts
@@ -11,6 +11,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -53,7 +57,7 @@ test("the TS lsdj-sync role in the DSP kernel is the sole clock that makes an ar
   // Real LSDj, authored SYNC=MIDI song, constructed bare (no C++ roles) so the kernel is the only clock.
   const sav = savFrom(SYNC_MIDI_SONG);
   const id = 1; // TS owns the id counter; this direct-backend test picks its own (fresh host per file)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/dsp-risa-sync-grid.test.ts b/packages/retroplug/test-native/dsp-risa-sync-grid.test.ts
index a96fbcab..ddc2d46e 100644
--- a/packages/retroplug/test-native/dsp-risa-sync-grid.test.ts
+++ b/packages/retroplug/test-native/dsp-risa-sync-grid.test.ts
@@ -11,6 +11,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { buildRisaMetronomeSav } from "./risaSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 
@@ -60,7 +64,7 @@ test("host sync puts one risa hit on every DAW beat, at the DAW's tempo", () =>
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: buildRisaMetronomeSav(),
   }, id)).toBeTruthy();
diff --git a/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts b/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
index 30b1ffc0..33a6e35d 100644
--- a/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
+++ b/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
@@ -16,6 +16,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { runtime } from "../src/risa";
 import { risaLocate } from "../src/risaSync";
 import { buildRisaMetronomeSav } from "./risaSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 
@@ -31,7 +35,7 @@ test("a stop, a rewind and a mid-song jump each land risa on the row the arm nam
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: buildRisaMetronomeSav(),
   }, id)).toBeTruthy();
@@ -103,7 +107,7 @@ test("the beat grid still holds on a run that follows a stop and rewind", () =>
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: buildRisaMetronomeSav(),
   }, id)).toBeTruthy();
diff --git a/packages/retroplug/test-native/dsp-risa-sync.test.ts b/packages/retroplug/test-native/dsp-risa-sync.test.ts
index 1bd575a7..50cf1173 100644
--- a/packages/retroplug/test-native/dsp-risa-sync.test.ts
+++ b/packages/retroplug/test-native/dsp-risa-sync.test.ts
@@ -11,6 +11,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { isRisaSyncRom, risaMarkerVersion } from "../src/risa";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 
@@ -41,7 +45,7 @@ test("the risa-sync role in the DSP kernel is the sole clock that makes risa 2.3
   const audio = createAudioDriver();
 
   const id = 1; // TS owns the id counter; this direct-backend test picks its own (fresh host per file)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ROM_230,
     platform: "nes",
     core: "mesen",
@@ -78,7 +82,7 @@ test("stopping the transport stops risa, and restarting it plays again", () => {
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "", savPath: LETGO, statePath: null,
   }, id)).toBeTruthy();
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
diff --git a/packages/retroplug/test-native/dsp-sms-sync-drift.test.ts b/packages/retroplug/test-native/dsp-sms-sync-drift.test.ts
index 81313a05..6419f044 100644
--- a/packages/retroplug/test-native/dsp-sms-sync-drift.test.ts
+++ b/packages/retroplug/test-native/dsp-sms-sync-drift.test.ts
@@ -34,6 +34,10 @@ import {
   SMS_SYNC_IN24,
   SMS_ROWS_PER_BEAT,
 } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 declare const __REPO_RESOURCES_DIR__: string;
@@ -114,7 +118,7 @@ for (const m of MACHINES) {
     const sr = audio.sampleRate();
 
     expect(
-      be.constructSystem(
+      graph.add(
         {
           romPath: m.rom,
           platform: m.platform,
@@ -218,6 +222,6 @@ for (const m of MACHINES) {
     expect(be.writeFile(wav, encodeWav(pcm, sr, 2))).toBeTruthy();
     console.log(`[sms-drift ${m.machine}] wrote ${wav} (analyze: tools/reaper-timing-analyze.py ${wav} --drift)`);
 
-    expect(be.removeSystem(1)).toBeTruthy();
+    expect(graph.remove(1)).toBeTruthy();
   });
 }
diff --git a/packages/retroplug/test-native/dsp-sms-sync.test.ts b/packages/retroplug/test-native/dsp-sms-sync.test.ts
index f20b598a..7bcc1c63 100644
--- a/packages/retroplug/test-native/dsp-sms-sync.test.ts
+++ b/packages/retroplug/test-native/dsp-sms-sync.test.ts
@@ -22,6 +22,10 @@ import {
   SMS_SYNC_OFF,
   SMS_SYNC_IN24,
 } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __DSP_KERNEL_BUNDLE__: string;
 declare const __REPO_RESOURCES_DIR__: string;
@@ -117,7 +121,7 @@ function boot(syncMode: number, m: Machine) {
   const be = createRealBackend();
   const audio = createAudioDriver();
   const id = nextSystemId++;
-  const ok = be.constructSystem({
+  const ok = graph.add({
     romPath: m.rom,
     platform: m.platform,
     core: "mesen",
@@ -210,7 +214,7 @@ for (const m of MACHINES) {
     expect(pos > 0.001).toBeTruthy(); // the kernel's level stream advances smsggdj -> audible
     expect(Math.abs(posRows - 24) <= 2).toBeTruthy(); // ~24 rows: one clock stream at the DAW's tempo
     expect(syncState(be, id)?.wait).toBe(0); // WAIT -> PLAY, flipped by the role's first clock
-    expect(be.removeSystem(id)).toBeTruthy();
+    expect(graph.remove(id)).toBeTruthy();
   });
 
   test(`${m.machine.toUpperCase()}: stopping the DAW transport stops the song, and restarting it resumes`, () => {
@@ -246,6 +250,6 @@ for (const m of MACHINES) {
     expect(stopped < play / 10).toBeTruthy(); // what is left is a held note, not a running song
     expect(againRows > 0).toBeTruthy(); // clocks resume -> so does the song
     expect(again > play / 2).toBeTruthy();
-    expect(be.removeSystem(id)).toBeTruthy();
+    expect(graph.remove(id)).toBeTruthy();
   });
 }
diff --git a/packages/retroplug/test-native/dsp-threaded.test.ts b/packages/retroplug/test-native/dsp-threaded.test.ts
index a5571732..29062d4f 100644
--- a/packages/retroplug/test-native/dsp-threaded.test.ts
+++ b/packages/retroplug/test-native/dsp-threaded.test.ts
@@ -13,6 +13,10 @@ import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { LsdjSyncMode } from "../src/settingsEnums";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -47,7 +51,7 @@ test("the lsdj-sync role clocks LSDj on a background audio thread, toggled via t
   // --- setup (single-threaded, before the audio thread) ---
   const sav = savFrom(SYNC_MIDI_SONG);
   const id = 1; // TS owns the id counter; this direct-backend test picks its own (fresh host per file)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-arduinoboy-slave.test.ts b/packages/retroplug/test-native/lsdj-arduinoboy-slave.test.ts
index 29e89964..99bc1d76 100644
--- a/packages/retroplug/test-native/lsdj-arduinoboy-slave.test.ts
+++ b/packages/retroplug/test-native/lsdj-arduinoboy-slave.test.ts
@@ -8,6 +8,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -48,7 +52,7 @@ test("the TS lsdj-sync Arduinoboy-slave role plays a real LSDj on note-24, gates
   const audio = createAudioDriver();
 
   const id = 1; // direct-backend test picks its own id (fresh host per file)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-drift-detect.test.ts b/packages/retroplug/test-native/lsdj-drift-detect.test.ts
index 9ef2ccb8..3c0e2e15 100644
--- a/packages/retroplug/test-native/lsdj-drift-detect.test.ts
+++ b/packages/retroplug/test-native/lsdj-drift-detect.test.ts
@@ -17,6 +17,10 @@ import { detectDriftLayout } from "../src/lsdj/runtime/detect";
 import type { DetectDriver } from "../src/lsdj/runtime/detect";
 import { legacyOffsets } from "../src/lsdj/runtime/legacyOffsets.generated";
 import { driftShifts } from "../src/lsdj/runtime/driftShifts.generated";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const DIR = __RESOURCES_DIR__ + "/roms/lsdj";
@@ -64,8 +68,8 @@ test("detectDriftLayout: full screen/tempo/cursor layout on real cores; cross-ch
   const audio = createAudioDriver();
   const driver: DetectDriver = {
     readFilePrefix: (p, n) => be.readFilePrefix(p, n),
-    construct: (id, romPath, sram) => be.constructSystem({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
-    remove: (id) => { be.removeSystem(id); },
+    construct: (id, romPath, sram) => graph.add({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
+    remove: (id) => { graph.remove(id); },
     readWram: (id) => be.readMemory(id, MemoryRegion.Ram),
     press: (id, button, down) => audio.pressButton(id, button, down),
     render: (ms) => audio.renderAudio(ms),
diff --git a/packages/retroplug/test-native/lsdj-keyboardmidi.test.ts b/packages/retroplug/test-native/lsdj-keyboardmidi.test.ts
index e479b4ce..556411de 100644
--- a/packages/retroplug/test-native/lsdj-keyboardmidi.test.ts
+++ b/packages/retroplug/test-native/lsdj-keyboardmidi.test.ts
@@ -18,6 +18,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -58,7 +62,7 @@ test("KeyboardMidi: MIDI notes play live on a real LSDj via the PS/2-keyboard se
   const audio = createAudioDriver();
 
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-launchpad-role.test.ts b/packages/retroplug/test-native/lsdj-launchpad-role.test.ts
index 58649c1b..dd12cedf 100644
--- a/packages/retroplug/test-native/lsdj-launchpad-role.test.ts
+++ b/packages/retroplug/test-native/lsdj-launchpad-role.test.ts
@@ -16,6 +16,10 @@ import { LsdjReader } from "../src/lsdj/runtime";
 import { SongSchema } from "../src/lsdj/model";
 import { songRowTicks } from "../src/lsdj/playback";
 import { padIndex } from "../src/launchpad";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -49,7 +53,7 @@ test("a pad press on the controller stream launches a row on a real cart", () =>
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
 
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
   }, SYSTEM)).toBeTruthy();
@@ -100,7 +104,7 @@ test("the same bytes on the MUSICAL stream launch a different row, which is why
   const id = 2;
   const dsp = createDspRuntime();
   const audio = createAudioDriver();
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom({
       workingSong: { ...songJson, rows: Array.from({ length: 64 }, () => ({ chains: [0, 0, 0, 0] })) },
diff --git a/packages/retroplug/test-native/lsdj-legacy-offsets.test.ts b/packages/retroplug/test-native/lsdj-legacy-offsets.test.ts
index ee3994d6..88d77dd7 100644
--- a/packages/retroplug/test-native/lsdj-legacy-offsets.test.ts
+++ b/packages/retroplug/test-native/lsdj-legacy-offsets.test.ts
@@ -9,6 +9,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { MemoryRegion } from "../src/backend";
 import { LsdjReader } from "../src/lsdj/runtime";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj6_9_0.gb";
@@ -22,7 +26,7 @@ test("ported legacy offsets: v6.9.0 resolves the 232-block and its SONG cursor i
   if (!be.fileExists(ROM) || !be.fileExists(SAV)) skip(`lsdj-legacy-offsets: v6.9.0 ROM/sav not found`);
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: SAV, statePath: null }, id)).toBeTruthy();
+  expect(graph.add({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: SAV, statePath: null }, id)).toBeTruthy();
 
   const reader = LsdjReader.fromHeader(be.readFilePrefix(ROM, 0x150)!);
   expect(reader.supported).toBeTruthy();
diff --git a/packages/retroplug/test-native/lsdj-mastersync.test.ts b/packages/retroplug/test-native/lsdj-mastersync.test.ts
index 8afece52..b4b358f1 100644
--- a/packages/retroplug/test-native/lsdj-mastersync.test.ts
+++ b/packages/retroplug/test-native/lsdj-mastersync.test.ts
@@ -14,6 +14,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -50,7 +54,7 @@ test("Master Sync (mode 8): a stock LSDj in SYNC=LSDj clocks the host — byte
   const audio = createAudioDriver();
 
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-midimap.test.ts b/packages/retroplug/test-native/lsdj-midimap.test.ts
index 77f9f4ac..8d44ae13 100644
--- a/packages/retroplug/test-native/lsdj-midimap.test.ts
+++ b/packages/retroplug/test-native/lsdj-midimap.test.ts
@@ -8,6 +8,10 @@ import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { LsdjProbe, transitions, changeMs, gaps, fmtSample } from "./lsdjPlaybackProbe";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -46,7 +50,7 @@ test("the TS lsdj-sync MidiMap role maps MIDI notes to LSDj row bytes on a real
   const audio = createAudioDriver();
 
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-midiout.test.ts b/packages/retroplug/test-native/lsdj-midiout.test.ts
index 0294ce4c..644fc4ad 100644
--- a/packages/retroplug/test-native/lsdj-midiout.test.ts
+++ b/packages/retroplug/test-native/lsdj-midiout.test.ts
@@ -13,6 +13,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -58,7 +62,7 @@ test("MIDIOUT (mode 7): a real LSDj in SYNC=MI.OUT emits Arduinoboy MIDI the ker
   const audio = createAudioDriver();
 
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: ABOY,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-old-drift.test.ts b/packages/retroplug/test-native/lsdj-old-drift.test.ts
index c4258faa..aeb6bea9 100644
--- a/packages/retroplug/test-native/lsdj-old-drift.test.ts
+++ b/packages/retroplug/test-native/lsdj-old-drift.test.ts
@@ -10,6 +10,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { decodeSav, savFrom } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
 import { LsdjReader } from "../src/lsdj/runtime";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const DIR = __RESOURCES_DIR__ + "/roms/lsdj";
@@ -46,7 +50,7 @@ for (const slug of ["lsdj5_0_3", "lsdj6_9_0"]) {
     const audio = createAudioDriver();
     const id = 1;
     const fmt = decodeSav(be.readFilePrefix(SAV, 0x20000)!).workingSong.formatVersion;
-    expect(be.constructSystem({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: detectionSong(fmt) }, id)).toBeTruthy();
+    expect(graph.add({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: detectionSong(fmt) }, id)).toBeTruthy();
     const decode = () => reader.read(be.readMemory(id, MemoryRegion.Ram)!);
     const chord = (dir: number) => { audio.pressButton(id, SELECT, true); audio.renderAudio(200); audio.pressButton(id, dir, true); audio.renderAudio(90); audio.pressButton(id, dir, false); audio.renderAudio(40); audio.pressButton(id, SELECT, false); audio.renderAudio(150); };
 
@@ -70,6 +74,6 @@ for (const slug of ["lsdj5_0_3", "lsdj6_9_0"]) {
     console.log(`[${slug}] CHAIN cursor row ${before.row} → ${after.row}`);
     expect(after.row).toBe(before.row + 1);
 
-    be.removeSystem(id);
+    graph.remove(id);
   });
 }
diff --git a/packages/retroplug/test-native/lsdj-passthrough.test.ts b/packages/retroplug/test-native/lsdj-passthrough.test.ts
index 35a52f74..6889f301 100644
--- a/packages/retroplug/test-native/lsdj-passthrough.test.ts
+++ b/packages/retroplug/test-native/lsdj-passthrough.test.ts
@@ -7,6 +7,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -34,7 +38,7 @@ test("the TS lsdj-sync MidiPassthrough role forwards raw MIDI to a real LSDj cor
   const audio = createAudioDriver();
 
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ,
     platform: "gb",
     core: "sameboy",
diff --git a/packages/retroplug/test-native/lsdj-rom.test.ts b/packages/retroplug/test-native/lsdj-rom.test.ts
index 524eb3a5..d8f08e8e 100644
--- a/packages/retroplug/test-native/lsdj-rom.test.ts
+++ b/packages/retroplug/test-native/lsdj-rom.test.ts
@@ -11,6 +11,10 @@ import { KitView, decodeNibbles } from "../src/lsdj/rom/kit";
 import { KIT_LOOKUP, BANK_SIZE, ROM_SIZE } from "../src/lsdj/rom/constants";
 import { buildKitBank, sampleBytesFromBank, kitSampleSpace } from "../src/lsdj/rom";
 import { encodeWav } from "../cli/wav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
@@ -88,7 +92,7 @@ test("LsdjRom reads real kit/palette/font assets and round-trips a surgical kit-
 
   // The patched ROM still boots on a real SameBoy core.
   const audio = createAudioDriver();
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null,
   }, 1)).toBeTruthy();
@@ -159,7 +163,7 @@ test("import-sample splice: compile one sample + rebuild a kit around the existi
   expect([...re.rawSampleBytes(1)]).toEqual(originalSlot1);
 
   // The patched ROM still boots on a real core.
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: out, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null,
   }, 2)).toBeTruthy();
   audio.renderAudio(300);
@@ -255,7 +259,7 @@ test("import a real .png font (main + extended) via the native codec; ROM boots;
 
   // The patched ROM boots on a real core.
   expect(be.writeFileAtomic(OUT, rom.bytes())).toBeTruthy();
-  expect(be.constructSystem({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 3)).toBeTruthy();
+  expect(graph.add({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 3)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(3) != null).toBeTruthy();
   console.log(`[lsdj-rom] font-file: 7.png (main) + BLSD.png (extended gfx) import, round-trip stable, ROM boots`);
@@ -281,7 +285,7 @@ test("import a real .lsdpal palette (colours + name) and a real .kit bank; both
   expect(rom.kit(20).sampleCount() > 0).toBeTruthy();
 
   expect(be.writeFileAtomic(OUT, rom.bytes())).toBeTruthy();
-  expect(be.constructSystem({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 4)).toBeTruthy();
+  expect(graph.add({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 4)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(4) != null).toBeTruthy();
   console.log(`[lsdj-rom] pal/kit-file: 0D-BLOO.lsdpal + DONK.kit import byte-identical, ROM boots`);
@@ -316,7 +320,7 @@ test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", (
   expect(rom.kit(20).name()).toBe("MYDR");
   expect(rom.kit(20).sampleName(0)).toBe("BD-"); // the native compiler pads a short name to 3 chars with '-'
 
-  expect(be.constructSystem({ romPath: "/tmp/rp-bk.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 5)).toBeTruthy();
+  expect(graph.add({ romPath: "/tmp/rp-bk.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 5)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(5) != null).toBeTruthy();
   console.log(`[lsdj-rom] build-kit → .kit (16KB) → import-kit slot 20 (MYDR/BD), ROM boots`);
@@ -352,7 +356,7 @@ test("CLI patch realizes a mixed manifest (build + .kit + rename + palette + fon
   expect(rom.palettes()[1].name).toBe("BLOO"); // .lsdpal
   expect(rom.palettes()[0].color(0, 0)).toEqual({ r: 255, g: 0, b: 0 }); // colour set
 
-  expect(be.constructSystem({ romPath: "/tmp/rp-ap-out.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 6)).toBeTruthy();
+  expect(graph.add({ romPath: "/tmp/rp-ap-out.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 6)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(6) != null).toBeTruthy();
   console.log(`[lsdj-rom] patch: build+import+rename+palette+font manifest realized, ROM boots`);
@@ -370,7 +374,7 @@ test("constructSystem romBytes loads a patched effective ROM (over the on-disk r
   const effective = patched.bytes();
 
   // Construct with romPath = the STOCK ROM but romBytes = the patched image; native must honour romBytes.
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, romBytes: effective,
   }, 7)).toBeTruthy();
   audio.renderAudio(300);
diff --git a/packages/retroplug/test-native/lsdj-runtime.test.ts b/packages/retroplug/test-native/lsdj-runtime.test.ts
index 0fa8e068..6f42b0c6 100644
--- a/packages/retroplug/test-native/lsdj-runtime.test.ts
+++ b/packages/retroplug/test-native/lsdj-runtime.test.ts
@@ -8,6 +8,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
 import { LsdjReader } from "../src/lsdj/runtime";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
@@ -36,7 +40,7 @@ test("LsdjReader decodes live 9.4.2 WRAM: identify, screen, playing flags, posit
   if (!be.fileExists(LSDJ)) skip(`lsdj-runtime: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
   }, id)).toBeTruthy();
diff --git a/packages/retroplug/test-native/lsdj-sync-negative.test.ts b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
index 013cc5f9..a54ca5af 100644
--- a/packages/retroplug/test-native/lsdj-sync-negative.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
@@ -6,6 +6,10 @@ import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SongSettings } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 
@@ -23,8 +27,8 @@ const songSav = (sync: SongSettings["syncMode"]) => savFrom({
   },
 });
 
-const construct = (be: ReturnType<typeof createRealBackend>, id: number, sync: SongSettings["syncMode"]) =>
-  be.constructSystem({
+const construct = (id: number, sync: SongSettings["syncMode"]) =>
+  graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: songSav(sync),
   }, id);
@@ -42,10 +46,10 @@ test("LSDj link-cable sync control: the follower stays silent without SYNC=LSDJ"
   const audio = createAudioDriver();
 
   const leader = 1, follower = 2;
-  expect(construct(be, leader, "None")).toBeTruthy();
-  expect(construct(be, follower, "None")).toBeTruthy(); // SYNC=None → no link response
-  expect(be.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
-  expect(be.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(construct(leader, "None")).toBeTruthy();
+  expect(construct(follower, "None")).toBeTruthy(); // SYNC=None → no link response
+  expect(graph.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
 
   audio.renderAudio(6000);
 
diff --git a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
index 96241667..a15e6b74 100644
--- a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
@@ -9,6 +9,10 @@ import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SongSettings } from "../src/lsdjSav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 
@@ -27,8 +31,8 @@ const songSav = (sync: SongSettings["syncMode"]) => savFrom({
   },
 });
 
-const construct = (be: ReturnType<typeof createRealBackend>, id: number, sync: SongSettings["syncMode"]) =>
-  be.constructSystem({
+const construct = (id: number, sync: SongSettings["syncMode"]) =>
+  graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: songSav(sync),
   }, id);
@@ -46,11 +50,11 @@ test("LSDj link-cable sync: the follower plays in lockstep with the leader (per-
   const audio = createAudioDriver();
 
   const leader = 1, follower = 2;
-  expect(construct(be, leader, "Lsdj")).toBeTruthy();
-  expect(construct(be, follower, "Lsdj")).toBeTruthy(); // same one-note SYNC=LSDJ song
+  expect(construct(leader, "Lsdj")).toBeTruthy();
+  expect(construct(follower, "Lsdj")).toBeTruthy(); // same one-note SYNC=LSDJ song
   // Put both in link group 1 → the translator wires the cable and the pair steps as one coupled group.
-  expect(be.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
-  expect(be.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.applyRoleConfig(leader, "sameboy", { linkGroupId: 1 })).toBeTruthy();
+  expect(graph.applyRoleConfig(follower, "sameboy", { linkGroupId: 1 })).toBeTruthy();
 
   audio.renderAudio(6000); // valid savs skip the self-test; still need a few s to the song screen
 
diff --git a/packages/retroplug/test-native/lsdj-wram-seam.test.ts b/packages/retroplug/test-native/lsdj-wram-seam.test.ts
index f3418424..23155838 100644
--- a/packages/retroplug/test-native/lsdj-wram-seam.test.ts
+++ b/packages/retroplug/test-native/lsdj-wram-seam.test.ts
@@ -8,6 +8,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
 import { LsdjReader } from "../src/lsdj/runtime";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
@@ -41,7 +45,7 @@ test("readRam publishes live WRAM per block on the emulator facet, and drives th
   if (!be.fileExists(LSDJ)) skip(`lsdj-wram-seam: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
   }, id)).toBeTruthy();
@@ -82,7 +86,7 @@ test("readRam tracks a live model switch that resizes WRAM (CGB 32K → DMG 8K),
   if (!be.fileExists(LSDJ)) skip(`lsdj-wram-model-switch: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
   const id = 2; // a distinct id: the host/engine is shared across a file's tests, so test 1's id=1 lingers
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
   }, id)).toBeTruthy();
@@ -91,7 +95,7 @@ test("readRam tracks a live model switch that resizes WRAM (CGB 32K → DMG 8K),
   expect(be.readRam(id)!.length).toBe(0x8000); // cgbC default → 32K CGB WRAM
 
   // Switch to a DMG-family model → restartEmulator rebuilds the core with an 8K WRAM region.
-  expect(be.applyRoleConfig(id, "sameboy", { model: 1 })).toBeTruthy(); // dmgB
+  expect(graph.applyRoleConfig(id, "sameboy", { model: 1 })).toBeTruthy(); // dmgB
   audio.renderAudio(2000); // a block republishes the (now smaller) region
 
   const dmgRam = be.readRam(id)!;
diff --git a/packages/retroplug/test-native/lsdj-wram-spike.test.ts b/packages/retroplug/test-native/lsdj-wram-spike.test.ts
index 611554f1..e100570f 100644
--- a/packages/retroplug/test-native/lsdj-wram-spike.test.ts
+++ b/packages/retroplug/test-native/lsdj-wram-spike.test.ts
@@ -13,6 +13,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
@@ -47,7 +51,7 @@ test("LSDj WRAM spike: readMemory(Ram) on SameBoy + differential offset detectio
   if (!be.fileExists(LSDJ)) skip(`lsdj-wram-spike: LSDj ROM not found at ${LSDJ}`);
   const audio = createAudioDriver();
   const id = 1;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
   }, id)).toBeTruthy();
diff --git a/packages/retroplug/test-native/lsdjPlaybackProbe.ts b/packages/retroplug/test-native/lsdjPlaybackProbe.ts
index 8531c617..dcd67140 100644
--- a/packages/retroplug/test-native/lsdjPlaybackProbe.ts
+++ b/packages/retroplug/test-native/lsdjPlaybackProbe.ts
@@ -26,6 +26,10 @@ import { createDspRuntime } from "../src/dspRuntime";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { LsdjReader, type LsdjState, CHANNELS, type ChannelName } from "../src/lsdj/runtime";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __DSP_KERNEL_BUNDLE__: string;
@@ -122,7 +126,7 @@ export class LsdjProbe {
     // A fresh id per probe: every test in this file shares ONE native host process, so reusing an id
     // would collide with the cart a previous test left running (TS owns system identity - doc-01).
     const id = nextSystemId++;
-    if (!be.constructSystem({
+    if (!graph.add({
       romPath: rom, platform: "gb", core: "sameboy", embeddedRom: "",
       savPath: null, statePath: null, sramBytes: savFrom(opts.song),
     }, id)) return null;
@@ -146,7 +150,7 @@ export class LsdjProbe {
   close(): void {
     if (this.closed) return;
     this.closed = true;
-    this.be.removeSystem(this.id);
+    graph.remove(this.id);
   }
 
   /** Close every probe built so far. Call it at the TOP of each test rather than the bottom: a test
diff --git a/packages/retroplug/test-native/mesen-state-ceiling.test.ts b/packages/retroplug/test-native/mesen-state-ceiling.test.ts
index 7f50c57d..71287697 100644
--- a/packages/retroplug/test-native/mesen-state-ceiling.test.ts
+++ b/packages/retroplug/test-native/mesen-state-ceiling.test.ts
@@ -18,6 +18,10 @@
 import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 declare const __REPO_RESOURCES_DIR__: string;
@@ -36,7 +40,7 @@ test("GBA: the published savestate tracks the running core, never freezing on th
   if (!be.fileExists(GBA)) skip(`mesen-state-ceiling GBA leg: no GBA ROM at ${GBA}`);
   const audio = createAudioDriver();
   const id = 801;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: GBA, platform: "gba", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null,
   }, id)).toBeTruthy();
@@ -54,14 +58,14 @@ test("GBA: the published savestate tracks the running core, never freezing on th
   expect(eq(warm, boot)).toBeFalsy();            // a frozen slot hands back the identical blob
   expect(warm.length > boot.length).toBeTruthy(); // ...and a populated console deflates worse than a blank one
 
-  be.removeSystem(id);
+  graph.remove(id);
 });
 
 test("NES: the published savestate tracks the running core", () => {
   const be = createRealBackend();
   const audio = createAudioDriver();
   const id = 802;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: NES, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null,
   }, id)).toBeTruthy();
@@ -75,5 +79,5 @@ test("NES: the published savestate tracks the running core", () => {
   console.log(`[mesen-state-ceiling] nes boot=${boot.length} warm=${warm.length}`);
   expect(eq(warm, boot)).toBeFalsy();
 
-  be.removeSystem(id);
+  graph.remove(id);
 });
diff --git a/packages/retroplug/test-native/parity/render-blocks.test.ts b/packages/retroplug/test-native/parity/render-blocks.test.ts
index 07447c42..d916186c 100644
--- a/packages/retroplug/test-native/parity/render-blocks.test.ts
+++ b/packages/retroplug/test-native/parity/render-blocks.test.ts
@@ -5,6 +5,10 @@ import { test, expect, skip } from "../../testing/harness";
 import { bootSession } from "../../src/bootSession";
 import { savFrom, type SavInput } from "../../src/lsdjSav";
 import type { RenderBlock } from "../../src/audioDriver";
+import { directGraph } from "../../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 
@@ -160,7 +164,7 @@ test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, t
   const scene = (render: (ms: number) => Msg[]): Msg[] => {
     project.newProject();
     expect(
-      be.constructSystem(
+      graph.add(
         { romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: savFrom(MIDIOUT_SONG) },
         id,
       ),
@@ -185,7 +189,7 @@ test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, t
     out.push(...render(300));
     tap();
     out.push(...render(1500));
-    be.removeSystem(id);
+    graph.remove(id);
     project.newProject();
     return out;
   };
diff --git a/packages/retroplug/test-native/render-guard.test.ts b/packages/retroplug/test-native/render-guard.test.ts
index 43ba5cf8..97103948 100644
--- a/packages/retroplug/test-native/render-guard.test.ts
+++ b/packages/retroplug/test-native/render-guard.test.ts
@@ -2,16 +2,18 @@
 // the background audio thread owns it, they must refuse (as renderBlocks does) rather than render
 // concurrently with it; once the thread has stopped they render again.
 import { test, expect } from "../testing/harness";
-import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import type { ConstructSpec } from "../src/backend";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };
 
 test("renderAudio, renderAudioPerSystem and renderAudioPerChannel refuse while the audio thread runs", () => {
-  const be = createRealBackend();
   const audio = createAudioDriver();
-  expect(be.constructSystem(MGB, 1)).toBeTruthy();
+  expect(graph.add(MGB, 1)).toBeTruthy();
 
   expect(audio.startAudio()).toBeTruthy();
   expect(audio.renderAudio(20).length).toBe(0);
@@ -22,5 +24,5 @@ test("renderAudio, renderAudioPerSystem and renderAudioPerChannel refuse while t
   expect(audio.renderAudio(20).length).toBeGreaterThan(0);
   expect(audio.renderAudioPerSystem(20).length).toBe(1);
   expect(audio.renderAudioPerChannel(1, 20).length).toBe(4);
-  be.removeSystem(1);
+  graph.remove(1);
 });
diff --git a/packages/retroplug/test-native/risa-kit.test.ts b/packages/retroplug/test-native/risa-kit.test.ts
index 33329ad4..8c364fbd 100644
--- a/packages/retroplug/test-native/risa-kit.test.ts
+++ b/packages/retroplug/test-native/risa-kit.test.ts
@@ -8,6 +8,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { RisaRom, bankToModel, isBankPopulated, dpcmDecode, assembleKitBank } from "../src/risa/rom";
 import { applyOverridesToRom, type RisaAssetOverride } from "../src/risaAssetsRole";
 import { encodeWav } from "../cli/wav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RISA_SRC__: string;
 const RISA_ROM = __RISA_SRC__ + "/build/risa-pal.nes";
@@ -74,7 +78,7 @@ test("a compiled DMC kit splices into a real risa ROM (bank + mirror) and the RO
 
   // The patched image boots (also exercises the Mesen romBytes path), on-disk ROM untouched.
   const base = be.readFile(RISA_ROM)!;
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: rom.bytes(),
   }, 21)).toBeTruthy();
@@ -138,7 +142,7 @@ test("the risa-assets kit override links a pre-built .rkit at construct (offline
   expect(rom.kits().some((k) => k.slot === 0 && k.name === "DRUMS")).toBe(true); // mirror-consistent
 
   // Mesen boots from the patched bytes (the romBytes channel); on-disk .nes + .rkit unchanged.
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: patched,
   }, 22)).toBeTruthy();
diff --git a/packages/retroplug/test-native/risa-rom-cli.test.ts b/packages/retroplug/test-native/risa-rom-cli.test.ts
index 363d51b1..e94b4030 100644
--- a/packages/retroplug/test-native/risa-rom-cli.test.ts
+++ b/packages/retroplug/test-native/risa-rom-cli.test.ts
@@ -8,6 +8,10 @@ import type { Session } from "../cli/session";
 import { risaRomTool } from "../cli/sessions/risa-rom";
 import { RisaRom, bankToModel, decodeThemeFromRom } from "../src/risa/rom";
 import { encodeWav } from "../cli/wav";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RISA_SRC__: string;
 const RISA_ROM = __RISA_SRC__ + "/build/risa-pal.nes";
@@ -48,7 +52,7 @@ test("risa-rom build-kit writes a bootable .rkit that import-kit places into a R
   expect(patched.isKitPopulated(0)).toBe(true);
   expect(patched.kits().some((k) => k.slot === 0 && k.name === "MYDR")).toBe(true);
 
-  expect(be.constructSystem({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 30)).toBeTruthy();
+  expect(graph.add({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 30)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(30) != null).toBeTruthy();
   console.log(`[risa-rom] build-kit → .rkit (8KB) → import-kit slot 0 (MYDR/BD); ROM boots`);
@@ -128,7 +132,7 @@ test("risa-rom patch realizes a mixed manifest (build kit + import theme + impor
   const rom = RisaRom.fromBytes(be.readFile("/tmp/rp-pt-out.nes")!);
   expect(rom.kits().some((k) => k.slot === 1 && k.name === "PT")).toBeTruthy();
 
-  expect(be.constructSystem({ romPath: "/tmp/rp-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 31)).toBeTruthy();
+  expect(graph.add({ romPath: "/tmp/rp-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 31)).toBeTruthy();
   audio.renderAudio(300);
   expect(be.getFrame(31) != null).toBeTruthy();
   console.log(`[risa-rom] patch: built kit 1 + theme 3 + font 2; patched ROM boots`);
diff --git a/packages/retroplug/test-native/risa-rom.test.ts b/packages/retroplug/test-native/risa-rom.test.ts
index eae1a4ec..a6dcdf6d 100644
--- a/packages/retroplug/test-native/risa-rom.test.ts
+++ b/packages/retroplug/test-native/risa-rom.test.ts
@@ -11,6 +11,10 @@ import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { rom as risaRom } from "../src/risa";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RISA_SRC__: string;
 const RISA_ROM = __RISA_SRC__ + "/build/risa-pal.nes";
@@ -22,7 +26,7 @@ test("constructSystem romBytes boots a NES (Mesen) system over a nonexistent rom
 
   const bytes = be.readFile(RISA_ROM)!;
   // romPath points at nothing; only romBytes can boot this. (Pre-fix Mesen slurps the path -> nullptr.)
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: "/definitely/not/here/nope.nes", platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: bytes,
   }, 11)).toBeTruthy();
@@ -51,7 +55,7 @@ test("a theme + font patched into the ROM in memory boots (the on-disk .nes is u
   rom.setChrFontSlot(0, font);
   const effective = rom.bytes();
 
-  expect(be.constructSystem({
+  expect(graph.add({
     romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
     savPath: null, statePath: null, romBytes: effective,
   }, 12)).toBeTruthy();
diff --git a/packages/retroplug/test-native/sameboy-state-model-switch.test.ts b/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
index 6a9843fe..6cb3e1c9 100644
--- a/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
+++ b/packages/retroplug/test-native/sameboy-state-model-switch.test.ts
@@ -18,6 +18,10 @@ import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
 import { savFrom, type SavInput } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __RESOURCES_DIR__: string;
 const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
@@ -49,8 +53,8 @@ const eq = (a: Uint8Array, b: Uint8Array): boolean => {
 
 /** Build a system on an explicit model. `settings` is the native SameBoyRoleConfig (integer fields),
  *  applied AT CONSTRUCT - which is what sizes the snapshot slot for that model. */
-const buildOn = (be: ReturnType<typeof createRealBackend>, id: number, model: number): boolean =>
-  be.constructSystem({
+const buildOn = (id: number, model: number): boolean =>
+  graph.add({
     romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "",
     savPath: null, statePath: null, sramBytes: savFrom(SONG),
     settings: JSON.stringify({ model }),
@@ -65,7 +69,7 @@ test("readState tracks a live model switch that GROWS the savestate (dmgB → cg
   requireRom(be, "state-model-grow");
   const audio = createAudioDriver();
   const id = 101;
-  expect(buildOn(be, id, DMG_B)).toBeTruthy();
+  expect(buildOn(id, DMG_B)).toBeTruthy();
 
   audio.renderAudio(2000);
   const dmgState = be.readState(id)!;
@@ -73,7 +77,7 @@ test("readState tracks a live model switch that GROWS the savestate (dmgB → cg
   expect(be.readRam(id)!.length).toBe(DMG_WRAM); // corroborates the core really booted as DMG
 
   // Switch UP: the CGB core's savestate is exactly RAM_VRAM_DELTA larger than the DMG one it replaces.
-  expect(be.applyRoleConfig(id, "sameboy", { model: CGB_C })).toBeTruthy();
+  expect(graph.applyRoleConfig(id, "sameboy", { model: CGB_C })).toBeTruthy();
   audio.renderAudio(2000);
 
   const cgbState = be.readState(id)!;
@@ -90,7 +94,7 @@ test("readState tracks a live model switch that GROWS the savestate (dmgB → cg
   audio.renderAudio(1600); // past the 0.5s publish interval, with the cart running
   expect(eq(be.readState(id)!, cgbState)).toBeFalsy();
 
-  be.removeSystem(id);
+  graph.remove(id);
 });
 
 test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB → sgb)", () => {
@@ -98,7 +102,7 @@ test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB 
   requireRom(be, "state-model-sgb");
   const audio = createAudioDriver();
   const id = 102;
-  expect(buildOn(be, id, DMG_B)).toBeTruthy();
+  expect(buildOn(id, DMG_B)).toBeTruthy();
 
   audio.renderAudio(2000);
   const dmgState = be.readState(id)!;
@@ -109,7 +113,7 @@ test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB 
   // DMG<->CGB switch this genuinely MOVES Sram.offset. A fix that only widened the slot (without
   // re-reading the region table) would slice here at the stale, too-low offset and hand the SRAM
   // auto-save mirror the SGB section's bytes as if they were the battery.
-  expect(be.applyRoleConfig(id, "sameboy", { model: SGB })).toBeTruthy();
+  expect(graph.applyRoleConfig(id, "sameboy", { model: SGB })).toBeTruthy();
   audio.renderAudio(2000);
 
   const sgbState = be.readState(id)!;
@@ -121,7 +125,7 @@ test("readSram stays correct across a switch that MOVES the SRAM offset (dmgB 
   expect(sgbSram.length).toBe(dmgSram.length); // cart-derived, so it never moves with the model
   expect(eq(sgbSram, be.readMemory(id, MemoryRegion.Sram)!)).toBeTruthy();
 
-  be.removeSystem(id);
+  graph.remove(id);
 });
 
 // Anti-rot: hardcodes no size at all. Switching INTO a model must be indistinguishable from being BUILT
@@ -134,16 +138,16 @@ test("a switched-into model publishes the same state size as one constructed on
   let id = 110;
 
   const live = id++;
-  expect(buildOn(be, live, DMG_B)).toBeTruthy(); // start on the SMALLEST family, so every switch grows
+  expect(buildOn(live, DMG_B)).toBeTruthy(); // start on the SMALLEST family, so every switch grows
 
   for (const m of models) {
     const fresh = id++;
-    expect(buildOn(be, fresh, m)).toBeTruthy();
+    expect(buildOn(fresh, m)).toBeTruthy();
     audio.renderAudio(1600);
     const built = be.readState(fresh)!.length;
-    be.removeSystem(fresh); // a live system is emulated by every later renderAudio in this file
+    graph.remove(fresh); // a live system is emulated by every later renderAudio in this file
 
-    expect(be.applyRoleConfig(live, "sameboy", { model: m })).toBeTruthy();
+    expect(graph.applyRoleConfig(live, "sameboy", { model: m })).toBeTruthy();
     audio.renderAudio(1600);
     const switched = be.readState(live)!.length;
 
@@ -151,5 +155,5 @@ test("a switched-into model publishes the same state size as one constructed on
     expect(switched).toBe(built);
   }
 
-  be.removeSystem(live);
+  graph.remove(live);
 });
diff --git a/packages/retroplug/test-native/sms-boot-timeline.test.ts b/packages/retroplug/test-native/sms-boot-timeline.test.ts
index 161382cb..2e29ebb4 100644
--- a/packages/retroplug/test-native/sms-boot-timeline.test.ts
+++ b/packages/retroplug/test-native/sms-boot-timeline.test.ts
@@ -27,6 +27,10 @@ import { SMSGGDJ_SYMBOLS } from "../src/smsggdj/runtime/symbols.generated";
 import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
 import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -55,9 +59,9 @@ const nameField = (ram: Uint8Array): Uint8Array => ram.subarray(layout.name, lay
 /** The hunt's invariant: every byte of the name field is a terminator or printable ASCII. */
 const nameFieldClean = (ram: Uint8Array): boolean => nameField(ram).every((c) => c === 0 || (c >= 0x20 && c <= 0x7e));
 
-function construct(be: Backend, id: number, rom: { path: string; platform: Platform }, sav: Uint8Array): void {
+function construct(id: number, rom: { path: string; platform: Platform }, sav: Uint8Array): void {
   expect(
-    be.constructSystem(
+    graph.add(
       { romPath: rom.path, platform: rom.platform, core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
       id,
     ),
@@ -112,7 +116,7 @@ for (const rom of ROMS) {
     const id = 60;
     const sav = buildSav([{ block: buildMetronomeBlock(), name: "ALPHA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
     const audio = createAudioDriver();
-    construct(be, id, rom, sav);
+    construct(id, rom, sav);
 
     // Frame 0: readable, and the boot's. This is the snapshot a project load used to act on.
     const frame0 = be.readRam(id)!;
@@ -139,7 +143,7 @@ for (const rom of ROMS) {
     expect(f2 - f1 >= 20 && f2 - f1 <= 40).toBeTruthy(); // ~30 frames in 500 ms, either region
     expect(smsggdjSongCatalog.workingName(be.readSram(id)!, be.readRam(id)!)).toBe(null); // song_new's blank song, no name
     expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, be.readRam(id)!)).toBe(false);
-    expect(be.removeSystem(id)).toBeTruthy();
+    expect(graph.remove(id)).toBeTruthy();
   });
 
   test(`[${tag}] a song written before the latch is erased by the boot; one written after it stays`, () => {
@@ -148,7 +152,7 @@ for (const rom of ROMS) {
     const id = 61;
     const sav = buildSav([{ block: buildMetronomeBlock(), name: "ALPHA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
     const audio = createAudioDriver();
-    construct(be, id, rom, sav);
+    construct(id, rom, sav);
     const writes = smsggdjIntegration.liveLoad!(be.readFile(rom.path)!, sav, 0)!;
     const wanted = writes.find((w) => w.offset === layout.song)!.bytes;
     const pool = (ram: Uint8Array) => ram.subarray(layout.phrasePool, layout.phrasePool + layout.phrasePoolLen);
@@ -182,7 +186,7 @@ for (const rom of ROMS) {
       expect(nameFieldClean(ram)).toBe(true);
     });
     expect(smsggdjSongCatalog.workingName(be.readSram(id)!, be.readRam(id)!)).toBe("ALPHA");
-    expect(be.removeSystem(id)).toBeTruthy();
+    expect(graph.remove(id)).toBeTruthy();
   });
 
   test(`[${tag}] the name field stays a name through the cart's own FILES save + load, and a live load`, () => {
@@ -192,7 +196,7 @@ for (const rom of ROMS) {
     // Boot BLANK (no directory), so the cart's own save has to build the SMDJ4 structure and copy the
     // name out of work RAM into the entry - the path where a snapshot could catch it half-copied.
     const audio = createAudioDriver();
-    construct(be, id, rom, new Uint8Array(32 * 1024));
+    construct(id, rom, new Uint8Array(32 * 1024));
     const anomalies: string[] = [];
     expect(bootSampled(be, audio, id, anomalies) > 0).toBeTruthy();
 
@@ -237,6 +241,6 @@ for (const rom of ROMS) {
     sample(be, audio, id, 1000, see);
     expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, be.readRam(id)!)).toBe(false); // the cart's own copy, clean
     console.log(`[${tag}] ${samples} post-boot snapshots through FILES save + live load, all clean`);
-    expect(be.removeSystem(id)).toBeTruthy();
+    expect(graph.remove(id)).toBeTruthy();
   });
 }
diff --git a/packages/retroplug/test-native/sms-fm.test.ts b/packages/retroplug/test-native/sms-fm.test.ts
index 2a16dbc2..c2b370c1 100644
--- a/packages/retroplug/test-native/sms-fm.test.ts
+++ b/packages/retroplug/test-native/sms-fm.test.ts
@@ -31,6 +31,10 @@ import {
   SMS_ROWS_PER_BEAT,
   SMS_SYNC_OFF,
 } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
@@ -89,7 +93,7 @@ function play(voices: Voices, enableFm: boolean, cartFm: number): number {
   const block = songBlock(voices);
 
   expect(
-    be.constructSystem(
+    graph.add(
       {
         romPath: ROM,
         platform: "sms",
@@ -114,7 +118,7 @@ function play(voices: Voices, enableFm: boolean, cartFm: number): number {
   audio.pressButton(id, PAUSE, false);
   audio.renderAudio(500);
   const level = rms(audio.renderAudio(2500));
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
   return level;
 }
 
diff --git a/packages/retroplug/test-native/sms-layout.test.ts b/packages/retroplug/test-native/sms-layout.test.ts
index 7725e5ee..d50f4c55 100644
--- a/packages/retroplug/test-native/sms-layout.test.ts
+++ b/packages/retroplug/test-native/sms-layout.test.ts
@@ -31,6 +31,10 @@ import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
 import { identifySmsggdjVersion } from "../src/smsggdj/romDetect";
 import { buildSav, SMDJ4_BLOCK_LEN, songLengthRows } from "../src/smsggdj/codec/sav";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_SONG } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -85,7 +89,7 @@ test("echo_mode is where the symbols say - changing that byte changes the sound"
   const sav = buildSav([{ block, name: "ECHOTEST", echo: echoOff }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
       1,
     ),
@@ -141,7 +145,7 @@ test("echo_mode is where the symbols say - changing that byte changes the sound"
   expect(both[2] < 0xf, "mode 2: T3 is audible too").toBeTruthy();
 
   expect(be.readRam(1)![layout.echo], "echo_mode reads back on the region readRam serves").toBe(2);
-  expect(be.removeSystem(1)).toBeTruthy();
+  expect(graph.remove(1)).toBeTruthy();
 });
 
 test("a load under a RUNNING transport rebases eng_len onto the new song", () => {
@@ -164,7 +168,7 @@ test("a load under a RUNNING transport rebases eng_len onto the new song", () =>
   const sav = buildSav([{ block: long, name: "LONG" }, { block: short, name: "SHORT" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
       5,
     ),
@@ -197,7 +201,7 @@ test("a load under a RUNNING transport rebases eng_len onto the new song", () =>
   // The negative control: with no RAM passed, liveLoad assumes stopped and emits no rebase at all - so a
   // regression that dropped the play_state check would show up as this set being identical.
   expect(smsggdjIntegration.liveLoad!(rom, sav, 1)!.some((w) => w.offset === layout.engLen)).toBe(false);
-  expect(be.removeSystem(5)).toBeTruthy();
+  expect(graph.remove(5)).toBeTruthy();
 });
 
 test("song_name and song_edited land where the symbols say", () => {
@@ -207,7 +211,7 @@ test("song_name and song_edited land where the symbols say", () => {
   const sav = buildSav([{ block: buildMetronomeBlock(), name: "ECHOTEST", echo: echoOn }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
       6,
     ),
@@ -230,5 +234,5 @@ test("song_name and song_edited land where the symbols say", () => {
   // The block still leads work RAM - the assumption the generator asserts, re-checked against the core.
   expect(layout.song, "the song block still leads work RAM").toBe(0);
   expect(ram.length >= SMDJ4_BLOCK_LEN, "readRam serves at least a whole song block").toBeTruthy();
-  expect(be.removeSystem(6)).toBeTruthy();
+  expect(graph.remove(6)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/sms-live-song-load.test.ts b/packages/retroplug/test-native/sms-live-song-load.test.ts
index 58439275..53dd079b 100644
--- a/packages/retroplug/test-native/sms-live-song-load.test.ts
+++ b/packages/retroplug/test-native/sms-live-song-load.test.ts
@@ -16,6 +16,10 @@ import { buildSav, readSongBlock, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/s
 import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
 import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_PHRASES, STEPS_PER_PHRASE, SMS_ROWS_PER_BEAT } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -61,7 +65,7 @@ test("a song decoded from a .sav loads into a PLAYING cart in one call", () => {
 
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       {
         romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
         savPath: null, statePath: null, sramBytes: sav,
@@ -117,5 +121,5 @@ test("a song decoded from a .sav loads into a PLAYING cart in one call", () => {
   expect(after > 0.001).toBeTruthy();
   expect(((stepC - stepB + PHRASE_STEPS) % PHRASE_STEPS) > 0).toBeTruthy(); // sequencer still advancing
 
-  expect(be.removeSystem(1)).toBeTruthy();
+  expect(graph.remove(1)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/sms-repaint.test.ts b/packages/retroplug/test-native/sms-repaint.test.ts
index 3092d558..d098e4fa 100644
--- a/packages/retroplug/test-native/sms-repaint.test.ts
+++ b/packages/retroplug/test-native/sms-repaint.test.ts
@@ -20,6 +20,10 @@ import { buildSav } from "../src/smsggdj/codec/sav";
 import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
 import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_SONG } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -72,7 +76,7 @@ test("a live song load repaints the cart's screen, without the user touching any
 
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
       id,
     ),
@@ -114,5 +118,5 @@ test("a live song load repaints the cart's screen, without the user touching any
   // was doing all the work - which is the whole bug, so both halves are asserted.
   expect(byLoad > 500).toBeTruthy();
   expect(byNudge < 100).toBeTruthy();
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/sms-sram.test.ts b/packages/retroplug/test-native/sms-sram.test.ts
index 307242b5..fabefc05 100644
--- a/packages/retroplug/test-native/sms-sram.test.ts
+++ b/packages/retroplug/test-native/sms-sram.test.ts
@@ -29,6 +29,10 @@ import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
 import { identifySmsggdjVersion } from "../src/smsggdj/romDetect";
 import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -72,10 +76,10 @@ function chord(audio: ReturnType<typeof createAudioDriver>, id: number, hold: nu
   audio.renderAudio(GAP_MS);
 }
 
-function boot(be: ReturnType<typeof createRealBackend>, id: number, romPath: string, platform: Platform, sram: Uint8Array) {
+function boot(id: number, romPath: string, platform: Platform, sram: Uint8Array) {
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       {
         romPath, platform, core: "mesen", embeddedRom: "",
         savPath: null, statePath: null, sramBytes: sram,
@@ -100,7 +104,7 @@ test("the cart's own probe finds 32K of working SRAM, and reads the host's direc
     32 * 1024,
     buildConfigBlock(SMS_SYNC_OFF),
   )!;
-  const audio = boot(be, id, ROM, "sms", sav);
+  const audio = boot(id, ROM, "sms", sav);
 
   // sram_detect ran during that boot. It is a real readback of the emulated cart RAM - it writes a probe
   // into the SRAM window in bank 0 and bank 1 and requires them to differ - so a nonzero sram_slots is
@@ -122,7 +126,7 @@ test("the cart's own probe finds 32K of working SRAM, and reads the host's direc
   const after = be.readSram(id)!;
   expect(after.length).toBe(32 * 1024);
   expect(after).toEqual(sav);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("a save made INSIDE the cart comes back out through readSram, and the TS codec reads it", () => {
@@ -131,7 +135,7 @@ test("a save made INSIDE the cart comes back out through readSram, and the TS co
   const id = 41;
   // A blank battery, so the save has to build the SMDJ4 structure from nothing rather than edit one the
   // host laid out - the case where a wrong write would be invisible to a codec round-trip test.
-  const audio = boot(be, id, ROM, "sms", new Uint8Array(32 * 1024));
+  const audio = boot(id, ROM, "sms", new Uint8Array(32 * 1024));
   chord(audio, id, BTN_2, BTN_DOWN); // -> FILES
   expect(be.readRam(id)![SYM.file_count]).toBe(0);
 
@@ -152,7 +156,7 @@ test("a save made INSIDE the cart comes back out through readSram, and the TS co
   const block = readSongBlock(saved, 0);
   expect(block != null).toBeTruthy();
   expect(block!.length).toBe(SMDJ4_BLOCK_LEN);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("a live song load works on the Game Gear flavor, on the SMS-derived layout", () => {
@@ -166,7 +170,7 @@ test("a live song load works on the Game Gear flavor, on the SMS-derived layout"
   const layout = resolveSmsggdjLayout("0.45")!;
 
   const sav = buildSav([{ block: buildMetronomeBlock(), name: "GGSONG" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
-  const audio = boot(be, id, GG_ROM, "gg", sav);
+  const audio = boot(id, GG_ROM, "gg", sav);
   const booted = be.readRam(id)!;
   expect(booted[SYM.sram_ok]).toBe(1); // the GG cart has its battery too
   expect(booted[SYM.sram_slots]).toBe(SRAM_SLOTS_32K);
@@ -195,5 +199,5 @@ test("a live song load works on the Game Gear flavor, on the SMS-derived layout"
   for (let i = 0; i < buf.length; i++) peak = Math.max(peak, Math.abs(buf[i]));
   console.log(`[gg] loaded "${name}", eng_len=${ram[layout.engLen]}, peak=${peak.toFixed(4)}`);
   expect(peak > 0.01).toBeTruthy(); // and it is audibly playing the loaded song
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/sms-working-song-dirty.test.ts b/packages/retroplug/test-native/sms-working-song-dirty.test.ts
index dbcb33e8..4d097797 100644
--- a/packages/retroplug/test-native/sms-working-song-dirty.test.ts
+++ b/packages/retroplug/test-native/sms-working-song-dirty.test.ts
@@ -21,6 +21,10 @@ import { SMSGGDJ_SYMBOLS } from "../src/smsggdj/runtime/symbols.generated";
 import { commonSongEditedOffset } from "../src/smsggdj/runtime/layout";
 import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
 import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -66,7 +70,7 @@ test("a freshly booted cart is clean, and a keypress on the SONG grid makes it d
 
   const audio = createAudioDriver();
   expect(
-    be.constructSystem(
+    graph.add(
       {
         romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
         savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }),
@@ -101,5 +105,5 @@ test("a freshly booted cart is clean, and a keypress on the SONG grid makes it d
   expect(typed[edited]).toBe(1);
   expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, typed)).toBe(true);
 
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
diff --git a/packages/retroplug/test-native/stage-system-midi.test.ts b/packages/retroplug/test-native/stage-system-midi.test.ts
index 1b07fd1f..95ca2e76 100644
--- a/packages/retroplug/test-native/stage-system-midi.test.ts
+++ b/packages/retroplug/test-native/stage-system-midi.test.ts
@@ -11,6 +11,10 @@
 import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 const ROM = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
@@ -36,8 +40,8 @@ test("stageSystemMidi reaches the system it names, and only that one", () => {
   const audio = createAudioDriver();
   if (!be.fileExists(ROM)) skip(`stageSystemMidi: no ROM at ${ROM}`);
   const spec = { romPath: ROM, platform: "nes" as const, core: "mesen" as const, embeddedRom: "", savPath: null, statePath: null };
-  expect(be.constructSystem(spec, 1)).toBeTruthy();
-  expect(be.constructSystem(spec, 2)).toBeTruthy();
+  expect(graph.add(spec, 1)).toBeTruthy();
+  expect(graph.add(spec, 2)).toBeTruthy();
 
   // Same ROM, same start, no input: the two cores run in lockstep, so any later difference is the message's doing.
   audio.renderAudio(WARMUP_MS);
@@ -62,6 +66,6 @@ test("stageSystemMidi reaches the system it names, and only that one", () => {
   expect(be.stageSystemMidi(1, [])).toBe(false);
   expect(be.stageSystemMidi(0xdead, sxSettings(3))).toBeTruthy(); // queued optimistically; no such system
 
-  be.removeSystem(1);
-  be.removeSystem(2);
+  graph.remove(1);
+  graph.remove(2);
 });
diff --git a/packages/retroplug/test-native/write-ram.test.ts b/packages/retroplug/test-native/write-ram.test.ts
index 1fd57a24..0c4db388 100644
--- a/packages/retroplug/test-native/write-ram.test.ts
+++ b/packages/retroplug/test-native/write-ram.test.ts
@@ -14,6 +14,10 @@
 import { test, expect, skip } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createAudioDriver } from "../src/audioDriver";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __REPO_RESOURCES_DIR__: string;
 
@@ -27,7 +31,7 @@ function boot(): { be: ReturnType<typeof createRealBackend>; audio: ReturnType<t
   const audio = createAudioDriver();
   const id = nextId++;
   expect(
-    be.constructSystem(
+    graph.add(
       {
         romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
         savPath: null, statePath: null, settings: JSON.stringify({ enableFm: false }),
@@ -58,7 +62,7 @@ test("a poke lands in the same region readRam serves, at the same offset", () =>
   // ...and nothing either side moved, so the offset is exact rather than approximately right.
   expect(after[offset - 1]).toBe(before[offset - 1]);
   expect(after[offset + payload.length]).toBe(before[offset + payload.length]);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("a bulk poke the size of a song block lands whole", () => {
@@ -81,7 +85,7 @@ test("a bulk poke the size of a song block lands whole", () => {
   }
   console.log(`[write-ram] 6912-byte poke: firstBad=${firstBad}`);
   expect(firstBad).toBe(-1);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("it works while the core is RUNNING - which writeCpu cannot promise", () => {
@@ -102,7 +106,7 @@ test("it works while the core is RUNNING - which writeCpu cannot promise", () =>
   }
   console.log(`[write-ram] ${landed}/8 pokes observed while running`);
   expect(landed).toBe(8);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("bounds are the one thing it refuses", () => {
@@ -121,7 +125,7 @@ test("bounds are the one thing it refuses", () => {
   // asked for 6,912 bytes has no use for the first 300.
   audio.renderAudio(20);
   expect(be.readRam(id)![WRAM_LEN - 1]).toBe(0x99);
-  expect(be.removeSystem(id)).toBeTruthy();
+  expect(graph.remove(id)).toBeTruthy();
 });
 
 test("a poke to a system that does not exist is refused, and says so", () => {
diff --git a/packages/retroplug/tools-src/lsdj-detect-offsets.ts b/packages/retroplug/tools-src/lsdj-detect-offsets.ts
index 17e3ec79..555767fd 100644
--- a/packages/retroplug/tools-src/lsdj-detect-offsets.ts
+++ b/packages/retroplug/tools-src/lsdj-detect-offsets.ts
@@ -15,6 +15,10 @@ import { createAudioDriver } from "../src/audioDriver";
 import { savFrom } from "../src/lsdjSav";
 import { MemoryRegion } from "../src/backend";
 import { detectDriftLayout, type DetectDriver, type DriftLayoutResult } from "../src/lsdj/runtime/detect";
+import { directGraph } from "../src/graph/directGraph";
+
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
+const graph = directGraph();
 
 declare const __CORPUS_DIR__: string;
 declare const __FILTER__: string; // comma-separated filename substrings; "" = all
@@ -53,8 +57,8 @@ const audio = createAudioDriver();
 const driver: DetectDriver = {
   readFilePrefix: (p, n) => be.readFilePrefix(p, n),
   construct: (id, romPath, sram) =>
-    be.constructSystem({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
-  remove: (id) => void be.removeSystem(id),
+    graph.add({ romPath, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: sram }, id),
+  remove: (id) => void graph.remove(id),
   readWram: (id) => be.readMemory(id, MemoryRegion.Ram),
   press: (id, button, down) => audio.pressButton(id, button, down),
   render: (ms) => void audio.renderAudio(ms),
````

- [ ] **Step 7: Verify nothing changed**

Run: `pnpm typecheck`
Expected: `packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 157 test file(s) passed`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 148 native test file(s) passed`, with `2 case(s) SKIPPED in 2 file(s): dsp-bench(1), nes-bench(1)`.

- [ ] **Step 8: Commit**

```bash
git add -A packages/retroplug
git commit -m "feat(graph): EngineGraph and DirectGraph; buildCore and setGraph on the Backend"
```

---
### Task 5: The flip

The stores publish the engine's graph, `DirectGraph` builds and publishes instead of passing through, TS owns
the host shape, and the native render calls stop setting one (design sections 3 and 4). One commit, accepted by
parity with the fixtures present and `RP_FAIL_ON_SKIP=1`.

What changes, and why it keeps every output bit:

- **Stores.** Each entry records what `buildCore` reported (`built`: node type, export mode, link group), and
  `graphSystems()` is the entries in list order. Every edit that builds or drops a core ends with one publish
  that re-arms the tap; a `sameboy` role edit that moves the link group republishes without one; `clear()`
  publishes the empty graph when there was something to clear; `adopt` publishes (before B2 its
  `constructSystem` placed the core at once), and a project load holds those publishes back and publishes once,
  with the loaded routing (`holdGraph`). A `SystemsStore` used without a `ProjectStore` publishes its systems
  itself at the default stereo routing: 24 `pnpm test` files and many native tests build systems that way.
- **Serial capture** moves into the build spec (`serialOutCapture`), since it only sets a flag on the Game Boy
  core; the live call remains for an `lsdj-sync` mode change on a running system.
- **`ConstructSpec.replaceId`** is no longer sent: the document says what a core replaces. The mock-tier tests
  that asserted it now check the store's view (a bare `SystemsStore`) or `be.replacedBy(call)` (the slot the
  build took in the published document).
- **Shapes.** `createAudioDriver(graph)` sets each render call's shape and puts the one before it back, and sets
  stereo before `startAudio`; `bootSession` passes the project's graph. The plugin calls `__rp_setHostOutputs(8)`
  once the control plane is up, and the SDL standalone calls it with its device width at startup and in
  `reconfigureAudio`. Natively, each render RPC refuses buffers that do not match the running plan's host width.
- **Tests.** Native tests whose renders need a non-stereo shape pass their graph to `createAudioDriver`;
  `app-audio-routing` exercises the store's routing instead of the raw `setAudioRouting` RPC (which Task 6
  deletes, and which would now replan from the translator's empty model); `render-blocks` shares the
  session's graph; `dsp-sms-sync`'s last raw remove (through a backend named `be2`) moves onto `DirectGraph`;
  `dsp-risa-sync` and `dsp-risa-sync-locate` remove their systems at the end of each test (their second test
  built a second core under id 1 while the first was live, which `buildCore` refuses); and the B1 invoker test
  that expected a render call to set its own shape becomes `EngineGraph.test.cpp`'s render-guard case.

**Files:** 59 files, by step below (one of them created: the store-graph test).

- [ ] **Step 1: The stores**

````diff
diff --git a/packages/retroplug/src/projectStore.ts b/packages/retroplug/src/projectStore.ts
index 3f392271..13c8505b 100644
--- a/packages/retroplug/src/projectStore.ts
+++ b/packages/retroplug/src/projectStore.ts
@@ -15,6 +15,7 @@
 
 import type { ControlPlaneBackend, ZipEntry } from "./backend";
 import { SystemsStore } from "./systemsStore";
+import { EngineGraph } from "./engineGraph";
 import type { SystemEntry } from "./systemsList";
 import type { RoleRegistry } from "./systemRoles";
 import type { RecentStore } from "./recentStore";
@@ -45,7 +46,6 @@ import {
   LAYOUT_VALUES,
   MIDI_ROUTING_VALUES,
   AUDIO_ROUTING_VALUES,
-  audioRoutingToIndex,
 } from "./settingsEnums";
 import { scanMissingFiles, autoFindSiblings, relinkInConfig, type MissingFile } from "./projectMissing";
 import { PROJECT_JSON, sramKey, stateKey, partitionEntries } from "./projectBinaries";
@@ -129,8 +129,19 @@ export class ProjectStore {
   private controllerReaderCache: LsdjReader | null = null;
   private onSystemsChange: () => void = () => {};
   private onChangeCb: () => void = () => {};
-
-  constructor(private readonly backend: ControlPlaneBackend, private readonly recent: RecentStore, registry?: RoleRegistry) {
+  private holdGraph = false; // a load's adopts: the graph is published once they are all built
+
+  /** The engine's graph this project publishes (docs/modular-graph-phase-b2-design.md section 2.2). Share it
+   *  with anything else that publishes to the same engine (a DirectGraph, the render calls' shapes). */
+  readonly graph: EngineGraph;
+
+  constructor(
+    private readonly backend: ControlPlaneBackend,
+    private readonly recent: RecentStore,
+    registry?: RoleRegistry,
+    graph?: EngineGraph,
+  ) {
+    this.graph = graph ?? new EngineGraph(backend);
     // Any user mutation of the systems list marks the project dirty and re-drives the DSP.
     this.systems = new SystemsStore(
       backend,
@@ -140,6 +151,15 @@ export class ProjectStore {
       },
       registry,
     );
+    this.systems.setOnGraphChange((rearmTap) => this.publishGraph(rearmTap));
+  }
+
+  // Publish the engine's graph for the systems and the audio routing as they stand: the systems store calls
+  // this at the end of every structural edit. `rearmTap` re-arms the NES tap, as every edit that builds or
+  // drops a core does. A load holds it back while it adopts (holdGraph), then publishes once.
+  private publishGraph(rearmTap: boolean): boolean {
+    if (this.holdGraph) return true;
+    return this.graph.publish(this.systems.graphSystems(), this.projectSettings.audioRouting, { rearmTap });
   }
 
   /** Install the hook fired whenever the systems structure changes (a user edit, or a load / new; the quiet
@@ -654,18 +674,24 @@ export class ProjectStore {
   // system's SRAM/savestate blobs (export only) seed its emulator via adopt.
   private commit(cfg: ProjectConfig, path: string, blobs: Map<string, Uint8Array>): LoadOutcome {
     this.systems.clear();
-    cfg.systems.forEach((s, i) => {
-      const sram = blobs.get(sramKey(i));
-      const state = blobs.get(stateKey(i));
-      // A savestate is the complete machine snapshot (it already contains the battery-backed SRAM), so
-      // boot from it ALONE — also seeding sramBytes cold-reseeds the battery and drops the restored
-      // runtime state (e.g. an LSDj armed in "wait for MIDI"). The sram blob only seeds a system that
-      // has no savestate (a thin load, or a battery-only export).
-      const sysBlobs = state ? { stateBytes: state } : sram ? { sramBytes: sram } : undefined;
-      this.systems.adopt(s, sysBlobs);
-    });
+    // Built one by one, placed together: the graph is published once, with the loaded routing.
+    this.holdGraph = true;
+    try {
+      cfg.systems.forEach((s, i) => {
+        const sram = blobs.get(sramKey(i));
+        const state = blobs.get(stateKey(i));
+        // A savestate is the complete machine snapshot (it already contains the battery-backed SRAM), so
+        // boot from it ALONE — also seeding sramBytes cold-reseeds the battery and drops the restored
+        // runtime state (e.g. an LSDj armed in "wait for MIDI"). The sram blob only seeds a system that
+        // has no savestate (a thin load, or a battery-only export).
+        const sysBlobs = state ? { stateBytes: state } : sram ? { sramBytes: sram } : undefined;
+        this.systems.adopt(s, sysBlobs);
+      });
+    } finally {
+      this.holdGraph = false;
+    }
     this.projectSettings = { ...DEFAULT_SETTINGS, ...cfg.settings };
-    this.pushAudioRouting(); // apply the loaded project's routing to native audio
+    this.pushAudioRouting(); // place the loaded systems, with the loaded project's routing
     this.projectName = cfg.name ?? ""; // only a name the user gave the project is stored; blank = unnamed
     this.songRequest = null; // a request parked against the previous cart cannot apply to this one
     this.recentPending = false;
@@ -684,11 +710,11 @@ export class ProjectStore {
     return true;
   }
 
-  /** Push the current audio-routing mode to native — the one project setting that drives native audio
-   *  (the translator rewires the graph's outputs). Converts the string value to native's AudioRouting integer.
-   *  Called on set / load / reset so native tracks the TS value. */
+  /** Publish the graph for the current audio-routing mode — the one project setting that drives native audio
+   *  (it decides the graph's output wiring). Called on set / load / reset, every one re-arming the NES tap,
+   *  as a routing change always has, even to the mode already set. */
   private pushAudioRouting(): void {
-    this.backend.setAudioRouting(audioRoutingToIndex(this.projectSettings.audioRouting));
+    this.publishGraph(true);
   }
 
   private markDirty(): void {
diff --git a/packages/retroplug/src/systemsList.ts b/packages/retroplug/src/systemsList.ts
index e9bce7bd..5fda7eff 100644
--- a/packages/retroplug/src/systemsList.ts
+++ b/packages/retroplug/src/systemsList.ts
@@ -12,6 +12,7 @@ import { siblingSavPath, siblingRomCandidates } from "./savPaths";
 import type { Platform, Core } from "./platform";
 import type { CommonSettings } from "./systemSettings";
 import type { RoleInstance } from "./systemRoles";
+import type { BuiltCore } from "./backend";
 
 /** The per-system record TS owns. `savSuffix` + `savPath` (an override, `""` when the
  *  natural sibling is used) are the persistent identity. `platform` (what the ROM is) and
@@ -29,6 +30,10 @@ export interface SystemEntry {
   battery: boolean; // derived (not serialized): the cart has battery-backed save memory
   settings: CommonSettings;
   roles: RoleInstance[];
+  /** What buildCore reported for the core now running this entry (not serialized): the node type it is
+   *  placed as, the export mode it was built with, and its link group, kept in step with each live
+   *  link-group edit. The graph is built from these. */
+  built: BuiltCore;
 }
 
 /** The live system with `id`, or undefined. */
diff --git a/packages/retroplug/src/systemsStore.ts b/packages/retroplug/src/systemsStore.ts
index 73a6db92..35e999f8 100644
--- a/packages/retroplug/src/systemsStore.ts
+++ b/packages/retroplug/src/systemsStore.ts
@@ -11,6 +11,8 @@
 // (PluginDSP.cpp:406-458), with every path derived by the pure kernels.
 
 import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
+import type { GraphSystem } from "./graph/buildGraph";
+import { EngineGraph } from "./engineGraph";
 import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, SEGA_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";
 
 import { resolveSavPath, siblingSavPath, siblingRplgPath, nextFreeSavSuffix, siblingSavCandidates } from "./savPaths";
@@ -116,6 +118,13 @@ export class SystemsStore {
   private focusedId = 0;
   private dirty = false;
   private onFocusChange: () => void = () => {};
+  // A store on its own publishes its systems at the default (stereo) routing through a graph of its own;
+  // the ProjectStore that owns one replaces this with its own publish, which adds the project's routing.
+  private ownGraph: EngineGraph | null = null;
+  private onGraphChange: (rearmTap: boolean) => void = (rearmTap) => {
+    this.ownGraph ??= new EngineGraph(this.backend);
+    this.ownGraph.publish(this.graphSystems(), "stereo", { rearmTap });
+  };
   // Whether a core teardown writes the battery to disk first. Off only for an offline render.
   private batteryFlush = true;
 
@@ -146,6 +155,10 @@ export class SystemsStore {
   systems(): SystemEntry[] {
     return this.entries.slice();
   }
+  /** The systems as the engine's graph places them, in slot order (graph/buildGraph.ts). */
+  graphSystems(): GraphSystem[] {
+    return this.entries.map((e) => ({ id: e.id, ...e.built }));
+  }
   focused(): number {
     return this.focusedId;
   }
@@ -165,6 +178,14 @@ export class SystemsStore {
     this.onFocusChange = fn;
   }
 
+  /** Install the hook that publishes the engine's graph (the ProjectStore's, which adds the routing; without
+   *  one the store publishes its systems at the default stereo routing itself). Called
+   *  once at the end of every edit that changes what the graph places, BEFORE the structural onChange that
+   *  pushes the kernel: `rearmTap` for an edit that builds or drops a core, which re-arms the NES tap. */
+  setOnGraphChange(fn: (rearmTap: boolean) => void): void {
+    this.onGraphChange = fn;
+  }
+
   /** Turn the before-teardown battery flush off for this store. For a throwaway headless session — an
    *  offline render builds systems, loads savestates into them and drops them, and an export must not
    *  write into the user's save folder as a side effect. The plugin and the standalone never call this. */
@@ -226,7 +247,7 @@ export class SystemsStore {
     const prev = findById(this.entries, id);
     if (!prev) return null;
     this.flushBeforeDrop(prev);
-    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), opts?.explicitSav, id);
+    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), opts?.explicitSav);
     if (!built) return null;
     this.entries = replaceById(this.entries, id, built.entry);
     if (this.focusedId === id) this.focusedId = built.id;
@@ -247,7 +268,7 @@ export class SystemsStore {
     if (!prev) return null;
     const sramBytes = this.backend.readSram(id) ?? undefined;
     this.flushBeforeDrop(prev); // the OLD cart's sav — the carried battery lands on the new ROM's
-    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, id, sramBytes);
+    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, sramBytes);
     if (!built) return null;
     this.entries = replaceById(this.entries, id, built.entry);
     if (this.focusedId === id) this.focusedId = built.id;
@@ -282,9 +303,9 @@ export class SystemsStore {
       stateBytes: state,
       settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
     }, src.roles);
-    const ok = this.backend.constructSystem(spec, newId);
-    if (!ok) {
-      console.warn(`[systems] duplicateSystem(${id}) failed (construct returned false) — no instance added`);
+    const built = this.backend.buildCore(spec, newId);
+    if (!built) {
+      console.warn(`[systems] duplicateSystem(${id}) failed (the core did not build) — no instance added`);
       return null;
     }
     this.entries = appendEntry(this.entries, {
@@ -298,6 +319,7 @@ export class SystemsStore {
       battery: src.battery, // same ROM as the source
       settings: { ...src.settings },
       roles: src.roles.map((r) => ({ kind: r.kind, config: { ...r.config } })),
+      built,
     });
     return this.committed(newId);
   }
@@ -307,16 +329,16 @@ export class SystemsStore {
     const e = findById(this.entries, id);
     if (!e) return false;
     this.flushBeforeDrop(e);
-    this.backend.removeSystem(id);
     this.entries = removeById(this.entries, id);
     this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
+    this.onGraphChange(true);
     this.markDirty();
     return true;
   }
 
   /** Rebuild a system's ROM from disk, carrying its battery SRAM forward, swapping in place with a new
    *  id and preserving identity + focus. Orchestrated in TS: pull SRAM from the registry, then cold-boot
-   *  the ROM with it (no savestate) via a replaceId construct. No native reload method. */
+   *  the ROM with it (no savestate) into the same slot. No native reload method. */
   reloadSystem(id: number): number | null {
     const prev = findById(this.entries, id);
     if (!prev) return null;
@@ -325,7 +347,7 @@ export class SystemsStore {
   }
 
   /** Reconstruct system `id` in place from seed bytes: capture the source's spec, build a fresh core
-   *  under a new id that swaps the old one (replaceId), and keep identity + focus. The seed (sramBytes
+   *  under a new id that takes the old one's slot, and keep identity + focus. The seed (sramBytes
    *  = cold-boot battery, stateBytes = boot from savestate) overrides what native would read from disk.
    *  `savPathOverride` repoints the auto-save target (New SRAM…); omitted, the system keeps its own. The
    *  shared body of reload / loadState / loadSram / newSramAs — the in-place twin of duplicateSystem
@@ -351,12 +373,11 @@ export class SystemsStore {
       statePath: null,
       sramBytes: seed.sramBytes,
       stateBytes: seed.stateBytes,
-      replaceId: id,
       settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
     }, src.roles);
-    const ok = this.backend.constructSystem(spec, newId);
-    if (!ok) return null;
-    this.entries = replaceById(this.entries, id, { ...src, id: newId, savPath: override });
+    const built = this.backend.buildCore(spec, newId);
+    if (!built) return null;
+    this.entries = replaceById(this.entries, id, { ...src, id: newId, savPath: override, built });
     if (this.focusedId === id) this.focusedId = newId;
     return this.committed(newId);
   }
@@ -507,7 +528,14 @@ export class SystemsStore {
     if (rt?.category === "system") this.backend.applyRoleConfig(id, roleKind, roleConfigForNative(roleKind, config));
     const roles = e.roles.slice();
     roles[idx] = { kind: roleKind, config };
-    this.entries = replaceById(this.entries, id, { ...e, roles });
+    // A Game Boy's link group is the one system-role field that is also wiring: the cable follows it.
+    const linkGroupId = (config as { linkGroupId?: unknown }).linkGroupId;
+    const built =
+      roleKind === "sameboy" && e.built.type === "rp.sameboy.gb"
+        ? { ...e.built, linkGroup: typeof linkGroupId === "number" ? linkGroupId : 0 }
+        : e.built;
+    this.entries = replaceById(this.entries, id, { ...e, roles, built });
+    if (built !== e.built) this.onGraphChange(false);
     // A feature role stays pure TS, EXCEPT lsdj-sync's serial-out capture gate — arm/disarm it on a
     // MIDIOUT mode change (the decoder itself runs in the kernel via the re-projection below).
     if (roleKind === "lsdj-sync") this.syncSerialOutCapture(id);
@@ -546,14 +574,15 @@ export class SystemsStore {
   // clear()/adopt() are QUIET: no dirty/onChange, since a load isn't a user edit
   // (the ProjectStore sets dirty explicitly around load/new).
 
-  /** Tear down every system + reset the list/focus (for `new` + before a load). */
+  /** Tear down every system + reset the list/focus (for `new` + before a load). Publishes the empty graph
+   *  (a load builds its systems only after their predecessors are gone, so it never holds both), but no
+   *  onChange: the caller notifies once it is done. */
   clear(): void {
-    for (const e of this.entries) {
-      this.flushBeforeDrop(e);
-      this.backend.removeSystem(e.id);
-    }
+    const had = this.entries.length > 0;
+    for (const e of this.entries) this.flushBeforeDrop(e);
     this.entries = [];
     this.focusedId = 0;
+    if (had) this.onGraphChange(true);
   }
 
   /** Reconstruct one system from a serialized config entry, preserving its EXACT
@@ -563,11 +592,12 @@ export class SystemsStore {
    *  of from disk; `savPath` stays the auto-save target.
    *
    *  QUIET ON PURPOSE: unlike every other mutation this does NOT markDirty(), so the
-   *  store→DSP projection does not run. A project load adopts N systems and then fires
-   *  ONE onSystemsChange (see projectStore.load), rather than re-projecting N times.
-   *  A standalone caller must therefore trigger a sync itself - any markDirty-ing call
-   *  (e.g. setRoleConfig) will do - or the system boots and renders VIDEO while the DSP
-   *  knows nothing about it and it produces NO AUDIO. */
+   *  store→DSP projection does not run. It does publish the graph, so the core runs at once
+   *  (a project load holds that back and publishes once its systems are all built; see
+   *  projectStore.load). A project load adopts N systems and then fires ONE onSystemsChange,
+   *  rather than re-projecting N times. A standalone caller must therefore trigger a sync
+   *  itself - any markDirty-ing call (e.g. setRoleConfig) will do - or the system boots and
+   *  renders VIDEO while the DSP knows nothing about it and it produces NO AUDIO. */
   adopt(
     config: {
       core?: Core; // persisted since the v2 project schema (else auto-derived from platform)
@@ -617,14 +647,15 @@ export class SystemsStore {
       stateBytes: blobs?.stateBytes,
       settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
     }, roles);
-    if (!this.backend.constructSystem(spec, id)) return null;
+    const built = this.backend.buildCore(spec, id); // a loaded system already in MIDIOUT mode is built capturing
+    if (!built) return null;
     // Stored settings/roles win; a config that omits them re-attaches defaults.
     const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
     const wasEmpty = this.entries.length === 0;
     const battery = this.detectBattery(romPath, embeddedRom, platform);
-    this.entries = appendEntry(this.entries, { id, platform, core, romPath, savPath: override, savSuffix, embeddedRom, battery, settings, roles });
+    this.entries = appendEntry(this.entries, { id, platform, core, romPath, savPath: override, savSuffix, embeddedRom, battery, settings, roles, built });
     if (wasEmpty) this.focusedId = id;
-    this.syncSerialOutCapture(id); // a loaded project may carry a system already in MIDIOUT mode
+    this.onGraphChange(true);
     return id;
   }
 
@@ -634,7 +665,7 @@ export class SystemsStore {
   private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
     const empty = this.entries.length === 0;
     const target = empty ? undefined : this.effectiveFocus();
-    const built = this.construct(romPath, embeddedRom, this.replacementSuffix(target, romPath), explicitSav, target);
+    const built = this.construct(romPath, embeddedRom, this.replacementSuffix(target, romPath), explicitSav);
     if (!built) return null;
     this.entries = empty
       ? appendEntry(this.entries, built.entry)
@@ -650,7 +681,6 @@ export class SystemsStore {
     embeddedRom: string,
     suffix: number,
     explicitSav: string | undefined,
-    replaceId?: number,
     sramBytes?: Uint8Array, // seed the cold-boot battery (swap-ROM-preserve-SRAM); undefined = native reads from disk
   ): { id: number; entry: SystemEntry } | null {
     let platform: Platform;
@@ -674,11 +704,9 @@ export class SystemsStore {
     // Roles are known before the build (a pure function of core/platform/header), so a role's
     // load-time hook can seed the spec — e.g. an empty LSDj sav — before the core is instantiated.
     const roles = this.defaultRoles(core, platform, romPath, embeddedRom);
-    const spec = this.applyConstructHooks(
-      { romPath, platform, core, embeddedRom, savPath, statePath: null, replaceId, sramBytes },
-      roles,
-    );
-    if (!this.backend.constructSystem(spec, id)) return null;
+    const spec = this.applyConstructHooks({ romPath, platform, core, embeddedRom, savPath, statePath: null, sramBytes }, roles);
+    const built = this.backend.buildCore(spec, id);
+    if (!built) return null;
     return {
       id,
       entry: {
@@ -692,6 +720,7 @@ export class SystemsStore {
         battery: this.detectBattery(romPath, embeddedRom, platform),
         settings: { ...DEFAULT_COMMON_SETTINGS },
         roles,
+        built,
       },
     };
   }
@@ -815,13 +844,14 @@ export class SystemsStore {
   // Additive by contract — a hook returns the spec unchanged when the data it would seed is present.
   // The backend satisfies ConstructCaps structurally (savFromJson + fileExists), so it's the caps arg.
   private applyConstructHooks(spec: ConstructSpec, roles: RoleInstance[]): ConstructSpec {
-    if (!this.registry) return spec;
     let s = spec;
-    for (const r of roles) {
-      const rt = this.registry.roleType(r.kind);
-      if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
-    }
-    return s;
+    if (this.registry)
+      for (const r of roles) {
+        const rt = this.registry.roleType(r.kind);
+        if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
+      }
+    // A core is built with its serial-out capture already armed, so no command has to chase it.
+    return serialOutCaptureFor(roles) ? { ...s, serialOutCapture: true } : s;
   }
 
   // Write a system's live battery to its own `.sav` before its core is destroyed. Every teardown path
@@ -869,9 +899,10 @@ export class SystemsStore {
     this.onChange();
   }
 
-  // Finish a successful mutation: mark dirty + notify, return the id.
+  // Finish a successful mutation that built a core: publish the graph that places it, mark dirty + notify,
+  // return the id.
   private committed(id: number): number {
-    this.syncSerialOutCapture(id); // a freshly (re)built core defaults to unarmed — re-establish the gate
+    this.onGraphChange(true);
     this.markDirty();
     return id;
   }
@@ -881,9 +912,15 @@ export class SystemsStore {
   // a core is (re)built (committed/adopt — a fresh core defaults to unarmed) and when the mode changes
   // (setRoleConfig). A no-op for a non-LSDj system (no lsdj-sync role → nothing to arm).
   private syncSerialOutCapture(id: number): void {
-    const lsdj = findById(this.entries, id)?.roles.find((r) => r.kind === "lsdj-sync");
-    if (!lsdj) return;
-    const mode = (lsdj.config as { mode?: LsdjSyncMode }).mode;
-    this.backend.setSerialOutCapture(id, mode === LsdjSyncMode.MidiOut || mode === LsdjSyncMode.MasterSync);
+    const roles = findById(this.entries, id)?.roles;
+    if (!roles?.some((r) => r.kind === "lsdj-sync")) return;
+    this.backend.setSerialOutCapture(id, serialOutCaptureFor(roles));
   }
 }
+
+// Whether a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it,
+// MidiOut (SYNC=MI.OUT) and MasterSync (SYNC=LSDJ).
+function serialOutCaptureFor(roles: RoleInstance[]): boolean {
+  const mode = (roles.find((r) => r.kind === "lsdj-sync")?.config as { mode?: LsdjSyncMode } | undefined)?.mode;
+  return mode === LsdjSyncMode.MidiOut || mode === LsdjSyncMode.MasterSync;
+}
````

- [ ] **Step 2: `DirectGraph` builds and publishes**

Replace `packages/retroplug/src/graph/directGraph.ts` with:

````ts
// Structure for the callers that build systems without stores (docs/modular-graph-phase-b2-design.md section
// 2.4): the native tests, the CLI's analysis sessions and the offset tools. It keeps the systems list the B1
// translator kept (add appends, replace keeps the slot, remove shifts later slots down) and publishes after each
// call. Use one per process, directGraph(), or one sharing a session's EngineGraph: two publishers with separate
// models would each drop the other's systems from the document.
import { createRealBackend } from "../realBackend";
import type { AudioRouting } from "../settingsEnums";
import type { ConstructSpec, EmulatorBackend } from "../backend";
import { EngineGraph } from "../engineGraph";
import type { GraphSystem } from "./buildGraph";

export type DirectBackend = Pick<EmulatorBackend, "buildCore" | "setGraph" | "applyRoleConfig">;

export class DirectGraph {
  readonly graph: EngineGraph;
  private systems: GraphSystem[] = [];
  private routing: AudioRouting = "stereo";

  constructor(
    private readonly backend: DirectBackend,
    graph?: EngineGraph,
  ) {
    this.graph = graph ?? new EngineGraph(backend);
  }

  /** The systems this graph places, in slot order. */
  ids(): number[] {
    return this.systems.map((s) => s.id);
  }

  /** Build a core under `id` and append it. False when it did not build, or the graph did not plan (the core
   *  is then freed by the next document that plans). */
  add(spec: ConstructSpec, id: number): boolean {
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => this.systems.push({ id, ...built }), true);
  }

  /** Build a core under `id` in `oldId`'s slot. False when `oldId` is absent (the tap is re-armed all the same,
   *  as a replace of a gone system always did), or when the core did not build. */
  replace(oldId: number, spec: ConstructSpec, id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === oldId);
    if (at < 0) {
      this.publish(true);
      return false;
    }
    const built = this.backend.buildCore(spec, id);
    if (!built) return false;
    return this.change(() => (this.systems[at] = { id, ...built }), true);
  }

  /** Drop system `id`; later slots shift down. An id already gone re-arms the tap and changes nothing else. */
  remove(id: number): boolean {
    const at = this.systems.findIndex((s) => s.id === id);
    if (at < 0) return this.publish(true);
    return this.change(() => this.systems.splice(at, 1), true);
  }

  setRouting(routing: AudioRouting): boolean {
    return this.change(() => (this.routing = routing), true);
  }

  /** Apply a system role's config to the live core. A `sameboy` config's link group is wiring as well: native
   *  decodes a config without one as group 0, so the cable follows what the core is told. */
  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean {
    const ok = this.backend.applyRoleConfig(id, kind, config);
    const at = this.systems.findIndex((s) => s.id === id);
    if (kind !== "sameboy" || at < 0 || this.systems[at].type !== "rp.sameboy.gb") return ok;
    const group = typeof config.linkGroupId === "number" ? config.linkGroupId : 0;
    if (group !== this.systems[at].linkGroup) this.change(() => (this.systems[at] = { ...this.systems[at], linkGroup: group }), false);
    return ok;
  }

  // Apply an edit to the model and publish; a refused document puts the model back.
  private change(edit: () => unknown, rearmTap: boolean): boolean {
    const systems = this.systems.slice();
    const routing = this.routing;
    edit();
    if (this.publish(rearmTap)) return true;
    this.systems = systems;
    this.routing = routing;
    return false;
  }

  private publish(rearmTap: boolean): boolean {
    return this.graph.publish(this.systems, this.routing, { rearmTap });
  }
}

let shared: DirectGraph | null = null;

/** The process's DirectGraph over the real backend. */
export function directGraph(): DirectGraph {
  shared ??= new DirectGraph(createRealBackend());
  return shared;
}
````

- [ ] **Step 3: The shapes in TS**

````diff
diff --git a/packages/retroplug/src/audioDriver.ts b/packages/retroplug/src/audioDriver.ts
index 3ea0e820..ebecc537 100644
--- a/packages/retroplug/src/audioDriver.ts
+++ b/packages/retroplug/src/audioDriver.ts
@@ -6,6 +6,8 @@
 
 
 import { makeCall } from "./rpcClient";
+import type { EngineGraph } from "./engineGraph";
+import type { HostShape } from "./graph/buildGraph";
 
 /** DSP-runtime allocation counters (spec/08-profiling.md). Fields are DELTAS since the last
  *  dspResetAllocStats(); `enabled` is false unless the host was built with RETROPLUG_PROFILE.
@@ -200,30 +202,58 @@ export interface AudioDriver {
   drainReleased(): number;
 }
 
-/** Build an audio driver backed by the native host. Throws if no RPC surface is bound. */
-export function createAudioDriver(): AudioDriver {
+/** Build an audio driver backed by the native host. Throws if no RPC surface is bound.
+ *
+ *  `graph` is the engine's graph whoever builds the systems publishes (docs/modular-graph-phase-b2-design.md
+ *  section 4): each render call renders into the buffers of one host shape, so it sets that shape for the
+ *  call and puts the one before it back, and startAudio sets the stereo pair its loop renders. Without one
+ *  the calls render whatever shape is planned, and native refuses buffers that do not match it. */
+export function createAudioDriver(graph?: EngineGraph): AudioDriver {
   const call = makeCall();
 
   const ints = (b: Uint8Array | number[]): number[] => Array.from(b);
+  const STEREO: HostShape = { kind: "multi", channels: 2 };
+  // While this driver's audio thread runs, the render calls refuse natively, so they leave the graph alone.
+  let running = false;
+  const shaped = <T>(shape: HostShape, render: () => T, refused: () => T): T => {
+    if (!graph || running) return render();
+    const out = graph.withShape(shape, render);
+    return out === null ? refused() : out;
+  };
 
   return {
     pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
     screenshot: (id, path) => call("screenshot", id, path) as boolean,
-    renderAudio: (ms) => {
-      const bytes = call("renderAudio", ms) as Uint8Array;
-      // Raw interleaved f32; slice() copies to a fresh 4-byte-aligned ArrayBuffer at offset 0.
-      return new Float32Array(bytes.slice().buffer);
-    },
-    renderAudioPerSystem: (ms) => {
-      const bufs = call("renderAudioPerSystem", ms) as Uint8Array[];
-      // One interleaved-f32 buffer per system; slice() 4-byte-aligns each like the mixed path above.
-      return bufs.map((b) => new Float32Array(b.slice().buffer));
-    },
-    renderAudioPerChannel: (id, ms) => {
-      const bufs = (call("renderAudioPerChannel", id, ms) as Uint8Array[] | null) ?? [];
-      // One interleaved-f32 buffer per channel stream; slice() 4-byte-aligns each.
-      return bufs.map((b) => new Float32Array(b.slice().buffer));
-    },
+    renderAudio: (ms) =>
+      shaped(
+        STEREO,
+        () => {
+          const bytes = call("renderAudio", ms) as Uint8Array;
+          // Raw interleaved f32; slice() copies to a fresh 4-byte-aligned ArrayBuffer at offset 0.
+          return new Float32Array(bytes.slice().buffer);
+        },
+        () => new Float32Array(0),
+      ),
+    renderAudioPerSystem: (ms) =>
+      shaped(
+        { kind: "perSystem" },
+        () => {
+          const bufs = call("renderAudioPerSystem", ms) as Uint8Array[];
+          // One interleaved-f32 buffer per system; slice() 4-byte-aligns each like the mixed path above.
+          return bufs.map((b) => new Float32Array(b.slice().buffer));
+        },
+        () => [],
+      ),
+    renderAudioPerChannel: (id, ms) =>
+      shaped(
+        { kind: "perChannel" },
+        () => {
+          const bufs = (call("renderAudioPerChannel", id, ms) as Uint8Array[] | null) ?? [];
+          // One interleaved-f32 buffer per channel stream; slice() 4-byte-aligns each.
+          return bufs.map((b) => new Float32Array(b.slice().buffer));
+        },
+        () => [],
+      ),
     compileKit: (spec) => call("compileKit", spec) as Uint8Array, // the 16 KB kit bank, straight through
     compileDmc: (spec) => call("compileDmc", spec) as Uint8Array, // the 8 KB risa DMC kit bank
     sampleRate: () => call("sampleRate") as number,
@@ -235,28 +265,10 @@ export function createAudioDriver(): AudioDriver {
     stageControllerIn: (bytes) => call("stageControllerIn", ints(bytes)) as boolean,
     drainMidiOut: () =>
       (call("drainMidiOut") as { system: number; frame: number; data: Uint8Array }[] | null) ?? [],
-    renderBlocks: (outputs, blocks) => {
-      // Every field is sent, absent ones as null/[]: the native struct has no field defaults.
-      const req = {
-        outputs,
-        blocks: blocks.map((b) => ({
-          frames: b.frames,
-          midi: (b.midi ?? []).map((m) => ({ frame: m.frame, bytes: Array.from(m.bytes) })),
-          controller: (b.controller ?? []).map((c) => Array.from(c)),
-          playing: b.playing ?? null,
-          bpm: b.bpm ?? null,
-          ppq: b.ppq ?? null,
-        })),
-      };
-      const r = call("renderBlocks", req) as {
-        error: string;
-        lanes: Uint8Array[];
-        midiOut: { block: number; system: number; frame: number; data: Uint8Array }[];
-      };
-      if (r.error) throw new Error(`renderBlocks: ${r.error}`);
-      // slice() copies each lane to a fresh 4-byte-aligned buffer, as renderAudio does.
-      return { lanes: r.lanes.map((l) => new Float32Array(l.slice().buffer)), midiOut: r.midiOut };
-    },
+    renderBlocks: (outputs, blocks) =>
+      shaped({ kind: "multi", channels: outputs }, () => renderBlocks(outputs, blocks), () => {
+        throw new Error(`renderBlocks: the ${outputs}-output host shape did not plan`);
+      }),
     buildTarget: () => call("buildTarget") as string,
     dspAllocStats: () => call("dspAllocStats") as DspAllocStats,
     dspResetAllocStats: (disableAutoGc) => call("dspResetAllocStats", disableAutoGc) as boolean,
@@ -264,11 +276,43 @@ export function createAudioDriver(): AudioDriver {
     dspTraceReset: (arm) => call("dspTraceReset", arm) as boolean,
     dspTrace: () => call("dspTrace") as DspTraceSpan[],
     dspTraceNames: () => call("dspTraceNames") as string[],
-    startAudio: () => call("startAudio") as boolean,
-    stopAudio: () => call("stopAudio") as boolean,
+    startAudio: () => {
+      if (graph && !running && !graph.setShape(STEREO)) return false;
+      const ok = call("startAudio") as boolean;
+      if (ok) running = true;
+      return ok;
+    },
+    stopAudio: () => {
+      const ok = call("stopAudio") as boolean;
+      if (ok) running = false;
+      return ok;
+    },
     sleepMs: (ms) => call("sleepMs", ms) as boolean,
     audioCaptured: () => call("audioCaptured") as { energy: number; frames: number },
     systemCount: () => call("systemCount") as number,
     drainReleased: () => call("drainReleased") as number,
   };
+
+  function renderBlocks(outputs: number, blocks: RenderBlock[]): RenderBlocksResult {
+    // Every field is sent, absent ones as null/[]: the native struct has no field defaults.
+    const req = {
+      outputs,
+      blocks: blocks.map((b) => ({
+        frames: b.frames,
+        midi: (b.midi ?? []).map((m) => ({ frame: m.frame, bytes: Array.from(m.bytes) })),
+        controller: (b.controller ?? []).map((c) => Array.from(c)),
+        playing: b.playing ?? null,
+        bpm: b.bpm ?? null,
+        ppq: b.ppq ?? null,
+      })),
+    };
+    const r = call("renderBlocks", req) as {
+      error: string;
+      lanes: Uint8Array[];
+      midiOut: { block: number; system: number; frame: number; data: Uint8Array }[];
+    };
+    if (r.error) throw new Error(`renderBlocks: ${r.error}`);
+    // slice() copies each lane to a fresh 4-byte-aligned buffer, as renderAudio does.
+    return { lanes: r.lanes.map((l) => new Float32Array(l.slice().buffer)), midiOut: r.midiOut };
+  }
 }
diff --git a/packages/retroplug/src/bootSession.ts b/packages/retroplug/src/bootSession.ts
index ec49affd..08148e1b 100644
--- a/packages/retroplug/src/bootSession.ts
+++ b/packages/retroplug/src/bootSession.ts
@@ -39,7 +39,8 @@ export function bootSession(): Session {
   const recent = new RecentStore(backend);
   const project = new ProjectStore(backend, recent, registry);
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  // The render calls set their host shape on the project's graph (docs/modular-graph-phase-b2-design.md 4).
+  const audio = createAudioDriver(project.graph);
 
   dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
diff --git a/packages/retroplug/src/pluginControlPlane.ts b/packages/retroplug/src/pluginControlPlane.ts
index f9791e1f..45a87417 100644
--- a/packages/retroplug/src/pluginControlPlane.ts
+++ b/packages/retroplug/src/pluginControlPlane.ts
@@ -7,6 +7,7 @@
 //   __rp_loadProjectB64(b64)    — DPF setState: load an in-memory chunk (base64 of the .rplg zip)
 //   __rp_saveProjectB64()       — DPF getState: export the project as a base64 .rplg chunk
 //   __rp_newProject()           — DPF setState(""): reset to an empty project
+//   __rp_setHostOutputs(n)      — the host's output channel count: the graph's multi(n) shape
 //   __rp_ready                  — set true once composition + kernel load succeeded
 //
 // The C++ boundary stays string-only: base64 is done HERE (DPF state is NUL-terminated UTF-8; the
@@ -87,6 +88,11 @@ g.__rp_saveProjectB64 = (): string => {
 
 g.__rp_newProject = (): void => project.newProject();
 
+// The host's output channels (the plugin's fixed 8, the standalone's device width), which the graph's output
+// wiring is built for: the host calls it once the control plane is up and again whenever the count changes.
+// "true" when the shape planned.
+g.__rp_setHostOutputs = (n: string | number): string => String(project.graph.setShape({ kind: "multi", channels: Number(n) }));
+
 // Drive the file watcher from the UI idle loop (PluginUI::uiIdle, throttled). Drains native's changed
 // paths and reacts; a no-op when nothing changed. Returns void — the host ignores the result.
 g.__rp_pumpWatcher = (): void => {
````

- [ ] **Step 4: Natively, the render guard and the hosts' outputs**

````diff
diff --git a/packages/native/plugin/PluginDSP.cpp b/packages/native/plugin/PluginDSP.cpp
index 54354aa2..a86899a2 100644
--- a/packages/native/plugin/PluginDSP.cpp
+++ b/packages/native/plugin/PluginDSP.cpp
@@ -265,21 +265,17 @@ protected:
     // --- audio lifecycle: DPF owns the audio thread and calls run(), so the plugin drives the Engine
     // directly here (mirrors AudioDriverRpcService's loop, minus the spawned thread). ---
     //
-    // activate and sampleRateChanged plan graphs, so they assume DPF calls them on the control thread, as it
-    // does for CLAP, VST3 and AU. JACK re-runs deactivate/activate from its buffer-size callback, and VST2
-    // resumes from processReplacing a plugin its host has not activated. Both are no-ops in practice: an
-    // earlier activate planned the shape (setHostShape returns at once), a block within the plan's maximum
-    // leaves setMaxBlockFrames nothing to do, and DPF skips an unchanged rate. Only a VST2 host that
-    // processes before it ever activates would plan here first. The hazard predates the graph engine.
+    // activate and sampleRateChanged can re-activate the graph, so they assume DPF calls them on the control
+    // thread, as it does for CLAP, VST3 and AU. JACK re-runs deactivate/activate from its buffer-size
+    // callback, and VST2 resumes from processReplacing a plugin its host has not activated. Both are no-ops in
+    // practice: a block within the plan's maximum leaves setMaxBlockFrames nothing to do, and DPF skips an
+    // unchanged rate. Only a VST2 host that processes before it ever activates, with a block larger than the
+    // default maximum, would re-activate here first. The hazard predates the graph engine. (The output shape
+    // is no longer set here: the control plane builds the graph for the plugin's outputs at boot.)
     void activate() override {
         // Hand the Engine to the DPF audio thread: from here every control-plane edit is push-only,
         // drained by run() each block. (Set BEFORE the host starts calling run(); the quiescent path
         // flushed every push, so the command ring is empty at this handoff.) Then report PDC latency.
-        // A shape that does not plan keeps the one before it (outs 1/2 only, before the first activate);
-        // the engine has logged why.
-        if (!svc_.invoker.setHostShape({rp::host::HostShape::Kind::Multi, DISTRHO_PLUGIN_NUM_OUTPUTS}))
-            d_stderr("[retroplug] the %u-output host shape did not plan; the outputs keep the shape before it",
-                     static_cast<unsigned>(DISTRHO_PLUGIN_NUM_OUTPUTS));
         svc_.engine.setMaxBlockFrames(getBufferSize());
         svc_.invoker.setAudioThreadOwns(true);
         updateLatency();
@@ -410,6 +406,9 @@ private:
         for (int i = 0; i < 1000 && !readReady(); ++i) host_.pump();
         jsReady_ = readReady();
         if (!jsReady_) { d_stderr("[retroplug] control plane not ready"); return; }
+        // The graph's output wiring is built for the plugin's fixed outputs (the control plane owns it).
+        if (callGlobal("__rp_setHostOutputs", std::to_string(DISTRHO_PLUGIN_NUM_OUTPUTS).c_str()) != "true")
+            d_stderr("[retroplug] the %u-output host shape did not plan", static_cast<unsigned>(DISTRHO_PLUGIN_NUM_OUTPUTS));
 
         // Publish the host so an editor can attach its LVGL display to this same context.
         shared_.host = &host_;
diff --git a/packages/native/sdl/main.cpp b/packages/native/sdl/main.cpp
index aac7aecf..91bf20f6 100644
--- a/packages/native/sdl/main.cpp
+++ b/packages/native/sdl/main.cpp
@@ -385,6 +385,14 @@ std::string callGlobal(TjsHostRuntime& host, const char* name, const char* arg)
 
 bool hasGlobalFunction(JSContext* ctx, const char* name) { return jsHasGlobalFunction(ctx, name); }
 
+// Tell the control plane how many output channels the device has: it builds the graph's output wiring for
+// that many. Before the audio thread takes the Engine, so the plan it publishes applies at once.
+void setHostOutputs(AppState& a) {
+    if (callGlobal(a.host, "__rp_setHostOutputs", std::to_string(a.numOutputs).c_str()) != "true")
+        std::fprintf(stderr, "[retroplug-sdl] the %d-output host shape did not plan; the outputs keep the shape before it\n",
+                     a.numOutputs);
+}
+
 // ---- __rp_* window hooks (PluginUI's window-op seam, single-instance) ------------------------------
 // The UI optional-chains these, so anything we omit is simply inert. We provide the handful a handheld
 // standalone needs; the render-job hooks are left to a later milestone.
@@ -1214,9 +1222,7 @@ void reconfigureAudio(AppState& a, int sampleRate, int blockSize, int channels,
     a.reqOutputDevice = device;
     a.svc.engine.setSampleRate(sampleRate); // re-rate live cores (safe — audio stopped)
     openAudio(a);                       // opens a real stream, or leaves it muted (numOutputs still sized)
-    if (!a.svc.invoker.setHostShape({rp::host::HostShape::Kind::Multi, static_cast<std::uint32_t>(a.numOutputs)}))
-        std::fprintf(stderr, "[retroplug-sdl] the %d-output host shape did not plan; the outputs keep the shape before it\n",
-                     a.numOutputs);
+    setHostOutputs(a);
     a.svc.engine.setMaxBlockFrames(static_cast<std::uint32_t>(a.reqBlockSize));
     a.svc.invoker.setAudioThreadOwns(true);
     startAudio(a);                      // Pa_StartStream, or start the headless fallback pump
@@ -1654,9 +1660,7 @@ int main(int argc, char** argv) {
 
     // Hand the Engine to the audio thread (control-plane edits become push-only, drained per block) then start
     // it. Mirrors PluginDSP::activate. startAudio runs the PortAudio stream, or the fallback pump when muted.
-    if (!app.svc.invoker.setHostShape({rp::host::HostShape::Kind::Multi, static_cast<std::uint32_t>(app.numOutputs)}))
-        std::fprintf(stderr, "[retroplug-sdl] the %d-output host shape did not plan; the outputs keep the shape before it\n",
-                     app.numOutputs);
+    setHostOutputs(app);
     app.svc.engine.setMaxBlockFrames(static_cast<std::uint32_t>(app.reqBlockSize));
     app.svc.invoker.setAudioThreadOwns(true);
 #if defined(__linux__)
diff --git a/packages/native/src/host/rpc/AudioDriverRpcService.cpp b/packages/native/src/host/rpc/AudioDriverRpcService.cpp
index 10a7050d..012b3858 100644
--- a/packages/native/src/host/rpc/AudioDriverRpcService.cpp
+++ b/packages/native/src/host/rpc/AudioDriverRpcService.cpp
@@ -63,8 +63,6 @@ bool AudioDriverRpcService::startAudio() {
     // The quiescent path applied every push, so the ring is empty and the plan current: this live read is
     // accurate, and it's the last one before the audio thread takes over the count.
     liveSystemCount_.store(static_cast<std::uint32_t>(engine_.systemCount()), std::memory_order_relaxed);
-    if (!invoker_.setHostShape({rp::host::HostShape::Kind::Multi, 2}))  // the loop's stereo pair
-        std::fprintf(stderr, "[audio-driver] the stereo host shape did not plan; the loop keeps the shape before it\n");
     invoker_.setAudioThreadOwns(true);  // pushes become push-only BEFORE the thread spawns
     running_.store(true, std::memory_order_release);
     audioThread_ = std::thread([this] { audioLoop(); });
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 1cd3aebb..15028c7a 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -82,6 +82,15 @@ SystemBuildSpec toBuildSpec(const BackendConstructSpec& spec) {
 EngineRpcService::EngineRpcService(Engine& engine, SystemFactory& factory, QueuedInvoker& invoker)
     : engine_(engine), factory_(factory), invoker_(invoker) {}
 
+// Whether the planned graph writes exactly `channels` host channels: TS sets the shape a render call needs
+// before making it, and a render into buffers of another width would fill them from the wrong wiring.
+bool EngineRpcService::rendersInto(std::size_t channels) const {
+    if (engine_.hostWidth() == channels) return true;
+    std::fprintf(stderr, "[engine] a render into %zu channels was refused: the planned graph writes %u\n", channels,
+                 static_cast<unsigned>(engine_.hostWidth()));
+    return false;
+}
+
 // Out-of-line so unique_ptr<KitCompiler> can destroy a complete type (fwd-declared in the header).
 EngineRpcService::~EngineRpcService() = default;
 
@@ -291,8 +300,7 @@ bool EngineRpcService::stageSystemMidi(std::uint32_t id, std::vector<std::uint8_
 rfl::Bytestring EngineRpcService::renderAudio(double ms) {
     // This thread calls processBlock itself, which only the Engine's owning thread may do.
     if (ms <= 0.0 || invoker_.audioThreadOwns()) return {};
-    // A shape that does not plan leaves the last one running, which these buffers do not match.
-    if (!invoker_.setHostShape({rp::host::HostShape::Kind::Multi, 2})) return {};
+    if (!rendersInto(2)) return {};
     scratchL_.resize(kBlockSize);  // idempotent — no per-call realloc after the first
     scratchR_.resize(kBlockSize);
 
@@ -319,9 +327,7 @@ std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerSystem(double ms) {
     if (ms <= 0.0 || invoker_.audioThreadOwns()) return result;  // as renderAudio
     const std::size_t n = engine_.systemCount();
     if (n == 0) return result;
-    // More systems than rp.host.audio-out has buses for (32) do not plan, and rendering anyway would fill
-    // these lanes from the previous shape's wiring.
-    if (!invoker_.setHostShape({rp::host::HostShape::Kind::PerSystem, 0})) return result;
+    if (!rendersInto(2 * n)) return result;
 
     // One persistent L/R block buffer per system; the PerSystem shape routes each core into its own pair,
     // so linked systems interleave exactly as they do in the mixed path.
@@ -368,7 +374,7 @@ std::vector<rfl::Bytestring> EngineRpcService::renderAudioPerChannel(std::uint32
     // The target's output streams (Game Boy = 4 stereo: Pulse 1/Pulse 2/Wave/Noise; default = 1 "Mix").
     const std::size_t n = sys->channelLayout().size();
     if (n == 0) return result;
-    if (!invoker_.setHostShape({rp::host::HostShape::Kind::PerChannel, 0})) return result;  // as renderAudio
+    if (!rendersInto(2 * n)) return result;
 
     // One persistent L/R block buffer per stream; the PerChannel shape routes each stream into its own pair.
     std::vector<std::vector<float>> bl(n, std::vector<float>(kBlockSize));
@@ -574,8 +580,10 @@ RpcRenderBlocksResult EngineRpcService::renderBlocks(RpcRenderBlocksRequest req)
     result.error = renderBlocksRefusal(req);
     if (!result.error.empty()) return result;
 
-    if (!invoker_.setHostShape({rp::host::HostShape::Kind::Multi, req.outputs})) {
-        result.error = "the graph did not plan for " + std::to_string(req.outputs) + " outputs";
+    // The graph TS published must be built for these lanes (docs/modular-graph-phase-b2-design.md section 4).
+    if (engine_.hostWidth() != req.outputs) {
+        result.error = "the planned graph writes " + std::to_string(engine_.hostWidth()) + " host channels, not " +
+                       std::to_string(req.outputs);
         return result;
     }
     std::uint64_t total = 0;
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 887563f1..44aec5f5 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -117,6 +117,7 @@ public:
     std::vector<std::string>  dspTraceNames();
 
 private:
+    bool rendersInto(std::size_t channels) const;
     // Append the kernel's per-block MIDI-out (Engine::midiOut(), cleared each block) to accumMidiOut_.
     // Called after each processBlock in the render loops so drainMidiOut() sees the whole window.
     void accumulateMidiOut();
````

- [ ] **Step 5: The native tests**

````diff
diff --git a/packages/native/test/audio/EngineInvoker.test.cpp b/packages/native/test/audio/EngineInvoker.test.cpp
index 51ac67f8..b0f304f3 100644
--- a/packages/native/test/audio/EngineInvoker.test.cpp
+++ b/packages/native/test/audio/EngineInvoker.test.cpp
@@ -2,11 +2,9 @@
 // command's 4-byte inline slot (drainInto reads `len` bytes out of it), and a queued per-system MIDI
 // payload must be freed when teardown discards it unapplied. The leak itself only shows under
 // LeakSanitizer: build this binary in build-asan/ (tools/run-sanitizer.sh's tree) and run "[invoker]".
-// Also that a host shape the graph cannot hold is refused, and the render that asked for it with it.
 //
 // Run via `pnpm test:plugin`.
 
-#include <cstdio>
 #include <memory>
 #include <vector>
 
@@ -14,31 +12,6 @@
 
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
-#include "host/graph/Translator.hpp"
-#include "host/rpc/EngineRpcService.hpp"
-#include "system/SystemFactory.hpp"
-#include "system/sameboy/SameBoyConfig.hpp"
-#include "system/sameboy/SameBoySystem.hpp"
-
-#ifndef RP_MGB_ROM_PATH
-#error "RP_MGB_ROM_PATH must be defined (path to resources/roms/mGB.gb)"
-#endif
-
-namespace {
-
-std::vector<std::uint8_t> readRom() {
-    std::vector<std::uint8_t> bytes;
-    if (std::FILE* f = std::fopen(RP_MGB_ROM_PATH, "rb")) {
-        std::fseek(f, 0, SEEK_END);
-        bytes.resize(static_cast<std::size_t>(std::ftell(f)));
-        std::fseek(f, 0, SEEK_SET);
-        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
-        std::fclose(f);
-    }
-    return bytes;
-}
-
-} // namespace
 
 TEST_CASE("stageControllerMidi refuses what the inline slot cannot hold", "[invoker]") {
     Engine        eng(44100.0);
@@ -58,29 +31,3 @@ TEST_CASE("freePending frees a queued per-system MIDI payload", "[invoker]") {
     inv.setAudioThreadOwns(false);
     SUCCEED();
 }
-
-TEST_CASE("a host shape that does not plan is refused, and so is the render that wanted it", "[invoker]") {
-    // rp.host.audio-out takes at most 32 buses, and PerSystem gives each system one, so a 33rd system has
-    // nowhere to go. Rendering anyway would fill the 33 pairs from the stereo wiring still running.
-    Engine           eng(44100.0);
-    SystemFactory    factory;
-    QueuedInvoker    inv(eng);
-    EngineRpcService svc(eng, factory, inv);
-    const std::vector<std::uint8_t> rom = readRom();
-    REQUIRE_FALSE(rom.empty());
-    for (SystemId id = 1; id <= 33; ++id) {
-        auto sys = std::make_unique<SameBoySystem>(id, SameBoyConfig{}, rom);
-        sys->onActivate(eng.sampleRate());
-        inv.adoptSystem(std::move(sys));
-    }
-    REQUIRE(eng.systemCount() == 33);
-    const rp::host::HostShape stereo{rp::host::HostShape::Kind::Multi, 2};
-    CHECK(inv.setHostShape(stereo));  // already current
-    CHECK_FALSE(inv.setHostShape({rp::host::HostShape::Kind::PerSystem, 0}));
-    CHECK(eng.translator().shape() == stereo);
-    CHECK(svc.renderAudioPerSystem(10.0).empty());
-    CHECK(eng.translator().shape() == stereo);
-
-    inv.removeSystem(33);
-    CHECK(svc.renderAudioPerSystem(10.0).size() == 32);
-}
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 8a3fd281..5229ebec 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -13,6 +13,9 @@
 
 #include "NodeRig.hpp"
 #include "host/engine/Engine.hpp"
+#include "host/engine/EngineInvoker.hpp"
+#include "host/rpc/EngineRpcService.hpp"
+#include "system/SystemFactory.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
 #include "system/sameboy/SameBoyConfig.hpp"
@@ -83,6 +86,21 @@ std::string stereoDoc(const std::vector<Sys>& systems) {
     return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
 }
 
+// What buildGraph writes for the perSystem shape: slot i's mix on host channels 2i and 2i + 1.
+std::string perSystemDoc(const std::vector<Sys>& systems) {
+    std::string nodes = R"({"id":"kernel","type":"rp.dsp.kernel"})";
+    std::string wires, buses;
+    for (std::size_t i = 0; i < systems.size(); ++i) {
+        const std::string id = std::to_string(systems[i].id);
+        nodes += R"(,{"id":"sys-)" + id + R"(","type":")" + systems[i].type + R"(","settings":{"core":)" + id + "}}";
+        wires += std::string(i ? "," : "") + R"({"from":"sys-)" + id + R"(.mix","to":"out.out.)" + std::to_string(2 * i + 1) +
+                 "-" + std::to_string(2 * i + 2) + "\"}";
+        buses += std::string(i ? "," : "") + "2";
+    }
+    nodes += R"(,{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + buses + "]}}";
+    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
+}
+
 Engine::GraphRequest request(const std::vector<Sys>& systems) {
     Engine::GraphRequest r;
     r.document = stereoDoc(systems);
@@ -230,3 +248,28 @@ TEST_CASE("a sample-rate change re-rates the running plan's cores", "[engine-gra
     e.setSampleRate(44100.0);
     CHECK(raw->sampleRate_ == 44100.0);
 }
+
+TEST_CASE("a render into buffers the planned graph does not write is refused", "[engine-graph][b2]") {
+    // TS sets the shape a render call needs before it makes the call; the render calls only check it.
+    Engine           e(kRate);
+    SystemFactory    factory;
+    QueuedInvoker    inv(e);
+    EngineRpcService svc(e, factory, inv);
+    REQUIRE(park(e, mgb(1)));
+    REQUIRE(park(e, mgb(2)));
+    REQUIRE(e.setGraph(request({{1, kGb}, {2, kGb}})).ok);
+    CHECK(e.hostWidth() == 2);
+    CHECK_FALSE(svc.renderAudio(10.0).empty());
+    CHECK(svc.renderAudioPerSystem(10.0).empty());  // 4 lanes wanted, 2 planned
+    RpcRenderBlocksRequest blocks{4, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
+    CHECK_FALSE(svc.renderBlocks(blocks).error.empty());
+
+    Engine::GraphRequest perSystem;
+    perSystem.document = perSystemDoc({{1, kGb}, {2, kGb}});
+    perSystem.slots = {1, 2};
+    REQUIRE(e.setGraph(perSystem).ok);
+    CHECK(e.hostWidth() == 4);
+    CHECK(svc.renderAudioPerSystem(10.0).size() == 2);
+    CHECK(svc.renderAudio(10.0).empty());
+    CHECK(svc.renderBlocks(blocks).error.empty());
+}
````

- [ ] **Step 6: The mock tier**

The recorder renamed (`constructCalls` to `buildCalls`), `replacedBy` added, the `replaceId` assertions
rewritten, the fixture entries given their `built` facts, and the log checks naming `buildCore`:

````diff
diff --git a/packages/retroplug/test/config/format.test.ts b/packages/retroplug/test/config/format.test.ts
index dc348d0a..73342696 100644
--- a/packages/retroplug/test/config/format.test.ts
+++ b/packages/retroplug/test/config/format.test.ts
@@ -14,7 +14,7 @@ import { buildConfig, parseConfig, serializeConfig, DEFAULT_SETTINGS } from "../
 import type { SystemEntry } from "../../src/systemsList";
 
 const identity = (p: string) => p;
-const sys: SystemEntry = { id: 1, platform: "gb", core: "sameboy", romPath: "/roms/a.gb", savPath: "", savSuffix: 0, embeddedRom: "", battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [] };
+const sys: SystemEntry = { id: 1, platform: "gb", core: "sameboy", romPath: "/roms/a.gb", savPath: "", savSuffix: 0, embeddedRom: "", battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [], built: { type: "rp.sameboy.gb", exportMode: 0, linkGroup: 0 } };
 const projectJson = () => serializeConfig(buildConfig(DEFAULT_SETTINGS, [sys]), "", identity);
 
 test("stringifyConfig: 2-space indent, one field per line, trailing newline", () => {
diff --git a/packages/retroplug/test/config/watcher.test.ts b/packages/retroplug/test/config/watcher.test.ts
index 4b6fa990..150a5e57 100644
--- a/packages/retroplug/test/config/watcher.test.ts
+++ b/packages/retroplug/test/config/watcher.test.ts
@@ -91,9 +91,9 @@ test("pump: a changed ROM reloads a system with reloadOnRomChange on", () => {
   const r = fw.pump();
   expect(r.romReloaded.length).toBe(1);
   expect(r.romReloaded[0] !== id).toBeTruthy(); // reload swapped in a new id
-  // Reload is TS orchestration now: a replaceId construct (cold-boot the ROM carrying SRAM forward).
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id);
+  // Reload is TS orchestration now: a build into the same slot (cold-boot the ROM carrying SRAM forward).
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(systems.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
 });
 
 test("pump: a changed ROM is ignored when reloadOnRomChange is off", () => {
@@ -102,7 +102,7 @@ test("pump: a changed ROM is ignored when reloadOnRomChange is off", () => {
   systems.addSystem("/proj/a.gb"); // reloadOnRomChange defaults false
   be.emitFileChange("/proj/a.gb");
   expect(fw.pump().romReloaded).toEqual([]);
-  expect(be.constructCalls.length).toBe(1); // only the initial add — no reload construct
+  expect(be.buildCalls.length).toBe(1); // only the initial add — no reload construct
 });
 
 test("pump: an unrelated path (recent.json) is ignored", () => {
diff --git a/packages/retroplug/test/file-selection/browse.test.ts b/packages/retroplug/test/file-selection/browse.test.ts
index 8013fb76..1cf7d28d 100644
--- a/packages/retroplug/test/file-selection/browse.test.ts
+++ b/packages/retroplug/test/file-selection/browse.test.ts
@@ -70,7 +70,7 @@ test("browseAdd: an unpaired .sav awaits a 2nd ROM-only browser and pairs the re
   expect(out.kind).toBe("added");
   expect(be.fileBrowserCalls.length).toBe(2); // opened a 2nd browser
   expect(be.fileBrowserCalls[1].patterns.includes("*.sav")).toBeFalsy(); // ROM-only
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romPath).toBe("/roms/thegame.gb"); // paired with the chosen ROM
   expect(spec.savPath).toBe("/saves/orphan.sav"); // the picked save crossed as the override
 });
@@ -92,7 +92,7 @@ test("browseAdd: a cancelled 2nd browser yields cancelled", async () => {
   expect(out).toEqual({ kind: "cancelled" });
 });
 
-test("browseReplace: swaps the target instance in place (replaceId), leaving the list length", async () => {
+test("browseReplace: swaps the target instance in place, leaving the list length", async () => {
   const { be, fs, systems } = newFs();
   be.seed("/roms/a.gb", gbRom());
   be.seed("/roms/b.gb", gbRom());
@@ -101,8 +101,8 @@ test("browseReplace: swaps the target instance in place (replaceId), leaving the
   const out = await fs.browseReplace(id);
   expect(out.kind).toBe("replaced");
   expect(systems.view().length).toBe(1); // in place — not appended
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id); // swapped the target id
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(systems.view().map((s) => s.id)).toEqual([spec.id]); // built in the replaced system's slot
   expect(spec.romPath).toBe("/roms/b.gb");
 });
 
@@ -115,8 +115,8 @@ test("browseReplace: an unpaired .sav pairs via the 2nd browser before replacing
   be.queueBrowse("/saves/orphan.sav", "/roms/thegame.gb");
   const out = await fs.browseReplace(id);
   expect(out.kind).toBe("replaced");
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(systems.view().map((s) => s.id)).toEqual([spec.id]); // built in the replaced system's slot
   expect(spec.romPath).toBe("/roms/thegame.gb");
   expect(spec.savPath).toBe("/saves/orphan.sav"); // paired save crossed as the override
 });
@@ -142,8 +142,8 @@ test("browseSwap: opens a ROM-only browser and swaps in place, carrying the live
   expect(out.kind).toBe("swapped");
   expect(systems.view().length).toBe(1); // in place — not appended
   expect(be.fileBrowserCalls[be.fileBrowserCalls.length - 1].patterns.includes("*.sav")).toBeFalsy(); // ROM-only
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id); // swapped the target id
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(systems.view().map((s) => s.id)).toEqual([spec.id]); // built in the replaced system's slot
   expect(spec.romPath).toBe("/roms/b.gb");
   expect(new Uint8Array(spec.sramBytes!)).toEqual(sramBytesFor(id)); // the old battery crossed into the new cart
 });
diff --git a/packages/retroplug/test/menu/leaves.test.ts b/packages/retroplug/test/menu/leaves.test.ts
index dbefb7fe..70f0edfa 100644
--- a/packages/retroplug/test/menu/leaves.test.ts
+++ b/packages/retroplug/test/menu/leaves.test.ts
@@ -372,8 +372,8 @@ test("instance menu Replace Instance swaps the anchored instance in place", asyn
   expect(view.length).toBe(2); // in place — no add/remove
   expect(view[0].romPath).toBe("/roms/c.gb"); // the anchored slot now holds the new ROM
   expect(view.find((s) => s.id === otherId)?.romPath).toBe("/roms/b.gb"); // the other instance is untouched
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(anchoredId); // swapped the anchored id, not the focused tile
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(anchoredId); // swapped the anchored id, not the focused tile
 });
 
 test("instance menu Duplicate inherits + promotes the link group", () => {
@@ -638,8 +638,8 @@ test("system Swap ROM... opens a ROM-only browser and swaps in place, keeping th
 
   const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
   expect(last.patterns.includes("*.sav")).toBeFalsy(); // ROM-only browser (a .sav pick would fight "keep SRAM")
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id); // swapped in place
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(call)).toBe(id); // swapped in place
   expect(call.romPath).toBe("/roms/new.gb");
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(id)); // the live battery carried into the new ROM
 });
@@ -669,8 +669,8 @@ test("system Load State... reconstructs the system from the picked savestate", a
   findItem(items, "sys-loadstate")!.onSelect!();
   await flush();
 
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(call)).toBe(id); // in-place replace
   expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(999)); // the file's bytes seeded the core
 });
 
@@ -768,8 +768,8 @@ test("system Load SRAM... cold-boots the system with the picked file's SRAM", as
   findItem(items, "sys-loadsram")!.onSelect!();
   await flush();
 
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id);
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(call)).toBe(id);
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(999));
   expect(stores.project.systems.view()[0].savPath).toBe("/in/x.sav"); // auto-save target repointed to the load
 });
@@ -786,8 +786,8 @@ test("system New SRAM... opens a save dialog, boots blank, and repoints to the c
   const last = be.fileBrowserCalls[be.fileBrowserCalls.length - 1];
   expect(last.saving).toBe(true); // a save (create) dialog, not an open
   expect(last.patterns.includes("*.sav")).toBeTruthy();
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(call)).toBe(id); // in-place replace
   const seed = new Uint8Array(call.sramBytes!);
   expect(seed.length).toBe(0x20000); // native truncates/zero-pads to the cart's real battery size
   expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
@@ -808,8 +808,8 @@ test("system Reset reboots carrying the live battery, no dialog", () => {
   findItem(items, "sys-reset")!.onSelect!();
 
   expect(be.fileBrowserCalls.length).toBe(0); // pathless — no browser opens
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(id); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(call)).toBe(id); // in-place replace
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(id)); // the live battery carried forward
 });
 
diff --git a/packages/retroplug/test/menu/lsdj-assets.test.ts b/packages/retroplug/test/menu/lsdj-assets.test.ts
index 2ea670c9..a02e86a4 100644
--- a/packages/retroplug/test/menu/lsdj-assets.test.ts
+++ b/packages/retroplug/test/menu/lsdj-assets.test.ts
@@ -138,5 +138,5 @@ test("Remove Override drops the override end-to-end (reload reverts the effectiv
   expect(findItem(palettes, "lsdj-palette-1")?.label).toBe("[1] ABCD"); // back to the base name (no *)
   expect(actionsOf(palettes, "lsdj-palette-1").some((a) => a.id === "lsdj-palette-1-remove")).toBe(false);
   // The last construct carries the base ROM (no override-patched romBytes).
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined);
 });
diff --git a/packages/retroplug/test/menu/lsdj-songs.test.ts b/packages/retroplug/test/menu/lsdj-songs.test.ts
index deca04b7..a3688b82 100644
--- a/packages/retroplug/test/menu/lsdj-songs.test.ts
+++ b/packages/retroplug/test/menu/lsdj-songs.test.ts
@@ -75,8 +75,8 @@ test("the LSDj song menu offers Move Up/Down (disabled at the ends) and swaps sp
 
   // Move Down on the first row (position 0) → swaps slots 0 and 3 → [0] BBB, [3] AAA.
   findItem(submenuChildren(songs(), "lsdj-song-0"), "lsdj-song-0-down")!.onSelect!();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   expect(listProjects(spec.sramBytes!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:BBB", "3:AAA"]);
 });
 
@@ -105,8 +105,8 @@ test("a LINKED, edited LSDj working song gets the [working] row, offering Save C
 
   // Save Changes writes the edit into slot 0 and leaves the song count alone.
   findItem(w, "lsdj-song-working-save")!.onSelect!();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   expect(listProjects(spec.sramBytes!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:AAA", "3:BBB"]);
   expect(decompressSlot(spec.sramBytes!, 0)![0x100]).toBe(edited[0x100]); // the edit really landed
 });
diff --git a/packages/retroplug/test/menu/recent-rows.test.ts b/packages/retroplug/test/menu/recent-rows.test.ts
index 92cb9bff..e98db062 100644
--- a/packages/retroplug/test/menu/recent-rows.test.ts
+++ b/packages/retroplug/test/menu/recent-rows.test.ts
@@ -88,24 +88,24 @@ function smsSongsMenu(ram: Parameters<typeof smsggdjRam>[0]) {
 
 test("Songs > Load on a running smsggdj cart loads right there, live - no reboot, no wait", () => {
   const m = smsSongsMenu({ booted: true });
-  const builds = m.be.constructCalls.length;
+  const builds = m.be.buildCalls.length;
   const load = m.load(1);
   expect(load.kind).toBe("action"); // a clean working song: no guard
   load.onSelect!();
   expect(m.working()).toEqual(block(2)); // BETA, this very call
   expect(m.stores.project.hasSongRequest()).toBe(false); // settled inline
-  expect(m.be.constructCalls.length).toBe(builds); // no cold boot
+  expect(m.be.buildCalls.length).toBe(builds); // no cold boot
 });
 
 test("Songs > Load on an smsggdj cart that is still booting parks the request and settles it later", () => {
   // Before: the live load declined (rightly - the boot would have erased it), and the menu fell back to
   // the cold-boot path, which on v0.45 boots BLANK. Now the row is a request the frame tick settles.
   const m = smsSongsMenu({ booted: false });
-  const builds = m.be.constructCalls.length;
+  const builds = m.be.buildCalls.length;
   m.load(1).onSelect!();
   expect(m.stores.project.hasSongRequest()).toBe(true);
   expect(m.writes()).toBe(0); // not a byte into a booting cart
-  expect(m.be.constructCalls.length).toBe(builds); // and no cold boot either
+  expect(m.be.buildCalls.length).toBe(builds); // and no cold boot either
   expect(m.working()).toEqual(new Uint8Array(SMDJ4_BLOCK_LEN));
 
   expect(m.stores.project.settleSong()).toBe("waiting"); // the tick, while the cart boots
diff --git a/packages/retroplug/test/menu/risa.test.ts b/packages/retroplug/test/menu/risa.test.ts
index 40f14712..4f4ed1c3 100644
--- a/packages/retroplug/test/menu/risa.test.ts
+++ b/packages/retroplug/test/menu/risa.test.ts
@@ -158,8 +158,8 @@ test("Delete confirms then removes the song end-to-end (readSram -> op -> loadSr
 
   // The op wrote the edited catalog back and cold-booted the core from it: the last construct carries a
   // 4-song catalog (DBZ gone).
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   expect(listSongs(new Uint8Array(spec.sramBytes!)).map((s) => s.name)).toEqual(["HOU8", "HOU", "DBZ2-F", "FUNK0"]);
 });
 
@@ -170,7 +170,7 @@ test("Move Down reorders the catalog end-to-end", () => {
   const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");
 
   findItem(submenuChildren(songRows, "risa-song-0"), "risa-song-0-down")!.onSelect!(); // HOU8 down one
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(listSongs(new Uint8Array(spec.sramBytes!)).map((s) => s.name)).toEqual(["HOU", "HOU8", "DBZ", "DBZ2-F", "FUNK0"]);
 });
 
@@ -197,8 +197,8 @@ test("Load expands the selected song into working memory end-to-end (current-lay
   // The cold-booted core carries a battery whose working song (banks 0-3) is the expanded BLUMARBL, and
   // whose catalog (banks 4-7) is unchanged. The one byte that differs from a bare record expansion is the
   // 'current entry' link at 0x3E94, which Load stamps with the slot it came from (0), as the cart does.
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   const out = new Uint8Array(spec.sramBytes!);
   const expected = expandRecordToWorking(recordBytesAt(battery, CURRENT_LAYOUT, 0)!);
   expect(out.slice(0, 0x8000).every((b, i) => (i === 0x3e94 ? b === 0 : b === expected[i]))).toBe(true);
@@ -210,10 +210,10 @@ test("Load is a safe no-op on a legacy-layout catalog (no cold-boot)", () => {
   const stores = composeAppStores({ backend: be });
   const { items } = risaSystem(be, stores); // seeds the legacy multi_legacy catalog
   const songRows = submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs");
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
 
   findItem(submenuChildren(songRows, "risa-song-0"), "risa-song-0-load")!.onSelect!();
-  expect(be.constructCalls.length).toBe(before); // loadSongToWorkingInSav returned null -> mutateSavBytes no-op
+  expect(be.buildCalls.length).toBe(before); // loadSongToWorkingInSav returned null -> mutateSavBytes no-op
 });
 
 // A distinct romPath from the header-prefix risaRom() the Songs tests use — the asset inventory is memoised
@@ -305,8 +305,8 @@ test("Delete on a base kit records an erase override and empties the effective s
   // The erase override drops the slot from the effective list AND the reload handed native a patched ROM
   // whose slot 0 is unpopulated. (reloadSystem swaps the id, so re-query the sole system by index.)
   expect(kits().some((k) => k.id === "risa-kit-0")).toBe(false);
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   expect(RisaRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false);
 });
 
@@ -359,8 +359,8 @@ test("an unlinked working song whose NAME matches a slot offers to overwrite it,
 
   // Taking it writes the working song over slot 0 and links there: one song, not two.
   overwrite.onSelect!();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   const out = new Uint8Array(spec.sramBytes!);
   expect(listSongs(out).map((s) => s.name)).toEqual(["BLUMARBL"]);
   expect(risaSongCatalog.workingSongDirty!(out)).toBe(false); // committed, so the row drops
@@ -389,8 +389,8 @@ test("Save to Catalog promotes the working song end-to-end (readSram -> op -> co
 
   findItem(workingRow(items), "risa-song-working-save")!.onSelect!();
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
-  expect(spec.replaceId).toBe(id);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
+  expect(be.replacedBy(spec)).toBe(id);
   expect(listSongs(new Uint8Array(spec.sramBytes!)).map((s) => s.name)).toEqual(["BLUMARBL"]); // now a real slot
 });
 
@@ -429,13 +429,13 @@ test("risa Save & Load commits the working song then loads, in one cold boot", (
   const stores = composeAppStores({ backend: be });
   const { items, id } = risaSystem(be, stores, unsavedWorkingBattery());
   const before = risaSongCatalog.list(be.readSram(id)!).length;
-  const writesBefore = be.constructCalls.length;
+  const writesBefore = be.buildCalls.length;
 
   const load = findItem(submenuChildren(submenuChildren(submenuChildren(items(), "inst-risa"), "risa-songs"), "risa-song-0"), "risa-song-0-load")!;
   findItem(load.children!, "risa-song-0-load-save")!.onSelect!();
 
-  const out = new Uint8Array(be.constructCalls[be.constructCalls.length - 1].sramBytes!);
+  const out = new Uint8Array(be.buildCalls[be.buildCalls.length - 1].sramBytes!);
   expect(risaSongCatalog.list(out).length).toBe(before + 1); // the working song became a new slot
   expect(risaSongCatalog.workingSongDirty!(out)).toBe(false); // nothing left uncommitted
-  expect(be.constructCalls.length - writesBefore).toBe(1); // save+load is ONE byte-op
+  expect(be.buildCalls.length - writesBefore).toBe(1); // save+load is ONE byte-op
 });
diff --git a/packages/retroplug/test/menu/rom-patch.test.ts b/packages/retroplug/test/menu/rom-patch.test.ts
index 932a02e2..6ac3309f 100644
--- a/packages/retroplug/test/menu/rom-patch.test.ts
+++ b/packages/retroplug/test/menu/rom-patch.test.ts
@@ -93,7 +93,7 @@ test("Patch ROM in Place bakes the overrides into the ROM on disk and clears the
   const kids = lsdjKids(be, stores, "/roms/bake.gb");
   stores.project.systems.setRoleConfig(stores.project.systems.view()[0].id, "lsdj-assets", { overrides: [NEON] });
   const base = be.readFile("/roms/bake.gb")!;
-  const constructs = be.constructCalls.length;
+  const constructs = be.buildCalls.length;
 
   const row = findItem(kids(), "lsdj-patch-rom")!;
   expect(row.prompt!.confirm).toBeTruthy(); // an irreversible write always confirms
@@ -111,7 +111,7 @@ test("Patch ROM in Place bakes the overrides into the ROM on disk and clears the
   // was evicted; without that it would still be parsing the pre-patch bytes).
   expect(findItem(submenuChildren(kids(), "lsdj-palettes"), "lsdj-palette-1")?.label).toBe("[1] NEON");
   // No cold boot: the effective ROM is byte-identical to what the core is already running.
-  expect(be.constructCalls.length).toBe(constructs);
+  expect(be.buildCalls.length).toBe(constructs);
 });
 
 test("the baked ROM is byte-identical to the image construct was already handing the core", () => {
@@ -121,7 +121,7 @@ test("the baked ROM is byte-identical to the image construct was already handing
   const id = stores.project.systems.view()[0].id;
   stores.project.systems.setRoleConfig(id, "lsdj-assets", { overrides: [NEON] });
   stores.project.systems.reloadSystem(id); // construct folds the override in -> the EFFECTIVE image
-  const effective = new Uint8Array(be.constructCalls[be.constructCalls.length - 1].romBytes!);
+  const effective = new Uint8Array(be.buildCalls[be.buildCalls.length - 1].romBytes!);
 
   findItem(kids(), "lsdj-patch-rom")!.prompt!.onConfirm("");
 
@@ -217,7 +217,7 @@ test("BlipToaster's bake carries the pinned SETTINGS too, and a settings pin alo
   // Bake must be the image construct hands the core - settings included, or the .nes on disk would boot
   // differently from the project that wrote it.
   stores.project.systems.reloadSystem(stores.project.systems.view()[0].id);
-  const effective = new Uint8Array(be.constructCalls[be.constructCalls.length - 1].romBytes!);
+  const effective = new Uint8Array(be.buildCalls[be.buildCalls.length - 1].romBytes!);
   expect(findItem(kids(), "bliptoaster-patch-rom")!.prompt!.onConfirm("")).toBe(null);
   expect(sameBytes(be.readFile("/roms/bake-bt.nes")!, effective)).toBe(true);
 
diff --git a/packages/retroplug/test/menu/song-guard.test.ts b/packages/retroplug/test/menu/song-guard.test.ts
index 10b790ce..78015c76 100644
--- a/packages/retroplug/test/menu/song-guard.test.ts
+++ b/packages/retroplug/test/menu/song-guard.test.ts
@@ -90,7 +90,7 @@ test("Save & Load commits the working song to its own slot FIRST, then loads - i
   dirty[0x100] ^= 0xff;
   const edited = dirty.slice(0, 0x8000);
   const { be, songRow, savOnDisk } = cart(dirty);
-  const writesBefore = be.constructCalls.length;
+  const writesBefore = be.buildCalls.length;
 
   findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!.onSelect!();
 
@@ -103,7 +103,7 @@ test("Save & Load commits the working song to its own slot FIRST, then loads - i
   expect(lsdjSongCatalog.workingName(out)).toBe("INTRO");
   // ONE cold boot, not two: save+load is a single byte-op, so the second half can't run against a stale
   // system id (loadSram allocates a new one every rebuild).
-  expect(be.constructCalls.length - writesBefore).toBe(1);
+  expect(be.buildCalls.length - writesBefore).toBe(1);
 });
 
 test("Replace confirms before touching a saved slot", () => {
@@ -151,7 +151,7 @@ test("the name prompt refuses an empty name and keeps itself open", () => {
 test("the name prompt commits under the typed name, then loads - still one cold boot", () => {
   const { be, songRow, savOnDisk } = cart(unlinkedDirtySav());
   const edited = unlinkedDirtySav().slice(0, 0x8000);
-  const writesBefore = be.constructCalls.length;
+  const writesBefore = be.buildCalls.length;
   const save = findItem(findItem(songRow(1), "lsdj-song-1-load")!.children!, "lsdj-song-1-load-save")!;
 
   expect(save.prompt!.onConfirm("my song!")).toBe(null); // null closes the overlay = success
@@ -162,7 +162,7 @@ test("the name prompt commits under the typed name, then loads - still one cold
   expect(lsdjSongCatalog.list(out).length).toBe(3); // a new slot, the two originals untouched
   expect([...decompressSlot(out, 2)!]).toEqual([...edited]); // the work survived
   expect(lsdjSongCatalog.workingName(out)).toBe("INTRO"); // and the picked song is loaded
-  expect(be.constructCalls.length - writesBefore).toBe(1);
+  expect(be.buildCalls.length - writesBefore).toBe(1);
 });
 
 test("a full catalog offers no save, and says why instead of failing on click", () => {
diff --git a/packages/retroplug/test/menu/song-import.test.ts b/packages/retroplug/test/menu/song-import.test.ts
index fa281a09..0596fc65 100644
--- a/packages/retroplug/test/menu/song-import.test.ts
+++ b/packages/retroplug/test/menu/song-import.test.ts
@@ -23,7 +23,7 @@ import { normalizeSaveContainer, listSongs, isRisaSav } from "../../src/risaSav"
 const findItem = (items: MenuItem[], id: string) => items.find((i) => i.id === id);
 const noop = () => {};
 const handlers = { toggle: noop, toggleAll: noop, apply: noop, onClose: noop };
-const lastSram = (be: MockBackend): Uint8Array => be.constructCalls[be.constructCalls.length - 1].sramBytes!;
+const lastSram = (be: MockBackend): Uint8Array => be.buildCalls[be.buildCalls.length - 1].sramBytes!;
 
 // --- fixtures ------------------------------------------------------------------------------------------
 
@@ -196,9 +196,9 @@ test("applyImport reports a PARTIAL import when the target fills up (imported <
 test("applyImport with nothing checked is a no-op (no write)", () => {
   const { be, stores, sys } = lsdjSystem();
   const pick = planImport(sys(), lsdjSource()) as PickState;
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
   expect(applyImport(stores, { ...pick, checked: new Set() })).toEqual({ requested: 0, imported: 0 });
-  expect(be.constructCalls.length).toBe(before); // no rebuild → nothing written
+  expect(be.buildCalls.length).toBe(before); // no rebuild → nothing written
 });
 
 // --- menu wiring: the Songs "Add..." routes a picked .sav to beginSongImport --------------------------
diff --git a/packages/retroplug/test/project/config.test.ts b/packages/retroplug/test/project/config.test.ts
index d93d7774..9dda3ae4 100644
--- a/packages/retroplug/test/project/config.test.ts
+++ b/packages/retroplug/test/project/config.test.ts
@@ -20,7 +20,7 @@ import type { SystemEntry } from "../../src/systemsList";
 const identity = (p: string) => p;
 
 function sys(id: number, romPath: string, savPath = "", savSuffix = 0, embeddedRom = ""): SystemEntry {
-  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom, battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [] };
+  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom, battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [], built: { type: "rp.sameboy.gb", exportMode: 0, linkGroup: 0 } };
 }
 
 test("schema: checkVersion + parseProjectVersion", () => {
diff --git a/packages/retroplug/test/project/export.test.ts b/packages/retroplug/test/project/export.test.ts
index f8a642ce..2fbfcb47 100644
--- a/packages/retroplug/test/project/export.test.ts
+++ b/packages/retroplug/test/project/export.test.ts
@@ -82,7 +82,7 @@ test("export then load: round-trips systems (blobs seed each emulator) + setting
   // Both new systems were reconstructed FROM the archive's blobs (not cold-booted).
   expect(be.restoredIds()).toEqual(project.systems.systems().map((s) => s.id).sort((a, b) => a - b));
   // The exact archive bytes (keyed by the EXPORT-time ids) reached construct.
-  const imported = be.constructCalls.slice(-2);
+  const imported = be.buildCalls.slice(-2);
   expect(new Uint8Array(imported[0].stateBytes!)).toEqual(stateBytesFor(mgbId));
   expect(new Uint8Array(imported[1].stateBytes!)).toEqual(stateBytesFor(aId));
 });
diff --git a/packages/retroplug/test/project/song-request.test.ts b/packages/retroplug/test/project/song-request.test.ts
index b1a3e25d..fc4e6fc7 100644
--- a/packages/retroplug/test/project/song-request.test.ts
+++ b/packages/retroplug/test/project/song-request.test.ts
@@ -238,10 +238,10 @@ test("LSDj settles synchronously - its working song is the battery, ready from t
   expect(project.save("/proj/lsdj.rplg")).toBeTruthy();
   expect(recent.view().map((v) => v.song)).toEqual(["GRUB"]); // recorded at once: no readiness to wait for
 
-  const builds = be.constructCalls.length;
+  const builds = be.buildCalls.length;
   project.requestSong({ name: "INTRO", confirmed: false });
   expect(project.settleSong()).toBe("loaded"); // this very call
-  expect(be.constructCalls.length).toBe(builds + 1); // the cold-boot spine: one rebuild from the written .sav
+  expect(be.buildCalls.length).toBe(builds + 1); // the cold-boot spine: one rebuild from the written .sav
   expect(lsdjSongCatalog.workingName(be.readFile("/roms/lsdj.sav")!)).toBe("INTRO");
   expect(recent.view().map((v) => v.song)).toEqual(["INTRO", "GRUB"]);
 });
diff --git a/packages/retroplug/test/system-settings/construct-settings.test.ts b/packages/retroplug/test/system-settings/construct-settings.test.ts
index fdfa0fa6..4ce7e31c 100644
--- a/packages/retroplug/test/system-settings/construct-settings.test.ts
+++ b/packages/retroplug/test/system-settings/construct-settings.test.ts
@@ -24,7 +24,7 @@ test("adopt forwards the saved sameboy role config as the construct-time setting
   const roles = [{ kind: "sameboy", config: { model: "dmgB", highpass: "removeDcOffset", linkGroupId: 0, fastBoot: true } }];
   store.adopt({ romPath: "/roms/a.gb", roles });
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.settings != null).toBeTruthy();
   expect(JSON.parse(spec.settings!)).toEqual({ model: 1, highpass: 2, linkGroupId: 0, fastBoot: true }); // native-encoded blob
 });
@@ -38,7 +38,7 @@ test("adopt forwards the saved mesen (NES) role config as the construct-time set
   const roles = [{ kind: "mesen", config: { region: "pal", removeSpriteLimit: true } }];
   store.adopt({ romPath: "/roms/a.nes", roles });
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.settings != null).toBeTruthy();
   expect(JSON.parse(spec.settings!)).toEqual({ region: 2, removeSpriteLimit: true }); // native-encoded blob
 });
@@ -48,6 +48,6 @@ test("a fresh add sends no settings blob (backend defaults suffice)", () => {
   be.seed("/roms/b.gb", gbRom());
 
   store.addSystem("/roms/b.gb"); // fresh construct — defaultRoles run AFTER construct, so no blob
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.settings).toBe(undefined);
 });
diff --git a/packages/retroplug/test/systems/bliptoaster-assets.test.ts b/packages/retroplug/test/systems/bliptoaster-assets.test.ts
index 2c5fa173..93e3f093 100644
--- a/packages/retroplug/test/systems/bliptoaster-assets.test.ts
+++ b/packages/retroplug/test/systems/bliptoaster-assets.test.ts
@@ -45,7 +45,7 @@ test("a BlipToaster system attaches an empty bliptoaster-assets role and constru
   const id = store.addSystem("/roms/synth.nes");
   expect(id).toBeTruthy();
   expect(store.view()[0].roles.map((r) => r.kind).includes("bliptoaster-assets")).toBeTruthy();
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
 });
 
 // ── The baked rig settings (the same role config, a sibling key to `overrides`) ────────────────────────
@@ -54,7 +54,7 @@ test("a BlipToaster system attaches an empty bliptoaster-assets role and constru
 
 /** The effective ROM's decoded settings after the latest construct (null when construct got no romBytes). */
 function constructedSettings(be: MockBackend) {
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   return spec.romBytes ? BlipToasterRom.fromBytes(spec.romBytes).settings() : null;
 }
 
@@ -83,7 +83,7 @@ test("settings and asset overrides ride the same config and are both applied", (
   });
   store.reloadSystem(id);
 
-  const patched = BlipToasterRom.fromBytes(be.constructCalls[be.constructCalls.length - 1].romBytes!);
+  const patched = BlipToasterRom.fromBytes(be.buildCalls[be.buildCalls.length - 1].romBytes!);
   expect(patched.settings()).toEqual({ baseChannel: 3, ppu: false, velCurve: true, theme: 0, font: 0 });
   expect(decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes).name).toBe("NEON");
 });
@@ -94,7 +94,7 @@ test("a settings field written on its own still triggers the patch (an empty ove
   const id = store.addSystem("/roms/synth.nes")!;
   store.setRoleConfig(id, "bliptoaster-assets", { settings: { ppu: false } });
   store.reloadSystem(id);
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes != null).toBeTruthy();
   expect(constructedSettings(be)!.ppu).toBe(false);
 });
 
@@ -108,7 +108,7 @@ test("clearing the settings patch reverts the next construct to the base ROM (no
 
   store.setRoleConfig(id2, "bliptoaster-assets", { settings: {} });
   store.reloadSystem(id2);
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // nothing pinned → base ROM
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined); // nothing pinned → base ROM
 });
 
 test("a ROM with no readable settings block takes the asset overrides and ignores the settings patch", () => {
@@ -123,7 +123,7 @@ test("a ROM with no readable settings block takes the asset overrides and ignore
   });
   store.reloadSystem(id);
 
-  const patched = BlipToasterRom.fromBytes(be.constructCalls[be.constructCalls.length - 1].romBytes!);
+  const patched = BlipToasterRom.fromBytes(be.buildCalls[be.buildCalls.length - 1].romBytes!);
   expect(patched.hasSettings).toBe(false);
   expect(patched.settings()).toBe(null);
   expect(decodeThemeFromRom(patched.getTheme(0)!.recordBytes, patched.getTheme(0)!.nameBytes).name).toBe("NEON");
@@ -142,7 +142,7 @@ test("a settings block stamped a format this build does not read is left entirel
 
   // Half-writing a layout we do not know would be worse than doing nothing, so construct hands over an
   // untouched image rather than a partly-patched one.
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect([...(spec.romBytes ?? before)]).toEqual([...before]);
 });
 
@@ -156,7 +156,7 @@ test("a theme override is stored INLINE (no path) and applied to the effective R
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [override] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no file link
 
@@ -178,7 +178,7 @@ test("a kit override LINKS a .rkit bank by path and splices it into the effectiv
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = BlipToasterRom.fromBytes(spec.romBytes!);
   expect([...patched.getKitBank(0)!]).toEqual([...bank]); // bank spliced verbatim
@@ -197,7 +197,7 @@ test("a banking ROM takes a kit override into a high slot; the base kit is prese
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 5, name: "HATS", path: "/kits/hats.rkit" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = BlipToasterRom.fromBytes(spec.romBytes!);
   expect([...patched.getKitBank(5)!]).toEqual([...bank]); // spliced into bank 5
@@ -216,7 +216,7 @@ test("a font override LINKS a .chr file by path and applies it to the effective
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "font", slot: 0, name: "new", path: "/fonts/new.chr" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect([...BlipToasterRom.fromBytes(spec.romBytes!).getChrFontSlot(0)!]).toEqual([...bank]);
   expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
@@ -231,7 +231,7 @@ test("a wrong-size .chr font override is skipped (leaves the base font intact)",
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "font", slot: 0, name: "bad", path: "/fonts/bad.chr" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   const slot0 = BlipToasterRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!).getChrFontSlot(0)!;
   expect(slot0[0]).toBe((0 * 7 + 3) & 0xff); // base font pattern, not 0x00-filled by a bad override
 });
@@ -246,7 +246,7 @@ test("an erase kit override empties the base kit slot in the effective ROM", ()
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect(BlipToasterRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false); // cleared
   expect([...be.readFile("/roms/synth.nes")!]).toEqual([...base]);
@@ -261,7 +261,7 @@ test("a wrong-size .rkit kit override is skipped (leaves the base kit intact)",
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "bad", path: "/kits/bad.rkit" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   const patched = BlipToasterRom.fromBytes(spec.romBytes ?? be.readFile("/roms/synth.nes")!);
   expect(patched.isKitPopulated(0)).toBe(true); // base kit intact, not zeroed by a bad override
 });
@@ -275,11 +275,11 @@ test("Remove Override reverts the next construct to the base ROM (no romBytes)",
   be.seed("/kits/drums.rkit", populatedBank(0x22));
   store.setRoleConfig(id, "bliptoaster-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/kits/drums.rkit" }] });
   const id2 = store.reloadSystem(id)!; // reload swaps the id in place
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes != null).toBeTruthy();
 
   store.setRoleConfig(id2, "bliptoaster-assets", { overrides: [] });
   store.reloadSystem(id2);
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined);
 });
 
 test("Duplicate boots the clone from the patched ROM, not the base one", () => {
@@ -289,13 +289,13 @@ test("Duplicate boots the clone from the patched ROM, not the base one", () => {
   store.setRoleConfig(id, "bliptoaster-assets", { settings: { theme: 11 } });
   const src = store.reloadSystem(id)!;
   expect(constructedSettings(be)!.theme).toBe(11);
-  const srcRom = be.constructCalls[be.constructCalls.length - 1].romBytes;
+  const srcRom = be.buildCalls[be.buildCalls.length - 1].romBytes;
 
   // The clone boots from the source's savestate, which was captured running the PATCHED ROM; booting it
   // under the base image would run that state against code and data it was never captured with.
   const dup = store.duplicateSystem(src);
   expect(dup).toBeTruthy();
-  const call = be.constructCalls[be.constructCalls.length - 1];
+  const call = be.buildCalls[be.buildCalls.length - 1];
   expect(call.stateBytes != null).toBeTruthy();
   expect(call.romBytes != null).toBeTruthy();
   expect(Array.from(call.romBytes!)).toEqual(Array.from(srcRom!));
diff --git a/packages/retroplug/test/systems/kind.test.ts b/packages/retroplug/test/systems/kind.test.ts
index bfb36fce..0b08e17d 100644
--- a/packages/retroplug/test/systems/kind.test.ts
+++ b/packages/retroplug/test/systems/kind.test.ts
@@ -20,7 +20,7 @@ test("constructSystem carries the classified platform and core for each backend"
     const { be, store } = makeStore();
     be.seed(path, bytes);
     expect(store.addSystem(path) != null).toBeTruthy();
-    const spec = be.constructCalls[be.constructCalls.length - 1];
+    const spec = be.buildCalls[be.buildCalls.length - 1];
     expect(spec.platform).toBe(platform);
     expect(spec.core).toBe(core);
   }
@@ -29,7 +29,7 @@ test("constructSystem carries the classified platform and core for each backend"
 test("embedded mGB constructs as sameboy", () => {
   const { be, store } = makeStore();
   store.loadMgb();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.platform).toBe("gb");
   expect(spec.core).toBe("sameboy");
 });
diff --git a/packages/retroplug/test/systems/list.test.ts b/packages/retroplug/test/systems/list.test.ts
index 104a0c31..f93c84e9 100644
--- a/packages/retroplug/test/systems/list.test.ts
+++ b/packages/retroplug/test/systems/list.test.ts
@@ -18,7 +18,7 @@ import {
 } from "../../src/systemsList";
 
 function entry(id: number, romPath: string, savSuffix = 0, savPath = ""): SystemEntry {
-  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom: "", battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [] };
+  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom: "", battery: false, settings: { gainDb: 0, reloadOnRomChange: false }, roles: [], built: { type: "rp.sameboy.gb", exportMode: 0, linkGroup: 0 } };
 }
 
 test("findById / appendEntry: append keeps order and returns a new list", () => {
diff --git a/packages/retroplug/test/systems/lsdj-assets.test.ts b/packages/retroplug/test/systems/lsdj-assets.test.ts
index 349137fa..9890c2fd 100644
--- a/packages/retroplug/test/systems/lsdj-assets.test.ts
+++ b/packages/retroplug/test/systems/lsdj-assets.test.ts
@@ -48,7 +48,7 @@ test("an LSDj system attaches an empty lsdj-assets role and constructs with no r
   const id = store.addSystem("/roms/song.gb");
   expect(id).toBeTruthy();
   expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-assets")).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes).toBe(undefined); // no overrides → base ROM loaded from romPath
 });
 
@@ -67,7 +67,7 @@ test("replacing a kit records an override and reload hands native the patched ef
   expect(store.reloadSystem(id)).toBeTruthy();
 
   // The reload constructed with a patched effective ROM (romBytes), NOT by touching romPath.
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = LsdjRom.fromBytes(spec.romBytes!);
   expect(patched.kit(0).name()).toBe("NEWK");
@@ -92,7 +92,7 @@ test("a palette override is stored INLINE (colorSets, no file path) and applied
 
   store.setRoleConfig(id, "lsdj-assets", { overrides: [override] });
   store.reloadSystem(id);
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no path stored
 
@@ -113,7 +113,7 @@ test("a kit erase override empties a base-valid kit slot in the effective ROM (D
   store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = LsdjRom.fromBytes(spec.romBytes!);
   expect(patched.kit(0).valid).toBe(false); // the slot is now empty
@@ -129,10 +129,10 @@ test("Remove Override reverts the next construct to the base ROM (no romBytes)",
   be.seed("/kits/new.kit", bank);
   store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "NEWK", path: "/kits/new.kit" }] });
   const id2 = store.reloadSystem(id)!; // reload swaps the id in place
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes != null).toBeTruthy();
 
   // Remove the override (menu's "Remove Override") → next construct is the base ROM again.
   store.setRoleConfig(id2, "lsdj-assets", { overrides: [] });
   store.reloadSystem(id2);
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined);
 });
diff --git a/packages/retroplug/test/systems/lsdj-lsdprj-safety.test.ts b/packages/retroplug/test/systems/lsdj-lsdprj-safety.test.ts
index 49e0108e..ad720204 100644
--- a/packages/retroplug/test/systems/lsdj-lsdprj-safety.test.ts
+++ b/packages/retroplug/test/systems/lsdj-lsdprj-safety.test.ts
@@ -93,11 +93,11 @@ test("importSongFiles skips a .lsdprj when the base ROM can't be read (no reboot
   be.setSram(id, savFrom({}));
   be.seed("/s/b.lsdprj", lsdprjWithKit());
   const sys = store.view().find((s) => s.id === id)!;
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
   const orig = be.readFile.bind(be);
   be.readFile = (p: string) => (p === "/roms/song.gb" ? null : orig(p)); // the ROM becomes unreadable
   expect(importSongFiles(be, store, sys, ["/s/b.lsdprj"])).toBe(false); // .lsdprj needs the ROM → skipped → nothing applied
-  expect(be.constructCalls.length).toBe(before); // no cold-boot
+  expect(be.buildCalls.length).toBe(before); // no cold-boot
 });
 
 test("importSongFiles applies the valid song and skips a malformed one in the same batch (one reboot)", () => {
@@ -110,7 +110,7 @@ test("importSongFiles applies the valid song and skips a malformed one in the sa
   be.seed("/s/bad.lsdprj", new Uint8Array([9, 9, 9])); // malformed .lsdprj
   const sys = store.view().find((s) => s.id === id)!;
   expect(importSongFiles(be, store, sys, ["/s/good.lsdsng", "/s/bad.lsdprj"])).toBe(true); // the good one still lands
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(listProjects(spec.sramBytes!).map((p) => p.name)).toEqual(["GOOD"]); // only the good song; the bad file corrupted nothing
   expect(sameBytes(decompressSlot(spec.sramBytes!, 0)!, good)).toBe(true);
 });
@@ -122,10 +122,10 @@ test("importSongFiles: a failed atomic write aborts cleanly — false, no reboot
   be.setSram(id, savFrom({}));
   be.seed("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, rawSong(90)));
   const sys = store.view().find((s) => s.id === id)!;
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
   be.writeFileAtomic = () => false; // the disk write fails
   expect(importSongFiles(be, store, sys, ["/s/good.lsdsng"])).toBe(false);
-  expect(be.constructCalls.length).toBe(before); // no cold-boot from a failed write
+  expect(be.buildCalls.length).toBe(before); // no cold-boot from a failed write
   expect(be.readFile("/roms/song.sav")).toBe(null); // nothing was written to disk
   // the lsdj-assets role config was never touched (no override written)
   expect(readOverrides(store.view().find((s) => s.id === id)?.roles.find((r) => r.kind === "lsdj-assets")?.config).length).toBe(0);
diff --git a/packages/retroplug/test/systems/lsdj-lsdprj.test.ts b/packages/retroplug/test/systems/lsdj-lsdprj.test.ts
index 742f451d..1881c3d0 100644
--- a/packages/retroplug/test/systems/lsdj-lsdprj.test.ts
+++ b/packages/retroplug/test/systems/lsdj-lsdprj.test.ts
@@ -72,7 +72,7 @@ test("a lsdprjKit override patches the effective ROM's kit at construct (via rel
   store.setRoleConfig(id, "lsdj-assets", { overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/p.lsdprj", lsdprjKit: 0 }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = LsdjRom.fromBytes(spec.romBytes!);
   expect(patched.kit(0).valid).toBeTruthy();
diff --git a/packages/retroplug/test/systems/lsdj-seed.test.ts b/packages/retroplug/test/systems/lsdj-seed.test.ts
index 5d4bafe5..af4e1cda 100644
--- a/packages/retroplug/test/systems/lsdj-seed.test.ts
+++ b/packages/retroplug/test/systems/lsdj-seed.test.ts
@@ -31,7 +31,7 @@ test("a fresh LSDj cart with no sav is seeded an empty sav at construct", () =>
   // The lsdj-sync role attached (LSDj identity), and its hook seeded sramBytes before the build.
   expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeTruthy();
   expect(be.log.includes("savFromJson")).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.sramBytes != null && spec.sramBytes.byteLength > 0).toBeTruthy();
 });
 
@@ -41,7 +41,7 @@ test("an LSDj cart with an existing .sav on disk is NOT re-seeded (never clobber
   be.seed("/roms/song.sav", "real battery"); // native will load this — the hook must stand down
   const id = store.addSystem("/roms/song.gb");
   expect(id).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.sramBytes).toBe(undefined);
   expect(be.log.includes("savFromJson")).toBeFalsy();
 });
@@ -52,7 +52,7 @@ test("a non-LSDj GB cart is never seeded (no lsdj-sync role → no hook)", () =>
   const id = store.addSystem("/roms/zelda.gb");
   expect(id).toBeTruthy();
   expect(store.view()[0].roles.map((r) => r.kind).includes("lsdj-sync")).toBeFalsy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.sramBytes).toBe(undefined);
 });
 
@@ -60,7 +60,7 @@ test("embedded mGB is never seeded (its role has no load-time hook)", () => {
   const { be, store } = newStore();
   const id = store.loadMgb();
   expect(id).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.sramBytes).toBe(undefined);
 });
 
@@ -70,7 +70,7 @@ test("duplicating an LSDj system seeds the clone from the savestate only (no bat
   const id = store.addSystem("/roms/song.gb")!;
   const dup = store.duplicateSystem(id);
   expect(dup).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   // A seeded battery would cold-reseed the clone and drop the restored state.
   expect(spec.stateBytes != null).toBeTruthy();
   expect(spec.sramBytes).toBe(undefined);
diff --git a/packages/retroplug/test/systems/lsdj-song-import.test.ts b/packages/retroplug/test/systems/lsdj-song-import.test.ts
index 90711895..f6e6040c 100644
--- a/packages/retroplug/test/systems/lsdj-song-import.test.ts
+++ b/packages/retroplug/test/systems/lsdj-song-import.test.ts
@@ -50,13 +50,13 @@ test("dropping a .lsdsng + a .lsdprj adds both songs in ONE reboot; kits ride ov
   be.seed("/s/a.lsdsng", encodeLsdsngRaw("AAA", 1, aSong));
   be.seed("/s/b.lsdprj", encodeLsdprj({ name: "BBB", version: 2, songBytes: bSong, kitBanks: [buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }])] }));
 
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
   const sys = store.view().find((s) => s.id === id)!;
   expect(importSongFiles(be, store, sys, ["/s/a.lsdsng", "/s/b.lsdprj"])).toBeTruthy();
 
   // Exactly one rebuild for the whole batch.
-  expect(be.constructCalls.length - before).toBe(1);
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  expect(be.buildCalls.length - before).toBe(1);
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   const sav = spec.sramBytes!;
   expect(listProjects(sav).map((p) => p.slot)).toEqual([0, 1]); // both songs present
   deepEqual([...decompressSlot(sav, 0)!], [...aSong], ".lsdsng byte-exact in slot 0");
@@ -74,9 +74,9 @@ test("importSongFiles is a no-op (no write) for an empty / all-bad list", () =>
   be.seed("/roms/song.gb", lsdjRom1M());
   const id = store.addSystem("/roms/song.gb")!;
   be.setSram(id, savFrom({}));
-  const before = be.constructCalls.length;
+  const before = be.buildCalls.length;
   const sys = store.view().find((s) => s.id === id)!;
   expect(importSongFiles(be, store, sys, [])).toBe(false);
   expect(importSongFiles(be, store, sys, ["/nope.lsdsng"])).toBe(false); // unreadable
-  expect(be.constructCalls.length).toBe(before); // never rebooted
+  expect(be.buildCalls.length).toBe(before); // never rebooted
 });
diff --git a/packages/retroplug/test/systems/lsdj-songs.test.ts b/packages/retroplug/test/systems/lsdj-songs.test.ts
index 7ab9e284..9aa57396 100644
--- a/packages/retroplug/test/systems/lsdj-songs.test.ts
+++ b/packages/retroplug/test/systems/lsdj-songs.test.ts
@@ -136,7 +136,7 @@ test("Delete via the store path hands native the edited SAV (song removed)", ()
 
   mutate(be, store, id, "/roms/song.gb", (sav) => deleteSongInSav(sav, 0));
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.sramBytes != null).toBeTruthy(); // rebuilt from the edited SRAM
   expect(decompressSlot(spec.sramBytes!, 0)).toBe(null); // AAA deleted
   expect(listProjects(spec.sramBytes!).map((p) => p.slot)).toEqual([3]); // BBB kept
@@ -151,7 +151,7 @@ test("Move (reorder) via the store path swaps the songs' slots end-to-end", () =
 
   mutate(be, store, id, "/roms/song.gb", (sav) => moveSongInSav(sav, 0, 1)); // Move Down on position 0
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(listProjects(spec.sramBytes!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:BBB", "3:AAA"]);
   expect(listProjects(be.readFile("/roms/song.sav")!).map((p) => `${p.slot}:${p.name}`)).toEqual(["0:BBB", "3:AAA"]); // + on disk
 });
@@ -165,7 +165,7 @@ test("Load via the store path boots from the loaded song; other slot untouched",
 
   mutate(be, store, id, "/roms/song.gb", (sav) => loadSongToWorking(sav, 3)); // load BBB
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   deepEqual([...spec.sramBytes!.subarray(0, 0x8000)], [...decompressSlot(seed, 3)!], "working memory = slot 3, byte-exact");
   expect(spec.sramBytes![0x8140]).toBe(3);
   deepEqual([...decompressSlot(spec.sramBytes!, 0)!], [...decompressSlot(seed, 0)!], "slot 0 untouched");
@@ -180,7 +180,7 @@ test("Add a .lsdsng via the store path lands it in the first free slot", () => {
   const file = encodeLsdsngRaw("ADD", 7, songWithSentinel(120, 0x44));
   mutate(be, store, id, "/roms/song.gb", (sav) => addLsdsngToSav(sav, file));
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(listProjects(spec.sramBytes!).map((p) => p.slot)).toEqual([0, 1, 3]); // slot 1 filled
   deepEqual([...decompressSlot(spec.sramBytes!, 1)!], [...songWithSentinel(120, 0x44)], "added song byte-exact");
   // Boots into the newly-added song (working memory + active project), not the old one.
diff --git a/packages/retroplug/test/systems/risa-assets.test.ts b/packages/retroplug/test/systems/risa-assets.test.ts
index 4fe2603c..9db6a786 100644
--- a/packages/retroplug/test/systems/risa-assets.test.ts
+++ b/packages/retroplug/test/systems/risa-assets.test.ts
@@ -37,7 +37,7 @@ test("a risa system attaches an empty risa-assets role and constructs with no ro
   const id = store.addSystem("/roms/song.nes");
   expect(id).toBeTruthy();
   expect(store.view()[0].roles.map((r) => r.kind).includes("risa-assets")).toBeTruthy();
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined); // no overrides → base ROM
 });
 
 test("a theme override is stored INLINE (no path) and applied to the effective ROM", () => {
@@ -50,7 +50,7 @@ test("a theme override is stored INLINE (no path) and applied to the effective R
   store.setRoleConfig(id, "risa-assets", { overrides: [override] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect((override as Record<string, unknown>).path).toBe(undefined); // inline — no file link
 
@@ -73,7 +73,7 @@ test("a font override LINKS a .chr file by path and applies it to the effective
   store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "font", slot: 1, name: "new", path: "/fonts/new.chr" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = RisaRom.fromBytes(spec.romBytes!);
   expect([...patched.getChrFontSlot(1)!]).toEqual([...bank]);
@@ -91,7 +91,7 @@ test("a wrong-size .chr font override is skipped (leaves the ROM unpatched)", ()
 
   // The one override was skipped, so applyOverridesToRom returns a clone equal to base — but romBytes is
   // still set (the role produced a fresh buffer). The font slot must remain the base pattern.
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   const slot0 = RisaRom.fromBytes(spec.romBytes ?? be.readFile("/roms/song.nes")!).getChrFontSlot(0)!;
   expect(slot0[0]).toBe(0); // base slot-0 pattern byte (s*13+0 = 0), not 0x00-filled by a bad override
 });
@@ -108,7 +108,7 @@ test("a kit override LINKS a .rkit bank by path and splices it (bank + mirror) i
   store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 5, name: "DRUMS", path: "/kits/drums.rkit" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   const patched = RisaRom.fromBytes(spec.romBytes!);
   expect([...patched.getKitBank(5)!]).toEqual([...bank]); // bank spliced verbatim
@@ -127,7 +127,7 @@ test("an erase kit override empties the base kit slot in the effective ROM", ()
   store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 0, name: "", erase: true }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romBytes != null).toBeTruthy();
   expect(RisaRom.fromBytes(spec.romBytes!).isKitPopulated(0)).toBe(false); // cleared, not linked
   expect([...be.readFile("/roms/song.nes")!]).toEqual([...base]);
@@ -143,7 +143,7 @@ test("a wrong-size .rkit kit override is skipped (leaves the base kit intact)",
   store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "kit", slot: 0, name: "bad", path: "/kits/bad.rkit" }] });
   store.reloadSystem(id);
 
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   const patched = RisaRom.fromBytes(spec.romBytes ?? be.readFile("/roms/song.nes")!);
   expect(patched.isKitPopulated(0)).toBe(true); // base kit intact, not zeroed by a bad override
 });
@@ -155,9 +155,9 @@ test("Remove Override reverts the next construct to the base ROM (no romBytes)",
 
   store.setRoleConfig(id, "risa-assets", { overrides: [{ type: "theme", slot: 0, name: "NEON", theme: THEME_NEON }] });
   const id2 = store.reloadSystem(id)!; // reload swaps the id in place
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes != null).toBeTruthy();
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes != null).toBeTruthy();
 
   store.setRoleConfig(id2, "risa-assets", { overrides: [] });
   store.reloadSystem(id2);
-  expect(be.constructCalls[be.constructCalls.length - 1].romBytes).toBe(undefined);
+  expect(be.buildCalls[be.buildCalls.length - 1].romBytes).toBe(undefined);
 });
diff --git a/packages/retroplug/test/systems/store-add.test.ts b/packages/retroplug/test/systems/store-add.test.ts
index f1ac5021..59be9f10 100644
--- a/packages/retroplug/test/systems/store-add.test.ts
+++ b/packages/retroplug/test/systems/store-add.test.ts
@@ -34,7 +34,7 @@ test("add: passes native a concrete resolved savPath (the suffix-0 sibling)", ()
   const { be, store } = newStore();
   be.seed("/roms/a.gb", gbRom());
   store.addSystem("/roms/a.gb");
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.romPath).toBe("/roms/a.gb");
   expect(spec.embeddedRom).toBe("");
   expect(spec.savPath).toBe("/roms/a.gb".replace(".gb", ".sav")); // /roms/a.sav
@@ -49,7 +49,7 @@ test("add: disambiguates the sav suffix against the live list (0, then 2, then 3
   store.addSystem("/roms/a.gb"); // 0,2 owned -> 3
   expect(store.view().map((v) => v.savSuffix)).toEqual([0, 2, 3]);
   // concrete savPaths the store handed native
-  expect(be.constructCalls.map((c) => c.savPath)).toEqual([
+  expect(be.buildCalls.map((c) => c.savPath)).toEqual([
     "/roms/a.sav",
     "/roms/a-2.sav",
     "/roms/a-3.sav",
@@ -72,7 +72,7 @@ test("add: an unknown or absent ROM is a no-op (rejected before construct)", ()
   expect(store.addSystem("/roms/missing.gb")).toBe(null); // not on disk at all
   expect(store.view().length).toBe(0);
   expect(changes()).toBe(0);
-  expect(be.log.includes("constructSystem")).toBeFalsy(); // never reached native
+  expect(be.log.includes("buildCore")).toBeFalsy(); // never reached native
 });
 
 test("focusNext: cycles focus forward/back through the instances in grid order, wrapping", () => {
diff --git a/packages/retroplug/test/systems/store-load.test.ts b/packages/retroplug/test/systems/store-load.test.ts
index 43fcab7e..928d1b6b 100644
--- a/packages/retroplug/test/systems/store-load.test.ts
+++ b/packages/retroplug/test/systems/store-load.test.ts
@@ -41,7 +41,7 @@ test("load: defers to a sibling <rom>.rplg and builds nothing", () => {
   const r = store.loadRom("/roms/a.gb");
   expect(r).toEqual({ deferredProject: "/roms/a.rplg" });
   expect(store.view().length).toBe(0);
-  expect(be.log.includes("constructSystem")).toBeFalsy();
+  expect(be.log.includes("buildCore")).toBeFalsy();
 });
 
 test("load: a paired save does NOT defer, and pins the different file as the savPath", () => {
@@ -50,7 +50,7 @@ test("load: a paired save does NOT defer, and pins the different file as the sav
   be.seed("/roms/a.rplg", "project bytes"); // present, but the paired save wins
   const r = store.loadRom("/roms/a.gb", { explicitSav: "/saves/mine.sav" });
   expect("system" in (r as object)).toBeTruthy();
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.savPath).toBe("/saves/mine.sav"); // a genuinely different file -> override
 });
 
@@ -58,7 +58,7 @@ test("load: a paired save equal to the natural sibling is NOT an override", () =
   const { be, store } = newStore();
   be.seed("/roms/a.gb", gbRom());
   store.loadRom("/roms/a.gb", { explicitSav: "/roms/a.sav" }); // == the suffix-0 sibling
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.savPath).toBe("/roms/a.sav"); // resolves to the sibling either way, no override pinned
   expect(store.view()[0].savSuffix).toBe(0);
 });
@@ -73,7 +73,7 @@ test("loadMgb: builds the embedded ROM (sameboy, no file, no header read)", () =
   expect(v[0].embedded).toBeTruthy();
   expect(v[0].romPath).toBe("");
   expect(v[0].missing).toBeFalsy(); // embedded is never missing
-  const spec = be.constructCalls[be.constructCalls.length - 1];
+  const spec = be.buildCalls[be.buildCalls.length - 1];
   expect(spec.embeddedRom).toBe("mgb");
   expect(spec.romPath).toBe("");
   expect(spec.savPath).toBe(null);
diff --git a/packages/retroplug/test/systems/store-mutate.test.ts b/packages/retroplug/test/systems/store-mutate.test.ts
index 1f37b191..f771c3bd 100644
--- a/packages/retroplug/test/systems/store-mutate.test.ts
+++ b/packages/retroplug/test/systems/store-mutate.test.ts
@@ -37,7 +37,7 @@ test("duplicate: appends a clone with a fresh suffix + concrete auto-save path",
   expect(store.focused()).toBe(a); // duplicate doesn't steal focus
   // Duplicate is TS orchestration now: readState(src) → constructSystem seeded with those bytes + the
   // clone's own auto-save path. No bespoke backend method — it lands as a construct call.
-  const call = be.constructCalls[be.constructCalls.length - 1];
+  const call = be.buildCalls[be.buildCalls.length - 1];
   expect(call.romPath).toBe("/roms/a.gb");
   expect(call.savPath).toBe("/roms/a-2.sav");
   expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(a as number));
@@ -48,7 +48,7 @@ test("addSystem adopts an existing <rom>.srm battery when there is no <rom>.sav
   be.seed("/roms/tune.nes", nesRom());
   be.seed("/roms/tune.srm", "battery"); // only a .srm sibling on disk, no .sav
   store.addSystem("/roms/tune.nes");
-  expect(be.constructCalls[be.constructCalls.length - 1].savPath).toBe("/roms/tune.srm"); // loaded + auto-saved there
+  expect(be.buildCalls[be.buildCalls.length - 1].savPath).toBe("/roms/tune.srm"); // loaded + auto-saved there
   expect(store.view()[0].savPath).toBe("/roms/tune.srm"); // recorded as an override (persisted in the project)
 });
 
@@ -58,12 +58,12 @@ test("addSystem prefers <rom>.sav over <rom>.srm, and derives .sav when neither
   be.seed("/roms/a.sav", "battery");
   be.seed("/roms/a.srm", "battery"); // both present → the .sav wins
   store.addSystem("/roms/a.nes");
-  expect(be.constructCalls[be.constructCalls.length - 1].savPath).toBe("/roms/a.sav");
+  expect(be.buildCalls[be.buildCalls.length - 1].savPath).toBe("/roms/a.sav");
   expect(store.view()[0].savPath).toBe(""); // derived, no override
 
   be.seed("/roms/b.nes", nesRom()); // neither .sav nor .srm
   store.addSystem("/roms/b.nes");
-  expect(be.constructCalls[be.constructCalls.length - 1].savPath).toBe("/roms/b.sav"); // the default write target
+  expect(be.buildCalls[be.buildCalls.length - 1].savPath).toBe("/roms/b.sav"); // the default write target
   expect(store.view()[1].savPath).toBe("");
 });
 
@@ -125,8 +125,8 @@ test("swapRom: changes the ROM in place carrying the live battery, reclassifying
   expect(v[0].platform).toBe("nes"); // reclassified — platform/core follow the new cart
   expect(v[0].savSuffix).toBe(0);
   expect(store.focused()).toBe(newId); // focus followed the swap
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   expect(call.romPath).toBe("/roms/b.nes");
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(a)); // the OLD system's live battery carried forward
   expect(call.stateBytes).toBe(undefined); // cold boot with the carried SRAM, no savestate
@@ -160,7 +160,7 @@ test("swapRom: an LSDj→LSDj swap carries the song forward — the seed-hook st
   const seedsBeforeSwap = be.log.filter((e) => e === "savFromJson").length;
   const newId = store.swapRom(a, "/roms/v2.gb");
   expect(newId).toBeTruthy();
-  const call = be.constructCalls[be.constructCalls.length - 1];
+  const call = be.buildCalls[be.buildCalls.length - 1];
   expect(call.romPath).toBe("/roms/v2.gb");
   // The carried live battery must survive: lsdjSeedSav sees spec.sramBytes set and stands down, so it does
   // NOT overwrite it with an empty savFromJson("{}") seed. This is the whole point of the feature.
@@ -198,8 +198,8 @@ test("loadState: reconstructs the system in place, booted from the file's bytes"
   expect(v.length).toBe(1); // swapped, not appended
   expect(v[0].id).toBe(newId);
   expect(store.focused()).toBe(newId); // focus followed the swap
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   expect(new Uint8Array(call.stateBytes!)).toEqual(stateBytesFor(999)); // the file's bytes seeded the core
   expect(store.loadState(9999, "/states/x.ss0")).toBe(null); // absent id -> no-op
   expect(store.loadState(newId as number, "/nope.ss0")).toBe(null); // unreadable file -> no-op
@@ -213,8 +213,8 @@ test("loadSram: cold-boots the ROM in place with the file's SRAM and repoints th
   const newId = store.loadSram(a, "/saves/x.sav");
   expect(newId).toBeTruthy();
   expect(newId === a).toBeFalsy();
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a);
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(999));
   expect(call.savPath).toBe("/saves/x.sav"); // native's auto-save target repointed to the loaded file
   expect(store.view()[0].savPath).toBe("/saves/x.sav"); // ...and recorded on the entry (persisted in the project)
@@ -242,8 +242,8 @@ test("reset: reboots in place carrying the live battery, with a new id + focus",
   expect(v[0].id).toBe(newId);
   expect(v[0].romPath).toBe("/roms/a.gb"); // identity preserved
   expect(store.focused()).toBe(newId); // focus followed the swap
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   expect(new Uint8Array(call.sramBytes!)).toEqual(sramBytesFor(a)); // the live battery carried forward
   expect(call.stateBytes).toBe(undefined); // cold boot, no savestate
   expect(store.reset(9999)).toBe(null); // absent -> no-op
@@ -258,8 +258,8 @@ test("newSram: cold-boots in place with a blank (all-zero) battery", () => {
   expect(newId === a).toBeFalsy();
   expect(store.view().length).toBe(1); // swapped, not appended
   expect(store.focused()).toBe(newId); // focus followed the swap
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a);
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   const seed = new Uint8Array(call.sramBytes!);
   expect(seed.length).toBe(0x20000); // native truncates/zero-pads this to the cart's real battery size
   expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
@@ -281,8 +281,8 @@ test("newSramAs: cold-boots blank, repoints the auto-save target to the picked f
   expect(v[0].savPath).toBe("/saves/fresh.sav"); // auto-save target repointed to the picked file
   expect(v[0].settings.gainDb).toBe(-6); // settings preserved across the rebuild
   expect(store.focused()).toBe(newId);
-  const call = be.constructCalls[be.constructCalls.length - 1];
-  expect(call.replaceId).toBe(a); // in-place replace
+  const call = be.buildCalls[be.buildCalls.length - 1];
+  expect(store.view().map((s) => s.id)).toEqual([call.id]); // built in the replaced system's slot
   const seed = new Uint8Array(call.sramBytes!);
   expect(seed.length).toBe(0x20000);
   expect(seed.every((b) => b === 0)).toBeTruthy(); // blank battery
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index ed098f57..34241bfc 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -84,8 +84,6 @@ export class MockBackend implements Backend {
   readonly buildCalls: (ConstructSpec & { id: number })[] = [];
   /** Every setGraph request, in order: the documents TS published, their slots and their tap re-arms. */
   readonly graphRequests: SetGraphRequest[] = [];
-  /** Cores buildCore built that no published document has placed yet. */
-  private parked = new Map<number, MockSystem>();
   // NOTE: duplicate/reload no longer hit the backend — they are TS orchestration over constructSystem,
   // so their effect shows up in constructCalls (with stateBytes/sramBytes + replaceId), not a dedicated log.
 
@@ -304,30 +302,41 @@ export class MockBackend implements Backend {
     return true;
   }
 
+  // A built core is live from the build on, as native's is (its snapshot slot is claimed at the build, so its
+  // reads answer before any document places it).
   buildCore(spec: ConstructSpec, id: number): BuiltCore | null {
     this.log.push("buildCore");
     this.buildCalls.push({ ...spec, id });
-    if (!this.buildable(spec) || this.systems.has(id) || this.parked.has(id)) return null;
-    this.parked.set(id, mockSystem(spec));
+    if (!this.buildable(spec) || this.systems.has(id)) return null;
+    this.systems.set(id, mockSystem(spec));
     return { type: nodeTypeFor(spec), exportMode: settingsNumber(spec, "channelExportMode"), linkGroup: settingsNumber(spec, "linkGroupId") };
   }
 
+  // The slots become the live systems: every core the document does not place goes, whether it ran in the
+  // last document or was never placed, as native frees them.
   setGraph(request: SetGraphRequest): SetGraphResult {
     this.log.push("setGraph");
     this.graphRequests.push(request);
-    // The slots become the live systems: a parked core is placed, a live one the document drops goes, and a
-    // core still parked once the document is published is freed, as native does.
     const live = new Map<number, MockSystem>();
     for (const id of request.slots) {
-      const sys = this.systems.get(id) ?? this.parked.get(id);
+      const sys = this.systems.get(id);
       if (!sys) return { ok: false, errors: [{ nodeId: `sys-${id}`, port: "", code: "unknown-core", message: `no core is built under id ${id}` }] };
       live.set(id, sys);
     }
     this.systems = live;
-    this.parked.clear();
     return { ok: true, errors: [] };
   }
 
+  /** The system the build `call` displaced from its slot: the id that held that slot in the document before
+   *  the first one placing `call.id`. Undefined for a build that was appended, or never placed. */
+  replacedBy(call: { id: number }): number | undefined {
+    const at = this.graphRequests.findIndex((r) => r.slots.includes(call.id));
+    if (at <= 0) return undefined;
+    const slot = this.graphRequests[at].slots.indexOf(call.id);
+    const before = this.graphRequests[at - 1].slots[slot];
+    return before !== undefined && !this.graphRequests[at].slots.includes(before) ? before : undefined;
+  }
+
   // Build only when a real ROM is available: an embedded marker, TS-supplied effective ROM bytes (romBytes
   // wins, as native does), or the on-disk romPath, classified to a known format.
   //
````

- [ ] **Step 7: The native-tier test files**

````diff
diff --git a/packages/retroplug/test-native/app-audio-routing.test.ts b/packages/retroplug/test-native/app-audio-routing.test.ts
index 9d544dfa..7c2fd182 100644
--- a/packages/retroplug/test-native/app-audio-routing.test.ts
+++ b/packages/retroplug/test-native/app-audio-routing.test.ts
@@ -1,10 +1,9 @@
-// The audio-routing seam: the project's `audioRouting` mode now reaches native (the translator rewires
-// the graph's outputs), through the same invoker as setBpm/setTransport. This proves the seam is
-// wired end-to-end - the RPC is accepted for the valid modes, rejected out of range, and
-// playback survives a mode switch (a single system still lands on its pair). Per-pair SEPARATION
-// needs the plugin's 8 outputs + a multi-out capture and is the user's / a DAW fixture's check;
-// the test-host render is stereo, so every mode collapses to one pair here. (One test per file:
-// whole-mix RMS needs an isolated native engine.)
+// The audio-routing seam: the project's `audioRouting` mode reaches native audio as the output wiring of
+// the graph the project publishes (graph/buildGraph.ts). This proves the seam is wired end-to-end - every
+// mode plans, an unknown one is refused, and playback survives a mode switch (a single system still lands
+// on its pair). Per-pair SEPARATION is buildGraph's wiring tables and the parity routing cases; the
+// test-host render is stereo, so every mode collapses to one pair here. (One test per file: whole-mix RMS
+// needs an isolated native engine.)
 import { test, expect } from "../testing/harness";
 import { createRealBackend } from "../src/realBackend";
 import { createDspRuntime } from "../src/dspRuntime";
@@ -39,18 +38,12 @@ test("audioRouting reaches native and playback survives a mode switch", () => {
   const id = project.systems.loadMgb()!;
   expect(typeof id).toBe("number");
 
-  // The seam: valid modes accepted (Stereo / TwoPerInstance / OnePerInstance / ChannelSplit / PinSplit /
-  // StereoPinSplit), out of range rejected. (Per-lane SEPARATION for the split modes is a native-Catch2
-  // render check — this test-host capture is stereo, so it can only prove acceptance here; see
-  // Translator.test.cpp and NesSplitRouting.test.cpp.)
-  expect(be.setAudioRouting(0)).toBeTruthy();
-  expect(be.setAudioRouting(1)).toBeTruthy();
-  expect(be.setAudioRouting(2)).toBeTruthy();
-  expect(be.setAudioRouting(3)).toBeTruthy(); // ChannelSplit (1 GB → 8 outs) — now accepted
-  expect(be.setAudioRouting(4)).toBeTruthy(); // PinSplit (1 NES → 3 mono outs); inert on this GB
-  expect(be.setAudioRouting(5)).toBeTruthy(); // StereoPinSplit (1 NES → 3 pairs); likewise inert here
-  expect(be.setAudioRouting(6)).toBeFalsy(); // > StereoPinSplit → rejected
-  // Through the store (the real UI path — the menu cycler calls this), which pushes to native.
+  // The seam, through the store (the real UI path: the menu cycler calls this): every mode plans, and an
+  // unknown one is refused before it reaches native. ChannelSplit on this GB fans its stems, which the
+  // stereo host is too narrow for, so it falls back to Stereo; the pin splits are inert on a GB.
+  for (const mode of ["stereo", "twoPerInstance", "onePerInstance", "channelSplit", "pinSplit", "stereoPinSplit"] as const)
+    expect(project.setAudioRouting(mode)).toBeTruthy();
+  expect(project.setAudioRouting("bogus" as never)).toBeFalsy();
   expect(project.setAudioRouting("twoPerInstance")).toBeTruthy();
 
   // Playback survives the switch: under TwoPerInstance the single system routes to its own pair
diff --git a/packages/retroplug/test-native/app-play-mgb-channels.test.ts b/packages/retroplug/test-native/app-play-mgb-channels.test.ts
index 36896e17..1171c392 100644
--- a/packages/retroplug/test-native/app-play-mgb-channels.test.ts
+++ b/packages/retroplug/test-native/app-play-mgb-channels.test.ts
@@ -33,7 +33,7 @@ test("mGB renders as four isolated Game Boy channels (renderAudioPerChannel)", (
   const recent = new RecentStore(be);
   const project = new ProjectStore(be, recent, registry);
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
 
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
diff --git a/packages/retroplug/test-native/app-play-nes-channels.test.ts b/packages/retroplug/test-native/app-play-nes-channels.test.ts
index d2e3ddaf..a5c83cd6 100644
--- a/packages/retroplug/test-native/app-play-nes-channels.test.ts
+++ b/packages/retroplug/test-native/app-play-nes-channels.test.ts
@@ -41,7 +41,7 @@ test("NES renders its stereo-mod pins (renderAudioPerChannel): Pulse rings, TND/
 
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
 
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
diff --git a/packages/retroplug/test-native/app-play-nes-mono.test.ts b/packages/retroplug/test-native/app-play-nes-mono.test.ts
index 4943ec94..bfb26123 100644
--- a/packages/retroplug/test-native/app-play-nes-mono.test.ts
+++ b/packages/retroplug/test-native/app-play-nes-mono.test.ts
@@ -41,7 +41,7 @@ test("NES renders its 5 core channels individually (renderAudioPerChannel mode 3
 
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
 
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
diff --git a/packages/retroplug/test-native/audio-levels-gb.test.ts b/packages/retroplug/test-native/audio-levels-gb.test.ts
index 7f87e445..9ac00181 100644
--- a/packages/retroplug/test-native/audio-levels-gb.test.ts
+++ b/packages/retroplug/test-native/audio-levels-gb.test.ts
@@ -20,7 +20,7 @@ test("audio levels: SameBoy (mGB)", () => {
   const be = createRealBackend();
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
   dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
   const id = project.systems.loadMgb()!;
diff --git a/packages/retroplug/test-native/audio-levels-nes-pins.test.ts b/packages/retroplug/test-native/audio-levels-nes-pins.test.ts
index 52dc4f20..6164e2ac 100644
--- a/packages/retroplug/test-native/audio-levels-nes-pins.test.ts
+++ b/packages/retroplug/test-native/audio-levels-nes-pins.test.ts
@@ -22,7 +22,7 @@ test("audio levels: Mesen NES 2A03 pins (n8-midi, stereoModPins)", () => {
   if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
   dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
   project.systems.adopt({
diff --git a/packages/retroplug/test-native/audio-levels-nes.test.ts b/packages/retroplug/test-native/audio-levels-nes.test.ts
index 708b65a4..528843ff 100644
--- a/packages/retroplug/test-native/audio-levels-nes.test.ts
+++ b/packages/retroplug/test-native/audio-levels-nes.test.ts
@@ -24,7 +24,7 @@ test("audio levels: Mesen NES mix + core channels (n8-midi)", () => {
   if (!be.fileExists(NES)) skip(`no ROM at ${NES}`);
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
   dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
   project.systems.adopt({
diff --git a/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts b/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
index 33a6e35d..52679cc4 100644
--- a/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
+++ b/packages/retroplug/test-native/dsp-risa-sync-locate.test.ts
@@ -98,6 +98,7 @@ test("a stop, a rewind and a mid-song jump each land risa on the row the arm nam
     console.log(`[risa-sync-locate] ppq=${ppq} want=${JSON.stringify(want(ppq))} got=${JSON.stringify(got)}`);
     expect(got).toEqual(want(ppq));
   }
+  graph.remove(id); // a live system is emulated by every later test in this file
 });
 
 test("the beat grid still holds on a run that follows a stop and rewind", () => {
@@ -161,4 +162,5 @@ test("the beat grid still holds on a run that follows a stop and rewind", () =>
   for (let i = 1; i < Math.min(first.length, second.length); i++) {
     expect(Math.abs(second[i] - first[i]) < 18).toBeTruthy();
   }
+  graph.remove(id); // a live system is emulated by every later test in this file
 });
diff --git a/packages/retroplug/test-native/dsp-risa-sync.test.ts b/packages/retroplug/test-native/dsp-risa-sync.test.ts
index 50cf1173..4a49c37d 100644
--- a/packages/retroplug/test-native/dsp-risa-sync.test.ts
+++ b/packages/retroplug/test-native/dsp-risa-sync.test.ts
@@ -73,6 +73,7 @@ test("the risa-sync role in the DSP kernel is the sole clock that makes risa 2.3
   expect(neg < 0.001).toBeTruthy(); // transport running but unclocked -> silent
   expect(pos > 0.001).toBeTruthy(); // the kernel's byte stream advances risa -> audible
   expect(pos > neg).toBeTruthy();
+  graph.remove(id); // a live system is emulated by every later test in this file
 });
 
 test("stopping the transport stops risa, and restarting it plays again", () => {
@@ -106,4 +107,5 @@ test("stopping the transport stops risa, and restarting it plays again", () => {
   expect(play > 0.001).toBeTruthy();
   expect(stopped < 0.001).toBeTruthy(); // FC really stopped it, rather than just muting the host
   expect(again > 0.001).toBeTruthy(); // and the re-arm brought it back
+  graph.remove(id); // a live system is emulated by every later test in this file
 });
diff --git a/packages/retroplug/test-native/dsp-sms-sync.test.ts b/packages/retroplug/test-native/dsp-sms-sync.test.ts
index 7bcc1c63..94c5fb97 100644
--- a/packages/retroplug/test-native/dsp-sms-sync.test.ts
+++ b/packages/retroplug/test-native/dsp-sms-sync.test.ts
@@ -165,7 +165,7 @@ for (const m of MACHINES) {
     expect(idle < 0.001).toBeTruthy(); // ...and makes no sound
     expect(playing > 0.001).toBeTruthy(); // the ROM's own clock plays it
     expect(playRows > 0).toBeTruthy(); // and its sequencer runs
-    expect(be2.removeSystem(id)).toBeTruthy(); // leave the mix silent for the next case
+    expect(graph.remove(id)).toBeTruthy(); // leave the mix silent for the next case
   });
 
   test(`${m.machine.toUpperCase()}: GUARD B: the sms-sync role in the DSP kernel is the sole clock that makes smsggdj play`, () => {
diff --git a/packages/retroplug/test-native/lsdj-sync-negative.test.ts b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
index a54ca5af..27207270 100644
--- a/packages/retroplug/test-native/lsdj-sync-negative.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-negative.test.ts
@@ -43,7 +43,7 @@ test("LSDj link-cable sync control: the follower stays silent without SYNC=LSDJ"
   const be = createRealBackend();
   if (!be.fileExists(LSDJ)) skip(`lsdj-sync-negative: LSDj ROM not found at ${LSDJ}`);
 
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(graph.graph);
 
   const leader = 1, follower = 2;
   expect(construct(leader, "None")).toBeTruthy();
diff --git a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
index a15e6b74..ecb6d5be 100644
--- a/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
+++ b/packages/retroplug/test-native/lsdj-sync-pattern.test.ts
@@ -47,7 +47,7 @@ test("LSDj link-cable sync: the follower plays in lockstep with the leader (per-
   const be = createRealBackend();
   if (!be.fileExists(LSDJ)) skip(`lsdj-sync-pattern: LSDj ROM not found at ${LSDJ}`);
 
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(graph.graph);
 
   const leader = 1, follower = 2;
   expect(construct(leader, "Lsdj")).toBeTruthy();
diff --git a/packages/retroplug/test-native/nes-capture-mix-drain.test.ts b/packages/retroplug/test-native/nes-capture-mix-drain.test.ts
index 80d21b44..c9543cbe 100644
--- a/packages/retroplug/test-native/nes-capture-mix-drain.test.ts
+++ b/packages/retroplug/test-native/nes-capture-mix-drain.test.ts
@@ -36,7 +36,7 @@ test("NES per-channel capture survives a MIX-path drain (renderAudio) then plays
 
   const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
   const dsp = createDspRuntime();
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(project.graph);
   expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
   project.setOnSystemsChange(() => syncDspFromStore(project, dsp));
 
diff --git a/packages/retroplug/test-native/parity/render-blocks.test.ts b/packages/retroplug/test-native/parity/render-blocks.test.ts
index d916186c..8408565e 100644
--- a/packages/retroplug/test-native/parity/render-blocks.test.ts
+++ b/packages/retroplug/test-native/parity/render-blocks.test.ts
@@ -5,14 +5,14 @@ import { test, expect, skip } from "../../testing/harness";
 import { bootSession } from "../../src/bootSession";
 import { savFrom, type SavInput } from "../../src/lsdjSav";
 import type { RenderBlock } from "../../src/audioDriver";
-import { directGraph } from "../../src/graph/directGraph";
-
-// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
-const graph = directGraph();
+import { DirectGraph } from "../../src/graph/directGraph";
 
 declare const __RESOURCES_DIR__: string;
 
 const session = bootSession();
+// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4), through
+// the session's own graph: the stores publish to it too, so each sees the documents the other sent.
+const graph = new DirectGraph(session.backend, session.project.graph);
 const { project, audio, backend: be, dsp } = session;
 
 const CHORD = [
diff --git a/packages/retroplug/test-native/render-guard.test.ts b/packages/retroplug/test-native/render-guard.test.ts
index 97103948..ef5f491b 100644
--- a/packages/retroplug/test-native/render-guard.test.ts
+++ b/packages/retroplug/test-native/render-guard.test.ts
@@ -12,7 +12,7 @@ const graph = directGraph();
 const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };
 
 test("renderAudio, renderAudioPerSystem and renderAudioPerChannel refuse while the audio thread runs", () => {
-  const audio = createAudioDriver();
+  const audio = createAudioDriver(graph.graph);
   expect(graph.add(MGB, 1)).toBeTruthy();
 
   expect(audio.startAudio()).toBeTruthy();
````

- [ ] **Step 8: What each store edit publishes**

The mock records every `setGraph` request, so the publishes are tested directly: one per edit, the re-arms,
a load's clear then one placement, serial capture in the build spec, a bare `SystemsStore`, and `DirectGraph`'s
model. Create `packages/retroplug/test/systems/store-graph.test.ts`:

````ts
// The graph the stores publish (docs/modular-graph-phase-b2-design.md section 3): one setGraph per edit that
// changes what runs, re-arming the NES tap where the edit builds or drops a core or changes the routing; a
// project load publishing its clear, then placing every system at once; the build spec carrying serial-out
// capture; and DirectGraph keeping the B1 translator's model for callers with no store.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { SystemsStore } from "../../src/systemsStore";
import { DirectGraph } from "../../src/graph/directGraph";
import { buildAppRegistry } from "../../src/appHost";
import { LsdjSyncMode } from "../../src/settingsEnums";
import type { ConstructSpec } from "../../src/backend";
import { gbRom, lsdjRom, nesRom } from "./fixtures";

function project() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.gb", gbRom());
  be.seed("/roms/c.nes", nesRom());
  be.seed("/roms/lsdj.gb", lsdjRom());
  const p = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  return { be, p, sys: p.systems };
}

// The requests sent since `from`: their slots, and their tap re-arm when they carry one.
const since = (be: MockBackend, from: number) => be.graphRequests.slice(from).map((r) => ({ slots: r.slots, tap: r.tap }));
const linked = (be: MockBackend, to: string): boolean =>
  (JSON.parse(be.graphRequests[be.graphRequests.length - 1].document) as { wires: { to: string }[] }).wires.some((w) => w.to === to);

test("adding or removing a system publishes once, re-arming the tap", () => {
  const { be, sys } = project();
  expect(be.graphRequests.length).toBe(0); // composing a project sends nothing
  const a = sys.addSystem("/roms/a.gb")!;
  expect(since(be, 0)).toEqual([{ slots: [a], tap: {} }]);
  const b = sys.addSystem("/roms/b.gb")!;
  expect(since(be, 1)).toEqual([{ slots: [a, b], tap: {} }]);
  sys.removeSystem(a);
  expect(since(be, 2)).toEqual([{ slots: [b], tap: {} }]);
});

test("a replace takes the slot of the system it replaces", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const b = sys.addSystem("/roms/b.gb")!;
  const n = be.graphRequests.length;
  const c = sys.replaceSystem(a, "/roms/c.nes")!;
  expect(since(be, n)).toEqual([{ slots: [c, b], tap: {} }]);
  expect(be.replacedBy(be.buildCalls[be.buildCalls.length - 1])).toBe(a);
  // The replacing NES was built in the default (mix) mode, and the graph places it as a NES.
  expect(sys.systems()[0].built).toEqual({ type: "rp.mesen.nes", exportMode: 0, linkGroup: 0 });
});

test("a live setting publishes nothing; a link-group change rewires without a re-arm", () => {
  const { be, sys } = project();
  const a = sys.addSystem("/roms/a.gb")!;
  const b = sys.addSystem("/roms/b.gb")!;
  const n = be.graphRequests.length;
  sys.setGain(a, -6);
  sys.setRoleConfig(a, "sameboy", { highpass: "removeDcOffset" });
  expect(since(be, n)).toEqual([]);
  sys.setRoleConfig(a, "sameboy", { linkGroupId: 1 }); // a group of one is no cable yet: nothing changes
  expect(since(be, n)).toEqual([]);
  sys.setRoleConfig(b, "sameboy", { linkGroupId: 1 });
  expect(since(be, n)).toEqual([{ slots: [a, b] }]);
  expect(linked(be, `sys-${b}.link`)).toBe(true);
});

test("a routing change re-arms the tap, even to the mode already set", () => {
  const { be, p, sys } = project();
  const c = sys.addSystem("/roms/c.nes")!;
  const n = be.graphRequests.length;
  p.setAudioRouting("stereo"); // the default: the document is unchanged, and it is sent for the re-arm
  p.setAudioRouting("pinSplit");
  expect(since(be, n)).toEqual([
    { slots: [c], tap: {} },
    { slots: [c], tap: { arm: { id: c, mode: 1 } } },
  ]);
});

test("a new project publishes its clear, then its routing", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const n = be.graphRequests.length;
  p.newProject();
  expect(since(be, n)).toEqual([
    { slots: [], tap: {} },
    { slots: [], tap: {} },
  ]);
});

test("a load publishes its clear, then places every system once", () => {
  const { be, p, sys } = project();
  sys.addSystem("/roms/a.gb");
  sys.addSystem("/roms/b.gb");
  const bytes = p.exportBytes()!;
  const n = be.graphRequests.length;
  expect(p.loadBytes(bytes, "/").kind).toBe("loaded");
  const sent = since(be, n);
  expect(sent.length).toBe(2);
  expect(sent[0]).toEqual({ slots: [], tap: {} });
  expect(sent[1].slots).toEqual(sys.systems().map((e) => e.id));
  expect(sent[1].slots.length).toBe(2);
  expect(sent[1].tap).toEqual({});
});

test("an adopt outside a load places its core at once", () => {
  const { be, sys } = project();
  const id = sys.adopt({ romPath: "/roms/a.gb" })!;
  expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
});

test("a system in an LSDj MIDI-out mode is built capturing its serial out", () => {
  const { be, sys } = project();
  const id = sys.addSystem("/roms/lsdj.gb")!;
  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(undefined); // midiSync by default
  sys.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiOut });
  expect(be.serialOutCaptureCalls[be.serialOutCaptureCalls.length - 1]).toEqual({ id, on: true }); // live
  sys.reset(id); // a rebuild: the new core is armed at the build
  expect(be.buildCalls[be.buildCalls.length - 1].serialOutCapture).toBe(true);
});

test("a systems store on its own publishes its systems at the stereo routing", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const id = new SystemsStore(be).addSystem("/roms/a.gb")!;
  expect(since(be, 0)).toEqual([{ slots: [id], tap: {} }]);
});

const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };

test("DirectGraph keeps the B1 model: append, replace in place, remove shifting down", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  expect(g.add(MGB, 1) && g.add(MGB, 2) && g.add(MGB, 3)).toBe(true);
  expect(g.replace(1, MGB, 4)).toBe(true);
  expect(g.ids()).toEqual([4, 2, 3]);
  expect(g.remove(2)).toBe(true);
  expect(g.ids()).toEqual([4, 3]);
  expect(since(be, 0).map((r) => r.slots)).toEqual([[1], [1, 2], [1, 2, 3], [4, 2, 3], [4, 3]]);
  // A remove or replace of an id already gone re-arms and changes nothing else.
  const n = be.graphRequests.length;
  expect(g.remove(9)).toBe(true);
  expect(g.replace(9, MGB, 5)).toBe(false);
  expect(since(be, n)).toEqual([
    { slots: [4, 3], tap: {} },
    { slots: [4, 3], tap: {} },
  ]);
  expect(be.buildCalls.some((c) => c.id === 5)).toBe(false);
});

test("DirectGraph rewires a Game Boy's link group as the core is told it", () => {
  const be = new MockBackend("/cfg");
  const g = new DirectGraph(be);
  g.add(MGB, 1);
  g.add(MGB, 2);
  const n = be.graphRequests.length;
  g.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
  g.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });
  expect(be.applyRoleCalls.length).toBe(2);
  expect(since(be, n)).toEqual([{ slots: [1, 2] }]);
  expect(linked(be, "sys-2.link")).toBe(true);
  // A config with no link group decodes natively as group 0, so the cable goes with it.
  g.applyRoleConfig(2, "sameboy", { model: 1 });
  expect(linked(be, "sys-2.link")).toBe(false);
});
````

Run: `pnpm test systems/store-graph`
Expected: `11 passed, 0 skipped, 0 failed`. Two of them guard the parts most easily lost: drop `holdGraph`'s
early return in `ProjectStore.publishGraph` and case 6 fails (`expected 2, got 4`); drop the
`serialOutCapture` line in `applyConstructHooks` and case 8 fails (`expected true, got undefined`).

- [ ] **Step 9: Build and verify everything**

Run: `cmake --build build -j$(nproc) && pnpm typecheck`
Expected: a clean build (only the existing `PluginDSP.cpp:167` format-truncation warning), and
`packages/retroplug typecheck: Done`.

Run: `pnpm test`
Expected: `# 158 test file(s) passed`.

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 24 native test file(s) passed`: bit-identical through the flip.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 148 native test file(s) passed`, with only the two bench files skipping.

Run: `pnpm test:plugin && pnpm test:ui && pnpm test:node && pnpm sdl:smoke`
Expected: `# 10 plugin test binary(ies) passed`, `# 37 UI test file(s) passed`, `ℹ pass 12` / `ℹ fail 0`, and
`SDL smoke OK`.

- [ ] **Step 10: Commit**

```bash
git add -A packages/
git commit -m "feat(graph): the flip - the stores publish the engine's graph, TS owns the host shape"
```

---
### Task 6: Delete what TS no longer sends

The translator and its tests, the invoker's structural half (`adoptSystem`, `replaceSystem`, `removeSystem`,
`setAudioRouting`, `setHostShape`, the link-group interception), `Engine::translator()` and `pushSyncTap`, the
native `AudioRouting` and `HostShape` types, the `constructSystem` / `removeSystem` / `setAudioRouting` RPCs
with their `Backend`, real and mock entries, `replaceId`, and `audioRoutingToIndex`. The engine publishes its
own empty first graph (the kernel and a stereo pair), as the translator's constructor did. The native tests
that drove structure through the invoker are ported onto documents spelled the way `buildGraph` writes them
(`GraphDocs.hpp`): `NesSplitRouting` (one request per routing step, the tap arm in the request) and
`EngineSampleRate`.

**Files:**
- Delete: `packages/native/src/host/graph/Translator.cpp`, `packages/native/src/host/graph/Translator.hpp`, `packages/native/test/engine-graph/Translator.test.cpp`, `packages/native/test/engine-graph/TranslatorGoldens.test.cpp`, `packages/native/test/engine-graph/TranslatorPlan.test.cpp`
- Create: `packages/native/test/engine-graph/GraphDocs.hpp`
- Modify: 25 files (the diff below)

- [ ] **Step 1: The shared test documents**

Create `packages/native/test/engine-graph/GraphDocs.hpp`:

````cpp
#pragma once

// Graph documents for native tests, spelled the way packages/retroplug/src/graph/buildGraph.ts writes them
// (node ids, document order, buses), so a test drives the Engine with what TS would send. Only the shapes the
// tests need: systems summed to a stereo pair, each system on its own pair, and a lone NES split.

#include <cstdint>
#include <string>
#include <vector>

#include "host/engine/Engine.hpp"
#include "system/SystemTypes.hpp"

namespace rp::host::test {

struct DocSystem {
    SystemId    id;
    const char* type;  // rp.sameboy.gb / rp.mesen.nes / ...
};

inline const char* const kGbType = "rp.sameboy.gb";
inline const char* const kNesType = "rp.mesen.nes";

namespace docs_detail {

inline std::string node(const std::string& id, const std::string& type, const std::string& settings = "") {
    return R"({"id":")" + id + R"(","type":")" + type + "\"" + (settings.empty() ? "" : R"(,"settings":)" + settings) + "}";
}
inline std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}
inline std::string join(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) s += (i ? "," : "") + items[i];
    return s;
}
inline std::string out(std::uint32_t first, std::uint32_t n) {
    return "out.out." + std::to_string(first + 1) + (n == 1 ? "" : "-" + std::to_string(first + n));
}
inline std::string buses(const std::vector<std::uint32_t>& b) {
    std::string s = R"({"buses":[)";
    for (std::size_t i = 0; i < b.size(); ++i) s += (i ? "," : "") + std::to_string(b[i]);
    return s + "]}";
}
inline std::string document(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    return R"({"nodes":[)" + join(nodes) + R"(],"wires":[)" + join(wires) + "]}";
}
inline std::string sys(const DocSystem& s) {
    return node("sys-" + std::to_string(s.id), s.type, R"({"core":)" + std::to_string(s.id) + "}");
}

}  // namespace docs_detail

// The Stereo routing at a `channels`-wide host: every system's L summed into channel 0, its R into 1.
inline std::string stereoDocument(const std::vector<DocSystem>& systems, std::uint32_t channels = 2) {
    using namespace docs_detail;
    std::vector<std::string> nodes{node("kernel", "rp.dsp.kernel")}, wires;
    for (const DocSystem& s : systems) nodes.push_back(sys(s));
    for (const DocSystem& s : systems) {
        const std::string split = "split-" + std::to_string(s.id);
        nodes.push_back(node(split, "rp.core.split"));
        wires.push_back(wire("sys-" + std::to_string(s.id) + ".mix", split + ".in"));
    }
    for (std::uint32_t c = 0; c < 2 && !systems.empty(); ++c) {
        const std::string half = c ? ".out2" : ".out1";
        if (systems.size() == 1) {
            wires.push_back(wire("split-" + std::to_string(systems[0].id) + half, out(c, 1)));
            continue;
        }
        const std::string lane = "lane-" + std::to_string(c);
        nodes.push_back(node(lane, "rp.core.add", R"({"inputs":)" + std::to_string(systems.size()) + "}"));
        for (std::size_t k = 0; k < systems.size(); ++k)
            wires.push_back(wire("split-" + std::to_string(systems[k].id) + half, lane + ".in" + std::to_string(k + 1)));
        wires.push_back(wire(lane + ".out", out(c, 1)));
    }
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(channels, 1))));
    return document(nodes, wires);
}

// The perSystem shape: slot i's mix on host channels 2i and 2i + 1.
inline std::string perSystemDocument(const std::vector<DocSystem>& systems) {
    using namespace docs_detail;
    std::vector<std::string> nodes{node("kernel", "rp.dsp.kernel")}, wires;
    for (std::size_t i = 0; i < systems.size(); ++i) {
        nodes.push_back(sys(systems[i]));
        wires.push_back(wire("sys-" + std::to_string(systems[i].id) + ".mix", out(static_cast<std::uint32_t>(2 * i), 2)));
    }
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(systems.size(), 2))));
    return document(nodes, wires);
}

// A lone NES split across an 8-channel host: its pins one per channel ("pins"), each pin on its own pair
// ("stereoPins"), or its five channels one per channel ("channels").
inline std::string nesSplitDocument(SystemId id, const std::string& split) {
    using namespace docs_detail;
    const std::string n = "sys-" + std::to_string(id);
    std::vector<std::string> nodes{node("kernel", "rp.dsp.kernel"), sys({id, kNesType})}, wires;
    std::vector<std::string> streams = split == "channels"
                                           ? std::vector<std::string>{"channels.sq1", "channels.sq2", "channels.tri", "channels.noise", "channels.dmc"}
                                           : std::vector<std::string>{"pins.pulse", "pins.tnd", "pins.expansion"};
    const std::uint32_t stride = split == "stereoPins" ? 2 : 1;
    for (std::uint32_t k = 0; k < streams.size(); ++k)
        for (std::uint32_t l = 0; l < stride; ++l) wires.push_back(wire(n + "." + streams[k], out(stride * k + l, 1)));
    nodes.push_back(node("out", "rp.host.audio-out", buses(std::vector<std::uint32_t>(8, 1))));
    return document(nodes, wires);
}

// A request for `document` placing `slots`. With `armMode` it re-arms the NES tap, arming `armId` to it
// (0 restores only).
inline Engine::GraphRequest graphRequest(std::string document, std::vector<SystemId> slots) {
    Engine::GraphRequest r;
    r.document = std::move(document);
    r.slots = std::move(slots);
    return r;
}
inline Engine::GraphRequest rearming(Engine::GraphRequest r, SystemId armId = 0, std::uint32_t armMode = 0) {
    r.rearmTap = true;
    r.arm = armMode != 0;
    r.armId = armId;
    r.armMode = armMode;
    return r;
}

}  // namespace rp::host::test
````

- [ ] **Step 2: Delete the translator and its tests**

Run: `git rm -q packages/native/src/host/graph/Translator.cpp packages/native/src/host/graph/Translator.hpp packages/native/test/engine-graph/Translator.test.cpp packages/native/test/engine-graph/TranslatorGoldens.test.cpp packages/native/test/engine-graph/TranslatorPlan.test.cpp`

- [ ] **Step 3: Everything that referred to them**

````diff
diff --git a/packages/native/CMakeLists.txt b/packages/native/CMakeLists.txt
index faa96fc8..b64dfe2a 100644
--- a/packages/native/CMakeLists.txt
+++ b/packages/native/CMakeLists.txt
@@ -141,7 +141,6 @@ add_library(retroplug-backend STATIC
     src/host/engine/EngineInvoker.cpp       # QueuedInvoker - the one mutation path to the Engine
     src/host/graph/SystemNodes.cpp          # the cores as rp.sameboy.gb / rp.mesen.* nodes
     src/host/graph/KernelNode.cpp           # the kernel stage as rp.dsp.kernel
-    src/host/graph/Translator.cpp           # the structural calls as graph documents
     src/system/SystemFactory.cpp            # backend-agnostic build registry (the one build path)
     src/system/CoreBackends.cpp             # registerCoreBackends: mounts sameboy + mesen on a factory
     src/system/sameboy/SameBoyBackend.cpp   # SameBoy builder — the only place SameBoyConfig is constructed
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 8e982f6f..9c784b1f 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -74,23 +74,20 @@ retroplug_add_test_binary(
              RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
              RP_GG_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.gg")
 
-# Guards B1's graph engine (docs/modular-graph-phase-b1-design.md): each system node renders exactly what
-# its core renders driven directly, and the translator builds the routing modes as wiring. Every core in it
-# powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two identical cores agree.
+# Guards the engine's graph (docs/modular-graph-phase-b1-design.md, -b2-design.md): each system node renders
+# exactly what its core renders driven directly, and the engine places, re-arms and frees what the documents
+# TS builds say. Every core in it powers on in a fixed state (test/engine-graph/Deterministic.cpp), so two
+# identical cores agree.
 retroplug_add_test_binary(
     NAME     retroplug-engine-graph-test
     SOURCES  test/engine-graph/Deterministic.cpp
              test/engine-graph/SystemNode.test.cpp
-             test/engine-graph/Translator.test.cpp
-             test/engine-graph/TranslatorPlan.test.cpp
-             test/engine-graph/TranslatorGoldens.test.cpp
              test/engine-graph/EngineGraph.test.cpp
     LIBS     retroplug-backend
     INCLUDES ${RP_QUICKJS_INCLUDE}
     DEFS     RP_MGB_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/mGB.gb"
              RP_BLIPTOASTER_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/bliptoaster.nes"
-             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms"
-             RP_TRANSLATOR_GOLDENS_PATH="${CMAKE_SOURCE_DIR}/packages/retroplug/test/graph/golden/build-graph.json")
+             RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj_v0_45.sms")
 
 # Guards the native file watcher (NativeFileWatcher over efsw): config.json + bindings/ recursive
 # detection, registered-ROM parent-dir watches, and the unregistered-file filter.
diff --git a/packages/native/plugin/PluginDSP.cpp b/packages/native/plugin/PluginDSP.cpp
index a86899a2..6ea106fc 100644
--- a/packages/native/plugin/PluginDSP.cpp
+++ b/packages/native/plugin/PluginDSP.cpp
@@ -25,7 +25,6 @@
 #include "PluginShared.hpp"     // SharedDSP handoff to the editor
 #include "LastGoodState.hpp"   // keeps a failed project serialization from reporting "empty"
 
-#include "host/graph/Translator.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
 #include "Version.hpp"                    // single source of truth for the plugin version
@@ -344,7 +343,7 @@ protected:
 
         // One audio block: drain control-thread edits on the audio thread, set transport (direct: we're
         // the audio thread), render into the output channels (the plugin's 4 stereo pairs, laid out by the
-        // translator's Multi(8) shape and the project's routing).
+        // graph TS builds for 8 outputs and the project's routing).
         svc_.invoker.drainInto(svc_.engine);
         svc_.engine.setBpm(bpm);
         svc_.engine.setTransport(playing);
diff --git a/packages/native/sdl/main.cpp b/packages/native/sdl/main.cpp
index 91bf20f6..92abefaf 100644
--- a/packages/native/sdl/main.cpp
+++ b/packages/native/sdl/main.cpp
@@ -60,7 +60,6 @@ extern "C" {
 #include "dpfjs/LvglJsEngine.hpp"            // LVGL display layer over the shared txiki host
 #include "dpfjs/host/TjsHostRuntime.hpp"     // shared txiki/QuickJS host (+ tjs.h/quickjs.h)
 
-#include "host/graph/Translator.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
 #include "host/input/GamepadManager.hpp"     // SDL controller poll (shared UI-thread input)
@@ -902,7 +901,7 @@ void renderAudioBlock(AppState& a, float* out, int frames) {
     a.svc.engine.setBpm(bpm);            // 120 free-run until an external clock master appears
     a.svc.engine.setTransport(playing);  // then locks to its tempo + start/stop
     // Multi-out: render into the N planar channel buffers; the Engine zeroes them, and the plan routes each
-    // system to its pair per audioRouting (the translator wires the N-channel host shape). N=2 collapses
+    // system to its pair per audioRouting (TS builds the graph for the N-channel host shape). N=2 collapses
     // every mode to the stereo mix. N=4/6/8 spreads stems across the device's pairs.
     a.svc.engine.processBlock(static_cast<std::uint32_t>(frames), a.audioPtrs.data(), static_cast<std::size_t>(N));
 
diff --git a/packages/native/src/host/engine/DspCommand.hpp b/packages/native/src/host/engine/DspCommand.hpp
index d0d328af..58b44091 100644
--- a/packages/native/src/host/engine/DspCommand.hpp
+++ b/packages/native/src/host/engine/DspCommand.hpp
@@ -10,7 +10,7 @@
 // user-initiated) cross as raw OWNING pointers: the audio thread applies then `delete`s them, the
 // accepted non-RT-on-rare-op pattern. StageMidi is inline for a channel message (fits in 4 bytes); a
 // longer one (SysEx, or several messages staged as one run) crosses as an owning `ext` vector, the same
-// rare-op pattern. Structure never comes this way: the translator plans it.
+// rare-op pattern. Structure never comes this way: TS sends it as graph documents.
 struct DspCommand {
     enum class Kind : std::uint8_t {
         None = 0, SetSystems = 1, LoadKernel = 2, StageMidi = 3,
diff --git a/packages/native/src/host/engine/Engine.cpp b/packages/native/src/host/engine/Engine.cpp
index 0ba15744..c32de349 100644
--- a/packages/native/src/host/engine/Engine.cpp
+++ b/packages/native/src/host/engine/Engine.cpp
@@ -23,7 +23,6 @@
 #include "host/graph/KernelNode.hpp"
 #include "host/graph/SystemNodes.hpp"
 #include "host/graph/SystemTable.hpp"
-#include "host/graph/Translator.hpp"
 
 namespace {
 
@@ -43,15 +42,9 @@ DspCommand syncTapCommand(bool arm, SystemId id, std::uint32_t mode) {
     return c;
 }
 
-// What the translator's routing arms against `table`, the systems of the plan it rides with: the pins (1) or
-// the five channels (3) of a lone NES under a split routing; a restore only otherwise.
-DspCommand syncTapFor(rp::host::AudioRouting routing, const rp::host::SystemTable& table) {
-    using rp::host::AudioRouting;
-    const bool pins  = routing == AudioRouting::PinSplit || routing == AudioRouting::StereoPinSplit;
-    const bool split = pins || routing == AudioRouting::ChannelSplit;
-    if (!split || table.slots.size() != 1 || !table.slots.front().nes) return syncTapCommand(false, 0, 0);
-    return syncTapCommand(true, table.slots.front().id, pins ? 1u : 3u);
-}
+// The graph before the first document TS sends: the kernel and the host's stereo pair.
+constexpr const char* kEmptyDocument =
+    R"({"nodes":[{"id":"kernel","type":"rp.dsp.kernel"},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],"wires":[]})";
 
 constexpr const char* kSystemNodeTypes[] = {"rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"};
 
@@ -93,9 +86,8 @@ std::uint32_t hostWidthOf(const rp::graph::GraphDocument& doc) {
 
 } // namespace
 
-// The Engine's graph: the node packages, the GraphHost running the plan, and the translator that turns
-// structure into its documents. Destroyed first (graph_ is the Engine's last member), the translator before
-// the host it plans for, and the host (whose system nodes call cores->released from their destructors)
+// The Engine's graph: the node packages and the GraphHost running the plan. Destroyed first (graph_ is the
+// Engine's last member), and the host (whose system nodes call cores->released from their destructors)
 // before the core handoff and the kernel interface those nodes call into.
 struct Engine::Graph {
     rp::graph::NodeRegistry                registry;
@@ -103,7 +95,6 @@ struct Engine::Graph {
     rp::host::HostCores                    cores{};
     rp::host::EngineKernel                 kernel{};
     std::unique_ptr<rp::graph::GraphHost>  host;
-    std::unique_ptr<rp::host::Translator>  translator;
     std::uint32_t                          maxFrames = kDefaultMaxBlockFrames;
 };
 
@@ -126,28 +117,7 @@ Engine::Engine(double sampleRate) : sampleRate_(sampleRate) {
     config.interfacesCtx = this;
     graph_->host = std::make_unique<rp::graph::GraphHost>(graph_->registry, graph_->resources, config);
     graph_->host->setHostCommandHandler(&Engine::onHostCommand, this);
-    graph_->translator = std::make_unique<rp::host::Translator>(
-        [this](const std::string& json, std::shared_ptr<const rp::host::SystemTable> table, bool rearmsTap) {
-            auto doc = rp::graph::parseGraphDocument(json);
-            if (!doc) {
-                std::fprintf(stderr, "[engine] the translator built a document that does not parse: %s\n", doc.error().what().c_str());
-                return false;
-            }
-            // An edit that re-arms the NES tap carries its SyncTap in the plan's swap, so the wiring and the
-            // tap land in one drain or not at all. routing() already holds the mode this edit set.
-            rp::graph::HostCommand trailer{};
-            if (rearmsTap) {
-                const DspCommand c = syncTapFor(graph_->translator->routing(), *table);
-                std::memcpy(trailer.bytes, &c, sizeof c);
-            }
-            const auto result = graph_->host->setGraph(*doc, std::move(table), rearmsTap ? &trailer : nullptr);
-            for (const auto& e : result.errors)
-                std::fprintf(stderr, "[engine] a graph was refused (%s) at %s.%s: %s\n", e.code.c_str(), e.nodeId.c_str(),
-                             e.port.c_str(), e.message.c_str());
-            if (result.ok) hostWidth_ = hostWidthOf(*doc);
-            return result.ok;
-        },
-        [this](SystemBase* core) { registry_.release(core->id()); });
+    setGraph({kEmptyDocument, {}});
 }
 
 Engine::~Engine() = default;
@@ -159,24 +129,16 @@ const void* Engine::graphInterface(void* ctx, const char* id) {
     return nullptr;
 }
 
-rp::host::Translator& Engine::translator() { return *graph_->translator; }
-
 bool Engine::pushCommand(const DspCommand& c) { return graph_->host->pushHostCommand(&c, sizeof c); }
 
-bool Engine::pushSyncTap() {
-    // Behind every plan already pushed, so the table it is applied against is the last one published: the
-    // translator's own.
-    return pushCommand(syncTapFor(graph_->translator->routing(), graph_->translator->table()));
-}
-
+// rp.host.cores/1: a system node's settings name its core by system id.
 SystemBase* Engine::takeCore(std::uint64_t token) {
+    if (token > UINT32_MAX) return nullptr;
     const auto it = parked_.find(static_cast<SystemId>(token));
-    if (token <= UINT32_MAX && it != parked_.end()) {
-        SystemBase* core = it->second.release();
-        parked_.erase(it);
-        return core;
-    }
-    return graph_->translator->take(token);
+    if (it == parked_.end()) return nullptr;
+    SystemBase* core = it->second.release();
+    parked_.erase(it);
+    return core;
 }
 
 bool Engine::parkCore(std::unique_ptr<SystemBase> core) {
@@ -307,7 +269,7 @@ void Engine::onHostCommand(void* ctx, const rp::graph::HostCommand& cmd, const v
 }
 
 // Every kind that rides the ring, SyncTap included, a per-system one aimed at the core `table` names. Structure
-// never arrives here: the translator turns it into plans.
+// never arrives here: TS sends it as graph documents.
 void Engine::applyCommand(DspCommand& cmd, const rp::host::SystemTable& table) {
     auto core = [&](std::uint32_t id) -> SystemBase* {
         const auto* e = table.find(id);
@@ -471,7 +433,7 @@ void Engine::runKernelStage(std::uint32_t frames) {
 #endif
 }
 
-// The translator's host shape decides who goes where: the caller set it before rendering.
+// The graph TS published decides who goes where: it built it for the host's outputs.
 void Engine::processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs) {
     // An empty block runs nothing, and the tail must not either: the cores never ran, so the MI.OUT gather
     // would collect the last block's log a second time. There are no frames to silence.
@@ -685,7 +647,7 @@ void Engine::applyConfigField(SystemBase* sys, std::uint8_t field, double value)
             const auto g = static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
             if (sb->config_.linkGroupId != g) {
                 sb->config_.linkGroupId = g;
-                // the translator rewires the cable (QueuedInvoker::applyConfigField)
+                // the cable is wiring: TS republishes the graph with the group (graph/buildGraph.ts)
             }
             break;
         }
diff --git a/packages/native/src/host/engine/Engine.hpp b/packages/native/src/host/engine/Engine.hpp
index c0ff77ca..6fe7a554 100644
--- a/packages/native/src/host/engine/Engine.hpp
+++ b/packages/native/src/host/engine/Engine.hpp
@@ -19,7 +19,7 @@ class SystemBase;
 class MesenNesSystem;
 struct DspCommand;
 namespace rp::graph { struct HostCommand; }
-namespace rp::host { class Translator; struct SystemTable; enum class AudioRouting : std::uint8_t; }
+namespace rp::host { struct SystemTable; }
 
 // A per-system config field a control-plane edit applies to the live core (SameBoy today). Carried
 // as a double across the command ring: gain in dB; a bool as 0/1; an enum/int as its integer value.
@@ -49,9 +49,9 @@ struct EngineFrame {
 };
 
 // The core of the host: the graph the systems run on (docs/modular-graph-phase-b1-design.md), the DSP role
-// kernel, the staged MIDI and the snapshot registry. Structure reaches it through the translator on the
-// control thread, which plans each change as a graph document; every other edit is a command on the
-// GraphHost's one ring, applied at the next drain. A removed core is freed on the control thread, with
+// kernel, the staged MIDI and the snapshot registry. Structure reaches it as graph documents TS builds
+// (docs/modular-graph-phase-b2-design.md), planned on the control thread; every other edit is a command on
+// the GraphHost's one ring, applied at the next drain. A removed core is freed on the control thread, with
 // the plan that last ran it. Transport and ppq are plain members, set per block by whoever drives it; the
 // only atomics count the blocks too long to run, which the audio thread may not log.
 class Engine {
@@ -66,7 +66,7 @@ public:
     double sampleRate() const { return sampleRate_; }
 
     // --- the running plan's systems: the block thread's view, or the control thread's while no audio
-    // thread runs (structure itself goes through translator()) ---
+    // thread runs (structure itself arrives through setGraph) ---
     std::size_t systemCount() const;
     SystemBase* findSystem(SystemId id);   // borrowed (non-owning)
 
@@ -134,8 +134,8 @@ public:
 
     // --- per block: the plan (the kernel stage, every system, the mixing, the host outputs), then the
     // MI.OUT collection, the snapshot publish and the ppq advance. `outputs` is a flat array of
-    // `numOutputs` planar channels laid out as the translator's host shape says (the plugin's 8, a
-    // render's pairs); the caller has drained the ring first. ---
+    // `numOutputs` planar channels laid out as the graph's host shape says (the plugin's 8, a render's
+    // pairs); the caller has drained the ring first. ---
     void processBlock(std::uint32_t frames, float* const* outputs, std::size_t numOutputs);
     void processBlock(std::uint32_t frames, float* outL, float* outR);
 
@@ -156,8 +156,7 @@ public:
     // when the core is deleted; processBlock publishes into it.
     SnapshotRegistry& registry() { return registry_; }
 
-    // --- the graph: QueuedInvoker routes structure to the translator and every other command onto the ring ---
-    rp::host::Translator& translator();             // control thread
+    // --- the ring: QueuedInvoker pushes every command onto it ---
     bool pushCommand(const DspCommand& c);          // control thread: false when the ring is full
     void drainCommands();                           // the block thread (or the control thread, quiescent)
     void setAudioThreadOwns(bool owns);
@@ -166,9 +165,6 @@ public:
     void discardPending();                          // teardown, the audio thread stopped
     // Grow the plan's block size to a host's maximum (never below kDefaultMaxBlockFrames). Quiescent.
     void setMaxBlockFrames(std::uint32_t frames);
-    // Re-arm the NES tap for an edit that re-arms without a plan (a remove or replace of an id already gone):
-    // a SyncTap behind everything pushed so far. A plan's own SyncTap rides in its swap. Control thread.
-    bool pushSyncTap();
 
     // --- graphs TS builds (docs/modular-graph-phase-b2-design.md section 2.5), control thread ---
     // Park a built core under its id until the system node a document names it in takes it. False, with the
diff --git a/packages/native/src/host/engine/EngineInvoker.cpp b/packages/native/src/host/engine/EngineInvoker.cpp
index 26c58092..87af6e2a 100644
--- a/packages/native/src/host/engine/EngineInvoker.cpp
+++ b/packages/native/src/host/engine/EngineInvoker.cpp
@@ -2,33 +2,12 @@
 
 #include <utility>
 
-#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase>
-
 #include "host/engine/Engine.hpp"
-#include "host/graph/Translator.hpp"
 
 // --- producer half (control thread): pack + push, then flush inline unless the audio thread drains ---
 
 bool QueuedInvoker::push(const DspCommand& c) { return engine_->pushCommand(c); }
 
-bool QueuedInvoker::setHostShape(rp::host::HostShape shape) { return engine_->translator().setShape(shape); }
-
-// Add, replace, remove and set-routing re-arm the NES tap. One that plans carries its SyncTap in the plan's
-// swap; one refused changes nothing, so it re-arms nothing either. An id already gone plans nothing, and
-// re-arms all the same.
-void QueuedInvoker::adoptSystem(std::unique_ptr<SystemBase> sys) { engine_->translator().add(std::move(sys)); }
-
-void QueuedInvoker::replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) {
-    const bool present = engine_->translator().find(id) != nullptr;
-    engine_->translator().replace(id, std::move(sys));  // absent: the core is freed, never adopted
-    if (!present) engine_->pushSyncTap();
-}
-
-void QueuedInvoker::removeSystem(SystemId id) {
-    if (engine_->translator().find(id)) engine_->translator().remove(id);
-    else engine_->pushSyncTap();
-}
-
 void QueuedInvoker::loadKernel(std::vector<std::uint8_t> bytecode) {
     DspCommand c;
     c.kind = DspCommand::Kind::LoadKernel;
@@ -111,18 +90,11 @@ void QueuedInvoker::setPpq(double ppq) {
     push(c);
 }
 
-void QueuedInvoker::setAudioRouting(std::uint8_t mode) {
-    engine_->translator().setRouting(static_cast<rp::host::AudioRouting>(mode));
-}
-
 void QueuedInvoker::applyConfigField(SystemId id, std::uint8_t field, double value) {
     DspCommand c;
     c.kind = DspCommand::Kind::SetConfigField;
     c.setConfigField = { static_cast<std::uint32_t>(id), field, value };
     push(c);  // no heap payload: dropped on a full ring
-    // The cable is wiring: the core's field changes, and the translator rewires.
-    if (static_cast<ConfigField>(field) == ConfigField::LinkGroup)
-        engine_->translator().setLinkGroup(id, static_cast<std::uint8_t>(static_cast<std::uint32_t>(value)));
 }
 
 void QueuedInvoker::pressButton(SystemId id, std::uint8_t button, bool down) {
diff --git a/packages/native/src/host/engine/EngineInvoker.hpp b/packages/native/src/host/engine/EngineInvoker.hpp
index 6d09b654..ca396db3 100644
--- a/packages/native/src/host/engine/EngineInvoker.hpp
+++ b/packages/native/src/host/engine/EngineInvoker.hpp
@@ -2,7 +2,6 @@
 
 #include <atomic>
 #include <cstdint>
-#include <memory>
 #include <string>
 #include <vector>
 
@@ -11,14 +10,11 @@
 #include "host/engine/DspCommand.hpp"
 
 class Engine;
-class SystemBase;
-namespace rp::host { struct HostShape; }
 
-// The ONE mutation path to the Engine. Structure (add / replace / remove, the routing mode, a Game Boy's
-// link group, the host's output shape) goes to the Engine's translator, which plans it as a graph
-// document; every other edit is packed into a POD DspCommand and pushed onto the GraphHost's one ring
-// (docs/modular-graph-phase-b1-design.md section 5), so all of them keep their push order. Who DRAINS the
-// ring is the only mode, held in `audioThreadOwns_`:
+// The ONE command path to the Engine. Structure arrives as graph documents TS builds (Engine::setGraph,
+// docs/modular-graph-phase-b2-design.md); every other edit is packed into a POD DspCommand and pushed
+// onto the GraphHost's one ring (docs/modular-graph-phase-b1-design.md section 5), in push order with the
+// plans. Who DRAINS the ring is the only mode, held in `audioThreadOwns_`:
 //   - while the audio thread owns the Engine (running), it drains each block; the producer just pushes.
 //   - otherwise every push applies inline (the GraphHost flushes as it is pushed onto), so the ring is
 //     empty at the moment the audio thread takes ownership, and the handoff is clean.
@@ -37,9 +33,6 @@ public:
     bool audioThreadOwns() const { return audioThreadOwns_.load(std::memory_order_acquire); }
 
     // producer (control thread)
-    void adoptSystem(std::unique_ptr<SystemBase> sys);
-    void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys);
-    void removeSystem(SystemId id);
     void loadKernel(std::vector<std::uint8_t> bytecode);
     void setSystems(std::string json);
     // Any length: a channel message rides the command inline, a longer run (SysEx) as an owning payload.
@@ -53,14 +46,9 @@ public:
     void setBpm(double bpm);
     void setTransport(bool playing);
     void setPpq(double ppq);
-    void setAudioRouting(std::uint8_t mode);
     void applyConfigField(SystemId id, std::uint8_t field, double value);
     void pressButton(SystemId id, std::uint8_t button, bool down);
     void writeRam(SystemId id, std::uint32_t offset, std::vector<std::uint8_t> bytes);
-    // The output shape the next blocks render into. The render calls set it before they render. True when
-    // it is planned (or already current); false when it does not plan (more systems than the host's outputs
-    // have buses for, say), and the shape before it keeps running, so a caller must not render as if it held.
-    bool setHostShape(rp::host::HostShape shape);
 
     // consumer: apply every queued command (the block thread, or the control thread while quiescent).
     void drainInto(Engine& engine);
diff --git a/packages/native/src/host/graph/HostServices.hpp b/packages/native/src/host/graph/HostServices.hpp
index 0114b784..70de6475 100644
--- a/packages/native/src/host/graph/HostServices.hpp
+++ b/packages/native/src/host/graph/HostServices.hpp
@@ -1,7 +1,7 @@
 #pragma once
 
-// The engine services B1's nodes ask the host for through rp_host.get_interface. B1 only: B2 hands
-// documents to TS and B3 dissolves the kernel, and each removes the service it no longer needs.
+// The engine services the engine's nodes ask the host for through rp_host.get_interface. B3 dissolves the
+// kernel, and with it rp.engine.kernel/1.
 
 #include <cstdint>
 
@@ -9,13 +9,13 @@ class SystemBase;
 
 namespace rp::host {
 
-// The core handoff (docs/modular-graph-phase-b1-design.md section 4.6). The translator parks each built
-// core under a token that goes in the system node's settings; the node takes it when first activated (not
-// when created) and owns it from then on. [control]
+// The core handoff (docs/modular-graph-phase-b2-design.md section 2.5). buildCore parks each built core under
+// its system id, which the system node's settings name as {"core": <id>}; the node takes it when first
+// activated (not when created) and owns it from then on. [control]
 //
-// A core belongs to one node id for life: take hands a token out once, so the same token under a new node
-// id finds nothing and that node fails to activate. A plan refused after its node took a core (a later node
-// failed, or the token named the wrong kind of core) has already released and freed that core.
+// A core belongs to one node for life: take hands a core out once, so a second node naming the same id
+// finds nothing and fails to activate. A plan refused after its node took a core (a later node failed, or
+// the id named the wrong kind of core) has already released and freed that core.
 #define RP_HOST_CORES_INTERFACE "rp.host.cores/1"
 struct HostCores {
     void* ctx;
diff --git a/packages/native/src/host/graph/SystemNodes.cpp b/packages/native/src/host/graph/SystemNodes.cpp
index c90c9ba6..e81d6fec 100644
--- a/packages/native/src/host/graph/SystemNodes.cpp
+++ b/packages/native/src/host/graph/SystemNodes.cpp
@@ -19,8 +19,8 @@ namespace rp::host {
 
 // Named, so its JSON Schema $defs key is the same under every compiler.
 struct SystemNodeSettings {
-    // The token the translator parked this node's built core under (section 4.6). A new core is a new
-    // token, so it is always a new instance.
+    // The system id buildCore parked this node's core under (docs/modular-graph-phase-b2-design.md section
+    // 2.5). A new core is a new id, so it is always a new instance.
     std::uint64_t core = 0;
 };
 
@@ -62,7 +62,7 @@ struct SystemNode {
         if (!cores) return false;
         SystemBase* built = cores->take(cores->ctx, token);
         core = dynamic_cast<Core*>(built);
-        if (!core && built) {  // a token naming another kind of core is a translator bug
+        if (!core && built) {  // an id naming another kind of core is a document bug
             cores->released(cores->ctx, built);  // the engine keeps a snapshot slot for it, as for any core
             delete built;
         }
diff --git a/packages/native/src/host/rpc/AudioDriverRpcService.cpp b/packages/native/src/host/rpc/AudioDriverRpcService.cpp
index 012b3858..9b0e7ddc 100644
--- a/packages/native/src/host/rpc/AudioDriverRpcService.cpp
+++ b/packages/native/src/host/rpc/AudioDriverRpcService.cpp
@@ -1,6 +1,5 @@
 #include "host/rpc/AudioDriverRpcService.hpp"
 
-#include "host/graph/Translator.hpp"
 
 #include <chrono>
 #include <cstdio>
diff --git a/packages/native/src/host/rpc/BackendRpcRegistration.hpp b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
index d80b0475..6031cf66 100644
--- a/packages/native/src/host/rpc/BackendRpcRegistration.hpp
+++ b/packages/native/src/host/rpc/BackendRpcRegistration.hpp
@@ -35,13 +35,11 @@ void registerHostRpc(Server& s, HostRpcService& h) {
     s.template addMethod<&HostRpcService::pngDecode>(h);
 }
 
-// --- emulator: lifecycle / snapshot reads / live config / input (16) ---
+// --- emulator: lifecycle / snapshot reads / live config / input (13) ---
 template <class Server>
 void registerEmulatorRpc(Server& s, EngineRpcService& e) {
-    s.template addMethod<&EngineRpcService::constructSystem>(e);
     s.template addMethod<&EngineRpcService::buildCore>(e);
     s.template addMethod<&EngineRpcService::setGraph>(e);
-    s.template addMethod<&EngineRpcService::removeSystem>(e);
     s.template addMethod<&EngineRpcService::applySystemSetting>(e);
     s.template addMethod<&EngineRpcService::applyRoleConfig>(e);
     s.template addMethod<&EngineRpcService::readState>(e);
@@ -52,7 +50,6 @@ void registerEmulatorRpc(Server& s, EngineRpcService& e) {
     s.template addMethod<&EngineRpcService::getFrame>(e);
     s.template addMethod<&EngineRpcService::pressButton>(e);
     s.template addMethod<&EngineRpcService::stageSystemMidi>(e);
-    s.template addMethod<&EngineRpcService::setAudioRouting>(e);
     s.template addMethod<&EngineRpcService::setSerialOutCapture>(e);
 }
 
@@ -133,7 +130,7 @@ void registerDriverRpc(Server& s, AudioDriverRpcService& d) {
     s.template addMethod<&AudioDriverRpcService::drainReleased>(d);
 }
 
-// The full union (92 methods) + discovery - every facet mounted. Hosts that expose the whole surface
+// The full union (89 methods) + discovery - every facet mounted. Hosts that expose the whole surface
 // (the CLI + test host) use this; scoped hosts call the individual register functions they're allowed to.
 template <class Server>
 void registerAllBackendRpc(Server& s, HostRpcService& host, EngineRpcService& engine,
diff --git a/packages/native/src/host/rpc/BackendTypes.hpp b/packages/native/src/host/rpc/BackendTypes.hpp
index be7a5101..90354fb7 100644
--- a/packages/native/src/host/rpc/BackendTypes.hpp
+++ b/packages/native/src/host/rpc/BackendTypes.hpp
@@ -101,7 +101,6 @@ struct BackendConstructSpec {
     std::optional<std::string>               core;      // TS Core ("sameboy"/"mesen"); the factory registry key. Absent → defaultCoreFor(platform)
     std::optional<std::string>               savPath;
     std::optional<std::string>               statePath;
-    std::optional<std::uint32_t>             replaceId;
     std::optional<rfl::Bytestring> sramBytes;
     std::optional<rfl::Bytestring> stateBytes;
     // Effective ROM bytes to load INSTEAD of slurping romPath — a TS-supplied patched image (e.g. LSDj
diff --git a/packages/native/src/host/rpc/EngineRpcService.cpp b/packages/native/src/host/rpc/EngineRpcService.cpp
index 15028c7a..f5b60e42 100644
--- a/packages/native/src/host/rpc/EngineRpcService.cpp
+++ b/packages/native/src/host/rpc/EngineRpcService.cpp
@@ -1,7 +1,5 @@
 #include "host/rpc/EngineRpcService.hpp"
 
-#include "host/graph/Translator.hpp"
-
 #include <algorithm>
 #include <cmath>
 #include <cstdio>
@@ -94,22 +92,6 @@ bool EngineRpcService::rendersInto(std::size_t channels) const {
 // Out-of-line so unique_ptr<KitCompiler> can destroy a complete type (fwd-declared in the header).
 EngineRpcService::~EngineRpcService() = default;
 
-bool EngineRpcService::constructSystem(BackendConstructSpec spec) {
-    // TS owns the id counter and passes it in; the build (control thread) can fail (bad ROM), the
-    // queued adopt can't — so this returns "did it build", never an id.
-    const SystemId id = spec.id;
-    auto sys = factory_.build(id, toBuildSpec(spec), engine_.sampleRate());
-    if (!sys) return false;  // unknown backend / unreadable or non-SameBoy ROM
-
-    // Seed the snapshot slot from the just-built core (control thread) BEFORE handoff, so a read
-    // right after construct works with no block rendered.
-    if (!engine_.registry().claim(id, *sys)) return false;  // pool full
-
-    if (spec.replaceId) invoker_.replaceSystem(*spec.replaceId, std::move(sys));
-    else                invoker_.adoptSystem(std::move(sys));
-    return true;
-}
-
 std::optional<RpcBuiltCore> EngineRpcService::buildCore(BackendConstructSpec spec) {
     // A core in a plan the audio thread has already handed back is freed first, so the id check sees it gone.
     invoker_.reclaimReleased();
@@ -153,17 +135,10 @@ RpcSetGraphResult EngineRpcService::setGraph(RpcSetGraphRequest request) {
     return out;
 }
 
-// duplicate + reload are TS orchestration now (SystemsStore over the registry reads + constructSystem-
-// with-state): duplicate pulls the source savestate and builds a seeded core; reload pulls SRAM and
-// cold-boots the ROM with replaceId. Native has no bespoke method for either — that deletes the
-// findSystem walks + the last live-core clone/cloneFromState reliance from this layer.
-
-bool EngineRpcService::removeSystem(std::uint32_t id) {
-    // No threading branch: the translator plans the system out. Quiescent, the plan applies at once and
-    // the core is freed; running, the core leaves with the plan it last ran in, freed at reclaim.
-    invoker_.removeSystem(id);
-    return true;
-}
+// duplicate, reload and remove are TS orchestration (SystemsStore over the registry reads + buildCore, and
+// the documents it publishes): duplicate pulls the source savestate and builds a seeded core; reload pulls
+// SRAM and cold-boots the ROM into the same slot; remove publishes a document without it. Native has no
+// bespoke method for any of them.
 
 bool EngineRpcService::applySystemSetting(std::uint32_t id, std::string key, double value) {
     // A universal per-system setting → the live core, pushed through the invoker (flushed inline when
@@ -489,12 +464,6 @@ bool EngineRpcService::setBpm(double bpm) {
     return true;
 }
 
-bool EngineRpcService::setAudioRouting(std::uint32_t mode) {
-    if (mode > 5) return false;  // Stereo / TwoPerInstance / OnePerInstance / ChannelSplit / PinSplit / StereoPinSplit
-    invoker_.setAudioRouting(static_cast<std::uint8_t>(mode));  // → the translator, which wires it
-    return true;
-}
-
 bool EngineRpcService::stageMidiIn(std::vector<std::uint8_t> bytes) {
     // Any non-empty length: a channel message, a SysEx, or several messages staged as one run. The
     // invoker carries the long ones as an owning payload; the kernel's routing broadcasts a run longer
diff --git a/packages/native/src/host/rpc/EngineRpcService.hpp b/packages/native/src/host/rpc/EngineRpcService.hpp
index 44aec5f5..57baf80f 100644
--- a/packages/native/src/host/rpc/EngineRpcService.hpp
+++ b/packages/native/src/host/rpc/EngineRpcService.hpp
@@ -26,14 +26,12 @@ public:
     ~EngineRpcService();   // out-of-line: kitCompiler_ holds a fwd-declared KitCompiler
 
     // --- emulator lifecycle / reads ---
-    // (duplicate + reload live in the TS SystemsStore as constructSystem-with-state orchestration.)
-    bool constructSystem(BackendConstructSpec spec);   // TS-owned id in spec.id; returns "did it build"
-    // Build a core and park it under spec.id for the next document to place (spec.replaceId is not read):
-    // the core's facts, or null when it did not build or a core under that id is still live.
+    // (duplicate, reload and remove live in the TS SystemsStore as buildCore + setGraph orchestration.)
+    // Build a core and park it under spec.id (TS owns the id counter) for the next document to place: the
+    // core's facts, or null when it did not build or a core under that id is still live.
     std::optional<RpcBuiltCore> buildCore(BackendConstructSpec spec);
     // Plan a document TS built (docs/modular-graph-phase-b2-design.md section 2.5).
     RpcSetGraphResult           setGraph(RpcSetGraphRequest request);
-    bool removeSystem(std::uint32_t id);
     bool applySystemSetting(std::uint32_t id, std::string key, double value);
     bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
     std::optional<rfl::Bytestring> readState(std::uint32_t id);
@@ -82,7 +80,6 @@ public:
     bool            setTransport(bool running);
     bool            setPpq(double ppq);
     bool            setBpm(double bpm);
-    bool            setAudioRouting(std::uint32_t mode);
     bool            stageMidiIn(std::vector<std::uint8_t> bytes);
     // The control-surface twin: staged as `controllerIn` rather than musical MIDI, so a headless test
     // can drive a Launchpad app without a Launchpad. Harness-facet only, like stageMidiIn.
diff --git a/packages/native/src/system/sameboy/SameBoyConfig.hpp b/packages/native/src/system/sameboy/SameBoyConfig.hpp
index fe9620ef..0a79a248 100644
--- a/packages/native/src/system/sameboy/SameBoyConfig.hpp
+++ b/packages/native/src/system/sameboy/SameBoyConfig.hpp
@@ -88,7 +88,7 @@ struct SameBoyConfig {
     // Serial-link group. 0 = standalone (default). Same nonzero id on multiple
     // systems links them on one cable: their serial ports are ferried
     // bit-for-bit and they step in instruction-level lockstep, as one coupled
-    // group of the graph (host/graph/Translator.cpp wires it).
+    // group of the graph (packages/retroplug/src/graph/buildGraph.ts wires it).
     std::uint8_t              linkGroupId = 0;
     std::string               romPath;    // absolute path; populated at bootstrap or load
     // Loose-battery disambiguator. 0 => this system owns the plain sibling
diff --git a/packages/native/test/audio/EngineSampleRate.test.cpp b/packages/native/test/audio/EngineSampleRate.test.cpp
index accb2aa4..b12056d1 100644
--- a/packages/native/test/audio/EngineSampleRate.test.cpp
+++ b/packages/native/test/audio/EngineSampleRate.test.cpp
@@ -14,8 +14,8 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#include "../engine-graph/GraphDocs.hpp"
 #include "host/engine/Engine.hpp"
-#include "host/engine/EngineInvoker.hpp"
 #include "system/sameboy/SameBoyConfig.hpp"
 #include "system/sameboy/SameBoySystem.hpp"
 
@@ -40,13 +40,15 @@ std::vector<std::uint8_t> readRom() {
 } // namespace
 
 TEST_CASE("Engine::setSampleRate re-rates already-live systems", "[audio][samplerate][engine]") {
-    Engine        eng(44100.0);
-    QueuedInvoker inv(eng);
+    Engine eng(44100.0);
 
     auto sys = std::make_unique<SameBoySystem>(1, SameBoyConfig{}, readRom());
     sys->onActivate(eng.sampleRate());  // built at the engine's current rate, as the factory does
     SameBoySystem* raw = sys.get();
-    inv.adoptSystem(std::move(sys));
+    REQUIRE(eng.registry().claim(1, *sys));
+    REQUIRE(eng.parkCore(std::move(sys)));
+    using namespace rp::host::test;
+    REQUIRE(eng.setGraph(graphRequest(stereoDocument({{1, kGbType}}), {1})).ok);
     REQUIRE(eng.findSystem(1) == raw);
     REQUIRE(raw->sampleRate_ == 44100.0);
 
diff --git a/packages/native/test/audio/NesSplitRouting.test.cpp b/packages/native/test/audio/NesSplitRouting.test.cpp
index 4a50eac3..0bed68a3 100644
--- a/packages/native/test/audio/NesSplitRouting.test.cpp
+++ b/packages/native/test/audio/NesSplitRouting.test.cpp
@@ -1,17 +1,18 @@
-// Guards NES-in-plugin split routing end to end through the Engine: AudioRouting::PinSplit and
-// AudioRouting::ChannelSplit over a REAL Mesen NES core, mono-packed one stream per output lane.
+// Guards NES split routing end to end through the Engine: the documents TS builds for PinSplit,
+// StereoPinSplit and ChannelSplit over a REAL Mesen NES core, mono-packed one stream per output lane.
 //
 // The load-bearing thing here is the LIVE ARM. MesenNesSystem only reports per-channel streams once
 // NesSoundMixer::SetChannelCapture has run, and that used to happen at construct only (from the "mesen"
 // role's channelExportMode), so selecting a split mode in the plugin would have silently rendered a
-// plain stereo mix. The engine's SyncTap arms the tap for the mode being entered without rebuilding the
-// core (a routing flip must not reset a playing game), and restores what it found on the way out so the
-// CLI / render path (which DOES set channelExportMode at construct and never selects a split routing)
-// is left alone. Every assertion below is really about that: a non-silent lane 2 (Expansion) or lane 4
-// (DMC) can only exist if the tap armed after construct.
+// plain stereo mix. The engine's SyncTap, riding the plan's swap, arms the tap for the mode being entered
+// without rebuilding the core (a routing flip must not reset a playing game), and restores what it found
+// on the way out so the CLI / render path (which DOES set channelExportMode at construct and never selects
+// a split routing) is left alone. Every assertion below is really about that: a non-silent lane 2
+// (Expansion) or lane 4 (DMC) can only exist if the tap armed after construct.
 //
-// Lane arithmetic itself (stride, budget, gating) is the translator's (test/engine-graph/Translator.test.cpp),
-// and pin fidelity (Sum(pins) == the mix) is NesStems.test.cpp's; neither is re-litigated here.
+// Which routing wants which arm and which wiring is TS's (packages/retroplug/test/graph/build-graph.test.ts);
+// the documents here are spelled the way it writes them (test/engine-graph/GraphDocs.hpp). Pin fidelity
+// (Sum(pins) == the mix) is NesStems.test.cpp's; neither is re-litigated here.
 //
 // Drives bliptoaster with a ch1 note (-> APU Pulse1), which is the only way it makes sound.
 //
@@ -27,10 +28,9 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#include "../engine-graph/GraphDocs.hpp"
 #include "host/engine/DspCommand.hpp"
 #include "host/engine/Engine.hpp"
-#include "host/engine/EngineInvoker.hpp"
-#include "host/graph/Translator.hpp"
 #include "system/SystemBase.hpp"
 #include "system/mesen/MesenNesConfig.hpp"
 #include "system/mesen/MesenNesSystem.hpp"
@@ -60,7 +60,7 @@ std::vector<std::uint8_t> readRom() {
     return bytes;
 }
 
-// A booted NES, adopted the way a host adopts one: constructed with the DEFAULT (Mix) export mode, so
+// A booted NES, built the way buildCore builds one: constructed with the DEFAULT (Mix) export mode, so
 // anything per-channel the Engine later reports had to be armed live.
 std::unique_ptr<MesenNesSystem> buildNes(SystemId id, std::uint32_t exportMode = 0) {
     MesenNesConfig cfg;
@@ -130,26 +130,28 @@ void driveAndMeasure(Engine& eng, SystemId id, Lanes& lanes, std::size_t numOutp
 
 constexpr float kSignal = 1.0e-4f;  // "this lane carried audio"; a routed NES stem is orders above it
 
-// An engine rendering into `outputs` host channels, edited the way the hosts edit it: through the invoker.
+// An engine placing the documents TS would send: a core is parked as buildCore parks one, then planned in.
 struct NesRig {
-    Engine        eng{kSampleRate};
-    QueuedInvoker inv{eng};
-    explicit NesRig(std::uint32_t outputs) { inv.setHostShape({rp::host::HostShape::Kind::Multi, outputs}); }
-    void route(rp::host::AudioRouting mode) { inv.setAudioRouting(static_cast<std::uint8_t>(mode)); }
+    Engine eng{kSampleRate};
+    void park(std::unique_ptr<SystemBase> core) {
+        REQUIRE(eng.registry().claim(core->id(), *core));
+        REQUIRE(eng.parkCore(std::move(core)));
+    }
+    void place(Engine::GraphRequest r) { REQUIRE(eng.setGraph(r).ok); }
 };
 
-using rp::host::AudioRouting;
+using namespace rp::host::test;
 
 } // namespace
 
 TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across outs 0..2",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::PinSplit);
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
 
-    // The arm happened on the routing change, against an already-booted core — no rebuild, no reset.
+    // The arm rode the plan's swap, against an already-booted core — no rebuild, no reset.
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
     CHECK(nes->channelExportMode() == 1);           // StereoModPins
@@ -170,10 +172,10 @@ TEST_CASE("Engine PinSplit arms a real NES tap live and fans 3 mono pins across
 
 TEST_CASE("Engine StereoPinSplit gives each pin its own PAIR, mirrored L=R",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::StereoPinSplit);
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "stereoPins"), {1}), 1, 1));
 
     // Same tap as PinSplit — the modes differ only in how many lanes the three streams spread over.
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
@@ -200,11 +202,12 @@ TEST_CASE("Engine StereoPinSplit gives each pin its own PAIR, mirrored L=R",
 
 TEST_CASE("StereoPinSplit needs 6 lanes, so a narrow host falls back rather than truncating",
           "[audio][channelsplit][nes]") {
-    // The SDL 4-channel case: mono Pins (3 lanes) fits, the pair-per-pin layout does not.
-    NesRig rig(4);
+    // The SDL 4-channel case: mono Pins (3 lanes) fits, the pair-per-pin layout does not, so TS wires it as
+    // Stereo and arms the tap all the same (the narrow-host defect, docs/modular-graph-phase-b0-design.md 2.5).
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::StereoPinSplit);
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 4), {1}), 1, 1));
 
     Lanes lanes;
     driveAndMeasure(eng, 1, lanes, 4);  // only 4 lanes offered — 6 needed
@@ -217,10 +220,10 @@ TEST_CASE("StereoPinSplit needs 6 lanes, so a narrow host falls back rather than
 
 TEST_CASE("Engine ChannelSplit on a real NES fans 5 mono core channels across outs 0..4",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::ChannelSplit);
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));
 
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
@@ -239,20 +242,21 @@ TEST_CASE("Engine ChannelSplit on a real NES fans 5 mono core channels across ou
 
 TEST_CASE("Leaving a split routing restores the NES tap and the plain stereo mix",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
 
     // Round-trip every split mode. Each leaves the core exactly as it was found, so a user flipping
     // through the Audio Routing cycler can't strand the tap armed (which would leave the mix path
     // draining capture streams forever).
-    rig.route(AudioRouting::PinSplit);
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
     CHECK(nes->channelExportMode() == 1);
-    rig.route(AudioRouting::ChannelSplit);
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));
     CHECK(nes->channelExportMode() == 3);
-    rig.route(AudioRouting::Stereo);
+    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
     CHECK(nes->channelExportMode() == 0);
     CHECK(nes->channelLayout().size() == 1);  // back to the single stereo "Mix" stream
 
@@ -266,78 +270,77 @@ TEST_CASE("Leaving a split routing restores the NES tap and the plain stereo mix
 
 TEST_CASE("A second system disarms the NES tap and falls back to Stereo",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::PinSplit);
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
 
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
     REQUIRE(nes->channelExportMode() == 1);
 
-    // The splits are single-system-only, so adopting a peer must retire the plan AND the tap it needed —
-    // otherwise the mix path would keep draining capture streams nothing reads.
-    rig.inv.adoptSystem(buildNes(2));
+    // The splits are single-system-only, so adding a peer must retire the wiring AND the tap it needed —
+    // otherwise the mix path would keep draining capture streams nothing reads. TS sends a restore only.
+    rig.park(buildNes(2));
+    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}, {2, kNesType}}, 8), {1, 2})));
     CHECK(nes->channelExportMode() == 0);
     CHECK(nes->channelLayout().size() == 1);
 
-    // Removing the peer re-enters the split, re-arming from scratch: the plan tracks the system set, not
-    // just the last setAudioRouting call.
-    rig.inv.removeSystem(2);
+    // Removing the peer re-enters the split, re-arming from scratch.
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1));
     CHECK(nes->channelExportMode() == 1);
     CHECK(nes->channelLayout().size() == 3);
 }
 
-TEST_CASE("A remove or replace of an id already gone still re-arms the NES tap",
+TEST_CASE("A re-arm restores and arms again, even with an unchanged document",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
-    rig.route(AudioRouting::PinSplit);
+    rig.park(buildNes(1));
+    const Engine::GraphRequest pins = rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1);
+    rig.place(pins);
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
     REQUIRE(nes->channelExportMode() == 1);
 
     // Knock the tap off the mode the routing wants, behind the engine's back: only a re-arm (restore, then
-    // arm again, resetting the capture streams) puts it back. Every remove and replace has re-armed, even
-    // one whose target is already gone, and each plans nothing here.
-    nes->setChannelExportMode(2);
-    rig.inv.removeSystem(99);
-    CHECK(nes->channelExportMode() == 1);
-
+    // arm again, resetting the capture streams) puts it back. Every edit that builds or drops a core, and
+    // every routing change, re-arms, even one that leaves the document as it was.
     nes->setChannelExportMode(2);
-    rig.inv.replaceSystem(98, buildNes(98));  // the core is freed, never adopted
+    rig.place(pins);
     CHECK(nes->channelExportMode() == 1);
     CHECK(eng.systemCount() == 1);
 }
 
-TEST_CASE("A NES replaced under its own id is armed afresh, never restored to the old core's mode",
+TEST_CASE("A replacing NES is armed afresh, never restored to the replaced core's mode",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1, 2));     // built with the pins + mix-reference tap
-    rig.route(AudioRouting::ChannelSplit);  // armed to 3, remembering 2
+    rig.park(buildNes(1, 2));  // built with the pins + mix-reference tap
+    rig.place(rearming(graphRequest(nesSplitDocument(1, "channels"), {1}), 1, 3));  // armed to 3, remembering 2
     REQUIRE(dynamic_cast<MesenNesSystem*>(eng.findSystem(1))->channelExportMode() == 3);
 
-    rig.inv.replaceSystem(1, buildNes(1));  // a fresh core, built as plain Mix, under the same id
-    auto* fresh = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
+    rig.park(buildNes(2));  // a fresh core, built as plain Mix, in the replaced one's slot
+    rig.place(rearming(graphRequest(nesSplitDocument(2, "channels"), {2}), 2, 3));
+    auto* fresh = dynamic_cast<MesenNesSystem*>(eng.findSystem(2));
     REQUIRE(fresh != nullptr);
     CHECK(fresh->channelExportMode() == 3);
     // Leaving the split puts back what THIS core was built with: the old core's 2 left with it.
-    rig.route(AudioRouting::Stereo);
+    rig.place(rearming(graphRequest(stereoDocument({{2, kNesType}}, 8), {2})));
     CHECK(fresh->channelExportMode() == 0);
 }
 
 TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they land together",
           "[audio][channelsplit][nes]") {
-    NesRig rig(8);
+    NesRig rig;
     Engine& eng = rig.eng;
-    rig.inv.adoptSystem(buildNes(1));
+    rig.park(buildNes(1));
+    rig.place(rearming(graphRequest(stereoDocument({{1, kNesType}}, 8), {1})));
     auto* nes = dynamic_cast<MesenNesSystem*>(eng.findSystem(1));
     REQUIRE(nes != nullptr);
 
     // The test thread is the audio thread from here: pushes only queue, and drainCommands applies them.
-    rig.inv.setAudioThreadOwns(true);
+    eng.setAudioThreadOwns(true);
     DspCommand bpm;
     bpm.kind = DspCommand::Kind::SetBpm;
     bpm.setBpm = { 120.0 };
@@ -348,10 +351,10 @@ TEST_CASE("A routing edit's plan and its tap arming take one ring slot, so they
 
     // One slot left. Were the arming a command of its own it would be dropped here, and the pin wiring
     // would run against an unarmed tap.
-    rig.route(AudioRouting::PinSplit);
+    REQUIRE(eng.setGraph(rearming(graphRequest(nesSplitDocument(1, "pins"), {1}), 1, 1)).ok);
     CHECK_FALSE(eng.pushCommand(bpm));
     eng.drainCommands();
     CHECK(nes->channelExportMode() == 1);
     CHECK(nes->channelLayout().size() == 3);
-    rig.inv.setAudioThreadOwns(false);
+    eng.setAudioThreadOwns(false);
 }
diff --git a/packages/native/test/engine-graph/EngineGraph.test.cpp b/packages/native/test/engine-graph/EngineGraph.test.cpp
index 5229ebec..09895ced 100644
--- a/packages/native/test/engine-graph/EngineGraph.test.cpp
+++ b/packages/native/test/engine-graph/EngineGraph.test.cpp
@@ -11,6 +11,7 @@
 
 #include <catch2/catch_test_macros.hpp>
 
+#include "GraphDocs.hpp"
 #include "NodeRig.hpp"
 #include "host/engine/Engine.hpp"
 #include "host/engine/EngineInvoker.hpp"
@@ -49,67 +50,16 @@ bool park(Engine& e, std::unique_ptr<SystemBase> core) {
     return e.parkCore(std::move(core));
 }
 
-struct Sys {
-    SystemId    id;
-    const char* type;
-};
+using rp::host::test::DocSystem;
 
-// What buildGraph writes for these systems summed to a stereo pair.
-std::string stereoDoc(const std::vector<Sys>& systems) {
-    std::string nodes = R"({"id":"kernel","type":"rp.dsp.kernel"})";
-    std::string wires;
-    std::string left, right;
-    for (const Sys& s : systems) {
-        const std::string id = std::to_string(s.id);
-        nodes += R"(,{"id":"sys-)" + id + R"(","type":")" + s.type + R"(","settings":{"core":)" + id + "}}";
-    }
-    for (const Sys& s : systems) {
-        const std::string id = std::to_string(s.id);
-        nodes += R"(,{"id":"split-)" + id + R"(","type":"rp.core.split"})";
-        wires += std::string(wires.empty() ? "" : ",") + R"({"from":"sys-)" + id + R"(.mix","to":"split-)" + id + R"(.in"})";
-    }
-    for (std::size_t c = 0; c < 2 && !systems.empty(); ++c) {
-        const std::string half = c ? "out2" : "out1";
-        const std::string out = R"("out.out.)" + std::to_string(c + 1) + "\"";
-        if (systems.size() == 1) {
-            wires += R"(,{"from":"split-)" + std::to_string(systems[0].id) + "." + half + R"(","to":)" + out + "}";
-            continue;
-        }
-        const std::string lane = "lane-" + std::to_string(c);
-        nodes += R"(,{"id":")" + lane + R"(","type":"rp.core.add","settings":{"inputs":)" + std::to_string(systems.size()) + "}}";
-        for (std::size_t k = 0; k < systems.size(); ++k)
-            wires += R"(,{"from":"split-)" + std::to_string(systems[k].id) + "." + half + R"(","to":")" + lane + ".in" +
-                     std::to_string(k + 1) + "\"}";
-        wires += R"(,{"from":")" + lane + R"(.out","to":)" + out + "}";
-    }
-    nodes += R"(,{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}})";
-    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
+Engine::GraphRequest request(const std::vector<DocSystem>& systems) {
+    std::vector<SystemId> slots;
+    for (const DocSystem& s : systems) slots.push_back(s.id);
+    return rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), slots);
 }
 
-// What buildGraph writes for the perSystem shape: slot i's mix on host channels 2i and 2i + 1.
-std::string perSystemDoc(const std::vector<Sys>& systems) {
-    std::string nodes = R"({"id":"kernel","type":"rp.dsp.kernel"})";
-    std::string wires, buses;
-    for (std::size_t i = 0; i < systems.size(); ++i) {
-        const std::string id = std::to_string(systems[i].id);
-        nodes += R"(,{"id":"sys-)" + id + R"(","type":")" + systems[i].type + R"(","settings":{"core":)" + id + "}}";
-        wires += std::string(i ? "," : "") + R"({"from":"sys-)" + id + R"(.mix","to":"out.out.)" + std::to_string(2 * i + 1) +
-                 "-" + std::to_string(2 * i + 2) + "\"}";
-        buses += std::string(i ? "," : "") + "2";
-    }
-    nodes += R"(,{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + buses + "]}}";
-    return R"({"nodes":[)" + nodes + R"(],"wires":[)" + wires + "]}";
-}
-
-Engine::GraphRequest request(const std::vector<Sys>& systems) {
-    Engine::GraphRequest r;
-    r.document = stereoDoc(systems);
-    for (const Sys& s : systems) r.slots.push_back(s.id);
-    return r;
-}
-
-const char* const kGb = "rp.sameboy.gb";
-const char* const kNes = "rp.mesen.nes";
+const char* const kGb = rp::host::test::kGbType;
+const char* const kNes = rp::host::test::kNesType;
 
 }  // namespace
 
@@ -264,10 +214,7 @@ TEST_CASE("a render into buffers the planned graph does not write is refused", "
     RpcRenderBlocksRequest blocks{4, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
     CHECK_FALSE(svc.renderBlocks(blocks).error.empty());
 
-    Engine::GraphRequest perSystem;
-    perSystem.document = perSystemDoc({{1, kGb}, {2, kGb}});
-    perSystem.slots = {1, 2};
-    REQUIRE(e.setGraph(perSystem).ok);
+    REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::perSystemDocument({{1, kGb}, {2, kGb}}), {1, 2})).ok);
     CHECK(e.hostWidth() == 4);
     CHECK(svc.renderAudioPerSystem(10.0).size() == 2);
     CHECK(svc.renderAudio(10.0).empty());
diff --git a/packages/retroplug/cli/sdkSurfaceCheck.ts b/packages/retroplug/cli/sdkSurfaceCheck.ts
index de702730..f8d97cf3 100644
--- a/packages/retroplug/cli/sdkSurfaceCheck.ts
+++ b/packages/retroplug/cli/sdkSurfaceCheck.ts
@@ -51,7 +51,7 @@ type Exact<A, B, Msg extends string> =
 
 // Values: the NAME sets, two-way, so the declaration can neither omit an export nor invent one. This is
 // deliberately not a comparison of the types behind those names, because the declaration is a curated
-// NARROWER view on purpose - its `Backend` omits 25 methods a ROM author has no use for - so neither
+// NARROWER view on purpose - its `Backend` omits 24 methods a ROM author has no use for - so neither
 // direction of a type-level namespace comparison can hold, and demanding one would only pressure the
 // declaration into growing surface nobody wants. A missing or invented NAME is always a defect, and it
 // is the drift that has actually shipped: `deleteFile` / `listDir` / `rename` were implemented and
diff --git a/packages/retroplug/src/backend.ts b/packages/retroplug/src/backend.ts
index 79d88851..60f86156 100644
--- a/packages/retroplug/src/backend.ts
+++ b/packages/retroplug/src/backend.ts
@@ -95,32 +95,21 @@ export interface Backend {
 
   // --- Emulator lifecycle -------------------------------------------------
   //
-  // The narrow native service: build / clone / reload / drop an emulator. It takes
+  // The narrow native service: build an emulator core, and place what has been built. It takes
   // CONCRETE, already-resolved paths — TS does every bit of derivation (suffix math,
   // sibling paths, the paired-sav override, classification, pairing) with the pure
   // kernels and hands native only finished paths. Native never sees a suffix, never
   // looks for a sibling, never classifies. No ROM/SRAM bytes cross for construction:
-  // native slurps the paths it's given.
-
-  /** Build + activate an emulator from a spec under the TS-allocated `id` (TS owns the id counter;
-   *  native never mints one). Returns whether it BUILT — true on success, false on an I/O failure
-   *  (unreadable ROM / unknown format / pool full). With `replaceId` the instance swaps that id in
-   *  place; otherwise it is appended. The adopt is fire-and-forget, so there's no id to return. */
-  constructSystem(spec: ConstructSpec, id: number): boolean;
-
-  // duplicate + reload are TS orchestration over constructSystem-with-state (SystemsStore): the store
-  // pulls the source's savestate/SRAM from the registry and builds a seeded/replaceId core — native has
-  // no bespoke method for either.
-
-  /** Drop system `id`. Returns false when it isn't present. */
-  removeSystem(id: number): boolean;
-
-  /** Build + activate an emulator core from a spec under the TS-allocated `id`, and park it for the next
-   *  graph document to place (docs/modular-graph-phase-b2-design.md section 2.5). Nothing runs it until a
-   *  `setGraph` document names it as `sys-<id>`; a document published without it frees it. Returns what the
-   *  core must be placed as, or null when it did not build (an unreadable ROM, an unknown format, a full
-   *  snapshot registry) or a core under `id` is still live. `spec.replaceId` is not read: the document
-   *  says what a core replaces. */
+  // native slurps the paths it's given. Which cores run, in what order and wired how is
+  // the graph TS builds (graph/buildGraph.ts); duplicate, reload and remove are TS
+  // orchestration over these two calls (SystemsStore), with no native method of their own.
+
+  /** Build + activate an emulator core from a spec under the TS-allocated `id` (TS owns the id counter;
+   *  native never mints one), and park it for the next graph document to place
+   *  (docs/modular-graph-phase-b2-design.md section 2.5). Nothing runs it until a `setGraph` document
+   *  names it as `sys-<id>`; a document published without it frees it. Returns what the core must be
+   *  placed as, or null when it did not build (an unreadable ROM, an unknown format, a full snapshot
+   *  registry) or a core under `id` is still live. */
   buildCore(spec: ConstructSpec, id: number): BuiltCore | null;
 
   /** Plan and publish a graph document TS built (graph/buildGraph.ts): the engine's systems, their wiring
@@ -157,15 +146,6 @@ export interface Backend {
    *  kernel's `ctx.serialOut` for the Arduinoboy decoder. SameBoy-only (a no-op on other cores). */
   setSerialOutCapture(id: number, on: boolean): boolean;
 
-  /** Set the project's audio-output routing (0 Stereo / 1 TwoPerInstance / 2 OnePerInstance /
-   *  3 ChannelSplit / 4 PinSplit / 5 StereoPinSplit) — which of the plugin's 4 stereo output pairs each
-   *  system mixes into, or (modes 3..5, single system only) one system's channels fanned across the
-   *  outs: a Game Boy's 4 stereo channels over the 4 pairs, a NES's 5 mono core channels over outs 1..5,
-   *  a NES's 3 mono pins over outs 1..3, or those same 3 pins over a PAIR each (outs 1/2, 3/4, 5/6,
-   *  mirrored L=R). The one project-level setting that reaches native audio (the translator wires the
-   *  graph for the mode + system count, falling back to Stereo where a split does not apply). Returns
-   *  false on an out-of-range mode. */
-  setAudioRouting(mode: number): boolean;
 
   // --- Live emulator input ------------------------------------------------
   // The UI→DSP direction for game input: a joypad button transition on the focused system. Native queues
@@ -408,8 +388,8 @@ export type HostBackend = Pick<
 /** Emulator lifecycle / live config / input / snapshot reads — bound by the plugin, UI, CLI, test host. */
 export type EmulatorBackend = Pick<
   Backend,
-  | "constructSystem" | "removeSystem" | "buildCore" | "setGraph" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
-  | "setAudioRouting" | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
+  | "buildCore" | "setGraph" | "applySystemSetting" | "applyRoleConfig" | "setSerialOutCapture"
+  | "pressButton" | "stageSystemMidi" | "readState" | "readSram" | "readRam" | "writeRam" | "getFrame"
 >;
 
 /** Live-core inspection / stepping / breakpoints / profiler — the CLI-only debug facet (spec/09). */
@@ -778,9 +758,6 @@ export interface ConstructSpec {
   /** Exact savestate file to boot from. `null` = cold boot (the systems domain
    *  always passes null; present for the later Project domain). */
   statePath: string | null;
-  /** When set, swap this existing SystemId in place (load / replace); otherwise the
-   *  new system is appended. */
-  replaceId?: number;
   /** Effective ROM bytes to load INSTEAD of slurping `romPath` — a load-time role's patched image (e.g.
    *  LSDj asset overrides applied non-destructively). `romPath` still travels (for the ROM watcher and
    *  `.sav` resolution); only the loaded bytes differ. Honoured by both SameBoy (GB) and Mesen (NES/GBA).
diff --git a/packages/retroplug/src/realBackend.ts b/packages/retroplug/src/realBackend.ts
index dd6ba94d..f660840b 100644
--- a/packages/retroplug/src/realBackend.ts
+++ b/packages/retroplug/src/realBackend.ts
@@ -66,7 +66,6 @@ const specParams = (spec: ConstructSpec, id: number): Record<string, unknown> =>
   const p: Record<string, unknown> = { id, romPath: spec.romPath, platform: spec.platform, core: spec.core, embeddedRom: spec.embeddedRom };
   if (spec.savPath != null) p.savPath = spec.savPath;
   if (spec.statePath != null) p.statePath = spec.statePath;
-  if (spec.replaceId !== undefined) p.replaceId = spec.replaceId;
   if (spec.romBytes) p.romBytes = spec.romBytes;
   if (spec.sramBytes) p.sramBytes = spec.sramBytes;
   if (spec.stateBytes) p.stateBytes = spec.stateBytes;
@@ -107,15 +106,12 @@ export function createHostClient(): HostBackend {
 export function createEmulatorClient(): EmulatorBackend {
   const call = makeCall();
   return {
-    constructSystem: (spec: ConstructSpec, id: number) => call("constructSystem", specParams(spec, id)) as boolean,
-    removeSystem: (id) => call("removeSystem", id) as boolean,
     buildCore: (spec: ConstructSpec, id: number) => (call("buildCore", specParams(spec, id)) as BuiltCore | null) ?? null,
     setGraph: (request: SetGraphRequest) => call("setGraph", request) as SetGraphResult,
     applySystemSetting: (id, key, value) =>
       call("applySystemSetting", id, key, typeof value === "boolean" ? (value ? 1 : 0) : value) as boolean,
     applyRoleConfig: (id, kind, config) => call("applyRoleConfig", id, kind, JSON.stringify(config)) as boolean,
     setSerialOutCapture: (id, on) => call("setSerialOutCapture", id, on) as boolean,
-    setAudioRouting: (mode) => call("setAudioRouting", mode) as boolean,
     pressButton: (id, button, down) => call("pressButton", id, button, down) as boolean,
     stageSystemMidi: (id, bytes) => call("stageSystemMidi", id, bytes) as boolean,
     readState: (id) => bytesOrNull(call("readState", id)),
diff --git a/packages/retroplug/src/settingsEnums.ts b/packages/retroplug/src/settingsEnums.ts
index 4ea6857b..be6239e7 100644
--- a/packages/retroplug/src/settingsEnums.ts
+++ b/packages/retroplug/src/settingsEnums.ts
@@ -2,8 +2,8 @@
 // index-ordered value tuple whose position EQUALS the native enum integer — the tuple feeds
 // `z.enum(...)` (configSchema.enumField), indexes the menu cyclers' display-name arrays, and drives
 // the string<->int conversion at the native/kernel boundary. TS carries the strings everywhere;
-// native (SetAudioRouting, the reflect-cpp role structs) stays numeric, so the boundary encoders here
-// (`roleConfigForNative`, `audioRoutingToIndex`) map back to the ints native decodes.
+// native (the reflect-cpp role structs) stays numeric, so the boundary encoder here
+// (`roleConfigForNative`) maps back to the ints native decodes.
 //
 // A LEAF module: it imports nothing, so the schema, the domain modules (layout/midiRouting/dspRoles),
 // the stores, the menu, and the project migration can all share these values without an import cycle.
@@ -42,15 +42,15 @@ export const MidiRouting = {
   MidiChannelToInstance: "midiChannelToInstance",
 } as const satisfies Record<string, MidiRouting>;
 
-// Audio output-pair placement. Crosses to native's AudioRouting enum by index via `audioRoutingToIndex`
-// (Stereo=0 … StereoPinSplit=5). The three split modes are single-system-only (the menu gates them via
-// `validAudioRoutings`; native re-checks systemCount()==1 and is the authority):
+// Audio output-pair placement. Never crosses to native as a value: graph/buildGraph.ts turns it into the
+// graph's output wiring. The three split modes are single-system-only (the menu gates them via
+// `validAudioRoutings`; buildGraph re-checks the system count and is the authority):
 //   channelSplit    one system's channels — a Game Boy's 4 STEREO channels across the 8 outs, or a NES's
 //                   5 MONO core channels across outs 1..5.
 //   pinSplit        a NES's three 2A03 output pins (Pulse | TND | Expansion) across outs 1..3, mono.
 //   stereoPinSplit  the same three pins, one PAIR each (outs 1/2, 3/4, 5/6), mirrored L=R — so every pin
 //                   is a normal stereo track in a DAW that can only bus/FX whole pairs.
-// Both pin modes are NES-only; native falls back to Stereo for any other console.
+// Both pin modes are NES-only; buildGraph falls back to Stereo for any other console.
 export const AUDIO_ROUTING_VALUES = ["stereo", "twoPerInstance", "onePerInstance", "channelSplit", "pinSplit", "stereoPinSplit"] as const;
 export type AudioRouting = (typeof AUDIO_ROUTING_VALUES)[number];
 
@@ -142,11 +142,6 @@ export const LsdjSyncMode = {
 
 // --- native boundary encoders ----------------------------------------------
 
-/** The native AudioRouting integer for a routing value (index == enum ordinal). */
-export function audioRoutingToIndex(v: AudioRouting): number {
-  return AUDIO_ROUTING_VALUES.indexOf(v);
-}
-
 // Which fields of a native-facing role config are string enums, and their value tuples. Only the two
 // "system"-category roles cross to native's reflect-cpp structs (SameBoyRoleConfig / MesenNesRoleConfig);
 // lsdj-sync is a pure-TS DSP role, so its `mode` string never needs an int here.
diff --git a/packages/retroplug/testing/mockBackend.ts b/packages/retroplug/testing/mockBackend.ts
index 34241bfc..8ced70da 100644
--- a/packages/retroplug/testing/mockBackend.ts
+++ b/packages/retroplug/testing/mockBackend.ts
@@ -75,17 +75,14 @@ export class MockBackend implements Backend {
   readonly log: string[] = [];
 
   // --- emulator-lifecycle bookkeeping (mock-only) -------------------------
-  // (no id counter — TS owns ids and passes them to constructSystem.)
+  // (no id counter — TS owns ids and passes them to buildCore.)
   private systems = new Map<number, MockSystem>();
-  /** Every ConstructSpec passed to constructSystem, in order — lets tests assert
-   *  the CONCRETE paths TS resolved (savPath/statePath/replaceId). */
-  readonly constructCalls: ConstructSpec[] = [];
-  /** Every buildCore call, in order, with the id it built under. */
+  /** Every buildCore call, in order, with the id it built under — lets tests assert the CONCRETE paths
+   *  TS resolved (savPath/statePath) and the seeds it carried. Duplicate and reload are TS orchestration
+   *  over buildCore, so they land here too (with stateBytes/sramBytes), not in a dedicated log. */
   readonly buildCalls: (ConstructSpec & { id: number })[] = [];
   /** Every setGraph request, in order: the documents TS published, their slots and their tap re-arms. */
   readonly graphRequests: SetGraphRequest[] = [];
-  // NOTE: duplicate/reload no longer hit the backend — they are TS orchestration over constructSystem,
-  // so their effect shows up in constructCalls (with stateBytes/sramBytes + replaceId), not a dedicated log.
 
   // --- file-dialog bookkeeping (mock-only) --------------------------------
   /** Opts passed to each openFileBrowser call, in order — lets tests assert which
@@ -98,7 +95,6 @@ export class MockBackend implements Backend {
   readonly applySettingCalls: { id: number; key: string; value: number | boolean }[] = [];
   readonly applyRoleCalls: { id: number; kind: string; config: Record<string, unknown> }[] = [];
   readonly serialOutCaptureCalls: { id: number; on: boolean }[] = [];
-  readonly audioRoutingCalls: number[] = [];
   readonly pressButtonCalls: { id: number; button: number; down: boolean }[] = [];
   /** Control MIDI aimed at one system (backend.stageSystemMidi), in call order. */
   readonly stageSystemMidiCalls: { id: number; bytes: number[] }[] = [];
@@ -293,15 +289,6 @@ export class MockBackend implements Backend {
 
   // --- Emulator lifecycle -------------------------------------------------
 
-  constructSystem(spec: ConstructSpec, id: number): boolean {
-    this.log.push("constructSystem");
-    this.constructCalls.push(spec);
-    if (!this.buildable(spec)) return false;
-    if (spec.replaceId !== undefined) this.systems.delete(spec.replaceId); // swap in place
-    this.systems.set(id, mockSystem(spec)); // TS owns the id counter; the mock just records under the given id
-    return true;
-  }
-
   // A built core is live from the build on, as native's is (its snapshot slot is claimed at the build, so its
   // reads answer before any document places it).
   buildCore(spec: ConstructSpec, id: number): BuiltCore | null {
@@ -353,8 +340,8 @@ export class MockBackend implements Backend {
     return fmt === "unknown" ? sega : !sega || fmt === "sms" || fmt === "gg";
   }
 
-  // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram + constructSystem —
-  // the mock has no bespoke method; those store paths land as constructCalls (restoredFromBytes).
+  // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram + buildCore — the mock has
+  // no bespoke method; those store paths land as buildCalls (restoredFromBytes).
 
   readState(id: number): Uint8Array | null {
     this.log.push("readState");
@@ -621,11 +608,6 @@ export class MockBackend implements Backend {
     return { width, height, rgba };
   }
 
-  removeSystem(id: number): boolean {
-    this.log.push("removeSystem");
-    return this.systems.delete(id);
-  }
-
   openFileBrowser(opts: FileBrowserOpts): Promise<string | null> {
     this.log.push("openFileBrowser");
     this.fileBrowserCalls.push(opts);
@@ -650,12 +632,6 @@ export class MockBackend implements Backend {
     return true;
   }
 
-  setAudioRouting(mode: number): boolean {
-    this.log.push("setAudioRouting");
-    this.audioRoutingCalls.push(mode);
-    return mode >= 0 && mode <= 3; // 0 Stereo / 1 TwoPerInstance / 2 OnePerInstance / 3 ChannelSplit
-  }
-
   pressButton(id: number, button: number, down: boolean): boolean {
     this.log.push("pressButton");
     this.pressButtonCalls.push({ id, button, down });
````

- [ ] **Step 4: Build and verify everything**

Run: `cmake -S . -B build > /dev/null && cmake --build build -j$(nproc) && pnpm typecheck`
Expected: a clean build and `packages/retroplug typecheck: Done`.

Run: `build/bin/retroplug-engine-graph-test "[b2]" && build/bin/retroplug-audio-test "[channelsplit],[samplerate]"`
Expected: `All tests passed (70 assertions in 8 test cases)`, then `All tests passed (425 assertions in 11 test cases)`.

Run: `pnpm test:plugin && pnpm test && pnpm test:ui && pnpm test:node`
Expected: 10 binaries, 158 files, 37 UI files, `ℹ pass 12` / `ℹ fail 0`.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native`
Expected: `# 148 native test file(s) passed` (the two bench skips only).

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 24 native test file(s) passed`.

Run: `grep -rn "Translator\|translator()\|setHostShape\|constructSystem\|setAudioRouting(" packages/native/src packages/native/plugin packages/native/sdl packages/retroplug/src packages/retroplug/testing`
Expected: only `ProjectStore.setAudioRouting` (the store's own setting), `RisaSyncTranslator` (unrelated), a
historical comment in `systemsStore.ts` about the legacy native service, and `SameBoyBackend.hpp`'s comment,
which Task 8 updates.

- [ ] **Step 5: Commit**

```bash
git add -A packages/
git commit -m "refactor(engine): delete the translator and the structural RPCs TS no longer sends

constructSystem, removeSystem and setAudioRouting go, with the invoker's
structural half, native AudioRouting and HostShape, and the translator and its
tests; the NES split and sample-rate tests drive the engine with documents
spelled as buildGraph writes them."
```

---
### Task 7: The planner benchmark

How `setGraph` scales (design section 7.4): about 1,000 and 10,000 nodes, as a chain and as a wide fan, each
timed cold (every instance new) and for a one-node edit. Hidden from the normal run; it reports and does not
gate.

**Files:**
- Create: `packages/native/test/graph/PlannerBench.test.cpp`
- Modify: `packages/native/cmake/tests.cmake`

- [ ] **Step 1: Write it**

Create `packages/native/test/graph/PlannerBench.test.cpp`:

````cpp
// How setGraph scales (docs/modular-graph-phase-b2-design.md section 7.4): documents of about 1,000 and
// 10,000 nodes, as a chain (each node feeding the next) and as a wide fan (every node live at once, summed by
// a tree of adds), timed end to end (parse, plan, publish) for a cold plan, where every instance is new, and
// for a warm edit that changes one node and reuses the rest. It reports and does not gate; the numbers are
// recorded in the main design's section 12.
//
// Hidden from the normal run. Run it on its own: build/bin/retroplug-graph-test "[bench]"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GraphRig.hpp"

using rp::graph::test::GraphRig;

namespace {

std::string delay(std::size_t k, std::uint32_t maxSamples = 1) {
    return R"({"id":"d)" + std::to_string(k) + R"(","type":"rp.core.delay","settings":{"channels":1,"maxSamples":)" +
           std::to_string(maxSamples) + "}}";
}
std::string wire(const std::string& from, const std::string& to) {
    return R"({"from":")" + from + R"(","to":")" + to + "\"}";
}
std::string document(const std::vector<std::string>& nodes, const std::vector<std::string>& wires) {
    std::string s = R"({"nodes":[)";
    for (std::size_t i = 0; i < nodes.size(); ++i) s += (i ? "," : "") + nodes[i];
    s += R"(],"wires":[)";
    for (std::size_t i = 0; i < wires.size(); ++i) s += (i ? "," : "") + wires[i];
    return s + "]}";
}
const std::string kOut = R"({"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}})";

// n delays, each feeding the next; `edited` gives the last one another ring, so it alone rebuilds.
std::string chain(std::size_t n, bool edited) {
    std::vector<std::string> nodes, wires;
    for (std::size_t k = 0; k < n; ++k) nodes.push_back(delay(k, edited && k + 1 == n ? 2 : 1));
    for (std::size_t k = 1; k < n; ++k) wires.push_back(wire("d" + std::to_string(k - 1) + ".out", "d" + std::to_string(k) + ".in"));
    nodes.push_back(kOut);
    wires.push_back(wire("d" + std::to_string(n - 1) + ".out", "out.out.1"));
    return document(nodes, wires);
}

// n delays side by side, summed 16 at a time by a tree of adds into the host's one channel.
std::string fan(std::size_t n, bool edited) {
    std::vector<std::string> nodes, wires;
    std::vector<std::string> level;
    for (std::size_t k = 0; k < n; ++k) {
        nodes.push_back(delay(k, edited && k + 1 == n ? 2 : 1));
        level.push_back("d" + std::to_string(k) + ".out");
    }
    std::size_t adds = 0;
    while (level.size() > 1) {
        std::vector<std::string> next;
        for (std::size_t at = 0; at < level.size(); at += 16) {
            const std::size_t width = std::min<std::size_t>(16, level.size() - at);
            const std::string id = "a" + std::to_string(adds++);
            nodes.push_back(R"({"id":")" + id + R"(","type":"rp.core.add","settings":{"inputs":)" + std::to_string(width) + "}}");
            for (std::size_t i = 0; i < width; ++i) wires.push_back(wire(level[at + i], id + ".in" + std::to_string(i + 1)));
            next.push_back(id + ".out");
        }
        level = std::move(next);
    }
    nodes.push_back(kOut);
    wires.push_back(wire(level[0], "out.out.1"));
    return document(nodes, wires);
}

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST_CASE("setGraph scales with the document", "[.][bench]") {
    std::printf("\n%-6s %7s %9s %12s %12s\n", "shape", "nodes", "bytes", "cold ms", "edit ms");
    for (const char* shape : {"chain", "fan"}) {
        for (std::size_t n : {1000u, 10000u}) {
            const std::string cold = shape[0] == 'c' ? chain(n, false) : fan(n, false);
            const std::string edit = shape[0] == 'c' ? chain(n, true) : fan(n, true);
            GraphRig rig({48000.0, 1024, rp::graph::Target::Desktop}, 1);
            auto t0 = std::chrono::steady_clock::now();
            REQUIRE(rig.set(cold).ok);
            const double coldMs = msSince(t0);
            rig.run(1024);  // the plan runs; reclaim nothing yet
            t0 = std::chrono::steady_clock::now();
            REQUIRE(rig.set(edit).ok);
            const double editMs = msSince(t0);
            std::printf("%-6s %7zu %9zu %12.2f %12.2f\n", shape, rig.host->instanceCount(), edit.size(), coldMs, editMs);
        }
    }
}
````

````diff
diff --git a/packages/native/cmake/tests.cmake b/packages/native/cmake/tests.cmake
index 9c784b1f..31d77f26 100644
--- a/packages/native/cmake/tests.cmake
+++ b/packages/native/cmake/tests.cmake
@@ -191,6 +191,7 @@ retroplug_add_test_binary(
              test/graph/NoAlloc.test.cpp
              test/graph/SwapStress.test.cpp
              test/graph/HostCommands.test.cpp
+             test/graph/PlannerBench.test.cpp         # hidden: run with "[bench]"
     LIBS     retroplug-graph
              Threads::Threads                          # SwapStress runs a real audio thread
     DEFS     RP_GRAPH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/graph/golden")
````

- [ ] **Step 2: Run it**

Run: `cmake -S . -B build > /dev/null && cmake --build build --target retroplug-graph-test -j$(nproc) && build/bin/retroplug-graph-test "[bench]"`
Expected: a table like the prototype's (Release build, Ryzen 9 9950X):

```
shape    nodes     bytes      cold ms      edit ms
chain     1001    112760        10.82         9.23
chain    10001   1156760       171.66       159.64
fan       1069    119471        13.96        13.16
fan      10670   1224770       330.58       332.61
```

Run: `pnpm test:plugin graph`
Expected: the hidden case does not run; both graph binaries pass.

- [ ] **Step 3: Commit**

```bash
git add packages/native/test/graph/PlannerBench.test.cpp packages/native/cmake/tests.cmake
git commit -m "test(graph): a planner benchmark at 1,000 and 10,000 nodes"
```

Record the table Step 2 printed in the docs of Task 8 if it differs from the prototype's.

---
### Task 8: Docs

The engine as it runs with TS owning the graph, and what building it found: the spec docs that described the
translator (00, 01, 02, 03, 06, 10, 13 and the README), the main design (status, section 5.1's scale, 7.5, 8,
and 12 with the benchmark), B0's section 0 row for B2, B1's status, the B2 design's status and its section 10,
AGENTS.md's `retroplug-engine-graph-test` description, `docs/lsdj.md`'s pointer to the link groups, and a
code comment in `SameBoyBackend.hpp` that still named `constructSystem`.

**Files:** `AGENTS.md`, `docs/lsdj.md`, `docs/modular-graph-design.md`, `docs/modular-graph-phase-b0-design.md`, `docs/modular-graph-phase-b1-design.md`, `docs/modular-graph-phase-b2-design.md`, `spec/00-overview.md`, `spec/01-architecture.md`, `spec/02-native-host.md`, `spec/03-ts-layer.md`, `spec/06-build-test.md`, `spec/10-multichannel-audio-out.md`, `spec/13-subsystems.md`, `spec/README.md`, `packages/native/src/system/sameboy/SameBoyBackend.hpp`

- [ ] **Step 1: Apply the docs**

````diff
diff --git a/AGENTS.md b/AGENTS.md
index f695c25d..77eb7d10 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -37,8 +37,9 @@ The rules below are the parts that don't fit those.
   `-watcher-test` / `-dynparams-test` / `-midi-test` / `-launchpad-test` / `-lottie-test` /
   `-graph-test` / `-engine-graph-test`) as `Catch2::Catch2WithMain`. `retroplug-engine-graph-test`
   holds the engine's graph to the cores it wraps: each system node renders what its core renders
-  driven directly, bit for bit, and the translator's document for every routing mode and host shape
-  matches an explicit wiring table. `retroplug-dynparams-test` is the odd one out: it
+  driven directly, bit for bit, and the engine places, re-arms and frees what the graph documents TS
+  builds say (the wiring itself is `buildGraph`'s, held by `pnpm test graph` to explicit tables and to
+  the B1 translator's goldens). `retroplug-dynparams-test` is the odd one out: it
   compiles DPF's format-neutral core (`DistrhoPlugin.cpp`) plus its OWN `DistrhoPluginInfo.h`,
   building no plugin format at all, to drive `PluginExporter::reinitParameters` over a toy plugin.
 - **`deps/sameboy` is patched at configure — a dirty working tree there is
diff --git a/docs/lsdj.md b/docs/lsdj.md
index c7d68c90..00d1f674 100644
--- a/docs/lsdj.md
+++ b/docs/lsdj.md
@@ -323,7 +323,7 @@ The control is the half that matters: without it the positive test passes on any
 that happen to make noise.
 
 If link sync is genuinely broken, the follower's per-system RMS stays at 0 in
-the positive test - look at `linkGroups` in [Translator.cpp](../packages/native/src/host/graph/Translator.cpp)
+the positive test - look at `linkGroups` in [buildGraph.ts](../packages/retroplug/src/graph/buildGraph.ts)
 (which Game Boys share a cable), `set_peers` in [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)
 (the cable itself), and the `serialStart` / `serialEnd` callbacks in `SameBoySystem.cpp`. `renderAudioPerSystem`
 isolates each instance's audio (the canonical way to tell synced playback from a
diff --git a/docs/modular-graph-design.md b/docs/modular-graph-design.md
index 8115efd0..98904c98 100644
--- a/docs/modular-graph-design.md
+++ b/docs/modular-graph-design.md
@@ -1,6 +1,6 @@
 # Design: the modular node-graph engine
 
-**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phases B to E not yet built.
+**Date:** 2026-10-23 · **Branch:** `feature/modularize` · **Status:** Phase A built (`packages/native/src/graph/`, `packages/retroplug/src/graph/`); Phase B0 to B2 built (B3 and Phases C to E not yet built).
 
 RetroPlug's long-term direction is a modular system in the spirit of Reaktor: emulated consoles (and
 the real hardware they can drive) appear as nodes in a graph alongside DSP effects and routing nodes.
@@ -585,9 +585,12 @@ planner can batch consecutive script nodes into one call. Not built until measur
 }
 ```
 
-TS sends the **whole document** on every change with one RPC call, `setGraph(doc)`. Graphs are tens of
-nodes, so size is a non-issue, and whole-document sends keep planning deterministic; the planner diffs
-against the running graph by node id.
+TS sends the **whole document** on every change with one RPC call, `setGraph(doc)`. Whole-document sends
+keep planning deterministic, and the planner diffs against the running graph by node id. Today's projects
+are tens of nodes; the long-term goal is user-built synths of thousands, which §12 measures (at about 1,000
+nodes a structural edit plans in about 10 to 14 ms) and lists the levers for. A patch form of `setGraph`,
+applied to native's stored copy of the last document, would save the transfer and the parse but not the
+planning, which is where the time goes. Live params never send a document.
 
 `setGraph` returns `{ ok, errors[], layouts }`. Each error is `{ nodeId, port?, code, message }`;
 `layouts` are each instance's ports and params as its settings resolve them, so TS can show and check
@@ -903,8 +906,8 @@ single system, pins need a NES) so behaviour is unchanged.
 
 | Stays or arrives | Goes |
 |---|---|
-| `setGraph(doc)` | `constructSystem`, `removeSystem` |
-| `putResource(type, bytes)` -> handle, `releaseResource(handle)` | `setAudioRouting` |
+| `setGraph(doc)` (built in B2: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)) | `constructSystem`, `removeSystem` (gone in B2; building a core stays a native call, `buildCore`, until Phase C) |
+| `putResource(type, bytes)` -> handle, `releaseResource(handle)` | `setAudioRouting` (gone in B2) |
 | capability calls (memory, SRAM, state, ROM info) addressed by node id | `applyRoleConfig` |
 | `setParam(node, param, value)` | `setSerialOutCapture` |
 | snapshot reads (unchanged) | `dspSetSystems`, kernel loading |
@@ -1000,6 +1003,14 @@ doc's §0 has the table, and its §4 the requirements recording today's engine f
 B1 is built: [modular-graph-phase-b1-design.md](modular-graph-phase-b1-design.md), executed from
 [modular-graph-phase-b1-plan.md](modular-graph-phase-b1-plan.md).
 
+B2 is built: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md), executed from
+[modular-graph-phase-b2-plan.md](modular-graph-phase-b2-plan.md). TS owns the graph's structure:
+`buildGraph` writes the document, the stores send one per edit, and the translator, `removeSystem` and
+`setAudioRouting` are gone. Two things moved out of B2 as designed: the role-config commands
+(`applySystemSetting`, `applyRoleConfig`, `ConfigField`) stay until Phase C, where the live knobs become
+node params, and the kernel commands stay until B3 deletes the kernel. Building a core stays a native call
+(`buildCore`) until Phase C, when system nodes build their own from settings and resources.
+
 ### Phase C: nodes own their knowledge
 
 - Console-specific code moves into each node; every `dynamic_cast` in §6.5 goes.
@@ -1131,3 +1142,21 @@ here so it is not lost.
 - **Script-node call overhead** versus today's single kernel call; measured with the profiling harness
   before batching is built.
 - **Surround to the DAW** depends on DPF port groups (§3.6).
+- **Large graphs.** The goal past chiptune is user-built synths and music apps, so graphs of thousands of
+  nodes. Measured in B2 (`retroplug-graph-test "[bench]"`, a Release build on a Ryzen 9 9950X; cold plans
+  every instance anew, an edit changes one node of the same document):
+
+  | Shape | Nodes | Cold plan | One-node edit |
+  |---|---|---|---|
+  | chain | 1,000 | 11 ms | 9 ms |
+  | chain | 10,000 | 172 ms | 160 ms |
+  | wide fan | 1,000 | 14 ms | 13 ms |
+  | wide fan | 10,000 | 331 ms | 333 ms |
+
+  The cost grows faster than the graph (ten times the nodes, 16 to 24 times the time), and an edit costs
+  what a cold plan does, so planning dominates, not instance creation or the document. The levers, in the
+  order they would bite: every plan allocates fresh audio buffers (reused by lifetime within a plan, never
+  across plans: pool them); the output-lifetime pass scans every wire per output port (index the wires once
+  per plan); every node's settings are re-parsed per edit, for its layout and the reuse compare (cache by
+  settings text); and only then the document itself (a patch form of `setGraph`). None is needed at today's
+  sizes; they are recorded here so the graph-editing project starts from them.
diff --git a/docs/modular-graph-phase-b0-design.md b/docs/modular-graph-phase-b0-design.md
index 1aef3485..81efc78c 100644
--- a/docs/modular-graph-phase-b0-design.md
+++ b/docs/modular-graph-phase-b0-design.md
@@ -1,7 +1,7 @@
 # Design: Phase B0, the parity baseline
 
 **Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** built
-([modular-graph-phase-b0-plan.md](modular-graph-phase-b0-plan.md)); B1 to B3 not yet built.
+([modular-graph-phase-b0-plan.md](modular-graph-phase-b0-plan.md)); B1 and B2 built, B3 not yet built.
 
 Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph,
 and its parity target is bit-identical audio in every routing mode, MIDI routing mode and sync mode.
@@ -19,7 +19,7 @@ Phase B is four sub-projects. Each lands working and is checked bit for bit agai
 |---|---|---|---|
 | **B0. Parity baseline** (this doc) | Determinism switch, `renderBlocks`, a store-driven case matrix with committed hashes of today's engine; three bugs fixed first (§2.5) | Only the fixes | Nothing |
 | **B1. Native runs on the executor** | Cores as system nodes, linked GBs as a coupled group, the kernel as one node, today's routing modes as wiring a native translator builds from the existing calls; the two open decisions of §8 settled. TS untouched | Nothing | The routers, `BlockRunner`, `LinkGroup`, `AudioRouting` |
-| **B2. TS owns the graph** | `buildGraph`, the stores call `setGraph`, the Backend surface switch (§7.5) | Nothing | The B1 translator; the construct / remove / routing / role-config / kernel commands |
+| **B2. TS owns the graph** | `buildGraph`, the stores call `setGraph`, the Backend surface switch (§7.5) | Nothing | The B1 translator; the remove and routing RPCs, with construct becoming `buildCore` (the role-config commands moved to Phase C and the kernel commands to B3: [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md) §0) |
 | **B3. The kernel dissolves** | The script-node runtime, the DSP roles as TS nodes, MIDI routing as a node, marker roles as extensions | Nothing | The DSP kernel and its bundle |
 
 B1 comes before B2 so the riskiest change, the executor swap (float order, event order, block
diff --git a/docs/modular-graph-phase-b1-design.md b/docs/modular-graph-phase-b1-design.md
index ab3033cb..0a417041 100644
--- a/docs/modular-graph-phase-b1-design.md
+++ b/docs/modular-graph-phase-b1-design.md
@@ -1,7 +1,8 @@
 # Design: Phase B1, native runs on the executor
 
 **Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** built
-([modular-graph-phase-b1-plan.md](modular-graph-phase-b1-plan.md)); B2 and B3 not yet built.
+([modular-graph-phase-b1-plan.md](modular-graph-phase-b1-plan.md)); B2 built
+([modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md)), B3 not yet built.
 
 Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph. B0
 ([modular-graph-phase-b0-design.md](modular-graph-phase-b0-design.md)) recorded today's engine bit for
@@ -447,7 +448,7 @@ Then, behind the switch:
   (`setChannelExportMode`, SameBoy's restart and lazy buffer growth, the payload frees), or the growth of a
   linked Game Boy's unread serial queue.
 - How B2 hands cores to TS-built documents: B1's core token is a translator detail, and B2 decides whether
-  it survives.
+  it survives. (It did not: a core is named by its system id, [modular-graph-phase-b2-design.md](modular-graph-phase-b2-design.md) §2.5.)
 
 ## 11. Found while building
 
diff --git a/docs/modular-graph-phase-b2-design.md b/docs/modular-graph-phase-b2-design.md
index 21376fe8..3ff01140 100644
--- a/docs/modular-graph-phase-b2-design.md
+++ b/docs/modular-graph-phase-b2-design.md
@@ -1,6 +1,7 @@
 # Design: Phase B2, TS owns the graph
 
-**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** designed; not yet built.
+**Date:** 2026-10-24 · **Branch:** `feature/modularize` · **Status:** built
+([modular-graph-phase-b2-plan.md](modular-graph-phase-b2-plan.md)); B3 not yet built.
 
 Phase B of [the modular graph design](modular-graph-design.md) (§8) moves the engine onto the graph. B1
 ([modular-graph-phase-b1-design.md](modular-graph-phase-b1-design.md)) made `Engine::processBlock` run a
@@ -98,7 +99,8 @@ The `SystemsStore` and `ProjectStore` derive the model from what they already ho
 order (append, replace in place, remove shifts later ones down, which is exactly the translator's slot
 model), each entry's built facts (§2.5), each Game Boy's `linkGroupId`, and `projectSettings.audioRouting`.
 They call `EngineGraph.publish` once at the end of each edit, before the `onSystemsChange` hook that pushes
-the kernel structure. §3 lists the edits.
+the kernel structure. §3 lists the edits. A `SystemsStore` used without a `ProjectStore` publishes its systems
+itself, through an `EngineGraph` of its own, at the default stereo routing.
 
 Each entry gains a transient `built: { type, exportMode, linkGroup }`, filled from `buildCore`'s answer and
 never persisted. A live link-group edit (`setRoleConfig` on the `sameboy` role) updates `built.linkGroup` to
@@ -112,18 +114,24 @@ a system list with B1's model rules and publishes after each call through its ow
 
 ```ts
 class DirectGraph {
-  constructor(backend: ControlPlaneBackend);
-  add(spec: ConstructSpec, id: number): boolean;                  // appends
+  constructor(backend: DirectBackend, graph?: EngineGraph);         // share a session's graph to mix with its stores
+  add(spec: ConstructSpec, id: number): boolean;                   // appends
   replace(oldId: number, spec: ConstructSpec, id: number): boolean; // keeps oldId's slot
-  remove(id: number): boolean;                                     // later slots shift down
+  remove(id: number): boolean;                                      // later slots shift down
   setRouting(routing: AudioRouting): boolean;
-  setLinkGroup(id: number, group: number): boolean;                // also sends the field to the core
-  readonly graph: EngineGraph;                                     // for setShape / withShape
+  applyRoleConfig(id: number, kind: string, config: Record<string, unknown>): boolean; // a sameboy link group rewires
+  ids(): number[];
+  readonly graph: EngineGraph;                                      // for setShape / withShape
 }
+directGraph(): DirectGraph;                                         // the process's one, over the real backend
 ```
 
-Every call but `setLinkGroup` re-arms the tap, as its native counterpart did. A file uses either the stores
-or a `DirectGraph`, never both: two publishers would overwrite each other's documents.
+Every call but `applyRoleConfig` re-arms the tap, as its native counterpart did (a remove or replace of an id
+already gone included). `applyRoleConfig` forwards the config to the core and, for a `sameboy` config, takes
+its link group as native decodes it (absent is 0) and republishes. The callers keep one `DirectGraph` per
+process (`directGraph()`), since the probe helper and the file using it would otherwise drop each other's
+systems; a file that also uses a session's stores shares the session's `EngineGraph`, so each publisher sees
+the documents the other sent.
 
 ### 2.5 Native: `buildCore` and `setGraph`
 
@@ -183,8 +191,10 @@ command after its plan. The live call remains for an `lsdj-sync` mode change on
 - **A load publishes its clear first**, then the new systems. Today the old cores are freed before the new
   ones are built; doing both in one plan would hold 2N cores at once, and the snapshot registry has 64
   slots, which 32 systems would fill.
-- **The quiet adopt stays quiet.** `adopt` builds without publishing; the load's own publish places every
-  adopted system at once, and the serial flags are already inside their cores.
+- **`adopt` places its core, as it always did** (before B2 its `constructSystem` placed the core at once).
+  It publishes but stays quiet otherwise (no dirty, no kernel push). A project load holds those publishes
+  back and places every adopted system with one publish of the loaded routing; the serial flags are
+  already inside their cores.
 - **`inheritLinkGroup`** (Add Instance, Duplicate) is two `setRoleConfig` edits, each with its own link plan,
   as today.
 
@@ -255,8 +265,10 @@ record, every read, `pressButton`, `stageSystemMidi` and `writeRam`.
 **Changes:**
 
 - `Engine::setSampleRate` re-rates the cores of the running plan's table, in slot order;
-- the native tests that drive structure through the invoker (`test/audio/EngineInvoker.test.cpp`,
-  `NesSplitRouting.test.cpp`) drive it through `setGraph` requests instead;
+- the native tests that drove structure through the invoker drive it through `setGraph` requests, with
+  documents spelled as `buildGraph` writes them (`test/engine-graph/GraphDocs.hpp`): `NesSplitRouting`
+  (the tap's arms, restores and lane packing, now one request each) and `EngineSampleRate`; the invoker
+  test's host-shape case became `EngineGraph.test.cpp`'s render-guard case;
 - the mock backend records `buildCore` calls (the recorder renamed in the 25 `pnpm test` files that read it)
   and every `setGraph` request, so pure tests can assert the wiring;
 - `buildCore` and `setGraph` sit on the control-plane facet `constructSystem` lives on today.
@@ -354,3 +366,27 @@ Each step its own commit, with parity unchanged:
 - The four scale levers of §7.4: recorded, built when a large graph needs them.
 - Rolling the store back when a document is refused: as today, the store keeps its model and the refusal is
   logged.
+
+## 10. Found while building
+
+- **A bare `SystemsStore` drove native structure.** Before B2 every store edit reached native through its own
+  calls, so 24 `pnpm test` files and many native tests build systems with a `SystemsStore` and no
+  `ProjectStore`. Only the `ProjectStore` installed the publish hook, so those systems were built and never
+  placed. A `SystemsStore` on its own now publishes at the default stereo routing (§2.3).
+- **`adopt` placed its core.** The design kept it quiet, but before B2 its `constructSystem` placed the core
+  at once, and twelve callers (the render library, two CLI sessions, native tests and authoring scripts)
+  relied on it. It publishes again, and a project load holds the publishes back to keep one plan (§3).
+- **Two cores under one id.** The second test in `dsp-risa-sync` and in `dsp-risa-sync-locate` built a core
+  under id 1 while the first test's was still live, which before B2 quietly ran both under one id (each
+  claimed a snapshot slot under it). `buildCore` refuses a live id, so the tests now remove their systems,
+  as AGENTS.md asks of every test file.
+- **A raw call the migration missed.** `dsp-sms-sync` removed its system through a second backend named
+  `be2`, which the mechanical move to `DirectGraph` did not match; the old call reached the translator, so
+  the system stayed in the document and played into the next case.
+- **The mock had to answer before a core is placed.** Native claims a built core's snapshot slot at the
+  build, so its reads answer at once; the mock now treats a built core as live until a published document
+  drops it.
+- **Planning dominates at scale.** The benchmark (§7.4) measured about 10 to 14 ms per edit at 1,000 nodes
+  and 160 to 330 ms at 10,000, with a one-node edit costing what a cold plan does. The numbers and the
+  levers are in the main design's §12.
+
diff --git a/packages/native/src/system/sameboy/SameBoyBackend.hpp b/packages/native/src/system/sameboy/SameBoyBackend.hpp
index 66cf85af..0857fd92 100644
--- a/packages/native/src/system/sameboy/SameBoyBackend.hpp
+++ b/packages/native/src/system/sameboy/SameBoyBackend.hpp
@@ -38,7 +38,7 @@ public:
 
     // The shared ctor + seed-order + onActivate + enableStateSnapshot sequence. SRAM/savestate go
     // into cfg BEFORE construct — a live core restores them inside onActivate. cfg + romBytes are
-    // consumed. (Reload/duplicate are TS orchestration over constructSystem now, not a native reuse.)
+    // consumed. (Reload/duplicate are TS orchestration over buildCore now, not a native reuse.)
     static std::unique_ptr<SameBoySystem> buildSameBoy(SystemId id, SameBoyConfig cfg,
                                                        std::vector<std::uint8_t> romBytes,
                                                        double sampleRate);
diff --git a/spec/00-overview.md b/spec/00-overview.md
index e3d68cc8..3f14164e 100644
--- a/spec/00-overview.md
+++ b/spec/00-overview.md
@@ -93,8 +93,8 @@ system routed to one pair by its audio routing.
 
 The LSDj-persistence path is TS-owned. `retroplug-core` is a neutral static lib in the
 native tree; `retroplug-backend` wraps it with the `Engine` (which runs the systems on
-`retroplug-graph`, through its own nodes and translator in `src/host/graph/`) + RPC
-surface, and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.
+`retroplug-graph`, through its own nodes in `src/host/graph/`, in the graph documents the TS
+stores build) + RPC surface, and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.
 
 ## Component map
 
@@ -105,7 +105,7 @@ surface, and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.
  ┌───────────────────────────────────────────────────────────────┐
  │ control plane  (main / UI thread)                             │
  │   control-plane QuickJS runtime (txiki)                       │
- │   TS stores ─► Backend (backend.ts) ─► __rpcSend              │
+ │   TS stores (+ EngineGraph) ─► Backend ─► __rpcSend           │
  └──────────────────────────────┬────────────────────────────────┘
                                 │  in-process RPC (QuickJSCodec)
                                 ▼
@@ -113,8 +113,8 @@ surface, and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.
  │ Backend RPC facets  (C++ services, per RPC server)            │
  │   HostRpcService · EngineRpcService · AudioDriverRpcService · DebugRpcService │
  └──────┬───────────────────────────────────────────▲────────────┘
-        │ QueuedInvoker: structure → translator →    │ SnapshotRegistry (reads)
-        │ plan; edits → commands; ONE ring           │ + release ring (replaced plans)
+        │ setGraph: the document → a plan;           │ SnapshotRegistry (reads)
+        │ edits → commands; ONE ring                 │ + release ring (replaced plans)
         ▼                                            │
  ┌───────────────────────────────────────────────────────────────┐
  │ audio thread  (DPF run() / spawned audioThread_)              │
@@ -124,8 +124,8 @@ surface, and every host (plugin, `retroplug-host`, `retroplug-cli`) links that.
 ```
 
 The control plane and the audio thread **never touch each other's memory
-directly**: every control→audio change goes through the one command ring (a plan the
-translator built from a structural edit, or a command), and every audio→control
+directly**: every control→audio change goes through the one command ring (a plan of a
+graph document the stores built, or a command), and every audio→control
 observation goes through the snapshot registry (reads) plus the release ring (a
 replaced plan, and the cores only it held, handed back to be freed). Native runs
 **two QuickJS runtimes**, never shared: the control-plane one (txiki, full host) and
@@ -137,7 +137,7 @@ the bare DSP-kernel one (byte-only, on the audio thread). All of this is specifi
 | Doc | Scope |
 |---|---|
 | [01-architecture.md](01-architecture.md) | The canonical runtime architecture: three hosts over one Backend RPC surface (the capability facets); control plane vs audio thread; the command ring, the graph the engine runs on, snapshot registry, release ring; the two QuickJS runtimes; threading/ownership invariants. |
-| [02-native-host.md](02-native-host.md) | `packages/native/`: the full RPC surface, `Engine`/`EngineInvoker` and the translator, `SnapshotRegistry` internals, `SystemFactory` + backends, the DSP runtime and script compiler. |
+| [02-native-host.md](02-native-host.md) | `packages/native/`: the full RPC surface, `Engine`/`EngineInvoker` and its graph requests, `SnapshotRegistry` internals, `SystemFactory` + backends, the DSP runtime and script compiler. |
 | [03-ts-layer.md](03-ts-layer.md) | `packages/retroplug/`: the `Backend` interface, the stores, the control-plane composition, the `__rp_*` UI↔native hooks, and the React/LVGL UI. |
 | [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model: system-role vs feature-role, the role registry, and the DSP kernel's per-system pipelines and byte-sink ABI. |
 | [05-data-persistence.md](05-data-persistence.md) | The project model + `.rplg`, DPF get/setState + autoload, config models, forward-tolerant reads + version stamps, the LSDj sav codec, SRAM auto-save. |
@@ -150,5 +150,5 @@ the bare DSP-kernel one (byte-only, on the audio thread). All of this is specifi
 - [`packages/retroplug/src/backend.ts`](../packages/retroplug/src/backend.ts) — the TypeScript side of that contract.
 - [`packages/retroplug/src/realBackend.ts`](../packages/retroplug/src/realBackend.ts) — the adapter that binds `Symbol.for("plugin").__rpcSend`.
 - [`packages/native/src/host/engine/Engine.hpp`](../packages/native/src/host/engine/Engine.hpp): the single-threaded owner of the graph the cores run on, the DSP kernel and the snapshot registry.
-- [`packages/native/src/host/graph/Translator.hpp`](../packages/native/src/host/graph/Translator.hpp): the control-side model that turns every structural edit into a graph document.
+- [`packages/retroplug/src/graph/buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts): the engine's graph document, built in TS from the stores' systems, routing and host shape.
 - [`packages/retroplug/src/dspKernel.ts`](../packages/retroplug/src/dspKernel.ts) — the TS DSP role kernel run as bytecode on the audio thread.
diff --git a/spec/01-architecture.md b/spec/01-architecture.md
index ba52d1a1..f3a45cb8 100644
--- a/spec/01-architecture.md
+++ b/spec/01-architecture.md
@@ -90,7 +90,7 @@ goes through the command ring; all audio→control observation goes through the
 | | **Control plane** (main / UI thread) | **Audio thread** (DPF `run()` / spawned `audioThread_`) |
 |---|---|---|
 | QuickJS runtime | control-plane txiki host ([`TjsHostRuntime`](../packages/native/plugin/PluginDSP.cpp)) | bare DSP context ([`DspRuntime`](../packages/native/src/host/dsp/DspRuntime.hpp), owned by `Engine::dsp_`) |
-| Owns | the RPC services + their facets, the TS stores, the editor, the translator's model | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp): the running plan (its system nodes, each owning its core) + the DSP kernel, **while running** |
+| Owns | the RPC services + their facets, the TS stores and the graph they publish (`EngineGraph`), the editor | the live [`Engine`](../packages/native/src/host/engine/Engine.hpp): the running plan (its system nodes, each owning its core) + the DSP kernel, **while running** |
 | Does | issues mutation *requests*; plans structure into graph documents; reads snapshots by id; frees replaced plans and the cores only they held | drains the command ring, adopting plans in order; runs the plan each block; publishes snapshots |
 | Never | dereferences a live `SystemBase` while audio runs; frees a core a running plan holds | builds or frees a core or a plan, or blocks |
 
@@ -106,7 +106,7 @@ and therefore who drains the command ring.
 
 ```
                          CONTROL PLANE  (main / UI thread)
-            txiki QuickJS · RPC facets · TS stores · editor · translator
+            txiki QuickJS · RPC facets · TS stores + EngineGraph · editor
                          │                    ▲                    ▲
    push plan / command   │                    │ read by id         │ free the replaced plan
                          ▼                    │ (owned copy)       │ (+ cores only it held)
@@ -144,13 +144,16 @@ Every control-plane edit goes through the one
 ([modular-graph-phase-b1-design.md](../docs/modular-graph-phase-b1-design.md) section 5). The
 invoker sends an edit one of two ways:
 
-- **Structure** (add / replace / remove a system, the routing mode, a Game Boy's link group, the
-  host's output shape) goes to the Engine's
-  [`Translator`](../packages/native/src/host/graph/Translator.hpp) on the control thread. The
-  translator updates its model (the ordered system list, the link groups, the routing mode, the host
-  shape), builds a graph document and its `SystemTable`, and calls `GraphHost::setGraph`, which plans
-  the document on the control thread (every layout checked before any instance is created, every new
-  instance created and activated) and pushes the plan onto the ring as a plan swap.
+- **Structure** (which systems run, in what order and wired how, a Game Boy's link cable, the host's
+  output shape) is a graph document TS builds
+  ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)). The stores call
+  [`EngineGraph.publish`](../packages/retroplug/src/engineGraph.ts) once at the end of every
+  structural edit; it builds the document with
+  [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) and sends it with the `setGraph` RPC.
+  `Engine::setGraph` builds the plan's `SystemTable` from the request's slots and the cores `buildCore`
+  parked, and calls `GraphHost::setGraph`, which plans the document on the control thread (every
+  layout checked before any instance is created, every new instance created and activated) and pushes
+  the plan onto the ring as a plan swap.
 - **Everything else** (load kernel bytecode, set the kernel's systems JSON, stage MIDI, controller
   MIDI or one system's MIDI, set bpm / transport / ppq, apply a config field, press a button, write
   RAM) is packed into a POD [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and
@@ -174,15 +177,16 @@ The only variable is **who drains the ring**, decided by `audioThreadOwns_`:
 - **Quiescent (control thread owns the Engine):** every push flushes **inline**: the GraphHost
   drains and reclaims right after the push, so a plan is adopted and a command applied before the
   call returns. The same thread is both producer and consumer, so the SPSC invariant holds
-  trivially, and a quiescent `removeSystem` frees the core synchronously (the plan it replaced is
-  reclaimed in the same flush), exactly as a direct call would.
+  trivially, and a quiescent remove (a document without the system) frees the core synchronously (the
+  plan it replaced is reclaimed in the same flush), exactly as a direct call would.
 
 The key consequence: because the quiescent path flushes on **every** push, **the ring is empty
 at the moment ownership is handed to the audio thread**. That is what makes the handoff clean:
-the plugin's `activate()` / the test host's `startAudio` set the host shape (the plugin its block
-size too) and then just set the bit ([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp),
+the plugin's `activate()` sets its block size and the test host's `startAudio` nothing at all, and
+then each just sets the bit ([`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp),
 [`AudioDriverRpcService.cpp`](../packages/native/src/host/rpc/AudioDriverRpcService.cpp)), with no
-ring to flush.
+ring to flush. The host shape is the control plane's: TS built the running graph for the host's
+outputs before the handoff.
 
 ### Producers and the drain
 
@@ -193,15 +197,16 @@ leak). Heavy commands carry an **owning heap payload**: `setSystems` / `loadKern
 a `stageMidi` / `stageSystemMidi` longer than 4 bytes, box a `new`-ed buffer (deleted after apply or,
 if the push fails, immediately).
 
-Structural calls never ride the ring as commands. `adoptSystem` / `replaceSystem` / `removeSystem` /
-`setAudioRouting` go to the translator, and the plan's swap command carries a `SyncTap` trailer
-(dispatched in the same pop, right after the plan is adopted), which arms or restores the NES channel tap
-against the plan just adopted
+Structure never rides the ring as a command. A `setGraph` request carries a `tap` when its edit
+re-arms the NES channel tap (every edit that builds or drops a core, and every routing change, even to
+the mode already set), and the plan's swap command carries it as a `SyncTap` trailer (dispatched in the
+same pop, right after the plan is adopted), which restores whatever is armed and then arms the tap the
+request names against the plan just adopted
 ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4). The `LinkGroup` config field
-goes both ways: the field reaches the core as a command, and the translator rewires the cable.
-`setHostShape` goes to the translator alone, and answers whether its shape planned. A change that does
-not plan (a refused document, or a full ring) leaves the translator's model as it was, and a core that
-was handed in is freed with its registry slot.
+goes both ways: the field reaches the core as a command, and the store republishes the graph with the
+cable. A document that does not plan (refused, or a full ring) changes nothing: the store keeps its
+model, and a core parked for it waits for the next document that plans, which frees it unless it
+places it.
 
 The handler (`Engine::onHostCommand` → `applyCommand`) switches on `DspCommand::Kind`, applies each
 command and deletes its owning payload. The block drivers (the plugin's `run()`, the test host's
@@ -278,14 +283,15 @@ A core's life, end to end:
 
 | Direction | Trigger | Mechanism |
 |---|---|---|
-| **build → park** | control builds and activates a core (`factory.build`), claims its registry slot, hands it to the invoker | `Translator::add` / `replace` parks it under a fresh token and puts `{ "core": <token> }` in its node's settings |
+| **build → park** | `buildCore` builds and activates a core (`factory.build`) and claims its registry slot | `Engine::parkCore` parks it under its system id; the document TS sends names it as `{ "core": <id> }` in its node's settings |
 | **plan → take** | `setGraph` plans the document on the control thread | the new node's first `activate` takes the core through the host service `rp.host.cores/1` and owns it from then on |
 | **adopt → hand back** | the audio thread drains the plan swap | the replaced plan is pushed onto the release ring, the last one of a drain only after every command of that drain is applied |
 | **reclaim → free** | the control thread reclaims | `reclaim()` deletes each released plan; a node only that plan held destroys its core: `registry.release(id)` first, then `delete` |
 
-A token never repeats, so even a reused system id with a new core plans as a new instance. A core
-no node ever took (a refused plan, or a replace whose target has gone) is freed by the translator
-itself, releasing its slot the same way.
+TS never reuses a system id, and `buildCore` refuses an id that is still live (parked, or held by a node
+of any plan not yet reclaimed), so a new core always plans as a new instance. A core no published
+document placed is freed by the engine once a document plans without it, releasing its slot the same
+way.
 
 `reclaim` runs at every `setGraph`, in every inline flush (quiescent), and at the hosts' stop and
 deactivate points; it returns the number of plans freed and is exposed on the wire as
@@ -388,11 +394,12 @@ lost mutation.
    system-addressed byte sinks. Its runtime is separate from the control-plane runtime and is
    re-anchored to the calling thread on every entry.
 
-8. **TypeScript owns identity and orchestration; native never mints or decides.** System ids are
-   allocated by TS and passed into `constructSystem`; native returns "did it build", never an id
-   ([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)).
-   Duplicate and reload are TS orchestration over `constructSystem`-with-state plus registry reads,
-   not native methods.
+8. **TypeScript owns identity, orchestration and structure; native never mints or decides.** System
+   ids are allocated by TS and passed into `buildCore`; native returns what it built, never an id
+   ([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)). Which systems
+   run, in what order and wired how is the graph document TS builds and sends with `setGraph`.
+   Duplicate, reload and remove are TS orchestration over `buildCore`-with-state, registry reads and
+   the documents the stores publish, not native methods.
    Cores are built bare (no default roles baked into C++); feature behaviour lives in the TS kernel.
 
 ### Not yet built / deferred
@@ -411,13 +418,13 @@ lost mutation.
 | File | Role |
 |---|---|
 | [`BackendRpcRegistration.hpp`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp) | mounts the capability facets onto an RPC server over the shared service instances |
-| [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker`: the one mutation path (structure to the translator, every other edit onto the ring), the `SyncTap` push, `audioThreadOwns_` |
+| [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp) | `QueuedInvoker`: every edit but structure onto the ring, `audioThreadOwns_` |
 | [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp) | the POD command the ring carries as an opaque host command |
 | [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp) | the one command ring, the release ring of plans, `setGraph` / `drain` / `run` / `reclaim` |
-| [`Translator.hpp`](../packages/native/src/host/graph/Translator.hpp) / [`.cpp`](../packages/native/src/host/graph/Translator.cpp) | the control-side model: every structural edit as a graph document + its `SystemTable` |
+| [`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts) / [`engineGraph.ts`](../packages/retroplug/src/engineGraph.ts) | the engine's graph as TS builds it from the stores, and the one publisher of it per context |
 | [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) / [`KernelNode.cpp`](../packages/native/src/host/graph/KernelNode.cpp) / [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp) | the cores as nodes (each owning its core), the kernel stage as a node, and the host services they ask for |
 | [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) | the id-keyed read door; claim/publishSystem/read/release |
-| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph, the DSP kernel and the registry; `processBlock`, the command handler, `SyncTap` |
+| [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp) | the single-threaded owner of the graph, the DSP kernel and the registry; `parkCore` / `setGraph`, `processBlock`, the command handler, `SyncTap` |
 | [`DspRuntime.hpp`](../packages/native/src/host/dsp/DspRuntime.hpp) / [`.cpp`](../packages/native/src/host/dsp/DspRuntime.cpp) | the bare DSP QuickJS context + byte-sink thunks |
 | [`PluginDSP.cpp`](../packages/native/plugin/PluginDSP.cpp) | plugin control-plane bring-up + the `run()` block driver |
 | [`PluginShared.hpp`](../packages/native/plugin/PluginShared.hpp) | the in-process editor↔control-plane handoff |
diff --git a/spec/02-native-host.md b/spec/02-native-host.md
index 4673be9f..6f526855 100644
--- a/spec/02-native-host.md
+++ b/spec/02-native-host.md
@@ -9,7 +9,7 @@ native makes no policy decisions.
 runtime concepts this doc leans on — the three hosts over one Backend RPC surface (its
 capability facets), the control-plane / audio-thread split, the command ring, the graph the
 engine runs on, the snapshot read door, and the release ring. This doc references those by name and
-goes deeper on the C++ mechanics: the method tables, the `Engine`'s block, the translator, the
+goes deeper on the C++ mechanics: the method tables, the `Engine`'s block, its graph requests, the
 `QueuedInvoker` internals, the `SnapshotRegistry` slot machinery, the core backends, and the bare
 DSP runtime.
 
@@ -133,8 +133,8 @@ Every mutation here routes through the `QueuedInvoker`; every read routes throug
 
 | Method | Signature | Behaviour |
 |---|---|---|
-| `constructSystem` | `(BackendConstructSpec) → bool` | TS-owned `spec.id`; `factory_.build` → `registry().claim` → `invoker_.replaceSystem`/`adoptSystem` (to the translator). Returns "did it build" |
-| `removeSystem` | `(uint32 id) → bool` | `invoker_.removeSystem` (to the translator); no threading branch |
+| `buildCore` | `(BackendConstructSpec) → optional<RpcBuiltCore>` | TS-owned `spec.id`; `factory_.build` → `registry().claim` → `Engine::parkCore`. Returns the core's node type, export mode and link group, or null (below) |
+| `setGraph` | `(RpcSetGraphRequest) → RpcSetGraphResult` | a graph document TS built, its slots and its tap re-arm → `Engine::setGraph` (below) |
 | `applySystemSetting` | `(uint32 id, string key, double value) → bool` | `gainDb` / `reloadOnRomChange` → a `ConfigField` |
 | `applyRoleConfig` | `(uint32 id, string kind, string config) → bool` | `kind=="sameboy"` only; decodes the whole role config, pushes Model/Highpass/LinkGroup/FastBoot |
 | `readState` | `(uint32 id) → optional<Bytestring>` | published savestate from the registry |
@@ -145,17 +145,17 @@ Every mutation here routes through the `QueuedInvoker`; every read routes throug
 | `dspLoadKernel` | `(vector<uint8> bytecode) → bool` | `invoker_.loadKernel` (hot-swap) |
 | `dspSetSystems` | `(string json) → bool` | `invoker_.setSystems` |
 | `pressButton` | `(uint32 id, uint32 button, bool down) → bool` | joypad transition → the addressed core |
-| `renderAudio` | `(double ms) → Bytestring` | synchronous pull render, interleaved f32 L/R, in the host shape `Multi(2)`; empty while the audio thread owns the engine (as are `renderAudioPerSystem` / `renderAudioPerChannel`, which set `PerSystem` / `PerChannel`) |
+| `renderAudio` | `(double ms) → Bytestring` | synchronous pull render, interleaved f32 L/R, into a graph TS built for 2 host channels; empty while the audio thread owns the engine or the running graph writes another width (as are `renderAudioPerSystem` / `renderAudioPerChannel`, for 2 channels per system or stream) |
 | `setTransport` / `setBpm` | `→ bool` | queued transport ops |
-| `setAudioRouting` | `(uint32 mode) → bool` | gated `mode ≤ 5` (Stereo / TwoPerInstance / OnePerInstance / ChannelSplit / PinSplit / StereoPinSplit); the translator rewires |
 | `stageMidiIn` | `(vector<uint8> bytes) → bool` | ≤ 4 bytes (one MIDI message) |
 
-`duplicate` and `reload` are **not** native methods — they are TS orchestration over
-`constructSystem`-with-state plus the registry reads
+`duplicate`, `reload` and `remove` are **not** native methods: they are TS orchestration over
+`buildCore`-with-state, the registry reads and the documents the stores publish
 ([`EngineRpcService.cpp`](../packages/native/src/host/rpc/EngineRpcService.cpp)):
 duplicate pulls a source savestate and builds a seeded core; reload pulls SRAM and cold-boots
-the ROM with `replaceId`. This is the thesis in miniature — native offers the primitives, TS
-composes the meaning.
+the ROM into the same slot; remove publishes a document without the system. Routing is not a
+native call either: it is the wiring of the document. This is the thesis in miniature: native
+offers the primitives, TS composes the meaning.
 
 ### `AudioDriverRpcService` — background audio thread (test host only)
 
@@ -208,8 +208,8 @@ It owns the DSP kernel (`DspRuntime`) and its staging vectors (`pendingMidi_`,
 `pendingControllerMidi_`, `pendingSerialOut_`), the snapshot registry, the transport fields, and the
 graph the systems run on: a `NodeRegistry` holding the node packages (`rp.core`, `rp.host`,
 `rp.sameboy`, `rp.mesen`, `rp.dsp`), a `ResourceTable`, the
-[`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, and the
-[`Translator`](../packages/native/src/host/graph/Translator.hpp) that plans structure. Every op runs
+[`GraphHost`](../packages/native/src/graph/GraphHost.hpp) running the plan, the cores `buildCore`
+parked until a document places them, and every live core by id. Every op runs
 "now, on the calling thread"; it holds **no atomics**: transport and the ppq clock are plain members
 mutated only by whichever thread owns it (the pull path or the audio loop, never concurrently). A
 removed or displaced core is never freed on a hot path: it leaves with the plan that last ran it.
@@ -253,37 +253,47 @@ Live config edits land in `Engine::applyConfigField`, dispatched by the core's c
 triggers `restartEmulator()` in place, and the cable needs no rewiring because `linkPeers_` holds
 the systems, which survive it; an unchanged one no-ops, avoiding a spurious restart that would nuke
 a restored savestate). It applies at drain against the plan current at that point, finding the core
-by id in that plan's `SystemTable`. `Engine::setSampleRate` re-rates every live core in slot order,
+by id in that plan's `SystemTable`. `Engine::setSampleRate` re-rates the running plan's cores in slot order,
 then re-activates the graph's buffers at the new rate; `Engine::setMaxBlockFrames` grows the plan's
 block size to a host's maximum (never below 8192, the largest block the parity matrix holds), and the
 plugin and the SDL standalone call it before handing the engine over.
 
-### The translator
-
-[`Translator`](../packages/native/src/host/graph/Translator.hpp) is control-side. It keeps the
-model: the ordered system list, each Game Boy's link group, the routing mode
-(`rp::host::AudioRouting`) and the host shape. After every change it builds a graph document and
-its `SystemTable` (a slot-ordered list of `{ id, SystemBase*, SameBoySystem*, MesenNesSystem* }`,
-[SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) and hands both to
-`GraphHost::setGraph`, which computes every node's layout from the document (`layout_for` on each
-node type), runs every layout check, and only then creates and activates the new instances. The
-table rides with the plan, so the audio thread's lookups always match the plan that is running. A
-change that does not plan leaves the model as it was.
-
-- **The model.** `add` appends a system; `replace` keeps the slot and takes the new id (a new core
-  is always a new node); `remove` erases, so later slots shift down. Link groups are Game Boys only,
-  bucketed by nonzero link-group id in slot order, a group of fewer than two dissolved.
-- **Document order.** `rp.dsp.kernel` first, then the unlinked systems by slot, then the link groups
-  in order of first appearance (members by slot, one `link` wire chaining each member to the next),
-  then the mixing nodes, then `rp.host.audio-out`. The planner breaks ties by document order, so
-  this is also the order the plan runs in. Node ids are `kernel`, `sys-<id>` and names derived from
-  them, never from position, so a system keeps its instance across edits.
-- **Routing as wiring.** For each host channel the translator lists the contributions in the order
-  they sum: unlinked systems by slot, then the link groups by first appearance, L before R within a
-  system. One contribution is wired straight to `audio-out`; several go through one mono
-  `rp.core.add` in that order (chained past its 16 inputs, the first add's output as the next one's
-  first input). A system's stereo output reaches mono lanes through `rp.core.split`. For the system
-  at slot i on N host channels:
+### Graph requests
+
+Structure arrives as a graph document TS builds
+([`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts),
+[modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)), in two calls:
+
+- **`buildCore(spec)`** builds and activates a core on the control thread (`factory.build`), arms a Game
+  Boy's serial-out capture if the spec asks, claims its snapshot slot, and parks it under its system id
+  (`Engine::parkCore`). It answers with the node type the core is placed as and the two values the graph
+  needs as the core decoded them (a NES's export mode, a Game Boy's link group), or null. It refuses an
+  id that is still live: parked, or held by a node of any plan not yet reclaimed (it reclaims first, so
+  with no audio thread running every removed core is already gone).
+- **`setGraph({ document, slots, tap? })`** (`Engine::setGraph`) checks that the document's system nodes
+  and the slots name the same systems, each node `sys-<id>` with settings `{ "core": <id> }`; builds the
+  plan's `SystemTable` (a slot-ordered list of `{ id, SystemBase*, SameBoySystem*, MesenNesSystem* }`,
+  [SystemTable.hpp](../packages/native/src/host/graph/SystemTable.hpp)) from the slots and the live cores;
+  turns `tap` into the swap's `SyncTap` trailer; and hands both to `GraphHost::setGraph`, which computes
+  every node's layout from the document (`layout_for` on each node type), runs every layout check, and
+  only then creates and activates the new instances. The table rides with the plan, so the audio
+  thread's lookups always match the plan that is running. Once a document has published, a core still
+  parked is one no document placed, and is freed with its snapshot slot. A refused document changes
+  nothing.
+
+The document `buildGraph` writes:
+
+- **Order.** `rp.dsp.kernel` first, then the unlinked systems by slot, then the link groups in order of
+  first appearance (members by slot, one `link` wire chaining each member to the next), then the mixing
+  nodes, then `rp.host.audio-out`. The planner breaks ties by document order, so this is also the order
+  the plan runs in. Node ids are `kernel`, `sys-<id>` and names derived from them, never from position,
+  so a system keeps its instance across edits. Link groups are Game Boys only, bucketed by nonzero
+  link-group id in slot order, a group of fewer than two dissolved.
+- **Routing as wiring.** For each host channel it lists the contributions in the order they sum:
+  unlinked systems by slot, then the link groups by first appearance, L before R within a system. One
+  contribution is wired straight to `audio-out`; several go through one mono `rp.core.add` in that order
+  (chained past its 16 inputs, the first add's output as the next one's first input). A system's stereo
+  output reaches mono lanes through `rp.core.split`. For the system at slot i on N host channels:
 
   | Mode | Wiring |
   |---|---|
@@ -295,31 +305,28 @@ change that does not plan leaves the model as it was.
   A split is valid with exactly one system, the pin splits only on a NES, and `stride × streams ≤ N`;
   an invalid split falls back to Stereo wiring
   ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 4).
-- **Host shapes** (`rp::host::HostShape`), set through `QueuedInvoker::setHostShape` before the blocks
-  that use them. A shape change is a rewire: every instance is reused, so no core notices.
+- **Host shapes.** The control plane's `EngineGraph` holds the shape and rebuilds for it. A shape change
+  is a rewire: every instance is reused, so no core notices.
 
   | Shape | Set by | Channels |
   |---|---|---|
-  | `Multi(n)` | the plugin (8, at `activate`), the SDL standalone (its device's channel count), `AudioDriverRpcService` and `renderAudio` (2), `renderBlocks` (its `outputs`) | the routing above |
-  | `PerSystem` | `renderAudioPerSystem` | slot i's mix on channels `2i`, `2i + 1` |
-  | `PerChannel` | `renderAudioPerChannel` (one system) | stream s on channels `2s`, `2s + 1`; a mono stream fills only `2s` |
-
-  The render RPCs set their shape before rendering and refuse while the audio thread owns the engine,
-  or when `setHostShape` answers that the shape did not plan (`PerSystem` past 32 systems, the most
-  buses `rp.host.audio-out` takes), since the shape before it would still be running. `Multi(0)` is
-  refused outright: its placement is modulo the channel count.
-- **Cores.** The translator parks each core it is handed under a fresh token, which goes in its
-  node's settings (`{ "core": <token> }`); the node takes it on first activation through the host
-  service `rp.host.cores/1` ([HostServices.hpp](../packages/native/src/host/graph/HostServices.hpp))
-  and owns it from then on. A core it never hands on is freed by the translator, its snapshot slot
-  released first.
+  | `multi(n)` | the plugin (8, through `__rp_setHostOutputs` once the control plane is up), the SDL standalone (its device's channel count, likewise), TS's audio driver for `startAudio` and `renderAudio` (2) and `renderBlocks` (its `outputs`) | the routing above |
+  | `perSystem` | TS's audio driver for `renderAudioPerSystem` | slot i's mix on channels `2i`, `2i + 1` |
+  | `perChannel` | TS's audio driver for `renderAudioPerChannel` (one system) | stream s on channels `2s`, `2s + 1`; a mono stream fills only `2s` |
+
+  The render calls set their shape, render, and put the one before it back. Natively, each render RPC
+  refuses while the audio thread owns the engine, and refuses buffers that do not match the running
+  plan's host width (the sum of its `audio-out` buses, recorded by `setGraph`), so a caller that forgot
+  the shape gets an error rather than audio wired for another output count.
+- **Cores.** A node takes its core on first activation through the host service `rp.host.cores/1`
+  ([HostServices.hpp](../packages/native/src/host/graph/HostServices.hpp)), by the id its settings name,
+  and owns it from then on.
 
 ### `QueuedInvoker`
 
 [`QueuedInvoker`](../packages/native/src/host/engine/EngineInvoker.hpp) (in the files
-`EngineInvoker.{hpp,cpp}`) is **the one mutation path; there is no Direct/Queued fork**. Structure
-(add / replace / remove, the routing mode, a Game Boy's link group, the host shape) goes to the
-translator. Every other edit packs into a POD
+`EngineInvoker.{hpp,cpp}`) is **the one command path; there is no Direct/Queued fork**. Structure
+arrives as graph requests (above). Every other edit packs into a POD
 [`DspCommand`](../packages/native/src/host/engine/DspCommand.hpp) and pushes onto the GraphHost's
 one `SpscRing<Command, 256>` as an opaque host command, behind any plan already there. The single
 control-thread bit `audioThreadOwns_` (forwarded to `GraphHost::setAudioThreadOwns`) decides who
@@ -343,18 +350,20 @@ cleanly.
 | `SetConfigField` | `id`, `field`, `value` | inline |
 | `PressButton` | `id`, `button`, `down` | inline |
 | `WriteRam` | `id`, `offset`, `vector<uint8>* bytes` | owning |
-| `SyncTap` | `routing` | inline; rides in the plan's swap command as a trailer for every add / replace / remove / set-routing (standalone only when the target id is already gone) |
+| `SyncTap` | `arm`, `mode`, `id` | inline; rides in the plan's swap command as a trailer for every `setGraph` request carrying a `tap` |
 
 Heavy, rare payloads (systems JSON, kernel bytecode, RAM writes, long MIDI) cross as raw **owning**
 pointers: the accepted non-RT-on-rare-op pattern. At drain the GraphHost hands each host command
 to `Engine::onHostCommand`, which applies it (`applyCommand`) against the `SystemTable` of the plan
 current at that point, and `delete`s owning payloads after apply. `SyncTap` is the NES channel-tap
-arming: restore whatever `armedTap_` armed, then, when the routing is a split mode over a single NES,
-arm the export mode it needs (either pin split 1, `ChannelSplit` 3), recording what it replaced. It
-runs on every structural change, even when the wanted mode is unchanged, and each run resets the
-capture streams; it rides the plan's swap, so no block runs between the two. `armedTap_` holds the core pointer as well as the id, so a NES replaced under its same id is treated as gone and the old core's mode is not restored onto the new one. The `LinkGroup`
-config field is the one field that is also structure: the invoker pushes it to the core AND hands
-it to the translator, which rewires the cable.
+arming: restore whatever `armedTap_` armed, then, when the request names an arm, set that NES to its
+export mode (either pin split 1, `ChannelSplit` 3, as `buildGraph` decides for a split routing over a
+single NES), recording what it replaced. TS sends it on every edit that builds or drops a core and on
+every routing change, even when the wanted mode is unchanged, and each run resets the capture streams;
+it rides the plan's swap, so no block runs between the two. `armedTap_` holds the core pointer as well
+as the id, so a core that has gone is never restored onto another. The `LinkGroup` config field is the
+one field that is also structure: the invoker pushes it to the core, and the store republishes the
+graph with the cable.
 
 **Ownership handoff.** A displaced or removed core leaves with the plan that last ran it, and is
 freed on the control thread when that plan is reclaimed (`reclaimReleased` → `GraphHost::reclaim`):
@@ -362,8 +371,8 @@ the node destroying it releases the snapshot slot first, then deletes the core
 ([01-architecture.md](01-architecture.md) section 5). Producer edge cases are handled without
 blocking or freeing in render: a **full ring** on an inline POD command drops a lost edge, not a
 leak; on an owning payload the producer deletes it; a plan the ring cannot take is freed
-unpublished (with the core its new node had taken) and `setGraph` answers busy, so the translator
-rolls its model back. A **full release ring** leaks a plan rather than block or free in the render
+unpublished (with the core its new node had taken) and `setGraph` answers busy, so the store keeps
+its model and logs the refusal. A **full release ring** leaks a plan rather than block or free in the render
 loop. [`freePending`](../packages/native/src/host/engine/EngineInvoker.cpp) (teardown only, ring
 single-accessor) discards what is still queued: it adopts the queued plans and frees the queued
 commands' payloads unapplied.
@@ -372,7 +381,7 @@ The block drivers are the same shape: the plugin's
 [`run()`](../packages/native/plugin/PluginDSP.cpp) (stage the DAW's MIDI → `drainInto` → set
 bpm/transport direct → `processBlock`), `audioLoop` for the test host, and `renderBlocks` for the
 harness. The plugin's [`activate` / `deactivate`](../packages/native/plugin/PluginDSP.cpp) flip the
-ownership bit around DPF's lifecycle (`activate` first sets the host shape and the block size),
+ownership bit around DPF's lifecycle (`activate` first sets the block size),
 draining and reclaiming on the way out.
 
 ## `SnapshotRegistry`: the read door
@@ -397,7 +406,7 @@ The ordering guarantees are the load-bearing detail:
   `sys.saveStateBytes()` + slice `sram` from `stateRegions()[Sram]`, publishing both. Then
   `id.store(..., release)` **LAST**: the slot becomes visible only after its buffers are ready.
   Seeding is why a read right after
-  `constructSystem` returns real bytes with no block yet rendered. `claim` requires the core to
+  `buildCore` returns real bytes with no block yet rendered. `claim` requires the core to
   already have `enableStateSnapshot()`'d (for `stateRegions()`).
 - **`publishSystem(system, frames, sampleRate)`**, which `processBlock` calls for each of the
   running plan's systems in slot order: `find(id)`; copy the framebuffer **every block** (cheap); on
@@ -536,7 +545,7 @@ its **runtime** classes:
 The graph the engine runs on is not in `retroplug-core`: the executor, planner and
 [`GraphHost`](../packages/native/src/graph/GraphHost.hpp), with the `rp.core` mixing nodes and the
 `rp.host` I/O nodes, are the `retroplug-graph` / `retroplug-graph-exec` libs (`src/graph/`), and
-the engine's own nodes and translator (`src/host/graph/`) compile into `retroplug-backend`.
+the engine's own nodes (`src/host/graph/`) compile into `retroplug-backend`.
 
 ROM classification (the old C++ `RomSniffer`) and the LSDj sav codec are TS-owned — neither is in
 the C++ tree.
@@ -588,7 +597,7 @@ is one automatable parameter applied post-render across all 8 channels
 - [`CoreBackends.cpp`](../packages/native/src/system/CoreBackends.cpp) — registers the `sameboy` / `mesen` backends into the `SystemFactory`.
 - [`Engine.hpp`](../packages/native/src/host/engine/Engine.hpp) / [`.cpp`](../packages/native/src/host/engine/Engine.cpp): the single-threaded owner of the graph, the DSP kernel and the registry; `processBlock`, the command handler, `SyncTap`.
 - [`EngineInvoker.hpp`](../packages/native/src/host/engine/EngineInvoker.hpp) / [`.cpp`](../packages/native/src/host/engine/EngineInvoker.cpp): `QueuedInvoker`, the one mutation path; [`DspCommand.hpp`](../packages/native/src/host/engine/DspCommand.hpp).
-- [`host/graph/`](../packages/native/src/host/graph): [`Translator.hpp`](../packages/native/src/host/graph/Translator.hpp) / [`.cpp`](../packages/native/src/host/graph/Translator.cpp) (structure as documents, routing as wiring, host shapes), [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`KernelNode.cpp`](../packages/native/src/host/graph/KernelNode.cpp) (the kernel stage as a node), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
+- [`host/graph/`](../packages/native/src/host/graph): [`SystemNodes.cpp`](../packages/native/src/host/graph/SystemNodes.cpp) (the cores as nodes), [`KernelNode.cpp`](../packages/native/src/host/graph/KernelNode.cpp) (the kernel stage as a node), [`SystemTable.hpp`](../packages/native/src/host/graph/SystemTable.hpp), [`HostServices.hpp`](../packages/native/src/host/graph/HostServices.hpp).
 - [`GraphHost.hpp`](../packages/native/src/graph/GraphHost.hpp) / [`.cpp`](../packages/native/src/graph/GraphHost.cpp), [`rp_node.h`](../packages/native/src/graph/rp_node.h): the one ring, plan swap and reclaim; the node ABI (`layout_for` per type).
 - [`SnapshotRegistry.hpp`](../packages/native/src/host/engine/SnapshotRegistry.hpp) / [`.cpp`](../packages/native/src/host/engine/SnapshotRegistry.cpp) — the owned read door.
 - [`SystemFactory.hpp`](../packages/native/src/system/SystemFactory.hpp), [`SameBoyBackend.cpp`](../packages/native/src/system/sameboy/SameBoyBackend.cpp), [`MesenBackend.cpp`](../packages/native/src/system/mesen/MesenBackend.cpp) — the build path.
diff --git a/spec/03-ts-layer.md b/spec/03-ts-layer.md
index 69e9b9b3..b98f5886 100644
--- a/spec/03-ts-layer.md
+++ b/spec/03-ts-layer.md
@@ -45,9 +45,9 @@ The methods, grouped by concern:
 |---|---|---|
 | **Filesystem** | `readFile` · `writeFile` · `writeFileAtomic` · `fileExists` · `rename` · `listDir` · `deleteFile` · `drainChangedPaths` · `setWatchedRoms` | `std::filesystem` bytes; atomic temp+rename; a generic non-recursive readdir; the pull-drain of the native efsw file watcher + the ROM-set registration that feeds it |
 | **Paths** | `canonicalize` · `readFilePrefix` · `configDir` | `weakly_canonical` (the dedupe key); a header-only prefix read (ROM sniff without marshalling MBs); the per-OS config dir |
-| **Emulator lifecycle** | `constructSystem(spec, id)` · `removeSystem(id)` | Build/activate a core under a **TS-allocated id**; drop one. No `duplicate`/`reload` primitive — those are TS orchestration |
+| **Emulator lifecycle** | `buildCore(spec, id)` · `setGraph(request)` | Build/activate a core under a **TS-allocated id** and park it; plan a graph document TS built (which cores run, in what order and wired how). No `remove`/`duplicate`/`reload` primitive: those are TS orchestration over the two |
 | **File dialog** | `openFileBrowser(opts)` → `Promise` | Open the OS browser, resolve the picked path (the one async method) |
-| **Live config** | `applySystemSetting(id,key,value)` · `applyRoleConfig(id,kind,config)` · `setAudioRouting(mode)` | Apply the two universal settings + a **system-role**'s knobs to the live core; set the block runner's output routing |
+| **Live config** | `applySystemSetting(id,key,value)` · `applyRoleConfig(id,kind,config)` | Apply the two universal settings + a **system-role**'s knobs to the live core (the audio routing is the graph's wiring, not a call) |
 | **Live input** | `pressButton(id,button,down)` | Queue a joypad edge to the core's audio-thread button sink |
 | **Live reads** | `readState(id)` · `readSram(id)` · `getFrame(id)` | Pull the latest tear-free snapshot from the `SnapshotRegistry` (see doc 01) — never touches a live core |
 | **Byte codecs** | `zip(entries)` · `unzip(bytes)` | miniz deflate/inflate — the only native part of `.rplg` framing (TS assembles every entry) |
@@ -66,7 +66,7 @@ set of RPC facets is bound behind `__rpcSend` (see doc 01/02).
 
 ### `ConstructSpec` — the resolved-paths-only build request
 
-`constructSystem` takes a `ConstructSpec`
+`buildCore` takes a `ConstructSpec`
 ([backend.ts:205](../packages/retroplug/src/backend.ts#L205)) in which **TS has
 already resolved every path, suffix, sibling, and classification** — *native never sees a
 suffix, never looks for a sibling, never classifies.*
@@ -76,7 +76,7 @@ suffix, never looks for a sibling, never classifies.*
 | `romPath` / `embeddedRom` | The ROM file to slurp, or a binary-baked marker (e.g. `"mgb"`) with `romPath = ""` |
 | `platform` / `core` | What the ROM targets + which factory builds it — TS classifies via [`platform.ts`](../packages/retroplug/src/platform.ts) |
 | `savPath` / `statePath` | The exact battery file (load-from + auto-save-to) and savestate to boot from, or `null` |
-| `replaceId?` | When set, swap this existing id **in place**; otherwise append |
+| `serialOutCapture?` | Arm a Game Boy's LSDj MI.OUT capture before the core first runs |
 | `sramBytes?` / `stateBytes?` | Seed bytes (zip-import blob, carried battery, role-synthesized sav) that override what native reads from disk; `savPath` stays the auto-save target |
 | `settings?` | The system-role config as JSON, applied **at construct** so a restored savestate isn't nuked by a post-build restart |
 
@@ -114,7 +114,7 @@ roles }`); the private state is `entries`, `focusedId`, and `dirty`
 - **TS owns the system-id counter.** `allocSystemId()` increments a module-scoped
   `nextSystemId` ([systemsStore.ts:37](../packages/retroplug/src/systemsStore.ts#L37)) —
   *"native never allocates."* Ids are opaque handles, one id space per control-plane JS
-  context (1:1 with the native `Project`), and start at **1** because the snapshot registry
+  context (1:1 with the native `Engine`), and start at **1** because the snapshot registry
   uses 0 as its free-slot sentinel.
 - **`classifyRom`** ([systemsStore.ts:48](../packages/retroplug/src/systemsStore.ts#L48))
   is the one place ROM bytes enter TS — and just the `ROM_SNIFF_LEN` (0x134) header prefix.
@@ -126,16 +126,24 @@ roles }`); the private state is `entries`, `focusedId`, and `dirty`
   a feature-role's config stays pure TS.
 - **`duplicateSystem`** ([systemsStore.ts:170](../packages/retroplug/src/systemsStore.ts#L170))
   is TS orchestration with **no native duplicate method**: it pulls `readState(id)` (state
-  includes SRAM), then `constructSystem` seeded with those `stateBytes` under a fresh
+  includes SRAM), then `buildCore` seeded with those `stateBytes` under a fresh
   `allocSystemId()`, and appends.
+- **The graph.** Every edit that changes what runs ends by publishing the engine's graph once
+  (`setOnGraphChange` → `ProjectStore.publishGraph` → `EngineGraph.publish`,
+  [engineGraph.ts](../packages/retroplug/src/engineGraph.ts)): the whole document, built by
+  [`buildGraph`](../packages/retroplug/src/graph/buildGraph.ts) from the entries in list order, each
+  entry's `built` facts (what `buildCore` reported, with its live link group) and the project's audio
+  routing, with a NES tap re-arm when the edit built or dropped a core. A `SystemsStore` used without a
+  `ProjectStore` publishes its systems itself, at the default stereo routing. An unchanged document is
+  not sent ([modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)).
 
 ### The reconstruct-in-place idiom
 
 The control plane **reconstructs** a core rather than live-injecting into it (no `GB_reset`, no
 in-place SRAM clear). The private `rebuildInPlace(id, seed)`
 ([systemsStore.ts:233](../packages/retroplug/src/systemsStore.ts#L233)) is the shared
-body: it captures the source spec, builds a fresh core under a **new id** that swaps the old
-one (`replaceId: id`), and preserves identity + focus
+body: it captures the source spec, builds a fresh core under a **new id** that takes the old
+one's slot, and preserves identity + focus
 (`replaceById(entries, id, {...src, id: newId})`; `if (focusedId === id) focusedId = newId`).
 The `seed` overrides what native would read from disk — `sramBytes` = cold-boot battery,
 `stateBytes` = boot-from-savestate — while `savPath` stays the auto-save target. Five public
@@ -150,7 +158,9 @@ operations are thin wrappers over it:
 | `newSram` | a blank `new Uint8Array(0x20000)` battery |
 
 A separate **project-load rebuild seam** is deliberately quiet (no dirty/onChange, since a
-load isn't a user edit): `clear()` tears every system down; `adopt(config, blobs?)`
+load isn't a user edit): `clear()` tears every system down and publishes the empty graph;
+`adopt(config, blobs?)` (which publishes the graph that places it, except inside a project load,
+which publishes once its systems are all built)
 ([systemsStore.ts:374](../packages/retroplug/src/systemsStore.ts#L374)) reconstructs
 one system preserving its **exact** `savSuffix`/`savPath`, with `blobs` seeding SRAM/state on
 a zip import. Default roles are derived before the build from a `ROLE_HEADER_LEN` (0x150)
@@ -179,7 +189,7 @@ refuses a **newer** schema stamp (`{kind:"incompatible"}`), absolutizes paths, s
 missing files, and holds `pendingLoad` for `relink` or `commit`
 ([projectStore.ts:236](../packages/retroplug/src/projectStore.ts#L236)).
 `setAudioRouting` is the **only** project setting that also reaches native audio, via
-`pushAudioRouting()` → `backend.setAudioRouting`
+`pushAudioRouting()` → `publishGraph`, as the wiring of the graph the project publishes
 ([projectStore.ts:278](../packages/retroplug/src/projectStore.ts#L278)).
 
 The project **name** is two-tier. `name()` is the project's own name - blank unless the user
diff --git a/spec/06-build-test.md b/spec/06-build-test.md
index f01f27a1..1e7680fc 100644
--- a/spec/06-build-test.md
+++ b/spec/06-build-test.md
@@ -32,7 +32,7 @@ All defined in [packages/native/CMakeLists.txt](../packages/native/CMakeLists.tx
 | Target | Kind | Produces | In `ALL`? |
 |---|---|---|---|
 | `retroplug-core` | static lib | The shared emulator / kit-codec core. | yes (via consumers) |
-| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes and translator, the RPC services, the DSP runtime, and the shared txiki host. Composed by every host. | yes (via consumers) |
+| `retroplug-backend` | static lib | `retroplug-core` + `retroplug-graph` (the graph executor and planner, [packages/native/cmake/graph.cmake](../packages/native/cmake/graph.cmake)) + the Engine and its graph nodes, the RPC services, the DSP runtime, and the shared txiki host. Composed by every host. | yes (via consumers) |
 | `retroplug` | umbrella plugin | `dpf_add_plugin(retroplug TARGETS clap vst3 vst2 au jack)` — the per-format DPF variants (`retroplug-clap`/`-vst3`/`-vst2`/`-au`/`-jack`). AU is macOS-only. | yes |
 | `retroplug-jack` | DPF variant | `build/bin/retroplug` — the **standalone binary**. | yes |
 | `retroplug-sdl` | executable | `build/bin/retroplug-sdl` — the SDL2 standalone ([sdl-standalone.md](../docs/sdl-standalone.md)). Shipped in the CI artifact, deliberately **not** in releases. | yes |
@@ -203,7 +203,7 @@ Root [package.json](../package.json). Each builds its CMake target(s) first, the
 | `test` | `tjs-cli` | Mock-backend TS suite. |
 | `test:native` | `retroplug-host` | Real-host suite. |
 | `test:ui` | `retroplug-ui-test` | LVGL React UI suite. |
-| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph: each system node rendering what its core renders driven directly, bit for bit, and the translator's wiring for every routing mode and host shape (`retroplug-engine-graph-test`). |
+| `test:plugin` | ten Catch2 binaries (`-plugin-test`, `-dynparams-test`, `-classid-test`, `-audio-test`, `-watcher-test`, `-midi-test`, `-launchpad-test`, `-lottie-test`, `-graph-test`, `-engine-graph-test`) | Pure-C++ unit checks (no TS runner; exit code is pass/fail): the per-context routing behind PluginUI's `__rp_*` window hooks ([ContextTargets.hpp](../packages/native/plugin/ContextTargets.hpp), proven on two live `JSContext`s so concurrent instances never cross-route), DPF's dynamic-parameter diff classifier, the class-id counter sync that keeps the DAW-hosted editor from rendering blank, the per-channel audio split, the efsw file watcher, MIDI in/out, the Launchpad scanner, the Lottie decoder, the node-graph engine's foundations (`retroplug-graph-test`), and the engine on that graph: each system node rendering what its core renders driven directly, bit for bit, and the engine placing, re-arming and freeing what the documents TS builds say (`retroplug-engine-graph-test`). |
 | `screenshot` | `retroplug-jack` | Boots the standalone headlessly → `/tmp/retroplug.png`. |
 | `sdl:smoke` | `retroplug-sdl` | Headless SDL-standalone boot + shutdown; needs no display server or audio server. In CI. |
 | `sdl:pipewire` | `retroplug-sdl` | Stands up a private PipeWire server and asserts WHICH output device the PortAudio backend opens. Not in CI (the runners have no PipeWire). |
diff --git a/spec/10-multichannel-audio-out.md b/spec/10-multichannel-audio-out.md
index 78ef13ef..a6afe050 100644
--- a/spec/10-multichannel-audio-out.md
+++ b/spec/10-multichannel-audio-out.md
@@ -7,8 +7,9 @@ packing) are all in. The one open piece is the per-mapper expansion sub-channels
 This doc describes outputting the *individual console sound channels* of a single emulator instance
 instead of its mixed stereo — 8 outputs for a Game Boy (a stereo pair per channel), and 5 mono
 channels / the hardware "stereo-mod" pins for an NES. It builds on
-[02-native-host.md](02-native-host.md) (the `Engine`, the system nodes and the translator that wires
-each system's streams to the host's lanes, and the Backend RPC surface it extends), [05-data-persistence.md](05-data-persistence.md) (the project-level
+[02-native-host.md](02-native-host.md) (the `Engine`, the system nodes, the graph documents
+[`buildGraph.ts`](../packages/retroplug/src/graph/buildGraph.ts) builds to wire each system's streams to
+the host's lanes, and the Backend RPC surface it extends), [05-data-persistence.md](05-data-persistence.md) (the project-level
 `audioRouting` setting it adds a value to), and [09-cli-debugging.md](09-cli-debugging.md) (the CLI
 render + WAV path it extends).
 
@@ -74,7 +75,7 @@ stream port on the default path, so a wide `outs` is **structurally impossible**
 ### The load-bearing correctness rule (do not skip)
 
 **The split must be mode-driven, never an intrinsic always-on system trait.** `channelLayout()` is
-*inert* unless the translator wires a system's stream ports, which it does only for a valid split
+*inert* unless the graph wires a system's stream ports, which it does only for a valid split
 mode or the per-channel render shape. If a system handed its stems to lanes meant for the mix,
 `finishBlock` would sum four *pre-highpass* stems into pair 0 and silently replace the shipped
 highpassed, gain-smoothed mix. So the authority is the wiring: the node sets the lane count from
@@ -83,8 +84,8 @@ per-channel).
 
 Two placement layers, mutually exclusive in the 8-lane budget:
 
-1. **Per-system** (`AudioRouting::Stereo / TwoPerInstance / OnePerInstance`, `rp::host::AudioRouting`
-   in [Translator.hpp](../packages/native/src/host/graph/Translator.hpp)): fans *many* systems across
+1. **Per-system** (`stereo` / `twoPerInstance` / `onePerInstance`, the TS `AudioRouting` in
+   [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)): fans *many* systems across
    the 8 fixed lanes, each system's `mix` only.
 2. **Per-channel** (`ChannelSplit` / `PinSplit` / `StereoPinSplit`): fans *one* system's streams
    across lanes. Two realizations:
@@ -94,8 +95,8 @@ Two placement layers, mutually exclusive in the 8-lane budget:
    - **CLI**: the `PerChannel` host shape, which wires each stream to its own pair (the system
      decides count/width); it serves GB 4-stereo *and* NES 5-mono / pins.
 
-`ChannelSplit` / `PinSplit` / `StereoPinSplit` are the 4th, 5th and 6th `AudioRouting` values, all gated
-to `systemCount() == 1`, **not** a combination with the per-instance modes. Do not generalize the
+`channelSplit` / `pinSplit` / `stereoPinSplit` are the 4th, 5th and 6th `AudioRouting` values, all gated
+to a single system, **not** a combination with the per-instance modes. Do not generalize the
 per-channel wiring to many systems.
 
 ## 2. Game Boy design (the primary case: 1 GB → 8 outputs)
@@ -214,10 +215,10 @@ Three project-level `AudioRouting` values, all **single-system**. Menu rows: `Ch
 vocabulary — same concept, same words. The old `Channels (1 GB)` label meant "one Game Boy" and read as
 a gigabyte.
 
-- **Native enum:** `AudioRouting::ChannelSplit = 3`, `PinSplit = 4`, `StereoPinSplit = 5`
-  (`rp::host::AudioRouting`, [Translator.hpp](../packages/native/src/host/graph/Translator.hpp)). TS mirrors them as
-  `channelSplit` / `pinSplit` / `stereoPinSplit` (additive to the persisted enum → no `K_PROJECT` bump,
-  no migration step).
+- **The values:** `channelSplit`, `pinSplit` and `stereoPinSplit`, the 4th to 6th `AudioRouting` values
+  ([settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)). They never cross to native as values,
+  only as the wiring of the graph TS builds (additive to the persisted enum → no `K_PROJECT` bump, no
+  migration step).
 - **Why two pin modes.** They tap the *same* three streams and differ only in lanes spent. `Pins` packs
   them 3-across (Pulse on out 1, TND on out 2, Expansion on out 3), which is tightest and works on a
   4-channel interface — but the plugin's outputs are four stereo PAIRS, so out 1 and out 2 arrive as the
@@ -225,7 +226,7 @@ a gigabyte.
   Pulse and TND separately. `Stereo Pins` spends a pair on each pin (out 1/2, 3/4, 5/6) with the mono
   stem mirrored L=R, so every pin is an ordinary centred stereo track. Six lanes instead of three, bought
   deliberately for DAW ergonomics.
-- **Wiring:** the translator wires the one system's stream ports to the host's lanes by a **lane
+- **Wiring:** `buildGraph` wires the one system's stream ports to the host's lanes by a **lane
   stride** taken from the layout:
   - **stride 2, stereo streams:** stream *k* → lanes 2k and 2k+1, through an `rp.core.split` of the
     stream's stereo port. GB channel *k* → plugin stereo pair *k* over the 8 DPF lanes.
@@ -241,9 +242,9 @@ a gigabyte.
 
   `GraphHost::run` silences every host output before the plan runs, so unused lanes are silent for
   free.
-- **Lane plan (in the document, not per block).** The translator works the lane plan out when it
-  builds the document, on the control thread, at every edge that can change it (set-routing, add /
-  replace / remove, a host-shape change), so the audio thread never counts streams. A split is wired
+- **Lane plan (in the document, not per block).** `buildGraph` works the lane plan out when it
+  builds the document, on the control thread, for every document the stores publish, so the audio
+  thread never counts streams. A split is wired
   only when `stride × streams` fits the host's channels, so a narrow host (an SDL 4-channel pick)
   still carries `Pins` (3 lanes) and falls back to Stereo wiring for `Channels` (5) and `Stereo Pins`
   (6) on its own.
@@ -252,19 +253,20 @@ a gigabyte.
   **construct-only** (from the "mesen" role's `channelExportMode`), so a split mode would have silently
   rendered a plain stereo mix. The `SyncTap` command arms it live via
   `MesenNesSystem::setChannelExportMode` (either pin mode → 1, `ChannelSplit` → 3) **without rebuilding
-  the core**: a routing flip must not reset a playing game the way `setRegion` does. The plan's swap
-  command carries a `SyncTap` trailer for every add / replace / remove / set-routing, dispatched in the
-  same pop right after the plan is adopted, so the arming lands with the wiring, against the plan just
-  adopted. It records `{ SystemId, core, prevMode }` (`Engine::armedTap_`) for what it armed and
+  the core**: a routing flip must not reset a playing game the way `setRegion` does. TS sends a re-arm
+  on every edit that builds or drops a core and on every routing change (`buildGraph`'s `tapArm` names
+  the NES and the mode), and the plan's swap command carries it as a `SyncTap` trailer, dispatched in
+  the same pop right after the plan is adopted, so the arming lands with the wiring, against the plan
+  just adopted. It records `{ SystemId, core, prevMode }` (`Engine::armedTap_`) for what it armed and
   restores that first on every run (even when the wanted mode is unchanged; each run resets the
-  capture streams). The core pointer is compared, never followed: a NES replaced under its same id is
-  a different core, so it counts as gone and is not handed the old core's mode. The CLI / render
-  path, which legitimately sets `channelExportMode` at construct and never selects a split routing, is
-  never disturbed. The translator wires tap ports only when the mode it computes for the core (the
-  routing's arm, else the build mode) is the one those ports need, so the wiring and the armed tap
-  agree by the time the NES runs. Deliberately NOT a `ConfigField` / `applyRoleConfig` entry: routing
+  capture streams). The core pointer is compared, never followed: a core that has gone counts as gone
+  and its mode is never handed to another. The CLI / render path, which legitimately sets
+  `channelExportMode` at construct and never selects a split routing, is never disturbed. `buildGraph`
+  wires tap ports only when the mode it computes for the core (the routing's arm, else the export mode
+  `buildCore` reported) is the one those ports need, so the wiring and the armed tap agree by the time
+  the NES runs. Deliberately NOT a `ConfigField` / `applyRoleConfig` entry: routing
   is the single owner in a live host.
-- **Gating (authority in native).** The translator wires a split **only** when the routing is a split
+- **Gating (authority in `buildGraph`).** It wires a split **only** when the routing is a split
   mode and there is exactly one system; both pin modes additionally require it to be a NES, since
   nothing else has output pins. Otherwise it wires the per-system routing (as Stereo) and the layout
   is inert. A multi-instance project can never mis-route. (On a host too narrow for the split, the
@@ -273,10 +275,8 @@ a gigabyte.
   [modular-graph-phase-b0-design.md](../docs/modular-graph-phase-b0-design.md).) The TS UI mirrors this gating purely for UX
   (`validAudioRoutings` in [menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts), whose
   predicate mirrors `validSplits`).
-- **RPC/command:** the guard in `EngineRpcService::setAudioRouting`
-  ([EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp)) is `mode > 5`; the
-  value goes through `QueuedInvoker::setAudioRouting` to the translator, which rewires, and then the
-  `SyncTap` riding its plan.
+- **The edit:** `ProjectStore.setAudioRouting` refuses an unknown value, then publishes the graph with
+  the new wiring and a re-arm, the `SyncTap` riding its plan.
 - **Port labels: generic (decision).** Keep the static `Out 1..4 L/R` labels with a documented mapping
   (GB: Out1 = Pulse1, Out2 = Pulse2, Out3 = Wave, Out4 = Noise. NES `Channels`: lanes 1..5 =
   Square1/Square2/Triangle/Noise/DMC. NES `Pins`: lanes 1..3 = Pulse/TND/Expansion. NES `Stereo Pins`:
@@ -348,11 +348,12 @@ One native pull-path RPC feeds all three export shapes; the WAV writer needs no
 TS-owned **project setting** (not per-system role-config), consistent with the existing `audioRouting`:
 
 - `projectConfig.ts` — widen the `audioRouting` clamp `clampedInt(0, 2, 0)` → `clampedInt(0, 3, 0)`.
-- `projectStore.ts` — `SETTING_MAX.audioRouting` `2` → `3`; the `setAudioRouting → pushAudioRouting →
-  backend.setAudioRouting` flow is reused as-is.
+- `projectStore.ts`: `SETTING_MAX.audioRouting` `2` → `3`; the `setAudioRouting → pushAudioRouting`
+  flow is reused as-is (it now publishes the graph, docs/modular-graph-phase-b2-design.md).
 - `menuDefs.ts` — add an `AUDIO_ROUTING_NAMES` entry (e.g. `"Channels (1 GB)"`); the cycler is unchanged.
   The UI hides/disables the row when `> 1` system.
-- Native — `AudioRouting::ChannelSplit = 3` + the widened RPC guard.
+- Native: at the time, `AudioRouting::ChannelSplit = 3` + a widened RPC guard; since B2 nothing, the
+  value being TS wiring.
 
 **Migration:** additive (a new enum value + a widened clamp) — an older project simply never carried
 value 3 and zod `.default()`/clamp fills it, so **no** raw `migrate.ts` step and **no** version bump
@@ -432,13 +433,13 @@ reflect-cpp `DefaultIfMissing`-tolerant config that crosses to native), **not**
    separation — Noise stays silent while the pulse voices ring); the summed 8-ch export matches the
    mixed render within ~5 int16 LSB.
 4. **Plugin GB option - DONE.** `AudioRouting::ChannelSplit = 3` + its flat-lane wiring (stream *k* →
-   outs 2k/2k+1, §4), which the translator builds **only** for a split of a single system (otherwise
+   outs 2k/2k+1, §4), which the graph builder builds **only** for a split of a single system (otherwise
    the per-instance routing, so the split stays inert and a multi-instance project can't mis-route),
    widened RPC guard (`mode > 3`), and
    the §7 TS widenings (clamp `0..3`, `SETTING_MAX`, the "Channels (1 GB)" cycler name gated to a single
    system: UX only, native is authority). Guards: `SameBoyStems.test.cpp` in `retroplug-audio-test` (the
-   real 8-lane split on a real mGB; summed pairs == the mixed render), the translator's ChannelSplit
-   case in `retroplug-engine-graph-test` (one Game Boy's stems on 8 lanes, a narrow host falling back),
+   real 8-lane split on a real mGB; summed pairs == the mixed render), `buildGraph`'s ChannelSplit
+   case in `pnpm test graph` (one Game Boy's stems on 8 lanes, a narrow host falling back),
    and the `routing-split` parity group (including a two-system project falling back);
    `app-audio-routing.test.ts` accepts mode 3. Per-pair separation in a real DAW (an 8-channel bus via `RETROPLUG_AUTOLOAD_PROJECT`)
    is the manual confirmation — no headless harness captures the plugin's 8 audio outs yet.
@@ -477,15 +478,15 @@ reflect-cpp `DefaultIfMissing`-tolerant config that crosses to native), **not**
    the wiring so the CLI export keeps its silent R) because the plugin's outputs are stereo PAIRS: under
    the packed `Pins` layout, Pulse and TND share one DAW track and hosts that only bus whole pairs cannot
    separate them.
-   The translator works the lane plan out when it builds the document, rather than the audio thread
+   The graph builder works the lane plan out when it builds the document, rather than the audio thread
    counting a `channelLayout()` vector every block, and the `SyncTap` command arms the NES tap
    **live**: `channelExportMode` was construct-only, so without it a split mode silently rendered the
    plain mix. It restores what it armed on the way out, leaving the CLI/render construct-time path
    untouched. Guards: `NesSplitRouting.test.cpp` (a real Mesen core through the Engine: the arm, the
    3- and 5-lane fan-outs, the round-trip restore, and the fall-back when a second system arrives;
    every case fails if the arming is removed), including the pair-per-pin layout, asserted as
-   bit-identical lanes within each pair, which fails if the mirror is dropped; plus the translator's
-   NES split case in `retroplug-engine-graph-test` (channels and pins mono-packed, pins on pairs, and
-   the fall-back on a narrow host or a second system).
+   bit-identical lanes within each pair, which fails if the mirror is dropped; plus `buildGraph`'s
+   NES split cases in `pnpm test graph` (channels and pins mono-packed, pins on pairs, and the
+   fall-back on a narrow host or a second system).
 8. **Follow-ons** (decisions permitting) — upstream-vs-diff for the SameBoy patch; the NES separation
    pot; EPSM; mode-aware plugin port relabeling; stereo-per-stem for the NES (needs > 8 ports).
diff --git a/spec/13-subsystems.md b/spec/13-subsystems.md
index de432148..819ed1d2 100644
--- a/spec/13-subsystems.md
+++ b/spec/13-subsystems.md
@@ -151,7 +151,7 @@ build. Distribution is the standing objection to it replacing the CLI.
 - Code: `packages/native/node/`.
 - **Prove a change:** `pnpm test:node`.
 
-## The node graph (Phases A and B1)
+## The node graph (Phases A, B1 and B2)
 
 ### `graph/` - the node-graph engine's foundations
 
@@ -170,16 +170,20 @@ descriptors by shared golden files.
 `Engine::processBlock` runs a `GraphHost` plan of the engine's own nodes, `rp.dsp.kernel` (today's
 kernel stage, first in every plan) and one system node per core (`rp.sameboy.gb`, `rp.mesen.nes`,
 `rp.mesen.gba`, `rp.mesen.sms`), mixed by `rp.core.split` / `rp.core.add` into `rp.host.audio-out`.
-A control-side translator turns every structural edit (add / replace / remove, the routing mode, a
-link group, the host shape) into a document; every other edit rides the one ring as a command
-([01-architecture.md](01-architecture.md), [02-native-host.md](02-native-host.md)). TS does not
-build graphs yet: handing it the graph and dissolving the kernel are B2 and B3 of
-[docs/modular-graph-design.md](../docs/modular-graph-design.md) section 8.
-
-- Code: `packages/native/src/graph/` (C++), `packages/native/src/host/graph/` (the engine's nodes and
-  translator), `packages/retroplug/src/graph/` (TS).
+**TS builds the graph** (Phase B2,
+[docs/modular-graph-phase-b2-design.md](../docs/modular-graph-phase-b2-design.md)): the stores publish one
+whole document per structural edit through `EngineGraph` (`buildGraph` writes it from the systems, the
+routing and the host shape), native builds each core with `buildCore` and plans the document with
+`setGraph`, and every other edit rides the one ring as a command
+([01-architecture.md](01-architecture.md), [02-native-host.md](02-native-host.md)). Dissolving the
+kernel is B3 of [docs/modular-graph-design.md](../docs/modular-graph-design.md) section 8.
+
+- Code: `packages/native/src/graph/` (C++), `packages/native/src/host/graph/` (the engine's nodes),
+  `packages/retroplug/src/graph/` (TS: the node and extension contracts, `buildGraph`, `DirectGraph`) and
+  `packages/retroplug/src/engineGraph.ts`.
 - **Prove a change:** `pnpm test:plugin graph` (a substring filter, so both `retroplug-graph-test`
   and `retroplug-engine-graph-test`), `pnpm test graph`, `pnpm test:native parity` for anything the
   engine renders, and the ThreadSanitizer build of `retroplug-graph-test` (`build-tsan/`, as in
-  Task 9 of [docs/modular-graph-phase-a-plan.md](../docs/modular-graph-phase-a-plan.md)).
+  Task 9 of [docs/modular-graph-phase-a-plan.md](../docs/modular-graph-phase-a-plan.md)). How the
+  planner scales: `build/bin/retroplug-graph-test "[bench]"`.
 - Deeper: [docs/modular-graph-design.md](../docs/modular-graph-design.md).
diff --git a/spec/README.md b/spec/README.md
index 1a4131e8..d7be574b 100644
--- a/spec/README.md
+++ b/spec/README.md
@@ -29,7 +29,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 |---|---|
 | [00-overview.md](00-overview.md) | The front door: what RetroPlug is, the thesis, the artifact set (clap/vst3/vst2/au/jack + standalone + test hosts), the package layout, a one-screen component map. |
 | [01-architecture.md](01-architecture.md) | **The canonical runtime model.** The three hosts over one Backend RPC surface (the capability facets); control plane vs audio thread; the command ring, the graph the engine runs on, the `SnapshotRegistry` read door, the release ring; the two QuickJS runtimes; the threading/ownership invariants. Other docs reference this one. |
-| [02-native-host.md](02-native-host.md) | The C++ host (`packages/native`): the full RPC surface + its facets, `Engine` + `EngineInvoker` + the translator (each block's plan, routing as wiring, host shapes), `SnapshotRegistry` internals, `SystemFactory` + the core backends, the bare DSP context runner, and the shared-core classes it wraps. |
+| [02-native-host.md](02-native-host.md) | The C++ host (`packages/native`): the full RPC surface + its facets, `Engine` + `EngineInvoker` + the graph requests (each block's plan, the documents TS builds: routing as wiring, host shapes), `SnapshotRegistry` internals, `SystemFactory` + the core backends, the bare DSP context runner, and the shared-core classes it wraps. |
 | [03-ts-layer.md](03-ts-layer.md) | The TypeScript layer (`packages/retroplug`): the `Backend` interface, the stores + the reconstruct-in-place idiom, the control-plane composition, the `__rp_*` hooks, and the React/LVGL UI. |
 | [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md) | The role model (system-role vs feature-role) and the DSP role kernel: the byte-sink ABI, the drift-exact PPQ clock in JS, and store→kernel projection. |
 | [05-data-persistence.md](05-data-persistence.md) | The project model and `.rplg` (thin vs export), DPF get/setState, the config schemas, the forward-tolerant-read + version-stamp policy, the LSDj sav codec, and SRAM auto-save. |
@@ -37,7 +37,7 @@ expansion sub-channels, the 4-byte MIDI-out cap) tracked in
 | [07-remaining-work.md](07-remaining-work.md) | The remaining feature gaps now the port is complete: the raw LSDj Keyboard mode, NES per-mapper expansion sub-channels, the 4-byte MIDI-out cap, the deferred items, and the code-comment cleanup follow-up. |
 | [08-profiling.md](08-profiling.md) | **Built** (behind `RETROPLUG_PROFILE`, `pnpm profile`). A benchmark harness that profiles the DSP-thread JS runtime — allocations/GC — under an mGB + heavy-MIDI workload: the refcount-churn reframe, the off-RT `renderAudio` harness, in-process QuickJS allocator instrumentation, the agent-friendly tool tier, and CI gates. |
 | [09-cli-debugging.md](09-cli-debugging.md) | **Built** (the plumbing tier). The CLI as a scriptable ROM-test harness: the session runner + `Timeline` + WAV/screenshot output, the compiled-in `render` subcommand, and Mesen's compiled-in debugger (decoded APU/PPU state, CPU/memory peeks + poke, breakpoints, trace, profiler, cc65 labels) surfaced through the debug RPC facet so an agent can drive a real NES and assert on its state. |
-| [10-multichannel-audio-out.md](10-multichannel-audio-out.md) | **Built** (steps 1–7; per-mapper NES expansion sub-channels remain). Outputting the individual console sound channels of one instance: Game Boy → 8 outputs (a stereo pair per channel, via a tracked SameBoy tap), NES → 5 mono stems or the hardware "stereo-mod" pins (via a `NesSoundMixer` edit). One mode-driven host seam (`channelLayout()` + a lane-counted `finishBlock`, with the translator wiring each stream to its lanes), the `AudioRouting::ChannelSplit` / `PinSplit` / `StereoPinSplit` plugin options (menu rows `Channels` / `Pins` / `Stereo Pins`; a stereo stream takes a lane pair, a mono one a single lane, so the NES fits 8 outputs - except `Stereo Pins`, which spends a pair per pin so each is its own DAW track), and a `renderAudioPerChannel` CLI RPC feeding multichannel / per-stereo / per-mono WAV export. |
+| [10-multichannel-audio-out.md](10-multichannel-audio-out.md) | **Built** (steps 1–7; per-mapper NES expansion sub-channels remain). Outputting the individual console sound channels of one instance: Game Boy → 8 outputs (a stereo pair per channel, via a tracked SameBoy tap), NES → 5 mono stems or the hardware "stereo-mod" pins (via a `NesSoundMixer` edit). One mode-driven host seam (`channelLayout()` + a lane-counted `finishBlock`, with the graph TS builds wiring each stream to its lanes), the `channelSplit` / `pinSplit` / `stereoPinSplit` plugin options (menu rows `Channels` / `Pins` / `Stereo Pins`; a stereo stream takes a lane pair, a mono one a single lane, so the NES fits 8 outputs - except `Stereo Pins`, which spends a pair per pin so each is its own DAW track), and a `renderAudioPerChannel` CLI RPC feeding multichannel / per-stereo / per-mono WAV export. |
 | [11-ui-rendering.md](11-ui-rendering.md) | **Built.** Rendering a system to WAV from the `System > Render` menu as a background job — a fresh instance built from a *copy* of the live SRAM/savestate (never the running core). A shared `src/render/` library the CLI `render` command and the worker both run; a bare-QuickJS `RenderHost` (own `Engine`, no txiki) running a global-code worker bundle; a `RenderJobRegistry` of per-job threads (concurrent, cancellable, survives editor close); and the `__rp_*Render` hooks + tile elapsed-duration/cancel badge. |
 | [12-dynamic-parameters.md](12-dynamic-parameters.md) | **Built** (CLAP + VST3; other formats keep their construct-time names). Per-ROM named DAW automation lanes mapped to mGB / BlipToaster MIDI CCs: the fixed-pool / mutable-descriptor contract the two formats actually permit, the DPF fork's `requestParameterInfoUpdate()` + re-declare-and-diff behind an opt-in macro, the plugin's CC slot pool and its `__rp_parameterMapJson` seam, and the real-Reaper check that proves a host acts on the flag. |
 | [13-subsystems.md](13-subsystems.md) | **The index of everything else.** One short entry each for the subsystems the docs above do not cover: the common tracker interface (`src/tracker/`), risa, smsggdj, the LSDj HD player, the Everdrive N8 hardware bridge, Launchpad, the controller role, the pico firmware, `retroplug-sdl`, the Node addon, and the node graph the engine runs on. What each is, where it lives, and which command proves a change to it. |
````

- [ ] **Step 2: Check the links and the emdash rule**

Run: `pnpm check:docs`
Expected: no errors (this plan exists by now, so the links to it resolve).

Run: `git diff HEAD~1 -U0 -- spec docs AGENTS.md | grep '^+' | grep -c "$(printf '\u2014')"`
Expected: `0`.

- [ ] **Step 3: Commit**

```bash
git add -A spec docs AGENTS.md packages/native/src/system/sameboy/SameBoyBackend.hpp
git commit -m "docs: the engine with TS owning the graph, and what building it found"
```

---
### Task 9: Verify everything

The acceptance run (design section 7.5), on the final tree.

- [ ] **Step 1: The suites**

Run: `RP_FAIL_ON_SKIP=1 RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native parity`
Expected: `# 24 native test file(s) passed`.

Run: `pnpm test:native parity`
Expected: `# 24 native test file(s) passed`, with `28 case(s) SKIPPED in 7 file(s)` (the fixture groups the
baseline allows), as CI runs it.

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources pnpm test:native && pnpm test && pnpm test:plugin && pnpm test:ui && pnpm test:node && pnpm typecheck && pnpm check:docs`
Expected: 148 native files (the two bench skips), 158 files, 10 binaries, 37 UI files, 12 node tests, a clean
typecheck and no doc errors.

- [ ] **Step 2: The sanitizers**

Run: `RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh thread && RETROPLUG_RESOURCES_DIR=/workspaces/resources tools/run-sanitizer.sh address`
Expected: `==> host passed under thread sanitizer`, then `==> host passed under address sanitizer`.

- [ ] **Step 3: The live hosts**

Run each, one at a time, with nothing rebuilding the plugin meanwhile:
`pnpm reaper:editor-reopen`, `pnpm reaper:editor-autoload`, `pnpm reaper:two-instances-load`,
`pnpm reaper:params`, `pnpm reaper:params-clap`.
Expected: each prints `PASS:` (the plugin's graph now arrives through `__rp_setHostOutputs` at boot and the
stores' documents).

Run: `pnpm sdl:smoke && pnpm sdl:pipewire`
Expected: `SDL smoke OK` (its `multi-output audio` check renders the standalone at more than two outputs, the
width arriving through `__rp_setHostOutputs`), then `SDL PipeWire check: 8 passed, 0 failed`. The wide-output
leg needs wireplumber; where it will not start (this devcontainer, in the prototype run) it prints
`SKIPPED: wide-output checks need wireplumber`, and it checks the PortAudio stream's width rather than the graph.

Nothing to commit unless a step failed and was fixed; a fix is its own commit, named for what it fixes.
