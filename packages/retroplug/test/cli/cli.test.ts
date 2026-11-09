// Guards the CLI dispatcher's top-level help — it must render straight from each tool's own name + summary
// (the registry), so adding a tool auto-appears in the index with no C++ / dispatcher edits. The routing
// itself (reading hostArgs, exit codes) is exercised end-to-end against the built binary; here we cover the
// pure formatting + that the real registry is wired.
import { test, expect } from "@retroplug/testing";
import { composeTools, topLevelHelp, tools, type CliTool } from "../../cli/tools";
import recordedHelp from "./recorded-help.json";
import recordedIndex from "./recorded-index.json";

const fakeTool = (name: string, summary: string): CliTool => ({ name, summary, help: "", run: () => {} });

test("cli: topLevelHelp shows the version banner + every tool's name + summary, column-aligned", () => {
  const out = topLevelHelp([fakeTool("render", "Render a ROM to WAV"), fakeTool("x", "Short one")], "RetroPlug v9.9.9");
  expect(out.startsWith("RetroPlug v9.9.9")).toBeTruthy(); // version banner first
  expect(out.includes("render  Render a ROM to WAV")).toBeTruthy();
  // "x" is padded to the width of the longest name ("render", 6) so summaries line up.
  expect(out.includes("x       Short one")).toBeTruthy();
  expect(out.includes("Run 'retroplug-cli <command> --help'")).toBeTruthy();
});

test("cli: the command index is the shell's five, then the packages' in product order", () => {
  // E1: only the ORDER changed. The shell's own commands come first, then each package's in `retroplug.products.desktop`
  // order (launchpad, lsdj, n8, risa, bliptoaster), which is the same order the menus' contributed entries land in.
  expect(tools.map((tool) => tool.name)).toEqual([
    "test", "run", "render", "analyze-capture", "grab-frame",
    "launchpad-probe", "lsdj-rom",
    "n8-load", "n8-bridge", "n8-play",
    "risa-rom", "n8-sync", "bliptoaster-rom",
  ]);
});

test("cli: the real registry exposes render with a summary and non-empty help", () => {
  const render = tools.find((t) => t.name === "render");
  expect(render !== undefined).toBeTruthy();
  expect(render!.summary.length > 0).toBeTruthy();
  expect(render!.help.includes("--split")).toBeTruthy(); // the detailed help reached the tool
  expect(topLevelHelp(tools, "RetroPlug v0.0.0").includes(render!.summary)).toBeTruthy();
});

test("cli: the real registry exposes risa-rom with build-kit / theme / kit subcommands in its help", () => {
  const risa = tools.find((t) => t.name === "risa-rom");
  expect(risa !== undefined).toBeTruthy();
  expect(risa!.summary.length > 0).toBeTruthy();
  expect(risa!.help.includes("build-kit")).toBeTruthy();
  expect(risa!.help.includes("import-theme")).toBeTruthy();
  expect(risa!.help.includes("import-kit")).toBeTruthy();
  expect(topLevelHelp(tools, "RetroPlug v0.0.0").includes("risa-rom")).toBeTruthy(); // auto-appears in the index
});

test("cli: every tool's summary and help are the recorded ones", () => {
  // Recorded from the pre-F4b binary (`recorded-help.json`), which printed each tool's own help text, so a move either
  // changed a byte a user reads or did not. The index's order is E1's one change; the lines themselves are not.
  const recorded = recordedHelp as { name: string; summary: string; help: string }[];
  expect(tools.length).toBe(recorded.length);
  for (const want of recorded) {
    const tool = tools.find((t) => t.name === want.name);
    expect(tool !== undefined, want.name).toBe(true);
    expect(tool!.summary, want.name).toEqual(want.summary);
    expect(tool!.help, want.name).toEqual(want.help);
  }
});

test("cli: the whole command index is the recorded rows, in the registry's order", () => {
  // `recorded-index.json` is the pre-F4b binary's index exactly as it printed it, kept verbatim so the record stays a
  // recording. E1's one change is the order, which the names test above pins; rebuilding the index from the record in
  // the registry's order holds every row's own text (the columns included) to what the old binary printed, and fails
  // for a row the record lacks or one no tool answers to. Taken at v0.8.0, the version the recording was made at.
  const lines = recordedIndex.split("\n");
  const isRow = (line: string): boolean => /^ {2}\S/.test(line);
  const first = lines.findIndex(isRow);
  const last = lines.length - 1 - [...lines].reverse().findIndex(isRow);
  const rowOf = new Map(lines.filter(isRow).map((line) => [line.trim().split(/\s+/)[0], line]));
  const rows = tools.map((tool) => {
    const row = rowOf.get(tool.name);
    expect(row !== undefined, tool.name).toBe(true);
    return row!;
  });
  // Every recorded row is one a tool answers to, so no row can sit unused in the record.
  expect(rowOf.size).toBe(tools.length);
  expect([...lines.slice(0, first), ...rows, ...lines.slice(last + 1)].join("\n")).toEqual(
    topLevelHelp(tools, "RetroPlug v0.8.0"),
  );
});

test("cli: a contributed tool whose name a shell tool has is refused", () => {
  // The dispatcher finds the first tool by name, so a package's `render` would be shadowed silently.
  expect(() => composeTools([fakeTool("render", "shell")], [fakeTool("render", "package")])).toThrow(
    "a package declares the CLI tool render, which the shell already has",
  );
});
