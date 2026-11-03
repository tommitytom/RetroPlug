// Must not compile in device mode (cmake/tests.cmake checks it at configure): settings a device cannot hold.
#include "graph/node.hpp"

namespace rp::graph::refused {
struct Settings {
    float  gain = 1.0f;
    float* table = nullptr;
};
}  // namespace rp::graph::refused

namespace {
struct Pointing {
    static constexpr const char* id = "x.pointing";
    using Settings = rp::graph::refused::Settings;
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* pointing() { return rp::node_type<Pointing>(); }
int main() { return pointing() != nullptr ? 0 : 1; }
