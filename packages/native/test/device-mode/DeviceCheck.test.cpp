// The device check (packages/graph-runtime/device-check, docs/modular-graph-phase-e1-design.md section 7) on the
// desktop. The RP2350 image is built and linked in CI (its link trap is the proof that it allocates and throws
// nothing) but run nowhere before E3, so the run itself is held here: the same DeviceCheck.cpp, over the device-mode
// builds of the same packages (retroplug-device-mode-objects), against the same header of cases
// (rp-device-check-cases' output), in an arena of the image's size.
//
// - Which types a device registers is the types' own word: each `device` entry of a manifest registers, in device
//   mode, exactly the types of its desktop build that declare neither heap nor threads, in the same order. A type left
//   out of the device build by its source but portable, or a heap type built anyway, fails here.
// - The device check carries every package with a `device` entry (its list is device-check/packages.cmake), and its
//   cases, generated for the entries they name, list exactly those entries' device types, in order.
// - The check passes, every type created from its defaults as msgpack, activated, run for one block and destroyed, in
//   the image's arena, with no allocation (the counting allocator stands in for the image's trap).
// - It is not vacuous: a missing case, a case no type takes, settings a type refuses, an arena too small, and a create,
//   an activate and a block that fail (a test type, rp.devtest.fault) each stop it at its own step, naming the type.
// - Its plan (plan.json, exported as the image's build exports it, DeviceCheckPlan.hpp) loads over the image's
//   registration in an arena of the image's size, runs its second of ragged blocks with its params moving and unloads,
//   allocating nothing and leaving no node alive, in exactly the arena the exporter estimated; it holds a coupled pair,
//   a feedback copy and param starts, and is its golden's bytes (golden/device-check-plan.hex; RP_PLAN_BLOB_UPDATE=1
//   records it); and its digest is what GraphHost's run of the same document with the desktop's builds gives, the
//   figure the image prints beside its own. Each of the plan check's steps stops it naming the node: a refused blob, a
//   type the registration lacks, an arena too small, a node failing part-way through the run-up, a block faulting a
//   node, a move with no stream to write, a run that does not hold (a move never pushed, a digest missing or deaf), an
//   arena past the estimate and a block size of 0.
// - The image's script pack (docs/modular-graph-phase-e4-design.md section 8): the devtest pack, registered as the
//   image registers it (with its heap, RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES, on the device-mode host), whose two types are
//   cases at the device MIDI input's queues and run with their package's memory set up in the arena, which stops the
//   check when it does not fit or the pack refuses it; its script plan (script-plan.json, exported as the image's build
//   exports it, its golden golden/device-check-script-plan.hex) runs the host's stimulus through both of its nodes,
//   allocating nothing, to a digest of messages alone that the image holds its own run to, pinned here on every host
//   (the nest node's deep path drops the real-time messages, so the digest is one only that path gives), with the
//   pack's context at the image's figure; a context of another size, another digest, a heap that runs out (the script
//   node's fault, in its words) and a run with no stimulus each stop it; the bench runs its node through the stimulus's
//   blocks, stopping at a type it cannot find, memory that does not fit, a case with no queue to feed and a heap that
//   runs out; and a script node that never returns faults by the clock the host offers, as the image's does.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "BuildTarget.hpp"  // packages/native/src/util: rp::buildTarget, the reference host's key
#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
#include "DeviceCheckPlan.hpp"
#include "DeviceCheckScriptPlan.hpp"
#include "DeviceModeEntries.hpp"
#include "N8Protocol.hpp"
#include "PlanNodes.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/Descriptor.hpp"
#include "graph/GraphHost.hpp"
#include "graph/JsonToMsgpack.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanBlob.hpp"
#include "graph/PlanExport.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/ThreadClock.hpp"
#include "graph/rp_device_midi_in.h"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "script/MqjsHost.hpp"

// Generated (device-entries.cpp): every manifest's `device` entries, with both builds, and every script pack's
// registration, with both builds and its source.
std::vector<DeviceEntry> deviceEntries();
std::vector<ScriptEntry> scriptEntries();

namespace {

using rp::device_check::LoopResult;
using rp::device_check::LoopStep;
using rp::device_check::Result;
using rp::device_check::Step;
using rp::device_check::TypeCase;

std::vector<std::string> idsOf(const rp_package* p) {
    std::vector<std::string> ids;
    for (std::uint32_t i = 0; i < p->node_type_count; ++i) ids.emplace_back(p->node_types[i]->id);
    return ids;
}

// The device-mode builds of the image's packages, as its rp_register_packages() lists them.
std::vector<const rp_package*> devicePackages();

Result runCheck(const std::vector<const rp_package*>& packages, const std::vector<TypeCase>& cases,
                std::size_t arenaBytes = rp::device_check::kArenaBytes) {
    std::vector<std::max_align_t> arena(arenaBytes / sizeof(std::max_align_t) + 1);
    return rp::device_check::run(packages.data(), packages.size(), cases.data(), cases.size(), arena.data(), arenaBytes,
                                 rp::device_check::kBlockFrames);
}

const std::vector<TypeCase> kAllCases(std::begin(rp::device_check::kCases), std::end(rp::device_check::kCases));

// The device types of a device entry's desktop build: its types that declare neither heap nor threads, in order.
std::vector<std::string> portableIds(const DeviceEntry& e, std::size_t* leftOut = nullptr) {
    std::vector<std::string> portable;
    const rp_package*        desktop = e.desktop();
    for (std::uint32_t i = 0; i < desktop->node_type_count; ++i) {
        const rp_node_type* t = desktop->node_types[i];
        auto                d = rp::graph::parseDescriptor(t->descriptor(t));
        REQUIRE(d);
        if (d->heap || d->threads) {
            if (leftOut) ++*leftOut;
        } else {
            portable.emplace_back(t->id);
        }
    }
    return portable;
}

// The entries the cases header names (the image's product), in its order, each as the manifests list it; an entry no
// manifest marks `device` and no script pack registers through is left out here, and the test that reads the header
// refuses it. A script pack's entry stands in its order with its source.
struct CheckedEntry {
    DeviceEntry                entry;
    const rp_mqjs_pack_source* script = nullptr;  // the pack's source for a script pack's registration
};
std::vector<CheckedEntry> checkedEntries() {
    std::vector<CheckedEntry> out;
    std::istringstream        words(rp::device_check::kEntries);
    const auto                all = deviceEntries();
    const auto                scripts = scriptEntries();
    for (std::string w; words >> w;) {
        for (const DeviceEntry& e : all)
            if (w == e.entry) out.push_back({e});
        for (const ScriptEntry& e : scripts)
            if (w == e.entry) out.push_back({{e.package, e.entry, e.desktop, e.device}, e.source});
    }
    return out;
}

// A script pack as the image registers it (the device check's CMakeLists.txt): its source with the image's own heap,
// RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES, in place of its manifest's, on the device-mode build of the host. The copy lives
// as long as the binary, as the host keys a pack on its source's address. `heapBytes` gives another figure.
const rp_package* imageScriptPackage(const rp_mqjs_pack_source& source,
                                     std::uint32_t heapBytes = RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES) {
    static std::deque<rp_mqjs_pack_source> copies;
    const rp_mqjs_pack_source*             copy = nullptr;
    for (const rp_mqjs_pack_source& c : copies)
        if (c.bytecode == source.bytecode && c.heap_bytes == heapBytes) copy = &c;
    if (!copy) {
        copies.push_back(source);
        copies.back().heap_bytes = heapBytes;
        copy = &copies.back();
    }
    const rp_package* p = rp_devmode::mqjs::package(*copy);
    INFO(rp_devmode::mqjs::packageRefusal());
    REQUIRE(p);
    return p;
}

std::vector<const rp_package*> devicePackages() {
    std::vector<const rp_package*> packages;
    for (const CheckedEntry& e : checkedEntries())
        packages.push_back(e.script ? imageScriptPackage(*e.script) : e.entry.device());
    return packages;
}

// The source of the image's script pack (its one), as its manifest built it.
const rp_mqjs_pack_source& imageScriptSource() {
    for (const CheckedEntry& e : checkedEntries())
        if (e.script) return *e.script;
    FAIL("the device check's cases name no script pack");
    throw;
}

// The image's script pack, on the device-mode build, at the image's heap or `heapBytes`.
const rp_package* imageScriptPack(std::uint32_t heapBytes = RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES) {
    return imageScriptPackage(imageScriptSource(), heapBytes);
}

// What a device build of this width makes the image's context: the declared heap times kDeviceHalves halves
// (MqjsHost.hpp: 3/2 at 64 bits, the toolchain check's; the heap exactly at 32, the image's).
constexpr std::size_t kImageContextBytes =
    std::size_t{RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES} * rp::mqjs::kDeviceHalves / 2;

// The check over rp.devtest.fault alone, from `json`: one mono output and no params.
Result runFault(const char* json) {
    static constexpr rp::device_check::PortCase kOut{RP_PORT_AUDIO, true, 1, 0, 0};
    const auto settings = rp::graph::settingsToMsgpack(json);
    REQUIRE(settings);
    TypeCase c;
    c.id = "rp.devtest.fault";
    c.settings = settings->data();
    c.settingsSize = static_cast<std::uint32_t>(settings->size());
    c.ports = &kOut;
    c.portCount = 1;
    return runCheck({rp_devtest_fault_package_device_mode()}, {c});
}

}  // namespace

