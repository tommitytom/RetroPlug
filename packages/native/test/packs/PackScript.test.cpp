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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

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
