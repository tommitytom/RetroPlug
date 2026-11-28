// What a project needs that this build lacks (docs/downloadable-packs-m3-design.md sections 1, 2 and 8): a placeholder
// system's platform, core or embedded ROM and the role kinds it stores that nothing registers, and a live system's
// missing roles; the pack each comes from (the file's provenance) and whether that pack is missing or refused. The
// shell names no pack, platform or kind: every string the UI shows is built here from a need.
import type { PackRef } from "../provenance";
import type { SystemView } from "../systemsStore";

export type NeedWhat = "platform" | "core" | "embed" | "role" | "rom";

export interface PackNeed {
  readonly what: NeedWhat;
  /** The platform id, core id, embedded ROM id or role kind the file names; for "rom", the ROM file's name (a ROM this
   *  build's detectors do not read, which no pack resolves). */
  readonly key: string;
  /** The package the file says provides it; absent for a file saved before M3. */
  readonly pack?: PackRef;
  readonly status: "missing" | "refused";
  /** Why the pack was refused. */
  readonly reason?: string;
  /** The refusal is the pack's API level against this RetroPlug's: the "needs updating" card. */
  readonly needsUpdate?: boolean;
}

/** A need as the parse finds it: missing until statusOf reads the refusals. */
export function needOf(what: NeedWhat, key: string, pack?: PackRef): PackNeed {
  return pack ? { what, key, pack, status: "missing" } : { what, key, status: "missing" };
}

/** `need` with its status as the packs stand now. (Refusals are read here from the refusal sources, see
 *  src/packs/refusals.ts.) */
export function statusOf(need: PackNeed): PackNeed {
  return need;
}

/** One pack (or, with no provenance, one platform or kind) a project needs, and what it covers. */
export interface MissingPack {
  /** The pack's id, or `<what>:<key>` for a need the file names no pack for. */
  readonly id: string;
  readonly name: string;
  readonly version?: string;
  readonly status: "missing" | "refused";
  readonly reason?: string;
  readonly needsUpdate?: boolean;
  /** The placeholder systems that need it, and the live systems' roles. */
  readonly systems: number;
  readonly roles: number;
}

const groupOf = (need: PackNeed): string => need.pack?.id ?? `${need.what}:${need.key}`;

/** The distinct packs `views` (SystemsStore.slotView) need, in the order first met: each placeholder's needs (a system
 *  counted once per pack) and each live system's missing roles. */
export function missingPacksOf(views: readonly SystemView[]): MissingPack[] {
  const groups = new Map<string, { first: PackNeed; systems: Set<number>; roles: number }>();
  const add = (need: PackNeed, system: number, role: boolean): void => {
    const id = groupOf(need);
    let g = groups.get(id);
    if (!g) {
      g = { first: need, systems: new Set(), roles: 0 };
      groups.set(id, g);
    }
    if (role) g.roles++;
    else g.systems.add(system);
  };
  for (const v of views) {
    for (const need of v.placeholder?.needs ?? []) if (need.what !== "rom") add(need, v.id, false);
    for (const need of v.missingRoles ?? []) add(need, v.id, true);
  }
  return [...groups].map(([id, g]) => {
    const n = g.first;
    const version = n.pack?.version;
    return {
      id,
      name: n.pack?.name ?? n.key,
      ...(version !== undefined ? { version } : {}),
      status: n.status,
      ...(n.reason !== undefined ? { reason: n.reason } : {}),
      ...(n.needsUpdate ? { needsUpdate: true } : {}),
      systems: g.systems.size,
      roles: g.roles,
    };
  });
}

/** A need as a placeholder's card says it: "Needs the Mesen pack", "Mesen 1.2 needs updating for this RetroPlug". */
export function needText(need: PackNeed): string {
  if (need.what === "rom") return `The ROM ${need.key} is not one this build reads`;
  const name = need.pack?.name;
  if (need.status === "refused") {
    const who = name ? `${name}${need.pack?.version ? ` ${need.pack.version}` : ""}` : need.key;
    return need.needsUpdate ? `${who} needs updating for this RetroPlug` : `${who} is refused`;
  }
  return name ? `Needs the ${name} pack` : `Needs a pack for the ${need.what} ${need.key}`;
}

/** A missing role as a live tile's menu says it: "BlipToaster pack missing". */
export function roleNeedText(need: PackNeed): string {
  if (need.status === "refused") return needText(need);
  return `${need.pack?.name ?? need.key} pack missing`;
}

/** One row of the load's notice: the pack and what it covers. */
export function missingPackText(p: MissingPack): string {
  const covers = [
    ...(p.systems ? [`${p.systems} system${p.systems === 1 ? "" : "s"}`] : []),
    ...(p.roles ? [`${p.roles} role${p.roles === 1 ? "" : "s"}`] : []),
  ].join(", ");
  const state = p.status === "refused" ? (p.needsUpdate ? " (needs updating)" : " (refused)") : "";
  return `${p.name}${state}: ${covers}`;
}
