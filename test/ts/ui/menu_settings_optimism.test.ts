// Headless UI: the Project submenu's Layout / MIDI Routing / Audio Routing rows
// are now applied OPTIMISTICALLY — the menu callback updates the UI's own working
// copy synchronously and only informs the DSP (which no longer echoes
// ConfigChanged for settings). This asserts each row's label updates when cycled,
// exercising the applyLayout / applyMidiRouting / applyAudioRouting path that the
// existing zoom tests don't cover. Run: pnpm test:ui menu_settings_optimism
import { test, expect, ui, Key } from "ui-harness";

const MGB  = "resources/roms/mGB.gb";
const PROJ = "/tmp/rp_settings_optimism.rplg";

function focusRowContaining(substr: string, max = 32) {
  for (let i = 0; i < max; i++) {
    const f = ui.focused();
    if (f && f.text.includes(substr)) return f;
    ui.tapKey(Key.Down);
    ui.pump(6);
  }
  const f = ui.focused();
  return f && f.text.includes(substr) ? f : null;
}

test("Project settings rows cycle through the optimistic apply* path", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);

  // A fresh project so we start from known defaults (Auto / Send to All / Stereo).
  ui.writeProjectJson(PROJ, MGB, 0);
  expect(ui.loadProject(PROJ)).toBeTruthy();
  ui.pump(60);
  ui.tapKey(Key.Esc);
  ui.pump(30);

  expect(focusRowContaining("Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);

  // Layout: Auto -> Row.
  const layout = focusRowContaining("Layout:");
  expect(layout!.text).toBe("Layout: Auto");
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(ui.focused()!.text).toBe("Layout: Row");

  // MIDI Routing: Send to All -> 4 Ch / Inst.
  const midi = focusRowContaining("MIDI Routing:");
  expect(midi!.text).toBe("MIDI Routing: Send to All");
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(ui.focused()!.text).toBe("MIDI Routing: 4 Ch / Inst");

  // Audio Routing: Stereo -> 2 Ch / Inst.
  const audio = focusRowContaining("Audio Routing:");
  expect(audio!.text).toBe("Audio Routing: Stereo");
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(ui.focused()!.text).toBe("Audio Routing: 2 Ch / Inst");
});
