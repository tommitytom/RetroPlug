// Per-user, machine-global preferences — config.json. The four fields of native's
// UserConfigJson (config/UserConfigSerialization.hpp): which keyboard/gamepad binding
// profiles are active, the default zoom for fresh projects, and the loose-.sav auto-save
// preference. This is the model + its zod validator; parse/serialize live in
// userConfigSerialization.ts and the Backend-backed store in userConfigStore.ts
// (mirroring the recentList / recentSerialization / recentStore triad).
//
// The bindings PROFILE files (bindings/<name>.json) are a separate concern — this owns
// only config.json, holding the active-profile names as plain strings.

import { z, clampedInt, stringField, enumField } from "./configSchema";
import type { SplitMode } from "./render";

/** The persisted render-menu selections (System > Render): what to split into, the output sample rate, and
 *  the max render length. Global (not per-system) so the single "Render..." action reads the current picks. */
export const RENDER_SPLITS = ["mix", "channels", "pins"] as const;
export const RENDER_SAMPLE_RATES = [44100, 48000, 96000] as const;
export const RENDER_MAX_DURATION_MIN_SEC = 5;
export const RENDER_MAX_DURATION_MAX_SEC = 1800; // 30 min cap

export interface RenderSettings {
  split: SplitMode; // clamped to the system's platform when a render actually starts
  sampleRate: number; // one of RENDER_SAMPLE_RATES
  maxDurationSec: number; // bounds every render (LSDj auto-length cap + the fixed-render length)
  lastDir: string; // last-used render output directory (seeds the Render save dialog); "" until first render
}

// A missing/garbage `render` block becomes {} so the child fields fill their own defaults (the preprocess
// idiom clampedInt uses, applied one level up).
const renderSchema = z.preprocess(
  (v) => (v && typeof v === "object" && !Array.isArray(v) ? v : {}),
  z.object({
    split: enumField(RENDER_SPLITS, "mix"),
    sampleRate: z.preprocess((v) => (RENDER_SAMPLE_RATES.includes(v as never) ? v : 44100), z.number()),
    maxDurationSec: clampedInt(RENDER_MAX_DURATION_MIN_SEC, RENDER_MAX_DURATION_MAX_SEC, 300),
    lastDir: stringField(""),
  }),
);

/** When the loose sibling <rom>.sav is auto-saved. The string values match native's
 *  rp::SramMirror enum spellings — Off = never write it; OnProjectSave = flush on
 *  save/quit (the default); Continuous = also throttled idle writes — but the TS side
 *  names the field `sramAutoSave`: "mirror" reads from the plugin's side, "auto save"
 *  fits both the plugin and the standalone. This store only holds the preference; the
 *  auto-save POLICY that consumes it is a later increment. */
export const SRAM_AUTO_SAVES = ["Off", "OnProjectSave", "Continuous"] as const;
export type SramAutoSave = (typeof SRAM_AUTO_SAVES)[number];

/** config.json — the per-user preferences. */
export interface UserConfig {
  activeKeyboardBindings: string; // names a bindings/<name>.json profile
  activeGamepadBindings: string; // names a bindings/<name>.json profile
  defaultZoom: number; // 1..6; a fresh project with zoom == 0 inherits this
  sramAutoSave: SramAutoSave;
  render: RenderSettings; // System > Render menu selections
}

/** Validates + defaults + clamps a (possibly partial/stale) config.json object. Strict:
 *  unknown keys are stripped; a missing/bad field takes its default; an out-of-range
 *  defaultZoom clamps to 1..6; an unknown sramAutoSave falls back to the default. */
export const userConfigSchema = z.object({
  activeKeyboardBindings: stringField("default"),
  activeGamepadBindings: stringField("default"),
  defaultZoom: clampedInt(1, 6, 3),
  sramAutoSave: z.enum(SRAM_AUTO_SAVES).catch("OnProjectSave").default("OnProjectSave"),
  render: renderSchema,
});

export const DEFAULT_USER_CONFIG: UserConfig = userConfigSchema.parse({}) as UserConfig;
