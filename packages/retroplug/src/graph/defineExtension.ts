// Extensions: control-plane features that attach to a node INSTANCE (docs/modular-graph-design.md
// sections 2.6, 2.7 and 7.1). An extension has no ports; it is chosen by a matcher over the node's
// identity, and contributes nodes it inserts and wires, views, and song / asset catalogs.
//
// Port ownership: each node input may be claimed by only ONE contributor - an extension on a node, or the
// base graph the builder made itself. collect() reports a second claim as a conflict naming every claimant,
// and does the same for two contributors inserting nodes with the same id, instead of silently picking one.
import type { z } from "zod";
import type { AssetCatalog } from "../tracker/assetCatalog";
import type { SongCatalog } from "../tracker/songCatalog";
import { plainId } from "./descriptor";
import { checkNode, checkWire, type NodeDoc, type WireDoc } from "./graphDocument";

/** What a matcher sees: the node type plus the ROM identity rp.system.rom/1 publishes (when it has one). */
export interface NodeInfo {
  id: string;
  type: string;
  rom?: {
    title?: string;
    /** The cart's identifying header bytes, as the system node reports them. */
    header?: Uint8Array;
    /** Markers a ROM provider found (e.g. "lsdj", "risa"). */
    markers?: readonly string[];
  };
}

export type ViewPlacement = "tile" | "overlay" | "fullscreen";

export interface ViewDefinition {
  id: string;
  label: string;
  placement: ViewPlacement;
  /** Beats the node type's own default view (BlipToaster opens on its panel, not NES video). */
  default?: boolean;
  /** The UI component. Typed loosely on purpose: src/ never imports ui/, which owns the component types. */
  component: unknown;
  /** Data the view reads, so the host can arm only those snapshot ranges. Optional. */
  needs?: { memory?: { region: string; range: [number, number] }[]; rate?: "frame" | "block" };
}

/** What an extension adds for one node. Menu items and actions are deliberately absent until the package
 *  split extracts the menu item model (design section 7.7). */
export interface Contribution {
  nodes?: NodeDoc[];
  wires?: WireDoc[];
  views?: ViewDefinition[];
  songs?: SongCatalog;
  assets?: AssetCatalog;
}

/** What an extension is handed. Capabilities and putResource (design section 7.1) arrive with Phase B. */
export interface ExtensionContext<S> {
  node: NodeInfo;
  settings: S;
}

export interface ExtensionDefinition<S = Record<string, never>> {
  id: string;
  matches(node: NodeInfo): boolean;
  settings?: z.ZodType<S>;
  contribute(ctx: ExtensionContext<S>): Contribution;
}

// Settings and their type come together: an extension that reads settings must say what they are, so
// `settings` cannot be typed as something the schema-less default ({}) is not.
export function defineExtension(
  def: Omit<ExtensionDefinition<Record<string, never>>, "settings"> & { settings?: undefined },
): ExtensionDefinition<Record<string, never>>;
export function defineExtension<S>(def: ExtensionDefinition<S> & { settings: z.ZodType<S> }): ExtensionDefinition<S>;
export function defineExtension<S>(def: ExtensionDefinition<S>): ExtensionDefinition<S> {
  if (!plainId(def.id))
    throw new Error(`extension id ${JSON.stringify(String(def.id))} must be non-empty, with no whitespace or control characters`);
  return def;
}

/** Who claimed something: an extension on a node, or the base graph the builder made itself. */
export type Contributor = { extension: string; node: string } | "base";

export type Conflict =
  /** Two or more contributors wire into the same node input, "node.port". */
  | { kind: "input"; input: string; by: Contributor[] }
  /** Two or more contributors insert a node with the same id. */
  | { kind: "node-id"; id: string; by: Contributor[] };

/** A contribution that could not be used as given; the rest of the graph is still collected. */
export interface CollectError {
  node: string;
  extension: string;
  code: "settings-invalid" | "node-invalid" | "wire-invalid";
  message: string;
}

export interface Collected {
  nodes: NodeDoc[];
  wires: WireDoc[];
  views: { node: string; extension: string; view: ViewDefinition }[];
  attached: { node: string; extension: string; contribution: Contribution }[];
  conflicts: Conflict[];
  errors: CollectError[];
}

