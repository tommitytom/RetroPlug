// The redesigned System > Render submenu, end to end on the headless display. A file-dropped ROM (unlike
// the embedded "Load mGB", it has a real romPath, so the Render submenu shows) lets us drive the selectors
// and the single Render… action. Audio Routing (the split mode) / Sample Rate cycle on Left/Right; Max Duration steps ±1s on
// Left/Right and ±30s on PageUp/PageDown (the coarse-step path added to Menu.tsx). Render… snapshots the
// live state and calls __rp_startRender — spied here (the harness doesn't bind it), with the file browser
// stubbed to return a path — so we assert the spec carries the picked split / sample rate / max duration.

import { test, expect, ui, navTo, Key } from "ui-harness";

const PAGE_UP = 0xe031; // DPF kKeyPageUp — reaches the raw "key" bus (not LVGL-translated)
const PAGE_DOWN = 0xe032;

interface Captured {
  systemId: number;
  spec: { split: string; sampleRate?: number; maxDurationMs?: number; out: string };
}

/** Read the "Max Duration: M:SS" row's seconds, or -1 if absent. */
function maxDurationSec(): number {
  const w = ui.findByTextContaining("Max Duration:");
  if (!w) return -1;
  const m = /Max Duration:\s*(\d+):(\d{2})/.exec(w.text);
  return m ? Number(m[1]) * 60 + Number(m[2]) : -1;
}

test("render submenu: selectors cycle (arrows + PageUp/PageDown) and Render… applies them", () => {
  const g = globalThis as {
    __rp_startRender?: (id: number, spec: string) => number;
    __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string) => void;
    __rp_onFileBrowserResult?: (path: string | null) => void;
  };
  let captured: Captured | null = null;
  g.__rp_startRender = (systemId, spec) => {
    captured = { systemId, spec: JSON.parse(spec) };
    return 1;
  };
  // Stub the native file dialog to immediately resolve a path (the UI's realBackend installs the callback).
  g.__rp_openFileBrowser = () => g.__rp_onFileBrowserResult?.("/tmp/uitest-render.wav");

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Drop a real ROM file → a disk-backed system (has a romPath, so Render shows).
  ui.fileDrop(ui.romDir() + "/mGB.gb", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Instance menu → System (expands inline) → Render (expands inline).
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(navTo("Render")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);

  // The three selectors + the action are present. (The split cycler is labelled "Audio Routing".)
  expect(ui.findByTextContaining("Audio Routing:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Sample Rate:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Max Duration:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Render...") != null).toBeTruthy();

  // Audio Routing: Mix → Channels (mGB is sameboy, so channels is offered; pins is not).
  expect(ui.findByTextContaining("Audio Routing: Mix") != null).toBeTruthy();
  expect(navTo("Audio Routing:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Audio Routing: Channels") != null).toBeTruthy();

  // Sample Rate: 44100 → 48000.
  expect(ui.findByTextContaining("Sample Rate: 44100 Hz") != null).toBeTruthy();
  expect(navTo("Sample Rate:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Sample Rate: 48000 Hz") != null).toBeTruthy();

  // Max Duration: Left/Right = ±1s; PageUp/PageDown = ±30s.
  expect(navTo("Max Duration:")).toBeTruthy();
  const base = maxDurationSec();
  expect(base > 0).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(maxDurationSec()).toBe(base + 1); // fine step
  ui.tapKey(PAGE_UP);
  ui.pump(6);
  expect(maxDurationSec()).toBe(base + 31); // coarse +30
  ui.tapKey(PAGE_DOWN);
  ui.pump(6);
  expect(maxDurationSec()).toBe(base + 1); // coarse -30 back

  const picked = maxDurationSec();

  // Render… → file browser stub resolves → __rp_startRender fires with the picked options.
  expect(navTo("Render...")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);

  expect(captured != null).toBeTruthy();
  const cap = captured!;
  expect(cap.spec.split).toBe("channels");
  expect(cap.spec.sampleRate).toBe(48000);
  expect(cap.spec.maxDurationMs).toBe(picked * 1000);
  expect(cap.spec.out).toBe("/tmp/uitest-render.wav");

  delete g.__rp_startRender;
  delete g.__rp_openFileBrowser;
});
