#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "graph/rp_node.h"
#include "host/packs/PackManifest.hpp"
#include "script/rp_mqjs_pack.h"

// A pack's script half (docs/downloadable-packs-m2d-plan.md): a device script pack, shipped as the two ES5 bundles the
// device-pack toolchain writes (the whole pack, halves.script, and the device's runtime, script.runtime), compiled to
// MicroQuickJS bytecode here and registered on the MicroQuickJS host (rp::mqjs::package) as a built-in device pack's
// generated registration does. What it holds is pointed at by that registration for the life of the process, so a
// ScriptHalf is never freed once compiled (PackTable keeps every one).
namespace rp::host::packs {

struct ScriptHalf {
    std::string               name;      // the pack's id: the MicroQuickJS pack's name, its package "rp.<id>.script"
    std::vector<std::uint8_t> runtime;   // the device runtime's bytecode at this build's width
    std::vector<std::uint8_t> control;   // the whole pack's, which the desktop's control context runs
    std::vector<std::string>  typeIds;   // script.nodeTypes, in their order
    std::vector<const char*>  typeIdPtrs;
    rp_mqjs_pack_source       source{};  // what package() was handed: points into the members above
    const rp_package*         package = nullptr;  // set once registered
};

// The pack's script half read from `dir` and compiled (null, with the reason in *why, for a file it cannot read or a
// bundle that does not compile). Not registered yet: registerScriptHalf does that, once the caller holds the half.
std::unique_ptr<ScriptHalf> compileScriptHalf(const std::filesystem::path& dir, const PackManifest& manifest,
                                              std::string* why);

// Register a compiled half on the MicroQuickJS host, which loads its whole pack in a control context and refuses one
// whose definitions are not its type ids. False with the host's reason in *why.
bool registerScriptHalf(ScriptHalf& half, std::string* why);

}  // namespace rp::host::packs
