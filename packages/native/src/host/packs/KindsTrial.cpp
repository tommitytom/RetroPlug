#include "host/packs/KindsTrial.hpp"

#include <utility>

#include "graph/CanonicalJson.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/script/ScriptHost.hpp"

namespace rp::host::packs {

namespace {

// Runs before the pack's chunk: records each registrar call's kind and ids, from this chunk alone.
const char* const kPrelude =
    "(function () {\n"
    "  var real = globalThis.__rp_registerKindItems;\n"
    "  var calls = {};\n"
    "  globalThis.__rpKindsTrialCalls = calls;\n"
    "  globalThis.__rpKindsNodeCount = (globalThis.__rp_scriptNodes || []).length;\n"
    "  globalThis.__rp_registerKindItems = function (kind, items, ids) {\n"
    "    var out = real(kind, items, ids);\n"
    "    calls[kind] = (calls[kind] || []).concat(ids);\n"
    "    return out;\n"
    "  };\n"
    "})();\n";

// Runs after it: the recorded calls must be the declared ids, kind for kind.
std::string epilogue(const std::map<std::string, std::vector<std::string>>& declared) {
    std::string want = "{";
    bool        first = true;
    for (const auto& [kind, ids] : declared) {
        want += std::string(first ? "" : ",") + rp::graph::jsonString(kind) + ":[";
        for (std::size_t i = 0; i < ids.size(); ++i) want += std::string(i ? "," : "") + rp::graph::jsonString(ids[i]);
        want += "]";
        first = false;
    }
    want += "}";
    return "(function () {\n"
           "  var want = " + want + ";\n"
           "  var got = globalThis.__rpKindsTrialCalls;\n"
           "  var nodes = (globalThis.__rp_scriptNodes || []).length;\n"
           "  if (nodes !== globalThis.__rpKindsNodeCount) {\n"
           "    throw new Error('its kinds half defines script nodes, which a kinds half may not');\n"
           "  }\n"
           "  var text = function (ids) { return ids.length ? ids.join(', ') : 'none'; };\n"
           "  var kinds = Object.keys(want).concat(Object.keys(got).filter(function (k) { return !(k in want); }));\n"
           "  kinds.forEach(function (kind) {\n"
           "    var w = (want[kind] || []).slice().sort();\n"
           "    var g = (got[kind] || []).slice().sort();\n"
           "    if (w.join('\\n') === g.join('\\n')) return;\n"
           "    if (!g.length) throw new Error('its kinds half registers no ' + kind + ' items');\n"
           "    throw new Error('its kinds half registers ' + kind + ' items ' + text(g) + \", and its pack.json's \"\n"
           "      + 'kinds member declares ' + text(w));\n"
           "  });\n"
           "})();\n";
}

}  // namespace

KindsTrial kindsTrial() {
    return [](const std::string& source, std::span<const std::vector<std::uint8_t>> earlier,
              const std::map<std::string, std::vector<std::string>>& declared,
              std::string* why) -> std::optional<KindsHalf> {
        const auto fail = [&](std::string reason) -> std::optional<KindsHalf> {
            if (why) *why = std::move(reason);
            return std::nullopt;
        };
        std::string error;
        auto bytecode = dsp::compileToBytecode(source, ScriptHost::kStackBytes, &error);
        if (!bytecode) return fail("its kinds half does not compile: " + error);
        auto prelude = dsp::compileToBytecode(kPrelude, ScriptHost::kStackBytes, &error);
        auto closing = dsp::compileToBytecode(epilogue(declared), ScriptHost::kStackBytes, &error);
        if (!prelude || !closing) return fail("its kinds half's trial does not compile: " + error);
        std::vector<ScriptHost::Chunk> chunks{ScriptHost::embeddedBundle()};
        for (const auto& e : earlier) chunks.push_back(ScriptHost::Chunk{e});
        chunks.push_back(ScriptHost::Chunk{*prelude});
        chunks.push_back(ScriptHost::Chunk{*bytecode});
        chunks.push_back(ScriptHost::Chunk{*closing});
        ScriptHost host;
        if (!host.load(chunks, &error))
            return fail("its kinds half did not run beside the built-in script nodes: " + error);
        return KindsHalf{std::move(*bytecode)};
    };
}

}  // namespace rp::host::packs
