// What a render's graph document carries (docs/modular-graph-phase-c2-design.md section 5): runRenderJob builds its
// system through the stores, so the instance's own configuration rides the system node the store publishes, not just
// the store's state. This was the UI's System > Render path, which dropped the gain it was handed.
//
// A mock backend and a silent driver: the audio is nothing here, only the document the store published.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { buildAppRegistry } from "../../src/appHost";
import { runRenderJob, type RenderContext, type RenderOpts } from "../../src/render";
import { gbRom } from "../systems/fixtures";

/** The driver calls a fixed render makes, answering silence. */
const silentAudio = () => ({
  renderAudio: (ms: number) => new Float32Array(Math.round((ms * 44100) / 1000) * 2),
  sampleRate: () => 44100,
  setSampleRate: () => true,
  setBpm: () => true,
  setTransport: () => true,
  pressButton: () => true,
});

const opts = (over: Partial<RenderOpts> = {}): RenderOpts => ({
  rom: "/roms/song.gb",
  out: "/out/song.wav",
  durationMs: 50,
  maxDurationMs: 5000,
  split: "mix",
  transport: false,
  start: true,
  listSongs: false,
  ...over,
});

test("a render's document carries the instance's gain as its system node's gain param", () => {
  const be = new MockBackend("/config");
  be.seed("/roms/song.gb", gbRom());
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  runRenderJob({ backend: be, audio: silentAudio(), project } as unknown as RenderContext, opts({ gainDb: -6 }), {
    log: () => {},
    warn: () => {},
  });
  const [id] = project.systems.systems().map((e) => e.id);
  expect(be.graphNode(`sys-${id}`)?.params).toEqual({ gain: -6 });
});
