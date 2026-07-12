// The greenfield CLI session runtime: the small composition root a session script boots against, plus
// the runSession() wrapper that reports the process exit code. This is the TS-authored ergonomics layer
// — a session imports { runSession } from here, and gets the whole greenfield control-plane API
// (backend / project / dsp / audio) already wired the way the plugin and native tests wire it.
//
// Authored in TS, esbuild-bundled to JS (tools/build-greenfield-session.js), then run on the standalone
// retroplug-greenfield-cli binary (no Node at runtime). The binary provides the Backend over
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

// The DSP role kernel source, baked in at bundle time (see tools/build-greenfield-session.js). Any
// session that renders audio needs it loaded into the DSP runtime before the first structural edit.
declare const __DSP_KERNEL_BUNDLE__: string;

/** Everything a session drives: the wired greenfield control plane over the real backend. */
export interface Session {
  backend: Backend;
  registry: RoleRegistry;
  recent: RecentStore;
  project: ProjectStore;
  dsp: DspRuntimeClient;
  audio: AudioDriver;
}

/** Stand up the greenfield control plane the way every host does: build the registry, compose the
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

/** The session's argument vector — everything after the session `.js` on the command line
 *  (`retroplug-greenfield-cli <session.js> [args...]`). The CLI host hangs it off the
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
    // Print the message FIRST — QuickJS's Error.stack omits it, so a bare `.stack` loses the actual
    // reason (e.g. a render flag-usage error). Then the stack, for debugging.
    const err = e as Error;
    console.error(`retroplug-cli: session failed: ${err?.message ?? e}`);
    if (err?.stack) console.error(err.stack);
    tjs.exit(1);
  }
}