TEST_CASE("a device entry registers its types that declare neither heap nor threads", "[device-mode][device-check]") {
    const std::vector<DeviceEntry> entries = deviceEntries();
    REQUIRE(entries.size() >= 2);  // rp.core and rp.dsp at least
    std::size_t leftOut = 0;
    for (const DeviceEntry& e : entries) {
        INFO(e.package << ": " << e.entry);
        CHECK(idsOf(e.device()) == portableIds(e, &leftOut));
    }
    CHECK(leftOut >= 1);  // rp.dsp.dpcm, which declares heap
}

TEST_CASE("the device check carries every device entry, and its cases list their device types",
          "[device-mode][device-check]") {
    std::vector<std::string> named;
    std::istringstream       words(rp::device_check::kEntries);
    for (std::string w; words >> w;) named.push_back(w);
    const std::vector<DeviceEntry> entries = deviceEntries();
    for (const DeviceEntry& e : entries) {
        INFO("the package " << e.package << " has a device entry, " << e.entry
                            << ", which the device check does not carry: add the package to "
                               "packages/graph-runtime/device-check/packages.cmake");
        CHECK(std::find(named.begin(), named.end(), e.entry) != named.end());
    }
    // A script pack's registration is the image's too (its product registers every listed package's), and its types
    // are cases like the rest, all of them device types.
    const std::vector<ScriptEntry> scripts = scriptEntries();
    std::vector<std::string>       registered;
    for (const std::string& n : named) {
        const auto s = std::find_if(scripts.begin(), scripts.end(), [&](const ScriptEntry& d) { return n == d.entry; });
        if (s != scripts.end()) {
            const std::vector<std::string> ids = idsOf(s->desktop());
            registered.insert(registered.end(), ids.begin(), ids.end());
            continue;
        }
        const auto e = std::find_if(entries.begin(), entries.end(), [&](const DeviceEntry& d) { return n == d.entry; });
        INFO("the cases name " << n << ", which no manifest marks `device` and no script pack registers through");
        REQUIRE(e != entries.end());
        const std::vector<std::string> portable = portableIds(*e);
        registered.insert(registered.end(), portable.begin(), portable.end());
    }
    std::vector<std::string> cases;
    for (const TypeCase& c : kAllCases) cases.emplace_back(c.id);
    CHECK(cases == registered);
}

TEST_CASE("the device check runs every device type from its default settings, in the image's arena, allocating "
          "nothing",
          "[device-mode][device-check]") {
    const std::vector<const rp_package*> packages = devicePackages();
    // The arena is made before counting starts, as the image's is a static.
    std::vector<std::max_align_t> arena(rp::device_check::kArenaBytes / sizeof(std::max_align_t) + 1);

    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    const Result r = rp::device_check::run(packages.data(), packages.size(), kAllCases.data(), kAllCases.size(),
                                           arena.data(), rp::device_check::kArenaBytes, rp::device_check::kBlockFrames);
    const auto counted = rp::test::endCountingAllocations();
    INFO("stopped at " << (r.id ? r.id : "-") << ", step " << static_cast<int>(r.step) << ": " << r.error.message);
    CHECK(r.step == Step::Ok);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(r.types == kAllCases.size());
    // The largest instance, with its ports, fitted the image's arena (an instance whose defaults outgrow it fails the
    // run with Step::Arena above). No type's size is pinned here: kArenaBytes says which one sizes it.
    CHECK(r.instanceBytes > 0);
    CHECK(r.arenaBytes >= r.instanceBytes);
    CHECK(r.arenaBytes <= rp::device_check::kArenaBytes);
}

TEST_CASE("the device check stops at the type that fails, and names it", "[device-mode][device-check]") {
    const std::vector<const rp_package*> packages = devicePackages();
    REQUIRE(kAllCases.size() >= 2);

    SECTION("a registered type with no case") {
        std::vector<TypeCase> cases(kAllCases.begin(), kAllCases.end() - 1);
        const Result          r = runCheck(packages, cases);
        CHECK(r.step == Step::NoCase);
        CHECK(std::string(r.id) == kAllCases.back().id);
        CHECK(r.type == kAllCases.size() - 1);
    }
    SECTION("a case no registered type takes") {
        std::vector<TypeCase> cases = kAllCases;
        TypeCase              extra = cases.front();
        extra.id = "rp.devtest.nobody";
        cases.push_back(extra);
        CHECK(runCheck(packages, cases).step == Step::CaseUnused);
    }
    SECTION("settings the type refuses") {
        std::vector<TypeCase> cases = kAllCases;
        static constexpr std::uint8_t kNotAMap[] = {0x92, 0x01, 0x02};  // [1, 2]
        cases[1].settings = kNotAMap;
        cases[1].settingsSize = sizeof kNotAMap;
        const Result r = runCheck(packages, cases);
        CHECK(r.step == Step::QueryMemory);
        CHECK(std::string(r.id) == kAllCases[1].id);
        CHECK(std::string(r.error.message) == "settings must be a msgpack map");
    }
    SECTION("an arena too small for an instance and its ports") {
        const Result r = runCheck(packages, kAllCases, 64);
        CHECK(r.step == Step::Arena);
        CHECK(std::string(r.id) == kAllCases.front().id);
    }
    // rp.devtest.fault (DeviceModeNodes.cpp) fails at the step its settings name, and runs clean with none named.
    SECTION("a type that runs clean") {
        const Result r = runFault(R"({"at":"none"})");
        INFO(r.error.message);
        CHECK(r.step == Step::Ok);
        CHECK(r.types == 1);
    }
    SECTION("a create that fails") {
        const Result r = runFault(R"({"rom":"res:1"})");  // res:1 is the host's audio buffer, not a "rom"
        CHECK(r.step == Step::Create);
        CHECK(std::string(r.id) == "rp.devtest.fault");
        CHECK(std::string(r.error.message).find("rom") != std::string::npos);
    }
    SECTION("an activate that fails") {
        const Result r = runFault(R"({"at":"activate"})");
        CHECK(r.step == Step::Activate);
        CHECK(std::string(r.id) == "rp.devtest.fault");
        CHECK(std::string(r.error.message) == "activate failed");
    }
    SECTION("a block that faults the node") {
        const Result r = runFault(R"({"at":"process"})");
        CHECK(r.step == Step::Process);
        CHECK(std::string(r.id) == "rp.devtest.fault");
        CHECK(r.types == 0);
    }
}

