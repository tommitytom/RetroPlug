// The TypeScript type stripper's bundle is built by CMake from whatever directory the build runs in
// (packages/native/CMakeLists.txt's retroplug-ts-stripper-bundle), and a build directory need not be inside the
// repository: `cmake -S <repo> -B /tmp/rpbuild` is a normal configure. An alias value is the one specifier esbuild
// resolves from absWorkingDir rather than from the importer, and the entry's own bare import ("ts-blank-space") walks
// up from the entry's directory, which is beside the output. The script therefore needs both the repository as its
// working directory (the typescript-5 alias lives in the root node_modules) and `nodePaths`; without them the build
// stops there. The stub and the generated entry are build products of this script too, so they belong beside the
// output file rather than in the repository's build/native whatever the output path is.
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

/** What one run reports: the bundle's size and whether it defines the one global as an iife, whether the stub and the
 *  entry landed beside it, and the two files' hashes in the repository's build/native before and after (null when
 *  absent). Content is the invariant, not an mtime: a concurrent default build rewrites those very files. */
interface Run {
  work: string;
  bytes: number;
  defines: boolean;
  iife: boolean;
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
    'const stamp = (name) => { try { const b = fs.readFileSync(path.join(repo, "build/native", name));',
    '  return b.length + ":" + require("crypto").createHash("sha1").update(b).digest("hex"); } catch { return null; } };',
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
    // The bundle is GLOBAL CODE: a minified iife, whoever's minifier emitted it. The opener is what says an iife was
    // built, where a size bound only says something big was.
    "    iife: made.startsWith('(()=>{') || made.startsWith('(function(){'),",
    "    stub: fs.existsSync(path.join(work, 'out/ts-stripper-stub.js')),",
    "    entry: fs.existsSync(path.join(work, 'out/ts-stripper-entry.mjs')), before, after: snap() };",
    "  process.stdout.write(JSON.stringify(run));",
    // The scratch tree is this run's, and a run leaves nothing behind: the parent has what it needs in the JSON above.
    "  fs.rmSync(work, { recursive: true, force: true });",
    "}).catch((e) => { try { fs.rmSync(work, { recursive: true, force: true }); } catch {}",
    "  process.stderr.write(String((e && e.stack) || e)); process.exit(1); });",
  ].join("");
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "-e", script], { stdout: "pipe", stderr: "pipe" });
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  // A child that died (a signal, a non-zero exit) and one that printed something else on stdout are both failures with
  // a reason worth reading: neither may become a bare SyntaxError that never mentions the child's stderr.
  if (status.term_signal || status.exit_status !== 0) {
    return { run: null, err: err || `signal ${status.term_signal}`, status: status.exit_status };
  }
  try {
    return { run: JSON.parse(out) as Run, err, status: 0 };
  } catch {
    return { run: null, err: err || out, status: 0 };
  }
}

test("the stripper builds outside the repository, with its stub and entry beside its output", async () => {
  const { run, err, status } = await runOutsideRepo();
  if (!run) {
    throw new Error(`the stripper did not build from outside the repository: ${err || "no output"} (exit ${status})`);
  }
  // The bundle really was built, not the two side files alone: it is the minified iife that defines __stripTypes.
  expect(run.bytes > 100_000).toBe(true);
  expect(run.iife).toBe(true);
  expect(run.defines).toBe(true);
  expect(run.stub).toBe(true);
  expect(run.entry).toBe(true);
  // Nothing in the repository's build/native moved: the two files are this run's, and this run's alone.
  expect(run.after).toEqual(run.before);
  console.log(`# the stripper built outside the repository, into ${run.work}, and removed it`);
});
