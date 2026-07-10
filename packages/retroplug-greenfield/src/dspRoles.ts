// The built-in DSP-thread role behaviors: doc-06 translators/sources authored as plain TS over the
// per-system context (dspKernel.ts). `mgb` + `lsdj-sync` are the two we're migrating off their
// legacy C++ roles; `midi-routing` is the project-scope behavior that fans host MIDI to systems,
// reusing the existing routeBlock decision. Registered into a RoleRegistry like registerCoreRoles.

import type { RoleRegistry, ConstructCaps } from "./systemRoles";
import type { ProjectBehavior, SystemBehavior, SystemCtx } from "./dspKernel";
import type { ConstructSpec } from "./backend";
import { z, clampedInt } from "./configSchema";
import { routeBlockInto, MidiRouting } from "./midiRouting";
import {
  KEYBOARD_NOTE_START,
  KEYBOARD_LOW_START,
  KEYBOARD_NOTE_MAP,
  KEYBOARD_LOW_OCTAVE_MAP,
  KEYBOARD_OCT_UP,
  KEYBOARD_OCT_DN,
  isExtendedScancode,
  toGbSerialByte,
} from "./lsdjKeyboardMap";

// Forward every host-MIDI byte verbatim into the system's serial input. This is both `mgb`
// (== MgbPassthroughRole) and lsdj-sync's MidiPassthrough mode (== LsdjSyncRole handlePassthrough).
const forwardMidiToSerial: SystemBehavior = (c) => {
  // Indexed loops, not `.forEach` — the nested forEach allocated a closure per call + one per event,
  // and this is the hot mGB path (spec/08-profiling.md).
  const midi = c.midi;
  for (let i = 0; i < midi.length; i++) {
    const e = midi[i];
    const data = e.data;
    for (let j = 0; j < data.length; j++) c.pushSerialIn(e.frame, data[j]);
  }
};
const mgb = forwardMidiToSerial;

// Forward every routed host-MIDI message straight to the core's onMidi (the emitCoreMidi sink). The
// NES twin of `mgb`: a core with no serial port (Mesen) receives MIDI here instead of over serial.
// The whole message goes as one event — the native NesN8MidiRole iterates the bytes into its FIFO.
const forwardMidiToCore: SystemBehavior = (c) => {
  const midi = c.midi;
  for (let i = 0; i < midi.length; i++) {
    const e = midi[i];
    c.emitCoreMidi(e.frame, e.data);
  }
};

// LSDj serial control bytes (host → LSDj over the link cable) and MIDI status helpers, mirroring
// LsdjSyncRole.cpp. DPF hands the full status byte (channel in the low nibble) in data[0].
const LSDJ_CLOCK = 0xf8; // 24-PPQN MIDI clock tick
const LSDJ_START = 0xfa; // transport start — Arduinoboy-mode bookend
const LSDJ_STOP = 0xfc; // transport stop
const MIDIMAP_NOTEOFF = 0xfe; // MidiMap NoteOff handshake sentinel
const isNoteOn = (status: number) => (status & 0xf0) === 0x90;
const isNoteOff = (status: number) => (status & 0xf0) === 0x80;
const channelOf = (status: number) => status & 0x0f;
// MidiMap row byte: ch0 NoteOn → note; ch1 → note + 128; other channels skipped (-1).
const midiMapRow = (channel: number, note: number) => (channel === 0 ? note : channel === 1 ? note + 128 : -1);

