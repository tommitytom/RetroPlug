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
import { placedKinds } from "./graph/protocolNodes";

/** Every tracker integration, in the product order. A system takes the first whose markerRole its graph places, in
 *  the order its OWN roles name them (resolveTracker): of two markers a stored list carries, the load moves the one
 *  today's ROM earns ahead of the stale one (SystemsStore.suggestedFirst), so this order decides only when the ROM
 *  earns neither. A marker has no protocol node, so it is placed whenever it is stored. The generated list,
 *  re-exported below as a live binding. */
export { TRACKER_INTEGRATIONS };

/** The role kinds a cart's tracker is marked by: one per integration, refused twice over by the generated list. */
export const TRACKER_MARKERS: ReadonlySet<string> = new Set(TRACKER_INTEGRATIONS.map((t) => t.markerRole));

/** The tracker integration for a system, resolved from its roles: the first of its PLACED markers in role order, as
 *  graph/protocolNodes.ts placedKinds lists them. undefined for a non-tracker system, and for one whose stored marker
 *  the graph leaves out: an mGB cart still carrying lsdj-sync after a ROM swap is not an LSDj cart. Role order rather
 *  than product order, because a stored system can hold two markers (a NES project stored with risa whose path now
 *  holds BlipToaster) and only the load knows which one the file earns. */
export function resolveTracker(roles: readonly RoleInstance[]): TrackerIntegration | undefined {
  const placed = placedKinds(roles);
  for (const kind of placed) {
    const integration = TRACKER_INTEGRATIONS.find((t) => t.markerRole === kind);
    if (integration) return integration;
  }
  return undefined;
}

/** The song catalog for a system (convenience over resolveTracker) - used by the recent list. */
export function resolveSongCatalog(roles: readonly RoleInstance[]): SongCatalog | undefined {
  return resolveTracker(roles)?.songs;
}

/** The asset catalog for a system (convenience over resolveTracker). */
export function resolveAssetCatalog(roles: readonly RoleInstance[]): AssetCatalog | undefined {
  return resolveTracker(roles)?.assets;
}
