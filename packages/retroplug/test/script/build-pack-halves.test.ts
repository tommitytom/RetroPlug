// tools/build-pack-halves.mjs (docs/downloadable-packs-m2c-plan.md): a package's halves built as a pack's, each one ES
// module importing only contract modules, and a package importing another package's module refused.
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

async function runNode(script: string): Promise<{ status: number; out: string; err: string }> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, s] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { status: s.exit_status, out, err };
}

test("the fixture's halves are built, each importing only contracts, and its pack.json written", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-halves-"));',
    'const fixture = path.join(repo, "packages/native/test/packs/ts-fixture");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: fixture, outDir: work, template: path.join(fixture, "pack.json") });',
    '  const imports = {};',
    '  for (const half of Object.keys(pack.halves)) {',
    '    const src = fs.readFileSync(path.join(work, pack.halves[half]), "utf8");',
    '    imports[half] = [...src.matchAll(/^import[^"\']*["\']([^"\']+)["\']/gm)].map((x) => x[1]);',
    '  }',
    '  const contracts = m.packExternals();',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '  process.stdout.write(JSON.stringify({ pack, imports, contracts }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const { pack, imports, contracts } = JSON.parse(run.out) as {
    pack: { apiLevel: number; halves: Record<string, string> };
    imports: Record<string, string[]>;
    contracts: string[];
  };
  expect(pack.halves).toEqual({ app: "js/app.js", ui: "js/ui.js", cli: "js/cli.js" });
  expect(pack.apiLevel > 0).toBe(true);
  for (const [half, list] of Object.entries(imports)) {
    for (const name of list) {
      if (!contracts.includes(name)) throw new Error(`${half} imports ${name}, which is not a contract`);
    }
  }
  expect(imports.app).toEqual(["@retroplug/core/definePackage"]);
});

test("Mesen's app half imports zod from the host rather than bundling it", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-mesen-"));',
    'const mesen = path.join(repo, "packages/mesen");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  await m.buildPackHalves({ packageDir: mesen, outDir: work, template: path.join(mesen, "pack.json") });',
    '  const src = fs.readFileSync(path.join(work, "js/app.js"), "utf8");',
    '  process.stdout.write(JSON.stringify({ bytes: Buffer.byteLength(src),',
    '    imports: /^import[^"\']*["\']zod["\']/m.test(src), bundled: /class ZodError|\$ZodError/.test(src) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const got = JSON.parse(run.out) as { bytes: number; imports: boolean; bundled: boolean };
  expect(got.imports).toBe(true);
  expect(got.bundled).toBe(false);
  expect(got.bytes < 64 * 1024, `app.js is ${got.bytes} bytes`).toBe(true);
});

test("a package importing another package's module is refused, naming it", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-halves-"));',
    'fs.mkdirSync(path.join(work, "pkg/ts"), { recursive: true });',
    'fs.writeFileSync(path.join(work, "pkg/package.json"), JSON.stringify({ retroplug: { app: "ts/app.ts" } }));',
    'fs.writeFileSync(path.join(work, "pkg/ts/app.ts"), \'import app from "@retroplug/sameboy/app"; export default app;\');',
    'fs.writeFileSync(path.join(work, "pack.json"), JSON.stringify({ id: "x", halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href)',
    '  .then((m) => m.buildPackHalves({ packageDir: path.join(work, "pkg"), outDir: path.join(work, "out"),',
    '    template: path.join(work, "pack.json") }))',
    '  .then(() => { process.stdout.write("built"); }, (e) => { process.stdout.write(String(e.message)); })',
    '  .finally(() => fs.rmSync(work, { recursive: true, force: true }));',
  ].join("\n");
  const run = await runNode(script);
  expect(run.out.includes("@retroplug/sameboy/app, which is not a contract module")).toBe(true);
});

