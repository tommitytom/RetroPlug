// The script-node bundle holds no zod (docs/modular-graph-phase-b3b-design.md section 3). It is embedded in every
// Engine and loaded into both of its script runtimes, the audio thread's among them, so what it imports costs every
// host twice: B3a's bundle was 660 KB, about 567 KB of it zod, which came in with the role registry the kernel node
// built. The nodes take their settings through graph/settingsSchema.ts instead, and the app's zod schemas (the role
// configs, the project files, the controller apps') stay on the control plane.
//
// The bundle is built here as packages/native/cmake/bundles.cmake builds it, through tools/build-session.js with its
// own options, in memory, and esbuild's metafile says which modules went in: a module from zod among them fails the
// test, and says which. A size ceiling backs the path check up for what it cannot see (zod arriving pre-inlined in
// another package's dist leaves no zod path in the inputs). The build is Node's (esbuild is not a txiki module), so
// the test runs it in a node child: the runner's own node (RP_NODE, which scripts/run-tests.mjs sets), else the node
// on the PATH when the test is run by hand. This catches zod and the like arriving; it does not prove what the
// script host can run.
import { test, expect } from "../../testing/harness";

// Just enough of txiki's spawn and a child's pipe, declared locally as cli/childSession.ts declares them.
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

// The script-node bundle's inputs and size, built as the cmake target builds it (an iife; the suite runs from
// packages/retroplug).
async function scriptBundle(): Promise<{ inputs: string[]; bytes: number }> {
  const script =
    'const r = require("../../tools/build-session.js").bundleInputs("src/script/bundle.ts", "iife");' +
    "process.stdout.write(JSON.stringify(r));";
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`bundling src/script/bundle.ts failed: ${err}`);
  return JSON.parse(out) as { inputs: string[]; bytes: number };
}

test("the script-node bundle imports no zod module", async () => {
  const { inputs, bytes } = await scriptBundle();
  console.log(`# script-node bundle: ${inputs.length} modules, ${bytes} bytes`);
  // B3a's bundle was 660 KB with zod; this one is about 78 KB. A loose ceiling, for zod inlined where no path says so.
  expect(bytes < 200 * 1024, `the bundle is ${bytes} bytes, past 200 KB`).toBe(true);
  // Every production node type is in it, so an entry that bundled nothing cannot pass.
  for (const node of ["midiRoute", "mgbMidi", "n8MidiToFifo", "risaSync", "smsSync", "lsdjSync", "controllerSession"])
    expect(inputs.includes(`src/script/nodes/${node}.ts`), node).toBe(true);
  // zod from node_modules, however the package manager lays it out (node_modules/zod/..., .pnpm/zod@x/...).
  const zod = inputs.filter((path) => /(^|[\\/])zod(@[^\\/]*)?[\\/]/.test(path));
  expect(zod.length, `${zod.length} zod modules in the bundle, the first ${zod[0]}`).toBe(0);
  // Nor anything of the app's that is built on it: the zod config helpers or a role registration.
  expect(inputs.filter((path) => /src\/(configSchema|systemRoles|dspRoles|coreRoles|projectConfig)\.ts$/.test(path))).toEqual([]);
});
