// The Launchpad package's CLI tool: drive a real device with the pack's own protocol layer and report what answers.
import { defineCliTools } from "@retroplug/core/cliTool";
import { launchpadProbeTool } from "./launchpadProbe";

export { launchpadProbeTool } from "./launchpadProbe";

export default defineCliTools([launchpadProbeTool]);
