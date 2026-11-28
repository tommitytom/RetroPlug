// The live project re-resolves when a pack arrives (docs/downloadable-packs-m3-design.md section 6): once the load
// that brought the pack has committed and marked its halves loaded, each placeholder the pack now provides is swapped
// into the same id and slot with its ROM and entries, the project stays as clean as it was, and one that still fails
// has its needs read again. Only the store its host registered (followPacks) re-resolves. Every console, role and id
// here is this file's own, and each commit is permanent in this file's registries, so each case commits its own.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { RoleType } from "@retroplug/core/systemRoles";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { appAdditions } from "../../src/contributions/app";
import { APP_BUILDERS, loadPacks, packStatus } from "../../src/packs/loadPacks";
import type { PackHost, PackInfo } from "../../src/packs/packHost";
import { protocolNodesRegistry, roleNodesRegistry } from "../../src/contributions/protocolRegistries";
import { LsdjSyncMode } from "@retroplug/lsdj/enums";
import { gbRomBattery, lsdjRom } from "../systems/fixtures";

const enc = new TextEncoder();

/** Let the re-resolve a commit scheduled run: it is a microtask queued at the commit, so it runs before this. */
const settled = (): Promise<void> => Promise.resolve().then(() => {});

function consoleOf(platform: string, magic: number[]): ConsoleDescriptor {
  return {
    id: `${platform}-console`,
    core: `${platform}-core`,
    nodeType: `rp.${platform}.system`,
    platforms: [{ id: platform, label: platform.toUpperCase(), extensions: [`.${platform}`] }],
    detect: {
      prefixBytes: 4,
      rank: 950,
      run: (b: Uint8Array) => (magic.every((m, i) => b[i] === m) ? platform : null),
    },
  } as unknown as ConsoleDescriptor;
}

function role(kind: string, category: "system" | "feature"): RoleType {
  return { kind, category, schema: { parse: (raw: unknown) => ({ ...(raw as object) }) } } as unknown as RoleType;
}

/** Commit a console for `platform` and its core's system role, as a pack's consoles and app halves do. */
function arrive(platform: string, magic: number[]): void {
  commitContributions([
    { registry: consolesRegistry, pkg: `${platform}-pack`, items: [consoleOf(platform, magic)] },
    ...appAdditions(`${platform}-pack`, { roles: [role(`${platform}-core`, "system")] }),
  ]);
}

function heldSystem(platform: string, extraRoles: { kind: string; config: object }[] = []) {
  return {
    platform,
    core: `${platform}-core`,
    romPath: `/roms/song.${platform}`,
    roles: [{ kind: `${platform}-core`, config: { level: 2 } }, ...extraRoles],
    provenance: { platform: { id: `${platform}-pack`, name: `${platform} pack`, version: "1.0.0" } },
  };
}

/** A project holding a placeholder for `platform` (slot 0, with a savestate entry) and a live Game Boy (slot 1). */
function loaded(
  platform: string,
  magic: number[],
  opts: { follow?: boolean; extraRoles?: { kind: string; config: object }[] } = {},
) {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  be.seed(`/roms/song.${platform}`, new Uint8Array([...magic, 0, 0, 0, 0]));
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  if (opts.follow !== false) project.followPacks();
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [heldSystem(platform, opts.extraRoles), { platform: "gb", core: "sameboy", romPath: "/roms/a.gb" }],
  };
  const zip = be.zip([
    { name: "project.json", bytes: enc.encode(JSON.stringify(doc)) },
    { name: "systems/0/state", bytes: new Uint8Array([6, 5, 4]) },
  ])!;
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  return { be, project };
}

