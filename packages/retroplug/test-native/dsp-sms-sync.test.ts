// The TS `sms-sync` role, running IN the real DSP kernel, is the SOLE clock for a real smsggdj core.
// The Master System counterpart of dsp-risa-sync / dsp-lsdj-midisync, and the end-to-end proof that
// the controller-port transport lands: nothing here fakes the transport shape, so the ROM's own
// `sync_in_delta` path has to recover a clock from levels held on port 2 exactly as GGSYNC.md says.
//
// Two guards, and the ORDER matters. The fixture is authored rather than vendored, so a bug in it
// would surface downstream as "sync doesn't work" and send the search to entirely the wrong place.
// Guard A therefore proves the song plays with sync OFF, using nothing but the ROM's own transport.
// Only once that holds does Guard B ask whether the host can drive it.
//
// smsggdj in a slave sync mode is a pure slave: on its own Play it latches the live line state, sets
// `sync_wait`, and shows WAIT until the first host clock (engine.asm:383-388). So the negative case
// (transport running, role absent) is an ARMED, WILLING ROM sitting silent - which is a much sharper
// control than an idle one.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import {
  buildSmsMetronomeSav,
  pokeMetronomeIntoWram,
  SMS_SYNC_OFF,
  SMS_SYNC_IN24,
} from "./smsSyncSong";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";

// smsggdj's Play/Stop. The manual lists both "2 hold + 1 tap" and the PAUSE button; PAUSE is one press
// and maps to the shared wire byte 7 (SmsButton::Start -> the console's Pause switch / Z80 NMI).
const PAUSE = 7;

// This system's pipeline: the sync role, or nothing at all (the negative control).
const withSync = (id: number) => ({ systems: [{ id, pipeline: [{ kind: "sms-sync", config: {} }] }] });
const noRoles = (id: number) => ({ systems: [{ id, pipeline: [] }] });

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

// smsggdj's current phrase step (0..15), the row the sequencer is on. Located the same way as the
// sync vars below - by watching which WRAM bytes advance while a song plays - and confirmed by its
// RATE: it stepped 4 rows per 500 ms at 120 bpm, which is exactly IN24's four rows per quarter note.
//
// This is what the guards below measure instead of audio level, and the difference is not cosmetic.
// A role that emitted levels without ADVANCING the counter still delivers one clock (the arm
// transition), so the ROM plays a single row and holds it - which reads as a healthy fraction of the
// running RMS. Position cannot be fooled that way: a stalled sequencer does not move.
const PHRASE_STEP = 0x1b02;
const PHRASE_STEPS = 16;

function phraseStep(be: ReturnType<typeof createRealBackend>, id: number): number | null {
  const r = be.readRam(id);
  return r && r.length > PHRASE_STEP ? r[PHRASE_STEP] : null;
}

/** Render `chunks` x `chunkMs` and return how many phrase rows the sequencer advanced, summing
 *  per-chunk deltas so the 0..15 wraparound is unambiguous. */
function rowsAdvanced(
  be: ReturnType<typeof createRealBackend>,
  audio: ReturnType<typeof createAudioDriver>,
  id: number,
  chunkMs: number,
  chunks: number,
): number {
  let prev = phraseStep(be, id) ?? 0;
  let total = 0;
  for (let i = 0; i < chunks; i++) {
    audio.renderAudio(chunkMs);
    const now = phraseStep(be, id) ?? prev;
    total += (now - prev + PHRASE_STEPS) % PHRASE_STEPS;
    prev = now;
  }
  return total;
}

// smsggdj's sync state in WRAM. engine.asm:148-153 declares these five contiguously
// (sync_mode, sync_cnt, sync_last, sync_wait, sync_acc), and the base was located empirically by
// diffing a sync-OFF boot against a sync-IN24 one - the single byte that reads 0 in one and 5 in the
// other. Reading them turns "it made a noise" into a statement about the ROM's actual state machine,
// which is what distinguishes a working transport from a lucky one.
const SYNC_VARS = 0x1b2d;
interface SyncState { mode: number; cnt: number; last: number; wait: number; acc: number; }
function syncState(be: ReturnType<typeof createRealBackend>, id: number): SyncState | null {
  const r = be.readRam(id);
  if (!r || r.length <= SYNC_VARS + 4) return null;
  return { mode: r[SYNC_VARS], cnt: r[SYNC_VARS + 1], last: r[SYNC_VARS + 2], wait: r[SYNC_VARS + 3], acc: r[SYNC_VARS + 4] };
}

