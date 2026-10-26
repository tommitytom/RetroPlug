// The control-plane composition root a session boots against: the wired backend / project / audio
// graph, the same way the plugin and native tests wire it. Lives in src/ (not cli/) so both the CLI session
// layer (cli/session.ts re-exports it) and the background render worker (src/render/worker.ts) share one
// boot path. Authored in TS, esbuild-bundled to JS; the host provides the Backend over
// globalThis[Symbol.for("plugin")].__rpcSend. The DSP kernel needs no boot: it is a script node every Engine
// has (docs/modular-graph-phase-b3a-design.md section 2.5), and the project's graph carries its structure.

import { createRealBackend } from "./realBackend";
import { RecentStore } from "./recentStore";
import { ProjectStore } from "./projectStore";
import { createAudioDriver } from "./audioDriver";
import { buildAppRegistry } from "./appHost";
import type { Backend } from "./backend";
import type { RoleRegistry } from "./systemRoles";
import type { AudioDriver } from "./audioDriver";

/** Everything a session drives: the wired control plane over the real backend. */
export interface Session {
  backend: Backend;
  registry: RoleRegistry;
  recent: RecentStore;
  project: ProjectStore;
  audio: AudioDriver;
}

/** Stand up the control plane the way every host does: build the registry and compose the store graph
 *  over the real backend.
 *  One session at a time: each owns an EngineGraph over the process's one native engine, so a new
 *  session's first edit publishes a graph without the systems earlier sessions built, which frees their cores. */
export function bootSession(): Session {
  const backend = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(backend);
  const project = new ProjectStore(backend, recent, registry);
  // The render calls set their host shape on the project's graph (docs/modular-graph-phase-b2-design.md 4).
  const audio = createAudioDriver(project.graph);

  return { backend, registry, recent, project, audio };
}
