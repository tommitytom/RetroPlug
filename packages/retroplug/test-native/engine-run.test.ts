// Engine jobs over the real host (src/offline.ts, docs/modular-graph-phase-d3-design.md section 3): backend.engineRun
// runs a render on a worker of the engine's offline pool, with a runtime and an Engine of its own, and `await` settles
// in this host, which fires no timer and has no frame tick, through the poll's native wait the audio driver turns on.
// A render settles "done" with the WAV it wrote and its length, reporting its progress (a running maximum) and its
// console lines on the way; an auto-detected LSDj render (a fixture) ends below its progress, at the length written;
// cancel() stops a running render, which settles "cancelled", after a graph job completed while it ran; a render that
// stops part way, cancelled or failed, leaves what was at its output paths as it was; and offlineRun's handle cancels
// a graph job just submitted, which rejects with the reason "cancelled".
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

/** `bytes` of a marker file: what an earlier export would have left at a path. */
const earlier = (name: string): Uint8Array => new TextEncoder().encode(`an earlier export ${name}`);
const text = (b: Uint8Array | null): string | null => (b ? new TextDecoder().decode(b) : null);

test("a cancelled render leaves the export that was at its path as it was, and no partial file", async () => {
  // The writer streams to <path>.part and moves it into place only when the render ends, so a cancel (or an error)
  // after the first chunk removes the partial and touches nothing else: not the mix export, and not the stems a
  // split render would have written.
  const mix = outPath("keep-mix");
  const stem = outPath("keep-stems");
  const stemPaths = ["pulse1", "pulse2", "wave", "noise"].map((c) => `${stem.replace(/\.wav$/, "")}_${c}.wav`);
  expect(be.writeFile(mix, earlier("mix"))).toBe(true);
  stemPaths.forEach((p) => expect(be.writeFile(p, earlier(p))).toBe(true));
  let mixHandle: EngineRunHandle | null = null;
  mixHandle = be.engineRun(
    "render",
    { rom: MGB, out: mix, durationMs: 600000 },
    { onProgress: () => mixHandle?.cancel() },
  );
  expect((await mixHandle).status).toBe("cancelled");
  expect(text(be.readFile(mix))).toBe("an earlier export mix");
  expect(be.fileExists(`${mix}.part`)).toBe(false);

  let stemHandle: EngineRunHandle | null = null;
  stemHandle = be.engineRun(
    "render",
    { rom: MGB, out: stem.replace(/\.wav$/, ""), durationMs: 600000, split: "channels" },
    { onProgress: () => stemHandle?.cancel() },
  );
  expect((await stemHandle).status).toBe("cancelled");
  stemPaths.forEach((p) => {
    expect(text(be.readFile(p))).toBe(`an earlier export ${p}`);
    expect(be.fileExists(`${p}.part`)).toBe(false);
  });
  expect(be.pumpJobs()).toBe(0);
  [mix, ...stemPaths].forEach((p) => be.deleteFile(p));
});

test("a cancelled render with onExists rename gives back the name it reserved", async () => {
  const out = outPath("rename-cancel");
  let handle: EngineRunHandle | null = null;
  handle = be.engineRun(
    "render",
    { rom: MGB, out, durationMs: 600000, onExists: "rename" },
    { onProgress: () => handle?.cancel() },
  );
  expect((await handle).status).toBe("cancelled");
  expect(be.fileExists(out)).toBe(false);
  expect(be.fileExists(`${out}.part`)).toBe(false);
  expect(be.pumpJobs()).toBe(0);
});

test("a render whose move into place fails keeps the whole render as its .part and names it", async () => {
  // The output path is a directory holding a file, so the move into place fails once the whole render is written.
  // The .part is then a complete WAV (headers patched), the only copy of a render that may have run for minutes: it
  // stays, and the error says where it is.
  const out = outPath("blocked");
  const inside = `${out}/keep.txt`;
  expect(be.writeFile(inside, earlier("inside"))).toBe(true);
  const result = await be.engineRun("render", { rom: MGB, out, durationMs: 500 });
  expect(result.status).toBe("error");
  expect(result.message.includes(`could not replace ${out}; the render is at ${out}.part`)).toBe(true);
  const part = be.readFile(`${out}.part`);
  expect(part != null).toBe(true);
  const wav = decodeWav(part!);
  expect(wav.pcm.length).toBe(22050 * 2); // 500 ms at 44.1 kHz, the header patched to say so
  expect(text(be.readFile(inside))).toBe("an earlier export inside");
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(`${out}.part`);
  be.deleteFile(inside);
  be.deleteFile(out);
});

