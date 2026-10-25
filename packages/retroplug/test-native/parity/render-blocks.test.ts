// renderBlocks: the parity suite's capture call. It renders a scripted run of host blocks the way the
// plugin renders each one, so these check that it is the SAME render the rest of the suite already
// trusts (renderAudio), that it places lanes, and that it refuses what a DAW never sends.
import { test, expect, skip } from "../../testing/harness";
import { bootSession } from "../../src/bootSession";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import type { RenderBlock } from "../../src/audioDriver";
import { DirectGraph } from "../../src/graph/directGraph";

declare const __RESOURCES_DIR__: string;

const session = bootSession();
// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4), through
// the session's own graph: the stores publish to it too, so each sees the documents the other sent.
const graph = new DirectGraph(session.backend, session.project.graph);
const { project, audio, backend: be, dsp } = session;

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
  // An output count native never renders is refused as native refuses it, before the shape is touched: no
  // throwaway document is published for it.
  const setGraph = be.setGraph;
  let published = 0;
  be.setGraph = (request) => {
    published++;
    return setGraph(request);
  };
  try {
    for (const outputs of [3, 10, 0, 2.5]) {
      const render = () => audio.renderBlocks(outputs as 2, [{ frames: 64 }]);
      expect(render, `${outputs} outputs`).toThrow("outputs must be 2, 4, 6 or 8");
    }
  } finally {
    be.setGraph = setGraph;
  }
  expect(published).toBe(0);
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
  expect(() => audio.renderBlocks(2, [{ frames: 64, bpm: 0 }])).toThrow("bpm must be a positive finite number");
  expect(() => audio.renderBlocks(2, [{ frames: 64, bpm: NaN }])).toThrow("bpm must be a positive finite number");
  expect(() => audio.renderBlocks(2, [{ frames: 64, bpm: Infinity }])).toThrow("bpm must be a positive finite number");
  expect(() => audio.renderBlocks(2, [{ frames: 64, ppq: NaN }])).toThrow("block 0: ppq must be finite");
  expect(() => audio.renderBlocks(2, [{ frames: 64, ppq: -Infinity }])).toThrow("ppq must be finite");
  // Ties at one frame are allowed: the order between them is part of what a baseline records.
  expect(audio.renderBlocks(2, [{ frames: 64, midi: [{ frame: 5, bytes: [0x90, 60, 1] }, { frame: 5, bytes: [0x80, 60, 0] }] }]).lanes.length).toBe(2);
});

test("the host names the target it was built for", () => {
  expect(/^[a-z]+-[a-z0-9_]+-[a-z]+[0-9]+$/.test(audio.buildTarget()), audio.buildTarget()).toBeTruthy();
});

// Plain blocks covering `ms` of audio the way renderAudio(ms) partitions it: 1024 frames, then the rest.
function partition(ms: number): RenderBlock[] {
  const frames = Math.floor((ms * audio.sampleRate()) / 1000);
  const blocks: RenderBlock[] = [];
  for (let at = 0; at < frames; at += 1024) blocks.push({ frames: Math.min(1024, frames - at) });
  return blocks;
}

function bitwiseEqual(a: Float32Array, b: Float32Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) return false;
  return true;
}

// A fresh mGB, booted past the ~1.5 s during which it ignores MIDI.
function bootMgb(): void {
  project.newProject();
  project.systems.loadMgb();
  audio.renderBlocks(2, partition(1600));
}

test("a note at frame N of a block sounds as the same note at frame 0 of a block starting at N", () => {
  const N = 2048;
  const note = [{ frame: 0, bytes: [0x90, 60, 100] }];
  bootMgb();
  const inside = audio.renderBlocks(2, [{ frames: 2 * N, midi: [{ frame: N, bytes: [0x90, 60, 100] }] }]).lanes;
  bootMgb();
  const split = audio.renderBlocks(2, [{ frames: N }, { frames: N, midi: note }]).lanes;
  bootMgb();
  const early = audio.renderBlocks(2, [{ frames: 2 * N, midi: note }]).lanes;

  expect(rms(inside[0], N)).toBeGreaterThan(0.001); // the note sounds, after its frame
  expect(bitwiseEqual(inside[0], split[0]) && bitwiseEqual(inside[1], split[1])).toBeTruthy();
  expect(bitwiseEqual(inside[0], early[0])).toBe(false); // and the frame is honoured, not collapsed to 0
});

