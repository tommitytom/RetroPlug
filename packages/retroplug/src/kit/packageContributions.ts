// The dsp package's key in a package's app module (docs/modular-graph-phase-f1-design.md section 3): the kit bank
// formats the audio driver dispatches to by id, each contributed by the cart whose bank it assembles. The key is
// declared here, beside KitFormat, rather than in core, so core needs no type of a package above it; this module is
// type-only and moves to packages/dsp/ts/packageContributions.ts with the kit chain, where the augmentation names
// "@retroplug/core" instead of the path below.
import type { KitFormat } from "./compile";

declare module "../core/definePackage" {
  interface PackageContributions {
    readonly kitFormats?: readonly KitFormat<any>[];
  }
}
