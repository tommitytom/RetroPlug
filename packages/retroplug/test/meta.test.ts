// Meta-test: proves the harness + runner + MockBackend work end to
// end under `tjs run`, before any real feature exists. If this is green, the
// TDD loop is ready and every subsequent feature test can rely on it.
import { test, expect } from "../testing/harness";
import { MockBackend } from "../testing/mockBackend";
import type { Backend } from "../src/backend";

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
