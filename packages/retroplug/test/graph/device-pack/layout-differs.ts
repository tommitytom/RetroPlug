// A device pack whose node lays itself out one way in the full bundle and another in the device's runtime of the pack
// (built with RP_DEVICE_RUNTIME: graph/defineNode.ts), its output's capacity moved: the device-pack toolchain lays both
// out for the node's default settings and must refuse it, naming the line where they part
// (test/graph/device-pack.test.ts). Its nodes would send the same, so the outputs alone would not tell.
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

declare const RP_DEVICE_RUNTIME: boolean | undefined;

export const layoutDiffers = defineNode({
  id: "rp.test.device.layout-differs",
  ports: {
    in: events.in(),
    out: events.out({ messages: typeof RP_DEVICE_RUNTIME === "undefined" ? 64 : 32 }),
  },
  create: () => ({ process() {} }),
});

registerScriptNodes([layoutDiffers]);
