// A device pack that imports a module outside its allowlist (core/canonicalJson, which is not part of the portable
// node SDK): the device-pack toolchain must refuse it, naming the module and what imported it
// (test/graph/device-pack.test.ts).
import { canonicalJson } from "@retroplug/core/canonicalJson";
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

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
