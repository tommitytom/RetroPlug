// The root `retroplug-cli` dispatcher — the single TS bundle compiled into the binary (tjsc bytecode, see
// packages/native/CMakeLists.txt). The C++ launcher evals this and hands us the whole arg vector
// ([command, ...toolArgs]); we own command routing AND all help text. Adding a command means editing
// cli/tools.ts only — never C++.
//
// Routing:
//   (no command) | -h | --help   → print the top-level command index (exit 0 for --help, 2 for none)
//   <cmd> --help                  → print that tool's detailed help (exit 0)
//   <cmd> [args]                  → boot the control plane and run the tool
//   <unknown>                     → error + the index (exit 2)
// Help/error paths print and exit WITHOUT booting a session (bootSession compiles+loads the DSP kernel —
// wasted work for a help print), so only a real tool run pays that cost.
import { hostArgs, runSession, exitProcess } from "./session";
import { tools, topLevelHelp } from "./tools";

const isHelpFlag = (a: string): boolean => a === "-h" || a === "--help";

const [cmd, ...rest] = hostArgs();
if (cmd === undefined) {
  // Bare `retroplug-cli`: show the index but signal misuse (nothing ran).
  console.error(topLevelHelp(tools));
  exitProcess(2);
} else if (isHelpFlag(cmd)) {
  console.log(topLevelHelp(tools));
  exitProcess(0);
} else {
  const tool = tools.find((t) => t.name === cmd);
  if (!tool) {
    console.error(`retroplug-cli: unknown command '${cmd}'\n\n${topLevelHelp(tools)}`);
    exitProcess(2);
  } else if (rest.some(isHelpFlag)) {
    console.log(tool.help);
    exitProcess(0);
  } else {
    // Only now boot the control plane. runSession reports the exit code (0, or 1 on a thrown error).
    runSession((s) => tool.run(s, rest));
  }
}
