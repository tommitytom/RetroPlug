// The protocol and kinds halves are data the pack list carries (committed without an import) and the placer half is a module
// (docs/downloadable-packs-m2e-plan.md); the render worker loads the control plane's halves (`loadAppPacks`) on a bare
// runtime with no timers (docs/downloadable-packs-m2d-plan.md).
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
import { contributionKindsRegistry } from "../contributions/kindRegistries";
import type { PackageContributions } from "@retroplug/core/definePackage";
import type { ConsoleDescriptor, ConsolesContribution } from "@retroplug/core/consoles";
import { placersRegistry } from "../contributions/placers";
import type { GraphPlacer } from "@retroplug/core/placer";
import { nativePackHost, type HalfKind, type PackHost, type PackInfo, type PackProtocol } from "./packHost";
import { protocolAdditions } from "./protocolHalf";
import { kindAdditions, kindsAdded, packKindItems, type PackKindItems } from "./kindsHalf";

/** A half's default export as the additions a commit takes; throws when the export is not the half's shape. */
export type HalfBuilder = (pkg: string, exported: unknown) => Addition<any>[];

/** The whole load's budget for the packs' halves to evaluate, in ms: one clock for every import of a call, below the
 *  hosts' own ready deadline (10 s), so a few slow packs cannot add up past it. A stuck pack is refused by name; a half
 *  with no top-level await still settles in microtasks, before a 0 ms timer, so a fast pack after the budget is spent
 *  still loads. A runtime with no timers has none (importBefore). */
let loadBudgetMs = 5000;

/** Test seam: the load budget in ms, returning the previous one. */
export function setLoadBudgetMs(ms: number): number {
  const was = loadBudgetMs;
  loadBudgetMs = ms;
  return was;
}

// QuickJS runs a module's body as an async function and drops that function's result promise, so a half that throws at
// load leaves a rejection nothing can attach a handler to, besides the import() promise this loader does handle. txiki
// reports it as an unhandled rejection: a session host (the CLI) fails on one, the test harness records it, and the
// plugin and standalone are left a stale pending exception that a later JS_HasException check could raise in unrelated
// code. A failed import's reason is remembered so that one report is cancelled, and forgotten again after the pass that
// would dispatch it (txiki dispatches right after the microtask drain this runs in, and a timer fires only afterwards):
// a half that throws after a top-level await never produces the report, and its reason must not cancel an unrelated
// rejection of the same value later.
const failedImports = new Set<unknown>();

function noteFailedImport(reason: unknown): void {
  failedImports.add(reason);
  const timers = globalThis as unknown as { setTimeout?(fn: () => void, ms: number): unknown };
  if (timers.setTimeout) timers.setTimeout(() => failedImports.delete(reason), 0);
  else failedImports.delete(reason);
}

// Registered when the module loads, so it runs before any listener a later bundle (the test harness's) adds, and stops
// the report from reaching them: the report is cancelled for everyone, not just the host's default.
(globalThis as unknown as {
  addEventListener?: (
    type: string,
    fn: (e: { reason: unknown; preventDefault(): void; stopImmediatePropagation?(): void }) => void,
  ) => void;
}).addEventListener?.("unhandledrejection", (e) => {
  if (!failedImports.delete(e.reason)) return;
  e.preventDefault();
  e.stopImmediatePropagation?.();
});

/** The half's namespace, or the refusal that names it when it has not settled by `deadline` (a Date.now() time). */
async function importBefore(host: PackHost, id: string, kind: HalfKind, deadline: number): Promise<unknown> {
  // The shell's tsconfig carries no timer types (txiki provides the globals), so they are reached by name.
  const timers = globalThis as unknown as {
    setTimeout?(fn: () => void, ms: number): unknown;
    clearTimeout?(id: unknown): void;
  };
  const imported = host.importHalf(id, kind);
  imported.catch(noteFailedImport);
  // A runtime with no timers (a render job's bare QuickJS) has no budget: a half there has no timer to wait on either,
  // so it settles in the job queue its host runs, or never, which the host reports (host/render/RenderHost).
  if (!timers.setTimeout || !timers.clearTimeout) return imported;
  let timer: unknown;
  const stuck = new Promise<never>((_, reject) => {
    timer = timers.setTimeout!(() => reject(new BudgetError(
      `the packs' load ran past its ${loadBudgetMs / 1000} s budget in its ${kind} half`)), Math.max(0, deadline - Date.now()));
  });
  try {
    return await Promise.race([imported, stuck]);
  } finally {
    timers.clearTimeout!(timer);
  }
}

/** The default export as an object, or the refusal that names the half. */
export function objectExport(half: HalfKind, exported: unknown): Record<string, unknown> {
  if (typeof exported !== "object" || exported === null || Array.isArray(exported)) {
    throw new Error(`its ${half} half's default export is not an object`);
  }
  return exported as Record<string, unknown>;
}

/** The halves whose content is the manifest's, not a module: committed from the pack list, never imported. */
const DATA_HALVES: ReadonlySet<HalfKind> = new Set<HalfKind>(["protocol", "kinds"]);

