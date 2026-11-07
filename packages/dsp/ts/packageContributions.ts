// The dsp package's key in a package's app module (docs/modular-graph-phase-f1-design.md section 3): the kit bank
// formats the audio driver dispatches to by id, each contributed by the cart whose bank it assembles. The key is
// declared here, beside KitFormat, rather than in core, so core needs no type of a package above it, and the
// augmentation names core by its package name. A package's app module is checked against it by importing this module
// for its types alone (src/app.ts), so it holds no runtime code.
import type {} from "@retroplug/core/definePackage";
import type { KitFormat } from "./kitContract";

declare module "@retroplug/core/definePackage" {
  interface PackageContributions {
    readonly kitFormats?: readonly KitFormat<any>[];
  }
}
