// The smsggdj BATTERY, end to end on the shipped v0.45 ROMs - the half the song tests assume and none
// of them check.
//
// sms-live-song-load and sms-layout both hand the core a `.sav` built by the TS codec and then poke work
// RAM; neither asks whether the emulated cart's SRAM is real. It is worth asking, because Mesen only
// gives an SMS/GG cart RAM at all through `_cartRamSize = CartRamMaxSize` (SmsMemoryManager.cpp:86) -
// there is no header flag behind it - and because everything the Songs menu offers except Load (Export,
// Replace, Delete, Move, Add, Import) still goes through the battery. If the cart could not see its own
// SRAM, every one of those would be writing into a void while the tests that poke RAM stayed green.
//
// Rather than assert on Mesen's plumbing, this asks the CART. Its boot-time probe (engine.asm
// sram_detect) writes into the SRAM window and reads it back in both banks to size it, so `sram_ok` and
// `sram_slots` ARE a readback test of the emulated cart RAM, run by the software that has to trust it.
// Then the cart is driven to its FILES screen - the only place it reads its own SMDJ4 directory - and
// made to SAVE, which closes the loop the other way: bytes the cart wrote come back out through
// `readSram` and parse with the same TS codec that writes `.sav` files.
//
// The last test covers Game Gear song loading, which nothing else does. The work-RAM layout is keyed by
// VERSION with no .sms/.gg distinction, so a GG load writes to addresses taken from an SMS link. (A
// local `wlalink -S` of both v0.45 links gives an identical $C000-$DFFF label set, so the shared entry
// is right; this is the behavioural half of that claim, against the ROM users actually run.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildSav, isSmsggdjSav, listSongs, readSongBlock, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { SMSGGDJ_SYMBOLS } from "../src/smsggdj/runtime/symbols.generated";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { identifySmsggdjVersion } from "../src/smsggdj/romDetect";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const GG_ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg";
const SYM = SMSGGDJ_SYMBOLS["0.45"];
const SCR_FILES = 10; // editor.asm SCR_FILES
const SRAM_SLOTS_32K = 6; // sram_detect: 0 none / 1 = 8K / 3 = 16K / 6 = 32K

// Wire button indices (InputTypes.hpp SmsButton). SMS "button 1" (PAD_B1, $10) is Mesen's Buttons::B,
// and "button 2" (PAD_B2, $20) is Buttons::A - the pad BITS, not the labels on the pad.
const BTN_1 = 5;
const BTN_2 = 4;
const BTN_DOWN = 3;
const BTN_START = 7;

// The cart resolves held-button chords per frame with no timing windows, EXCEPT the double-tap window:
// two button-1 taps closer than DT_WINDOW (15 frames = 250 ms) are a double-tap, which pastes instead of
// confirming. Every gap below clears it with room to spare.
const TAP_MS = 100;
const GAP_MS = 500;

/** One press/release of `btn`, spaced so the cart sees a pad edge and never a double-tap. */
function tap(audio: ReturnType<typeof createAudioDriver>, id: number, btn: number): void {
  audio.pressButton(id, btn, true);
  audio.renderAudio(TAP_MS);
  audio.pressButton(id, btn, false);
  audio.renderAudio(GAP_MS);
}

/** Hold `hold`, tap `btn`, release both - the cart's chord shape (`pad_edge` on one, `pad_raw` on the
 *  other), used for both "2 held + Down" and "2 held + 1". */
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

function boot(be: ReturnType<typeof createRealBackend>, id: number, romPath: string, platform: string, sram: Uint8Array) {
  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      {
        romPath, platform, core: "mesen", embeddedRom: "",
        savPath: null, statePath: null, sramBytes: sram,
        settings: JSON.stringify({ enableFm: false }),
      },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // splash, config_load, sram_detect, song_new (the cart boots blank on purpose)
  return audio;
}

