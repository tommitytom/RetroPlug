// The build spec as realBackend.ts puts it on the wire. A loaded LSDj in a MIDI-out mode captures serial out
// only through `serialOutCapture`, and native skips a field it does not know, so the key must be the one
// BackendConstructSpec names (the engine-graph Catch2 case decodes that same key on the native side). Driven
// over a stand-in __rpcSend that records each request, so no native host is needed.
import { test, expect } from "../../testing/harness";
import { createEmulatorClient } from "../../src/realBackend";
import type { ConstructSpec } from "../../src/backend";

type Request = { method: string; params: unknown[] };

// An emulator client over a channel that records every request, answering each with a null result.
function recording(): { sent: Request[]; client: ReturnType<typeof createEmulatorClient> } {
  const sent: Request[] = [];
  const key = Symbol.for("plugin");
  const g = globalThis as Record<symbol, unknown>;
  const previous = g[key];
  g[key] = {
    __rpcSend: (r: Request) => {
      sent.push(r);
      return { result: null };
    },
  };
  try {
    return { sent, client: createEmulatorClient() }; // the channel is resolved once, here
  } finally {
    g[key] = previous;
  }
}

const spec = (extra: Partial<ConstructSpec> = {}): ConstructSpec => ({
  romPath: "",
  platform: "gb",
  core: "sameboy",
  embeddedRom: "mgb",
  savPath: null,
  statePath: null,
  ...extra,
});

test("buildCore carries serialOutCapture under that exact key, each mode as its name", () => {
  const { sent, client } = recording();
  client.buildCore(spec({ serialOutCapture: "drive" }), 7);
  client.buildCore(spec({ serialOutCapture: "listen" }), 8);
  expect(sent.map((r) => r.method)).toEqual(["buildCore", "buildCore"]);
  const params = sent.map((r) => r.params[0] as Record<string, unknown>);
  expect(params.map((p) => [p.id, p.serialOutCapture])).toEqual([[7, "drive"], [8, "listen"]]);
});

test("buildCore does not arm the capture for a spec without the field", () => {
  const { sent, client } = recording();
  client.buildCore(spec(), 7);
  expect(sent.length).toBe(1);
  expect("serialOutCapture" in (sent[0].params[0] as Record<string, unknown>)).toBe(false);
});

test("setSerialOutCapture carries the mode, and null for off", () => {
  const { sent, client } = recording();
  client.setSerialOutCapture(3, "listen");
  client.setSerialOutCapture(3, null);
  expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setSerialOutCapture", 3, "listen"], ["setSerialOutCapture", 3, null]]);
});
