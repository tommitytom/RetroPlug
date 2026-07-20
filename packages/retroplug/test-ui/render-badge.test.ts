// The background-render badge on a system tile, end to end on the headless display. The editor binds
// __rp_getRenderJobs natively; the harness doesn't, so (like resize.test.ts spying __rp_setWindowSize) we
// stub it to feed the tile a fake job and assert the badge renders for each state. Proves the tile's
// per-frame poll + the badge JSX (Box/Text/align props) actually work — not just typecheck.

import { test, expect, ui, navTo, Key } from "ui-harness";

interface FakeJob {
  id: number;
  systemId: number;
  state: "rendering" | "done" | "error" | "cancelled";
  progress: number;
  message: string;
}

type RenderGlobals = {
  __rp_getRenderJobs?: () => FakeJob[];
  __rp_cancelRender?: (id: number) => void;
  __rp_dismissRenderJob?: (id: number) => void;
};

// One job per candidate system id, so whichever id mGB lands on, its tile matches.
function jobsInState(state: FakeJob["state"], progress: number, message = ""): FakeJob[] {
  return [0, 1, 2, 3, 4].map((sid) => ({ id: 900 + sid, systemId: sid, state, progress, message }));
}

test("the render badge shows progress / error on a tile and clears when the job finishes", () => {
  const g = globalThis as RenderGlobals;
  const dismissed: number[] = [];
  g.__rp_cancelRender = () => {};
  g.__rp_dismissRenderJob = (id) => dismissed.push(id);
  g.__rp_getRenderJobs = () => jobsInState("rendering", 0.42);

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Rendering: the per-frame poll surfaces the progress badge.
  ui.pump(10);
  expect(ui.findByTextContaining("rendering") != null).toBeTruthy();
  expect(ui.findByTextContaining("42%") != null).toBeTruthy();

  // Error: the badge switches to the failure message.
  g.__rp_getRenderJobs = () => jobsInState("error", 0.42, "boom");
  ui.pump(10);
  expect(ui.findByTextContaining("render failed") != null).toBeTruthy();
  expect(ui.findByTextContaining("rendering")).toBe(null);

  // Done: the tile dismisses the job and the badge clears.
  g.__rp_getRenderJobs = () => jobsInState("done", 1);
  ui.pump(10);
  expect(dismissed.length > 0).toBeTruthy();
  expect(ui.findByTextContaining("render failed")).toBe(null);
  expect(ui.findByTextContaining("rendering")).toBe(null);

  delete g.__rp_getRenderJobs;
  delete g.__rp_cancelRender;
  delete g.__rp_dismissRenderJob;
});
