// The CLI tool contract (docs/modular-graph-phase-f4b-design.md section 5.1): a package's subcommand is data it
// contributes, run against the groups of the CLI host, so the shell's dispatcher carries no cart's or device's code.
import type { Host } from "./host";

/** What a CLI tool may reach: the file and image helpers, the kit compiler, the native facets' devices and the
 *  process a session runs in. No dialogs (a tool takes paths), no live systems and no views; this is the one name for
 *  it, which the shell's host builder returns and a package's `run` receives. */
export type CliHost = Host<"files" | "png" | "kits" | "devices" | "process" | "songs">;

export interface CliTool<H = CliHost> {
  /** The subcommand, what the user types after `retroplug-cli`. */
  readonly name: string;
  /** One line for the top-level command index. */
  readonly summary: string;
  /** The full `retroplug-cli <name> --help` text: flags, defaults, examples. */
  readonly help: string;
  /** A live tool (a MIDI bridge): it calls `host.process.keepAlive()` and returns, and the dispatcher leaves it running
   *  until the process ends. Omit for a batch command. */
  readonly longRunning?: boolean;
  /** Run against the tool's host, given its own argument vector (everything after the command name). A promise is
   *  waited for before the process exits, so a rejection is still reported as a failed command. */
  run(host: H, args: string[]): void | Promise<void>;
}

/** The identity a package's tools module default-exports; it exists so the contribution is checked where it is
 *  written. */
export function defineCliTools(tools: readonly CliTool[]): readonly CliTool[] {
  return tools;
}
