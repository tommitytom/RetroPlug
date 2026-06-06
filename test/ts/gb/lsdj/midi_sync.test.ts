// Replacement for examples/scripts/lsdj_midi_sync.json.
//
// The JSON booted stock LSDj 15s, drove ~16 fragile chord/tap events to set
// SYNC=MIDI and build a one-note song, flipped the host transport on, and
// screenshotted to eyeball it (it never asserted audio). We author the song
// state directly (SYNC=MIDI + chain0->phrase0->C note) and boot LSDj into it.
//
// What this verifies: the MidiSync role attaches, SYNC=MIDI lands in the song,
// and LSDj stays live while the host transport drives the role's clock output.
//
// What it deliberately does NOT assert: audible playback. MidiSync emits MIDI
// clock (0xF8) only — no 0xFA start — faithfully matching the legacy
// LsdjAudioHooks (its 0xFA-for-midiSync path was commented out). LSDj SYNC=MIDI
// therefore needs a real MIDI Start from the DAW to begin, which is outside this
// role's scope. The functional "host clock advances LSDj playback" path is
// covered by arduinoboy_input.test.ts (MidiSyncArduinoboy, which does send 0xFA).
import { test, expect, emu, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const fill = <T>(n: number, f: () => T): T[] => Array.from({ length: n }, f);

function syncedSongSav(): ArrayBuffer {
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

test("LSDj SYNC=MIDI song authored + MidiSync role stays live under transport", () => {
  const sys = emu.loadRom(LSDJ, syncedSongSav(), "MidiSync");
  emu.runMs(6000); // valid sav skips the self-test; LSDj still needs a few s to boot
  emu.screenshot(sys, "/tmp/lsdj_midi_sync_boot.png");

  // SYNC=MIDI got written to the song settings (no UI navigation needed).
  const sram = new Uint8Array(emu.readMemory(sys, Mem.Sram));
  expect(sram[0x3fbd]).toBe(2); // SYNC = MIDI

  // Drive the role's clock output with a running host transport; LSDj must stay
  // live (framebuffer keeps publishing) rather than crash or hang.
  emu.setTransport(true);
  emu.runMs(4000);
  emu.setTransport(false);
  emu.screenshot(sys, "/tmp/lsdj_midi_sync_after.png");

  const frame = emu.getFrame(sys);
  expect(frame.published).toBeTruthy();
  expect(frame.width).toBeGreaterThan(0);
  expect(frame.height).toBeGreaterThan(0);
});
