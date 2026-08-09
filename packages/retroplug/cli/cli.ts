// The root `retroplug-cli` dispatcher — the single TS bundle compiled into the binary (tjsc bytecode, see
// packages/native/CMakeLists.txt). The C++ launcher evals this and hands us the whole arg vector
// ([command, ...toolArgs]); we own command routing AND all help text. Adding a command means editing
// cli/tools.ts only — never C++.
//
// Routing:
//   --version                     → print the version and exit (exit 0)
//   (no command) | -h | --help   → print the top-level command index (exit 0 for --help, 2 for none)
//   <cmd> --help                  → print that tool's detailed help (exit 0)
//   <cmd> [args]                  → boot the control plane and run the tool
//   <unknown>                     → error + the index (exit 2)
// Help/version/error paths print and exit WITHOUT booting a session (bootSession compiles+loads the DSP
// kernel — wasted work for a help print), so only a real tool run pays that cost.
import { hostArgs, runSession, runLongSession, exitProcess } from "./session";
import { createHostClient } from "../src/realBackend";
import { tools, topLevelHelp } from "./tools";

const isHelpFlag = (a: string): boolean => a === "-h" || a === "--help";

/** The version, single-sourced from the native Version.hpp via the backend RPC (no session boot). */
function appVersion(): string {
  try { return createHostClient().version(); } catch { return "unknown"; } // no RPC bridge
}

const [cmd, ...rest] = hostArgs();
if (cmd === "--version") {
  console.log(`v${appVersion()}`); // bare version, e.g. "v0.7.1"
  exitProcess(0);
} else if (cmd === undefined) {
  // Bare `retroplug-cli`: show the index but signal misuse (nothing ran).
  console.error(topLevelHelp(tools, `RetroPlug v${appVersion()}`));
  exitProcess(2);
} else if (isHelpFlag(cmd)) {
  console.log(topLevelHelp(tools, `RetroPlug v${appVersion()}`));
  exitProcess(0);
} else {
  const tool = tools.find((t) => t.name === cmd);
  if (!tool) {
    console.error(`retroplug-cli: unknown command '${cmd}'\n\n${topLevelHelp(tools, `RetroPlug v${appVersion()}`)}`);
    exitProcess(2);
  } else if (rest.some(isHelpFlag)) {
    console.log(tool.help);
    exitProcess(0);
  } else if (tool.longRunning) {
    // A long-running tool (live MIDI bridge) sets up an event loop + keepAlive() and returns; the native
    // pump runs it until tjs.exit / Ctrl-C, so it must NOT be auto-exited.
    runLongSession((s) => tool.run(s, rest));
  } else {
    // Only now boot the control plane. runSession reports the exit code (0, or 1 on a thrown error).
    runSession((s) => tool.run(s, rest));
  }
}
