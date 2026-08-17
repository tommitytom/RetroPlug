// FM and PSG both sound. The guard on the vendored SmsFmAudio change, and on the `FM Audio` menu row
// meaning what it says.
//
// Stock Mesen models port $F2 as a MUX after the Japanese SMS: writing $F2 = $01 selects FM and
// `SmsPsg::PlayQueuedAudio` memsets the whole PSG buffer (`SmsPsg.cpp:117-119`). smsggdj writes exactly
// that whenever its own FM option is on, so on stock Mesen switching FM on cost it three tone voices
// plus noise. Measured before the change, one PSG-instrument song, cart FM on: rms 0.05089 -> 0.00000,
// literal silence. A Mark III with the FM add-on sums instead, and smsggdj's own source says real
// hardware and SMSPlus sum while Emulicious muxes - so `SmsConfig::FmMutesPsg` (default true, stock)
// is set false by `configureSms` and RetroPlug sums.
//
// Why this needs a mixed song rather than the FM-only one that first proved the path works: an
// FM-instrument song is audible under BOTH models (the mux picks FM, the sum includes it), so it
// cannot tell them apart. Only a song playing PSG and FM AT ONCE distinguishes "both" from "FM won".
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import {
  buildMetronomeBlock,
  buildConfigBlock,
  buildSmdj4Sav,
  BLOCK_LEN,
  WRAM_BASE,
  P_PHRASES,
  P_CHAINS,
  P_SONG,
  P_INSTR,
  STEPS_PER_PHRASE,
  CHAIN_ENTRIES,
  HIT_NOTE,
  SMS_ROWS_PER_BEAT,
  SMS_SYNC_OFF,
} from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;
const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";

const PAUSE = 7;
/** smsggdj instrument types (editor.asm:27). 0 = TONE (a PSG voice), 4 = FM (a YM2413 voice). */
const TYPE_TONE = 0;
const TYPE_FM = 4;
/** An FM record's PROG byte (editor.asm:802 - "FM: INST TYPE VOL HLD(4) TSP(6) TBL(10) TBS(11) PROG(12)"). */
const FM_PROG_OFFSET = 12;
const FM_PROG = 1; // patch 1-15, as CMD_FMPROG selects

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

/** Which voices the song plays. `psg` puts a TONE instrument on channel 0 (T1), `fm` an FM instrument
 *  on channel 1 (T2), and `both` runs them together - the only combination that separates sum from mux. */
type Voices = "psg" | "fm" | "both";

function songBlock(voices: Voices): Uint8Array {
  // buildMetronomeBlock already gives phrase 0 / chain 0 / channel 0 with instrument 0 left at the
  // TONE default, so the PSG half needs nothing. The FM half is a second phrase, chain and channel.
  const b = buildMetronomeBlock();

  b[P_INSTR + 0 * 16] = TYPE_TONE; // already the metronome's default; written so the PSG half is explicit
  b[P_INSTR + 1 * 16] = TYPE_FM;
  b[P_INSTR + 1 * 16 + FM_PROG_OFFSET] = FM_PROG;

  // Phrase 1: the same rhythm on instrument 1. No kill row - an FM voice keys off on its own HLD, and
  // a K would pre-empt the attack exactly as it does on a PSG note (engine.asm:2020).
  for (let step = 0; step < STEPS_PER_PHRASE; step += SMS_ROWS_PER_BEAT) {
    b.set([HIT_NOTE, 0x01, 0x00, 0x00], P_PHRASES + (STEPS_PER_PHRASE + step) * 4);
  }
  for (let i = 0; i < CHAIN_ENTRIES; i++) b.set([0x01, 0x00], P_CHAINS + CHAIN_ENTRIES * 2 + i * 2);

  // Song rows: chain 0 on T1 (PSG), chain 1 on T2 (FM), per the requested combination.
  const t1 = voices === "fm" ? 0xff : 0x00;
  const t2 = voices === "psg" ? 0xff : 0x01;
  for (let row = 0; row < 8; row++) b.set([t1, t2, 0xff, 0xff], P_SONG + row * 4);
  return b;
}