// MidiSyncArduinoboy (== LsdjSyncRole MidiSyncArduinoboy). Input notes drive runtime state: 24/25 toggle
// the play flag, 26-29 set the tempo divisor, 30+ push a raw row byte (note-30). The 0xF8 clock flows
// only while the play flag is set (NOT on host transport), and 0xFA/0xFC bookend host-transport edges.
const arduinoboy: SystemBehavior = (c) => {
  const st = c.state as { playing?: boolean; divisor?: number; prevTransport?: boolean };
  if (st.divisor === undefined) st.divisor = (c.config.tempoDivisor as number) || 1;
  for (const e of c.midi) {
    if (!isNoteOn(e.data[0])) continue;
    const note = e.data[1];
    if (note === 24) st.playing = true;
    else if (note === 25) st.playing = false;
    else if (note === 26) st.divisor = 1;
    else if (note === 27) st.divisor = 2;
    else if (note === 28) st.divisor = 4;
    else if (note === 29) st.divisor = 8;
    else if (note >= 30) c.pushSerialIn(e.frame, note - 30);
  }
  if (c.block.transport !== (st.prevTransport ?? false)) {
    c.pushSerialIn(0, c.block.transport ? LSDJ_START : LSDJ_STOP);
    st.prevTransport = c.block.transport;
  }
  if (st.playing) c.eachTick(24 / (st.divisor || 1), (_t, off) => c.pushSerialIn(off, LSDJ_CLOCK));
};

// MidiMap (== LsdjSyncRole handleMidiMap): NoteOn → a row byte LSDj reads as a SONG-row jump; a matching
// NoteOff sends the 0xFE handshake. lastRow persists across blocks so the NoteOff only fires for the row
// most recently sounded.
const midiMap: SystemBehavior = (c) => {
  const st = c.state as { lastRow?: number };
  if (st.lastRow === undefined) st.lastRow = -1;
  for (const e of c.midi) {
    const status = e.data[0];
    const note = e.data.length >= 2 ? e.data[1] : 0;
    if (isNoteOn(status)) {
      const row = midiMapRow(channelOf(status), note);
      if (row < 0) continue;
      c.pushSerialIn(e.frame, row & 0xff);
      st.lastRow = row;
    } else if (isNoteOff(status)) {
      if (midiMapRow(channelOf(status), note) === st.lastRow) {
        c.pushSerialIn(e.frame, MIDIMAP_NOTEOFF);
        st.lastRow = -1;
      }
    }
  }
};

// Slide LSDj's internal keyboard octave toward `target` by emitting OCT_UP/OCT_DN scancodes; returns the
// new octave. Mirrors slideKeyboardOctave in LsdjSyncRole.cpp.
function slideKeyboardOctave(c: SystemCtx, frame: number, target: number, current: number): number {
  if (target === current) return current;
  const code = toGbSerialByte(target > current ? KEYBOARD_OCT_UP : KEYBOARD_OCT_DN);
  let steps = Math.abs(target - current);
  while (steps-- > 0) c.pushSerialIn(frame, code);
  return target;
}

// KeyboardMidi (== LsdjSyncRole handleKeyboardMidi): translate MIDI NoteOns into LSDj PS/2 scancodes,
// sliding the octave cursor to track the incoming note. octave persists across blocks.
const keyboardMidi: SystemBehavior = (c) => {
  const st = c.state as { octave?: number };
  if (st.octave === undefined) st.octave = 4;
  for (const e of c.midi) {
    if (!isNoteOn(e.data[0])) continue;
    let note = e.data[1];
    if (note >= KEYBOARD_NOTE_START) {
      note -= KEYBOARD_NOTE_START;
      st.octave = slideKeyboardOctave(c, e.frame, Math.floor(note / 12), st.octave);
      const idx = note >= 0x3c ? (note % 12) + 0x0c : note % 12; // two rows of note keys
      c.pushSerialIn(e.frame, toGbSerialByte(KEYBOARD_NOTE_MAP[idx]));
    } else if (note >= KEYBOARD_LOW_START) {
      const command = KEYBOARD_LOW_OCTAVE_MAP[note - KEYBOARD_LOW_START];
      // Cursor keys need the extended (0xE0) prefix; every byte is mangled to LSDj's GB-serial form.
      if (isExtendedScancode(command)) c.pushSerialIn(e.frame, toGbSerialByte(0xe0));
      c.pushSerialIn(e.frame, toGbSerialByte(command));
    }
  }
};

