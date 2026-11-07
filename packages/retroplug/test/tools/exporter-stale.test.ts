// Guards pico/n8-host/exporter-stale.sh, the comparison loop.sh refuses to export a firmware's plan through an exporter
// older than one of its sources (docs/modular-graph-phase-f1-design.md section 4). The script stands alone so it can be
// tested without the rig; this is that test, and it needs no hardware: the driver (test/tools/exporter-stale-check.mjs)
// builds a scratch repository and asks the script the three questions it answers - current, stale, and a usage error -
// with a stamp whose mtime it controls.
//
// The driver is Node's because a test under txiki has no filesystem, and the script runs in bash, which every checkout
// that can build this tree has: Git for Windows supplies it on the platform whose suite runs through test.bat.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn, a child's pipe and its file reads, declared locally as bundle-no-zod.test.ts does.
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

test(
  "the exporter's staleness check holds when current, names the newer source, and refuses a usage error",
  async () => {
    const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/tools/exporter-stale-check.mjs"], {
      stdout: "pipe",
      stderr: "pipe",
    });
    // Both pipes drained beside the wait, so a child that fills one cannot block on it.
    const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
    if (status.term_signal || status.exit_status !== 0) throw new Error(`the driver failed: ${err || out}`);
    const report = JSON.parse(out) as Record<string, { status: number; out: string }>;
    console.log(`# ${JSON.stringify(report)}`);

    // A stamp newer than every source: the exporter is current, and the script says nothing.
    expect(report.current.status).toBe(0);
    expect(report.current.out).toBe("");

    // A core TS file newer than the stamp: refused, naming the source. The list is the exporter's whole build, which is
    // what makes this a check of the list and not of the tool's own mtime.
    expect(report.newer.status).toBe(1);
    expect(report.newer.out.split(/[\\/]/).join("/").endsWith("packages/core/ts/clock.ts")).toBe(true);

    // No stamp at all, and no argument: both are usage errors, which print what the script takes.
    expect(report.missing.status).toBe(2);
    expect(report.missing.out.includes("no stamp at")).toBe(true);
    expect(report.usage.status).toBe(2);
    expect(report.usage.out.includes("./exporter-stale.sh <stamp> [<repo>]")).toBe(true);
  },
);
