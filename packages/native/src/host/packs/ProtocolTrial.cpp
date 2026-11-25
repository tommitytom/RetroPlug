#include "host/packs/ProtocolTrial.hpp"

#include <utility>

#include "graph/NodeRegistry.hpp"
#include "graph/Descriptor.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/script/ScriptHost.hpp"

namespace rp::host::packs {

ProtocolTrial protocolTrial() {
    return [](const std::string& scriptId, const std::string& source, std::string* why) -> std::optional<ProtocolHalf> {
        const auto fail = [&](std::string reason) -> std::optional<ProtocolHalf> {
            if (why) *why = std::move(reason);
            return std::nullopt;
        };
        std::string error;
        auto bytecode = dsp::compileToBytecode(source, ScriptHost::kStackBytes, &error);
        if (!bytecode) return fail("its protocol half does not compile: " + error);
        ScriptHost host;
        const ScriptHost::Chunk builtIn[] = {ScriptHost::embeddedBundle()};
        if (!host.load(builtIn, &error)) return fail("the built-in script nodes did not load beside it: " + error);
        const ScriptHost::Chunk chunk{*bytecode};
        const rp_package* package = host.loadAppPack(scriptId, {&chunk, 1}, 0, &error);
        if (!package) return fail("its protocol half did not load: " + error);
        rp::graph::NodeRegistry registry;
        std::string clash;
        const bool added = registry.addPackage(host.package(), &clash) && registry.addPackage(package, &clash);
        ProtocolHalf half{scriptId, std::move(*bytecode), {}, {}};
        if (added) {
            for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
                const std::string id = package->node_types[k]->id;
                half.typeIds.push_back(id);
                half.descriptors.push_back(rp::graph::descriptorJson(registry.find(id)->descriptor));
            }
        }
        host.discardPack(package);
        if (!added) return fail("its protocol half's nodes were refused: " + clash);
        return half;
    };
}

}  // namespace rp::host::packs
