// The single source of truth for every string-valued enum setting in a project. Each enum is an
// index-ordered value tuple whose position EQUALS the native enum integer — the tuple feeds
// `z.enum(...)` (configSchema.enumField) and indexes the menu cyclers' display-name arrays. TS carries the
// strings everywhere: a system node's settings name a label, which its C++ type reads (graph/buildGraph.ts
// systemKnobs), and a node reads a NES export mode's position in its tuple where the graph needs the mode as
// a number (exportModeOf).
//
// It imports nothing at run time but core's MIDI routing enum, re-exported below, so the schema, the domain modules
// (layout/midiRouting/dspRoles), the stores, the menu and the project migration share these values without a cycle.

// --- project-level settings ------------------------------------------------

// Grid layout. `SystemLayout.Row`/`.Column` are switched on in the grid math (layout.ts); the value
// pair keeps those member references working now that the value is a string, not an ordinal.
export const LAYOUT_VALUES = ["auto", "row", "column", "grid"] as const;
export type SystemLayout = (typeof LAYOUT_VALUES)[number];
export const SystemLayout = {
  Auto: "auto",
  Row: "row",
  Column: "column",
  Grid: "grid",
} as const satisfies Record<string, SystemLayout>;

// MIDI routing mode - the routing oracle switches on these members. The values live with the oracle, in core's
// midiRouting (a package that owns a node owning the mode cannot import this shell module), and are re-exported here
// so the schema, the stores and the menu keep reading one source.
export { MIDI_ROUTING_VALUES, MidiRouting } from "@retroplug/core/midiRouting";

/** Where a controller app's row launches go: into an emulated cart's MIDI inbox (which the tracker's own
 *  sync role then puts on the link port), or out of the host to real hardware (an Arduinoboy driving a
 *  real Game Boy). See docs/launchpad-plan.md 7.3. */
export const CONTROLLER_TARGET_VALUES = ["system", "midiOut"] as const;
export type ControllerTarget = (typeof CONTROLLER_TARGET_VALUES)[number];
// Audio output-pair placement. Never crosses to native as a value: graph/buildGraph.ts turns it into the
// graph's output wiring. The three split modes are single-system-only (the menu gates them via
// `validAudioRoutings`; buildGraph re-checks the system count and is the authority):
//   channelSplit    one system's channels — a Game Boy's 4 STEREO channels across the 8 outs, or a NES's
//                   5 MONO core channels across outs 1..5.
//   pinSplit        a NES's three 2A03 output pins (Pulse | TND | Expansion) across outs 1..3, mono.
//   stereoPinSplit  the same three pins, one PAIR each (outs 1/2, 3/4, 5/6), mirrored L=R — so every pin
//                   is a normal stereo track in a DAW that can only bus/FX whole pairs.
// Both pin modes are NES-only; buildGraph falls back to Stereo for any other console.
export const AUDIO_ROUTING_VALUES = ["stereo", "twoPerInstance", "onePerInstance", "channelSplit", "pinSplit", "stereoPinSplit"] as const;
export type AudioRouting = (typeof AUDIO_ROUTING_VALUES)[number];

// --- per-system role enums -------------------------------------------------

// SameBoy model (SameBoyModel 0..13; index 9 = CgbC, the default). Index == the native model int.
export const MODEL_VALUES = [
  "auto", "dmgB", "mgb", "sgb", "sgbPal", "sgb2", "cgb0", "cgbA", "cgbB", "cgbC", "cgbD", "cgbE", "agb", "gbp",
] as const;
export type SameBoyModel = (typeof MODEL_VALUES)[number];

// SameBoy high-pass filter (SameBoyHighpass 0..2).
export const HIGHPASS_VALUES = ["off", "accurate", "removeDcOffset"] as const;
export type SameBoyHighpass = (typeof HIGHPASS_VALUES)[number];

