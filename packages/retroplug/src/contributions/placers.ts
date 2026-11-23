// The placers facade: the generated module seeds the registry when this one is first imported, and the planner reads
// the placers through here, in the `{ pkg, placer }` shape buildGraph takes.
import "../generated/placers";
import type { GraphPlacer } from "@retroplug/core/placer";
import { placersRegistry } from "./placerRegistries";
import { derived } from "./registry";

export { placersRegistry };

/** Every placer with the package it came from, in registration order: the same array until the next commit. */
export const placerEntries = derived((): readonly { pkg: string; placer: GraphPlacer<any> }[] =>
  placersRegistry.entries().map(({ pkg, item }) => ({ pkg, placer: item })),
);