const MIDIOUT_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiOut" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [
      {
        notes: [1, 13, 25, 1, 13, 25, 1, 13],
        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
        commandValues: [48, 52, 55, 60, 64, 67, 72, 36],
      },
    ],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};

test("MIDI out through renderBlocks is what renderAudio + drainMidiOut yields, tagged with its block", () => {
  const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
  if (!be.fileExists(ABOY)) skip(`render-blocks: arduinoboy LSDj ROM not found at ${ABOY}`);
  const id = 1;
  const START = 7; // GameboyButton::Start

  type Msg = { system: number; frame: number; data: number[] };
  const flat = (m: { system: number; frame: number; data: Uint8Array }): Msg => ({
    system: m.system,
    frame: m.frame,
    data: Array.from(m.data),
  });

  // One scene: boot the song, then play / stop / play, rendering through `render(ms)` and collecting
  // what it yields. Both runs boot identically (the determinism switch is on for parity files).
  const scene = (render: (ms: number) => Msg[]): Msg[] => {
    project.newProject();
    expect(
      graph.add(
        { romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, sramBytes: savFrom(MIDIOUT_SONG) },
        id,
      ),
    ).toBeTruthy();
    expect(
      dsp.setSystems({
        project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
        systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiOut" } }] }],
      }),
    ).toBeTruthy();
    expect(be.setSerialOutCapture(id, true)).toBeTruthy();
    render(6000); // reach the song screen; its output is not compared
    const out: Msg[] = [];
    const tap = () => {
      audio.pressButton(id, START, true);
      out.push(...render(80));
      audio.pressButton(id, START, false);
    };
    tap();
    out.push(...render(1500));
    tap();
    out.push(...render(300));
    tap();
    out.push(...render(1500));
    graph.remove(id);
    project.newProject();
    return out;
  };

  const viaAudio = scene((ms) => {
    audio.renderAudio(ms);
    return audio.drainMidiOut().map(flat);
  });

  const blockOf: number[] = [];
  const viaBlocks = scene((ms) => {
    const blocks = partition(ms);
    const r = audio.renderBlocks(2, blocks);
    let last = 0;
    for (const m of r.midiOut) {
      expect(m.block >= last && m.block < blocks.length, `block ${m.block} of ${blocks.length}`).toBeTruthy();
      last = m.block;
      blockOf.push(m.block);
    }
    return r.midiOut.map(flat);
  });

  expect(viaAudio.length).toBeGreaterThan(4); // a real stream, not two empties agreeing
  expect(viaAudio.some((m) => m.data[0] === 0xf8)).toBeTruthy();
  expect(JSON.stringify(viaBlocks)).toBe(JSON.stringify(viaAudio));
  expect(Math.max(...blockOf)).toBeGreaterThan(0); // entries carry the block they came from, not always 0
});

test("it refuses while the background audio thread is running", () => {
  project.newProject();
  expect(audio.startAudio()).toBeTruthy();
  try {
    expect(() => audio.renderBlocks(2, [{ frames: 64 }])).toThrow("the background audio thread is running");
  } finally {
    audio.stopAudio();
  }
});

test("a refused request leaves the engine untouched", () => {
  const chord = CHORD.map((bytes) => ({ frame: 0, bytes }));
  const follow = Array.from({ length: 20 }, () => ({ frames: 512 }));

  bootMgb();
  expect(() => audio.renderBlocks(2, [{ frames: 512, midi: chord }, { frames: 0 }])).toThrow("block 1: frames must be 1..8192");
  const after = audio.renderBlocks(2, follow).lanes;

  bootMgb();
  const clean = audio.renderBlocks(2, follow).lanes;

  bootMgb();
  const sounded = audio.renderBlocks(2, [{ frames: 512, midi: chord }, ...follow]).lanes;

  expect(bitwiseEqual(after[0], clean[0]) && bitwiseEqual(after[1], clean[1])).toBeTruthy();
  expect(rms(sounded[0])).toBeGreaterThan(rms(clean[0]) + 0.001); // the chord would have been audible
});
