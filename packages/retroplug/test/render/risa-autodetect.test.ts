// risa (NES) song-length auto-detect: `runRenderJob` must render to the song's HFF stop, not a fixed
// window — the risa parity of LSDj's NR52 detection. risa signals the stop through its sequencer flag:
// when the last active track hits an HFF (HOP_STOP), the firmware sets seq_mode = SEQ_MODE_STOPPED
// (src/seq_reset_inst_state.s _seq_deactivate_track_x_note_off), which the pure runtime reader surfaces as
// `playing === false`. render.ts polls that per chunk via the readRam snapshot seam.
//
// We prove it deterministically with a mock backend + audio that model the timeline: silent until the
// SELECT+START play gesture, then a tone for SONG_MS, then the sequencer stops (seq_mode → 0). The render
// must trim at the stop — a length well under the cap, hff true, and a WAV exactly as long as the reported
// frame count. A real-core version lives in test-native/risa-render.ts. Mirrors start-truncation.test.ts.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { runRenderJob, decodeWav, type RenderContext, type RenderOpts } from "../../src/render";
import { runtime as risaRuntime } from "../../src/risa";

const SR = 44100;
const NES_SELECT = 6; // NesButton::Select — risa plays a song on SELECT+START; SELECT-down begins playback.
const SONG_MS = 1800; // the "song" plays this long, then all tracks HFF → seq_mode STOPPED
const SONG_FRAMES = Math.round((SONG_MS * SR) / 1000);
const CAP_MS = 5000; // maxDurationMs — the no-HFF fallback; a working detect must land well under it
const TONE = 0.2; // constant level while playing (RMS == TONE), silence otherwise

const RISA_ROM = "/roms/song.nes";
const OUT = "/out/song.wav";
const LAYOUT = risaRuntime.resolveRisaLayout("2.2.1")!; // committed symbol snapshot → seq_mode/seq_active offsets

// The shared playback clock both the mock audio (tone/silence) and the mock WRAM (seq_mode) read, so the
// audio the render captures and the "still playing" flag it polls move together — as they do on a real core.
const sim = { playing: false, playFrames: 0 };

/** A minimal risa-shaped .nes: the iNES 2.0 header fingerprint isRisaRomHeader wants (NES 2.0 + MMC5 +
 *  battery + 64 KB PRG-NVRAM) plus the "RISA V2.2.1" PRG marker identifyRisaVersion scans for. */
function risaRomBytes(): Uint8Array {
  const rom = new Uint8Array(0x4000);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // "NES\x1a"
  rom[6] = 0x52; // mapper low nibble 5 (MMC5) + battery (bit 1)
  rom[7] = 0x08; // NES 2.0 (flags7 bits 2-3 == 0b10), mapper mid nibble 0
  rom[8] = 0x00; // mapper high nibble 0
  rom[10] = 0xa0; // PRG-NVRAM 64 KB (high nibble 0x0a)
  const marker = "RISA V2.2.1";
  for (let i = 0; i < marker.length; i++) rom[0x20 + i] = marker.charCodeAt(i);
  return rom;
}

/** A mock AudioDriver: silence until the SELECT+START gesture, then a tone until the song reaches
 *  SONG_FRAMES, after which the sequencer stops and it falls silent. `playFrames` counts tone frames from
 *  the gesture, so the render's captured audio and reported length align with SONG_FRAMES. */
class RisaSpyAudio {
  renderAudio(ms: number): Float32Array {
    const frames = Math.round((ms * SR) / 1000);
    const out = new Float32Array(frames * 2); // interleaved stereo; NES mix takes the left lane
    for (let f = 0; f < frames; f++) {
      if (sim.playing && sim.playFrames >= SONG_FRAMES) sim.playing = false; // last track HFF'd → seq stopped
      const v = sim.playing ? TONE : 0;
      if (sim.playing) sim.playFrames++;
      out[f * 2] = v;
      out[f * 2 + 1] = v;
    }
    return out;
  }

  pressButton(_id: number, button: number, down: boolean): boolean {
    if (button === NES_SELECT && down) sim.playing = true; // SELECT+START begins song playback
    return true;
  }

  sampleRate(): number {
    return SR;
  }
}

/** MockBackend + a live seq_mode: readRam returns an internal-RAM snapshot whose seq_mode/seq_active bytes
 *  track `sim.playing`, exactly the WRAM the risa runtime reader decodes. */
class RisaMockBackend extends MockBackend {
  override readRam(_id: number): Uint8Array {
    const ram = new Uint8Array(0x800); // NES internal RAM ($0000-$07FF)
    ram[LAYOUT.seqMode] = sim.playing ? 1 : 0; // SEQ_MODE_SONG(1) while playing, SEQ_MODE_STOPPED(0) after
    ram[LAYOUT.seqActive] = sim.playing ? 0x1f : 0; // all 5 tracks active while playing
    return ram;
  }
}

