// `retroplug-cli n8-sync` - translate a MIDI clock/transport into risa's host-sync bytes on a physical Everdrive N8
// Pro. It lives in risa, which depends on n8 and drives that pack's shared bridge loop; n8 itself names no cart.
import type { CliHost, CliTool } from "@retroplug/core/cliTool";
import { COMMON_FLAGS, printInputs, runBridge } from "@retroplug/n8/bridge";
import { RisaSyncTranslator } from "../risaSyncTranslator";

const SYNC_HELP = [
  "usage: retroplug-cli n8-sync [--list] [--midi-in <name>] [--serial <port>] [--lookahead-ms <N>]",
  "",
  "  Turn an incoming MIDI clock/transport (Start/Continue/Stop + 24-PPQN clock, optional Song Position)",
  "  into risa's host-sync protocol on a physical N8, so a risa cart plays locked to the DAW. Runs until",
  "  Ctrl-C. Load a risa ROM first (retroplug-cli n8-load).",
  "",
  ...COMMON_FLAGS,
].join("\n");

export const n8SyncTool: CliTool = {
  name: "n8-sync",
  summary: "drive risa host-sync on a physical Everdrive N8 Pro from a MIDI clock",
  help: SYNC_HELP,
  longRunning: true,
  run(host: CliHost, args: string[]): void {
    if (args.includes("--list")) {
      printInputs(host);
      host.process.exit(0);
      return;
    }
    const translator = new RisaSyncTranslator();
    runBridge(host, args, "RetroPlug N8 Sync", (bytes, out) => translator.onMessage(bytes, out));
  },
};
