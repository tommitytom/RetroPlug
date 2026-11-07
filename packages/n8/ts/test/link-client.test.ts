// The N8 pack's client, its streaming-link half (@retroplug/n8, packages/n8/ts/index.ts), over a fake RPC call: each
// method sends the facet's method with its arguments (packages/n8/native/src/n8/desktop/N8Rpc.hpp), the config reply
// is read tolerantly as the UI read the hook object it replaces, and a host that did not mount the facet throws from
// the call, which is what the UI's hasN8 takes for "no N8 seam". The facet itself is held in C++ (retroplug-n8-test's
// [n8-rpc] cases); this is the TS half alone, so it runs in the mock tier, with no native host.
import { test, expect } from "@retroplug/testing";
import { createN8Client, type N8Call } from "../index";

function recordingCall(answers: Record<string, unknown>): { call: N8Call; sent: unknown[][] } {
  const sent: unknown[][] = [];
  const call: N8Call = (method, ...params) => {
    sent.push([method, ...params]);
    if (!(method in answers)) throw new Error(`rpc ${method}: [-32601] Method not found`);
    return answers[method];
  };
  return { call, sent };
}

test("the link client sends each facet method with its arguments", () => {
  const { call, sent } = recordingCall({ n8SetPort: true, n8Connect: true, n8SetLookahead: true });
  const link = createN8Client(call).link;
  link.setPort("/dev/ttyACM0");
  link.connect(true);
  link.connect(false);
  link.setLookahead(15.7);
  expect(sent).toEqual([
    ["n8SetPort", "/dev/ttyACM0"],
    ["n8Connect", true],
    ["n8Connect", false],
    ["n8SetLookahead", 15],
  ]);
});

test("the link config reply is read whole, and tolerantly where a field is missing or of another type", () => {
  const full = {
    ports: [{ port: "/dev/ttyACM0", isN8: true }, { port: "/dev/ttyS0", isN8: false }],
    selectedPort: "/dev/ttyACM0",
    connected: true,
    enabled: true,
    lookaheadMs: 10,
    bytes: 1234,
    error: "",
  };
  expect(createN8Client(recordingCall({ n8LinkConfig: full }).call).link.linkConfig()).toEqual(full);

  const sparseReply = { n8LinkConfig: { ports: [null, { port: 7 }], bytes: 5n, error: 3 } };
  const sparse = createN8Client(recordingCall(sparseReply).call);
  expect(sparse.link.linkConfig()).toEqual({
    ports: [
      { port: "", isN8: false },
      { port: "", isN8: false },
    ],
    selectedPort: "",
    connected: false,
    enabled: false,
    lookaheadMs: 0,
    bytes: 5,
    error: "",
  });
  expect(createN8Client(recordingCall({ n8LinkConfig: null }).call).link.linkConfig().ports).toEqual([]);
});

test("creating a client calls nothing, and a host without the facet throws from the call", () => {
  const { call, sent } = recordingCall({});
  const link = createN8Client(call).link;
  expect(sent).toEqual([]);
  expect(() => link.linkConfig()).toThrow("Method not found");
  expect(() => link.connect(true)).toThrow("Method not found");
});
