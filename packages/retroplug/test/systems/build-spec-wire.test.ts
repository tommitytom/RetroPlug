// The build spec as realBackend.ts puts it on the wire: native skips a field it does not know, so each key must be the
// one BackendConstructSpec names, and a null path is left out rather than sent as "" (native reads an absent field as
// nullopt). And setParam, how the stores set a gain or a NES's expansion volume live, as EngineRpcService::setParam
// takes it. Driven over a stand-in __rpcSend that records each request, so no native host is needed.
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

test("buildCore carries each BackendConstructSpec field under its name, a null path or absent seed left out", () => {
  const { sent, client } = recording();
  const romBytes = new Uint8Array([4]);
  const sramBytes = new Uint8Array([1, 2, 3]);
  const stateBytes = new Uint8Array([5, 6]);
  const carried = {
    savPath: "/roms/a.sav", statePath: "/roms/a.state", romBytes, sramBytes, stateBytes, settings: "{}",
  };
  client.buildCore(spec(carried), 7);
  client.buildCore(spec(), 8);
  expect(sent.map((r) => r.method)).toEqual(["buildCore", "buildCore"]);
  const params = sent.map((r) => r.params[0] as Record<string, unknown>);
  const named = ["core", "embeddedRom", "id", "platform", "romPath"];
  expect(Object.keys(params[0]).sort()).toEqual([
    ...named, "romBytes", "savPath", "settings", "sramBytes", "stateBytes", "statePath",
  ].sort());
  expect([params[0].id, params[0].savPath, params[0].statePath, params[0].settings]).toEqual([
    7, "/roms/a.sav", "/roms/a.state", "{}",
  ]);
  // Each as the Uint8Array it was: rfl::Bytestring on the native side.
  expect([params[0].romBytes, params[0].sramBytes, params[0].stateBytes]).toEqual([romBytes, sramBytes, stateBytes]);
  expect(Object.keys(params[1]).sort()).toEqual(named);
});

test("setParam carries the node, the param and the value, in that order", () => {
  const { sent, client } = recording();
  client.setParam("sys-3", "gain", -6.5);
  client.setParam("sys-4", "expansionVolume", 40);
  expect(sent.map((r) => [r.method, ...r.params])).toEqual([["setParam", "sys-3", "gain", -6.5], ["setParam", "sys-4", "expansionVolume", 40]]);
});
