// The controller-app registry: name -> the app's CONTROL-PLANE half (docs/modular-graph-phase-f3-design.md section 4).
//
// An app is registered by id, which is the string a project stores. Two runtimes hold its two halves: the control
// plane (the menu, the project's config validation) has zod and holds the label and the config's schema, and the script
// node's bundle (which runs the app on the audio thread) has none. This module is the control plane's table; the runtime
// half is the package's `protocol.apps` entry, joined to it by id (ts/apps/index.ts).
//
// Nothing here names a tracker or a cart: which sync mode an app's launches need is DATA in its manifest's
// `protocol.apps` entry (the node type, the mode and the polled value that counts as listening), applied by
// graph/controller.ts, so a new app needs no code in the shell.
//
// Loading user scripts at runtime stays the deferred extension model (spec/07).

import type { RoleConfigSchema } from "@retroplug/core/systemRoles";

/** One of an app's knobs, as the menu offers it: which key of the config it edits and the row's text. A knob the app
 *  leaves out is one the menu has no row for (the session's live window, say), which is why this is a list of its own
 *  rather than something the shell derives: labels are the app's words, and only it has them. */
export interface ControllerAppField {
  /** A key of the app's config schema: what the row reads and writes. */
  readonly key: string;
  /** The row's text. */
  readonly label: string;
  /** A choice row: the values (each one the schema accepts) with the text the cycler shows, in offer order. */
  readonly choices?: readonly { readonly value: string | number; readonly label: string }[];
  /** An Off/On row (no choices). */
  readonly toggle?: boolean;
}

/** One app's control-plane half: what the registry holds, and what a package's app module contributes. */
export interface ControllerAppContribution {
  /** Stable id, as a project stores it. */
  readonly id: string;
  /** The menu's text. */
  readonly label: string;
  /** The app's own knobs: the schema the publishers parse a saved config with, and the menu its rows from. */
  readonly config: RoleConfigSchema;
  /** The app's rows in the Launchpad submenu, in order. A field's current value is the project's, and its DEFAULT is the
   *  schema's (`config.parse({})`), so a project stored before a knob existed still shows the value the app will run. */
  readonly menu?: readonly ControllerAppField[];
}

export class ControllerRegistry {
  // An array, not a Map: this module is in the portable core, and MicroQuickJS has no Map. The table is a handful of
  // apps, so a linear lookup costs nothing a menu would notice.
  private readonly apps: ControllerAppContribution[] = [];

  register(app: ControllerAppContribution): void {
    for (let i = 0; i < this.apps.length; i++) {
      if (this.apps[i].id !== app.id) continue;
      this.apps[i] = app; // a re-registration replaces in place, keeping the app's position
      return;
    }
    this.apps.push(app);
  }

  get(id: string): ControllerAppContribution | undefined {
    for (let i = 0; i < this.apps.length; i++) if (this.apps[i].id === id) return this.apps[i];
    return undefined;
  }

  /** Every registered app, in registration order - the menu's source. */
  list(): ControllerAppContribution[] {
    return this.apps.slice();
  }

  /** A full config for `id` with defaults filled and values clamped, or undefined for an unknown app. */
  defaultConfig(id: string): Record<string, unknown> | undefined {
    const app = this.get(id);
    return app ? app.config.parse({}) : undefined;
  }
}
