// Execution trace + single-step against a REAL Mesen NES core, driven through the CLI session + Timeline.
// Proves the spec/09 trace surface end-to-end: enabling Mesen's per-instruction trace logger and running
// captures a non-empty stream of rows (each carrying a program-counter + formatted disassembly), and the
// step trio (stepInto/stepOver/stepOut) advances the core and returns a BreakInfo. Mirrors the legacy
// nes/observe.test.ts + nes/debug.test.ts assertions. (The mock counterpart is testing/mockBackend.ts.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { BreakInfo, TraceLine } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("trace logger captures the instruction stream + the step trio advances a real NES", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Enable the trace logger early, accumulate rows across the render, then read + single-step at the end.
  let enabled = false;
  let rows: TraceLine[] = [];
  // `null as BreakInfo | null` (not `: BreakInfo | null = null`) so the type stays the union at the reads
  // below — these are assigned inside the Timeline callbacks, which TS can't see running, and a plain
  // `= null` would narrow the reads to `null` (then `!= null` → `never`).
  let into = null as BreakInfo | null;
  let over = null as BreakInfo | null;
  let out = null as BreakInfo | null;
  let pcAfterSteps = -1;
  let pcLater = -1;

  const tl = new Timeline()
    .at(10, (sess) => (enabled = sess.backend.setTrace(id, true)))
    .at(300, (sess) => {
      rows = sess.backend.readTrace(id, 16);
      sess.backend.setTrace(id, false); // rows captured; a trace row per instruction is slow over a run
      into = sess.backend.stepInto(id);
      over = sess.backend.stepOver(id);
      // Before stepOut, make sure the CPU is INSIDE a subroutine: from the top-level idle loop stepOut has
      // no return frame and grinds the full ~50M-cycle cap (minutes). stepInto over a JSR pushes a return
      // address (SP drops), so step until SP falls below its current value — ROM-agnostic, no hardcoded
      // subroutine address. The idle loop polls the FIFO via a JSR, so this descends within a few steps.
      const sp = () => sess.backend.getCpuRegisters(id).find((r) => r.name === "sp")!.value;
      const spTop = sp();
      for (let i = 0; i < 256 && sp() >= spTop; i++) sess.backend.stepInto(id);
      out = sess.backend.stepOut(id);
      pcAfterSteps = sess.backend.getCpuRegisters(id).find((r) => r.name === "pc")!.value;
    })
    // Ordinary emulation must survive the step trio. A step installs a Mesen StepRequest, and a SPENT one
    // breaks on the next instruction; nothing outside the step call resumes from that, so if doStep fails to
    // disarm it (Debugger::Run) the core stops making progress the moment the render resumes. That is a HANG,
    // not a failed assertion - it spins at 100% CPU and takes the whole native suite with it - so this
    // sampled PC is a documentation of intent as much as a check.
    .at(380, (sess) => (pcLater = sess.backend.getCpuRegisters(id).find((r) => r.name === "pc")!.value));
  renderTimeline(s, tl, { durationMs: 400, warmupMs: 1000 });

  // setTrace succeeds on a live NES debug target.
  expect(enabled).toBeTruthy();

  // The trace captured rows; each carries a PC and a formatted disassembly line (mirrors observe.test.ts).
  expect(rows.length > 0).toBeTruthy();
  expect(rows[0].pc >= 0).toBeTruthy();
  expect(rows[0].text.length > 0).toBeTruthy();

  // stepInto reliably advances one instruction (mirrors legacy debug.test.ts): broke=true, breakpointId
  // of -1 (a step, not a breakpoint hit). stepOver/stepOut are wired the same way but their break depends
  // on the idle-loop call depth (stepOut can hit the cycle cap at the top level), so only assert the RPC
  // returns a clean BreakInfo: a numeric pc + the step's -1 breakpointId.
  expect(into != null && into!.broke).toBeTruthy();
  expect(into!.breakpointId === -1).toBeTruthy();
  expect(over != null && typeof over!.pc === "number" && over!.breakpointId === -1).toBeTruthy();
  expect(out != null && typeof out!.pc === "number" && out!.breakpointId === -1).toBeTruthy();

  // ...and the core kept running afterwards (see the note on the .at(380) sample above).
  expect(pcAfterSteps >= 0).toBeTruthy();
  expect(pcLater >= 0).toBeTruthy();
  expect(pcLater !== pcAfterSteps).toBeTruthy(); // emulation advanced past where the steps left it
});
