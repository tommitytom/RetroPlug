// Extensions: control-plane features that attach to a node INSTANCE (docs/modular-graph-design.md
// sections 2.6, 2.7 and 7.1). An extension has no ports; it is chosen by a matcher over the node's
// identity, and contributes nodes it inserts and wires, views, and song / asset catalogs.
//
// Port ownership: each node input may be claimed by only ONE contributor. collect() reports two
// contributions wiring into the same input as a conflict, by name, instead of silently picking one.
import type { z } from "zod";
import type { AssetCatalog } from "../tracker/assetCatalog";
import type { SongCatalog } from "../tracker/songCatalog";
import type { NodeDoc, WireDoc } from "./graphDocument";

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

export interface Contribution {
  nodes?: NodeDoc[];
  wires?: WireDoc[];
  views?: ViewDefinition[];
  songs?: SongCatalog;
  assets?: AssetCatalog;
}

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

export function defineExtension<S = Record<string, never>>(def: ExtensionDefinition<S>): ExtensionDefinition<S> {
  if (!def.id) throw new Error("an extension needs an id");
  return def;
}

export interface Conflict {
  /** The node input two contributors both wire into, "node.port". */
  input: string;
  extensions: string[];
}

export interface Collected {
  nodes: NodeDoc[];
  wires: WireDoc[];
  views: { node: string; extension: string; view: ViewDefinition }[];
  attached: { node: string; extension: string; contribution: Contribution }[];
  conflicts: Conflict[];
}

// Extensions are listed in registration order; that order is also the order their contributions are
// collected in, so graphs built from the same project are identical.
export class ExtensionRegistry {
  private readonly list: ExtensionDefinition<any>[] = [];

  add<S>(ext: ExtensionDefinition<S>): void {
    if (this.list.some((e) => e.id === ext.id)) throw new Error(`extension "${ext.id}" is already registered`);
    this.list.push(ext);
  }

  attached(node: NodeInfo): ExtensionDefinition<any>[] {
    return this.list.filter((e) => e.matches(node));
  }

  /** Every matched extension's contribution for these nodes. settingsFor returns an extension's raw
   *  persisted settings for a node (undefined: defaults); they are parsed here, on the control plane. */
  collect(nodes: readonly NodeInfo[], settingsFor: (node: string, extension: string) => unknown = () => undefined): Collected {
    const out: Collected = { nodes: [], wires: [], views: [], attached: [], conflicts: [] };
    const claimed = new Map<string, string[]>();
    for (const node of nodes) {
      for (const ext of this.attached(node)) {
        const raw = settingsFor(node.id, ext.id);
        const settings = ext.settings ? ext.settings.parse(raw ?? {}) : {};
        const c = ext.contribute({ node, settings });
        out.attached.push({ node: node.id, extension: ext.id, contribution: c });
        out.nodes.push(...(c.nodes ?? []));
        out.wires.push(...(c.wires ?? []));
        for (const view of c.views ?? []) out.views.push({ node: node.id, extension: ext.id, view });
        for (const w of c.wires ?? []) claimed.set(w.to, [...(claimed.get(w.to) ?? []), ext.id]);
      }
    }
    for (const [input, extensions] of claimed)
      if (extensions.length > 1) out.conflicts.push({ input, extensions });
    return out;
  }
}