TEST_CASE("the device check's N8 protocol run drives the pack's Edio, Menu and Ops to what its script says, "
          "allocating nothing",
          "[device-mode][device-check]") {
    // The image links N8Protocol.cpp under the trap; here the same source runs over the desktop's build of the pack,
    // so the constants it holds the device to (the bytes its calls write, and their hash) are the desktop's.
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    const rp::device_check::N8ProtocolResult r = rp::device_check::n8Protocol();
    const auto                               counted = rp::test::endCountingAllocations();
    INFO("failed at " << (r.failed ? r.failed : "-") << ", " << r.written << " bytes, hash " << r.hash);
    CHECK(r.ok);
    CHECK(r.written == rp::device_check::kN8ProtocolBytes);
    CHECK(r.hash == rp::device_check::kN8ProtocolHash);
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
}

// ---- the plan -----------------------------------------------------------------------------------------------------

namespace {

using rp::device_check::PlanCase;
using rp::device_check::PlanMove;
using rp::device_check::PlanResult;
using rp::device_check::PlanStep;
using rp::graph::devtest::faultPackageDesktop;
using rp::graph::devtest::misfitPackageDesktop;

static_assert(RP_DEVICE_CHECK_ARENA_BYTES == rp::device_check::kArenaBytes,
              "plan.cmake's RP_DEVICE_CHECK_ARENA_BYTES is not DeviceCheck.hpp's kArenaBytes");

const std::vector<std::uint8_t> kPlanBlob(std::begin(kDeviceCheckPlan), std::end(kDeviceCheckPlan));

// The image's plan case (main.cpp's kPlan) over `blob`, with `moves` and the arena estimate `estimate` (the embedded
// plan's, from its header, by default).
PlanCase planCase(const std::vector<std::uint8_t>& blob, const PlanMove* moves = rp::device_check::kPlanMoves,
                  std::size_t moveCount = std::size(rp::device_check::kPlanMoves),
                  std::size_t estimate = kDeviceCheckPlanArenaEstimate) {
    return PlanCase{blob.data(),
                    blob.size(),
                    estimate,
                    rp::device_check::kPlanFrames,
                    rp::device_check::kPlanSplits,
                    std::size(rp::device_check::kPlanSplits),
                    moves,
                    moveCount,
                    rp::device_check::kPlanDigest};
}

struct PlanRun {
    PlanResult result{};
    int        allocations = -1;
    int        frees = -1;
    int        alive = -1;  // devtest nodes alive after the run (the device-mode build's)
};

// runPlan as the image runs it, in an arena of `arenaBytes` made before counting starts (the image's is a static),
// counting what the load, the blocks and the unload allocate.
PlanRun runPlanCase(const PlanCase& c, const std::vector<const rp_package*>& packages,
                    std::size_t arenaBytes = rp::device_check::kArenaBytes) {
    PlanRun                       r;
    std::vector<std::max_align_t> arena(arenaBytes / sizeof(std::max_align_t) + 1);
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    rp::device_check::runPlan(c, packages.data(), packages.size(), arena.data(), arenaBytes, &r.result);
    const auto counted = rp::test::endCountingAllocations();
    r.allocations = counted.allocations;
    r.frees = counted.frees;
    r.alive = rp_devtest_plan_live_device_mode();
    return r;
}

std::string describe(const PlanResult& r) {
    return "step " + std::to_string(static_cast<int>(r.step)) + " at " + r.error.node + ": " + r.error.code + ": " +
           r.error.message;
}

// The desktop's builds of what the check's plans name, which plan them and which GraphHost runs.
std::unique_ptr<rp::graph::NodeRegistry> planRegistry() {
    auto registry = std::make_unique<rp::graph::NodeRegistry>();
    for (const rp_package* p :
         {rp_core_package(), rp_dsp_package(), rp_devtest_plan_package(), faultPackageDesktop(),
          misfitPackageDesktop()}) {
        std::string why;
        const bool  added = registry->addPackage(p, &why);
        INFO(why);
        REQUIRE(added);
    }
    return registry;
}

// `document` exported as the image's plan is, against the image's arena, sized by the check's device builds and the
// fault node's; with trialLoad false, a plan those builds refuse at load.
std::vector<std::uint8_t> exportForCheck(const std::string& document, bool trialLoad = true) {
    auto                           registry = planRegistry();
    std::vector<const rp_package*> types = devicePackages();
    types.push_back(rp_devtest_fault_package_device_mode());
    types.push_back(rp_devtest_misfit_package_device_mode());
    rp::graph::ExportOptions o;
    o.sampleRate = 48000.0;
    o.maxFrames = 256;
    o.arenaBytes = rp::device_check::kArenaBytes;
    o.trialLoad = trialLoad;
    const auto r = rp::graph::exportPlan(document, *registry, types, o);
    std::string errors;
    for (const auto& e : r.errors) errors += e.code + " " + e.nodeId + ": " + e.message + "\n";
    INFO(errors);
    REQUIRE(r.ok());
    return r.blob;
}

// The names of kPlanMoves' params, which GraphHost's setParam takes (the check moves a param by its position).
const char* moveParamName(const PlanMove& m) {
    const std::string node = m.node;
    if (node == "g") return "gain";
    if (node == "a" || node == "b") return "drive";
    if (node == "bq") return "frequency";
    if (node == "k") return "value";
    if (node == "d") return "time";
    return nullptr;
}

rp::graph::blob::Header headerOf(const std::vector<std::uint8_t>& blob) {
    rp::graph::blob::Header h;
    std::memcpy(&h, blob.data(), sizeof h);
    return h;
}

}  // namespace

