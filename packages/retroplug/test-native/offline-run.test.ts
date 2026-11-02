// Offline jobs over the real host (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3): backend.offlineRun
// runs a graph document to completion on a worker of the engine's offline pool and `await` settles in this host, which
// fires no timer and has no frame tick, through the poll's native wait the audio driver turns on. Samples go through
// rp.dsp.gain into a recorder and come back sample for sample; a file input comes back windowed, with its report; a
// refusal and a failure reject with native's reason; several jobs run at once, a chain of them one after another, and a
// fan-out past the engine's 64 outstanding jobs all come back.
// The pool, the threading, the windows and the live plan's isolation are held by retroplug-engine-graph-test
// "[offline]".
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { OfflineRunError, offlineInputRef, offlineOutputRef, type OfflineRunRequest } from "../src/offline";

const be = createRealBackend();
createAudioDriver(); // the poll's wait: this host has no UI tick

// Player -> gain -> recorder over res:1 into res:2, `channels` wide; with `loop`, the player wraps.
function gainDocument(gain: number, channels: number, loop = false): string {
  const layout = channels === 1 ? "mono" : "stereo";
  return JSON.stringify({
    nodes: [
      {
        id: "p",
        type: "rp.core.buffer-player",
        settings: { channels, layout, loop },
        resources: { sample: offlineInputRef(0) },
      },
      { id: "g", type: "rp.dsp.gain", settings: { channels }, params: { gain } },
      {
        id: "rec",
        type: "rp.core.buffer-recorder",
        settings: { channels },
        resources: { target: offlineOutputRef(1, 0) },
      },
    ],
    wires: [
      { from: "p.out", to: "g.in" },
      { from: "g.out", to: "rec.in" },
    ],
  });
}

function signal(frames: number, channels: number, seed: number): Float32Array {
  const out = new Float32Array(frames * channels);
  for (let i = 0; i < out.length; i++) out[i] = Math.fround(0.8 * Math.sin((i + 1) * 0.01 * (seed + 1)));
  return out;
}

function gainJob(input: Float32Array, channels: number, gain: number): OfflineRunRequest {
  const frames = input.length / channels;
  return {
    document: gainDocument(gain, channels),
    inputs: [{ samples: input, channels, rate: 48000 }],
    outputs: [{ channels, frames, rate: 48000 }],
    frames,
    rate: 48000,
  };
}

// A 16-bit PCM mono WAV, little-endian.
function wav(samples: number[], rate: number): Uint8Array {
  const bytes = new Uint8Array(44 + samples.length * 2);
  const v = new DataView(bytes.buffer);
  const tag = (at: number, s: string) => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)));
  tag(0, "RIFF");
  v.setUint32(4, 36 + samples.length * 2, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  v.setUint32(16, 16, true);
  v.setUint16(20, 1, true);
  v.setUint16(22, 1, true);
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 2, true);
  v.setUint16(32, 2, true);
  v.setUint16(34, 16, true);
  tag(36, "data");
  v.setUint32(40, samples.length * 2, true);
  samples.forEach((s, i) => v.setInt16(44 + i * 2, s, true));
  return bytes;
}

async function rejection(p: Promise<unknown>): Promise<Error> {
  try {
    await p;
  } catch (e) {
    return e as Error;
  }
  throw new Error("expected a rejection, got a resolution");
}

test("samples through rp.dsp.gain into a recorder come back sample for sample", async () => {
  const input = signal(3000, 2, 1);
  const result = await be.offlineRun(gainJob(input, 2, 0.5));
  expect(result.inputs).toEqual([]);
  expect(result.buffers.length).toBe(1);
  const [out] = result.buffers;
  expect(out.handle).toBe(2);
  expect(out.channels).toBe(2);
  expect(out.frames).toBe(3000);
  expect(out.rate).toBe(48000);
  expect(out.samples.length).toBe(input.length);
  let mismatches = 0;
  for (let i = 0; i < input.length; i++) if (out.samples[i] !== Math.fround(input[i] * 0.5)) mismatches++;
  expect(mismatches).toBe(0);
  expect(be.pumpJobs()).toBe(0);
});

