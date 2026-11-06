// A device pack that imports a module outside its allowlist (graph/canonicalJson, which is not part of the portable
// node SDK): the device-pack toolchain must refuse it, naming the module and what imported it
// (test/graph/device-pack.test.ts).
import { canonicalJson } from "../../../src/graph/canonicalJson";
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

export const spell = defineNode({
  id: "rp.test.device.spell",
  ports: { in: events.in() },
  create: () => ({
    process(b) {
      canonicalJson(b.messages("in").length);
    },
  }),
});

registerScriptNodes([spell]);
