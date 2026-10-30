// A system's resources, as the store owns them (docs/modular-graph-phase-c3-design.md section 6): a boot puts its ROM
// and its seeds, the document that follows names their ids, and the engine is told to drop one only after the document
// that stops naming it has planned. A refused document puts the store's model back, gives back what the edit put and
// reports the verb's failure; a project load drops the system a refusal names and publishes the rest. Every release is
// offered on its own, so one that throws is said and the handles behind it still reach the engine. And a
// construct-time edit is a reset that carries the battery: a new id, a ROM re-read from its source, a new value in the
// document. Every rebuild reads the ROM again, so a file rebuilt on disk is what the next boot runs.
import { test, expect } from "../../testing/harness";
import { MockBackend, sramBytesFor, stateBytesFor } from "../../testing/mockBackend";
import { SystemsStore } from "../../src/systemsStore";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { buildAppRegistry } from "../../src/appHost";
import { DirectGraph } from "../../src/graph/directGraph";
import { ResourcePool, systemHandles } from "../../src/graph/systemResources";
import type { ConstructSpec, SetGraphResult } from "../../src/backend";
import { warned } from "../_warnings";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { gbRom, nesRom } from "./fixtures";

// A store with the real role registry: a system's core-config role is what carries a construct-time field.
function newStore() {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be, () => {}, buildAppRegistry());
  return { be, store };
}

// The resource and graph calls in the order the store made them: what a boot put, when the document naming it went
// out, and when the resource it replaced was dropped.
const order = (be: MockBackend): string[] =>
  be.log.filter((e) => e === "putResource" || e === "releaseResource" || e === "setGraph");

// Refuse the next `n` documents with a reason naming `nodeId`, recording the request as the mock does.
function refuse(be: MockBackend, n: number, nodeId = "", code = "activate-failed"): void {
  const setGraph = be.setGraph.bind(be);
  be.setGraph = (request) => {
    if (n-- <= 0) return setGraph(request);
    be.graphRequests.push(request);
    const result: SetGraphResult = { ok: false, errors: [{ nodeId, port: "", code, message: "refused for the test" }] };
    return result;
  };
}

// `v` as a document carries it: canonical JSON, its keys sorted.
const canon = (v: unknown): unknown => JSON.parse(canonicalJson(v));

// The ids a boot put that the engine still holds, in put order (a refusal's id 0 aside).
const heldIds = (be: MockBackend): number[] =>
  be.putResourceCalls.map((c) => c.id).filter((id) => id > 0 && be.resource(id) !== undefined);

test("add: puts the ROM and the battery seed, then publishes the document that names them", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph"]);
  // The ROM is the file the engine reads itself; the battery is the .sav, also by path (TS never holds its bytes).
  expect(be.putResourceCalls.map((c) => c.spec)).toEqual([
    { type: "rom", path: "/roms/a.gb" },
    { type: "sram", path: "/roms/a.sav" },
  ]);
  expect(canon(be.bootOf(id)!.settings)).toEqual(canon({
    system: id,
    rom: "res:1",
    sram: "res:2",
    romName: "a",
    model: "cgbC",
    highpass: "accurate",
    fastBoot: true,
    colorCorrection: "disabled",
    dmgPalette: "grey",
    lightTemperature: 0,
    serialOutCapture: "off",
  }));
});

test("remove: drops the document first, then gives every resource of the system back", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const held = store.systems()[0].resources;
  be.log.length = 0;
  expect(store.removeSystem(id)).toBe(true);
  expect(order(be)).toEqual(["setGraph", "releaseResource", "releaseResource"]);
  expect(be.releaseResourceCalls).toEqual([held.rom, held.sram]);
  expect(heldIds(be)).toEqual([]);
});

