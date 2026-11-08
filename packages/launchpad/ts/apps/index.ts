// The controller apps' RUNTIME half: each app's id, the function that runs it, the playback model it reads and the
// launch target its launches go to (docs/modular-graph-phase-f3-design.md section 4). No schema and no zod: a module
// that imports this runs in the script node's bundle, where there is no schema library.
//
// The entries are REGISTERED, not listed here: the generated script entry (src/generated/scriptNodes.ts) registers every
// package's runtime before it registers the session node, and the node looks an app up by id. That is what keeps a
// tracker's app out of this package (an app that drives an LSDj cart lives with the tracker's bridge) while the node
// still runs it, and what makes the bundle carry exactly the apps the product's manifests declare.
//
// No app is built here: the node builds one lazily, from the settings the control plane resolved, and holds the model
// across rebuilds so a settings push does not restart the song.
import type { PredictivePlaybackModel } from "@retroplug/tracker/playbackModel";
import type { ControllerApp } from "../session";
import type { TrackerTarget } from "../launchTarget";

/** The playback model an app reads: one it is DRIVEN through, plus the two hooks the session node's settings handling
 *  needs - adopting a new song table IN PLACE (a chain added on the cart lights up without the playhead moving) and the
 *  start-edge re-anchor. A model that only observes a cart implements the same two as no-ops. */
export interface ControllerPlaybackModel extends PredictivePlaybackModel {
  /** Adopt a new per-row timing table in place, leaving the playhead where it is. */
  setRowTicks(table: unknown): void;
  /** Move each channel to where the cart actually is: a row index, or null for a channel that is not playing. */
  anchorTo(rows: readonly (number | null)[]): void;
}

/** One app's runtime half. */
export interface ControllerAppRuntime {
  /** Stable id, the same string the control-plane half registers under. */
  readonly id: string;
  /** Build the app. */
  readonly factory: () => ControllerApp;
  /** The playback model the app reads, built from the song's per-row timing table (whatever shape the app's song
   *  type has: this package never reads it). */
  readonly playbackModel: (songRowTicks: unknown) => ControllerPlaybackModel;
  /** Where launches go: the target over a `send` that reaches the cart (or the host's MIDI out). */
  readonly target: (send: (data: number[]) => void) => TrackerTarget;
}

const runtimes: ControllerAppRuntime[] = [];

/** Register every app's runtime, in the order the product lists them. The generated script entry calls this once,
 *  before the session node can run, so a document naming an app finds it from its first block. */
export function registerControllerApps(apps: readonly ControllerAppRuntime[]): void {
  for (let i = 0; i < apps.length; i++) {
    const app = apps[i];
    let replaced = false;
    for (let j = 0; j < runtimes.length; j++) {
      if (runtimes[j].id !== app.id) continue;
      runtimes[j] = app;
      replaced = true;
      break;
    }
    if (!replaced) runtimes.push(app);
  }
}

/** The app registered under `id`, or undefined for an unknown one. */
export function controllerApp(id: string): ControllerAppRuntime | undefined {
  for (let i = 0; i < runtimes.length; i++) if (runtimes[i].id === id) return runtimes[i];
  return undefined;
}

/** Every registered app, in registration order: what a test or a host lists. */
export function controllerApps(): ControllerAppRuntime[] {
  return runtimes.slice();
}
