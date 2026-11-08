// The system-category pack: one node whose descriptor declares category "system", which a native type alone may do. A
// system node builds a core through rp.host.systems/2 in its first activation, and a script node builds none, so the
// type would be lined up against a plan slot that stayed null (docs/modular-graph-phase-f2-design.md section 3). The
// MicroQuickJS host refuses it where a pack's types are registered (graph-runtime's MqjsHost::openControl), which the
// device-pack toolchain's run of every pack meets, so the toolchain refuses to build this pack: a test fixture of that
// refusal (packages/native/cmake/tests.cmake, cmake/refused-script-pack.cmake), never built by a product.
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

export const coreNode = defineNode({
  id: "rp.systemcategory.core",
  category: "system",
  ports: { out: events.out() },
  create: () => ({ process() {} }),
});

registerScriptNodes([coreNode]);
