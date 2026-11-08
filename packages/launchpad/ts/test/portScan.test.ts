// PortScan, driven a millisecond at a time over a fake port API.
//
// The scan is the one place RetroPlug writes MIDI at a stranger's gear, and what it records decides which
// pair a control surface gets claimed on - so the cases here pin ATTRIBUTION (a reply belongs to the output
// that was being probed when it arrived, never the next one), what happens to ports that will not open, and
// that every port it opens is closed again. It never interprets a byte: the probe and the replies are
// opaque, exactly as they are to native.
import { test, expect } from "@retroplug/testing";
import { GAP_MS, MAX_REPLIES, PortScan, type MidiPortApi } from "../portScan";

const PROBE = [0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7];
const WINDOW = 300;

/** Ports by name. `answer` maps an output to what comes back on which input when it is written to. */
function rig(opts: { answer?: Record<string, { input: string; bytes: number[] }>; busy?: string[] } = {}) {
  const open = new Map<number, { input: boolean; name: string; queue: number[][] }>();
  const log: string[] = [];
  let next = 1;
  let inputCalls = 0;
  const openOne = (input: boolean, name: string): number => {
    if (opts.busy?.includes(name)) return 0;
    const h = next++;
    open.set(h, { input, name, queue: [] });
    log.push(`open ${input ? "in" : "out"} ${name}`);
    return h;
  };
  const api: MidiPortApi = {
    openInputs(names) {
      inputCalls++;
      return names.map((n) => openOne(true, n));
    },
    openOutput: (name) => openOne(false, name),
    send(h, bytes) {
      const p = open.get(h)!;
      log.push(`send ${p.name} ${bytes.length}`);
      const a = opts.answer?.[p.name];
      if (a) for (const q of open.values()) if (q.input && q.name === a.input) q.queue.push(a.bytes);
      return true;
    },
    read(h) {
      const p = open.get(h)!;
      const out = p.queue;
      p.queue = [];
      return out;
    },
    close(h) {
      log.push(`close ${open.get(h)!.name}`);
      open.delete(h);
    },
  };
  const hear = (input: string, bytes: number[]) => {
    for (const q of open.values()) if (q.input && q.name === input) q.queue.push(bytes);
  };
  return { api, log, open, hear, inputCalls: () => inputCalls };
}

function run(scan: PortScan, from = 0): number {
  let t = from;
  while (scan.step(t)) t += 10;
  return t;
}

test("probes every output in turn, one answer window each, and closes everything", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["A in", "B in"], ["A out", "B out"], PROBE, WINDOW);
  const end = run(scan);
  expect(r.log).toEqual([
    "open in A in", "open in B in",
    "open out A out", "send A out 6", "close A out",
    "open out B out", "send B out 6", "close B out",
    "close A in", "close B in",
  ]);
  expect(r.open.size).toBe(0);
  expect(end >= 2 * WINDOW).toBe(true);
  expect(scan.status()).toEqual({ ...scan.status(), busy: false, done: true, phase: "", skipped: 0, replies: [] });
});

test("a clock that is not finite cannot wedge the scan, or hold its inputs open", () => {
  // The scan is stepped from the UI's frame poll with whatever its clock says. A NaN would stop the elapsed time
  // advancing for ever (every window waits), and an infinity would close each window the instant it opened: either way
  // the scan never ends and the loose inputs stay open, which on Windows holds the shared stream's inputs down too.
  const r = rig();
  const scan = new PortScan(r.api, ["A in"], ["A out", "B out"], PROBE, WINDOW);
  expect(scan.step(NaN)).toBe(true);
  expect(scan.step(Infinity)).toBe(true);
  expect(scan.step(-Infinity)).toBe(true);
  expect(r.log).toEqual(["open in A in"]); // nothing was probed on a clock it cannot read
  // A real clock then carries the scan to its end.
  expect(run(scan, 1000) > 1000).toBe(true);
  expect(scan.status().busy).toBe(false);
  expect(r.open.size).toBe(0);
});

