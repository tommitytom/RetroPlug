// The controller-app registry: name -> app module, mirroring RoleRegistry (systemRoles.ts).
//
// This is what "scriptable in TS" means for v1 (docs/launchpad-plan.md 0). An app is a module conforming
// to an interface, registered by name, with a zod schema owning its config shape, defaults and clamping -
// the same rails a DSP role runs on, down to reusing RoleConfigSchema rather than inventing a second
// config abstraction. Loading user scripts at runtime stays the deferred extension model (spec/07).
//
// The control plane's view: each app's runtime (apps/index.ts, zod-free, which the audio side imports alone) with
// its label and its config's schema.

import { z, boolField, clampedInt, enumField } from "../configSchema";
import type { RoleConfigSchema } from "../systemRoles";
import type { ControllerApp } from "./session";
import { CONTROLLER_APPS, type ControllerAppRuntime } from "./apps";
import { MAX_PAGE, QUANTISE_VALUES } from "./apps/lsdjMidiMap";

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

/** Each built-in app's menu label and config schema, by id: what the control plane adds to its runtime. The one
 *  place a new app's schema is added; its runtime is added to CONTROLLER_APPS. */
export const APP_CONFIGS: Record<string, { label: string; schema: RoleConfigSchema }> = {
  "lsdj-midimap": {
    label: "LSDj MI.MAP",
    // quantise: when a pressed pad's launch actually fires. Default `bar` - one bar is also exactly one
    // LSDj phrase at the factory groove, so launches land where the music does. follow: scroll the
    // 16-row window to keep the playhead visible. page: which window to start on.
    schema: z.object({
      quantise: enumField(QUANTISE_VALUES, "bar"),
      follow: boolField(true),
      page: clampedInt(0, MAX_PAGE, 0),
    }),
  },
};

/** Register the built-in controller apps: every runtime in CONTROLLER_APPS, in its order, with its label and
 *  schema. An app with no label and schema here is refused when this runs, rather than reaching a menu half-made.
 *  `runtimes` is what is registered (CONTROLLER_APPS unless a test hands others). */
export function registerControllerApps(registry: ControllerRegistry, runtimes: readonly ControllerAppRuntime[] = CONTROLLER_APPS): void {
  for (const { id, app } of runtimes) {
    const config = Object.prototype.hasOwnProperty.call(APP_CONFIGS, id) ? APP_CONFIGS[id] : undefined;
    if (!config) throw new Error(`controller app "${id}" has no label or config schema in registry.ts`);
    registry.register({ id, label: config.label, schema: config.schema, app });
  }
}

/** `config` as app `id`'s schema parses it (defaults filled, values clamped): the app config the control plane hands
 *  the controller node (graph/controller.ts), so the audio side parses nothing and imports no schema. An id no app
 *  has gets an empty config, which nothing reads: its node runs nothing. */
export function parseAppConfig(id: string, config: unknown): Record<string, unknown> {
  const entry = Object.prototype.hasOwnProperty.call(APP_CONFIGS, id) ? APP_CONFIGS[id] : undefined;
  return entry ? entry.schema.parse(config ?? {}) : {};
}
