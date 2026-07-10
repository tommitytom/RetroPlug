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
function lastSep(p) {
  return Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
}
function basename(p) {
  const i = lastSep(p);
  return i < 0 ? p : p.slice(i + 1);
}

// packages/retroplug/src/projectBinaries.ts
var romKey = (i) => `systems/${i}/rom`;
var sramKey = (i) => `systems/${i}/sram`;
var stateKey = (i) => `systems/${i}/state`;

// packages/retroplug/src/projectMissing.ts
function scanMissingFiles(cfg2, blobKeys, exists) {
  const out = [];
  cfg2.systems.forEach((sys, i) => {
    const romOk = !!sys.embeddedRom || blobKeys.has(romKey(i)) || !!sys.romPath && exists(sys.romPath);
    if (!romOk) out.push({ systemIndex: i, itemKind: "rom", path: sys.romPath ?? "" });
    const savOk = !sys.savPath || blobKeys.has(sramKey(i)) || blobKeys.has(stateKey(i)) || exists(sys.savPath);
    if (!savOk) out.push({ systemIndex: i, itemKind: "sram", path: sys.savPath ?? "" });
  });
  return out;
}
function relinkInConfig(cfg2, item, newPath) {
  const sys = cfg2.systems[item.systemIndex];
  if (!sys) return false;
  if (item.itemKind === "rom") sys.romPath = newPath;
  else sys.savPath = newPath;
  return true;
}
function autoFindSiblings(cfg2, newDir, blobKeys, exists) {
  let resolved = 0;
  for (const item of scanMissingFiles(cfg2, blobKeys, exists)) {
    const candidate = (newDir ? newDir + "/" : "") + basename(item.path);
    if (exists(candidate) && relinkInConfig(cfg2, item, candidate)) resolved++;
  }
  return resolved;
}

// packages/retroplug/test/project/missing.test.ts
var NO_BLOBS = /* @__PURE__ */ new Set();
function cfg(systems) {
  return { schemaVersion: "1", settings: { layout: 0, midiRouting: 0, audioRouting: 0, zoom: 0 }, systems };
}
test("scan: a ROM is missing when its path is absent and no blob is embedded", () => {
  const c = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  const missing = scanMissingFiles(c, NO_BLOBS, () => false);
  expect(missing).toEqual([{ systemIndex: 0, itemKind: "rom", path: "/roms/a.gb" }]);
});
test("scan: present ROM / embedded ROM / bundled rom-blob are all OK", () => {
  const present = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(present, NO_BLOBS, (p) => p === "/roms/a.gb")).toEqual([]);
  const embedded = cfg([{ platform: "gb", embeddedRom: "mgb" }]);
  expect(scanMissingFiles(embedded, NO_BLOBS, () => false)).toEqual([]);
  const bundled = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(bundled, /* @__PURE__ */ new Set(["systems/0/rom"]), () => false)).toEqual([]);
});
test("scan: an explicit savPath with no file + no blob is missing; an empty savPath is allowed", () => {
  const withOverride = cfg([{ platform: "gb", romPath: "/roms/a.gb", savPath: "/saves/x.sav" }]);
  const missing = scanMissingFiles(withOverride, NO_BLOBS, (p) => p === "/roms/a.gb");
  expect(missing).toEqual([{ systemIndex: 0, itemKind: "sram", path: "/saves/x.sav" }]);
  const noOverride = cfg([{ platform: "gb", romPath: "/roms/a.gb" }]);
  expect(scanMissingFiles(noOverride, NO_BLOBS, (p) => p === "/roms/a.gb")).toEqual([]);
});
test("scan: multiple systems report by config index", () => {
  const c = cfg([
    { platform: "gb", romPath: "/roms/a.gb" },
    // present
    { platform: "gb", romPath: "/roms/b.gb" }
    // missing
  ]);
  const missing = scanMissingFiles(c, NO_BLOBS, (p) => p === "/roms/a.gb");
  expect(missing).toEqual([{ systemIndex: 1, itemKind: "rom", path: "/roms/b.gb" }]);
});
test("relinkInConfig: repoints rom / sram in place; false for a bad index", () => {
  const c = cfg([{ platform: "gb", romPath: "/old/a.gb", savPath: "/old/a.sav" }]);
  expect(relinkInConfig(c, { systemIndex: 0, itemKind: "rom", path: "/old/a.gb" }, "/new/a.gb")).toBeTruthy();
  expect(c.systems[0].romPath).toBe("/new/a.gb");
  expect(relinkInConfig(c, { systemIndex: 0, itemKind: "sram", path: "/old/a.sav" }, "/new/a.sav")).toBeTruthy();
  expect(c.systems[0].savPath).toBe("/new/a.sav");
  expect(relinkInConfig(c, { systemIndex: 9, itemKind: "rom", path: "x" }, "y")).toBeFalsy();
});
test("autoFindSiblings: one located folder fixes the rest by basename", () => {
  const c = cfg([
    { platform: "gb", romPath: "/old/a.gb" },
    { platform: "gb", romPath: "/old/b.gb" }
  ]);
  const onDisk = /* @__PURE__ */ new Set(["/new/a.gb", "/new/b.gb"]);
  const fixed = autoFindSiblings(c, "/new", NO_BLOBS, (p) => onDisk.has(p));
  expect(fixed).toBe(2);
  expect(c.systems.map((s) => s.romPath)).toEqual(["/new/a.gb", "/new/b.gb"]);
});
