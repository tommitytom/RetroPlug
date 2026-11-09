// The N8 package's CLI tools: load/boot a ROM on the cart, stream live MIDI to it, and play a scripted sequence.
import { defineCliTools } from "@retroplug/core/cliTool";
import { n8BridgeTool } from "./n8Bridge";
import { n8LoadTool } from "./n8Load";
import { n8PlayTool } from "./n8Play";

export { n8BridgeTool } from "./n8Bridge";
export { n8LoadTool } from "./n8Load";
export { n8PlayTool } from "./n8Play";

export default defineCliTools([n8LoadTool, n8BridgeTool, n8PlayTool]);
