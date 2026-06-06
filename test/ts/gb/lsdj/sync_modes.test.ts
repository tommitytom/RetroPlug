// Replacement for examples/scripts/lsdj_aboy_sync_discovery.json.
//
// The JSON was a diagnostic: it navigated the aboy PROJECT screen and pressed
// A+Right eight times with a screenshot after each, to *empirically map* the
// SYNC cycle order (OFF, LSDJ, MIDI, KEYBD, ...) — because there was no other
// way to reach a given SYNC value. Now that the sav codec authors the SYNC byte
// directly, that whole discovery is obsolete.
//
// This deterministic replacement authors each model SyncMode value and verifies
// the codec writes the expected SYNC byte and LSDj boots cleanly with it — the
// authoring path that supersedes the UI navigation. (The aboy-only MI.MAP / MI.OUT
// entries are past the model enum; see arduinoboy_master.test.ts for why forcing
// the raw byte does not engage MI.OUT.)
import { test, expect, emu, Mem } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const SYNC_OFF = 0x3fbd; // working-song SYNC byte

// model::SyncMode (src/lsdj/model/Types.hpp): None=0,Lsdj=1,Midi=2,Keyboard=3,
// AnalogIn=4,AnalogOut=5 — matches the on-screen SYNC cycle positions 0..5.
const MODES: { name: string; byte: number }[] = [
  { name: "None", byte: 0 },
  { name: "Lsdj", byte: 1 },
  { name: "Midi", byte: 2 },
  { name: "Keyboard", byte: 3 },
  { name: "AnalogIn", byte: 4 },
  { name: "AnalogOut", byte: 5 },
];

for (const m of MODES) {
  test(`LSDj boots with authored SYNC=${m.name} (byte ${m.byte})`, () => {
    const sav = emu.savFromJson(JSON.stringify({
      workingSong: { formatVersion: 22, settings: { syncMode: m.name } },
    }));
    const sys = emu.loadRom(ABOY, sav);
    emu.runMs(4000); // valid sav skips the self-test
    emu.screenshot(sys, `/tmp/lsdj_sync_${m.name}.png`);

    const sram = new Uint8Array(emu.readMemory(sys, Mem.Sram));
    expect(sram[SYNC_OFF]).toBe(m.byte); // codec wrote the right SYNC byte
    const frame = emu.getFrame(sys);
    expect(frame.published).toBeTruthy(); // LSDj is alive with this SYNC value
  });
}
