// A real render settles on the plugin's own path: the editor binds no harness facet and no audio driver, so nothing
// waits in native, and the only mover of the engine job's promise is useJobPump's backend.pumpJobs on a frame. Like
// kit-compile.test.ts for graph jobs, this runs the store graph's RenderJobs over the real backend on this harness
// (registerHostRpc + registerEmulatorRpc, as PluginDSP.cpp and the SDL host mount): the badge appears on the tile of
// the system the render came from, the job ends done, the badge clears, and the WAV is on disk.
import { test, expect, ui, navTo, Key } from "ui-harness";
import { installScriptedStores } from "./scriptedRenders";

test("a real render settles from frame pumps alone: the badge shows, the job ends done, the WAV is written", async () => {
  const scripted = installScriptedStores(true);
  const { stores } = scripted;
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  const sysId = stores.project.systems.view()[0].id;

  const out = ui.romDir() + "/rp-ui-render.wav";
  stores.backend.deleteFile(out);
  const id = stores.renders.start(sysId, { rom: ui.romDir() + "/mGB.gb", durationMs: 3000, out });

  // Microtasks alone settle nothing: a native wait in the poll would, which is the settle path the plugin lacks.
  for (let i = 0; i < 16; i++) await Promise.resolve();
  expect(stores.renders.list().find((j) => j.id === id)!.state).toBe("rendering");

  let sawBadge = false;
  const until = Date.now() + 60000;
  while (Date.now() < until && stores.renders.list().some((j) => j.id === id && j.state === "rendering")) {
    ui.pump();
    stores.backend.pumpJobs();
    sawBadge ||= ui.findByTextContaining("rendering") != null;
    await Promise.resolve();
  }
  expect(sawBadge, "the tile showed the badge while the render ran").toBe(true);
  const job = stores.renders.list().find((j) => j.id === id);
  expect(job?.state === "done" || job === undefined, `the render ended done: ${job?.message}`).toBe(true);
  expect(stores.backend.fileExists(out)).toBe(true);
  // The tile dismisses the finished job and the badge clears.
  ui.pump(10);
  expect(ui.findByTextContaining("rendering")).toBe(null);
  expect(stores.renders.list().length).toBe(0);

  stores.backend.deleteFile(out);
  scripted.restore();
});
