// loadLabels + symbolAddress — load a cc65 `.dbg` symbol file into a REAL Mesen NES debug target over the
// backend RPC, then resolve names from it. Proves the spec/09 symbol-labels verb is wired end-to-end: a
// bogus path fails gracefully (no crash), the committed bliptoaster `.dbg` fixture loads, and afterwards a
// symbol resolves by its C name - INCLUDING a file-scope static, whose `.dbg` record is an assembler label
// (`_s_mode1`, with a `val`) reached through a `csym ... sc=static` record; the C name itself never carries
// an address, which is why a name-regex over the file could not find it (BlipToaster's HARNESS-NOTES 3.4).
// Reached via the live core's debugTarget() — NES-only (SameBoy/GBA have none). Mirrors cli-observe.test.ts.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
// A MINIMAL hand-written cc65 `.dbg`, not the real 1.3 MB build output — this test only proves the file
// parses, loads and resolves, so the addresses need not be exhaustive. It lives beside its ROM in
// resources/roms. It carries: two plain labels (`reset`, `midiIdleLoop`), an `equ`, a C global `g_frame`
// whose csym points at an IMPORT sym (no val) exporting to the defining sym, and a static `s_mode1`.
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

  // Nothing loaded yet: a lookup is null, not an error.
  expect(s.backend.symbolAddress(id, "g_frame")).toBe(null);

  // A missing/bogus path never crashes and returns false (the RPC is wired to the live debugTarget).
  expect(s.backend.loadLabels(id, "/no/such/file.dbg")).toBe(false);
  // A gone id also returns false / null (no live core to reach).
  expect(s.backend.loadLabels(0xdead, DBG)).toBe(false);
  expect(s.backend.symbolAddress(0xdead, "reset")).toBe(null);

  // If the real cc65 fixture is committed, loading it into the NES core succeeds.
  if (!s.backend.fileExists(DBG)) {
    console.log("# note: no cc65 .dbg fixture — asserted graceful-failure path only");
    return;
  }
  expect(s.backend.loadLabels(id, DBG)).toBe(true);

  // Assembler labels resolve as written.
  expect(s.backend.symbolAddress(id, "reset")).toBe(0x8000);
  expect(s.backend.symbolAddress(id, "midiIdleLoop")).toBe(0x9fc4);
  expect(s.backend.symbolAddress(id, "_g_frame")).toBe(0x0356);
  // C names resolve through the csym table: the global via its import -> export chain, and the STATIC.
  expect(s.backend.symbolAddress(id, "g_frame")).toBe(0x0356);
  expect(s.backend.symbolAddress(id, "s_mode1")).toBe(0x04b3);
  // An `equ` is not a label and an unknown name is null.
  expect(s.backend.symbolAddress(id, "someConstant")).toBe(null);
  expect(s.backend.symbolAddress(id, "no_such_symbol")).toBe(null);
});
