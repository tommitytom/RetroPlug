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

// packages/retroplug/src/dspKernel.ts
function walkTicks(block2, resolution, nextTick, cb) {
  if (!block2.transport || block2.frames === 0 || resolution === 0 || block2.tempo <= 0) return nextTick;
  const beatLenSamples = block2.sampleRate * 60 / block2.tempo;
  const beatLenSamplesRes = beatLenSamples / resolution;
  const ppqRes = block2.ppqStart * resolution;
  const framePpqLen = block2.frames / beatLenSamples * resolution;
  const framePpqEnd = ppqRes + framePpqLen;
  if (nextTick < ppqRes - 1 || nextTick > framePpqEnd + 1) nextTick = Math.ceil(ppqRes);
  while (nextTick < framePpqEnd) {
    let offset = beatLenSamplesRes * (nextTick - ppqRes);
    if (offset < 0) offset = 0;
    if (offset >= block2.frames) offset = block2.frames - 1;
    cb(nextTick, Math.trunc(offset));
    nextTick++;
  }
  return nextTick;
}

// packages/retroplug/test/dsp/each-tick.test.ts
var block = (ppqStart, transport) => ({
  frames: 22050,
  sampleRate: 44100,
  tempo: 120,
  ppqStart,
  transport
});
test("walkTicks emits a drift-free 24-PPQN clock (24 / 24 / 0 across two blocks + a stopped control)", () => {
  const b1 = [];
  let nextTick = walkTicks(block(0, true), 24, 0, (t, o) => b1.push({ t, o }));
  expect(b1.length).toBe(24);
  expect(b1[0]).toEqual({ t: 0, o: 0 });
  expect(b1[23].t).toBe(23);
  expect(nextTick).toBe(24);
  const b2 = [];
  nextTick = walkTicks(block(1, true), 24, nextTick, (t) => b2.push(t));
  expect(b2.length).toBe(24);
  expect(b2[0]).toBe(24);
  expect(b2[23]).toBe(47);
  expect(nextTick).toBe(48);
  const stopped = [];
  const after = walkTicks(block(2, false), 24, nextTick, (t) => stopped.push(t));
  expect(stopped.length).toBe(0);
  expect(after).toBe(48);
});
