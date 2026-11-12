// Arduinoboy MIDIOUT (MI.OUT, mode 7) at 48 kHz: the case lsdj-midiout.test.ts runs at 44.1 (./lsdjMidiOut.ts), at a
// rate whose clocking once decoded the song into other notes and Program Changes it never sends.
import { test } from "@retroplug/testing";
import { midiOutCase } from "./lsdjMidiOut";

test("MIDIOUT (mode 7) at 48 kHz: a real LSDj's decoded MI.OUT is exactly the MIDI its song plays", () => {
  midiOutCase(48000);
});
