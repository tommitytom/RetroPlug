// registerScriptNodes: how a script-node bundle hands its definitions to the native script host, which reads one
// global once the bundle has run (packages/native/src/host/script/ScriptHost.cpp).
import { test, expect } from "../../testing/harness";
import { defineNode, events } from "../../src/graph/defineNode";
import { SCRIPT_NODES_GLOBAL, registerScriptNodes } from "../../src/script/register";

test("each registration appends to the one list the script host reads, in order", () => {
  const g = globalThis as Record<string, unknown>;
  delete g[SCRIPT_NODES_GLOBAL];
  const a = defineNode({ id: "rp.test.a", ports: { out: events.out() }, create: () => ({ process() {} }) });
  const b = defineNode({ id: "rp.test.b", ports: {}, create: () => ({ process() {} }) });
  registerScriptNodes([a]);
  registerScriptNodes([b]);
  expect((g[SCRIPT_NODES_GLOBAL] as { id: string }[]).map((d) => d.id)).toEqual(["rp.test.a", "rp.test.b"]);
  delete g[SCRIPT_NODES_GLOBAL];
});
