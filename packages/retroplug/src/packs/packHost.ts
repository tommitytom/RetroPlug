// The host's half of a pack's TS halves (docs/downloadable-packs-m2c-plan.md): which packs the process loaded, how a
// half is imported, and where a refusal is reported. A txiki host installs the globals (host/js/PackModules.cpp)
// before its entry evaluates; a context without them (a test's bundle, the render worker) has no packs.

import type { TypeDescriptor } from "@retroplug/core/descriptor";

/**
 * The halves a context loads. `protocol` and `kinds` are data (pack.json's protocol and kinds members, carried in the
 * pack list), committed without an import: their code is global code every Engine loads (the native loader). The
 * script half is MicroQuickJS code the native loader registers, never a context's.
 */
export type HalfKind = "app" | "consoles" | "protocol" | "kinds" | "placer" | "ui" | "views" | "cli";

/** A pack's protocol half as the host lists it (docs/downloadable-packs-m2e-plan.md). */
export interface PackProtocol {
  /** pack.json's protocol.nodes as written: the live table's checks hold each entry (graph/protocolSpec.ts). */
  readonly nodes: readonly unknown[];
  /** Each role kind and the node type it is placed as. */
  readonly roles: Readonly<Record<string, string>>;
  /** Each node type's descriptor, as the host's trial load read it. */
  readonly descriptors: Readonly<Record<string, TypeDescriptor>>;
}

/** A pack as its pack.json declares it: what the loader holds its halves to. */
export interface PackInfo {
  readonly id: string;
  readonly version: string;
  /** Every half the manifest names but the native one. */
  readonly halves: readonly string[];
  readonly provides: { readonly platforms: readonly string[]; readonly nodeTypes: readonly string[];
    readonly roles: readonly string[] };
  readonly requires: { readonly packs: readonly string[]; readonly platforms: readonly string[] };
  /** The protocol half's data, when `halves` names it. */
  readonly protocol?: PackProtocol;
  /** pack.json's kinds member: each kind's items with their data, as written (docs/downloadable-packs-m2f-plan.md). The
   *  kinds half itself is global code every Engine's built-in pack runs; the loader commits this data and the app
   *  half's items. */
  readonly kinds?: Readonly<Record<string, readonly Readonly<Record<string, unknown>>[]>>;
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
