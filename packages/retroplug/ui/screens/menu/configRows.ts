// A config row as a menu cycler (docs/modular-graph-phase-f4b-design.md sections 4.2 and 4.3): one place turns a row
// into the row the menu draws, shared by a console's role rows and a controller app's knobs, so both read a stored
// value through the same fallbacks and step through the same label list. A row is data a package declares; what a step
// DOES is the caller's, because that is the one part only a surface knows (systems.setRoleConfig, a project's
// controller).
import { hasRole, roleConfig, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
import type { ConfigRow, RoleRow } from "@retroplug/ui/contribution";
import { readConfigRow, rowShownFor, rowShownOn } from "@retroplug/ui/contribution";
import { cycler, separator, type MenuItem } from "@retroplug/ui/menu";

/** One config row as the menu draws it. The value a row reads is the stored one when its type matches the schema's
 *  default, else the default (`readConfigRow`, core's rule), so a role row and a controller-app row fall back alike.
 *
 *  `index` overrides where the stored value points, for the one caller whose rows predate that rule: a controller app's
 *  knobs keep the index they have always shown for a hand-edited config, so an invalid stored value reads as it did
 *  (the labels and the value a step writes are the same either way). Omitted, the typed reader's index is used. */
export function configRowCycler(
  id: string,
  row: ConfigRow,
  stored: unknown,
  fallback: unknown,
  apply: (value: string | number | boolean) => void,
  index?: number,
): MenuItem {
  const reading = readConfigRow(row, stored, fallback);
  return cycler(id, row.label, [...reading.names], index ?? reading.index, (step) => apply(reading.at(step)));
}

/** What a role-row renderer reads of a system: the platform a row may be gated on and the roles whose configs hold the
 *  values. A view of a system satisfies it. */
export interface RoleRowSystem {
  readonly platform: string;
  readonly roles: readonly RoleInstance[];
}

/** The items a set of contributed rows builds for a system, in the rows' order: a row shows only on a system that has
 *  its role (the core role attaches to every system of that core, so this is the old `core === "sameboy"` gate), where
 *  its platform and `when` gates allow, and a `separatorBefore` id is emitted once, before the first row that shows
 *  carrying it. A row's default is its role schema's (`schema.parse({})[key]`), which is what makes an untouched config
 *  read as the value the core really runs. */
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
    if (!hasRole(sys.roles, row.role)) continue;
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
