// `retroplug-cli run <session.ts> [args...]` - strip and run a single TypeScript session.
//
// The one-off sibling of `test`: same stripping, same directory layout, but ONE file. This is what a
// repro / probe script uses (it replaces BlipToaster's repro/run.mjs, the second place Node was needed).
// `run <x.ts>` is exactly the `.ts` equivalent of `retroplug-cli <x.js>`, down to the exit code.
//
// The whole source directory is stripped, not just the named file, so a session that imports a sibling
// helper works without us resolving the module graph.

import { keepAlive, exitProcess } from "../session";
import type { Session } from "../session";
import type { CliTool } from "../tools";
import { buildTsDir, buildDirFor, outputName } from "../tsStrip";
import { spawnSession } from "../childSession";
import { ensureSdk, sdkDirFor } from "../sdkAssets";

export interface RunArgs {
  session: string;
  out: string | null;
  sessionArgs: string[];
}

/** Parse `run`'s arguments: the session path, then everything else forwarded to it verbatim. Pure. */
export function parseRunArgs(args: string[]): RunArgs {
  const out: RunArgs = { session: "", out: null, sessionArgs: [] };
  const rest: string[] = [];
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--out" && !out.session) out.out = args[++i] ?? null;
    else if (!out.session) out.session = a;
    else rest.push(a);
  }
  out.sessionArgs = rest;
  return out;
}

/** Split a path into its directory and file name. `foo.ts` (no slash) means the current directory. */
export function splitPath(path: string): { dir: string; file: string } {
  const slash = path.lastIndexOf("/");
  return slash < 0 ? { dir: ".", file: path } : { dir: path.slice(0, slash), file: path.slice(slash + 1) };
}

const help = `usage: retroplug-cli run <session.ts> [args...]

Strip and run a single TypeScript session file. Everything after the path is passed to the session as
its arguments (read with hostArgs()).

  --out <dir>   where to write stripped output (default: the session dir's sibling .rp-test-build)

TypeScript is stripped, not compiled: only erasable syntax is supported. enum, namespace and
constructor parameter properties are rejected with a file:line:col error.

example:
  retroplug-cli run repro/verify-pitch.ts rom/build/bliptoaster.nes`;

export const runTool: CliTool = {
  name: "run",
  summary: "Strip and run a single TypeScript session file",
  help,
  // The session runs in a child process (see childSession) and we wait on it, so we must not be
  // auto-exited; we report the child's exit code once it finishes.
  longRunning: true,
  run(s: Session, args: string[]): void {
    const opts = parseRunArgs(args);
    if (!opts.session) {
      console.error("retroplug-cli run: missing <session.ts>\n\n" + help);
      exitProcess(2);
      return;
    }

    const { dir, file } = splitPath(opts.session);
    const outDir = opts.out ?? buildDirFor(dir);
    const { needsSdk } = buildTsDir(s.backend, dir, outDir);
    if (needsSdk) ensureSdk(s.backend, sdkDirFor(outDir)); // see the note in sessions/test.ts

    keepAlive();
    void load(`${outDir}/${outputName(file)}`, opts.sessionArgs);
  },
};

async function load(path: string, args: string[]): Promise<void> {
  // The session owns its exit code (a TAP harness calls tjs.exit when it finishes); we just pass it on,
  // so `run x.ts` and `retroplug-cli x.js` behave identically.
  exitProcess(await spawnSession(path, args));
}
