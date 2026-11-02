// Engine jobs over the real host (src/offline.ts, docs/modular-graph-phase-d3-design.md section 3): backend.engineRun
// runs a render on a worker of the engine's offline pool, with a runtime and an Engine of its own, and `await` settles
// in this host, which fires no timer and has no frame tick, through the poll's native wait the audio driver turns on.
// A render settles "done" with the WAV it wrote and its length, reporting its progress (a running maximum) and its
// console lines on the way; an auto-detected LSDj render (a fixture) ends below its progress, at the length written;
// cancel() stops a running render, which settles "cancelled", after a graph job completed while it ran; and
// offlineRun's handle cancels a graph job, which rejects with the reason "cancelled".
// The pool, the threading, the budget, the bytes against a direct RenderHost run and teardown are held by
// retroplug-engine-graph-test "[engine-jobs]".
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { decodeWav } from "../src/render";
import { OfflineRunError, type EngineLogLine, type EngineRunHandle } from "../src/offline";

declare const __REPO_RESOURCES_DIR__: string;
declare const __RESOURCES_DIR__: string;

const MGB = __REPO_RESOURCES_DIR__ + "/roms/mGB.gb";

const be = createRealBackend();
createAudioDriver(); // the poll's wait: this host has no UI tick

// A WAV path of this run's own: the files of the suite run in parallel processes.
const RUN = `${Date.now()}-${Math.floor(Math.random() * 1e9)}`;
const outPath = (name: string) => `/tmp/rp-engine-run-${RUN}-${name}.wav`;

test("a render job writes its WAV and settles done, its progress and console lines arriving as it runs", async () => {
  const out = outPath("done");
  const progress: number[] = [];
  const lines: EngineLogLine[] = [];
  const result = await be.engineRun(
    "render",
    { rom: MGB, out, durationMs: 2000, sampleRate: 48000 },
    { onProgress: (ms) => progress.push(ms), onLog: (line) => lines.push(line) },
  );
  expect(result).toEqual({ status: "done", message: "", outputs: [out], renderedMs: 2000 });

  const bytes = be.readFile(out);
  expect(bytes != null).toBe(true);
  const tag = (at: number) => String.fromCharCode(...bytes!.subarray(at, at + 4));
  expect(tag(0)).toBe("RIFF");
  expect(tag(8)).toBe("WAVE");
  const wav = decodeWav(bytes!);
  expect(wav.sampleRate).toBe(48000);
  expect(wav.channels).toBe(2);
  expect(wav.pcm.length).toBe(96000 * 2); // two seconds, interleaved stereo
  expect(bytes!.byteLength).toBe(44 + 96000 * 2 * 2);

  expect(progress.length > 0).toBe(true);
  for (let i = 1; i < progress.length; i++) expect(progress[i] > progress[i - 1]).toBe(true);
  expect(progress[progress.length - 1]).toBe(2000);
  expect(lines.some((l) => l.stream === "stdout" && l.text.startsWith("rendering "))).toBe(true);
  expect(lines.some((l) => l.stream === "stdout" && l.text === out)).toBe(true);
  expect(lines.every((l) => !l.text.endsWith("\n"))).toBe(true);
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(out);
});

test("a render job reports a spec it cannot render as its error, and an unknown kind is refused", async () => {
  expect(await be.engineRun("render", { out: outPath("no-rom") })).toEqual({
    status: "error",
    message: "render worker: job spec missing 'rom'",
    outputs: [],
    renderedMs: 0,
  });
  let refused = "";
  await be.engineRun("bake" as "render", {}).catch((e: Error) => (refused = e.message));
  expect(refused).toBe('unknown engine job kind "bake": the one kind is "render"');
});