test("reset: re-reads the ROM from its source, so a file rebuilt on disk is what the cart boots", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0].resources;
  be.log.length = 0;
  const next = store.reset(id)!;
  // The ROM is put again from its path, and the live battery as a seed; then the document; then the two handles the
  // old system held, once that document has planned.
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph", "releaseResource", "releaseResource"]);
  expect(store.systems()[0].resources.rom === before.rom).toBe(false);
  expect(be.bootOf(next)!.rom!.spec).toEqual({ type: "rom", path: "/roms/a.gb" });
  expect(be.bootOf(next)!.sramBytes).toEqual(sramBytesFor(id)); // the live battery, read back rather than re-read
  // The old system's two handles, released after the document that replaced it.
  expect(be.releaseResourceCalls).toEqual([before.rom, before.sram]);
  expect(be.resource(before.rom)).toBe(undefined);
});

test("reload: re-reads the ROM from disk, so its resource is put again rather than shared", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0].resources;
  be.log.length = 0;
  const next = store.reloadSystem(id)!;
  // The ROM and the live battery as a seed; then the document; then the two handles the old system held.
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph", "releaseResource"]);
  expect(store.systems()[0].resources.rom === before.rom).toBe(false);
  expect(be.releaseResourceCalls).toEqual([before.rom]); // the old system held no seed: no .sav on disk
  expect(be.bootOf(next)!.rom!.spec).toEqual({ type: "rom", path: "/roms/a.gb" });
});

test("duplicate: re-reads the ROM and seeds the clone from its live state", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb")!;
  const src = store.systems()[0].resources;
  be.log.length = 0;
  const copy = store.duplicateSystem(id)!;
  // The ROM and the state seed, then the document.
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph"]);
  const clone = store.systems().find((e) => e.id === copy)!;
  expect(clone.resources.rom === src.rom).toBe(false);
  expect(be.bootOf(copy)!.rom!.spec).toEqual({ type: "rom", path: "/roms/a.gb" });
  expect(be.bootOf(copy)!.stateBytes).toEqual(stateBytesFor(id)); // the source's published machine
  expect(be.bootOf(copy)!.state!.source).toBe("bytes");
  // Removing the source keeps the clone's own ROM: the two hold resources of their own.
  store.removeSystem(id);
  expect(be.resource(clone.resources.rom)).toBeTruthy();
  store.removeSystem(copy);
  expect(be.resource(clone.resources.rom)).toBe(undefined);
});

test("every put is balanced by a release once every system is gone", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  be.seed("/roms/b.nes", nesRom());
  const a = store.addSystem("/roms/a.gb")!;
  const b = store.addSystem("/roms/b.nes")!;
  store.duplicateSystem(a);
  store.reset(a);
  store.reloadSystem(b);
  expect(heldIds(be).length).toBeGreaterThan(0);
  for (const e of store.systems()) store.removeSystem(e.id);
  expect(store.systems().length).toBe(0);
  expect(heldIds(be)).toEqual([]); // every resource the engine was given has been given back
});

test("a refused publish puts the model back, gives back what it put, and reports the verb's failure", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0];
  const heldBefore = heldIds(be);
  const requests = be.graphRequests.length;
  refuse(be, 1);
  const failure = warned(() => store.reset(id));
  expect(failure.result).toBe(null); // the reason the refused document gave, as the verb's failure
  expect(store.systems()[0]).toEqual(before); // the list as it was: the same id, the same handles
  expect(heldIds(be)).toEqual(heldBefore); // the seed the refused edit put is gone
  // The restored model is published again, so the engine keeps running the plan it ran (and the next edit starts from
  // a graph that matches the store). The refused document and that one are the two requests.
  expect(be.graphRequests.length).toBe(requests + 2);
  expect(be.graphRequests[be.graphRequests.length - 1].slots).toEqual([id]);
  expect(failure.warnings.length).toBeGreaterThan(0); // the engine's reason, and the rollback, are said
});

