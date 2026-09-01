// writeCpu (debugger-style memory poke) against a REAL Mesen NES core, round-tripped through readCpu.
// Proves the write counterpart of the spec/09 observe surface: a byte written into zero-page RAM reads
// back with the same value, and a write to a live id is served (true). (The mock ordering lives in the
// pure-TS suite; this is the native round-trip.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("writeCpu pokes NES zero-page RAM and readCpu reads it back", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Warm the core so it is activated (writeCpu/readCpu touch the live memory manager).
  renderTimeline(s, new Timeline(), { durationMs: 200, warmupMs: 1000 });

  // Round-trip: poke a byte into zero-page RAM ($0010), then peek it back.
  const ok = s.backend.writeCpu(id, 0x0010, 0x42);
  expect(ok).toBeTruthy();
  expect(s.backend.readCpu(id, 0x0010) === 0x42).toBeTruthy();

  // A second distinct value overwrites the first (proves it is a real write, not a fixed stub).
  expect(s.backend.writeCpu(id, 0x0010, 0xa5)).toBeTruthy();
  expect(s.backend.readCpu(id, 0x0010) === 0xa5).toBeTruthy();

  // Writing to a dead id is refused.
  expect(s.backend.writeCpu(9999, 0x0010, 0x00)).toBeFalsy();
});
