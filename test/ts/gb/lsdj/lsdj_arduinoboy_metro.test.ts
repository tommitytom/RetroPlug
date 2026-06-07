// Replacement for examples/scripts/lsdj_arduinoboy_metro_setup.json.
//
// The JSON script booted the aboy ROM for 15s (SRAM self-test) and then drove
// ~12 fragile PROJECT/SONG/CHAIN/PHRASE chord events to build the LSDj state:
// SYNC=LSDj plus chain 00 -> phrase 00 -> one note (instrument 00). All of that
// is just bytes in the .sav, so here we author it directly with the sav codec
// and boot LSDj straight into it — a valid sav skips the self-test, so this is
// both fast and robust (no timing-sensitive chord navigation).
//
// The authored values match what the original navigation produced, read back
// from `retroplug-cli --script lsdj_arduinoboy_metro_setup.json --save-sav`:
//   sync=LSDj, row0.ch0=chain0, chain0.step0=phrase0, phrase0.step0 = note 1 /
//   instrument 0, and instrument 0 = LSDj's default pulse.
import { test, expect, emu, Button, Mem } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const fill = <T>(n: number, f: () => T): T[] => Array.from({ length: n }, f);

function authoredSav(): ArrayBuffer {
  const rows = fill(256, () => ({ chains: [null, null, null, null] as (number | null)[] }));
  rows[0].chains[0] = 0; // SONG row 0, pulse1 -> chain 00

  const chains = fill(128, () => null as unknown);
  chains[0] = { phrases: [0, ...fill(15, () => null)], transpositions: fill(16, () => 0) };

  const phrases = fill(256, () => null as unknown);
  phrases[0] = {
    notes: [1, ...fill(15, () => 0)],
    instruments: [0, ...fill(15, () => null)],
    commands: fill(16, () => "None"),
    commandValues: fill(16, () => 0),
  };

  const instruments = fill(64, () => null as unknown);
  instruments[0] = { // LSDj default pulse (the rest defaults via DefaultIfMissing)
    type: "pulse", panning: "LeftRight",
    adsr: { initialLevel: 8, attackSpeed: 8 },
    vibrato: { direction: "Up" }, sweep: 127,
  };

  return emu.savFromJson(JSON.stringify({
    workingSong: { formatVersion: 22, settings: { syncMode: "Lsdj" }, rows, chains, phrases, instruments },
  }));
}

test("author the arduinoboy-metro LSDj state from TS and boot aboy into it", () => {
  // Attach the MidiSyncArduinoboy role so the .rplg fixture below captures it
  // (the original setup configured the role via the autoload .rplg).
  const sys = emu.loadRom(ABOY, authoredSav(), "MidiSyncArduinoboy");
  emu.runMs(3000); // GB boot logo only — the valid sav skips the 12-15s self-test
  emu.screenshot(sys, "/tmp/aboy_authored_song.png");

  const sram = new Uint8Array(emu.readMemory(sys, Mem.Sram));
  expect(sram.length).toBe(0x20000);
  // sav loaded (not a fresh self-test)
  expect(sram[0x813e]).toBe(0x6a); // 'j'
  expect(sram[0x813f]).toBe(0x6b); // 'k'
  // the exact authored state survived the boot, byte-for-byte vs the navigated sav:
  expect(sram[0x3fbd]).toBe(1);    // SYNC = LSDj
  expect(sram[0x1290]).toBe(0);    // SONG row0.ch0 -> chain 0
  expect(sram[0x3ea2] & 1).toBe(1); // chain 0 allocated
  expect(sram[0x2080]).toBe(0);    // chain0.step0 -> phrase 0
  expect(sram[0x3e82] & 1).toBe(1); // phrase 0 allocated
  expect(sram[0x0000]).toBe(1);    // phrase0.step0 note = 1
  expect(sram[0x7000]).toBe(0);    // phrase0.step0 instrument = 0
  expect(sram[0x2040]).toBe(1);    // instrument 0 allocated

  // Emit the Reaper DAW fixture: reaper-lsdj-arduinoboy-author bakes this .rplg
  // (config + savestate) into examples/reaper/lsdj_arduinoboy_metro.rpp.
  emu.saveRplg("/tmp/lsdj_arduinoboy_metro_author.rplg");
});