// Runs the tool's CLI from the repository root (the cases above run from the package, two levels down) and prints a
// JSON record of the exit status, the streams and, when it wrote one, the pack's files.
function toolScript(template: string | null, only: string): string {
  return [
    'const fs = require("fs"), os = require("os"), path = require("path"), cp = require("child_process");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-script-test-"));',
    `let template = ${template === null ? 'null' : JSON.stringify(template)};`,
    'if (template === null) {',
    '  template = path.join(work, "template.json");',
    '  fs.writeFileSync(template, JSON.stringify({ id: "bliptoaster", name: "x", version: "1.0.0", apiLevel: 0,',
    '    license: "MIT", source: "x", requires: { packs: [], platforms: [] },',
    '    provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {} }));',
    '} else { template = path.join(repo, template); }',
    'const out = path.join(work, "v");',
    'const r = cp.spawnSync(process.execPath, [path.join(repo, "tools/build-pack-halves.mjs"),',
    `  path.join(repo, "packages/bliptoaster"), out, template, "--only", ${JSON.stringify(only)}], { encoding: "utf8", cwd: repo });`,
    'const read = (f) => { try { return fs.readFileSync(path.join(out, f), "utf8"); } catch (e) { return null; } };',
    'const rec = { status: r.status, stderr: r.stderr, pack: read("pack.json"), whole: read("js/script.js"),',
    '  runtime: read("js/script.device.js") };',
    'fs.rmSync(work, { recursive: true, force: true });',
    'process.stdout.write(JSON.stringify(rec));',
  ].join("\n");
}

interface ToolRun {
  status: number;
  stderr: string;
  pack: string | null;
  whole: string | null;
  runtime: string | null;
}

test("a package's device script pack becomes the pack's script half: the two ES5 bundles and the script member", async () => {
  const run = await runNode(toolScript("packages/native/test/packs/script-fixture/bliptoaster.pack.json", "script"));
  if (run.status !== 0) throw new Error(run.err);
  const rec = JSON.parse(run.out) as ToolRun;
  expect(rec.status, rec.stderr).toBe(0);
  const pack = JSON.parse(rec.pack as string);
  expect(pack.halves).toEqual({ script: "js/script.js" });
  expect(pack.script).toEqual({ runtime: "js/script.device.js", heapBytes: 24576, nodeTypes: ["rp.bliptoaster.midi-policy"] });
  // The bundles are the toolchain's ES5: no module syntax, the strict directive first.
  const whole = rec.whole as string;
  expect(whole.startsWith('"use strict"')).toBe(true);
  expect(/\bexport\b|\bimport\b/.test(whole)).toBe(false);
  expect((rec.runtime as string).length).toBeGreaterThan(0);
});

test("a script half whose types the template does not declare is refused, naming the type", async () => {
  const run = await runNode(toolScript(null, "script"));
  if (run.status !== 0) throw new Error(run.err);
  const rec = JSON.parse(run.out) as ToolRun;
  expect(rec.status).toBe(1);
  expect(rec.stderr.includes(
    "the script half defines rp.bliptoaster.midi-policy, which the template's provides.nodeTypes does not declare")).toBe(true);
});

test("a package's protocol member is built as a pack's protocol and placer halves", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-protocol-"));',
    'const fixture = path.join(repo, "packages/native/test/packs/ts-fixture/protocol");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: fixture, outDir: work, template: path.join(fixture, "pack.json") });',
    '  const bundle = fs.readFileSync(path.join(work, "js/protocol.js"), "utf8");',
    '  const ctx = vm.createContext({});',
    '  vm.runInContext(bundle, ctx);',
    '  console.log(JSON.stringify({ halves: pack.halves, protocol: pack.protocol,',
    '    registered: ctx.__rp_scriptNodes.map((n) => n.id), imports: /^\\s*import\\b/m.test(bundle) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.halves).toEqual({ app: "js/app.js", protocol: "js/protocol.js", placer: "js/placer.js" });
  expect(got.protocol).toEqual({
    nodes: [{ type: "rp.tsfix-proto.echo", midiIn: "in", writes: [], midiOut: true }],
    roles: { "tsfix-proto": "rp.tsfix-proto.echo" },
  });
  expect(got.registered).toEqual(["rp.tsfix-proto.echo"]);
  expect(got.imports).toBe(false);
});

test("risa's real protocol half builds, its labels dropped, and registers its node", async () => {
  // The real package (docs/downloadable-packs-m2e-plan.md): what part 6 will ship as a pack. Built alone (--only), from
  // a template that declares its node.
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-risa-"));',
    'const risa = path.join(repo, "packages/risa");',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "risa", name: "risa", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] },',
    '  provides: { platforms: [], nodeTypes: ["rp.risa.sync"], roles: ["risa-sync"] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = path.join(work, "out");',
    '  const pack = await m.buildPackHalves({ packageDir: risa, outDir: out, template, only: ["protocol"] });',
    '  const ctx = vm.createContext({});',
    '  vm.runInContext(fs.readFileSync(path.join(out, "js/protocol.js"), "utf8"), ctx);',
    '  const manifest = JSON.parse(fs.readFileSync(path.join(risa, "package.json"), "utf8")).retroplug.protocol;',
    '  console.log(JSON.stringify({ protocol: pack.protocol, manifest, registered: ctx.__rp_scriptNodes.map((n) => n.id) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.protocol.nodes).toEqual(got.manifest.nodes.map(({ label: _label, ...rest }: Record<string, unknown>) => rest));
  expect(got.protocol.roles).toEqual(got.manifest.roles);
  expect(got.registered).toEqual(["rp.risa.sync"]);
});