// System ids are a PER-HOST space, and every case in this file shares one host - so each boot has to
// take a fresh id AND drop its system afterwards. Two distinct traps, both of which cost a detour the
// first time round: reusing an id silently constructs OVER the previous system rather than making a
// second, and leaving a system alive means the next case's audio measurement includes it, so a
// negative control that should read silence reads the previous case's song instead.
let nextSystemId = 1;

// Boot smsggdj with the metronome fixture and poke the song into the running core.
// `enableFm: false` is not optional here: smsggdj writes $F2 = $01 when its FM option is on, and Mesen
// models $F2 as a mux whose PSG branch memsets the buffer, so an FM-routed core renders silence no
// matter how well the sync works. The fixture's OPTIONS block sets fm_on = 0 as well.
function boot(syncMode: number) {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const id = nextSystemId++;
  const ok = be.constructSystem({
    romPath: ROM,
    platform: "sms",
    core: "mesen",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: buildSmsMetronomeSav(syncMode),
    settings: JSON.stringify({ enableFm: false }),
  }, id);
  expect(ok).toBeTruthy();
  audio.renderAudio(3000); // boot: splash, config_load, song_new
  const writes = pokeMetronomeIntoWram(be, id);
  expect(writes > 0).toBeTruthy();
  return { be, audio, id };
}

// Tap the ROM's Play. In a slave mode this ARMS it (WAIT) rather than starting it.
function pressPlay(audio: ReturnType<typeof createAudioDriver>, id: number): void {
  audio.pressButton(id, PAUSE, true);
  audio.renderAudio(100);
  audio.pressButton(id, PAUSE, false);
  audio.renderAudio(100);
}

test("GUARD A: the authored song plays on the ROM's own transport, with sync OFF", () => {
  // Proves the fixture end to end - SMDJ4 image, OPTIONS block, WRAM layout, instrument defaults and
  // the metronome pattern - with ZERO sync involved. If this fails, nothing below is about sync.
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP dsp-sms-sync: missing ${ROM}`);
    return;
  }
  const { be: be2, audio, id } = boot(SMS_SYNC_OFF);

  const idleRows = rowsAdvanced(be2, audio, id, 500, 3);
  const idle = rms(audio.renderAudio(500)); // poked but not started
  pressPlay(audio, id);
  const playRows = rowsAdvanced(be2, audio, id, 500, 6);
  const playing = rms(audio.renderAudio(500));

  console.log(`[dsp-sms-sync] guardA idle=${idle.toFixed(5)}/${idleRows}rows playing=${playing.toFixed(5)}/${playRows}rows`);
  expect(idleRows).toBe(0); // a loaded song does not advance until asked
  expect(idle < 0.001).toBeTruthy(); // ...and makes no sound
  expect(playing > 0.001).toBeTruthy(); // the ROM's own clock plays it
  expect(playRows > 0).toBeTruthy(); // and its sequencer runs
  expect(be2.removeSystem(id)).toBeTruthy(); // leave the mix silent for the next case
});

test("GUARD B: the sms-sync role in the DSP kernel is the sole clock that makes smsggdj play", () => {
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) {
    console.log(`# SKIP dsp-sms-sync: missing ${ROM}`);
    return;
  }
  const { be, audio, id } = boot(SMS_SYNC_IN24);
  const dsp = createDspRuntime();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();

  // The fixture's OPTIONS block really did reach the ROM: config_load ran at boot and took IN24.
  // Without this the negative control below would pass for the wrong reason (a ROM that is simply
  // stopped looks exactly like one that is armed and unclocked).
  expect(syncState(be, id)?.mode).toBe(SMS_SYNC_IN24);

  pressPlay(audio, id); // arms: latches the lines, sets sync_wait, shows WAIT
  audio.renderAudio(200);
  const armed = syncState(be, id)!;
  console.log(`[dsp-sms-sync] armed: wait=${armed.wait} last=${armed.last} acc=${armed.acc}`);
  expect(armed.wait).toBe(1); // WAIT: armed, holding for the first host clock
  expect(armed.acc).toBe(5); // the divisor-1 head start, so the first clock plays row 0 (engine.asm:377)
  expect(armed.last).toBe(3); // latched from the idle lines (0xFF = both counter bits high = 3)

  audio.setBpm(120);
  audio.setTransport(true);

  // Negative: no role -> no levels -> the counter never moves -> `(current - last) & 3` stays 0 and the
  // ROM sits in WAIT, even though it is armed and the DAW transport is running. This ALSO proves
  // config_load applied the fixture's sync_mode: with sync OFF the ROM would already be playing here,
  // which is exactly what Guard A shows.
  expect(dsp.setSystems(noRoles(id))).toBeTruthy();
  const negRows = rowsAdvanced(be, audio, id, 500, 6);
  const neg = rms(audio.renderAudio(500));
  expect(syncState(be, id)?.wait).toBe(1); // still WAIT - nothing clocked it

  // Positive: the role steps the counter at 24 PPQN -> the ROM recovers clocks -> the sequencer runs.
  expect(dsp.setSystems(withSync(id))).toBeTruthy();
  const posRows = rowsAdvanced(be, audio, id, 500, 6);
  const pos = rms(audio.renderAudio(500));

  // 3 s at 120 bpm is 6 beats, and IN24 puts 4 rows in each, so 24 rows. Checking the RATE rather than
  // just "it moved" makes this a tempo check too: a role clocking at the wrong PPQN would still make
  // noise, and would still advance, but not by this much.
  console.log(`[dsp-sms-sync] guardB neg=${neg.toFixed(5)}/${negRows}rows pos=${pos.toFixed(5)}/${posRows}rows`);
  expect(negRows).toBe(0); // armed, transport running, unclocked -> the sequencer never moves
  expect(neg < 0.001).toBeTruthy(); // and it is silent
  expect(pos > 0.001).toBeTruthy(); // the kernel's level stream advances smsggdj -> audible
  expect(Math.abs(posRows - 24) <= 2).toBeTruthy(); // ~24 rows: one clock stream at the DAW's tempo
  expect(syncState(be, id)?.wait).toBe(0); // WAIT -> PLAY, flipped by the role's first clock
  expect(be.removeSystem(id)).toBeTruthy();
});

