// A contribution kind a package declares for other packages to fill (docs/modular-graph-phase-f5a-design.md section
// 6.5): one package says what an item of the kind IS (its id, the load-time accept that fills its own registry, and its
// own check over the data a contributing manifest carries), and any listed package may contribute items. A kind is
// manifest data, so the generator knows no kind by name.
//
// The two halves of an item live apart on purpose. Its CONTROL-plane half is a value in the contributing package's app
// module, reachable only from the control plane (the app modules carry a role's schema and zod). Its RUNTIME half and
// its data are a manifest entry (`protocol.kinds.<kind>`), which the generated script entry imports and the generated
// protocol module carries as data. The generated registry joins the halves by id, refuses one alone, and hands the
// declaring package its items and their data once at load.
//
// This file is types and an identity function: the declaring package's `control` module default-exports defineKind, and
// the generated registry imports it. Nothing here imports a package above core.
//
// A package that DECLARES a kind owes one more thing: an augmentation of PackageContributions (definePackage.ts) for
// the list its app module carries, the kind's name, so that `definePackage({ <kind>: [...] })` typechecks. Without it
// the generated registry fails tsc at a line that names no manifest; every declaring package's
// ts/packageContributions.ts carries one (see the launchpad's, which also declares the shape of an item).

/** A kind's item, as its declaring package's registry holds it: whatever that package's contract declares, and an `id`
 *  the generated registry reads (the string a project stores, and the one a manifest entry's runtime half is joined
 *  by). */
export interface KindItem {
  readonly id: string;
}

/** The data the contributing manifests declare for a kind's items, by item id: each entry's `data` keys, and nothing
 *  else (the generator refuses an entry key its declaring manifest does not list). */
export type KindData = Readonly<Record<string, Readonly<Record<string, unknown>>>>;

/** What a kind's `check` is handed beside the items and their data. */
export interface KindCheckContext {
  /** Every protocol node type the product's graph WIRES (the generated protocol module's PROTOCOL_NODES, which is data
   *  alone): a kind whose data names a node type to set a mode on (a cart's sync node, say) checks it here, because
   *  only the product knows which node types it places. A script node the graph does not wire - one a package's own
   *  placer places, or the routing node the shell builds - is not in the set: nothing could set a mode on it through a
   *  document's node type, so a kind naming one is refused. */
  readonly nodeTypes: readonly string[];
  /** The protocol node type each role kind is placed as (the generated protocol module's ROLE_NODES), for a kind whose
   *  data names a role beside the node it belongs to and must find the two agree. */
  readonly roleNodes: Readonly<Record<string, string>>;
}

/** A contribution kind, as its declaring package states it (a package's `retroplug.kinds.<kind>.control` module). */
export interface ContributionKind<I extends KindItem = KindItem> {
  /** Hand the declaring package its items once, at load, in product order: it fills its own registry (a menu's table, a
   *  default a section reads). Called with every declared kind, an empty list included. */
  accept(items: readonly I[], data: KindData): void;
  /** The declaring package's own checks over the items and their data, run at load before `accept`: what no generic
   *  validation can know (a mode naming a node type no package declares, a field a runtime cannot read). It throws its
   *  refusal, naming the item. */
  check?(items: readonly I[], data: KindData, ctx: KindCheckContext): void;
}

/** The identity function with types: the default export of a declaring package's `control` module. */
export function defineKind<I extends KindItem>(kind: ContributionKind<I>): ContributionKind<I> {
  return kind;
}
