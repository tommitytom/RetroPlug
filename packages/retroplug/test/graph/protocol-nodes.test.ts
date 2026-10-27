// graph/protocolNodes.ts against the node modules it describes. buildGraph wires each protocol node by PROTOCOL_NODES'
// port names (its routed MIDI's input, the system inputs it writes, its launches, MI.OUT and host MIDI out), and the
// table spells them without importing the modules, so nothing but this file holds a name to the port its module
// declares: a name the module does not have is a wire the planner refuses, and with it the whole document, for every
// project with that role. So each entry's names are held to the ports of the node of that type, each in the direction
// and kind buildGraph wires it, and the other way round, each port the module declares to an entry's name, since a port
// the table does not name is one buildGraph never wires. And every type the table names, and every type a role kind is
// placed as, is one the script-node bundle registers.
import { test, expect } from "../../testing/harness";
import type { DefinedNode } from "../../src/graph/defineNode";
import type { PortDesc } from "../../src/graph/descriptor";
import { PROTOCOL_NODES, ROLE_NODES, protocolNodeOf, type ProtocolNodeSpec } from "../../src/graph/protocolNodes";
import { SCRIPT_NODES_GLOBAL } from "../../src/script/register";
import { mgbMidiNode } from "../../src/script/nodes/mgbMidi";
import { n8MidiToFifoNode } from "../../src/script/nodes/n8MidiToFifo";
import { risaSyncNode } from "../../src/script/nodes/risaSync";
import { smsSyncNode } from "../../src/script/nodes/smsSync";
import { lsdjSyncNode } from "../../src/script/nodes/lsdjSync";
// The production bundle's entry point: importing it registers its node types on SCRIPT_NODES_GLOBAL, as it does in each
// of the script host's contexts.
import "../../src/script/bundle";

const PROTOCOL_MODULES: readonly DefinedNode<any>[] = [mgbMidiNode, n8MidiToFifoNode, risaSyncNode, smsSyncNode, lsdjSyncNode];
const moduleOf = (type: string): DefinedNode<any> | undefined => PROTOCOL_MODULES.find((d) => d.id === type);

const registered = (): string[] =>
  ((globalThis as Record<string, unknown>)[SCRIPT_NODES_GLOBAL] as DefinedNode<any>[]).map((d) => d.id);

// A port as buildGraph wires it: its name, direction and kind.
const port = (p: Pick<PortDesc, "id" | "dir" | "kind">): string => `${p.id} ${p.dir} ${p.kind}`;

/** The ports an entry says its node has, as buildGraph wires each. A system input a node writes is an output of the
 *  kind the system node takes it as: serial bytes into a Game Boy, core bytes into a NES or an SMS, the rest events. */
function portsOf(spec: ProtocolNodeSpec): string[] {
  const out: string[] = [];
  if (spec.midiIn !== null) out.push(port({ id: spec.midiIn, dir: "in", kind: "events" }));
  if (spec.launches) out.push(port({ id: "launches", dir: "in", kind: "events" }));
  if (spec.serialOut) out.push(port({ id: "serialOut", dir: "in", kind: "bytes" }));
  for (const input of spec.writes)
    out.push(port({ id: input, dir: "out", kind: input === "serialIn" || input === "bytes" ? "bytes" : "events" }));
  if (spec.midiOut) out.push(port({ id: "midiOut", dir: "out", kind: "events" }));
  return out.sort();
}

test("every PROTOCOL_NODES entry names exactly the ports its node module declares, in the direction buildGraph wires each", () => {
  const entries = Object.entries(PROTOCOL_NODES) as [string, ProtocolNodeSpec][];
  expect(entries.map(([type]) => type).sort()).toEqual(PROTOCOL_MODULES.map((d) => d.id).sort());
  for (const [type, spec] of entries) {
    const def = moduleOf(type)!;
    // Default settings: no protocol node's ports follow its settings.
    expect(def.descriptor().ports.map(port).sort(), type).toEqual(portsOf(spec));
  }
});

test("every type a role kind is placed as, and every type PROTOCOL_NODES names, is one the script bundle registers", () => {
  const types = new Set(registered());
  for (const [kind, type] of Object.entries(ROLE_NODES)) {
    expect(types.has(type), `${kind} -> ${type}`).toBe(true);
    expect(protocolNodeOf(kind)).toBe(type);
  }
  for (const type of Object.keys(PROTOCOL_NODES)) expect(types.has(type), type).toBe(true);
  // Every protocol node type is some role's: a type no role is placed as would be dead in the bundle.
  expect([...new Set(Object.values(ROLE_NODES))].sort()).toEqual(Object.keys(PROTOCOL_NODES).sort());
});
