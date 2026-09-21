// The smsggdj boot, frame by frame, on the shipped v0.45 ROMs - the timeline every working-song read
// and write now waits on.
//
// Work RAM is readable from the moment the core is constructed (the snapshot registry seeds it at
// claim), and for the cart's first seconds it is the boot sequence's, not the cart's: `init` zero-fills
// $C000-$DFEE, the splash runs, `song_new` seeds the blank song, `editor_init` and `init_paint` follow,
// and only then `ld a,1 / ld (ints_on),a / ei` starts the main loop. Reading the song name in that window
// read nothing; writing a song into it had the write erased. Both happened, from the Recent list, at
// frame 0 of a project load - the Recent row's song silently did not load, and the project's row went
// in songless.
//
// This proves, against the binary users run and on both machines:
//   - `ints_on` is 0 from construction until the main loop, then 1 for good, and `frame` climbs after it;
//   - before the latch the catalog names no song; after it the name field never holds a non-name (the
//     hunt for the box-glyph rows Recent showed: any non-printable byte there, at any sample after boot,
//     fails with the bytes and the time);
//   - a song written BEFORE the latch is gone once the cart is up, and one written AFTER it stays -
//     the exact hazard the readiness gate closes, demonstrated rather than asserted;
//   - the invariant holds through the cart's OWN FILES save + load and through a host-side live load.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { Platform } from "../src/platform";
import { buildSav, isSongSaved, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { SMSGGDJ_SYMBOLS } from "../src/smsggdj/runtime/symbols.generated";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROMS: { path: string; platform: Platform; start: number }[] = [
  // Button indices are InputTypes.hpp's: SMS Pause is its own line (7); GG Start is a real Start.
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms", platform: "sms", start: 7 },
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg", platform: "gg", start: 7 },
];
const layout = resolveSmsggdjLayout("0.45")!;
const STEP_MS = 16; // one video frame, near enough: the snapshot is republished every block anyway
const BOOT_LIMIT_MS = 8000; // the splash is ~3 s; a cart not up by here is not coming up
const BTN_1 = 5;
const BTN_2 = 4;
const BTN_DOWN = 3;
const TAP_MS = 100;
const GAP_MS = 500;
const SCR_FILES = 10; // editor.asm SCR_FILES
const SYM = SMSGGDJ_SYMBOLS["0.45"];

type Backend = ReturnType<typeof createRealBackend>;
type Audio = ReturnType<typeof createAudioDriver>;

const u16 = (ram: Uint8Array, at: number): number => ram[at] | (ram[at + 1] << 8);
const hex = (b: Uint8Array): string => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join(" ");
const nameField = (ram: Uint8Array): Uint8Array => ram.subarray(layout.name, layout.name + layout.nameLen);
/** The hunt's invariant: every byte of the name field is a terminator or printable ASCII. */
const nameFieldClean = (ram: Uint8Array): boolean => nameField(ram).every((c) => c === 0 || (c >= 0x20 && c <= 0x7e));

function construct(be: Backend, id: number, rom: { path: string; platform: Platform }, sav: Uint8Array): void {
  expect(
    be.constructSystem(
      { romPath: rom.path, platform: rom.platform, core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      id,
    ),
  ).toBeTruthy();
}

/** Advance `ms` in STEP_MS slices, handing every published snapshot to `see` (with the elapsed ms). */
function sample(be: Backend, audio: Audio, id: number, ms: number, see: (ram: Uint8Array, at: number) => void): void {
  for (let t = 0; t < ms; t += STEP_MS) {
    audio.renderAudio(STEP_MS);
    see(be.readRam(id)!, t + STEP_MS);
  }
}

/** Boot a fresh core to its main loop, sampling all the way, and return when the latch flipped. */
function bootSampled(be: Backend, audio: Audio, id: number, anomalies: string[]): number {
  let bootedAt = -1;
  let seen = 0;
  // Sample until the latch flips, then keep sampling well past it - a latch that flips back would be a
  // "booted" that was a coincidence, and the reads after it are the ones that matter.
  for (let t = 0; t < BOOT_LIMIT_MS + 3000; t += STEP_MS) {
    audio.renderAudio(STEP_MS);
    const ram = be.readRam(id)!;
    seen++;
    const booted = ram[layout.booted] === 1;
    if (bootedAt < 0) {
      if (booted) bootedAt = t + STEP_MS;
      else {
        // Before the latch: whatever the bytes are, the catalog must name nothing. The raw field is only
        // LOGGED here - a non-name in the boot window is precisely what the gate exists for.
        expect(smsggdjSongCatalog.workingName(be.readSram(id)!, ram)).toBe(null);
        expect(ram[layout.booted]).toBe(0); // never anything but 0 or 1
        if (!nameFieldClean(ram)) anomalies.push(`pre-boot t=${t + STEP_MS}ms name=[${hex(nameField(ram))}]`);
      }
    } else {
      // After it: stays 1, and the field is a name or nothing - at EVERY sample.
      expect(ram[layout.booted]).toBe(1);
      if (!nameFieldClean(ram)) throw new Error(`post-boot non-name at t=${t + STEP_MS}ms: [${hex(nameField(ram))}]`);
      if (t + STEP_MS >= bootedAt + 3000) break;
    }
  }
  expect(seen > 0).toBeTruthy();
  return bootedAt;
}

for (const rom of ROMS) {
  const tag = rom.platform;

  test(`[${tag}] ints_on is 0 from construction to the main loop, then 1 for good - and nothing before it is a song`, () => {
    const be = createRealBackend();
    if (!be.fileExists(rom.path)) skip(`sms-boot-timeline: missing ${rom.path}`);
    const id = 60;
    const sav = buildSav([{ block: buildMetronomeBlock(), name: "ALPHA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
    const audio = createAudioDriver();
    construct(be, id, rom, sav);

    // Frame 0: readable, and the boot's. This is the snapshot a project load used to act on.
    const frame0 = be.readRam(id)!;
    expect(frame0.length).toBe(0x2000);
    expect(frame0[layout.booted]).toBe(0);
    expect(u16(frame0, layout.frame)).toBe(0);
    expect(smsggdjSongCatalog.workingSongReady!(frame0)).toBe(false);
    expect(smsggdjSongCatalog.workingName(be.readSram(id)!, frame0)).toBe(null);

    const anomalies: string[] = [];
    const bootedAt = bootSampled(be, audio, id, anomalies);
    console.log(`[${tag}] ints_on flipped at ~${bootedAt} ms; pre-boot name-field anomalies: ${anomalies.length}`);
    for (const a of anomalies) console.log(`[${tag}]   ${a}`);
    expect(bootedAt > 500).toBeTruthy(); // the splash is real time, not a frame
    expect(bootedAt < BOOT_LIMIT_MS).toBeTruthy();

    // Up: ready, alive (the main-loop counter moves), and the blank song is a blank song.
    const up = be.readRam(id)!;
    expect(smsggdjSongCatalog.workingSongReady!(up)).toBe(true);
    const f1 = u16(up, layout.frame);
    audio.renderAudio(500);
    const f2 = u16(be.readRam(id)!, layout.frame);
    expect(f2 > f1).toBeTruthy();
    expect(f2 - f1 >= 20 && f2 - f1 <= 40).toBeTruthy(); // ~30 frames in 500 ms, either region
    expect(smsggdjSongCatalog.workingName(be.readSram(id)!, be.readRam(id)!)).toBe(null); // song_new's blank song, no name
    expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, be.readRam(id)!)).toBe(false);
    expect(be.removeSystem(id)).toBeTruthy();
  });

  test(`[${tag}] a song written before the latch is erased by the boot; one written after it stays`, () => {
    const be = createRealBackend();
    if (!be.fileExists(rom.path)) return;
    const id = 61;
    const sav = buildSav([{ block: buildMetronomeBlock(), name: "ALPHA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
    const audio = createAudioDriver();
    construct(be, id, rom, sav);
    const writes = smsggdjIntegration.liveLoad!(be.readFile(rom.path)!, sav, 0)!;
    const wanted = writes.find((w) => w.offset === layout.song)!.bytes;
    const pool = (ram: Uint8Array) => ram.subarray(layout.phrasePool, layout.phrasePool + layout.phrasePoolLen);
    const landed = (ram: Uint8Array) => pool(ram).every((b, i) => b === wanted[layout.phrasePool + i]);

    // The hazard: 200 ms in, the splash is on screen and the latch is 0. The write is accepted...
    audio.renderAudio(200);
    expect(be.readRam(id)![layout.booted]).toBe(0);
    for (const w of writes) expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
    audio.renderAudio(100);
    expect(landed(be.readRam(id)!)).toBe(true); // ...and it is there, for now
    expect(isSongSaved(be.readSram(id)!, be.readRam(id)!.subarray(0, SMDJ4_BLOCK_LEN))).toBe(true);

    // ...until the boot reaches song_new. Once the cart is up the block is the blank song again and the
    // name is rle_name_default's spaces: exactly what a Recent-row load produced, while reporting success.
    let bootedAt = -1;
    sample(be, audio, id, BOOT_LIMIT_MS, (ram, at) => {
      if (bootedAt < 0 && ram[layout.booted] === 1) bootedAt = at;
    });
    expect(bootedAt > 0).toBeTruthy();
    audio.renderAudio(200);
    const wiped = be.readRam(id)!;
    expect(landed(wiped)).toBe(false);
    expect(smsggdjSongCatalog.workingName(be.readSram(id)!, wiped)).toBe(null);
    expect(isSongSaved(be.readSram(id)!, wiped.subarray(0, SMDJ4_BLOCK_LEN))).toBe(false);

    // The fix's premise: the same write, after the latch, holds - through 5 s of the cart running.
    for (const w of writes) expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
    sample(be, audio, id, 5000, (ram) => {
      expect(landed(ram)).toBe(true);
      expect(nameFieldClean(ram)).toBe(true);
    });
    expect(smsggdjSongCatalog.workingName(be.readSram(id)!, be.readRam(id)!)).toBe("ALPHA");
    expect(be.removeSystem(id)).toBeTruthy();
  });

  test(`[${tag}] the name field stays a name through the cart's own FILES save + load, and a live load`, () => {
    const be = createRealBackend();
    if (!be.fileExists(rom.path)) return;
    const id = 62;
    // Boot BLANK (no directory), so the cart's own save has to build the SMDJ4 structure and copy the
    // name out of work RAM into the entry - the path where a snapshot could catch it half-copied.
    const audio = createAudioDriver();
    construct(be, id, rom, new Uint8Array(32 * 1024));
    const anomalies: string[] = [];
    expect(bootSampled(be, audio, id, anomalies) > 0).toBeTruthy();

    // Every snapshot from here on is post-boot: the invariant is asserted at each one. `see` is what
    // every step below samples through.
    let samples = 0;
    const see = (ram: Uint8Array, at: number): void => {
      samples++;
      expect(ram[layout.booted]).toBe(1);
      if (!nameFieldClean(ram)) throw new Error(`non-name during cart I/O at +${at}ms: [${hex(nameField(ram))}]`);
    };
    const pressSampled = (btn: number, down: boolean, ms: number): void => {
      audio.pressButton(id, btn, down);
      sample(be, audio, id, ms, see);
    };
    const tapS = (btn: number): void => {
      pressSampled(btn, true, TAP_MS);
      pressSampled(btn, false, GAP_MS);
    };
    const chordS = (hold: number, btn: number): void => {
      pressSampled(hold, true, TAP_MS);
      pressSampled(btn, true, TAP_MS);
      pressSampled(btn, false, TAP_MS);
      pressSampled(hold, false, GAP_MS);
    };

    // SONG -> FILES ("2 held + Down"), then SAVE: "2 held + 1" opens the action menu with SAVE selected,
    // one tap arms it, a second runs it (sms-sram.test.ts drives the same sequence).
    chordS(BTN_2, BTN_DOWN);
    expect(be.readRam(id)![SYM.scr_mode]).toBe(SCR_FILES);
    chordS(BTN_2, BTN_1);
    tapS(BTN_1);
    tapS(BTN_1);
    sample(be, audio, id, 1500, see); // rle_song_save + config_save
    expect(be.readRam(id)![SYM.file_count]).toBe(1);

    // A host-side live load of what was just saved, sampled through its landing.
    const sav = be.readSram(id)!;
    for (const w of smsggdjIntegration.liveLoad!(be.readFile(rom.path)!, sav, 0, be.readRam(id)!)!) {
      expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();
    }
    sample(be, audio, id, 1000, see);
    expect(smsggdjSongCatalog.workingSongDirty!(be.readSram(id)!, be.readRam(id)!)).toBe(false); // the cart's own copy, clean
    console.log(`[${tag}] ${samples} post-boot snapshots through FILES save + live load, all clean`);
    expect(be.removeSystem(id)).toBeTruthy();
  });
}