test("a project load drops the system a refusal names, with a note, and publishes the rest", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  be.seed("/roms/c.gb", gbRom());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  project.systems.addSystem("/roms/a.gb");
  project.systems.addSystem("/roms/b.nes");
  project.systems.addSystem("/roms/c.gb");
  const bytes = project.exportBytes()!;

  be.seed("/proj/p.rplg.zip", bytes);
  const setGraph = be.setGraph.bind(be);
  let bad = 0;
  be.setGraph = (request) => {
    // The engine refuses the second system the load places, as a core that would not build does.
    const second = request.slots[1];
    if (!bad && second !== undefined) {
      bad = second;
      be.graphRequests.push(request);
      return { ok: false, errors: [{ nodeId: `sys-${second}`, port: "", code: "activate-failed", message: "the ROM is not a NES ROM" }] };
    }
    return setGraph(request);
  };
  const { warnings, result } = warned(() => project.load("/proj/p.rplg.zip"));
  be.setGraph = setGraph;
  expect(result).toEqual({ kind: "loaded", systems: 2 });
  const placed = be.lastDocument()!.nodes.filter((n) => n.id.startsWith("sys-")).map((n) => n.id);
  expect(placed.length).toBe(2);
  expect(placed.includes(`sys-${bad}`)).toBe(false); // the refused system is gone, the other two run
  expect(warnings.some((w) => w.includes("the engine refused it, so the project loads without it"))).toBe(true);
  // The dropped system's resources went with it: the engine holds exactly the survivors' handles, so a leaked ROM
  // of the refused system cannot pass a check that only asks after the survivors.
  const survivors = project.systems.systems().flatMap((e) => systemHandles(e.resources));
  expect(heldIds(be).sort()).toEqual([...survivors].sort());
});

test("a publish that throws puts the model back, gives back what the edit put, and sends the restored one", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0];
  const heldBefore = heldIds(be);
  const requests = be.graphRequests.length;
  const setGraph = be.setGraph.bind(be);
  let threw = false;
  be.setGraph = (request) => {
    if (!threw) {
      threw = true;
      throw new Error("the setGraph RPC died");
    }
    return setGraph(request);
  };
  expect(() => store.reset(id)).toThrow("the setGraph RPC died");
  be.setGraph = setGraph;
  // A throw is the engine's failure, not a refusal it answered, and the graph counts it as refused (the engine keeps
  // the plan it ran): the edit is put back and its puts given back, as a refused document's are.
  expect(store.systems()[0]).toEqual(before);
  expect(heldIds(be)).toEqual(heldBefore);
  // The model the graph keeps was the edit's, so the restored one is sent too: the plan the engine runs is what a
  // later shape change rebuilds. The throw is not recorded as a request, so the one new request is that resend.
  expect(be.graphRequests.length).toBe(requests + 1);
  expect(be.graphRequests[be.graphRequests.length - 1].slots).toEqual([id]);
});

test("a throwing release does not replace the publish's error, and the restored model still goes out", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0];
  const requests = be.graphRequests.length;
  const releaseResource = be.releaseResource.bind(be);
  const putsBefore = be.putResourceCalls.length;
  const givenBack: number[] = [];
  be.releaseResource = (handle: number): boolean => {
    givenBack.push(handle);
    throw new Error("the releaseResource RPC died");
  };
  const setGraph = be.setGraph.bind(be);
  let threw = false;
  be.setGraph = (request) => {
    if (!threw) {
      threw = true;
      throw new Error("the setGraph RPC died");
    }
    return setGraph(request);
  };
  const { result: failure, warnings } = warned(() => {
    try {
      store.reset(id);
      return "";
    } catch (e) {
      return String(e);
    }
  });
  be.releaseResource = releaseResource;
  be.setGraph = setGraph;
  // Both calls cross the same transport: a release that throws cannot replace the caller's own failure or stop the
  // rollback before the restored model is sent.
  expect(failure.includes("the setGraph RPC died")).toBe(true);
  // The handles given back are the edit's own puts, every one in the order it took them. A loop that stopped at the
  // first throw would leave the rest held by an engine whose document names none of them.
  const editPuts = be.putResourceCalls.slice(putsBefore).map((c) => c.id);
  expect(editPuts.length).toBe(2); // the reset put its ROM and the battery it carried
  expect(givenBack).toEqual(editPuts);
  expect(store.systems()[0]).toEqual(before);
  expect(be.graphRequests.length).toBe(requests + 1);
  expect(be.graphRequests[be.graphRequests.length - 1].slots).toEqual([id]);
  expect(warnings.some((w) => w.includes("giving back the edit's resource threw too"))).toBe(true);
});

