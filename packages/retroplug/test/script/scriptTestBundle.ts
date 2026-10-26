// A script-node bundle for the native tests alone (packages/native/test/engine-graph/ScriptNodes.test.cpp), never
// shipped: embedded only in retroplug-engine-graph-test (packages/native/cmake/bundles.cmake,
// retroplug-script-test-bundle), by the production bundle's recipe. It carries the authoring layer, as a global
// `rp`, for the toy nodes those tests write as JS source and load after it, and one node of its own, defined as a
// production node is (defineNode, zod settings), which holds TS -> esbuild -> bytecode -> the script host to a
// real definition.
import { z } from "zod";
import { audio, bytes, channels, control, defineNode, events, param } from "../../src/graph/defineNode";
import { registerScriptNodes } from "../../src/script/register";

// Each message on `in`, `copies` times on `out`, its second byte moved by the `transpose` param.
const echo = defineNode({
  id: "rp.test.script-echo",
  settings: z.object({ copies: z.number().int().min(1).max(4).default(1) }),
  ports: { in: events.in(), out: events.out() },
  params: { transpose: param.int({ name: "Transpose", min: -12, max: 12, default: 0 }) },
  create: ({ settings }) => ({
    process(block) {
      const messages = block.messages("in");
      const transpose = block.control("transpose").start;
      for (let i = 0; i < messages.length; ++i) {
        const m = messages[i];
        for (let c = 0; c < settings.copies; ++c) block.send("out", m.frame, [m.data[0], m.data[1] + transpose, m.data[2]], m.flags);
      }
    },
  }),
});

(globalThis as Record<string, unknown>).rp = { audio, bytes, channels, control, defineNode, events, param, z, register: registerScriptNodes };
registerScriptNodes([echo]);
