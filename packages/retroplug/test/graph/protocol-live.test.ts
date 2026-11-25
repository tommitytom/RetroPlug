// The live protocol tables (docs/downloadable-packs-m2e-plan.md): the generated module seeds them with the product's
// protocol nodes and roles, a commit adds a pack's, the readers see it after the commit, and a commit the checks refuse
// leaves both tables as they were.
import { test, expect } from "@retroplug/testing";
import { commitContributions, contributionsVersion } from "../../src/contributions/registry";
import { protocolNodesRegistry, roleNodesRegistry } from "../../src/contributions/protocolRegistries";
import { declaredNodeTypes, protocolNodeOf, protocolNodes, protocolSpecOf, roleNodes } from "../../src/graph/protocolNodes";

const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};

test("the seed is the product's tables", () => {
  expect(protocolNodeOf("mgb")).toBe("rp.mgb.midi");
  expect(protocolSpecOf("rp.mgb.midi")).toEqual({ midiIn: "midi", writes: ["serialIn"] });
  expect(declaredNodeTypes().includes("rp.midi.route")).toBe(true);
  // Unwired nodes are declared and not wired.
  expect(protocolSpecOf("rp.midi.route")).toBe(undefined);
});

test("a pack's node and role join the tables in one commit, and the readers see them", () => {
  const before = contributionsVersion();
  commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-a", items: [{ type: "rp.live-a.echo", midiIn: "in", writes: [], midiOut: true }] },
    { registry: roleNodesRegistry, pkg: "live-a", items: [{ kind: "live-a", node: "rp.live-a.echo" }] },
  ]);
  expect(contributionsVersion()).toBe(before + 1);
  expect(protocolNodeOf("live-a")).toBe("rp.live-a.echo");
  expect(protocolNodes()["rp.live-a.echo"]).toEqual({ midiIn: "in", writes: [], midiOut: true });
  expect(roleNodes()["live-a"]).toBe("rp.live-a.echo");
  const declared = declaredNodeTypes();
  expect(declared[declared.length - 1]).toBe("rp.live-a.echo");
});

test("a commit the checks refuse changes nothing", () => {
  const nodes = protocolNodesRegistry.items();
  const roles = roleNodesRegistry.items();
  expect(refusal(() => commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-b", items: [{ type: "rp.mgb.midi", midiIn: "midi", writes: ["serialIn"] }] },
  ]))).toBe("two packages declare the script node rp.mgb.midi: mgb and live-b");
  expect(refusal(() => commitContributions([
    { registry: roleNodesRegistry, pkg: "live-c", items: [{ kind: "mgb", node: "rp.mgb.midi" }] },
  ]))).toBe("two packages declare the role kind mgb: mgb and live-c");
  expect(refusal(() => commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-d", items: [{ type: "rp.live-d.idle" }] },
    { registry: roleNodesRegistry, pkg: "live-d", items: [{ kind: "live-d", node: "rp.live-d.idle" }] },
  ]))).toBe("live-d: the role live-d names rp.live-d.idle, which declares no midiIn and writes to wire it");
  expect(protocolNodesRegistry.items()).toBe(nodes);
  expect(roleNodesRegistry.items()).toBe(roles);
});
