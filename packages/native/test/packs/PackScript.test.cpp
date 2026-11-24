// A pack's script half (docs/downloadable-packs-m2d-plan.md): a device script pack shipped as the ES5 source the
// device-pack toolchain writes, compiled when the pack loads. Here: the host's compiler, run on the toolchain's own
// bundles. Its bytes are not mqjs -o's: mqjs compiles against upstream's standard library and this host against
// RetroPlug's (upstream's tables and the node SDK's names), and a bytecode's references to the library's atoms are the
// table's own (deps/mquickjs's relocation: "add atom checksum to avoid problems if the stdlib is modified"). Compiled
// here, a pack is bound to the table that runs it; what holds it is that it is the same each time and that the host
// loads it, and BlipToaster's pack behaving as its static build (below).
//
// Run via `pnpm test:plugin packs-test` or `build/bin/retroplug-packs-test "[packs-script]"`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "GraphDocs.hpp"
#include "PackTestSupport.hpp"
#include "graph/Packages.hpp"
#include "host/engine/Engine.hpp"
#include "host/packs/PackTable.hpp"
#include "script/MqjsHost.hpp"
#include "script/rp_mqjs_pack.h"

namespace fs = std::filesystem;

namespace {

// The device-pack toolchain's outputs for a package's script pack (cmake/packages.cmake's retroplug_device_script_pack).
fs::path toolchainOut(const std::string& name) { return fs::path(RP_SCRIPT_PACKS_BUILD_DIR) / name / "script"; }

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A compiled pack as the MicroQuickJS host takes it. The host keeps a pointer to the source for the life of the
// process, so every one this file registers is kept too.
struct Compiled {
    std::string               name;
    std::vector<std::uint8_t> runtime, control;
    std::vector<std::string>  ids;
    std::vector<const char*>  idPtrs;
    rp_mqjs_pack_source       source{};
};
std::vector<std::unique_ptr<Compiled>>& kept() {
    static std::vector<std::unique_ptr<Compiled>> all;
    return all;
}

}  // namespace

TEST_CASE("compileScript compiles a pack's bundles to the same bytes each time, and the host registers them",
          "[packs-script]") {
    struct Pack {
        std::string              name;
        std::uint32_t            heapBytes;
        std::vector<std::string> ids;
    };
    for (const Pack& pack : {Pack{"devtest", 57344,
                                  {"rp.devtest.script-transpose", "rp.devtest.script-nest", "rp.devtest.script-loop"}},
                             Pack{"bliptoaster", 24576, {"rp.bliptoaster.midi-policy"}}}) {
        INFO(pack.name);
        const fs::path out = toolchainOut(pack.name);
        const std::string whole = readText(out / (pack.name + ".js"));
        const std::string runtime = readText(out / (pack.name + ".device.js"));
        const std::string wholeName = pack.name + ".js", runtimeName = pack.name + ".device.js";
        std::string why;
        const auto control = rp::mqjs::compileScript(whole, wholeName.c_str(), false, &why);
        INFO(why);
        REQUIRE(control);
        const auto device = rp::mqjs::compileScript(runtime, runtimeName.c_str(), false, &why);
        REQUIRE(device);
        const auto box = rp::mqjs::compileScript(runtime, runtimeName.c_str(), true, &why);
        REQUIRE(box);
        // The same bytes every time: the heap's address never reaches them (relocated to 0, the header zeroed, the
        // rounding bytes after each string and byte array cleared).
        CHECK(*control == *rp::mqjs::compileScript(whole, wholeName.c_str(), false, &why));
        CHECK(*device == *rp::mqjs::compileScript(runtime, runtimeName.c_str(), false, &why));
        CHECK(*box == *rp::mqjs::compileScript(runtime, runtimeName.c_str(), true, &why));
        CHECK(box->size() < device->size());  // the box's words are half the width

        // Registered as a pack's are: the desktop's control context loads the whole pack and holds its definitions to
        // the ids, and a node's context loads the runtime when a plan places one (the cases below).
        auto c = std::make_unique<Compiled>();
        c->name = "compiled-" + pack.name;
        c->runtime = *device;
        c->control = *control;
        c->ids = pack.ids;
        for (const auto& id : c->ids) c->idPtrs.push_back(id.c_str());
        c->source = rp_mqjs_pack_source{c->name.c_str(), c->runtime.data(), static_cast<std::uint32_t>(c->runtime.size()),
                                        pack.heapBytes, static_cast<std::uint32_t>(c->idPtrs.size()), c->idPtrs.data(),
                                        static_cast<std::uint32_t>(box->size()), c->control.data(),
                                        static_cast<std::uint32_t>(c->control.size())};
        kept().push_back(std::move(c));
        const rp_package* registered = rp::mqjs::package(kept().back()->source);
        INFO(rp::mqjs::packageRefusal());
        REQUIRE(registered != nullptr);
        CHECK(registered->node_type_count == pack.ids.size());
    }
}