// SameBoy CGB colour correction — index == GB_color_correction_mode_t (Core/display.h). Acts on the
// 15-bit -> RGB conversion, so it only bites on a CGB-family model; a DMG/MGB core ignores it.
// `disabled` is the historical RetroPlug behaviour (raw, oversaturated) and stays the default so no
// existing project changes appearance.
export const COLOR_CORRECTION_VALUES = [
  "disabled",
  "correctCurves",
  "modernBalanced",
  "modernBoostContrast",
  "reduceContrast",
  "lowContrast",
  "modernAccurate",
] as const;
export type SameBoyColorCorrection = (typeof COLOR_CORRECTION_VALUES)[number];

// SameBoy DMG palette — the four built-ins SameBoy exports (Core/display.h). The mirror image of
// colour correction: it only bites in DMG rendering, and a CGB-family core ignores it. `grey` is
// SameBoy's own fallback when no palette is set (GB_update_dmg_palette), i.e. today's behaviour.
export const DMG_PALETTE_VALUES = ["grey", "dmg", "mgb", "gbl"] as const;
export type SameBoyDmgPalette = (typeof DMG_PALETTE_VALUES)[number];

// NES console region (ConsoleRegion 0..4).
export const REGION_VALUES = ["auto", "ntsc", "pal", "dendy", "ntscJapan"] as const;
export type ConsoleRegion = (typeof REGION_VALUES)[number];

// NES per-channel export mode (spec/10 §5/§5b): mix=0, stereoModPins=1, pinsPlusRef=2, individualMono=3.
// Cartridge-accuracy switches. "chip" is the documented hardware behaviour and the default; "n8" matches
// an Everdrive N8 Pro's FPGA cores, which measurably differ, so software developed against that cartridge
// (BlipToaster) sounds the same in the emulator as on the console. Both are per-system and live.
//   s5bNoise "n8"       - the 5B has no noise generator there, so tone-AND-noise mutes the channel
//   mmc5PhaseReset "n8" - a $5003/$5007 write does not restart the pulse duty sequencer there
export const CARTRIDGE_ACCURACY_VALUES = ["chip", "n8"] as const;
export type CartridgeAccuracy = (typeof CARTRIDGE_ACCURACY_VALUES)[number];

export const CHANNEL_EXPORT_VALUES = ["mix", "stereoModPins", "pinsPlusRef", "individualMono"] as const;
export type ChannelExportMode = (typeof CHANNEL_EXPORT_VALUES)[number];

// How faithfully the emulated EverDrive N8 cart FIFO behaves like the real one.
//   "instant"  - unbounded and delivered immediately. The DEFAULT, and what every existing test was
//                written against: nothing can ever be lost and a pushed chunk is readable at once.
//   "hardware" - 2048 bytes deep, dropping when full, delivered at the wire's ~150 KB/s. Both of
//                those hide bug classes when they are absent: a ROM that cannot lose a byte never has
//                its recovery path exercised, and one handed a whole chunk atomically is never
//                starved mid-structure the way a real wire starves it.
// Construct-time (the FIFO is built when the core activates): setRoleConfig rebuilds the system for an edit.
export const FIFO_PROFILE_VALUES = ["instant", "hardware"] as const;
export type FifoProfile = (typeof FIFO_PROFILE_VALUES)[number];

// BlipToaster's reliable link (src/transport/N8ReliableLink.hpp): MIDI to a ROM that advertises it (SIG capability
// bit 0) goes out framed and is re-sent when an NTSC DMC-DMA halt loses a byte, exactly as on a physical N8.
//   "auto" - on whenever the ROM advertises it. The DEFAULT: the emulator should lose and recover bytes the way
//            the cartridge does, not be quietly better than it.
//   "off"  - raw MIDI, unframed - for tests that drive the protocol (or a malformed stream) by hand.
// Construct-time (the FIFO role is built when the core activates): setRoleConfig rebuilds the system for an edit.
export const N8_LINK_VALUES = ["auto", "off"] as const;
export type N8LinkMode = (typeof N8_LINK_VALUES)[number];

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
