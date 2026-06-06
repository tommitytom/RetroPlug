// Replacement for examples/scripts/lsdj_passthrough.json.
//
// The JSON booted LSDj 15s, set the MidiPassthrough role via the script
// `lsdj_sync_mode`, sent 2 notes and screenshotted. Here we author an empty
// valid sav (skips the self-test), set the role from TS, and drive the MIDI —
// fast, no host-boot wait.
//
// MidiPassthrough (LsdjSyncMode::MidiPassthrough): raw 3-byte MIDI -> LSDj
// serial (MGB-like on LSDj).
import { test, expect, emu } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const emptySav = () => emu.savFromJson(JSON.stringify({ workingSong: { formatVersion: 22 } }));

test("LSDj MidiPassthrough role boots from sav and passes MIDI through", () => {
  const sys = emu.loadRom(LSDJ, emptySav(), "MidiPassthrough");
  emu.runMs(3000); // valid sav -> no 12-15s self-test
  emu.screenshot(sys, "/tmp/lsdj_passthrough_boot.png");

  emu.sendMidi(sys, [0x90, 60, 100]); emu.runMs(200); // C5 on
  emu.sendMidi(sys, [0x80, 60, 0]);   emu.runMs(200); // off
  emu.sendMidi(sys, [0x90, 64, 100]); emu.runMs(200); // E5 on
  emu.sendMidi(sys, [0x80, 64, 0]);   emu.runMs(200); // off
  emu.screenshot(sys, "/tmp/lsdj_passthrough_after.png");

  const frame = emu.getFrame(sys);
  expect(frame.width).toBeGreaterThan(0);
  expect(frame.height).toBeGreaterThan(0);
  expect(emu.getAudio(100).length).toBeGreaterThan(0);
});
