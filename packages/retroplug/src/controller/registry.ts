// The control plane's view of the controller apps: the generated registry's list (launchpad's ts/packageContributions
// declares the `controllerApps` key of a package's app module), held in the launchpad package's registry so the menu
// cycles it and the publishers parse a saved config with the app's own schema.
//
// Nothing is registered here by hand: the list is the desktop product's, in product order, so an app a package
// contributes appears without the shell naming it, and an app the product loses is refused by the generated module
// rather than found missing at run time. The runtime half - the app's function, its playback model and its target - is
// the same package's `protocol.apps` entry, registered by the script bundle (src/generated/scriptNodes.ts).
//
// The registry mechanism is @retroplug/launchpad/registry.ts; this module only fills it and adds what the shell reads.
import { ControllerRegistry, type ControllerAppContribution } from "@retroplug/launchpad/registry";
import { CONTROLLER_APPS } from "../generated/packages";

export { ControllerRegistry, type ControllerAppContribution };

/** Every controller app the product carries, in product order. */
export const controllerApps = new ControllerRegistry();
for (const app of CONTROLLER_APPS) controllerApps.register(app);

/** The app registered under `id`, or undefined for an unknown one. */
export function controllerApp(id: string): ControllerAppContribution | undefined {
  return controllerApps.get(id);
}

/** `config` as app `id`'s schema parses it (defaults filled, values clamped): the app config the control plane hands the
 *  controller node (graph/controller.ts), so the audio side parses nothing and imports no schema. An id no app has gets
 *  an empty config, which nothing reads: its node runs nothing. */
export function parseAppConfig(id: string, config: unknown): Record<string, unknown> {
  const app = controllerApps.get(id);
  return app ? app.config.parse(config ?? {}) : {};
}
