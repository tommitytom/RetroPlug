// An unsaved smsggdj working song, end to end in the real app on the headless display: it survives the
// FM Audio toggle, the close prompt names it, and Save & Quit puts it in the `.sav`.
//
// The working song is the cart's work RAM - in neither the battery nor the project - so before this, the
// FM toggle (which never took effect at all until the next boot) and the close both lost it without a
// word. The song is composed by poking the cart's RAM through the real backend, which shares the app's
// native engine; the id is found the same way, since the headless harness exposes no stores.

import { test, expect, skip, ui, navTo, Key } from "ui-harness";
import { createRealBackend } from "../src/realBackend";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { listSongs, readSongBlock, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { buildMetronomeBlock } from "../test-native/smsSyncSong";

const ROM = () => ui.romDir() + "/smsggdj_v0_45.sms";
const SAV = () => ui.romDir() + "/smsggdj_v0_45.sav";
const BOOT_MS = 4000; // the splash is ~2.3 s; well clear of it
const layout = resolveSmsggdjLayout("0.45")!;

const onCloseRequested = (): boolean =>
  (globalThis as unknown as { __rp_onCloseRequested?: () => boolean }).__rp_onCloseRequested?.() ?? false;

/** Whether the instance menu is showing ("Add Instance" is on no other menu). */
const instanceMenuOpen = (): boolean => ui.findByTextContaining("Add Instance") != null;

/** Open the instance menu with focus at its top, closing it first if it is already open. */
function openInstanceMenuFresh(): void {
  if (instanceMenuOpen()) {
    ui.tapKey(Key.Esc);
    ui.pump(6);
  }
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(instanceMenuOpen()).toBeTruthy();
}

test("an unsaved smsggdj song survives FM Audio, is named on close, and Save & Quit writes it to the .sav", () => {
  let quitCalls = 0;
  (globalThis as unknown as { __rp_quitWindow?: () => void }).__rp_quitWindow = () => {
    quitCalls++;
  };
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  const be = createRealBackend();
  if (!be.fileExists(ROM())) skip("working-song-smsggdj: no smsggdj ROM staged in romDir");
  if (be.fileExists(SAV())) be.deleteFile(SAV()); // a new user's cart: no battery file, no directory

  ui.fileDrop(ROM(), 0, 0);
  ui.pump(30);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  ui.advance(BOOT_MS);
  ui.pump(10);

  /** The live cart's system id: the one whose work RAM carries a raised boot latch. */
  const liveId = (): number => {
    for (let id = 1; id < 64; id++) {
      const ram = be.readRam(id);
      if (ram && ram.length === 0x2000 && ram[layout.booted] === 1) return id;
    }
    return 0;
  };
  const before = liveId();
  expect(before > 0, "no booted smsggdj cart found").toBeTruthy();

  // Compose: a song block, a name and the cart's own UNSAVED flag, as an hour of editing would leave them.
  const song = buildMetronomeBlock();
  expect(be.writeRam(before, layout.song, song)).toBeTruthy();
  expect(be.writeRam(before, layout.name, Uint8Array.from("MYSONG  ", (c) => c.charCodeAt(0)))).toBeTruthy();
  expect(be.writeRam(before, layout.edited, Uint8Array.of(1))).toBeTruthy();
  ui.advance(300);

  // FM Audio: it now resets the cart to take effect, and the reset carries the song across.
  openInstanceMenuFresh();
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.findByTextContaining("FM Audio: On") != null).toBeTruthy();
  expect(navTo("FM Audio")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  // The cart reboots under a new id, and the menu FOLLOWS it: still open on the rebuilt cart (at its top
  // level - the tile is keyed by id, so its menu mounts fresh), and that cart's setting reads Off. It used
  // to strand on the dead id instead - open but invisible, eating the next Esc.
  expect(instanceMenuOpen()).toBe(true);
  expect(navTo("System")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.findByTextContaining("FM Audio: Off") != null).toBeTruthy();
  ui.tapKey(Key.Esc);
  ui.pump(6);
  if (instanceMenuOpen()) {
    ui.tapKey(Key.Esc);
    ui.pump(6);
  }
  expect(instanceMenuOpen()).toBe(false);

  ui.advance(BOOT_MS); // the new cart boots...
  ui.pump(10); // ...the carry settles on a frame tick once its latch is up...
  ui.advance(300); // ...and the writes land on the next blocks
  const after = liveId();
  expect(after > 0 && after !== before, "the FM toggle did not rebuild the cart").toBeTruthy();
  const ram = be.readRam(after)!;
  expect(ram.subarray(layout.song, layout.song + SMDJ4_BLOCK_LEN)).toEqual(song);
  expect(ram[layout.edited]).toBe(1); // still unsaved: a reset is not a save

  // Close: vetoed, and the prompt names the song and where Save will put it.
  expect(onCloseRequested()).toBe(true);
  ui.pump(10);
  expect(ui.findByText("Song: MYSONG -> smsggdj_v0_45.sav (new slot)") != null).toBeTruthy();
  expect(ui.focused()!.text).toBe("Save & Quit");

  // Save & Quit: the battery write lands on the next audio block (advance), the save resolves on a UI tick.
  ui.tapKey(Key.Enter);
  for (let i = 0; i < 20 && quitCalls === 0; i++) {
    ui.advance(50);
    ui.pump(2);
  }
  expect(quitCalls).toBe(1);
  const sav = be.readFile(SAV())!;
  expect(listSongs(sav)).toEqual([{ index: 0, name: "MYSONG" }]);
  expect(readSongBlock(sav, 0)).toEqual(song);
});