test("a release that throws after a planned document does not take the verb's answer, and the rest go back", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  const id = store.addSystem("/roms/a.gb")!;
  const replaced = store.systems()[0].resources;
  const releaseResource = be.releaseResource.bind(be);
  const offered: number[] = [];
  be.releaseResource = (handle: number): boolean => {
    offered.push(handle);
    throw new Error("the releaseResource RPC died");
  };
  const { result, warnings } = warned(() => store.reset(id));
  be.releaseResource = releaseResource;
  // The document planned, so the edit succeeded: what a replacement handle then does to the transport is said, not
  // thrown over the verb's answer, and the handles behind the one that failed are still offered to the engine.
  expect(result === null).toBe(false);
  expect(offered).toEqual(systemHandles(replaced)); // the handles it replaced, every one, in the order it held them
  expect(store.systems()[0].id).toBe(result);
  expect(warnings.some((w) => w.includes("releasing a replaced resource threw"))).toBe(true);
});

test("a release that throws keeps its handle: the pool forgets it only once the engine gave it back", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  const pool = new ResourcePool(be);
  const rom = (pool.put({ type: "rom", path: "/roms/a.gb" }) as { id: number }).id;
  const releaseResource = be.releaseResource.bind(be);
  let unreachable = true;
  be.releaseResource = (): boolean => {
    if (unreachable) throw new Error("the releaseResource RPC died");
    return releaseResource(rom);
  };
  // A handle the engine never took back is the only way to drop that resource again, so a release that died keeps
  // the id: the caller that survives the throw can offer it once more instead of leaving the engine holding a
  // resource no handle can name.
  expect(() => pool.drop(rom)).toThrow("the releaseResource RPC died");
  expect(be.resource(rom)).toBeTruthy();
  unreachable = false;
  pool.drop(rom);
  expect(be.resource(rom)).toBe(undefined);
  be.releaseResource = releaseResource;
  pool.drop(rom); // once it is given back, a further drop is the no-op it always was
  expect(be.releaseResourceCalls).toEqual([rom]);
});

test("a construct-time edit resets the system: a new id, a re-read ROM, the battery carried, the new value", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  const before = store.systems()[0].resources;
  be.log.length = 0;
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware", fifoDepth: 4096 })).toBe(true);
  const next = store.systems()[0].id;
  expect(next === id).toBe(false); // a construct-time field is read at the build, so the node is rebuilt
  expect(store.systems().length).toBe(1);
  expect(store.focused()).toBe(next);
  // The live battery is carried: the seed is the old core's, read back rather than the .sav on disk. The ROM is read
  // again from its path, as every boot reads its own.
  expect(be.bootOf(next)!.sramBytes).toEqual(sramBytesFor(id));
  expect(be.bootOf(next)!.rom!.spec).toEqual({ type: "rom", path: "/roms/a.nes" });
  expect(store.systems()[0].resources.rom === before.rom).toBe(false);
  const settings = be.bootOf(next)!.settings;
  expect(settings.fifo).toBe("hardware");
  expect(settings.fifoDepth).toBe(4096);
  // The ROM and the carried battery, made before the document that names them, then the old system's handle once it
  // planned (it held no seed: there was no .sav on disk).
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph", "releaseResource"]);
  expect(be.releaseResourceCalls).toEqual([before.rom]);
});

test("reset of the id a construct-time edit replaced resets what it became, carrying that system's battery", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware", fifoDepth: 4096 })).toBe(true);
  const rebuilt = store.systems()[0].id;
  expect(rebuilt === id).toBe(false);
  const replacement = store.systems()[0].resources;
  // The pre-C3 contract: follow the construct-time edit with a reset of the id the caller held.
  be.log.length = 0;
  be.releaseResourceCalls.length = 0;
  const next = store.reset(id);
  expect(next === null).toBe(false);
  expect(next === rebuilt).toBe(false); // the reset builds a system of its own, under a new id
  expect(store.systems().map((e) => e.id)).toEqual([next as number]);
  expect(store.focused()).toBe(next);
  // What it reset is the system the edit built: the new value is still its node's, and the battery it boots with is
  // the replacement's live one, not the system the caller's id named before the edit.
  expect(be.bootOf(next as number)!.settings.fifo).toBe("hardware");
  expect(be.bootOf(next as number)!.settings.fifoDepth).toBe(4096);
  expect(be.bootOf(next as number)!.sramBytes).toEqual(sramBytesFor(rebuilt));
  // The replacement's own ROM and seed are the ones the document stopped naming, so they go after it planned.
  expect(order(be)).toEqual(["putResource", "putResource", "setGraph", "releaseResource", "releaseResource"]);
  expect(be.releaseResourceCalls).toEqual([replacement.rom, replacement.sram]);
});

