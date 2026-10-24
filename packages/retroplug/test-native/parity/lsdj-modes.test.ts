// LSDj's other lsdj-sync modes, each with the stimulus it answers to: free-running (off, keyboard),
// row launches from MIDI notes (midiMap), live keys (keyboardMidi), raw MIDI on the serial port
// (midiPassthrough), and LSDj as the source: MI.OUT notes and clocks (midiOut) and LSDj's own clock
// driving the host (masterSync), both captured as MIDI out. lsdj.ts has the scenes.
import { runGroup, type ParityCase } from "./group";
import { blocks, denseSongSav, lsdj, lsdjCase, midiOutSav, songSav, SR, tap } from "./lsdj";
import { frames, ROMS } from "./scenes";
import { even } from "./rig";

const play = (ms: number) => ({ blocks: blocks(frames(SR, ms)) });
const by1024 = (n: number) => even(n, 1024);
const at = (ms: number) => frames(SR, ms);

const cases: ParityCase[] = [
  lsdjCase("off", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("None"), { mode: "off" }), [...tap(), play(1500)]),
  // The role emits nothing for Keyboard (it needs a host keys feed, which is not built), so this case only
  // pins that the mode stays inert.
  lsdjCase("keyboard", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Keyboard"), { mode: "keyboard" }), [...tap(), play(1500)]),
  // MI.MAP launches a row from a note, and its clock (the role's 0xFF stream, one per tick) only runs while the
  // host transport does, so the transport starts with the launch: otherwise the row freezes on its first step.
  lsdjCase("midiMap", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, denseSongSav("MidiMap"), { mode: "midiMap" }), [
    play(500),
    {
      blocks: blocks(at(2000)),
      transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }],
      midi: [{ at: 0, bytes: [0x90, 0, 100] }, { at: at(1800), bytes: [0x80, 0, 0] }],
    },
  ]),
  lsdjCase("keyboardMidi", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, songSav("Keyboard"), { mode: "keyboardMidi" }), [
    ...tap(),
    play(500),
    { blocks: blocks(at(1500)), midi: [48, 52, 55, 60, 64, 67].map((key, i) => ({ at: at(250 * i), bytes: [0x90, key, 100] })) },
  ]),
  // Raw MIDI forwarded to the serial port, byte for byte. A cart armed in SYNC=MIDI steps on each byte it
  // receives, so a run of clock bytes plays the song (with SYNC off the bytes leave no trace at all).
  lsdjCase("midiPassthrough", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, denseSongSav("Midi"), { mode: "midiPassthrough" }), [
    ...tap(),
    { blocks: blocks(at(1500)), midi: Array.from({ length: 72 }, (_, i) => ({ at: at(20 * i), bytes: [0xf8] })) },
  ]),
  // Play, stop, play again, with MI.OUT's MIDI captured. KNOWN DEFECT, recorded as it is (design section 2.5):
  // the captured messages depend on the host sample rate (at 48 kHz NoteOns 0x52-0x6D plus program changes
  // the song never sends, at 44.1 kHz 0x69-0x6D) and none match the song's N values (48-72), so this is
  // partly a capture artefact. It pins what is captured today, not what MI.OUT should send.
  lsdjCase("midiOut", ROMS.arduinoboy, (rig) => lsdj(rig, ROMS.arduinoboy, midiOutSav(), { mode: "midiOut" }), [
    ...tap(),
    play(2000),
    ...tap(),
    play(400),
    ...tap(),
    play(2000),
  ]),
  // LSDj as the clock master, captured as MIDI out: start, clocks, the idle-flood stop. The RATE is the
  // cause of the defect (48 kHz at 1024 frames is also broken; 44.1 kHz at 512 plays). 1024 frames is chosen
  // separately: at 512 the boot-time bytes stay under the decoder's 16-bytes-per-block flood threshold and
  // open a run that never closes before START, so the real run gets no NoteOn / 0xFA. At 44.1 kHz in 1024
  // it works (about 51 clocks/s, and it plays).
  lsdjCase(
    "masterSync-44100",
    ROMS.lsdj,
    (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }),
    [...tap(0, 44100, by1024), { blocks: by1024(frames(44100, 1500)) }, ...tap(0, 44100, by1024), { blocks: by1024(frames(44100, 1200)) }],
    2,
    [0, 1],
    44100,
    by1024,
  ),
  // KNOWN DEFECT, recorded as it is (design section 2.5): at 48 kHz and above, with serial-out capture armed,
  // master sync is broken: the only MIDI out is boot noise and nothing follows START, and it is silent
  // (mode off plays at 48 kHz).
  lsdjCase("masterSync-48000", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "masterSync" }), [...tap(), play(1500), ...tap(), play(1200)], 2, []),
];

runGroup("lsdj-modes", cases);
