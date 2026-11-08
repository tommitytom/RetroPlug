// The control plane's half of the controller-app registry: each app's menu label and config schema, by id.
//
// The registry mechanism (ControllerRegistry) is @retroplug/launchpad/registry.ts, zod-free, because the session
// node looks an app up without carrying a schema. This module pairs every runtime with the label and schema the
// menu and the project's config validation need, reusing RoleConfigSchema rather than inventing a second config
// abstraction (the same rails a DSP role's config runs on).

import { z } from "zod";
import { boolField, clampedInt, enumField } from "@retroplug/core/configFields";
import type { RoleConfigSchema } from "@retroplug/core/systemRoles";
import { ControllerRegistry, type ControllerAppType } from "@retroplug/launchpad/registry";
import { CONTROLLER_APPS, type ControllerAppRuntime } from "@retroplug/launchpad/apps";
import { MAX_PAGE, QUANTISE_VALUES } from "@retroplug/launchpad/apps/lsdjMidiMap";

export { ControllerRegistry, type ControllerAppType };

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
