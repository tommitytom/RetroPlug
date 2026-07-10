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

// packages/retroplug/src/systemsList.ts
function findById(list, id) {
  return list.find((e) => e.id === id);
}
function appendEntry(list, entry2) {
  return [...list, entry2];
}
function removeById(list, id) {
  return list.filter((e) => e.id !== id);
}
function replaceById(list, id, next) {
  const idx = list.findIndex((e) => e.id === id);
  if (idx < 0) return list;
  const out = list.slice();
  out[idx] = next;
  return out;
}
function isSuffixOwned(list, romPath, suffix) {
  return list.some((e) => e.romPath === romPath && e.savSuffix === suffix);
}
function resolveSavOverride(romPath, suffix, pickedSav, canonicalize) {
  if (!pickedSav) return "";
  const picked = canonicalize(pickedSav);
  const sibN = canonicalize(siblingSavPath(romPath, suffix));
  const sib0 = canonicalize(siblingSavPath(romPath, 0));
  return picked !== sibN && picked !== sib0 ? pickedSav : "";
}
function pickSiblingRom(savPath, exists, classify) {
  for (const cand of siblingRomCandidates(savPath)) {
    if (exists(cand) && classify(cand) !== "unknown") return cand;
  }
  return null;
}
function nextFocusAfterRemove(remaining, removedId, focusedId) {
  if (focusedId !== removedId) return focusedId;
  return remaining.length ? remaining[0].id : 0;
}

// packages/retroplug/test/systems/list.test.ts
function entry(id, romPath, savSuffix = 0, savPath = "") {
  return { id, platform: "gb", core: "sameboy", romPath, savPath, savSuffix, embeddedRom: "" };
}
test("findById / appendEntry: append keeps order and returns a new list", () => {
  const a = entry(1, "/a.gb");
  const list = appendEntry([a], entry(2, "/b.gb"));
  expect(list.map((e) => e.id)).toEqual([1, 2]);
  expect(findById(list, 2)?.romPath).toBe("/b.gb");
  expect(findById(list, 9)).toBe(void 0);
});
test("removeById: drops by id, survivors keep ids + relative order", () => {
  const list = [entry(1, "/a.gb"), entry(2, "/b.gb"), entry(3, "/c.gb")];
  expect(removeById(list, 2).map((e) => e.id)).toEqual([1, 3]);
  expect(removeById(list, 9).map((e) => e.id)).toEqual([1, 2, 3]);
});
test("replaceById: swaps in place, preserving the slot index", () => {
  const list = [entry(1, "/a.gb"), entry(2, "/b.gb"), entry(3, "/c.gb")];
  const next = replaceById(list, 2, entry(9, "/new.gb"));
  expect(next.map((e) => e.id)).toEqual([1, 9, 3]);
  expect(findById(next, 9)?.romPath).toBe("/new.gb");
  expect(replaceById(list, 42, entry(9, "/x.gb"))).toBe(list);
});
test("isSuffixOwned: true when a live system with this rom holds the suffix", () => {
  const list = [entry(1, "/game.gb", 0), entry(2, "/game.gb", 2), entry(3, "/other.gb", 0)];
  expect(isSuffixOwned(list, "/game.gb", 0)).toBeTruthy();
  expect(isSuffixOwned(list, "/game.gb", 2)).toBeTruthy();
  expect(isSuffixOwned(list, "/game.gb", 3)).toBeFalsy();
  expect(isSuffixOwned(list, "/other.gb", 2)).toBeFalsy();
});
test("resolveSavOverride: the natural sibling is NOT an override; a different file IS", () => {
  const canon = (p) => p;
  expect(resolveSavOverride("/d/game.gb", 0, "/d/game.sav", canon)).toBe("");
  expect(resolveSavOverride("/d/game.gb", 2, "/d/game-2.sav", canon)).toBe("");
  expect(resolveSavOverride("/d/game.gb", 2, "/d/game.sav", canon)).toBe("");
  expect(resolveSavOverride("/d/game.gb", 0, "/other/mine.sav", canon)).toBe("/other/mine.sav");
  expect(resolveSavOverride("/d/game.gb", 0, "", canon)).toBe("");
});
test("resolveSavOverride: applies canonicalization before comparing", () => {
  const canon = (p) => p.replace(/\/[^/]+\/\.\.\//g, "/").replace(/\/\.\//g, "/");
  expect(resolveSavOverride("/d/game.gb", 0, "/d/sub/../game.sav", canon)).toBe("");
});
test("pickSiblingRom: first candidate that exists AND classifies as a real ROM", () => {
  const present = /* @__PURE__ */ new Set(["/roms/game.gbc", "/roms/game.gba"]);
  const kinds = { "/roms/game.gbc": "unknown", "/roms/game.gba": "gba" };
  const got = pickSiblingRom(
    "/roms/game.sav",
    (p) => present.has(p),
    (p) => kinds[p] ?? "unknown"
  );
  expect(got).toBe("/roms/game.gba");
});
test("pickSiblingRom: null when no candidate both exists and validates", () => {
  expect(pickSiblingRom("/roms/game.sav", () => false, () => "unknown")).toBe(null);
  expect(pickSiblingRom("/roms/game.sav", () => true, () => "unknown")).toBe(null);
});
test("nextFocusAfterRemove: refocus the new front only when the focused one went away", () => {
  const remaining = [entry(2, "/b.gb"), entry(3, "/c.gb")];
  expect(nextFocusAfterRemove(remaining, 1, 1)).toBe(2);
  expect(nextFocusAfterRemove(remaining, 1, 3)).toBe(3);
  expect(nextFocusAfterRemove([], 1, 1)).toBe(0);
});
