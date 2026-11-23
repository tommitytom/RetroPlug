#!/usr/bin/env node
// Drives tools/build-shared-modules.mjs for test/script/shared-modules.test.ts: builds a set in memory and prints one
// JSON line describing it. The build is Node's (esbuild), so the txiki test spawns this, as bundle-protocol.test.ts
// spawns its driver.
//
//   node test/script/shared-modules.mjs <app|cli> [product]
import { mkdtempSync, realpathSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const [set, product] = process.argv.slice(2);
const REPO = resolve(process.cwd(), "../..");
const { buildSharedModules } = await import("../../../../tools/build-shared-modules.mjs");
let work;
let productDir;
if (product) {
  const { generate } = await import("../../scripts/lib/packages.mjs");
  work = realpathSync(mkdtempSync(join(tmpdir(), "rp-shared-")));
  generate({ root: REPO, product, buildDir: work });
  productDir = join(work, "products", product, "packages/retroplug");
}
try {
  const run = () => buildSharedModules({ set, outDir: join(tmpdir(), "rp-shared-unused"), productDir, write: false });
  const a = await run();
  const b = await run();
  // The outputs the set's first host reaches, by name: the ones whose class fields its pinned options used to compile.
  const byName = new Map([...a.shared, ...a.hosts].map((m) => [m.name, m]));
  const reached = new Set();
  const walk = (name) => {
    if (reached.has(name) || !byName.has(name)) return;
    reached.add(name);
    for (const imp of byName.get(name).imports) walk(imp);
  };
  walk(a.hosts[0].name);
  process.stdout.write(JSON.stringify({
    shared: a.shared.map((m) => ({ name: m.name, imports: m.imports, exports: m.exports })),
    hosts: a.hosts.map((m) => ({ name: m.name, imports: m.imports, exports: m.exports })),
    deterministic: JSON.stringify([a.shared, a.hosts].map((l) => l.map((m) => [m.name, m.text])))
      === JSON.stringify([b.shared, b.hosts].map((l) => l.map((m) => [m.name, m.text]))),
    closures: Object.fromEntries(Object.entries(a.closures).map(([k, v]) => [k, v.map((p) => p.split("\\").join("/"))])),
    // A CALL of the helper, not its definition: esbuild's shared runtime-helper chunk defines __publicField beside
    // __toESM, and the control plane imports that chunk for __toESM.
    firstHostPublicField: [...reached].filter((name) => /\b__publicField\(/.test(byName.get(name).text)),
  }));
} finally {
  if (work) rmSync(work, { recursive: true, force: true });
}
