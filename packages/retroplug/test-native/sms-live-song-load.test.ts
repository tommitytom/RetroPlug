// Loading an smsggdj song into a RUNNING cart, with no ROM change and no cold boot.
//
// This is what `writeRam` buys. smsggdj's working song is the live 6,912-byte work-RAM block at $C000
// and the cart boots blank on purpose, so the tracker-spine route (write the .sav, cold-boot, let the
// cart restore it) cannot reach it - that is why the Songs menu waits on a ROM that maintains a
// currently-loaded-slot byte. The control-plane RAM write is the other answer: decode the song here and
// put it straight into the core, while it plays.
//
// Two things are proven together, which is the point: the SMDJ4 codec produces a block the real cart
// accepts, and the write lands on a live core without stopping it.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildSav, readSongBlock, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_PHRASES, STEPS_PER_PHRASE, SMS_ROWS_PER_BEAT } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const PAUSE = 7;
const PHRASE_STEP = 0x1b02;
const PHRASE_STEPS = 16;
/** Region offset 0 IS CPU $C000: readRam serves the 8 KB SmsWorkRam region, and the working song sits at
 *  its base - the same coordinates pokeMetronomeIntoWram uses one writeCpu at a time. */
const SONG_AT = 0;

/** The metronome, with its note transposed - two songs a test can tell apart in RAM. */
function metronomeAtNote(note: number): Uint8Array {
  const b = buildMetronomeBlock();
  for (let step = 0; step < STEPS_PER_PHRASE; step += SMS_ROWS_PER_BEAT) b[P_PHRASES + step * 4] = note;
  return b;
}

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("a song decoded from a .sav loads into a PLAYING cart in one call", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-live-song-load: missing ${ROM}`);
    return;
  }

  // A real two-song battery, built and read back through the shipping codec - RLE, directory, heap and
  // all. Nothing here hand-rolls a block.
  const sav = buildSav(
    [
      { block: metronomeAtNote(0x0d), name: "LOW" },
      { block: metronomeAtNote(0x19), name: "HIGH" },
    ],
    32 * 1024,
    buildConfigBlock(SMS_SYNC_OFF),
  )!;
  const low = readSongBlock(sav, 0)!;
  const high = readSongBlock(sav, 1)!;
  expect(low.length).toBe(SMDJ4_BLOCK_LEN);
  expect(high).toEqual(metronomeAtNote(0x19)); // the codec round-trip is exact

  const audio = createAudioDriver();
  expect(
    be.constructSystem(
      {
        romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "",
        savPath: null, statePath: null, sramBytes: sav,
        settings: JSON.stringify({ enableFm: false }),
      },
      1,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // boot: splash, config_load, song_new (blank - the cart does not autoload)

  // Load song 0 and start it. One call replaces the 6,912 writeCpu round-trips the older fixtures make,
  // and unlike those it does not require a stopped audio thread.
  expect(be.writeRam(1, SONG_AT, low)).toBeTruthy();
  audio.pressButton(1, PAUSE, true);
  audio.renderAudio(100);
  audio.pressButton(1, PAUSE, false);
  audio.renderAudio(400);
  const playing = rms(audio.renderAudio(1500));
  const stepA = be.readRam(1)![PHRASE_STEP];
  console.log(`[live-load] song 0 playing rms=${playing.toFixed(5)}`);
  expect(playing > 0.001).toBeTruthy(); // the poked block is a song the real cart accepts

  // Now swap the song underneath it WHILE IT PLAYS. No stop, no reboot, no button press.
  expect(be.writeRam(1, SONG_AT, high)).toBeTruthy();
  audio.renderAudio(1500);

  const ram = be.readRam(1)!;
  const loaded = ram.subarray(SONG_AT, SONG_AT + SMDJ4_BLOCK_LEN);
  // The phrase pool is the part the sequencer reads and does not rewrite, so it is the honest place to
  // check the swap landed. (The cart owns other bytes in the block - cursor state and the like - so a
  // whole-block compare would be asserting something false about a running tracker.)
  const wantPhrases = high.subarray(P_PHRASES, P_PHRASES + STEPS_PER_PHRASE * 4);
  expect(loaded.subarray(P_PHRASES, P_PHRASES + STEPS_PER_PHRASE * 4)).toEqual(wantPhrases);

  // ...and the cart is still running, not wedged by having its song replaced mid-play.
  const stepB = be.readRam(1)![PHRASE_STEP];
  const after = rms(audio.renderAudio(1000));
  const stepC = be.readRam(1)![PHRASE_STEP];
  console.log(`[live-load] after swap rms=${after.toFixed(5)} steps ${stepA}->${stepB}->${stepC}`);
  expect(after > 0.001).toBeTruthy();
  expect(((stepC - stepB + PHRASE_STEPS) % PHRASE_STEPS) > 0).toBeTruthy(); // sequencer still advancing

  expect(be.removeSystem(1)).toBeTruthy();
});
