// The shell's own app contribution, its package.json's retroplug.app: the lists the app hardcoded in appHost.ts,
// romProviders.ts, tracker/trackerIntegration.ts and kit/compile.ts until the generated registry
// (src/generated/packages.ts) concatenated every package's. Every list is empty now: each console and cart that
// contributes one has its own package (its app module), and what is left in this tree is the stores, the graph, the UI
// and the controller, whose roles and node (rp.controller.session) arrive with the controller's own package in F3.
//
// The module stays because the generated registry reads it, and because an empty list is a statement: a role added
// here would be a role no package owns, which the boundary work exists to prevent.
import { definePackage } from "@retroplug/core/definePackage";
// The keys the owner packages add to the contract (their modules augment PackageContributions): type-only, so nothing
// loads at run time.
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";

export default definePackage({
  roles: [],
  romProviders: [],
  trackerIntegrations: [],
  kitFormats: [],
});
