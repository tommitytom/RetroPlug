// The two core-config "system" roles, for the tests that want a registry holding just those. They are
// @retroplug/sameboy's and @retroplug/mesen's own (each package's app module contributes it to the product's registry,
// which buildAppRegistry builds); a test that is about one role's config, or about the graph that config feeds,
// registers the roles here rather than the whole app's registry, so a feature role's schema cannot leak into what it
// asserts.
import type { RoleRegistry } from "@retroplug/core/systemRoles";
import { SAMEBOY_ROLE } from "@retroplug/sameboy";
import { MESEN_ROLE } from "@retroplug/mesen";

/** Register SameBoy's and Mesen's roles, in the order the product's registry takes them. */
export function registerCoreRoles(registry: RoleRegistry): void {
  registry.registerRole(SAMEBOY_ROLE);
  registry.registerRole(MESEN_ROLE);
}
