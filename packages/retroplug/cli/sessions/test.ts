// `retroplug-cli test <dir>` - strip and run a directory of TypeScript test files.
//
// This is what lets a consumer repo (BlipToaster) use the CLI as its whole test harness with nothing
// installed: no Node, no npm, no esbuild, no node_modules. The kit is the binary, the SDK `.js`, the
// `.d.ts`, and the tests.
//
// Two things make it work, both pre-existing:
//   * The CLI's txiki runtime resolves `import` off disk at runtime, so tests need no bundling; they
//     just have to be JavaScript.
//   * Consumer tests already use explicit `.js` specifiers ("./helper.js", "../sdk/retroplug-cli.js"),
//     so stripping `X.ts` -> `X.js` at the same directory DEPTH needs no specifier rewriting. See
//     buildDirFor in ../tsStrip.
//
// Each test file runs in its OWN PROCESS, which is required rather than tidy: the TAP harness calls
// tjs.exit when a file finishes, its module-level case list is shared once the SDK resolves to a single
// module, and the native Engine is per-process, so systems loaded by one file would leak into the next.

import { keepAlive, exitProcess } from "../session";
import type { Session } from "../session";
import type { CliTool } from "../tools";
import { buildTsDir, buildDirFor } from "../tsStrip";

declare const tjs: {
  exePath: string;
  env: Record<string, string>;
  spawn(args: string[], options?: { env?: Record<string, string> }): { wait(): Promise<{ exit_status: number; term_signal: string | null }> };
};

export interface TestArgs {
  dir: string;
  /** Substring match on the test file name; empty runs everything. */
  filter: string;
  /** Forwarded to every test file as its first session argument (BlipToaster tests read a ROM path). */
  rom: string | null;
  /** Override the stripped-output directory (default: the source dir's sibling .rp-test-build). */
  out: string | null;
  /** Extra session arguments, after `--`, appended to every test file's argv. */
  passthrough: string[];
}

/** Parse `test`'s arguments. Pure, so the unit tests can cover it without a binary. */
export function parseTestArgs(args: string[]): TestArgs {
  const out: TestArgs = { dir: "", filter: "", rom: null, out: null, passthrough: [] };
  const dashDash = args.indexOf("--");
  const head = dashDash < 0 ? args : args.slice(0, dashDash);
  out.passthrough = dashDash < 0 ? [] : args.slice(dashDash + 1);

  const positional: string[] = [];
  for (let i = 0; i < head.length; i++) {
    const a = head[i];
    if (a === "--rom") out.rom = head[++i] ?? null;
    else if (a === "--out") out.out = head[++i] ?? null;
    else positional.push(a);
  }
  out.dir = positional[0] ?? "";
  out.filter = positional[1] ?? "";
  return out;
}

/** The emitted files that are test files, in the order they should run. */
export function selectTests(emitted: string[], filter: string): string[] {
  return emitted
    .filter((f) => f.endsWith(".test.js"))
    .filter((f) => !filter || f.includes(filter))
    .sort();
}

const help = `usage: retroplug-cli test <dir> [name-filter] [options] [-- session-args...]

Strip and run every *.test.ts in <dir>. Each file runs in its own process (a fresh Engine and a fresh
config dir), and the exit code is nonzero if any file fails, so this is a real pass/fail gate.

  <dir>             directory of *.test.ts / *.test.js files
  [name-filter]     only run files whose name contains this substring

options:
  --rom <path>      passed to every test file as its first argument
  --out <dir>       where to write stripped output (default: <dir>'s sibling .rp-test-build)
  -- <args...>      extra arguments appended to every test file's argv

TypeScript is stripped, not compiled: only erasable syntax is supported. enum, namespace and
constructor parameter properties emit runtime code and are rejected with a file:line:col error.

example:
  retroplug-cli test tests --rom rom/build/bliptoaster.nes
  retroplug-cli test tests pulse --rom rom/build/bliptoaster.nes`;

export const testTool: CliTool = {
  name: "test",
  summary: "Strip and run a directory of TypeScript test files",
  help,
  // Spawning children and awaiting them is async, so the dispatcher must not auto-exit us; we report
  // the exit code ourselves once every file has been waited on.
  longRunning: true,
  run(s: Session, args: string[]): void {
    const opts = parseTestArgs(args);
    if (!opts.dir) {
      console.error("retroplug-cli test: missing <dir>\n\n" + help);
      exitProcess(2);
      return;
    }

    const outDir = opts.out ?? buildDirFor(opts.dir);
    const { emitted } = buildTsDir(s.backend, opts.dir, outDir);
    const tests = selectTests(emitted, opts.filter);

    if (tests.length === 0) {
      console.error(`no tests matched in ${opts.dir}${opts.filter ? ` (filter: ${opts.filter})` : ""}`);
      exitProcess(2);
      return;
    }

    const sessionArgs = [...(opts.rom ? [opts.rom] : []), ...opts.passthrough];

    keepAlive(); // opt into the run-until-exit pump while the children run
    void runAll(tests, outDir, sessionArgs);
  },
};

async function runAll(tests: string[], outDir: string, sessionArgs: string[]): Promise<void> {
  let failed = 0;
  for (const file of tests) {
    const name = file.replace(/\.test\.js$/, "");
    console.log(`\n# ${file}`);
    // A fresh config dir per file so runs never cross-contaminate (mirrors what the Node runner did
    // with mkdtemp). The child creates it on first write - writeFile makes parent dirs on demand.
    const proc = tjs.spawn([tjs.exePath, `${outDir}/${file}`, ...sessionArgs], {
      env: { ...tjs.env, RETROPLUG_USER_CONFIG_DIR: `${outDir}/.cfg/${name}` },
    });
    const status = await proc.wait();
    if (status.exit_status !== 0 || status.term_signal) failed++;
  }

  const total = tests.length;
  console.log(`\n${failed === 0 ? "PASS" : "FAIL"}: ${total - failed}/${total} test file(s) ok`);
  exitProcess(failed === 0 ? 0 : 1);
}
