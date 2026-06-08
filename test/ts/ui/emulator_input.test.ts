// Headless UI: keyboard input routed to the FOCUSED emulator moves the LSDj
// cursor. Exercises the full input path — UI "key" event -> JS routing
// (runtime/lvgljs/input.ts maps the d-pad key to a GameboyButton) -> RPC
// pressButton notification -> the harness drains the ButtonPress command into
// the focused SameBoy system -> LSDj advances the cursor.
//
// Asserted via the emulator's own state (a WRAM cursor byte), not pixels: the
// LSDj song screen blinks, so a snapshot diff can't cleanly attribute change to
// input. Reading the cursor column is deterministic.
import { test, expect, ui, Key, Mem } from "ui-harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
// A prebuilt SRAM image (the golden corpus, generated alongside the ROM) -> LSDj
// boots straight to the song screen, skipping the 12-15s self-test. We load it as
// raw bytes; the sav codec is deliberately NOT linked into the UI test binary.
const LSDJ_SAV = "../resources/roms/lsdj/lsdj9_4_2.sav";

// LSDj v9.4.2 song-screen cursor X (column) in WRAM, empirically determined:
// 0=PU1, 1=PU2, 2=WAV, 3=NOI. Moves with the d-pad, reverts on the opposite key.
const CURSOR_X = 0x41d;

test("keyboard input moves the LSDj cursor in the focused emulator", () => {
  expect(ui.boot()).toBeTruthy();
  const sys = ui.loadRom(LSDJ, LSDJ_SAV);
  ui.pump(220); // boot to the song screen (focus was set by loadRom)

  const cursorX = () => ui.readMemory(sys, Mem.Ram)[CURSOR_X];

  // Control: with no input the cursor is stable (so the change below is input,
  // not the screen's blink/animation churn).
  const x0 = cursorX();
  ui.pump(20);
  expect(cursorX()).toBe(x0);

  // Right d-pad -> the cursor moves to a higher column.
  ui.tapKey(Key.Right);
  ui.pump(10);
  const x1 = cursorX();
  expect(x1).toBeGreaterThan(x0); // input reached the emulator

  // Left d-pad -> it moves back (deterministic, opposite of Right).
  ui.tapKey(Key.Left);
  ui.pump(10);
  expect(cursorX()).toBe(x0);

  ui.snapshotPng("/tmp/ui-emu-cursor.png");
});
