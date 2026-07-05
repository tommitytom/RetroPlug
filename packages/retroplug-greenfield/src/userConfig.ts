// Per-user, machine-global preferences — config.json. The four fields of native's
// UserConfigJson (config/UserConfigSerialization.hpp): which keyboard/gamepad binding
// profiles are active, the default zoom for fresh projects, and the loose-.sav mirror
// preference. This is the model + its zod validator; parse/serialize live in
// userConfigSerialization.ts and the Backend-backed store in userConfigStore.ts
// (mirroring the recentList / recentSerialization / recentStore triad).
//
// The bindings PROFILE files (bindings/<name>.json) are a separate concern — this owns
// only config.json, holding the active-profile names as plain strings.

import { z, clampedInt, stringField } from "./configSchema";

/** How the loose sibling <rom>.sav mirror is kept in sync (native rp::SramMirror). The
 *  string spellings match native's reflect-cpp enum, so config.json is cross-compatible:
 *  Off = never write it; OnProjectSave = flush on save/quit (default); Continuous = also
 *  throttled idle writes. This store only holds the preference; the mirror POLICY that
 *  consumes it is a later increment. */
export const SRAM_MIRRORS = ["Off", "OnProjectSave", "Continuous"] as const;
export type SramMirror = (typeof SRAM_MIRRORS)[number];

/** config.json — the per-user preferences. */
export interface UserConfig {
  activeKeyboardBindings: string; // names a bindings/<name>.json profile
  activeGamepadBindings: string; // names a bindings/<name>.json profile
  defaultZoom: number; // 1..6; a fresh project with zoom == 0 inherits this
  sramMirror: SramMirror;
}

/** Validates + defaults + clamps a (possibly partial/stale) config.json object. Strict:
 *  unknown keys are stripped; a missing/bad field takes its default; an out-of-range
 *  defaultZoom clamps to 1..6; an unknown sramMirror falls back to the default. */
export const userConfigSchema = z.object({
  activeKeyboardBindings: stringField("default"),
  activeGamepadBindings: stringField("default"),
  defaultZoom: clampedInt(1, 6, 3),
  sramMirror: z.enum(SRAM_MIRRORS).catch("OnProjectSave").default("OnProjectSave"),
});

export const DEFAULT_USER_CONFIG: UserConfig = userConfigSchema.parse({}) as UserConfig;
