// Bootstrap for the reaper-lsdj-midi-drift DAW fixture: the hour-long, per-beat
// counterpart to lsdj_midi_metro.test.ts.
//
// Authors a per-beat pulse "click", sets SYNC=MIDI, arms LSDj's WAIT-for-clock
// state, and emits the .rplg that reaper-lsdj-midi-drift-author bakes into
// examples/reaper/lsdj_midi_drift.rpp.
//
// SYNC is set via the PROJECT-screen UI rather than the authored sav byte
// because the model's SyncMode enum doesn't match LSDj 9.4.2's on-disk encoding
// (model Midi=2, but 9.4.2's MIDI is byte 3; byte 2 is an unused/blank slot).
// Two A+Right cycles SYNC to the ROM's real MIDI value (short key-holds dodge
// LSDj's auto-repeat). Once the SyncMode encoding is fixed in the codec this can
// author syncMode:"Midi" directly and drop the navigation.
//
// (The other half of the authoring bug — grooves defaulting to all-zeros instead
// of LSDj's factory 6/6, which left LSDj unable to advance — is fixed in the
// model: Groove::steps now defaults to 6/6, so this fixture loops correctly.)
import { test, expect, emu, Button, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

function songSav(): ArrayBuffer {
  return emu.savFromJson(JSON.stringify({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode: "None", tempo: 120 }, // set to MIDI via the UI below
      rows:    [{ chains: [0] }],
      chains:  [{ phrases: [0] }],
      // One note per beat: steps 0/4/8/12 (4 steps/beat at groove 6, 24 PPQN).
      phrases: [{
        notes:       [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
        instruments: [0, null, null, null, 0, null, null, null,
                      0, null, null, null, 0, null, null, null],
      }],
      // Short percussive click: full level, fastest decay, hard LENGTH cut (~4
      // frames) so each beat is a distinct transient with silence before the
      // next — the per-beat onsets the drift analyzer pairs against the click.
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 15, attackSpeed: 15, attackLevel: 15, decaySpeed: 7, sustainLevel: 0, releaseSpeed: 7 }, length: 4 }],
    },
  }));
}

test("author the LSDj MidiSync per-beat drift state (armed) and emit the Reaper fixture", () => {
  const sys = emu.loadRom(LSDJ, songSav(), "MidiSync");
  emu.runMs(6000); // valid sav skips the self-test; LSDj needs a few s to song screen

  // Cycle SYNC to MIDI via the PROJECT screen (Off -> Lsdj -> Midi).
  emu.chord(sys, [Button.Select, Button.Up]); emu.runMs(500);     // SONG -> PROJECT
  emu.tap(sys, Button.Down, 50); emu.runMs(300);                  // cursor -> TRANSPOSE
  emu.tap(sys, Button.Down, 50); emu.runMs(300);                  // cursor -> SYNC
  emu.chord(sys, [Button.A, Button.Right], { holdMs: 40 }); emu.runMs(400);
  emu.chord(sys, [Button.A, Button.Right], { holdMs: 40 }); emu.runMs(400);
  emu.chord(sys, [Button.Select, Button.Down]); emu.runMs(400);   // PROJECT -> SONG
  emu.runMs(1500);
  emu.screenshot(sys, "/tmp/lsdj_midi_drift_sync.png");
  const syncByte = new Uint8Array(emu.readMemory(sys, Mem.Sram))[0x3fbd];
  console.log(`[fixture] SYNC SRAM byte after UI set = 0x${syncByte.toString(16)} (9.4.2 MIDI = 3)`);
  expect(syncByte > 0).toBeTruthy();

  // Arm LSDj: in SYNC=MIDI, START parks it "waiting for clock".
  emu.tap(sys, Button.Start, 100);
  emu.runMs(500);
  emu.screenshot(sys, "/tmp/lsdj_midi_drift_armed.png");
  expect(emu.getFrame(sys).published).toBeTruthy();

  emu.saveRplg("/tmp/lsdj_midi_drift_author.rplg");
});
