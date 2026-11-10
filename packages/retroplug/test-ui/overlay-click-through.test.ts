// A tile's view slot passes the click (docs/modular-graph-phase-f4c-design section 5.2): a slot is
// a transparent full-tile Box, so clicking an unfocused tile with an overlay SHOWING must still focus it, exactly as
// clicking the dim layer does when no view covers it. SKIPs when no LSDj ROM was staged.
import { test, expect, skip, ui, navTo, Key, type WidgetInfo } from "ui-harness";

const LSDJ = () => ui.romDir() + "/lsdj9_4_2.gb";
const BACKTICK = 0x60;

function clickCenter(w: WidgetInfo): void {
  ui.clickAt(w.x + Math.floor(w.width / 2), w.y + Math.floor(w.height / 2));
}

test("clicking an unfocused tile focuses it while the LSDj overlay shows", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(LSDJ(), 0, 0);
  ui.pump(30);
  if (ui.findByTestId("tile-0") == null) skip("overlay-click-through: no LSDj ROM staged in romDir");
  ui.advance(1500);
  ui.pump(10);

  // A second instance, and the overlay shown over both.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  ui.tapKey(BACKTICK);
  ui.advance(300);
  ui.pump(10);
  expect(ui.findByTestId("lsdj-overlay") != null).toBeTruthy();

  // Tile 0 is focused after the duplicate, so tile 1 is dimmed; the click lands on its overlay strip's slot.
  expect(ui.findByTestId("dim-1") != null).toBeTruthy();
  clickCenter(ui.findByTestId("tile-1")!);
  ui.pump(10);
  expect(ui.findByTestId("dim-1")).toBe(null);
  expect(ui.findByTestId("dim-0") != null).toBeTruthy();
});