// lsdj-sync: dispatch on the configured mode (LsdjSyncMode). Off(0)/Keyboard(4)/ArduinoboyMaster(7) emit
// nothing here — 4 needs the host `keys` feed and 7 needs the emulator serial-out fed into the block,
// both later phases. MidiSync's 0xF8 stream carries no 0xFA; the START-arm that begins LSDj is a user
// action, not part of the clock (only Arduinoboy mode bookends with 0xFA/0xFC).
const lsdjSync: SystemBehavior = (c) => {
  switch (c.config.mode as number) {
    case 1: { // MidiSync
      const divisor = (c.config.tempoDivisor as number) || 1;
      c.eachTick(24 / divisor, (_t, off) => c.pushSerialIn(off, LSDJ_CLOCK));
      break;
    }
    case 2: arduinoboy(c); break; // MidiSyncArduinoboy
    case 3: midiMap(c); break; // MidiMap
    case 5: keyboardMidi(c); break; // KeyboardMidi
    case 6: forwardMidiToSerial(c); break; // MidiPassthrough
    default: break;
  }
};

// lsdj-sync load-time hook: a fresh LSDj cart with no SRAM runs a 12–15 s cartridge self-test on boot.
// When nothing else will seed the battery (no savestate, no sram blob, no on-disk .sav for native to
// load), hand it a valid empty sav — savFromJson stamps the jk/rb validity markers LSDj checks — so it
// boots straight to the song screen. Additive: return the spec untouched when real save data is present.
const lsdjSeedSav = (spec: ConstructSpec, caps: ConstructCaps): ConstructSpec => {
  const willLoadData = !!spec.stateBytes || !!spec.sramBytes || (spec.savPath != null && caps.fileExists(spec.savPath));
  if (willLoadData) return spec;
  return { ...spec, sramBytes: caps.savFromJson("{}") };
};

// midi-routing (project scope): fan the block's GLOBAL midiIn into the per-system inboxes the kernel
// then hands to each system's pipeline. `inboxes` are the kernel's persistent, pre-cleared arrays
// (positional, parallel to block.systems), filled in place with no per-block allocation.
const midiRouting: ProjectBehavior = (block, inboxes, config) => {
  routeBlockInto(block.midiIn, (config.mode as MidiRouting) ?? MidiRouting.SendToAll, inboxes);
};

/** Register the built-in DSP-thread roles into `registry`. */
export function registerDspRoles(registry: RoleRegistry): void {
  registry.registerRole({ kind: "mgb", category: "feature", scope: "system", schema: z.object({}), dsp: mgb });
  // NES host-MIDI: forward routed MIDI to the core's onMidi (→ the always-attached N8 FIFO). Attached to
  // every NES ROM by the rom provider (romProviders.ts), mirroring the native always-on N8 role.
  registry.registerRole({ kind: "nes-n8-midi", category: "feature", scope: "system", schema: z.object({}), dsp: forwardMidiToCore });
  registry.registerRole({
    kind: "lsdj-sync",
    category: "feature",
    scope: "system",
    // mode: LsdjSyncMode (Off=0, MidiSync=1, … ArduinoboyMaster=7). tempoDivisor subdivides the 24-PPQN
    // clock (24/divisor) for MidiSync + MidiSyncArduinoboy; the menu offers 1/2/4/8.
    schema: z.object({ mode: clampedInt(0, 7, 1), tempoDivisor: clampedInt(1, 8, 1) }),
    dsp: lsdjSync,
    onConstruct: lsdjSeedSav,
  });
  registry.registerRole({
    kind: "midi-routing",
    category: "feature",
    scope: "project",
    schema: z.object({ mode: clampedInt(0, 3, 0) }), // MidiRouting 0..3
    dsp: midiRouting,
  });
}
