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
// BEHAVIOR (the doc-06 translator scripts + a UI descriptor) is a DEFERRED RoleType
// seam — named here, filled by a later domain — so a role is never an opaque native
// blob.

export type RoleCategory = "system" | "feature";

/** A role on a system: a kind + its opaque config data. This is what serializes. */
export interface RoleInstance {
  kind: string;
  config: Record<string, unknown>;
}

/** A registry entry contributed by an extension (or the core, for backend roles). */
export interface RoleType {
  kind: string;
  category: RoleCategory;
  /** A fresh default config for this role. */
  defaultConfig(): Record<string, unknown>;
  /** Fill defaults + validate/clamp a (possibly partial/out-of-range) config. The
   *  only place ranges live. */
  clampConfig(config: Record<string, unknown>): Record<string, unknown>;
  /** DEFERRED: the doc-06 translator (byte/MIDI behavior). A later domain fills it. */
  behavior?: unknown;
  /** DEFERRED: a render descriptor for the settings UI. */
  ui?: unknown;
}

/** Inspect a ROM header (title at 0x134) and return the feature roles to attach. */
export type RomProvider = (header: Uint8Array) => RoleInstance[];

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

  /** The "system" role whose kind === the backend kind (e.g. "sameboy"), or none. */
  systemRoleFor(backendKind: string): RoleType | undefined {
    const t = this.types.get(backendKind);
    return t && t.category === "system" ? t : undefined;
  }

  /** The default roles for a freshly-constructed system: the backend's system role
   *  (if any), then every provider's feature-role suggestions for this ROM. */
  defaultRoles(backendKind: string, header: Uint8Array): RoleInstance[] {
    const out: RoleInstance[] = [];
    const sysRole = this.systemRoleFor(backendKind);
    if (sysRole) out.push({ kind: sysRole.kind, config: sysRole.defaultConfig() });
    for (const p of this.providers) out.push(...p(header));
    return out;
  }
}
