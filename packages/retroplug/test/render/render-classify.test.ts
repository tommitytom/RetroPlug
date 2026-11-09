// Two edges the design changes deliberately (docs/modular-graph-phase-f4a-design.md section 6): a render classifies its
// ROM by its CONTENT, and the end-of-song detection follows the ROM's cart rather than the battery beside it. Both are
// proved here in-process, over mocks: the audio is nothing, the decisions are everything.
//
// E1: `platformOf` read the extension alone, so a Game Boy ROM named `.bin` was an unknown console and `--split
// channels` was refused with "got other". The registry's content rule makes it a Game Boy, so the split is accepted and
// the stems take the descriptor's names.
//
// E2: the LSDj probe was chosen when the SIBLING BATTERY was a 128 KiB LSDj image, so an LSDj ROM with no battery
// rendered a fixed length and a non-LSDj Game Boy beside one was polled. The probe now follows the ROM's roles, and the
// output's default name follows the same cart.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { buildAppRegistry } from "../../src/appHost";
import {
  renderSongs,
  runRenderJob,
  validateRenderOpts,
  type RenderAudioDriver,
  type RenderContext,
  type RenderOpts,
} from "../../src/render";
import { gbRom, lsdjRom, risaRom } from "../systems/fixtures";
import { savBytes as lsdjSav } from "@retroplug/lsdj/testFixtures";
import { savBytes as risaSav } from "@retroplug/risa/testFixtures";

const SR = 44100;
const NR52 = 0x80;

/** Silence in every shape a render asks for. `streams` is the split's stream count, as the descriptor names it. */
function silentAudio(streams = 1): RenderAudioDriver {
  const frames = (ms: number): Float32Array => new Float32Array(Math.round((ms * SR) / 1000) * 2);
  return {
    renderAudio: (ms) => frames(ms),
    renderAudioPerChannel: (_id, ms) => Array.from({ length: streams }, () => frames(ms)),
    sampleRate: () => SR,
    setSampleRate: () => true,
    setBpm: () => true,
    setTransport: () => true,
    pressButton: () => true,
  };
}

/** A backend whose NR52 answers what `playing()` says: an LSDj cart whose APU the poll sees switch off after a while. */
class Nr52Backend extends MockBackend {
  private reads = 0;
  constructor(
    root: string,
    private readonly onReads: number,
  ) {
    super(root);
  }
  override readCpu(id: number, addr: number): number | null {
    if (addr !== 0xff26 || !super.readCpu(id, addr)) return 0;
    this.reads++;
    return this.reads <= this.onReads ? NR52 : 0;
  }
}

