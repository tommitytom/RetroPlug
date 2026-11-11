// Port-name matching (docs/modular-graph-phase-f5b-design.md section 6.3): a project saves the ports it opened, and the
// same rig on another machine, or the same machine after other clients came and went, lists them under names with
// volatile parts. The table below is the whole point of the module: real ALSA, WinMM and CoreMIDI names, and the rule
// that a normalised match has to be UNIQUE.
import { test, expect } from "@retroplug/testing";
import { foldPortName, resolvePort, resolvePorts } from "../portMatch";

/** The names a real machine lists, by platform: ALSA's "<client>:<port> <client#>:<port#>" (RtMidi's form), WinMM's
 *  "<name> <index>" suffix (deps/rtmidi appends the port number to every Windows name), and CoreMIDI's own spelling.
 *
 *  The WinMM and CoreMIDI rows are UNVERIFIED: no Windows or macOS machine has listed its ports for this table yet
 *  (design section 11). The WinMM form is the vendored RtMidi's own (deps/rtmidi/RtMidi.cpp's getPortName), and the
 *  CoreMIDI rows are the spellings the Launchpad pack's tests carry; the agents replace them with real listings. */
const ALSA = [
  "LPProMK3 MIDI:LPProMK3 MIDI 24:0",
  "Launchpad Pro MK3:Launchpad Pro MK3 LPProMK3 MIDI 28:0",
  "Midi Through:Midi Through Port-0 14:0",
  "Arturia KeyStep 32:Arturia KeyStep 32 MIDI 1 20:0",
  // A two-port interface: both names end in a number of their own, and only the client:port suffix is volatile.
  "MIDISPORT 2x2:MIDISPORT 2x2 MIDI 1 28:0",
  "MIDISPORT 2x2:MIDISPORT 2x2 MIDI 2 28:1",
];
const WINMM = ["Microsoft GS Wavetable Synth 0", "MIDISPORT 2x2 Port A 1", "MIDISPORT 2x2 Port B 2"];
const COREMIDI = ["Launchpad Pro MK3", "IAC Driver Bus 1", "Launchpad Pro MK3 LPProMK3 MIDI"];

test("an exact name wins, and needs no normalising", () => {
  expect(resolvePort("IAC Driver Bus 1", COREMIDI)).toBe("IAC Driver Bus 1");
  expect(resolvePort("Microsoft GS Wavetable Synth 0", WINMM)).toBe("Microsoft GS Wavetable Synth 0");
});

test("ALSA's client:port numbers move, and the saved name still finds its port", () => {
  // The same interface, listed after another client came and went: the saved 24:0 is 28:0 now, and 26:0 is not in the
  // list at all.
  expect(resolvePort("LPProMK3 MIDI:LPProMK3 MIDI 26:0", ALSA)).toBe("LPProMK3 MIDI:LPProMK3 MIDI 24:0");
  expect(resolvePort("Launchpad Pro MK3:Launchpad Pro MK3 LPProMK3 MIDI 26:0", ALSA)).toBe(
    "Launchpad Pro MK3:Launchpad Pro MK3 LPProMK3 MIDI 28:0",
  );
  expect(resolvePort("Midi Through:Midi Through Port-0 99:0", ALSA)).toBe("Midi Through:Midi Through Port-0 14:0");
});

test("WinMM's trailing index moves, and the saved name still finds its port", () => {
  // A driver reinstalled (or another device added) renumbers the list, not the name (RtMidi's own suffix form).
  expect(resolvePort("MIDISPORT 2x2 Port A 9", WINMM)).toBe("MIDISPORT 2x2 Port A 1");
  expect(resolvePort("MIDISPORT 2x2 Port B 4", WINMM)).toBe("MIDISPORT 2x2 Port B 2");
  expect(resolvePort("Microsoft GS Wavetable Synth 0", WINMM)).toBe("Microsoft GS Wavetable Synth 0"); // exact
  // A name that already ended in a number on a platform that does not append one still matches exactly first.
  expect(resolvePort("IAC Driver Bus 1", COREMIDI)).toBe("IAC Driver Bus 1");
});

test("CoreMIDI's repeated device name folds to one, in both directions", () => {
  expect(resolvePort("Launchpad Pro MK3 Launchpad Pro MK3", COREMIDI)).toBe("Launchpad Pro MK3");
  expect(resolvePort("Launchpad Pro MK3", ["Launchpad Pro MK3 Launchpad Pro MK3"]))
    .toBe("Launchpad Pro MK3 Launchpad Pro MK3");
  // The port name that carries the device name and its own does NOT fold the same way: ".. MK3 LPProMK3 MIDI" is not a
  // repeat, so it stays itself rather than colliding with the bare device name.
  expect(foldPortName("Launchpad Pro MK3 LPProMK3 MIDI")).toBe("Launchpad Pro MK3 LPProMK3 MIDI");
});

test("ALSA's multi-port interface keeps its two ports apart when the client renumbers", () => {
  // The case the ALSA rule exists for, on an interface whose port names END in a number: with the index rule applied
  // to every name, "MIDI 1 24:0" and "MIDI 2 28:1" both folded to "... MIDI", so the saved entry matched two ports (or
  // none) and the row said "(missing)" after every renumber. Only the trailing client:port may move.
  expect(resolvePort("MIDISPORT 2x2:MIDISPORT 2x2 MIDI 1 24:0", ALSA))
    .toBe("MIDISPORT 2x2:MIDISPORT 2x2 MIDI 1 28:0");
  expect(resolvePort("MIDISPORT 2x2:MIDISPORT 2x2 MIDI 2 24:1", ALSA))
    .toBe("MIDISPORT 2x2:MIDISPORT 2x2 MIDI 2 28:1");
});

test("spacing is the machine's punctuation, not the device's name", () => {
  expect(resolvePort("Arturia  KeyStep 32 ", ["Arturia KeyStep 32"])).toBe("Arturia KeyStep 32");
  expect(resolvePort("Arturia KeyStep 32", [" Arturia  KeyStep 32 "])).toBe(" Arturia  KeyStep 32 ");
});

test("two candidates resolve to nothing; so does none", () => {
  // Two identical interfaces: the stem matches both, and guessing would silently drive the wrong one.
  const two = ["LPProMK3 MIDI:LPProMK3 MIDI 24:0", "LPProMK3 MIDI:LPProMK3 MIDI 30:0"];
  expect(resolvePort("LPProMK3 MIDI:LPProMK3 MIDI 26:0", two)).toBe(null);
  // Nothing present matches the saved name at all.
  expect(resolvePort("Some Synth", ALSA)).toBe(null);
  expect(resolvePort("", COREMIDI)).toBe(null);
  // A name that normalises away entirely names nothing.
  expect(resolvePort("   ", COREMIDI)).toBe(null);
});

test("a whole saved list resolves in order, dropping what opened nothing", () => {
  const saved = ["Launchpad Pro MK3", "Gone Synth", "IAC Driver Bus 1", "Launchpad Pro MK3"];
  expect(resolvePorts(saved, COREMIDI)).toEqual(["Launchpad Pro MK3", "IAC Driver Bus 1"]);
});
