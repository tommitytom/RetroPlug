// Replacement for examples/scripts/lsdj_midi_metro_setup.json (the bootstrap for
// the reaper-lsdj-midi-author DAW fixture).
//
// The JSON booted stock LSDj, navigated SYNC=MIDI + a one-note song, then pressed
// START to arm LSDj's "WAIT for clock" state — all captured in the savestate so
// the .RPP is self-contained. We author the song directly (SYNC=MIDI), attach the
// MidiSync role, boot, press START to arm, and emit the .rplg the
// reaper-lsdj-midi-author target bakes into examples/reaper/lsdj_midi_metro.rpp.
import { test, expect, emu, Button, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const fill = <T>(n: number, f: () => T): T[] => Array.from({ length: n }, f);

function midiSyncSongSav(): ArrayBuffer {
  const rows = fill(256, () => ({ chains: [null, null, null, null] as (number | null)[] }));
  rows[0].chains[0] = 0;
  const chains = fill(128, () => null as unknown);
  chains[0] = { phrases: [0, ...fill(15, () => null)], transpositions: fill(16, () => 0) };
  const phrases = fill(256, () => null as unknown);
  phrases[0] = {
    notes: [1, ...fill(15, () => 0)],
    instruments: [0, ...fill(15, () => null)],
    commands: fill(16, () => "None"), commandValues: fill(16, () => 0),
  };
  const instruments = fill(64, () => null as unknown);
  instruments[0] = { type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 };
  return emu.savFromJson(JSON.stringify({
    workingSong: { formatVersion: 22, settings: { syncMode: "Midi" }, rows, chains, phrases, instruments },
  }));
}

test("author the LSDj MidiSync metro state (armed) and emit the Reaper fixture", () => {
  const sys = emu.loadRom(LSDJ, midiSyncSongSav(), "MidiSync");
  emu.runMs(6000); // valid sav skips the self-test; LSDj needs a few s to song screen

  expect(new Uint8Array(emu.readMemory(sys, Mem.Sram))[0x3fbd]).toBe(2); // SYNC = MIDI

  // Arm LSDj: in SYNC=MIDI, START parks it "waiting for clock" (captured in the
  // savestate so the .RPP is self-contained).
  emu.tap(sys, Button.Start, 100);
  emu.runMs(500);
  emu.screenshot(sys, "/tmp/lsdj_midi_metro_armed.png");
  expect(emu.getFrame(sys).published).toBeTruthy();

  // Emit the Reaper DAW fixture (config + armed savestate).
  emu.saveRplg("/tmp/lsdj_midi_metro_author.rplg");
});