// System ids are a per-host space and every case here shares one host: take a fresh id and drop the
// system afterwards, or the next measurement includes the previous song.
let nextId = 1;

/** Boot, poke the song into the running working song, press Play, and return the level. `cartFm` is
 *  smsggdj's OWN FM option (the OPTIONS block byte that makes it write $F2); `enableFm` is ours. */
function play(voices: Voices, enableFm: boolean, cartFm: number): number {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const id = nextId++;
  const block = songBlock(voices);

  expect(
    be.constructSystem(
      {
        romPath: ROM,
        platform: "sms",
        core: "mesen",
        embeddedRom: "",
        savPath: null,
        statePath: null,
        sramBytes: buildSmdj4Sav(block, buildConfigBlock(SMS_SYNC_OFF, cartFm)),
        settings: JSON.stringify({ enableFm }),
      },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(3000); // boot: splash, config_load (takes fm_on), song_new

  for (let i = 0; i < BLOCK_LEN; i++) {
    if (!be.writeCpu(id, WRAM_BASE + i, block[i])) throw new Error(`writeCpu failed at +0x${i.toString(16)}`);
  }

  audio.pressButton(id, PAUSE, true);
  audio.renderAudio(100);
  audio.pressButton(id, PAUSE, false);
  audio.renderAudio(500);
  const level = rms(audio.renderAudio(2500));
  expect(be.removeSystem(id)).toBeTruthy();
  return level;
}

test("FM on does not silence the PSG (the $F2 mux is off)", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-fm: missing ${ROM}`);
    return;
  }

  // The whole claim, and its own negative control: this exact configuration measured 0.00000 before
  // the vendored change, against 0.05089 with the cart's FM switched off. Nothing about the song, the
  // poke or the transport differs between those two numbers - only $F2.
  const psgFmOn = play("psg", true, 1);
  const psgFmOff = play("psg", true, 0);
  console.log(`[sms-fm] PSG song: cartFm=1 ${psgFmOn.toFixed(5)}  cartFm=0 ${psgFmOff.toFixed(5)}`);
  expect(psgFmOn > 0.001).toBeTruthy(); // was exactly 0 under the mux
  // ...and not merely non-zero: switching FM on must not COST the PSG anything either, so it has to
  // hold its own level. A partial duck would pass a bare "is it audible" check.
  expect(psgFmOn > psgFmOff * 0.8).toBeTruthy();
});

test("an FM instrument is audible, and only when both FM switches are on", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-fm: missing ${ROM}`);
    return;
  }
  const on = play("fm", true, 1);
  const cartOff = play("fm", true, 0); // ROM never writes $F2, so MixAudio mutes FM at _audioControl 0
  const hostOff = play("fm", false, 1); // our enableFm gates the $F0/$F1/$F2 writes at source
  console.log(`[sms-fm] FM song: on ${on.toFixed(5)}  cartOff ${cartOff.toFixed(5)}  hostOff ${hostOff.toFixed(5)}`);
  expect(on > 0.001).toBeTruthy(); // the YM2413 really is reaching the mix
  // Both switches still mean OFF. Unmuting the PSG must not have unmuted FM as a side effect - that
  // would make `System > FM Audio > OFF` a lie in the other direction.
  expect(cartOff < 0.001).toBeTruthy();
  expect(hostOff < 0.001).toBeTruthy();
});

test("a song playing PSG and FM at once sounds both", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP sms-fm: missing ${ROM}`);
    return;
  }
  // The measurement the other two cannot make. Under the mux this collapses to the FM-only level,
  // because the PSG half is memset away; under summing it has to carry both. The PSG voice is ~6x the
  // FM voice's level here, so those two outcomes are far apart and the threshold is not delicate.
  const both = play("both", true, 1);
  const fmOnly = play("fm", true, 1);
  const psgOnly = play("psg", true, 1);
  console.log(`[sms-fm] both ${both.toFixed(5)}  fmOnly ${fmOnly.toFixed(5)}  psgOnly ${psgOnly.toFixed(5)}`);
  expect(both > fmOnly * 2).toBeTruthy(); // not "FM won"
  expect(both > psgOnly * 0.8).toBeTruthy(); // ...and the PSG is carrying its full share
});
