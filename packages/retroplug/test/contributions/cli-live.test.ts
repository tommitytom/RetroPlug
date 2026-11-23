// The CLI's tools follow a commit (docs/downloadable-packs-m2a-plan.md, Task 7): a tool committed after the built-ins
// joins the index after them, and a tool taking one of the shell's five names, or a contributed name, is refused at
// its commit rather than at a user's first run.
import { test, expect } from "@retroplug/testing";
import type { CliTool } from "@retroplug/core/cliTool";
import { commitContributions } from "../../src/contributions/registry";
import { cliToolsRegistry, SHELL_TOOL_NAMES } from "../../cli/contributions/toolRegistries";
import { tools } from "../../cli/tools";

const fixtureTool = {
  name: "packfixture-hello",
  summary: "say hello from a pack",
  help: "usage: retroplug-cli packfixture-hello",
  run: () => 0,
} as unknown as CliTool;

test("the shell's tool names are the dispatcher's own five, first in the index", () => {
  expect(tools().slice(0, SHELL_TOOL_NAMES.length).map((tool) => tool.name)).toEqual([...SHELL_TOOL_NAMES]);
});

test("a tool committed later joins the index after the built-ins", () => {
  const before = tools();
  commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture", items: [fixtureTool] }]);
  const after = tools();
  expect(after.length).toBe(before.length + 1);
  expect(after[after.length - 1].name).toBe("packfixture-hello");
  expect(tools()).toBe(after);
});

test("a tool taking a shell command's name is refused at its commit", () => {
  const clash = { ...fixtureTool, name: "render" } as CliTool;
  expect(() => commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture2", items: [clash] }])).toThrow(
    "a package declares the CLI tool render, which the shell already has",
  );
});

test("a tool taking a contributed name is refused naming both packages", () => {
  expect(() => commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture3", items: [fixtureTool] }])).toThrow(
    "two packages declare the CLI tool packfixture-hello: packfixture and packfixture3",
  );
});
