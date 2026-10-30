// Meta-test: proves the harness + runner + MockBackend work end to
// end under `tjs run`, before any real feature exists. If this is green, the
// TDD loop is ready and every subsequent feature test can rely on it.
import { test, expect } from "../testing/harness";
import { MockBackend } from "../testing/mockBackend";
import type { Backend, SetGraphRequest } from "../src/backend";

test("harness: expect matchers behave", () => {
  expect(1 + 1).toBe(2);
  expect({ a: [1, 2] }).toEqual({ a: [1, 2] });
  expect("").toBeFalsy();
  expect([1]).toBeTruthy();
  expect(() => {
    throw new Error("boom");
  }).toThrow("boom");
});

test("mock backend: files round-trip through read / write / exists", () => {
  const be = new MockBackend("/cfg");
  expect(be.fileExists("/cfg/recent.json")).toBeFalsy();
  be.writeFile("/cfg/recent.json", new TextEncoder().encode("[]"));
  expect(be.fileExists("/cfg/recent.json")).toBeTruthy();
  expect(be.readText("/cfg/recent.json")).toBe("[]");
  expect(be.configDir()).toBe("/cfg");
});

test("mock backend: atomic write + rename move bytes", () => {
  const be = new MockBackend("/cfg");
  be.writeFileAtomic("/cfg/a.json", new TextEncoder().encode("x"));
  expect(be.rename("/cfg/a.json", "/cfg/b.json")).toBeTruthy();
  expect(be.fileExists("/cfg/a.json")).toBeFalsy();
  expect(be.readText("/cfg/b.json")).toBe("x");
  expect(be.rename("/cfg/missing.json", "/cfg/c.json")).toBeFalsy();
});

test("mock backend: canonicalize collapses ./ and ../ (the dedupe key)", () => {
  const be = new MockBackend("/cfg");
  expect(be.canonicalize("/a/b/../c")).toBe("/a/c");
  expect(be.canonicalize("/a/./b")).toBe("/a/b");
  expect(be.canonicalize("recent.json")).toBe("/cfg/recent.json"); // relative -> config dir
});

test("mock backend: putResource answers ids that count up, and a dropped id stays dropped", () => {
  const be = new MockBackend("/cfg");
  const rom = be.putResource({ type: "rom", bytes: Uint8Array.of(1, 2, 3) });
  if ("error" in rom) throw new Error(rom.error);
  expect(rom.id).toBe(1);
  const sram = be.putResource({ type: "sram", path: "/cfg/game.sav" }); // absent: refused
  expect("error" in sram).toBeTruthy();
  be.seed("/cfg/game.sav", "BATTERY");
  const seeded = be.putResource({ type: "sram", path: "/cfg/game.sav" });
  if ("error" in seeded) throw new Error(seeded.error);
  expect(seeded.id).toBe(2); // a refused put spends no id
  expect(be.resource(rom.id)).toEqual({ type: "rom", bytes: Uint8Array.of(1, 2, 3) });
  expect(be.resource(seeded.id)!.bytes.length).toBe(7);

  expect(be.releaseResource(rom.id)).toBeTruthy();
  expect(be.releaseResource(rom.id)).toBeFalsy(); // idempotent
  expect(be.resource(rom.id) === undefined).toBeTruthy(); // and it stays dropped
  expect(be.releaseResource(99)).toBeFalsy();
  expect(be.putResource({ type: "rom", embedded: "mgb" })).toEqual({ id: 3 }); // never reused
  expect(be.putResource({ type: "rom", embedded: "nope" })).toEqual({ error: '"nope" is not an embedded ROM' });
  expect(be.putResource({ type: "rom", path: "/cfg/missing.gb" })).toEqual({
    error: "could not read /cfg/missing.gb: the file is absent, unreadable or empty",
  });
  // A blank battery is a legitimate empty seed; a zero-length ROM has nothing to boot.
  expect(be.putResource({ type: "rom", bytes: new Uint8Array(0) })).toEqual({
    error: "a rom resource cannot be empty: a zero-length ROM cannot boot",
  });
  expect(be.putResource({ type: "sram", bytes: new Uint8Array(0) })).toEqual({ id: 4 });
  // A recorded call owns its bytes: the caller mutating its array afterwards changes neither the log nor the resource.
  const mutable = Uint8Array.of(5, 5);
  const copied = be.putResource({ type: "rom", bytes: mutable });
  if ("error" in copied) throw new Error(copied.error);
  mutable[0] = 0;
  expect(be.putResourceCalls[be.putResourceCalls.length - 1].spec).toEqual({
    type: "rom",
    bytes: Uint8Array.of(5, 5),
  });
  expect(be.resource(copied.id)).toEqual({ type: "rom", bytes: Uint8Array.of(5, 5) });
  expect(be.putResourceCalls.length).toBe(9); // refused puts are recorded too, with id 0
  expect(be.putResourceCalls.filter((c) => c.id === 0).length).toBe(4);
});

