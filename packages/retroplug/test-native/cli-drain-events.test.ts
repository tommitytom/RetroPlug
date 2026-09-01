// drainEvents against a REAL Mesen NES core: each MIDI note-on drives a burst of APU register writes
// ($4000-$4013) that Mesen's event viewer logs for that frame. This proves the drainEvents RPC
// end-to-end — a non-empty array of sanely-shaped register-write events. Events are frame-scoped (cleared
// each PPU frame) and this ROM only touches registers at note boundaries, so we fire several notes and
// poll drainEvents densely, accumulating the catches. (No legacy test exists; we assert shape + non-empty
// register writes rather than exact per-frame values, which are timing dependent.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { DebugEvent } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("drainEvents captures APU register-write events from a real NES", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // The event viewer only logs while Mesen's debugger is live, and it's initialised lazily on first use.
  // Warm it up early, fire a run of ch1 notes (each on/off writes the APU regs), and poll every few ms so
  // at least one poll lands on a write-bearing frame.
  let events: DebugEvent[] = [];
  const tl = new Timeline().at(10, (sess) => sess.backend.drainEvents(id)); // warm-up: init the debugger
  for (const onset of [150, 300, 450, 600]) tl.note(onset, 60, { channel: 1, durationMs: 100 });
  for (let t = 140; t <= 760; t += 8) {
    tl.at(t, (sess) => {
      const e = sess.backend.drainEvents(id);
      if (e.length > 0) events = events.concat(e);
    });
  }
  renderTimeline(s, tl, { durationMs: 820, warmupMs: 1000 });

  // At least one register event was logged across the run.
  expect(events.length > 0).toBeTruthy();

  // Every event has a sane shape: numeric fields, a type ordinal in the DebugEventType range (0-7), and
  // an operationType in the MemoryOperationType range (0-9).
  for (const e of events) {
    expect(typeof e.address === "number").toBeTruthy();
    expect(typeof e.value === "number").toBeTruthy();
    expect(typeof e.programCounter === "number").toBeTruthy();
    expect(e.type >= 0 && e.type <= 7).toBeTruthy();
    expect(e.operationType >= 0 && e.operationType <= 9).toBeTruthy();
  }

  // At least one event is a WRITE to the CPU register page ($2000-$401F: PPU + APU/IO regs) — the note-on
  // APU writes ($4000-$4013) land here. type 0 = Register, operationType 1 = Write.
  const regWrite = events.some(
    (e) => e.type === 0 && e.operationType === 1 && e.address >= 0x2000 && e.address <= 0x401f
  );
  expect(regWrite).toBeTruthy();
});
