// Crux spike for the plugin-lifetime runtime (04 §A): the QuickJS runtime must
// survive an editor window close + reopen WITHOUT re-evaluating the bundle
// (module caching would no-op a top-level re-mount). ui.reopen() detaches the
// display layer + unmounts React, then re-attaches a fresh display + re-mounts
// via the bundle's __rp_mountUI/__rp_unmountUI hooks — all on the same runtime.
// If the second session renders the start menu again, the persistent-context
// mount/unmount path works. Run: pnpm test:ui reopen_persist
import { test, expect, ui } from "ui-harness";

const MGB = "resources/roms/mGB.gb";

test("the runtime survives an editor close + reopen (persistent context)", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // First session: the start menu renders.
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
  expect(ui.findByText("About")).toBeTruthy();

  // Close + reopen the editor on the SAME runtime (no re-eval).
  ui.reopen();

  // Second session: the React tree re-mounted onto the fresh display binding.
  expect(ui.findByTextContaining("Load mGB")).toBeTruthy();
  expect(ui.findByText("About")).toBeTruthy();

  // The re-mounted tree is fully live, not just present: a ROM load still
  // round-trips through the RPC bridge and mounts an emulator tile.
  ui.loadRom(MGB);
  ui.pump(40);
  expect(ui.findFirstByType(0 /* View */)).toBeTruthy();

  // A second reopen (with a system loaded) also survives — catches teardown
  // state that only leaks on the 2nd cycle.
  ui.reopen();
  expect(ui.findFirstByType(0 /* View */)).toBeTruthy();
});
