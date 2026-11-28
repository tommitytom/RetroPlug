// What a project needs that this build lacks (docs/downloadable-packs-m3-design.md sections 1, 2 and 8): a placeholder
// system's platform, core or embedded ROM and the role kinds it stores that nothing registers, and a live system's
// missing roles; the pack each comes from (the file's provenance) and whether that pack is missing or refused. The
// shell names no pack, platform or kind: every string the UI shows is built here from a need.
import type { PackRef } from "../provenance";
import type { SystemView } from "../systemsStore";
import { engineRefusals, isLoadedPack, nativeRefusals, tsRefusals } from "./refusals";

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

/** `need` with its status as the packs stand now: refused (with the reason, and the refused folder's version) when its
 *  pack was refused natively, in TS or by this context's Engine; else refused with `fallback` when the caller has a
 *  reason of its own (a role node the engine could not place); else missing. `engineOnly` skips the native and TS
 *  refusals. A "rom" need names no pack, so it is left as it is. */
export function statusOf(need: PackNeed, fallback?: string, engineOnly = false): PackNeed {
  if (need.what === "rom") return need;
  if (engineOnly) {
    // A role the engine could not place is one TS registered, so its pack loaded: a native or TS refusal under its id
    // belongs to another folder or predates a live load, and only the Engine's reason (then the planner's) applies.
    // A pack that loaded is skipped the same way on the other path (isLoadedPack).
    const reason = (need.pack ? engineRefusals().get(need.pack.id) : undefined) ?? fallback;
    const base = need.pack ? needOf(need.what, need.key, need.pack) : needOf(need.what, need.key);
    return reason !== undefined ? { ...base, status: "refused", reason } : base;
  }
  const base: PackNeed = need.pack
    ? { what: need.what, key: need.key, pack: need.pack, status: "missing" }
    : needOf(need.what, need.key);
  const id = need.pack?.id;
  // A pack that loaded here has no native or TS refusal that counts: the table's is another folder's.
  const loaded = id !== undefined && isLoadedPack(id);
  const n = id && !loaded ? nativeRefusals().get(id) : undefined;
  if (n && need.pack) {
    const pack = n.version !== undefined ? { ...need.pack, version: n.version } : need.pack;
    return { ...base, pack, status: "refused", reason: n.reason, ...(n.needsUpdate ? { needsUpdate: true } : {}) };
  }
  const own = id && !loaded ? tsRefusals().get(id) : undefined;
  const reason = (own ?? (id ? engineRefusals().get(id) : undefined)) ?? fallback;
  return reason !== undefined ? { ...base, status: "refused", reason } : base;
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
  if (need.what === "rom") {
    if (need.status === "missing") return `The ROM ${need.key} is not one this build reads`;
    return need.reason === undefined
      ? `The ROM ${need.key} could not be built`
      : `The ROM ${need.key} could not be built: ${need.reason}`;
  }
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
