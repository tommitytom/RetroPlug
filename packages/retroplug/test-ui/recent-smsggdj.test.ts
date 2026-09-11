// The reported flow, end to end in the real app on the headless display: an smsggdj project in the
// Recent list, from the first open through reopening it by its song row.
//
// What used to happen: the project row went in SONGLESS the instant the ROM was dropped (the cart had
// not booted, so it could name no song), a song loaded from the Songs menu added a second row, and
// picking that row on the start menu prompted "Unsaved song" for a song nobody had written - and then
// did not load the song at all, because the write landed before the cart's boot erased it. This drives
// exactly that path and asserts the opposite at every step: nothing in Recent until the cart is up,
// then one row; the song row replaces it; the row reopens the project with NO prompt and the song is
// what the cart holds once it has booted (read off the window title, which names the working song).
//
// The `.sav` is written by the test itself, beside the staged ROM, with the SMDJ4 codec - the test runs
// in the app's own JS context, so the real backend's file ops are right there.

import { test, expect, ui, navTo, Key } from "ui-harness";
import { createRealBackend } from "../src/realBackend";
import { buildSav } from "../src/smsggdj/codec/sav";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "../test-native/smsSyncSong";

const ROM = () => ui.romDir() + "/smsggdj_v0_45.sms";
const SAV = () => ui.romDir() + "/smsggdj_v0_45.sav";
const PROJECT_LABEL = "smsggdj_v0_45.sav [smsggdj_v0_45]"; // the cart's identity: "<sav> [<rom stem>]"
const SONG_ROW = "ALPHA - " + PROJECT_LABEL;
const CART_TITLE = "ALPHA - smsggdj v0.45"; // the window title's "<working song> - <ROM's own name>"
const BOOT_MS = 4000; // the splash is ~2.3 s; well clear of it
const WATCH_FRAMES = 60; // two ticks of the song watcher (POLL_FRAMES = 30)

/** Open the Recent submenu of whichever menu is showing (focus starts at the top of a fresh menu). */
function openRecent(): void {
  expect(navTo("Recent")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
}

/** Whether the instance menu is showing ("Add Instance" is on no other menu). */
const instanceMenuOpen = (): boolean => ui.findByTextContaining("Add Instance") != null;

/** Open the instance menu with focus at its top - closing it first if it is already open (a plain
 *  action row like Load... closes the menu itself; a submenu row leaves it open, so the state is not
 *  known in advance). Esc is the (rebindable) open/close toggle, so it is never assumed which way. */
function openInstanceMenuFresh(): void {
  if (instanceMenuOpen()) {
    ui.tapKey(Key.Esc);
    ui.pump(6);
  }
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(instanceMenuOpen()).toBeTruthy();
}

test("an smsggdj project through Recent: no row until the cart is up, one row per song, reopen by row loads the song", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  const be = createRealBackend();
  if (!be.fileExists(ROM())) {
    console.log("# SKIP recent-smsggdj: no smsggdj ROM staged in romDir");
    return;
  }
  expect(be.writeFile(SAV(), buildSav([{ block: buildMetronomeBlock(), name: "ALPHA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!)).toBeTruthy();

  const titles: string[] = [];
  (globalThis as { __rp_setWindowTitle?: (t: string) => void }).__rp_setWindowTitle = (t) => titles.push(t);
  const lastTitle = () => titles[titles.length - 1] ?? "";

  // Drop the ROM on the start screen: a fresh project, its sibling .rplg written, the cart booting.
  ui.fileDrop(ROM(), 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();

  // Recent, before the cart is up: NOTHING - not a songless placeholder for a project about to have a song.
  openInstanceMenuFresh();
  openRecent();
  expect(ui.findByTextContaining("No Recent Files") != null).toBeTruthy();
  expect(ui.findByTextContaining(PROJECT_LABEL)).toBe(null);

  // Let the cart boot (pump only ticks the UI; advance runs the core), then let the watcher tick: the
  // owed row is paid, songless, because v0.45 boots blank.
  ui.advance(BOOT_MS);
  ui.pump(WATCH_FRAMES);
  openInstanceMenuFresh();
  openRecent();
  expect(ui.findByText(PROJECT_LABEL) != null).toBeTruthy();
  expect(ui.findByTextContaining("No Recent Files")).toBe(null);
  expect(ui.findByTextContaining(SONG_ROW)).toBe(null);

  // Load ALPHA from the cart's Songs menu (below Recent in the same menu). The cart is up, so the load
  // is immediate; its row supersedes the songless one.
  expect(navTo("SMSGGDJ")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(navTo("Songs")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(navTo("ALPHA")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(navTo("Load...")).toBeTruthy();
  ui.tapKey(Key.Enter);
  // The write is queued to the audio thread and the RAM snapshot republished per block, so the core has
  // to run for it to be visible (in the app the audio thread never stops; here advance IS that thread).
  // The title is recomposed on the next render, which reopening the menu provides.
  ui.advance(300);
  ui.pump(WATCH_FRAMES);
  openInstanceMenuFresh();
  expect(lastTitle().endsWith(CART_TITLE)).toBeTruthy(); // the cart names ALPHA as its working song
  openRecent();
  expect(ui.findByTextContaining(SONG_ROW) != null).toBeTruthy();
  expect(ui.findByText(PROJECT_LABEL)).toBe(null); // the songless row is gone, not sitting next to it

  // Back to the start menu. New Project is above Recent, so reopen the menu to start from the top; a
  // clean project should not prompt, but a battery the cart touched would, so answer it if it does.
  openInstanceMenuFresh();
  expect(navTo("New Project")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  if (ui.findByTextContaining("Unsaved changes") != null) {
    expect(navTo("Don't Save")).toBeTruthy();
    ui.tapKey(Key.Enter);
    ui.pump(10);
  }
  expect(ui.findByTestId("tile-0")).toBe(null);
  expect(ui.findByTextContaining("Load mGB") != null).toBeTruthy(); // the start menu

  // The reported step: pick the song row. No "Unsaved song" prompt (the cart has not even booted, and when
  // it has, it holds nothing to lose); the project opens; the song lands once the cart is up.
  openRecent();
  expect(navTo(SONG_ROW)).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTextContaining("Unsaved song")).toBe(null);
  expect(ui.findByTextContaining("has unsaved changes")).toBe(null);
  expect(lastTitle().endsWith(CART_TITLE)).toBeFalsy(); // not yet: the cart is booting, nothing is loaded

  ui.advance(BOOT_MS);
  ui.pump(WATCH_FRAMES); // the request settles on a frame tick once the latch is up...
  ui.advance(300); // ...its write lands on the next blocks...
  openInstanceMenuFresh(); // ...and the menu's render recomposes the title
  expect(ui.findByTextContaining("Unsaved song")).toBe(null);
  expect(lastTitle().endsWith(CART_TITLE)).toBeTruthy(); // ALPHA is what the cart holds

  // ...and Recent still has exactly the song row for it: reopened by row, not re-recorded blank.
  openRecent();
  expect(ui.findByTextContaining(SONG_ROW) != null).toBeTruthy();
  expect(ui.findByText(PROJECT_LABEL)).toBe(null);

  delete (globalThis as { __rp_setWindowTitle?: unknown }).__rp_setWindowTitle;
});
