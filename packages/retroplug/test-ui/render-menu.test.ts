// The redesigned System > Render submenu, end to end on the headless display. A file-dropped mGB (a real
// romPath, non-tracker → the filename derives to the ROM stem "mGB") drives the new explicit rows:
//   Output Dir (a native FOLDER picker), Filename (derived), If Exists (Overwrite/Rename),
//   Audio Routing / Sample Rate / Max Duration, and a dialog-less "Render".
// The file browser is stubbed; we assert the Output Dir opens in DIRECTORY mode and that "Render" writes
// to <outputDir>/<filename>.wav with the chosen routing / rate / on-exists policy — no dialog at render time.

import { test, expect, ui, navTo, Key } from "ui-harness";

interface Captured {
  systemId: number;
  spec: { split: string; sampleRate?: number; maxDurationMs?: number; onExists?: string; out: string };
}

interface BrowseOpen {
  title: string;
  directory: boolean;
  startDir: string;
}

test("render submenu: Output Dir folder-picks, and Render writes <dir>/<name>.wav (no dialog)", () => {
  const g = globalThis as {
    __rp_startRender?: (id: number, spec: string) => number;
    __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string, sd: string, dir: boolean) => void;
    __rp_onFileBrowserResult?: (path: string | null) => void;
  };
  let captured: Captured | null = null;
  g.__rp_startRender = (systemId, spec) => {
    captured = { systemId, spec: JSON.parse(spec) };
    return 1;
  };
  // Stub the native browser: record each open (title / directory-mode / startDir), then resolve a folder.
  const opens: BrowseOpen[] = [];
  g.__rp_openFileBrowser = (title, _p, _s, _d, sd, dir) => {
    opens.push({ title, directory: dir, startDir: sd });
    g.__rp_onFileBrowserResult?.("/tmp/renders");
  };

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  // Drop a real ROM → a disk-backed system (has a romPath, so Render shows). mGB is not a tracker cart.
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

  // All the new rows are present, and the filename defaulted to the ROM stem ("mGB", non-tracker).
  expect(ui.findByTextContaining("Output Dir:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Filename: mGB") != null).toBeTruthy();
  expect(ui.findByTextContaining("Audio Routing:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Sample Rate:") != null).toBeTruthy();
  expect(ui.findByTextContaining("Max Duration:") != null).toBeTruthy();
  expect(ui.findByTextContaining("If Exists: Overwrite") != null).toBeTruthy();

  // Output Dir → a DIRECTORY picker; the chosen folder shows next to the row.
  expect(navTo("Output Dir:")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(opens.length).toBe(1);
  expect(opens[0].title).toBe("Output Dir");
  expect(opens[0].directory).toBe(true); // the render "Output Dir" is a folder picker, not a file dialog
  expect(ui.findByTextContaining("Output Dir: /tmp/renders") != null).toBeTruthy();

  // If Exists: Overwrite → Rename (sits right below Filename, above the routing rows).
  expect(navTo("If Exists:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("If Exists: Rename") != null).toBeTruthy();

  // Audio Routing: Mix → Channels (mGB is sameboy, so channels is offered).
  expect(navTo("Audio Routing:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Audio Routing: Channels") != null).toBeTruthy();

  // Sample Rate: 44100 → 48000.
  expect(navTo("Sample Rate:")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(6);
  expect(ui.findByTextContaining("Sample Rate: 48000 Hz") != null).toBeTruthy();

  // Render → dialog-less; __rp_startRender fires with the composed path + the chosen options. No extra
  // browser open (still just the one Output Dir dialog). Focus is on Sample Rate (above the Render action),
  // so this down-only navTo lands on the child action, not the parent "Render" submenu row above it.
  expect(navTo("Render")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);

  expect(opens.length).toBe(1); // Render opened NO dialog
  expect(captured != null).toBeTruthy();
  const cap = captured!;
  expect(cap.spec.out).toBe("/tmp/renders/mGB.wav"); // <Output Dir>/<Filename>.wav
  expect(cap.spec.split).toBe("channels");
  expect(cap.spec.sampleRate).toBe(48000);
  expect(cap.spec.onExists).toBe("rename");

  delete g.__rp_startRender;
  delete g.__rp_openFileBrowser;
});
