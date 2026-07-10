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

// packages/retroplug/src/midiRouting.ts
var MIDI_DATA_SIZE = 4;
function routeEvent(ev, mode, systemCount) {
  const data = ev.data;
  if (systemCount <= 0 || data.length === 0) return { targets: [], data };
  const status = data[0];
  const isSystemMsg = (status & 240) === 240;
  if (isSystemMsg || data.length > MIDI_DATA_SIZE) return { targets: allTargets(systemCount), data };
  const chan = status & 15;
  switch (mode) {
    case 0 /* SendToAll */:
      return { targets: allTargets(systemCount), data };
    case 1 /* FourChannelsPerInstance */:
      return { targets: [Math.floor(chan / 4) % systemCount], data };
    case 2 /* OneChannelPerInstance */:
      return { targets: [chan % systemCount], data };
    case 3 /* MidiChannelToInstance */:
      return { targets: [chan % systemCount], data: [status & 240, ...data.slice(1)] };
    default:
      return { targets: [], data };
  }
}
function routeBlock(events, mode, systemCount) {
  const inboxes = [];
  for (let i = 0; i < systemCount; i++) inboxes.push([]);
  for (const ev of events) {
    if (ev.data.length === 0) continue;
    const { targets, data } = routeEvent(ev, mode, systemCount);
    for (const t of targets) inboxes[t].push({ frame: ev.frame, data });
  }
  return inboxes;
}
function allTargets(n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(i);
  return out;
}

// packages/retroplug/test/midi/routing.test.ts
var noteOn = (chan, frame = 0) => ({ frame, data: [144 | chan, 60, 100] });
test("SendToAll: every system gets the event, channel preserved", () => {
  const r = routeEvent(noteOn(5), 0 /* SendToAll */, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([149, 60, 100]);
});
test("FourChannelsPerInstance: channels group by 4 into instances (mod n)", () => {
  const t = (c) => routeEvent(noteOn(c), 1 /* FourChannelsPerInstance */, 3).targets;
  expect(t(0)).toEqual([0]);
  expect(t(3)).toEqual([0]);
  expect(t(4)).toEqual([1]);
  expect(t(8)).toEqual([2]);
  expect(t(12)).toEqual([0]);
  expect(t(15)).toEqual([0]);
});
test("OneChannelPerInstance: channel mod n selects the instance", () => {
  const t = (c) => routeEvent(noteOn(c), 2 /* OneChannelPerInstance */, 3).targets;
  expect(t(0)).toEqual([0]);
  expect(t(3)).toEqual([0]);
  expect(t(4)).toEqual([1]);
  expect(t(2)).toEqual([2]);
});
test("MidiChannelToInstance: channel mod n selects, and the channel nibble is rewritten to 0", () => {
  const r = routeEvent(noteOn(5), 3 /* MidiChannelToInstance */, 3);
  expect(r.targets).toEqual([2]);
  expect(r.data).toEqual([144, 60, 100]);
});
test("system/realtime messages broadcast to all, ignoring the routing mode", () => {
  const clock = { frame: 10, data: [248] };
  const r = routeEvent(clock, 2 /* OneChannelPerInstance */, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([248]);
});
test("SysEx (size > 4) broadcasts to all, unchanged, even under a per-channel mode", () => {
  const sysex = { frame: 0, data: [144, 1, 2, 3, 4] };
  const r = routeEvent(sysex, 2 /* OneChannelPerInstance */, 3);
  expect(r.targets).toEqual([0, 1, 2]);
  expect(r.data).toEqual([144, 1, 2, 3, 4]);
});
test("edges: n=1 delivers to [0] for every mode; n=0 and a size-0 event yield no targets", () => {
  for (const m of [
    0 /* SendToAll */,
    1 /* FourChannelsPerInstance */,
    2 /* OneChannelPerInstance */,
    3 /* MidiChannelToInstance */
  ]) {
    expect(routeEvent(noteOn(7), m, 1).targets).toEqual([0]);
  }
  expect(routeEvent(noteOn(7), 0 /* SendToAll */, 0).targets).toEqual([]);
  expect(routeEvent({ frame: 0, data: [] }, 0 /* SendToAll */, 3).targets).toEqual([]);
});
test("routeBlock: fans a mixed block into per-system inboxes (skips size-0)", () => {
  const events = [
    noteOn(0, 0),
    // OneChannel → sys 0
    noteOn(4, 1),
    // OneChannel → sys 1
    { frame: 2, data: [] },
    // size-0 → skipped
    { frame: 3, data: [248] }
    // clock → all
  ];
  const inboxes = routeBlock(events, 2 /* OneChannelPerInstance */, 3);
  expect(inboxes.length).toBe(3);
  expect(inboxes[0]).toEqual([{ frame: 0, data: [144, 60, 100] }, { frame: 3, data: [248] }]);
  expect(inboxes[1]).toEqual([{ frame: 1, data: [148, 60, 100] }, { frame: 3, data: [248] }]);
  expect(inboxes[2]).toEqual([{ frame: 3, data: [248] }]);
});
test("routeBlock: a MidiChannelToInstance rewrite lands only on the target inbox", () => {
  const inboxes = routeBlock([noteOn(5)], 3 /* MidiChannelToInstance */, 3);
  expect(inboxes[0]).toEqual([]);
  expect(inboxes[1]).toEqual([]);
  expect(inboxes[2]).toEqual([{ frame: 0, data: [144, 60, 100] }]);
});
