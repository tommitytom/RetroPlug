// Must not compile in device mode (cmake/tests.cmake checks it at configure): an adjust_layout left outside
// `#ifndef RP_DEVICE`, whose body uses the description layer's Layout, which a device build declares and never defines.
#include "graph/node.hpp"

namespace rp::graph::refused {
struct Settings {
    float gain = 1.0f;
};
}  // namespace rp::graph::refused

namespace {
struct Adjusting {
    static constexpr const char* id = "x.adjusting";
    using Settings = rp::graph::refused::Settings;
    static void adjust_layout(rp::graph::Layout& layout, const Settings&) { layout.inputs.clear(); }
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* adjusting() { return rp::node_type<Adjusting>(); }
int main() { return adjusting() != nullptr ? 0 : 1; }
