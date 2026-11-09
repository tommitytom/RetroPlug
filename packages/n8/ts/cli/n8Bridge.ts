// `retroplug-cli n8-bridge` - stream live MIDI to a physical Everdrive N8 Pro over USB, entirely in TS (it replaced the
// native subcommand). The bridge loop itself is the pack's `/bridge` entry, which risa's n8-sync drives too.
import type { CliHost, CliTool } from "@retroplug/core/cliTool";
import { COMMON_FLAGS, printInputs, runBridge } from "../bridge";

const BRIDGE_HELP = [
  "usage: retroplug-cli n8-bridge [--list] [--midi-in <name>] [--serial <port>] [--lookahead-ms <N>]",
  "",
  "  Stream live MIDI straight to a physical Everdrive N8 Pro's cart FIFO over USB, so a controller / DAW",
  "  plays the real NES (for the BlipToaster ROM). Runs until Ctrl-C.",
  "",
  ...COMMON_FLAGS,
].join("\n");

export const n8BridgeTool: CliTool = {
  name: "n8-bridge",
  summary: "stream live MIDI to a physical Everdrive N8 Pro over USB",
  help: BRIDGE_HELP,
  longRunning: true,
  run(host: CliHost, args: string[]): void {
    if (args.includes("--list")) {
      printInputs(host);
      host.process.exit(0);
      return;
    }
    runBridge(host, args, "RetroPlug N8", (bytes, out) => {
      for (const b of bytes) out.push(b); // raw forward
    });
  },
};
