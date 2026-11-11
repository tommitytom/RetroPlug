// A product without the Launchpad places no controller (docs/modular-graph-phase-f5a-design.md section 8, E2). The
// plugin's product list carries neither the pack nor its placer, so a project whose `controller` section is enabled -
// written by the standalone, or by hand; the plugin's own submenu cannot enable it - has a model no placer claims:
// buildGraph places no surface-in, no session and no surface-out, and applies no sync override, so the cart syncs as
// its own settings say. On the desktop the same model places all three and forces the LSDj cart to `midiMap`, which is
// the positive control: a case that passed by the model being unreadable would fail there.
//
// The plugin's plan runs in a node child: buildGraph imports the generated placer table, so the product's plan is the
// shell's buildGraph bundled against the plugin's tree (as its own bundles are, through tools/product-generated.js).
import { test, expect } from "@retroplug/testing";
import { buildGraph, type GraphModel } from "../../src/graph/buildGraph";
import { parseConfig } from "../../src/projectConfig";

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

/** The model both cases plan: one Game Boy running LSDj at the user's own MIDI-sync mode, and a controller enabled for
 *  it with the MI.MAP app (whose manifest sync override asks for `midiMap`). */
const MODEL = {
  systems: [
    {
      id: 1,
      platform: "gb",
      type: "rp.sameboy.gb",
      linkGroup: 0,
      exportMode: 0,
      resources: { rom: 1 },
      romName: "rom1",
      pipeline: [
        { kind: "lsdj-sync", node: "rp.lsdj.sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } },
      ],
    },
  ],
  routing: "stereo",
  shape: { kind: "multi", channels: 2 },
  midiRouting: "sendToAll",
  models: {
    controller: {
      app: "lsdj-midimap",
      target: "system",
      systemId: 0,
      appConfig: { quantise: "bar", follow: true, page: 0 },
      songRowTicks: [],
      anchor: null,
      cartSync: null,
    },
  },
};

function typesOf(model: unknown): { types: string[]; sync: { settings?: Record<string, unknown> } | null } {
  const built = buildGraph(model as GraphModel);
  const sync = built.document.nodes.find((node) => node.id === "sys-1/lsdj-sync");
  return {
    types: built.document.nodes.map((node) => node.type),
    sync: (sync as { settings?: Record<string, unknown> }) ?? null,
  };
}

test("the desktop places the controller for that model and forces the cart's sync mode", () => {
  // The positive control: the same model, the desktop's placers.
  const { types, sync } = typesOf(MODEL);
  expect(types.includes("rp.launchpad.surface-in")).toBe(true);
  expect(types.includes("rp.controller.session")).toBe(true);
  expect(types.includes("rp.launchpad.surface-out")).toBe(true);
  expect(sync?.settings?.mode).toBe("midiMap");
});

test("the plugin places no controller and no surface, and leaves the cart at the user's mode", async () => {
  const script =
    'const fs = require("fs"), os = require("os"), path = require("path");' +
    'const buildDir = fs.mkdtempSync(path.join(os.tmpdir(), "rp-plugin-plan-"));' +
    'require("../../packages/retroplug/scripts/lib/packages.mjs")' +
    '.generate({ root: path.resolve("../../"), product: "plugin", buildDir });' +
    'const product = path.join(buildDir, "products/plugin/packages/retroplug");' +
    'const esbuild = require("esbuild");' +
    'esbuild.build({ entryPoints: ["src/graph/buildGraph.ts"], bundle: true, format: "esm", platform: "neutral",' +
    ' mainFields: ["module", "main"], target: "es2020",' +
    ' tsconfigRaw: require("../../tools/ts-options").tsOptions({ alwaysStrict: true }),' +
    ' outfile: path.join(buildDir, "buildGraph.mjs"), nodePaths: [path.resolve("node_modules")],' +
    ' plugins: [require("../../tools/product-generated").productGenerated({ dir: product })].filter(Boolean) })' +
    '.then(() => import(require("url").pathToFileURL(path.join(buildDir, "buildGraph.mjs")).href))' +
    `.then((m) => { const built = m.buildGraph(${JSON.stringify(MODEL)});` +
    '  const sync = built.document.nodes.find((n) => n.id === "sys-1/lsdj-sync");' +
    '  fs.rmSync(buildDir, { recursive: true, force: true });' +
    '  process.stdout.write(JSON.stringify({ types: built.document.nodes.map((n) => n.type),' +
    '    sync: sync ? sync.settings ?? null : null })); });';
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the plugin's plan failed: ${err || out}`);
  const plan = JSON.parse(out) as { types: string[]; sync: Record<string, unknown> | null };
  console.log(`# the plugin's plan: ${plan.types.join(", ")}; the cart's mode: ${String(plan.sync?.mode)}`);
  const placed = plan.types.filter((type) => type.startsWith("rp.controller.") || type.startsWith("rp.launchpad."));
  expect(placed).toEqual([]);
  // The cart follows the user's own setting, which the controller's override does not touch.
  expect(plan.sync?.mode).toBe("midiSync");
});

test("a fresh plugin project writes no controller block, and one already carrying it keeps it byte for byte", async () => {
  // E8 and E5 in the product without the contributor (docs/modular-graph-phase-f5a-design.md section 8): the
  // `controller` section is a package's, so a product that carries no package contributing it has no default block to
  // write, and a block a project already carries is kept verbatim - never stripped, never rewritten, so a project
  // that goes back to the standalone loses nothing.
  const raw = {
    schemaVersion: "5",
    settings: {
      // A valid core value, so the whole settings block can be compared: a core field the schema clamps ("tile" is no
      // layout) would move for a reason that is not the section's.
      layout: "grid",
      midiRouting: "sendToAll",
      audioRouting: "stereo",
      zoom: 4,
      controller: {
        enabled: true,
        app: "lsdj-midimap",
        target: "system",
        systemId: 2,
        appConfig: { quantise: "beat" },
      },
      futureSetting: { knob: 3 },
      // The standalone's MIDI ports, saved with the project (docs/modular-graph-phase-f5b-design.md E9): the plugin
      // lists no `midi` package, so this is a key it has no contributor for and keeps as it came.
      midi: { inputs: [{ name: "Arturia KeyStep 32" }], allInputs: false, outputs: [{ name: "Deluge" }], clockBpm: 133 },
    },
    systems: [],
  };
  // What the desktop parses the same project to, so the plugin's kept block can be compared with it too (a key
  // stripped, or reordered, would differ); and the fresh project a product with no contributor writes.
  const desktop = parseConfig(JSON.stringify(raw));
  const script =
    'const fs = require("fs"), os = require("os"), path = require("path");' +
    'const buildDir = fs.mkdtempSync(path.join(os.tmpdir(), "rp-plugin-project-"));' +
    'require("../../packages/retroplug/scripts/lib/packages.mjs")' +
    '.generate({ root: path.resolve("../../"), product: "plugin", buildDir });' +
    'const product = path.join(buildDir, "products/plugin/packages/retroplug");' +
    'require("esbuild").build({ entryPoints: ["src/projectConfig.ts"], bundle: true, format: "esm",' +
    ' platform: "neutral", mainFields: ["module", "main"], target: "es2020",' +
    ' tsconfigRaw: require("../../tools/ts-options").tsOptions({ alwaysStrict: true }),' +
    ' outfile: path.join(buildDir, "projectConfig.mjs"), nodePaths: [path.resolve("node_modules")],' +
    ' plugins: [require("../../tools/product-generated").productGenerated({ dir: product })].filter(Boolean) })' +
    '.then(() => import(require("url").pathToFileURL(path.join(buildDir, "projectConfig.mjs")).href))' +
    `.then((m) => { const raw = ${JSON.stringify(JSON.stringify(raw))};` +
    '  const parsed = m.parseConfig(raw);' +
    '  const written = m.serializeConfig(m.buildConfig(parsed.settings, parsed.systems));' +
    '  const fresh = JSON.stringify(m.buildConfig(m.DEFAULT_SETTINGS, []));' +
    '  fs.rmSync(buildDir, { recursive: true, force: true });' +
    '  process.stdout.write(JSON.stringify({ sections: Object.keys(m.defaultSections()),' +
    '    carries: Object.prototype.hasOwnProperty.call(m.DEFAULT_SETTINGS.sections, "controller"),' +
    '    written, fresh })); });';
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the plugin's project failed: ${err || out}`);
  const project = JSON.parse(out) as { sections: string[]; carries: boolean; written: string; fresh: string };
  console.log(`# the plugin's project sections: ${project.sections.join(", ") || "(none)"}`);
  expect(project.sections).toEqual([]);
  // E8: a FRESH project the plugin writes carries no `controller` key at all: no package in its list contributes the
  // section, so there is no default block for it to write.
  expect(project.carries).toBe(false);
  const fresh = JSON.parse(project.fresh) as { settings: Record<string, unknown> };
  expect(Object.prototype.hasOwnProperty.call(fresh.settings, "controller")).toBe(false);
  // E5: the project that carries one is written back byte for byte as the file had it, which is the whole of "kept":
  // the comparison is against the INPUT block, not against another build's re-serialisation.
  const written = JSON.parse(project.written) as { settings: Record<string, unknown> };
  expect(written.settings.controller).toEqual(raw.settings.controller);
  // E9: the `midi` block a standalone wrote goes through the plugin verbatim, like the controller's.
  expect(written.settings.midi).toEqual(raw.settings.midi);
  expect(written.settings).toEqual(raw.settings);
  // And the desktop reads the same block to the same thing, so "kept" is not the plugin's own idea of it.
  expect(desktop.settings.sections.controller).toEqual(raw.settings.controller);
});
