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

// packages/retroplug/src/projectBinaries.ts
var PROJECT_JSON = "project.json";
var romKey = (i) => `systems/${i}/rom`;
var sramKey = (i) => `systems/${i}/sram`;
var stateKey = (i) => `systems/${i}/state`;
function blobKeysFromEntries(entries) {
  const out = /* @__PURE__ */ new Set();
  for (const e of entries) if (e.name !== PROJECT_JSON) out.add(e.name);
  return out;
}
function partitionEntries(entries) {
  let config = null;
  const blobs = /* @__PURE__ */ new Map();
  for (const e of entries) {
    if (e.name === PROJECT_JSON) config = e.bytes;
    else blobs.set(e.name, e.bytes);
  }
  return { config, blobs };
}

// packages/retroplug/test/project/binaries.test.ts
test("keys: the per-system blob key contract (shared with native)", () => {
  expect(PROJECT_JSON).toBe("project.json");
  expect(romKey(0)).toBe("systems/0/rom");
  expect(sramKey(1)).toBe("systems/1/sram");
  expect(stateKey(2)).toBe("systems/2/state");
});
test("partition: splits project.json from the blob map; blobKeys excludes it", () => {
  const entries = [
    { name: PROJECT_JSON, bytes: new Uint8Array([1, 2]) },
    { name: stateKey(0), bytes: new Uint8Array([3]) },
    { name: sramKey(0), bytes: new Uint8Array([4]) }
  ];
  const { config, blobs } = partitionEntries(entries);
  expect(config).toEqual(new Uint8Array([1, 2]));
  expect([...blobs.keys()].sort()).toEqual(["systems/0/sram", "systems/0/state"]);
  expect(blobs.get(stateKey(0))).toEqual(new Uint8Array([3]));
  expect([...blobKeysFromEntries(entries)].sort()).toEqual(["systems/0/sram", "systems/0/state"]);
});
test("partition: an archive without project.json yields config null", () => {
  const { config, blobs } = partitionEntries([{ name: stateKey(0), bytes: new Uint8Array([9]) }]);
  expect(config).toBe(null);
  expect(blobs.size).toBe(1);
});
