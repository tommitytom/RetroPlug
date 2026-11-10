// The shell's UI before the views moved, as the check recorded it: a tracker id in a role test, and a ROM extension in
// a path. The walk reads ui/ as it reads src/ and cli/, so both are hits there; the real tree's ui/ names neither.
import { hasRole, type RoleInstance } from "@retroplug/core/systemRoles";

export function isRisa(roles: readonly RoleInstance[]): boolean {
  return hasRole(roles, "risa");
}

export const RISA_ROM = "/roms/risa.nes";
