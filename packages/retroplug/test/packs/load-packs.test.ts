// The pack loader (docs/downloadable-packs-m2c-plan.md): a pack's halves imported through the host, held to its
// manifest, and committed all or nothing per pack, in one commit when every pack passes.
import { test, expect } from "@retroplug/testing";
import { loadPacks, packStatus, setLoadBudgetMs, APP_BUILDERS, type HalfBuilder } from "../../src/packs/loadPacks";
import type { HalfKind, PackHost, PackInfo } from "../../src/packs/packHost";
import { rolesRegistry, projectSectionsRegistry } from "../../src/contributions/appRegistries";
import { consolesRegistry, realCartsRegistry } from "../../src/contributions/consoleRegistries";
import { contributionsVersion } from "../../src/contributions/registry";
import { protocolNodeOf, protocolPipeline } from "../../src/graph/protocolNodes";
import { buildGraph, type GraphSystem } from "../../src/graph/buildGraph";
import type { PortDesc, TypeDescriptor } from "@retroplug/core/descriptor";
import { placersRegistry } from "../../src/contributions/placers";
import { controllerApp } from "@retroplug/launchpad/registry";
import { controllerSyncOverride } from "@retroplug/launchpad/placer";
import { kindData } from "../../src/contributions/kindRegistries";

const role = (kind: string) => ({ kind, category: "feature", schema: { parse: () => ({}) } });

function pack(id: string, over: Partial<PackInfo> = {}): PackInfo {
  return {
    id,
    version: "1.0.0",
    halves: ["app"],
    provides: { platforms: [], nodeTypes: [], roles: [] },
    requires: { packs: [], platforms: [] },
    ...over,
  };
}

/** A host serving `modules[id][half]` as each half's default export (a function is called, so a half can throw). */
function fakeHost(packs: PackInfo[], modules: Record<string, Partial<Record<HalfKind, unknown>>>) {
  const refused: Record<string, string> = {};
  const skipLater: Record<string, boolean> = {};
  const imported: string[] = [];
  const host: PackHost = {
    list: () => packs,
    importHalf: async (id, half) => {
      imported.push(`${id}/${half}`);
      const m = modules[id]?.[half];
      if (typeof m === "function") return { default: (m as () => unknown)() };
      return { default: m };
    },
    refuse: (id, reason, skip) => { refused[id] = reason; skipLater[id] = skip === true; },
  };
  return { host, refused, imported, skipLater };
}

