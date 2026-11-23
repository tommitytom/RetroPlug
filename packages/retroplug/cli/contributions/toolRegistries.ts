// The CLI's live tool registry (docs/downloadable-packs-m2a-plan.md): each package's `retroplug.cli` module's tools,
// seeded by cli/generated/tools.ts in product order, and a pack's appended. One subcommand is one tool: the index is
// what a user types, so a name two packages declare, or one of the shell's own, is refused at the commit that brings
// it. This module may not import the shell's five sessions (each pulls a session's whole graph), so it names them;
// test/contributions/cli-live.test.ts holds the list to cli/tools.ts's.
import type { CliTool } from "@retroplug/core/cliTool";
import { Registry } from "../../src/contributions/registry";

/** The shell's own commands, by name, in index order (cli/tools.ts's SHELL_TOOLS). */
export const SHELL_TOOL_NAMES: readonly string[] = ["test", "run", "render", "analyze-capture", "grab-frame"];

/** Every contributed tool, in registration order: the dispatcher lists them after the shell's five. */
export const cliToolsRegistry = new Registry<CliTool>("cliTools", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: tool } of entries) {
    if (SHELL_TOOL_NAMES.includes(tool.name)) {
      throw new Error(`the package ${pkg} declares the CLI tool ${tool.name}, which the shell already has`);
    }
    const owner = owners.get(tool.name);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the CLI tool ${tool.name} twice`
        : `two packages declare the CLI tool ${tool.name}: ${owner} and ${pkg}`);
    }
    owners.set(tool.name, pkg);
  }
});