test("a commit that provides a placeholder's platform swaps it in at its id and slot, with its state, and the " +
  "project stays clean", async () => {
  const { be, project } = loaded("rrone", [0x52, 0x52, 0x31, 0x21]);
  const [held, gb] = project.systems.slotView();
  expect(held.placeholder !== undefined).toBe(true);
  expect(project.isDirty()).toBe(false);
  arrive("rrone", [0x52, 0x52, 0x31, 0x21]);
  // Scheduled, not run inside the commit: the loader that committed has not finished its pass yet.
  expect(project.systems.slotView()[0].placeholder !== undefined).toBe(true);
  await settled();
  const [now, still] = project.systems.slotView();
  expect(now.placeholder).toBe(undefined);
  expect(now.id).toBe(held.id);
  expect(now.platform).toBe("rrone");
  expect(now.romPath).toBe("/roms/song.rrone");
  expect(still.id).toBe(gb.id);
  expect(be.bootOf(now.id)?.stateBytes).toEqual(new Uint8Array([6, 5, 4]));
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${now.id}`)).toBe(true);
  expect(project.isDirty()).toBe(false);
  expect(project.missingPacks()).toEqual([]);
});

test("a commit that provides one of two needs leaves the placeholder with only the other, read again", async () => {
  const { project } = loaded("rrtwo", [0x52, 0x52, 0x32, 0x21], { extraRoles: [{ kind: "rrtwo-marker", config: {} }] });
  expect(project.systems.slotView()[0].placeholder?.needs.map((n) => n.key)).toEqual(["rrtwo", "rrtwo-marker"]);
  commitContributions(appAdditions("rrtwo-markers", { roles: [role("rrtwo-marker", "feature")] }));
  await settled();
  const [held] = project.systems.slotView();
  expect(held.placeholder?.needs.map((n) => n.key)).toEqual(["rrtwo"]);
  expect(project.isDirty()).toBe(false);
});

test("only the registered store re-resolves; one nobody registered keeps its placeholder until it loads again",
  async () => {
  const unregistered = loaded("rrthree", [0x52, 0x52, 0x33, 0x21], { follow: false });
  const registered = loaded("rrfour", [0x52, 0x52, 0x34, 0x21]);
  arrive("rrthree", [0x52, 0x52, 0x33, 0x21]);
  await settled();
  expect(unregistered.project.systems.slotView()[0].placeholder !== undefined).toBe(true);
  arrive("rrfour", [0x52, 0x52, 0x34, 0x21]);
  await settled();
  expect(registered.project.systems.slotView()[0].placeholder).toBe(undefined);
});

test("one load whose app half commits roles and whose consoles half commits a platform re-resolves once, after both",
  async () => {
  // The real ordering: loadPacks commits the pack's consoles and app halves in one commit, marks both halves loaded and
  // accepts its kind items, and only then does the live project re-resolve, once, seeing the platform and the roles.
  const magic = [0x52, 0x52, 0x35, 0x21];
  const { project } = loaded("rrorder", magic, { extraRoles: [{ kind: "rrorder-marker", config: {} }] });
  const seen: string[][] = [];
  const reresolve = project.reresolve.bind(project);
  project.reresolve = () => {
    seen.push(packStatus().loaded["rrorder-pack"] ?? []);
    return reresolve();
  };
  const pack: PackInfo = {
    id: "rrorder-pack",
    version: "1.0.0",
    halves: ["app", "consoles"],
    provides: { platforms: ["rrorder"], nodeTypes: ["rp.rrorder.system"], roles: ["rrorder-core", "rrorder-marker"] },
    requires: { packs: [], platforms: [] },
  };
  const modules: Record<string, unknown> = {
    app: { roles: [role("rrorder-core", "system"), role("rrorder-marker", "feature")] },
    consoles: { consoles: [consoleOf("rrorder", magic)] },
  };
  const host: PackHost = {
    list: () => [pack],
    importHalf: async (_id, half) => ({ default: modules[half] }),
    refuse: (id, reason) => {
      throw new Error(`${id} was refused: ${reason}`);
    },
  };
  await loadPacks(host, APP_BUILDERS);
  expect(seen).toEqual([["app", "consoles"]]);
  const [now] = project.systems.slotView();
  expect(now.placeholder).toBe(undefined);
  expect(now.roles.map((r) => r.kind)).toEqual(["rrorder-core", "rrorder-marker"]);
  expect(now.missingRoles).toBe(undefined);
  expect(project.isDirty()).toBe(false);
});

test("a dirty project stays dirty through the re-resolve", async () => {
  const { project } = loaded("rrsix", [0x52, 0x52, 0x36, 0x21]);
  project.systems.setFocus(project.systems.slotView()[1].id);
  project.setName("edited");
  expect(project.isDirty()).toBe(true);
  arrive("rrsix", [0x52, 0x52, 0x36, 0x21]);
  await settled();
  expect(project.systems.slotView()[0].placeholder).toBe(undefined);
  expect(project.isDirty()).toBe(true);
});

test("a later followPacks takes over: the first store is no longer re-resolved", async () => {
  const first = loaded("rrseven", [0x52, 0x52, 0x37, 0x21]);
  const second = loaded("rrseven", [0x52, 0x52, 0x37, 0x21]);
  arrive("rrseven", [0x52, 0x52, 0x37, 0x21]);
  await settled();
  expect(second.project.systems.slotView()[0].placeholder).toBe(undefined);
  expect(first.project.systems.slotView()[0].placeholder !== undefined).toBe(true);
});

/** Make the engine refuse any document that places system node `sys-<id>` for `id` of `pick`, as activate-failed. */
function refuseSystem(be: MockBackend, pick: () => number | undefined, message: string): () => void {
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    const id = pick();
    const doc = JSON.parse(request.document) as { nodes: { id: string }[] };
    if (id !== undefined && doc.nodes.some((n) => n.id === `sys-${id}`)) {
      be.graphRequests.push(request);
      return { ok: false, errors: [{ nodeId: `sys-${id}`, port: "", code: "activate-failed", message }] };
    }
    return setGraph(request);
  };
  return () => {
    be.setGraph = setGraph;
  };
}

test("a swapped system the engine refuses goes back as its placeholder, with its JSON, entries and a reason",
  async () => {
  const { be, project } = loaded("rreight", [0x52, 0x52, 0x38, 0x21]);
  const [held] = project.systems.slotView();
  const heldEntry = project.systems.placeholders()[0];
  const restore = refuseSystem(be, () => held.id, "the core refused its settings");
  arrive("rreight", [0x52, 0x52, 0x38, 0x21]);
  await settled();
  restore();
  const [now, gb] = project.systems.slotView();
  expect(now.id).toBe(held.id);
  expect(now.placeholder?.needs[0].reason).toBe("the core refused its settings");
  expect(project.systems.placeholders()[0].raw).toBe(heldEntry.raw);
  expect(project.systems.placeholders()[0].blobs.get("state")).toEqual(new Uint8Array([6, 5, 4]));
  expect(gb.placeholder).toBe(undefined);
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${gb.id}`)).toBe(true);
  expect(project.isDirty()).toBe(false);
});

