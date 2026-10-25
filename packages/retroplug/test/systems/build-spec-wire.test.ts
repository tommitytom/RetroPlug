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

test("buildCore carries serialOutCapture under that exact key", () => {
  const { sent, client } = recording();
  client.buildCore(spec({ serialOutCapture: true }), 7);
  expect(sent.length).toBe(1);
  expect(sent[0].method).toBe("buildCore");
  const params = sent[0].params[0] as Record<string, unknown>;
  expect(params.id).toBe(7);
  expect(params.serialOutCapture).toBe(true);
});

test("buildCore does not arm the capture for a spec without the flag", () => {
  const { sent, client } = recording();
  client.buildCore(spec(), 7);
  client.buildCore(spec({ serialOutCapture: false }), 8);
  expect(sent.length).toBe(2);
  for (const r of sent) expect((r.params[0] as Record<string, unknown>).serialOutCapture ?? false).toBe(false);
});
