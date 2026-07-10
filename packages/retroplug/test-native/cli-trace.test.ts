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
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

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
  let into: BreakInfo | null = null;
  let over: BreakInfo | null = null;
  let out: BreakInfo | null = null;

  const tl = new Timeline()
    .at(10, (sess) => (enabled = sess.backend.setTrace(id, true)))
    .at(300, (sess) => {
      rows = sess.backend.readTrace(id, 16);
      into = sess.backend.stepInto(id);
      over = sess.backend.stepOver(id);
      out = sess.backend.stepOut(id);
      sess.backend.setTrace(id, false);
    });
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
});
