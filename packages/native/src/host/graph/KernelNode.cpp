#include "host/graph/KernelNode.hpp"

#include <iterator>

#include "graph/node.hpp"
#include "host/graph/HostServices.hpp"

namespace rp::host {

namespace {

struct Kernel {
    static constexpr const char* id = "rp.dsp.kernel";
    static constexpr const char* name = "DSP Kernel";
    static constexpr const char* category = "dsp";

    const EngineKernel* kernel = nullptr;

    void bind_host(const rp_host* h) noexcept {
        kernel = static_cast<const EngineKernel*>(h->get_interface(h, RP_ENGINE_KERNEL_INTERFACE));
    }
    bool activate(double, std::uint32_t) { return kernel != nullptr; }
    void process(const rp::Block& b) noexcept { kernel->run(kernel->ctx, b.frames()); }
};

}  // namespace

const rp_package* dspPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Kernel>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::host