test("a file input comes back decoded and windowed, with its report", async () => {
  const path = be.configDir() + "/offline-run.wav";
  const pcm = Array.from({ length: 3000 }, (_, i) => Math.round(12000 * Math.sin(i * 0.05)));
  expect(be.writeFile(path, wav(pcm, 22050))).toBeTruthy();
  const result = await be.offlineRun({
    inputs: [{ path, offset: 1000, length: 500 }],
    returns: [1],
    frames: 0,
    rate: 22050,
  });
  expect(result.inputs).toEqual([
    { handle: 1, decodedRate: 22050, decodedFrames: 3000, frames: 500, nonFinite: false },
  ]);
  const [buffer] = result.buffers;
  expect(buffer.frames).toBe(500);
  expect(buffer.rate).toBe(22050);
  // miniaudio's 16-bit to float: the sample over 32768.
  let mismatches = 0;
  for (let i = 0; i < 500; i++) if (buffer.samples[i] !== Math.fround(pcm[1000 + i] / 32768)) mismatches++;
  expect(mismatches).toBe(0);
  // Past the end, under LSDj's cap: an empty window, reported, and a buffer of no frames.
  const empty = await be.offlineRun({
    inputs: [{ path, offset: 5000, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } }],
    returns: [1],
    frames: 0,
    rate: 22050,
  });
  expect(empty.inputs[0].frames).toBe(0);
  expect(empty.buffers[0].samples.length).toBe(0);
  be.deleteFile(path);
});

test("a refusal and a failure reject with native's reason", async () => {
  const refused = await rejection(
    be.offlineRun({ outputs: [{ channels: 1, frames: 0, rate: 48000 }], frames: 1, rate: 48000 }),
  );
  expect(refused.message).toBe("output 0 needs at least one channel and one frame");
  const failed = await rejection(
    be.offlineRun({
      document: JSON.stringify({ nodes: [{ id: "gb", type: "rp.sameboy.gb" }], wires: [] }),
      frames: 64,
      rate: 48000,
    }),
  );
  expect(failed instanceof OfflineRunError).toBe(true);
  expect(failed.message).toBe('the document was refused: gb: unknown-type: no node type "rp.sameboy.gb" is registered');
  const missing = await rejection(be.offlineRun({ inputs: [{ path: "/nonexistent/rp.wav" }], frames: 0, rate: 48000 }));
  expect(missing.message).toBe("input 0: /nonexistent/rp.wav could not be read or decoded");
  expect((missing as OfflineRunError).inputs).toEqual([]);
});

test("several jobs run at once, and a chain of them one after another", async () => {
  const input = signal(2000, 1, 2);
  const gains = [0.25, 0.5, 1, 2, -1, 0.125];
  const results = await Promise.all(gains.map((g) => be.offlineRun(gainJob(input, 1, g))));
  results.forEach((r, k) => {
    let mismatches = 0;
    for (let i = 0; i < input.length; i++)
      if (r.buffers[0].samples[i] !== Math.fround(input[i] * gains[k])) mismatches++;
    expect(mismatches).toBe(0);
  });
  // Each job's output the next one's input: three halvings.
  let buffer = input;
  for (let i = 0; i < 3; i++) buffer = (await be.offlineRun(gainJob(buffer, 1, 0.5))).buffers[0].samples;
  expect(buffer[100]).toBe(Math.fround(input[100] * 0.125));
  expect(be.pumpJobs()).toBe(0);
});

test("a fan-out past the engine's bound of outstanding jobs is held back, and every job comes back", async () => {
  // Native refuses a 65th outstanding job as busy; offlineRun holds the rest in TS and submits them as room comes.
  // Each job runs long enough (ten minutes of a looping player, recorded for its first 256 frames) that the
  // submissions outrun the completions and the bound is reached.
  const count = 150;
  const input = signal(256, 1, 3);
  const long = (k: number): OfflineRunRequest => ({
    ...gainJob(input, 1, k / 64),
    document: gainDocument(k / 64, 1, true),
    frames: 48000 * 600,
  });
  const results = await Promise.all(Array.from({ length: count }, (_, k) => be.offlineRun(long(k))));
  expect(results.length).toBe(count);
  let mismatches = 0;
  results.forEach((r, k) => {
    for (let i = 0; i < input.length; i++)
      if (r.buffers[0].samples[i] !== Math.fround(input[i] * Math.fround(k / 64))) mismatches++;
  });
  expect(mismatches).toBe(0);
  expect(be.pumpJobs()).toBe(0);
});