test("a split render whose second move fails names what moved and what is left as .part", async () => {
  const prefix = outPath("blocked-split").replace(/\.wav$/, "");
  const first = `${prefix}_pulse1.wav`;
  const second = `${prefix}_pulse2.wav`;
  const inside = `${second}/keep.txt`;
  expect(be.writeFile(inside, earlier("inside"))).toBe(true);
  const result = await be.engineRun("render", { rom: MGB, out: prefix, durationMs: 500, split: "channels" });
  expect(result.status).toBe("error");
  expect(result.message.includes(`could not replace ${second}; the render is at ${second}.part`)).toBe(true);
  expect(result.message.includes(`${first} was moved into place`)).toBe(true);
  expect(result.message.includes(`${prefix}_wave.wav.part`)).toBe(true);
  expect(be.fileExists(first)).toBe(true);
  expect(be.fileExists(`${first}.part`)).toBe(false);
  for (const c of ["pulse2", "wave", "noise"]) expect(be.fileExists(`${prefix}_${c}.wav.part`)).toBe(true);
  expect(be.pumpJobs()).toBe(0);
  for (const c of ["pulse1", "pulse2", "wave", "noise"]) be.deleteFile(`${prefix}_${c}.wav.part`);
  be.deleteFile(first);
  be.deleteFile(inside);
  be.deleteFile(second);
});

test("two overlapping renders of one target with onExists rename end with two whole, distinct files", async () => {
  // The first render's file appears only when it ends, so the name is claimed by its .part: the second render, started
  // while the first runs, takes the next name instead of streaming into the same .part.
  const out = outPath("rename-overlap");
  const second = out.replace(/\.wav$/, "_2.wav");
  let sawFirst = false;
  let secondHandle: EngineRunHandle | null = null;
  const firstHandle = be.engineRun(
    "render",
    { rom: MGB, out, durationMs: 30000, onExists: "rename" },
    {
      onProgress: () => {
        if (sawFirst) return;
        sawFirst = true;
        secondHandle = be.engineRun("render", { rom: MGB, out, durationMs: 700, onExists: "rename" });
      },
    },
  );
  const a = await firstHandle;
  expect(secondHandle !== null).toBe(true);
  const b = await secondHandle!;
  expect(a.status).toBe("done");
  expect(b.status).toBe("done");
  expect(a.outputs).toEqual([out]);
  expect(b.outputs).toEqual([second]);
  expect(decodeWav(be.readFile(out)!).pcm.length).toBe(1323000 * 2);
  expect(decodeWav(be.readFile(second)!).pcm.length).toBe(30870 * 2);
  expect(be.fileExists(`${out}.part`)).toBe(false);
  expect(be.fileExists(`${second}.part`)).toBe(false);
  expect(be.pumpJobs()).toBe(0);
  be.deleteFile(out);
  be.deleteFile(second);
});

test("a render pulled in less than one frame is refused, rather than hanging for ever on empty chunks", async () => {
  const result = await be.engineRun("render", { rom: MGB, out: outPath("tiny-pull"), durationMs: 500, pullMs: 0.01 });
  expect(result.status).toBe("error");
  expect(result.message.includes("pullMs 0.01 is under one frame")).toBe(true);
  expect(be.pumpJobs()).toBe(0);
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
  // A looping player recorded for its first frames over ten minutes of blocks, cancelled as soon as it is submitted: it
  // is queued or planning, or just started, and the cancel ends it in any of those. A graph job cancelled while it runs
  // blocks is the C++ [offline] cases'.
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
