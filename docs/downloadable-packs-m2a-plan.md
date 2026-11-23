# Downloadable packs M2a: live registries. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn every generated registry constant into a live, append-only `Registry` that the generated module seeds
with the built-in packages, move the checks the generator emits into one hand-written module per registry that runs on
every commit, and make every consumer that copied a generated list at module load read the live one, so a contribution
committed after launch reaches the stores, the menus, the views and the CLI. No pack loads yet.

**Architecture:** `packages/retroplug/src/contributions/registry.ts` holds a dependency-free `Registry<T>` (entries of
`{ pkg, item }`, append-only) and `commitContributions`, which stages additions across any registries, runs the check
of every seeded registry over the staged view, and commits all or none with one change notification. Hand-written
registry modules (`consoleRegistries.ts`, `placerRegistries.ts`, `appRegistries.ts`, `ui/contributions/uiRegistries.ts`,
`cli/contributions/toolRegistries.ts`) declare the registries and carry the checks the generator used to emit, word
for word. The generated modules shrink to imports plus one `commitContributions` call, and consumers read the
registries through small facades that import the generated seed for its side effect. Derived lookups (the console
maps, the tracker markers, the action ids) are memoised on the commit counter, stores pull new entries when they are
next read, and the UI re-renders on the counter.

**Tech Stack:** TypeScript (esbuild bundles, txiki/QuickJS), the shell's `@retroplug/testing` harness (`pnpm test`),
the UI harness (`pnpm test:ui`), Node for the generator (`packages/retroplug/scripts/lib/packages.mjs`) and its fixture
test (`test/packages/run-generator.mjs`).

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) is part 1 of the packs road. Its
milestones (the [M1 plan](downloadable-packs-m1-plan.md) set them out) are M1 native packs (done), M2 the TS halves,
M3 placeholders, M4 archives and commands, M5 the real packages as packs. M2 is split in two (decided 2026-11-23):

- **M2a, live registries (this plan):** the design's section 3 "Live registries" and "One set of checks" for every
  registry the app half, the consoles, the placers, the UI and the CLI fill; the stores, the menus, the views and the
  CLI following a commit; the TS mirror of `RP_API_LEVEL`. Nothing here loads a pack: the proof is tests that commit a
  contribution after the built-ins and watch each consumer see it, and the whole existing suite unchanged.
- **M2b, loading the TS halves:** reading a loaded pack's halves on the control plane, the UI, the CLI and the render
  worker, staging a pack across its halves and its native library, the protocol half (the protocol tables, the
  contribution kinds, the script nodes and their span labels: included in part 1, decided 2026-11-23), launch wiring,
  and the module linking. How a pack's ES modules reach the host's contract modules is decided by a spike first: the
  control plane and the UI are two bundles in one txiki context, each with its own inlined copy of every
  `@retroplug/*` module and of these registries, and the ESM answer is to build the contract modules once as shared
  named modules resolved through txiki's import map (see [Design questions](#design-questions-this-plan-found)).

**What M2a leaves alone:** `src/generated/protocol.ts` and `scriptNodes.ts` (M2b's protocol half), the contribution
kinds' block in `packages.ts` (its `accept` runs once at load, unchanged; M2b makes it live with the protocol half),
`PACKAGE_IDS` (the built-in list, read by tests only), and the duplicated registries of the two bundles (each bundle
keeps its own copy; M2b's spike decides whether they merge).

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. Every step names its exact command and what it prints. Unlike M1's, this plan's code was not run in a
prototype: where a step's code meets the tree differently (a name, an import path), the implementer follows the tree
and says so in the report, and the reviewer checks the deviation.

## Decisions this plan makes

- **A registry is append-only.** Part 1 adds packs live and removes or updates them only at the next launch (the
  design's decision), so a registry's entries only grow within a process. Every consumer below relies on it: a store
  keeps how many entries it has adopted and adopts the rest, a memo rebuilds on the commit counter, and nothing ever
  has to forget an entry.
- **One commit counter per bundle, and every check on every commit.** `commitContributions` bumps one counter and runs
  the check of every registry that is seeded after the commit, over the staged entries of all of them. The checks are
  linear in the entries and a commit is rare (a launch, a pack), so running all of them is cheaper to reason about than
  declaring which registry reads which: a cross-registry rule (a console's core needs a system role, an embed's
  platform needs a console, a menu row's role must be registered) is held whichever side a commit touched.
- **A registry not yet seeded reads as empty, and its check does not run.** The generated modules seed in import order
  (`packages.ts` imports `./consoles` and `./placers` first; `menus.ts` imports `../../src/generated/packages` first),
  so when a check runs, every registry it reads is seeded in that bundle, or is absent from it (the planner's bundle
  carries the consoles and no app). This is what keeps today's split: the console-core check lives with the roles and
  never runs in the planner's bundle, as it never ran there before.
- **The checks' messages do not change by a byte.** They are today's emitted checks moved into hand-written modules,
  so the generator's fixture test (`test/packages/packages-generator.test.ts`, which loads each generated module and
  reads its refusal) holds them unchanged; only its expected module texts are regenerated.
- **The generated modules re-export the registries they seed.** A consumer reads a registry through its facade, which
  imports the generated module for its side effect; the re-export is there for `run-generator.mjs`, which bundles a
  generated module alone and reads what it registered.
- **Registries are named for the member a package fills** (`roles`, `romProviders`, `trackerIntegrations`, `kitFormats`,
  `projectSections`, `projectServices`, `configSections`, `embeds`, `consoles`, `realCarts`, `placers`, `ui`, `views`,
  `cliTools`, and `bakedEmbeds` for the manifest's half of an embed), and `commitByName` commits by those names. M2b
  maps a pack's exported contributions onto registries by these names, and the UI's test seam uses them now.
- **Stores pull, and a host asks them to adopt.** `ProjectStore` and `UserConfigStore` have no dispose, so neither
  subscribes: each adopts the entries committed since it last looked at the top of every read that consults its
  sections or services, and each gains `adoptContributions()` for the caller that committed (M2b's loader) to publish
  the graph at once. A project section registered after the project was read is parsed from the block the project kept
  verbatim (the shell already keeps every key it does not know), or defaulted when the project never carried one; a
  config section is parsed from the block the file carried, or left absent (a reader gets its defaults, and the file is
  not stamped with a block nobody set, as today).
- **The UI re-renders on the counter, and a tile remounts.** `App` reads the counter through `useSyncExternalStore`, so a
  commit rebuilds the menus (they are rebuilt every render) and the action maps. A tile's view slots must not grow at
  the end of its child list (lv_binding_js appends; spec/03's framework gotchas), so the grid keys each tile on the
  counter and a commit remounts the tiles.
- **The CLI's shell tool names are a constant beside the tool registry.** The shell-collision refusal moves from
  `composeTools` into the registry's check so a pack's tool is refused at its commit; the registry module cannot import
  the five sessions (each pulls a session's whole graph), so it lists their names and a test holds the list to
  `tools.ts`'s.
- **`API_LEVEL` is generated into `src/generated/apiLevel.ts`,** read from `rp_pack.h`'s `#define` beside the generator
  (not under the generator's root, so the fixture tree needs no copy of the header), written into every product tree,
  and redirected for the products by `tools/product-generated.js`.

## Commands used throughout

- The TS suite: `pnpm test` (all files), `pnpm test <slug>` (one file, or every file under a directory: a slug is the
  file's path under `test/` without `.test.ts`, for example `contributions/registry`, and `contributions` takes the
  directory; a filter followed by a dash also matches the dashed slug, so `script-bundle` takes `script/bundle-protocol`
  and `script/bundle-views`, while `script/bundle` matches nothing and exits 1).
- The UI suite: `pnpm test:ui` and `pnpm test:ui <slug-filter>` (files under `packages/retroplug/test-ui/`).
- The generator alone: `node packages/retroplug/scripts/lib/packages.mjs` (writes the desktop's generated modules).
- `pnpm typecheck` (runs the generator, then every package's and product's `tsc`), `pnpm check:boundaries`,
  `pnpm check:docs`.
- The CLI's TS tests: `pnpm test:cli-ts`.
- `deps/sameboy` shows as modified after a configure: expected, never stage, reset or stash it. Stage files by name.
- The generated desktop modules (`packages/retroplug/{src,ui,cli}/generated/`) are tracked: a task that changes the
  generator regenerates them and commits them with the generator change.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/retroplug/src/contributions/registry.ts` | `Registry`, `commitContributions`, `commitByName`, the counter, `derived` |
| `packages/retroplug/src/contributions/consoleRegistries.ts` | the `consoles` and `realCarts` registries and their checks (no zod, planner-safe) |
| `packages/retroplug/src/contributions/placerRegistries.ts` | the `placers` registry and its check (planner-safe) |
| `packages/retroplug/src/contributions/appRegistries.ts` | the app's nine registries, their checks, the shell's reserved keys, the entry views |
| `packages/retroplug/src/contributions/app.ts` | the app facade: seeds through `../generated/packages`, re-exports the registries |
| `packages/retroplug/src/contributions/placers.ts` | the placers facade: seeds through `../generated/placers` |
| `packages/retroplug/src/appRoleRegistry.ts` | `AppRoleRegistry`, a `RoleRegistry` that adopts roles and providers committed later |
| `packages/retroplug/ui/contributions/uiRegistries.ts` | the `ui` and `views` registries and their checks |
| `packages/retroplug/ui/contributions/menus.ts` | the menus facade: seeds through `../generated/menus` |
| `packages/retroplug/ui/contributions/views.ts` | the views facade: seeds through `../generated/views` |
| `packages/retroplug/ui/stores/useContributions.ts` | `useContributionsVersion`, the UI's subscription to the counter |
| `packages/retroplug/cli/contributions/toolRegistries.ts` | the `cliTools` registry, its check, `SHELL_TOOL_NAMES` |
| `packages/retroplug/src/generated/apiLevel.ts` | generated: `API_LEVEL` |
| `packages/retroplug/test/contributions/registry.test.ts` | the primitive |
| `packages/retroplug/test/contributions/consoles-live.test.ts` | the console lookups after a commit |
| `packages/retroplug/test/contributions/app-live.test.ts` | roles, providers, trackers, kits, embeds after a commit |
| `packages/retroplug/test/contributions/sections-live.test.ts` | project and config sections and services after a commit |
| `packages/retroplug/test/contributions/ui-live.test.ts` | menu rows, actions, services and views after a commit (pure tier) |
| `packages/retroplug/test/contributions/cli-live.test.ts` | the CLI's tools after a commit |
| `packages/retroplug/test-ui/contributions-live.test.ts` | the running UI re-rendering on a commit |

Modified: `packages/retroplug/scripts/lib/packages.mjs` (the six render functions, `apiLevel.ts`),
`tools/product-generated.js` (`apiLevel`), `packages/retroplug/test/packages/{run-generator.mjs,fixtureAliases.mjs}`
and `fixtures/expected/*.ts`, `packages/retroplug/test/script/bundle-views.mjs`, the generated modules, and the
consumers: `src/{consoles,trackerRegistry,romProviders,appHost,kitFormats,embeds,configSections,userConfig,
userConfigSerialization,userConfigStore,projectConfig,projectStore,fileSelection,fileDrop,systemsStore,sramAutoSave,
batteryListing}.ts`, `src/render/render.ts`, `src/graph/{buildGraph,directGraph}.ts`, `ui/host/{menuContributions,
viewContributions}.ts`, `ui/App.tsx`, `ui/app/{useUiServices,projectModals}.ts`, `ui/screens/grid/SystemGrid.tsx`,
`ui/screens/menu/menuDefs.ts`, `ui/host/nativeHooks.ts`, `ui/main.tsx`, `cli/{tools,cli,sdkSurfaceCheck}.ts`, the
tests that named a generated constant, and the docs (`spec/02`, `spec/04`, `spec/06`, `spec/07`).

---

### Task 1: The registry primitive

**Files:**
- Create: `packages/retroplug/src/contributions/registry.ts`
- Test: `packages/retroplug/test/contributions/registry.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/registry.test.ts`:

```ts
// The live registries' primitive (docs/downloadable-packs-m2a-plan.md, Task 1): append-only entries, a commit staged
// across registries and checked whole, one counter and one notification per commit, and a refused commit leaving
// every registry as it was. The registries here are this file's own: each test file runs in a process of its own, so
// the names cannot meet the app's.
import { test, expect } from "@retroplug/testing";
import {
  Registry,
  commitByName,
  commitContributions,
  contributionsVersion,
  derived,
  subscribeContributions,
} from "../../src/contributions/registry";

const fruit = new Registry<string>("test.fruit", (entries) => {
  const seen = new Set<string>();
  for (const { pkg, item } of entries) {
    if (seen.has(item)) throw new Error(`the package ${pkg} adds the fruit ${item}, which is already registered`);
    seen.add(item);
  }
});
const baskets = new Registry<{ fruit: string }>("test.baskets", (entries, view) => {
  const known = new Set(view(fruit).map((entry) => entry.item));
  for (const { pkg, item } of entries) {
    if (!known.has(item.fruit)) throw new Error(`the package ${pkg} fills a basket with ${item.fruit}, which no package registers`);
  }
});

test("a registry starts empty and unseeded", () => {
  const empty = new Registry<number>("test.empty");
  expect(empty.entries()).toEqual([]);
  expect(empty.items()).toEqual([]);
  expect(empty.seeded).toBe(false);
});

test("a commit appends in order, with each item's package, and seeds what it touches", () => {
  commitContributions([
    { registry: fruit, pkg: "alpha", items: ["apple", "pear"] },
    { registry: fruit, pkg: "beta", items: ["plum"] },
  ]);
  expect(fruit.items()).toEqual(["apple", "pear", "plum"]);
  expect(fruit.entries().map((entry) => entry.pkg)).toEqual(["alpha", "alpha", "beta"]);
  expect(fruit.seeded).toBe(true);
  expect(baskets.seeded).toBe(false);
});

test("an addition with no items still seeds its registry", () => {
  commitContributions([{ registry: baskets, pkg: "alpha", items: [] }]);
  expect(baskets.seeded).toBe(true);
  expect(baskets.items()).toEqual([]);
});

test("the arrays a registry hands out are frozen and keep their identity until the next commit", () => {
  const before = fruit.items();
  expect(Object.isFrozen(before)).toBe(true);
  expect(Object.isFrozen(fruit.entries())).toBe(true);
  expect(fruit.items()).toBe(before);
  commitContributions([{ registry: fruit, pkg: "gamma", items: ["fig"] }]);
  expect(fruit.items() === before).toBe(false);
  expect(before).toEqual(["apple", "pear", "plum"]);
});

test("a check reads the other registries as the commit would leave them", () => {
  // The basket names a fruit added in the same commit: the staged view, not the committed one, is what is checked.
  commitContributions([
    { registry: fruit, pkg: "delta", items: ["quince"] },
    { registry: baskets, pkg: "delta", items: [{ fruit: "quince" }] },
  ]);
  expect(baskets.items().map((b) => b.fruit)).toEqual(["quince"]);
});

test("a refused commit changes no registry, bumps no counter and notifies no one", () => {
  const fruitBefore = fruit.items();
  const basketsBefore = baskets.items();
  const version = contributionsVersion();
  let calls = 0;
  const off = subscribeContributions(() => calls++);
  expect(() =>
    commitContributions([
      { registry: fruit, pkg: "eps", items: ["kiwi"] },
      { registry: baskets, pkg: "eps", items: [{ fruit: "durian" }] },
    ]),
  ).toThrow("the package eps fills a basket with durian, which no package registers");
  expect(fruit.items()).toBe(fruitBefore);
  expect(baskets.items()).toBe(basketsBefore);
  expect(contributionsVersion()).toBe(version);
  expect(calls).toBe(0);
  off();
});

test("a check on a registry the commit did not touch still runs", () => {
  // A seeded registry's rule holds whichever side a commit touched: here the fruit check refuses a duplicate even
  // though the commit's other addition is a basket.
  expect(() =>
    commitContributions([
      { registry: fruit, pkg: "zeta", items: ["apple"] },
      { registry: baskets, pkg: "zeta", items: [{ fruit: "apple" }] },
    ]),
  ).toThrow("the package zeta adds the fruit apple, which is already registered");
});

test("an unseeded registry reads as empty and its check does not run", () => {
  const strict = new Registry<string>("test.strict", () => {
    throw new Error("an unseeded registry's check ran");
  });
  const reader = new Registry<string>("test.reader", (_entries, view) => {
    if (view(strict).length !== 0) throw new Error("an unseeded registry read as non-empty");
  });
  commitContributions([{ registry: reader, pkg: "alpha", items: ["x"] }]);
  expect(reader.items()).toEqual(["x"]);
  expect(strict.seeded).toBe(false);
});

test("a commit bumps the counter once and notifies every listener once", () => {
  const version = contributionsVersion();
  const seen: number[] = [];
  const off = subscribeContributions(() => seen.push(contributionsVersion()));
  commitContributions([
    { registry: fruit, pkg: "eta", items: ["lime"] },
    { registry: fruit, pkg: "theta", items: ["date"] },
  ]);
  expect(contributionsVersion()).toBe(version + 1);
  expect(seen).toEqual([version + 1]);
  off();
  commitContributions([{ registry: fruit, pkg: "iota", items: ["sloe"] }]);
  expect(seen).toEqual([version + 1]);
});

test("a listener that throws does not stop the others or undo the commit", () => {
  const warned: string[] = [];
  const realWarn = console.warn;
  console.warn = (...args: unknown[]) => warned.push(args.map(String).join(" "));
  let second = 0;
  const offA = subscribeContributions(() => {
    throw new Error("a broken listener");
  });
  const offB = subscribeContributions(() => second++);
  try {
    commitContributions([{ registry: fruit, pkg: "kappa", items: ["yuzu"] }]);
  } finally {
    console.warn = realWarn;
    offA();
    offB();
  }
  expect(second).toBe(1);
  expect(fruit.items().includes("yuzu")).toBe(true);
  expect(warned.some((line) => line.includes("a broken listener"))).toBe(true);
});

test("derived rebuilds only after a commit", () => {
  let builds = 0;
  const count = derived(() => {
    builds++;
    return fruit.items().length;
  });
  const first = count();
  expect(count()).toBe(first);
  expect(builds).toBe(1);
  commitContributions([{ registry: fruit, pkg: "lambda", items: ["sorb"] }]);
  expect(count()).toBe(first + 1);
  expect(builds).toBe(2);
});

test("commitByName resolves registries by their names, and refuses an unknown one naming the known", () => {
  commitByName([{ registry: "test.fruit", pkg: "mu", items: ["medlar"] }]);
  expect(fruit.items().includes("medlar")).toBe(true);
  expect(() => commitByName([{ registry: "test.nuts", pkg: "mu", items: ["pecan"] }])).toThrow(
    "no registry is named test.nuts in this bundle",
  );
});

test("two registries may not share a name", () => {
  expect(() => new Registry<string>("test.fruit")).toThrow("two registries are named test.fruit");
});
```

- [ ] **Step 2: Run the test to see it fail**

Run: `pnpm test contributions/registry`
Expected: `BUILD FAILED: contributions/registry` with esbuild's `Could not resolve "../../src/contributions/registry"`.

- [ ] **Step 3: Write the primitive**

Create `packages/retroplug/src/contributions/registry.ts`:

```ts
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

/** A registry's rule over its whole staged list, which throws its refusal. */
export type RegistryCheck<T> = (entries: readonly Contribution<T>[], view: RegistryView) => void;

/** One commit's additions to one registry, all from one package. */
export interface Addition<T = unknown> {
  readonly registry: Registry<T>;
  readonly pkg: string;
  readonly items: readonly T[];
}

/** An addition naming its registry: what a pack's loader and the UI's test seam commit, since neither holds this
 *  bundle's registry objects. */
export interface NamedAddition {
  readonly registry: string;
  readonly pkg: string;
  readonly items: readonly unknown[];
}

interface State {
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
  return STATE.get(registry as Registry<unknown>)!;
}

export class Registry<T> {
  constructor(
    readonly name: string,
    private readonly rule?: RegistryCheck<T>,
  ) {
    if (BY_NAME.has(name)) throw new Error(`two registries are named ${name}`);
    BY_NAME.set(name, this as Registry<unknown>);
    ALL.push(this as Registry<unknown>);
    STATE.set(this as Registry<unknown>, { entries: EMPTY, items: EMPTY, seeded: false });
  }

  /** Every entry, in commit order (within a commit, the additions' order). Frozen, and the same array until the next
   *  commit that adds to this registry. */
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

  /** Run this registry's rule over `entries` (the module's own use: a commit calls it). */
  checkStaged(entries: readonly Contribution<T>[], view: RegistryView): void {
    this.rule?.(entries, view);
  }
}

/** Commit `additions` across their registries, all or none: see the header. Throws the first refusal, in the order the
 *  registries were created, and leaves every registry, the counter and the listeners untouched. */
export function commitContributions(additions: readonly Addition<any>[]): void {
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
    if (seededAfter(registry)) registry.checkStaged(view(registry), view);
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
```

- [ ] **Step 4: Run the test to see it pass**

Run: `pnpm test contributions/registry`
Expected: TAP `1..13`, then `ok 1` to `ok 13` with no `not ok`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add docs/downloadable-packs-m2a-plan.md packages/retroplug/src/contributions/registry.ts \
  packages/retroplug/test/contributions/registry.test.ts
git commit -m "contributions: the live registry primitive, committed all or none across registries"
```

### Task 2: The console and placer registries

The consoles, the real carts and the placers first: the planner's bundle carries them and no app, so these two
registry modules import nothing but the primitive and types. The generated `packages.ts` still reads both lists for its
checks until Task 3 rewrites it, so this task points those two reads at the registries.

**Files:**
- Create: `packages/retroplug/src/contributions/consoleRegistries.ts`, `packages/retroplug/src/contributions/placerRegistries.ts`,
  `packages/retroplug/src/contributions/placers.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderConsoles`, `renderPlacers`, two lines of
  `renderPackages`, their call sites in `generate`), `packages/retroplug/src/consoles.ts`,
  `packages/retroplug/src/graph/buildGraph.ts`, `packages/retroplug/src/graph/directGraph.ts`,
  `packages/retroplug/cli/sdkSurfaceCheck.ts`, `packages/retroplug/test/packages/fixtureAliases.mjs`,
  `packages/retroplug/test/packages/run-generator.mjs`, `packages/retroplug/test/script/bundle-views.mjs`
- Regenerate: `packages/retroplug/src/generated/{consoles,placers,packages}.ts`,
  `packages/retroplug/test/packages/fixtures/expected/{consoles,placers,packages}.ts`
- Test: `packages/retroplug/test/contributions/consoles-live.test.ts`; modify
  `packages/retroplug/test/consoles/registry.test.ts`, `packages/retroplug/test/source-literals/no-cart-literals.test.ts`,
  `packages/retroplug/test/graph/direct-graph.test.ts`, `packages/retroplug/test/script/bundle-protocol.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/consoles-live.test.ts`:

```ts
// The console lookups follow the registry (docs/downloadable-packs-m2a-plan.md, Task 2): a console and a real cart
// committed after the built-ins classify, place and drive like a built-in one, and a commit the console checks refuse
// changes nothing. The console here is a fixture: its platform, extension, magic and node type are this file's own.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor, RealCart } from "@retroplug/core/consoles";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry, realCartsRegistry } from "../../src/contributions/consoleRegistries";
import {
  classifyRom,
  consoleDescriptors,
  consoleFor,
  defaultCoreFor,
  detectPlatform,
  platformForExtension,
  realCartOf,
  registeredCores,
  registeredPlatforms,
  registeredRealCarts,
  romExtensions,
  sniffLengths,
  systemNodeTypeFor,
} from "../../src/consoles";

const MAGIC = [0x52, 0x50, 0x4b, 0x21]; // "RPK!"

const fixtureConsole = {
  id: "packfixture",
  core: "packfixture-core",
  nodeType: "rp.packfixture.system",
  platforms: [{ id: "packfixture", label: "Pack Fixture", extensions: [".rpk"] }],
  detect: {
    prefixBytes: 4,
    rank: 900,
    run: (bytes: Uint8Array) => (MAGIC.every((b, i) => bytes[i] === b) ? "packfixture" : null),
  },
} as unknown as ConsoleDescriptor;

const fixtureCart = { platform: "packfixture", cartNode: "rp.packfixture.cart" } as unknown as RealCart;

test("the built-in consoles are seeded before anything reads them", () => {
  expect(consolesRegistry.seeded).toBe(true);
  expect(consoleDescriptors().length).toBeGreaterThan(0);
  expect(consoleDescriptors()).toBe(consolesRegistry.items());
});

test("a console committed later is classified, placed and listed", () => {
  const before = registeredPlatforms().length;
  expect(detectPlatform(new Uint8Array(MAGIC))).toBe("unknown");
  commitContributions([
    { registry: consolesRegistry, pkg: "packfixture", items: [fixtureConsole] },
    { registry: realCartsRegistry, pkg: "packfixture", items: [fixtureCart] },
  ]);
  expect(registeredPlatforms().length).toBe(before + 1);
  expect(detectPlatform(new Uint8Array(MAGIC))).toBe("packfixture");
  expect(classifyRom({ readFilePrefix: () => new Uint8Array(MAGIC) }, "/x/song.bin")).toBe("packfixture");
  expect(consoleFor("rp.packfixture.system")?.id).toBe("packfixture");
  expect(systemNodeTypeFor("packfixture")).toBe("rp.packfixture.system");
  expect(defaultCoreFor("packfixture")).toBe("packfixture-core");
  expect(registeredCores().includes("packfixture-core")).toBe(true);
  expect(romExtensions().includes(".rpk")).toBe(true);
  expect(platformForExtension(".rpk")).toBe("packfixture");
  expect(sniffLengths().includes(4)).toBe(true);
  expect(realCartOf("packfixture")?.cartNode).toBe("rp.packfixture.cart");
  expect(registeredRealCarts().some((cart) => cart.cartNode === "rp.packfixture.cart")).toBe(true);
});

test("a console declaring a platform already registered is refused, and nothing moves", () => {
  const count = consoleDescriptors().length;
  const clash = { ...fixtureConsole, id: "packfixture-two", nodeType: "rp.packfixture.two" } as ConsoleDescriptor;
  expect(() => commitContributions([{ registry: consolesRegistry, pkg: "packfixture-two", items: [clash] }])).toThrow(
    "two console descriptors declare the platform packfixture: packfixture and packfixture-two",
  );
  expect(consoleDescriptors().length).toBe(count);
});

test("a real cart naming a platform no console registers is refused", () => {
  const orphan = { platform: "nowhere", cartNode: "rp.packfixture.orphan" } as unknown as RealCart;
  expect(() => commitContributions([{ registry: realCartsRegistry, pkg: "packfixture", items: [orphan] }])).toThrow(
    "the real cart rp.packfixture.orphan names the platform nowhere, which no console descriptor registers",
  );
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test contributions/consoles-live`
Expected: `BUILD FAILED: contributions/consoles-live` (`Could not resolve "../../src/contributions/consoleRegistries"`).

- [ ] **Step 3: The two registry modules**

Create `packages/retroplug/src/contributions/consoleRegistries.ts`:

```ts
// The consoles and the physical carts, as live registries (docs/downloadable-packs-m2a-plan.md): the generated
// src/generated/consoles.ts seeds them with the product's packages, and a pack appends. The descriptors say how a ROM
// is classified and which node type places a system; a real cart says which platform's systems reach it. The planner
// reads these, and its bundle must carry no role schema (test/script/bundle-protocol.test.ts), so this module imports
// the primitive and types only. src/consoles.ts is the module the callers read them through.
import type { ConsoleDescriptor, RealCart } from "@retroplug/core/consoles";
import { Registry } from "./registry";

/** The console descriptors, in product order, then packs' in load order. A platform id is looked up by the classifier,
 *  a stored project's validation and the menus, so one declared twice would leave one of the two consoles unreachable;
 *  a node type places a system, so two descriptors sharing one would make the node table depend on the order. (A
 *  descriptor's core must also be a system role, which appRegistries.ts checks: the roles are its.) */
export const consolesRegistry = new Registry<ConsoleDescriptor>("consoles", (entries) => {
  const platforms = new Map<string, string>();
  const nodeTypes = new Map<string, string>();
  for (const { item: descriptor } of entries) {
    for (const platform of descriptor.platforms) {
      const owner = platforms.get(platform.id);
      if (owner !== undefined) {
        throw new Error(`two console descriptors declare the platform ${platform.id}: ${owner} and ` + descriptor.id);
      }
      platforms.set(platform.id, descriptor.id);
    }
    const nodeOwner = nodeTypes.get(descriptor.nodeType);
    if (nodeOwner !== undefined) {
      throw new Error(`two console descriptors declare the node type ${descriptor.nodeType}: ` +
        `${nodeOwner} and ${descriptor.id}`);
    }
    nodeTypes.set(descriptor.nodeType, descriptor.id);
  }
});

/** The physical carts a project can drive. One cart per platform and one per node, because a document places at most
 *  one: the store's lookup, buildGraph's placement and the menu's jobs all resolve a platform's cart, and with two
 *  registered the answer would be whichever was registered last. */
export const realCartsRegistry = new Registry<RealCart>("realCarts", (entries, view) => {
  const platforms = new Set<string>();
  for (const { item: descriptor } of view(consolesRegistry)) {
    for (const platform of descriptor.platforms) platforms.add(platform.id);
  }
  const cartPlatforms = new Map<string, string>();
  const cartNodes = new Map<string, string>();
  for (const { item: realCart } of entries) {
    if (!platforms.has(realCart.platform)) {
      throw new Error(`the real cart ${realCart.cartNode} names the platform ${realCart.platform}, which no ` +
        "console descriptor registers");
    }
    const platformOwner = cartPlatforms.get(realCart.platform);
    if (platformOwner !== undefined) {
      throw new Error(`two real carts name the platform ${realCart.platform}: ${platformOwner} and ` +
        realCart.cartNode);
    }
    cartPlatforms.set(realCart.platform, realCart.cartNode);
    const nodeOwner = cartNodes.get(realCart.cartNode);
    if (nodeOwner !== undefined) {
      throw new Error(`two real carts name the cart node ${realCart.cartNode}: ${nodeOwner} and ` +
        realCart.platform);
    }
    cartNodes.set(realCart.cartNode, realCart.platform);
  }
});
```

Create `packages/retroplug/src/contributions/placerRegistries.ts`:

```ts
// The graph placers, as a live registry (docs/downloadable-packs-m2a-plan.md): each package's `protocol.placer`
// module, which says where the nodes of a model the shell hands it go (docs/modular-graph-phase-f5a-design.md section
// 6.3). buildGraph reads them in this order. The generated src/generated/placers.ts seeds it; only the planner reads
// it, so this module imports the primitive and a type, and no package's node code or schema.
import type { GraphPlacer } from "@retroplug/core/placer";
import { Registry } from "./registry";

/** Each package's graph placer, in product order. A model is placed by one package: a second placer of one key would
 *  place its model twice. (A placer's key must also be a section's, which appRegistries.ts checks.) */
export const placersRegistry = new Registry<GraphPlacer<any>>("placers", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: placer } of entries) {
    const owner = owners.get(placer.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} places the graph's model ${placer.key} twice`
        : `two packages place the graph's model ${placer.key}: ${owner} and ${pkg}`);
    }
    owners.set(placer.key, pkg);
  }
});
```

Create `packages/retroplug/src/contributions/placers.ts`:

```ts
// The placers facade: the generated module seeds the registry when this one is first imported, and the planner reads
// the placers through here, in the `{ pkg, placer }` shape buildGraph takes.
import "../generated/placers";
import type { GraphPlacer } from "@retroplug/core/placer";
import { placersRegistry } from "./placerRegistries";
import { derived } from "./registry";

export { placersRegistry };

/** Every placer with the package it came from, in registration order: the same array until the next commit. */
export const placerEntries = derived((): readonly { pkg: string; placer: GraphPlacer<any> }[] =>
  placersRegistry.entries().map(({ pkg, item }) => ({ pkg, placer: item })),
);
```

- [ ] **Step 4: The generator seeds them**

In `packages/retroplug/scripts/lib/packages.mjs`, add this helper above `renderConsoles` (after `manifestsWithEmbeds`):

```js
/** The import of one of the shell's hand-written contribution modules (src/contributions/<name>.ts, ui/contributions,
 *  cli/contributions) from a generated module, by a path relative to wherever that module is written: the desktop's
 *  tree and a product's tree under build/ both reach the shell's one file, so the registry a generated module seeds is
 *  the one the shell reads. */
function contributionsSpecifier(outDir, shellDir, file) {
  return specifier(outDir, join(shellDir, file));
}

/** A commit's additions as generated lines: the shell's own empty addition first for each registry, which seeds it even
 *  in a product none of whose packages contributes (an unseeded registry reads as empty and goes unchecked), then one
 *  addition per contributing package. */
function seedLines(registries, contributions) {
  return [
    "commitContributions([",
    ...registries.map((registry) => `  { registry: ${registry}, pkg: ${JSON.stringify(SHELL)}, items: [] },`),
    ...contributions.map(({ registry, pkg, items }) =>
      `  { registry: ${registry}, pkg: ${JSON.stringify(pkg)}, items: ${items} },`),
    "]);",
  ];
}
```

`SHELL` is the generator's existing constant for the shell's package name (`"retroplug"`, the `retroplug.products`
lists never name it); `specifier` and `join` are already imported. Replace the whole of `renderConsoles` with:

```js
function renderConsoles({ packages, outDir, shellDir, product }) {
  const contributors = packages.filter((p) => p.consoles);
  // A module identifier per package: the package name loses its punctuation, as the app imports' do, and a collision
  // would be a tsc error naming neither manifest.
  const local = (name) => `consoles_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The consoles the product's packages contribute, in product order, and the physical carts a project can drive,",
    "// seeded into the live registries (src/contributions/consoleRegistries.ts holds them and their checks). Nothing",
    "// here imports an app module or zod: the planner reads these, and a bundle of the planner must carry no role's",
    "// schema (test/script/bundle-protocol.test.ts).",
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    "import { consolesRegistry, realCartsRegistry } from " +
      `"${contributionsSpecifier(outDir, shellDir, "src/contributions/consoleRegistries.ts")}";`,
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.consoles)}";`);
  }
  lines.push(
    "",
    "export { consolesRegistry, realCartsRegistry };",
    "",
    ...seedLines(["consolesRegistry", "realCartsRegistry"], [
      ...contributors.map((p) => ({
        registry: "consolesRegistry", pkg: p.pkg.name, items: `${local(p.pkg.name)}.consoles ?? []`,
      })),
      ...contributors.map((p) => ({
        registry: "realCartsRegistry", pkg: p.pkg.name, items: `${local(p.pkg.name)}.realCarts ?? []`,
      })),
    ]),
    "",
  );
  return lines.join("\n");
}
```

Replace the whole of `renderPlacers` with:

```js
function renderPlacers({ packages, outDir, shellDir, product }) {
  const contributors = packages.filter((p) => p.placer);
  const local = (name) => `placer_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's graph placers, in product order: each package's `protocol.placer` module, which says where",
    "// the nodes of a model the shell hands it go (docs/modular-graph-phase-f5a-design.md section 6.3), seeded into",
    "// the live registry src/contributions/placerRegistries.ts. buildGraph reads them in this order, so a package's",
    `// place in \`retroplug.products.${product}\` is where its nodes land. Only the planner imports this module, and it`,
    "// carries types and constants alone: no package's node code and no schema.",
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    `import { placersRegistry } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/placerRegistries.ts")}";`,
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.placer)}";`);
  }
  lines.push(
    "",
    "export { placersRegistry };",
    "",
    ...seedLines(["placersRegistry"], contributors.map((p) => ({
      registry: "placersRegistry", pkg: p.pkg.name, items: `[${local(p.pkg.name)}]`,
    }))),
    "",
  );
  return lines.join("\n");
}
```

In `generate`, the shell's directory is `shell.dir`; pass it to both calls:

```js
    {
      path: join(outDir, "consoles.ts"),
      content: renderConsoles({ packages: contributionsOf, outDir, shellDir: shell.dir, product }),
    },
