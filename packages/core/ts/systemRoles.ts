// The generic per-system role system. A role is `{ kind, config }` keyed by a string
// - the core treats config as an opaque per-kind blob. Everything specific to a
// system (a backend's own knobs, or an optional feature like LSDj) is a role, so the
// core stays free of backend/LSDj knowledge. Extensions populate a kind-keyed
// RoleRegistry; a console package contributes its own system roles through its app module, and the generated registry
// (packages/retroplug/src/generated/packages.ts) hands them over. Ports the shape of native RoleConfig
// (RoleConfig.hpp), a
// tagged union on "kind", into a runtime registry, minus the hardcoded if/else chains.
//
// Two categories: "system" roles carry a backend's emulator settings (attached by
// backend kind); "feature" roles are behaviors attached by a ROM provider. What a role
// does on the audio thread (mGB, lsdj-sync, risa's host sync) is its protocol script node,
// which the generated protocol tables name for its kind and buildGraph places with the role's
// config as its settings: the registry holds the role as the app sees it (its schema and
// a load-time hook). Its UI-thread behavior (a role's own settings UI) stays a deferred
// `ui` seam: a role is never an opaque native blob.

import type { Platform, Core } from "./platform";
import type { ConstructSpec } from "./constructSpec";
import type { RomParameter } from "./romParameters";
import type { Host } from "./host";

export type RoleCategory = "system" | "feature";

/** A role on a system: a kind + its opaque config data. This is what serializes. */
export interface RoleInstance {
  kind: string;
  config: Record<string, unknown>;
}

/** The role of `kind` on a system, or undefined. Takes the bare list (not a SystemView) so it serves anything
 *  that carries roles: a view, a construct spec, a project entry. */
export function findRole(roles: readonly RoleInstance[] | undefined, kind: string): RoleInstance | undefined {
  return roles?.find((r) => r.kind === kind);
}

/** The config of the `kind` role, or undefined when the system has no such role. */
export function roleConfig(roles: readonly RoleInstance[] | undefined, kind: string): Record<string, unknown> | undefined {
  return findRole(roles, kind)?.config;
}

export function hasRole(roles: readonly RoleInstance[] | undefined, kind: string): boolean {
  return findRole(roles, kind) !== undefined;
}

/** Whether a system's graph places a role of `kind`: what a view asks before it treats the system as its cart's. A
 *  stored role list can keep a role the graph leaves out (an mGB cart still carrying lsdj-sync after a ROM swap), which
 *  `hasRole` still finds, so its config reads as stored; `placed` is the shell's answer (SystemInfo.placed), and a
 *  role with no protocol node is placed whenever it is present. */
export function isPlaced(sys: { readonly placed: readonly string[] }, kind: string): boolean {
  return sys.placed.includes(kind);
}

/** A role's config validator: a zod schema whose `.parse` fills defaults + clamps a
 *  (possibly partial/invalid) config into a full one. Structurally-typed so the
 *  registry stays zod-agnostic; the schemas themselves are built with zod
 *  (roleSchema.ts). `parse({})` yields the default config. */
export interface RoleConfigSchema {
  parse(config: unknown): Record<string, unknown>;
}

