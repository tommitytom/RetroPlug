// Replacement for examples/scripts/multi_nes_gb.json.
//
// Proves the two emulator backends coexist in one Project: a Mesen NES system
// (n8-midi.nes) and a SameBoy GB system (LSDj) loaded together, advanced
// together, mixed together. The JSON sent a MIDI note (OneChannelPerInstance
// routing) and screenshotted both; here we drive the NES's MIDI FIFO directly,
// run both, and assert both backends stay live and the mix renders without
// crashing across the backend boundary.
import { test, expect, emu } from "harness";

const NES = "resources/roms/n8-midi.nes";          // in-repo
const GB = "../resources/roms/lsdj/lsdj9_4_2.gb";  // sibling resources/

test("NES + GB systems coexist, advance, and mix in one project", () => {
  const nes = emu.loadRom(NES);
  const gb = emu.loadRom(GB);

  // Drive the n8-midi ROM's note path (host MIDI -> NES FIFO).
  emu.sendMidi(nes, [0x90, 60, 100]);
  emu.runMs(400);
  emu.sendMidi(nes, [0x80, 60, 0]);
  const audio = emu.getAudio(3000); // advances BOTH systems, returns the mix
  emu.screenshot(nes, "/tmp/multi_nes_gb_nes.png");
  emu.screenshot(gb, "/tmp/multi_nes_gb_gb.png");

  // Both backends produced frames (neither stalled the shared process loop).
  expect(emu.getFrame(nes).published).toBeTruthy();
  expect(emu.getFrame(gb).published).toBeTruthy();
  // Distinct system ids, mixed buffer rendered across the backend boundary.
  expect(nes).toBeLessThan(gb);
  expect(audio.length).toBeGreaterThan(0);
});
