// Breakpoints + run-until-break against a REAL Mesen NES core, driven through the CLI session +
// Timeline. Proves the spec/09 breakpoint surface end-to-end: an execute breakpoint fires at a known PC,
// a read watchpoint fires on the MIDI FIFO access, the cycle cap returns broke=false, and a condition
// expression gates the break. $9FC4 is bliptoaster's `ed_fifo_busy` (LDA $40F1 / AND #$80 / RTS) - the MIDI
// FIFO poll the idle loop calls every pass, so an execute break there and a $40F1 read watchpoint both fire
// reliably; it's also the `midiIdleLoop` label in the committed resources/roms/bliptoaster.dbg. Y is 8
// throughout that routine (sampled stable across 25 separate hits), so "Y == 8" matches and "Y == 0" cannot.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("breakpoints fire on a real NES: execute PC, FIFO read watchpoint, cycle cap, conditions", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Warm the core so the 6502 is running its idle loop before we install breakpoints.
  renderTimeline(s, new Timeline(), { durationMs: 200, warmupMs: 1000 });

  // Execute breakpoint at the idle loop entry must fire exactly there.
  expect(s.backend.setBreakpoints(id, [{ type: "execute", start: 0x9fc4 }])).toBeTruthy();
  const exec = s.backend.runUntilBreak(id, 5_000_000);
  expect(exec.broke).toBeTruthy();
  expect(exec.pc).toBe(0x9fc4);
  expect(exec.breakpointId >= 0).toBeTruthy();

  // A read watchpoint on the MIDI FIFO ($40F1) — the idle loop polls it, so it must fire.
  expect(s.backend.setBreakpoints(id, [{ type: "read", start: 0x40f1 }])).toBeTruthy();
  const wp = s.backend.runUntilBreak(id, 5_000_000);
  expect(wp.broke).toBeTruthy();
  expect(wp.breakpointId >= 0).toBeTruthy();

  // A breakpoint on an address the CPU never executes → the cycle cap trips → broke=false.
  expect(s.backend.setBreakpoints(id, [{ type: "execute", start: 0xfffe }])).toBeTruthy();
  expect(s.backend.runUntilBreak(id, 200_000).broke).toBeFalsy();

  // A contradiction never matches — the breakpoint must not fire.
  s.backend.setBreakpoints(id, [{ type: "execute", start: 0x9fc4, condition: "1 == 0" }]);
  expect(s.backend.runUntilBreak(id, 500_000).broke).toBeFalsy();

  // Y is 8 throughout the FIFO poll — this condition matches and fires at $9FC4.
  s.backend.setBreakpoints(id, [{ type: "execute", start: 0x9fc4, condition: "Y == 8" }]);
  const cond = s.backend.runUntilBreak(id, 5_000_000);
  expect(cond.broke).toBeTruthy();
  expect(cond.pc).toBe(0x9fc4);
});
