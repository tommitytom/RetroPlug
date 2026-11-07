// Version → WRAM offset layout resolution. A thin seam over the offsets table so callers (reader,
// overlay, detector cross-check) depend on `resolveLayout`, not the table's internal band structure.
import type { LsdjVersion, OffsetLayout } from "./types";
import { layoutForVersion } from "./offsets";

/** Resolve the offset layout for a version, or null when unsupported (unknown/too-old ROM). */
export function resolveLayout(version: LsdjVersion | null): OffsetLayout | null {
  return version ? layoutForVersion(version) : null;
}
