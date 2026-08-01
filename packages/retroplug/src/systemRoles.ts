// The generic per-system role system. A role is `{ kind, config }` keyed by a string
// — the core treats config as an opaque per-kind blob. Everything specific to a
// system (a backend's own knobs, or an optional feature like LSDj) is a role, so the
// core stays free of backend/LSDj knowledge. Extensions populate a kind-keyed
// RoleRegistry; the core registers only the built-in backend "system" roles
// (coreRoles.ts). Ports the shape of native RoleConfig (RoleConfig.hpp) — a tagged
// union on "kind" — into a runtime registry, minus the hardcoded if/else chains.
//
// Two categories: "system" roles carry a backend's emulator settings (attached by
// backend kind); "feature" roles are behaviors attached by a ROM provider. A role's
// DSP-thread behavior (the doc-06 translator/source/router — mGB, lsdj-sync, routing)
// is the `dsp` field, authored as a plain TS behavior over dspKernel's per-system
// context. Its UI-thread behavior (control-plane, e.g. kit-patch) stays a deferred `ui`
// seam — a role is never an opaque native blob.

import type { ProjectBehavior, SystemBehavior } from "./dspKernel";
import type { Platform, Core } from "./platform";
import type { ConstructSpec } from "./backend";

export type RoleCategory = "system" | "feature";

/** Where a DSP-thread behavior runs: per-system (translator/source), or once over all systems
 *  (project scope — e.g. MIDI routing). Defaults to "system". */
export type RoleScope = "system" | "project";

/** A role on a system: a kind + its opaque config data. This is what serializes. */
export interface RoleInstance {
  kind: string;
  config: Record<string, unknown>;
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
  /** Where the `dsp` behavior runs (default "system"). "project" behaviors (routing) run once
   *  over all systems, before the per-system pipelines. */
  scope?: RoleScope;
  /** The zod schema for this role's config — the single source of truth for its
   *  shape, defaults, and clamping. */
  schema: RoleConfigSchema;
  /** The DSP-thread behavior (doc-06 translator/source/router): a `SystemBehavior` for system
   *  scope, a `ProjectBehavior` for project scope. Run per block by the DSP kernel. */
  dsp?: SystemBehavior | ProjectBehavior;
  /** A load-time hook: transform the resolved `ConstructSpec` just before the core is instantiated
   *  (after paths + any seed blobs are resolved). ADDITIVE by contract — seed data that is otherwise
   *  absent (e.g. a fresh LSDj cart gets a valid empty sav), never clobber what's already there.
   *  Runs only for roles actually attached to the system, so it's inherently ROM-gated. `config` is this
   *  role's own config (e.g. an LSDj asset-override list). */
  onConstruct?(spec: ConstructSpec, caps: ConstructCaps, config: Record<string, unknown>): ConstructSpec;
  /** DEFERRED: a UI-thread behavior (control-plane, e.g. kit-patch) + settings render descriptor. */
  ui?: unknown;
}

/** The narrow Backend slice a load-time `onConstruct` hook may use: synthesize an LSDj SRAM image, check
 *  whether native will find a real sav on disk, read the base ROM (to patch it), and decode a PNG (for an
 *  LSDj font override). The full `Backend` satisfies this structurally, so the store passes itself. */
export interface ConstructCaps {
  savFromJson(json: string): Uint8Array;
  fileExists(path: string): boolean;
  readFile(path: string): Uint8Array | null;
  pngDecode(bytes: Uint8Array): { width: number; height: number; rgba: Uint8Array } | null;
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
