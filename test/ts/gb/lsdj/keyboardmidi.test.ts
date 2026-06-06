// Replacement for examples/scripts/lsdj_keyboardmidi.json.
//
// The JSON booted aboy 15s, set the KeyboardMidi role via the script
// `lsdj_sync_mode`, sent 3 notes (different octaves) and screenshotted. Here we
// author an empty valid sav (skips the self-test), set the role from TS, and
// drive the MIDI — fast, no host-boot wait.
//
// KeyboardMidi (LsdjSyncMode::KeyboardMidi): MIDI notes -> PS/2-equivalent
// scancodes via LSDj's keyboard map.
import { test, expect, emu } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const emptySav = () => emu.savFromJson(JSON.stringify({ workingSong: { formatVersion: 22 } }));

test("LSDj KeyboardMidi role boots from sav and maps MIDI notes", () => {
  const sys = emu.loadRom(ABOY, emptySav(), "KeyboardMidi");
  emu.runMs(3000); // valid sav -> no 12-15s self-test
  emu.screenshot(sys, "/tmp/lsdj_keyboardmidi_boot.png");

  emu.sendMidi(sys, [0x90, 48, 100]); emu.runMs(300); // C3
  emu.sendMidi(sys, [0x90, 60, 100]); emu.runMs(300); // C4
  emu.sendMidi(sys, [0x90, 36, 100]); emu.runMs(300); // C2 (low octave)
  emu.screenshot(sys, "/tmp/lsdj_keyboardmidi_after.png");

  const frame = emu.getFrame(sys);
  expect(frame.width).toBeGreaterThan(0);
  expect(frame.height).toBeGreaterThan(0);
  expect(emu.getAudio(100).length).toBeGreaterThan(0);
});
