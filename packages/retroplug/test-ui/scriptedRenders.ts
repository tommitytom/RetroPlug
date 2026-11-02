// A store graph whose renders are scripted, for the UI tests of System > Render and the tile badge. The harness has an
// engine, but a real render takes seconds and a test wants to see the spec the menu sent and to put a job in each
// state; so the graph is composed over the real backend with engineRun replaced by controllable jobs, and published the
// way the plugin's control plane publishes its own (pluginNamespace().stores), which the next mount of the UI picks up.

import { composeAppStores, type AppStores, type StoreChannel } from "../src/appStores";
import { createRealBackend } from "../src/realBackend";
import { jobHandle, type EngineRunOptions, type EngineRunResult } from "../src/offline";
import { ui } from "./uiHarness";

/** One render the UI submitted: what it was asked, and the controls the test settles it with. */
export interface ScriptedRender {
  spec: Record<string, unknown>;
  options: EngineRunOptions;
  cancelled: number;
  settle(result: EngineRunResult): void;
}

export interface ScriptedStores {
  stores: AppStores;
  renders: ScriptedRender[];
  /** Put the plugin namespace back as it was and remount the UI on a graph of its own. */
  restore(): void;
}

type Namespace = { stores?: AppStores; setUiNotify?: (fn: ((channel: StoreChannel) => void) | null) => void };

/** `real`: keep the backend's own engineRun (real jobs on the harness's engine) and script nothing. */
export function installScriptedStores(real = false): ScriptedStores {
  const sym = Symbol.for("plugin");
  const g = globalThis as Record<symbol, unknown>;
  const before = g[sym] as Namespace | undefined;
  const ns: Namespace = before ?? {};
  const saved = { stores: ns.stores, setUiNotify: ns.setUiNotify };

  const renders: ScriptedRender[] = [];
  const realBackend = createRealBackend({ debug: false });
  const backend = real ? realBackend : {
    ...realBackend,
    engineRun: (_kind: string, spec: object | string, options: EngineRunOptions = {}) => {
      let resolve!: (r: EngineRunResult) => void;
      const promise = new Promise<EngineRunResult>((res) => (resolve = res));
      const job: ScriptedRender = {
        spec: (typeof spec === "string" ? JSON.parse(spec) : JSON.parse(JSON.stringify(spec))) as Record<string, unknown>,
        options,
        cancelled: 0,
        settle: resolve,
      };
      renders.push(job);
      return jobHandle(promise, () => {
        job.cancelled++;
        return true;
      });
    },
  };

  let uiNotify: ((channel: StoreChannel) => void) | null = null;
  const stores = composeAppStores({ backend, notify: (channel) => uiNotify?.(channel) });
  ns.stores = stores;
  ns.setUiNotify = (fn) => {
    uiNotify = fn;
  };
  g[sym] = ns;
  ui.boot();
  ui.reopen(); // the UI's store provider reads the namespace as it mounts

  return {
    stores,
    renders,
    restore() {
      ns.stores = saved.stores;
      ns.setUiNotify = saved.setUiNotify;
      if (before === undefined) delete g[sym];
      ui.reopen();
    },
  };
}
