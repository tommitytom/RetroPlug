// graph/protocolNodes.ts against the node modules it describes. buildGraph wires each protocol node by the protocol table's
// port names (its routed MIDI's input, the system inputs it writes, its launches, the Game Boy's serial out and host
// MIDI out), and the table spells them without importing the modules, so nothing but this file holds a name to the port
// its module declares: a name the module does not have is a wire the planner refuses, and with it the whole document,
// for every project with that role. So each entry's names are held to the ports of the node of that type, each in the
// direction and kind buildGraph wires it, and the other way round, each port the module declares to an entry's name,
// since a port the table does not name is one buildGraph never wires. And every type the table names, every type a
// role kind is placed as and every type an entry names as the node after its system (`out`) is one the script-node
// bundle registers.
import { test, expect } from "@retroplug/testing";
import type { DefinedNode } from "@retroplug/core/defineNode";
import { protocolNodes, roleNodes, outNodeOf, protocolNodeOf, type ProtocolNodeSpec, type ProtocolNodeType } from "../../src/graph/protocolNodes";
import { portKey, portsOf } from "../../src/graph/protocolSpec";
import { SCRIPT_NODES_GLOBAL } from "@retroplug/core/register";
import { mgbMidiNode } from "@retroplug/mgb/mgbMidiNode";
import { n8MidiToFifoNode } from "@retroplug/n8/n8MidiToFifoNode";
import { risaSyncNode } from "@retroplug/risa/risaSyncNode";
import { smsSyncNode } from "@retroplug/smsggdj/smsSyncNode";
import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
import { lsdjSerialOutNode } from "@retroplug/lsdj/lsdjSerialOutNode";
// The production bundle's entry point: importing it registers its node types on SCRIPT_NODES_GLOBAL, as it does in each
// of the script host's contexts.
import "../../src/script/bundle";

const PROTOCOL_MODULES: readonly DefinedNode<any>[] = [mgbMidiNode, n8MidiToFifoNode, risaSyncNode, smsSyncNode, lsdjSyncNode, lsdjSerialOutNode];
const moduleOf = (type: string): DefinedNode<any> | undefined => PROTOCOL_MODULES.find((d) => d.id === type);

const registered = (): string[] =>
  ((globalThis as Record<string, unknown>)[SCRIPT_NODES_GLOBAL] as DefinedNode<any>[]).map((d) => d.id);

test("every protocol table entry names exactly the ports its node module declares, in the direction buildGraph wires each", () => {
  const entries = Object.entries(protocolNodes()) as [string, ProtocolNodeSpec][];
  expect(entries.map(([type]) => type).sort()).toEqual(PROTOCOL_MODULES.map((d) => d.id).sort());
  for (const [type, spec] of entries) {
    const def = moduleOf(type)!;
    // Default settings: no protocol node's ports follow its settings.
    expect(def.descriptor().ports.map(portKey).sort(), type).toEqual(portsOf(spec));
  }
});

test("every type a role kind is placed as, every type the protocol table names and every `out` is one the script bundle registers", () => {
  const types = new Set(registered());
  for (const [kind, type] of Object.entries(roleNodes())) {
    expect(types.has(type), `${kind} -> ${type}`).toBe(true);
    expect(protocolNodeOf(kind)).toBe(type);
  }
  for (const type of Object.keys(protocolNodes())) expect(types.has(type), type).toBe(true);
  // Each `out` is an entry of the table, and one placed after its system: it writes no system input, so the one-writer
  // rule, which protocolPipeline keeps for a role's first node, has nothing of it to keep.
  const outs: string[] = [];
  for (const [type, spec] of Object.entries(protocolNodes()) as [ProtocolNodeType, ProtocolNodeSpec][]) {
    if (spec.out === undefined) continue;
    expect(Object.prototype.hasOwnProperty.call(protocolNodes(), spec.out), `${type} -> ${spec.out}`).toBe(true);
    expect(outNodeOf(type)).toBe(spec.out);
    expect(protocolNodes()[spec.out].writes, spec.out).toEqual([]);
    outs.push(spec.out);
  }
  expect(outs).toEqual(["rp.lsdj.serial-out"]);
  // Every protocol node type is some role's, before its system or after it: a type no role is placed as would be dead
  // in the bundle.
  expect([...new Set([...Object.values(roleNodes()), ...outs])].sort()).toEqual(Object.keys(protocolNodes()).sort());
});

test("a type reads its system's serial out only as some entry's `out`, and an `out` reads neither routed MIDI nor launches", () => {
  // buildGraph wires `serialOut` only into a node it places after the systems, as a role's `out`, and wires neither
  // `midiIn` nor `launches` into one: a type that read the serial out as a role's first node, or an `out` that read
  // MIDI or launches, would have an input the document leaves unwired, and nothing would say so.
  const specs = Object.entries(protocolNodes()) as [ProtocolNodeType, ProtocolNodeSpec][];
  const outs = new Set(specs.map(([, spec]) => spec.out).filter((out) => out !== undefined));
  for (const [type, spec] of specs) {
    if (spec.serialOut) expect(outs.has(type), `${type} reads serialOut`).toBe(true);
    if (!outs.has(type)) continue;
    expect(spec.midiIn, `${type} is an out`).toBe(null);
    expect(spec.launches, `${type} is an out`).toBe(undefined);
  }
});