test("an answer is tagged with the output probed AND the input it came back on", () => {
  // The two differ - that pairing is the thing a scan exists to discover.
  const r = rig({ answer: { "IF Port B": { input: "IF Port A", bytes: [0xf0, 1, 0xf7] } } });
  const scan = new PortScan(r.api, ["Keys", "IF Port A"], ["Keys", "IF Port B"], PROBE, WINDOW);
  run(scan);
  expect(scan.status().replies).toEqual([{ output: "IF Port B", input: "IF Port A", bytes: [0xf0, 1, 0xf7] }]);
});

test("an answer inside its window counts even if it is only read as the window closes", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["IF in"], ["Out 1", "Out 2"], PROBE, WINDOW);
  scan.step(0); // Out 1 probed
  scan.step(WINDOW - 1);
  r.hear("IF in", [0xf0, 0x11, 0xf7]); // just inside Out 1's window; nothing reads it until the next step
  scan.step(WINDOW); // reads FIRST, then closes Out 1's window
  expect(scan.status().replies.map((x) => x.output)).toEqual(["Out 1"]);
});

test("a late answer lands in the quiet between windows and is dropped, never credited to the next port", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["IF in"], ["Out 1", "Out 2"], PROBE, WINDOW);
  scan.step(0);
  scan.step(WINDOW); // Out 1's window closes; Out 2 is NOT probed yet
  expect(r.log.filter((l) => l.startsWith("send"))).toEqual(["send Out 1 6"]);
  r.hear("IF in", [0xf0, 0x11, 0xf7]); // Out 1's device, late
  scan.step(WINDOW + GAP_MS - 1);
  scan.step(WINDOW + GAP_MS); // the quiet is over: Out 2 goes out now
  expect(scan.status().phase).toBe("Probing Out 2");
  run(scan, WINDOW + GAP_MS + 10);
  expect(scan.status().replies).toEqual([]);
});

test("talk from before the first probe, or after the last window, is not an answer", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["IF in"], ["Out 1"], PROBE, WINDOW);
  r.hear("IF in", [0x90, 60, 1]); // the inputs are open before any probe goes out
  run(scan);
  expect(scan.status().replies).toEqual([]);
});

test("System Real-Time is the stream within the stream, never a reply", () => {
  const r = rig({ answer: { Out: { input: "In", bytes: [0xf8] } } });
  const scan = new PortScan(r.api, ["In"], ["Out"], PROBE, WINDOW);
  scan.step(0);
  r.hear("In", [0xfe]);
  r.hear("In", [0xfa]);
  run(scan, 10);
  expect(scan.status().replies).toEqual([]);
});

test("ports that will not open are counted and stepped over, not fatal", () => {
  const r = rig({ busy: ["Held in", "Held out"], answer: { "Free out": { input: "Free in", bytes: [0xf0, 0xf7] } } });
  const scan = new PortScan(r.api, ["Held in", "Free in"], ["Held out", "Free out"], PROBE, WINDOW);
  run(scan);
  expect(scan.status().skipped).toBe(2);
  expect(scan.status().replies.length).toBe(1);
});

test("a rig with nothing to probe finishes on its first step", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["In"], [], PROBE, WINDOW);
  expect(scan.step(0)).toBe(false);
  expect(scan.status().done).toBe(true);
  expect(r.open.size).toBe(0);
});

