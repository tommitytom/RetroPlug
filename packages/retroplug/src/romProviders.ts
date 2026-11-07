// The built-in ROM providers as the registry applies them: the shell's app contribution (romProviderList.ts) is a
// list, and this registers it into a control-plane RoleRegistry in product order. Tests that build a registry by
// hand call it too, so a ROM provider's behaviour is testable without the app's roles.
import type { RoleRegistry } from "./systemRoles";
import { ROM_PROVIDERS } from "./generated/packages";

/** Register the built-in feature-role providers, in the product's order, into `registry`. */
export function registerRomProviders(registry: RoleRegistry): void {
  for (const provider of ROM_PROVIDERS) registry.registerRomProvider(provider);
}
