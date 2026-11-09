// The two axes a loaded system carries, as the console contract names them, and the console lookups the shell's
// callers know: the core that runs a platform, the graph node that places it and the sniffer.
//
// The authority is the console registry (./consoles.ts) over the descriptors the product's packages contribute
// (docs/modular-graph-phase-f4a-design.md section 3): a console is a package that contributes one, so nothing here names
// a console. `Platform` and `Core` are the contract's open string aliases, which a package may import from
// @retroplug/core/platform or @retroplug/core/consoles.
import {
  classifyRom,
  defaultCoreFor,
  detectPlatform,
  romHasBattery,
  systemNodeTypeFor as nodeTypeFor,
  type PrefixReader,
  type Platform,
} from "./consoles";

/** What a ROM targets, and the emulator that runs a platform: one definition, core's, because ConstructSpec and the
 *  role contract name them and a package may import neither the shell nor a console package. Re-exported here so the
 *  shell's callers keep reading one spelling. */
export type { Platform, Core } from "@retroplug/core/platform";

/** Pick the core that runs `platform` (the sole core-selection policy), or undefined for a platform no console
 *  registers. */
export { defaultCoreFor };

/** The graph node type that runs a system of a platform: the node a document places a system with, which is also what
 *  the engine reads the node's console from (its C++ type builds that console's core). */
export type SystemNodeType = string;

/** The node type that places a system of `platform`. Every caller places a system whose platform was classified or
 *  validated against the registry, so a platform no console registers is a bug rather than a file's: it throws, naming
 *  the platform, instead of placing a document the engine would refuse. */
export function systemNodeTypeFor(platform: Platform): SystemNodeType {
  const type = nodeTypeFor(platform);
  if (type === undefined) throw new Error(`no console registers the platform ${JSON.stringify(platform)}`);
  return type;
}

export { classifyRom, detectPlatform, romHasBattery };
export type { PrefixReader };
