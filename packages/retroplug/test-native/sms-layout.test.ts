// Certifies the smsggdj work-RAM layout against the SHIPPED v0.45 ROM.
//
// The symbol snapshot in runtime/symbols.generated.ts comes from a LOCAL build of the smsggdj source,
// and that build is NOT byte-identical to the released binary (local md5 04696fa0…, the vendored ROM
// 3af4a0d1…). So the linker's label file alone does not prove the addresses fit the ROM users actually
// run - it proves they fit a ROM built here. This does the rest, the same way risa-230-layout does for
// cc65: it boots the released binary and asserts the addresses BEHAVE.
//
// The write that has to be certified is `echo_mode`: eight bytes into live engine state, and audible.
// (The block base is already proven by sms-live-song-load - poke at offset 0, the cart plays that song -
// and `song_name` is display-only, so a wrong address there is cosmetic.) The song runs on T1 alone, so
// T2 and T3 make a sound ONLY because the echo pass replays onto them, which is exactly what echo_mode
// selects. Polling which channels the engine drives is a binary; a wrong address leaves T2/T3 silent in
// all three modes and fails.
//
// It measures channels rather than loudness on purpose. Comparing total energy across windows is not
// reproducible here - writeRam lands between blocks, so the echo ring's contents at the moment of the
// change vary run to run, and the same three states swung between -43% and +99% of baseline across
// repeats. The channel set does not move: three runs give T2=f/T2=2/T2=2, T3=f/T3=f/T3=4, every time.
//
// The emulation underneath is fully deterministic - renderAudio steps whole blocks off a sample count,
// with no clock in it, and SMS work RAM powers on zeroed - so repeated runs of this file are identical
// byte for byte. If it ever fails ONLY under the parallel suite, suspect a resource shared between host
// PROCESSES rather than anything here: it did exactly that until Mesen's scratch folder was given a pid
// (MesenGlobalInit.cpp), because every concurrent host was staging its ROM over the same /tmp path.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { identifySmsggdjVersion } from "../src/smsggdj/romDetect";
import { buildSav, SMDJ4_BLOCK_LEN, songLengthRows } from "../src/smsggdj/codec/sav";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_SONG } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const PAUSE = 7;

/** Render until the cart's OWN boot latch says the main loop is running, and answer how long that took.
 *
 *  This used to be `renderAudio(3000)`, a stopwatch - which is precisely the bug the rest of this branch
 *  went and removed from the app. The shipped v0.45 flips `ints_on` at ~2.3 s, so the constant carried
 *  700 ms of margin, no statement of what it was waiting FOR, and no way to notice when it ran out: a
 *  slower boot (v0.46 does an autoload before the loop) turns every write below into a write the boot
 *  then erases, and the test fails on an attenuation reading four steps downstream. Waiting on the latch
 *  asks the cart, and a cart that never answers fails saying so.
 *
 *  The slicing is deliberate at 256 ms = 12 whole 1024-frame render blocks at the host's 48 kHz, so the
 *  poll steps the core exactly as one long render would. Nothing here asserts that, but it keeps this
 *  change from moving the audio measurements it feeds. */