test("reset follows a chain of rebuilds: the first id resets the live end of it", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const first = store.addSystem("/roms/a.nes")!;
  expect(store.setRoleConfig(first, "mesen", { fifo: "hardware" })).toBe(true);
  const second = store.systems()[0].id;
  expect(store.setRoleConfig(second, "mesen", { fifoDepth: 4096 })).toBe(true);
  const third = store.systems()[0].id;
  expect(third === second).toBe(false);
  const next = store.reset(first);
  expect(next === null).toBe(false);
  expect(next === third).toBe(false);
  expect(store.systems().map((e) => e.id)).toEqual([next as number]);
  // Both edits are still what its node names, and the battery carried is the live end's.
  expect(be.bootOf(next as number)!.settings.fifo).toBe("hardware");
  expect(be.bootOf(next as number)!.settings.fifoDepth).toBe(4096);
  expect(be.bootOf(next as number)!.sramBytes).toEqual(sramBytesFor(third));
});

test("the replaced-id map is capped: a very old id stops resolving, the recent ones still do", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const first = store.addSystem("/roms/a.nes")!;
  // Every construct-time edit replaces the system, so a session that does this hundreds of times must not keep every
  // id that ever named it. The cap keeps the most recent ones, which is what code written against the old contract
  // holds: it reset the id it had just edited.
  for (let i = 0; i < 200; i++) {
    const id = store.systems()[0].id;
    expect(store.setRoleConfig(id, "mesen", { fifo: "hardware", fifoDepth: 1000 + i })).toBe(true);
  }
  expect(store.systems().length).toBe(1);
  expect(store.reset(first)).toBe(null); // evicted long ago
  const recent = store.reset(first + 190);
  expect(recent === null).toBe(false);
  expect(store.systems().map((e) => e.id)).toEqual([recent as number]);
  expect(be.bootOf(recent as number)!.settings.fifoDepth).toBe(1199); // the live system's own value
  store.removeSystem(recent as number);
  expect(store.reset(first + 190)).toBe(null); // its chain ended with it
});

test("reset of a replaced id whose live system was removed answers null", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = store.systems()[0].id;
  expect(store.removeSystem(rebuilt)).toBe(true);
  expect(store.reset(id)).toBe(null);
  expect(store.reset(rebuilt)).toBe(null);
  expect(store.systems()).toEqual([]);
  expect(heldIds(be)).toEqual([]);
});

test("only reset follows a replaced id: removeSystem of one does nothing, as it always has", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = store.systems()[0].id;
  be.log.length = 0;
  expect(store.removeSystem(id)).toBe(false);
  expect(store.systems().map((e) => e.id)).toEqual([rebuilt]);
  expect(order(be)).toEqual([]); // no document, no release: the replaced id names nothing the store holds
  expect(be.resource(store.systems()[0].resources.rom)).toBeTruthy();
});

test("a construct-time edit that moves nothing rebuilds nothing, and a live knob stays a document", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  const resources = store.systems()[0].resources;
  be.log.length = 0;
  // The stored config already names the field's value (every document writes it in full), so writing it again moves
  // no rebuild key: the same system, no put, no document.
  expect(store.setRoleConfig(id, "mesen", { fifo: "instant" })).toBe(true);
  expect(order(be)).toEqual([]);
  expect(store.systems()[0].id).toBe(id);
  expect(store.systems()[0].resources).toEqual(resources);
  // A live setting is a document under the id the running node holds.
  expect(store.setRoleConfig(id, "mesen", { region: "pal" })).toBe(true); // a live setting: a document
  expect(order(be)).toEqual(["setGraph"]);
  expect(store.systems()[0].id).toBe(id);
  expect(store.systems()[0].resources).toEqual(resources);
});

