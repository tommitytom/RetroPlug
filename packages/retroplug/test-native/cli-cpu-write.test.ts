// Live-core debug WRITES / control-flow (setCpuRegister / runUntilPc) against a REAL Mesen NES core,
// driven through the CLI session + Timeline. Proves the spec/09 mutate surface end-to-end: a register
// write is observable via getCpuRegisters, and runUntilPc returns a boolean (false when the cap trips on
// an address the CPU never executes). Known-good values mirror the legacy nes/cpu.test.ts.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

test("debug writes mutate a real NES: setCpuRegister is observable, runUntilPc returns a boolean", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Warm the core so the 6502 is running real code before we poke it.
  renderTimeline(s, new Timeline(), { durationMs: 200, warmupMs: 1000 });

  // Write A = 0x42 and observe it in the register file (canonical lower-case "a").
  expect(s.backend.setCpuRegister(id, "a", 0x42)).toBeTruthy();
  const a = s.backend.getCpuRegisters(id).find((r) => r.name === "a");
  expect(a != null && a.value === 0x42).toBeTruthy();

  // An unknown register name is rejected.
  expect(s.backend.setCpuRegister(id, "nope", 0)).toBeFalsy();

  // runUntilPc returns a boolean; 0x0000 is zero-page RAM the CPU won't execute, so the cap trips → false.
  const hit = s.backend.runUntilPc(id, 0x0000, 100000);
  expect(typeof hit === "boolean").toBeTruthy();
  expect(hit).toBeFalsy();
});
