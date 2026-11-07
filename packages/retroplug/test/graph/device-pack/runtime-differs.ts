// A device pack whose node does one thing in the full bundle and another in the device's runtime of the pack (the
// box's bundle, built with RP_DEVICE_RUNTIME: packages/core/ts/defineNode.ts): the device-pack toolchain runs both through the
// same blocks and must refuse it, saying the two outputs differ (test/graph/device-pack.test.ts).
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

declare const RP_DEVICE_RUNTIME: boolean | undefined;

export const differs = defineNode({
  id: "rp.test.device.differs",
  ports: { out: events.out() },
  create: () => {
    const note = new Uint8Array([0x90, typeof RP_DEVICE_RUNTIME === "undefined" ? 60 : 61, 100]);
    return {
      process(b) {
        b.send("out", 0, note);
      },
    };
  },
});

registerScriptNodes([differs]);
