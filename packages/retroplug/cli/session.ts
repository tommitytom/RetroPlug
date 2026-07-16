// The CLI session runtime: the small composition root a session script boots against, plus
// the runSession() wrapper that reports the process exit code. This is the TS-authored ergonomics layer
// — a session imports { runSession } from here, and gets the whole control-plane API
// (backend / project / dsp / audio) already wired the way the plugin and native tests wire it.
//
// Authored in TS, esbuild-bundled to JS (tools/build-session.js), then run on the standalone
// retroplug-cli binary (no Node at runtime). The binary provides the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend and globalThis.tjs.exit(code); __DSP_KERNEL_BUNDLE__ is
// injected at bundle time.

import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";
import type { Backend } from "../src/backend";
import type { RoleRegistry } from "../src/systemRoles";
import type { DspRuntimeClient } from "../src/dspRuntime";
import type { AudioDriver } from "../src/audioDriver";

// The DSP role kernel source, baked in at bundle time (see tools/build-session.js). Any
// session that renders audio needs it loaded into the DSP runtime before the first structural edit.
declare const __DSP_KERNEL_BUNDLE__: string;

/** Everything a session drives: the wired control plane over the real backend. */
export interface Session {
  backend: Backend;
  registry: RoleRegistry;
  recent: RecentStore;
  project: ProjectStore;
  dsp: DspRuntimeClient;
  audio: AudioDriver;
}

/** Stand up the control plane the way every host does: build the registry, compose the
 *  store graph over the real backend, then load the DSP kernel BEFORE installing the store→DSP
 *  projection hook (the first systems mutation fires the hook, which needs the kernel already loaded). */
export function bootSession(): Session {
  const backend = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(backend);
  const project = new ProjectStore(backend, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  return { backend, registry, recent, project, dsp, audio };
}

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
