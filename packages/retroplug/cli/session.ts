// The CLI session runtime: the small composition root a session script boots against, plus
// the runSession() wrapper that reports the process exit code. This is the TS-authored ergonomics layer
// — a session imports { runSession } from here, and gets the whole control-plane API
// (backend / project / dsp / audio) already wired the way the plugin and native tests wire it.
//
// Authored in TS, esbuild-bundled to JS (tools/build-session.js), then run on the standalone
// retroplug-cli binary (no Node at runtime). The binary provides the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend and globalThis.tjs.exit(code); __DSP_KERNEL_BUNDLE__ is
// injected at bundle time.

// bootSession + the Session type moved to src/bootSession.ts so the background render worker shares the
// same boot path; re-exported here so every existing importer (test-native, sdk, sessions) is unchanged.
export { bootSession } from "../src/bootSession";
export type { Session } from "../src/bootSession";
import { bootSession } from "../src/bootSession";
import type { Session } from "../src/bootSession";

// The host records the exit code through globalThis.tjs.exit (see cli/main.cpp).
declare const tjs: { exit(code: number): void };

/** Set the process exit code (via the host's tjs.exit). For entry points that finish WITHOUT booting a
 *  session — e.g. the CLI dispatcher printing help — where runSession's boot+exit wrapper is overkill. */
export function exitProcess(code: number): void {
  tjs.exit(code);
}

/** The session's argument vector — everything after the session `.js` on the command line
 *  (`retroplug-cli <session.js> [args...]`). The CLI host hangs it off the
 *  Symbol.for("plugin") namespace (tjs.args is a read-only txiki accessor). Empty when absent. */
export function hostArgs(): string[] {
  const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { args?: string[] } | undefined;
  return ns?.args ?? [];
}

/** Boot a session, run `main` against it, and exit 0. Any throw is reported and exits 1. This is the
 *  entry every session file wraps its body in. */
export function runSession(main: (s: Session) => void): void {
  try {
    main(bootSession());
    tjs.exit(0);
  } catch (e) {
    // Report just the message — these are user-facing usage / IO errors (bad flags, missing ROM, unreadable
    // file), not crashes to debug, so no stack trace. (QuickJS's Error.stack omits the message anyway, so we
    // read err.message directly rather than printing `.stack`.)
    const err = e as Error;
    console.error(`ERROR: ${err?.message ?? e}`);
    tjs.exit(1);
  }
}
