// The whole chain, through the real native host: a pad press staged on the control-surface stream reaches a
// real LSDj cart and moves it.
//
// test/dsp/controller-role.test.ts proves the same path in pure TS, but against the mock host - it shows the
// kernel produces the right bytes, not that the bytes arrive. This runs the identical role over the real
// Engine, the real DSP runtime and a real SameBoy, and reads the answer out of the cart's WRAM. Everything
// between the pad and the sound is genuine except the Launchpad itself.
//
// Run: pnpm test:native lsdj-launchpad-role
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "../src/lsdjSav";
import { LsdjReader } from "../src/lsdj/runtime";
import { SongSchema } from "../src/lsdj/model";
import { songRowTicks } from "../src/lsdj/playback";
import { padIndex } from "../src/launchpad";

declare const __RESOURCES_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const SYSTEM = 1;

// 16 contiguous rows, each one single-phrase chain: every row playable, every row 96 ticks. Contiguous
// because an empty row is the end of the song (B9), so a gap would make the higher rows unreachable.
const songJson = {
  formatVersion: 22,
  settings: { syncMode: "MidiMap" as const, tempo: 128 },
  rows: Array.from({ length: 16 }, () => ({ chains: [0, 0, 0, 0] })),
  chains: [{ phrases: [0] }],
  phrases: [{ notes: Array.from({ length: 16 }, () => 1), instruments: Array.from({ length: 16 }, () => 0) }],
  instruments: [{ type: "pulse" as const, panning: "LeftRight" as const, adsr: { initialLevel: 8, attackSpeed: 8 } }],
};
const SONG: SavInput = { workingSong: songJson };

/** Press the pad showing (pu1, row) on page 0: rows 0-7 are the left pane, 8-15 the right. */
const press = (row: number): number[] => [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100];

test("a pad press on the controller stream launches a row on a real cart", () => {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) return console.log(`# SKIP lsdj-launchpad-role: aboy ROM not found at ${ABOY}`);

  const header = be.readFilePrefix(ABOY, 0x150);
  const reader = header ? LsdjReader.fromHeader(header) : null;
  if (!reader || !reader.supported) return console.log("# SKIP lsdj-launchpad-role: unsupported LSDj version");

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(be.constructSystem({
    romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom(SONG),
  }, SYSTEM)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems({
    project: [
      { kind: "midi-routing", config: { mode: "sendToAll" } },
      // Exactly what kernelProjection synthesizes for an enabled controller, song table and all.
      {
        kind: "launchpad",
        config: {
          app: "lsdj-midimap", target: "system", systemId: SYSTEM,
          appConfig: { quantise: "immediate" },
          songRowTicks: songRowTicks(SongSchema.parse(songJson)),
        },
      },
    ],
    systems: [{ id: SYSTEM, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiMap" } }] }],
  })).toBeTruthy();

  audio.renderAudio(6000); // past the cartridge self-test, onto the song screen
  const before = reader.read(be.readRam(SYSTEM)!);
  expect(before.channels.pu1.playing).toBe(false); // nothing pressed yet, nothing playing

  // The one line this test exists for: a pad press, on the control-surface stream, with no MIDI staged.
  expect(audio.stageControllerIn(press(6))).toBe(true);
  audio.renderAudio(400);

  const after = reader.read(be.readRam(SYSTEM)!);
  console.log(`[lsdj-launchpad-role] after pressing the pad for row 6: playing=${after.channels.pu1.playing} row=${after.channels.pu1.songRow}`);
  expect(after.channels.pu1.playing).toBe(true);
  expect(after.channels.pu1.songRow).toBe(6);
});

test("the same bytes on the MUSICAL stream launch a different row, which is why the streams are separate", () => {
  // The pad for row 6 is note 0x33 (51). Through the controller stream it means "row 6"; staged as ordinary
  // MIDI it reaches LSDj's MidiMap translator directly and means "row 51". A control surface sharing the
  // musical stream would therefore fire every launch twice, to two different places - this measures that on
  // a real cart rather than asserting it.
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) return console.log("# SKIP lsdj-launchpad-role: aboy ROM not found");

  const header = be.readFilePrefix(ABOY, 0x150);
  const reader = header ? LsdjReader.fromHeader(header) : null;
  if (!reader || !reader.supported) return console.log("# SKIP lsdj-launchpad-role: unsupported LSDj version");

  const id = 2;
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(be.constructSystem({
    romPath: ABOY, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: savFrom({
      workingSong: { ...songJson, rows: Array.from({ length: 64 }, () => ({ chains: [0, 0, 0, 0] })) },
    }),
  }, id)).toBeTruthy();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems({
    project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
    systems: [{ id, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiMap" } }] }],
  })).toBeTruthy();

  audio.renderAudio(6000);
  const note = press(6)[1];
  audio.stageMidiIn([0x90, note, 100]);
  audio.renderAudio(400);

  const after = reader.read(be.readRam(id)!);
  console.log(`[lsdj-launchpad-role] the SAME note (${note}) as plain MIDI lands on row ${after.channels.pu1.songRow}, not 6`);
  expect(after.channels.pu1.songRow).toBe(note);
});