test("a protocol module importing another package is refused", async () => {
  // A scratch package: its node module imports @retroplug/sameboy, which a protocol half may not carry; then zod and an
  // unexported core subpath.
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-protocol-bad-"));',
    'const pkg = path.join(work, "pkg");',
    'fs.mkdirSync(path.join(pkg, "ts"), { recursive: true });',
    'fs.writeFileSync(path.join(pkg, "ts/nodes.ts"), "import { x } from \\"@retroplug/sameboy/cartridge\\";\\nexport default [x];\\n");',
    'const manifest = (extra) => ({ name: "bad", private: true, retroplug: { protocol: { module: "ts/nodes.ts",',
    '  nodes: [{ type: "rp.bad.n" }], roles: {}, ...extra } } });',
    'const template = path.join(work, "pack.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bad", name: "b", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: ["rp.bad.n"],',
    '  roles: [] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = {};',
    '  for (const [name, extra] of [["foreign", {}], ["zod", {}], ["subpath", {}]]) {',
    '    if (name === "zod") fs.writeFileSync(path.join(pkg, "ts/nodes.ts"), "import { z } from \\"zod\\";\\nexport default [z];\\n");',
    '    if (name === "subpath") fs.writeFileSync(path.join(pkg, "ts/nodes.ts"), "import { x } from \\"@retroplug/core/nope\\";\\nexport default [x];\\n");',
    '    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify(manifest(extra)));',
    '    try { await m.buildPackHalves({ packageDir: pkg, outDir: path.join(work, name), template }); out[name] = "built"; }',
    '    catch (e) { out[name] = e.message; }',
    '  }',
    '  console.log(JSON.stringify(out));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '});',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.foreign.includes("imports @retroplug/sameboy/cartridge: a protocol half bundles @retroplug/core alone"),
    got.foreign).toBe(true);
  expect(got.zod.includes("imports zod: a protocol half's nodes run in the script contexts, which carry no schema library"),
    got.zod).toBe(true);
  expect(got.subpath.includes("@retroplug/core/nope is not a module @retroplug/core exports"), got.subpath).toBe(true);
});

// A recording registrar a kinds bundle runs against, in a bare vm context: no process, and console absent.
const RECORD = [
  'const calls = [];',
  'const ctx = vm.createContext({ __rp_registerKindItems(kind, items, ids) {',
  '  calls.push({ kind, ids, itemIds: items.map((i) => i.id) });',
  '} });',
].join("\n");

test("a package's kind items are built as a pack's kinds half and kinds member", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-kinds-"));',
    'const fixture = path.join(repo, "packages/native/test/packs/ts-fixture/kinds");',
    RECORD,
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: fixture, outDir: work, template: path.join(fixture, "pack.json") });',
    '  const bundle = fs.readFileSync(path.join(work, "js/kinds.js"), "utf8");',
    '  vm.runInContext(bundle, ctx);',
    '  console.log(JSON.stringify({ halves: pack.halves, kinds: pack.kinds, calls, imports: /^\\s*import\\b/m.test(bundle) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.halves).toEqual({ app: "js/app.js", kinds: "js/kinds.js" });
  expect(got.kinds).toEqual({ controllerApps: [{ id: "tsfix-app" }] });
  expect(got.calls).toEqual([{ kind: "controllerApps", ids: ["tsfix-app"], itemIds: ["tsfix-app"] }]);
  expect(got.imports).toBe(false);
});

