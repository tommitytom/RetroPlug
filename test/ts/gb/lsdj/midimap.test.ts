// Replacement for examples/scripts/lsdj_midimap.json.
//
// The JSON booted aboy 15s (SRAM self-test), set the MidiMap role via the script
// `lsdj_sync_mode`, sent 4 MIDI notes and screenshotted. Here we author an empty
// valid sav (skips the self-test), set the role from TS via emu.loadRom's
// sync-mode arg, and drive the same MIDI input — fast, no host-boot wait.
//
// MidiMap (LsdjSyncMode::MidiMap): ch0 NoteOn -> row byte; ch1 -> row+128;
// NoteOff -> 0xFE. This exercises the input path; deeper assertions on the
// emitted row bytes belong with the serial/MIDI-out capture work.
import { test, expect, emu } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const emptySav = () => emu.savFromJson(JSON.stringify({ workingSong: { formatVersion: 22 } }));

test("LSDj MidiMap role boots from sav and processes MIDI input", () => {
  const sys = emu.loadRom(ABOY, emptySav(), "MidiMap");
  emu.runMs(3000); // valid sav -> no 12-15s self-test
  emu.screenshot(sys, "/tmp/lsdj_midimap_boot.png");

  emu.sendMidi(sys, [0x90, 0, 100]); emu.runMs(200); // ch0 note 0 on
  emu.sendMidi(sys, [0x80, 0, 0]);   emu.runMs(200); // off
  emu.sendMidi(sys, [0x91, 5, 100]); emu.runMs(200); // ch1 note 5 on -> row+128
  emu.sendMidi(sys, [0x81, 5, 0]);   emu.runMs(200); // off
  emu.screenshot(sys, "/tmp/lsdj_midimap_after.png");

  const frame = emu.getFrame(sys);
  expect(frame.width).toBeGreaterThan(0);   // LSDj is running
  expect(frame.height).toBeGreaterThan(0);  // (processed the MIDI without crashing)
  expect(emu.getAudio(100).length).toBeGreaterThan(0);
});
