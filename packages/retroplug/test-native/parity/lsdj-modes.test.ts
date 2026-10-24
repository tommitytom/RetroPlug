// LSDj's other lsdj-sync modes, each with the stimulus it answers to: free-running (off, keyboard),
// row launches from MIDI notes (midiMap), live keys (keyboardMidi), raw MIDI on the serial port
// (midiPassthrough), and LSDj as the source: MI.OUT notes and clocks (midiOut) and LSDj's own clock
// driving the host (masterSync), both captured as MIDI out. lsdj.ts has the scenes.
import { runGroup, type ParityCase } from "./group";
import { blocks, lsdj, lsdjCase, midiOutSav, songSav, SR, tap } from "./lsdj";
import { frames, ROMS } from "./scenes";

const play = (ms: number) => ({ blocks: blocks(frames(SR, ms)) });
const at = (ms: number) => frames(SR, ms);

const cases: ParityCase[] = [
  lsdjCase("off", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("None"), { mode: "off" }), [...tap(), play(1500)]),
  lsdjCase("keyboard", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Keyboard"), { mode: "keyboard" }), [...tap(), play(1500)]),
  lsdjCase("midiMap", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, songSav("MidiMap"), { mode: "midiMap" }), [
    play(500),
    { blocks: blocks(at(2000)), midi: [{ at: 0, bytes: [0x90, 0, 100] }, { at: at(1800), bytes: [0x80, 0, 0] }] },
  ]),
  lsdjCase("keyboardMidi", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, songSav("Keyboard"), { mode: "keyboardMidi" }), [
    ...tap(),
    play(500),
    { blocks: blocks(at(1500)), midi: [48, 52, 55, 60, 64, 67].map((key, i) => ({ at: at(250 * i), bytes: [0x90, key, 100] })) },
  ]),
  // Raw MIDI forwarded to the serial port, byte for byte. A cart armed in SYNC=MIDI steps on each byte it
  // receives, so a run of clock bytes plays the song (with SYNC off the bytes leave no trace at all).
  lsdjCase("midiPassthrough", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Midi"), { mode: "midiPassthrough" }), [
    ...tap(),
    { blocks: blocks(at(1500)), midi: Array.from({ length: 72 }, (_, i) => ({ at: at(20 * i), bytes: [0xf8] })) },
  ]),
  // Play, stop (0xFC), play again (0xFA): MI.OUT's notes, clocks and transport bytes.
  lsdjCase("midiOut", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, midiOutSav(), { mode: "midiOut" }), [
    ...tap(),
    play(2000),
    ...tap(),
    play(400),
    ...tap(),
    play(2000),
  ]),
  // LSDj as the clock master renders silence here, as it does today; its output is the MIDI clock (with a
  // start and a stop) it drives the host with, which the case captures.
  lsdjCase("masterSync", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }), [...tap(), play(1500), ...tap(), play(1200)], 2, []),
];

runGroup("lsdj-modes", cases);
