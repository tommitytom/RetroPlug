// The registry of `retroplug-cli` tools (subcommands), in the order the index prints them: the shell's own five, then
// the packages' contributed ones in product order (docs/modular-graph-phase-f4b-design.md sections 5.1 and 5.2). Each
// tool OWNS its help - its name, one-line summary (shown in the top-level command index) and detailed `--help` text -
// so adding a command is a package's own module plus its manifest's `retroplug.cli`; nothing here names a cart. The
// root dispatcher (cli/cli.ts) renders the top-level help from these entries and routes `<cmd>` / `<cmd> --help`; the
// C++ launcher (native/cli/main.cpp) knows nothing about commands.

import type { CliTool as CoreCliTool } from "@retroplug/core/cliTool";
import type { Session } from "./session";
import { cliHost } from "./cliHost";
import { CLI_TOOLS } from "./generated/tools";
import { testTool } from "./sessions/test";
import { runTool } from "./sessions/run";
import { renderTool } from "./sessions/render";
import { analyzeCaptureTool } from "./sessions/analyze-capture";
import { grabFrameTool } from "./sessions/grab-frame";

/** A tool the shell owns: it runs against the whole booted session (the stores, the backend and the audio driver),
 *  which a contributed tool cannot see - it gets the CLI host's groups instead. */
export type CliTool = CoreCliTool<Session>;

/** The shell's own commands, in index order: the ones that drive the control plane itself. */
const SHELL_TOOLS: readonly CliTool[] = [
  testTool,
  runTool,
  renderTool,
  analyzeCaptureTool,
  grabFrameTool,
];

/** Bind a contributed tool to a booted session: its `run` takes the host's groups, which the session builds. */
function fromContribution(tool: CoreCliTool): CliTool {
  return { ...tool, run: (session, args) => tool.run(cliHost(session, args), args) };
}

/** Every contributed command, in product order. */
const CONTRIBUTED: readonly CliTool[] = CLI_TOOLS.map(fromContribution);

/** Every baked-in command, in the index's order: the shell's five, then the packages' in product order. One subcommand
 *  is one tool: a contributed name the shell already has would silently shadow it (the dispatcher finds the first), so
 *  it is refused here, where a test can show it, rather than at a user's first run. */
export function composeTools(shell: readonly CliTool[], contributed: readonly CliTool[]): readonly CliTool[] {
  for (const tool of contributed) {
    if (shell.some((own) => own.name === tool.name)) {
      throw new Error(`a package declares the CLI tool ${tool.name}, which the shell already has`);
    }
  }
  return [...shell, ...contributed];
}

/** Every baked-in command, in the index's order. */
export const tools: readonly CliTool[] = composeTools(SHELL_TOOLS, CONTRIBUTED);

/** The top-level command index: a version banner + the tools' own name/summary (column-aligned). Pure - the dispatcher
 *  (cli/cli.ts) supplies the banner and prints this for `retroplug-cli` / `--help`; kept here so it's testable in
 *  isolation. */
export function topLevelHelp(list: readonly CliTool[], versionLine: string): string {
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
