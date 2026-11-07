// LSDj's string-valued settings enums: the sync mode its role's schema takes and its two protocol nodes switch on. The
// tuple's position EQUALS the native enum integer - it feeds `z.enum(...)` (core's configFields' enumField) and indexes
// the menu cyclers' display-name arrays - and TS carries the string label everywhere, which the C++ type reads.

// LSDj sync mode (LsdjSyncMode 0..8). The lsdj-sync DSP role switches on these members, and the
// serial-out capture gate / host-sync latency compare against MidiOut/MasterSync/MidiSync/…
export const LSDJ_MODE_VALUES = [
  "off",
  "midiSync",
  "midiSyncArduinoboy",
  "midiMap",
  "keyboard",
  "keyboardMidi",
  "midiPassthrough",
  "midiOut",
  "masterSync",
] as const;
export type LsdjSyncMode = (typeof LSDJ_MODE_VALUES)[number];
export const LsdjSyncMode = {
  Off: "off",
  MidiSync: "midiSync",
  MidiSyncArduinoboy: "midiSyncArduinoboy",
  MidiMap: "midiMap",
  Keyboard: "keyboard",
  KeyboardMidi: "keyboardMidi",
  MidiPassthrough: "midiPassthrough",
  MidiOut: "midiOut",
  MasterSync: "masterSync",
} as const satisfies Record<string, LsdjSyncMode>;
