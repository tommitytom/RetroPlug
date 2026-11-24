// The host's half of a pack's TS halves (docs/downloadable-packs-m2c-plan.md): which packs the process loaded, how a
// half is imported, and where a refusal is reported. A txiki host installs the globals (host/js/PackModules.cpp)
// before its entry evaluates; a context without them (a test's bundle, the render worker) has no packs.

/** The halves a context may load (the protocol and script halves are M2d's). */
export type HalfKind = "app" | "consoles" | "ui" | "views" | "cli";

/** A pack as its pack.json declares it: what the loader holds its halves to. */
export interface PackInfo {
  readonly id: string;
  readonly version: string;
  /** Every half the manifest names but the native one. */
  readonly halves: readonly string[];
  readonly provides: { readonly platforms: readonly string[]; readonly nodeTypes: readonly string[];
    readonly roles: readonly string[] };
  readonly requires: { readonly packs: readonly string[]; readonly platforms: readonly string[] };
}

export interface PackHost {
  /** The packs with a TS half, in load order (dependency order). */
  list(): readonly PackInfo[];
  /** The half's module namespace. */
  importHalf(id: string, half: HalfKind): Promise<unknown>;
  /** Report a refusal where Settings > Packs (M4) reads them. */
  refuse(id: string, reason: string, skipLater?: boolean): void;
}

interface PackHooks {
  __rp_packList?: () => string;
  __rp_packRefused?: (id: string, reason: string, skipLater?: boolean) => void;
}

// A specifier no bundler may resolve: a half is served by the host's module loader, by name.
const importByName = (name: string): Promise<unknown> => import(name);

export function nativePackHost(): PackHost {
  const g = globalThis as PackHooks;
  return {
    list: () => (g.__rp_packList ? (JSON.parse(g.__rp_packList()) as PackInfo[]) : []),
    importHalf: (id, half) => importByName(`pack:${id}/${half}`),
    refuse: (id, reason, skipLater) => g.__rp_packRefused?.(id, reason, skipLater === true),
  };
}
