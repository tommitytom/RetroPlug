// The pack loader (docs/downloadable-packs-m2c-plan.md): each pack's halves a context loads, imported by name, held to
// the pack's manifest, and committed into the live registries. Every pack of a call goes in one commit; when that is
// refused, they commit one at a time in load order, so the refused pack is named and the rest still load (all or
// nothing is per pack). A pack refused in a later context (its UI half, at the first editor open) keeps what an earlier
// one committed: native code and a commit are never undone, and the refusal is reported.
//
// State is per module set: the plugin's control plane and its UI share one, so a half the control plane loaded is not
// loaded again for the UI, and a pack the control plane refused is not tried there.
import { commitContributions, type Addition } from "../contributions/registry";
import { appAdditions } from "../contributions/app";
import { consolesRegistry, realCartsRegistry } from "../contributions/consoleRegistries";
import { KIND_DATA } from "../generated/protocol";
import type { PackageContributions } from "@retroplug/core/definePackage";
import type { ConsoleDescriptor, ConsolesContribution } from "@retroplug/core/consoles";
import { nativePackHost, type HalfKind, type PackHost, type PackInfo } from "./packHost";

/** A half's default export as the additions a commit takes; throws when the export is not the half's shape. */
export type HalfBuilder = (pkg: string, exported: unknown) => Addition<any>[];

/** The default export as an object, or the refusal that names the half. */
export function objectExport(half: HalfKind, exported: unknown): Record<string, unknown> {
  if (typeof exported !== "object" || exported === null || Array.isArray(exported)) {
    throw new Error(`its ${half} half's default export is not an object`);
  }
  return exported as Record<string, unknown>;
}

/** The halves the control plane loads: the app's registries and the consoles'. The UI's and the CLI's builders add
 *  theirs (ui/packs/uiPacks.ts, cli/packs/cliPacks.ts). */
export const APP_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  app: (pkg, exported) => appAdditions(pkg, objectExport("app", exported) as PackageContributions),
  consoles: (pkg, exported) => {
    const c = objectExport("consoles", exported) as ConsolesContribution;
    return [
      { registry: consolesRegistry, pkg, items: c.consoles ?? [] },
      { registry: realCartsRegistry, pkg, items: c.realCarts ?? [] },
    ];
  },
};

const loaded = new Map<string, Set<HalfKind>>();
const refused = new Map<string, string>();

/** What this module set loaded and refused: each pack's halves, and each refused pack's reason. */
export function packStatus(): { loaded: Record<string, HalfKind[]>; refused: Record<string, string> } {
  return {
    loaded: Object.fromEntries([...loaded].map(([id, halves]) => [id, [...halves]])),
    refused: Object.fromEntries(refused),
  };
}

function refuse(host: PackHost, id: string, reason: string): void {
  refused.set(id, reason);
  console.warn(`[packs] pack ${id} is refused: ${reason}`);
  try {
    host.refuse(id, reason);
  } catch (e) {
    console.warn(`[packs] the host could not record the refusal of ${id}: ${message(e)}`);
  }
}

