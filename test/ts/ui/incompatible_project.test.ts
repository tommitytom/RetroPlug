// Headless UI: loading a project whose stamped schemaVersion is newer than the
// build understands is refused by C++ (emits "project-incompatible"), and the UI
// shows the IncompatibleProjectModal. End-to-end through the real load path.
import { test, expect, ui } from "ui-harness";

const PROJ = "/tmp/rp_incompatible_proj.rplg";

// Encode an ASCII string to the Uint8Array writeFile expects (no TextEncoder
// dependency in the txiki runtime).
function bytes(s: string): Uint8Array {
  const a = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i);
  return a;
}

test("a project from a newer schema version shows the incompatible modal", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // A thin project stamped with a far-future schemaVersion.
  ui.writeFile(PROJ, bytes(
    '{"schemaVersion":"999","systems":[{"kind":"sameboy","romPath":"/tmp/none.gb"}]}'));

  // C++ refuses the load and emits "project-incompatible"; the modal appears.
  ui.loadProject(PROJ);
  ui.pump(40);

  expect(ui.findByTextContaining("newer version")).toBeTruthy();
  const ok = ui.findByText("OK");
  expect(ok).toBeTruthy();

  // OK dismisses it.
  ui.clickAt(ok!.x + (ok!.width >> 1), ok!.y + (ok!.height >> 1));
  ui.pump(20);
  expect(ui.findByTextContaining("newer version")).toBeFalsy();
});
