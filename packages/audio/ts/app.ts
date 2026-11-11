// The audio package's app contribution: the `audio` config section (configSection.ts). The Settings > Audio rows are
// the package's UI module (ts/ui), which reads the same section through the shell's `config` group.
import { definePackage } from "@retroplug/core/definePackage";

import { audioSection } from "./configSection";

export default definePackage({
  // The standalone's audio device, as a root key of config.json (docs/modular-graph-phase-f5b-design.md section 6.1):
  // the shell keeps the block where it is, and a product that does not list this package keeps it verbatim.
  configSections: [audioSection],
});