function message(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

/** The app members appAdditions maps: any other member that is not a contribution kind is a typo or a mistake. */
const APP_MEMBERS: ReadonlySet<string> = new Set(["roles", "romProviders", "trackerIntegrations", "kitFormats",
  "projectSections", "projectServices", "configSections", "embeds"]);

/** The platforms the loaded consoles half declares (none without one). */
function ownPlatforms(exported: Map<HalfKind, unknown>): Set<string> {
  const mine = new Set<string>();
  if (exported.has("consoles")) {
    const consoles = (objectExport("consoles", exported.get("consoles")).consoles ?? []) as ConsoleDescriptor[];
    for (const d of consoles) for (const p of d.platforms) mine.add(p.id);
  }
  return mine;
}

/** The first platform the pack requires that neither `available` nor its own consoles provide. */
function missingPlatform(pack: PackInfo, available: ReadonlySet<string>, own: ReadonlySet<string>): string | undefined {
  return pack.requires.platforms.find((id) => !available.has(id) && !own.has(id));
}

/** The pack's manifest against the halves loaded now (`exported`, by kind): throws the first mismatch. */
function checkManifest(pack: PackInfo, exported: Map<HalfKind, unknown>, platforms: ReadonlySet<string>): void {
  if (exported.has("app")) {
    const app = objectExport("app", exported.get("app"));
    if (Array.isArray(app.embeds) && app.embeds.length) {
      throw new Error("its app half declares embeds, and a pack cannot carry an embedded ROM yet (it would be baked " +
        "into the RetroPlug binary)");
    }
    for (const kind of Object.keys(KIND_DATA)) {
      if (Array.isArray(app[kind]) && (app[kind] as unknown[]).length) {
        throw new Error(`its app half contributes ${kind}, a contribution kind a pack cannot add to yet`);
      }
    }
    for (const member of Object.keys(app)) {
      if (!APP_MEMBERS.has(member) && !Object.prototype.hasOwnProperty.call(KIND_DATA, member)) {
        throw new Error(`its app half exports ${member}, which is not something an app half contributes`);
      }
    }
    const roles = ((app.roles ?? []) as { kind: string }[]).map((r) => r.kind);
    for (const kind of roles) {
      if (!pack.provides.roles.includes(kind)) {
        throw new Error(`its app half contributes the role kind ${kind}, which its pack.json's provides.roles does ` +
          "not declare");
      }
    }
    for (const kind of pack.provides.roles) {
      if (!roles.includes(kind)) throw new Error(`provides.roles names ${kind}, which no half of the pack contributes`);
    }
  } else if (pack.provides.roles.length && !pack.halves.includes("app")) {
    throw new Error(`provides.roles names ${pack.provides.roles[0]}, which no half of the pack contributes`);
  }
  const mine = ownPlatforms(exported);
  if (exported.has("consoles")) {
    const consoles = ((objectExport("consoles", exported.get("consoles")).consoles ?? []) as ConsoleDescriptor[]);
    for (const d of consoles) {
      for (const p of d.platforms) {
        if (!pack.provides.platforms.includes(p.id)) {
          throw new Error(`its console ${d.id} declares the platform ${p.id}, which its pack.json's ` +
            "provides.platforms does not declare");
        }
      }
      if (!pack.provides.nodeTypes.includes(d.nodeType)) {
        throw new Error(`its console ${d.id} names the node type ${d.nodeType}, which its pack.json's ` +
          "provides.nodeTypes does not declare");
      }
    }
    for (const id of pack.provides.platforms) {
      if (!mine.has(id)) throw new Error(`provides.platforms names ${id}, which no half of the pack contributes`);
    }
  } else if (pack.provides.platforms.length && !pack.halves.includes("consoles")) {
    throw new Error(`provides.platforms names ${pack.provides.platforms[0]}, which no half of the pack contributes`);
  }
  if (exported.has("app") || exported.has("consoles")) {
    const id = missingPlatform(pack, platforms, mine);
    if (id !== undefined) throw new Error(`it requires the platform ${id}, which no loaded console provides`);
  }
}

interface Staged {
  readonly pack: PackInfo;
  readonly kinds: readonly HalfKind[];
  readonly additions: Addition<any>[];
  /** The platforms its own consoles half declares. */
  readonly own: ReadonlySet<string>;
}

// Calls run one at a time: two overlapping loads would each stage the same pack and commit it twice.
let chain: Promise<void> = Promise.resolve();

/** Load every pack's halves `builders` names that this module set has not loaded yet. Never rejects: a pack that fails
 *  is refused, reported, and commits nothing, and a failure of the loader itself is reported and resolves. */
export function loadPacks(host: PackHost, builders: Partial<Record<HalfKind, HalfBuilder>>): Promise<void> {
  const run = chain.then(() => load(host, builders));
  chain = run.catch(() => {});
  return run;
}

async function load(host: PackHost, builders: Partial<Record<HalfKind, HalfBuilder>>): Promise<void> {
  try {
    await loadUnguarded(host, builders);
  } catch (e) {
    console.warn("[packs] the loader failed: " + message(e));
  }
}

async function loadUnguarded(host: PackHost, builders: Partial<Record<HalfKind, HalfBuilder>>): Promise<void> {
  const kinds = Object.keys(builders) as HalfKind[];
  const platforms = new Set(consolesRegistry.items().flatMap((d) => d.platforms.map((p) => p.id)));
  const staged: Staged[] = [];
  for (const pack of host.list()) {
    if (refused.has(pack.id)) continue;
    const missing = pack.requires.packs.find((dep) => refused.has(dep));
    if (missing) {
      refuse(host, pack.id, `it requires the pack ${missing}, which is refused`);
      continue;
    }
    const todo = kinds.filter((k) => pack.halves.includes(k) && !loaded.get(pack.id)?.has(k));
    if (!todo.length) continue;
    try {
      const exported = new Map<HalfKind, unknown>();
      for (const kind of todo) {
        const ns = (await host.importHalf(pack.id, kind)) as { default?: unknown };
        exported.set(kind, ns.default);
      }
      checkManifest(pack, exported, platforms);
      const additions = todo.flatMap((kind) => builders[kind]!(pack.id, exported.get(kind)));
      staged.push({ pack, kinds: todo, additions, own: ownPlatforms(exported) });
      for (const id of pack.provides.platforms) platforms.add(id);
    } catch (e) {
      refuse(host, pack.id, message(e));
    }
  }
  if (!staged.length) return;
  const accept = (s: Staged): void => {
    const halves = loaded.get(s.pack.id) ?? new Set<HalfKind>();
    for (const kind of s.kinds) halves.add(kind);
    loaded.set(s.pack.id, halves);
  };
  try {
    commitContributions(staged.flatMap((s) => s.additions));
    staged.forEach(accept);
  } catch {
    for (const s of staged) {
      const missing = s.pack.requires.packs.find((dep) => refused.has(dep));
      if (missing) {
        refuse(host, s.pack.id, `it requires the pack ${missing}, which is refused`);
        continue;
      }
      const available = new Set(consolesRegistry.items().flatMap((d) => d.platforms.map((p) => p.id)));
      const lost = missingPlatform(s.pack, available, s.own);
      if (lost !== undefined) {
        refuse(host, s.pack.id, `it requires the platform ${lost}, which no loaded console provides`);
        continue;
      }
      try {
        commitContributions(s.additions);
        accept(s);
      } catch (e) {
        refuse(host, s.pack.id, message(e));
      }
    }
  }
}

/** The control plane's packs: their app and consoles halves. */
export function loadAppPacks(): Promise<void> {
  return loadPacks(nativePackHost(), APP_BUILDERS);
}
