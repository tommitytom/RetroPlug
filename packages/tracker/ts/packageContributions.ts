// The tracker package's key in a package's app module (docs/modular-graph-phase-f1-design.md section 3): the console
// integrations the Songs and Assets menus search, resolved by a system's markerRole. A cart's app module contributes
// its own integration; the shell's generated registry concatenates them.
//
// The key is declared here, beside TrackerIntegration, rather than in core, so core needs no type of a package above
// it. The module is type-only: importing it (the shell's src/app.ts does, as `import type {}`) is what makes the key
// legal in a definePackage call, and the core import below is what the augmentation attaches to.
import type {} from "@retroplug/core/definePackage";
import type { TrackerIntegration } from "./trackerIntegration";

declare module "@retroplug/core/definePackage" {
  interface PackageContributions {
    readonly trackerIntegrations?: readonly TrackerIntegration[];
  }
}