test("the cart's own probe finds 32K of working SRAM, and reads the host's directory out of it", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-sram: missing ${ROM}`);
    return;
  }
  const id = 40;
  const sav = buildSav(
    [
      { block: buildMetronomeBlock(), name: "ALPHA" },
      { block: buildMetronomeBlock(), name: "BETA" },
    ],
    32 * 1024,
    buildConfigBlock(SMS_SYNC_OFF),
  )!;
  const audio = boot(be, id, ROM, "sms", sav);

  // sram_detect ran during that boot. It is a real readback of the emulated cart RAM - it writes a probe
  // into the SRAM window in bank 0 and bank 1 and requires them to differ - so a nonzero sram_slots is
  // the cart certifying Mesen's cart RAM, not us certifying it for the cart.
  const booted = be.readRam(id)!;
  expect(booted[SYM.sram_ok]).toBe(1);
  expect(booted[SYM.sram_slots]).toBe(SRAM_SLOTS_32K);

  // SONG -> FILES is "hold button 2, tap Down" (editor.asm sn_song). It matters which screen this is:
  // files_refresh is the ONLY caller of rle_dir_count, so before this the cart has never looked at the
  // directory and file_count reads 0 on a cart full of songs.
  expect(booted[SYM.file_count]).toBe(0);
  chord(audio, id, BTN_2, BTN_DOWN);
  const inFiles = be.readRam(id)!;
  expect(inFiles[SYM.scr_mode]).toBe(SCR_FILES);
  expect(inFiles[SYM.file_count]).toBe(2); // the cart parsed the image the TS codec wrote

  // Browsing does not disturb the battery: the detect restores its probe bytes, and FILES only reads.
  const after = be.readSram(id)!;
  expect(after.length).toBe(32 * 1024);
  expect(after).toEqual(sav);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("a save made INSIDE the cart comes back out through readSram, and the TS codec reads it", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) return;
  const id = 41;
  // A blank battery, so the save has to build the SMDJ4 structure from nothing rather than edit one the
  // host laid out - the case where a wrong write would be invisible to a codec round-trip test.
  const audio = boot(be, id, ROM, "sms", new Uint8Array(32 * 1024));
  chord(audio, id, BTN_2, BTN_DOWN); // -> FILES
  expect(be.readRam(id)![SYM.file_count]).toBe(0);

  // "2 held + 1" opens the FILES action menu with SAVE preselected (fc_files); button 1 then arms the
  // action and a second button 1 runs it (fp_files is deliberately two-tap, showing SURE in between).
  // files_row 0 == file_count 0 is the trailing empty slot, so this APPENDS rather than overwriting.
  chord(audio, id, BTN_2, BTN_1);
  tap(audio, id, BTN_1); // arm
  tap(audio, id, BTN_1); // run
  audio.renderAudio(1500); // rle_song_save + config_save

  expect(be.readRam(id)![SYM.file_count]).toBe(1);
  const saved = be.readSram(id)!;
  expect(isSmsggdjSav(saved)).toBe(true);
  expect(listSongs(saved).length).toBe(1);
  // A real RLE blob, not just a directory entry: readSongBlock decompresses it and returns null unless
  // the stored checksum matches what came out.
  const block = readSongBlock(saved, 0);
  expect(block != null).toBeTruthy();
  expect(block!.length).toBe(SMDJ4_BLOCK_LEN);
  expect(be.removeSystem(id)).toBeTruthy();
});

test("a live song load works on the Game Gear flavor, on the SMS-derived layout", () => {
  const be = createRealBackend();
  if (!be.fileExists(GG_ROM)) {
    console.log(`# SKIP gg live load: missing ${GG_ROM}`);
    return;
  }
  const id = 42;
  const rom = be.readFile(GG_ROM)!;
  // The .gg build self-identifies as the same version as the .sms one, which is what makes them share a
  // layout entry - so check that rather than leaving it implied.
  expect(identifySmsggdjVersion(rom)).toBe("0.45");
  const layout = resolveSmsggdjLayout("0.45")!;

  const sav = buildSav([{ block: buildMetronomeBlock(), name: "GGSONG" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
  const audio = boot(be, id, GG_ROM, "gg", sav);
  const booted = be.readRam(id)!;
  expect(booted[SYM.sram_ok]).toBe(1); // the GG cart has its battery too
  expect(booted[SYM.sram_slots]).toBe(SRAM_SLOTS_32K);

  for (const w of smsggdjIntegration.liveLoad!(rom, sav, 0, booted)!) {
    expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
  }
  audio.pressButton(id, BTN_START, true); // GG Start is a real Start, not the SMS Pause NMI
  audio.renderAudio(TAP_MS);
  audio.pressButton(id, BTN_START, false);
  audio.renderAudio(500);

  const ram = be.readRam(id)!;
  const name = String.fromCharCode(...ram.subarray(layout.name, layout.name + layout.nameLen)).replace(/\0+$/, "").trim();
  expect(name).toBe("GGSONG"); // the directory-entry field, so the SMS name address holds on GG
  expect(ram[layout.playState] !== 0).toBeTruthy();
  expect(ram[layout.engLen]).toBe(8); // the GG engine's own scan of the block we wrote

  // The phrase pool is the part the sequencer reads and does not rewrite, so it is the honest place to
  // check the block landed at offset 0 on this flavor too.
  const block = readSongBlock(sav, 0)!;
  expect(ram.subarray(0x100, 0x140)).toEqual(block.subarray(0x100, 0x140));

  let peak = 0;
  const buf = audio.renderAudio(1500);
  for (let i = 0; i < buf.length; i++) peak = Math.max(peak, Math.abs(buf[i]));
  console.log(`[gg] loaded "${name}", eng_len=${ram[layout.engLen]}, peak=${peak.toFixed(4)}`);
  expect(peak > 0.01).toBeTruthy(); // and it is audibly playing the loaded song
  expect(be.removeSystem(id)).toBeTruthy();
});
