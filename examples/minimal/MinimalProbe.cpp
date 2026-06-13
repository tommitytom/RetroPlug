// dpf.js seam probe — compile-only proof that the framework subtree (src/dpfjs/)
// builds against a trivial consumer with ZERO RetroPlug headers. Enable with
// -DDPFJS_BUILD_EXAMPLE=ON; it compiles to an object (no link, no plugin).
//
// If this stops compiling, the framework has grown a dependency on RetroPlug
// domain code — the seam restructure-06 introduced has leaked.

#include "dpfjs/JsRpcBridge.hpp"
#include "dpfjs/PluginDescriptor.hpp"
#include "dpfjs/Env.hpp"

#include "MinimalService.hpp"

// Never called — forces template instantiation of the generic engine + bridge
// against MinimalService, so a compile success is the seam proof.
[[maybe_unused]] static void dpfjs_seam_probe(LvglJsEngine& engine, MinimalService& service) {
    dpfjs::JsRpcBridge<MinimalService> bridge(engine, service, "minimal");
    registerMinimalRpcMethods(bridge.server());
    bridge.pumpAsync();

    (void)dpfjs::kEmptyDescriptor;
    (void)dpfjs::getenvWithPrefix("PROBE");
}