/** A context over `backend` with the mock's project store, which needs only the four calls buildSystem makes. */
function ctxOf(backend: MockBackend, audio: RenderAudioDriver): RenderContext {
  return {
    backend,
    audio,
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

test("E1: a Game Boy ROM under a .bin extension renders its channels, because the content decides", () => {
  const backend = new MockBackend("/config");
  backend.seed("/roms/cart.bin", gbRom());
  const ctx = ctxOf(backend, silentAudio(4));
  const result = runRenderJob(ctx, opts({ rom: "/roms/cart.bin", split: "channels", durationMs: 50 }), {
    log: () => {},
    warn: () => {},
  });
  // The descriptor's channel split: one file per stream, under its own name.
  expect(result.outputs.map((p) => p.slice(p.lastIndexOf("_"), -".wav".length))).toEqual([
    "_pulse1",
    "_pulse2",
    "_wave",
    "_noise",
  ]);
  for (const path of result.outputs) expect(backend.fileExists(path)).toBe(true);
});

test("E2: an LSDj ROM with no battery auto-detects its length, because the ROM names the cart", () => {
  // No sav is seeded at all: the old rule needed a 128 KiB LSDj image beside the ROM to choose the probe.
  const backend = new Nr52Backend("/config", 3);
  backend.seed("/roms/lsdj.gb", lsdjRom());
  const result = runRenderJob(ctxOf(backend, silentAudio()), opts({ rom: "/roms/lsdj.gb" }), {
    log: () => {},
    warn: () => {},
  });
  // The auto-detect path ran: it reports a length, and the APU going off ended it well under the cap.
  expect(result.lengthMs !== undefined).toBe(true);
  expect(result.hff).toBe(true);
  expect(result.lengthMs! < 5000).toBe(true);
});

test("E2: a Game Boy ROM that is not LSDj runs the full cap, even beside an LSDj battery", () => {
  // The backend answers NR52 on, then off: the poll the OLD rule chose here (a 128 KiB LSDj image beside the ROM) would
  // have ended this render a few hundred ms in. No duration is pinned, so a render with no probe runs to maxDurationMs.
  const backend = new Nr52Backend("/config", 3);
  backend.seed("/roms/song.gb", gbRom()); // a Game Boy cart whose title is not LSDj's
  backend.seed("/roms/song.sav", lsdjSav("lsdj499")); // ... next to one, which the old rule polled
  const progress: number[] = [];
  const result = runRenderJob(ctxOf(backend, silentAudio()), opts({ rom: "/roms/song.gb", out: undefined }), {
    log: () => {},
    warn: () => {},
    onRendered: (ms) => progress.push(ms),
  });
  // A fixed render of the whole cap: no detected length, no HFF, and the last progress report is the cap itself.
  expect(result.lengthMs).toBe(undefined);
  expect(result.hff).toBe(undefined);
  expect(progress[progress.length - 1]).toBe(5000);
  // Its name is the ROM's, because the battery's song is no cart of this ROM's (the naming follows the same rule as the
  // probe).
  expect(result.outputs).toEqual(["/roms/song.wav"]);
});

test("E2: the same ROM's default file name comes from its own cart's battery", () => {
  // A real LSDj cart WITH its own battery: content and battery agree, so the working song names the file (the old
  // behaviour, unchanged).
  const backend = new MockBackend("/config");
  backend.seed("/roms/lsdj.gb", lsdjRom());
  backend.seed("/roms/lsdj.sav", lsdjSav("lsdj499"));
  const ctx = ctxOf(backend, silentAudio());
  const result = runRenderJob(ctx, opts({ rom: "/roms/lsdj.gb", out: undefined, durationMs: 50 }), {
    log: () => {},
    warn: () => {},
  });
  expect(result.outputs.length).toBe(1);
  expect(result.outputs).toEqual(["/roms/SNDTESTx.wav"]); // named after its working song, not the ROM
});

test("a song the cart cannot promote refuses the render before the boot, in the cart's words", () => {
  // risa's listSongs reads a LEGACY-layout catalog too, and its seedSong cannot promote one: dropping the null would
  // boot the battery's own working song and name the file after another (the silent-wrong-song bug this guards).
  const backend = new MockBackend("/config");
  backend.seed("/roms/cart.nes", risaRom());
  backend.seed("/roms/cart.sav", risaSav("multi_legacy")); // the demo batteries' v1 catalog at 0x6000
  let boots = 0;
  const ctx = ctxOf(backend, silentAudio());
  ctx.project.systems.adopt = () => { boots++; return 1; };
  ctx.project.systems.addSystem = () => { boots++; return 1; };
  expect(() =>
    runRenderJob(ctx, opts({ rom: "/roms/cart.nes", songIndex: 0, durationMs: 50 }), { log: () => {}, warn: () => {} }),
  ).toThrow("render: could not load risa song 0 (needs a current-layout catalog)");
  expect(boots).toBe(0); // refused before anything booted
});

test("LSDj's --song matches a cart's 8-character name from a longer request", () => {
  // LSDj stores a project name in 8 characters, so `--song` was always compared truncated to that: a request longer
  // than the name matches it rather than missing. The length is the cart's own data on its render hooks.
  const backend = new MockBackend("/config");
  backend.seed("/roms/lsdj.gb", lsdjRom());
  backend.seed("/roms/lsdj.sav", lsdjSav("lsdj499")); // its one project is SNDTESTx
  const result = runRenderJob(
    ctxOf(backend, silentAudio()),
    opts({ rom: "/roms/lsdj.gb", song: "SNDTESTxyz", out: undefined, durationMs: 50 }),
    { log: () => {}, warn: () => {} },
  );
  expect(result.outputs).toEqual(["/roms/SNDTESTx.wav"]);
});

test("E4: --song on a ROM whose cart has no song hooks is refused, naming what this ROM lacks", () => {
  // A Game Boy ROM whose title names no tracker (mGB, a homebrew cart) beside an LSDj battery: the render classifies
  // through the ROM's roles, so the flag is refused even though the battery is one a song cart could list (edge E4's
  // generic-ROM case).
  const backend = new MockBackend("/config");
  backend.seed("/roms/song.gb", gbRom());
  backend.seed("/roms/song.sav", lsdjSav("lsdj499"));
  let boots = 0;
  const ctx = ctxOf(backend, silentAudio());
  ctx.project.systems.adopt = () => { boots++; return 1; };
  ctx.project.systems.addSystem = () => { boots++; return 1; };
  expect(() =>
    runRenderJob(ctx, opts({ rom: "/roms/song.gb", songIndex: 0, durationMs: 50 }), { log: () => {}, warn: () => {} }),
  ).toThrow(
    "render: --song/--song-index is a feature of LSDj, SMSGGDJ, Risa (this ROM runs no tracker: its console is Game Boy)",
  );
  expect(boots).toBe(0); // refused before anything booted
});

test("E4: --list-songs on the same ROM is refused the same way", () => {
  // The sentence names why: the ROM runs no tracker, not that its console has none (a Game Boy's carts do).
  const backend = new MockBackend("/config");
  backend.seed("/roms/song.gb", gbRom());
  backend.seed("/roms/song.sav", lsdjSav("lsdj499"));
  expect(() => renderSongs(ctxOf(backend, silentAudio()), opts({ rom: "/roms/song.gb" }))).toThrow(
    "render: --list-songs is a feature of LSDj, SMSGGDJ, Risa (this ROM runs no tracker: its console is Game Boy)",
  );
});

test("LSDj's song flags read the battery through the codec, so a malformed image is refused in its words", () => {
  // A full-size image with no 'jk' magic: the render has always decoded the battery it lists, so both flags refuse it
  // with the codec's sentence rather than answering "no songs" from a header scan.
  const backend = new MockBackend("/config");
  backend.seed("/roms/lsdj.gb", lsdjRom());
  backend.seed("/roms/lsdj.sav", new Uint8Array(0x20000));
  const ctx = ctxOf(backend, silentAudio());
  expect(() => renderSongs(ctx, opts({ rom: "/roms/lsdj.gb" }))).toThrow("missing 'jk' SRAM init magic");
  expect(() =>
    runRenderJob(ctx, opts({ rom: "/roms/lsdj.gb", songIndex: 0, durationMs: 50 }), { log: () => {}, warn: () => {} }),
  ).toThrow("missing 'jk' SRAM init magic");
});

test("the --split refusals are rebuilt from the descriptors, so today's consoles print the old words", () => {
  // The sentence names the consoles that DECLARE the split (a NES and nothing else for pins; a Game Boy and a NES for
  // channels), so it is the platform switch's text with nothing shell-side spelling a console.
  expect(() => validateRenderOpts(opts({ rom: "/roms/cart.gb", split: "pins" }), "gb")).toThrow(
    "render: --split pins is NES-only (got gb)",
  );
  expect(() => validateRenderOpts(opts({ rom: "/roms/cart.gba", split: "channels" }), "gba")).toThrow(
    "render: --split channels needs a Game Boy or NES ROM (got gba)",
  );
  expect(() => validateRenderOpts(opts({ rom: "/roms/cart.sms", split: "pins" }), "sms")).toThrow(
    "render: --split pins is NES-only (got sms)",
  );
  // A file nothing classifies prints the word the render's own union gave it, as it always has: this refusal did not
  // change when classification moved to the registry.
  expect(() => validateRenderOpts(opts({ rom: "/roms/cart.bin", split: "pins" }), "unknown")).toThrow(
    "render: --split pins is NES-only (got other)",
  );
});
