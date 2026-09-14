// Breakpoints + run-until-break against a REAL Mesen NES core, driven through the CLI session +
// Timeline. Proves the spec/09 breakpoint surface end-to-end: a read watchpoint fires on the MIDI FIFO
// access, an execute breakpoint fires at a known PC, the cycle cap returns broke=false, and a condition
// expression gates the break in both directions.
//
// Every ROM-specific value here is DISCOVERED, not pinned. It used to hard-code $8D98 (the `LDA $40F1` FIFO
// poll, once labelled `midiIdleLoop`) and a register value sampled beside it; re-staging the ROM moved the
// instruction and deleted the label, and the test failed on an address that no longer means anything. What
// does survive a rebuild is the FIFO ADDRESS: a read watchpoint on $40F1 reports the PC of whatever polls
// it, and the register read at that break gives the condition its value. So the cart can be rebuilt freely
// and this still tests the breakpoint surface rather than a snapshot of one build's layout.
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

  // A read watchpoint on the MIDI FIFO ($40F1) — the idle loop polls it every pass, so it must fire. Its
  // PC is the poll instruction in THIS build, which the execute + condition legs below then use.
  expect(s.backend.setBreakpoints(id, [{ type: "read", start: 0x40f1 }])).toBeTruthy();
  const wp = s.backend.runUntilBreak(id, 5_000_000);
  expect(wp.broke).toBeTruthy();
  expect(wp.breakpointId >= 0).toBeTruthy();
  const POLL_PC = wp.pc;
  expect(POLL_PC >= 0x8000).toBeTruthy(); // in the PRG bank, i.e. a real instruction and not a stale 0

  // An execute breakpoint there fires exactly there.
  expect(s.backend.setBreakpoints(id, [{ type: "execute", start: POLL_PC }])).toBeTruthy();
  const exec = s.backend.runUntilBreak(id, 5_000_000);
  expect(exec.broke).toBeTruthy();
  expect(exec.pc).toBe(POLL_PC);
  expect(exec.breakpointId >= 0).toBeTruthy();
  // The registers AT that break give the condition legs a value that is true of this build by construction.
  const y = s.backend.getCpuRegisters(id).find((r) => r.name === "y")?.value;
  expect(y != null).toBeTruthy();

  // A breakpoint on an address the CPU never executes → the cycle cap trips → broke=false.
  expect(s.backend.setBreakpoints(id, [{ type: "execute", start: 0xfffe }])).toBeTruthy();
  expect(s.backend.runUntilBreak(id, 200_000).broke).toBeFalsy();

  // A contradiction never matches — the breakpoint must not fire.
  s.backend.setBreakpoints(id, [{ type: "execute", start: POLL_PC, condition: "1 == 0" }]);
  expect(s.backend.runUntilBreak(id, 500_000).broke).toBeFalsy();
  // Nor does a register condition the poll never satisfies (Y is `y` there, so `y + 1` cannot match).
  s.backend.setBreakpoints(id, [{ type: "execute", start: POLL_PC, condition: `Y == ${(y! + 1) & 0xff}` }]);
  expect(s.backend.runUntilBreak(id, 500_000).broke).toBeFalsy();

  // ...and the one that IS true of the poll fires, so the gate is proven in both directions.
  s.backend.setBreakpoints(id, [{ type: "execute", start: POLL_PC, condition: `Y == ${y!}` }]);
  const cond = s.backend.runUntilBreak(id, 5_000_000);
  expect(cond.broke).toBeTruthy();
  expect(cond.pc).toBe(POLL_PC);
  console.log(`[breakpoints] FIFO poll at $${POLL_PC.toString(16).toUpperCase()}, Y=${y} — discovered, not pinned`);
});
