// Must not compile in device mode (cmake/tests.cmake checks it at configure): a validate left outside
// `#ifndef RP_DEVICE`. It names nothing a device build lacks, so only DeviceTypeCheck's own check refuses it.
#include "graph/node.hpp"

namespace rp::graph::refused {
struct Settings {
    float gain = 1.0f;
};
}  // namespace rp::graph::refused

namespace {
struct Validating {
    static constexpr const char* id = "x.validating";
    using Settings = rp::graph::refused::Settings;
    static const char* validate(const Settings&) { return nullptr; }
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* validating() { return rp::node_type<Validating>(); }
int main() { return validating() != nullptr ? 0 : 1; }