TEST_CASE("the device check's plan loads, runs its ragged blocks with its params moving and unloads, in the image's "
          "arena, allocating nothing",
          "[device-mode][device-check]") {
    // What the image embeds: plan.json exported for the image's arena, at 48 kHz and 256 frames a block, with a coupled
    // pair, a feedback copy, the five params plan.json sets, a mask for each of its ten nodes and a peer list for each
    // of the pair.
    namespace blob = rp::graph::blob;
    const blob::Header h = headerOf(kPlanBlob);
    CHECK(h.arenaBytes == rp::device_check::kArenaBytes);
    CHECK(h.maxFrames == 256);
    auto          count = [&](blob::Section s) { return h.sections[static_cast<int>(s)].count; };
    std::uint32_t pairs = 0;
    for (std::uint32_t i = 0; i < count(blob::Section::Steps); ++i) {
        blob::StepRecord step;
        std::memcpy(&step,
                    kPlanBlob.data() + h.sections[static_cast<int>(blob::Section::Steps)].offset + i * sizeof step,
                    sizeof step);
        pairs += step.groupSize == 2 ? 1 : 0;
    }
    CHECK(pairs == 1);
    CHECK(count(blob::Section::Feedback) == 1);
    CHECK(count(blob::Section::ParamStarts) == 5);
    CHECK(count(blob::Section::Masks) == 10);
    CHECK(count(blob::Section::Peers) == 2);

    REQUIRE(rp_devtest_plan_live_device_mode() == 0);
    const PlanRun run = runPlanCase(planCase(kPlanBlob), devicePackages());
    const PlanResult& r = run.result;
    INFO(describe(r));
    REQUIRE(r.step == PlanStep::Ok);
    CHECK(run.allocations == 0);
    CHECK(run.frees == 0);
    CHECK(run.alive == 0);  // unloadPlan destroyed every node
    CHECK(r.nodes == 10);
    CHECK(r.sampleRate == 48000.0);
    CHECK(r.maxFrames == 256);
    CHECK(r.arenaBytes > 0);
    CHECK(r.arenaBytes <= rp::device_check::kArenaBytes);
    // The desktop's layout is the exporter's sizing, so here the plan takes exactly the estimate the image holds it to.
    CHECK(r.arenaBytes == kDeviceCheckPlanArenaEstimate);
    CHECK(r.frames == rp::device_check::kPlanFrames);
    CHECK(r.moves == std::size(rp::device_check::kPlanMoves));
    CHECK(r.drops == 0);
    REQUIRE(r.digested);
    CHECK(r.heard);
    CHECK(r.digestBlocks == r.blocks);

    // The blocks are kPlanSplits in turn, the last cut to what was left: 1 frame and maxFrames among them.
    std::uint32_t blocks = 0;
    for (std::uint32_t at = 0; at < rp::device_check::kPlanFrames; ++blocks)
        at += std::min(rp::device_check::kPlanSplits[blocks % std::size(rp::device_check::kPlanSplits)],
                       rp::device_check::kPlanFrames - at);
    CHECK(r.blocks == blocks);

    // The same document in GraphHost with the desktop's builds, in the same blocks with the same moves, hears the same.
    std::ifstream            in(RP_DEVICE_CHECK_PLAN, std::ios::binary);
    const std::string        json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto                     registry = planRegistry();
    rp::graph::ResourceTable resources;
    rp::graph::GraphHost     host(*registry, resources, rp::graph::HostConfig{48000.0, 256});
    const auto               set = host.setGraph(json);
    std::string              errors;
    for (const auto& e : set.errors) errors += e.code + ": " + e.message + "\n";
    INFO(errors);
    REQUIRE(set.ok);
    const rp_transport   transport{48000.0, 120.0, 0.0, RP_TRANSPORT_PLAYING};
    const std::uint32_t* splits = rp::device_check::kPlanSplits;
    std::uint32_t        at = 0;
    for (std::uint32_t block = 0; at < rp::device_check::kPlanFrames; ++block) {
        for (const PlanMove& m : rp::device_check::kPlanMoves)
            if (m.block == block) {
                REQUIRE(moveParamName(m));
                REQUIRE(host.setParam(m.node, moveParamName(m), m.value));
            }
        const std::uint32_t n = std::min(splits[block % std::size(rp::device_check::kPlanSplits)],
                                         rp::device_check::kPlanFrames - at);
        host.processBlock(n, transport);
        at += n;
    }
    rp_node* digest = host.node(rp::device_check::kPlanDigest);
    REQUIRE(digest);
    const auto* view =
        static_cast<const rp_devtest_digest*>(digest->vt->get_interface(digest, RP_DEVTEST_DIGEST_INTERFACE));
    REQUIRE(view);
    CHECK(host.faultCount() == 0);
    CHECK(view->blocks == r.digestBlocks);
    CHECK(view->hash == r.digest);
    // The figure the image prints beside its own (DeviceCheck.hpp): a change to plan.json, to a node it names or to the
    // case moves it, and this says the new value. It is float arithmetic, so it is linux-x64 GCC 13's figure and held
    // only there, as parity and the kit and render baselines hold theirs (rp::buildTarget's key): another compiler or
    // architecture may contract a multiply-add or round a sine otherwise. The comparison above, the loader's run
    // against GraphHost's in one binary, holds everywhere.
    if (rp::buildTarget() == "linux-x64-gnu13") CHECK(r.digest == rp::device_check::kPlanDesktopDigest);
}

