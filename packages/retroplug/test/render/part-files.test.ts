// The files a render streams to before it moves them into place (render.ts's buildSink): each render's own, so one
// render never opens, truncates or deletes another's. Two overwrite renders of one target that overlap each write a
// whole WAV and the target is one of them; a `.part` a failed move kept, the only copy of a finished render, survives a
// later render of the same target that is cancelled; and a rename render stopped before its first chunk gives back the
// name it reserved. The renders nest (the second runs inside one of the first's pulls), so the overlap is exact rather
// than timed; the engine jobs' overlap is engine-run's.
// It runs the library in-process, on an exact-blocks engine; the bytes users get are the render baseline's.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { runRenderJob, decodeWav, type RenderContext, type RenderOpts } from "../../src/render";

const SR = 44100;

/** A driver answering a constant `level`, calling `onPull` (with the pull's index, the boot settle's being 0) before
 *  each chunk it renders. */
const levelAudio = (level: number, onPull: (n: number) => void = () => {}): RenderContext["audio"] => {
  let pulls = 0;
  const chunk = (ms: number): Float32Array => {
    onPull(pulls++);
    return new Float32Array(Math.round((ms * SR) / 1000) * 2).fill(level);
  };
  return {
    renderAudio: chunk,
    renderAudioPerChannel: (_id: number, ms: number) => [chunk(ms)],
    sampleRate: () => SR,
    setSampleRate: () => true,
    setBpm: () => true,
    setTransport: () => true,
    pressButton: () => true,
  };
};

/** A context over `backend`: buildSystem's GB path needs only these four calls of the project store. */
const ctxOf = (backend: MockBackend, audio: RenderContext["audio"]): RenderContext => ({
  backend,
  audio,
  project: {
    systems: { addSystem: () => 1, view: () => [{ id: 1 }], adopt: () => {}, loadState: () => 1 },
  } as unknown as RenderContext["project"],
});

const opts = (over: Partial<RenderOpts> = {}): RenderOpts => ({
  rom: "/roms/song.gb",
  out: "/out/song.wav",
  durationMs: 200,
  maxDurationMs: 5000,
  split: "mix",
  transport: false,
  start: true,
  listSongs: false,
  ...over,
});

const quiet = { log: () => {}, warn: () => {} };
const frames = (ms: number) => Math.floor((ms * SR) / 1000);

test("two overlapping overwrite renders of one target each write a whole WAV, the target the last to end", () => {
  const be = new MockBackend("/config");
  let second: string[] = [];
  const nested = (n: number) => {
    // The first render's writer is open by its third pull (the settle, the Start press's lead-in, then the loop).
    if (n !== 2) return;
    second = runRenderJob(ctxOf(be, levelAudio(0.5)), opts({ durationMs: 100 }), quiet).outputs;
  };
  const first = runRenderJob(ctxOf(be, levelAudio(0.25, nested)), opts(), quiet).outputs;
  expect(second).toEqual(["/out/song.wav"]);
  expect(first).toEqual(["/out/song.wav"]);
  const bytes = be.readFile("/out/song.wav")!;
  const wav = decodeWav(bytes);
  expect(bytes.byteLength).toBe(44 + frames(200) * 2 * 2);
  expect(wav.pcm.length).toBe(frames(200) * 2);
  expect(wav.pcm.every((v) => Math.abs(v - 0.25) < 1e-3)).toBe(true);
  expect(be.listDir("/out")).toEqual(["song.wav"]);
});

/** A backend whose next move onto `blocked` fails, as a DAW holding the file open makes it fail on Windows. */
class BlockedMove extends MockBackend {
  constructor(private blocked: string | null) {
    super("/config");
  }
  override rename(from: string, to: string): boolean {
    if (to === this.blocked) {
      this.blocked = null;
      return false;
    }
    return super.rename(from, to);
  }
}

test("a .part a failed move kept survives a later overwrite render of the target that is cancelled", () => {
  const be = new BlockedMove("/out/song.wav");
  let message = "";
  try {
    runRenderJob(ctxOf(be, levelAudio(0.25)), opts(), quiet);
  } catch (e) {
    message = (e as Error).message;
  }
  const kept = /the render is at (\S+\.part)$/.exec(message)?.[1] ?? "";
  expect(kept.startsWith("/out/song.wav."), `the failed move names its .part: ${message}`).toBe(true);
  const keptBytes = be.readFile(kept);
  expect(keptBytes !== null).toBe(true);
  expect(decodeWav(keptBytes!).pcm.length).toBe(frames(200) * 2);

  // The second render opens its writer with the Start press's lead-in, then is cancelled at its first poll.
  let message2 = "";
  try {
    runRenderJob(ctxOf(be, levelAudio(0.5)), opts(), { ...quiet, isCancelled: () => true });
  } catch (e) {
    message2 = (e as Error).message;
  }
  expect(message2).toBe("render: cancelled");
  const now = be.readFile(kept);
  expect(now !== null && now.length === keptBytes!.length && now.every((b, i) => b === keptBytes![i]), "kept as it was")
    .toBe(true);
  expect(be.listDir("/out")).toEqual([kept.slice("/out/".length)]);
});

test("a rename render cancelled before its first chunk gives back the name it reserved", () => {
  // With no Start press there is no lead-in, so the cancel lands at the first poll, before any writer opened: what is
  // left to remove is only the reservation.
  const be = new MockBackend("/config");
  be.seed("/out/song.wav", "an earlier export");
  let cancelled = false;
  try {
    runRenderJob(ctxOf(be, levelAudio(0.25)), opts({ onExists: "rename", start: false }), {
      ...quiet,
      isCancelled: () => true,
    });
  } catch (e) {
    cancelled = (e as Error).message === "render: cancelled";
  }
  expect(cancelled).toBe(true);
  expect(be.listDir("/out")).toEqual(["song.wav"]);
  expect(be.readText("/out/song.wav")).toBe("an earlier export");
});
