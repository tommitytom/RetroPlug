// Backtick still flips BOTH debug overlays, and rebinding one leaves the other alone (docs/modular-graph-phase-f4c
// design section 8.7): the two cart packages each own a flag and an action, and one press with the defaults
// carries both ids. SKIPs when either fixture ROM was not staged.
import { test, expect, skip, ui, navTo, Key } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const RISA = () => ui.romDir() + "/risa.nes";
const BACKTICK = 0x60;
const KEY_Q = "Q".charCodeAt(0);

test("one backtick toggles both overlays, and rebinding one leaves the other", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-actions: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // A second instance, then risa over it: two carts with a readout each.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  const t1 = ui.findByTestId("tile-1");
  expect(t1 != null).toBeTruthy();
  ui.fileDrop(RISA(), t1!.x + Math.floor(t1!.width / 2), t1!.y + Math.floor(t1!.height / 2));
  ui.pump(40);

  // The fixture gate, decided BEFORE the press: tile 1 must be running risa, or a broken risa toggle would read as a
  // missing ROM (the case exists to prove one press reaches both packages). The instance menu names the cart it
  // resolved: "Risa >" for a risa tile, "LSDj >" for the Game Boy a risa ROM that was not staged leaves behind.
  ui.clickAt(t1!.x + Math.floor(t1!.width / 2), t1!.y + Math.floor(t1!.height / 2));
  ui.pump(10);
  ui.tapKey(Key.Esc);
  ui.pump(10);
  const risaLoaded = navTo("Risa");
  ui.tapKey(Key.Esc); // close the menu again (Esc is the OpenMenu binding)
  ui.pump(10);
  if (!risaLoaded) skip("overlay-actions: no risa ROM staged in romDir, so the second instance stayed a Game Boy");

  // Backtick on: one press carries both packages' action ids, so both strips appear.
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();
  ui.tapKey(BACKTICK);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay")).toBe(null);

  // Rebind only LSDj's action to Q: Settings -> Keyboard Bindings -> its row -> capture.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Keyboard Bindings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("LSDj Debug Overlay")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused()!.text.includes("Press a key")).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.pump(10);
  expect(ui.findByTextContaining("LSDj Debug Overlay: Q") != null).toBeTruthy();
  ui.tapKey(Key.Esc); // close the menu (Esc is the OpenMenu binding)
  ui.pump(10);

  // Backtick now reaches risa only; Q is LSDj's.
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();
  ui.tapKey(KEY_Q);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay")).toBe(null);
  expect(ui.findByTestId("risa-overlay") != null).toBeTruthy(); // risa is untouched by LSDj's rebind
});
