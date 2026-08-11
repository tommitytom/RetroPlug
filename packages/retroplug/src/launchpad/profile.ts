// Device profiles for Novation Launchpads - the per-model facts every other file here reads.
//
// Protocol reference: the Launchpad Pro [MK3] Programmer's Reference Manual
// (fael-downloads-prod.focusrite.com/.../LPP3_prog_ref_guide_200415.pdf). Only the Pro MK3 ships today,
// but everything model-specific lives in this table so a Mini MK3 (SysEx id 0x0D) or Launchpad X (0x0C)
// is a data entry rather than new code.
//
// One thing to know before touching anything else: the device addresses its 8x8 grid BOTTOM-UP, as
// `row * 10 + col + 11`, so index 11 is the bottom-left pad and 81 the top-left. This module presents a
// TOP-LEFT origin instead (y = 0 is the top row), because song grids read downwards and so does every
// other coordinate system in the codebase. The flip happens in padIndex/padAt and nowhere else.

/** Which USB interface to talk to. The Pro MK3 exposes three, and picking wrong is the classic first
 *  failure: Custom + Programmer traffic lives on MIDI, while DAW carries Session mode only. */
export const PRO_MK3_PORT_HINT = "LPProMK3 MIDI";

/** Universal Device Inquiry, and the reply's manufacturer + family bytes for a Pro MK3 in app mode:
 *  `F0 7E 00 06 02 00 20 29 13 01 00 00 <4-byte app version> F7`. */
export const NOVATION_ID = [0x00, 0x20, 0x29] as const;
export const PRO_MK3_FAMILY = [0x13, 0x01] as const;

export const GRID_SIZE = 8;

/** A pad coordinate, top-left origin: x rightwards 0..7, y DOWNWARDS 0..7. */
export interface Pad {
  x: number;
  y: number;
}

/** Whether a control is addressed by Note or Control Change. In Programmer mode the 8x8 pads are notes
 *  and the surrounding buttons are CCs, and lighting has to use the matching status byte. */
export type ControlKind = "note" | "cc";

export interface LaunchpadProfile {
  readonly name: string;
  /** The byte after `F0 00 20 29 02` in every message, both directions. */
  readonly sysexId: number;
  /** Layout number for Programmer mode, for the select-layout message. */
  readonly programmerLayout: number;
  /** Most colour specs one bulk LED SysEx may carry. */
  readonly maxColourSpecs: number;
  /** Substring identifying the right USB port among the model's several. */
  readonly portHint: string;
  /** Named edge buttons -> their CC number. */
  readonly buttons: Readonly<Record<string, number>>;
}

// The edge buttons, named for the labels printed on the hardware.
//
// VERIFIED on a real Pro MK3 with `retroplug-cli launchpad-probe --sweep`, which lights one name at a time
// so the physical button each addresses can simply be read off. That was worth doing: the widely-used
// community mapping this file previously carried was WRONG across the whole top row, shifted by two,
// because it assumed the up/down arrows live there. They do not - they are the top of the LEFT column, and
// the top row's last two buttons (Sequencer, Projects) were missing from the map entirely.
//
// One name per CC, deliberately: Surface enumerates `Object.values(...)` and buttonName reverse-looks-up,
// so an alias would produce a duplicate index and an ambiguous name.
const PRO_MK3_BUTTONS: Record<string, number> = {
  // Top row, left to right: the two page arrows then the mode buttons.
  left: 91, right: 92, session: 93, note: 94,
  chord: 95, custom: 96, sequencer: 97, projects: 98,
  // The logo LED, top right - lightable but not pressable.
  logo: 99,
  // Left column, top to bottom. `up`/`down` are the ▲/▼ arrows, which is where a song list is scrolled from.
  up: 80, down: 70, clear: 60, duplicate: 50,
  quantise: 40, fixedLength: 30, play: 20, record: 10,
  // Right column, top to bottom (the ">" buttons).
  patterns: 89, steps: 79, patternSettings: 69, velocity: 59,
  probability: 49, mutation: 39, microStep: 29, printToClip: 19,
  // Bottom row, left to right (the track-function row).
  recordArm: 101, mute: 102, solo: 103, volume: 104,
  pan: 105, sends: 106, device: 107, stopClip: 108,
};

// NOT here, and not an oversight: `Shift` (top-left corner) and `Setup` (bottom-left corner). Neither is
// part of the addressable grid+edge surface, and whether they emit anything in Programmer mode is untested -
// so they are absent rather than guessed at.

export const PRO_MK3: LaunchpadProfile = {
  name: "Launchpad Pro [MK3]",
  sysexId: 0x0e, // Mini MK3 is 0x0D, Launchpad X 0x0C
  programmerLayout: 0x11,
  maxColourSpecs: 106, // "up to 106 <Colour Spec> entries to light up the entire surface"
  portHint: PRO_MK3_PORT_HINT,
  buttons: PRO_MK3_BUTTONS,
};

/** Every profile this build knows, by a short key. */
export const PROFILES: Readonly<Record<string, LaunchpadProfile>> = { "pro-mk3": PRO_MK3 };

/** True for a pad coordinate inside the 8x8 grid. */
export function isPad(pad: Pad): boolean {
  return Number.isInteger(pad.x) && Number.isInteger(pad.y)
    && pad.x >= 0 && pad.x < GRID_SIZE && pad.y >= 0 && pad.y < GRID_SIZE;
}

/** Top-left pad coordinate -> device LED/note index. `y` is flipped here, and only here: the device
 *  counts rows upwards from the bottom, so our y=0 (top) is its row 7. Verified against the manual's
 *  three stated anchors - (0,7) = 11, (0,0) = 81, (7,7) = 18. Returns -1 for an off-grid coordinate
 *  rather than a nonsense index that would light some unrelated button. */
export function padIndex(pad: Pad): number {
  if (!isPad(pad)) return -1;
  return (GRID_SIZE - 1 - pad.y) * 10 + pad.x + 11;
}

/** Device index -> top-left pad coordinate, or null when the index is not one of the 64 grid pads
 *  (an edge button, or out of range entirely). The inverse of padIndex. */
export function padAt(index: number): Pad | null {
  if (!Number.isInteger(index) || index < 11 || index > 88) return null;
  const row = Math.floor(index / 10);
  const col = index % 10;
  if (row < 1 || row > 8 || col < 1 || col > 8) return null; // the 0 and 9 columns are edge buttons
  return { x: col - 1, y: GRID_SIZE - row };
}

/** The button name for a CC number, or null when it is not a known edge button. */
export function buttonName(profile: LaunchpadProfile, cc: number): string | null {
  for (const [name, value] of Object.entries(profile.buttons)) if (value === cc) return name;
  return null;
}

/** How a control index must be addressed. The 8x8 pads take Note messages; everything else on the
 *  surface is a CC, and lighting one with a Note On would simply be ignored. */
export function controlKind(index: number): ControlKind {
  return padAt(index) !== null ? "note" : "cc";
}
