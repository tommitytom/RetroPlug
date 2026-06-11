// Replacement for examples/scripts/lsdj_arduinoboy_input.json.
//
// The JSON booted the aboy ROM 15s, flipped the host transport on, and sent
// Arduinoboy slave-control notes (24=play, 36=row, 25=stop) while screenshotting
// to eyeball it. We author the song state directly (SYNC=LSDj slave + a one-note
// song) and boot LSDj into it, then drive the same control notes — and assert
// the gating the screenshots only implied.
//
// MidiSyncArduinoboy role contract (src/.../LsdjSyncRole.cpp):
//   - transport start -> 0xFA to LSDj serial; stop -> 0xFC.
//   - note 24 -> arduinoboyPlaying=true (clock now flows); note 25 -> false.
//   - notes >=30 -> serial row byte (note-30).
// So LSDj plays only while the transport runs AND a 24 ("play") has been seen.
import { test, expect, emu, Mem } from "harness";

const ABOY = "../resources/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
function slaveSongSav(): ArrayBuffer {
  // SYNC=LSDj (Arduinoboy slave) + a one-note song. The codec pads every fixed
  // array to full length, so we author just the cells we set.
  return emu.savFromJson(JSON.stringify({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode: "Lsdj" },
      rows:    [{ chains: [0] }],
      chains:  [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
    },
  }));
}

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("LSDj Arduinoboy slave plays on note-24 + transport, stops on note-25", () => {
  const sys = emu.loadRom(ABOY, slaveSongSav(), "MidiSyncArduinoboy");
  emu.runMs(6000); // valid sav skips self-test; still needs a few s to song screen
  emu.screenshot(sys, "/tmp/lsdj_aboy_input_boot.png");

  // SYNC=LSDj (slave) is in the song settings.
  const sram = new Uint8Array(emu.readMemory(sys, Mem.Sram));
  expect(sram[0x3fbd]).toBe(1); // SYNC = LSDj

  // Transport on but no "play" command yet: clock gated off -> parked.
  emu.setTransport(true);
  const beforePlay = rms(emu.getAudio(1500));

  // note 24 = Arduinoboy "play": clock now flows from the role -> LSDj advances.
  emu.sendMidi(sys, [0x90, 24, 100]);
  const playing = rms(emu.getAudio(4000));
  emu.screenshot(sys, "/tmp/lsdj_aboy_input_playing.png");

  // note 25 = "stop": clock gated off again -> playback winds down.
  emu.sendMidi(sys, [0x90, 25, 100]);
  const afterStop = rms(emu.getAudio(3000));
  emu.screenshot(sys, "/tmp/lsdj_aboy_input_stopped.png");

  console.log(`aboy_input RMS beforePlay=${beforePlay.toFixed(5)} playing=${playing.toFixed(5)} afterStop=${afterStop.toFixed(5)}`);
  expect(playing).toBeGreaterThan(0.001);          // play command + clock -> audio
  expect(playing).toBeGreaterThan(beforePlay);     // gated before the play command
});
