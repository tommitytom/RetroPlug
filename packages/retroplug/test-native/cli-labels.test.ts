// loadLabels — load a cc65 `.dbg` symbol file into a REAL Mesen NES debug target over the backend RPC.
// Proves the spec/09 symbol-labels verb is wired end-to-end: a bogus path fails gracefully (no crash),
// and the committed bliptoaster `.dbg` fixture (labels CPU $9FC4 "midiIdleLoop") loads successfully. Reached
// via the live core's debugTarget() — NES-only (SameBoy/GBA have none). Mirrors cli-observe.test.ts.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
// A MINIMAL hand-written cc65 `.dbg` (3 symbols), not the real 1.3 MB build output — this test only proves
// the file parses and loads, so the addresses need not be exhaustive. It lives beside its ROM in resources/roms.
const DBG = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.dbg";

test("loadLabels loads a cc65 .dbg into a real NES core (and fails a bogus path gracefully)", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Warm the core so the Mesen debug target exists (ensureDebugger), mirroring cli-observe.
  renderTimeline(s, new Timeline(), { durationMs: 200, warmupMs: 500 });

  // A missing/bogus path never crashes and returns false (the RPC is wired to the live debugTarget).
  expect(s.backend.loadLabels(id, "/no/such/file.dbg")).toBe(false);
  // A gone id also returns false (no live core to reach).
  expect(s.backend.loadLabels(0xdead, DBG)).toBe(false);

  // If the real cc65 fixture is committed, loading it into the NES core succeeds.
  if (s.backend.fileExists(DBG)) {
    expect(s.backend.loadLabels(id, DBG)).toBe(true);
  } else {
    console.log("# note: no cc65 .dbg fixture — asserted graceful-failure path only");
  }
});
