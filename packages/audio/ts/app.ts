// The audio package's app contribution. The `audio` config section and the Settings > Audio rows arrive with the
// TypeScript half of the phase; this module exists so the manifest's entry resolves and the package is in the product
// list's generated registries.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({});
