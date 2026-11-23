// The graph placers, as a live registry (docs/downloadable-packs-m2a-plan.md): each package's `protocol.placer`
// module, which says where the nodes of a model the shell hands it go (docs/modular-graph-phase-f5a-design.md section
// 6.3). buildGraph reads them in this order. The generated src/generated/placers.ts seeds it; only the planner reads
// it, so this module imports the primitive and a type, and no package's node code or schema.
import type { GraphPlacer } from "@retroplug/core/placer";
import { Registry } from "./registry";

/** Each package's graph placer, in product order. A model is placed by one package: a second placer of one key would
 *  place its model twice. (A placer's key must also be a section's, which appRegistries.ts checks.) */
export const placersRegistry = new Registry<GraphPlacer<any>>("placers", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: placer } of entries) {
    const owner = owners.get(placer.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} places the graph's model ${placer.key} twice`
        : `two packages place the graph's model ${placer.key}: ${owner} and ${pkg}`);
    }
    owners.set(placer.key, pkg);
  }
});
