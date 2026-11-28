// Which package provided what a saved system names (docs/downloadable-packs-m3-design.md section 4): its platform and
// core (the console's package), its embedded ROM's and each role's, so a project opened where a pack is missing can
// name the pack it needs. Written at every save, read back with the system; additive, so no migration step (a reader
// that does not know it ignores it). A pack is named and versioned from the pack list (__rp_packList), a built-in
// package by its pack.json name (the generated PACKAGE_NAMES) or its id.
import { z } from "zod";
import { consolesRegistry } from "./contributions/consoleRegistries";
import { embedsRegistry, rolesRegistry } from "./contributions/app";
import { derived } from "./contributions/registry";
import { PACKAGE_NAMES } from "./generated/packages";
import { nativePackHost } from "./packs/packHost";

/** A package as a saved project names it: its id, its display name, and its version when it is a pack. */
export interface PackRef {
  readonly id: string;
  readonly name: string;
  readonly version?: string;
}

export interface Provenance {
  /** The console's package: it provides the platform and its core. */
  readonly platform?: PackRef;
  /** The embedded ROM's package. */
  readonly embed?: PackRef;
  /** Each role kind's package, by kind. */
  readonly roles?: Readonly<Record<string, PackRef>>;
}

const packRefSchema = z.object({ id: z.string(), name: z.string(), version: z.string().optional() });

const ref = (r: PackRef): PackRef =>
  r.version !== undefined ? { id: r.id, name: r.name, version: r.version } : { id: r.id, name: r.name };

/** A parsed provenance with every absent member left out (zod's `.catch(undefined)` leaves the key in place). */
function stripUndefined(p: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> }): Provenance {
  const out: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> } = {};
  if (p.platform) out.platform = ref(p.platform);
  if (p.embed) out.embed = ref(p.embed);
  if (p.roles) {
    const roles: Record<string, PackRef> = {};
    for (const [kind, r] of Object.entries(p.roles)) put(roles, kind, ref(r));
    out.roles = roles;
  }
  return out;
}

/** Tolerant: a malformed member reads as absent, and the system it is on still parses. The output carries only the
 *  members that read, so a parsed system compares equal to the provenance its file wrote. */
export const provenanceSchema = z
  .object({
    platform: packRefSchema.optional().catch(undefined),
    embed: packRefSchema.optional().catch(undefined),
    roles: z.record(z.string(), packRefSchema).optional().catch(undefined),
  })
  .transform(stripUndefined);

/** A raw provenance member as this build reads it, or undefined for none / not an object. */
export function readProvenance(raw: unknown): Provenance | undefined {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return undefined;
  const r = provenanceSchema.safeParse(raw);
  return r.success ? r.data : undefined;
}

/** Store `value` as an own property: a hand-edited `__proto__` kind is data, never the record's prototype. */
function put(record: Record<string, PackRef>, key: string, value: PackRef): void {
  Object.defineProperty(record, key, { value, enumerable: true, writable: true, configurable: true });
}

/** The registries' providers, rebuilt after a commit (a pack's commit follows its native load, so the pack list is read
 *  again with them). */
const providers = derived(() => {
  const platforms = new Map<string, string>();
  for (const { pkg, item } of consolesRegistry.entries()) for (const p of item.platforms) platforms.set(p.id, pkg);
  const roles = new Map<string, string>();
  for (const { pkg, item } of rolesRegistry.entries()) roles.set(item.kind, pkg);
  const embeds = new Map<string, string>();
  for (const { pkg, item } of embedsRegistry.entries()) embeds.set(item.id, pkg);
  const packs = new Map(nativePackHost().list().map((p) => [p.id, p]));
  return { platforms, roles, embeds, packs };
});

/** The package `pkg` (a contribution's `pkg`) as a project records it. */
export function packRefOf(pkg: string): PackRef {
  const pack = providers().packs.get(pkg);
  if (pack) return { id: pkg, name: pack.name ?? pkg, version: pack.version };
  const name = Object.prototype.hasOwnProperty.call(PACKAGE_NAMES, pkg) ? PACKAGE_NAMES[pkg] : pkg;
  return { id: pkg, name };
}

const providerOf = (map: ReadonlyMap<string, string>, key: string): PackRef | undefined => {
  const pkg = map.get(key);
  return pkg === undefined ? undefined : packRefOf(pkg);
};

/** What a provenance is written from: a system's platform, embedded ROM and roles (in the order a save writes them),
 *  and the provenance it was read with. */
export interface ProvenanceSource {
  readonly platform: string;
  readonly embeddedRom: string;
  readonly roles: readonly { readonly kind: string }[];
  readonly provenance?: Provenance;
}

/** The provenance a save writes for `s`: each thing it names, by the package that registers it now, else by the
 *  package the file named for it (a role kind nothing registers keeps its pack). Undefined when it names nothing. */
export function provenanceOf(s: ProvenanceSource): Provenance | undefined {
  const { platforms, roles: roleProviders, embeds } = providers();
  const platform = providerOf(platforms, s.platform) ?? s.provenance?.platform;
  const embed = s.embeddedRom ? (providerOf(embeds, s.embeddedRom) ?? s.provenance?.embed) : undefined;
  const roles: Record<string, PackRef> = {};
  for (const { kind } of s.roles) {
    if (Object.prototype.hasOwnProperty.call(roles, kind)) continue;
    const stored = s.provenance?.roles;
    const r =
      providerOf(roleProviders, kind) ??
      (stored && Object.prototype.hasOwnProperty.call(stored, kind) ? stored[kind] : undefined);
    if (r) put(roles, kind, r);
  }
  const out: { platform?: PackRef; embed?: PackRef; roles?: Record<string, PackRef> } = {};
  if (platform) out.platform = platform;
  if (embed) out.embed = embed;
  if (Object.keys(roles).length) out.roles = roles;
  return Object.keys(out).length ? out : undefined;
}
