// Path primitives — the std::filesystem::path member operations the native code
// leans on (parent_path / filename / stem / extension / replace_extension /
// replace_filename), reproduced as pure string ops. Both `/` and `\` count as
// separators (the plugin is cross-platform); output is forward-slash form. These
// underpin every sibling-.sav/.rplg derivation, so the edge cases (no extension,
// multi-dot, dotfiles, backslashes) are locked here.
import { test, expect } from "../../testing/harness";
import {
  dirname,
  basename,
  stem,
  extension,
  extensionLower,
  replaceExtension,
  replaceFilename,
  joinPath,
  isAbsolute,
} from "../../src/pathUtil";

test("dirname: everything before the last separator", () => {
  expect(dirname("/dir/game.gb")).toBe("/dir");
  expect(dirname("a/b/c")).toBe("a/b");
  expect(dirname("game.gb")).toBe(""); // no separator
  expect(dirname("/game.gb")).toBe("/"); // root parent stays root
  expect(dirname("dir\\game.gb")).toBe("dir"); // backslash is a separator too
});

test("basename: the last component", () => {
  expect(basename("/dir/game.gb")).toBe("game.gb");
  expect(basename("game.gb")).toBe("game.gb");
  expect(basename("a/b/c.nes")).toBe("c.nes");
  expect(basename("dir\\game.gb")).toBe("game.gb");
});

test("stem: filename minus the final extension", () => {
  expect(stem("/d/game.gb")).toBe("game");
  expect(stem("game")).toBe("game"); // no extension
  expect(stem("/d/game.tar.gz")).toBe("game.tar"); // only the last dot
  expect(stem("/d/.config")).toBe(".config"); // leading-dot file has no extension
});

test("extension: the final extension including the dot", () => {
  expect(extension("/d/game.gb")).toBe(".gb");
  expect(extension("game")).toBe(""); // none
  expect(extension("a.tar.gz")).toBe(".gz"); // only the last
  expect(extension("/d/.config")).toBe(""); // dotfile: no extension
  expect(extension("GAME.GB")).toBe(".GB"); // preserves case
  expect(extensionLower("GAME.GB")).toBe(".gb"); // the .sav-compare seam lowercases
});

test("replaceExtension: swaps the final extension, or appends when there is none", () => {
  expect(replaceExtension("/d/game.gb", ".rplg")).toBe("/d/game.rplg");
  expect(replaceExtension("/d/game", ".rplg")).toBe("/d/game.rplg"); // append
  expect(replaceExtension("/d/game.tar.gz", ".sav")).toBe("/d/game.tar.sav"); // only last
  expect(replaceExtension("/d/.config", ".rplg")).toBe("/d/.config.rplg"); // dotfile appends
});

test("replaceFilename: swaps the whole last component", () => {
  expect(replaceFilename("/d/game.gb", "game-2.sav")).toBe("/d/game-2.sav");
  expect(replaceFilename("game.gb", "x.sav")).toBe("x.sav"); // no dir
  expect(replaceFilename("a/b/c.gb", "d.sav")).toBe("a/b/d.sav");
});

test("joinPath: joins a dir and a name with a single separator", () => {
  expect(joinPath("/d", "x.gb")).toBe("/d/x.gb");
  expect(joinPath("", "x.gb")).toBe("x.gb"); // empty dir → relative
  expect(joinPath("/", "x.gb")).toBe("/x.gb"); // root already ends in a separator
});

test("isAbsolute: leading slash or a drive letter", () => {
  expect(isAbsolute("/roms/x.gb")).toBeTruthy();
  expect(isAbsolute("C:/roms/x.gb")).toBeTruthy();
  expect(isAbsolute("C:\\roms\\x.gb")).toBeTruthy();
  expect(isAbsolute("roms/x.gb")).toBeFalsy();
  expect(isAbsolute("./x.gb")).toBeFalsy();
  expect(isAbsolute("")).toBeFalsy();
});
