// The controller apps' runtime: each app's id, as config stores it, and the function that runs it. No schema, so a
// module that imports this carries no zod: the controller session's script node (script/nodes/controllerSession.ts),
// which runs on the audio side, takes its app config already parsed and needs only the function. The registry
// (registry.ts) pairs each with its menu label and its config's zod schema for the control plane.
import type { ControllerApp } from "../session";
import { lsdjMidiMap } from "./lsdjMidiMap";

export interface ControllerAppRuntime {
  /** Stable id, as stored in config. */
  readonly id: string;
  readonly app: ControllerApp;
}

/** Every built-in app, in the order the registry lists them. The one place a new app's runtime is added; its label
 *  and schema go in registry.ts. */
export const CONTROLLER_APPS: readonly ControllerAppRuntime[] = [{ id: "lsdj-midimap", app: lsdjMidiMap }];

/** The app registered under `id`, or undefined for an unknown one. */
export function controllerApp(id: string): ControllerApp | undefined {
  for (let i = 0; i < CONTROLLER_APPS.length; i++) if (CONTROLLER_APPS[i].id === id) return CONTROLLER_APPS[i].app;
  return undefined;
}