TEST_CASE("compileScript refuses a script that does not parse, naming the file", "[packs-script]") {
    std::string why;
    CHECK_FALSE(rp::mqjs::compileScript("var x = ;", "broken.js", false, &why));
    CHECK(why.find("broken.js") != std::string::npos);
}

using Catch::Matchers::ContainsSubstring;
using rp::host::packs::PackLoad;
using rp::host::packs::PackTable;
using rp::host::packs::test::fixture;

TEST_CASE("a script half compiles at load and its package joins the pack's", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    const PackLoad load = table.load(fixture("scriptfix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->packages.size() == 1);
    const rp_package* p = load.pack->packages[0];
    REQUIRE(p->node_type_count == 3);
    CHECK(std::string(p->node_types[0]->id) == "rp.devtest.script-transpose");
    CHECK(std::string(p->node_types[2]->id) == "rp.devtest.script-loop");
    CHECK(load.pack->packageIds == std::vector<std::string>{"rp.scriptfix.script"});
}

TEST_CASE("a script half is refused, and the pack with it, for each fault", "[packs-script]") {
    // Each variant but the twin is refused in a table of its own: its types would clash with scriptfix's first, which
    // is refused before anything is compiled.
    const auto refused = [&](const char* id, const char* reason, bool afterGood = false) {
        PackTable table(rp::graph::productPackages());
        if (afterGood) REQUIRE(table.load(fixture("scriptfix")));
        INFO(id);
        const PackLoad load = table.load(fixture(id));
        INFO(load.error);
        CHECK_FALSE(load);
        CHECK_THAT(load.error, ContainsSubstring(reason));
        CHECK(table.find(id) == nullptr);
    };
    refused("scriptfix-twin", "registers node type rp.devtest.script-transpose, which package rp.scriptfix.script "
                              "has registered already", true);
    refused("scriptfix-types", "its script half is refused");
    refused("scriptfix-syntax", "its script half (js/script.js) does not compile: scriptfix-syntax.js");
    refused("scriptfix-nofile", "script.runtime names js/missing.js, which the pack does not have");
}

TEST_CASE("every Engine registers a loaded script half, and a document places its types", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    PackTable none(rp::graph::productPackages());
    REQUIRE(table.load(fixture("scriptfix")));
    Engine bare(48000.0, none);
    Engine e(48000.0, table);
    CHECK(e.nodeTypeCount() == bare.nodeTypeCount() + 3);
    const std::string doc =
        R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"t","type":"rp.devtest.script-transpose",)"
        R"("settings":{"semitones":12}},{"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
        R"("wires":[{"from":"in.midi","to":"t.in"},{"from":"t.out","to":"out.in1"}]})";
    const auto placed = e.setGraph(rp::host::test::graphRequest(doc, {}));
    INFO((placed.errors.empty() ? std::string() : placed.errors[0].message));
    CHECK(placed.ok);
}

TEST_CASE("a script half whose types clash is refused before it takes a MicroQuickJS pack slot", "[packs-script]") {
    PackTable table(rp::graph::productPackages());
    REQUIRE(table.load(fixture("scriptfix")));
    // The host holds a few dozen packs for the life of the process, so a refusal that registered would run it out.
    for (int i = 0; i < 40; ++i) CHECK_FALSE(table.load(fixture("scriptfix-twin")));
    PackTable later(rp::graph::productPackages());
    const PackLoad load = later.load(fixture("scriptfix"));
    INFO(load.error);
    CHECK(load);
}
