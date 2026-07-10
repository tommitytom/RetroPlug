// packages/retroplug/testing/harness.ts
var cases = [];
var scheduled = false;
function test(name, fn) {
  cases.push({ name, fn });
  if (!scheduled) {
    scheduled = true;
    Promise.resolve().then(runAll);
  }
}
function fmt(v) {
  if (v instanceof Uint8Array) {
    const head = Array.from(v.slice(0, 8)).join(",");
    return `Uint8Array(${v.length})[${head}${v.length > 8 ? ",\u2026" : ""}]`;
  }
  try {
    return typeof v === "string" ? JSON.stringify(v) : JSON.stringify(v) ?? String(v);
  } catch {
    return String(v);
  }
}
function expect(actual) {
  return {
    toBe(expected) {
      if (!Object.is(actual, expected)) {
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
      }
    },
    toEqual(expected) {
      const a = JSON.stringify(actual);
      const b = JSON.stringify(expected);
      if (a !== b) throw new Error(`expected ${b}, got ${a}`);
    },
    toBeTruthy() {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy() {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
    toThrow(match) {
      if (typeof actual !== "function") throw new Error("toThrow expects a function");
      let threw;
      let didThrow = false;
      try {
        actual();
      } catch (e) {
        didThrow = true;
        threw = e;
      }
      if (!didThrow) throw new Error("expected function to throw, but it did not");
      if (match !== void 0) {
        const msg = threw instanceof Error ? threw.message : String(threw);
        const ok = typeof match === "string" ? msg.includes(match) : match.test(msg);
        if (!ok) throw new Error(`expected throw matching ${fmt(match)}, got ${fmt(msg)}`);
      }
    }
  };
}
function exit(code) {
  const g = globalThis;
  g.tjs?.exit(code);
}
async function runAll() {
  const out = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    try {
      await fn();
      out.push(`ok ${i + 1} - ${name}`);
    } catch (e) {
      failed++;
      const detail = e instanceof Error ? `${e.message}
${e.stack ?? ""}` : String(e);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  console.log(out.join("\n"));
  exit(failed ? 1 : 0);
}

// packages/retroplug/src/pathUtil.ts
var SEP = /[\\/]/;
function lastSep(p) {
  return Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
}
function dirname(p) {
  const i = lastSep(p);
  if (i < 0) return "";
  if (i === 0) return "/";
  return p.slice(0, i);
}
function basename(p) {
  const i = lastSep(p);
  return i < 0 ? p : p.slice(i + 1);
}
function extDot(name) {
  const dot = name.lastIndexOf(".");
  return dot <= 0 ? -1 : dot;
}
function stem(p) {
  const name = basename(p);
  const dot = extDot(name);
  return dot < 0 ? name : name.slice(0, dot);
}
function extension(p) {
  const name = basename(p);
  const dot = extDot(name);
  return dot < 0 ? "" : name.slice(dot);
}
function extensionLower(p) {
  return extension(p).toLowerCase();
}
function replaceExtension(p, ext) {
  const i = lastSep(p);
  const dir = i < 0 ? "" : p.slice(0, i + 1);
  const name = i < 0 ? p : p.slice(i + 1);
  const dot = extDot(name);
  const base = dot < 0 ? name : name.slice(0, dot);
  const suffix = ext === "" ? "" : ext.startsWith(".") ? ext : "." + ext;
  return dir + base + suffix;
}
function replaceFilename(p, name) {
  const i = lastSep(p);
  return i < 0 ? name : p.slice(0, i + 1) + name;
}
function joinPath(dir, name) {
  if (!dir) return name;
  return SEP.test(dir[dir.length - 1]) ? dir + name : dir + "/" + name;
}
function isAbsolute(p) {
  return /^([a-zA-Z]:[\\/]|[\\/])/.test(p);
}

// packages/retroplug/test/paths/primitives.test.ts
test("dirname: everything before the last separator", () => {
  expect(dirname("/dir/game.gb")).toBe("/dir");
  expect(dirname("a/b/c")).toBe("a/b");
  expect(dirname("game.gb")).toBe("");
  expect(dirname("/game.gb")).toBe("/");
  expect(dirname("dir\\game.gb")).toBe("dir");
});
test("basename: the last component", () => {
  expect(basename("/dir/game.gb")).toBe("game.gb");
  expect(basename("game.gb")).toBe("game.gb");
  expect(basename("a/b/c.nes")).toBe("c.nes");
  expect(basename("dir\\game.gb")).toBe("game.gb");
});
test("stem: filename minus the final extension", () => {
  expect(stem("/d/game.gb")).toBe("game");
  expect(stem("game")).toBe("game");
  expect(stem("/d/game.tar.gz")).toBe("game.tar");
  expect(stem("/d/.config")).toBe(".config");
});
test("extension: the final extension including the dot", () => {
  expect(extension("/d/game.gb")).toBe(".gb");
  expect(extension("game")).toBe("");
  expect(extension("a.tar.gz")).toBe(".gz");
  expect(extension("/d/.config")).toBe("");
  expect(extension("GAME.GB")).toBe(".GB");
  expect(extensionLower("GAME.GB")).toBe(".gb");
});
test("replaceExtension: swaps the final extension, or appends when there is none", () => {
  expect(replaceExtension("/d/game.gb", ".rplg")).toBe("/d/game.rplg");
  expect(replaceExtension("/d/game", ".rplg")).toBe("/d/game.rplg");
  expect(replaceExtension("/d/game.tar.gz", ".sav")).toBe("/d/game.tar.sav");
  expect(replaceExtension("/d/.config", ".rplg")).toBe("/d/.config.rplg");
});
test("replaceFilename: swaps the whole last component", () => {
  expect(replaceFilename("/d/game.gb", "game-2.sav")).toBe("/d/game-2.sav");
  expect(replaceFilename("game.gb", "x.sav")).toBe("x.sav");
  expect(replaceFilename("a/b/c.gb", "d.sav")).toBe("a/b/d.sav");
});
test("joinPath: joins a dir and a name with a single separator", () => {
  expect(joinPath("/d", "x.gb")).toBe("/d/x.gb");
  expect(joinPath("", "x.gb")).toBe("x.gb");
  expect(joinPath("/", "x.gb")).toBe("/x.gb");
});
test("isAbsolute: leading slash or a drive letter", () => {
  expect(isAbsolute("/roms/x.gb")).toBeTruthy();
  expect(isAbsolute("C:/roms/x.gb")).toBeTruthy();
  expect(isAbsolute("C:\\roms\\x.gb")).toBeTruthy();
  expect(isAbsolute("roms/x.gb")).toBeFalsy();
  expect(isAbsolute("./x.gb")).toBeFalsy();
  expect(isAbsolute("")).toBeFalsy();
});
