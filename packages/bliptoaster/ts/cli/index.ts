// The BlipToaster package's CLI tool: its ROM's kits, themes, fonts and baked settings. It compiles through the "risa"
// kit FORMAT by id (host.kits.compile), so this package depends on no cart.
import { defineCliTools } from "@retroplug/core/cliTool";
import { blipToasterRomTool } from "./bliptoasterRom";

export { blipToasterRomTool } from "./bliptoasterRom";

export default defineCliTools([blipToasterRomTool]);
