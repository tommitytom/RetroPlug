// The single source of truth for every string-valued enum setting in a project. Each enum is an
// index-ordered value tuple whose position EQUALS the native enum integer — the tuple feeds
// `z.enum(...)` (configSchema.enumField), indexes the menu cyclers' display-name arrays, and drives
// the string<->int conversion at the native/kernel boundary. TS carries the strings everywhere;
// native (SetAudioRouting, the reflect-cpp role structs) stays numeric, so the boundary encoders here
// (`roleConfigForNative`, `audioRoutingToIndex`) map back to the ints native decodes.
//
// A LEAF module: it imports nothing, so the schema, the domain modules (layout/midiRouting/dspRoles),
// the stores, the menu, and the project migration can all share these values without an import cycle.

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

// MIDI routing mode — the routing oracle (midiRouting.ts) switches on these members.
export const MIDI_ROUTING_VALUES = [
  "sendToAll",
  "fourChannelsPerInstance",
  "oneChannelPerInstance",
  "midiChannelToInstance",
] as const;
export type MidiRouting = (typeof MIDI_ROUTING_VALUES)[number];

/** Where a controller app's row launches go: into an emulated cart's MIDI inbox (which the tracker's own
 *  sync role then puts on the link port), or out of the host to real hardware (an Arduinoboy driving a
 *  real Game Boy). See docs/launchpad-plan.md 7.3. */
export const CONTROLLER_TARGET_VALUES = ["system", "midiOut"] as const;
export type ControllerTarget = (typeof CONTROLLER_TARGET_VALUES)[number];
export const MidiRouting = {
  SendToAll: "sendToAll",
  FourChannelsPerInstance: "fourChannelsPerInstance",
  OneChannelPerInstance: "oneChannelPerInstance",
  MidiChannelToInstance: "midiChannelToInstance",
} as const satisfies Record<string, MidiRouting>;

// Audio output-pair placement. Crosses to native's AudioRouting enum by index via `audioRoutingToIndex`
// (Stereo=0 … ChannelSplit=3). `channelSplit` fans one Game Boy's 4 channels across the 8 outs and is
// single-system-only (the menu gates it; native re-checks systemCount()==1).
export const AUDIO_ROUTING_VALUES = ["stereo", "twoPerInstance", "onePerInstance", "channelSplit"] as const;
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
// (EverMIDI) sounds the same in the emulator as on the console. Both are per-system and live.
//   s5bNoise "n8"       - the 5B has no noise generator there, so tone-AND-noise mutes the channel
//   mmc5PhaseReset "n8" - a $5003/$5007 write does not restart the pulse duty sequencer there
export const CARTRIDGE_ACCURACY_VALUES = ["chip", "n8"] as const;
export type CartridgeAccuracy = (typeof CARTRIDGE_ACCURACY_VALUES)[number];

export const CHANNEL_EXPORT_VALUES = ["mix", "stereoModPins", "pinsPlusRef", "individualMono"] as const;
export type ChannelExportMode = (typeof CHANNEL_EXPORT_VALUES)[number];

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

// --- native boundary encoders ----------------------------------------------

/** The native AudioRouting integer for a routing value (index == enum ordinal). */
export function audioRoutingToIndex(v: AudioRouting): number {
  return AUDIO_ROUTING_VALUES.indexOf(v);
}

// Which fields of a native-facing role config are string enums, and their value tuples. Only the two
// "system"-category roles cross to native's reflect-cpp structs (SameBoyRoleConfig / MesenNesRoleConfig);
// lsdj-sync is a pure-TS DSP role, so its `mode` string never needs an int here.
const ROLE_NATIVE_ENUMS: Record<string, Record<string, readonly string[]>> = {
  sameboy: {
    model: MODEL_VALUES,
    highpass: HIGHPASS_VALUES,
    colorCorrection: COLOR_CORRECTION_VALUES,
    dmgPalette: DMG_PALETTE_VALUES,
  },
  mesen: {
    region: REGION_VALUES,
    channelExportMode: CHANNEL_EXPORT_VALUES,
    s5bNoise: CARTRIDGE_ACCURACY_VALUES,
    mmc5PhaseReset: CARTRIDGE_ACCURACY_VALUES,
  },
};

/** Encode a role config for native: replace each string enum field with its native integer (index),
 *  passing every other field (linkGroupId, fastBoot, removeSpriteLimit, …) through unchanged. A role
 *  kind that doesn't cross to native returns its config untouched. Native stays numeric; this is the
 *  one place TS strings become the ints reflect-cpp expects. */
export function roleConfigForNative(kind: string, config: Record<string, unknown>): Record<string, unknown> {
  const fields = ROLE_NATIVE_ENUMS[kind];
  if (!fields) return config;
  const out: Record<string, unknown> = { ...config };
  for (const key of Object.keys(fields)) {
    const v = out[key];
    if (typeof v === "string") {
      const i = fields[key].indexOf(v);
      if (i >= 0) out[key] = i;
    }
  }
  return out;
}
