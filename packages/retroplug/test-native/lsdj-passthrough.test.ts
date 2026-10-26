// Port of legacy test/ts/gb/lsdj/passthrough.test.ts: the TS `lsdj-sync` role in
// MidiPassthrough mode (6), running in the real DSP kernel, forwards raw host-MIDI bytes verbatim to
// LSDj's serial (dspRoles.ts `forwardMidiToSerial`). This exercises the raw-forward path against a real
// LSDj core — the legacy floor: LSDj keeps running (valid frames) after the byte stream, no crash.
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom } from "../src/lsdjSav";
import { directGraph } from "../src/graph/directGraph";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

declare const __RESOURCES_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb"; // stock ROM supports MidiPassthrough

const emptySav = () => savFrom({ workingSong: { formatVersion: 22 } });

const SEND_TO_ALL = [{ kind: "midi-routing", config: { mode: "sendToAll" } }];
const lsdjSync = (mode: string) => [{ kind: "lsdj-sync", config: { mode } }];

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the TS lsdj-sync MidiPassthrough role forwards raw MIDI to a real LSDj core without crashing", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) skip(`lsdj-passthrough: LSDj ROM not found at ${LSDJ}`);

  const audio = createAudioDriver();

  const id = 1;
  expect(graph.add({
    romPath: LSDJ,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: emptySav(),
  }, id)).toBeTruthy();

  expect(graph.setProjectStages(SEND_TO_ALL)).toBeTruthy();
  expect(graph.setPipeline(id, lsdjSync("midiPassthrough"))).toBeTruthy();

  audio.renderAudio(6000); // reach the song screen from the sav

  // Raw MIDI note-on/off pairs → the role forwards every byte to LSDj's serial FIFO.
  for (const m of [[0x90, 60, 100], [0x80, 60, 0], [0x90, 64, 100], [0x80, 64, 0]]) {
    audio.stageMidiIn(m);
    audio.renderAudio(200);
  }
  const after = rms(audio.renderAudio(500));

  const frame = be.getFrame(id);
  console.log(`[lsdj-passthrough] after=${after.toFixed(5)}`);
  expect(frame!.width).toBe(160); // the core survived the raw byte stream (processed serial, no crash)
  expect(frame!.height).toBe(144);
});
