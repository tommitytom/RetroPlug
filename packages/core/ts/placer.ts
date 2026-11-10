// What a package's graph placer declares (docs/modular-graph-phase-f5a-design.md section 6.3): the slots in the graph
// the shell builds where the package's own nodes go, read by buildGraph at the positions it holds them, in product
// order. A placer is how a package places nodes of its own into a document the shell assembles - the control surface's
// session and its two surface nodes are the launchpad package's - without the shell naming the package, its node types
// or its settings.
//
// The types are core's because buildGraph reads them and a package writes them, and they must import no node code and
// no zod: the generated placers module (packages/retroplug/src/generated/placers.ts) carries one import per declaring
// package, and the planner's bundle is held to carrying no node module and no schema
// (test/script/bundle-protocol.test.ts).
import type { NodeDoc, WireDoc } from "./graphDocument";
import type { KindData } from "./defineKind";

/** Nodes and wires a slot places, in document order: the nodes first in the order given, the wires in theirs. Document
 *  order is plan order on ties (the planner breaks a cycle's tie by it), so a placer says where its nodes sit by which
 *  slot it fills. */
export interface Placed {
  readonly nodes: readonly NodeDoc[];
  readonly wires: readonly WireDoc[];
}

/** A system as a placer sees it: its id, the node type its core is placed as, and the protocol nodes its roles place,
 *  in the system's order. The shell's own system model satisfies it. */
export interface PlaceSystem {
  readonly id: number;
  readonly type: string;
  readonly pipeline: readonly PlaceRole[];
}

/** One protocol role as a placer sees it (the shell's ProtocolRole). */
export interface PlaceRole {
  readonly kind: string;
  readonly node: string;
  readonly config: Readonly<Record<string, unknown>>;
}

/** The protocol node spec a placement's `wires` is handed, as far as a placer may read it: whether the node takes a
 *  controller's launches (the shell's ProtocolNodeSpec carries more, and satisfies this). */
export interface PlaceSpec {
  readonly launches?: true;
}

/** What a placement asks the shell to do, slot by slot. Every member is optional: a placer fills the slots it has
 *  something for, and a slot it leaves out places nothing. */
export interface Placement {
  /** After the host's MIDI input and before the routing node: the session's device input, say. */
  readonly inputs?: Placed;
  /** After the routing node and its wire, before any system's protocol node. */
  readonly session?: Placed;
  /** After the host MIDI out and the wires into it: the session's device output. */
  readonly outputs?: Placed;
  /** The host MIDI out's first inputs, ahead of every system's, so they win a tie at a frame. */
  readonly hostMidi?: readonly { readonly from: string; readonly system: number }[];
  /** Settings merged over a system's protocol node of a type, and over its role's second node: what a session forces on
   *  the cart it drives (a sync mode). A Game Boy's serial-out capture follows them. */
  readonly overrides?: readonly {
    readonly system: number;
    readonly node: string;
    readonly settings: Readonly<Record<string, unknown>>;
  }[];
  /** Wires to add beside a protocol node's own, called after its routing wire and before its writes: where a session's
   *  launches go. `spec` is the node's own spec (PlaceSpec), and `nodeId` is the id buildGraph placed it under, so a
   *  placer names the node it wires into without rebuilding the shell's id convention. */
  wires?(system: number, role: PlaceRole, spec: PlaceSpec, nodeId: string): readonly WireDoc[];
}

/** A package's graph placer: the section / model key it reads (`GraphModel.models[key]`), and where its nodes go. */
export interface GraphPlacer<M = unknown> {
  /** The model key: the project section the placer's model comes from ("controller"). */
  readonly key: string;
  /** The model as the package runs it (a saved app config parsed by the app's own schema). A model read from a
   *  project is normalized by the store's service before it is published, so `DirectGraph`, which takes a model from a
   *  caller, applies this itself - and a model built by hand in a test can apply it too. */
  normalize?(model: M): M;
  place(model: M, ctx: PlaceContext): Placement;
}

/** What a placer may read of the model it is placing into: the systems in slot order, and the manifest data the
 *  packages contribute to the kinds they declare, by kind and then by item id (docs/modular-graph-phase-f5a-design.md
 *  section 6.5: generated/protocol.ts's KIND_DATA). */
export interface PlaceContext {
  readonly systems: readonly PlaceSystem[];
  /** The data the contributing manifests carry under `kind`, by item id, or an empty record for a kind nothing
   *  contributes to this product. */
  kindData(kind: string): KindData;
}

/** The identity function with types: a placer module's default export, checked against the contract. */
export function definePlacer<M>(placer: GraphPlacer<M>): GraphPlacer<M> {
  return placer;
}