```

```js
    {
      path: join(outDir, "placers.ts"),
      content: renderPlacers({ packages: contributionsOf, outDir, shellDir: shell.dir, product }),
    },
```

In `renderPackages`, the two imports that read the old constants for the checks become the registries (Task 3 replaces
this function; until then its emitted checks read these):

```js
    'import { consolesRegistry } from "./consoles";',
    // The placers are checked against the sections below (a placer's key is its section's), so this module imports
    // them: the control plane already carries them through buildGraph, so only the check is new here.
    'import { placersRegistry } from "./placers";',
```

These replace the existing `'import { CONSOLES } from "./consoles";',` line, its comment, and
`'import { PLACERS } from "./placers";',`. The emitted checks still name `CONSOLES` and `PLACERS`, so emit these two
lines as the first two entries of the `lines.push(` that begins the module's body (the one whose first entries are
`""` and the `PACKAGE_IDS` doc comment), so they follow every import:

```js
    "const CONSOLES = consolesRegistry.items();",
    "const PLACERS = placersRegistry.entries().map(({ pkg, item }) => ({ pkg, placer: item }));",
```

- [ ] **Step 5: The fixture tree gets the shell's contribution modules**

The generator's fixture test copies `test/packages/fixtures/tree` into a scratch directory and generates there, so the
generated modules' imports of `src/contributions/*` must find those files in the copy. They are the real ones, copied in:
the checks the test reads refusals from are the shell's own.

In `packages/retroplug/test/packages/fixtureAliases.mjs` (which already defines `SHELL`, the real shell's directory,
and imports `join`), add `import { cpSync, existsSync } from "node:fs";` and:

```js
/** Copy the shell's hand-written contribution modules (src/contributions, ui/contributions, cli/contributions) into a
 *  fixture tree at `root`: the generated modules import them by a path relative to the tree's own shell, and the checks
 *  a fixture's refusal comes from are the real ones. */
export function copyShellContributions(root) {
  for (const tree of ["src", "ui", "cli"]) {
    const from = join(SHELL, tree, "contributions");
    if (existsSync(from)) cpSync(from, join(root, "packages/retroplug", tree, "contributions"), { recursive: true });
  }
}
```

In `packages/retroplug/test/packages/run-generator.mjs`, import it beside `fixtureAliases`
(`import { copyShellContributions, fixtureAliases } from "./fixtureAliases.mjs";`) and call it right after the
`cpSync(join(FIXTURES, "tree"), root, { recursive: true });` line:

```js
copyShellContributions(root);
```

In `packages/retroplug/test/script/bundle-views.mjs`, do the same after its
`cpSync(join(HERE, "../packages/fixtures/tree"), root, { recursive: true });` (import from
`"../packages/fixtureAliases.mjs"`).

`run-generator.mjs` reads the generated modules' old exports in two loaders. In `loadPlacers` nothing reads an export
(the mode only wants the load's refusal), so it is unchanged. In `loadPackages`, the consoles half becomes:

```js
    consoles: consoles.consolesRegistry.items().map((descriptor) => ({
      id: descriptor.id,
      core: descriptor.core,
      nodeType: descriptor.nodeType,
      platforms: descriptor.platforms.map((platform) => platform.id),
    })),
    realCarts: consoles.realCartsRegistry.items().map((cart) => ({ platform: cart.platform, cartNode: cart.cartNode })),
```

and the placers mode near the end of the file (`result = { placers: result.PLACERS.map(...) }`) becomes:

```js
        result = {
          placers: result.placersRegistry.entries().map(({ pkg, item }) => `${pkg}:${item.key}`),
        };
```

(`grep -n "CONSOLES\|REAL_CARTS\|PLACERS" packages/retroplug/test/packages/run-generator.mjs` must print nothing after
this step. The shell's own seed addition has no items, so it adds no entry to either list.)

- [ ] **Step 6: The consumers read the registries**

In `packages/retroplug/src/consoles.ts`, replace the top of the file, from the header comment through the `SNIFF_LENGTHS`
constant (about the first 49 lines), with the block below; delete the `RANK_ORDERED` constant near the end of the file
(after `detectAt`); and keep every function in between, changing its body to read `index()` as listed after the block.
The new top of the file:

```ts
// The console registry: the shell's one read of the consoles its packages contribute
// (docs/modular-graph-phase-f4a-design.md section 3). The descriptors are the live registry
// src/contributions/consoleRegistries.ts, which src/generated/consoles.ts seeds in product order and a pack appends to,
// and this module turns them into the lookups the shell makes: which core runs a platform, which node type places it,
// what a ROM's bytes are, whether a cart has a battery, and which physical cart a platform's systems can drive. The
// lookups are rebuilt after a commit (`derived`), so a console a pack adds is classified like a built-in one.
//
// src/platform.ts stays the module the callers know (defaultCoreFor, systemNodeTypeFor, detectPlatform, romHasBattery,
// the two sniff lengths), with the same names over these lookups.
import "./generated/consoles";
import { consolesRegistry, realCartsRegistry } from "./contributions/consoleRegistries";
import { derived } from "./contributions/registry";
import type { ConsoleDescriptor, ConsolePlatform, Core, Platform, RealCart } from "@retroplug/core/consoles";
import { extensionLower } from "./pathUtil";

export type {
  ConsoleDescriptor,
  ConsolePlatform,
  ConsoleSplit,
  Core,
  Platform,
  RealCart,
} from "@retroplug/core/consoles";

/** Every registered descriptor, in registration order (the product's, then packs'). */
export function consoleDescriptors(): readonly ConsoleDescriptor[] {
  return consolesRegistry.items();
}

type Detector = { descriptor: ConsoleDescriptor; detect: NonNullable<ConsoleDescriptor["detect"]> };

/** The lookups over the registered consoles, one map each, so a classification never walks the list. */
interface ConsoleIndex {
  readonly byPlatform: Map<Platform, { platform: ConsolePlatform; descriptor: ConsoleDescriptor }>;
  readonly byNodeType: Map<string, ConsoleDescriptor>;
  readonly platforms: readonly ConsolePlatform[];
  readonly cores: readonly Core[];
  readonly realCartByPlatform: Map<Platform, RealCart>;
  /** The distinct prefix lengths the detectors run on, ascending: a file is read once per length, shortest first, and
   *  the read stops at the first answer (see classifyRom). */
  readonly sniffLengths: readonly number[];
  /** The detectors in rank order: the tiebreak among detectors that both match one prefix, so a crafted file carrying
   *  two consoles' signatures classifies the same whatever order the packages registered in. A rank two descriptors
   *  share is broken by their ids, which makes the order total (and the sort's answer independent of the engine's). */
  readonly rankOrdered: readonly Detector[];
}

const index = derived((): ConsoleIndex => {
  const byPlatform = new Map<Platform, { platform: ConsolePlatform; descriptor: ConsoleDescriptor }>();
  const byNodeType = new Map<string, ConsoleDescriptor>();
  const platforms: ConsolePlatform[] = [];
  const cores: Core[] = [];
  const detectors: Detector[] = [];
  const realCartByPlatform = new Map<Platform, RealCart>();
  for (const descriptor of consolesRegistry.items()) {
    byNodeType.set(descriptor.nodeType, descriptor);
    if (!cores.includes(descriptor.core)) cores.push(descriptor.core);
    if (descriptor.detect) detectors.push({ descriptor, detect: descriptor.detect });
    for (const platform of descriptor.platforms) {
      byPlatform.set(platform.id, { platform, descriptor });
      platforms.push(platform);
    }
  }
  for (const cart of realCartsRegistry.items()) realCartByPlatform.set(cart.platform, cart);
  const sniffLengths = [...new Set(detectors.map(({ detect }) => detect.prefixBytes))].sort((a, b) => a - b);
  const rankOrdered = [...detectors].sort((a, b) => {
    if (a.detect.rank !== b.detect.rank) return a.detect.rank - b.detect.rank;
    return a.descriptor.id < b.descriptor.id ? -1 : a.descriptor.id > b.descriptor.id ? 1 : 0;
  });
  return { byPlatform, byNodeType, platforms, cores, realCartByPlatform, sniffLengths, rankOrdered };
});
```

The rest of the file keeps every exported function, signature and doc comment, with these substitutions in the
bodies: `BY_NODE_TYPE` becomes `index().byNodeType`, `BY_PLATFORM` becomes `index().byPlatform`, `PLATFORMS` becomes
`index().platforms`, `CORES` becomes `index().cores`, `SNIFF_LENGTHS` becomes `index().sniffLengths` (read it once into
a local where a function uses it twice: `classifyRom`, `detectPlatform`, `detectAt`), `REAL_CART_BY_PLATFORM` becomes
`index().realCartByPlatform`, `RANK_ORDERED` becomes `index().rankOrdered`, and `registeredRealCarts()` returns
`realCartsRegistry.items()`. `CONSOLE_DESCRIPTORS` is gone (`consoleDescriptors()` replaces it), and so are the
module-level `DETECTORS`, `SNIFF_LENGTHS` and `RANK_ORDERED` constants.

In `packages/retroplug/src/graph/buildGraph.ts`, replace `import { PLACERS } from "../generated/placers";` with
`import { placerEntries } from "../contributions/placers";` and the default parameter
`placers: readonly { pkg: string; placer: GraphPlacer<any> }[] = PLACERS,` with
`placers: readonly { pkg: string; placer: GraphPlacer<any> }[] = placerEntries(),`.

In `packages/retroplug/src/graph/directGraph.ts`, replace `import { PLACERS } from "../generated/placers";` with
`import { placerEntries } from "../contributions/placers";` and, in `setModel`,
`PLACERS.find((entry) => entry.placer.key === key)` with `placerEntries().find((entry) => entry.placer.key === key)`.
Leave its `import "../generated/packages";` and its comment: the kind-filled registry it reaches is M2b's.

In `packages/retroplug/cli/sdkSurfaceCheck.ts`, replace `import { CONSOLES, REAL_CARTS } from "../src/generated/consoles";`
with `import { consoleDescriptors, registeredRealCarts } from "../src/consoles";`, `REAL_CARTS[0]!` with
`registeredRealCarts()[0]!` and `CONSOLES.find(` with `consoleDescriptors().find(`.

The tests that named the old constants:

- `packages/retroplug/test/consoles/registry.test.ts`: the import `CONSOLE_DESCRIPTORS,` becomes `consoleDescriptors,`
  and `for (const descriptor of CONSOLE_DESCRIPTORS)` becomes `for (const descriptor of consoleDescriptors())`.
- `packages/retroplug/test/source-literals/no-cart-literals.test.ts`: `import { CONSOLES } from "../../src/generated/consoles";`
  becomes `import { consoleDescriptors } from "../../src/consoles";`, and each `CONSOLES.` in the file becomes
  `consoleDescriptors().` (lines 28, 31, 33, 43). Its comment on line 12 says "the generated CONSOLES list"; make it
  "the console registry's list".
- `packages/retroplug/test/graph/direct-graph.test.ts`: `import { PLACERS } from "../../src/generated/placers";` becomes
  `import { placerEntries } from "../../src/contributions/placers";` and `PLACERS.find(` becomes `placerEntries().find(`.

- [ ] **Step 7: Regenerate, and refresh the fixture's expected modules**

Run: `node packages/retroplug/scripts/lib/packages.mjs`
Expected: exits 0; `git diff --stat packages/retroplug/src/generated` lists `consoles.ts`, `placers.ts` and
`packages.ts`.

The fixture test compares three of its modules byte for byte with `test/packages/fixtures/expected/`. Write the new
ones from a generate run:

```bash
node -e '
const { execFileSync } = require("child_process");
const fs = require("fs");
const out = JSON.parse(execFileSync("node", ["packages/retroplug/test/packages/run-generator.mjs", "generate"], { encoding: "utf8" }));
for (const name of ["consoles.ts", "placers.ts", "packages.ts"]) {
  fs.writeFileSync("packages/retroplug/test/packages/fixtures/expected/" + name, out.files[name]);
}'
```

Then read `git diff packages/retroplug/test/packages/fixtures/expected`: the consoles and placers modules lost their
inline checks and gained the imports, the re-export and one `commitContributions` call; `packages.ts` changed only in
its two imports and two `const` lines. Anything else in the diff is a mistake in Step 4.

- [ ] **Step 8: Run the tests**

Run: `pnpm test contributions/consoles-live`
Expected: `1..4`, four `ok`, exit 0.

Run: `pnpm test packages/packages-generator`
Expected: every case `ok` (the console, real-cart and placer refusal modes read the same messages from the registries'
checks), exit 0.

The planner's bundle now carries four more modules (`src/contributions/{registry,consoleRegistries,placerRegistries,placers}.ts`),
and `packages/retroplug/test/script/bundle-protocol.test.ts` holds it to a count (`expect(inputs.length <= 22, ...)`, the
count plus one). Raise the ceiling to 26 and make its comment's "22 modules today" say "25 modules today (the console
and placer registries and their primitive among them)".

Run: `pnpm test consoles` then `pnpm test source-literals` then `pnpm test graph` then `pnpm test script-bundle`
Expected: all `ok`, exit 0 (`script/bundle-protocol` holds the planner's bundle to carrying no `src/generated/packages.ts`
and no zod with the new modules in it).

Run: `pnpm typecheck`
Expected: exit 0 (it regenerates every product tree, so the plugin's and packs-host's generated modules import the
shell's `src/contributions` through their own relative paths).

- [ ] **Step 9: Commit**

```bash
git add packages/retroplug/src/contributions/consoleRegistries.ts packages/retroplug/src/contributions/placerRegistries.ts \
  packages/retroplug/src/contributions/placers.ts packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/src/consoles.ts packages/retroplug/src/graph/buildGraph.ts packages/retroplug/src/graph/directGraph.ts \
  packages/retroplug/cli/sdkSurfaceCheck.ts packages/retroplug/src/generated/consoles.ts \
  packages/retroplug/src/generated/placers.ts packages/retroplug/src/generated/packages.ts \
  packages/retroplug/test/packages/fixtureAliases.mjs packages/retroplug/test/packages/run-generator.mjs \
  packages/retroplug/test/script/bundle-views.mjs packages/retroplug/test/packages/fixtures/expected/consoles.ts \
  packages/retroplug/test/packages/fixtures/expected/placers.ts packages/retroplug/test/packages/fixtures/expected/packages.ts \
  packages/retroplug/test/contributions/consoles-live.test.ts packages/retroplug/test/consoles/registry.test.ts \
  packages/retroplug/test/source-literals/no-cart-literals.test.ts packages/retroplug/test/graph/direct-graph.test.ts \
  packages/retroplug/test/script/bundle-protocol.test.ts
git commit -m "contributions: the consoles, real carts and placers are live registries the generated modules seed"
```

### Task 3: The app registries

The roles, ROM providers, tracker integrations, kit formats, project and config sections, project services and embeds.
`appRegistries.ts` carries every check the generated `packages.ts` emits today, word for word, and `appAdditions` maps
one package's app contribution onto the eight registries it fills (the generated module calls it per package; M2b's
loader will call it per pack). The stores' sections and services stay a per-instance snapshot until Task 4; this task
only moves their imports.

**Files:**
- Create: `packages/retroplug/src/contributions/appRegistries.ts`, `packages/retroplug/src/contributions/app.ts`,
  `packages/retroplug/src/appRoleRegistry.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderPackages`, its call in `generate`),
  `packages/retroplug/src/{trackerRegistry,romProviders,appHost,kitFormats,embeds,sramAutoSave,batteryListing,fileDrop,
  systemsStore,configSections,userConfig,userConfigSerialization,userConfigStore,projectConfig,projectStore}.ts`,
  `packages/retroplug/src/render/render.ts`, `packages/retroplug/test/packages/run-generator.mjs`
- Regenerate: `packages/retroplug/src/generated/packages.ts`, `packages/retroplug/test/packages/fixtures/expected/packages.ts`
- Test: `packages/retroplug/test/contributions/app-live.test.ts`; modify
  `packages/retroplug/test/system-settings/registry.test.ts`, `packages/retroplug/test/kit/kit-formats.test.ts`,
  `packages/retroplug/test/source-literals/no-cart-literals.test.ts`, `packages/retroplug/test/config/config-sections.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/app-live.test.ts`:

```ts
// The app's registries follow a commit (docs/downloadable-packs-m2a-plan.md, Task 3): a role, a ROM provider, a tracker
// integration, a kit format and an embed committed after the built-ins reach the role registry the stores drive, the
// tracker lookups, the kit dispatch and the embed lookups; and a commit the app's checks refuse changes nothing. Every
// id here is this file's own.
import { test, expect } from "@retroplug/testing";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import type { KitFormat } from "@retroplug/dsp/kitContract";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import {
  appAdditions,
  embedsRegistry,
  bakedEmbedsRegistry,
  kitFormatsRegistry,
  rolesRegistry,
} from "../../src/contributions/app";
import { buildAppRegistry } from "../../src/appHost";
import { resolveTracker, trackerIntegrations, trackerMarkers } from "../../src/trackerRegistry";
import { kitFormat, kitFormats } from "../../src/kitFormats";
import { embeddedRomOf, embeddedRoms } from "../../src/embeds";

const fixtureRole = {
  kind: "packfixture-core",
  category: "system",
  label: "Pack Fixture",
  schema: { parse: (raw: unknown) => ({ level: 3, ...(raw as object) }) },
} as unknown as RoleType;

const markerRole = {
  kind: "packfixture-marker",
  category: "feature",
  label: "Pack Fixture Tracker",
  schema: { parse: () => ({}) },
} as unknown as RoleType;

const fixtureConsole = {
  id: "packfixture",
  core: "packfixture-core",
  nodeType: "rp.packfixture.system",
  platforms: [{ id: "packfixture", label: "Pack Fixture", extensions: [".rpk"] }],
} as unknown as ConsoleDescriptor;

const fixtureTracker = {
  id: "packfixture-tracks",
  label: "Pack Fixture",
  markerRole: "packfixture-marker",
} as unknown as TrackerIntegration;

const fixtureKit = { id: "packfixture-kit" } as unknown as KitFormat<unknown>;

test("the built-in app contributions are seeded", () => {
  expect(rolesRegistry.seeded).toBe(true);
  expect(rolesRegistry.items().length).toBeGreaterThan(0);
  expect(trackerIntegrations().length).toBeGreaterThan(0);
});

test("a pack's app contribution reaches a role registry built before it, and the tracker and kit lookups", () => {
  // Built before the commit: the stores hold one for their whole life, so it must adopt what arrives later.
  const registry = buildAppRegistry();
  expect(registry.roleType("packfixture-core")).toBe(undefined);
  commitContributions([
    { registry: consolesRegistry, pkg: "packfixture", items: [fixtureConsole] },
    ...appAdditions("packfixture", {
      roles: [fixtureRole, markerRole],
      romProviders: [
        (rom) => (rom.platform === "packfixture" ? [{ kind: "packfixture-marker", config: {} }] : []),
      ],
      trackerIntegrations: [fixtureTracker],
      kitFormats: [fixtureKit],
    }),
  ]);
  expect(registry.roleType("packfixture-core")?.label).toBe("Pack Fixture");
  expect(registry.systemRoleFor("packfixture-core")?.kind).toBe("packfixture-core");
  const roles = registry.defaultRoles("packfixture-core", "packfixture", new Uint8Array(0));
  expect(roles.map((role) => role.kind)).toEqual(["packfixture-core", "packfixture-marker"]);
  expect(roles[0].config).toEqual({ level: 3 });
  expect(trackerMarkers().has("packfixture-marker")).toBe(true);
  expect(resolveTracker([{ kind: "packfixture-marker", config: {} }])?.id).toBe("packfixture-tracks");
  expect(kitFormats().some((format) => format.id === "packfixture-kit")).toBe(true);
  expect(kitFormat("packfixture-kit")).toBe(fixtureKit);
});

test("a console whose core no system role names is refused with the roles' message", () => {
  const orphan = { ...fixtureConsole, id: "orphan", core: "orphan-core", nodeType: "rp.orphan.system",
    platforms: [{ id: "orphan", label: "Orphan", extensions: [".orp"] }] } as unknown as ConsoleDescriptor;
  expect(() => commitContributions([{ registry: consolesRegistry, pkg: "orphan", items: [orphan] }])).toThrow(
    "the console orphan names the core orphan-core, whose system role no package contributes",
  );
});

test("a duplicate kit format id is refused and nothing moves", () => {
  const count = kitFormatsRegistry.items().length;
  expect(() => commitContributions(appAdditions("packfixture-two", { kitFormats: [fixtureKit] }))).toThrow(
    "the app's kit formats declare the id packfixture-kit twice",
  );
  expect(kitFormatsRegistry.items().length).toBe(count);
});

test("an embed needs its manifest half from the same package, and a console for its platform", () => {
  const embed = { id: "packfixture-synth", label: "Pack Synth", platform: "packfixture" };
  expect(() => commitContributions(appAdditions("packfixture", { embeds: [embed] }))).toThrow(
    "the package packfixture declares the embedded ROM packfixture-synth, which its manifest does not bake in " +
      "(its retroplug.embed)",
  );
  commitContributions([
    ...appAdditions("packfixture", { embeds: [embed] }),
    { registry: bakedEmbedsRegistry, pkg: "packfixture", items: [{ id: "packfixture-synth" }] },
  ]);
  expect(embeddedRomOf("packfixture-synth")?.label).toBe("Pack Synth");
  expect(embeddedRoms().some((e) => e.id === "packfixture-synth")).toBe(true);
  expect(embedsRegistry.entries().find((e) => e.item.id === "packfixture-synth")?.pkg).toBe("packfixture");
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test contributions/app-live`
Expected: `BUILD FAILED: contributions/app-live` (`Could not resolve "../../src/contributions/app"`).

- [ ] **Step 3: The app registries**

Create `packages/retroplug/src/contributions/appRegistries.ts`:

```ts
// The app's live registries (docs/downloadable-packs-m2a-plan.md): each package's app contribution (its definePackage
// default export) fills eight of them, and a package's manifest `retroplug.embed` fills a ninth (the embedded ROMs whose
// bytes the binary carries). src/generated/packages.ts seeds them with the product's packages, in product order, and a
// pack appends (appAdditions maps one contribution onto the eight). The checks below are the ones the generator used to
// emit into that module, word for word, and they run at every commit over every registry seeded in the bundle, so a
// pack is held to exactly what a built-in package is. src/contributions/app.ts is the module the shell reads them
// through.
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";
import type { RoleType, RomProvider } from "@retroplug/core/systemRoles";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import type { KitFormat } from "@retroplug/dsp/kitContract";
import type { Embed, PackageContributions, ProjectSection } from "@retroplug/core/definePackage";
import type { ProjectServiceFactory } from "@retroplug/tracker/projectService";
import { Registry, derived, type Addition, type Contribution } from "./registry";
import { consolesRegistry } from "./consoleRegistries";
import { placersRegistry } from "./placerRegistries";

/** The settings keys a package may not contribute a section under: the shell's own (projectConfig.ts's
 *  CORE_SETTING_KEYS, which test/project/sections.test.ts holds this list to). */
export const SHELL_SECTION_KEYS: readonly string[] = ["layout", "midiRouting", "audioRouting", "zoom"];

/** The config.json root keys a package may not contribute a section under: the shell's own (userConfig.ts's
 *  CORE_CONFIG_KEYS, which test/config/config-sections.test.ts holds this list to). A copy, because userConfig.ts reads
 *  the registries and this module may not import it back. */
export const SHELL_CONFIG_KEYS: readonly string[] = ["schemaVersion", "activeKeyboardBindings", "activeGamepadBindings",
  "defaultZoom", "sramAutoSave", "render", "useNativeFileDialogs"];

/** The app's roles: the role contract's objects, which a RoleRegistry holds by kind. A console's core is the kind of its
 *  core-config role (the role is named after its core), so a descriptor naming a core nothing contributes could be
 *  classified and never configured: that check is here rather than with the consoles because the roles are these. */
export const rolesRegistry = new Registry<RoleType>("roles", (entries, view) => {
  const kinds = new Set<string>();
  for (const { item: role } of entries) {
    if (kinds.has(role.kind)) {
      throw new Error(`the app's roles declare the role kind ${role.kind} twice`);
    }
    kinds.add(role.kind);
  }
  for (const { item: descriptor } of view(consolesRegistry)) {
    if (!entries.some(({ item: role }) => role.kind === descriptor.core && role.category === "system")) {
      throw new Error(`the console ${descriptor.id} names the core ${descriptor.core}, whose system ` +
        "role no package contributes");
    }
  }
});

/** The ROM providers: the first to match a ROM fixes its feature roles' order, so this list's order is behaviour. */
export const romProvidersRegistry = new Registry<RomProvider>("romProviders");

/** The tracker integrations. A system's tracker is the first of its marker roles the system's own order places
 *  (resolveTracker), which a load orders by the ROM's marker; a marker claimed twice is refused. */
export const trackerIntegrationsRegistry = new Registry<TrackerIntegration>("trackerIntegrations", (entries) => {
  const markers = new Set<string>();
  for (const { item: integration } of entries) {
    if (markers.has(integration.markerRole)) {
      throw new Error(`the app's tracker integrations declare the marker role ${integration.markerRole} twice`);
    }
    markers.add(integration.markerRole);
  }
});

/** The kit bank formats, by the id the audio driver dispatches with. */
export const kitFormatsRegistry = new Registry<KitFormat<any>>("kitFormats", (entries) => {
  const ids = new Set<string>();
  for (const { item: format } of entries) {
    if (ids.has(format.id)) {
      throw new Error(`the app's kit formats declare the id ${format.id} twice`);
    }
    ids.add(format.id);
  }
});

/** The project sections: a block under a .rplg's `settings` which its package parses, defaults and migrates
 *  (docs/modular-graph-phase-f5a-design.md section 6.1). A key the shell owns, and a key two packages declare, are
 *  refused: the shell keeps every non-core key verbatim, so a duplicate would have one section silently replace the
 *  other's bytes in every project. */
export const projectSectionsRegistry = new Registry<ProjectSection>("projectSections", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: section } of entries) {
    if (SHELL_SECTION_KEYS.includes(section.key)) {
      throw new Error(`the package ${pkg} declares the project section ${section.key}, which is one of the ` +
        `shell's own settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    const owner = owners.get(section.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the project section ${section.key} twice`
        : `two packages declare the project section ${section.key}: ${owner} and ${pkg}`);
    }
    owners.set(section.key, pkg);
  }
});

/** The project services: a feature's control-plane half, which the shell builds one instance of per project store
 *  (docs/modular-graph-phase-f5a-design.md section 6.2). A key is a model's and a section's, so two services under one
 *  key would fight over the graph's placement; and a placer's key is the section whose model it places, so the same two
 *  rules hold where the placement is made (checked here, where the sections are known). */
export const projectServicesRegistry = new Registry<ProjectServiceFactory>("projectServices", (entries, view) => {
  const sectionOwners = new Map<string, string>();
  const sectionKeysByPkg = new Map<string, string[]>();
  for (const { pkg, item: section } of view(projectSectionsRegistry)) {
    sectionOwners.set(section.key, pkg);
    sectionKeysByPkg.set(pkg, [...(sectionKeysByPkg.get(pkg) ?? []), section.key]);
  }
  const serviceKeys = new Map<string, string>();
  for (const { pkg, item: service } of entries) {
    const owner = serviceKeys.get(service.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the project service ${service.key} twice`
        : `two packages declare the project service ${service.key}: ${owner} and ${pkg}`);
    }
    serviceKeys.set(service.key, pkg);
  }
  for (const { pkg, item: service } of entries) {
    // A service's key is the section it reads and the model its placer places: a key no package declares a section
    // for would read undefined where the store asserts, and one of the shell's keys would fight the shell's field.
    if (SHELL_SECTION_KEYS.includes(service.key)) {
      throw new Error(`the package ${pkg} declares the project service ${service.key}, which is one of the shell's ` +
        `own settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    // A package that declares no section at all is not held to one: a fixture tree may replace an app module, and a
    // service key is judged against the sections the product actually declares.
    if (!sectionOwners.has(service.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {
      throw new Error(`the package ${pkg} declares the project service ${service.key}, which matches no project ` +
        "section (the service key is its section's key)");
    }
  }
  for (const { pkg, item: placer } of view(placersRegistry)) {
    if (SHELL_SECTION_KEYS.includes(placer.key)) {
      throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which is one of the shell's own ` +
        `settings (${SHELL_SECTION_KEYS.join(", ")})`);
    }
    if (!sectionOwners.has(placer.key) && (sectionKeysByPkg.get(pkg) ?? []).length > 0) {
      throw new Error(`the package ${pkg} places the graph's model ${placer.key}, which matches no project ` +
        "section (a placer's key is its section's key)");
    }
  }
});

/** The config.json sections: a root key of the user config which its package parses, defaults and migrates
 *  (docs/modular-graph-phase-f5b-design.md section 6.1). The shell keeps every non-core root key verbatim, so the same
 *  two refusals hold: a key the shell owns, and a key two packages declare. */
export const configSectionsRegistry = new Registry<ProjectSection>("configSections", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: section } of entries) {
    if (SHELL_CONFIG_KEYS.includes(section.key)) {
      throw new Error(`the package ${pkg} declares the config section ${section.key}, which is one of the ` +
        `shell's own keys (${SHELL_CONFIG_KEYS.join(", ")})`);
    }
    const owner = owners.get(section.key);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the config section ${section.key} twice`
        : `two packages declare the config section ${section.key}: ${owner} and ${pkg}`);
    }
    owners.set(section.key, pkg);
  }
});

/** The embedded ROMs the packages declare: the id native's resource table knows one by, the label a menu offers it
 *  under, and the console it runs on. A declaration and a manifest entry (bakedEmbedsRegistry) are one embedded ROM's
 *  two halves: the shell loads it by the declaration, native boots the bytes the manifest names, so neither can stand
 *  alone. The id belongs to the package whose bytes carry it, an id is one ROM, and its platform is a console this
 *  product runs - the same rule the consoles' own checks hold the descriptors to. */
export const embedsRegistry = new Registry<Embed>("embeds", (entries, view) => {
  const baked = view(bakedEmbedsRegistry);
  const platforms = new Set<string>();
  for (const { item: descriptor } of view(consolesRegistry)) {
    for (const platform of descriptor.platforms) platforms.add(platform.id);
  }
  for (const { pkg, item: embed } of entries) {
    const declarers = entries.filter((entry) => entry.item.id === embed.id).map((entry) => entry.pkg);
    if (declarers.length > 1) {
      const [first, second] = declarers;
      throw new Error(first === second
        ? `the package ${first} declares the embedded ROM ${embed.id} twice: an id is one ROM`
        : `two packages declare the embedded ROM ${embed.id}: ${first} and ${second}`);
    }
    const bakers = baked.filter((entry) => entry.item.id === embed.id);
    if (!bakers.length) {
      throw new Error(`the package ${pkg} declares the embedded ROM ${embed.id}, which its manifest does ` +
        "not bake in (its retroplug.embed)");
    }
    if (!bakers.some((entry) => entry.pkg === pkg)) {
      throw new Error(`the embedded ROM ${embed.id} is declared by ${pkg} and baked in by ${bakers[0].pkg}: ` +
        "the two halves belong to one package");
    }
    if (!platforms.has(embed.platform)) {
      throw new Error(`the embedded ROM ${embed.id} names the platform ${embed.platform}, which no console ` +
        "descriptor registers");
    }
  }
});

/** The manifest half of each embedded ROM: the ids a package's `retroplug.embed` puts in the binary. */
export const bakedEmbedsRegistry = new Registry<{ readonly id: string }>("bakedEmbeds", (entries, view) => {
  const declared = view(embedsRegistry);
  for (const { pkg, item } of entries) {
    if (!declared.some((entry) => entry.item.id === item.id)) {
      throw new Error(`the package ${pkg} bakes in the embedded ROM ${item.id}, which no app module ` +
        "declares (an embeds entry with its label and platform)");
    }
  }
});

/** One package's app contribution as the additions a commit takes: the eight registries an app module fills, in one
 *  order, an absent member an empty addition. The generated registry calls this once per package; a pack's loader once
 *  per pack. */
export function appAdditions(pkg: string, app: PackageContributions): Addition<any>[] {
  return [
    { registry: rolesRegistry, pkg, items: app.roles ?? [] },
    { registry: romProvidersRegistry, pkg, items: app.romProviders ?? [] },
    { registry: trackerIntegrationsRegistry, pkg, items: app.trackerIntegrations ?? [] },
    { registry: kitFormatsRegistry, pkg, items: app.kitFormats ?? [] },
    { registry: projectSectionsRegistry, pkg, items: app.projectSections ?? [] },
    { registry: projectServicesRegistry, pkg, items: app.projectServices ?? [] },
    { registry: configSectionsRegistry, pkg, items: app.configSections ?? [] },
    { registry: embedsRegistry, pkg, items: app.embeds ?? [] },
  ];
}

/** A section entry as the shell's section code reads one (projectConfig.ts's SectionEntry, configSections.ts's
 *  ConfigSectionEntry): its contributor and the package a warning names. */
type SectionEntryShape = { readonly pkg: string; readonly section: ProjectSection };

function sectionEntries(entries: readonly Contribution<ProjectSection>[]): readonly SectionEntryShape[] {
  return entries.map(({ pkg, item }) => ({ pkg, section: item }));
}

/** The project sections in the shape the section code reads: the same array until the next commit. */
export const projectSectionEntries = derived(() => sectionEntries(projectSectionsRegistry.entries()));

/** The config sections in the shape the section code reads: the same array until the next commit. */
export const configSectionEntries = derived(() => sectionEntries(configSectionsRegistry.entries()));

/** The project services with their packages, in the shape a store builds them from. */
export const projectServiceEntries = derived((): readonly { pkg: string; service: ProjectServiceFactory }[] =>
  projectServicesRegistry.entries().map(({ pkg, item }) => ({ pkg, service: item })),
);
```

Create `packages/retroplug/src/contributions/app.ts`:

```ts
// The app facade: importing this seeds the app's registries (through the generated module, which seeds the consoles
// and the placers first, since the app's checks read them), and re-exports them. A shell module that reads an app
// registry imports it from here, never from the generated module or appRegistries.ts directly, so the seed has always
// run by the time anything reads.
import "../generated/packages";

export * from "./appRegistries";
```

- [ ] **Step 4: The generator seeds them**

In `packages/retroplug/scripts/lib/packages.mjs`, replace `renderPackages` from its first line through the
`lines.push(` that ends with the `CONFIG_SECTIONS` checks (the line `"",` right before `...kindBlocks({ packages, kindDecls }),`),
and the embeds block after `kindBlocks` through the function's end, with this function. `kindBlocks` and its output are
unchanged; this function calls it in the same place, after the seed, so a kind still sees the app modules as before.

```js
function renderPackages({ packages, outDir, shellDir, names, product }) {
  const app = packages.filter((p) => p.app);
  // Each declaring package's kind module: the control-plane half's `accept` and `check`, imported rather than
  // named by the generator, so a kind is data (docs/modular-graph-phase-f5a-design.md section 6.5).
  const kindDecls = packages.flatMap((p) => Object.values(p.kinds).map((decl) => ({ pkg: p, decl })));
  const registries = [
    "rolesRegistry", "romProvidersRegistry", "trackerIntegrationsRegistry", "kitFormatsRegistry",
    "projectSectionsRegistry", "projectServicesRegistry", "configSectionsRegistry", "embedsRegistry",
    "bakedEmbedsRegistry",
  ];
  const appRegistries = contributionsSpecifier(outDir, shellDir, "src/contributions/appRegistries.ts");
  const lines = [];
  lines.push(
    `${generatedFrom(product)} (the root`,
    `// package.json's retroplug.products.${product}) and the packages' manifests. Do not edit; edit a manifest and`,
    "// rebuild.",
    "//",
    "// The app's registrations, in product order (the list's, which is a dependency order, then the shell), seeded into",
    "// the live registries src/contributions/appRegistries.ts holds: the roles buildAppRegistry applies, the ROM",
    "// providers it offers, the tracker integrations the Songs and Assets menus search, the kit formats the audio driver",
    "// dispatches to, the project sections and services each package owns, the config sections and the embedded ROMs;",
    "// then the contribution kinds' items. The consoles and the real carts are apart, in ./consoles.ts, the protocol",
    "// tables and the kinds' data in ./protocol.ts and the graph placers in ./placers.ts, so a module that needs one of",
    "// those imports no package's app; this module imports the consoles and the placers first, for their seeds, because",
    "// the app's checks read them.",
    'import "./consoles";',
    'import "./placers";',
    'import type { PackageContributions } from "@retroplug/core/definePackage";',
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    `import { appAdditions, ${registries.join(", ")} } from "${appRegistries}";`,
  );
  for (const p of app) lines.push(`import ${identifier(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.app)}";`);
  // The kind's data and the product's node types reach the declaring package's check: a mode naming a node type no
  // package declares would place nothing.
  if (kindDecls.length) lines.push('import { KIND_DATA, PROTOCOL_NODES, ROLE_NODES } from "./protocol";');
  for (const { pkg: owner, decl } of kindDecls) {
    lines.push(
      `import ${kindLocal(owner.pkg.name, decl.kind)} from "${moduleSpecifier(outDir, owner.pkg, decl.control)}";`,
    );
  }
  const baked = packages.flatMap((p) => p.embeds.map((entry) => ({ pkg: p.pkg.name, id: entry.id })));
  lines.push(
    "",
    `export { ${registries.join(", ")}, SHELL_CONFIG_KEYS } from "${appRegistries}";`,
    "",
    "/** The product's package ids, in product order, the shell excluded: what a contribution's `pkg` names, and the",
    " *  prefix a node type of the package's starts with (a few name a console instead: rp.sms.sync is smsggdj's). */",
    "export const PACKAGE_IDS: readonly string[] = [",
    ...names.map((name) => `  ${JSON.stringify(name)},`),
    "];",
    "",
    "/** Each package's app contribution, in product order. */",
    "const APPS: readonly { pkg: string; app: PackageContributions }[] = [",
    ...app.map((p) => `  { pkg: ${JSON.stringify(p.pkg.name)}, app: ${identifier(p.pkg.name)} },`),
    "];",
    "",
    ...seedLines(registries, []).slice(0, -1),
    "  ...APPS.flatMap(({ pkg, app }) => appAdditions(pkg, app)),",
    ...baked.map((entry) =>
      `  { registry: bakedEmbedsRegistry, pkg: ${JSON.stringify(entry.pkg)}, items: [{ id: ${JSON.stringify(entry.id)} }] },`),
    "]);",
    "",
    ...kindBlocks({ packages, kindDecls }),
  );
  return lines.join("\n");
}
```

(`seedLines(registries, [])` returns the opening line, the nine empty additions and the closing `"]);"`; `.slice(0, -1)`
drops the closing line so the app additions and the baked embeds join the same array.) `manifestsWithEmbeds` and
`contributionLines` are no longer called: delete both. In `generate`, pass the shell's directory:

```js
      content: renderPackages({ packages: contributionsOf, outDir, shellDir: shell.dir, names, product }),
```

(keep whatever other arguments the existing call passes; `grep -n "renderPackages(" packages/retroplug/scripts/lib/packages.mjs`
finds it).

The kind block reads `app_<pkg>.controllerApps` from the app modules directly, so it is unchanged by this task (M2b makes
the kinds live with the protocol half).

- [ ] **Step 5: The consumers**

`packages/retroplug/src/appRoleRegistry.ts` (new):

```ts
// The control plane's role registry as a live view (docs/downloadable-packs-m2a-plan.md): a store graph holds one
// RoleRegistry for its whole life, so the one buildAppRegistry builds adopts the roles and ROM providers committed
// after it was made, at its next read. The registries only grow, so it keeps how many of each it has taken and takes the
// rest, in registration order (which is the providers' behaviour: the first to match a ROM fixes its roles' order).
import { RoleRegistry, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
import type { Core, Platform } from "@retroplug/core/platform";
import { rolesRegistry, romProvidersRegistry } from "./contributions/app";

export class AppRoleRegistry extends RoleRegistry {
  private rolesTaken = 0;
  private providersTaken = 0;

  /** The built-ins first, as buildAppRegistry always applied them: a caller's own registerRole or registerRomProvider
   *  then comes after them (a hand-registered provider runs after the product's, a hand-registered role replaces a
   *  built-in of its kind), and only a later commit's entries are adopted lazily. */
  constructor() {
    super();
    this.adopt();
  }

  /** Take the roles and providers committed since the last read. */
  private adopt(): void {
    const roles = rolesRegistry.items();
    for (; this.rolesTaken < roles.length; this.rolesTaken++) super.registerRole(roles[this.rolesTaken]);
    const providers = romProvidersRegistry.items();
    for (; this.providersTaken < providers.length; this.providersTaken++) {
      super.registerRomProvider(providers[this.providersTaken]);
    }
  }

  override roleType(kind: string): RoleType | undefined {
    this.adopt();
    return super.roleType(kind);
  }

  override systemRoleFor(core: string): RoleType | undefined {
    this.adopt();
    return super.systemRoleFor(core);
  }

  override defaultRoles(core: Core, platform: Platform, header: Uint8Array, embeddedRom = ""): RoleInstance[] {
    this.adopt();
    return super.defaultRoles(core, platform, header, embeddedRom);
  }
}
```

`packages/retroplug/src/appHost.ts`: replace the two imports `import { RoleRegistry } from "@retroplug/core/systemRoles";`
and `import { ROLES } from "./generated/packages";` and `import { registerRomProviders } from "./romProviders";` with
`import type { RoleRegistry } from "@retroplug/core/systemRoles";` and `import { AppRoleRegistry } from "./appRoleRegistry";`,
and the function with:

```ts
/** The control-plane role registry: backend system roles + the feature roles + the ROM providers that attach features
 *  by ROM identity, as registered now and as committed later (a pack's), adopted at the registry's next read. */
export function buildAppRegistry(): RoleRegistry {
  return new AppRoleRegistry();
}
```

In the file's header comment, "each package's app contribution is generated into src/generated/packages.ts" becomes
"each package's app contribution is seeded into the live registries by src/generated/packages.ts".

`packages/retroplug/src/romProviders.ts`: replace `import { ROM_PROVIDERS } from "./generated/packages";` with
`import { romProvidersRegistry } from "./contributions/app";` and the loop with
`for (const provider of romProvidersRegistry.items()) registry.registerRomProvider(provider);`. Its header's
"generated into src/generated/packages.ts" becomes "registered in the live registry src/contributions/appRegistries.ts".

`packages/retroplug/src/trackerRegistry.ts`: replace `import { TRACKER_INTEGRATIONS } from "./generated/packages";` with
`import { trackerIntegrationsRegistry } from "./contributions/app";` and `import { derived } from "./contributions/registry";`;
replace the re-export block and `TRACKER_MARKERS` with:

```ts
/** Every tracker integration, in registration order (the product's, then packs'). A system takes the first whose
 *  markerRole its graph places, in the order its OWN roles name them (resolveTracker): of two markers a stored list
 *  carries, the load moves the one today's ROM earns ahead of the stale one (SystemsStore.suggestedFirst), so this order
 *  decides only when the ROM earns neither. A marker has no protocol node, so it is placed whenever it is stored. */
export function trackerIntegrations(): readonly TrackerIntegration[] {
  return trackerIntegrationsRegistry.items();
}

/** The role kinds a cart's tracker is marked by: one per integration, refused twice over by the registry's check. */
export const trackerMarkers = derived((): ReadonlySet<string> =>
  new Set(trackerIntegrationsRegistry.items().map((t) => t.markerRole)),
);
```

and in `resolveTracker`, `TRACKER_INTEGRATIONS.find(` becomes `trackerIntegrations().find(`.

The four readers of the old names: `src/sramAutoSave.ts`, `src/batteryListing.ts`, `src/fileDrop.ts` and
`src/render/render.ts` import `trackerIntegrations` instead of `TRACKER_INTEGRATIONS` from the same module and call it
where they read the constant (`for (const t of trackerIntegrations())`, `trackerIntegrations().map(`,
`trackerIntegrations().filter(`). In `src/fileDrop.ts` the module constant
`const SONG_EXTENSIONS: readonly string[] = TRACKER_INTEGRATIONS.flatMap(...)` becomes a function read at the drop:

```ts
const songExtensions = derived((): readonly string[] =>
  trackerIntegrations().flatMap((t) => t.songImport?.extensions ?? []),
);
```

(import `derived` from `./contributions/registry`) and `isSongPath(SONG_EXTENSIONS, p)` becomes
`isSongPath(songExtensions(), p)`. In `src/systemsStore.ts`, the import `TRACKER_MARKERS` becomes `trackerMarkers` and
`TRACKER_MARKERS.has(kind)` becomes `trackerMarkers().has(kind)`.

`packages/retroplug/src/kitFormats.ts`: replace `import { KIT_FORMATS } from "./generated/packages";` with
`import { kitFormatsRegistry } from "./contributions/app";`, the `export { KIT_FORMATS };` block with:

```ts
/** The product's kit formats, then packs', in registration order: what kitFormat resolves an id through. */
export function kitFormats(): readonly KitFormat<any>[] {
  return kitFormatsRegistry.items();
}
```

and `resolveKitFormat(id, KIT_FORMATS)` with `resolveKitFormat(id, kitFormats())`.

`packages/retroplug/src/embeds.ts`: replace `import { EMBEDS } from "./generated/packages";` with
`import { embedsRegistry } from "./contributions/app";`; `return EMBEDS;` becomes `return embedsRegistry.items();` and
`EMBEDS.find(` becomes `embedsRegistry.items().find(`. Its header's "The generated list is src/generated/packages.ts"
paragraph becomes "The list is the live registry src/contributions/appRegistries.ts, which src/generated/packages.ts
seeds beside the roles and the providers, in product order."

The section and service modules (made live in Task 4; this step only re-points them):

- `src/configSections.ts`, `src/userConfig.ts`, `src/userConfigSerialization.ts`: replace
  `import { CONFIG_SECTIONS } from "./generated/packages";` with `import { configSectionEntries } from "./contributions/app";`
  and every default parameter `= CONFIG_SECTIONS` with `= configSectionEntries()` (a default parameter is evaluated per
  call, so each call reads the registry as it stands).
- `src/userConfigStore.ts`: the same import change; the constructor's `configSections: readonly ConfigSectionEntry[] =
  CONFIG_SECTIONS,` becomes `= configSectionEntries(),`.
- `src/projectConfig.ts`: replace `import { PROJECT_SECTIONS } from "./generated/packages";` with
  `import { projectSectionEntries } from "./contributions/app";` and every `= PROJECT_SECTIONS` default with
  `= projectSectionEntries()`.
- `src/projectStore.ts`: replace `import { PROJECT_SERVICES, PROJECT_SECTIONS } from "./generated/packages";` with
  `import { projectSectionEntries, projectServiceEntries } from "./contributions/app";`;
  `contributions.sections ?? PROJECT_SECTIONS` becomes `contributions.sections ?? projectSectionEntries()` and
  `(contributions.services ?? PROJECT_SERVICES)` becomes `(contributions.services ?? projectServiceEntries())`.

`grep -rnE '(from|import) "(\.\./|\./)+generated/packages"' packages/retroplug/src packages/retroplug/ui packages/retroplug/cli`
must now print only `src/contributions/app.ts`, `src/graph/directGraph.ts` (its side-effect import for the kinds, M2b's)
and the generated modules themselves. The comments that still say a list is generated into `src/generated/packages.ts`
(`src/app.ts`, `src/audioDriver.ts`, `src/kitFormats.ts`, `src/trackerRegistry.ts`) say it is seeded into the live
registries by that module.

The tests that named the old constants:

- `test/system-settings/registry.test.ts`: `import { ROLES } from "../../src/generated/packages";` becomes
  `import { rolesRegistry } from "../../src/contributions/app";` and `ROLES.map(` becomes `rolesRegistry.items().map(`.
- `test/kit/kit-formats.test.ts`: the import `KIT_FORMATS,` becomes `kitFormats,` and each `KIT_FORMATS` read becomes
  `kitFormats()`.
- `test/source-literals/no-cart-literals.test.ts`: the import line becomes
  `import { PACKAGE_IDS } from "../../src/generated/packages";` plus
  `import { embedsRegistry, kitFormatsRegistry, trackerIntegrationsRegistry } from "../../src/contributions/app";`, and
  `EMBEDS.map(`, `TRACKER_INTEGRATIONS.map(`, `KIT_FORMATS.map(` become `embedsRegistry.items().map(`,
  `trackerIntegrationsRegistry.items().map(`, `kitFormatsRegistry.items().map(`.
- `test/config/config-sections.test.ts`: `import { SHELL_CONFIG_KEYS } from "../../src/generated/packages";` becomes
  `import { SHELL_CONFIG_KEYS } from "../../src/contributions/app";`.
- `test/project/sections.test.ts`: add a case holding the section keys' copy to the shell's, beside its other cases:

```ts
test("the registry's reserved section keys are the shell's own settings keys", () => {
  expect([...SHELL_SECTION_KEYS]).toEqual([...CORE_SETTING_KEYS]);
});
```

  with `import { SHELL_SECTION_KEYS } from "../../src/contributions/app";` and `CORE_SETTING_KEYS` added to its
  `../../src/projectConfig` import (export it from `projectConfig.ts` if it is not exported).

In `packages/retroplug/test/packages/run-generator.mjs`, `loadPackages` reads the registries the generated module
re-exports:

```js
  return {
    roles: loaded.rolesRegistry.items().map((role) => role.kind),
    romProviders: loaded.romProvidersRegistry.items().map((provider) => provider.id),
    trackerIntegrations: loaded.trackerIntegrationsRegistry.items().map((integration) => integration.id),
    kitFormats: loaded.kitFormatsRegistry.items().map((format) => format.id),
    projectSections: loaded.projectSectionsRegistry.entries().map((entry) => `${entry.pkg}:${entry.item.key}`),
    projectServices: loaded.projectServicesRegistry.items().map((service) => service.key),
    configSections: loaded.configSectionsRegistry.entries().map((entry) => `${entry.pkg}:${entry.item.key}`),
    embeds: loaded.embedsRegistry.items().map((embed) => ({ id: embed.id, label: embed.label, platform: embed.platform })),
    consoles: consoles.consolesRegistry.items().map((descriptor) => ({
```

(the consoles half as Task 2 left it). `grep -n "loaded\.[A-Z]" packages/retroplug/test/packages/run-generator.mjs`
must print only the menus, views, actions and CLI loaders, which Tasks 5 and 7 change.

- [ ] **Step 6: Regenerate and refresh the fixture's `packages.ts`**

Run: `node packages/retroplug/scripts/lib/packages.mjs`
Expected: exit 0; `packages/retroplug/src/generated/packages.ts` now holds the imports, the re-export, `PACKAGE_IDS`,
`APPS`, one `commitContributions` call and the kind block, and no `throw`.

```bash
node -e '
const { execFileSync } = require("child_process");
const fs = require("fs");
const out = JSON.parse(execFileSync("node", ["packages/retroplug/test/packages/run-generator.mjs", "generate"], { encoding: "utf8" }));
fs.writeFileSync("packages/retroplug/test/packages/fixtures/expected/packages.ts", out.files["packages.ts"]);'
```

Read `git diff packages/retroplug/test/packages/fixtures/expected/packages.ts`: the checks are gone, the seed and the
re-export are new, the kind block is unchanged.

- [ ] **Step 7: Run the tests**

Run: `pnpm test contributions`
Expected: every case of `contributions/registry`, `contributions/consoles-live` and `contributions/app-live` `ok`, exit 0.

Run: `pnpm test packages/packages-generator`
Expected: every case `ok` (each refusal mode, `duplicate-role-kind` through `embed-mismatched-package`, reads the same
message from the registries' checks), exit 0.

Run: `pnpm test`
Expected: exit 0, the summary listing no failed file.

Run: `pnpm typecheck`
Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add packages/retroplug/src/contributions/appRegistries.ts packages/retroplug/src/contributions/app.ts \
  packages/retroplug/src/appRoleRegistry.ts packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/src/generated/packages.ts packages/retroplug/src/appHost.ts packages/retroplug/src/romProviders.ts \
  packages/retroplug/src/trackerRegistry.ts packages/retroplug/src/sramAutoSave.ts packages/retroplug/src/batteryListing.ts \
  packages/retroplug/src/fileDrop.ts packages/retroplug/src/systemsStore.ts packages/retroplug/src/render/render.ts \
  packages/retroplug/src/kitFormats.ts packages/retroplug/src/embeds.ts packages/retroplug/src/configSections.ts \
  packages/retroplug/src/userConfig.ts packages/retroplug/src/userConfigSerialization.ts \
  packages/retroplug/src/userConfigStore.ts packages/retroplug/src/projectConfig.ts packages/retroplug/src/projectStore.ts \
  packages/retroplug/test/packages/run-generator.mjs packages/retroplug/test/packages/fixtures/expected/packages.ts \
  packages/retroplug/test/contributions/app-live.test.ts packages/retroplug/test/system-settings/registry.test.ts \
  packages/retroplug/test/kit/kit-formats.test.ts packages/retroplug/test/source-literals/no-cart-literals.test.ts \
  packages/retroplug/test/config/config-sections.test.ts packages/retroplug/test/project/sections.test.ts
git commit -m "contributions: the app's registries are live, with the generated checks moved beside them"
```

### Task 4: Sections and services follow a commit

A project store and a config store live as long as their store graph and have no dispose, so neither subscribes. Each
adopts what was committed since it last looked, at the top of every read that consults its sections or services, and
each gains `adoptContributions()` for the caller that committed to publish at once. A store built over a test's fixed
contributions adopts nothing, as it reads nothing from the registries today.

**Files:**
- Modify: `packages/retroplug/src/projectConfig.ts`, `packages/retroplug/src/projectStore.ts`,
  `packages/retroplug/src/configSections.ts`, `packages/retroplug/src/userConfigStore.ts`
- Test: `packages/retroplug/test/contributions/sections-live.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/sections-live.test.ts`:

```ts
// Sections and services follow a commit (docs/downloadable-packs-m2a-plan.md, Task 4): a project section, a project
// service and a config section a pack contributes after the stores were built are parsed from the block the project or
// the file kept verbatim (or defaulted), the service is built and polled, and the store publishes when asked to adopt.
import { test, expect } from "@retroplug/testing";
import type { ProjectSection } from "@retroplug/core/definePackage";
import type { ProjectServiceFactory } from "@retroplug/tracker/projectService";
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores } from "../../src/appStores";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import { defaultSettings, K_PROJECT, projectSection } from "../../src/projectConfig";
import { UserConfigStore } from "../../src/userConfigStore";
import { USER_CONFIG_SCHEMA } from "../../src/userConfigSerialization";
import { DEFAULT_USER_CONFIG } from "../../src/userConfig";
import { stringifyConfig } from "../../src/configSchema";

const CONFIG = "/config/config.json";

/** A section whose parse marks what it read, so a test can tell a parsed block from the raw one the store kept. */
function markingSection(key: string): ProjectSection<{ value: number; parsed: true }> {
  return {
    key,
    schema: {
      parse: (raw: unknown) => ({ value: Number((raw as { value?: unknown } | undefined)?.value ?? 0), parsed: true }),
    },
    defaults: () => ({ value: 7, parsed: true }),
  };
}

test("a project section committed after a load parses the block the project kept verbatim", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  const raw = JSON.stringify({ schemaVersion: K_PROJECT, settings: { layout: "column", packfixtureA: { value: 3 } },
    systems: [] });
  be.seed("/p/one.rplg", raw);
  stores.project.load("/p/one.rplg");
  // No contributor yet: the block is kept as the file carried it.
  expect(stores.project.section("packfixtureA")).toEqual({ value: 3 });
  commitContributions(appAdditions("packfixture", { projectSections: [markingSection("packfixtureA")] }));
  expect(stores.project.section("packfixtureA")).toEqual({ value: 3, parsed: true });
  // And it saves back through its contributor.
  expect(projectSection(stores.project.settings(), "packfixtureA")).toEqual({ value: 3, parsed: true });
});

test("a project section the project never carried reads its defaults, and a new project starts with them", () => {
  const stores = composeAppStores({ backend: new MockBackend("/cfg") });
  commitContributions(appAdditions("packfixture-b", { projectSections: [markingSection("packfixtureB")] }));
  expect(stores.project.section("packfixtureB")).toEqual({ value: 7, parsed: true });
  stores.project.newProject();
  expect(stores.project.section("packfixtureB")).toEqual({ value: 7, parsed: true });
  expect(projectSection(defaultSettings(), "packfixtureB")).toEqual({ value: 7, parsed: true });
});

test("a block a section parsed is not parsed again by a later commit", () => {
  let parses = 0;
  const counting: ProjectSection<{ n: number }> = {
    key: "packfixtureC",
    schema: { parse: (raw: unknown) => (parses++, { n: Number((raw as { n?: unknown } | undefined)?.n ?? 0) }) },
    defaults: () => ({ n: 0 }),
  };
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  be.seed("/p/three.rplg", JSON.stringify({ schemaVersion: K_PROJECT, settings: { packfixtureC: { n: 2 } }, systems: [] }));
  stores.project.load("/p/three.rplg");
  commitContributions(appAdditions("packfixture-c", { projectSections: [counting] }));
  expect(stores.project.section("packfixtureC")).toEqual({ n: 2 });
  expect(parses).toBe(1);
  commitContributions(appAdditions("packfixture-c2", { projectSections: [markingSection("packfixtureC2")] }));
  expect(stores.project.section("packfixtureC")).toEqual({ n: 2 });
  expect(parses).toBe(1);
});

test("a project service committed later is built once, polled, and its model published on adopt", () => {
  const be = new MockBackend("/cfg");
  const stores = composeAppStores({ backend: be });
  let created = 0;
  let polls = 0;
  const factory: ProjectServiceFactory = {
    key: "packfixtureD",
    create: () => {
      created++;
      return {
        poll: () => (polls++, false),
        state: () => ({ alive: true }),
      };
    },
  } as unknown as ProjectServiceFactory;
  commitContributions(appAdditions("packfixture-d", {
    projectSections: [markingSection("packfixtureD")],
    projectServices: [factory],
  }));
  expect(stores.project.adoptContributions()).toBe(true);
  expect(stores.project.adoptContributions()).toBe(false);
  expect(created).toBe(1);
  stores.project.pollServices();
  expect(polls).toBe(1);
  expect(stores.project.serviceState("packfixtureD")).toEqual({ alive: true });
});

test("a config section committed later parses the block the file carried, and a missing one reads its defaults", () => {
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG,
    packfixtureE: { value: 5 } }));
  const store = new UserConfigStore(be, () => {});
  store.load();
  expect(store.section("packfixtureE")).toEqual({ value: 5 });
  commitContributions(appAdditions("packfixture-e", {
    configSections: [markingSection("packfixtureE"), markingSection("packfixtureF")],
  }));
  expect(store.section("packfixtureE")).toEqual({ value: 5, parsed: true });
  expect(store.section("packfixtureF")).toEqual({ value: 7, parsed: true });
  // The file is not stamped with a block nobody set: a core write leaves packfixtureF out of it.
  expect(store.setDefaultZoom(2)).toBeTruthy();
  const written = JSON.parse(be.readText(CONFIG)!) as Record<string, unknown>;
  expect("packfixtureF" in written).toBe(false);
  expect(written.packfixtureE).toEqual({ value: 5 });
});

test("a store built over fixed contributions adopts nothing", () => {
  const be = new MockBackend("/config");
  be.seed(CONFIG, stringifyConfig({ schemaVersion: USER_CONFIG_SCHEMA, ...DEFAULT_USER_CONFIG, packfixtureG: { value: 1 } }));
  const store = new UserConfigStore(be, () => {}, []);
  store.load();
  commitContributions(appAdditions("packfixture-g", { configSections: [markingSection("packfixtureG")] }));
  expect(store.section("packfixtureG")).toEqual({ value: 1 });
});
```

(`MockBackend.seed` and `readText` are the helpers `test/config/config-sections.test.ts` uses;
`stores.project.load(path)` is the store's thin `.rplg` load, which reads the seeded file through the backend and, with
no system to relink, commits at once.)

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test contributions/sections-live`
Expected: a TS build error naming `defaultSettings` (not exported by `projectConfig.ts`) or `adoptContributions`; if
the build passes on an older tree, the first case fails with `expected {"value":3,"parsed":true}, got {"value":3}`.

- [ ] **Step 3: projectConfig.ts**

Replace the `DEFAULT_SETTINGS` constant (`export const DEFAULT_SETTINGS: ProjectSettings = {...}`) with:

```ts
/** The settings a new project starts with: the shell's defaults and every section registered now, its defaults. */
export function defaultSettings(): ProjectSettings {
  return {
    ...(projectSettingsSchema.parse({}) as Omit<ProjectSettings, "sections">),
    sections: defaultSections(),
  };
}

/** The built-in product's default settings, read once at load: what tests compare a project with. A store starts a
 *  project from defaultSettings(), which reads the sections registered at that moment. */
export const DEFAULT_SETTINGS: ProjectSettings = defaultSettings();
```

After `parseSections` add:

```ts
/** A section registered after a project's settings were read (a pack's, docs/downloadable-packs-m2a-plan.md): its block
 *  parsed through the new contributor from what the project kept verbatim, or the contributor's defaults when the
 *  project never carried one, as parseConfig fills a contributor's absent key. Mutates `sections`. */
export function adoptProjectSection(sections: Record<string, unknown>, entry: SectionEntry): void {
  const key = entry.section.key;
  const value = Object.prototype.hasOwnProperty.call(sections, key)
    ? parseSectionValue(key, sections[key], [entry])
    : sectionDefaults(entry);
  putSection(sections, key, value);
}
```

- [ ] **Step 4: projectStore.ts**

Imports: add `adoptProjectSection` and `defaultSettings` to the `./projectConfig` import (and drop `DEFAULT_SETTINGS` from
it once the three uses below are gone).

Fields: replace

```ts
  private readonly services: readonly { pkg: string; key: string; service: ProjectService }[];
  /** The product's sections, or a test's: what parseConfig and the readers below consult. */
  private readonly sections: readonly SectionEntry[];
```

with

```ts
  private readonly services: { pkg: string; key: string; service: ProjectService }[] = [];
  /** A test's fixed sections and services; undefined is the registries', adopted as they grow. */
  private readonly fixedSections?: readonly SectionEntry[];
  private readonly fixedServices?: readonly { pkg: string; service: ProjectServiceFactory }[];
  /** How many registered sections the current settings were read with, and how many service factories are built: the
   *  registries only grow, so the rest are what a commit added since (adoptPending). */
  private sectionsTaken = 0;
  private servicesTaken = 0;
  /** How many registered sections each pending parse was read with, so a load adopts what arrived between its parse
   *  and its commit. */
  private readonly parsedWith = new WeakMap<ProjectConfig, number>();
```

and `private projectSettings: ProjectSettings = { ...DEFAULT_SETTINGS };` with
`private projectSettings: ProjectSettings = defaultSettings();`. Add a getter beside the fields:

```ts
  /** The sections parseConfig and the readers consult: a test's, or the registry's as it stands. */
  private get sections(): readonly SectionEntry[] {
    return this.fixedSections ?? projectSectionEntries();
  }
```

Constructor: replace `this.sections = contributions.sections ?? projectSectionEntries();` with

```ts
    this.fixedSections = contributions.sections;
    this.fixedServices = contributions.services;
    this.sectionsTaken = this.sections.length;
```

and the services block (`this.services = (contributions.services ?? projectServiceEntries()).map(...)`) with

```ts
    // Built once per factory, here and as later commits add factories: a service's poll and model read the systems and
    // the backend, never a file, so nothing about them is per-project the way the settings are. `newProject` leaves them
    // standing, as it leaves their state.
    for (const { pkg, service: factory } of this.fixedServices ?? projectServiceEntries()) {
      this.services.push({ pkg, key: factory.key, service: factory.create() });
    }
    this.servicesTaken = this.services.length;
```

Add the two methods after the constructor:

```ts
  /** Adopt the sections and services committed since this store last looked (docs/downloadable-packs-m2a-plan.md): a
   *  section's block parsed from what the project kept verbatim, or defaulted; a service built. True when anything was
   *  adopted. A store over a test's fixed contributions adopts nothing. */
  private adoptPending(): boolean {
    let adopted = false;
    if (!this.fixedSections) {
      const entries = projectSectionEntries();
      if (this.sectionsTaken < entries.length) {
        const sections = { ...this.projectSettings.sections };
        for (; this.sectionsTaken < entries.length; this.sectionsTaken++) {
          adoptProjectSection(sections, entries[this.sectionsTaken]);
        }
        this.projectSettings = { ...this.projectSettings, sections };
        adopted = true;
      }
    }
    if (!this.fixedServices) {
      const factories = projectServiceEntries();
      for (; this.servicesTaken < factories.length; this.servicesTaken++) {
        const { pkg, service: factory } = factories[this.servicesTaken];
        this.services.push({ pkg, key: factory.key, service: factory.create() });
        adopted = true;
      }
    }
    return adopted;
  }

  /** Adopt what was committed since this store last looked, then publish the graph and tell the observers: a new
   *  service's model is placed and a new section is seen at once. What a pack's loader calls after its commit (M2b); a
   *  read adopts too, without the publish. True when anything was adopted. */
  adoptContributions(): boolean {
    if (!this.adoptPending()) return false;
    this.publishGraph(false);
    this.notifySystemsChange();
    return true;
  }

  /** Parse a project's JSON with the sections registered now, remembering how many, so a section registered between
   *  this parse and the load's commit is adopted from the block the parse kept verbatim. */
  private parse(json: string): ProjectConfig {
    const sections = this.sections;
    const cfg = parseConfig(json, sections);
    this.parsedWith.set(cfg, sections.length);
    return cfg;
  }
```

Call `this.adoptPending();` as the first statement of `graphModels()`, `pollServices()`, `serviceState()`, `section()`,
`setSection()`, `settings()`, `save()` and `buildArchive()`.

Replace the three `parseConfig(<text>, this.sections)` calls in `loadBytes`, the thin load and `loadZip` with
`this.parse(<text>)`. In `newProject`, replace
`this.projectSettings = { ...DEFAULT_SETTINGS, sections: defaultSections(this.sections) };` with

```ts
    this.projectSettings = { ...defaultSettings(), sections: defaultSections(this.sections) };
    this.sectionsTaken = this.sections.length;
```

and where a load commits its settings (`this.projectSettings = { ...DEFAULT_SETTINGS, ...cfg.settings };`):

```ts
    this.projectSettings = { ...defaultSettings(), ...cfg.settings };
    this.sectionsTaken = this.parsedWith.get(cfg) ?? this.sections.length;
```

(`ProjectConfig` is already imported from `./projectConfig` where `beginLoad` is typed; add it to the import if not.)

- [ ] **Step 5: configSections.ts and userConfigStore.ts**

In `src/configSections.ts`, after `parseConfigSections` add:

```ts
/** A config section registered after the file was read (a pack's, docs/downloadable-packs-m2a-plan.md): the block the
 *  file carried, kept verbatim until now, parsed through its contributor. A key the file never carried stays absent, as
 *  parseConfigSections leaves it: a reader gets the defaults and the file is not stamped with a block nobody set. */
export function adoptConfigSection(sections: Record<string, unknown>, entry: ConfigSectionEntry): void {
  const key = entry.section.key;
  if (!Object.prototype.hasOwnProperty.call(sections, key)) return;
  let parsed: unknown;
  try {
    parsed = parseConfigSection(entry.section, sections[key]);
  } catch (e) {
    warnSectionOnce(entry, "parsing its saved block", e);
    parsed = configSectionDefaults(entry);
  }
  putConfigSection(sections, key, parsed);
}
```

In `src/userConfigStore.ts`:

- import `adoptConfigSection` beside the other `./configSections` names and `configSectionEntries` from
  `./contributions/app`;
- replace `private readonly configSections: readonly ConfigSectionEntry[];` with

```ts
  /** A test's fixed config sections; undefined is the registry's, adopted as it grows. */
  private readonly fixedSections?: readonly ConfigSectionEntry[];
  /** How many registered config sections `sections` was read with: the rest are what a commit added since. */
  private sectionsTaken = 0;

  /** The config sections this store parses, reads and writes: a test's, or the registry's as it stands. */
  private get configSections(): readonly ConfigSectionEntry[] {
    return this.fixedSections ?? configSectionEntries();
  }
```

- the constructor's last parameter becomes `configSections?: readonly ConfigSectionEntry[],` (no default: undefined is
  the live registry) and its body:

```ts
    this.fixedSections = configSections;
    this.sections = defaultConfigSections(this.configSections);
    this.sectionsTaken = this.configSections.length;
    this.raw = {};
```

- add after the constructor:

```ts
  /** Adopt the config sections committed since this store last looked (docs/downloadable-packs-m2a-plan.md): the block
   *  the file carried is parsed through its new contributor; one it never carried stays absent. True when anything was
   *  adopted. A store over a test's fixed sections adopts nothing. */
  private adoptPending(): boolean {
    if (this.fixedSections) return false;
    const entries = configSectionEntries();
    if (this.sectionsTaken >= entries.length) return false;
    const sections = { ...this.sections };
    for (; this.sectionsTaken < entries.length; this.sectionsTaken++) {
      adoptConfigSection(sections, entries[this.sectionsTaken]);
    }
    this.sections = sections;
    return true;
  }

  /** Adopt what was committed since this store last looked, and tell the observer: what a pack's loader calls after its
   *  commit (M2b); a read adopts too, silently. True when anything was adopted. */
  adoptContributions(): boolean {
    if (!this.adoptPending()) return false;
    this.onChange();
    return true;
  }
```

- call `this.adoptPending();` first in `config()`, `section()` and `model()`;
- in `adopt(model)` (where `this.sections = { ...model.sections };`), add `this.sectionsTaken = this.configSections.length;`
  after it: a model parsed now was parsed with every section registered now (`load`, `reload` and `syncFromDisk` parse
  with `this.configSections` immediately before adopting).

- [ ] **Step 6: Run the tests**

Run: `pnpm test contributions/sections-live`
Expected: `1..6`, six `ok`, exit 0.

Run: `pnpm test project` then `pnpm test config` then `pnpm test system-settings`
Expected: every case `ok`, exit 0 (the existing section, migration and round-trip cases are unchanged by a store that
adopts nothing until a commit).

Run: `pnpm typecheck`
Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add packages/retroplug/src/projectConfig.ts packages/retroplug/src/projectStore.ts packages/retroplug/src/configSections.ts \
  packages/retroplug/src/userConfigStore.ts packages/retroplug/test/contributions/sections-live.test.ts
git commit -m "contributions: project and config stores adopt sections and services committed after they were built"
```

### Task 5: The UI registries

The menu contributions (rows, menu builders, tracker UIs, services, actions) and the views. The two facades stay apart
for the reason the generated modules are apart: a component view imports React, which the pure tier (the menu tests,
the control plane) has no path to, so a module reading the menus must not reach the views' seed. The registry module
holds both registries and their checks, and imports neither seed, so it carries no React either.

**Files:**
- Create: `packages/retroplug/ui/contributions/uiRegistries.ts`, `packages/retroplug/ui/contributions/menus.ts`,
  `packages/retroplug/ui/contributions/views.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderMenus`, `renderViews`, their calls),
  `packages/retroplug/ui/host/menuContributions.ts`, `packages/retroplug/ui/host/viewContributions.ts`,
  `packages/retroplug/test/packages/run-generator.mjs`
- Regenerate: `packages/retroplug/ui/generated/{menus,views}.ts`,
  `packages/retroplug/test/packages/fixtures/expected/{menus,views}.ts`
- Test: `packages/retroplug/test/contributions/ui-live.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/ui-live.test.ts`:

```ts
// The menu contributions follow a commit (docs/downloadable-packs-m2a-plan.md, Task 5): a UI contribution committed
// after the built-ins adds its rows, its menu builders, its actions and its services to what the composers read, and a
// commit the menus' checks refuse changes nothing. The views are the UI suite's (test-ui/contributions-live.test.ts):
// a views module reaches React, which this tier has no path to.
import { test, expect } from "@retroplug/testing";
import type { UiContribution } from "@retroplug/ui/contribution";
import type { RoleType } from "@retroplug/core/systemRoles";
import { commitContributions } from "../../src/contributions/registry";
import { appAdditions } from "../../src/contributions/app";
import { uiRegistry } from "../../ui/contributions/menus";
import {
  contributedActionIds,
  contributedActions,
  menuPlacement,
  roleRows,
  uiServices,
} from "../../ui/host/menuContributions";

const fixtureRole = {
  kind: "packfixture-ui-role",
  category: "feature",
  label: "Pack Fixture UI",
  schema: { parse: () => ({ level: 1, on: false }) },
} as unknown as RoleType;

const fixtureUi = {
  rows: [{ id: "packfixture-level", role: "packfixture-ui-role", key: "level", label: "Level", kind: "cycle",
    choices: [{ value: 1, label: "One" }] }],
  // A placement no built-in package builds at, so the composer runs this builder alone over the stand-in host.
  menus: [{ placement: "instance.tracker", build: () => [{ id: "packfixture-hello", label: "Hello", kind: "action" }] }],
  actions: [{ id: "packfixture.toggle", label: "Toggle the fixture", run: () => {} }],
  services: [{ id: "packfixture-service" }],
} as unknown as UiContribution;

test("the built-in UI contributions are seeded", () => {
  expect(uiRegistry.seeded).toBe(true);
  expect(uiRegistry.items().length).toBeGreaterThan(0);
});

test("a UI contribution committed later is read by every composer", () => {
  const actionIdsBefore = contributedActionIds();
  commitContributions([
    ...appAdditions("packfixture", { roles: [fixtureRole] }),
    { registry: uiRegistry, pkg: "packfixture", items: [fixtureUi] },
  ]);
  expect(roleRows().some((row) => row.id === "packfixture-level")).toBe(true);
  const built = menuPlacement("instance.tracker", {} as never, null, {} as never);
  expect(built.some((item) => item.id === "packfixture-hello")).toBe(true);
  expect(contributedActions().some((entry) => entry.pkg === "packfixture" && entry.action.id === "packfixture.toggle"))
    .toBe(true);
  // A new list, so a memo keyed on it (App's key maps) rebuilds.
  expect(contributedActionIds() === actionIdsBefore).toBe(false);
  expect(contributedActionIds().includes("packfixture.toggle")).toBe(true);
  expect(uiServices().some((service) => service.id === "packfixture-service")).toBe(true);
});

test("the lists keep their identity between commits", () => {
  expect(contributedActionIds()).toBe(contributedActionIds());
  expect(uiServices()).toBe(uiServices());
});

test("a row over a role no package registers is refused, and nothing moves", () => {
  const count = uiRegistry.items().length;
  const stray = { rows: [{ id: "stray", role: "no-such-role", key: "level", label: "Stray", kind: "cycle",
    choices: [] }] } as unknown as UiContribution;
  expect(() => commitContributions([{ registry: uiRegistry, pkg: "stray", items: [stray] }])).toThrow(
    "the package stray declares the row stray over the role no-such-role, which no registered role type has",
  );
  expect(uiRegistry.items().length).toBe(count);
});

test("an action not prefixed by its package's name is refused", () => {
  const bad = { actions: [{ id: "other.thing", label: "Thing", run: () => {} }] } as unknown as UiContribution;
  expect(() => commitContributions([{ registry: uiRegistry, pkg: "packfixture2", items: [bad] }])).toThrow(
    'the package packfixture2 declares the action other.thing, which is not prefixed by its own name ("packfixture2.")',
  );
});
```

(The row's fields beyond `id`, `role`, `key` and `when` are not checked; the ones given are a plausible `RoleRow`.
`instance.tracker` is a placement no built-in package builds at, so `menuPlacement` calls only this builder and the
stand-in host and context are never read.)

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test contributions/ui-live`
Expected: `BUILD FAILED: contributions/ui-live` (`Could not resolve "../../ui/contributions/menus"`).

- [ ] **Step 3: The UI registries and facades**

Create `packages/retroplug/ui/contributions/uiRegistries.ts`:

```ts
// The UI's live registries (docs/downloadable-packs-m2a-plan.md): each package's `retroplug.ui` contribution (its
// defineUi: system-settings rows, menu builders, tracker UIs, services, app actions) and each view of its
// `retroplug.views` module. ui/generated/menus.ts and ui/generated/views.ts seed them with the product's packages, a pack
// appends, and the checks below (the ones the generator used to emit into those two modules, word for word) run at every
// commit. This module imports neither seed and no React: ui/contributions/menus.ts and views.ts are the facades the
// shell reads through, kept apart so a menu module never reaches a component view.
import { PLACEMENTS, type RoleRow, type UiAction, type UiContribution, type ViewDef } from "@retroplug/ui/contribution";
import { Registry } from "../../src/contributions/registry";
import { rolesRegistry, trackerIntegrationsRegistry } from "../../src/contributions/appRegistries";
import { GAMEPAD_BUTTON_NAMES, resolveKeyName } from "../../src/keyCodes";
import { DEFAULT_GAMEPAD_ACTIONS, DEFAULT_KEYBOARD_ACTIONS, defaultBindingMap } from "../../src/bindingMap";

/** The binding vocabulary an action's default is checked against: the key codes and pad names the shell and the Game Boy
 *  buttons already take at a default (a key on both would fire the action AND the button it is on). */
function takenBindings(): { keyCodes: Set<number>; padNames: Set<string> } {
  const keyCodes = new Set<number>();
  const padNames = new Set<string>();
  const defaultMap = defaultBindingMap();
  for (const names of Object.values(DEFAULT_KEYBOARD_ACTIONS)) for (const name of names) keyCodes.add(resolveKeyName(name)!);
  for (const names of Object.values(defaultMap.keyboard)) for (const name of names) keyCodes.add(resolveKeyName(name)!);
  for (const names of Object.values(DEFAULT_GAMEPAD_ACTIONS)) for (const name of names) padNames.add(name);
  for (const names of Object.values(defaultMap.gamepad)) for (const name of names) padNames.add(name);
  return { keyCodes, padNames };
}

/** Each package's UI contribution, one item per package. A row's id is unique inside its placement (the menu is keyed by
 *  id); a row's role must be registered and its key (and its gate's) one its role's schema reads; a tracker UI names a
 *  registered integration, once; a service id and an action id are unique; an action's id is prefixed by its package's
 *  name and its defaults are key and pad names the shell resolves and does not already bind; a menu builder's placement
 *  is one the shell has. */
export const uiRegistry = new Registry<UiContribution>("ui", (entries, view) => {
  const roles = view(rolesRegistry).map((entry) => entry.item);
  const roleKinds = new Set(roles.map((role) => role.kind));
  const roleKeys = new Map(roles.map((role) => [role.kind, new Set(Object.keys(role.schema.parse({})))]));
  const trackerIds = new Set(view(trackerIntegrationsRegistry).map((entry) => entry.item.id));
  const placements = new Set<string>(PLACEMENTS);
  const padButtonNames = new Set<string>(GAMEPAD_BUTTON_NAMES);
  const { keyCodes: takenKeyCodes, padNames: takenPadNames } = takenBindings();
  const rowOwners = new Map<string, string>();
  const trackerOwners = new Map<string, string>();
  const serviceOwners = new Map<string, string>();
  const actionOwners = new Map<string, string>();

  const claimRow = (placement: string, id: string, pkg: string): void => {
    const owner = rowOwners.get(`${placement}:${id}`);
    if (owner === undefined) {
      rowOwners.set(`${placement}:${id}`, pkg);
      return;
    }
    throw new Error(owner === pkg
      ? `the package ${pkg} declares the row ${id} twice in the placement ${placement}`
      : `two packages declare the row ${id} in the placement ${placement}: ${owner} and ${pkg}`);
  };

  const checkRow = (placement: string, pkg: string, row: RoleRow): void => {
    if (!roleKinds.has(row.role)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} over the role ${row.role}, which no ` +
        "registered role type has");
    }
    claimRow(placement, row.id, pkg);
    const keys = roleKeys.get(row.role)!;
    if (!keys.has(row.key)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} over the key ${row.key}, which the role ` +
        `${row.role} does not read`);
    }
    if (row.when && !keys.has(row.when.key)) {
      throw new Error(`the package ${pkg} declares the row ${row.id} gated on the key ${row.when.key}, which ` +
        `the role ${row.role} does not read`);
    }
  };

  const checkAction = (pkg: string, action: UiAction): void => {
    const owner = actionOwners.get(action.id);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the action ${action.id} twice`
        : `two packages declare the action ${action.id}: ${owner} and ${pkg}`);
    }
    actionOwners.set(action.id, pkg);
    if (!action.id.startsWith(`${pkg}.`)) {
      throw new Error(`the package ${pkg} declares the action ${action.id}, which is not prefixed ` +
        `by its own name ("${pkg}.")`);
    }
    for (const name of action.keys ?? []) {
      const code = resolveKeyName(name);
      if (code === null) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
          `${name}, which is no key name`);
      }
      if (takenKeyCodes.has(code)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ` +
          `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
      }
      if (/^[A-Z]$/.test(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the key ${name} by ` +
          `default, which no host sends (the hosts send letters unshifted: bind "${name.toLowerCase()}")`);
      }
    }
    for (const name of action.pad ?? []) {
      if (!padButtonNames.has(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
          `${name}, which is no SDL button name (the names are GAMEPAD_BUTTON_NAMES)`);
      }
      if (takenPadNames.has(name)) {
        throw new Error(`the package ${pkg} declares the action ${action.id} on the gamepad button ` +
          `${name} by default, which the shell already binds (a shell action or a Game Boy button)`);
      }
    }
  };

  for (const { pkg, item: ui } of entries) {
    for (const row of ui.rows ?? []) checkRow("system.settings", pkg, row);
    for (const action of ui.actions ?? []) checkAction(pkg, action);
    for (const tracker of ui.trackers ?? []) {
      if (!trackerIds.has(tracker.id)) {
        throw new Error(`the package ${pkg} declares a tracker UI for ${tracker.id}, which no registered ` +
          "tracker integration has");
      }
      const owner = trackerOwners.get(tracker.id);
      if (owner !== undefined) {
        throw new Error(owner === pkg
          ? `the package ${pkg} declares the tracker UI ${tracker.id} twice`
          : `two packages declare a tracker UI for ${tracker.id}: ${owner} and ${pkg}`);
      }
      trackerOwners.set(tracker.id, pkg);
      for (const row of tracker.extras?.rows ?? []) checkRow(`instance.tracker:${tracker.id}`, pkg, row);
    }
    for (const service of ui.services ?? []) {
      const owner = serviceOwners.get(service.id);
      if (owner !== undefined) {
        throw new Error(owner === pkg
          ? `the package ${pkg} declares the UI service ${service.id} twice`
          : `two packages declare a UI service ${service.id}: ${owner} and ${pkg}`);
      }
      serviceOwners.set(service.id, pkg);
    }
    for (const menu of ui.menus ?? []) {
      if (!placements.has(menu.placement)) {
        throw new Error(`the package ${pkg} declares a menu builder at the placement ${menu.placement}, ` +
          `which is no placement (the placements are ${PLACEMENTS.join(", ")})`);
      }
    }
  }
});

/** Every view, one item per view. A view's id is its test id and the id host.views.open names, so it is unique across
 *  the product; its placement and its body's kind are the two data fields the type cannot hold a package to at load. */
export const viewsRegistry = new Registry<ViewDef>("views", (entries) => {
  const viewOwners = new Map<string, string>();
  const viewPlacements = new Set<string>(["window", "tile"]);
  const bodyKinds = new Set<string>(["pixels", "readout", "component"]);
  for (const { pkg, item: view } of entries) {
    const owner = viewOwners.get(view.id);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the view ${view.id} twice`
        : `two packages declare the view ${view.id}: ${owner} and ${pkg}`);
    }
    viewOwners.set(view.id, pkg);
    if (!viewPlacements.has(view.placement)) {
      throw new Error(`the package ${pkg} declares the view ${view.id} at the placement ${view.placement}, ` +
        `which is no placement (the placements are window, tile)`);
    }
    const kind = view.body?.kind;
    if (!bodyKinds.has(kind)) {
      throw new Error(`the package ${pkg} declares the view ${view.id} with the body kind ${kind}, ` +
        `which is no body kind (the kinds are pixels, readout, component)`);
    }
    const body = view.body as { readonly open?: unknown; readonly component?: unknown };
    if (kind === "component") {
      if (typeof body.component !== "function") {
        throw new Error(`the package ${pkg} declares the view ${view.id} with a component body and no ` +
          "component (reactView builds the one a component body carries)");
      }
    } else if (typeof body.open !== "function") {
      throw new Error(`the package ${pkg} declares the view ${view.id} with a ${kind} body and no open ` +
        `(a ${kind} body's open is what the shell calls)`);
    }
    if (view.placement !== "tile" && (view as { readonly visible?: unknown }).visible !== undefined) {
      throw new Error(`the package ${pkg} declares the window view ${view.id} with a visible flag, which only ` +
        `a tile view has`);
    }
  }
});
```

Create `packages/retroplug/ui/contributions/menus.ts`:

```ts
// The menus facade: importing this seeds the UI contributions (the generated module seeds the app's registries first,
// since the menus' checks read the roles and the tracker integrations), and re-exports the registry. It never reaches
// the views' seed: a component view imports React.
import "../generated/menus";

export { uiRegistry } from "./uiRegistries";
```

Create `packages/retroplug/ui/contributions/views.ts`:

```ts
// The views facade: importing this seeds the views (every package's `retroplug.views` module, which a component view's
// React reaches), and re-exports the registry. Only the UI bundle imports it.
import "../generated/views";

export { viewsRegistry } from "./uiRegistries";
```

- [ ] **Step 4: The generator seeds them**

In `packages/retroplug/scripts/lib/packages.mjs`, replace `renderMenus` with:

```js
function renderMenus({ packages, outDir, generatedDir, shellDir, product }) {
  const contributors = packages.filter((p) => p.ui);
  // A module identifier per package: the package name loses its punctuation, and a collision would be a tsc error
  // naming neither manifest.
  const local = (name) => `ui_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's UI contributions, in product order: each package's `retroplug.ui` module, which default-exports the",
    "// system-settings rows, menu builders, tracker specs and services it adds, seeded into the live registry",
    "// ui/contributions/uiRegistries.ts (which holds the checks). The shell composes the menus from it, so a package's",
    `// place in \`retroplug.products.${product}\` is where its rows and entries land. Only ui/ imports this module: the`,
    "// control plane, the render worker, the SDK and the planner carry no menu contribution. It seeds the app's",
    "// registries first, through their generated module, because the checks read the roles and the trackers.",
    `import "${specifier(outDir, join(generatedDir, "packages.ts"))}";`,
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    `import { uiRegistry } from "${contributionsSpecifier(outDir, shellDir, "ui/contributions/uiRegistries.ts")}";`,
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.ui)}";`);
  }
  lines.push(
    "",
    "export { uiRegistry };",
    "",
    ...seedLines(["uiRegistry"], contributors.map((p) => ({
      registry: "uiRegistry", pkg: p.pkg.name, items: `[${local(p.pkg.name)}]`,
    }))),
    "",
  );
  return lines.join("\n");
}
```

and `renderViews` with:

```js
function renderViews({ packages, outDir, shellDir, product }) {
  const contributors = packages.filter((p) => p.views);
  const local = (name) => `views_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's views, in product order: each package's `retroplug.views` module, default-exporting the window",
    "// and tile views it draws, seeded into the live registry ui/contributions/uiRegistries.ts (which holds the checks).",
    "// Only the shell's view slots import this module, and only the UI bundle resolves it: a component view imports",
    "// React, which the pure tier (the menu tests, the control plane, the CLI) has no path to.",
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    `import { viewsRegistry } from "${contributionsSpecifier(outDir, shellDir, "ui/contributions/uiRegistries.ts")}";`,
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.views)}";`);
  }
  lines.push(
    "",
    "export { viewsRegistry };",
    "",
    ...seedLines(["viewsRegistry"], contributors.map((p) => ({
      registry: "viewsRegistry", pkg: p.pkg.name, items: `${local(p.pkg.name)}.views ?? []`,
    }))),
    "",
  );
  return lines.join("\n");
}
```

In `generate`, pass `shellDir: shell.dir` to both calls (`renderMenus` keeps `generatedDir: outDir`; it no longer takes
`shellSrc`, whose imports moved into `uiRegistries.ts`).

The views module no longer seeds the app (its check reads no app registry). The menus module imports the generated
`packages.ts` for its seed; in a product tree that is the product's own `packages.ts`, by the relative path from the
product's `ui/generated` to its `src/generated`, as the old `ROLES` import was.

- [ ] **Step 5: The consumers**

`packages/retroplug/ui/host/menuContributions.ts`: replace `import { ACTIONS, UI_CONTRIBUTIONS } from "../generated/menus";`
with

```ts
import { uiRegistry } from "../contributions/menus";
import { derived } from "../../src/contributions/registry";
```

then:

- `roleRows`: `return uiRegistry.items().flatMap((ui) => ui.rows ?? []);`
- `contributedTrackerUi`: loop `for (const ui of uiRegistry.items())` over `ui.trackers ?? []`.
- `menuPlacement` and `menuPlacementOpen`: `uiRegistry.items().flatMap((ui) => (ui.menus ?? []).filter(...)...)`.
- the action and service block becomes:

```ts
/** Every app action a package contributes, in registration order, with the package it came from: the bindings menus'
 *  rows, the key and gamepad maps' "known" ids, and the dispatch's lookups. The same array until the next commit, so a
 *  hook's dependency (and a memo over it) changes only when a pack adds actions. */
const ACTIONS = derived((): readonly ContributedAction[] =>
  uiRegistry.entries().flatMap(({ pkg, item }) => (item.actions ?? []).map((action) => ({ pkg, action }))),
);

const ACTION_IDS = derived((): readonly string[] => ACTIONS().map((entry) => entry.action.id));

export function contributedActions(): readonly ContributedAction[] {
  return ACTIONS();
}

/** Their ids alone, for the maps' "known" set and the bindings rows. */
export function contributedActionIds(): readonly string[] {
  return ACTION_IDS();
}

/** Every service a package contributes, in registration order: the list `useUiServices` runs, subscribes and polls. The
 *  same array until the next commit, so the hook's dependency changes only when a pack adds services. */
const SERVICES = derived((): readonly UiService[] => uiRegistry.items().flatMap((ui) => ui.services ?? []));

export function uiServices(): readonly UiService[] {
  return SERVICES();
}
```

The header comment's "the generated list, in product order, is the one place..." becomes "the live registry
(ui/contributions/uiRegistries.ts, seeded by the generated module in product order and appended to by packs) is the
one place...", and "the generated module has already refused" becomes "the registry's check has already refused".

`packages/retroplug/ui/host/viewContributions.ts`: replace `import { VIEWS } from "../generated/views";` with

```ts
import { viewsRegistry } from "../contributions/views";
import { derived } from "../../src/contributions/registry";

/** Every view with the package it came from, in registration order: the same array until the next commit. */
const VIEWS = derived((): readonly { pkg: string; view: ViewDef }[] =>
  viewsRegistry.entries().map(({ pkg, item }) => ({ pkg, view: item })),
);

const TILE_VIEWS = derived((): readonly { pkg: string; view: TileView }[] =>
  VIEWS().filter((entry): entry is { pkg: string; view: TileView } => entry.view.placement === "tile"),
);
```

and read `VIEWS()` in `viewById`, `return TILE_VIEWS();` in `tileViews`. The header's "the generated views module, in
product order," becomes "the live views registry, seeded by the generated module in product order,".

In `packages/retroplug/test/packages/run-generator.mjs`:

```js
async function loadMenus() {
  return (await importGenerated("menus", "menus.mjs")).uiRegistry.entries().map((entry) => entry.pkg);
}
```

```js
async function loadViews() {
  const loaded = await importGenerated("views", "views.mjs");
  return loaded.viewsRegistry.entries().map(({ pkg, item: view }) =>
    ({ pkg, id: view.id, placement: view.placement, kind: view.body.kind }));
}
```

```js
async function loadActions() {
  const loaded = await importGenerated("menus", "actions.mjs");
  return loaded.uiRegistry.entries().flatMap(({ pkg, item }) => (item.actions ?? []).map((action) => ({
    pkg,
    id: action.id,
    label: action.label,
    keys: action.keys ?? [],
    pad: action.pad ?? [],
  })));
}
```

`menusImports` reads the generated module's source for its `import ui_<x> from "@retroplug/<x>/ui";` lines, which the new
module still has: unchanged.

- [ ] **Step 6: Regenerate and refresh the fixture's two modules**

Run: `node packages/retroplug/scripts/lib/packages.mjs`
Expected: exit 0; `ui/generated/menus.ts` and `views.ts` hold imports, a re-export and one `commitContributions` call.

```bash
node -e '
const { execFileSync } = require("child_process");
const fs = require("fs");
const out = JSON.parse(execFileSync("node", ["packages/retroplug/test/packages/run-generator.mjs", "generate"], { encoding: "utf8" }));
for (const name of ["menus.ts", "views.ts"]) {
  fs.writeFileSync("packages/retroplug/test/packages/fixtures/expected/" + name, out.files[name]);
}'
```

- [ ] **Step 7: Run the tests**

Run: `pnpm test contributions/ui-live`
Expected: `1..5`, five `ok`, exit 0.

Run: `pnpm test packages` then `pnpm test script-bundle` then `pnpm test menu`
Expected: every case `ok` (the `ui-*`, `views-*` and `actions-*` refusal modes read the same messages from the
registries' checks; `script/bundle-views` and `script/bundle-view-package` still find React only in the views bundle),
exit 0.

Run: `pnpm typecheck`
Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add packages/retroplug/ui/contributions/uiRegistries.ts packages/retroplug/ui/contributions/menus.ts \
  packages/retroplug/ui/contributions/views.ts packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/ui/generated/menus.ts packages/retroplug/ui/generated/views.ts \
  packages/retroplug/ui/host/menuContributions.ts packages/retroplug/ui/host/viewContributions.ts \
  packages/retroplug/test/packages/run-generator.mjs packages/retroplug/test/packages/fixtures/expected/menus.ts \
  packages/retroplug/test/packages/fixtures/expected/views.ts packages/retroplug/test/contributions/ui-live.test.ts
git commit -m "contributions: the menu contributions and the views are live registries with their checks beside them"
```

### Task 6: The UI follows a commit

The composers now read live lists, but React renders only when a store channel fires. This task makes the app
re-render on the commit counter, remounts a tile so a new tile view's slot lands in its place, lets the services hook
take services that arrive later, turns the ROM file patterns into a read, and gives the UI bundle a seam a test (and
M2b's UI loader) commits through, since neither holds the UI bundle's registry objects.

**Files:**
- Create: `packages/retroplug/ui/stores/useContributions.ts`, `packages/retroplug/test-ui/contributions-live.test.ts`
- Modify: `packages/retroplug/ui/App.tsx`, `packages/retroplug/ui/screens/grid/SystemGrid.tsx`,
  `packages/retroplug/ui/app/useUiServices.ts`, `packages/retroplug/ui/views/TileViews.tsx` (comment),
  `packages/retroplug/src/fileSelection.ts`, `packages/retroplug/ui/app/projectModals.ts`,
  `packages/retroplug/ui/screens/menu/menuDefs.ts`, `packages/retroplug/ui/host/nativeHooks.ts`,
  `packages/retroplug/ui/main.tsx`, `packages/retroplug/test/menu/n8-sd-ops.test.ts`

- [ ] **Step 1: Write the failing UI test**

Create `packages/retroplug/test-ui/contributions-live.test.ts`:

```ts
// The running UI follows a commit (docs/downloadable-packs-m2a-plan.md, Task 6): a menu builder committed after the UI
// booted shows in the start menu, and a tile view committed with a system on screen gets its slot in every tile. The
// commit goes through the UI bundle's own seam (__rp_commitUiContributions), since the UI bundle's registries are its
// own copy and this test's bundle cannot name them.
import { test, expect, ui, navTo, Key } from "ui-harness";

type NamedAddition = { registry: string; pkg: string; items: unknown[] };
const commit = (additions: NamedAddition[]): void =>
  ((globalThis as Record<string, unknown>).__rp_commitUiContributions as (a: NamedAddition[]) => void)(additions);

test("a menu builder committed after boot shows in the start menu", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(ui.findByText("Pack Fixture Row")).toBe(null);
  commit([{
    registry: "ui",
    pkg: "packfixture",
    items: [{
      menus: [{
        placement: "start",
        build: () => [{ id: "packfixture-row", label: "Pack Fixture Row", kind: "action", onSelect: () => {} }],
      }],
    }],
  }]);
  ui.pump(10);
  expect(ui.findByText("Pack Fixture Row") != null).toBeTruthy();
});

test("a tile view committed with a system on screen gets its slot", () => {
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
  expect(ui.findByTestId("view-slot-packfixture-tile")).toBe(null);
  commit([{
    registry: "views",
    pkg: "packfixture",
    items: [{
      id: "packfixture-tile",
      label: "Pack Fixture Tile",
      placement: "tile",
      when: () => false,
      body: { kind: "readout", open: () => null },
    }],
  }]);
  ui.pump(10);
  expect(ui.findByTestId("view-slot-packfixture-tile") != null).toBeTruthy();
  // The tile is still the system's, drawn again rather than lost.
  expect(ui.findByTestId("tile-0") != null).toBeTruthy();
});
```

- [ ] **Step 2: Build the UI test binary and run it to see it fail**

Run: `pnpm test:ui contributions-live`
Expected: the first case fails with a `TypeError` (`not a function`: the seam is not installed), exit 1.

- [ ] **Step 3: The seam and the hook**

In `packages/retroplug/ui/host/nativeHooks.ts`, in `RpHooks`'s "installed BY the UI" block, add:

```ts
  /** Commit contributions into this UI bundle's own registries, by registry name (ui, views, roles, ...): what a test
   *  and M2b's pack loader reach the UI's copy through (docs/downloadable-packs-m2a-plan.md). Throws a check's refusal. */
  __rp_commitUiContributions?: (additions: readonly NamedAddition[]) => void;
```

with `import type { NamedAddition } from "../../src/contributions/registry";` at the top.

In `packages/retroplug/ui/main.tsx`, beside the other installs (`native.__rp_mountUI = mountUI;`):

```ts
native.__rp_commitUiContributions = commitByName;
```

with `import { commitByName } from "../src/contributions/registry";` and, so every UI registry exists and is seeded in
the bundle before anything commits by name, `import "./contributions/menus";` and `import "./contributions/views";`
(the app's facade is reached through `menus`' seed).

Create `packages/retroplug/ui/stores/useContributions.ts`:

```ts
// The UI's subscription to the live registries (docs/downloadable-packs-m2a-plan.md): the store channels re-render what
// the stores hold, and this re-renders what the registries hold (a pack's menu rows, actions, services and views). The
// counter moves only at a commit, which is rare (a launch, a pack), so a component reading it re-renders then and only
// then.
import { useSyncExternalStore } from "react";
import { contributionsVersion, subscribeContributions } from "../../src/contributions/registry";

/** The commit counter, re-rendering the caller when it moves: a key or a memo dependency over the registries. */
export function useContributionsVersion(): number {
  return useSyncExternalStore(subscribeContributions, contributionsVersion);
}
```

- [ ] **Step 4: The app, the grid and the services re-render on it**

`packages/retroplug/ui/App.tsx`: import `useContributionsVersion` from `./stores/useContributions` and call it near the
top of `App` (beside the other store hooks), so a commit re-renders the app and its menus are rebuilt:

```ts
  // A commit to the registries (a pack's rows, builders, actions, views) re-renders the app: the menus are built from
  // them on every render, and the action maps below are memoised over the registry's list.
  useContributionsVersion();
```

Move `const actionIds = contributedActionIds();` above `actionBindings`, with its comment made
`// the registry's list: the same array until a commit adds actions, so a stable dep`, and add it to `actionBindings`'
dependencies, so a pack's action defaults reach the key and pad maps (which are built from `actionBindings`):

```tsx
  const actionIds = contributedActionIds(); // the registry's list: the same array until a commit adds actions, so a stable dep
  const actionBindings = useMemo(
    () => withActionDefaults(bindings, contributedActions().map((entry) => entry.action)),
    [bindings, actionIds],
  );
```

`packages/retroplug/ui/screens/grid/SystemGrid.tsx`: import `useContributionsVersion` and read it at the top of the
component (`const contributions = useContributionsVersion();`); give the `EmulatorTile` a key that carries it:

```tsx
                    <EmulatorTile
                      key={`tile-${sys.id}-${contributions}`}
```

with this comment above the `StableSlot`'s child:

```tsx
                  {/* The tile is keyed on the registries' commit counter: a commit can add a tile view, whose slot must sit
                      before the border and the badge, and lv_binding_js only appends (spec/03's framework gotchas), so the
                      tile remounts with its slots in place. A commit is a launch or a pack, never a frame. */}
```

`packages/retroplug/ui/views/TileViews.tsx`: its header's "The list is the registry's, which is a module constant, so
the slot list never changes length" becomes "The list is the registry's, which only grows at a commit, and the grid
remounts the tile at a commit (SystemGrid keys it on the counter), so within a mount the slot list never changes
length".

`packages/retroplug/ui/app/useUiServices.ts`: replace the hook's body with:

```ts
export function useUiServices(host: UiHost): void {
  // The registry's list: the same array until a commit adds services, so the effects below re-run only then. The
  // caller (App) re-renders on the counter, which is what brings a new list here.
  const services = uiServices();
  const rerender = useForceUpdate();
  // Every subscription through one effect over the list: a service a pack adds is subscribed when its list arrives, and
  // the old subscriptions are dropped with the old list.
  useEffect(() => {
    const offs = services.flatMap((service) => (service.subscribe ? [service.subscribe(rerender)] : []));
    return () => offs.forEach((off) => off());
  }, [services, rerender]);
  // The host the mount's services started with. Held in a ref so a fresh host object (App rebuilds it when its own
  // inputs move) cannot restart them: `start` emits, which re-renders, so a start that followed the host's identity
  // would loop.
  const hostRef = useRef(host);
  hostRef.current = host;
  // The mount's own record of what has started, which `startServices` reads: one `start` per service per mount, so a
  // longer list starts only the services it added.
  const started = useRef(new Set<string>());
  useEffect(() => {
    startServices(services, hostRef.current, started.current);
  }, [services]);
  // One frame event, a counter per service, so two devices keep their own cadence rather than the fastest one's;
  // `tickServices` re-sizes the counters when the list's length changes.
  const ticks = useRef<number[]>(services.map(() => 0));
  useNativeEvent("frame", () => {
    if (tickServices(services, host, ticks.current)) rerender();
  });
}
```

with `useForceUpdate` imported from `@retroplug/ui-react` (its index exports it beside `useRerenderOn`; drop
`useRerenderOn` from the import, which nothing else in the file uses).

- [ ] **Step 5: The ROM patterns are a read**

`packages/retroplug/src/fileSelection.ts`: replace

```ts
export const ROM_PATTERNS: string[] = romExtensions().map((ext) => `*${ext}`);
export const ROM_OR_SAV_PATTERNS = [...ROM_PATTERNS, ...SAV_PATTERNS];
```

with

```ts
/** The file dialogs' ROM patterns: every registered console's extensions, read when a dialog opens, so a console a pack
 *  registers is offered from then on. */
export function romPatterns(): string[] {
  return romExtensions().map((ext) => `*${ext}`);
}

/** The ROM patterns and the save patterns together. */
export function romOrSavPatterns(): string[] {
  return [...romPatterns(), ...SAV_PATTERNS];
}
```

and each `patterns: ROM_PATTERNS` in the file with `patterns: romPatterns()` (and `ROM_OR_SAV_PATTERNS` with
`romOrSavPatterns()` wherever it is read: `grep -rn "ROM_OR_SAV_PATTERNS\|ROM_PATTERNS" packages/retroplug` lists
them). `packages/retroplug/ui/app/projectModals.ts` and `packages/retroplug/ui/screens/menu/menuDefs.ts` import
`romPatterns` instead and call it at each use (`patterns: m.itemKind === "rom" ? romPatterns() : SRAM_PATTERNS`,
`patterns: romPatterns()`, `romPatterns: romPatterns()`). `packages/retroplug/test/menu/n8-sd-ops.test.ts` reads it too:
its import becomes `romPatterns` and its two reads `romPatterns()` (`.toEqual(romPatterns())`,
`romPatterns().includes("*.nes")`).

- [ ] **Step 6: Run the tests**

Run: `pnpm test:ui contributions-live`
Expected: `1..2`, both `ok`, exit 0.

Run: `pnpm test:ui`
Expected: every file passes, exit 0 (`ui-services`, `menu`, `grid`, `lsdj-overlay` and `risa-overlay` among them: the
services hook, the menus and the tile views behave as before with no commit after boot).

Run: `pnpm test file-selection` then `pnpm test menu`
Expected: all `ok`, exit 0.

Run: `pnpm typecheck` then `pnpm check:boundaries`
Expected: both exit 0.

- [ ] **Step 7: Commit**

```bash
git add packages/retroplug/ui/stores/useContributions.ts packages/retroplug/test-ui/contributions-live.test.ts \
  packages/retroplug/ui/App.tsx packages/retroplug/ui/screens/grid/SystemGrid.tsx packages/retroplug/ui/app/useUiServices.ts \
  packages/retroplug/ui/views/TileViews.tsx packages/retroplug/src/fileSelection.ts packages/retroplug/ui/app/projectModals.ts \
  packages/retroplug/ui/screens/menu/menuDefs.ts packages/retroplug/ui/host/nativeHooks.ts packages/retroplug/ui/main.tsx \
  packages/retroplug/test/menu/n8-sd-ops.test.ts
git commit -m "ui: the app re-renders on a registry commit, a tile remounts for a new view, services join late"
```


### Task 7: The CLI's tools

**Files:**
- Create: `packages/retroplug/cli/contributions/toolRegistries.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderCliTools`, its call), `packages/retroplug/cli/tools.ts`,
  `packages/retroplug/cli/cli.ts`, `packages/retroplug/test/cli/cli.test.ts`, `packages/retroplug/test/packages/run-generator.mjs`
- Regenerate: `packages/retroplug/cli/generated/tools.ts`
- Test: `packages/retroplug/test/contributions/cli-live.test.ts`

- [ ] **Step 1: Write the failing test**

Create `packages/retroplug/test/contributions/cli-live.test.ts`:

```ts
// The CLI's tools follow a commit (docs/downloadable-packs-m2a-plan.md, Task 7): a tool committed after the built-ins
// joins the index after them, and a tool taking one of the shell's five names, or a contributed name, is refused at
// its commit rather than at a user's first run.
import { test, expect } from "@retroplug/testing";
import type { CliTool } from "@retroplug/core/cliTool";
import { commitContributions } from "../../src/contributions/registry";
import { cliToolsRegistry, SHELL_TOOL_NAMES } from "../../cli/contributions/toolRegistries";
import { tools } from "../../cli/tools";

const fixtureTool = {
  name: "packfixture-hello",
  summary: "say hello from a pack",
  help: "usage: retroplug-cli packfixture-hello",
  run: () => 0,
} as unknown as CliTool;

test("the shell's tool names are the dispatcher's own five, first in the index", () => {
  expect(tools().slice(0, SHELL_TOOL_NAMES.length).map((tool) => tool.name)).toEqual([...SHELL_TOOL_NAMES]);
});

test("a tool committed later joins the index after the built-ins", () => {
  const before = tools();
  commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture", items: [fixtureTool] }]);
  const after = tools();
  expect(after.length).toBe(before.length + 1);
  expect(after[after.length - 1].name).toBe("packfixture-hello");
  expect(tools()).toBe(after);
});

test("a tool taking a shell command's name is refused at its commit", () => {
  const clash = { ...fixtureTool, name: "render" } as CliTool;
  expect(() => commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture2", items: [clash] }])).toThrow(
    "a package declares the CLI tool render, which the shell already has",
  );
});

test("a tool taking a contributed name is refused naming both packages", () => {
  expect(() => commitContributions([{ registry: cliToolsRegistry, pkg: "packfixture3", items: [fixtureTool] }])).toThrow(
    "two packages declare the CLI tool packfixture-hello: packfixture and packfixture3",
  );
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test contributions/cli-live`
Expected: `BUILD FAILED: contributions/cli-live` (`Could not resolve "../../cli/contributions/toolRegistries"`).

- [ ] **Step 3: The registry, the seed and the dispatcher**

Create `packages/retroplug/cli/contributions/toolRegistries.ts`:

```ts
// The CLI's live tool registry (docs/downloadable-packs-m2a-plan.md): each package's `retroplug.cli` module's tools,
// seeded by cli/generated/tools.ts in product order, and a pack's appended. One subcommand is one tool: the index is
// what a user types, so a name two packages declare, or one of the shell's own, is refused at the commit that brings
// it. This module may not import the shell's five sessions (each pulls a session's whole graph), so it names them;
// test/contributions/cli-live.test.ts holds the list to cli/tools.ts's.
import type { CliTool } from "@retroplug/core/cliTool";
import { Registry } from "../../src/contributions/registry";

/** The shell's own commands, by name, in index order (cli/tools.ts's SHELL_TOOLS). */
export const SHELL_TOOL_NAMES: readonly string[] = ["test", "run", "render", "analyze-capture", "grab-frame"];

/** Every contributed tool, in registration order: the dispatcher lists them after the shell's five. */
export const cliToolsRegistry = new Registry<CliTool>("cliTools", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item: tool } of entries) {
    if (SHELL_TOOL_NAMES.includes(tool.name)) {
      throw new Error(`a package declares the CLI tool ${tool.name}, which the shell already has`);
    }
    const owner = owners.get(tool.name);
    if (owner !== undefined) {
      throw new Error(owner === pkg
        ? `the package ${pkg} declares the CLI tool ${tool.name} twice`
        : `two packages declare the CLI tool ${tool.name}: ${owner} and ${pkg}`);
    }
    owners.set(tool.name, pkg);
  }
});
```

In `packages/retroplug/scripts/lib/packages.mjs`, replace `renderCliTools` with:

```js
function renderCliTools({ packages, outDir, shellDir, product }) {
  const contributors = packages.filter((p) => p.cli);
  const local = (name) => `cli_${name.replace(/[^A-Za-z0-9_]/g, "_")}`;
  const lines = [
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The product's CLI tools, in product order: each package's `retroplug.cli` module, which default-exports the",
    "// subcommands it adds, seeded into the live registry cli/contributions/toolRegistries.ts (which holds the checks).",
    "// The dispatcher appends them after the shell's own five, so a package's place in",
    `// \`retroplug.products.${product}\` is where its commands land in the index. Imported only by cli/, so the control`,
    "// plane, the render worker, the SDK and the UI carry no tool.",
    `import { commitContributions } from "${contributionsSpecifier(outDir, shellDir, "src/contributions/registry.ts")}";`,
    `import { cliToolsRegistry } from "${contributionsSpecifier(outDir, shellDir, "cli/contributions/toolRegistries.ts")}";`,
  ];
  for (const p of contributors) {
    lines.push(`import ${local(p.pkg.name)} from "${moduleSpecifier(outDir, p.pkg, p.cli)}";`);
  }
  lines.push(
    "",
    "export { cliToolsRegistry };",
    "",
    ...seedLines(["cliToolsRegistry"], contributors.map((p) => ({
      registry: "cliToolsRegistry", pkg: p.pkg.name, items: local(p.pkg.name),
    }))),
    "",
  );
  return lines.join("\n");
}
```

and pass `shellDir: shell.dir` at its call in `generate`.

`packages/retroplug/cli/tools.ts`: replace `import { CLI_TOOLS } from "./generated/tools";` with

```ts
import "./generated/tools";
import { cliToolsRegistry } from "./contributions/toolRegistries";
import { derived } from "../src/contributions/registry";
```

delete the `CONTRIBUTED` constant, and replace `export const tools: readonly CliTool[] = composeTools(SHELL_TOOLS, CONTRIBUTED);`
with:

```ts
/** Every command, in the index's order: the shell's five, then the contributed ones in registration order (the
 *  product's, then packs'). The same array until the next commit. */
export const tools = derived((): readonly CliTool[] =>
  composeTools(SHELL_TOOLS, cliToolsRegistry.items().map(fromContribution)),
);
```

`composeTools` keeps its refusal (its own test reaches it with two lists of its own). `packages/retroplug/cli/cli.ts`
reads `tools()` where it read `tools` (four places: the two `topLevelHelp(tools, ...)`, `tools.find(`, and the unknown
command's help). `packages/retroplug/test/cli/cli.test.ts` reads `tools()` wherever it read `tools` (`tools.map(`,
`tools.find(`, `topLevelHelp(tools, ...)`, `tools.length`).

In `packages/retroplug/test/packages/run-generator.mjs`'s `loadCli`, the return becomes:

```js
  const byPkg = new Map();
  for (const { pkg, item } of loaded.cliToolsRegistry.entries()) byPkg.set(pkg, [...(byPkg.get(pkg) ?? []), item.name]);
  return [...byPkg].map(([pkg, tools]) => ({ pkg, tools }));
```

(the mode reports each contributing package's tool names in product order, as `CLI_CONTRIBUTIONS` did; a package that
contributes an empty list is no longer listed, and if a fixture case expects one, the case's expectation drops it).

- [ ] **Step 4: Regenerate and run**

Run: `node packages/retroplug/scripts/lib/packages.mjs`
Expected: exit 0; `cli/generated/tools.ts` holds imports, a re-export and one commit.

Run: `pnpm test contributions/cli-live` then `pnpm test cli` then `pnpm test packages`
Expected: all `ok` (`cli-live` prints `1..4`), exit 0.

Run: `pnpm typecheck` then `pnpm test:cli-ts`
Expected: both exit 0 (the CLI binary's dispatcher bundle is rebuilt by `test:cli-ts` and its tests run the commands).

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/cli/contributions/toolRegistries.ts packages/retroplug/scripts/lib/packages.mjs \
  packages/retroplug/cli/generated/tools.ts packages/retroplug/cli/tools.ts packages/retroplug/cli/cli.ts \
  packages/retroplug/test/cli/cli.test.ts packages/retroplug/test/packages/run-generator.mjs \
  packages/retroplug/test/contributions/cli-live.test.ts
git commit -m "contributions: the CLI's tools are a live registry, a shell name refused at the commit"
```

### Task 8: The API level in TypeScript

`RP_API_LEVEL` (`packages/node-sdk/native/src/graph/rp_pack.h`) is the one integer a pack is held to; its comment says
the TS halves read it the way `cmake/packs.cmake` does, from the `#define` line, through a generated module. M2b's
loader compares a pack's `apiLevel` with it.

**Files:**
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (a reader, a renderer, the file list), `tools/product-generated.js`,
  `packages/retroplug/test/packages/run-generator.mjs` (an `api-level` mode, and the `twice` mode's file list),
  `packages/retroplug/test/packages/packages-generator.test.ts` (the new case, and the `twice` case's counts)
- Generated: `packages/retroplug/src/generated/apiLevel.ts`

- [ ] **Step 1: Write the failing test**

In `packages/retroplug/test/packages/packages-generator.test.ts`, add beside the expected-module cases:

```ts
test("the generator writes RP_API_LEVEL from rp_pack.h into apiLevel.ts", async () => {
  const out = await drive("api-level");
  expect(typeof out.header).toBe("number");
  expect(out.header).toBeGreaterThan(0);
  expect(out.file.includes(`export const API_LEVEL = ${out.header};`)).toBe(true);
});
```

and in `packages/retroplug/test/packages/run-generator.mjs`, a mode in the `switch` beside `"generate"`:

```js
    case "api-level": {
      generate({ root });
      // The header's own line, read independently of the generator: the two must agree.
      const header = readFileSync(join(HERE, "../../../node-sdk/native/src/graph/rp_pack.h"), "utf8");
      const level = /^#define RP_API_LEVEL (\d+)u?\s*$/m.exec(header);
      result = { header: level ? Number(level[1]) : null, file: readFileSync(join(generated, "apiLevel.ts"), "utf8") };
      break;
    }
```

and `api-level` to the mode list in the file's header comment.

- [ ] **Step 2: Run it to see it fail**

Run: `pnpm test packages/packages-generator`
Expected: the new case fails (`ENOENT` reading `apiLevel.ts`, reported as the child's error), the rest `ok`.

- [ ] **Step 3: The generator writes it**

In `packages/retroplug/scripts/lib/packages.mjs` (import `fileURLToPath` from `node:url` if it is not imported), add:

```js
/** RetroPlug's pack API level, read from the `#define` in the node SDK's rp_pack.h as cmake/packs.cmake reads it. The
 *  header is the repository's, beside this script, not the generator's root: a fixture tree carries no SDK, and the
 *  level is the binary's, whichever tree is generated. */
function apiLevel() {
  const header = fileURLToPath(new URL("../../../node-sdk/native/src/graph/rp_pack.h", import.meta.url));
  const found = /^#define RP_API_LEVEL (\d+)u?\s*$/m.exec(readFileSync(header, "utf8"));
  if (!found) throw new Error(`${header} has no "#define RP_API_LEVEL <n>u" line`);
  return Number(found[1]);
}

function renderApiLevel(product) {
  return [
    `${generatedFrom(product)} and`,
    "// packages/node-sdk/native/src/graph/rp_pack.h. Do not edit; bump RP_API_LEVEL there.",
    "//",
    "// RetroPlug's pack API level (docs/downloadable-packs-loading-design.md, Decisions): a pack records the level it was",
    "// built against and is refused at any other. The native loader reads the header's constant; the TS halves' loader",
    "// reads this one.",
    "",
    "/** The level a pack's manifest must state. */",
    `export const API_LEVEL = ${apiLevel()};`,
    "",
  ].join("\n");
}
```

and to `generate`'s `files` list, beside `placers.ts`:

```js
    {
      path: join(outDir, "apiLevel.ts"),
      content: renderApiLevel(product),
    },
```

The generator's `twice` case reports one `changed` flag per file `generate` writes, which is now one more. In
`run-generator.mjs`'s `twice` mode, add `"apiLevel.ts"` to the list of `src/generated` names its mtimes are taken from
(after `"placers.ts"`), and in `packages-generator.test.ts`'s "an unchanged input leaves the generated files untouched"
case, both `changed` arrays get an eleventh entry (`true` in the first, `false` in the second) and the stamp checks gain
`expect(after[10]).toBe(before[10]);`.

In `tools/product-generated.js`, add `apiLevel: "src",` to `GENERATED` (a product's bundle must read its own tree's copy,
and the plugin's guard refuses a bundle carrying the desktop's).

- [ ] **Step 4: Run**

Run: `node packages/retroplug/scripts/lib/packages.mjs`
Expected: exit 0, and `packages/retroplug/src/generated/apiLevel.ts` exists with `export const API_LEVEL = 1;`.

Run: `pnpm test packages/packages-generator` then `pnpm typecheck`
Expected: all `ok` (the `twice` case with its eleven flags), exit 0.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/scripts/lib/packages.mjs packages/retroplug/src/generated/apiLevel.ts tools/product-generated.js \
  packages/retroplug/test/packages/run-generator.mjs packages/retroplug/test/packages/packages-generator.test.ts
git commit -m "packs: the TS mirror of RP_API_LEVEL, generated from rp_pack.h"
```

### Task 9: The docs

- [ ] **Step 1: spec/06's "The registries are generated, not written"**

In `spec/06-build-test.md`, that paragraph's lead sentence becomes **"The registries are seeded by generated modules,
and live."**, and after its list of generated files add:

> Each list is a live registry (`packages/retroplug/src/contributions/registry.ts`): the generated modules hold no
> lists and no checks, only one `commitContributions` call each, which seeds the hand-written registries
> (`src/contributions/{consoleRegistries,placerRegistries,appRegistries}.ts`, `ui/contributions/uiRegistries.ts`,
> `cli/contributions/toolRegistries.ts`) in product order. A registry only grows, a commit is all or nothing across
> registries, and every seeded registry's check runs at every commit, so a contribution committed later (a pack's,
> [downloadable-packs-loading-design.md](../docs/downloadable-packs-loading-design.md)) is held to what a built-in one
> is and reaches the stores, the menus, the views and the CLI: the console lookups and other derived lists are
> rebuilt on the commit counter, the project and config stores adopt sections and services at their next read
> (`adoptContributions` publishes at once), the app re-renders on the counter and a tile remounts. A shell module
> reads a registry through its facade (`src/contributions/app.ts`, `src/contributions/placers.ts`, `src/consoles.ts`,
> `ui/contributions/menus.ts`, `ui/contributions/views.ts`), which imports the generated seed, never the generated
> module directly. `src/generated/apiLevel.ts` is `RP_API_LEVEL` read from `rp_pack.h`.

and in the same section, every sentence that says "the generated registry refuses" (the project sections, services,
placers, config sections, views and actions paragraphs: `grep -n "generated registry refuses" spec/06-build-test.md`)
says "the registry's check refuses".

- [ ] **Step 2: spec/04, spec/02 and spec/05**

- `spec/04-roles-protocol-nodes.md`, "The built-in roles": "applies the generated `ROLES` list (`src/generated/packages.ts`)
  and then the ROM providers (`registerRomProviders`)" becomes "is an `AppRoleRegistry` over the live roles and ROM
  providers registries (`src/contributions/appRegistries.ts`, seeded by `src/generated/packages.ts`), adopting what a
  later commit adds at its next read". Further down the same file, "holds it to the roles the generated `ROLES` list
  carries" becomes "holds it to the roles the roles registry carries".
- `spec/02-native-host.md` near "the UI's menu contributions and the CLI's tools arrive through the generated
  `ui/generated/menus.ts` and `cli/generated/tools.ts`": "arrive through the live registries those generated modules
  seed (`ui/contributions/uiRegistries.ts`, `cli/contributions/toolRegistries.ts`)".
- `spec/05-data-persistence.md` near line 224 ("the generated registry refuses a contributor of one"): "the config
  sections registry's check refuses a contributor of one".

- [ ] **Step 3: spec/07**

In `spec/07-remaining-work.md`'s "Downloadable packs" entry, "what is left of it is [the M1 plan](...)'s M2 (a pack's TS
halves and the live registries), M3" becomes "what is left of it is M2b (loading a pack's TS halves, the protocol half
and the contribution kinds among them, with the module-linking spike first: [the M2a plan](../docs/downloadable-packs-m2a-plan.md)'s
design questions), M3", and the sentence (wrapped across lines 166-168 of the file) "With them come menus on extensions (a downloaded pack contributing menu rows,
which packages in the tree do through `defineUi`) and third-party controller apps: `controllerApps` registers from the
product's package list, so an app from outside the tree waits for this road." becomes "With them come third-party
controller apps: `controllerApps` registers from the product's package list (the kinds are M2b's), so an app from
outside the tree waits for this road; menu rows a pack contributes go through the live UI registry."

- [ ] **Step 4: Check and commit**

Run: `pnpm check:docs`
Expected: exit 0.

```bash
git add spec/02-native-host.md spec/04-roles-protocol-nodes.md spec/05-data-persistence.md spec/06-build-test.md \
  spec/07-remaining-work.md
git commit -m "docs: the registries are live, seeded by the generated modules"
```

### Task 10: Verify the whole tree

- [ ] **Step 1: The suites**

Run each and read its exit status:

```bash
pnpm test
pnpm test:ui
pnpm typecheck
pnpm check:boundaries
pnpm check:docs
pnpm test:cli-ts
pnpm test:native
pnpm test:plugin
```

Expected: every one exits 0. `test:native` and `test:plugin` rebuild the embedded bundles (the control plane, the UI,
the CLI dispatcher, the render worker), so they are what proves the products' bundles (plugin and packs-host included)
still build and boot with the registries in them. A failure is fixed in the task it belongs to, not here.

- [ ] **Step 2: The planner's and the products' bundles**

Run: `pnpm test script-bundle`
Expected: every case `ok`: the planner's bundle carries `src/contributions/{registry,consoleRegistries,placerRegistries}.ts`
and no `src/generated/packages.ts`, no `src/contributions/appRegistries.ts` and no zod; the UI split holds; a product's
bundle carries its own tree's generated modules (the redirect's guard).

- [ ] **Step 3: The tip verify**

Run the repository's parallel tip-verify the way the project runs it
(`env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 .superpowers/verify/tip-verify-par.sh`), and compare with the last
recorded run under `.superpowers/verify/runs/`. Expected: the same pass set as before M2a (the one known local failure,
the GBA app-cores case that reads the M8 worktree's sidecar in the shared fixtures, may remain).

- [ ] **Step 4: Report**

Write what ran, with exit codes, into `.superpowers/verify/runs/m2a/summary.md`. Nothing is pushed: CI on the other
platforms runs when the user asks for it.

## Design questions this plan found

**For M2b:**

1. **The module linking needs a spike before M2b's plan.** The desktop's control plane (`cp-bundle`) and UI
   (`ui-bundle`) are two separately built bundles evaluated into one txiki context, each with its own inlined copy of
   every `@retroplug/*` module, and so of these registries. A pack's ES modules importing `@retroplug/core/...` resolve
   to one module record per context, which cannot be both copies, while a pack's contributions must reach both (the
   control plane's roles for the stores, the UI's for the menus) and its UI half must use the UI's React context
   objects. The ESM answer (the user prefers ESM, 2026-11-23): build the contract modules once, as shared named modules
   in the context (esbuild code splitting, each output compiled by `tjsc` under its output path as its module name),
   build the control plane and the UI against them as externals, and resolve the bare specifiers through txiki's
   existing import map (`tjs.setImportMap`, unused today; its normalizer consults the map for a bare name before the
   loaded-module lookup the CLI's `retroplug-cli` module already relies on). Then a context holds one copy of each
   contract module and one set of registries, the two `src/contributions` copies merge, and a pack's halves evaluate
   once. Node would serve the same modules through `module.register`. Unproven: that relative imports between named
   bytecode chunks resolve, and the import map's order against the loaded-module lookup. The spike proves one contract
   module, a chunk, the map and a fake pack importing it, on txiki and on Node.
2. **The contribution kinds' `accept` is called once, at load** (`defineKind.ts`: "Hand the declaring package its items
   once, at load"). A pack contributing `controllerApps` items needs either a second `accept` with the whole list or an
   `add`; M2b changes the contract with the protocol half, whose kind data the items pair with.
3. **`commitByName`'s names are the registries' contract with a pack's loader.** They are the `PackageContributions`
   member names (`roles`, `romProviders`, ...) and the consoles module's (`consoles`, `realCarts`), so a pack's exports
   map onto them by name; renaming a registry is then a pack-breaking change under `RP_API_LEVEL`.
4. **`isInsidePath` (M1's `PackManifest.cpp`) does not refuse Windows reserved device names or a trailing dot or
   space** (M1's design question 0): M2b adds them when it reads a TS half's path.

**Open beyond M2b:**

5. **A refused commit leaves a `RoleRegistry` or a store untouched, but a commit that passes cannot be undone.** Part 1
   removes a pack only at the next launch, so nothing needs to, but a pack whose TS half throws after its native
   library loaded (M2b's all-or-nothing across halves) must stage every half's additions into one commit, not one per
   half.
6. **The UI re-renders the whole app at a commit and remounts every tile.** A commit is a launch or a pack install, so
   the cost is a one-off; a pack store that commits often (a development pack's hot reload, the C1 design's section 8)
   would want the tile to keep its canvas and re-key only the view slots' container.

## After the final review

M2a was built as `b78595daa..3bb37c074`. Where the task blocks above and the tree differ, the tree
wins: Task 1's `commitContributions` is generic over a tuple with each addition's items typed `NoInfer<T>` against its
registry, and a registry's rule lives in its private state; `derivedList` keeps a list's array across a commit that
does not change it (the menus' actions and services, the views); a project service whose `create()` throws when adopted
is warned about and skipped; `adoptContributions` also fires the project channel; the grid keys a tile on
`tileViews().length`, not the commit counter; the generated modules are gitignored build outputs, not tracked; the CLI's
shell-name refusal names the package.

The final review's findings for M2b, beside design questions 1 to 4:

7. **A registry's name is not always a member name.** It is for the app members, the consoles module (`consoles`,
   `realCarts`) and the views module (`views`), but a `ui` item is a whole `defineUi` export, the `placers` item is the
   placer module's default export, `cliTools` takes the CLI module's exported array, `bakedEmbeds` is no member, and a
   kind's members (`controllerApps`) have no registry. M2b's loader needs an explicit table from a pack's half to the
   registries and item shapes it fills, not a member-name passthrough (this refines question 3).
8. **No bundle holds every registry**, and `commitByName` refuses an unknown name. The control plane has the app, the
   consoles and the placers; the UI those and `ui` and `views`; the render worker the consoles and placers; the CLI the
   tools. A loader needs to ask a bundle which registries it has (a `registryNames()` export) or skip absent ones.
   Within a bundle a pack's halves must go in one commit: a console whose core role is in the same pack fails the roles
   check alone.
9. **A pack's embedded ROM is always refused**: the embeds check requires the declaring package to bake the ROM into the
   binary (`bakedEmbeds`), which a pack cannot. A pack-carried ROM needs a rule of its own.
10. **The control plane's and the UI's registries do not follow each other.** In the plugin the UI reuses the control
    plane's stores (whose role registry and sections read the control plane's registries), while
    `__rp_commitUiContributions` reaches only the UI's copy, and the UI bundle is evaluated at the first editor open,
    after packs committed at launch. If the spike (question 1) does not merge the copies, the UI must pull the packs
    already loaded when it evaluates.
11. **A system already loaded keeps the feature roles its ROM was given**: a pack's ROM provider attaches at the next load
    or derive, not to systems on screen.
12. **Commit cost**: every seeded check runs at every commit (the UI check parses every role's schema defaults, the embeds
    check is quadratic), so a launch should commit its packs in one batch per bundle, not one commit per pack.

## Self-review

**Spec coverage** (the design's section 3, "Live registries" and "One set of checks", for M2a's share):

- A small `Registry` replaces each generated constant: the roles, ROM providers, tracker integrations, kit formats,
  project and config sections, project services and embeds (Task 3), the consoles and real carts and the placers
  (Task 2), the UI's menus and views (Task 5), the CLI tools (Task 7). The generated modules seed them (each task's
  generator step) and `commitContributions` appends after the built-ins (Task 1), keeping product order for the seed and
  load order after it.
- A registry notifies on change and the menus and views re-render (Task 1's listeners, Task 6's hook, the grid's key).
- One set of checks: the generator's emitted checks move into the registries' modules, word for word, and run at every
  commit, so the same mistake fails the same way at load (the generator's fixture test) and at a later commit (each
  `*-live` test's refusal case). The build-time manifest checks (`packages.mjs` section A: a manifest's shape, the
  protocol tables, the capture rules) stay in the generator: they judge `package.json` fields a pack does not have in
  that form, and the protocol half's are M2b's (design question 1's plan names them).
- All or nothing: a commit across registries is all or nothing (Task 1); staging a pack's halves into one commit is M2b
  (design question 5).
- The TS mirror of the API level (Task 8), which M1 promised to M2.

**Placeholder scan:** every new module is given whole; every consumer change names the file, the old text and the new;
the generator's render functions are given whole; each step names its command and what it prints. Two steps defer to the
tree where this plan's code was not compiled (an import a type lives under, a fixture mode's exact expectation), and
each says what to check.

**Type and name consistency:** `Registry`, `Contribution`, `RegistryView`, `RegistryCheck`, `Addition`, `NamedAddition`,
`commitContributions`, `commitByName`, `contributionsVersion`, `subscribeContributions`, `derived` (Task 1);
`consolesRegistry`, `realCartsRegistry`, `placersRegistry`, `placerEntries`, `consoleDescriptors` (Task 2);
`rolesRegistry`, `romProvidersRegistry`, `trackerIntegrationsRegistry`, `kitFormatsRegistry`, `projectSectionsRegistry`,
`projectServicesRegistry`, `configSectionsRegistry`, `embedsRegistry`, `bakedEmbedsRegistry`, `appAdditions`,
`projectSectionEntries`, `configSectionEntries`, `projectServiceEntries`, `SHELL_SECTION_KEYS`, `SHELL_CONFIG_KEYS`,
`AppRoleRegistry`, `trackerIntegrations`, `trackerMarkers`, `kitFormats` (Task 3); `defaultSettings`,
`adoptProjectSection`, `adoptConfigSection`, `adoptContributions` (Task 4); `uiRegistry`, `viewsRegistry` (Task 5);
`useContributionsVersion`, `__rp_commitUiContributions`, `romPatterns`, `romOrSavPatterns` (Task 6);
`cliToolsRegistry`, `SHELL_TOOL_NAMES`, `tools()` (Task 7); `API_LEVEL` (Task 8). The registry names
(`"consoles"`, `"realCarts"`, `"placers"`, `"roles"`, `"romProviders"`, `"trackerIntegrations"`, `"kitFormats"`,
`"projectSections"`, `"projectServices"`, `"configSections"`, `"embeds"`, `"bakedEmbeds"`, `"ui"`, `"views"`,
`"cliTools"`) are each given once and used by Task 6's seam and its test.
