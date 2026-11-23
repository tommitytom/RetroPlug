// The CLI builder holds a pack's cli half to the CliTool shape, so a malformed tool is refused at load and never reaches
// the dispatcher's help (cli/packs/cliPacks.ts).
import { test, expect } from "@retroplug/testing";
import { CLI_BUILDERS } from "../../cli/packs/cliPacks";

const build = (exported: unknown): unknown => CLI_BUILDERS.cli!("some-pack", exported);
const tool = { name: "x-tool", summary: "s", help: "h", run: () => {} };

test("a well-formed tool list builds", () => {
  expect((build([tool]) as unknown[]).length).toBe(1);
});

test("a tool that is not a CliTool is refused, naming its index", () => {
  const msg = "its cli half's tool 1 is not a CliTool (name, summary, help, run)";
  expect(() => build([tool, { ...tool, summary: 5 }])).toThrow(msg);
  expect(() => build([tool, { name: "y", summary: "s", help: "h" }])).toThrow(msg);
  expect(() => build([tool, { ...tool, name: "" }])).toThrow(msg);
  expect(() => build([tool, null])).toThrow(msg);
});
