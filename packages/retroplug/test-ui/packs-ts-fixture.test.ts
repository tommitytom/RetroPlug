// A pack's halves in the running UI (docs/downloadable-packs-m2c-plan.md): the TS fixture pack's app and ui halves load
// in the UI test harness (which has no control plane, so the UI loads the app half too), its start-menu row shows, and
// the broken pack beside it is refused with their reasons while the good one loads. The runner gives this file
// the packs (run-ui-tests.mjs, PACK_TESTS). The throwing variant tsfix-throws is left to the CLI leg: any dynamically
// imported module that throws at top level raises an unhandledrejection, which the harness records as a failure and a
// host with no listener answers by stopping its loop.
import { test, expect, ui } from "ui-harness";

type Status = { loaded: Record<string, string[]>; refused: Record<string, string> };
const status = (): Status => ((globalThis as Record<string, unknown>).__rp_packStatus as () => Status)();

test("the fixture pack's row shows, and the broken packs are refused by name", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(status().loaded.tsfix).toEqual(["app", "ui"]);
  expect(ui.findByText("TS Fixture Row") != null).toBeTruthy();
  expect(status().refused["tsfix-guard"]).toBe(
    "pack:tsfix-guard/app imports '@retroplug/sameboy/app', which is not a RetroPlug contract module");
});
