// The protocol nodes a system's roles are placed as (docs/modular-graph-phase-b3b-design.md section 2): which role
// kind has a script node, its type, and how buildGraph wires each type between the routing, the controller, its
// system and the host. A role's node is placed before the systems, feeding its system's inputs; its type may name one
// more for the role, placed after the systems, which reads what its system sent in the same block (`out`: LSDj's
// serial-out decoder, docs/lsdj-serial-out-design.md section 3). Pure and zod-free, so buildGraph, both stores and a
// DirectGraph read the same table, with or without a role registry: the tables are generated into
// src/generated/protocol.ts (docs/modular-graph-phase-f1-design.md section 3), a module that imports nothing but a
// type, so a package's protocol member is the one statement of its nodes and what is here is the contract they
// satisfy and the wiring the app reads them through.
//
// The mapping lives here rather than on RoleType because the node types are code in the script-node bundle every
// Engine embeds: which role kinds have one is a fact of that bundle, not of whichever registry a store was handed,
// and three publishers need it, two of which hold no registry at all (a DirectGraph, a SystemsStore built without
// one, as the tests build them). A field on RoleType would need this table as its fallback anyway, which makes two
// sources that can disagree about a role. The registry stays what it is to the app: each role's schema,
// onConstruct and menu, on the control plane.
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { SerialOutCaptureMode } from "./controller";
import { PROTOCOL_NODES, ROLE_NODES } from "../generated/protocol";

/** The system-node inputs a protocol node feeds, each from its output of the same name. */
export type SystemInput = "serialIn" | "midi" | "bytes" | "buttons";

/** How buildGraph wires a protocol node type. */
export interface ProtocolNodeSpec {
  /** The input its system's routed MIDI arrives on, or null for a node that reads only the transport. */
  midiIn: string | null;
  /** The system inputs it writes, each by one node of its system at most: protocolPipeline keeps that so. */
  writes: readonly SystemInput[];
  /** It takes the controller's launches on `launches` when its system is the controller's target. */
  launches?: true;
  /** It takes its Game Boy's serial out on `serialOut`, in the block the core captured it, over a forward wire from
   *  the system, so it is placed after its system: what MI.OUT and master sync decode. */
  serialOut?: true;
  /** It sends host MIDI on `midiOut`, under its system's id. */
  midiOut?: true;
  /** The serial-out capture a system's core runs for each of this node's modes: `drive` (clock a transfer waiting on
   *  an external clock, capturing the bytes too), `listen` (capture the bytes the core clocks out itself) or `off`. The
   *  node's own package supplies it as data, because the mapping is the cart's, while the planner that applies it may
   *  carry no package. A mode this does not name captures nothing. */
  capture?: Readonly<Record<string, SerialOutCaptureMode>>;
  /** The type of the second node buildGraph places for the role, after the systems, as sys-<id>/<kind>/out with the
   *  same settings: a PROTOCOL_NODES type that writes no system input, so the one-writer rule below is the first
   *  node's alone. */
  out?: string;
}

// Each type's id as its node module spells it (the owning package's protocol module), and each port name as that
// module declares it, which test/graph/build-graph.test.ts and test/graph/protocol-nodes.test.ts hold these to:
// importing the modules would pull every protocol into whatever imports buildGraph.
//
// One writer per system input: the planner refuses a second producer wired into one input, and with it the whole
// document, so the project would go silent for one system's roles. The roles a provider attaches never collide, but a
// loaded project's can: one stored with mgb whose ROM path holds LSDj today gains lsdj-sync as a missing feature role
// (SystemsStore.withMissingFeatureRoles), and both write serialIn. So of two roles of one system whose nodes would
// write the same input, the first in role order is placed and the later one left out of the pipeline whole
// (protocolPipeline), where the kernel merged what both sent; the store orders a loaded system's roles so that, of two
// that collide, the one its ROM calls for comes first (SystemsStore.suggestedFirst). A kind named twice (a hand-edited
// file) is left out the same way, as its node id, sys-<id>/<kind>, is taken, which the planner refuses too.
//
// An lsdj-sync role is two nodes: rp.lsdj.sync, before its Game Boy, takes the host's side into the cart (the routed
// MIDI and the launches in, serial in and buttons out), and rp.lsdj.serial-out, after it, the cart's side to the host
// (the Game Boy's serial out in, host MIDI out). The generated module holds the literals as a package's manifest
// spells them, so ProtocolNodeType stays the union of the ids a product has.
//
// Both tables are re-exported as live bindings: PROTOCOL_NODES is the spec per type, ROLE_NODES the type per role kind
// (exported for test/graph/protocol-nodes.test.ts, which holds each type to one the script bundle registers; everything
// else reads it through protocolNodeOf).
export { PROTOCOL_NODES, ROLE_NODES };