test("a construct-time edit whose rebuild is refused leaves the system as it was", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  const id = store.addSystem("/roms/a.nes")!;
  const before = store.systems()[0];
  const heldBefore = heldIds(be);
  refuse(be, 1);
  expect(warned(() => store.setRoleConfig(id, "mesen", { fifo: "hardware" })).result).toBe(false);
  expect(store.systems()[0]).toEqual(before);
  expect(store.systems()[0].roles[0].config.fifo).toBe("instant"); // the edit went with the refused rebuild
  expect(heldIds(be)).toEqual(heldBefore);
});

test("a refused setting edit leaves the role as it was", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0];
  const n = be.graphRequests.length;
  refuse(be, 1);
  expect(store.setRoleConfig(id, "sameboy", { highpass: "off" })).toBe(false); // a setting: its document was refused
  expect(store.systems()[0]).toEqual(before);
  // The restored model is sent again, so the document the engine runs is the one the store holds.
  expect(be.graphRequests.length).toBe(n + 2);
  expect(be.lastBoot()!.id).toBe(id);
});

test("a sparse stored core config keeps live edits live, and only a construct-time edit rebuilds", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  // A project saved before the mesen role schema grew its construct-time fields: the stored config holds only what
  // it knew. Every document names every construct-time field (the node's default where the stored config holds none),
  // so what the running node was built from and what an edit writes are the same values.
  const id = store.adopt({
    romPath: "/roms/a.nes",
    core: "mesen",
    roles: [{ kind: "mesen", config: { region: "pal" } }],
  })!;
  const resources = store.systems()[0].resources;
  // The document the adopt published already names them, so a live edit moves no rebuild key.
  expect(be.graphNode(`sys-${id}`)?.settings?.fifo).toBe("instant");
  expect(be.graphNode(`sys-${id}`)?.settings?.n8LinkRttUs).toBe(2200);
  be.log.length = 0;
  expect(store.setRoleConfig(id, "mesen", { removeSpriteLimit: true })).toBe(true); // a live setting: a document
  expect(store.systems()[0].id).toBe(id);
  expect(store.systems()[0].resources).toEqual(resources);
  expect(order(be)).toEqual(["setGraph"]);
  expect(be.graphNode(`sys-${id}`)?.settings?.removeSpriteLimit).toBe(true);

  be.log.length = 0;
  expect(store.setRoleConfig(id, "mesen", { expansionVolume: 40 })).toBe(true); // a param: live, no document
  expect(store.systems()[0].id).toBe(id);
  expect(store.systems()[0].resources).toEqual(resources);
  expect(order(be)).toEqual([]);
  expect(
    be.paramCalls.some((c) => c.nodeId === `sys-${id}` && c.paramId === "expansionVolume" && c.value === 40),
  ).toBe(true);
  expect(be.releaseResourceCalls).toEqual([]);

  // A publish after the param-only edit names the whole config now, construct-time fields included: still the same
  // node, so the edit lands live rather than being refused.
  be.log.length = 0;
  expect(store.setRoleConfig(id, "mesen", { region: "ntsc" })).toBe(true);
  expect(store.systems()[0].id).toBe(id);
  expect(order(be)).toEqual(["setGraph"]);
  expect(be.graphNode(`sys-${id}`)?.settings?.region).toBe("ntsc");

  // A construct-time field is the one edit that rebuilds, under a new id with the battery.
  expect(store.setRoleConfig(id, "mesen", { fifo: "hardware" })).toBe(true);
  const rebuilt = store.systems()[0].id;
  expect(rebuilt === id).toBe(false);
  expect(be.graphNode(`sys-${rebuilt}`)?.settings?.fifo).toBe("hardware");
  expect(be.graphNode(`sys-${rebuilt}`)?.settings?.removeSpriteLimit).toBe(true);
});

