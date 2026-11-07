// The product's ROM providers as the registry applies them: each is its package's app contribution, generated into
// src/generated/packages.ts, and this registers that list into a control-plane RoleRegistry in the product's order -
// which is behaviour here, since the first provider to match a ROM fixes its feature roles' order. Tests that build a
// registry by hand call it too, so a ROM provider's behaviour is testable without the app's roles.
import type { RoleRegistry } from "@retroplug/core/systemRoles";
import { ROM_PROVIDERS } from "./generated/packages";

/** Register the built-in feature-role providers, in the product's order, into `registry`. */
export function registerRomProviders(registry: RoleRegistry): void {
  for (const provider of ROM_PROVIDERS) registry.registerRomProvider(provider);
}
