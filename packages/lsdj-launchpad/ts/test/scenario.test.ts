// The LSDj bridge's scenario (packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts) through the device-pack toolchain's
// scenario mode: bundled, lowered to ES5, scanned for library calls MicroQuickJS lacks (the scenario's bundle AND the
// package's whole portable core), then run under Node and on build/bin/retroplug-mqjs-run, whose transcripts must agree
// byte for byte inside the heap it is given.
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
const SCENARIO = "../../packages/lsdj-launchpad/ts/scenario/lsdjLaunchpad.ts";
const PACK_DIR = "../../packages/lsdj-launchpad";
const OUT = ".test-build/lsdj-launchpad-scenario";
// The bridge's own heap, measured the way the launchpad core's is, by bisecting --heap-bytes on this tree: the run
// passes at 144960 bytes and fails at 144896 (the four quantise sessions' tables are live at once), and the budget is
// 1.25x that. The scenario's figure, NOT the box's 64 KiB arena, which no controller pack claims.
const LEAST_HEAP = 144960;
const HEAP = Math.ceil((LEAST_HEAP * 1.25) / 1024) * 1024; // 184320

async function runScenario(): Promise<{ ok: boolean; out: string; err: string }> {
  const args = [tjs.env.RP_NODE || "node", "scripts/build-device-pack.mjs", "--scenario", SCENARIO,
                "--name", "lsdj-launchpad", "--heap-bytes", String(HEAP), "--out-dir", OUT, "--pack-dir", PACK_DIR];
  const child = tjs.spawn(args, { stdout: "pipe", stderr: "pipe" });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  return { ok: !status.term_signal && status.exit_status === 0, out, err };
}

test("the LSDj bridge runs on MicroQuickJS as on Node, byte for byte", async () => {
  const run = await runScenario();
  expect(run.ok, `the toolchain refused the scenario:\n${run.out}${run.err}`).toBe(true);
  expect(run.out.includes("matches Node byte for byte")).toBe(true);
  expect(run.out.includes("device pack scenario lsdj-launchpad OK")).toBe(true);
  // The package's whole core is scanned, its control-plane half (ts/app.ts, which is zod's) aside: a call the engine
  // lacks on a path the scenario never takes is refused there.
  expect(run.out.includes("no library calls outside MicroQuickJS's stdlib, in the core's ")).toBe(true);

  // The transcript the two engines agreed on: the app driving the predictor, the predictor's edit and anchor paths, a
  // song's arithmetic, every quantise mode and the target's bytes. A run that stopped early would still agree with
  // itself, so the sections are asserted.
  const transcript = new TextDecoder().decode(await tjs.readFile(`${OUT}/lsdj-launchpad.transcript.txt`));
  const lines = transcript.split("\n");
  expect(lines.length > 25, `${lines.length} transcript lines`).toBe(true);
  for (const part of [
    'connect [[240,0,32,41,2,14,14,1,247]]', // Programmer mode
    '"launches":[[144,2,100]]', // the stopped-transport press launched at once (row 2, at the top)
    "position {", // where the predictor ended, after the bars, the seek and the stop
    "grid [256,4,true,false,false]",
    "layout [0,8,7,31,0,3,0]",
    "follow [3,null,15,4]",
    'model playing {"playing":true,"channels":[{"playing":true,"songRow":2}', // a launch, then 150 ticks
    "model edited {", // a row losing its chain, a chain shortened under the ticks already played
    'model anchored {"playing":true,"channels":[{"playing":true,"songRow":3}',
    "model stopped {",
    "song ticks [192,96,null,null]", // a custom groove, a chain ending at a hole
    "zero groove 192", // an all-zero groove falls back to the factory one
    "target release [[128,5,0],[129,72,0],[145,125,100]]", // release, release, and a row past the range dropped
    "launch encoding [[144,0,100],[144,127,100],[145,0,100],[145,125,100],null,null,null,[128,3,0]]",
    "quantise immediate {",
    "quantise beat {",
    "quantise bar {",
    "quantise rowEnd {", // the four modes: the launches land the same rows, the LED traffic counts differ
    "null target session 1",
  ])
    expect(transcript.includes(part), part).toBe(true);
});
