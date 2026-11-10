// The kit's move (docs/modular-graph-phase-f4c-design.md section 4.2): the shell's React widgets are @retroplug/ui-react's
// now, so every file of the kit's ts/ui tree is re-exported by its index, no file under packages/retroplug/ui reaches one
// at its old path, and every name the shell imports from the package is one the index publishes.
//
// The walk is the compiler's, as the boundary check's is (test/boundaries/run-ui-kit-walk.mjs): a grep would match the
// comments that still describe where each widget came from, and would miss a specifier spelled any other way.
import { test, expect } from "@retroplug/testing";

// Just enough of txiki's spawn and a child's pipe, declared locally as bundle-ui-split.test.ts does.
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

interface Walk {
  kitIndex: string | null;
  movedModules: string[];
  kitModules: string[];
  unexported: string[];
  oldPathImports: { file: string; specifier: string }[];
  foreignBindings: { file: string; specifier: string; name: string }[];
  kitBindings: number;
}

/** The walk's report for a tree (this one by default, or a checkout of another). */
async function walk(root?: string): Promise<Walk> {
  const child = tjs.spawn(
    [tjs.env.RP_NODE || "node", "test/boundaries/run-ui-kit-walk.mjs", ...(root ? ["--root", root] : [])],
    { stdout: "pipe", stderr: "pipe" },
  );
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the kit walk failed: ${err || out}`);
  return JSON.parse(out.trim().split("\n").at(-1)!) as Walk;
}

test("every module of the kit's ts/ui tree is re-exported by its index", async () => {
  const report = await walk();
  console.log(`# ${report.kitModules.length} kit modules, ${report.unexported.length} unexported`);
  expect(report.kitIndex).toBe("packages/ui-react/ts/ui/index.ts");
  // No frozen size: the kit may gain a module, and the case is that the walk saw the tree and nothing of it is left out.
  expect(report.kitModules.length, "the walk saw no kit module").toBeGreaterThan(0);
  expect(report.unexported).toEqual([]);
});

test("no shell file imports a moved module from its old path", async () => {
  const report = await walk();
  console.log(`# ${report.oldPathImports.length} old-path import(s)`);
  expect(report.oldPathImports).toEqual([]);
});

test("every name the shell imports from the kit is one its index publishes", async () => {
  // A name the index does not export would fail `tsc` too; this says which name, and holds the walk itself to seeing
  // the shell's imports rather than passing because it found none.
  const report = await walk();
  console.log(`# ${report.kitBindings} kit binding(s), ${report.foreignBindings.length} the kit does not export`);
  expect(report.kitBindings, "the walk saw no name the shell imports from the kit").toBeGreaterThan(0);
  expect(report.foreignBindings).toEqual([]);
});