// A JSON-only copy of a node or wire document, so a builder that edits the collected graph cannot reach the
// contribution it came from (or the other way round). Views and catalogs are not documents (they carry
// components and methods), so they are passed through as they are.
const copy = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

// Extensions are kept sorted by id, and contributions are collected node by node in that order, so the
// same project builds the same graph whatever order packages registered (or loaded) in.
export class ExtensionRegistry {
  private readonly list: ExtensionDefinition<any>[] = [];

  add<S>(ext: ExtensionDefinition<S>): void {
    if (this.list.some((e) => e.id === ext.id)) throw new Error(`extension "${ext.id}" is already registered`);
    this.list.push(ext);
    this.list.sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
  }

  attached(node: NodeInfo): ExtensionDefinition<any>[] {
    return this.list.filter((e) => e.matches(node));
  }

  /** Every matched extension's contribution for these nodes. settingsFor returns an extension's raw
   *  persisted settings for a node (undefined: defaults); they are parsed here, on the control plane, and
   *  settings that no longer parse are reported and replaced by the defaults rather than failing the
   *  whole collection. `base` is the graph the builder made itself: its node ids and wired inputs are
   *  already claimed. */
  collect(
    nodes: readonly NodeInfo[],
    settingsFor: (node: string, extension: string) => unknown = () => undefined,
    base: { nodes: readonly NodeDoc[]; wires: readonly WireDoc[] } = { nodes: [], wires: [] },
  ): Collected {
    const out: Collected = { nodes: [], wires: [], views: [], attached: [], conflicts: [], errors: [] };
    const inputs = new Map<string, Contributor[]>();
    const ids = new Map<string, Contributor[]>();
    const claim = (map: Map<string, Contributor[]>, key: string, by: Contributor) => map.set(key, [...(map.get(key) ?? []), by]);
    for (const n of base.nodes) claim(ids, n.id, "base");
    for (const n of nodes) if (!ids.has(n.id)) claim(ids, n.id, "base");
    for (const w of base.wires) claim(inputs, w.to, "base");

    for (const node of nodes) {
      for (const ext of this.attached(node)) {
        const by: Contributor = { extension: ext.id, node: node.id };
        const fail = (code: CollectError["code"], message: string) =>
          out.errors.push({ node: node.id, extension: ext.id, code, message });
        let settings: unknown = {};
        if (ext.settings) {
          const parsed = ext.settings.safeParse(settingsFor(node.id, ext.id) ?? {});
          if (parsed.success) {
            settings = parsed.data;
          } else {
            const issues = parsed.error.issues.map((i) => `${i.path.join(".") || "settings"}: ${i.message}`).join("; ");
            const defaults = ext.settings.safeParse({});
            if (!defaults.success) {  // nothing usable: leave this extension out for this node
              fail("settings-invalid", `${issues}; the defaults do not parse either, so it is left out`);
              continue;
            }
            fail("settings-invalid", `${issues}; using the defaults`);
            settings = defaults.data;
          }
        }
        const c = ext.contribute({ node, settings });
        out.attached.push({ node: node.id, extension: ext.id, contribution: c });
        // Each inserted node and wire is checked as toGraphJson will check it, BEFORE the copy (which would
        // spell a NaN as null): one bad item is reported against its extension and left out, rather than
        // failing the whole document later with no one to blame.
        for (const n of c.nodes ?? []) {
          const why = checkNode(n);
          if (why) {
            fail("node-invalid", `inserted node ${JSON.stringify(String(n.id))}: ${why}`);
            continue;
          }
          claim(ids, n.id, by);
          out.nodes.push(copy(n));
        }
        for (const w of c.wires ?? []) {
          const why = checkWire(w);
          if (why) {
            fail("wire-invalid", `wire ${JSON.stringify(String(w.from))} -> ${JSON.stringify(String(w.to))}: ${why}`);
            continue;
          }
          claim(inputs, w.to, by);
          out.wires.push(copy(w));
        }
        for (const view of c.views ?? []) out.views.push({ node: node.id, extension: ext.id, view });
      }
    }
    for (const [input, by] of inputs) if (by.length > 1) out.conflicts.push({ kind: "input", input, by });
    for (const [id, by] of ids) if (by.length > 1) out.conflicts.push({ kind: "node-id", id, by });
    return out;
  }
}
