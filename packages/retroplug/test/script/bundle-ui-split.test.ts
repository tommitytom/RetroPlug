// The UI/non-UI bundle split (docs/modular-graph-phase-f4b-design.md section 7.4): until per-product bundles land,
// every product loads every package's registries, so this is the precondition that a non-UI surface carries no UI code
// and the UI carries no CLI code. Two generated modules make the split possible and would undo it silently:
// ui/generated/menus.ts (the packages' menu contributions) and cli/generated/tools.ts (their CLI tools), and beside
// them the packages' own `ts/ui/` and `ts/cli/` trees, reached through their `./ui` and `./cli` entries. The control
// plane, the render worker, the script nodes, the SDK, the CLI dispatcher and the planner must carry no `ui` module
// (the package, the React kit, any package's `ts/ui/`, the menus), nor a package's view; the UI must carry no CLI one
// (any package's `ts/cli/`, the tool contract, the tools). The dispatcher is the bundle that carries the tools, which is
// why it is named here.
//
// The bundles are built as the build builds them, in memory: tools/build-session.js's bundleInputs (esm/iife) and
// tools/build-ui.js's, and esbuild's metafile says which modules went in. The build is Node's, so test/script/
// bundle-split.mjs runs it in a child, as bundle-protocol.test.ts does.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-protocol.test.ts declares them.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(
    args: string[],
    opts: { stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

async function text(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const chunks: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  let total = 0;
  for (const c of chunks) total += c.length;
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.length;
  }
  return new TextDecoder().decode(all);
}

/** Every bundle's input modules, as absolute paths. */
async function bundles(): Promise<Record<string, string[]>> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/script/bundle-split.mjs"], {
    stdout: "pipe",
    stderr: "pipe",
  });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) {
    throw new Error(`the bundle split could not be built: ${err || out}`);
  }
  return JSON.parse(out) as Record<string, string[]>;
}

/** The UI's own modules: the `ui` package, the React kit (a package of its own, so one regex covers both), any
 *  package's UI module tree, and the shell's generated menu table. No non-UI surface may load one. */
const isUi = (path: string): boolean =>
  /\/packages\/ui(-react)?\//.test(path) ||
  /\/packages\/[^/]+\/ts\/ui\//.test(path) ||
  /\/packages\/retroplug\/ui\/generated\//.test(path);
/** The CLI's own modules: the shell's generated tool table, any package's CLI module tree, and the tool contract every
 *  one of them implements. The UI may load none of them. */
const isCli = (path: string): boolean =>
  /\/packages\/retroplug\/cli\/generated\//.test(path) ||
  /\/packages\/[^/]+\/ts\/cli\//.test(path) ||
  /\/packages\/core\/ts\/cliTool\.ts$/.test(path);

const NON_UI = ["control-plane", "render-worker", "script-nodes", "cli-sdk", "cli-dispatcher", "planner"];

test("no non-UI bundle carries any package's UI module or the generated menu table", async () => {
  const built = await bundles();
  for (const name of NON_UI) {
    const ui = built[name]!.filter(isUi);
    console.log(`# ${name}: ${built[name]!.length} modules, ${ui.length} UI: ${ui.join(", ")}`);
    expect(ui, `${name} carries UI code`).toEqual([]);
  }
});

test("the UI bundle carries no package's CLI module, no tool contract and no tool table", async () => {
  // A package's own `./cli` entry is the sharp case: it carries the tool AND `@retroplug/core/cliTool`, and neither is
  // the generated table, so the table alone would not see it.
  const built = await bundles();
  const cli = built.ui!.filter(isCli);
  console.log(`# ui: ${built.ui!.length} modules, ${cli.length} CLI: ${cli.join(", ")}`);
  expect(cli).toEqual([]);
});

test("the UI bundle carries the kit, and no bundle carries the unlisted fixture package", async () => {
  // The kit is UI code by package, not only by tree: its entry and any later ts/ file count as much as the components.
  // The fixture package is listed by no product, so nothing pulls it in, and this is what would catch a product list
  // that picked it up.
  const built = await bundles();
  const kit = built.ui!.filter((path) => /\/packages\/ui-react\//.test(path));
  console.log(`# ui: ${kit.length} @retroplug/ui-react modules: ${kit.join(", ")}`);
  expect(kit.length > 0, "the UI bundle carries no @retroplug/ui-react module").toBe(true);
  for (const name of Object.keys(built)) {
    const fixture = built[name]!.filter((path) => /\/packages\/ui-fixture\//.test(path));
    expect(fixture, `${name} carries the unlisted fixture package`).toEqual([]);
  }
});

test("the CLI dispatcher carries the generated tool table, and the planner the generated protocol data", async () => {
  // The split is not "carry no generated module": each surface carries its own. A dispatcher that lost the tool table
  // would list no package command, and a planner without the protocol data would place none.
  const built = await bundles();
  expect(built["cli-dispatcher"]!.some(isCli)).toBe(true);
  expect(built.planner!.some((path) => /\/src\/generated\/protocol\.ts$/.test(path))).toBe(true);
});
