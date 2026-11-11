// The midi package's app contribution: the `midi` project section (configSection.ts) - the ports a project listens to
// and mirrors to, and its clock tempo. The Settings > MIDI rows, the MIDI Clock submenu and the service that applies a
// project's selection are the package's UI module (ts/ui).
import { definePackage } from "@retroplug/core/definePackage";

import { midiSection } from "./configSection";

export default definePackage({
  // Project state, not machine state: a setup travels with the file, and portMatch.ts finds the saved names on
  // another machine (docs/modular-graph-phase-f5b-design.md section 6.2). A product that does not list this package
  // keeps a project's `midi` block verbatim.
  projectSections: [midiSection],
});
