// Extensions attach to node instances by matcher, contribute nodes and wires, and may not both claim the
// same node input or node id.
import { test, expect } from "../../testing/harness";
import { z } from "zod";
import { defineExtension, ExtensionRegistry, type NodeInfo } from "../../src/graph/defineExtension";
import { endpoint } from "../../src/graph/graphDocument";

const gb: NodeInfo = { id: "sys-1", type: "rp.sameboy.gb", rom: { markers: ["lsdj"] } };
const gb2: NodeInfo = { id: "sys-2", type: "rp.sameboy.gb", rom: { markers: ["lsdj"] } };
const nes: NodeInfo = { id: "sys-3", type: "rp.mesen.nes", rom: { markers: ["risa"] } };

const lsdj = defineExtension({
  id: "rp.lsdj",
  matches: (n) => n.type === "rp.sameboy.gb" && (n.rom?.markers ?? []).includes("lsdj"),
  settings: z.object({ syncMode: z.enum(["off", "midi-sync"]).default("midi-sync") }),
  contribute: ({ node, settings }) => ({
    nodes: [{ id: `${node.id}/lsdj-sync`, type: "rp.lsdj.sync", params: { mode: settings.syncMode } }],
    wires: [{ from: endpoint(`${node.id}/lsdj-sync`, "serialOut"), to: endpoint(node.id, "serialIn") }],
    views: [{ id: "hd", label: "HD", placement: "fullscreen", component: () => null }],
  }),
});

const tap = (id: string, input: string) =>
  defineExtension({ id, matches: (n) => n.type === "rp.sameboy.gb", contribute: ({ node }) => ({ wires: [{ from: "mgb.out", to: endpoint(node.id, input) }] }) });

test("an extension attaches only to the nodes its matcher accepts", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  expect(reg.attached(gb).map((e) => e.id)).toEqual(["rp.lsdj"]);
  expect(reg.attached(nes).length).toBe(0);
  expect(() => defineExtension({ id: "  ", matches: () => true, contribute: () => ({}) })).toThrow("whitespace");
});

test("collect parses each extension's settings and gathers its nodes, wires and views", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  const c = reg.collect([gb, gb2, nes], (node, ext) => (node === "sys-1" && ext === "rp.lsdj" ? { syncMode: "off" } : undefined));
  expect(c.nodes).toEqual([
    { id: "sys-1/lsdj-sync", type: "rp.lsdj.sync", params: { mode: "off" } },
    { id: "sys-2/lsdj-sync", type: "rp.lsdj.sync", params: { mode: "midi-sync" } },  // the default
  ]);
  expect(c.wires).toEqual([
    { from: "sys-1/lsdj-sync.serialOut", to: "sys-1.serialIn" },
    { from: "sys-2/lsdj-sync.serialOut", to: "sys-2.serialIn" },
  ]);
  expect(c.views.map((v) => [v.node, v.extension, v.view.id])).toEqual([["sys-1", "rp.lsdj", "hd"], ["sys-2", "rp.lsdj", "hd"]]);
  expect(c.attached.map((a) => [a.node, a.extension])).toEqual([["sys-1", "rp.lsdj"], ["sys-2", "rp.lsdj"]]);
  expect(c.conflicts.length).toBe(0);
  expect(c.errors.length).toBe(0);
  c.nodes[0].id = "edited";  // the collected graph is the builder's own copy
  expect(c.attached[0].contribution.nodes![0].id).toBe("sys-1/lsdj-sync");
});

test("collection order follows extension ids, not registration order", () => {
  const a = new ExtensionRegistry();
  a.add(tap("rp.z", "joypad"));
  a.add(lsdj);
  const b = new ExtensionRegistry();
  b.add(lsdj);
  b.add(tap("rp.z", "joypad"));
  const order = (r: ExtensionRegistry) => r.collect([gb, gb2]).attached.map((x) => `${x.node}:${x.extension}`);
  expect(order(a)).toEqual(["sys-1:rp.lsdj", "sys-1:rp.z", "sys-2:rp.lsdj", "sys-2:rp.z"]);
  expect(order(b)).toEqual(order(a));
});

test("two contributors wiring the same input is a conflict naming both", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  reg.add(tap("rp.mgb", "serialIn"));
  reg.add(tap("rp.pad", "joypad"));  // a different input of the same node is no conflict
  const c = reg.collect([gb]);
  expect(c.conflicts).toEqual([
    { kind: "input", input: "sys-1.serialIn", by: [{ extension: "rp.lsdj", node: "sys-1" }, { extension: "rp.mgb", node: "sys-1" }] },
  ]);
});

test("the base graph's wires and nodes are claimed before any extension's", () => {
  const reg = new ExtensionRegistry();
  reg.add(tap("rp.mgb", "serialIn"));
  reg.add(
    defineExtension({ id: "rp.dup", matches: () => true, contribute: ({ node }) => ({ nodes: [{ id: "mix", type: "rp.core.add" }, { id: node.id, type: "rp.x" }] }) }),
  );
  const c = reg.collect([gb], () => undefined, { nodes: [{ id: "mix", type: "rp.core.add" }], wires: [{ from: "midi.out", to: "sys-1.serialIn" }] });
  expect(c.conflicts).toEqual([
    { kind: "input", input: "sys-1.serialIn", by: ["base", { extension: "rp.mgb", node: "sys-1" }] },
    { kind: "node-id", id: "mix", by: ["base", { extension: "rp.dup", node: "sys-1" }] },
    { kind: "node-id", id: "sys-1", by: ["base", { extension: "rp.dup", node: "sys-1" }] },  // the system node itself
  ]);
});

test("one extension claiming an input twice names the nodes it came from", () => {
  const reg = new ExtensionRegistry();
  reg.add(defineExtension({ id: "rp.bus", matches: () => true, contribute: () => ({ wires: [{ from: "a.out", to: "bus.in" }] }) }));
  expect(reg.collect([gb, gb2]).conflicts).toEqual([
    { kind: "input", input: "bus.in", by: [{ extension: "rp.bus", node: "sys-1" }, { extension: "rp.bus", node: "sys-2" }] },
  ]);
});

test("bad settings or node ids are reported, and the rest of the graph is still collected", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  reg.add(defineExtension({ id: "rp.ok", matches: () => true, contribute: () => ({ nodes: [{ id: "ok", type: "rp.x" }, { id: "bad.id", type: "rp.x" }] }) }));
  const c = reg.collect([gb], () => ({ syncMode: "master" }));  // saved by a newer build, say
  expect(c.errors.map((e) => [e.extension, e.code])).toEqual([["rp.lsdj", "settings-invalid"], ["rp.ok", "node-id"]]);
  expect(c.errors[0].message.includes("syncMode")).toBe(true);
  expect(c.nodes.map((n) => [n.id, n.params?.mode])).toEqual([["sys-1/lsdj-sync", "midi-sync"], ["ok", undefined]]);  // defaults stand in
});

test("an extension id can be registered once", () => {
  const reg = new ExtensionRegistry();
  reg.add(lsdj);
  expect(() => reg.add(lsdj)).toThrow("already registered");
});

// @ts-expect-error an extension that reads settings has to say what they are
defineExtension<{ n: number }>({ id: "rp.typed", matches: () => true, contribute: ({ settings }) => ({ nodes: [{ id: String(settings.n), type: "t" }] }) });
