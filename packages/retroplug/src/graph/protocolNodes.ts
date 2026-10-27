// The protocol nodes a system's roles are placed as (docs/modular-graph-phase-b3b-design.md section 2): which role
// kind has a script node, its type, and how buildGraph wires each type between the routing, the controller, its
// system and the host. Pure and zod-free, so buildGraph, both stores and a DirectGraph read the same table, with or
// without a role registry.
//
// The mapping lives here rather than on RoleType because the node types are code in the script-node bundle every
// Engine embeds: which role kinds have one is a fact of that bundle, not of whichever registry a store was handed,
// and three publishers need it, two of which hold no registry at all (a DirectGraph, a SystemsStore built without
// one, as the tests build them). A field on RoleType would need this table as its fallback anyway, which makes two
// sources that can disagree about a role. The registry stays what it is to the app: each role's schema,
// onConstruct and menu, on the control plane.
import type { RoleInstance } from "../systemRoles";

/** The system-node inputs a protocol node feeds, each from its output of the same name. */
export type SystemInput = "serialIn" | "midi" | "bytes" | "buttons";

/** How buildGraph wires a protocol node type. */
export interface ProtocolNodeSpec {
  /** The input its system's routed MIDI arrives on, or null for a node that reads only the transport. */
  midiIn: string | null;
  /** The system inputs it writes. No two roles of one system write the same one, so no input is fed twice. */
  writes: readonly SystemInput[];
  /** It takes the controller's launches on `launches` when its system is the controller's target. */
  launches?: true;
  /** It takes its Game Boy's serial out on `serialOut`, a block late (a feedback wire): what MI.OUT and master sync
   *  decode. */
  serialOut?: true;
  /** It sends host MIDI on `midiOut`, under its system's id. */
  midiOut?: true;
}

// Each type's id as its node module spells it (script/nodes/*.ts), which test/graph/build-graph.test.ts holds these
// to: importing the modules would pull every protocol into whatever imports buildGraph.
export const PROTOCOL_NODES = {
  "rp.mgb.midi": { midiIn: "midi", writes: ["serialIn"] },
  "rp.n8.midi-to-fifo": { midiIn: "midiIn", writes: ["midi"] },
  "rp.risa.sync": { midiIn: null, writes: ["bytes"] },
  "rp.sms.sync": { midiIn: null, writes: ["bytes"] },
  "rp.lsdj.sync": { midiIn: "midi", writes: ["serialIn", "buttons"], launches: true, serialOut: true, midiOut: true },
} as const satisfies Record<string, ProtocolNodeSpec>;

export type ProtocolNodeType = keyof typeof PROTOCOL_NODES;

/** The role kinds that have a protocol node (dspRoles.ts registers them), each with the node type it is placed as. */
const ROLE_NODES: Readonly<Record<string, ProtocolNodeType>> = {
  mgb: "rp.mgb.midi",
  "nes-n8-midi": "rp.n8.midi-to-fifo",
  "risa-sync": "rp.risa.sync",
  "sms-sync": "rp.sms.sync",
  "lsdj-sync": "rp.lsdj.sync",
};

/** A role on a system as buildGraph places it: its node, `sys-<id>/<kind>`, of type `node`, with the role's config as
 *  the store parsed it as its settings. */
export interface ProtocolRole {
  kind: string;
  node: ProtocolNodeType;
  config: Record<string, unknown>;
}

/** The node type a role of `kind` is placed as, or undefined for a role with none: a core's config role, and the
 *  features with no DSP behaviour (asset overrides, a ROM's facts). */
export function protocolNodeOf(kind: string): ProtocolNodeType | undefined {
  return Object.prototype.hasOwnProperty.call(ROLE_NODES, kind) ? ROLE_NODES[kind] : undefined;
}

/** `roles`, in order, as the protocol nodes they are placed as; a role with none is left out, so an edit to it (a
 *  model, a palette, an asset override) leaves the document as it was. */
export function protocolPipeline(roles: readonly RoleInstance[]): ProtocolRole[] {
  const out: ProtocolRole[] = [];
  for (const r of roles) {
    const node = protocolNodeOf(r.kind);
    if (node) out.push({ kind: r.kind, node, config: r.config });
  }
  return out;
}
