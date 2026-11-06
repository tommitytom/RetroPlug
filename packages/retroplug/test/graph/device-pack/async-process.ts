// A device pack whose process is asynchronous: the box's engine has no Promise, so the device-pack toolchain must
// refuse it, naming the `async` and this file's line (test/graph/device-pack.test.ts).
import { defineNode, events } from "../../../src/graph/defineNode";
import type { Block } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

const later = (b: Block): Block => b;

export const slowThru = defineNode({
  id: "rp.test.device.slow-thru",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => ({
    process(b) {
      void (async () => {
        const block = await later(b);
        block.send("out", 0, [0xf8]);
      })();
    },
  }),
});

registerScriptNodes([slowThru]);
