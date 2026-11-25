// A pack's contribution-kind items (docs/downloadable-packs-m2f-plan.md): its app half's control halves paired with its
// pack.json's kinds entries, held to the declaring kind's rules as the generator holds a package's (the pairing, the
// data keys, the kind's own check), as the live kind tables' additions. The kind's `add` runs once the commit stands
// (kindsAdded).
import type { Addition } from "../contributions/registry";
import { declaredKind, kindDataRegistry, kindItemsRegistry } from "../contributions/kindRegistries";
import { protocolNodes, roleNodes } from "../graph/protocolNodes";
import { isWired, type ProtocolNodeEntry } from "../graph/protocolSpec";
import type { PackInfo } from "./packHost";

/** One kind's items as a pack contributes them: the app half's items and the manifest's data, by id. */
export interface PackKindItems {
  readonly kind: string;
  readonly items: readonly { readonly id: string }[];
  readonly data: Readonly<Record<string, Readonly<Record<string, unknown>>>>;
}

const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

/** The kinds the pack contributes to, checked; throws the first refusal. `app` is its app half's default export. */
export function packKindItems(pack: PackInfo, app: Record<string, unknown> | undefined): PackKindItems[] {
  const manifest = pack.kinds ?? {};
  const kinds = new Set<string>(Object.keys(manifest));
  if (app) for (const key of Object.keys(app)) if (declaredKind(key) && Array.isArray(app[key])) kinds.add(key);
  const out: PackKindItems[] = [];
  for (const kind of kinds) {
    const decl = declaredKind(kind);
    if (!decl) throw new Error(`it contributes to ${kind}, a contribution kind no loaded package declares`);
    if (!decl.contribution.add) throw new Error(`it contributes to ${kind}, which takes no items from a pack`);
    const items = (app && Array.isArray(app[kind]) ? app[kind] : []) as { id: string }[];
    const entries = has(manifest, kind) ? manifest[kind]! : [];
    const entryIds = entries.map((e) => e.id as string);
    for (const item of items) {
      if (!entryIds.includes(item.id)) {
        throw new Error(`its app half contributes the ${kind} item ${item.id}, which its pack.json's kinds member does ` +
          "not declare");
      }
    }
    for (const id of entryIds) {
      if (!items.some((item) => item.id === id)) {
        throw new Error(`its pack.json's kinds member declares the ${kind} item ${id}, which its app half does not ` +
          "contribute");
      }
    }
    // A null-prototype table, so an id of "__proto__" is a key and never the prototype.
    const data: Record<string, Readonly<Record<string, unknown>>> = Object.create(null);
    for (const entry of entries) {
      const own: Record<string, unknown> = {};
      for (const key of Object.keys(entry)) {
        if (key === "id") continue;
        if (!decl.data.includes(key)) {
          throw new Error(`its ${kind} item ${entry.id as string} has ${JSON.stringify(key)}, which nothing reads ` +
            `(the kind's data keys are ${decl.data.join(", ")})`);
        }
        own[key] = entry[key];
      }
      if (Object.keys(own).length) data[entry.id as string] = own;
    }
    // The kind's own check, against the live protocol tables and the pack's own protocol half, which commits with it.
    const ownNodes = (pack.protocol?.nodes ?? []) as ProtocolNodeEntry[];
    decl.contribution.check?.(items, data, {
      nodeTypes: [...Object.keys(protocolNodes()), ...ownNodes.filter(isWired).map((n) => n.type)],
      roleNodes: { ...roleNodes(), ...(pack.protocol?.roles ?? {}) },
    });
    out.push({ kind, items, data });
  }
  return out;
}

/** The additions a pack's checked kind items make: their ids and their data. */
export function kindAdditions(pkg: string, kinds: readonly PackKindItems[]): Addition<any>[] {
  return [
    { registry: kindItemsRegistry, pkg, items: kinds.flatMap((k) => k.items.map((item) => ({ kind: k.kind, id: item.id }))) },
    { registry: kindDataRegistry, pkg, items: kinds.flatMap((k) =>
      Object.entries(k.data).map(([id, data]) => ({ kind: k.kind, id, data }))) },
  ];
}

/** After the commit stood: each kind's `add` with the pack's items. A throw is reported, never raised: the commit has
 *  happened (a kind's check is where a refusal belongs). */
export function kindsAdded(pkg: string, kinds: readonly PackKindItems[]): void {
  for (const k of kinds) {
    try {
      declaredKind(k.kind)?.contribution.add?.(k.items, k.data);
    } catch (e) {
      console.warn(`[packs] the ${k.kind} kind's add threw for pack ${pkg}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
}