// The blob holds no float arithmetic, so its bytes are the same on every host, as plan-blob.hex's and box-plan.hex's
// are: its settings are msgpack written from the parsed settings, each number the bits the JSON reader parsed (one
// reader, from one source, on every host), the sample rate is a double's bits, and the rest are integers in the
// planner's own order. Only the header's arena estimate follows the host's sizes, and the golden does not hold it.
TEST_CASE("the device check's plan is its golden's bytes, as the image's build exports and embeds it",
          "[device-mode][device-check]") {
    std::string hex;
    char        digits[3];
    for (std::size_t i = 0; i < kPlanBlob.size(); ++i) {
        std::snprintf(digits, sizeof digits, "%02x", kPlanBlob[i]);
        hex += digits;
        hex += (i % 32 == 31 || i + 1 == kPlanBlob.size()) ? '\n' : ' ';
    }
    const std::filesystem::path golden = std::filesystem::path(RP_DEVICE_MODE_GOLDEN_DIR) / "device-check-plan.hex";
    if (const char* update = std::getenv("RP_PLAN_BLOB_UPDATE"); update && std::string(update) == "1") {
        std::ofstream(golden, std::ios::binary) << hex;
        WARN("recorded " << golden.string());
    }
    std::ifstream     in(golden, std::ios::binary);
    const std::string want((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(want == hex);
}

TEST_CASE("the device check's plan check stops at the step that fails, and names the node",
          "[device-mode][device-check]") {
    REQUIRE(rp_devtest_plan_live_device_mode() == 0);

    SECTION("a refused blob") {
        std::vector<std::uint8_t> blob = kPlanBlob;
        blob[blob.size() / 2] ^= 0x40;
        const PlanRun run = runPlanCase(planCase(blob), devicePackages());
        CHECK(run.result.step == PlanStep::Load);
        CHECK(std::string(run.result.error.code) == "plan-crc");
        CHECK(std::string(run.result.error.node) == "-");
        CHECK(run.result.blocks == 0);
    }
    SECTION("a type the registration lacks") {
        std::vector<const rp_package*> packages;
        for (const rp_package* p : devicePackages())
            if (p != rp_devtest_plan_package_device_mode()) packages.push_back(p);
        const PlanRun run = runPlanCase(planCase(kPlanBlob), packages);
        CHECK(run.result.step == PlanStep::Load);
        CHECK(std::string(run.result.error.code) == "unknown-type");
        CHECK(std::string(run.result.error.node) == "src");
        CHECK(std::string(run.result.error.message) == "no node type \"rp.devtest.source\" is registered");
    }
    SECTION("an arena too small") {
        const PlanRun run = runPlanCase(planCase(kPlanBlob), devicePackages(), 16 * 1024);
        INFO(describe(run.result));
        CHECK(run.result.step == PlanStep::Load);
        CHECK(std::string(run.result.error.code) == "arena");
        CHECK(run.alive == 0);
    }
    // rp.devtest.fault fails at the step its settings name in its device build; its desktop build plans and activates,
    // so the plans export and the loaded plan is what fails.
    SECTION("a node failing part-way through the run-up") {
        const auto blob = exportForCheck(R"({"nodes":[
            {"id":"src","type":"rp.devtest.source"},
            {"id":"f","type":"rp.devtest.fault","settings":{"at":"activate"}},
            {"id":"dg","type":"rp.devtest.digest"}],
          "wires":[{"from":"src.out","to":"dg.in"}]})",
                                         false);
        std::vector<const rp_package*> packages = devicePackages();
        packages.push_back(rp_devtest_fault_package_device_mode());
        const PlanRun run = runPlanCase(planCase(blob, nullptr, 0, 0), packages);
        CHECK(run.result.step == PlanStep::Load);
        CHECK(std::string(run.result.error.code) == "activate-failed");
        CHECK(std::string(run.result.error.node) == "f");
        CHECK(run.alive == 0);  // src unwound, dg never made
        CHECK(run.allocations == 0);
    }
    SECTION("a block that faults a node") {
        const auto blob = exportForCheck(R"({"nodes":[
            {"id":"src","type":"rp.devtest.source"},
            {"id":"f","type":"rp.devtest.fault","settings":{"at":"process"}},
            {"id":"dg","type":"rp.devtest.digest"}],
          "wires":[{"from":"f.out","to":"dg.in"},{"from":"src.events","to":"dg.events"}]})");
        std::vector<const rp_package*> packages = devicePackages();
        packages.push_back(rp_devtest_fault_package_device_mode());
        const PlanRun run = runPlanCase(planCase(blob, nullptr, 0, 0), packages);
        CHECK(run.result.step == PlanStep::Process);
        CHECK(std::string(run.result.error.node) == "f");
        CHECK(std::string(run.result.error.message) == "the block faulted the node");
        CHECK(run.result.blocks == 1);
        CHECK(run.alive == 0);
    }
    SECTION("a block size of 0") {
        static constexpr std::uint32_t kZero[] = {64, 0};
        PlanCase                       c = planCase(kPlanBlob);
        c.splits = kZero;
        c.splitCount = std::size(kZero);
        const PlanRun run = runPlanCase(c, devicePackages());
        CHECK(run.result.step == PlanStep::Case);
        CHECK(std::string(run.result.error.node) == "-");
        CHECK(run.result.blocks == 0);
    }
    SECTION("a plan that takes more arena than its estimate") {
        const PlanRun run = runPlanCase(planCase(kPlanBlob, rp::device_check::kPlanMoves,
                                                 std::size(rp::device_check::kPlanMoves), 1024),
                                        devicePackages());
        CHECK(run.result.step == PlanStep::Estimate);
        CHECK(run.result.arenaBytes == kDeviceCheckPlanArenaEstimate);
        CHECK(run.result.blocks == 0);
        CHECK(run.alive == 0);
    }
    SECTION("a run that does not hold") {
        // A move past the last block is never pushed.
        static constexpr PlanMove kLate[] = {{2, "g", 0, 0.5}, {100000, "k", 0, 0.1}};
        const PlanRun             late = runPlanCase(planCase(kPlanBlob, kLate, std::size(kLate)), devicePackages());
        CHECK(late.result.step == PlanStep::Result);
        CHECK(std::string(late.result.error.node) == "k");
        CHECK(late.result.moves == 1);
        CHECK(late.alive == 0);
        // A digest the plan does not have, and a node that is no digest.
        for (const char* digest : {"nobody", "bq"}) {
            PlanCase c = planCase(kPlanBlob);
            c.digest = digest;
            const PlanRun run = runPlanCase(c, devicePackages());
            CHECK(run.result.step == PlanStep::Result);
            CHECK(std::string(run.result.error.node) == digest);
            CHECK(run.result.frames == rp::device_check::kPlanFrames);
        }
        // A queue that drops: a flood pushes 8 messages a block into a queue of 4.
        {
            const auto blob = exportForCheck(R"({"nodes":[
                {"id":"fl","type":"rp.devtest.flood"},
                {"id":"dg","type":"rp.devtest.digest"}],
              "wires":[{"from":"fl.out","to":"dg.events"}]})");
            std::vector<const rp_package*> packages = devicePackages();
            packages.push_back(rp_devtest_misfit_package_device_mode());
            const PlanRun run = runPlanCase(planCase(blob, nullptr, 0, 0), packages);
            CHECK(run.result.step == PlanStep::Result);
            CHECK(std::string(run.result.error.node) == "fl");
            CHECK(std::string(run.result.error.message) == "a full output queue dropped messages");
            CHECK(run.result.drops == 4 * run.result.blocks);
            CHECK(run.alive == 0);
        }
        // A digest that counts other blocks than ran: one a plan skipped every other block of.
        {
            const auto blob = exportForCheck(R"({"nodes":[{"id":"dg","type":"rp.devtest.miscount"}],"wires":[]})");
            std::vector<const rp_package*> packages = devicePackages();
            packages.push_back(rp_devtest_misfit_package_device_mode());
            const PlanRun run = runPlanCase(planCase(blob, nullptr, 0, 0), packages);
            CHECK(run.result.step == PlanStep::Result);
            CHECK(std::string(run.result.error.node) == "dg");
            CHECK(std::string(run.result.error.message) == "the digest counted other blocks than ran");
        }
        // A digest that hears nothing: the fault node's silence, with no events or control behind it.
        const auto blob = exportForCheck(R"({"nodes":[
            {"id":"f","type":"rp.devtest.fault","settings":{"at":"none"}},
            {"id":"dg","type":"rp.devtest.digest"}],
          "wires":[{"from":"f.out","to":"dg.in"}]})");
        std::vector<const rp_package*> packages = devicePackages();
        packages.push_back(rp_devtest_fault_package_device_mode());
        const PlanRun deaf = runPlanCase(planCase(blob, nullptr, 0, 0), packages);
        CHECK(deaf.result.step == PlanStep::Result);
        CHECK(std::string(deaf.result.error.node) == "dg");
        CHECK(std::string(deaf.result.error.message) == "the digest heard nothing but silence");
    }
    SECTION("a move with no stream to write") {
        static constexpr PlanMove kNobody[] = {{0, "nobody", 0, 1.0}};
        const PlanRun             nobody = runPlanCase(planCase(kPlanBlob, kNobody, 1), devicePackages());
        CHECK(nobody.result.step == PlanStep::Move);
        CHECK(std::string(nobody.result.error.node) == "nobody");
        CHECK(nobody.result.blocks == 0);
        CHECK(nobody.alive == 0);
        // The biquad's Q (its params: frequency, q, gainDb) is driven by the constant's wire, so a host writes none.
        static constexpr PlanMove kWired[] = {{3, "bq", 1, 2.0}};
        const PlanRun             wired = runPlanCase(planCase(kPlanBlob, kWired, 1), devicePackages());
        CHECK(wired.result.step == PlanStep::Move);
        CHECK(std::string(wired.result.error.node) == "bq");
    }
}