test("stopping the DAW transport stops the song, and restarting it resumes", () => {
  const be0 = createRealBackend();
  if (!be0.fileExists(ROM)) {
    console.log(`# SKIP dsp-sms-sync: missing ${ROM}`);
    return;
  }
  const { be, audio, id } = boot(SMS_SYNC_IN24);
  const dsp = createDspRuntime();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(withSync(id))).toBeTruthy();

  pressPlay(audio, id);
  audio.setBpm(120);
  audio.setTransport(true);
  const play = rms(audio.renderAudio(3000));

  // There is no stop MESSAGE in this protocol - the clocks simply cease, so the ROM's per-frame delta
  // goes to 0 and it STALLS. Note that stalling is not silence: the tracker holds whatever note was
  // sounding when the clocks stopped, so a small steady level remains (measured ~2% of the running
  // level). Asserting a hard zero here would be asserting something false about how a tracker stops,
  // so the check is that the metronome stopped ADVANCING, by a wide margin.
  audio.setTransport(false);
  audio.renderAudio(500); // let the transport change settle
  const stoppedRows = rowsAdvanced(be, audio, id, 500, 6);
  const stopped = rms(audio.renderAudio(500));

  audio.setTransport(true);
  const againRows = rowsAdvanced(be, audio, id, 500, 6);
  const again = rms(audio.renderAudio(500));

  console.log(`[dsp-sms-sync] play=${play.toFixed(5)} stopped=${stopped.toFixed(5)}/${stoppedRows}rows again=${again.toFixed(5)}/${againRows}rows`);
  expect(play > 0.001).toBeTruthy();
  expect(stoppedRows).toBe(0); // no clocks -> the sequencer stops dead
  expect(stopped < play / 10).toBeTruthy(); // what is left is a held note, not a running song
  expect(againRows > 0).toBeTruthy(); // clocks resume -> so does the song
  expect(again > play / 2).toBeTruthy();
  expect(be.removeSystem(id)).toBeTruthy();
});
