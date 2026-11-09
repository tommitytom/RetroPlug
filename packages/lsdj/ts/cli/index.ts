// The LSDj package's CLI tools (docs/modular-graph-phase-f4b-design.md section 5.2): what `retroplug.cli` names, so the
// shell's dispatcher carries no cart's code. The tools reach the shell only through the CLI host.
import { defineCliTools } from "@retroplug/core/cliTool";
import { lsdjRomTool } from "./lsdjRom";

export { lsdjRomTool } from "./lsdjRom";

export default defineCliTools([lsdjRomTool]);