test("cancel() stops a running render, which settles cancelled, and a graph job completes while it runs", async () => {
  // Ten minutes of audio: many seconds of wall time. Once it has rendered some, a graph job (a document-less echo) is
  // submitted, and its completion, which comes while the render still runs, cancels the render. That pins the two
  // kinds overlapping on the pool; the budget itself (a render never holding the last worker) is the C++ case's.
  let handle: EngineRunHandle | null = null;
  let renderSettled = false;
  let renderRunningAtGraph = false;
  let cancelled: boolean | null = null;
  let lastMs = 0;
  let graph: Promise<number> | null = null;
  const echo = { inputs: [{ samples: Float32Array.of(0.5), channels: 1, rate: 48000 }], returns: [1], frames: 0 };
  handle = be.engineRun(
    "render",
    { rom: MGB, out: outPath("cancel"), durationMs: 600000 },
    {
      onProgress: (ms) => {
        lastMs = ms;
        if (graph !== null || ms < 500) return;
        graph = be.offlineRun({ ...echo, rate: 48000 }).then((r) => {
          renderRunningAtGraph = !renderSettled;
          cancelled = handle!.cancel();
          return r.buffers[0].samples[0];
        });
      },
    },
  );
  void handle.then(() => (renderSettled = true));
  const result = await handle;
  expect(graph !== null).toBe(true);
  expect(await graph!).toBe(0.5);
  expect(renderRunningAtGraph).toBe(true);
  expect(cancelled).toBe(true);
  expect(result.status).toBe("cancelled");
  expect(result.outputs).toEqual([]);
  expect(lastMs >= 500 && lastMs < 600000).toBe(true);
  expect(handle.cancel()).toBe(false); // settled: nothing left to cancel
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(outPath("cancel"));
});

test("an auto-detected render's progress is a running maximum, and its result carries the length it wrote",
  async () => {
    // An LSDj song that ends in an HFF: the render counts the chunks it holds back past the stop as it goes, then
    // writes up to the stop, so native's count drops once at the end. onProgress never goes back; the length written
    // is the result's renderedMs, and the WAV's.
    const rom = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.gbc";
    const sav = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.sav";
    if (!be.fileExists(rom) || !be.fileExists(sav)) skip(`engine-run: the LSDj song fixture is not at ${rom}`);
    const out = outPath("hff");
    const progress: number[] = [];
    const result = await be.engineRun(
      "render",
      { rom, sav, out, songIndex: 3 },
      { onProgress: (ms) => progress.push(ms) },
    );
    expect(result.status).toBe("done");
    expect(result.renderedMs).toBe(191700); // MACHETE's length to its HFF
    // Whether a drain caught a count above the length depends on when the polls fell (the mock tier pins the clamp
    // on an exact timeline); what holds every time is that onProgress never went back and reached the length.
    for (let i = 1; i < progress.length; i++) expect(progress[i] > progress[i - 1]).toBe(true);
    expect(progress[progress.length - 1] >= result.renderedMs).toBe(true);
    console.log(`[engine-run] hff: most reported ${progress[progress.length - 1]} ms, length ${result.renderedMs} ms`);
    const wav = decodeWav(be.readFile(out)!);
    expect(wav.pcm.length / wav.channels).toBe(191700 * 44.1);
    be.deleteFile(out);
  });

test("offlineRun's handle cancels a graph job, which rejects with the reason cancelled", async () => {
  // A looping player recorded for its first frames over ten minutes of blocks: long enough to be cancelled mid-run.
  const document = JSON.stringify({
    nodes: [
      {
        id: "p",
        type: "rp.core.buffer-player",
        settings: { channels: 1, layout: "mono", loop: true },
        resources: { sample: "res:1" },
      },
      { id: "rec", type: "rp.core.buffer-recorder", settings: { channels: 1 }, resources: { target: "res:2" } },
    ],
    wires: [{ from: "p.out", to: "rec.in" }],
  });
  const run = be.offlineRun({
    document,
    inputs: [{ samples: new Float32Array(256).fill(0.25), channels: 1, rate: 48000 }],
    outputs: [{ channels: 1, frames: 256, rate: 48000 }],
    frames: 48000 * 600,
    rate: 48000,
  });
  expect(run.cancel()).toBe(true);
  let error: Error | null = null;
  await run.catch((e: Error) => (error = e));
  expect(error! instanceof OfflineRunError).toBe(true);
  expect(error!.message).toBe("cancelled");
  expect(run.cancel()).toBe(false);
  expect(be.pumpJobs()).toBe(0);
});
