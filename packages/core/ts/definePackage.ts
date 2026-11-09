// The app-contribution contract a package's `app` module default-exports (docs/modular-graph-phase-f1-design.md
// section 3): the pure registrations a package adds to the app, as lists rather than as calls into a registry. Every
// list is optional, and a list's order is the order its contributions register in, so the shell's registry, the ROM
// sniffer, the Songs menu and the kit compiler see the same sequence whatever package they came from.
//
// What this file declares is the part core owns: the lists whose types core knows (a role, a ROM provider). A package
// that owns another kind augments PackageContributions from its own module, so core imports nothing above itself: the
// tracker its trackerIntegrations (packages/tracker/ts/packageContributions.ts), dsp its kitFormats
// (packages/dsp/ts/packageContributions.ts), and each imports the types it names from the package that owns them.
//
// definePackage holds no registry and does nothing at run time: it is the identity function with types, so a package
// states its contributions where they are defined, and the generated registry
// (packages/retroplug/src/generated/packages.ts) only has to concatenate them in product order. It is deliberately not
// generic: a type parameter inferred from the literal would swallow a misspelt key, where the parameter's own type
// refuses it (test/packages/define-package.test.ts).
//
// A package's consoles are NOT here: they are data the planner reads, so they come from a module of their own, which a
// manifest's `retroplug.consoles` member names (docs/modular-graph-phase-f4a-design.md section 3). The app module is
// the wrong place for them, because it is where the role schemas and zod are, and a bundle of the planner must carry
// the descriptors without either (test/script/bundle-protocol.test.ts).
import type { RoleType, RomProvider } from "./systemRoles";
import type { Platform } from "./consoles";

/** An embedded ROM a package bakes into the product's binary, and what the shell needs to load it: the id native's
 *  resource table knows it by (the manifest's `retroplug.embed` names the file), the label a menu offers it under, and
 *  the console it runs on, which a load takes instead of assuming one. The generator pairs the declaration with the
 *  manifest entry, so an unpaired side refuses the build. */
export interface Embed {
  readonly id: string;
  readonly label: string;
  /** What a menu title calls it: the short name a narrow row has room for, where `label` is the full one a Load row
   *  spells out ("mGB" against "mGB (GB MIDI Synth)"). Absent, the label is the title's name. */
  readonly shortLabel?: string;
  readonly platform: Platform;
}

/** A package's contributions as core knows them: the role contract's objects, the ROM providers, and the embedded
 *  ROMs its bytes carry. */
export interface PackageContributions {
  readonly roles?: readonly RoleType[];
  readonly romProviders?: readonly RomProvider[];
  readonly embeds?: readonly Embed[];
}

/** The identity function with types: an app module's default export, checked against what a package may contribute. */
export function definePackage(contributions: PackageContributions): PackageContributions {
  return contributions;
}
