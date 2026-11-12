// Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master", mode 7) at 44.1 kHz: the full end-to-end proof on a real LSDj
// core, that the MIDI its decoder sends is exactly what the song plays, and the one way a real LSDj reaches the
// decoder's clock, an N command's value of 0x7F (./lsdjMidiOut.ts runs the cases; the lsdj-midiout-48k and
// lsdj-midiout-96k files run the first at their rates, each a host process of its own).
//
// This is the reversal of the old "MI.OUT is unreachable headlessly" folklore (docs/lsdj.md): SYNC=MI.OUT is byte 9
// (cold-authored here), and the wire's 1-flag-bit + 7-payload-bit framing, which the raw 8-bit capture had mis-read
// as garbage, decodes cleanly once the capture feeds the Game Boy the 0s an Arduinoboy does.
import { test } from "@retroplug/testing";
import { midiOutCase, midiOutClockCase } from "./lsdjMidiOut";

test("MIDIOUT (mode 7) at 44.1 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays", () => {
  midiOutCase(44100);
});

test("MIDIOUT (mode 7): an N command's value of 0x7F reaches the host as an Arduinoboy reads it, a clock", () => {
  midiOutClockCase();
});
