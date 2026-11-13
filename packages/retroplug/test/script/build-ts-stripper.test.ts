// The TypeScript type stripper's bundle is built by CMake from whatever directory the build runs in
// (packages/native/CMakeLists.txt's retroplug-ts-stripper-bundle), and a build directory need not be inside the
// repository: `cmake -S <repo> -B /tmp/rpbuild` is a normal configure. esbuild resolves an alias's value, and every
// bare import, from its absWorkingDir, which defaults to the process's own directory, so without one the typescript-5
// alias has no node_modules above it there and the build stops. The stub and the generated entry are build products of
// this script too, so they belong beside the output file rather than in the repository's build/native whatever the
// output path is.
//
// The build is Node's (esbuild is not a txiki module), so this runs it in a node child, which chdirs to a fresh
// directory outside the repository before importing the script, as bundle-protocol.test.ts runs its builds.
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

/** What one run reports: the bundle's size and whether it defines the one global, whether the stub and the entry
 *  landed beside it, and the two files' stamps in the repository's build/native before and after (null when absent). */
interface Run {
  work: string;
  bytes: number;
  defines: boolean;
  stub: boolean;
  entry: boolean;
  before: { stub: string | null; entry: string | null };
  after: { stub: string | null; entry: string | null };
}

/** Run tools/build-ts-stripper.mjs once from a fresh directory outside the repository, writing its output there. */
async function runOutsideRepo(): Promise<{ run: Run | null; err: string; status: number }> {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    // The suite runs from packages/retroplug; the repository is two levels up.
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-stripper-"));',
    'const out = path.join(work, "out", "stripper.js");',
    'const stamp = (name) => { try { const s = fs.statSync(path.join(repo, "build/native", name));',
    '  return s.size + ":" + s.mtimeMs; } catch { return null; } };',
    'const snap = () => ({ stub: stamp("ts-stripper-stub.js"), entry: stamp("ts-stripper-entry.mjs") });',
    "const before = snap();",
    // cwd is what esbuild's default absWorkingDir is, and the script is outside it on purpose.
    "process.chdir(work);",
    'const real = path.join(repo, "tools/build-ts-stripper.mjs");',
    // The output path is the script's only argument.
    "process.argv = [process.argv[0], real, out];",
    // The script's own progress line is not this driver's output, which is the JSON below.
    "console.log = () => {};",
    "import(url.pathToFileURL(real).href).then(() => {",
    "  const made = fs.existsSync(out) ? fs.readFileSync(out, 'utf8') : '';",
    "  const run = { work, bytes: made.length, defines: made.includes('__stripTypes'),",
    "    stub: fs.existsSync(path.join(work, 'out/ts-stripper-stub.js')),",
    "    entry: fs.existsSync(path.join(work, 'out/ts-stripper-entry.mjs')), before, after: snap() };",
    "  process.stdout.write(JSON.stringify(run));",
    // The scratch tree is this run's, and a run leaves nothing behind: the parent has what it needs in the JSON above.
    "  fs.rmSync(work, { recursive: true, force: true });",
    "}).catch((e) => { process.stderr.write(String((e && e.stack) || e)); process.exit(1); });",
  ].join("");
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) return { run: null, err, status: status.exit_status };
  return { run: JSON.parse(out) as Run, err, status: 0 };
}

test("the stripper builds from a directory outside the repository, and writes its stub and entry beside its output", async () => {
  const { run, err } = await runOutsideRepo();
  if (!run) throw new Error(`the stripper did not build from outside the repository: ${err}`);
  // The bundle really was built, not the two side files alone: it is the minified iife that defines __stripTypes.
  expect(run.bytes > 1_000_000).toBe(true);
  expect(run.defines).toBe(true);
  expect(run.stub).toBe(true);
  expect(run.entry).toBe(true);
  // Nothing in the repository's build/native moved: the two files are this run's, and this run's alone.
  expect(run.after).toEqual(run.before);
  console.log(`# the stripper built outside the repository, into ${run.work}, and removed it`);
});
