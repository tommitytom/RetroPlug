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

// packages/retroplug/src/savPaths.ts
function siblingPath(romPath, suffix, ext) {
  if (!romPath) return "";
  if (suffix >= 2) return replaceFilename(romPath, `${stem(romPath)}-${suffix}${ext}`);
  return replaceExtension(romPath, ext);
}
function siblingSavPath(romPath, suffix = 0) {
  return siblingPath(romPath, suffix, ".sav");
}
function siblingRplgPath(romPath) {
  if (!romPath) return "";
  return replaceExtension(romPath, ".rplg");
}
function resolveSavPath(romPath, suffix, override) {
  return override || siblingSavPath(romPath, suffix);
}
function nextFreeSavSuffix(romPath, isOwned, existsOnDisk) {
  if (!romPath) return 0;
  if (!isOwned(0)) return 0;
  let n = 2;
  while (isOwned(n) || existsOnDisk(n)) n++;
  return n;
}
var ROM_EXTS = [".gb", ".gbc", ".gba", ".nes"];
function siblingRomCandidates(savPath) {
  const dir = dirname(savPath);
  const s = stem(savPath);
  const stems = [s];
  const dash = s.lastIndexOf("-");
  const tail = dash >= 0 ? s.slice(dash + 1) : "";
  if (dash >= 0 && tail.length > 0 && /^[0-9]+$/.test(tail)) stems.push(s.slice(0, dash));
  const out = [];
  for (const st of stems) {
    for (const ext of ROM_EXTS) out.push(joinPath(dir, st + ext));
  }
  return out;
}

// packages/retroplug/test/paths/siblings.test.ts
test("siblingSavPath: suffix 0/1 replace the extension, \u22652 disambiguate the filename", () => {
  expect(siblingSavPath("/d/game.gb")).toBe("/d/game.sav");
  expect(siblingSavPath("/d/game.gb", 0)).toBe("/d/game.sav");
  expect(siblingSavPath("/d/game.gb", 1)).toBe("/d/game.sav");
  expect(siblingSavPath("/d/game.gb", 2)).toBe("/d/game-2.sav");
  expect(siblingSavPath("/d/game.gb", 3)).toBe("/d/game-3.sav");
  expect(siblingSavPath("")).toBe("");
});
test("siblingPath: the generic ext helper (also used for savestates)", () => {
  expect(siblingPath("/d/game.gb", 0, ".ss0")).toBe("/d/game.ss0");
  expect(siblingPath("/d/game.gb", 2, ".ss0")).toBe("/d/game-2.ss0");
  expect(siblingPath("", 2, ".ss0")).toBe("");
});
test("siblingRplgPath: replace the extension, suffix-independent", () => {
  expect(siblingRplgPath("/d/game.gb")).toBe("/d/game.rplg");
  expect(siblingRplgPath("/d/game.gbc")).toBe("/d/game.rplg");
  expect(siblingRplgPath("")).toBe("");
});
test("resolveSavPath: an explicit override wins, else the suffix sibling", () => {
  expect(resolveSavPath("/d/game.gb", 0, "")).toBe("/d/game.sav");
  expect(resolveSavPath("/d/game.gb", 2, "")).toBe("/d/game-2.sav");
  expect(resolveSavPath("/d/game.gb", 0, "/other/battery.sav")).toBe("/other/battery.sav");
});
test("nextFreeSavSuffix: reclaim slot 0 when unowned, ignoring any file on disk", () => {
  expect(nextFreeSavSuffix("/d/game.gb", () => false, () => true)).toBe(0);
  expect(nextFreeSavSuffix("", () => true, () => true)).toBe(0);
});
test("nextFreeSavSuffix: skip to 2, then grow past owned + on-disk slots", () => {
  const owned = (set) => (n) => set.includes(n);
  const onDisk = (set) => (n) => set.includes(n);
  expect(nextFreeSavSuffix("/d/game.gb", owned([0]), () => false)).toBe(2);
  expect(nextFreeSavSuffix("/d/game.gb", owned([0]), onDisk([2]))).toBe(3);
  expect(nextFreeSavSuffix("/d/game.gb", owned([0, 2]), onDisk([3]))).toBe(4);
});
test("siblingRomCandidates: exact stem first, all four extensions in order", () => {
  expect(siblingRomCandidates("/roms/game.sav")).toEqual([
    "/roms/game.gb",
    "/roms/game.gbc",
    "/roms/game.gba",
    "/roms/game.nes"
  ]);
});
test("siblingRomCandidates: a -<digits> slot also probes the base stem, after the exact one", () => {
  expect(siblingRomCandidates("/roms/game-2.sav")).toEqual([
    "/roms/game-2.gb",
    "/roms/game-2.gbc",
    "/roms/game-2.gba",
    "/roms/game-2.nes",
    "/roms/game.gb",
    "/roms/game.gbc",
    "/roms/game.gba",
    "/roms/game.nes"
  ]);
});
test("siblingRomCandidates: only a purely-numeric suffix adds the base stem", () => {
  expect(siblingRomCandidates("/roms/game-2a.sav")).toEqual([
    "/roms/game-2a.gb",
    "/roms/game-2a.gbc",
    "/roms/game-2a.gba",
    "/roms/game-2a.nes"
  ]);
  expect(siblingRomCandidates("/roms/game-.sav")).toEqual([
    "/roms/game-.gb",
    "/roms/game-.gbc",
    "/roms/game-.gba",
    "/roms/game-.nes"
  ]);
});
test("siblingRomCandidates: no directory yields bare relative candidates", () => {
  expect(siblingRomCandidates("game.sav")).toEqual(["game.gb", "game.gbc", "game.gba", "game.nes"]);
});
