// The pure-tier proof of the views split (docs/modular-graph-phase-f5a-design.md section 5.4): a package's views are UI
// code, and the shell's menu modules are not. Before the split, ui/generated/menus.ts imported every package's `ui`
// module, so the first product to list a React view package would have broken every pure-tier test that reaches
// menuDefs.ts, because the runner bundles with no aliases and no React path. Three cases hold the two halves apart:
//
// 1. a view package bundles as the UI bundle bundles it: with tools/build-ui.js's aliases, nodePaths and JSX, the
//    fixture package's views module carries @retroplug/ui-react and React itself;
// 2. the pure tier's modules (ui/host/menuContributions.ts, the generated menus.ts and ui/screens/menu/menuDefs.ts, the
//    file the menu tests enter through) bundle with the runner's own options and carry no generated views module, no
//    package's views and nothing of the kit;
// 3. the cause, held as a case: a fixture tree whose views module imports react generates a menus module that bundles
//    without it (and a views module that does not), which is the failure the pre-split generator produced for the
//    menus module itself.
//
// The build is Node's, so test/script/bundle-views.mjs runs it in a child, as bundle-split.mjs runs the surface
// bundles.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-split.test.ts declares them.
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

interface BundleReport {
  inputs?: string[];
  error?: string;
}

/** One driver mode's report, with every input path made forward-slashed (a Windows build's metafile paths are). */
async function drive(mode: string): Promise<any> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/script/bundle-views.mjs", mode], {
    stdout: "pipe",
    stderr: "pipe",
  });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) {
    throw new Error(`the view bundle driver failed: ${err || out}`);
  }
  return JSON.parse(out) as Record<string, any>;
}

/** A report's inputs as forward-slashed paths, or the failure reported beside them. */
function inputsOf(report: BundleReport): string[] {
  if (report.error !== undefined) throw new Error(`the bundle failed: ${report.error}`);
  return (report.inputs ?? []).map((path) => path.split("\\").join("/"));
}

const isKit = (path: string): boolean => /\/packages\/ui-react\//.test(path);
const isReact = (path: string): boolean => /\/node_modules\/react\//.test(path);
/** A package's views module, or the shell's generated view table. */
const isViews = (path: string): boolean =>
  /\/ui\/generated\/views\.ts$/.test(path) || /\/packages\/[^/]+\/ts\/ui\/views\.ts$/.test(path);

test("a view package bundles as the UI bundle bundles it: the kit and React itself", async () => {
  const inputs = inputsOf((await drive("fixture-view")) as BundleReport);
  const kit = inputs.filter(isKit);
  const react = inputs.filter(isReact);
  console.log(`# fixture-view: ${inputs.length} modules, ${kit.length} kit, ${react.length} React`);
  expect(kit.length > 0, "the view package's bundle carries no @retroplug/ui-react module").toBe(true);
  expect(react.length > 0, "the view package's bundle carries no React module").toBe(true);
});

test("the pure tier's menu modules carry no view and no kit", async () => {
  const report = await drive("pure");
  for (const name of ["menus", "composer", "defs"] as const) {
    const inputs = inputsOf(report[name] as BundleReport);
    const views = inputs.filter(isViews);
    const kit = inputs.filter(isKit);
    console.log(`# pure ${name}: ${inputs.length} modules, ${views.length} views, ${kit.length} kit`);
    expect(views, `the pure ${name} bundle carries a views module`).toEqual([]);
    expect(kit, `the pure ${name} bundle carries @retroplug/ui-react`).toEqual([]);
  }
  // The positive control: the generated menu table IS carried by the two modules that read it, so the case is not
  // vacuous (the menus module is the table itself).
  for (const name of ["menus", "composer"] as const) {
    expect(
      inputsOf(report[name] as BundleReport).some((path) => /\/ui\/generated\/menus\.ts$/.test(path)),
      `the pure ${name} bundle carries no generated menus module`,
    ).toBe(true);
  }
});

test("a generated menus module never reaches a React view, and a generated views module does", async () => {
  const report = await drive("views-react");
  // The menus module of a tree whose package contributes a React view still bundles with the pure options: it reaches
  // the package's `ui` module alone.
  const menus = inputsOf(report.menus as BundleReport);
  expect(menus.some(isViews)).toBe(false);
  // And the views module that tree generated needs React, which the pure tier has no path to: asserted as the expected
  // failure, so the split cannot be undone silently.
  const views = report.views as BundleReport;
  console.log(`# views-react: menus ${menus.length} modules; views: ${views.error}`);
  expect(views.inputs).toBe(undefined);
  expect((views.error ?? "").includes('Could not resolve "react"')).toBe(true);
});
