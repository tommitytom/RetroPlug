// appStores.ts — the composition root for the greenfield store graph.
//
// One place that builds the full store graph over a Backend, so every host constructs it identically
// instead of re-deriving pluginControlPlane.ts's sequence. Pure — no React, no DSP. Wiring the DSP stays
// the control plane's job (it installs syncDspFromStore on project.setOnSystemsChange AFTER its runtime
// exists).
//
// Change notification is injected as a single `notify(channel)` so a consumer can observe the whole
// graph through one seam: the UI's store provider passes a fan-out that re-renders the matching hooks;
// non-observing hosts leave it a no-op. The stores that only take an onChange at construction (recent /
// userConfig / bindings) get it here; project's two signals (systems-structure vs any-state) are wired
// through its setters.
//
// Future unification: the plugin's control-plane bundle (pluginControlPlane.ts) still composes its stores
// inline because the plugin is headless today — there's no co-resident UI context to share a graph with.
// When the greenfield plugin gains an editor, route its composition through composeAppStores too so the
// control plane and the UI observe ONE graph.

import { createRealBackend } from "./realBackend";
import type { Backend } from "./backend";
import type { RoleRegistry } from "./systemRoles";
import { buildAppRegistry } from "./appHost";
import { RecentStore } from "./recentStore";
import { UserConfigStore } from "./userConfigStore";
import { BindingsStore } from "./bindingsStore";
import { ProjectStore } from "./projectStore";
import { FileSelection } from "./fileSelection";

/** The categories of store change a consumer can observe. `project` = project settings / dirty;
 *  `systems` = the systems-structure list; the rest name their store. */
export type StoreChannel = "project" | "systems" | "recent" | "userConfig" | "bindings";

/** The full greenfield store graph, plus the shared backend + role registry the stores hang off. */
export interface AppStores {
  backend: Backend;
  registry: RoleRegistry;
  recent: RecentStore;
  userConfig: UserConfigStore;
  bindings: BindingsStore;
  project: ProjectStore;
  /** Turns a user's file pick (ROM / .sav) into the right systems op — the menu's Load / Add items. */
  fileSelection: FileSelection;
}

export interface ComposeOptions {
  /** The native backend the stores drive. Defaults to the real RPC backend. */
  backend?: Backend;
  /** Called with the changed channel on every store mutation. Defaults to a no-op. */
  notify?: (channel: StoreChannel) => void;
}

/** Build the store graph over `backend` (the real native backend by default). Mirrors
 *  pluginControlPlane.ts's sequence and additionally composes the config/bindings stores (nothing in
 *  src/ constructs them yet), loading each store's on-disk state. Does NOT wire the DSP. */
export function composeAppStores({ backend = createRealBackend(), notify = () => {} }: ComposeOptions = {}): AppStores {
  const registry = buildAppRegistry();

  const recent = new RecentStore(backend, () => notify("recent"));
  recent.load();

  // Also re-fire "bindings": resolvedBindings() is derived from userConfig's active-profile pointers, so
  // switching the active profile (or any config change) invalidates the resolved bindings snapshot too.
  const userConfig = new UserConfigStore(backend, () => {
    notify("userConfig");
    notify("bindings");
  });
  userConfig.load();

  const bindings = new BindingsStore(backend, userConfig, () => notify("bindings"));
  bindings.ensureDefaults();

  const project = new ProjectStore(backend, recent, registry);
  project.setOnSystemsChange(() => notify("systems"));
  project.setOnChange(() => notify("project"));
  // Focus is transient: re-render the tiles on the systems channel, but bypass dirty + the DSP re-project.
  project.systems.setOnFocusChange(() => notify("systems"));

  const fileSelection = new FileSelection(backend, project.systems);

  return { backend, registry, recent, userConfig, bindings, project, fileSelection };
}
