// The live registries (docs/downloadable-packs-loading-design.md section 3, "Live registries" and "One set of checks";
// docs/downloadable-packs-m2a-plan.md). Each list a package contributes to (the roles, the consoles, the menus, the CLI
// tools) is a Registry: the generated modules seed it with the product's packages at load, and a pack loaded later
// appends to it. A registry only grows: a pack is added live and removed or updated only at the next launch, so a
// consumer can keep how many entries it has seen and take the rest, and nothing ever forgets an entry.
//
// A commit is all or nothing across registries: its additions are staged, the check of every registry seeded after the
// commit runs over the staged entries of all of them, and only when every check passes are the registries swapped, the
// counter bumped once and the listeners told once. A registry not yet seeded in this bundle reads as empty and its
// check does not run: the generated modules seed in import order, so a check that reads another registry finds it
// seeded, or absent from the bundle altogether (the planner's bundle carries the consoles and no app).
//
// This module imports nothing: the planner's bundle carries it, and must carry no schema.

/** One registered item and the package that contributed it (a package id, or a pack's). */
export interface Contribution<T> {
  readonly pkg: string;
  readonly item: T;
}

/** What a check reads another registry through: its entries as the commit would leave them, or none for a registry not
 *  yet seeded in this bundle. */
export type RegistryView = <U>(registry: Registry<U>) => readonly Contribution<U>[];

/** A registry's rule over its whole staged list, which throws its refusal. A check reads other registries through
 *  `view` only: their `entries()`, `items()` and any `derived` value are the pre-commit lists. */
export type RegistryCheck<T> = (entries: readonly Contribution<T>[], view: RegistryView) => void;

/** One commit's additions to one registry, all from one package. */
export interface Addition<T = unknown> {
  readonly registry: Registry<T>;
  readonly pkg: string;
  readonly items: readonly NoInfer<T>[];
}

/** An addition naming its registry: what a pack's loader and the UI's test seam commit, since neither holds this
 *  context's registry objects (a render worker's or a script host's bundle has its own). */
export interface NamedAddition {
  readonly registry: string;
  readonly pkg: string;
  readonly items: readonly unknown[];
}

interface State {
  rule?: RegistryCheck<unknown>;
  entries: readonly Contribution<unknown>[];
  items: readonly unknown[];
  seeded: boolean;
}

const EMPTY: readonly never[] = Object.freeze([]);

/** Every registry in this bundle, in the order their modules created them: the order the checks run in. */
const ALL: Registry<unknown>[] = [];
const BY_NAME = new Map<string, Registry<unknown>>();
const STATE = new WeakMap<Registry<unknown>, State>();
let counter = 0;
const listeners = new Set<() => void>();

function stateOf<T>(registry: Registry<T>): State {
  const state = STATE.get(registry as Registry<unknown>);
  if (!state) {
    throw new Error(
      `the registry ${registry.name} is not this context's (each context's module set has its own registries): commit it by name`,
    );
  }
  return state;
}

export class Registry<T> {
  constructor(
    readonly name: string,
    rule?: RegistryCheck<T>,
  ) {
    if (BY_NAME.has(name)) throw new Error(`two registries are named ${name}`);
    BY_NAME.set(name, this as Registry<unknown>);
    ALL.push(this as Registry<unknown>);
    STATE.set(this as Registry<unknown>, {
      rule: rule as RegistryCheck<unknown> | undefined,
      entries: EMPTY,
      items: EMPTY,
      seeded: false,
    });
  }

  /** Every entry, in commit order (within a commit, the additions' order). Frozen, and the same array until the next
   *  commit that names this registry. */
  entries(): readonly Contribution<T>[] {
    return stateOf(this).entries as readonly Contribution<T>[];
  }

  /** The items alone, in the entries' order. Frozen, with the same identity rule as `entries`. */
  items(): readonly T[] {
    return stateOf(this).items as readonly T[];
  }

  /** Whether a commit has touched this registry in this bundle. */
  get seeded(): boolean {
    return stateOf(this).seeded;
  }
}

/** Commit `additions` across their registries, all or none: see the header. Throws the first refusal, in the order the
 *  registries were created, and leaves every registry, the counter and the listeners untouched. */
export function commitContributions<A extends readonly unknown[]>(
  additions: readonly [...{ [K in keyof A]: Addition<A[K]> }],
): void {
  const staged = new Map<Registry<unknown>, Contribution<unknown>[]>();
  for (const addition of additions) {
    const registry = addition.registry as Registry<unknown>;
    let list = staged.get(registry);
    if (!list) {
      list = [...stateOf(registry).entries];
      staged.set(registry, list);
    }
    for (const item of addition.items) list.push(Object.freeze({ pkg: addition.pkg, item }));
  }
  const seededAfter = (registry: Registry<unknown>): boolean => stateOf(registry).seeded || staged.has(registry);
  const view: RegistryView = <U>(registry: Registry<U>): readonly Contribution<U>[] => {
    const r = registry as Registry<unknown>;
    if (!seededAfter(r)) return EMPTY;
    return (staged.get(r) ?? stateOf(r).entries) as readonly Contribution<U>[];
  };
  for (const registry of ALL) {
    if (seededAfter(registry)) stateOf(registry).rule?.(view(registry), view);
  }
  for (const [registry, list] of staged) {
    const state = stateOf(registry);
    state.entries = Object.freeze(list);
    state.items = Object.freeze(list.map((entry) => entry.item));
    state.seeded = true;
  }
  counter++;
  for (const listener of [...listeners]) {
    try {
      listener();
    } catch (e) {
      console.warn(`[contributions] a listener threw after commit ${counter}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
}

/** `commitContributions` over registry names. An unknown name is refused before anything is staged, naming the
 *  registries this bundle has. */
export function commitByName(additions: readonly NamedAddition[]): void {
  commitContributions(
    additions.map((addition) => {
      const registry = BY_NAME.get(addition.registry);
      if (!registry) {
        throw new Error(
          `no registry is named ${addition.registry} in this bundle (its registries: ${[...BY_NAME.keys()].join(", ")})`,
        );
      }
      return { registry, pkg: addition.pkg, items: addition.items };
    }),
  );
}

/** How many commits this bundle has made: a memo's key, and what the UI re-renders on. */
export function contributionsVersion(): number {
  return counter;
}

/** Call `listener` after every commit; returns the unsubscribe. A listener's throw is reported and does not stop the
 *  others: the commit has already happened. */
export function subscribeContributions(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** A value built from registries, rebuilt on first read after a commit: the console maps, the tracker markers, the
 *  action ids. The build must read registries only, so the counter is all it depends on. */
export function derived<R>(build: () => R): () => R {
  let at = -1;
  let value!: R;
  return () => {
    if (at !== counter) {
      value = build();
      at = counter;
    }
    return value;
  };
}

/** A list built from registries, which keeps its previous array when a rebuild after a commit gives the same elements
 *  in the same order: a commit to an unrelated registry then leaves a hook's dependency (and a memo over it) as it was. */
export function derivedList<T>(build: () => readonly T[]): () => readonly T[] {
  let last: readonly T[] | undefined;
  return derived(() => {
    const next = build();
    if (last && last.length === next.length && last.every((item, i) => item === next[i])) return last;
    last = next;
    return next;
  });
}
