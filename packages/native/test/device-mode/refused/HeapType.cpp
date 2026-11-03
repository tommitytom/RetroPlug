// Must not compile in device mode (cmake/tests.cmake checks it at configure): a type that declares heap.
#include "graph/node.hpp"

namespace {
struct Heavy {
    static constexpr const char* id = "x.heavy";
    static constexpr bool        heap = true;
    void process(const rp::Block&) noexcept {}
};
}  // namespace

const rp_node_type* heavy() { return rp::node_type<Heavy>(); }
int main() { return heavy() != nullptr ? 0 : 1; }
