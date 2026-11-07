// The tracker package's key in a package's app module (docs/modular-graph-phase-f1-design.md section 3): the console
// integrations the Songs and Assets menus search, resolved by a system's markerRole. The key is declared here, beside
// TrackerIntegration, rather than in core, so core needs no type of a package above it; this module is type-only and
// moves to packages/tracker/ts/packageContributions.ts with the tracker package, where the augmentation names
// "@retroplug/core" instead of the path below.
import type { TrackerIntegration } from "./trackerIntegration";

declare module "@retroplug/core/definePackage" {
  interface PackageContributions {
    readonly trackerIntegrations?: readonly TrackerIntegration[];
  }
}
