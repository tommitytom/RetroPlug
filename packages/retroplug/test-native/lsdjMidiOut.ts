// The MI.OUT cases the lsdj-midiout files run, one file a host rate (44.1, 48 and 96 kHz): each file is its own host
// process, so its engine is empty when a case sets its rate. Arduinoboy MIDIOUT (MI.OUT / "Arduinoboy Master",
// mode 7) end to end on a real LSDj core: a song authored with SYNC=MI.OUT and N (NoteOn) commands, once playing,
// sends the Arduinoboy protocol out of LSDj's serial port; native clocks the port as the Arduinoboy does (`drive`,
// the capture the Game Boy node's setting places, feeding the Game Boy 0s on SIN) and captures the bits, which the
// lsdj-sync role's decoder after the Game Boy (rp.lsdj.serial-out) reads in the block they were captured in: a start
// bit, then 7 payload bits, decoded into host MIDI on its midiOut port, each message at the frame of the byte that
// completes it.
//
// What LSDj writes to its serial port is the same at every rate (the 43 bytes this song plays: 2 starts, a stop and 20
// NoteOns; docs/known-bugs-batch-2-design.md section 3), so the decoded MIDI is held to exactly that sequence: every
// NoteOn the song plays, with the values it authors, and nothing it does not send (no clock, no Program Change).
// (Not a *.test.ts, so the runner does not run it on its own; the three files import it.)
import { expect, skip } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { savFrom, type SavInput } from "@retroplug/lsdj";
import { directGraph } from "../src/graph/directGraph";

// Structure for these files' directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

declare const __RESOURCES_DIR__: string;

const ABOY = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_3_3-arduinoboy.gb";
const START = 7; // GameboyButton::Start
const MIDIOUT = "midiOut"; // LsdjSyncMode / lsdj-sync role mode

/** The N commands' values: the MIDI notes the phrase sends, one a row. */
const SONG_NOTES = [48, 52, 55, 60, 64, 67, 72, 36];

/** A phrase of eight rows, each an audible note plus an `N` (NoteOn) command whose value is what LSDj transmits: that
 *  command is what drives the MI.OUT NoteOn protocol. SYNC=MI.OUT, so LSDj sends the Arduinoboy stream (START and STOP
 *  on its transport, a command and its value for each N command). */
const midiOutSong = (values: readonly number[]): SavInput => ({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode: "MidiOut" },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [
      {
        notes: [1, 13, 25, 1, 13, 25, 1, 13],
        instruments: [0, 0, 0, 0, 0, 0, 0, 0],
        commands: ["N", "N", "N", "N", "N", "N", "N", "N"],
        commandValues: [...values],
      },
    ],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 } }],
  },
});

const noteOn = (note: number): number[] => [0x90, note, 127];

/** What 2 s of play sends: the phrase's eight NoteOns, then the first two of its next pass. */
const PLAY = [...SONG_NOTES, SONG_NOTES[0], SONG_NOTES[1]].map(noteOn);

/** The whole run's MIDI: START and the first play, STOP, START and the second play. */
export const EXPECTED_MIDI_OUT: readonly (readonly number[])[] = [[0xfa], ...PLAY, [0xfc], [0xfa], ...PLAY];

/** Boot `song` at `rate`, play it for 2 s with a START tap and, with `replay`, stop it and play it again, and return
 *  the host MIDI its decoder sent from the first tap on. */
