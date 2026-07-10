// packages/retroplug-greenfield/testing/harness.ts
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

// packages/retroplug-greenfield/src/dspRuntime.ts
function resolveSend() {
  const ns = globalThis[/* @__PURE__ */ Symbol.for("plugin")];
  if (!ns || typeof ns.__rpcSend !== "function")
    throw new Error("no native backend: globalThis[Symbol.for('plugin')].__rpcSend is missing");
  return ns.__rpcSend;
}
function createDspRuntime() {
  const send = resolveSend();
  let nextId = 1;
  const call = (method, ...params) => {
    const reply = send({ jsonrpc: "2.0", id: nextId++, method, params });
    if (reply == null) return void 0;
    if (reply.error) throw new Error(`rpc ${method}: [${reply.error.code}] ${reply.error.message}`);
    return reply.result;
  };
  const ints = (b) => Array.from(b);
  return {
    compileScript: (source) => {
      const r = call("compileScript", source);
      return r == null ? null : r;
    },
    loadScript: (bytecode) => call("dspLoadScript", ints(bytecode)),
    setConfig: (bytes) => call("dspSetConfig", ints(bytes)),
    runBlock: (midi, block2) => {
      const midiParam = midi.map((m) => ({ frame: m.frame, data: ints(m.data) }));
      const out = call("dspRunBlock", midiParam, block2);
      return out;
    }
  };
}

// packages/retroplug-greenfield/test-native/dsp-eachtick.test.ts
var CLOCK = `
function onBlock(input) {
  eachTick(24, function(tick, off) { emitMidiOut(off, [0xF8]); });
}
`;
var block = (ppq, playing) => ({
  frames: 22050,
  sampleRate: 44100,
  tempo: 120,
  ppqPosBlockStart: ppq,
  transportPlaying: playing
});
var allClocks = (out) => out.every((e) => e.data.length === 1 && e.data[0] === 248);
test("eachTick emits a drift-free 24-PPQN clock (24 / 24 / 0 across two blocks + a stopped control)", () => {
  const dsp = createDspRuntime();
  expect(dsp.loadScript(dsp.compileScript(CLOCK))).toBeTruthy();
  const b1 = dsp.runBlock([], block(0, true));
  expect(b1.length).toBe(24);
  expect(allClocks(b1)).toBeTruthy();
  const b2 = dsp.runBlock([], block(1, true));
  expect(b2.length).toBe(24);
  expect(allClocks(b2)).toBeTruthy();
  const stopped = dsp.runBlock([], block(2, false));
  expect(stopped.length).toBe(0);
});
