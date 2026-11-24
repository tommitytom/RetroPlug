#include "host/packs/PackScript.hpp"

#include <fstream>
#include <iterator>
#include <optional>

#include "host/packs/PackDirs.hpp"
#include "script/MqjsHost.hpp"

namespace fs = std::filesystem;

namespace rp::host::packs {

namespace {

std::optional<std::string> readText(const fs::path& file, std::string* why) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (why) *why = "cannot read " + displayPath(file);
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

std::unique_ptr<ScriptHalf> compileScriptHalf(const fs::path& dir, const PackManifest& manifest, std::string* why) {
    const ScriptSpec& spec = *manifest.script;
    const std::string& wholeFile = manifest.halves.at("script");
    const auto whole = readText(dir / fs::path(wholeFile), why);
    if (!whole) return nullptr;
    const auto runtime = readText(dir / fs::path(spec.runtime), why);
    if (!runtime) return nullptr;
    // The names the device-pack toolchain compiles the two bundles under (the bytecode records its file's name), so a
    // compile error names the file as the toolchain's would.
    const std::string wholeName = manifest.id + ".js";
    const std::string runtimeName = manifest.id + ".device.js";
    std::string err;
    auto control = rp::mqjs::compileScript(*whole, wholeName.c_str(), false, &err);
    if (!control) {
        if (why) *why = "its script half (" + wholeFile + ") does not compile: " + err;
        return nullptr;
    }
    auto device = rp::mqjs::compileScript(*runtime, runtimeName.c_str(), false, &err);
    auto box = device ? rp::mqjs::compileScript(*runtime, runtimeName.c_str(), true, &err) : std::nullopt;
    if (!device || !box) {
        if (why) *why = "its script half's runtime (" + spec.runtime + ") does not compile: " + err;
        return nullptr;
    }
    auto half = std::make_unique<ScriptHalf>();
    half->name = manifest.id;
    half->runtime = std::move(*device);
    half->control = std::move(*control);
    half->typeIds = spec.nodeTypes;
    for (const auto& t : half->typeIds) half->typeIdPtrs.push_back(t.c_str());
    half->source = rp_mqjs_pack_source{half->name.c_str(),
                                       half->runtime.data(),
                                       static_cast<std::uint32_t>(half->runtime.size()),
                                       spec.heapBytes,
                                       static_cast<std::uint32_t>(half->typeIdPtrs.size()),
                                       half->typeIdPtrs.data(),
                                       static_cast<std::uint32_t>(box->size()),
                                       half->control.data(),
                                       static_cast<std::uint32_t>(half->control.size())};
    return half;
}

bool registerScriptHalf(ScriptHalf& half, std::string* why) {
    half.package = rp::mqjs::package(half.source);
    if (half.package == nullptr) {
        if (why) *why = std::string("its script half is refused: ") + rp::mqjs::packageRefusal();
        return false;
    }
    return true;
}

}  // namespace rp::host::packs
