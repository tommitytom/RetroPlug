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
      } catch (e2) {
        didThrow = true;
        threw = e2;
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
    } catch (e2) {
      failed++;
      const detail = e2 instanceof Error ? `${e2.message}
${e2.stack ?? ""}` : String(e2);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  console.log(out.join("\n"));
  exit(failed ? 1 : 0);
}

// packages/retroplug/src/recentList.ts
var MAX_ENTRIES = 10;
function addEntry(list, path, name, max = MAX_ENTRIES) {
  const existing = list.find((e2) => e2.path === path);
  const keepName = name || existing?.name || "";
  return [{ path, name: keepName }, ...list.filter((e2) => e2.path !== path)].slice(0, max);
}
function removeEntry(list, path) {
  return list.filter((e2) => e2.path !== path);
}
function renameEntry(list, path, name) {
  return list.map((e2) => e2.path === path ? { path: e2.path, name } : e2);
}
function relinkEntry(list, oldPath, newPath) {
  const idx = list.findIndex((e2) => e2.path === oldPath);
  if (idx < 0) return list;
  const out = [];
  list.forEach((e2, i) => {
    if (i === idx) out.push({ path: newPath, name: e2.name });
    else if (e2.path !== newPath) out.push(e2);
  });
  return out;
}
function label(entry) {
  return entry.name.trim() || stripProjectExt(basename(entry.path));
}
function basename(p) {
  return p.split(/[\\/]/).pop() ?? p;
}
function stripProjectExt(name) {
  return name.replace(/\.rplg(\.zip)?$/i, "");
}

// packages/retroplug/test/recent/list.test.ts
var e = (path, name = "") => ({ path, name });
var paths = (list) => list.map((x) => x.path);
test("add: prepends and keeps most-recent-first", () => {
  let l = [];
  l = addEntry(l, "/a", "");
  l = addEntry(l, "/b", "");
  l = addEntry(l, "/c", "");
  expect(paths(l)).toEqual(["/c", "/b", "/a"]);
});
test("add: re-adding an existing path moves it to the front (dedupe)", () => {
  let l = [e("/c"), e("/b"), e("/a")];
  l = addEntry(l, "/a", "");
  expect(paths(l)).toEqual(["/a", "/c", "/b"]);
  expect(l.length).toBe(3);
});
test("add: caps at max, dropping the oldest", () => {
  let l = [];
  for (let i = 0; i < 12; i++) l = addEntry(l, `/p${i}`, "", 10);
  expect(l.length).toBe(10);
  expect(l[0].path).toBe("/p11");
  expect(l[9].path).toBe("/p2");
});
test("add: preserves an existing alias on re-add unless a new name is given", () => {
  let l = [e("/a", "My Song")];
  l = addEntry(l, "/a", "");
  expect(l[0].name).toBe("My Song");
  l = addEntry(l, "/a", "Renamed");
  expect(l[0].name).toBe("Renamed");
});
test("rename: sets the alias; empty clears it", () => {
  let l = [e("/a", "Old"), e("/b")];
  l = renameEntry(l, "/a", "New");
  expect(l[0].name).toBe("New");
  l = renameEntry(l, "/a", "");
  expect(l[0].name).toBe("");
});
test("remove: drops the entry, leaves the rest", () => {
  let l = [e("/a"), e("/b"), e("/c")];
  l = removeEntry(l, "/b");
  expect(paths(l)).toEqual(["/a", "/c"]);
});
test("relink: repoints in place, keeps position + name, absorbs a collision", () => {
  let l = [e("/x"), e("/old", "Alias"), e("/y"), e("/new")];
  l = relinkEntry(l, "/old", "/new");
  expect(paths(l)).toEqual(["/x", "/new", "/y"]);
  expect(l[1].name).toBe("Alias");
});
test("relink: a missing source leaves the list untouched", () => {
  const l = [e("/a"), e("/b")];
  expect(relinkEntry(l, "/nope", "/c")).toEqual(l);
});
test("label: alias wins, else the basename with the project extension stripped", () => {
  expect(label(e("/music/song.rplg", "Nice Name"))).toBe("Nice Name");
  expect(label(e("/music/song.rplg", "  "))).toBe("song");
  expect(label(e("/music/song.rplg"))).toBe("song");
  expect(label(e("/music/song.rplg.zip"))).toBe("song");
  expect(label(e("/music/mixtape.RPLG"))).toBe("mixtape");
  expect(label(e("/roms/game.gb"))).toBe("game.gb");
});
