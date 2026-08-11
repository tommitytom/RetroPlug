// The controller-app registry: name -> app module, mirroring RoleRegistry (systemRoles.ts).
//
// This is what "scriptable in TS" means for v1 (docs/launchpad-plan.md 0). An app is a module conforming
// to an interface, registered by name, with a zod schema owning its config shape, defaults and clamping -
// the same rails a DSP role runs on, down to reusing RoleConfigSchema rather than inventing a second
// config abstraction. Loading user scripts at runtime stays the deferred extension model (spec/07).

import { z, boolField, clampedInt, enumField } from "../configSchema";
import type { RoleConfigSchema } from "../systemRoles";
import type { ControllerApp } from "./session";
import { lsdjMidiMap, MAX_PAGE, QUANTISE_VALUES } from "./apps/lsdjMidiMap";

export interface ControllerAppType {
  /** Stable id, as stored in config. */
  readonly id: string;
  /** Menu label. */
  readonly label: string;
  readonly schema: RoleConfigSchema;
  readonly app: ControllerApp;
}

export class ControllerRegistry {
  private readonly types = new Map<string, ControllerAppType>();

  register(type: ControllerAppType): void {
    this.types.set(type.id, type);
  }

  get(id: string): ControllerAppType | undefined {
    return this.types.get(id);
  }

  /** Every registered app, in registration order - the menu's source. */
  list(): ControllerAppType[] {
    return [...this.types.values()];
  }

  /** A full config for `id` with defaults filled and values clamped, or undefined for an unknown app. */
  defaultConfig(id: string): Record<string, unknown> | undefined {
    return this.types.get(id)?.schema.parse({});
  }
}

/** Register the built-in controller apps. The one place a new app is added. */
export function registerControllerApps(registry: ControllerRegistry): void {
  registry.register({
    id: "lsdj-midimap",
    label: "LSDj MI.MAP",
    // quantise: when a pressed pad's launch actually fires. Default `bar` - one bar is also exactly one
    // LSDj phrase at the factory groove, so launches land where the music does. follow: scroll the
    // 16-row window to keep the playhead visible. page: which window to start on.
    schema: z.object({
      quantise: enumField(QUANTISE_VALUES, "bar"),
      follow: boolField(true),
      page: clampedInt(0, MAX_PAGE, 0),
    }),
    app: lsdjMidiMap,
  });
}
