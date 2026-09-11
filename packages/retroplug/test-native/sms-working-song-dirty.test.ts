// "Does the working song have unsaved changes" - asked of the SHIPPED v0.45 ROM, because the answer
// decides whether the user is shown a discard prompt.
//
// The bug: the predicate compared the live 6,912-byte block against every saved song and called a match
// clean. On this console the working song is work RAM and the cart boots BLANK (`song_new`), so a cart
// that had only just come up matched nothing and read as an hour of unsaved work. Loading a project from
// the start menu's Recent list therefore offered to discard a song that had never existed - and could
// only call it `"the working song"`, the no-name fallback, because there wasn't one to name.
//
// The fix reads the cart's own `song_edited` (editor.asm:141, "1 = song data changed since last
// save/load"). That is an ADDRESS, and the symbol snapshot comes from a local build of the smsggdj
// source which is not byte-identical to the released binary (the same gap sms-layout certifies for
// `echo_mode`). A wrong address here fails silently in the worst direction: the flag would read 0
// forever and the guard would never fire again, losing work with no prompt at all. So both edges are
// driven on the real ROM - boot says clean, a real keypress on the SONG grid says dirty.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildSav, isSongSaved, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { SMSGGDJ_SYMBOLS } from "../src/smsggdj/runtime/symbols.generated";
import { commonSongEditedOffset } from "../src/smsggdj/runtime/layout";
import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const SYM = SMSGGDJ_SYMBOLS["0.45"];
const WORK_RAM = 8192; // the region readRam returns, mapped at CPU $C000

// Wire button indices (InputTypes.hpp SmsButton). "Button 1" (PAD_B1) is Mesen's Buttons::B - the pad
// BIT, not the label - and holding it while the dpad moves is the cart's EDIT chord (editor.asm ei_edit).
const BTN_1 = 5;
const BTN_DOWN = 3;
const TAP_MS = 100;
const GAP_MS = 500;

/** Hold `hold`, tap `btn`, release both. The cart resolves chords per frame off `pad_raw` + `pad_edge`;
 *  the gaps keep the two button-1 taps clear of the 15-frame double-tap window. */
function chord(audio: ReturnType<typeof createAudioDriver>, id: number, hold: number, btn: number): void {
  audio.pressButton(id, hold, true);
  audio.renderAudio(TAP_MS);
  audio.pressButton(id, btn, true);
  audio.renderAudio(TAP_MS);
  audio.pressButton(id, btn, false);
  audio.renderAudio(TAP_MS);
  audio.pressButton(id, hold, false);
  audio.renderAudio(GAP_MS);
}

test("a freshly booted cart is clean, and a keypress on the SONG grid makes it dirty", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-working-song-dirty: missing ${ROM}`);
    return;
  }
  const id = 45;
  // Two saved songs, neither of them blank, so the cart's boot song matches NO slot - exactly the state
  // the old content-only compare mis-read. If the blank song happened to be saved there would be nothing
  // to reproduce.
  const sav = buildSav(
    [
      { block: buildMetronomeBlock(), name: "ALPHA" },
      { block: buildMetronomeBlock(), name: "BETA" },
    ],
    32 * 1024,
    buildConfigBlock(SMS_SYNC_OFF),
  )!;

  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      {
        romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
        savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }),
      },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // splash, config_load, sram_detect, song_new - the cart boots blank on purpose

  // The flag has to be INSIDE the snapshot the predicate is handed, or its range check makes every cart
  // clean and the guard is dead.
  const edited = commonSongEditedOffset()!;
  expect(edited).toBe(SYM.song_edited);
  const booted = be.readRam(id)!;
  expect(booted.length).toBe(WORK_RAM);
  expect(edited < booted.length).toBeTruthy();
  // ...and 3 s in, the cart is UP: the readiness latch is what lets the predicate answer at all (before
  // it, "clean" is the only answer, whatever the bytes - see sms-boot-timeline for the boot itself).
  expect(booted[SYM.ints_on]).toBe(1);
  expect(smsggdjSongCatalog.workingSongReady!(booted)).toBe(true);

  // The reported bug, on the real cart. The content signal alone says dirty here - the boot song is in
  // no slot - and the cart says nothing has been typed, so nothing is at stake.
  expect(isSongSaved(be.readSram(id)!, booted.subarray(0, SMDJ4_BLOCK_LEN))).toBe(false);
  expect(booted[edited]).toBe(0);
  expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, booted)).toBe(false);

  // ...and the other edge, which is what proves the address rather than assuming it: button 1 held with
  // the dpad is the cart's EDIT chord, and the boot screen is the SONG grid, which is song data.
  chord(audio, id, BTN_1, BTN_DOWN);
  const typed = be.readRam(id)!;
  expect(typed[edited]).toBe(1);
  expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, typed)).toBe(true);

  expect(be.removeSystem(id)).toBeTruthy();
});
