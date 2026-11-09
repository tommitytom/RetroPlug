// The files a render streams to before it moves them into place (render.ts's buildSink): each render's own, so one
// render never opens, truncates or deletes another's. Two overwrite renders of one target that overlap each write a
// whole WAV and the target is one of them; a `.part` a failed move kept, the only copy of a finished render, survives
// a later render of the same target that is cancelled; a rename render stopped before its first chunk gives back the
// name it reserved; a render cancelled while it boots stops at its next boot step, before it has a file at all; a
// rename render sees another's `.part` of its name in any case on a filesystem that folds case; and each `.part` is
// synced to the disk before it is moved into place. The renders nest (the second runs inside one of the first's
// pulls), so the overlap is exact rather than timed; the engine jobs' overlap is engine-run's.
// It runs the library in-process, on an exact-blocks engine; the bytes users get are the render baseline's.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { buildAppRegistry } from "../../src/appHost";
import { runRenderJob, decodeWav, type RenderContext, type RenderOpts } from "../../src/render";

const SR = 44100;

/** A driver answering a constant `level`, calling `onPull` (with the pull's index, the boot settle's being 0) before
 *  each chunk it renders; a per-channel pull answers `streams` streams. */
const levelAudio = (level: number, onPull: (n: number) => void = () => {}, streams = 1): RenderContext["audio"] => {
  let pulls = 0;
  const chunk = (ms: number): Float32Array => {
    onPull(pulls++);
    return new Float32Array(Math.round((ms * SR) / 1000) * 2).fill(level);
  };
  return {
    renderAudio: chunk,
    renderAudioPerChannel: (_id: number, ms: number) => {
      const c = chunk(ms);
      return Array.from({ length: streams }, () => c.slice());
    },
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
  registry: buildAppRegistry(),
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
    const open = be.listDir("/out").some((f) => /^song\.wav\..+\.part$/.test(f));
    expect(open, "the first render's .part is open").toBe(true);
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
  readonly created: string[] = []; // every path a writeFile created or truncated, in order
  constructor(private blocked: string | null) {
    super("/config");
  }
  override writeFile(path: string, bytes: Uint8Array): boolean {
    this.created.push(path);
    return super.writeFile(path, bytes);
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

  // The second render is cancelled once it has a .part of its own open (its writer opens with the Start press's
  // lead-in), so it has a file of its own to discard.
  const before = be.created.length;
  const ownPart = () => be.created.slice(before).filter((p) => p.endsWith(".part") && p !== kept);
  let message2 = "";
  try {
    runRenderJob(ctxOf(be, levelAudio(0.5)), opts(), { ...quiet, isCancelled: () => ownPart().length > 0 });
  } catch (e) {
    message2 = (e as Error).message;
  }
  expect(message2).toBe("render: cancelled");
  expect(ownPart().length, "the cancelled render wrote a .part of its own").toBe(1);
  const now = be.readFile(kept);
  expect(now !== null && now.length === keptBytes!.length && now.every((b, i) => b === keptBytes![i]), "kept as it was")
    .toBe(true);
  expect(be.listDir("/out")).toEqual([kept.slice("/out/".length)]);
});

test("a rename render cancelled before its first chunk gives back the name it reserved", () => {
  // With no Start press there is no lead-in, so a cancel taken once the name is reserved lands at the stream's first
  // poll, before any writer opened: what is left to remove is only the reservation, which is still empty then.
  const be = new MockBackend("/config");
  be.seed("/out/song.wav", "an earlier export");
  let cancelled = false;
  let atCancel: number[] = [];
  const reserved = () => be.paths().filter((p) => p.endsWith(".part"));
  try {
    runRenderJob(ctxOf(be, levelAudio(0.25)), opts({ onExists: "rename", start: false }), {
      ...quiet,
      isCancelled: () => {
        if (reserved().length === 0) return false;
        atCancel = reserved().map((p) => be.readFile(p)!.length);
        return true;
      },
    });
  } catch (e) {
    cancelled = (e as Error).message === "render: cancelled";
  }
  expect(cancelled).toBe(true);
  expect(atCancel, "cancelled holding only its empty reservation").toEqual([0]);
  expect(be.listDir("/out")).toEqual(["song.wav"]);
  expect(be.readText("/out/song.wav")).toBe("an earlier export");
});

test("a render cancelled while it boots stops at its next boot step, before it reserves or writes a file", () => {
  // The cancel lands during the boot settle (the first pull): the render ends there, rather than run its Start press
  // and open its writer before its stream's first poll sees it.
  const be = new MockBackend("/config");
  let pulls = 0;
  let cancel = false;
  let message = "";
  try {
    runRenderJob(ctxOf(be, levelAudio(0.25, () => ((cancel = true), pulls++))), opts({ onExists: "rename" }), {
      ...quiet,
      isCancelled: () => cancel,
    });
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message).toBe("render: cancelled");
  expect(pulls, "only the settle ran").toBe(1);
  expect(be.log.filter((m) => m === "writeFile" || m === "appendFile")).toEqual([]);
  expect(be.listDir("/out")).toEqual([]);

  // One cancelled before it starts pulls no audio from the job.
  let pulled = 0;
  try {
    runRenderJob(ctxOf(be, levelAudio(0.25, () => pulled++)), opts(), { ...quiet, isCancelled: () => true });
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message).toBe("render: cancelled");
  expect(pulled).toBe(0);
});

/** A backend over a filesystem that matches names without regard to case, as Windows and a default macOS volume do:
 *  a name is found under any spelling, and a listing shows each entry as it was created. */
class CaseFolding extends MockBackend {
  override fileExists(path: string): boolean {
    return this.paths().some((p) => p.toLowerCase() === this.canonicalize(path).toLowerCase());
  }
}

test("a rename render counts another's .part of its name as taken, in any case where the filesystem folds case", () => {
  // Another render of song.wav is running (its .part is there); this one asks for Song.wav, which on a folding
  // filesystem is the same file, so it takes the next name. On a case-sensitive one they are two files.
  const run = (be: MockBackend): string[] => {
    be.seed("/out/song.wav.abc-1.part", "another render's");
    return runRenderJob(ctxOf(be, levelAudio(0.25)), opts({ out: "/out/Song.wav", onExists: "rename" }), quiet).outputs;
  };
  expect(run(new CaseFolding("/config"))).toEqual(["/out/Song_2.wav"]);
  expect(run(new MockBackend("/config"))).toEqual(["/out/Song.wav"]);
});

/** A backend recording each sync and move, whose syncs fail when `failSync`, as a disk error would make them. */
class SyncLog extends MockBackend {
  readonly calls: string[] = [];
  constructor(private failSync = false) {
    super("/config");
  }
  override syncFile(path: string): boolean {
    this.calls.push(`sync ${path}`);
    return this.failSync ? false : super.syncFile(path);
  }
  override rename(from: string, to: string): boolean {
    this.calls.push(`move ${from}`);
    return super.rename(from, to);
  }
}

test("each finished .part is synced to the disk before it is moved into place, and one that cannot be is kept", () => {
  // A move puts the new name over the old file at once, and a filesystem need not have written the data by then, so a
  // power loss just after could leave a short file where a whole export was.
  const be = new SyncLog();
  runRenderJob(ctxOf(be, levelAudio(0.25, () => {}, 4)), opts({ split: "channels", out: "/out/song" }), quiet);
  const parts = be.calls.filter((c) => c.startsWith("move ")).map((c) => c.slice("move ".length));
  expect(parts.length).toBe(4); // the Game Boy's four stems
  expect(be.calls).toEqual(parts.flatMap((p) => [`sync ${p}`, `move ${p}`]));

  // A .part that cannot be synced is not moved over its path: it is a finished render, kept as a failed move keeps it.
  const failing = new SyncLog(true);
  failing.seed("/out/song.wav", "an earlier export");
  let message = "";
  try {
    runRenderJob(ctxOf(failing, levelAudio(0.25)), opts(), quiet);
  } catch (e) {
    message = (e as Error).message;
  }
  const kept = /the render is at (\S+\.part)$/.exec(message)?.[1] ?? "";
  expect(kept.startsWith("/out/song.wav."), `the failed sync names its .part: ${message}`).toBe(true);
  expect(failing.calls.filter((c) => c.startsWith("move "))).toEqual([]);
  expect(failing.readText("/out/song.wav")).toBe("an earlier export");
  expect(decodeWav(failing.readFile(kept)!).pcm.length).toBe(frames(200) * 2);
});
