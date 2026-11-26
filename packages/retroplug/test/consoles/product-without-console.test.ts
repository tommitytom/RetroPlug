// A product without the Mesen package has no `nes` platform, and the built-in N8 package's real cart names it. The cart
// is dormant (docs/downloadable-packs-m5b-plan.md): the product's seed does not throw, and no lookup offers the cart.
// The lookups run in a node child, the shell's consoles module bundled against the packs-host product's tree (as
// test/graph/plugin-product.test.ts bundles the planner against the plugin's).
import { test, expect } from "@retroplug/testing";

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

test("a product without the console a real cart names seeds, and the cart is not offered", async () => {
  const script =
    'const fs = require("fs"), os = require("os"), path = require("path");' +
    'const buildDir = fs.mkdtempSync(path.join(os.tmpdir(), "rp-nocart-"));' +
    'require("../../packages/retroplug/scripts/lib/packages.mjs")' +
    '.generate({ root: path.resolve("../../"), product: "packs-host", buildDir });' +
    'const product = path.join(buildDir, "products/packs-host/packages/retroplug");' +
    'const esbuild = require("esbuild");' +
    'fs.writeFileSync(path.join(buildDir, "entry.ts"), ' +
    '  `export { realCartOf, registeredRealCarts } from ${JSON.stringify(path.resolve("src/consoles.ts"))};`);' +
    'esbuild.build({ entryPoints: [path.join(buildDir, "entry.ts")], bundle: true, format: "esm", platform: "neutral",' +
    ' mainFields: ["module", "main"], target: "es2020",' +
    ' tsconfigRaw: require("../../tools/ts-options").tsOptions({ alwaysStrict: true }),' +
    ' outfile: path.join(buildDir, "consoles.mjs"), nodePaths: [path.resolve("node_modules")],' +
    ' plugins: [require("../../tools/product-generated").productGenerated({ dir: product })].filter(Boolean) })' +
    '.then(() => import(require("url").pathToFileURL(path.join(buildDir, "consoles.mjs")).href))' +
    '.then((m) => { const out = { nes: m.realCartOf("nes") === undefined,' +
    '  carts: m.registeredRealCarts().map((c) => c.platform) };' +
    '  fs.rmSync(buildDir, { recursive: true, force: true });' +
    '  process.stdout.write(JSON.stringify(out)); });';
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the packs-host lookups failed: ${err || out}`);
  const result = JSON.parse(out) as { nes: boolean; carts: string[] };
  console.log(`# packs-host real carts: ${result.carts.join(", ") || "(none)"}`);
  expect(result.nes).toBe(true);
  expect(result.carts.includes("nes")).toBe(false);
});