test("lsdj-launchpad's real kind items build as a pack's, with their data and their runtime", async () => {
  // The real package, built from a temp template with only its kinds half (its app half imports other packages, which
  // the app half bundler refuses). Its runtime imports @retroplug/lsdj/playback/predict, which the bundle must carry,
  // and the bundle runs in a context with no process and no console.
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-lsdj-kinds-"));',
    'const pkg = path.join(repo, "packages/lsdj-launchpad");',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "lsdj-launchpad", name: "l", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {} }));',
    RECORD,
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = path.join(work, "out");',
    '  const pack = await m.buildPackHalves({ packageDir: pkg, outDir: out, template, only: ["kinds"] });',
    '  const bundle = fs.readFileSync(path.join(out, "js/kinds.js"), "utf8");',
    '  vm.runInContext(bundle, ctx);',
    '  const manifest = JSON.parse(fs.readFileSync(path.join(pkg, "package.json"), "utf8")).retroplug.protocol.kinds;',
    '  console.log(JSON.stringify({ halves: Object.keys(pack.halves), kinds: pack.kinds, manifest, calls, ownRegistry: bundle.includes("registerControllerApps") }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.ownRegistry).toBe(false);
  expect(got.halves).toEqual(["kinds"]);
  const dropModule = (entries: Record<string, unknown>[]) => entries.map(({ module: _m, ...rest }) => rest);
  expect(got.kinds).toEqual({ controllerApps: dropModule(got.manifest.controllerApps) });
  expect(got.calls).toEqual([{ kind: "controllerApps", ids: ["lsdj-midimap"], itemIds: ["lsdj-midimap"] }]);
});

test("a kinds half importing a schema or UI library, or a kind's own registry, is refused", async () => {
  const SPECS = ["zod", "react", "react/jsx-runtime", "lvgljs", "lvgljs-ui", "@retroplug/launchpad/apps"];
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-kinds-bad-"));',
    'const pkg = path.join(work, "pkg");',
    'fs.mkdirSync(path.join(pkg, "ts"), { recursive: true });',
    'fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "bad", private: true, retroplug: {',
    '  protocol: { kinds: { controllerApps: [{ id: "bad-app", module: "ts/rt.ts" }] } } } }));',
    'const template = path.join(work, "pack.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bad", name: "b", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = {};',
    `  for (const spec of ${JSON.stringify(SPECS)}) {`,
    '    fs.writeFileSync(path.join(pkg, "ts/rt.ts"), "import { x } from \\"" + spec + "\\";\\nexport default { id: \\"bad-app\\", x };\\n");',
    '    try { await m.buildPackHalves({ packageDir: pkg, outDir: path.join(work, spec.replace(/\\W/g, "_")), template }); out[spec] = "built"; }',
    '    catch (e) { out[spec] = e.message; }',
    '  }',
    '  console.log(JSON.stringify(out));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '});',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  for (const spec of SPECS.slice(0, 5)) {
    expect(got[spec].includes(`imports ${spec}: a kinds half runs in the script contexts, which carry no schema or UI library`),
      got[spec]).toBe(true);
  }
  expect(got["@retroplug/launchpad/apps"].includes("imports @retroplug/launchpad/apps: a kinds half registers its items " +
    "through __rp_registerKindItems, never the kind's own registry"), got["@retroplug/launchpad/apps"]).toBe(true);
});

test("a kinds entry with no id or no module is refused", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-kinds-entry-"));',
    'const pkg = path.join(work, "pkg");',
    'fs.mkdirSync(pkg, { recursive: true });',
    'const template = path.join(work, "pack.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bad", name: "b", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = {};',
    '  for (const [name, entry] of [["noId", { module: "ts/rt.ts" }], ["noModule", { id: "bad-app" }]]) {',
    '    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "bad", private: true, retroplug: {',
    '      protocol: { kinds: { controllerApps: [entry] } } } }));',
    '    try { await m.buildPackHalves({ packageDir: pkg, outDir: path.join(work, name), template }); out[name] = "built"; }',
    '    catch (e) { out[name] = e.message; }',
    '  }',
    '  console.log(JSON.stringify(out));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '});',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.noId).toBe("a retroplug.protocol.kinds.controllerApps entry has no id");
  expect(got.noModule).toBe("retroplug.protocol.kinds.controllerApps: bad-app has no module");
});

test("a package declaring a contribution kind of its own, and a runtime reaching a kind's registry through another module, are refused", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-kinds-own-"));',
    'const pkg = path.join(work, "pkg");',
    'fs.mkdirSync(path.join(pkg, "ts"), { recursive: true });',
    'const template = path.join(work, "pack.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bad", name: "b", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: [], roles: [] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = {};',
    '  const run = async (name, retroplug) => {',
    '    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "bad", private: true, retroplug }));',
    '    try { await m.buildPackHalves({ packageDir: pkg, outDir: path.join(work, name), template }); out[name] = "built"; }',
    '    catch (e) { out[name] = e.message; }',
    '  };',
    '  await run("own", { kinds: { thing: {} } });',
    '  fs.writeFileSync(path.join(pkg, "ts/rt.ts"), "import { controllerSessionNode as x } from \\"@retroplug/launchpad/controllerSession\\";\\nexport default { id: \\"bad-app\\", x };\\n");',
    '  await run("transitive", { protocol: { kinds: { controllerApps: [{ id: "bad-app", module: "ts/rt.ts" }] } } });',
    '  console.log(JSON.stringify(out));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '});',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.own).toBe("retroplug.kinds: a pack cannot declare a contribution kind of its own (docs/downloadable-packs-m2f-plan.md)");
  expect(got.transitive.includes("rt.ts imports a kind's own registry module: a kinds half registers its items through " +
    "__rp_registerKindItems, never the kind's own registry"), got.transitive).toBe(true);
});

