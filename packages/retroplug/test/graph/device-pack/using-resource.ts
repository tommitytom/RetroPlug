// @ts-nocheck: `using` needs disposable types this package's lib leaves out, and it is here only to be refused.
// A device pack with a `using` declaration: esbuild lowers it to helpers of its own that read Symbol, which
// MicroQuickJS lacks, and this file names no Symbol itself. The device-pack toolchain must refuse it, naming the
// helper and the line of this file that pulled it in (test/graph/device-pack.test.ts).
import { defineNode, events } from "../../../src/graph/defineNode";
import { registerScriptNodes } from "../../../src/script/register";

const lease = (held: object) => held;

export const leased = defineNode({
  id: "rp.test.device.leased",
  ports: { in: events.in() },
  create: () => ({
    process(b) {
      using held = lease(b);
      void held;
    },
  }),
});

registerScriptNodes([leased]);
