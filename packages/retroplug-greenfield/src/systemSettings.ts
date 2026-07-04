// The UNIVERSAL per-system settings — the ones every emulator backend has, so they
// live on the system itself rather than in a backend role. Backend-specific knobs
// (model/highpass/…) are system roles (coreRoles.ts); LSDj etc. are feature roles.

/** Per-system settings common to all backends. */
export interface CoreSettings {
  gainDb: number; // -90..+12, 0 = unity (mirrors the master-gain param range)
  reloadOnRomChange: boolean;
}

export const DEFAULT_CORE_SETTINGS: CoreSettings = { gainDb: 0, reloadOnRomChange: false };

export const GAIN_MIN = -90;
export const GAIN_MAX = 12;

/** Clamp a gain value to the supported range. */
export function clampGain(db: number): number {
  if (!Number.isFinite(db)) return 0;
  return Math.max(GAIN_MIN, Math.min(GAIN_MAX, db));
}
