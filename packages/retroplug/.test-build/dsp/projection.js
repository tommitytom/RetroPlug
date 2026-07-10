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

// packages/retroplug/src/kernelProjection.ts
function projectKernelStructure(views, midiRouting) {
  return {
    project: [{ kind: "midi-routing", config: { mode: midiRouting } }],
    systems: views.map((v) => ({ id: v.id, pipeline: v.roles }))
  };
}

// packages/retroplug/test/dsp/projection.test.ts
function view(id, roles) {
  return {
    id,
    platform: "gb",
    core: "sameboy",
    romPath: "",
    savPath: "",
    savSuffix: 0,
    embedded: false,
    battery: false,
    focused: false,
    missing: false,
    settings: { gainDb: 0, reloadOnRomChange: false },
    roles
  };
}
test("projectKernelStructure: synthesizes the project midi-routing role from the routing mode", () => {
  const s = projectKernelStructure([], 2);
  expect(s.project).toEqual([{ kind: "midi-routing", config: { mode: 2 } }]);
  expect(s.systems).toEqual([]);
});
test("projectKernelStructure: each system's pipeline mirrors its roles in order", () => {
  const a = [
    { kind: "sameboy", config: { model: 9 } },
    { kind: "lsdj-sync", config: { mode: 1 } }
  ];
  const b = [{ kind: "sameboy", config: {} }, { kind: "mgb", config: {} }];
  const s = projectKernelStructure([view(1, a), view(2, b)], 0);
  expect(s.project).toEqual([{ kind: "midi-routing", config: { mode: 0 } }]);
  expect(s.systems).toEqual([
    { id: 1, pipeline: a },
    // order preserved: system role first, then the feature role
    { id: 2, pipeline: b }
  ]);
});
