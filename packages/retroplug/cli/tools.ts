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
  /** Run the tool against a booted control plane, given its own args (everything after the command name). */
  run(s: Session, args: string[]): void;
}

import { renderTool } from "./sessions/render";

/** Every baked-in command. The ONLY place a new tool is registered. */
export const tools: CliTool[] = [renderTool];

/** The top-level command index, rendered from each tool's own name + summary (column-aligned). Pure — the
 *  dispatcher (cli/cli.ts) prints it for `retroplug-cli` / `--help`; kept here so it's testable in isolation. */
export function topLevelHelp(list: CliTool[]): string {
  const width = list.reduce((w, t) => Math.max(w, t.name.length), 0);
  const commands = list.map((t) => `  ${t.name.padEnd(width)}  ${t.summary}`).join("\n");
  return [
    "usage: retroplug-cli <command> [args...]",
    "       retroplug-cli <session.js> [args...]   (run a TS-authored session by path)",
    "",
    "commands:",
    commands,
    "",
    "Run 'retroplug-cli <command> --help' for a command's options.",
  ].join("\n");
}
