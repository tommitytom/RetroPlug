// A device pack that tests for Symbol and then uses it on the branch where the test failed: only what runs when a guard
// holds is accepted, so the else-branch's Symbol must be refused, naming its line (test/graph/device-pack.test.ts).
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

export const iterates = defineNode({
  id: "rp.test.device.iterates",
  ports: { in: events.in() },
  create: () => ({
    process(b) {
      let key: unknown = 0;
      if (typeof Symbol === "function") {
        key = 1;
      } else {
        key = (Symbol as unknown as { iterator: unknown }).iterator;  // the else-branch types Symbol as never
      }
      void b.messages(String(key));
    },
  }),
});

registerScriptNodes([iterates]);
