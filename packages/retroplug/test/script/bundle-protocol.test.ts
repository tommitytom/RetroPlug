// The protocol tables are import-free (docs/modular-graph-phase-f1-design.md section 3): buildGraph, both stores and a
// DirectGraph read the wiring with or without a role registry, and a bundle of the planner must not pull in a node's
// code, a role's zod schema or the app. The tables are generated into src/generated/protocol.ts, a module that imports
// nothing but a type, and graph/protocolNodes.ts reads only that one (src/generated/packages.ts, which imports every
// package's app module, is where they used to live and must not be in this graph).
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

/** The modules a bundle of the protocol data may not carry: zod, the app, and the generated registries importing it. */
function forbidden(inputs: readonly string[]): string[] {
  return inputs.filter(
    (path) =>
      /(^|[\\/])zod(@[^\\/]*)?[\\/]/.test(path) ||
      /src\/app\.ts$/.test(path) ||
      /src\/appHost\.ts$/.test(path) ||
      /src\/generated\/packages\.ts$/.test(path) ||
      /src\/(configSchema|systemRoles)\.ts$/.test(path),
  );
}

test("graph/protocolNodes.ts reads the generated protocol data and nothing else", async () => {
  const { inputs, bytes } = await bundle("src/graph/protocolNodes.ts");
  console.log(`# protocolNodes.ts: ${inputs.length} modules, ${bytes} bytes: ${inputs.join(", ")}`);
  expect(inputs.includes("src/generated/protocol.ts")).toBe(true);
  // The module and the data it reads: the tables are apart from the node modules that describe them.
  expect(inputs.length).toBe(2);
  expect(forbidden(inputs)).toEqual([]);
});

test("graph/buildGraph.ts carries the protocol tables but neither zod nor the app", async () => {
  const { inputs, bytes } = await bundle("src/graph/buildGraph.ts");
  console.log(`# buildGraph.ts: ${inputs.length} modules, ${bytes} bytes`);
  expect(inputs.includes("src/generated/protocol.ts")).toBe(true);
  // The planner and its own helpers: 8 modules today; the ceiling keeps a whole package's app out of a quiet arrival.
  expect(inputs.length < 20, `${inputs.length} modules went in`).toBe(true);
  expect(bytes < 64 * 1024, `the bundle is ${bytes} bytes`).toBe(true);
  expect(forbidden(inputs)).toEqual([]);
});
