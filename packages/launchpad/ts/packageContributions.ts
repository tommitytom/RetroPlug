// The launchpad package's key in a package's app module (docs/modular-graph-phase-f3-design.md section 4): the
// controller apps the control plane offers - each app's id, its menu label, its config's schema and the rows the menu
// builds from that schema. A bridge package contributes its own app, and the shell's generated registry concatenates
// them. What an app needs the cart it drives to be set to is NOT here: it is the runtime half's `syncOverride`, in
// `protocol.kinds.controllerApps`, so the shell's planner reads it as data and the app module keeps none of it.
//
// The key is declared here, beside ControllerAppContribution, rather than in core, so core needs no type of a package
// above it. The module is type-only: importing it (a package's ts/app.ts does, as `import type {}`) is what makes the
// key legal in a definePackage call, and the core import below is what the augmentation attaches to.
import type {} from "@retroplug/core/definePackage";
import type { ControllerAppContribution } from "./registry";

declare module "@retroplug/core/definePackage" {
  interface PackageContributions {
    readonly controllerApps?: readonly ControllerAppContribution[];
  }
}
