#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// A pack's kinds half (docs/downloadable-packs-m2f-plan.md): the runtimes of its contribution-kind items, global code
// compiled once by the table and run in every Engine's built-in pack after the embedded bundle, from construction.
namespace rp::host::packs {

struct KindsHalf {
    std::vector<std::uint8_t> bytecode;  // the global-code bundle, compiled
};

// Compile a kinds half's source and run it after the built-in script nodes in a scratch script host, as an Engine's
// construction will: its bytecode, or nothing with the reason in *why. The backend's (host/packs/KindsTrial.hpp), which
// the table is handed (PackTable::setKindsTrial): the script host is not this library's.
using KindsTrial = std::function<std::optional<KindsHalf>(const std::string& source, std::string* why)>;

}  // namespace rp::host::packs
