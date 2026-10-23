#include "graph/GraphDocument.hpp"

#include <charconv>

#include <rfl/NoExtraFields.hpp>
#include <rfl/from_generic.hpp>

#include "graph/CanonicalJson.hpp"

namespace rp::graph {

// Through parseJson, not rfl::json::read: settings and param values are kept as Generic, and reflect-cpp's
// reader would turn an unsigned number above INT64_MAX into a negative one on the way in. (GraphDocument
// has no integer fields of its own, which matters: rfl::from_generic does not range-check them.) Unknown
// fields are refused, so a misspelt "setings" is an error rather than a node quietly given defaults.
rfl::Result<GraphDocument> parseGraphDocument(std::string_view json) {
    auto doc = parseJson(json).and_then(
        [](const rfl::Generic& g) { return rfl::from_generic<GraphDocument, rfl::NoExtraFields>(g); });
    if (!doc) return rfl::Unexpected<rfl::Error>(rfl::Error(std::string("graph document: ") + doc.error().what()));
    return doc;
}

std::optional<Endpoint> parseEndpoint(std::string_view text) {
    const auto dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= text.size()) return std::nullopt;
    return Endpoint{text.substr(0, dot), text.substr(dot + 1)};
}

std::optional<std::uint32_t> parseResourceRef(std::string_view text) {
    constexpr std::string_view kPrefix = "res:";
    if (text.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
    const auto digits = text.substr(kPrefix.size());
    std::uint32_t id = 0;
    if (digits.empty() || digits[0] == '0') return std::nullopt;  // one spelling per id: no "res:007", no "res:0"
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    if (ec != std::errc() || ptr != digits.data() + digits.size()) return std::nullopt;
    return id;
}

}  // namespace rp::graph
