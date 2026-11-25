#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// A pack's protocol half (docs/downloadable-packs-m2e-plan.md): its QuickJS script nodes, compiled once by the table and
// loaded into every Engine's script host as the app pack `scriptId`.
namespace rp::host::packs {

struct ProtocolHalf {
    std::string               scriptId;     // "rp.<pack id>.protocol": its package's id in every Engine
    std::vector<std::uint8_t> bytecode;     // the global-code bundle, compiled
    std::vector<std::string>  typeIds;      // its definitions' ids, in registration order
    std::vector<std::string>  descriptors;  // each type's descriptor as canonical JSON, index for index
};

// Compile a protocol half's source and load it beside the built-in script nodes, as an Engine will: its bytecode, its
// types and their descriptors, or nothing with the reason in *why. The backend's (host/packs/ProtocolTrial.hpp), which
// the table is handed (PackTable::setProtocolTrial): the script host is not this library's.
using ProtocolTrial =
    std::function<std::optional<ProtocolHalf>(const std::string& scriptId, const std::string& source, std::string* why)>;

}  // namespace rp::host::packs
