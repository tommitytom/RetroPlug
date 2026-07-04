// Schema-version stamp/validate — the TS port of config/SchemaVersions.hpp.
// Detection, not migration: a file stamped newer than the running build is
// refused; older would (eventually) grow a migration at the load seam.

// Bump ONLY on a breaking (non-additive) project-format change; additive
// changes are covered by forward-tolerant reads. Mirrors rp::schema::kProject.
export const K_PROJECT = 1;

export enum VersionCheck {
  Ok = "ok",
  Older = "older",
  Newer = "newer",
}

export function checkVersion(fileVersion: number, current: number): VersionCheck {
  if (fileVersion === current) return VersionCheck.Ok;
  return fileVersion < current ? VersionCheck.Older : VersionCheck.Newer;
}

// ProjectConfig.schemaVersion is a legacy *string* ("1.0", "2", ...). Take the
// leading integer; floor to K_PROJECT when there are no leading digits (an old
// file predating the stamp reads as the baseline).
export function parseProjectVersion(s: string): number {
  const m = /^\s*(\d+)/.exec(s);
  return m ? parseInt(m[1], 10) : K_PROJECT;
}
