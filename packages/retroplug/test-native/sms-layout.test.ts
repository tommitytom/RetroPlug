// Certifies the smsggdj work-RAM layout against the SHIPPED v0.45 ROM.
//
// The symbol snapshot in runtime/symbols.generated.ts comes from a LOCAL build of the smsggdj source,
// and that build is NOT byte-identical to the released binary (local md5 04696fa0…, the vendored ROM
// 3af4a0d1…). So the linker's label file alone does not prove the addresses fit the ROM users actually
// run - it proves they fit a ROM built here. This does the rest, the same way risa-230-layout does for
// cc65: it boots the released binary and asserts the addresses BEHAVE.
//
// The write that has to be certified is `echo_mode`. The block base is already proven by
// sms-live-song-load (poke at offset 0, the cart plays that song), and `song_name` is display-only - a
// wrong address there is cosmetic. But echo is eight bytes into live engine state, and it is audible:
// the echo pass replays delayed, attenuated copies of T1 onto T2/T3, so turning it on has to change the
// sound. A wrong address changes nothing, and fails here.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { smsggdjIntegration } from "../src/tracker/trackerIntegration";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { identifySmsggdjVersion } from "../src/smsggdj/romDetect";
import { buildSav, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const PAUSE = 7;
/** Echo settings as SMDJ4 stores them: mode, tap1, tap2, red1, red2, stereo, tsp1, tsp2. */
const echoOff = Uint8Array.of(0, 4, 8, 2, 4, 0, 0, 0);
const echoOn = Uint8Array.of(2, 4, 8, 2, 4, 0, 0, 0); // 2 = T2 + T3
const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

/** Boot the shipped ROM with a one-song battery whose entry carries `echo`, live-load it, play, and
 *  report the energy plus what the layout addresses read back as. */
function runWithEcho(be: ReturnType<typeof createRealBackend>, id: number, echo: Uint8Array) {
  const block = buildMetronomeBlock();
  const sav = buildSav([{ block, name: "ECHOTEST", echo }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, sramBytes: sav, settings: JSON.stringify({ enableFm: false }) },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // splash + config_load + song_new

  const writes = smsggdjIntegration.liveLoad!(be.readFile(ROM)!, sav, 0)!;
  expect(writes != null).toBeTruthy();
  for (const w of writes) expect(be.writeRam(id, w.offset, w.bytes)).toBeTruthy();

  audio.pressButton(id, PAUSE, true);
  audio.renderAudio(80);
  audio.pressButton(id, PAUSE, false);
  audio.renderAudio(600); // let the echo ring fill (64-tick history)
  const energy = rms(audio.renderAudio(2500));
  const ram = be.readRam(id)!;
  expect(be.removeSystem(id)).toBeTruthy();
  return { energy, ram };
}

test("echo_mode is where the symbols say - turning echo on changes the sound", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-layout: missing ${ROM}`);
    return;
  }
  // The shipped ROM self-identifies, and resolves a layout - which is what isVersionSupported gates on.
  const version = identifySmsggdjVersion(be.readFile(ROM)!);
  expect(version).toBe("0.45");
  const layout = resolveSmsggdjLayout(version)!;
  expect(layout != null).toBeTruthy();

  const off = runWithEcho(be, 1, echoOff);
  const on = runWithEcho(be, 2, echoOn);
  const delta = (on.energy - off.energy) / off.energy;
  console.log(`[sms-layout] echo off rms=${off.energy.toFixed(5)}  on rms=${on.energy.toFixed(5)}  ${(delta * 100).toFixed(1)}%`);

  // Both must actually be playing - a silent pair would make the comparison meaningless.
  expect(off.energy > 0.001).toBeTruthy();
  expect(on.energy > 0.001).toBeTruthy();
  // Echo adds delayed copies on two more channels, so the energy must MOVE. Writing to a wrong address
  // leaves the engine's real echo_mode at whatever the cart booted with, and the two runs come out equal.
  expect(Math.abs(delta) > 0.02).toBeTruthy();

  // ...and the byte we wrote is readable back at the layout offset, on the same region readRam serves.
  expect(on.ram[layout.echo]).toBe(2);
  expect(off.ram[layout.echo]).toBe(0);
});

test("song_name and song_edited land where the symbols say", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) return;
  const { ram } = runWithEcho(be, 3, echoOff);
  const layout = resolveSmsggdjLayout("0.45")!;

  // The name travelled from the DIRECTORY ENTRY into the cart's own song_name - the field that is not in
  // the block, and the reason the layout exists rather than a bare offset-0 poke.
  const name = String.fromCharCode(...ram.subarray(layout.name, layout.name + layout.nameLen)).replace(/\0+$/, "").trim();
  expect(name).toBe("ECHOTEST");

  // The cart's own load clears its dirty flag; a live load has to leave the same state behind, or the
  // cart believes a freshly loaded song already has unsaved edits.
  expect(ram[layout.edited]).toBe(0);

  // The block still leads work RAM - the assumption the generator asserts, re-checked against the core.
  expect(layout.song).toBe(0);
  expect(ram.length >= SMDJ4_BLOCK_LEN).toBeTruthy();
});
