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

// packages/retroplug/src/keyCodes.ts
var BUTTON_VALUE = {
  Right: 0,
  Left: 1,
  Up: 2,
  Down: 3,
  A: 4,
  B: 5,
  Select: 6,
  Start: 7
};
var KEY_BACKSPACE = 8;
var KEY_TAB = 9;
var KEY_ENTER = 13;
var KEY_ESCAPE = 27;
var KEY_LEFT = 57397;
var KEY_UP = 57398;
var KEY_RIGHT = 57399;
var KEY_DOWN = 57400;
var KEY_SHIFT_L = 57425;
var KEY_SHIFT_R = 57426;
var DPF_TO_KEY_NAME = {
  [KEY_BACKSPACE]: "Backspace",
  [KEY_TAB]: "Tab",
  [KEY_ENTER]: "Enter",
  [KEY_ESCAPE]: "Escape",
  [KEY_LEFT]: "Left",
  [KEY_UP]: "Up",
  [KEY_RIGHT]: "Right",
  [KEY_DOWN]: "Down",
  [KEY_SHIFT_L]: "ShiftL",
  [KEY_SHIFT_R]: "ShiftR"
};

// packages/retroplug/cli/timeline.ts
var Button = { ...BUTTON_VALUE, L: 8, R: 9 };
var statusFor = (base, channel = 1) => base | channel - 1 & 15;
var noteOnBytes = (note, o) => [statusFor(144, o?.channel), note & 127, (o?.velocity ?? 100) & 127];
var noteOffBytes = (note, o) => [statusFor(128, o?.channel), note & 127, 0];
var Timeline = class {
  constructor() {
    this.events = [];
  }
  push(ev) {
    this.events.push(ev);
    return this;
  }
  /** Stage a raw MIDI message (≤4 bytes) — global host MIDI, fanned to systems by the routing role. */
  midi(ms, bytes) {
    return this.push({ ms, kind: "midi", bytes });
  }
  noteOn(ms, note, opts) {
    return this.midi(ms, noteOnBytes(note, opts));
  }
  noteOff(ms, note, opts) {
    return this.midi(ms, noteOffBytes(note, opts));
  }
  /** A note: noteOn at `ms`, noteOff at `ms + durationMs`. Channel 1-based (default 1), velocity default 100. */
  note(ms, note, opts) {
    return this.noteOn(ms, note, opts).noteOff(ms + opts.durationMs, note, opts);
  }
  /** Press or release `button` on `system` at `ms`. */
  press(ms, system, button, down) {
    return this.push({ ms, kind: "press", system, button, down });
  }
  /** Tap `button` on `system`: down at `ms`, up at `ms + holdMs` (default 50). */
  tap(ms, system, button, opts) {
    const hold = opts?.holdMs ?? 50;
    return this.press(ms, system, button, true).press(ms + hold, system, button, false);
  }
  bpm(ms, bpm) {
    return this.push({ ms, kind: "bpm", bpm });
  }
  transport(ms, running) {
    return this.push({ ms, kind: "transport", running });
  }
  screenshot(ms, system, path) {
    return this.push({ ms, kind: "screenshot", system, path });
  }
  /** Run `fn` against the live Session at `ms` — the render advances to `ms` first, so `fn` observes
   *  the core at exactly that time. This is the observe/assert hook: read APU/CPU/memory and `expect`
   *  on it (`s.backend.getApuState(id)`, `readCpu`, `getCpuRegisters`). */
  at(ms, fn) {
    return this.push({ ms, kind: "at", fn });
  }
  /** The events flattened to a stable ms-sorted list — insertion order breaks ties, so a same-ms noteOn
   *  precedes its noteOff and a tap's down precedes its up. Pure; touches no engine. */
  build() {
    return this.events.map((ev, i) => ({ ev, i })).sort((a, b) => a.ev.ms - b.ev.ms || a.i - b.i).map(({ ev }) => ev);
  }
};

// packages/retroplug/test/cli/timeline.test.ts
test("build() stable-sorts events by ms; insertion order breaks ties", () => {
  const evs = new Timeline().midi(500, [144, 62, 100]).midi(0, [144, 60, 100]).midi(0, [145, 64, 100]).build();
  expect(evs.map((e) => e.ms)).toEqual([0, 0, 500]);
  expect(evs[0].bytes).toEqual([144, 60, 100]);
  expect(evs[1].bytes).toEqual([145, 64, 100]);
});
test("note expands to noteOn + noteOff with the right bytes and timing", () => {
  const evs = new Timeline().note(100, 60, { durationMs: 400 }).build();
  expect(evs.length).toBe(2);
  expect(evs[0]).toEqual({ ms: 100, kind: "midi", bytes: [144, 60, 100] });
  expect(evs[1]).toEqual({ ms: 500, kind: "midi", bytes: [128, 60, 0] });
});
test("channel is 1-based \u2192 the status low nibble; velocity honored", () => {
  const on = new Timeline().noteOn(0, 67, { channel: 2, velocity: 64 }).build()[0];
  expect(on.bytes).toEqual([145, 67, 64]);
  const off = new Timeline().noteOff(0, 67, { channel: 16 }).build()[0];
  expect(off.bytes).toEqual([143, 67, 0]);
});
test("tap expands to a down then an up at ms + holdMs (default 50)", () => {
  const evs = new Timeline().tap(200, 3, Button.A, { holdMs: 80 }).build();
  expect(evs).toEqual([
    { ms: 200, kind: "press", system: 3, button: 4, down: true },
    { ms: 280, kind: "press", system: 3, button: 4, down: false }
  ]);
  expect(new Timeline().tap(0, 1, Button.Start).build()[1].ms).toBe(50);
});
test("at() records a scheduled callback that sorts by ms; build() never invokes it", () => {
  const seen = [];
  const evs = new Timeline().midi(0, [144, 60, 100]).at(200, () => seen.push("probe")).midi(100, [128, 60, 0]).build();
  expect(evs.map((e) => e.kind)).toEqual(["midi", "midi", "at"]);
  expect(evs.map((e) => e.ms)).toEqual([0, 100, 200]);
  expect(seen).toEqual([]);
  evs[2].fn();
  expect(seen).toEqual(["probe"]);
});
test("raw midi / bpm / transport / screenshot pass through as typed events", () => {
  const evs = new Timeline().midi(0, [176, 1, 64]).bpm(10, 140).transport(20, true).screenshot(30, 7, "/tmp/x.png").build();
  expect(evs).toEqual([
    { ms: 0, kind: "midi", bytes: [176, 1, 64] },
    { ms: 10, kind: "bpm", bpm: 140 },
    { ms: 20, kind: "transport", running: true },
    { ms: 30, kind: "screenshot", system: 7, path: "/tmp/x.png" }
  ]);
});