/** A placer half's default export, held to the contract buildGraph calls. */
function placerExport(exported: unknown): GraphPlacer<unknown> {
  const p = objectExport("placer", exported);
  if (typeof p.key !== "string" || p.key === "" || typeof p.place !== "function") {
    throw new Error("its placer half's default export is not a graph placer (a key and a place function)");
  }
  return p as unknown as GraphPlacer<unknown>;
}

/** The halves the control plane loads: the app's registries, the consoles', the protocol tables, the kinds' items and the placers.
 *  The UI's and the CLI's builders add theirs (ui/packs/uiPacks.ts, cli/packs/cliPacks.ts). */
export const APP_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  app: (pkg, exported) => appAdditions(pkg, objectExport("app", exported) as PackageContributions),
  consoles: (pkg, exported) => {
    const c = objectExport("consoles", exported) as ConsolesContribution;
    return [
      { registry: consolesRegistry, pkg, items: c.consoles ?? [] },
      { registry: realCartsRegistry, pkg, items: c.realCarts ?? [] },
    ];
  },
  protocol: (pkg, exported) => protocolAdditions(pkg, exported as PackProtocol | undefined),
  // The kinds half is global code the Engines run and its data is pack.json's: the additions need the app half too, so
  // the staging builds them (kindAdditions). The builder is here so the half is in the control plane's set.
  kinds: () => [],
  placer: (pkg, exported) => [{ registry: placersRegistry, pkg, items: [placerExport(exported)] }],
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

/** A half that ran past the load budget: the one refusal worth remembering process-wide, since it cost the budget. */
class BudgetError extends Error {}

function refuse(host: PackHost, id: string, reason: string, skipLater = false): void {
  refused.set(id, reason);
  console.warn(`[packs] pack ${id} is refused: ${reason}`);
  try {
    host.refuse(id, reason, skipLater);
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
    const declaredKinds = contributionKindsRegistry.items().map((k) => k.kind);
    const manifestKinds = Object.keys(pack.kinds ?? {});
    for (const member of Object.keys(app)) {
      if (!APP_MEMBERS.has(member) && !declaredKinds.includes(member) && !manifestKinds.includes(member)) {
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
  if (exported.has("placer")) {
    const key = objectExport("placer", exported.get("placer")).key;
    const sections = exported.has("app") ? (objectExport("app", exported.get("app")).projectSections ?? []) : [];
    if (!(sections as { key: string }[]).some((s) => s.key === key)) {
      throw new Error(`its placer half places the model ${String(key)}, which its app half declares no project ` +
        "section for");
    }
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
  /** The contribution-kind items it commits, whose kinds take them through `add` once the commit stands. */
  readonly kindItems: readonly PackKindItems[];
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
  const deadline = Date.now() + loadBudgetMs;
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
    // The kinds half and the app half commit together: the items pair across the two, so one without the other (a
    // builder set holding only one, or an app half an earlier call loaded) would only misfire later as a mismatch.
    if (pack.halves.includes("app") && pack.halves.includes("kinds") && builders.kinds) {
      const at = todo.indexOf("kinds");
      if (!todo.includes("app") && at >= 0) todo.splice(at, 1);
    }
    if (!todo.length) continue;
    try {
      const exported = new Map<HalfKind, unknown>();
      for (const kind of todo) {
        if (DATA_HALVES.has(kind)) {
          exported.set(kind, kind === "kinds" ? pack.kinds : pack.protocol);
          continue;
        }
        const ns = (await importBefore(host, pack.id, kind, deadline)) as { default?: unknown };
        exported.set(kind, ns.default);
      }
      checkManifest(pack, exported, platforms);
      const additions = todo.flatMap((kind) => builders[kind]!(pack.id, exported.get(kind)));
      let kindItems: PackKindItems[] = [];
      if (exported.has("app") || todo.includes("kinds")) {
        const app = exported.has("app") ? objectExport("app", exported.get("app")) : undefined;
        kindItems = packKindItems(pack, app, staged.map((s) => s.pack));
        // The items commit with the kinds half; a context without its builder commits none.
        if (todo.includes("kinds")) additions.push(...kindAdditions(pack.id, kindItems));
        else kindItems = [];
      }
      staged.push({ pack, kinds: todo, kindItems, additions, own: ownPlatforms(exported) });
      for (const id of pack.provides.platforms) platforms.add(id);
    } catch (e) {
      refuse(host, pack.id, message(e), e instanceof BudgetError);
    }
  }
  if (!staged.length) return;
  const accept = (s: Staged): void => {
    const halves = loaded.get(s.pack.id) ?? new Set<HalfKind>();
    for (const kind of s.kinds) halves.add(kind);
    loaded.set(s.pack.id, halves);
    kindsAdded(s.pack.id, s.kindItems);
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

/** The control plane's packs: their app, consoles, protocol, kinds and placer halves. */
export function loadAppPacks(): Promise<void> {
  return loadPacks(nativePackHost(), APP_BUILDERS);
}
