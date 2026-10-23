// Extensions attach to node instances by matcher, contribute nodes and wires, and may not both claim the
// same node input.
import { test, expect } from "../../testing/harness";
import { z } from "zod";
import { defineExtension, ExtensionRegistry, type NodeInfo } from "../../src/graph/defineExtension";

const gb: NodeInfo = { id: "sys-1", type: "rp.sameboy.gb", rom: { markers: ["lsdj"] } };
const nes: NodeInfo = { id: "sys-2", type: "rp.mesen.nes", rom: { markers: ["risa"] } };

const lsdj = defineExtension({
  id: "rp.lsdj",
  matches: (n) => n.type === "rp.sameboy.gb" && (n.rom?.markers ?? []).includes("lsdj"),
  settings: z.object({ syncMode: z.enum(["off", "midi-sync"]).default("midi-sync") }),
  contribute: ({ node, settings }) => ({
    nodes: [{ id: `${node.id}.lsdj-sync`.replace(".", "-"), type: "rp.lsdj.sync", params: { mode: settings.syncMode } }],
    wires: [{ from: `${node.id}-lsdj-sync.serialOut`, to: `${node.id}.serialIn` }],
  }),
});

test("an extension attaches only to the nodes its matcher accepts", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  expect(reg.attached(gb).map((e) => e.id)).toEqual(["rp.lsdj"]);
  expect(reg.attached(nes).length).toBe(0);
});

test("collect parses each extension's settings and gathers its nodes and wires", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  const c = reg.collect([gb, nes], (node, ext) => (node === "sys-1" && ext === "rp.lsdj" ? { syncMode: "off" } : undefined));
  expect(c.nodes).toEqual([{ id: "sys-1-lsdj-sync", type: "rp.lsdj.sync", params: { mode: "off" } }]);
  expect(c.wires.length).toBe(1);
  expect(c.conflicts.length).toBe(0);
});

test("two contributors wiring the same input is a named conflict", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  reg.add(
    defineExtension({
      id: "rp.mgb",
      matches: (n) => n.type === "rp.sameboy.gb",
      contribute: ({ node }) => ({ wires: [{ from: "mgb.serialOut", to: `${node.id}.serialIn` }] }),
    }),
  );
  const c = reg.collect([gb]);
  expect(c.conflicts).toEqual([{ input: "sys-1.serialIn", extensions: ["rp.lsdj", "rp.mgb"] }]);
});

test("an extension id can be registered once", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  expect(() => reg.add(lsdj)).toThrow("already registered");
});
