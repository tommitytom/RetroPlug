#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rfl/Generic.hpp>
#include <rfl/Result.hpp>

namespace rp::graph {

// The graph document TS sends with setGraph (docs/modular-graph-design.md section 5.1). Node ids may not
// contain '.', so an endpoint "gb1.pins.pulse" always splits at the first dot into node "gb1" and port
// "pins.pulse".

struct WireDoc {
    std::string         from;
    std::string         to;
    std::optional<bool> feedback;
};

struct NodeDoc {
    std::string                                        id;
    std::string                                        type;
    std::optional<rfl::Generic>                        settings;
    std::optional<std::map<std::string, rfl::Generic>> params;     // number, enum label, or bool
    std::optional<std::map<std::string, std::string>>  resources;  // resource input port -> "res:<id>"
};

struct GraphDocument {
    std::vector<NodeDoc> nodes;
    std::vector<WireDoc> wires;
};

// Numbers are read as JSON.parse reads them (see parseJson).
rfl::Result<GraphDocument> parseGraphDocument(std::string_view json);

struct Endpoint {
    std::string_view node;
    std::string_view port;
};

// Split "node.port"; nullopt when there is no dot or either side is empty.
std::optional<Endpoint> parseEndpoint(std::string_view text);

// "res:12" -> 12; nullopt for anything else.
std::optional<std::uint32_t> parseResourceRef(std::string_view text);

}  // namespace rp::graph