// ---- the script pack ------------------------------------------------------------------------------------------------

namespace {

using rp::device_check::BenchResult;
using rp::device_check::BenchStep;

const std::vector<std::uint8_t> kScriptPlanBlob(std::begin(kDeviceCheckScriptPlan), std::end(kDeviceCheckScriptPlan));

// The export's estimate for the script plan at this width. The exporter sizes a pack's memory at the box's width from
// its manifest's heap (boxPackageBytes; MqjsHost.hpp), and the image's own is what its registration gives: so on the
// image the estimate holds the pack's memory with the room between the two heaps, and here the pack's memory at this
// width stands in for the box's (as BoxHost.test.cpp's arena does for the box's plan).
std::size_t scriptPlanEstimate() {
    const rp_package* pack = imageScriptPack();
    rp_memory_req     req{};
    rp_error          err{};
    REQUIRE(pack->memory->query(pack, &req, &err));
    return kDeviceCheckScriptPlanArenaEstimate - rp::mqjs::boxPackageBytes(imageScriptSource()) + req.size;
}

// The image's script plan case (main.cpp's kScriptPlan) over `blob`, its context held to this width's figure for the
// image's heap, its arena to the export's estimate at this width, untimed (the desktop has no cycle counter the check
// reads).
PlanCase scriptPlanCase(const std::vector<std::uint8_t>& blob = kScriptPlanBlob) {
    PlanCase c;
    c.blob = blob.data();
    c.blobSize = blob.size();
    c.arenaEstimate = scriptPlanEstimate();
    c.frames = rp::device_check::kScriptPlanFrames;
    c.splits = rp::device_check::kScriptPlanSplits;
    c.splitCount = std::size(rp::device_check::kScriptPlanSplits);
    c.digest = rp::device_check::kScriptPlanDigest;
    c.stimulus = true;
    c.expectDigest = true;
    c.want = rp::device_check::kScriptPlanWant;
    c.contextBytes = kImageContextBytes;
    return c;
}

// `packages` with the image's script pack replaced by `pack`.
std::vector<const rp_package*> withScriptPack(const rp_package* pack) {
    std::vector<const rp_package*> packages = devicePackages();
    for (const rp_package*& p : packages)
        if (p->memory) p = pack;
    return packages;
}

// The messages the stimulus hands a run of `blocks` blocks.
std::uint32_t stimulusMessages(std::uint32_t blocks) {
    std::uint32_t n = 0;
    for (std::uint32_t b = 0; b < blocks; ++b) n += rp::device_check::stimulusCount(b);
    return n;
}

struct BenchRun {
    BenchResult result{};
    int         allocations = -1;
};

// benchScript as the image runs it, untimed, in an arena of `arenaBytes` made before counting starts.
BenchRun runBench(const std::vector<const rp_package*>& packages, const std::vector<TypeCase>& cases = kAllCases,
                  std::size_t arenaBytes = rp::device_check::kArenaBytes, std::uint32_t rounds = 4) {
    BenchRun                      r;
    std::vector<std::max_align_t> arena(arenaBytes / sizeof(std::max_align_t) + 1);
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    rp::device_check::benchScript(packages.data(), packages.size(), cases.data(), cases.size(), arena.data(),
                                  arenaBytes, rounds, nullptr, &r.result);
    r.allocations = rp::test::endCountingAllocations().allocations;
    return r;
}

}  // namespace

TEST_CASE("the device check registers the devtest script pack with the image's heap, and checks its types with the "
          "rest",
          "[device-mode][device-check][script-pack]") {
    const rp_package* pack = imageScriptPack();
    REQUIRE(pack->memory);
    CHECK(std::string(pack->memory->name) == "devtest");
    CHECK(idsOf(pack) == std::vector<std::string>{"rp.devtest.script-transpose", "rp.devtest.script-nest",
                                                  "rp.devtest.script-loop"});
    rp_memory_req req{};
    rp_error      err{};
    REQUIRE(pack->memory->query(pack, &req, &err));
    // Its memory is the host's state, the bytecode's copy and a context of the image's figure at this width.
    CHECK(req.size > kImageContextBytes);
    CHECK(req.size < kImageContextBytes + 64 * 1024);
    // At the box's width, the image's figure exactly beside the state and the 32-bit bytecode.
    REQUIRE(pack->memory->query_target);
    rp_memory_req box{};
    REQUIRE(pack->memory->query_target(pack, 4, &box, &err));
    CHECK(box.size > RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES);
    CHECK(box.size < std::size_t{RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES} + 32 * 1024);

    // Its cases: a script type's message queues are the device MIDI input's, whatever the planner's default.
    std::size_t scriptCases = 0;
    for (const TypeCase& c : kAllCases) {
        if (std::string(c.id).rfind("rp.devtest.script-", 0) != 0) continue;
        ++scriptCases;
        for (std::uint32_t i = 0; i < c.portCount; ++i) {
            INFO(c.id << " port " << i);
            CHECK(c.ports[i].messages == RP_DEVICE_MIDI_IN_MESSAGES);
            CHECK(c.ports[i].bytes == RP_DEVICE_MIDI_IN_BYTES);
        }
    }
    CHECK(scriptCases == 3);
}

TEST_CASE("the device check stops at a script type whose package memory does not fit, and names it",
          "[device-mode][device-check][script-pack]") {
    std::vector<TypeCase> cases;
    for (const TypeCase& c : kAllCases)
        if (std::string(c.id).rfind("rp.devtest.script-", 0) == 0) cases.push_back(c);
    REQUIRE(cases.size() == 3);
    const Result fits = runCheck({imageScriptPack()}, cases);
    INFO(fits.error.message);
    CHECK(fits.step == Step::Ok);
    CHECK(fits.types == 3);
    // The pack's memory takes the most of any type's run: the context and the bytecode's copy, beside its nodes.
    CHECK(fits.arenaBytes > kImageContextBytes);

    const Result r = runCheck({imageScriptPack()}, cases, 16 * 1024);
    CHECK(r.step == Step::Package);
    CHECK(std::string(r.id) == "rp.devtest.script-transpose");
    CHECK(std::string(r.error.message) == "the package's memory does not fit the arena");

    // A context too small for the pack's runtime to define its nodes in: its setup refuses, in its words.
    const Result small = runCheck({imageScriptPack(4096)}, cases);
    CHECK(small.step == Step::Package);
    CHECK(std::string(small.error.message).find("devtest") != std::string::npos);
}

