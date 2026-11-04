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
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "DeviceCheck.hpp"
#include "DeviceCheckCases.hpp"
#include "DeviceModeEntries.hpp"
#include "N8Protocol.hpp"
#include "graph/CountingAllocator.hpp"
#include "graph/Descriptor.hpp"
#include "graph/JsonToMsgpack.hpp"

// Generated (device-entries.cpp): every manifest's `device` entries, with both builds.
std::vector<DeviceEntry> deviceEntries();

namespace {

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
// manifest marks `device` is left out here, and the test that reads the header refuses it.
std::vector<DeviceEntry> checkedEntries() {
    std::vector<DeviceEntry> out;
    std::istringstream       words(rp::device_check::kEntries);
    const auto               all = deviceEntries();
    for (std::string w; words >> w;)
        for (const DeviceEntry& e : all)
            if (w == e.entry) out.push_back(e);
    return out;
}

std::vector<const rp_package*> devicePackages() {
    std::vector<const rp_package*> packages;
    for (const DeviceEntry& e : checkedEntries()) packages.push_back(e.device());
    return packages;
}

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
    std::vector<std::string> registered;
    for (const std::string& n : named) {
        const auto e = std::find_if(entries.begin(), entries.end(), [&](const DeviceEntry& d) { return n == d.entry; });
        INFO("the cases name " << n << ", which no manifest marks `device`");
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
