// The plugin's host latency (docs/modular-graph-phase-f4a-design.md section 4.5): how far behind the host clock a cart's
// playback sits, so the DAW can apply PDC and land the song on the grid. HOW MUCH is each role's own knowledge
// (core's RoleType.hostLatencyMs), so this asks the registry and names no cart. Only a role the system's protocol
// pipeline PLACES is asked: a role whose node the graph leaves out runs nothing and shifts nothing.
import type { RoleInstance, RoleRegistry } from "@retroplug/core/systemRoles";
import { protocolPipeline } from "./graph/protocolNodes";

/** A system as this reads it: the roles its pipeline places. */
interface LatencySystem {
  readonly roles: readonly RoleInstance[];
}

/** The latency the plugin reports, in ms: the maximum over every system's placed roles, 0 when nothing asks for any. An
 *  answer that is not a finite positive number is ignored: a hook is a package's, and a NaN or an infinity would reach
 *  the host as its PDC. */
export function syncLatencyMs(systems: readonly LatencySystem[], registry: RoleRegistry): number {
  let ms = 0;
  for (const system of systems) {
    for (const placed of protocolPipeline(system.roles)) {
      const latency = registry.roleType(placed.kind)?.hostLatencyMs?.(placed.config);
      if (latency !== undefined && Number.isFinite(latency) && latency > 0) ms = Math.max(ms, latency);
    }
  }
  return ms;
}