test("mock backend: a document naming a resource the engine does not hold is refused, as native refuses it", () => {
  const be = new MockBackend("/cfg");
  const rom = be.putResource({ type: "rom", bytes: Uint8Array.of(1, 2, 3) });
  const sram = be.putResource({ type: "sram", bytes: Uint8Array.of(4) });
  if ("error" in rom || "error" in sram) throw new Error("the puts should have been taken");
  const doc = (settings: Record<string, unknown>): SetGraphRequest => ({
    document: JSON.stringify({ nodes: [{ id: "sys-1", type: "rp.sameboy.gb", settings }], wires: [] }),
    slots: [1],
  });
  const name = (field: string, id: number): Record<string, unknown> => ({ rom: `res:${rom.id}`, [field]: `res:${id}` });
  expect(be.setGraph(doc(name("sram", sram.id))).ok).toBe(true); // a live rom and a live battery plan
  // A dropped id is not a resource: the engine keeps its own table, and a stale reference must not name whatever took
  // its place, so it is refused with the text native's node gives.
  const dropped = be.putResource({ type: "sram", bytes: Uint8Array.of(9) });
  if ("error" in dropped) throw new Error(dropped.error);
  be.releaseResource(dropped.id);
  expect(be.setGraph(doc(name("sram", dropped.id)))).toEqual({
    ok: false,
    errors: [{ nodeId: "sys-1", port: "", code: "create-failed", message: `sram: "res:${dropped.id}" is not a live resource` }],
  });
  expect(be.setGraph(doc(name("state", 999))).errors[0].message).toBe('state: "res:999" is not a live resource');
  // A resource of another type, a field that is not a reference at all, and no ROM: each its own reason.
  expect(be.setGraph(doc(name("sram", rom.id))).errors[0]).toEqual({
    nodeId: "sys-1",
    port: "",
    code: "create-failed",
    message: `sram: "res:${rom.id}" is a rom resource, not a sram`,
  });
  expect(be.setGraph(doc({ rom: "res:007" })).errors[0].message).toBe('rom: "res:007" is not a resource reference (res:N)');
  expect(be.setGraph(doc({ sram: `res:${sram.id}` })).errors[0]).toEqual({
    nodeId: "sys-1",
    port: "",
    code: "settings-invalid",
    message: 'a system node names its ROM as {"rom": "res:N"}',
  });
});

test("mock backend: loadScriptPack refuses every pack, before any replan, with no types", () => {
  // It has no JS engine to run a pack's nodes in (test-native/script-packs.test.ts runs them on the real host).
  const be: Backend = new MockBackend("/cfg");
  expect(be.loadScriptPack({ id: "user.pack", source: "", memoryLimitBytes: 1 << 20 })).toEqual({
    ok: false,
    error: "the mock backend runs no script packs",
    errors: [],
    types: [],
  });
});
