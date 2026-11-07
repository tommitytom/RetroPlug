// A device pack that reaches for globals upstream mqjs's REPL has and the box's engine will not: a timer (asynchrony a
// device pack may not have) and console.warn. The device-pack toolchain must refuse it, naming each and its line
// (test/graph/device-pack.test.ts). Its own function named `load` is its own, and passes.
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

declare function setTimeout(f: () => void, ms: number): number;
declare const console: { log(s: string): void; warn(s: string): void };

function load(n: number): number {
  return n + 1;
}

export const later = defineNode({
  id: "rp.test.device.later",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => ({
    process(b) {
      const n = load(b.messages("in").length);
      setTimeout(() => b.send("out", 0, [n]), 1);
      console.warn("deferred");
    },
  }),
});

registerScriptNodes([later]);
