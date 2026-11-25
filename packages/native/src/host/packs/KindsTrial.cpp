#include "host/packs/KindsTrial.hpp"

#include <utility>

#include "host/dsp/ScriptCompiler.hpp"
#include "host/script/ScriptHost.hpp"

namespace rp::host::packs {

KindsTrial kindsTrial() {
    return [](const std::string& source, std::string* why) -> std::optional<KindsHalf> {
        const auto fail = [&](std::string reason) -> std::optional<KindsHalf> {
            if (why) *why = std::move(reason);
            return std::nullopt;
        };
        std::string error;
        auto bytecode = dsp::compileToBytecode(source, ScriptHost::kStackBytes, &error);
        if (!bytecode) return fail("its kinds half does not compile: " + error);
        ScriptHost host;
        const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), ScriptHost::Chunk{*bytecode}};
        if (!host.load(chunks, &error))
            return fail("its kinds half did not run beside the built-in script nodes: " + error);
        return KindsHalf{std::move(*bytecode)};
    };
}

}  // namespace rp::host::packs
