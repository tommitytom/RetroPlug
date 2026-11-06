// A device pack whose port map has a computed "__proto__" key: TypeScript's ES5 lowering spells it as an assignment,
// which sets the object's prototype under Node and makes an own key under MicroQuickJS, so the node lays out one port
// on the reference and two on the engine. The device-pack toolchain must refuse it, naming the transcript line where
// the two disagree (test/graph/device-pack.test.ts).
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

export const protoKey = defineNode({
  id: "rp.test.device.proto-key",
  ports: { ["__proto__"]: events.in(), out: events.out() },
  create: () => ({ process() {} }),
});

registerScriptNodes([protoKey]);
