// The Launchpad core's scenario (packages/launchpad/ts/scenario/launchpadCore.ts) through the device-pack
// toolchain's scenario mode (packages/retroplug/scripts/build-device-pack.mjs): bundled, lowered to ES5, scanned for
// library calls MicroQuickJS lacks, then run under Node and on build/bin/retroplug-mqjs-run, whose transcripts must
// agree byte for byte inside the heap it is given.
//
// The toolchain is Node's (esbuild and TypeScript are not txiki modules), so this runs it in a node child: the runner's
// own node (RP_NODE, which scripts/run-tests.mjs sets), else the node on the PATH when run by hand.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-no-zod.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  readFile(path: string): Promise<Uint8Array>;
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
const SCENARIO = "../../packages/launchpad/ts/scenario/launchpadCore.ts";
const PACK_DIR = "../../packages/launchpad";
const OUT = ".test-build/launchpad-scenario";
// The least heap the run needs, measured on this tree by bisecting --heap-bytes: it passes at 61552 bytes and fails
// below 61300. The budget is 1.25x that, so a path the scenario adds shows up as a refusal rather than quietly needing
// more. It is the scenario's own figure, not a claim about the box's 64 KiB arena (there is no Pico controller path).
const LEAST_HEAP = 61300;
const HEAP = Math.ceil(LEAST_HEAP * 1.25 / 256) * 256; // 76800

async function runScenario(): Promise<{ ok: boolean; out: string; err: string }> {
  const args = [tjs.env.RP_NODE || "node", "scripts/build-device-pack.mjs", "--scenario", SCENARIO,
                "--name", "launchpad-core", "--heap-bytes", String(HEAP), "--out-dir", OUT, "--pack-dir", PACK_DIR];
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { ok: !status.term_signal && status.exit_status === 0, out, err };
}

test("the Launchpad core runs on MicroQuickJS as on Node, byte for byte", async () => {
  const run = await runScenario();
  expect(run.ok, `the toolchain refused the scenario:\n${run.out}${run.err}`).toBe(true);
  expect(run.out.includes("matches Node byte for byte")).toBe(true);
  expect(run.out.includes("device pack scenario launchpad-core OK")).toBe(true);
  expect(run.out.includes("no library calls outside MicroQuickJS's stdlib, in the pack or anywhere in the SDK")).toBe(true);
  // The package's WHOLE core is scanned, not only what the scenario reaches: the scan is the guard a path neither the
  // scenario nor a node runs. (It is what caught launchMessage's Number.isInteger, which tree shaking had dropped.)
  expect(run.out.includes("no library calls outside MicroQuickJS's stdlib, in the core's ")).toBe(true);

  // The transcript the two engines agreed on, kept beside the outputs: it has the sections the scenario claims to
  // walk, so a run that silently stopped early cannot pass on the two engines merely agreeing.
  const transcript = new TextDecoder().decode(
    await tjs.readFile(`${OUT}/launchpad-core.transcript.txt`),
  );
  const lines = transcript.split("\n");
  expect(lines.length > 25, `${lines.length} transcript lines`).toBe(true);
  for (const part of [
    'connect [[240,0,32,41,2,14,14,1,247]]', // Programmer mode
    "reconnect 43 [[240,0,32,41,2,14,14,1,247]]", // a generation change mid-run re-takes the device
    "ticks 480", // and the seek after it advanced the model nothing
    '"phase":"","error":"","skipped":2',
    'found {"input":"IF A in","output":"IF B out","name":"Launchpad Pro [MK3]"}',
    "model reply [[240,126,0,6,2,0,32,41,19,1,0,0,0,1,2,3,247],false]", // an echo confirms nothing
    "model reply [[240,0,32,41,2,14,0,0,0,247],true]",
    "surface bulk ",
    "surface steady",
    "decode [",
    "profiles [",
    "late reply 0",
    "cancelled [false,true,false]",
    'broken ["gone",true]',
  ])
    expect(transcript.includes(part), part).toBe(true);
});