/** A registry entry contributed by an extension (or the core, for backend roles). */
export interface RoleType {
  kind: string;
  category: RoleCategory;
  /** The zod schema for this role's config — the single source of truth for its
   *  shape, defaults, and clamping. */
  schema: RoleConfigSchema;
  /** A load-time hook: transform the resolved `ConstructSpec` just before the core is instantiated
   *  (after paths + any seed blobs are resolved). ADDITIVE by contract — seed data that is otherwise
   *  absent (e.g. a fresh LSDj cart gets a valid empty sav), never clobber what's already there.
   *  Runs only for roles actually attached to the system, so it's inherently ROM-gated. `config` is this
   *  role's own config (e.g. an LSDj asset-override list), and `host` the two groups a hook may use: the
   *  file reads (the base ROM, a linked asset) and the PNG codec (a linked font image). */
  onConstruct?(spec: ConstructSpec, host: Host<"files" | "png">, config: Record<string, unknown>): ConstructSpec;
  /** The DAW automation parameters this role's ROM exposes, for the config the role carries (a BlipToaster's `chip`
   *  picks its build's set), or undefined for a role that exposes none. The projection onto the plugin's slot pool is
   *  the shell's (packages/retroplug/src/parameterMap.ts); what a control IS belongs to the cart. */
  parameters?(config: Record<string, unknown>): readonly RomParameter[];
  /** A config field this role derives from the ROM FILE rather than from the user's choices (BlipToaster's `chip`): the
   *  role says how much of the file it needs, and the store applies `derive` at every build, so a loaded project
   *  reflects the ROM on disk today.
   *
   *  `headerBytes(short)` is asked of the short header (the first `ROLE_HEADER_LEN` bytes) and answers how many bytes
   *  `derive` needs: a role whose fact is in the short header says so by returning the short length, and one that may
   *  need a deeper read says so for the files that need it, so the read happens only when a role asks. */
  deriveConfig?: RoleDerive;
  /** How much plugin latency this role adds to a host, for the config it carries: the DAW applies it as PDC, so a cart
   *  that locks its playback a fixed distance behind the host clock lands on the grid. Absent (or 0): none.
   *
   *  Only a role the system's protocol pipeline PLACES is asked: a role whose node the graph leaves out runs nothing,
   *  so a product that stored the role but does not place it adds no latency. */
  hostLatencyMs?(config: Record<string, unknown>): number;
  /** DEFERRED: a role-owned settings UI + its render descriptor. (Kit patching does NOT need this -
   *  `lsdj-assets` is a role with no protocol node that splices pre-built kit, palette and font files into
   *  the ROM on the control plane.) */
  ui?: unknown;
}

/** How much of a ROM the short header is: enough for the cartridge title at 0x134 and the fields around it. The store
 *  reads this much for the role suggestions, and a role's `deriveConfig` is told it is the short header's length. */
export const ROLE_HEADER_LEN = 0x150;

/** A role's ROM-derived config: how much of the file `derive` needs for a given short header, and the fields it writes.
 *  `config` is the role's config as the build has it (a stored one on a load), and the answer replaces it. `derive` is
 *  called only with the bytes of a file that could be READ (a non-empty header): a file the backend cannot open derives
 *  nothing, and the config the build already carries stands. */
export interface RoleDerive {
  headerBytes(short: Uint8Array): number;
  derive(header: Uint8Array, config: Record<string, unknown>): Record<string, unknown>;
}

/** What a ROM provider inspects to decide feature roles: the platform + core, the ROM
 *  header (cartridge title at 0x134), and the embedded-ROM marker — "" for a file-backed
 *  ROM, else the baked-in synth's id (e.g. "mgb"). The marker is the ONLY signal for an
 *  embedded ROM, whose bytes never reach TS, so its header is empty. (Built-in providers
 *  match on ROM identity — header/marker — but `platform`/`core` are here for extensions.) */
export interface RomContext {
  platform: Platform;
  core: Core;
  header: Uint8Array;
  embeddedRom: string;
}

/** Inspect a ROM and return the feature roles to attach. */
export type RomProvider = (rom: RomContext) => RoleInstance[];

export class RoleRegistry {
  private types = new Map<string, RoleType>();
  private providers: RomProvider[] = [];

  registerRole(t: RoleType): void {
    this.types.set(t.kind, t);
  }
  registerRomProvider(fn: RomProvider): void {
    this.providers.push(fn);
  }

  roleType(kind: string): RoleType | undefined {
    return this.types.get(kind);
  }

  /** The core-config "system" role whose kind === the core (e.g. "sameboy"), or none. */
  systemRoleFor(core: string): RoleType | undefined {
    const t = this.types.get(core);
    return t && t.category === "system" ? t : undefined;
  }

  /** The default roles for a freshly-constructed system: the core's config role (if any),
   *  then every provider's feature-role suggestions for this ROM. Each config is parsed
   *  through its role's schema (defaults filled, values clamped). `embeddedRom` ("" for a
   *  file-backed ROM) lets a provider match a baked-in synth whose header can't be sniffed. */
  defaultRoles(core: Core, platform: Platform, header: Uint8Array, embeddedRom = ""): RoleInstance[] {
    const out: RoleInstance[] = [];
    const sysRole = this.systemRoleFor(core);
    if (sysRole) out.push({ kind: sysRole.kind, config: sysRole.schema.parse({}) });
    const ctx: RomContext = { platform, core, header, embeddedRom };
    for (const p of this.providers) {
      for (const r of p(ctx)) {
        const rt = this.types.get(r.kind);
        out.push({ kind: r.kind, config: rt ? rt.schema.parse(r.config) : r.config });
      }
    }
    return out;
  }
}
