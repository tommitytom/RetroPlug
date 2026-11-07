// The app-contribution contract a package's `app` module default-exports (docs/modular-graph-phase-f1-design.md
// section 3): the pure registrations a package adds to the app, as lists rather than as calls into a registry. Every
// list is optional, and a list's order is the order its contributions register in, so the shell's registry, the ROM
// sniffer, the Songs menu and the kit compiler see the same sequence whatever package they came from.
//
// What this file declares is the part core owns: the lists whose types core knows (a role, a ROM provider). A package
// that owns another kind augments PackageContributions from its own module, so core imports nothing above itself: the
// tracker its trackerIntegrations (packages/tracker/ts/packageContributions.ts), dsp its kitFormats
// (packages/dsp/ts/packageContributions.ts), and each module imports the types it names from the package that owns
// them.
//
// definePackage holds no registry and does nothing at run time: it is the identity function with types, so a package
// states its contributions where they are defined and the generated registry (src/generated/packages.ts) only has to
// concatenate them in product order. It is deliberately not generic: a type parameter inferred from the literal would
// swallow a misspelt key, where the parameter's own type refuses it (test/packages/define-package.test.ts).
import type { RoleType, RomProvider } from "./systemRoles";

/** A package's contributions as core knows them: the role contract's objects and the ROM providers. */
export interface PackageContributions {
  readonly roles?: readonly RoleType[];
  readonly romProviders?: readonly RomProvider[];
}

/** The identity function with types: an app module's default export, checked against what a package may contribute. */
export function definePackage(contributions: PackageContributions): PackageContributions {
  return contributions;
}
