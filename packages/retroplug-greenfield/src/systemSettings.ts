// The UNIVERSAL per-system settings — the ones every core has, so they live on the system itself
// rather than in a core-config role. Core-specific knobs (SameBoy model/highpass/…) are system roles
// (coreRoles.ts); LSDj etc. are feature roles. ("Common" here = common to all cores; not to be confused
// with the `Core` emulator axis in platform.ts.)
//
// The shape + defaults + clamping are a zod schema (the load-time validator); the
// interface is kept as the public type. `z.looseObject` preserves any unknown
// forward-compat fields.

import { z, clampedNumber, boolField } from "./configSchema";

export const GAIN_MIN = -90;
export const GAIN_MAX = 12;

/** Per-system settings common to all cores. */
export interface CommonSettings {
  gainDb: number; // -90..+12, 0 = unity (mirrors the master-gain param range)
  reloadOnRomChange: boolean;
}

/** Validates + defaults + clamps a (possibly partial/stale) settings object. Strict:
 *  unknown keys are stripped (a newer writer's fields are refused by version detection,
 *  additive fields are filled by defaults). */
export const commonSettingsSchema = z.object({
  gainDb: clampedNumber(GAIN_MIN, GAIN_MAX, 0),
  reloadOnRomChange: boolField(false),
});

export const DEFAULT_COMMON_SETTINGS: CommonSettings = commonSettingsSchema.parse({}) as CommonSettings;

/** Clamp a gain value to the supported range (the set-time clamp for `setGain`). */
export function clampGain(db: number): number {
  if (!Number.isFinite(db)) return 0;
  return Math.max(GAIN_MIN, Math.min(GAIN_MAX, db));
}