test("a stored construct value the schema repairs takes the rebuild it needs, rather than a refused document", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.nes", nesRom());
  // The stored depth is out of the role schema's range: the node never took it, so the adopt's document names the
  // node's own default. An edit's schema parse clamps it, which is a different value from the one the live core was
  // built with, so the edit rebuilds rather than publishing a document that moves the key under a live id.
  const adopted = warned(() =>
    store.adopt({
      romPath: "/roms/a.nes",
      core: "mesen",
      roles: [{ kind: "mesen", config: { fifoDepth: 2_000_000 } }],
    }),
  );
  const id = adopted.result!;
  expect(be.graphNode(`sys-${id}`)?.settings?.fifoDepth).toBe(0);
  const resources = store.systems()[0].resources;
  expect(store.setRoleConfig(id, "mesen", { region: "pal" })).toBe(true);
  const rebuilt = store.systems()[0].id;
  expect(rebuilt === id).toBe(false);
  expect(be.graphNode(`sys-${rebuilt}`)?.settings?.fifoDepth).toBe(1 << 20); // the schema's clamp, in the document
  expect(be.graphNode(`sys-${rebuilt}`)?.settings?.region).toBe("pal");
  expect(store.systems()[0].resources.rom === resources.rom).toBe(false);
});

test("clear publishes the empty document and then gives every system's resources back", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  store.addSystem("/roms/a.gb");
  store.addSystem("/roms/b.nes");
  be.log.length = 0;
  store.clear();
  expect(order(be)).toEqual(["setGraph", "releaseResource", "releaseResource"]);
  expect(store.systems()).toEqual([]);
  expect(heldIds(be)).toEqual([]);
});

test("clear warns when the empty document is refused and still releases what it held", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  store.addSystem("/roms/a.gb");
  store.addSystem("/roms/b.nes");
  refuse(be, 1);
  const { warnings } = warned(() => store.clear());
  expect(store.systems()).toEqual([]);
  expect(heldIds(be)).toEqual([]);
  expect(warnings.some((w) => w.includes("[systems] clear: the engine refused the empty document"))).toBe(true);
});

test("clear offers every handle it holds to the engine even when one release throws", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  be.seed("/roms/b.nes", nesRom());
  store.addSystem("/roms/a.gb"); // its ROM and the battery seed
  store.addSystem("/roms/b.nes"); // its ROM alone
  const held = heldIds(be);
  expect(held.length).toBe(3);
  const releaseResource = be.releaseResource.bind(be);
  const offered: number[] = [];
  let first = true;
  be.releaseResource = (handle: number): boolean => {
    offered.push(handle);
    if (first) {
      first = false;
      throw new Error("the releaseResource RPC died");
    }
    return releaseResource(handle);
  };
  const { warnings } = warned(() => store.clear());
  be.releaseResource = releaseResource;
  // `new` and a load have moved the entries on by the time these are released, so a release that dies mid-loop would
  // strand every handle behind it: bytes the engine holds with no system, no document and no later verb to give them
  // back. Each one is tried on its own, and the loop finishes.
  expect(offered).toEqual(held); // every handle, in the order it was held, the throw among them
  expect(store.systems()).toEqual([]);
  expect(warnings.some((w) => w.includes("releasing a system's resource threw"))).toBe(true);
});

test("discard drops one system and its resources at once, publishing nothing", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  const a = store.addSystem("/roms/a.gb")!;
  const b = store.addSystem("/roms/b.nes")!;
  const doomed = store.systems().find((e) => e.id === a)!;
  be.log.length = 0;
  expect(store.discard(a)).toEqual(doomed);
  expect(order(be)).toEqual(["releaseResource"]);
  expect(be.releaseResourceCalls).toEqual([doomed.resources.rom]);
  expect(store.systems().map((e) => e.id)).toEqual([b]);
  expect(be.resource(doomed.resources.rom)).toBe(undefined);
  expect(store.discard(999)).toBe(undefined);
  expect(order(be)).toEqual(["releaseResource"]); // nothing to drop for an id it does not hold
});

