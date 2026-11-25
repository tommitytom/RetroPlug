// The contribution kinds' live tables (docs/downloadable-packs-m2f-plan.md): the kinds the product's packages declare
// (their ContributionKind and the data keys an item may carry), the item ids taken in each, and each item's data. The
// generated modules seed them: the data from src/generated/protocol.ts, which the planner's bundle carries with no
// app; the kinds and the ids from src/generated/packages.ts, which is the app's. A pack's kind items add to the last
// two at its commit (src/packs/kindsHalf.ts). Only registry.ts and core's types are imported, so the planner's bundle
// carries no schema.
import type { ContributionKind, KindData } from "@retroplug/core/defineKind";
import { derived, Registry } from "./registry";

/** A kind a package declares: its ContributionKind and the manifest keys an item of it may carry as data. */
export interface DeclaredKind {
  readonly kind: string;
  readonly contribution: ContributionKind<any>;
  readonly data: readonly string[];
}

/** An item a package contributes to a kind, by id: what a document names it by. */
export interface KindItemId {
  readonly kind: string;
  readonly id: string;
}

/** An item's data, as its manifest declares it (only the kind's data keys). */
export interface KindItemData extends KindItemId {
  readonly data: Readonly<Record<string, unknown>>;
}

export const contributionKindsRegistry = new Registry<DeclaredKind>("contributionKinds", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item } of entries) {
    const at = owners.get(item.kind);
    if (at !== undefined) {
      throw new Error(at === pkg
        ? `the package ${pkg} declares the contribution kind ${item.kind} twice`
        : `two packages declare the contribution kind ${item.kind}: ${at} and ${pkg}`);
    }
    owners.set(item.kind, pkg);
  }
});

export const kindItemsRegistry = new Registry<KindItemId>("kindItems", (entries, view) => {
  const kinds = new Set(view(contributionKindsRegistry).map((e) => e.item.kind));
  const owners = new Map<string, string>();
  for (const { pkg, item } of entries) {
    // Unseeded (a bundle with no app) reads as empty: the declared-kind rule is the app's to run.
    if (contributionKindsRegistry.seeded && !kinds.has(item.kind)) {
      throw new Error(`${pkg} contributes the ${item.kind} item ${item.id}, and no package declares the contribution ` +
        `kind ${item.kind}`);
    }
    const key = `${item.kind}\u0000${item.id}`;
    const at = owners.get(key);
    if (at !== undefined) {
      throw new Error(at === pkg
        ? `the package ${pkg} contributes the ${item.kind} item ${item.id} twice`
        : `two packages contribute the ${item.kind} item ${item.id}: ${at} and ${pkg}`);
    }
    owners.set(key, pkg);
  }
});

export const kindDataRegistry = new Registry<KindItemData>("kindData", (entries, view) => {
  if (!kindItemsRegistry.seeded) return;
  const items = new Set(view(kindItemsRegistry).map((e) => `${e.item.kind}\u0000${e.item.id}`));
  for (const { pkg, item } of entries) {
    if (!items.has(`${item.kind}\u0000${item.id}`)) {
      throw new Error(`${pkg} declares data for the ${item.kind} item ${item.id}, which no package contributes`);
    }
  }
});

/** The data the manifests declare for each kind's items, by kind and then item id. */
const dataByKind = derived((): Readonly<Record<string, KindData>> => {
  const out: Record<string, Record<string, Readonly<Record<string, unknown>>>> = Object.create(null);
  for (const { kind, id, data } of kindDataRegistry.items()) {
    const of = out[kind] ?? (out[kind] = Object.create(null));
    of[id] = data;
  }
  return out;
});
const EMPTY: KindData = Object.freeze({});

/** The data the manifests declare for `kind`'s items, by item id: what a placer reads through PlaceContext.kindData.
 *  An empty record for a kind nothing contributes to. */
export function kindData(kind: string): KindData {
  const all = dataByKind();
  return Object.prototype.hasOwnProperty.call(all, kind) ? all[kind] : EMPTY;
}

/** The kind `kind` as its declaring package states it, with that package's id, or undefined for one nobody declares. */
export function declaredKind(kind: string): (DeclaredKind & { pkg: string }) | undefined {
  const e = contributionKindsRegistry.entries().find((x) => x.item.kind === kind);
  return e ? { ...e.item, pkg: e.pkg } : undefined;
}