TEST_CASE("the device check's script plan runs the stimulus through two script nodes behind the device MIDI input, "
          "to the digest the image holds it to, in the image's arena, allocating nothing",
          "[device-mode][device-check][script-pack]") {
    REQUIRE(rp_devtest_plan_live_device_mode() == 0);
    const PlanRun run = runPlanCase(scriptPlanCase(), devicePackages());
    const PlanResult& r = run.result;
    INFO(describe(r) << "; the digest 0x" << std::hex << r.digest << std::dec);
    REQUIRE(r.step == PlanStep::Ok);
    CHECK(run.allocations == 0);
    CHECK(run.frees == 0);
    CHECK(run.alive == 0);
    CHECK(r.nodes == 4);
    CHECK(r.frames == rp::device_check::kScriptPlanFrames);
    CHECK(r.blocks == 40);
    CHECK(r.messages == stimulusMessages(40));
    CHECK(r.drops == 0);
    CHECK(r.digestBlocks == r.blocks);
    CHECK(r.heard);
    // Its digest is of messages alone, integers the script nodes made, so it is the same on every host: the figure the
    // image holds the RP2350's run to (DeviceCheck.hpp), pinned everywhere.
    CHECK(r.digest == rp::device_check::kScriptPlanWant);
    // Its one pack, at the image's heap, with what the run left in it.
    INFO("the pack: " << r.packs[0].inUseBytes << " of " << r.packs[0].contextBytes << " bytes in use at this width, "
                      << r.packs[0].memoryBytes << " placed; " << r.arenaBytes << " arena bytes");
    REQUIRE(r.packCount == 1);
    CHECK(std::string(r.packs[0].name) == "devtest");
    CHECK(r.packs[0].contextBytes == kImageContextBytes);
    CHECK(r.packs[0].memoryBytes > r.packs[0].contextBytes);
    CHECK(r.packs[0].fixedBytes > 0);
    CHECK(r.packs[0].inUseBytes > r.packs[0].fixedBytes);
    CHECK(r.packs[0].inUseBytes < r.packs[0].contextBytes);
    // The estimate at this width is this width's layout exactly, as the main plan's is; the image's 32-bit layout,
    // with a pack's memory under the manifest's heap's, is under the export's own figure.
    CHECK(r.arenaBytes == scriptPlanEstimate());
    CHECK(rp::mqjs::boxPackageBytes(imageScriptSource()) > std::size_t{RP_DEVICE_CHECK_SCRIPT_HEAP_BYTES});
}

