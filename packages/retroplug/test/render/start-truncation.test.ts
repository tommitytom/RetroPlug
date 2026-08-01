// Regression: `runRenderJob` (the shared CLI + UI render path) must NOT drop the first frames of the
// recording. The bug: after pressing Start — which begins playback — render.ts renders ~100 ms with the
// button held (`renderAudio(100)`) purely to register the press, and THROWS THAT AUDIO AWAY; capture only
// starts afterward. So the first ~100 ms of the song (which began on the press) never reach the WAV.
//
// We prove it host-neutrally with a mock AudioDriver that models the real timing: SILENT until the Start
// press (LSDj boots to a silent menu), then a deterministic ramp once "playing". The ramp's value at the
// first captured WAV sample tells us exactly how many already-playing frames were discarded before capture.
// A correct render captures from the press → first sample ≈ the song's first sample (≈ BASE, drop ≈ 0).
// The current code discards the button-hold chunk → first sample is ~100 ms up the ramp (drop ≈ 4410).
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { runRenderJob, decodeWav, type RenderContext, type RenderOpts } from "../../src/render";

const SR = 44100;
const GB_START = 7; // GameboyButton::Start — LSDj/mGB begin playback on a Start press (matches render.ts).

// The "song" signal: silence before Start, then a slow ramp from BASE, +SLOPE per playing frame. The slope
// is tiny relative to the ~4410-frame (100 ms) gap yet far above int16 quantization (~3e-5), so the first
// captured sample recovers the dropped-frame count to within a frame, with no clipping over a short render.
const BASE = 0.05;
const SLOPE = 2e-5;
const songSample = (playFrame: number): number => BASE + playFrame * SLOPE;

/** A mock AudioDriver: emits silence until Start is pressed, then the ramp above. `playFrames` counts frames
 *  emitted since playback began — so `songSample(0)` is the true first sample of the song. */
class SpyAudio {
  private playing = false;
  private playFrames = 0; // frames emitted while playing (monotonic from the Start press)

  renderAudio(ms: number): Float32Array {
    const frames = Math.round((ms * SR) / 1000);
    const out = new Float32Array(frames * 2); // interleaved stereo, as the real driver returns
    for (let f = 0; f < frames; f++) {
      const v = this.playing ? songSample(this.playFrames++) : 0;
      out[f * 2] = v;
      out[f * 2 + 1] = v;
    }
    return out;
  }

  pressButton(_id: number, button: number, down: boolean): boolean {
    if (button === GB_START && down) this.playing = true; // the song starts here (LSDj Start = play toggle)
    return true;
  }

  sampleRate(): number {
    return SR;
  }
}

/** A RenderContext over the mock backend + mock audio. buildSystem's GB path only needs
 *  `project.systems.addSystem` to hand back an id; dsp is untouched on that branch. */
function mockCtx(): { ctx: RenderContext; backend: MockBackend } {
  const backend = new MockBackend("/config");
  const ctx = {
    backend,
    audio: new SpyAudio(),
    project: { systems: { addSystem: () => 1, view: () => [{ id: 1 }], adopt: () => {}, loadState: () => 1 } },
    dsp: {},
  } as unknown as RenderContext;
  return { ctx, backend };
}

const opts = (over: Partial<RenderOpts>): RenderOpts => ({
  rom: "/roms/song.gb",
  out: "/out/song.wav",
  durationMs: 200,
  maxDurationMs: 5000,
  split: "mix",
  transport: false,
  start: true, // auto-start playback (the default) — this is what triggers the discarded button-hold render
  listSongs: false,
  ...over,
});

test("render captures from the moment playback starts — no dropped head (CLI + UI share runRenderJob)", () => {
  const { ctx, backend } = mockCtx();
  const quiet = { log: () => {}, warn: () => {} };

  runRenderJob(ctx, opts({}), quiet);

  const wav = decodeWav(backend.readFile("/out/song.wav")!);
  expect(wav.channels).toBe(2);
  expect(wav.pcm.length > 0).toBe(true); // the render produced audio at all

  // Recover how many already-playing frames were discarded before the first captured sample.
  const droppedFrames = Math.round((wav.pcm[0] - BASE) / SLOPE);
  const droppedMs = (droppedFrames / SR) * 1000;
  console.log(`[start-truncation] first sample ${wav.pcm[0].toFixed(5)} → ${droppedFrames} frames (${droppedMs.toFixed(1)} ms) dropped from the head`);

  // A correct render begins at the song's first sample (drop ≈ 0). The current code discards the 100 ms
  // button-hold render, so the head is missing — this assertion FAILS until that audio is captured.
  expect(Math.abs(droppedFrames) < 20).toBe(true);
});
