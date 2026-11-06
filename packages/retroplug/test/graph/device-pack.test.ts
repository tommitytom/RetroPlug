// The device-pack toolchain (scripts/build-device-pack.mjs, docs/modular-graph-phase-e4-design.md section 6) and
// defineNode in MicroQuickJS's subset. A device pack's TypeScript is bundled against an allowlist, lowered to ES5,
// scanned for library calls the engine lacks and compiled to bytecode, and its definitions run under Node and as
// bytecode on the engine (the desktop build's mqjs, which `pnpm test` builds), whose transcripts must match byte for
// byte: check:portable's method, on feature/launchpad. Here:
//
// - test/graph/device-pack/scenario.ts, defineNode and its helpers at work as a device pack (every port kind a device
//   pack may have, every param type, settings and the ports that follow them, every refusal, a node's process on a
//   block), runs the same on both engines;
// - the toolchain refuses a pack that calls what the engine lacks (a Map, an `async`, a timer of mqjs's REPL, a method
//   whose name the SDK declares, a use beside a guard, a call in a helper the bundler wrote), naming the call and its
//   source line; one whose node has an audio port, as defineNode refuses it at definition, naming the node and the
//   port; one that imports outside its allowlist, naming the module and its importer; one the engine runs unlike Node,
//   naming the transcript line; and one whose bytecode does not define its node types in its declared heap;
// - the devtest package's script pack (packages/devtest/ts), which CMake compiles from its manifest
//   (retroplug_script_pack in packages/native/cmake/tests.cmake), has its bytecode at both widths, and the toolchain
//   run by hand on the same entry writes the same bytes.
//
// The toolchain is Node's (esbuild and TypeScript are not txiki modules), so the test runs it in a node child: the
// runner's own node (RP_NODE, which scripts/run-tests.mjs sets), else the node on the PATH when run by hand.
import { test, expect } from "../../testing/harness";

// Just enough of txiki's spawn, a child's pipe and its file reads, declared locally as bundle-no-zod.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  readFile(path: string): Promise<Uint8Array>;
  stat(path: string): Promise<unknown>;
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

// The suite runs from packages/retroplug.
const FIXTURES = "test/graph/device-pack";
const OUT = ".test-build/device-pack";
const DEVTEST_BYTECODE = "../../build/packages/devtest/script";
// What defineNode says of a device pack's audio port, after naming the node and the port.
const NO_AUDIO = "which a device pack's node cannot have: audio stays C++";
// And of a device pack's settings field named like an integer, after naming the node and the field.
const NO_ORDER = "which a device pack's settings cannot keep in order";

/** Whether a path exists. */
async function exists(path: string): Promise<boolean> {
  try {
    await tjs.stat(path);
    return true;
  } catch {
    return false;
  }
}

/** `part` is in `whole`, which the failure prints. */
function contains(whole: string, part: string): void {
  expect(whole.includes(part), `${JSON.stringify(part)} is not in:\n${whole}`).toBe(true);
}

/** The toolchain on one entry, into OUT/<name>: its exit status and what it printed. */
async function buildPack(entry: string, name: string, heapBytes: number, packDir?: string) {
  const args = [tjs.env.RP_NODE || "node", "scripts/build-device-pack.mjs", "--entry", entry, "--name", name,
                "--heap-bytes", String(heapBytes), "--out-dir", `${OUT}/${name}`];
  if (packDir) args.push("--pack-dir", packDir);
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { ok: !status.term_signal && status.exit_status === 0, out, err };
}