function bootCart(be: ReturnType<typeof createRealBackend>, audio: ReturnType<typeof createAudioDriver>, id: number, booted: number): number {
  const SLICE = 256;
  const BUDGET = 8000; // ~3.5x the shipped v0.45 boot; a real regression is an infinite wait, not a slow one
  for (let t = 0; t < BUDGET; t += SLICE) {
    audio.renderAudio(SLICE);
    const ram = be.readRam(id);
    if (ram && ram[booted] === 1) return t + SLICE;
  }
  const ram = be.readRam(id);
  throw new Error(
    `cart never reached its main loop within ${BUDGET} ms: ints_on=${ram ? ram[booted] : "no ram"} ` +
      `(ram ${ram ? ram.length : 0} bytes). A ROM that failed to load reads as a permanently unbooted cart.`,
  );
}
/** Echo settings as SMDJ4 stores them: mode, tap1, tap2, red1, red2, stereo, tsp1, tsp2. */
const echoOff = Uint8Array.of(0, 4, 8, 2, 4, 0, 0, 0);
const echoOn = Uint8Array.of(2, 4, 8, 2, 4, 0, 0, 0); // 2 = T2 + T3
test("echo_mode is where the symbols say - changing that byte changes the sound", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) skip(`sms-layout: missing ${ROM}`);
  // The shipped ROM self-identifies, and resolves a layout - which is what isVersionSupported gates on.
  const version = identifySmsggdjVersion(be.readFile(ROM)!);
  expect(version).toBe("0.45");
  const layout = resolveSmsggdjLayout(version)!;
  expect(layout != null).toBeTruthy();

  // ONE core, held running, with a single byte changed between measurements. Booting a fresh system per
  // state and comparing across them was the obvious design and it is not reproducible: only the FIRST
  // measurement in a host process came out the same twice, so cross-system comparisons swung by 80% of
  // the baseline run to run. Staying on one core removes phase, ring history and construct order from
  // the comparison entirely, and it is a sharper test of the address anyway - nothing changes between
  // the windows except the byte under test.
  const block = buildMetronomeBlock();
  const sav = buildSav([{ block, name: "ECHOTEST", echo: echoOff }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      1,
    ),
    "constructSystem (a false here is Mesen refusing the ROM, not a layout problem)",
  ).toBeTruthy();
  console.log(`[sms-layout] booted after ${bootCart(be, audio, 1, layout.booted)} ms`); // splash + config_load + song_new
  for (const w of smsggdjIntegration.liveLoad!(be.readFile(ROM)!, sav, 0)!) expect(be.writeRam(1, w.offset, w.bytes)).toBeTruthy();
  audio.pressButton(1, PAUSE, true);
  audio.renderAudio(80);
  audio.pressButton(1, PAUSE, false);
  audio.renderAudio(1200); // settle, and let the 64-tick echo history fill

  // Everything below reads as silence if the transport never started, and "T1 is silent" is a long way
  // from "PAUSE did not take". Say which it is here, once, before the three measurements.
  expect(be.readRam(1)![layout.playState] !== 0, "the cart is not playing - the PAUSE tap did not start the transport").toBeTruthy();

  /** Set echo_mode alone, then poll which PSG channels the ENGINE actually drives, as the loudest
   *  attenuation each one reaches (0 loud .. $F silent).
   *
   *  Deliberately not an RMS comparison. Total energy across separate windows is not reproducible here -
   *  writeRam lands between blocks, so the echo ring's contents at the moment of the change vary run to
   *  run, and the same three states swung between -43% and +99% of baseline across repeats. Which
   *  channels are audible at all is a BINARY the engine either does or does not set, so polling for it
   *  over many frames is immune to that jitter and is a sharper statement of what echo_mode means. */
  const loudestPerChannel = (mode: number): number[] => {
    expect(be.writeRam(1, layout.echo, Uint8Array.of(mode))).toBeTruthy();
    audio.renderAudio(1500); // let the change take, and the 64-tick ring turn over
    const min = [0xf, 0xf, 0xf, 0xf];
    for (let i = 0; i < 120; i++) {
      audio.renderAudio(25);
      const v = be.readRam(1)!;
      for (let ch = 0; ch < layout.psgVolsLen; ch++) min[ch] = Math.min(min[ch], v[layout.psgVols + ch] & 0x0f);
    }
    return min;
  };

  // The song plays on T1 only, so T2 and T3 are audible ONLY because echo replays onto them - which is
  // exactly what echo_mode selects (0 off, 1 = T2, 2 = T2+T3). Three states, and each has to differ from
  // the others: a byte we merely perturb could change the mix by accident, but only the engine's real
  // echo_mode turns those specific channels on one at a time.
  const off = loudestPerChannel(0);
  const t2 = loudestPerChannel(1);
  const both = loudestPerChannel(2);
  const show = (m: number[]) => `T1=${m[0].toString(16)} T2=${m[1].toString(16)} T3=${m[2].toString(16)}`;
  console.log(`[sms-layout] loudest attenuation  off: ${show(off)}   mode1: ${show(t2)}   mode2: ${show(both)}`);

  expect(off[0] < 0xf, "echo off: T1 plays the song in every case").toBeTruthy();
  expect(off[1], "echo off: T2 never makes a sound").toBe(0xf);
  expect(off[2], "echo off: T3 never makes a sound").toBe(0xf);
  expect(t2[1] < 0xf, "mode 1: T2 is audible").toBeTruthy();
  expect(t2[2], "mode 1: T3 is still silent").toBe(0xf);
  expect(both[1] < 0xf, "mode 2: T2 is audible").toBeTruthy();
  expect(both[2] < 0xf, "mode 2: T3 is audible too").toBeTruthy();

  expect(be.readRam(1)![layout.echo], "echo_mode reads back on the region readRam serves").toBe(2);
  expect(be.removeSystem(1)).toBeTruthy();
});

