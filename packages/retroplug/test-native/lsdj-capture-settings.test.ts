// A loaded LSDj in a MIDI-out mode captures its serial out from its Game Boy node's settings alone
// (docs/modular-graph-phase-c2-design.md section 4): the store adopts it as a project load does, its boot puts no
// capture with a resource, and the first document that places its node carries `serialOutCapture`, which the node gives
// the core as it takes it, before the core first runs, so the core captures from its first block.
// Each mode once, on the ROM it targets: MI.OUT drives (an Arduinoboy build, its N commands' NoteOns reaching the
// host), master sync listens (a stock LSDj in SYNC=LSDj, its clocks reaching the host, which they would not had its
// capture ever driven it: clocked while stopped, it stops answering START). (lsdj-midiout.test.ts and
// lsdj-mastersync.test.ts prove the decoders themselves, and retroplug-engine-graph-test holds a capture applied at
// activation to one armed on the core directly, bit for bit.)
import { test, expect, skip } from "@retroplug/testing";
import { bootSession } from "../src/bootSession";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjSyncMode } from "../src/settingsEnums";
import type { GraphDocument } from "@retroplug/core/graphDocument";

declare const __RESOURCES_DIR__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const START = 7; // GameboyButton::Start

const pulse = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } } as const;

// SYNC=MI.OUT and a phrase of eight notes, each with an `N` (NoteOn) command: once playing, LSDj sends a NoteOn
// per row on its serial out (as lsdj-midiout.test.ts authors it).
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
    instruments: [pulse],
  },
};

// SYNC=LSDj and a one-note phrase: once playing, LSDj is the serial master and sends a byte a 24-PPQN tick (as
// lsdj-mastersync.test.ts authors it).
const MASTER_SONG: SavInput = {
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "Lsdj" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [pulse],
  },
};

const session = bootSession();
const { project, audio, backend: be } = session;
project.systems.setBatteryFlush(false); // a remove must not write the song into the fixture ROM's .sav

// What a run saw: whether a boot put a capture with a resource, the capture its node was first placed with, the status
// bytes its decoder sent while LSDj booted, and those it sent once LSDj played.
type Run = { captureInABoot: boolean; placed: unknown; boot: number[]; played: number[] };

// Adopt an LSDj in `mode` as a project load adopts each system, recording its resource puts and every document
// published; boot it, then play its song. Removed at the end.
function play(romPath: string, song: SavInput, mode: string): Run {
  const setGraph = be.setGraph;
  const putResource = be.putResource;
  const putSpecs: Record<string, unknown>[] = [];
  be.putResource = (spec) => {
    putSpecs.push(spec as unknown as Record<string, unknown>);
    return putResource(spec);
  };
  const documents: GraphDocument[] = [];
  be.setGraph = (request) => {
    documents.push(JSON.parse(request.document) as GraphDocument);
    return setGraph(request);
  };
  let id: number | null = null;
  try {
    const roles = [{ kind: "lsdj-sync", config: { mode } }];
    id = project.systems.adopt({ romPath, roles }, { sramBytes: savFrom(song) });
    expect(typeof id, "adopted").toBe("number");
    const node = `sys-${id}`;
    const placing = documents.find((d) => d.nodes.some((n) => n.id === node));
    const placed = placing?.nodes.find((n) => n.id === node)?.settings?.serialOutCapture;
    audio.renderAudio(6000); // boot to the song screen
    const boot = audio.drainMidiOut().map((m) => m.data[0]);
    audio.pressButton(id!, START, true);
    audio.renderAudio(100);
    audio.pressButton(id!, START, false);
    audio.renderAudio(3000);
    const played = audio.drainMidiOut().map((m) => m.data[0]);
    // The capture is the node's own setting: no resource a boot put carried it.
    const captureInABoot = putSpecs.some((spec) => Object.prototype.hasOwnProperty.call(spec, "serialOutCapture"));
    return { captureInABoot, placed, boot, played };
  } finally {
    if (id !== null) project.systems.removeSystem(id);
    be.setGraph = setGraph;
    be.putResource = putResource;
  }
}

test("an LSDj loaded in MI.OUT is built with no capture, placed driving, and its NoteOns reach the host", () => {
  if (!be.fileExists(ABOY)) skip(`lsdj-capture-settings: aboy LSDj ROM not found at ${ABOY}`);
  const run = play(ABOY, MIDIOUT_SONG, LsdjSyncMode.MidiOut);
  expect(run.captureInABoot, "a capture in a resource put").toBe(false);
  expect(run.placed, "the capture its node was first placed with").toBe("drive");
  const noteOns = run.played.filter((s) => (s & 0xf0) === 0x90).length;
  console.log(`[lsdj-capture-settings] MI.OUT: ${noteOns} NoteOns`);
  expect(noteOns).toBeGreaterThan(0);
  expect(project.systems.systems().length).toBe(0);
});

test("an LSDj loaded in master sync is built with no capture, placed listening, and its clocks reach the host", () => {
  if (!be.fileExists(LSDJ)) skip(`lsdj-capture-settings: LSDj ROM not found at ${LSDJ}`);
  const run = play(LSDJ, MASTER_SONG, LsdjSyncMode.MasterSync);
  expect(run.captureInABoot, "a capture in a resource put").toBe(false);
  expect(run.placed, "the capture its node was first placed with").toBe("listen");
  // A stopped master waits on an external clock, which listening never supplies, so its boot sends nothing; and one
  // that was ever clocked would take that clock for another master's and stop answering START.
  expect(run.boot).toEqual([]);
  const clocks = run.played.filter((s) => s === 0xf8).length;
  console.log(`[lsdj-capture-settings] master sync: ${clocks} clocks`);
  expect(clocks).toBeGreaterThan(20);
  expect(project.systems.systems().length).toBe(0);
});
