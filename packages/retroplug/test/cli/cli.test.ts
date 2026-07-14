// Guards the CLI dispatcher's top-level help — it must render straight from each tool's own name + summary
// (the registry), so adding a tool auto-appears in the index with no C++ / dispatcher edits. The routing
// itself (reading hostArgs, exit codes) is exercised end-to-end against the built binary; here we cover the
// pure formatting + that the real registry is wired.
import { test, expect } from "../../testing/harness";
import { topLevelHelp, tools, type CliTool } from "../../cli/tools";

const fakeTool = (name: string, summary: string): CliTool => ({ name, summary, help: "", run: () => {} });

test("cli: topLevelHelp lists every tool's name + summary, column-aligned", () => {
  const out = topLevelHelp([fakeTool("render", "Render a ROM to WAV"), fakeTool("x", "Short one")]);
  expect(out.includes("render  Render a ROM to WAV")).toBeTruthy();
  // "x" is padded to the width of the longest name ("render", 6) so summaries line up.
  expect(out.includes("x       Short one")).toBeTruthy();
  expect(out.includes("Run 'retroplug-cli <command> --help'")).toBeTruthy();
});

test("cli: the real registry exposes render with a summary and non-empty help", () => {
  const render = tools.find((t) => t.name === "render");
  expect(render !== undefined).toBeTruthy();
  expect(render!.summary.length > 0).toBeTruthy();
  expect(render!.help.includes("--split")).toBeTruthy(); // the detailed help reached the tool
  expect(topLevelHelp(tools).includes(render!.summary)).toBeTruthy();
});
