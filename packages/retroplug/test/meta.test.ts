// Meta-test: proves the harness + runner + MockBackend work end to
// end under `tjs run`, before any real feature exists. If this is green, the
// TDD loop is ready and every subsequent feature test can rely on it.
import { test, expect } from "../testing/harness";
import { MockBackend } from "../testing/mockBackend";

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
