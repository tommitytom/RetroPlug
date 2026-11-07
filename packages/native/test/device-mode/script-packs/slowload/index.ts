// The slowload pack: its module scope ends in a search that MicroQuickJS's split drops the deadline's interrupt in (it
// answers "not found" with the interrupt left pending, and the script runs on), for ScriptPack.test.cpp's case that a
// pack whose load runs past its deadline is refused in the deadline's words though its script returned. A test fixture
// of the device-pack toolchain (packages/native/cmake/tests.cmake), never built by a product.
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

export const restNode = defineNode({
  id: "rp.slowload.rest",
  ports: { out: events.out() },
  create: () => ({ process() {} }),
});

registerScriptNodes([restNode]);

// Last, so nothing of the pack's runs after the search to throw the dropped interrupt again.
let text = "a";
while (text.length < 8000) text += text;
text.substring(0, 8000).split(text.substring(0, 4000) + "b");