function played(song: SavInput, rate: number, replay: boolean): { frame: number; data: number[] }[] {
  const be = createRealBackend();
  if (!be.fileExists(ABOY)) skip(`lsdj-midiout: aboy LSDj ROM not found at ${ABOY}`);

  const audio = createAudioDriver();
  expect(audio.setSampleRate(rate)).toBeTruthy();

  const id = 1;
  expect(graph.add({
    romPath: ABOY,
    platform: "gb",
    core: "sameboy",
    embeddedRom: "",
    savPath: null,
    statePath: null,
    sramBytes: savFrom(song),
  }, id)).toBeTruthy();

  try {
    expect(graph.setMidiRouting("sendToAll")).toBeTruthy();
    // The mode's capture, `drive`, rides the same document on the Game Boy's node, as a store places it.
    expect(graph.setPipeline(id, [{ kind: "lsdj-sync", config: { mode: MIDIOUT } }])).toBeTruthy();

    audio.renderAudio(6000); // reach the song screen
    audio.drainMidiOut(); // discard any boot-time noise

    // LSDj's START button toggles play and stop, and each tap sends the transport's bookend on its serial out. Play,
    // stop, then play again, so the run holds both edges and a second pass of the same notes.
    const tap = () => {
      audio.pressButton(id, START, true);
      audio.renderAudio(80);
      audio.pressButton(id, START, false);
    };
    tap(); // play: 0xFA, then the N commands' NoteOns
    audio.renderAudio(2000);
    if (replay) {
      tap(); // stop: 0xFC
      audio.renderAudio(400);
      tap(); // play again: 0xFA
      audio.renderAudio(2000);
    }
    return audio.drainMidiOut().map((m) => ({ frame: m.frame, data: Array.from(m.data) }));
  } finally {
    graph.remove(id); // a live system is emulated by every later case in the file, a failed one included
  }
}

/** Play, stop and play the song at `rate` and hold the decoded MIDI to what LSDj sent: the exact sequence, and each
 *  message at its byte's frame inside the block it was captured in. */
export function midiOutCase(rate: number): void {
  const out = played(midiOutSong(SONG_NOTES), rate, true);
  const drained = out.map((m) => m.data);
  const status = drained.map((m) => m[0]);
  const count = (pick: (s: number) => boolean) => status.filter(pick).length;
  const noteOns = count((s) => (s & 0xf0) === 0x90);
  const starts = count((s) => s === 0xfa);
  const stops = count((s) => s === 0xfc);
  const clocks = count((s) => s === 0xf8);
  const counts = `starts=${starts} stops=${stops} clocks=${clocks} noteOns=${noteOns}`;
  console.log(`[lsdj-midiout] ${rate}: ${drained.length} msgs; ${counts}`);
  // Every NoteOn the song plays, with its own value, both transport edges, and nothing else: a clock or a Program
  // Change here is a bit the capture made up, which LSDj never sent.
  expect(drained).toEqual(EXPECTED_MIDI_OUT);
  expect(noteOns).toBe(20);

  // Each message at its byte's frame in the block it was captured in: inside renderAudio's 1024-frame blocks, and
  // spread across them, never all at a block's frame 0 as a decoder timestamping by block would send them.
  const frames = out.map((m) => m.frame);
  const offZero = frames.filter((f) => f > 0).length;
  const span = `${Math.min(...frames)}..${Math.max(...frames)}`;
  console.log(`[lsdj-midiout] ${rate}: frames ${span}, ${offZero} of ${frames.length} past frame 0`);
  expect(frames.every((f) => f >= 0 && f < 1024)).toBeTruthy();
  expect(offZero > frames.length / 2).toBeTruthy();
}

/** LSDj's MI.OUT sender has no clock of its own: it sends a command (0x70 to 0x7B) and its value for each N, X, Y and
 *  Q command, and 0x7D and 0x7E at its transport's edges, and no setting adds a 0x7F (ROM bank 3, $7ADE to $7BBC, in
 *  9.3.3). But an N command's value goes on the wire as it is, so N7F sends the byte an Arduinoboy reads as a clock,
 *  its NoteOn left waiting for a value: the one way a real LSDj reaches the decoder's 0x7F path. Played at 44.1 kHz,
 *  every other row's N7F is a 0xF8 where its NoteOn would be. */
export function midiOutClockCase(): void {
  const values = [48, 0x7f, 52, 0x7f, 55, 0x7f, 60, 0x7f];
  const drained = played(midiOutSong(values), 44100, false).map((m) => m.data);
  console.log(`[lsdj-midiout] N7F: ${JSON.stringify(drained)}`);
  const clock = [0xf8];
  expect(drained).toEqual([[0xfa], noteOn(48), clock, noteOn(52), clock, noteOn(55), clock, noteOn(60), clock,
    noteOn(48), clock]);
}
