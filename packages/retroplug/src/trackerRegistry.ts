// The product's tracker integrations (docs/modular-graph-phase-f1-design.md section 3): which consoles this build
// searches songs and assets on, and the resolvers a system's roles are read through. Each console's integration is its
// package's app contribution, so the list is src/generated/packages.ts and this module is the shell's - beside
// src/romProviders.ts, for the same reason: a package exports to the registry, never the other way. What an
// integration IS is tracker's contract (./tracker/trackerIntegration.ts), which reads nothing here.
import type { RoleInstance } from "@retroplug/core/systemRoles";
import type { AssetCatalog } from "@retroplug/tracker/assetCatalog";
import type { SongCatalog } from "@retroplug/tracker/songCatalog";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { TRACKER_INTEGRATIONS } from "./generated/packages";

/** Every tracker integration, in the product order: a system takes the one whose markerRole it has, and the markers are
 *  exclusive, so the order decides nothing. The generated list, re-exported below as a live binding. */
export { TRACKER_INTEGRATIONS };

/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
 *  integration's markerRole). undefined for a non-tracker system. */
export function resolveTracker(roles: readonly RoleInstance[]): TrackerIntegration | undefined {
  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
}

/** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
export function resolveSongCatalog(roles: readonly RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: readonly RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}