test("BlipToaster's app, ui and cli halves bundle the tracker modules PACK_BUNDLED lists and import only contracts", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-bt-"));',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bliptoaster", name: "x", version: "0.1.0", apiLevel: 0,',
    '  license: "MIT", source: "x", requires: { packs: [], platforms: ["nes"] },',
    '  provides: { platforms: [], nodeTypes: [], roles: ["bliptoaster", "bliptoaster-assets"] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: path.join(repo, "packages/bliptoaster"), outDir: path.join(work, "v"),',
    '    template, only: ["app", "ui", "cli"] });',
    '  const imports = {}, src = {};',
    '  for (const half of Object.keys(pack.halves)) {',
    '    src[half] = fs.readFileSync(path.join(work, "v", pack.halves[half]), "utf8");',
    '    imports[half] = [...src[half].matchAll(/^import[^"\']*["\']([^"\']+)["\']/gm)].map((x) => x[1]);',
    '  }',
    '  const contracts = m.packExternals();',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '  process.stdout.write(JSON.stringify({ halves: pack.halves, imports, contracts,',
    '    findBundled: /function findMagicInRange\\b/.test(src.app), themeBundled: /function decodeThemeFromRom\\b/.test(src.ui) }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const got = JSON.parse(run.out) as { halves: Record<string, string>; imports: Record<string, string[]>; contracts: string[];
    findBundled: boolean; themeBundled: boolean };
  expect(got.halves).toEqual({ app: "js/app.js", ui: "js/ui.js", cli: "js/cli.js" });
  for (const [half, list] of Object.entries(got.imports)) {
    for (const name of list) {
      if (!got.contracts.includes(name)) throw new Error(`${half} imports ${name}, which is not a contract`);
    }
  }
  expect(got.findBundled).toBe(true);
  expect(got.themeBundled).toBe(true);
});

test("a half whose bundle reaches another package's file that PACK_BUNDLED does not list is refused, naming it", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-reach-"));',
    'fs.mkdirSync(path.join(work, "pkg/ts"), { recursive: true });',
    'fs.writeFileSync(path.join(work, "pkg/package.json"), JSON.stringify({ retroplug: { app: "ts/app.ts" } }));',
    'const font = path.join(repo, "packages/tracker/ts/nes/font.ts");',
    'fs.writeFileSync(path.join(work, "pkg/ts/app.ts"), `import * as f from ${JSON.stringify(font)}; export default { roles: [], f };`);',
    'fs.writeFileSync(path.join(work, "pack.json"), JSON.stringify({ id: "x", halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href)',
    '  .then((m) => m.buildPackHalves({ packageDir: path.join(work, "pkg"), outDir: path.join(work, "out"),',
    '    template: path.join(work, "pack.json") }))',
    '  .then(() => { process.stdout.write("built"); }, (e) => { process.stdout.write(String(e.message)); })',
    '  .finally(() => fs.rmSync(work, { recursive: true, force: true }));',
  ].join("\n");
  const run = await runNode(script);
  expect(run.out.includes("packages/tracker/ts/nes/font.ts"), run.out).toBe(true);
  expect(run.out.includes("ts/app.ts"), run.out).toBe(true);
  expect(run.out.includes("PACK_BUNDLED"), run.out).toBe(true);
});

test("PACK_BUNDLED names only modules their packages export, none of them a contract", async () => {
  const script = [
    'const path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then((m) => {',
    '  const files = m.bundledModuleFiles();',
    '  process.stdout.write(JSON.stringify({ specifiers: [...files.keys()], contracts: m.packExternals() }));',
    '}).catch((e) => { process.stderr.write(String(e && e.stack || e)); process.exit(1); });',
  ].join("\n");
  const run = await runNode(script);
  if (run.status !== 0) throw new Error(run.err);
  const got = JSON.parse(run.out) as { specifiers: string[]; contracts: string[] };
  expect(got.specifiers.length > 0).toBe(true);
  for (const s of got.specifiers) expect(got.contracts.includes(s), s).toBe(false);
});