test("only SysEx is a reply, so a fader ride cannot hide the device", () => {
  // A controller sending CCs every frame must not fill the reply cap before the Launchpad, asked third, has
  // answered.
  const DEVICE = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0, 0, 0, 1, 2, 3, 0xf7];
  const r = rig({ answer: { "LP out": { input: "LP in", bytes: DEVICE } } });
  const scan = new PortScan(r.api, ["Faders", "LP in"], ["Faders", "Other", "LP out"], PROBE, WINDOW);
  for (let t = 0; scan.step(t); t += 16) {
    for (let k = 0; k < 5; k++) r.hear("Faders", [0xb0, 7, (t + k) & 0x7f]);
    r.hear("Faders", [0x90, 60, 100]);
    r.hear("Faders", [0xd0, 40]);
  }
  expect(scan.status().replies).toEqual([{ output: "LP out", input: "LP in", bytes: DEVICE }]);
});

test("replies are capped, so a port flooding SysEx cannot grow the list without bound", () => {
  const r = rig();
  const scan = new PortScan(r.api, ["Chatty"], ["Out"], PROBE, WINDOW);
  scan.step(0);
  for (let i = 0; i < MAX_REPLIES + 20; i++) r.hear("Chatty", [0xf0, i & 0x7f, 0xf7]);
  run(scan, 10);
  expect(scan.status().replies.length).toBe(MAX_REPLIES);
});

test("every input is opened in ONE call, so a host steps its own stream aside once per scan", () => {
  const r = rig({ busy: ["Held A", "Held B"] });
  const scan = new PortScan(r.api, ["Held A", "Held B", "Free"], ["Out"], PROBE, WINDOW);
  run(scan);
  expect(r.inputCalls()).toBe(1);
  expect(scan.status().skipped).toBe(2);
});

test("a port API that throws ends the scan with the error, and every port it opened is closed", () => {
  const r = rig();
  const api: MidiPortApi = { ...r.api, read: () => { throw new Error("device vanished"); } };
  const scan = new PortScan(api, ["A", "B"], ["Out"], PROBE, WINDOW);
  expect(scan.step(0)).toBe(false);
  expect(scan.status().error).toBe("device vanished");
  expect(scan.status().done).toBe(true);
  expect(r.open.size).toBe(0); // a host would otherwise keep its shared MIDI inputs suspended until quit
});

test("cancel closes every port and keeps what was gathered", () => {
  const r = rig({ answer: { "Out 1": { input: "In", bytes: [0xf0, 0xf7] } } });
  const scan = new PortScan(r.api, ["In"], ["Out 1", "Out 2"], PROBE, WINDOW);
  scan.step(0);
  scan.step(5);
  scan.cancel();
  expect(r.open.size).toBe(0);
  expect(scan.status().busy).toBe(false);
  expect(scan.status().replies.length).toBe(1);
});

test("a follow-up is asked on the SAME pair while it is open, and its answer gets a window of its own", () => {
  // How a scan confirms WHAT answered without reopening anything: reopening would cost a Windows host another
  // round of stepping its MIDI stream aside, and a port grabbed in between would make a real device look silent.
  const r = rig({ answer: { "IF out": { input: "IF in", bytes: [0xf0, 0x7e, 0x01, 0xf7] } } });
  const ASK = [0xf0, 0x00, 0x20, 0x29, 0x02, 0x0e, 0x00, 0xf7];
  const scan = new PortScan(r.api, ["IF in"], ["IF out", "Other"], PROBE, WINDOW, {
    followUp: (reply) => (reply.bytes[2] === 0x01 ? [ASK] : []),
  });
  scan.step(0); // the inquiry goes out and is answered at once
  scan.step(WINDOW - 10); // the reply is read, and the follow-up sent - restarting the window
  expect(r.log.filter((l) => l.startsWith("send"))).toEqual(["send IF out 6", "send IF out 8"]);
  r.hear("IF in", [0xf0, 0x00, 0x20, 0x29, 0x02, 0x0e, 0x00, 0x00, 0x00, 0xf7]); // the model answers late-ish
  scan.step(WINDOW + 50); // past the ORIGINAL window, inside the restarted one
  expect(scan.status().phase).toBe("Probing IF out");
  run(scan, WINDOW + 60);
  expect(scan.status().replies.some((x) => x.output === "IF out" && x.bytes.length === 10)).toBe(true);
});

