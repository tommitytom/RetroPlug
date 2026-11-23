// The control plane's role registry as a live view (docs/downloadable-packs-m2a-plan.md): a store graph holds one
// RoleRegistry for its whole life, so the one buildAppRegistry builds adopts the roles and ROM providers committed
// after it was made, at its next read. The registries only grow, so it keeps how many of each it has taken and takes the
// rest, in registration order (which is the providers' behaviour: the first to match a ROM fixes its roles' order).
import { RoleRegistry, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
import type { Core, Platform } from "@retroplug/core/platform";
import { rolesRegistry, romProvidersRegistry } from "./contributions/app";

export class AppRoleRegistry extends RoleRegistry {
  private rolesTaken = 0;
  private providersTaken = 0;

  /** The built-ins first, as buildAppRegistry always applied them: a caller's own registerRole or registerRomProvider
   *  then comes after them (a hand-registered provider runs after the product's, a hand-registered role replaces a
   *  built-in of its kind), and only a later commit's entries are adopted lazily. */
  constructor() {
    super();
    this.adopt();
  }

  /** Take the roles and providers committed since the last read. */
  private adopt(): void {
    const roles = rolesRegistry.items();
    for (; this.rolesTaken < roles.length; this.rolesTaken++) super.registerRole(roles[this.rolesTaken]);
    const providers = romProvidersRegistry.items();
    for (; this.providersTaken < providers.length; this.providersTaken++) {
      super.registerRomProvider(providers[this.providersTaken]);
    }
  }

  override roleType(kind: string): RoleType | undefined {
    this.adopt();
    return super.roleType(kind);
  }

  override systemRoleFor(core: string): RoleType | undefined {
    this.adopt();
    return super.systemRoleFor(core);
  }

  override defaultRoles(core: Core, platform: Platform, header: Uint8Array, embeddedRom = ""): RoleInstance[] {
    this.adopt();
    return super.defaultRoles(core, platform, header, embeddedRom);
  }
}
