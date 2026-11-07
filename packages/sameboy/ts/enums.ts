// The SameBoy core's string-valued settings enums. Each is an index-ordered value tuple whose position EQUALS the
// native enum integer the core's own knob reads: the tuple feeds `z.enum(...)` (core's configFields' enumField) and
// indexes the menu cycler's display-name array, and the system node's settings name a label, which
// graph/buildGraph.ts's systemKnobs hands to the C++ type as that label. The tuples are the app's and the native
// enum's shared definition, so their order and their members are load-bearing: reordering one changes what a stored
// project means.
//
// They live here because SameBoy's role schema reads them and this package owns that role; the shell reads them from
// here for its project schema and its menu cyclers.

// SameBoy model (SameBoyModel 0..13; index 9 = CgbC, the default). Index == the native model int.
export const MODEL_VALUES = [
  "auto", "dmgB", "mgb", "sgb", "sgbPal", "sgb2", "cgb0", "cgbA", "cgbB", "cgbC", "cgbD", "cgbE", "agb", "gbp",
] as const;
export type SameBoyModel = (typeof MODEL_VALUES)[number];

// SameBoy high-pass filter (SameBoyHighpass 0..2).
export const HIGHPASS_VALUES = ["off", "accurate", "removeDcOffset"] as const;
export type SameBoyHighpass = (typeof HIGHPASS_VALUES)[number];

// SameBoy CGB colour correction - index == GB_color_correction_mode_t (Core/display.h). Acts on the
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

// SameBoy DMG palette - the four built-ins SameBoy exports (Core/display.h). The mirror image of
// colour correction: it only bites in DMG rendering, and a CGB-family core ignores it. `grey` is
// SameBoy's own fallback when no palette is set (GB_update_dmg_palette), i.e. today's behaviour.
export const DMG_PALETTE_VALUES = ["grey", "dmg", "mgb", "gbl"] as const;
export type SameBoyDmgPalette = (typeof DMG_PALETTE_VALUES)[number];
