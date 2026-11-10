// The app's own string-valued settings enums: the ones no cart or console package owns. Each is an index-ordered value
// tuple whose position EQUALS the native enum integer - the tuple feeds `z.enum(...)` (core's configFields' enumField)
// and indexes the menu cyclers' display-name arrays. TS carries the strings everywhere: a system node's settings name a
// label, which its C++ type reads (graph/buildGraph.ts systemKnobs), and a node reads a mode's position in its tuple
// where the graph needs the mode as a number (exportModeOf).
//
// A console's or a cart's enum lives with its owner, and this file re-exports none of them: the SameBoy ones are
// @retroplug/sameboy's, the Mesen ones @retroplug/mesen's, the LSDj ones @retroplug/lsdj's, MIDI routing's are
// @retroplug/core's, and a reader names the package for each. What is left here is the app's own: layout and audio
// routing. The controller's launch target (CONTROLLER_TARGET_VALUES) is the launchpad pack's, and so is the project
// section that stores one.
//
// So the file imports nothing and is imported by the schema, the domain modules (layout), the stores, the menu and the
// project migration.

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

// Audio output-pair placement. Never crosses to native as a value: graph/buildGraph.ts turns it into the
// graph's output wiring. The three split modes are single-system-only (the menu gates them via
// `validAudioRoutings`; buildGraph re-checks the system count and is the authority):
//   channelSplit    one system's channels — a Game Boy's 4 STEREO channels across the 8 outs, or a NES's
//                   5 MONO core channels across outs 1..5.
//   pinSplit        a NES's three 2A03 output pins (Pulse | TND | Expansion) across outs 1..3, mono.
//   stereoPinSplit  the same three pins, one PAIR each (outs 1/2, 3/4, 5/6), mirrored L=R - so every pin
//                   is a normal stereo track in a DAW that can only bus/FX whole pairs.
// Both pin modes are NES-only; buildGraph falls back to Stereo for any other console.
export const AUDIO_ROUTING_VALUES = [
  "stereo",
  "twoPerInstance",
  "onePerInstance",
  "channelSplit",
  "pinSplit",
  "stereoPinSplit",
] as const;
export type AudioRouting = (typeof AUDIO_ROUTING_VALUES)[number];