test("a pack's app half commits its roles, in one commit with the other packs'", async () => {
  const before = contributionsVersion();
  const { host, refused } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } }),
     pack("lp-b", { provides: { platforms: [], nodeTypes: [], roles: ["lp-b-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } }, "lp-b": { app: { roles: [role("lp-b-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(rolesRegistry.items().some((r) => r.kind === "lp-a-role")).toBe(true);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-b-role")).toBe(true);
  expect(contributionsVersion()).toBe(before + 1);
  expect(packStatus().loaded["lp-a"]).toEqual(["app"]);
});

// Relies on the previous test's module-level state: lp-a is already loaded in this bundle's loader.
test("a half loaded in this context is not imported again", async () => {
  const { host, imported } = fakeHost(
    [pack("lp-a", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } })],
    { "lp-a": { app: { roles: [role("lp-a-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(imported).toEqual([]);
});

test("a pack that fails the commit is refused by name, and the others commit", async () => {
  const builtIn = rolesRegistry.items()[0]!.kind;
  const { host, refused, skipLater } = fakeHost(
    [pack("lp-dupe", { provides: { platforms: [], nodeTypes: [], roles: [builtIn] } }),
     pack("lp-c", { provides: { platforms: [], nodeTypes: [], roles: ["lp-c-role"] } })],
    { "lp-dupe": { app: { roles: [role(builtIn)] } }, "lp-c": { app: { roles: [role("lp-c-role")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-dupe"] ?? "").includes(`declare the role kind ${builtIn} twice`)).toBe(true);
  expect(skipLater["lp-dupe"]).toBe(false); // a deterministic refusal is free to repeat in the next runtime
  expect(rolesRegistry.items().some((r) => r.kind === "lp-c-role")).toBe(true);
  expect((packStatus().refused["lp-dupe"] ?? "").includes("twice")).toBe(true);
});

test("a dependent of a pack refused in the fallback is refused, and a refused pack is not retried", async () => {
  const dupe = pack("lp-dupe", { provides: { platforms: [], nodeTypes: [], roles: ["lp-a-role"] } });
  const { host, refused, imported } = fakeHost(
    [dupe, pack("lp-dep2", { requires: { packs: ["lp-dupe"], platforms: [] } })],
    { "lp-dep2": { app: {} } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(imported).toEqual([]);
  expect((refused["lp-dep2"] ?? "").includes("requires the pack lp-dupe, which is refused")).toBe(true);
  const again = fakeHost([dupe], {});
  await loadPacks(again.host, APP_BUILDERS);
  expect(again.imported).toEqual([]);
});

test("the manifest is held to the halves", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-undeclared"),
     pack("lp-overdeclared", { provides: { platforms: [], nodeTypes: [], roles: ["lp-ghost"] } }),
     pack("lp-embeds", { provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-kind"),
     pack("lp-throws"),
     pack("lp-shape"),
     pack("lp-dependent", { requires: { packs: ["lp-throws"], platforms: [] } }),
     pack("lp-needs-platform", { halves: ["app", "consoles"], requires: { packs: [], platforms: ["lp-nowhere"] } })],
    {
      "lp-undeclared": { app: { roles: [role("lp-undeclared-role")] } },
      "lp-overdeclared": { app: {} },
      "lp-embeds": { app: { embeds: [{ id: "lp-rom" }] } },
      "lp-kind": { app: { controllerApps: [{ id: "x" }] } },
      "lp-throws": { app: () => { throw new Error("the half threw"); } },
      "lp-shape": { app: 42 },
      "lp-dependent": { app: {} },
      "lp-needs-platform": { app: {}, consoles: {} },
    },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-undeclared"] ?? "").includes("contributes the role kind lp-undeclared-role, which its pack.json's " +
    "provides.roles does not declare")).toBe(true);
  expect((refused["lp-overdeclared"] ?? "").includes("provides.roles names lp-ghost, which no half of the pack contributes")).toBe(true);
  expect((refused["lp-embeds"] ?? "").includes("a pack cannot carry an embedded ROM")).toBe(true);
  expect((refused["lp-kind"] ?? "").includes("controllerApps")).toBe(true);
  expect((refused["lp-throws"] ?? "").includes("the half threw")).toBe(true);
  expect((refused["lp-shape"] ?? "").includes("its app half's default export is not an object")).toBe(true);
  expect((refused["lp-dependent"] ?? "").includes("requires the pack lp-throws, which is refused")).toBe(true);
  expect((refused["lp-needs-platform"] ?? "").includes("requires the platform lp-nowhere, which no loaded console provides")).toBe(true);
});

test("a console's platforms and node type are the pack's to declare", async () => {
  // A console needs its core's system role: borrow a built-in console's, on a platform of the pack's own.
  const borrowed = consolesRegistry.items()[0]!;
  const descriptor = { ...borrowed, id: "lp-console", nodeType: "rp.lp.console",
    platforms: [{ ...borrowed.platforms[0]!, id: "lp-plat", extensions: [".lpx"] }] };
  const { host, refused } = fakeHost(
    [pack("lp-console-bad", { halves: ["consoles"], provides: { platforms: [], nodeTypes: [], roles: [] } }),
     pack("lp-console", { halves: ["consoles"],
       provides: { platforms: ["lp-plat"], nodeTypes: ["rp.lp.console"], roles: [] } })],
    { "lp-console-bad": { consoles: { consoles: [descriptor] } }, "lp-console": { consoles: { consoles: [descriptor] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-console-bad"] ?? "").includes("the platform lp-plat, which its pack.json's provides.platforms does " +
    "not declare")).toBe(true);
  expect(refused["lp-console"]).toBe(undefined);
  expect(consolesRegistry.items().some((d) => d.id === "lp-console")).toBe(true);
});

test("a context's builders decide which halves it loads", async () => {
  const only: Partial<Record<HalfKind, HalfBuilder>> = { consoles: APP_BUILDERS.consoles };
  const { host, imported } = fakeHost([pack("lp-app-only", { halves: ["app"] })], { "lp-app-only": { app: {} } });
  await loadPacks(host, only);
  expect(imported).toEqual([]);
});

function consoleOf(id: string, platform: string, nodeType: string) {
  const borrowed = consolesRegistry.items()[0]!;
  return { ...borrowed, id, nodeType, platforms: [{ ...borrowed.platforms[0]!, id: platform, extensions: [".lpy"] }] };
}

test("a console's node type must be declared, and a declared platform must be contributed", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-nt", { halves: ["consoles"], provides: { platforms: ["lp-plat2"], nodeTypes: [], roles: [] } }),
     pack("lp-extra", { halves: ["consoles"], provides: { platforms: ["lp-extra", "lp-plat3"], nodeTypes: ["rp.lp.c3"],
       roles: [] } })],
    { "lp-nt": { consoles: { consoles: [consoleOf("lp-c2", "lp-plat2", "rp.lp.console2")] } },
      "lp-extra": { consoles: { consoles: [consoleOf("lp-c3", "lp-plat3", "rp.lp.c3")] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-nt"] ?? "").includes("names the node type rp.lp.console2, which its pack.json's " +
    "provides.nodeTypes does not declare")).toBe(true);
  expect((refused["lp-extra"] ?? "").includes("provides.platforms names lp-extra, which no half of the pack " +
    "contributes")).toBe(true);
});

test("provides with no half to contribute them is refused", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-noc", { halves: ["app"], provides: { platforms: ["x"], nodeTypes: [], roles: [] } }),
     pack("lp-noa", { halves: ["consoles"], provides: { platforms: [], nodeTypes: [], roles: ["y"] } })],
    { "lp-noc": { app: {} }, "lp-noa": { consoles: {} } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-noc"] ?? "").includes("provides.platforms names x")).toBe(true);
  expect((refused["lp-noa"] ?? "").includes("provides.roles names y")).toBe(true);
});

test("a required platform may come from the pack's own console or an earlier staged pack", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-self", { halves: ["consoles"], requires: { packs: [], platforms: ["lp-p4"] },
       provides: { platforms: ["lp-p4"], nodeTypes: ["rp.lp.c4"], roles: [] } }),
     pack("lp-later", { halves: ["app"], requires: { packs: ["lp-self"], platforms: ["lp-p4"] } })],
    { "lp-self": { consoles: { consoles: [consoleOf("lp-c4", "lp-p4", "rp.lp.c4")] } }, "lp-later": { app: {} } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(packStatus().loaded["lp-later"]).toEqual(["app"]);
});

test("an app-only pack requiring a missing platform is refused", async () => {
  const { host, refused } = fakeHost(
    [pack("lp-app-req", { requires: { packs: [], platforms: ["nowhere"] } })], { "lp-app-req": { app: {} } });
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-app-req"] ?? "").includes("requires the platform nowhere")).toBe(true);
});

test("a consoles half's real carts land in the real cart registry", async () => {
  const cartNode = "rp.lp.cart5";
  const { host, refused } = fakeHost(
    [pack("lp-cart", { halves: ["consoles"], provides: { platforms: ["lp-p5"], nodeTypes: ["rp.lp.c5"], roles: [] } })],
    { "lp-cart": { consoles: { consoles: [consoleOf("lp-c5", "lp-p5", "rp.lp.c5")],
      realCarts: [{ platform: "lp-p5", cartNode }] } } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(realCartsRegistry.items().some((c) => c.cartNode === cartNode)).toBe(true);
});

test("an app half's unknown member is refused by name", async () => {
  const { host, refused } = fakeHost([pack("lp-typo")], { "lp-typo": { app: { rolez: [] } } });
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-typo"] ?? "").includes("exports rolez")).toBe(true);
});

test("a loader failure resolves, and a throwing refuse is survived", async () => {
  const bad: PackHost = { list: () => { throw new Error("no list"); }, importHalf: async () => ({}), refuse: () => {} };
  await loadPacks(bad, APP_BUILDERS);
  const { host } = fakeHost([pack("lp-refuse-throws", { requires: { packs: [], platforms: ["nowhere2"] } })],
    { "lp-refuse-throws": { app: {} } });
  await loadPacks({ ...host, refuse: () => { throw new Error("cannot record"); } }, APP_BUILDERS);
  expect((packStatus().refused["lp-refuse-throws"] ?? "").includes("nowhere2")).toBe(true);
});

test("overlapping loads of the same pack commit it once", async () => {
  const before = contributionsVersion();
  const { host, refused } = fakeHost(
    [pack("lp-race", { provides: { platforms: [], nodeTypes: [], roles: ["lp-race-role"] } })],
    { "lp-race": { app: { roles: [role("lp-race-role")] } } });
  await Promise.all([loadPacks(host, APP_BUILDERS), loadPacks(host, APP_BUILDERS)]);
  expect(refused).toEqual({});
  expect(contributionsVersion()).toBe(before + 1);
});

test("the fallback refuses a pack whose required platform's provider was refused there", async () => {
  const builtIn = rolesRegistry.items()[0]!.kind;
  // The provider's console commits, its role clashes: the batch fails, then the provider is refused in the fallback.
  const { host, refused } = fakeHost(
    [pack("lp-prov", { halves: ["app", "consoles"],
       provides: { platforms: ["lp-p6"], nodeTypes: ["rp.lp.c6"], roles: [builtIn] } }),
     pack("lp-user", { halves: ["app"], requires: { packs: [], platforms: ["lp-p6"] } })],
    { "lp-prov": { app: { roles: [role(builtIn)] }, consoles: { consoles: [consoleOf("lp-c6", "lp-p6", "rp.lp.c6")] } },
      "lp-user": { app: {} } },
  );
  await loadPacks(host, APP_BUILDERS);
  expect((refused["lp-prov"] ?? "").includes("twice")).toBe(true);
  expect((refused["lp-user"] ?? "").includes("requires the platform lp-p6")).toBe(true);
});

test("a half that never settles is refused by name within the timeout, and the load still resolves", async () => {
  const { host, refused, skipLater } = fakeHost([pack("lp-stuck")], {});
  host.importHalf = () => new Promise(() => {});
  const was = setLoadBudgetMs(20);
  try {
    await loadPacks(host, APP_BUILDERS);
  } finally {
    setLoadBudgetMs(was);
  }
  expect(refused["lp-stuck"]).toBe("the packs' load ran past its 0.02 s budget in its app half");
  expect(skipLater["lp-stuck"]).toBe(true);
});

test("one slow pack consumes the budget and a fast pack after it still loads", async () => {
  const { host, refused, skipLater } = fakeHost(
    [pack("lp-slow"), pack("lp-slow2"),
     pack("lp-fast", { provides: { platforms: [], nodeTypes: [], roles: ["lp-fast-role"] } })],
    { "lp-fast": { app: { roles: [role("lp-fast-role")] } } },
  );
  const fast = host.importHalf;
  host.importHalf = (id, half) => (id.startsWith("lp-slow") ? new Promise(() => {}) : fast(id, half));
  const was = setLoadBudgetMs(200);
  const start = Date.now();
  try {
    await loadPacks(host, APP_BUILDERS);
  } finally {
    setLoadBudgetMs(was);
  }
  // Two stuck packs share the one budget: a per-half timer would take twice as long.
  expect(Date.now() - start < 380).toBe(true);
  expect(refused["lp-slow"]).toBe("the packs' load ran past its 0.2 s budget in its app half");
  expect(refused["lp-slow2"]).toBe("the packs' load ran past its 0.2 s budget in its app half");
  expect(skipLater["lp-slow"]).toBe(true);
  expect(refused["lp-fast"]).toBe(undefined);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-fast-role")).toBe(true);
});

test("a runtime with no timers loads its packs with no budget", async () => {
  const g = globalThis as unknown as { setTimeout?: unknown; clearTimeout?: unknown };
  const saved = { setTimeout: g.setTimeout, clearTimeout: g.clearTimeout };
  g.setTimeout = undefined;
  g.clearTimeout = undefined;
  try {
    const { host, refused } = fakeHost(
      [pack("lp-notimers", { provides: { platforms: [], nodeTypes: [], roles: ["lp-notimers-role"] } })],
      { "lp-notimers": { app: { roles: [role("lp-notimers-role")] } } },
    );
    await loadPacks(host, APP_BUILDERS);
    expect(refused).toEqual({});
    expect(packStatus().loaded["lp-notimers"]).toEqual(["app"]);
  } finally {
    g.setTimeout = saved.setTimeout;
    g.clearTimeout = saved.clearTimeout;
  }
});

/** A descriptor with these ports, as the host's trial hands one over. */
const descriptor = (id: string, ports: PortDesc[]): TypeDescriptor =>
  ({ id, version: "1.0.0", name: id, category: "script", requires: { heap: false, threads: false }, ports, params: [],
     settings: { type: "object" } });

const ECHO_PORTS: PortDesc[] = [{ id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }];

/** A pack with an app half (one role, one project section), a protocol half placing the role as an echo node, and a
 *  placer half. */
function protocolPack(id: string, over: { ports?: PortDesc[];
                                          node?: Record<string, unknown>; roles?: Record<string, string> } = {}) {
  const node = over.node ?? { type: `rp.${id}.echo`, midiIn: "in", writes: [], midiOut: true };
  const type = node.type as string;
  const info = pack(id, {
    halves: ["app", "protocol", "placer"],
    provides: { platforms: [], nodeTypes: [type], roles: [id] },
    protocol: {
      nodes: [node],
      roles: over.roles ?? { [id]: type },
      descriptors: { [type]: descriptor(type, over.ports ?? ECHO_PORTS) },
    },
  });
  const modules = {
    [id]: {
      app: { roles: [role(id)], projectSections: [{ key: `${id}-model`, schema: { parse: () => ({}) }, defaults: () => ({}) }] },
      placer: { key: `${id}-model`, place: () => ({}) },
    },
  };
  return { info, modules, type };
}

test("a pack's protocol data and placer commit with it, and buildGraph places its role's node", async () => {
  const { info, modules, type } = protocolPack("lp-proto");
  const { host, refused, imported } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(packStatus().loaded["lp-proto"]).toEqual(["app", "protocol", "placer"]);
  // The protocol half is data: nothing imported it.
  expect(imported.includes("lp-proto/protocol")).toBe(false);
  expect(protocolNodeOf("lp-proto")).toBe(type);
  expect(placersRegistry.items().some((p) => p.key === "lp-proto-model")).toBe(true);
  const gb: GraphSystem = {
    id: 1, platform: "gb", type: "rp.sameboy.gb", linkGroup: 0, exportMode: 0, resources: { rom: 1 }, romName: "rom1",
    pipeline: protocolPipeline([{ kind: "lp-proto", config: {} }]),
  };
  const { document } = buildGraph({ systems: [gb], routing: "stereo", shape: { kind: "multi", channels: 2 },
    midiRouting: "sendToAll", models: {} });
  expect(document.nodes.find((n) => n.id === "sys-1/lp-proto")?.type).toBe(type);
  expect(document.wires.filter((w) => w.to === "sys-1/lp-proto.in")).toEqual([{ from: "routing.sys-1", to: "sys-1/lp-proto.in" }]);
  expect(document.wires.filter((w) => w.from === "sys-1/lp-proto.midiOut")).toEqual([{ from: "sys-1/lp-proto.midiOut", to: "midi-out.in1" }]);
});

test("a protocol node whose ports are not its wiring is refused, with nothing of the pack committed", async () => {
  const { info, modules } = protocolPack("lp-ports", { ports: [
    { id: "midi", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }] });
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-ports"]).toBe("its protocol node rp.lp-ports.echo has the ports midi in events, midiOut out " +
    "events, and its pack.json's protocol.nodes wires in in events, midiOut out events");
  expect(protocolNodeOf("lp-ports")).toBe(undefined);
  expect(placersRegistry.items().some((p) => p.key === "lp-ports-model")).toBe(false);
  expect(rolesRegistry.items().some((r) => r.kind === "lp-ports")).toBe(false);
  expect(projectSectionsRegistry.items().some((x) => x.key === "lp-ports-model")).toBe(false);
});

test("a protocol node another package declares, or one with a label, is refused", async () => {
  // A node with mGB's own type and ports, so its ports pass and the table's check is what refuses it.
  const dup = protocolPack("lp-dup", { node: { type: "rp.mgb.midi", midiIn: "midi", writes: ["serialIn"] }, roles: {},
    ports: [{ id: "midi", dir: "in", kind: "events" }, { id: "serialIn", dir: "out", kind: "bytes" }] });
  const labelled = protocolPack("lp-label", { node: { type: "rp.lp-label.echo", label: 30, midiIn: "in", writes: [], midiOut: true } });
  const { host, refused } = fakeHost([dup.info, labelled.info], { ...dup.modules, ...labelled.modules });
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-dup"]).toBe("two packages declare the script node rp.mgb.midi: mgb and lp-dup");
  expect(refused["lp-label"]).toBe('lp-label: the node rp.lp-label.echo has a "label": a span label is the host\'s ' +
    "to give, at load");
  for (const id of ["lp-dup", "lp-label"]) {
    expect(rolesRegistry.items().some((r) => r.kind === id)).toBe(false);
    expect(projectSectionsRegistry.items().some((x) => x.key === `${id}-model`)).toBe(false);
    expect(placersRegistry.items().some((x) => x.key === `${id}-model`)).toBe(false);
  }
});

test("a placer half that is not a placer is refused", async () => {
  const { info, modules } = protocolPack("lp-noplacer");
  (modules["lp-noplacer"] as Record<string, unknown>).placer = { key: "lp-noplacer-model" };
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-noplacer"]).toBe("its placer half's default export is not a graph placer (a key and a place " +
    "function)");
});

test("a protocol node whose descriptor lists no ports is refused by name", async () => {
  const { info, modules } = protocolPack("lp-noports");
  (info.protocol!.descriptors as Record<string, unknown>)["rp.lp-noports.echo"] = { id: "rp.lp-noports.echo" };
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-noports"]).toBe("its protocol node rp.lp-noports.echo's descriptor has no ports list");
});

test("a placer for a model its app half declares no project section for is refused", async () => {
  const { info, modules } = protocolPack("lp-nosection");
  (modules["lp-nosection"] as { app: { projectSections: unknown[] } }).app.projectSections = [];
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-nosection"]).toBe("its placer half places the model lp-nosection-model, which its app half " +
    "declares no project section for");
  expect(placersRegistry.items().some((x) => x.key === "lp-nosection-model")).toBe(false);
});

// A pack contributing one controller app: its app half's control half, and its manifest's kinds entry.
function kindPack(id: string, over: { appIds?: string[]; kindIds?: string[]; data?: Record<string, unknown>;
                                      kind?: string } = {}) {
  const kind = over.kind ?? "controllerApps";
  const appIds = over.appIds ?? [`${id}-app`];
  const kindIds = over.kindIds ?? [`${id}-app`];
  const info = pack(id, {
    halves: ["app", "kinds"],
    kinds: { [kind]: kindIds.map((i) => ({ id: i, ...(over.data ?? {}) })) },
  });
  const modules = {
    [id]: { app: { [kind]: appIds.map((i) => ({ id: i, label: `App ${i}`, config: { parse: () => ({}) } })) } },
  };
  return { info, modules };
}

test("a pack's controller app reaches the menu's table, its data the planner's, and nothing imports the kinds half", async () => {
  const { info, modules } = kindPack("lk-a", { data: { syncOverride: { node: "rp.lsdj.sync", role: "lsdj-sync",
    mode: "midiMap" } } });
  const { host, refused, imported } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(packStatus().loaded["lk-a"]).toEqual(["app", "kinds"]);
  expect(imported.includes("lk-a/kinds")).toBe(false);
  expect(controllerApp("lk-a-app")?.label).toBe("App lk-a-app");
  expect(kindData("controllerApps")["lk-a-app"]).toEqual({ syncOverride: { node: "rp.lsdj.sync", role: "lsdj-sync",
    mode: "midiMap" } });
  // The kind's add merged the data into the menu's copy: the override the menu reads for the app.
  const systems = [{ id: 1, pipeline: [{ node: "rp.lsdj.sync" }] }];
  const ctl = { app: "lk-a-app", target: "system", systemId: 1, cartSync: true } as never;
  expect(controllerSyncOverride(systems, ctl)?.mode).toBe("midiMap");
});

test("a pack's kind items are held to the kind's rules", async () => {
  const unpaired = kindPack("lk-b", { kindIds: ["lk-b-other"] });
  const taken = kindPack("lk-c", { appIds: ["lsdj-midimap"], kindIds: ["lsdj-midimap"] });
  const badKey = kindPack("lk-d", { data: { colour: "red" } });
  const undeclared = kindPack("lk-e", { kind: "nope" });
  const badRule = kindPack("lk-f", { data: { syncOverride: { node: "rp.nobody.sync", role: "x", mode: "m" } } });
  const all = [unpaired, taken, badKey, undeclared, badRule];
  const { host, refused } = fakeHost(all.map((p) => p.info), Object.assign({}, ...all.map((p) => p.modules)));
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lk-b"]).toBe("its app half contributes the controllerApps item lk-b-app, which its pack.json's " +
    "kinds member does not declare");
  expect(refused["lk-c"]).toBe("two packages contribute the controllerApps item lsdj-midimap: lsdj-launchpad and lk-c");
  expect(refused["lk-d"]).toBe("its controllerApps item lk-d-app has \"colour\", which nothing reads (the kind's data " +
    "keys are syncOverride)");
  expect(refused["lk-e"]).toBe("it contributes to nope, a contribution kind no loaded package declares");
  expect((refused["lk-f"] ?? "").includes("names \"rp.nobody.sync\"")).toBe(true);
  for (const id of ["lk-b-app", "lk-d-app", "lk-e-app", "lk-f-app"]) expect(controllerApp(id)).toBe(undefined);
});