test("a follow-up goes out once per output, however many inputs the device was heard on", () => {
  const r = rig();
  const ASK = [0xf0, 0x01, 0xf7];
  let asked = 0;
  const api: MidiPortApi = { ...r.api, send: (h, b) => { if (b.length === 3) asked++; return r.api.send(h, b); } };
  const scan = new PortScan(api, ["A", "B"], ["Out"], PROBE, WINDOW, { followUp: () => [ASK] });
  scan.step(0);
  r.hear("A", [0xf0, 0x7e, 0xf7]); // a merge: the same answer on two inputs
  r.hear("B", [0xf0, 0x7e, 0xf7]);
  run(scan, 10);
  expect(asked).toBe(1);
});

test("a status handed out is never mutated by a later step", () => {
  // Held at EVERY step, not just the first: a mutation once a probe is under way is the case that matters.
  const r = rig({ busy: ["Gone"], answer: { Out: { input: "In", bytes: [0xf0, 0xf7] } } });
  const scan = new PortScan(r.api, ["In"], ["Gone", "Out"], PROBE, WINDOW);
  const held: { status: ReturnType<PortScan["status"]>; snapshot: string }[] = [];
  for (let t = 0; ; t += 10) {
    const st = scan.status();
    held.push({ status: st, snapshot: JSON.stringify(st) });
    if (!scan.step(t)) break;
  }
  held.push({ status: scan.status(), snapshot: JSON.stringify(scan.status()) });
  for (const h of held) expect(JSON.stringify(h.status)).toBe(h.snapshot);
  expect(scan.status().replies.length).toBe(1);
});

test("the version strictly increases on every change - a probe, a reply, the end", () => {
  // A poller re-renders on version alone, so a change that does not move it freezes the Settings row.
  const r = rig({ answer: { "Out 1": { input: "In", bytes: [0xf0, 0x01, 0xf7] } } });
  const scan = new PortScan(r.api, ["In"], ["Out 1", "Out 2"], PROBE, WINDOW);
  let last = scan.status();
  let changes = 0;
  for (let t = 0; ; t += 10) {
    const busy = scan.step(t);
    const now = scan.status();
    if (now !== last) {
      expect(now.version > last.version).toBe(true);
      changes++;
    } else {
      expect(now.version).toBe(last.version);
    }
    last = now;
    if (!busy) break;
  }
  expect(changes >= 5).toBe(true); // probe 1, reply, window close, probe 2, window close, finish
});

test("one close that throws does not leave the other ports open", () => {
  const r = rig();
  let first = true;
  const api: MidiPortApi = {
    ...r.api,
    close(h) {
      if (first) { first = false; throw new Error("close failed"); }
      r.api.close(h);
    },
  };
  const scan = new PortScan(api, ["A", "B", "C"], [], PROBE, WINDOW);
  scan.step(0); // nothing to probe: finishes, closing all three inputs
  expect(scan.status().done).toBe(true);
  expect(scan.status().error).toBe("close failed");
  expect(r.open.size).toBe(1); // only the one whose close threw; the other two closed
});

test("a wall clock stepping backwards neither stalls nor rushes the scan", () => {
  // An NTP correction mid-scan. Time measured off the raw clock would hold the window open until the clock
  // caught up - an hour, for an hour's step - with every input held the whole time.
  const r = rig();
  const scan = new PortScan(r.api, ["In"], ["Out 1", "Out 2"], PROBE, WINDOW);
  scan.step(1_000_000);
  scan.step(1_000_000 + 100);
  scan.step(1_000_000 - 3_600_000); // back an hour: no time passes
  expect(scan.status().phase).toBe("Probing Out 1");
  let t = 1_000_000 - 3_600_000;
  let steps = 0;
  while (scan.step((t += 10))) steps++;
  expect(steps < 100).toBe(true); // finished in about the scan's own length, not an hour
});