/** A project with one live Game Boy that stores a role of `kind`, which nothing registers yet. */
function liveWithRole(kind: string, notified?: { n: number }) {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRomBattery());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  project.followPacks();
  if (notified) project.addOnSystemsChange(() => notified.n++);
  const doc = {
    schemaVersion: "4",
    settings: {},
    systems: [{ platform: "gb", core: "sameboy", romPath: "/roms/a.gb", roles: [{ kind, config: { level: 1 } }] }],
  };
  const zip = be.zip([{ name: "project.json", bytes: enc.encode(JSON.stringify(doc)) }])!;
  expect(project.loadBytes(zip, "").kind).toBe("loaded");
  return { be, project };
}

test("a role kind that arrives with a construct hook rebuilds its system, carrying the battery, and stays clean",
  async () => {
  const { be, project } = liveWithRole("rrhook");
  const [before] = project.systems.systems();
  expect(before.builtWithout).toEqual(["rrhook"]);
  const battery = new Uint8Array([9, 8, 7, 6]);
  be.setSram(before.id, battery);
  const constructed: string[] = [];
  const typed = { ...role("rrhook", "feature"), onConstruct: (spec: object) => (constructed.push("rrhook"), spec) };
  const notified = { n: 0 };
  project.addOnSystemsChange(() => notified.n++);
  commitContributions(appAdditions("rrhook-pack", { roles: [typed as unknown as RoleType] }));
  await settled();
  const [after] = project.systems.systems();
  expect(constructed).toEqual(["rrhook"]);
  expect(after.id === before.id).toBe(false);
  expect(after.builtWithout).toBe(undefined);
  expect(be.bootOf(after.id)?.sramBytes).toEqual(battery);
  expect(notified.n).toBe(1);
  expect(project.isDirty()).toBe(false);
});

test("a role kind that arrives without a hook keeps its system, and its stored config is parsed for its node",
  async () => {
  const { project } = liveWithRole("rrplain");
  const [before] = project.systems.systems();
  const parsing = {
    kind: "rrplain",
    category: "feature",
    schema: { parse: (raw: unknown) => ({ ...(raw as object), parsed: 1 }) },
  };
  commitContributions([
    ...appAdditions("rrplain-pack", { roles: [parsing as unknown as RoleType] }),
    {
      registry: protocolNodesRegistry,
      pkg: "rrplain-pack",
      items: [{ type: "rp.rrplain.node", midiIn: "midi", writes: ["serialIn"] }],
    },
    { registry: roleNodesRegistry, pkg: "rrplain-pack", items: [{ kind: "rrplain", node: "rp.rrplain.node" }] },
  ]);
  await settled();
  const [after] = project.systems.systems();
  expect(after.id).toBe(before.id);
  expect(after.builtWithout).toBe(undefined);
  expect(after.roles.find((r) => r.kind === "rrplain")?.config).toEqual({ level: 1, parsed: 1 });
  expect(project.isDirty()).toBe(false);
});

