// The registry of baked-in `retroplug-cli` tools (subcommands). Each tool OWNS its own help — its name,
// one-line summary (shown in the top-level command index) and detailed `--help` text — so adding a command
// is pure TS: write the tool, then add it to `tools` below. The root dispatcher (cli/cli.ts) renders the
// top-level help from these entries and routes `<cmd>` / `<cmd> --help`; the C++ launcher (native/cli/main.cpp)
// knows nothing about commands.

import type { Session } from "./session";

export interface CliTool {
  /** The subcommand name, e.g. "render" (what the user types after `retroplug-cli`). */
  name: string;
  /** One line for the top-level command index (`retroplug-cli --help`). */
  summary: string;
  /** The full `retroplug-cli <name> --help` text — flags, defaults, examples. */
  help: string;
  /** A long-running tool (e.g. a live MIDI bridge): it calls keepAlive() + sets up an event-driven loop and
   *  returns, and the dispatcher does NOT auto-exit it (runLongSession) — the native pump runs it until
   *  tjs.exit / Ctrl-C. Omit / false for a normal batch command. */
  longRunning?: boolean;
  /** Run the tool against a booted control plane, given its own args (everything after the command name). */
  run(s: Session, args: string[]): void;
}

import { renderTool } from "./sessions/render";
import { lsdjRomTool } from "./sessions/lsdj-rom";
import { risaRomTool } from "./sessions/risa-rom";
import { everMidiRomTool } from "./sessions/evermidi-rom";
import { n8LoadTool } from "./sessions/n8-load";
import { n8BridgeTool, n8SyncTool } from "./sessions/n8-bridge";

/** Every baked-in command. The ONLY place a new tool is registered. */
export const tools: CliTool[] = [renderTool, lsdjRomTool, risaRomTool, everMidiRomTool, n8LoadTool, n8BridgeTool, n8SyncTool];

/** The top-level command index: a version banner + the tools' own name/summary (column-aligned). Pure — the
 *  dispatcher (cli/cli.ts) supplies the banner and prints this for `retroplug-cli` / `--help`; kept here so
 *  it's testable in isolation. */
export function topLevelHelp(list: CliTool[], versionLine: string): string {
  const width = list.reduce((w, t) => Math.max(w, t.name.length), 0);
  const commands = list.map((t) => `  ${t.name.padEnd(width)}  ${t.summary}`).join("\n");
  return [
    versionLine,
    "",
    "usage: retroplug-cli <command> [args...]",
    "       retroplug-cli <session.js> [args...]   (run a JavaScript session file by path)",
    "",
    "commands:",
    commands,
    "",
    "Run 'retroplug-cli <command> --help' for a command's options.",
  ].join("\n");
}
