// Relative↔absolute path rebasing — the single-field kernels of
// packages/native/src/project/ProjectPaths.hpp. `rebaseToAbsolute` is the proven
// lexical join (load side; matches packages/retroplug's missingFiles.ts).
// `rebaseToRelative` mirrors the realpath-hard save side: canonicalize base + field,
// then keep the asset absolute unless it sits at/under the base. `lexicallyRelative`
// is the pure component math both rely on. The ProjectConfig-walking that applies
// these across romPath/savPath/kit samples belongs to the later project domain.
import { test, expect } from "../../testing/harness";
import { rebaseToAbsolute, rebaseToRelative, lexicallyRelative } from "../../src/projectPaths";
import { MockBackend } from "../../testing/mockBackend";

const identity = (p: string) => p;

test("rebaseToAbsolute: joins a relative field onto the base dir", () => {
  expect(rebaseToAbsolute("game.gb", "/project")).toBe("/project/game.gb");
  expect(rebaseToAbsolute("sub/game.gb", "/project")).toBe("/project/sub/game.gb");
  expect(rebaseToAbsolute("./game.gb", "/project")).toBe("/project/game.gb");
});

test("rebaseToAbsolute: collapses .. against the base", () => {
  expect(rebaseToAbsolute("../sib/game.gb", "/project/nested")).toBe("/project/sib/game.gb");
});

test("rebaseToAbsolute: absolute / empty / no-base fields are left untouched", () => {
  expect(rebaseToAbsolute("/abs/game.gb", "/project")).toBe("/abs/game.gb");
  expect(rebaseToAbsolute("", "/project")).toBe("");
  expect(rebaseToAbsolute("game.gb", "")).toBe("game.gb");
});

test("lexicallyRelative: a path at/under the base", () => {
  expect(lexicallyRelative("/project/game.gb", "/project")).toBe("game.gb");
  expect(lexicallyRelative("/project/sub/game.gb", "/project")).toBe("sub/game.gb");
});

test("lexicallyRelative: outside the base emits a .. chain; equal is '.'", () => {
  expect(lexicallyRelative("/other/game.gb", "/project")).toBe("../other/game.gb");
  expect(lexicallyRelative("/project", "/project")).toBe(".");
});

test("lexicallyRelative: different roots yield an empty relative", () => {
  expect(lexicallyRelative("C:/a/game.gb", "D:/a")).toBe(""); // different drive
});

test("rebaseToRelative: a field under the base becomes forward-slash relative", () => {
  expect(rebaseToRelative("/project/sub/game.gb", "/project", identity)).toBe("sub/game.gb");
});

test("rebaseToRelative: an asset outside the base is kept absolute (no ../ chains)", () => {
  expect(rebaseToRelative("/other/game.gb", "/project", identity)).toBe("/other/game.gb");
});

test("rebaseToRelative: empty / already-relative fields are left untouched", () => {
  expect(rebaseToRelative("", "/project", identity)).toBe("");
  expect(rebaseToRelative("sub/game.gb", "/project", identity)).toBe("sub/game.gb");
});

test("rebaseToRelative: canonicalization is applied before the relative is computed", () => {
  // A real canonicalizer collapses ./ and ../ so the non-canonical field still
  // rebases correctly under the base.
  const canon = new MockBackend("/project").canonicalize.bind(new MockBackend("/project"));
  expect(rebaseToRelative("/project/./sub/../game.gb", "/project", canon)).toBe("game.gb");
});
