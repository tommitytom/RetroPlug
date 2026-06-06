// Replacement for examples/scripts/lsdj_arduinoboy_master.json.
//
// The JSON booted aboy 15s, drove fragile PROJECT-screen chords trying to reach
// SYNC=MI.OUT, but the aboy ROM stops accepting A+Right past KEYBD, so it landed
// on KEYBD and verified only the serial-out *capture + synthetic-clock* path
// (150k+ raw bytes). We author the song + SYNC=KEYBD directly (no navigation)
// and assert the same capture path through the new drainSerial binding.
//
// Scope (unchanged from the JSON):
//  - The ArduinoboyMaster role attaches and enables serial-out capture.
//  - SameBoySystem's synthetic Arduinoboy clock shifts LSDj's serial register
//    while it polls the port (KEYBD / external-clock mode), so bytes flow.
//  - drainSerial delivers those captured bytes to the test.
// Out of scope: functional MI.OUT protocol decode. The MI.OUT byte->MIDI decoder
// is unit-tested in test/ArduinoboyMasterTests.cpp. Reaching MI.OUT mode
// end-to-end is still future work: forcing the raw SYNC byte to 7 boots LSDj but
// does NOT engage the MI.OUT protocol (it emits idle 0x00/0xFF, not 0x7D/0x7F) —
// a pre-configured MI.OUT savestate fixture is needed.
import { test, expect, emu, Button, Mem } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const fill = <T>(n: number, f: () => T): T[] => Array.from({ length: n }, f);
const SYNC_OFF = 0x3fbd;

function keybdSongSav(): ArrayBuffer {
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
    workingSong: { formatVersion: 22, settings: { syncMode: "Keyboard" }, rows, chains, phrases, instruments },
  }));
}

test("ArduinoboyMaster role captures LSDj serial-out via the synthetic clock", () => {
  const sys = emu.loadRom(ABOY, keybdSongSav(), "ArduinoboyMaster");
  emu.runMs(6000); // valid sav skips self-test; LSDj needs a few s to song screen
  emu.screenshot(sys, "/tmp/lsdj_aboy_master_boot.png");

  // SYNC=KEYBD landed in the song (byte 3) — KEYBD polls the serial port.
  const sram = new Uint8Array(emu.readMemory(sys, Mem.Sram));
  expect(sram[SYNC_OFF]).toBe(3); // SYNC = KEYBD

  emu.drainSerial(sys); // clear any boot-time transients
  emu.tap(sys, Button.Start, 100);
  emu.runMs(5000);

  const serial = emu.drainSerial(sys);
  console.log(`aboy_master: captured ${serial.length} serial-out bytes`);
  // The synthetic clock shifts roughly one bit/sample while LSDj polls the port,
  // so 5s of playback yields thousands of captured bytes. A nonzero, large count
  // proves the capture binding + synthetic-clock path end-to-end.
  expect(serial.length).toBeGreaterThan(1000);
  // Each captured entry carries an absolute sample position and a byte value.
  expect(serial[0].byte).toBeGreaterThan(-1);
  expect(serial[serial.length - 1].sample).toBeGreaterThan(0);
});
