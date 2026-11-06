// A device pack that calls what MicroQuickJS does not have (a Map): the device-pack toolchain must refuse it, naming
// the call and this file's line (test/graph/device-pack.test.ts).
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

export const seenNotes = defineNode({
  id: "rp.test.device.seen-notes",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => {
    const seen = new Map<number, number>();
    return {
      process(b) {
        const ms = b.messages("in");
        for (let i = 0; i < ms.length; i++) seen.set(ms[i].data[1], ms[i].frame);
      },
    };
  },
});

registerScriptNodes([seenNotes]);