TEST_CASE("the device check's script plan is its golden's bytes, as the image's build exports and embeds it",
          "[device-mode][device-check][script-pack]") {
    std::string hex;
    char        digits[3];
    for (std::size_t i = 0; i < kScriptPlanBlob.size(); ++i) {
        std::snprintf(digits, sizeof digits, "%02x", kScriptPlanBlob[i]);
        hex += digits;
        hex += (i % 32 == 31 || i + 1 == kScriptPlanBlob.size()) ? '\n' : ' ';
    }
    const std::filesystem::path golden =
        std::filesystem::path(RP_DEVICE_MODE_GOLDEN_DIR) / "device-check-script-plan.hex";
    if (const char* update = std::getenv("RP_PLAN_BLOB_UPDATE"); update && std::string(update) == "1") {
        std::ofstream(golden, std::ios::binary) << hex;
        WARN("recorded " << golden.string());
    }
    std::ifstream     in(golden, std::ios::binary);
    const std::string want((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(want == hex);
}

TEST_CASE("the device check's script plan check stops at the step that fails, and says why",
          "[device-mode][device-check][script-pack]") {
    REQUIRE(rp_devtest_plan_live_device_mode() == 0);

    SECTION("a pack whose context is not the image's figure") {
        PlanCase c = scriptPlanCase();
        c.contextBytes = kImageContextBytes + 8;
        const PlanRun run = runPlanCase(c, devicePackages());
        CHECK(run.result.step == PlanStep::Heap);
        CHECK(std::string(run.result.error.node) == "-");
        CHECK(std::string(run.result.error.message) ==
              "a script pack's context is not the size the case says its image declares");
        CHECK(run.result.packCount == 1);
        CHECK(run.alive == 0);
    }
    SECTION("a digest that is not the desktop's") {
        PlanCase c = scriptPlanCase();
        c.want = rp::device_check::kScriptPlanWant ^ 1;
        const PlanRun run = runPlanCase(c, devicePackages());
        CHECK(run.result.step == PlanStep::Result);
        CHECK(std::string(run.result.error.node) == "dg");
        CHECK(std::string(run.result.error.message) == "the digest is not the one the case expects");
        CHECK(run.alive == 0);
    }
    SECTION("a pack whose heap runs out: the script node's fault, in its words") {
        const PlanRun run = runPlanCase(scriptPlanCase(), withScriptPack(imageScriptPack(12288)));
        INFO(describe(run.result));
        CHECK(run.result.step == PlanStep::Process);
        CHECK(run.result.blocks == 1);
        const std::string message = run.result.error.message;
        CHECK(message.rfind("the block faulted the node: ", 0) == 0);
        CHECK(message.find("heap") != std::string::npos);
        CHECK(run.alive == 0);
        CHECK(run.allocations == 0);
    }
    SECTION("a plan without the stimulus: the digest hears nothing") {
        PlanCase c = scriptPlanCase();
        c.stimulus = false;
        const PlanRun run = runPlanCase(c, devicePackages());
        CHECK(run.result.step == PlanStep::Result);
        CHECK(run.result.messages == 0);
        CHECK(std::string(run.result.error.message) == "the digest heard nothing but silence");
    }
}

TEST_CASE("the device check's script bench runs its node through the stimulus's blocks in the image's arena, "
          "allocating nothing",
          "[device-mode][device-check][script-pack]") {
    const BenchRun    run = runBench(devicePackages());
    const BenchResult& r = run.result;
    INFO(r.error.message);
    REQUIRE(r.step == BenchStep::Ok);
    CHECK(run.allocations == 0);
    CHECK(r.blocks == 1 + 3 * 4);
    CHECK(r.noteMessages == rp::device_check::stimulusCount(0));
    CHECK(r.sysexMessages == rp::device_check::stimulusCount(1));
    CHECK(r.messages == 4 * 2 * RP_DEVICE_MIDI_IN_MESSAGES);
    CHECK(r.passed == r.messages);
    CHECK(r.contextBytes == kImageContextBytes);
    CHECK(r.inUseBytes > 0);
    CHECK(r.inUseBytes < r.contextBytes);
    // Untimed here: the desktop passes no counter.
    CHECK(r.firstCycles == 0);
    CHECK(r.noteCycles == 0);
}

TEST_CASE("the device check's script bench stops at the step that fails, and says why",
          "[device-mode][device-check][script-pack]") {
    SECTION("no registered type is the bench's") {
        std::vector<const rp_package*> packages;
        for (const rp_package* p : devicePackages())
            if (!p->memory) packages.push_back(p);
        const BenchRun run = runBench(packages);
        CHECK(run.result.step == BenchStep::Type);
        CHECK(std::string(run.result.error.message) == "no registered type, or no case, is the bench's");
    }
    SECTION("its package's memory does not fit the arena") {
        const BenchRun run = runBench(devicePackages(), kAllCases, 16 * 1024);
        CHECK(run.result.step == BenchStep::Package);
        CHECK(std::string(run.result.error.message) == "the package's memory does not fit the arena");
    }
    SECTION("a case with no queue to feed") {
        std::vector<TypeCase> cases = kAllCases;
        for (TypeCase& c : cases)
            if (std::string(c.id) == rp::device_check::kBenchType) c.portCount = 0;
        const BenchRun run = runBench(devicePackages(), cases);
        CHECK(run.result.step == BenchStep::Ports);
        CHECK(std::string(run.result.error.message) ==
              "it has no events or bytes input and output to feed and read");
        CHECK(run.allocations == 0);
    }
    SECTION("a heap that runs out: its fault, in its words") {
        const BenchRun run = runBench(withScriptPack(imageScriptPack(12288)));
        INFO(run.result.error.message);
        CHECK(run.result.step == BenchStep::Process);
        const std::string message = run.result.error.message;
        CHECK(message.find("heap") != std::string::npos);
        CHECK(run.allocations == 0);
    }
}

TEST_CASE("the device check's host gives a script node the clock the image offers, by which a node past its deadline "
          "faults, in its words",
          "[device-mode][device-check][script-pack]") {
    // rp.scripttest.spins, which never returns, in a plan of its own; its pack at a heap the image's arena holds.
    const rp_package*       pack = imageScriptPackage(rp_script_scripttest, 65536);
    rp::graph::NodeRegistry registry;
    std::string             why;
    REQUIRE(registry.addPackage(rp_scripttest_script_package(), &why));
    std::vector<const rp_package*> types = devicePackages();
    types.push_back(pack);
    rp::graph::ExportOptions o;
    o.sampleRate = 48000.0;
    o.maxFrames = 256;
    o.arenaBytes = rp::device_check::kArenaBytes;
    o.trialLoad = false;  // the exporter's own deadline would fault it first
    const auto exported =
        rp::graph::exportPlan(R"({"nodes":[{"id":"sp","type":"rp.scripttest.spins"}],"wires":[]})", registry, types, o);
    REQUIRE(exported.ok());

    // A clock a millisecond further on at each reading, as the engine polls it while the node spins: the image's
    // (time_us_64) by the host's figure, kHostClockDeadlineMicros, as a block that hands the node no message has it.
    struct Ticks {
        static std::uint64_t read(void* ctx) { return *static_cast<std::uint64_t*>(ctx) += 1000; }
    };
    std::uint64_t       now = 0;
    const rp_host_clock clock{&now, &Ticks::read, 0};
    // Offered for this case alone: taken back as it ends, however it ends, so no later case meets a clock of this
    // frame's.
    struct Offered {
        explicit Offered(const rp_host_clock* c) { rp::device_check::offerClock(c); }
        ~Offered() { rp::device_check::offerClock(nullptr); }
    } offered(&clock);
    PlanCase c;
    c.blob = exported.blob.data();
    c.blobSize = exported.blob.size();
    c.frames = 256;
    const PlanRun run = runPlanCase(c, types);
    INFO(describe(run.result));
    CHECK(run.result.step == PlanStep::Process);
    CHECK(std::string(run.result.error.node) == "sp");
    CHECK(std::string(run.result.error.message) ==
          "the block faulted the node: process ran past its deadline of " +
              std::to_string(rp::mqjs::kHostClockDeadlineMicros) + " us");
    CHECK(run.result.blocks == 1);
    CHECK(run.allocations == 0);
    CHECK(now > rp::mqjs::kHostClockDeadlineMicros);
}

namespace {

struct LoopRun {
    LoopResult result{};
    int        allocations = -1;
};

// The deadline case as the image runs it, in an arena of `arenaBytes` made before counting starts, over the image's
// script pack registered at `heapBytes`, under a clock of the thread's CPU time at the host's default deadline (the
// image offers the box's, time_us_64; with none, a device build's script node has no deadline, and a loop no end),
// its blocks timed by the steady clock.
std::uint64_t steadyMicros() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
LoopRun runLoops(std::uint32_t heapBytes = rp::device_check::kLoopHeapBytes,
                 const std::vector<TypeCase>& cases = kAllCases,
                 std::size_t arenaBytes = rp::device_check::kArenaBytes) {
    const rp_host_clock clock{nullptr, &rp::graph::threadCpuMicros, 0};
    struct Offered {
        explicit Offered(const rp_host_clock* c) { rp::device_check::offerClock(c); }
        ~Offered() { rp::device_check::offerClock(nullptr); }
    } offered(&clock);
    const rp_package*             pack = imageScriptPack(heapBytes);
    LoopRun                       r;
    std::vector<std::max_align_t> arena(arenaBytes / sizeof(std::max_align_t) + 1);
    rp::test::beginCountingAllocations(rp::test::countsCAllocations());
    rp::device_check::loopDeadline(pack, cases.data(), cases.size(), arena.data(), arenaBytes, &steadyMicros,
                                   &r.result);
    r.allocations = rp::test::endCountingAllocations().allocations;
    return r;
}

}  // namespace

TEST_CASE("the device check's deadline case faults every loop over a builtin by its deadline, in the box's pack's "
          "heap, allocating nothing",
          "[device-mode][device-check][script-pack]") {
    const LoopRun     run = runLoops();
    const LoopResult& r = run.result;
    INFO(r.error.message);
    REQUIRE(r.step == LoopStep::Ok);
    CHECK(run.allocations == 0);
    CHECK(r.contextBytes == std::size_t{rp::device_check::kLoopHeapBytes} * rp::mqjs::kDeviceHalves / 2);
    for (std::uint32_t k = 0; k < rp::device_check::kLoopCount; ++k) {
        INFO(rp::device_check::kLoops[k].loop);
        // The sort's and forEach's data takes a block for each 256 numbers first; the search's string, one.
        const std::uint32_t count = rp::device_check::kLoops[k].count;
        const std::string   loop = rp::device_check::kLoops[k].loop;
        const bool          numbers = loop == "sort" || loop == "forEach" || loop == "stringify";
        const std::uint32_t data = numbers ? (count + 255) / 256 : loop == "search" ? 1u : 0u;
        CHECK(r.blocks[k] == data + 1);
        // Its last block ran to its deadline (10 ms of CPU time) and not much past it: a generous bound by the wall
        // clock, which a loaded machine stretches.
        CHECK(r.micros[k] >= rp::mqjs::kHostClockDeadlineMicros);
        CHECK(r.micros[k] < 2000000);
    }
}

TEST_CASE("the device check's deadline case stops at the step that fails, and says why",
          "[device-mode][device-check][script-pack]") {
    SECTION("no case is the loop type's") {
        std::vector<TypeCase> cases;
        for (const TypeCase& c : kAllCases)
            if (std::string(c.id) != rp::device_check::kLoopType) cases.push_back(c);
        const LoopRun run = runLoops(rp::device_check::kLoopHeapBytes, cases);
        CHECK(run.result.step == LoopStep::Type);
        CHECK(std::string(run.result.error.message) == "the package has no such type, or no case is the type's");
    }
    SECTION("its package's memory does not fit the arena") {
        const LoopRun run = runLoops(rp::device_check::kLoopHeapBytes, kAllCases, 16 * 1024);
        CHECK(run.result.step == LoopStep::Package);
        CHECK(std::string(run.result.error.message) == "the package's memory does not fit the arena");
    }
    SECTION("a loop that ends in another fault: a heap too small for its data, in its words") {
        const LoopRun run = runLoops(16384);
        INFO(run.result.error.message);
        CHECK(run.result.step == LoopStep::Result);
        CHECK(std::string(rp::device_check::kLoops[run.result.loop].loop) == "sort");
        const std::string message = run.result.error.message;
        CHECK(message.rfind("its blocks did not end in its process call's deadline: ", 0) == 0);
        CHECK(message.find("heap") != std::string::npos);
        CHECK(run.allocations == 0);
    }
}
