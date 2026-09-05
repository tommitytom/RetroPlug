// drainEvents against a REAL Mesen NES core: each MIDI note-on drives a burst of APU register writes
// ($4000-$4013) that Mesen's event viewer logs. This proves the drainEvents RPC end-to-end AND its two
// contracts: every event carries a `frame` (the event manager's own counter), and the call actually DRAINS -
// each event is returned exactly once, so a dense poll never sees a repeat and needs no dedupe key. Before
// this, every call returned the whole retained frame pair and a 5 ms poll saw each event ~3 times; the
// only dedupe key available (scanline:cycle:address) collapsed writes landing on the same dot in
// consecutive frames, which a deterministic idle loop does (BlipToaster's HARNESS-NOTES 2.1).
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { DebugEvent } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

const key = (e: DebugEvent) => `${e.type}:${e.operationType}:${e.frame}:${e.scanline}:${e.cycle}:${e.address}`;

test("drainEvents captures APU register-write events, each stamped with a frame, and never repeats one", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // The event viewer only logs while Mesen's debugger is live, and it's initialised lazily on first use.
  // Warm it up early, fire a run of ch1 notes (each on/off writes the APU regs), and poll every few ms.
  const polls: DebugEvent[][] = [];
  const tl = new Timeline().at(10, (sess) => sess.backend.drainEvents(id)); // warm-up: init the debugger
  for (const onset of [150, 300, 450, 600]) tl.note(onset, 60, { channel: 1, durationMs: 100 });
  for (let t = 140; t <= 760; t += 5) tl.at(t, (sess) => polls.push(sess.backend.drainEvents(id)));
  renderTimeline(s, tl, { durationMs: 820, warmupMs: 1000 });

  const events = polls.flat();
  expect(events.length, "events over the run").toBeGreaterThan(0);

  // Every event has a sane shape: numeric fields, a type ordinal in the DebugEventType range (0-7), an
  // operationType in the MemoryOperationType range (0-9), and a frame stamp.
  for (const e of events) {
    expect(typeof e.address === "number").toBeTruthy();
    expect(typeof e.value === "number").toBeTruthy();
    expect(typeof e.programCounter === "number").toBeTruthy();
    expect(typeof e.frame === "number" && e.frame >= 0).toBeTruthy();
    expect(e.type >= 0 && e.type <= 7).toBeTruthy();
    expect(e.operationType >= 0 && e.operationType <= 9).toBeTruthy();
  }

  // At least one event is a WRITE to the CPU register page ($2000-$401F: PPU + APU/IO regs) — the note-on
  // APU writes ($4000-$4013) land here. type 0 = Register, operationType 1 = Write.
  const regWrite = events.some(
    (e) => e.type === 0 && e.operationType === 1 && e.address >= 0x2000 && e.address <= 0x401f
  );
  expect(regWrite).toBeTruthy();

  // The drain contract: across ~125 polls at 5 ms (about 3 per frame) no event comes back twice, so the raw
  // concatenation IS the distinct set. Also the frame stamps only ever move forward.
  const seen = new Set<string>();
  let dupes = 0;
  let lastFrame = -1;
  for (const e of events) {
    const k = key(e);
    if (seen.has(k)) dupes++;
    seen.add(k);
    expect(e.frame, "frame order").toBeGreaterThanOrEqual(lastFrame);
    lastFrame = e.frame;
  }
  expect(dupes, "repeated events across polls").toBe(0);
  // ~620 ms of NTSC is ~37 frames; the stamps must span most of that (the NMI writes $2000 every frame).
  const frames = new Set(events.map((e) => e.frame));
  expect(frames.size, "distinct frames seen").toBeGreaterThan(25);
});

test("a second drain with no time elapsed is empty; a slow poll gets at most the two retained frames", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  let first: DebugEvent[] = [], again: DebugEvent[] = [], slow: DebugEvent[] = [];
  const tl = new Timeline()
    .at(10, (sess) => sess.backend.drainEvents(id))
    .note(100, 60, { channel: 1, durationMs: 300 })
    // Two drains at the same instant: the second has nothing new.
    .at(200, (sess) => (first = sess.backend.drainEvents(id)))
    .at(200, (sess) => (again = sess.backend.drainEvents(id)))
    // 100 ms (~6 frames) later: the manager retains only the frame in progress + the previous one, so this
    // poll sees at most two distinct frames - a slow poll loses whole frames, it never repeats them.
    .at(300, (sess) => (slow = sess.backend.drainEvents(id)));
  renderTimeline(s, tl, { durationMs: 400, warmupMs: 1000 });

  expect(first.length, "first drain").toBeGreaterThan(0);
  expect(again.length, "immediate re-drain").toBe(0);
  expect(slow.length, "slow poll").toBeGreaterThan(0);
  const slowFrames = new Set(slow.map((e) => e.frame));
  expect(slowFrames.size, "frames in a slow poll").toBeLessThanOrEqual(2);
  // ...and those frames are strictly after the ones the 200 ms drain returned.
  const lastFirst = Math.max(...first.map((e) => e.frame));
  expect(Math.min(...slow.map((e) => e.frame)), "slow poll's oldest frame").toBeGreaterThan(lastFirst);
});