test("discard offers every handle of the system it drops even when one release throws", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/a.sav", "battery");
  be.seed("/roms/b.nes", nesRom());
  const a = store.addSystem("/roms/a.gb")!;
  const b = store.addSystem("/roms/b.nes")!;
  const doomed = store.systems().find((e) => e.id === a)!;
  const handles = systemHandles(doomed.resources);
  expect(handles.length).toBe(2); // its ROM and the battery seed
  const releaseResource = be.releaseResource.bind(be);
  const offered: number[] = [];
  let first = true;
  be.releaseResource = (handle: number): boolean => {
    offered.push(handle);
    if (first) {
      first = false;
      throw new Error("the releaseResource RPC died");
    }
    return releaseResource(handle);
  };
  const { result, warnings } = warned(() => store.discard(a));
  be.releaseResource = releaseResource;
  // A project load's retry calls this between refusals, with its own list already shortened: a release that died here
  // would throw out of the load and leave the handles behind it held by an engine that stopped naming their system.
  expect(result).toEqual(doomed); // the entry it dropped, as it always answers
  expect(offered).toEqual(handles); // both of them, the throw among them
  expect(store.systems().map((e) => e.id)).toEqual([b]);
  expect(warnings.some((w) => w.includes("releasing a dropped system's resource threw"))).toBe(true);
});

test("a restore republish that is refused too is reported, with the model still the one before the edit", () => {
  const { be, store } = newStore();
  be.seed("/roms/a.gb", gbRom());
  const id = store.addSystem("/roms/a.gb")!;
  const before = store.systems()[0];
  const heldBefore = heldIds(be);
  refuse(be, 2); // the edit's document, then the restored model's
  const failure = warned(() => store.reset(id));
  expect(failure.result).toBe(null);
  expect(store.systems()[0]).toEqual(before);
  expect(heldIds(be)).toEqual(heldBefore);
  expect(failure.warnings.some((w) => w.includes("the restored model was refused too"))).toBe(true);
});

test("DirectGraph puts each system's resources and releases them after the document that stopped naming them", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  const g = new DirectGraph(be);
  const spec = (romPath: string, platform: "gb" | "nes", core: "sameboy" | "mesen"): ConstructSpec => ({
    romPath,
    platform,
    core,
    embeddedRom: "",
    savPath: null,
    statePath: null,
  });
  be.log.length = 0;
  expect(g.add(spec("/roms/a.gb", "gb", "sameboy"), 1)).toBe(true);
  const first = be.bootOf(1)!;
  expect(order(be)).toEqual(["putResource", "setGraph"]);
  be.log.length = 0;
  expect(g.replace(1, spec("/roms/b.nes", "nes", "mesen"), 2)).toBe(true);
  // The replacing system's ROM is put before the document, the old one released only after it planned.
  expect(order(be)).toEqual(["putResource", "setGraph", "releaseResource"]);
  expect(be.releaseResourceCalls).toEqual([first.rom!.id]);
  be.log.length = 0;
  expect(g.remove(2)).toBe(true);
  expect(order(be)).toEqual(["setGraph", "releaseResource"]);
  expect(heldIds(be)).toEqual([]);
});

test("DirectGraph gives back a refused change's resources and keeps the ones it still places", () => {
  const be = new MockBackend("/cfg");
  be.seed("/roms/a.gb", gbRom());
  be.seed("/roms/b.nes", nesRom());
  const g = new DirectGraph(be);
  const gb: ConstructSpec = {
    romPath: "/roms/a.gb",
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
  };
  const nes: ConstructSpec = {
    romPath: "/roms/b.nes",
    platform: "nes",
    core: "mesen",
    embeddedRom: "",
    savPath: null,
    statePath: null,
  };
  expect(g.add(gb, 1)).toBe(true);
  const heldBefore = heldIds(be);
  be.log.length = 0;
  refuse(be, 1);
  expect(g.add(nes, 2)).toBe(false);
  expect(g.ids()).toEqual([1]);
  expect(heldIds(be)).toEqual(heldBefore);
  // The refused document (which this test's refusal helper records without logging), the resource its boot put given
  // back, then the restored model.
  expect(order(be)).toEqual(["putResource", "releaseResource", "setGraph"]);
  expect(be.graphRequests[be.graphRequests.length - 1].slots).toEqual([1]);
});
