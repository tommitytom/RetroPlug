// The control-plane composition root a session boots against: the wired backend / project / dsp / audio
// graph, the same way the plugin and native tests wire it. Lives in src/ (not cli/) so both the CLI session
// layer (cli/session.ts re-exports it) and the background render worker (src/render/worker.ts) share one
// boot path. Authored in TS, esbuild-bundled to JS; the host provides the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend, and __DSP_KERNEL_BUNDLE__ is injected at bundle time.

import { createRealBackend } from "./realBackend";
import { RecentStore } from "./recentStore";
import { ProjectStore } from "./projectStore";
import { createDspRuntime } from "./dspRuntime";
import { createAudioDriver } from "./audioDriver";
import { buildAppRegistry, syncDspFromStore } from "./appHost";
import type { Backend } from "./backend";
import type { RoleRegistry } from "./systemRoles";
import type { DspRuntimeClient } from "./dspRuntime";
import type { AudioDriver } from "./audioDriver";

// The DSP role kernel source, baked in at bundle time (build-session.js / build-controlplane.js /
// build-render-worker.js). Any session that renders audio needs it loaded into the DSP runtime before the
// first structural edit.
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
