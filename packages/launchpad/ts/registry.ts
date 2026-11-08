// The controller-app registry mechanism: name -> app module, mirroring RoleRegistry (@retroplug/core's
// systemRoles.ts).
//
// An app is a function over the session's context, registered by name, with a config schema owning its shape,
// defaults and clamping - the same rails a DSP role runs on, down to reusing RoleConfigSchema rather than
// inventing a second config abstraction. The schema itself belongs to the app's owner: this module only holds the
// table.
//
// The control plane's half (an app's label and its config's schema) is the shell's registry, which pairs each runtime
// it finds here with its schema. Loading user scripts at runtime stays the deferred extension model (spec/07).

import type { RoleConfigSchema } from "@retroplug/core/systemRoles";
import type { ControllerApp } from "./session";

export interface ControllerAppType {
  /** Stable id, as stored in config. */
  readonly id: string;
  /** Menu label. */
  readonly label: string;
  readonly schema: RoleConfigSchema;
  readonly app: ControllerApp;
}

export class ControllerRegistry {
  // An array, not a Map: this module is in the portable core, and MicroQuickJS has no Map. The tables are a handful of
  // apps, so a linear lookup costs nothing a menu would notice.
  private readonly types: ControllerAppType[] = [];

  register(type: ControllerAppType): void {
    for (let i = 0; i < this.types.length; i++) {
      if (this.types[i].id !== type.id) continue;
      this.types[i] = type; // a re-registration replaces in place, keeping the app's position
      return;
    }
    this.types.push(type);
  }

  get(id: string): ControllerAppType | undefined {
    for (let i = 0; i < this.types.length; i++) if (this.types[i].id === id) return this.types[i];
    return undefined;
  }

  /** Every registered app, in registration order - the menu's source. */
  list(): ControllerAppType[] {
    return this.types.slice();
  }

  /** A full config for `id` with defaults filled and values clamped, or undefined for an unknown app. */
  defaultConfig(id: string): Record<string, unknown> | undefined {
    const type = this.get(id);
    return type ? type.schema.parse({}) : undefined;
  }
}