test("a rebuild the engine refuses rolls back: the running system stays, still marked", async () => {
  const { be, project } = liveWithRole("rrrefused");
  const [before] = project.systems.systems();
  const restore = refuseSystem(be, () => project.systems.systems().find((e) => e.id !== before.id)?.id, "refused");
  const typed = { ...role("rrrefused", "feature"), onConstruct: (spec: object) => spec };
  commitContributions(appAdditions("rrrefused-pack", { roles: [typed as unknown as RoleType] }));
  await settled();
  restore();
  const [after] = project.systems.systems();
  expect(after.id).toBe(before.id);
  expect(after.builtWithout).toEqual(["rrrefused"]);
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${before.id}`)).toBe(true);
  expect(project.isDirty()).toBe(false);
});

test("a role left out because the engine lacked its node is placed after a commit, once the engine has it",
  async () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/lsdj.gb", lsdjRom());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  project.followPacks();
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    const doc = JSON.parse(request.document) as { nodes: { id: string; type: string }[] };
    const node = doc.nodes.find((n) => /^sys-\d+\/lsdj-sync$/.test(n.id));
    if (!node) return setGraph(request);
    be.graphRequests.push(request);
    return { ok: false, errors: [{ nodeId: node.id, port: "", code: "unknown-type", message: "no such node" }] };
  };
  const id = project.systems.addSystem("/roms/lsdj.gb")!;
  project.systems.setRoleConfig(id, "lsdj-sync", { mode: LsdjSyncMode.MidiSync });
  const placedNode = (): boolean => be.lastDocument()!.nodes.some((n) => /^sys-\d+\/lsdj-sync$/.test(n.id));
  expect(placedNode()).toBe(false);
  expect(project.systems.view()[0].missingRoles?.map((n) => n.key)).toEqual(["lsdj-sync"]);
  be.setGraph = setGraph; // the engine has the type now (a pack loaded)
  arrive("rrnine", [0x52, 0x52, 0x39, 0x21]);
  await settled();
  expect(placedNode()).toBe(true);
  expect(project.systems.view()[0].missingRoles).toBe(undefined);
});

/** Make the engine refuse any document placing a node whose id matches `pattern`, naming that node. */
function refuseNode(be: MockBackend, pattern: RegExp, code: string, message: string): () => void {
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    const doc = JSON.parse(request.document) as { nodes: { id: string }[] };
    const node = doc.nodes.find((n) => pattern.test(n.id));
    if (!node) return setGraph(request);
    be.graphRequests.push(request);
    return { ok: false, errors: [{ nodeId: node.id, port: "", code, message }] };
  };
  return () => {
    be.setGraph = setGraph;
  };
}

/** The additions of a role `kind` with a protocol node of its own. */
function withNode(kind: string, over: object = {}) {
  const typed = { ...role(kind, "feature"), ...over } as unknown as RoleType;
  return [
    ...appAdditions(`${kind}-pack`, { roles: [typed] }),
    {
      registry: protocolNodesRegistry,
      pkg: `${kind}-pack`,
      items: [{ type: `rp.${kind}.node`, midiIn: "midi", writes: ["serialIn"] }],
    },
    { registry: roleNodesRegistry, pkg: `${kind}-pack`, items: [{ kind, node: `rp.${kind}.node` }] },
  ];
}

test("a late kind with a hook and a node the engine refuses leaves the running system alone", async () => {
  const { be, project } = liveWithRole("rrhooknode");
  const [before] = project.systems.systems();
  const restore = refuseNode(be, /^sys-\d+\/rrhooknode/, "activate-failed", "the node refused its settings");
  commitContributions(withNode("rrhooknode", { onConstruct: (spec: object) => spec }));
  await settled();
  restore();
  expect(project.systems.systems().map((e) => e.id)).toEqual([before.id]);
  expect(project.systems.systems()[0].builtWithout).toEqual(["rrhooknode"]);
  const nodes = be.lastDocument()!.nodes.map((n) => n.id);
  expect(nodes.includes(`sys-${before.id}`)).toBe(true);
  expect(nodes.some((id) => id.includes("rrhooknode"))).toBe(false);
  expect(project.isDirty()).toBe(false);
});

test("a late hookless kind whose node the engine refuses is left out, and its running system stays", async () => {
  const { be, project } = liveWithRole("rrnohook");
  const [before] = project.systems.systems();
  const restore = refuseNode(be, /^sys-\d+\/rrnohook/, "activate-failed", "the node refused its settings");
  commitContributions(withNode("rrnohook"));
  await settled();
  restore();
  const [after] = project.systems.systems();
  expect(after.id).toBe(before.id);
  expect(project.systems.view()[0].missingRoles?.map((n) => [n.key, n.reason])).toEqual([
    ["rrnohook", "the node refused its settings"],
  ]);
  expect(be.lastDocument()!.nodes.some((n) => n.id === `sys-${before.id}`)).toBe(true);
});
