// A loaded LSDj in a MIDI-out mode captures its serial out through its build alone
// (docs/modular-graph-phase-b2-design.md section 3): the store adopts it as a project load does, building the core
// with `serialOutCapture` in the spec, and nothing calls setSerialOutCapture after it. The same system in the same
// mode, built without that flag, emits nothing, so the flag is what arms the capture. (lsdj-midiout.test.ts proves
// the decode itself, armed by the live command.)
import { test, expect, skip } from "../testing/harness";
import { bootSession } from "../src/bootSession";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjSyncMode } from "../src/settingsEnums";
import type { ConstructSpec } from "../src/backend";

declare const __RESOURCES_DIR__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const START = 7; // GameboyButton::Start

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
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
};

const session = bootSession();
const { project, audio, backend: be } = session;
project.systems.setBatteryFlush(false); // a remove must not write the song into the fixture ROM's .sav

// Adopt the LSDj in MIDI-out mode, as a project load adopts each system, with `build` editing the spec it is built
// from; play its song and count the NoteOns the kernel decoded from its serial out. Removed at the end.
type MidiOutRun = { spec: ConstructSpec; liveArms: number; noteOns: number };
function playMidiOut(build: (spec: ConstructSpec) => ConstructSpec): MidiOutRun {
  const buildCore = be.buildCore;
  const setSerialOutCapture = be.setSerialOutCapture;
  let spec: ConstructSpec | null = null;
  let liveArms = 0;
  be.buildCore = (s, id) => buildCore((spec = build(s)), id);
  be.setSerialOutCapture = (id, mode) => {
    liveArms++;
    return setSerialOutCapture(id, mode);
  };
  let id: number | null = null;
  try {
    id = project.systems.adopt(
      { romPath: ABOY, roles: [{ kind: "lsdj-sync", config: { mode: LsdjSyncMode.MidiOut } }] },
      { sramBytes: savFrom(MIDIOUT_SONG) },
    );
    expect(typeof id, "adopted").toBe("number");
    audio.renderAudio(4000); // boot
    audio.drainMidiOut();
    audio.pressButton(id!, START, true);
    audio.renderAudio(100);
    audio.pressButton(id!, START, false);
    audio.renderAudio(3000);
    const noteOns = audio.drainMidiOut().filter((m) => (m.data[0] & 0xf0) === 0x90).length;
    return { spec: spec!, liveArms, noteOns };
  } finally {
    if (id !== null) project.systems.removeSystem(id);
    be.buildCore = buildCore;
    be.setSerialOutCapture = setSerialOutCapture;
  }
}

test("an adopted LSDj in MIDI-out mode is built capturing, and its NoteOns reach the host", () => {
  if (!be.fileExists(ABOY)) skip(`lsdj-midiout-build-flag: aboy LSDj ROM not found at ${ABOY}`);
  const run = playMidiOut((spec) => spec);
  expect(run.spec.serialOutCapture, "the build flag").toBe("drive");
  expect(run.liveArms, "setSerialOutCapture calls").toBe(0);
  console.log(`[lsdj-midiout-build-flag] built capturing: ${run.noteOns} NoteOns`);
  expect(run.noteOns).toBeGreaterThan(0);
  expect(project.systems.systems().length).toBe(0);
});

test("the same system built without the flag emits none: the flag is what arms the capture", () => {
  if (!be.fileExists(ABOY)) skip(`lsdj-midiout-build-flag: aboy LSDj ROM not found at ${ABOY}`);
  const run = playMidiOut((spec) => {
    const bare = { ...spec };
    delete bare.serialOutCapture;
    return bare;
  });
  expect(run.spec.serialOutCapture, "the build flag").toBe(undefined);
  expect(run.liveArms, "setSerialOutCapture calls").toBe(0);
  expect(run.noteOns).toBe(0);
  expect(project.systems.systems().length).toBe(0);
});
