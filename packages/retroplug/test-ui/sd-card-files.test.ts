// System > SD Card Files: the NES's emulated SD card listed in a grid cell of its own beside the game (the mesen
// package's "nes-sd-files" side view). Opening it adds a cell after the tile and asks for a window twice as wide, as a
// second instance would; the panel lists the folder System > SD Card picked, and stays open across the rebuild that
// pick makes (the system comes back under a new id, which the grid follows). Off takes the cell away again.
import { test, expect, ui, navTo, Key } from "ui-harness";

test("SD Card Files opens the card's listing beside the NES, and it follows a new card", () => {
  const g = globalThis as {
    __rp_setWindowSize?: (w: number, h: number) => void;
    __rp_openFileBrowser?: (t: string, p: string, s: boolean, d: string, sd: string, dir: boolean) => void;
    __rp_onFileBrowserResult?: (path: string | null) => void;
  };
  const sizes: Array<[number, number]> = [];
  g.__rp_setWindowSize = (w, h) => sizes.push([w, h]);
  // The card: the harness's own ROM folder, which holds files this test can name.
  g.__rp_openFileBrowser = () => g.__rp_onFileBrowserResult?.(ui.romDir());

  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  ui.fileDrop(ui.romDir() + "/bliptoaster.nes", 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  const single = sizes[sizes.length - 1];
  expect(ui.findByTestId("side-nes-sd-files-0")).toBe(null);

  const openSystem = (): void => {
    expect(navTo("System")).toBeTruthy();
    ui.tapKey(Key.Enter);
    ui.pump(10);
  };
  ui.tapKey(Key.Esc);
  ui.pump(10);
  openSystem();
  expect(navTo("SD Card Files: Off")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(ui.findByTextContaining("SD Card Files: On") != null).toBeTruthy(); // the menu kept its place

  // The panel's cell sits right of the tile, and the window grew to two columns of the same height.
  const tile = ui.findByTestId("tile-0");
  const side = ui.findByTestId("side-nes-sd-files-0");
  expect(side != null).toBeTruthy();
  expect(side!.x).toBe(tile!.x + tile!.width);
  expect(side!.y).toBe(tile!.y);
  const dual = sizes[sizes.length - 1];
  expect(dual[0]).toBe(single[0] * 2);
  expect(dual[1]).toBe(single[1]);
  expect(ui.findByTextContaining("No folder picked.") != null).toBeTruthy(); // the scratch card has no folder to list

  // Pick the card: the system rebuilds under a new id and the panel stays open, now listing the folder.
  expect(navTo("SD Card: Scratch")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(30);
  expect(ui.findByTestId("side-nes-sd-files-0") != null).toBeTruthy();
  expect(ui.findByTextContaining("bliptoaster.nes") != null).toBeTruthy();
  expect(ui.findByTextContaining("No folder picked.")).toBe(null);

  // Closing the menu hands the panel a new host; it keeps what it shows rather than blanking until its next read.
  ui.tapKey(Key.Esc);
  ui.pump(5);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTextContaining("bliptoaster.nes") != null).toBeTruthy();

  // Off takes the cell away and the window back to one column.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  openSystem();
  expect(navTo("SD Card Files: On")).toBeTruthy();
  ui.tapKey(Key.Right);
  ui.pump(20);
  expect(ui.findByTestId("side-nes-sd-files-0")).toBe(null);
  expect(sizes[sizes.length - 1]).toEqual(single);
});