test("a load under a RUNNING transport rebases eng_len onto the new song", () => {
  // The cart's own load calls load_rebase, which rescans the new song's grid for its wrap point. Skipping
  // it is not a passing glitch: eng_len IS the wrap point, so the sequencer would loop at the previous
  // song's length indefinitely. This proves the rebase reaches the live engine, on the shipped ROM.
  const be = createRealBackend();
  if (!be.fileExists(ROM)) return;
  const layout = resolveSmsggdjLayout("0.45")!;
  const rom = be.readFile(ROM)!;

  // Two songs of different LENGTH: the fixture's 8 song rows, and a 2-row cut of it.
  const long = buildMetronomeBlock();
  const short = buildMetronomeBlock();
  short.fill(0xff, P_SONG, P_SONG + 128 * 4);
  for (let row = 0; row < 2; row++) short.set([0x00, 0xff, 0xff, 0xff], P_SONG + row * 4);
  expect(songLengthRows(long)).toBe(8);
  expect(songLengthRows(short)).toBe(2);

  const sav = buildSav([{ block: long, name: "LONG" }, { block: short, name: "SHORT" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      5,
    ),
    "constructSystem (a false here is Mesen refusing the ROM, not a layout problem)",
  ).toBeTruthy();
  bootCart(be, audio, 5, layout.booted);

  // Load the LONG song while stopped, then start the transport.
  for (const w of smsggdjIntegration.liveLoad!(rom, sav, 0, be.readRam(5) ?? undefined)!) expect(be.writeRam(5, w.offset, w.bytes)).toBeTruthy();
  audio.pressButton(5, PAUSE, true);
  audio.renderAudio(80);
  audio.pressButton(5, PAUSE, false);
  audio.renderAudio(500);

  const playingRam = be.readRam(5)!;
  expect(playingRam[layout.playState] !== 0, "the cart is not playing - the PAUSE tap did not start the transport").toBeTruthy();
  expect(playingRam[layout.engLen], "the cart's own scan agrees with songLengthRows").toBe(8);

  // Now load the SHORT song UNDER the running transport. Without the rebase, eng_len stays 8.
  const writes = smsggdjIntegration.liveLoad!(rom, sav, 1, playingRam)!;
  expect(writes.some((w) => w.offset === layout.engLen)).toBeTruthy(); // the rebase fired
  for (const w of writes) expect(be.writeRam(5, w.offset, w.bytes)).toBeTruthy();
  audio.renderAudio(400);

  const after = be.readRam(5)!;
  console.log(`[sms-layout] eng_len 8 -> ${after[layout.engLen]} across a load under a running transport`);
  expect(after[layout.engLen], "eng_len wraps at the NEW song's length").toBe(2);
  expect(after[layout.playState] !== 0, "...and the transport survived the load").toBeTruthy();

  // The negative control: with no RAM passed, liveLoad assumes stopped and emits no rebase at all - so a
  // regression that dropped the play_state check would show up as this set being identical.
  expect(smsggdjIntegration.liveLoad!(rom, sav, 1)!.some((w) => w.offset === layout.engLen)).toBe(false);
  expect(be.removeSystem(5)).toBeTruthy();
});

test("song_name and song_edited land where the symbols say", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) return;
  const layout = resolveSmsggdjLayout("0.45")!;
  const sav = buildSav([{ block: buildMetronomeBlock(), name: "ECHOTEST", echo: echoOn }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      6,
    ),
    "constructSystem (a false here is Mesen refusing the ROM, not a layout problem)",
  ).toBeTruthy();
  bootCart(be, audio, 6, layout.booted);
  for (const w of smsggdjIntegration.liveLoad!(be.readFile(ROM)!, sav, 0)!) expect(be.writeRam(6, w.offset, w.bytes)).toBeTruthy();
  audio.renderAudio(50);
  const ram = be.readRam(6)!;

  // The name travelled from the DIRECTORY ENTRY into the cart's own song_name - the field that is not in
  // the block, and the reason the layout exists rather than a bare offset-0 poke.
  const name = String.fromCharCode(...ram.subarray(layout.name, layout.name + layout.nameLen)).replace(/\0+$/, "").trim();
  expect(name, "the directory entry's name reached the cart's own song_name").toBe("ECHOTEST");

  // The cart's own load clears its dirty flag; a live load has to leave the same state behind, or the
  // cart believes a freshly loaded song already has unsaved edits.
  expect(ram[layout.edited], "a live load leaves song_edited clear, as the cart's own load does").toBe(0);

  // The block still leads work RAM - the assumption the generator asserts, re-checked against the core.
  expect(layout.song, "the song block still leads work RAM").toBe(0);
  expect(ram.length >= SMDJ4_BLOCK_LEN, "readRam serves at least a whole song block").toBeTruthy();
  expect(be.removeSystem(6)).toBeTruthy();
});
