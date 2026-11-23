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
