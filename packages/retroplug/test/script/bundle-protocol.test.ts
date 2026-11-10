// The protocol tables are import-free (docs/modular-graph-phase-f1-design.md section 3): buildGraph, both stores and a
// DirectGraph read the wiring with or without a role registry, and a bundle of the planner must not pull in a node's
// code, a role's zod schema or the app. The tables are generated into src/generated/protocol.ts, a module that imports
// nothing but a type, and graph/protocolNodes.ts reads only that one (src/generated/packages.ts, which imports every
// package's app module, is where they used to live and must not be in this graph).
//
// The graph placers are the same rule (docs/modular-graph-phase-f5a-design.md section 6.3): buildGraph reads
// src/generated/placers.ts, which imports one module per declaring package, so that module must carry types and
// constants alone - no node module, no schema - and the same bundle is held to it here.
//
// The bundle is built here as the build builds it, in memory through tools/build-session.js, and esbuild's metafile
// says which modules went in: a zod module, the app or the generated app registries among them fails the test, and
// says which. The build is Node's (esbuild is not a txiki module), so the test runs it in a node child, as
// test/script/bundle-no-zod.test.ts does.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-no-zod.test.ts declares them.
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

/** One entry's inputs and size, bundled as the build bundles a session (the suite runs from packages/retroplug). */
async function bundle(entry: string): Promise<{ inputs: string[]; bytes: number }> {
  const script =
    `const r = require("../../tools/build-session.js").bundleInputs(${JSON.stringify(entry)}, "esm");` +
    "process.stdout.write(JSON.stringify(r));";
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`bundling ${entry} failed: ${err}`);
  return JSON.parse(out) as { inputs: string[]; bytes: number };
}

/** The repo-relative paths of every module that defines protocol nodes: each listed manifest's `protocol.module`, and
 *  core's `defineNode` helper, which a node module reaches. Read from the manifests, so a package added to the product
 *  has its node module refused here without editing this test. */
async function nodeModules(): Promise<string[]> {
  const script =
    'const fs = require("fs"), path = require("path");' +
    'const ids = JSON.parse(fs.readFileSync("../../package.json", "utf8")).retroplug.products.desktop;' +
    'const mods = [path.relative(process.cwd(), path.resolve("../../packages/core/ts/defineNode.ts"))];' +
    'for (const id of ids) {' +
    '  const manifest = JSON.parse(fs.readFileSync(path.join("../../packages", id, "package.json"), "utf8"));' +
    '  const protocol = manifest.retroplug && manifest.retroplug.protocol;' +
    '  if (protocol && typeof protocol.module === "string") {' +
    '    mods.push(path.relative(process.cwd(), path.resolve("../../packages", id, protocol.module)));' +
    '  }' +
    '}' +
    'process.stdout.write(JSON.stringify(mods));';
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`reading the node modules failed: ${err}`);
  return JSON.parse(out) as string[];
}

/** The modules a bundle of the protocol data may not carry: zod, the app, the generated registries importing it, and
 *  every module that defines a protocol node (its code, not only the table describing it). */
function forbidden(inputs: readonly string[], nodeModules: readonly string[]): string[] {
  const norm = (path: string) => path.replace(/\\/g, "/");
  return inputs.filter((raw) => {
    const path = norm(raw);
    return (
      /(^|\/)zod(@[^/]*)?\//.test(path) ||
      /src\/app\.ts$/.test(path) ||
      /src\/appHost\.ts$/.test(path) ||
      /src\/generated\/packages\.ts$/.test(path) ||
      /src\/(configSchema|systemRoles)\.ts$/.test(path) ||
      nodeModules.some((modulePath) => path === modulePath || path.endsWith(`/${modulePath}`))
    );
  });
}

test("graph/protocolNodes.ts reads the generated protocol data and nothing else", async () => {
  const { inputs, bytes } = await bundle("src/graph/protocolNodes.ts");
  console.log(`# protocolNodes.ts: ${inputs.length} modules, ${bytes} bytes: ${inputs.join(", ")}`);
  expect(inputs.includes("src/generated/protocol.ts")).toBe(true);
  // The module and the data it reads: the tables are apart from the node modules that describe them.
  expect(inputs.length).toBe(2);
  expect(forbidden(inputs, await nodeModules())).toEqual([]);
});

test("graph/buildGraph.ts carries the protocol tables but neither zod nor the app", async () => {
  const { inputs, bytes } = await bundle("src/graph/buildGraph.ts");
  console.log(`# buildGraph.ts: ${inputs.length} modules, ${bytes} bytes`);
  expect(inputs.includes("src/generated/protocol.ts")).toBe(true);
  // The positive control the placers need: the generated placer table IS carried, so the case is not vacuous.
  expect(inputs.includes("src/generated/placers.ts")).toBe(true);
  // The planner, its own helpers, the consoles' descriptors it reads, core's placer types, the one package placer the
  // product ships and that package's node-type constants: 22 modules today, and not one of them is a node module. The
  // ceiling is the count plus one, so a whole package's app or a node module dragged in by a placer fails by NAME
  // (forbidden, below) and a quiet arrival of anything else fails by count.
  expect(inputs.length <= 22, `${inputs.length} modules went in`).toBe(true);
  expect(bytes < 64 * 1024, `the bundle is ${bytes} bytes`).toBe(true);
  expect(forbidden(inputs, await nodeModules())).toEqual([]);
});
