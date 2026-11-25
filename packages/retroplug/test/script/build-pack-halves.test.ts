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

test("a protocol module importing another package, or a kinds member, is refused", async () => {
  // A scratch package: its node module imports @retroplug/sameboy, which a protocol half may not carry; then the same
  // package with protocol.kinds.
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
    '  for (const [name, extra] of [["foreign", {}], ["kinds", { kinds: { controllerApps: [] } }], ["zod", {}], ["subpath", {}]]) {',
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
  expect(got.kinds).toBe("retroplug.protocol.kinds: a pack cannot contribute to a contribution kind yet");
});