function mockCtx(): { ctx: RenderContext; backend: RisaMockBackend } {
  const backend = new RisaMockBackend("/config");
  backend.seed(RISA_ROM, risaRomBytes());
  const ctx = {
    backend,
    audio: new RisaSpyAudio(),
    project: { systems: { addSystem: () => 1, view: () => [{ id: 1 }], adopt: () => {}, loadState: () => 1 } },
    dsp: {},
  } as unknown as RenderContext;
  return { ctx, backend };
}

const opts = (over: Partial<RenderOpts>): RenderOpts => ({
  rom: RISA_ROM,
  out: OUT,
  maxDurationMs: CAP_MS,
  split: "mix",
  transport: false,
  start: true, // auto-start — the SELECT+START gesture; with no durationMs this arms auto-detect
  listSongs: false,
  ...over,
});

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("risa render auto-detects the HFF stop (seq_mode) and trims to the song length", () => {
  sim.playing = false;
  sim.playFrames = 0;
  const { ctx, backend } = mockCtx();

  const res = runRenderJob(ctx, opts({}), { log: () => {}, warn: () => {} });

  console.log(`[risa-autodetect] hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames} (song ${SONG_MS}ms, cap ${CAP_MS}ms)`);

  // The stop was detected (not the cap): seq_mode → STOPPED ended the render at the song's HFF.
  expect(res.hff).toBe(true);
  expect(res.lengthMs !== undefined).toBe(true);
  expect(res.lengthMs! < CAP_MS - 500).toBe(true); // nowhere near the 5 s fallback
  expect(Math.abs(res.lengthMs! - SONG_MS) <= 150).toBe(true); // ≈ the true song length

  // The WAV is mono (NES mix) and exactly the reported song length — the silent tail (held off-chunks) trimmed.
  const wav = decodeWav(backend.readFile(OUT)!);
  expect(wav.channels).toBe(1);
  expect(wav.pcm.length).toBe(res.frames);
  expect(rms(wav.pcm) > 0.05).toBe(true); // the captured audio is the song, not silence
});

test("the render reports the audio duration rendered so far, not a fraction of the cap", () => {
  sim.playing = false;
  sim.playFrames = 0;
  const { ctx } = mockCtx();

  // What the tile badge counts. The cap is CAP_MS but the song stops at SONG_MS, so a fraction-of-cap
  // "progress" could only ever crawl to ~36% before the render finished - hence a duration instead.
  const reported: number[] = [];
  const res = runRenderJob(ctx, opts({}), { log: () => {}, warn: () => {}, onRendered: (ms) => reported.push(ms) });

  const stream = reported.slice(0, -1); // the per-chunk reports; the last one settles (below)
  expect(stream.length > 0).toBe(true);
  for (let i = 1; i < stream.length; i++) expect(stream[i] >= stream[i - 1]).toBe(true); // never goes backwards
  // Real audio time: the first report is the captured play gesture plus one 100 ms render chunk.
  expect(Math.round(stream[0])).toBe(200);
  // The stream runs a little past the song (the held off-chunks that prove the HFF stop are rendered audio
  // too), then the final report settles on the length actually written to the WAV.
  expect(stream[stream.length - 1] >= res.lengthMs!).toBe(true);
  expect(stream[stream.length - 1] - res.lengthMs! <= 200).toBe(true);
  expect(reported[reported.length - 1]).toBe(res.lengthMs);
  expect(Math.abs(res.lengthMs! - SONG_MS) <= 150).toBe(true);
});

test("a risa song that never HFFs falls back to the maxDuration cap (hff false)", () => {
  sim.playing = false;
  sim.playFrames = 0;
  const { ctx, backend } = mockCtx();

  // A looping song: it never reaches SONG_FRAMES within the cap, so seq_mode never returns to STOPPED.
  const noStopOpts = opts({ maxDurationMs: 800 }); // cap well under SONG_MS(1800) → the stop can't fire
  const res = runRenderJob(ctx, noStopOpts, { log: () => {}, warn: () => {} });

  console.log(`[risa-autodetect] no-HFF: hff=${res.hff} lengthMs=${res.lengthMs} frames=${res.frames}`);
  expect(res.hff).toBe(false); // hit the cap, not a detected stop
  expect(res.lengthMs !== undefined).toBe(true); // still the auto-detect path (reports a length)
  const wav = decodeWav(backend.readFile(OUT)!);
  expect(rms(wav.pcm) > 0.05).toBe(true); // everything kept (no trim) → the tone fills the cap
});
