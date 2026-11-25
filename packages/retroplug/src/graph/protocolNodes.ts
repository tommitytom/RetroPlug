// The protocol nodes a system's roles are placed as (docs/modular-graph-phase-b3b-design.md section 2): which role
// kind has a script node, its type, and how buildGraph wires each type between the routing, the controller, its
// system and the host. A role's node is placed before the systems, feeding its system's inputs; its type may name one
// more for the role, placed after the systems, which reads what its system sent in the same block (`out`: LSDj's
// serial-out decoder, docs/lsdj-serial-out-design.md section 3). Pure and zod-free, so buildGraph, both stores and a
// DirectGraph read the same table, with or without a role registry: the tables are live registries
// (contributions/protocolRegistries.ts), seeded by the generated src/generated/protocol.ts and added to by a pack's
// protocol half, so a package's protocol member is the one statement of its nodes and what is here is the contract
// they satisfy and the wiring the app reads them through.
//
// The mapping lives here rather than on RoleType because the node types are code in the script-node bundle every
// Engine embeds: which role kinds have one is a fact of that bundle, not of whichever registry a store was handed,
// and three publishers need it, two of which hold no registry at all (a DirectGraph, a SystemsStore built without
// one, as the tests build them). A field on RoleType would need this table as its fallback anyway, which makes two
// sources that can disagree about a role. The registry stays what it is to the app: each role's schema,
// onConstruct and menu, on the control plane.
import type { RoleInstance } from "@retroplug/core/systemRoles";
import { isWired, type ProtocolNodeSpec, type SystemInput } from "./protocolSpec";
// The live tables, seeded by the generated module this imports first (docs/downloadable-packs-m2e-plan.md): a pack's
// protocol half adds to them, so they are read through the functions below, each rebuilt after a commit.
import "../generated/protocol";
import { protocolNodesRegistry, roleNodesRegistry } from "../contributions/protocolRegistries";
import { derived, derivedList } from "../contributions/registry";

export type { SystemInput, ProtocolNodeSpec } from "./protocolSpec";

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
// (the Game Boy's serial out in, host MIDI out). The tables hold the literals as a package's manifest spells them.

/** A protocol node type: any id a package or a loaded pack declares. */
export type ProtocolNodeType = string;

/** Every wired protocol node's spec, by type: the product's and each loaded pack's. */
export const protocolNodes = derived((): Readonly<Record<string, ProtocolNodeSpec>> => {
  const out: Record<string, ProtocolNodeSpec> = Object.create(null);
  for (const entry of protocolNodesRegistry.items()) {
    if (!isWired(entry)) continue;
    const { type, ...spec } = entry;
    out[type] = spec as ProtocolNodeSpec;
  }
  return out;
});

/** The node type each role kind is placed as. */
export const roleNodes = derived((): Readonly<Record<string, string>> => {
  const out: Record<string, string> = Object.create(null);
  for (const { kind, node } of roleNodesRegistry.items()) out[kind] = node;
  return out;
});

/** Every protocol node type declared, wired or not, in commit order (the seed's span-label order, then the packs'). */
export const declaredNodeTypes = derivedList((): readonly string[] => protocolNodesRegistry.items().map((e) => e.type));

/** A wired node type's spec, or undefined for one that is not wired or not declared. */
export function protocolSpecOf(type: string): ProtocolNodeSpec | undefined {
  const table = protocolNodes();
  return Object.prototype.hasOwnProperty.call(table, type) ? table[type] : undefined;
}

/** The type of the node placed after its system for a role whose node is of type `node`, or undefined for none. */
export function outNodeOf(node: ProtocolNodeType): ProtocolNodeType | undefined {
  return protocolSpecOf(node)?.out;
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
  const table = roleNodes();
  return Object.prototype.hasOwnProperty.call(table, kind) ? table[kind] : undefined;
}

/** The system inputs the node a role of `kind` is placed as writes; none for a role with no node. */
export function protocolWrites(kind: string): readonly SystemInput[] {
  const node = protocolNodeOf(kind);
  return node ? (protocolSpecOf(node)?.writes ?? []) : [];
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
 *  the protocol table), and `leftOut` hears of each of those. */
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
    const writes: readonly SystemInput[] = protocolSpecOf(node)!.writes;
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

/** The kinds of `roles` the graph places, in role order, each once: a kind with a protocol node when protocolPipeline
 *  places it, and a kind with none (a core's config role, an asset override, a tracker's marker) when it is present,
 *  as the pipeline never leaves one of those out. What a reader asks before it treats a system as a cart's: a stored
 *  role list can keep a role the graph leaves out (an mGB cart still carrying lsdj-sync after a ROM swap, an LSDj cart
 *  carrying mgb), and the stored role stays, so a ROM swapped back brings its settings back. The pipeline decides by
 *  kind and order alone, so no config is read. */
export function placedKinds(roles: readonly { readonly kind: string }[]): string[] {
  const withNodes = new Set(protocolPipeline(roles.map((r) => ({ kind: r.kind, config: {} }))).map((p) => p.kind));
  const out: string[] = [];
  for (const r of roles) {
    if (out.includes(r.kind)) continue;
    if (withNodes.has(r.kind) || protocolNodeOf(r.kind) === undefined) out.push(r.kind);
  }
  return out;
}
