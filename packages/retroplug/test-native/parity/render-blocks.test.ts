// renderBlocks: the parity suite's capture call. It renders a scripted run of host blocks the way the
// plugin renders each one, so these check that it is the SAME render the rest of the suite already
// trusts (renderAudio), that it places lanes, and that it refuses what a DAW never sends.
import { test, expect } from "../../testing/harness";
import { bootSession } from "../../src/bootSession";

const session = bootSession();
const { project, audio } = session;

const CHORD = [
  [0x90, 60, 100],
  [0x91, 64, 100],
  [0x92, 67, 100],
];

function rms(a: Float32Array, from = 0, to = a.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += a[i] * a[i];
  return to > from ? Math.sqrt(s / (to - from)) : 0;
}

test("at 2 outputs in 1024-frame blocks it is renderAudio, bit for bit", () => {
  const frames = Math.floor((500 * audio.sampleRate()) / 1000); // what renderAudio(500) renders
  project.newProject();
  project.systems.loadMgb();
  for (const m of CHORD) audio.stageMidiIn(m);
  const mixed = audio.renderAudio(500);

  project.newProject();
  project.systems.loadMgb();
  const blocks = [];
  for (let at = 0; at < frames; at += 1024) blocks.push({ frames: Math.min(1024, frames - at) });
  blocks[0] = { ...blocks[0], midi: CHORD.map((bytes) => ({ frame: 0, bytes })) };
  const { lanes } = audio.renderBlocks(2, blocks);

  expect(lanes.length).toBe(2);
  expect(lanes[0].length).toBe(frames);
  expect(mixed.length).toBe(frames * 2);
  let differing = 0;
  for (let i = 0; i < frames; i++) {
    if (Object.is(mixed[2 * i], lanes[0][i]) === false) differing++;
    if (Object.is(mixed[2 * i + 1], lanes[1][i]) === false) differing++;
  }
  expect(differing).toBe(0);
  expect(rms(lanes[0])).toBeGreaterThan(0.001); // and it is not two silences agreeing
});

test("one system under Stereo routing lands on the first pair of an 8-lane host", () => {
  project.newProject();
  project.systems.loadMgb();
  const { lanes } = audio.renderBlocks(8, [
    { frames: 512, midi: CHORD.map((bytes) => ({ frame: 100, bytes })) },
    ...Array.from({ length: 30 }, () => ({ frames: 512 })),
  ]);
  expect(lanes.length).toBe(8);
  expect(rms(lanes[0])).toBeGreaterThan(0.001);
  expect(rms(lanes[1])).toBeGreaterThan(0.001);
  for (let c = 2; c < 8; c++) expect(rms(lanes[c]), `lane ${c}`).toBe(0);
});

test("it refuses what a host never sends", () => {
  project.newProject();
  expect(() => audio.renderBlocks(3 as 2, [{ frames: 64 }])).toThrow("outputs must be 2, 4, 6 or 8");
  expect(() => audio.renderBlocks(2, [{ frames: 0 }])).toThrow("block 0: frames must be 1..8192");
  expect(() => audio.renderBlocks(2, [{ frames: 64 }, { frames: 8193 }])).toThrow("block 1: frames must be 1..8192");
  expect(() => audio.renderBlocks(2, [{ frames: 64, midi: [{ frame: 64, bytes: [0x90, 60, 1] }] }])).toThrow(
    "is outside a 64-frame block",
  );
  expect(() =>
    audio.renderBlocks(2, [{ frames: 64, midi: [{ frame: 9, bytes: [0x90, 60, 1] }, { frame: 8, bytes: [0x80, 60, 0] }] }]),
  ).toThrow("MIDI frames go backwards (8 after 9)");
  expect(() => audio.renderBlocks(2, [{ frames: 64, midi: [{ frame: 0, bytes: [] }] }])).toThrow("a MIDI message is empty");
  expect(() => audio.renderBlocks(2, [{ frames: 64, controller: [[0x90, 1, 2, 3, 4]] }])).toThrow("1..4 bytes");
  expect(() => audio.renderBlocks(2, [{ frames: 64, bpm: 0 }])).toThrow("bpm must be positive");
  // Ties at one frame are allowed: the order between them is part of what a baseline records.
  expect(audio.renderBlocks(2, [{ frames: 64, midi: [{ frame: 5, bytes: [0x90, 60, 1] }, { frame: 5, bytes: [0x80, 60, 0] }] }]).lanes.length).toBe(2);
});

test("the host names the target it was built for", () => {
  expect(/^[a-z]+-[a-z0-9_]+-[a-z]+[0-9]+$/.test(audio.buildTarget()), audio.buildTarget()).toBeTruthy();
});
