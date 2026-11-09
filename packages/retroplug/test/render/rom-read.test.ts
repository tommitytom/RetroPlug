// A render hands a ROM to its core as a path, so the whole file crosses into this process only when a cart's hooks take
// the bytes (docs/modular-graph-phase-f4a-design.md section 4.1 and the final review's Minor 5). Before the follow-up
// every render read the whole ROM up front, which a GBA's 32 MB paid for nothing; now the read is the hooks' and happens
// once however many of them are handed the bytes.
//
// A mock backend and a silent driver: the reads are the subject, the audio is nothing.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { buildAppRegistry } from "../../src/appHost";
import { runRenderJob, type RenderAudioDriver, type RenderContext, type RenderOpts } from "../../src/render";
import { gbRom, risaRom } from "../systems/fixtures";

const SR = 44100;

/** Silence in the two shapes a mix render asks for. */
function silentAudio(): RenderAudioDriver {
  const frames = (ms: number): Float32Array => new Float32Array(Math.round((ms * SR) / 1000) * 2);
  return {
    renderAudio: (ms) => frames(ms),
    renderAudioPerChannel: (_id, ms) => [frames(ms)],
    sampleRate: () => SR,
    setSampleRate: () => true,
    setBpm: () => true,
    setTransport: () => true,
    pressButton: () => true,
  };
}

/** A backend that records every whole-file read, so a case can say which reads the render made. */
class CountingReads extends MockBackend {
  readonly reads: string[] = [];
  override readFile(path: string): Uint8Array | null {
    this.reads.push(path);
    return super.readFile(path);
  }
}

/** A context over `backend` with the mock's project store, which needs only the four calls buildSystem makes. */
function ctxOf(backend: MockBackend): RenderContext {
  return {
    backend,
    audio: silentAudio(),
    registry: buildAppRegistry(),
    project: {
      systems: { addSystem: () => 1, view: () => [{ id: 1 }], adopt: () => {}, loadState: () => 1 },
    } as unknown as RenderContext["project"],
  };
}

const opts = (over: Partial<RenderOpts> & { rom: string }): RenderOpts => ({
  out: "/out/song.wav",
  maxDurationMs: 5000,
  split: "mix",
  transport: false,
  start: true,
  listSongs: false,
  ...over,
});

test("a render whose ROM runs no tracker reads no whole file", () => {
  // A Game Boy ROM whose title names no tracker: the boot hands its path to the core, and nothing here takes the bytes,
  // so the file is not pulled across at all (the classification and the battery check read prefixes).
  const backend = new CountingReads("/config");
  backend.seed("/roms/game.gb", gbRom());
  const result = runRenderJob(ctxOf(backend), opts({ rom: "/roms/game.gb", durationMs: 20 }), {
    log: () => {},
    warn: () => {},
  });
  expect(result.outputs).toEqual(["/out/song.wav"]);
  expect(backend.reads.filter((path) => path === "/roms/game.gb")).toEqual([]);
});

test("a cart's hooks read the ROM once, though two of them are handed the bytes", () => {
  // risa's press gesture ignores the bytes and its playing probe reads them: one read, not one each, and not one the
  // render made before the boot.
  const backend = new CountingReads("/config");
  backend.seed("/roms/cart.nes", risaRom());
  runRenderJob(ctxOf(backend), opts({ rom: "/roms/cart.nes", durationMs: 20 }), { log: () => {}, warn: () => {} });
  expect(backend.reads.filter((path) => path === "/roms/cart.nes")).toEqual(["/roms/cart.nes"]);
});