test("defineNode and its helpers run on MicroQuickJS as on Node, byte for byte", async () => {
  const run = await buildPack(`${FIXTURES}/scenario.ts`, "scenario", 65536);
  expect(run.ok, `the toolchain refused the scenario:\n${run.out}${run.err}`).toBe(true);
  contains(run.out, "matches Node byte for byte");
  // The transcript the two engines agreed on: what the scenario prints, then every registered node's descriptor.
  const transcript = new TextDecoder().decode(await tjs.readFile(`${OUT}/scenario/scenario.transcript.txt`));
  const lines = transcript.split("\n");
  console.log(`# scenario transcript: ${lines.length - 1} lines`);
  for (const line of [
    `an audio input: rp.test.portable.audio: port "in" is audio, ${NO_AUDIO}`,
    `audio by settings: rp.test.portable.audio-later: port "sound" is audio, ${NO_AUDIO}`,
    '__proto__ twice: rp.t: "__proto__" is used by more than one port or param',
    "toString once: accepted",
    'capacity stray field: rp.t: port "o": capacity: unknown field "frames"',
    "an enum twice: rp.t: param \"m\": an enum lists a value twice",
    "an int's fraction: rp.t: param \"i\": an int param needs whole-number min, max and default",
    'fan-in settings {"inputs":99}: node type "rp.test.portable.fan-in": settings: inputs: Too big: expected ' +
      "number to be <=8",
    "counter sent: out@0=144,63/0 out@5=128,4/0 count@31=36 out@0=144,69/0 out@5=128,10/0 count@31=42",
    `an integer-like settings field: rp.t: settings field "7" is a number, ${NO_ORDER}`,
    `one in an array's items: rp.t: settings field "taps[].0" is a number, ${NO_ORDER}`,
    "# 4 node type(s)",
  ])
    expect(lines.includes(line), line).toBe(true);
});

test("a device pack that calls what MicroQuickJS lacks is refused, naming the call and its source line", async () => {
  const map = await buildPack(`${FIXTURES}/missing-call.ts`, "missing-call", 65536);
  expect(map.ok).toBe(false);
  contains(map.err, "calls library functions MicroQuickJS does not have");
  contains(map.err, "packages/retroplug/test/graph/device-pack/missing-call.ts:10: new Map");
  // Found as written, at its own line, before the lowering turns it into a Promise in a helper.
  const later = await buildPack(`${FIXTURES}/async-process.ts`, "async-process", 65536);
  expect(later.ok).toBe(false);
  contains(later.err, "packages/retroplug/test/graph/device-pack/async-process.ts:14: async (async/await)");
  // Globals of upstream mqjs's REPL, which the toolchain's own runs have and the box's engine will not; a function the
  // pack declares itself under one of their names is its own.
  const host = await buildPack(`${FIXTURES}/host-global.ts`, "host-global", 65536);
  expect(host.ok).toBe(false);
  contains(host.err, "device-pack/host-global.ts:20: setTimeout( (a global of mqjs's REPL");
  contains(host.err, "device-pack/host-global.ts:21: console.warn (a console method other than log");
  expect(host.err.includes("load("), host.err).toBe(false);
  // A method call in process() whose name the bundled SDK also declares (defineNode's startsWith, settingsSchema's
  // `from`): a declaration of the name elsewhere makes no call the pack's own.
  const methods = await buildPack(`${FIXTURES}/method-calls.ts`, "method-calls", 65536);
  expect(methods.ok).toBe(false);
  contains(methods.err, "device-pack/method-calls.ts:16: Array.from (Array.from/of)");
  contains(methods.err, "device-pack/method-calls.ts:16: .startsWith( (String.raw");
  // A guard accepts only what runs when it holds: the else-branch's Symbol is refused.
  const unguarded = await buildPack(`${FIXTURES}/guard-else.ts`, "guard-else", 65536);
  expect(unguarded.ok).toBe(false);
  contains(unguarded.err, "device-pack/guard-else.ts:15: Symbol (Symbol/Reflect/Proxy/Promise/WeakRef/Intl)");
  // A call in a helper the bundler wrote (esbuild's `using` reads Symbol), named with the line that pulled it in.
  const helper = await buildPack(`${FIXTURES}/using-resource.ts`, "using-resource", 65536);
  expect(helper.ok).toBe(false);
  contains(
    helper.err,
    "Symbol (Symbol/Reflect/Proxy/Promise/WeakRef/Intl) in the helper __knownSymbol that the bundler or the ES5 " +
      "lowering wrote, which packages/retroplug/test/graph/device-pack/using-resource.ts:15 pulls in (through __using)",
  );
});