export type ProtocolNodeType = keyof typeof PROTOCOL_NODES;

/** The type of the node placed after its system for a role whose node is of type `node`, or undefined for none. */
export function outNodeOf(node: ProtocolNodeType): ProtocolNodeType | undefined {
  // A PROTOCOL_NODES key, which test/graph/protocol-nodes.test.ts holds every `out` to.
  return (PROTOCOL_NODES[node] as ProtocolNodeSpec).out as ProtocolNodeType | undefined;
}

/** A role on a system as buildGraph places it: its node, `sys-<id>/<kind>`, of type `node`, with the role's config as
 *  its settings (and its type's `out`, when it names one, `sys-<id>/<kind>/out`, with the same). A store with a
 *  registry that registers the kind hands it as the role's schema parsed it (an edit, a fresh system's defaults and a
 *  loaded project's alike); a DirectGraph, or a store with no registry, as it was given. */
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

/** The system inputs the node a role of `kind` is placed as writes; none for a role with no node. */
export function protocolWrites(kind: string): readonly SystemInput[] {
  const node = protocolNodeOf(kind);
  return node ? PROTOCOL_NODES[node].writes : [];
}

/** A role protocolPipeline leaves out: its node would write `input`, which the node of `by`, earlier in its system's
 *  roles, already writes; or, with `input` null, a role of its kind is already placed (and `by` is its kind). */
export interface LeftOutRole {
  kind: string;
  input: SystemInput | null;
  by: string;
}

/** `roles`, in order, as the protocol nodes they are placed as; a role with none is left out, so an edit to it (a
 *  model, a palette, an asset override) leaves the document as it was. So is a role whose kind is already placed, or
 *  whose node would write a system input an earlier role's node already writes (one writer per input, beside
 *  PROTOCOL_NODES), and `leftOut` hears of each of those. */
export function protocolPipeline(
  roles: readonly RoleInstance[],
  leftOut?: (role: LeftOutRole) => void,
): ProtocolRole[] {
  const out: ProtocolRole[] = [];
  const writer: Partial<Record<SystemInput, string>> = {};
  for (const r of roles) {
    const node = protocolNodeOf(r.kind);
    if (!node) continue;
    if (out.some((placed) => placed.kind === r.kind)) {
      leftOut?.({ kind: r.kind, input: null, by: r.kind });
      continue;
    }
    const writes: readonly SystemInput[] = PROTOCOL_NODES[node].writes;
    const taken = writes.find((input) => writer[input] !== undefined);
    if (taken !== undefined) {
      leftOut?.({ kind: r.kind, input: taken, by: writer[taken]! });
      continue;
    }
    for (const input of writes) writer[input] = r.kind;
    out.push({ kind: r.kind, node, config: r.config });
  }
  return out;
}

/** The role of `kind` as `roles`' pipeline places it, or undefined when there is none or protocolPipeline leaves it
 *  out. What a role's mode decides beside its node (the serial-out capture an lsdj-sync arms, the latency it reports)
 *  reads this, not the role list: a role left out runs no node. */
export function placedRole(roles: readonly RoleInstance[], kind: string): ProtocolRole | undefined {
  return protocolPipeline(roles).find((r) => r.kind === kind);
}
