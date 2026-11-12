// A config row as a menu cycler (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.3): one place turns a row
// into the row the menu draws, shared by a console's role rows and a controller app's knobs, so both read a stored
// value through the same fallbacks and step through the same label list. A row is data a package declares; what a step
// DOES is the caller's, because that is the one part only a surface knows (systems.setRoleConfig, a project's
// controller).
import { roleConfig, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
import type { RoleRow } from "@retroplug/ui/contribution";
import { configRowCycler, rowShownFor, rowShownOn } from "@retroplug/ui/contribution";
import { separator, type MenuItem } from "@retroplug/ui/menu";
import { placedRole } from "../../../src/graph/protocolNodes";

export { configRowCycler };

/** What a role-row renderer reads of a system: the platform a row may be gated on and the roles whose configs hold the
 *  values. A view of a system satisfies it. */
export interface RoleRowSystem {
  readonly platform: string;
  readonly roles: readonly RoleInstance[];
}

/** The items a set of contributed rows builds for a system, in the rows' order: a row shows only on a system whose
 *  graph places its role (graph/protocolNodes.ts placedRole: the core role, which has no node, attaches to every system
 *  of that core, so this is the old `core === "sameboy"` gate, and a stored role the graph leaves out, LSDj's sync on
 *  an mGB cart after a ROM swap, shows nothing), where its platform and `when` gates allow, and a `separatorBefore` id
 *  is emitted once, before the first row that shows carrying it. A row's default is its role schema's
 *  (`schema.parse({})[key]`), which is what makes an untouched config read as the value the core really runs. */
export function roleRowItems(
  rows: readonly RoleRow[],
  sys: RoleRowSystem,
  roleType: (kind: string) => RoleType | undefined,
  apply: (role: string, key: string, value: string | number | boolean) => void,
): MenuItem[] {
  const items: MenuItem[] = [];
  const emitted = new Set<string>();
  const defaultsOf = new Map<string, Record<string, unknown>>();
  for (const row of rows) {
    if (!placedRole(sys.roles, row.role)) continue;
    if (!rowShownOn(row, sys.platform)) continue;
    let defaults = defaultsOf.get(row.role);
    if (!defaults) {
      defaults = (roleType(row.role)?.schema.parse({}) ?? {}) as Record<string, unknown>;
      defaultsOf.set(row.role, defaults);
    }
    const config = roleConfig(sys.roles, row.role) ?? {};
    if (row.when && !rowShownFor(row, config[row.when.key], defaults[row.when.key])) continue;
    if (row.separatorBefore && !emitted.has(row.separatorBefore)) {
      emitted.add(row.separatorBefore);
      items.push(separator(row.separatorBefore));
    }
    items.push(
      configRowCycler(row.id, row, config[row.key], defaults[row.key], (value) => apply(row.role, row.key, value)),
    );
  }
  return items;
}