test("a device pack the engine runs unlike Node is refused, at the transcript line where they part", async () => {
  // A computed "__proto__" key, which the ES5 lowering makes an assignment: Node sets the prototype, the engine an own
  // key, so the node has one port on the reference and two on the engine.
  const run = await buildPack(`${FIXTURES}/proto-key.ts`, "proto-key", 65536);
  expect(run.ok).toBe(false);
  contains(run.err, "MicroQuickJS (deps/mquickjs, 6d4d7eb) disagrees with Node at line 2 of the transcript");
  contains(run.err, '"ports":[{"id":"out","dir":"out","kind":"events"}]');
  contains(run.err, '"ports":[{"id":"__proto__","dir":"in","kind":"events"},{"id":"out","dir":"out","kind":"events"}]');
  // Refused after its bytecode was made: nothing of it is left, neither an output nor the work files.
  for (const file of ["proto-key.js", "proto-key.mqjs64.bin", "proto-key.mqjs32.bin", "proto-key.transcript.txt",
                      ".proto-key.work"])
    expect(await exists(`${OUT}/proto-key/${file}`), file).toBe(false);
});

test("a device pack whose bytecode does not define its node types in its declared heap is refused", async () => {
  // devtest defines its node in about 12 KiB of a 64-bit context. (Below about 4 KiB the engine crashes in its own
  // setup, which the toolchain reports as that.)
  const run = await buildPack("../devtest/ts/index.ts", "devtest-small", 8192, "../devtest");
  expect(run.ok).toBe(false);
  contains(run.err, "its bytecode does not define its node types in its declared heap of 8192 bytes");
  contains(run.err, "InternalError: out of memory");
});

test("a device pack's audio port is refused at definition, naming the node and the port", async () => {
  const run = await buildPack(`${FIXTURES}/audio-port.ts`, "audio-port", 65536);
  expect(run.ok).toBe(false);
  contains(
    run.err,
    'defining its node types threw: rp.test.device.midi-to-gate: port "gate" is audio, which a device pack\'s node ' +
      "cannot have: audio stays C++",
  );
});

test("a device pack that imports outside its allowlist is refused, naming the module and its importer", async () => {
  const run = await buildPack(`${FIXTURES}/outside-import.ts`, "outside-import", 65536);
  expect(run.ok).toBe(false);
  contains(
    run.err,
    "packages/retroplug/src/graph/canonicalJson.ts " +
      "(imported by packages/retroplug/test/graph/device-pack/outside-import.ts)",
  );
});

// A bytecode file's header (mquickjs.h's JSBytecodeHeader): magic 0xacfb and the version, whose bit 15 says 64-bit,
// then the address it is relocated to, which the toolchain leaves at 0 (the loader relocates it to where it lies).
function header(bytes: Uint8Array, wide: boolean) {
  const word = (at: number) => bytes[at] | (bytes[at + 1] << 8);
  let base = 0;
  for (let i = 0; i < (wide ? 8 : 4); i++) base += bytes[4 + i];
  return { magic: word(0), version: word(2), base };
}

test("devtest's script pack is built from its manifest at both widths, the same bytes every time", async () => {
  const wide = await tjs.readFile(`${DEVTEST_BYTECODE}/devtest.mqjs64.bin`);
  const narrow = await tjs.readFile(`${DEVTEST_BYTECODE}/devtest.mqjs32.bin`);
  console.log(`# devtest bytecode: ${wide.length} bytes at 64-bit, ${narrow.length} at 32-bit`);
  expect(header(wide, true)).toEqual({ magic: 0xacfb, version: 0x8001, base: 0 });
  expect(header(narrow, false)).toEqual({ magic: 0xacfb, version: 0x0001, base: 0 });
  // A pointer's width, and no more, between them: the 32-bit form is the smaller.
  expect(narrow.length > 1024 && narrow.length < wide.length).toBe(true);
  // The toolchain by hand, on the entry and heap the manifest names, writes what CMake's target wrote.
  const run = await buildPack("../devtest/ts/index.ts", "devtest", 16384, "../devtest");
  expect(run.ok, `${run.out}${run.err}`).toBe(true);
  const same = async (file: string, built: Uint8Array) => {
    const again = await tjs.readFile(`${OUT}/devtest/${file}`);
    return again.length === built.length && again.every((b, i) => b === built[i]);
  };
  expect(await same("devtest.mqjs64.bin", wide)).toBe(true);
  expect(await same("devtest.mqjs32.bin", narrow)).toBe(true);
});
