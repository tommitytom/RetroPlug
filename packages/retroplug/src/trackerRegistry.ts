// The product's tracker integrations (docs/modular-graph-phase-f1-design.md section 3): which consoles this build
// searches songs and assets on, and the resolvers a system's roles are read through. Each console's integration is its
// package's app contribution, so the list is src/generated/packages.ts and this module is the shell's - beside
// src/romProviders.ts, for the same reason: a package exports to the registry, never the other way. What an
// integration IS is tracker's contract (./tracker/trackerIntegration.ts), which reads nothing here.
import type { RoleInstance } from "./systemRoles";
import type { AssetCatalog } from "./tracker/assetCatalog";
import type { SongCatalog } from "./tracker/songCatalog";
import type { TrackerIntegration } from "./tracker/trackerIntegration";
import { TRACKER_INTEGRATIONS } from "./generated/packages";

// The integrations themselves are the app contributions (./tracker/integrations), re-exported here because a leaf
// imports this module for an integration (render.ts) and the tests reach one by its name on this path.
export { lsdjIntegration, risaIntegration, smsggdjIntegration, blipToasterIntegration } from "./tracker/integrations";

/** Every registered tracker integration, in the product's package order: a system takes the first whose markerRole
 *  it has. The generated list, re-exported below as a live binding. */
export { TRACKER_INTEGRATIONS };

/** The tracker integration for a system, resolved from its attached roles (the first role whose kind is an
 *  integration's markerRole). undefined for a non-tracker system. */
export function resolveTracker(roles: RoleInstance[]): TrackerIntegration | undefined {
  return TRACKER_INTEGRATIONS.find((t) => roles.some((r) => r.kind === t.markerRole));
}

/** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
export function resolveSongCatalog(roles: RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}
