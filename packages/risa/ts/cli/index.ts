// The risa package's CLI tools: the ROM tool, and the N8 host-sync bridge. n8-sync lives here because it translates
// into risa's host-sync protocol; the bridge loop it drives is n8's, and n8 names no cart.
import { defineCliTools } from "@retroplug/core/cliTool";
import { n8SyncTool } from "./n8Sync";
import { risaRomTool } from "./risaRom";

export { n8SyncTool } from "./n8Sync";
export { risaRomTool } from "./risaRom";

export default defineCliTools([risaRomTool, n8SyncTool]);
