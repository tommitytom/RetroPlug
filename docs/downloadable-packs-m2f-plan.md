# Downloadable packs M2f: a pack's contribution-kind items. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pack contributes items to a contribution kind a built-in package declares (a controller app, today's one
kind), with all three of an item's halves:
- the control-plane half reaches the declaring package's registry;
- its data reaches the live kind data the planner reads;
- its runtime reaches the built-in script contexts where the declaring package's node runs.

The result is that a pack's controller app is offered, placed and run as a built-in package's is.

**Architecture:**
- **The kinds' data and items are live.** `KIND_DATA` becomes a live registry, and so do the declared kinds and the
  taken item ids. The generated modules seed all three, and a contribution kind gains an `add` for a later commit's
  items.
- **The runtime is a global-code chunk.** A pack's `kinds` half is global code calling a registrar the built-in
  script bundle now exposes, `__rp_registerKindItems`.
- **The pack table checks the chunk once.** It compiles the chunk and trial-loads it beside the built-in script nodes.
- **Every Engine runs it from construction.** An Engine loads its built-in pack with the embedded bundle and then
  every loaded pack's kinds chunk.
- **Every host loads its launch packs before it builds an Engine.** An Engine built before a kind-carrying pack loaded
  refuses that pack by name.

**Tech Stack:** C++20, QuickJS-ng (the script host, `dsp::compileToBytecode`), M1's `retroplug-packs`, M2a's live
registries, M2c's TS loader, M2e's protocol half (its trial, `DATA_HALVES`, the build tool's script-realm bundling),
esbuild, Catch2 v3, the shell's TS, CLI, UI and generator suites.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones. These
are done, each with its plan beside this one:
- M1, native packs.
- M2a, live registries.
- M2b, shared contract modules.
- M2c, a pack's TS halves.
- M2d, the device script half and the render worker.
- M2e, the protocol half.

Then:

- **M2f (this plan, decided with the user 2026-11-25):** the contribution kinds, for the one direction a planned pack
  needs: a pack contributing items to a kind a built-in package declares. Today:
  - a pack's app half contributing to a kind is refused (`loadPacks.ts`, "a contribution kind a pack cannot add to
    yet");
  - pack.json's `protocol.kinds` is refused;
  - the build tool refuses `retroplug.protocol.kinds`.

  The user's decisions:
  - **A kind item's runtime half runs in the built-in script contexts.** That is where the declaring package's node
    looks it up (`controllerApp(id)` in `rp.controller.session`).
  - **It gets there when an Engine is built**, not by reloading the built-in pack. So every host loads its launch
    packs first.
  - **A pack installed live (M4) gets its kind items at the next launch.**
  - **A pack declaring a kind of its own stays refused.**
- M3 placeholders, M4 archives, commands and Settings > Packs, and M5 the real packages as packs, are as before.

**What exists.**

*The manifests.*
- A kind is declared by a package's `retroplug.kinds.<kind>` member. The launchpad's `controllerApps` declares:
  - `control`, the module default-exporting a `ContributionKind` with `accept` and `check`;
  - `runtime`, the module, its `register` function and the item type;
  - `data`, the manifest keys an item may carry (`syncOverride`).
- A package contributes items in two places:
  - its app module's `<kind>` list, the control-plane half (`ControllerAppContribution`);
  - its manifest's `retroplug.protocol.kinds.<kind>` entries, each `{ id, module, ...data }`, which is the runtime
    module and the data.

*The generated modules.*
- `src/generated/packages.ts` pairs the halves by id, refuses a duplicate, and calls the kind's `check` and then
  `accept` once with every item.
- `src/generated/protocol.ts` carries the data as `KIND_DATA`. `buildGraph`'s placers read it through
  `PlaceContext.kindData`.
- `src/generated/scriptNodes.ts` imports each runtime module and calls `registerControllerApps(items)` in the built-in
  bundle, before the node definitions.

*The launchpad's side.*
- `accept` fills `controllerApps` (the menu's table).
- It also calls `setControllerSyncData(data)`, which replaces the menu's copy of the data.
- `registerControllerApps` replaces an app registered under the same id.

*The hosts.*
- The plugin (`PluginDSP.cpp`'s `HostServices svc_`), the CLI (`HostServices svc` in `cli/main.cpp`), the SDL standalone
  and the UI harness all build their Engine before they call `ensureLaunchPacks`.
- Render jobs build theirs later, so they already see the launch packs.

**Execution model.**
- Sonnet implements each task from its text alone, and Opus reviews each task's diff before the next one starts.
- The code here was not compiled or run as written. Where a step meets the tree differently (a helper's name, an
  include, a type's exact field), the implementer follows the tree, keeps the step's intent, and reports the deviation.
- Task 11 ends with a CI gate on Windows and macOS, which needs the user's go-ahead to push.

## Decisions this plan makes

- **A pack's kind items have three halves, as a built-in package's do, each where a built-in item's is.**
  - **The control half** is in the pack's app half, under the kind's name. For example, `controllerApps:
    [{ id, label, config, menu }]`.
  - **The data** is pack.json's new top-level `kinds` member: `{ "<kind>": [{ "id": ..., <data keys> }] }`. It is the
    manifest's `retroplug.protocol.kinds` with each entry's `module` dropped, and the build tool copies it. It is top
    level rather than inside `protocol` because a pack may contribute items with no protocol nodes, as lsdj-launchpad
    does.
  - **The runtime** is the new `kinds` half (`halves.kinds`): a global-code bundle that, for each kind, imports its
    entries' runtime modules and calls `__rp_registerKindItems(kind, items, ids)`.
  - `kinds` without `halves.kinds`, or the reverse, is refused. The members are additive (a host before M2f refuses
    them anyway), so the API level does not move.
- **The built-in bundle exposes one registrar.** The generated `scriptNodes.ts` defines `globalThis.__rp_registerKindItems`
  after it registers the product's own items. For each kind the product declares, it calls that kind's runtime
  `register` function. It refuses:
  - a kind the product does not declare;
  - items whose ids are not the `ids` given, in order;
  - an id the kind already holds. `registerControllerApps` would replace it silently, so a pack could shadow a built-in
    app.

  A product with no kinds still defines the registrar, refusing every call.
- **One trial, every Engine, from construction.**
  - **The trial.** `PackTable` reads the kinds half and hands it to a kinds trial the backend installs
    (`host/packs/KindsTrial.cpp`, beside M2e's protocol trial). The trial compiles it, and loads the embedded built-in
    bundle and then the chunk into a scratch `ScriptHost`. A chunk that throws (an unknown kind, a taken id, a bad
    module) is refused there.
  - **Every Engine.** An Engine built on the embedded bundle loads its built-in pack with that bundle and then every
    loaded pack's kinds chunk, in the table's order. It remembers which packs those were.
  - **Late packs.** At `syncPacks`, a pack with a kinds half that is not among them is refused in this Engine: "its
    kind items reach an Engine built after it loaded; they take effect at the next launch".
  - **Another bundle.** An Engine built on another bundle (a test's) loads no kinds chunk and refuses such a pack: "this
    engine runs another script bundle".
  - **A failed load.** If the built-in load with the chunks fails, the Engine loads the bundle alone and refuses every
    kind-carrying pack. One pack's runtime can never take the built-in script nodes down.
- **Every host loads its launch packs before it builds an Engine.** That covers the plugin, the CLI dispatcher (not a
  session file, which loads no pack), the SDL standalone and the UI harness. Render jobs already build theirs after.
- **The kinds' tables are live.** These replace the frozen `KIND_DATA` and the generated duplicate loop:
  - `kindDataRegistry`: `{ kind, id, data }`, seeded by the generated `protocol.ts`, so the planner's bundle still
    carries no app.
  - `kindItemsRegistry`: `{ kind, id }`, the taken ids, seeded by the generated `packages.ts`. Its check refuses a
    duplicate with today's messages.
  - `contributionKindsRegistry`: `{ kind, contribution, data }`, the declared kinds with their `ContributionKind` and
    data keys, seeded by `packages.ts`.
  - The readers are `kindData(kind)` and `declaredKind(kind)`, rebuilt after a commit.
- **A kind takes a later commit's items through `add`.**
  - `ContributionKind` gains `add?(items, data)`. It is called after a pack's commit with that pack's items and their
    data alone, and must not throw: its `check` runs first, at staging, and is where a refusal belongs.
  - A kind without `add` takes no items from a pack, and such a pack is refused.
  - The launchpad's `add` registers the items and merges the data into the menu's copy. This is an additive change to
    a contract module (`@retroplug/core/defineKind`), so no API level bump.
- **The loader holds a pack's items to the same rules as the generator's.** For each kind the pack contributes to:
  - the kind must be declared, and must have `add`;
  - the app half's ids and the `kinds` entries' ids must pair, in both directions;
  - every data key must be one the kind declares;
  - no id may be another package's;
  - the kind's `check` runs over the pack's items. Its context is the live protocol tables plus the pack's own staged
    protocol nodes and roles, so an app whose `syncOverride` names the pack's own sync node is accepted.
- **A kinds half bundles what its runtime imports, except a schema or UI library.** It runs in the built-in contexts,
  which have no module loader. So the build tool bundles its `@retroplug` imports, resolving each through that
  package's exports map: lsdj-launchpad's runtime imports `@retroplug/lsdj/playback/predict`. It refuses `zod`,
  `react`, `react/jsx-runtime`, `lvgljs` and `lvgljs-ui`.

## Commands used throughout

- **The TS suite.** Run `pnpm test <slug>`, one slug per run: a file's path under `test/` without `.test.ts`, or a
  directory. Read each exit code with `echo $?` right after the command, not piped. A slug that matches nothing exits
  1 with "no tests match".
- **The other suites.**
  - The native suite: `pnpm test:native [slug]`.
  - The CLI: `pnpm test:cli-ts`.
  - The UI: `pnpm test:ui [slug]`.
  - The plugin binaries: `pnpm test:plugin [binary]`, e.g. `pnpm test:plugin packs-test`.
- **One binary.** `cmake --build build --target <t> -j$(nproc)`, then e.g. `build/bin/retroplug-packs-test "[packs-kinds]"`.
  - After a CMake change, run `pnpm configure`.
  - Never `rm -rf build`.
- **Checks.** `pnpm typecheck` (in `packages/retroplug`), `pnpm check:boundaries`, `pnpm check:docs` (repo root).
- **Tree hygiene.** `deps/sameboy` shows as modified after a configure: that is expected, so never stage it. Generated
  modules and `build/` are gitignored outputs. Stage files by name.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/retroplug/src/contributions/kindRegistries.ts` | `kindDataRegistry`, `kindItemsRegistry`, `contributionKindsRegistry`, `kindData()`, `declaredKind()` |
| `packages/retroplug/src/packs/kindsHalf.ts` | a pack's kind items checked and turned into additions; the `add` calls after its commit |
| `packages/retroplug/test/contributions/kinds-live.test.ts` | the live kind tables, their readers, their refusals |
| `packages/retroplug/test/script/kind-registrar.test.ts` | the built-in bundle's `__rp_registerKindItems` |
| `packages/native/src/host/packs/PackKinds.hpp` | `KindsHalf`, `KindsTrial` |
| `packages/native/src/host/packs/KindsTrial.{hpp,cpp}` | `kindsTrial()`: the backend's compile and trial load |
| `packages/native/test/packs/PackKinds.test.cpp` | `[packs-kinds]`: the table, the pack list, Engines from construction, the built fixture |
| `packages/native/test/packs/kinds-fixture/` | `kindfix` and its variants, hand-written, with their CMake |
| `packages/native/test/packs/ts-fixture/kinds/` | `tsfix-kind`: a package with an app half and a controller app's runtime, built by the tool |

Modified:
- **TS shell and contracts:**
  - `packages/core/ts/defineKind.ts` (`add`)
  - `packages/launchpad/ts/control/{kind,placer}.ts` (`add`, merged data)
  - `packages/retroplug/scripts/lib/packages.mjs` (`renderProtocol`, `kindBlocks`, `renderScriptNodes`)
  - `packages/retroplug/test/packages/fixtures/expected/{protocol,packages,scriptNodes}.ts`
  - `packages/retroplug/src/graph/buildGraph.ts`
  - `packages/retroplug/src/packs/{loadPacks,packHost}.ts`
  - `packages/retroplug/test/packs/load-packs.test.ts`
  - every reader of `KIND_DATA` (the typecheck lists them)
- **Native packs, engine and hosts:**
  - `packages/native/src/host/packs/{PackManifest,PackTable,ProcessPacks}.{hpp,cpp}`
  - `packages/native/src/host/engine/Engine.{hpp,cpp}`
  - `packages/native/src/host/js/PackLoaderCore.cpp`
  - `packages/native/plugin/PluginDSP.cpp`, `packages/native/cli/main.cpp`, `packages/native/sdl/main.cpp`
  - `packages/native/test/ui/UiHarness.{hpp,cpp}`
  - `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`
  - `packages/native/test/packs/{PackManifest,PackProtocol}.test.cpp`
  - `packages/native/test/packs/ts-fixture/{CMakeLists.txt,tsconfig.json}`
- **The build tool:** `tools/build-pack-halves.mjs`, `packages/retroplug/test/script/build-pack-halves.test.ts`
- **End-to-end tests:** `tools/run-cli-ts-tests.sh`, `packages/retroplug/scripts/run-ui-tests.mjs`,
  `packages/retroplug/test-ui/packs-ts-fixture.test.ts`
- **The docs:** spec/02, spec/04, spec/06, spec/07, the loading design's sections 1 and 3.

---

### Task 1: The live kind tables, and `add`

**Files:**
- Create: `packages/retroplug/src/contributions/kindRegistries.ts`, `packages/retroplug/test/contributions/kinds-live.test.ts`
- Modify: `packages/core/ts/defineKind.ts`, `packages/launchpad/ts/control/kind.ts`, `packages/launchpad/ts/control/placer.ts`,
  `packages/retroplug/scripts/lib/packages.mjs` (`renderProtocol`, `kindBlocks`, `renderPackages`),
  `packages/retroplug/src/graph/buildGraph.ts`, the generator's expected fixtures, every `KIND_DATA` reader

- [ ] **Step 1: Write the failing test**

`packages/retroplug/test/contributions/kinds-live.test.ts`:

```ts
// The live kind tables (docs/downloadable-packs-m2f-plan.md): the generated modules seed the declared kinds, the taken
// item ids and the items' data; a commit adds a pack's; the readers see it after the commit; and a commit the checks
// refuse changes nothing.
import { test, expect } from "@retroplug/testing";
import "../../src/generated/packages";
import { commitContributions } from "../../src/contributions/registry";
import { contributionKindsRegistry, declaredKind, kindData, kindDataRegistry, kindItemsRegistry } from "../../src/contributions/kindRegistries";

const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};

test("the seed is the product's kinds, items and data", () => {
  const k = declaredKind("controllerApps");
  expect(k?.pkg).toBe("launchpad");
  expect(k?.data).toEqual(["syncOverride"]);
  expect(typeof k?.contribution.accept).toBe("function");
  expect(kindItemsRegistry.items().some((i) => i.kind === "controllerApps" && i.id === "lsdj-midimap")).toBe(true);
  expect(Object.keys(kindData("controllerApps"))).toEqual(["lsdj-midimap"]);
  expect(kindData("nothing")).toEqual({});
});

test("a pack's item id and data join in one commit, and the readers see them", () => {
  commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-a", items: [{ kind: "controllerApps", id: "kl-a-app" }] },
    { registry: kindDataRegistry, pkg: "kl-a", items: [{ kind: "controllerApps", id: "kl-a-app", data: { syncOverride: { node: "x" } } }] },
  ]);
  expect(kindData("controllerApps")["kl-a-app"]).toEqual({ syncOverride: { node: "x" } });
});

test("a taken id, an item of an undeclared kind, or data with no item is refused, and nothing changes", () => {
  const items = kindItemsRegistry.items();
  const data = kindDataRegistry.items();
  expect(refusal(() => commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-b", items: [{ kind: "controllerApps", id: "lsdj-midimap" }] },
  ]))).toBe("two packages contribute the controllerApps item lsdj-midimap: lsdj-launchpad and kl-b");
  expect(refusal(() => commitContributions([
    { registry: kindItemsRegistry, pkg: "kl-c", items: [{ kind: "nope", id: "x" }] },
  ]))).toBe("kl-c contributes the nope item x, and no package declares the contribution kind nope");
  expect(refusal(() => commitContributions([
    { registry: kindDataRegistry, pkg: "kl-d", items: [{ kind: "controllerApps", id: "kl-d-app", data: {} }] },
  ]))).toBe("kl-d declares data for the controllerApps item kl-d-app, which no package contributes");
  expect(kindItemsRegistry.items()).toBe(items);
  expect(kindDataRegistry.items()).toBe(data);
  expect(contributionKindsRegistry.items().length).toBe(1);
});
```

The data-without-item rule reads the items registry from the data registry's check. In the planner's bundle, which
carries no app and so no items seed, the items registry reads as unseeded and empty. So the data registry's check
skips this rule when `kindItemsRegistry` is not seeded in the module set (`view(kindItemsRegistry)` returns the empty
list for an unseeded registry, and the check asks `kindItemsRegistry.seeded`). The test imports the generated
`packages.ts`, so the rule runs here.

- [ ] **Step 2: Run it to make sure it fails**

Run: `pnpm test contributions/kinds-live`
Expected: FAIL, the module `kindRegistries` cannot be resolved.

- [ ] **Step 3: The contract's `add`**

In `packages/core/ts/defineKind.ts`, `ContributionKind` gains:

```ts
  /** A later commit's items, after it committed: a downloaded pack's, which arrive after `accept` ran
   *  (docs/downloadable-packs-m2f-plan.md). `data` holds those items' data alone. It must not throw: `check` runs over
   *  the same items before the commit and is where a refusal belongs. A kind without `add` takes no pack's items. */
  add?(items: readonly I[], data: KindData): void;
```

Update the file's header to say `accept` runs once with the product's items and `add` once per later commit. Update
`KindCheckContext`'s comments, which name the generated tables, to say the live protocol tables.

- [ ] **Step 4: The registries**

`packages/retroplug/src/contributions/kindRegistries.ts`:

```ts
// The contribution kinds' live tables (docs/downloadable-packs-m2f-plan.md): the kinds the product's packages declare
// (their ContributionKind and the data keys an item may carry), the item ids taken in each, and each item's data. The
// generated modules seed them: the data from src/generated/protocol.ts, which the planner's bundle carries with no
// app; the kinds and the ids from src/generated/packages.ts, which is the app's. A pack's kind items add to the last
// two at its commit (src/packs/kindsHalf.ts). Only registry.ts and core's types are imported, so the planner's bundle
// carries no schema.
import type { ContributionKind, KindData } from "@retroplug/core/defineKind";
import { derived, Registry } from "./registry";

/** A kind a package declares: its ContributionKind and the manifest keys an item of it may carry as data. */
export interface DeclaredKind {
  readonly kind: string;
  readonly contribution: ContributionKind<any>;
  readonly data: readonly string[];
}

/** An item a package contributes to a kind, by id: what a document names it by. */
export interface KindItemId {
  readonly kind: string;
  readonly id: string;
}

/** An item's data, as its manifest declares it (only the kind's data keys). */
export interface KindItemData extends KindItemId {
  readonly data: Readonly<Record<string, unknown>>;
}

export const contributionKindsRegistry = new Registry<DeclaredKind>("contributionKinds", (entries) => {
  const owners = new Map<string, string>();
  for (const { pkg, item } of entries) {
    const at = owners.get(item.kind);
    if (at !== undefined) {
      throw new Error(at === pkg
        ? `the package ${pkg} declares the contribution kind ${item.kind} twice`
        : `two packages declare the contribution kind ${item.kind}: ${at} and ${pkg}`);
    }
    owners.set(item.kind, pkg);
  }
});

export const kindItemsRegistry = new Registry<KindItemId>("kindItems", (entries, view) => {
  const kinds = new Set(view(contributionKindsRegistry).map((e) => e.item.kind));
  const owners = new Map<string, string>();
  for (const { pkg, item } of entries) {
    // Unseeded (a bundle with no app) reads as empty: the declared-kind rule is the app's to run.
    if (contributionKindsRegistry.seeded && !kinds.has(item.kind)) {
      throw new Error(`${pkg} contributes the ${item.kind} item ${item.id}, and no package declares the contribution ` +
        `kind ${item.kind}`);
    }
    const key = `${item.kind}\u0000${item.id}`;
    const at = owners.get(key);
    if (at !== undefined) {
      throw new Error(at === pkg
        ? `the package ${pkg} contributes the ${item.kind} item ${item.id} twice`
        : `two packages contribute the ${item.kind} item ${item.id}: ${at} and ${pkg}`);
    }
    owners.set(key, pkg);
  }
});

export const kindDataRegistry = new Registry<KindItemData>("kindData", (entries, view) => {
  if (!kindItemsRegistry.seeded) return;
  const items = new Set(view(kindItemsRegistry).map((e) => `${e.item.kind}\u0000${e.item.id}`));
  for (const { pkg, item } of entries) {
    if (!items.has(`${item.kind}\u0000${item.id}`)) {
      throw new Error(`${pkg} declares data for the ${item.kind} item ${item.id}, which no package contributes`);
    }
  }
});

/** The data the manifests declare for `kind`'s items, by item id: what a placer reads through PlaceContext.kindData.
 *  An empty record for a kind nothing contributes to. */
const dataByKind = derived((): Readonly<Record<string, KindData>> => {
  const out: Record<string, Record<string, Readonly<Record<string, unknown>>>> = Object.create(null);
  for (const { kind, id, data } of kindDataRegistry.items()) (out[kind] ??= Object.create(null))[id] = data;
  return out;
});
const EMPTY: KindData = Object.freeze({});
export function kindData(kind: string): KindData {
  const all = dataByKind();
  return Object.prototype.hasOwnProperty.call(all, kind) ? all[kind] : EMPTY;
}

/** The kind `kind` as its declaring package states it, with that package's id, or undefined for one nobody declares. */
export function declaredKind(kind: string): (DeclaredKind & { pkg: string }) | undefined {
  const e = contributionKindsRegistry.entries().find((x) => x.item.kind === kind);
  return e ? { ...e.item, pkg: e.pkg } : undefined;
}
```

Check `??=` against the shell's TS target. If it is below ES2021, write the assignment out.

- [ ] **Step 5: The generated seeds**

In `packages.mjs`:

- **`renderProtocol`.** Replace the `KIND_DATA` constant with commits into `kindDataRegistry`, added to the M2e seed
  commit: one addition per contributing package, with items `{ kind, id, data }` in product order. Write an entry only
  when its data is non-empty, as `KIND_DATA` held only those. Import the registry with the same specifier helper the
  M2e seed uses. Drop the `import type { KindData }` the constant needed.
- **`kindBlocks`.** Keep the generated pairing checks (an item with one half and not the other), which stay
  build-time. Replace the duplicate-id loop with commits:
  - one `contributionKindsRegistry` addition per declaring package, `{ kind, contribution: <the kind's local>, data:
    <its data keys> }`;
  - one `kindItemsRegistry` addition per contributing package, `{ kind, id }` for each item of `KIND_SOURCES_<kind>`.

  Both go into one `commitContributions` call, before the pairing checks, so the registry's check now gives the
  duplicate messages. The check and accept lines read the data through the reader:

  ```js
      `const KIND_DATA_${decl.kind} = kindData(${JSON.stringify(decl.kind)});`,
  ```

  `renderPackages` imports `{ commitContributions }` (already imported), `{ contributionKindsRegistry,
  kindItemsRegistry, kindData }` from `src/contributions/kindRegistries.ts`, and stops importing `KIND_DATA`. The
  generated `packages.ts` still imports `./protocol` for the protocol readers' seed (M2e).

The messages the old loop threw, "the package X contributes the K item I twice" and "two packages contribute the K item
I: A and B", are the registry's now, word for word. The generator's own fixture test asserts one of them.

- [ ] **Step 6: The readers**

- `buildGraph.ts`: `const kindData = (kind: string): KindData => KIND_DATA[kind] ?? {};` becomes an import of
  `kindData` from `../contributions/kindRegistries`, passed as `PlaceContext.kindData`. Drop the generated import.
- `loadPacks.ts`'s `KIND_DATA` reads (the "contribution kind a pack cannot add to yet" refusal and the members check)
  become `contributionKindsRegistry.items().map((k) => k.kind)`. Task 7 replaces the refusal itself.
- `build-graph.test.ts`: `setControllerSyncData(KIND_DATA.controllerApps)` becomes
  `setControllerSyncData(kindData("controllerApps"))`, and the exact assertion on lsdj-launchpad's data reads
  `kindData("controllerApps")`.
- `pnpm typecheck` names any other reader. Fix each by the same substitution.

- [ ] **Step 7: The launchpad's `add`**

`packages/launchpad/ts/control/placer.ts` gains, beside `setControllerSyncData`:

```ts
/** Merge more items' data into the menu's copy: a pack's apps, after the product's (the kind's `add`). An id already
 *  held is never passed: the shell's registry refuses a taken id before `add` runs. */
export function addControllerSyncData(data: KindData): void {
  syncData = { ...syncData, ...data };
}
```

and `kind.ts`'s `controllerAppsKind` gains:

```ts
  add(items: readonly ControllerAppContribution[], data: KindData): void {
    for (const item of items) controllerApps.register(item);
    addControllerSyncData(data);
  },
```

Update `setControllerSyncData`'s comment ("Replaces it ...") to say it is `accept`'s, once.

- [ ] **Step 8: The generator's fixtures**

Run: `pnpm test packages/packages-generator`
Expected: FAIL on `expected/protocol.ts` and `expected/packages.ts`.
- Re-record them from `run-generator.mjs`'s output, as Task 2 of M2e did, and read each whole.
- The fixture tree's shell copy (`test/packages/fixtureAliases.mjs`'s `copyShellContributions`) also needs
  `src/contributions/kindRegistries.ts`.

Rerun: PASS.

- [ ] **Step 9: Run the tests**

Run each with `echo $?`:
- `pnpm test contributions/kinds-live`
- `pnpm test graph`
- `pnpm test packages`
- `pnpm test script`
- `pnpm test systems`
- `pnpm test launchpad` (or the launchpad package's own test slug: find it in `run-tests.mjs`)
- `pnpm typecheck`
- `pnpm check:boundaries`

Expected: every one exits 0.
- If `test/script/bundle-protocol.test.ts`'s module counts move (the planner's bundle now carries `kindRegistries.ts`),
  change the counts and say why in the report. The forbidden-module checks must pass unchanged.
- Then run the whole `pnpm test`.

- [ ] **Step 10: Commit**

```bash
git add packages/retroplug/src/contributions/kindRegistries.ts packages/retroplug/test/contributions/kinds-live.test.ts \
  packages/core/ts/defineKind.ts packages/launchpad/ts/control/kind.ts packages/launchpad/ts/control/placer.ts \
  packages/retroplug/scripts/lib/packages.mjs packages/retroplug/src/graph/buildGraph.ts \
  packages/retroplug/test/packages/fixtures/expected/protocol.ts packages/retroplug/test/packages/fixtures/expected/packages.ts
git commit -m "kinds: the declared kinds, the taken item ids and the items' data are live registries, and a kind takes a later commit's items through add"
```

Also stage, by name, every reader the typecheck sent you to, `fixtureAliases.mjs`, and `bundle-protocol.test.ts` if
its counts moved.

---

### Task 2: The built-in bundle's kind registrar

**Files:**
- Create: `packages/retroplug/test/script/kind-registrar.test.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs` (`renderScriptNodes`), `packages/retroplug/test/packages/fixtures/expected/scriptNodes.ts`

- [ ] **Step 1: Write the failing test**

`packages/retroplug/test/script/kind-registrar.test.ts`:

```ts
// The built-in script bundle's kind registrar (docs/downloadable-packs-m2f-plan.md): a pack's kinds half registers its
// items' runtimes through it, after the bundle has registered the product's own. It refuses a kind the product does not
// declare, items that are not the ids it was given, and an id the kind already holds, so a pack never shadows a
// built-in app.
import { test, expect } from "@retroplug/testing";
// The production bundle's entry point: importing it registers the product's apps and defines the registrar.
import "../../src/script/bundle";
import { controllerApp } from "@retroplug/launchpad/apps";

type Registrar = (kind: string, items: readonly { id: string }[], ids: readonly string[]) => void;
const register = (): Registrar => (globalThis as Record<string, unknown>).__rp_registerKindItems as Registrar;
const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};
const runtime = (id: string) => ({ id, factory: () => () => {}, playbackModel: () => ({}) as never, target: () => ({}) as never });

test("the registrar is defined, and a pack's app is found by the session's lookup after it", () => {
  expect(typeof register()).toBe("function");
  register()("controllerApps", [runtime("kr-app")], ["kr-app"]);
  expect(controllerApp("kr-app")?.id).toBe("kr-app");
});

test("it refuses an undeclared kind, ids that are not the items', and a taken id", () => {
  expect(refusal(() => register()("nope", [runtime("x")], ["x"]))).toBe(
    "a pack's kinds half registers items of nope, a contribution kind this product does not declare");
  expect(refusal(() => register()("controllerApps", [runtime("kr-b")], ["kr-c"]))).toBe(
    "a pack's kinds half registers the controllerApps items kr-b, and its manifest declares kr-c");
  expect(refusal(() => register()("controllerApps", [runtime("lsdj-midimap")], ["lsdj-midimap"]))).toBe(
    "a pack's kinds half registers the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds");
  expect(refusal(() => register()("controllerApps", [runtime("kr-app")], ["kr-app"]))).toBe(
    "a pack's kinds half registers the controllerApps item kr-app, which an earlier pack holds");
});
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `pnpm test script/kind-registrar`
Expected: FAIL: `__rp_registerKindItems` is not a function.

- [ ] **Step 3: Generate the registrar**

In `renderScriptNodes`, after each kind's `register<Kind>(kindItems_<kind>)` line and before `registerScriptNodes(nodes)`,
emit:

```js
  lines.push(
    "",
    "// A downloaded pack's kind items register their runtimes through this, after the product's own above",
    "// (docs/downloadable-packs-m2f-plan.md): its kinds half runs it in both of the built-in pack's contexts. It refuses",
    "// a kind this product does not declare, items that are not the ids the pack's manifest declares, and an id the",
    "// kind already holds, which the kind's own register would replace without a word.",
    "const KIND_REGISTRARS: Record<string, { register: (items: readonly any[]) => void; owners: Record<string, string> }> = {",
    ...kindDecls.map((decl) =>
      `  ${JSON.stringify(decl.kind)}: { register: ${decl.registerLocal}, owners: ${JSON.stringify(ownersOf(decl.kind))} },`),
    "};",
    "(globalThis as Record<string, unknown>).__rp_registerKindItems = (",
    "  kind: string, items: readonly { id: string }[], ids: readonly string[],",
    "): void => {",
    "  const entry = Object.prototype.hasOwnProperty.call(KIND_REGISTRARS, kind) ? KIND_REGISTRARS[kind] : undefined;",
    "  if (!entry) throw new Error(`a pack's kinds half registers items of ${kind}, a contribution kind this product does not declare`);",
    "  const got = items.map((item) => item.id);",
    "  if (got.join(\"\\n\") !== ids.join(\"\\n\")) {",
    "    throw new Error(`a pack's kinds half registers the ${kind} items ${got.join(\", \")}, and its manifest declares ${ids.join(\", \")}`);",
    "  }",
    "  for (const id of got) {",
    "    const owner = Object.prototype.hasOwnProperty.call(entry.owners, id) ? entry.owners[id] : undefined;",
    "    if (owner !== undefined) {",
    "      throw new Error(`a pack's kinds half registers the ${kind} item ${id}, which ${owner === \"\" ? \"an earlier pack\" : `this product's ${owner}`} holds`);",
    "    }",
    "  }",
    "  for (const id of got) entry.owners[id] = \"\";",
    "  entry.register(items);",
    "};",
  );
```

- `decl.registerLocal` is the local the generator already imports the kind's runtime `register` function under (e.g.
  `registerControllerApps`). Use the name the existing import line gives it.
- `ownersOf(kind)` is the product's items of that kind as `{ id: pkgName }`, from the entries the generator already
  has (`KIND_RUNTIMES_<kind>`'s source).
- A product with no kinds emits `const KIND_REGISTRARS = {}` and the same function, so every call is refused.

The strings above are TS template literals inside JS string literals: check the generated file reads as intended, and
compare it with the test's expected messages.

- [ ] **Step 4: The fixture and the tests**

- Run `pnpm test packages/packages-generator` and re-record `expected/scriptNodes.ts` (as in Task 1).
- Then run each, with `echo $?`:
  - `pnpm test script/kind-registrar`
  - `pnpm test script`
  - `pnpm test packages`
  - `pnpm typecheck`
- Also build the script bundle, `cmake --build build --target retroplug-script-bundle -j$(nproc)`, so the native
  embed compiles with it.

Expected: every one exits 0.

- [ ] **Step 5: Commit**

```bash
git add packages/retroplug/scripts/lib/packages.mjs packages/retroplug/test/packages/fixtures/expected/scriptNodes.ts \
  packages/retroplug/test/script/kind-registrar.test.ts
git commit -m "kinds: the built-in script bundle exposes a registrar a pack's kind items register their runtimes through"
```

---

### Task 3: pack.json's `kinds` member and half

**Files:**
- Modify: `packages/native/src/host/packs/PackManifest.{hpp,cpp}`, `packages/native/test/packs/PackManifest.test.cpp`

- [ ] **Step 1: Write the failing tests**

In `PackManifest.test.cpp`, beside M2e's protocol cases, add a `[packs-manifest]` case built the way those are (the
file's `protocolManifest`-style helper):

```cpp
TEST_CASE("pack.json's kinds member: each kind's items, held to their half", "[packs-manifest]") {
    // A manifest with a kinds half and the member:
    //   "halves": {"kinds": "js/kinds.js"},
    //   "kinds": {"controllerApps": [{"id": "pk-app", "syncOverride": {"node": "rp.x.sync"}}]}
    // parses, with m->kinds->kindsJson ==
    //   R"({"controllerApps":[{"id":"pk-app","syncOverride":{"node":"rp.x.sync"}}]})"
    // and m->kinds->ids == {{"controllerApps", {"pk-app"}}}.
    // Refusals (each reason after "pack.json: "):
    //   no member:                  "halves.kinds needs the kinds member (each kind's items)"
    //   member, no half:            "kinds is set, and halves names no kinds half"
    //   {}:                         "kinds names no kind: a kinds half registers items"
    //   {"controllerApps": []}:     "kinds.controllerApps is empty"
    //   an item with no id:         "kinds.controllerApps[0] has no \"id\""
    //   an id twice in one kind:    "kinds.controllerApps names pk-app twice"
    //   a kind that is not a list:  the reader's own refusal (a generic "not a manifest" one is fine)
    // And the old protocol.kinds now points at the member:
    //   "protocol.kinds: a pack's kind items are its top-level kinds member"
}
```

Write the case out in full in the file's style: one good manifest and a `refused(json, reason)` per line above.

- [ ] **Step 2: Run them to make sure they fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-manifest]"`
Expected: FAIL to compile (`PackManifest` has no `kinds`).

- [ ] **Step 3: The member**

`PackManifest.hpp`:

```cpp
// A kinds half's member (docs/downloadable-packs-m2f-plan.md): the items the pack contributes to contribution kinds
// built-in packages declare, by kind, each with its id and its data. The TS loader holds them to the declared kinds;
// the half itself (halves.kinds) is global code every Engine's built-in pack runs from construction.
struct KindsSpec {
    std::map<std::string, std::vector<std::string>> ids;  // kind -> its items' ids, in the manifest's order
    std::string                                     kindsJson;  // the member as canonical JSON, read by parseJson
};
```

and `std::optional<KindsSpec> kinds;  // present exactly when halves names a kinds half` in `PackManifest`.

`PackManifest.cpp`:
- Add `"kinds"` to `kHalves`.
- Add `std::optional<std::map<std::string, std::vector<rfl::Generic>>> kinds;` to `RawManifest`, with each item read as
  a `RawKindItem { std::optional<std::string> id; rfl::ExtraFields<rfl::Generic> extra; }`.
- Parse after the protocol member, the way the protocol nodes are parsed: the id rules and refusals above.
- Build `kindsJson` from a `parseJson` re-read of the manifest text, taking its `kinds` member, as M2e's `nodesJson`
  does (an integer above INT64_MAX stays a number).
- The protocol member's `kinds` refusal message becomes "protocol.kinds: a pack's kind items are its top-level kinds
  member".

- [ ] **Step 4: Run them**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-manifest]"`
and `"[packs-protocol]"` (`echo $?` unpiped).
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/PackManifest.hpp packages/native/src/host/packs/PackManifest.cpp \
  packages/native/test/packs/PackManifest.test.cpp
git commit -m "packs: pack.json's kinds member and the kinds half"
```

---

### Task 4: The pack table loads a kinds half

**Files:**
- Create: `packages/native/src/host/packs/PackKinds.hpp`, `packages/native/src/host/packs/KindsTrial.{hpp,cpp}`,
  `packages/native/test/packs/PackKinds.test.cpp`, `packages/native/test/packs/kinds-fixture/` (CMakeLists.txt and four variant folders)
- Modify: `packages/native/src/host/packs/{PackTable,ProcessPacks}.{hpp,cpp}`, `packages/native/src/host/js/PackLoaderCore.cpp`,
  `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`

- [ ] **Step 1: The fixture packs**

`packages/native/test/packs/kinds-fixture/CMakeLists.txt` stages hand-written variants at configure time, exactly as
`protocol-fixture/CMakeLists.txt` does: the same `RP_PACK_STAGED` claim, and `configure_file` of `pack.json.in` and
`js/kinds.js`. Add it to `tests.cmake` beside the protocol fixture. Its header lists the variants:

- `kindfix`:
  - Its kinds half registers one controller app, `kindfix-app`.
  - The app's `target(send)` sends `[0xB0, 0x7F, 0x2A]` (CC 127, value 42) once, when the session builds it, so a test
    driving the session with `target: "midiOut"` sees the runtime ran.
  - Its `factory` returns an app that does nothing.
  - Its `playbackModel` returns a model whose methods do nothing and whose position is idle.
- `kindfix-throws`: the bundle throws as it loads.
- `kindfix-taken`: registers `lsdj-midimap`, a built-in app's id.
- `kindfix-unknown`: registers items of a kind no package declares, `nope`.

`kindfix/pack.json.in`:

```json
{
  "id": "kindfix", "name": "Kinds fixture", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/packs/kinds-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": [], "roles": [] },
  "halves": { "kinds": "js/kinds.js" },
  "kinds": { "controllerApps": [{ "id": "kindfix-app" }] }
}
```

`kindfix/js/kinds.js`:

```js
// The kinds fixture's runtime half (docs/downloadable-packs-m2f-plan.md): global code, as tools/build-pack-halves.mjs
// writes a kinds half, registering one controller app through the built-in bundle's registrar. Its target sends CC 127
// = 42 once, when the session builds it, so a document driving the session out of the host sees this code ran.
(function () {
  var idle = function () { return { playing: false, channels: [] }; };
  var model = {
    position: idle, launch: function () {}, advance: function () {}, stop: function () {}, reset: function () {},
    setRowTicks: function () {}, anchorTo: function () {},
  };
  var app = {
    id: "kindfix-app",
    factory: function () { return function () {}; },
    playbackModel: function () { return model; },
    target: function (send) {
      send([0xb0, 0x7f, 0x2a]);
      return { maxRow: 0, launch: function () {}, release: function () {} };
    },
  };
  globalThis.__rp_registerKindItems("controllerApps", [app], ["kindfix-app"]);
})();
```

The model's members follow `ControllerPlaybackModel` / `PredictivePlaybackModel` (`packages/launchpad/ts/apps/index.ts`,
`packages/tracker/ts/playbackModel.ts`). Give it every member the session calls, with no-op bodies. If the session
reads a field this stub lacks, add it.

The variants are the same files with their own ids. `kindfix-taken`'s item and manifest entry are `lsdj-midimap`.
`kindfix-unknown`'s kind is `nope` in both. `kindfix-throws` runs `throw new Error("the kinds fixture throws");` before
it registers.

- [ ] **Step 2: Write the failing tests**

`packages/native/test/packs/PackKinds.test.cpp` (add it to `retroplug-packs-test`'s SOURCES with the comment `# the
kinds half: the table's trial, the pack list, Engines from construction`). Use `PackTestSupport.hpp`'s helpers, as
`PackProtocol.test.cpp` does (`fixture(id)`, `runBlock`, `MidiOut`), and a `withTrials(table)` that sets both trials:

```cpp
TEST_CASE("a kinds half is compiled and trial-loaded beside the built-in script nodes, its bytecode kept", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("kindfix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->kinds);
    CHECK_FALSE(load.pack->kinds->bytecode.empty());
}

TEST_CASE("a kinds half is refused when it throws, takes a built-in id or names an undeclared kind", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(fixture(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("kindfix-throws", "the kinds fixture throws");
    refused("kindfix-taken", "the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds");
    refused("kindfix-unknown", "a contribution kind this product does not declare");
}

TEST_CASE("a table with no kinds trial refuses a kinds half", "[packs-kinds]") {
    PackTable table;
    const auto load = table.load(fixture("kindfix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack kindfix has a kinds half, and this host loads none");
}

TEST_CASE("the pack list carries the kinds member, and pack:<id>/kinds is no module", "[packs-kinds]") {
    // As PackProtocol.test.cpp's pack-list case: a bare runtime over a table holding kindfix; __rp_packList()'s entry
    // contains "\"halves\":[\"kinds\"]" and "\"kinds\":{\"controllerApps\":[{\"id\":\"kindfix-app\"}]}", and
    // import("pack:kindfix/kinds") rejects.
}
```

Write the last case out against the same helpers `PackProtocol.test.cpp`'s pack-list case uses.

- [ ] **Step 3: Run them to make sure they fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL to compile.

- [ ] **Step 4: The trial and the table**

`packages/native/src/host/packs/PackKinds.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// A pack's kinds half (docs/downloadable-packs-m2f-plan.md): the runtimes of its contribution-kind items, global code
// compiled once by the table and run in every Engine's built-in pack after the embedded bundle, from construction.
namespace rp::host::packs {

struct KindsHalf {
    std::vector<std::uint8_t> bytecode;  // the global-code bundle, compiled
};

// Compile a kinds half's source and run it after the built-in script nodes in a scratch script host, as an Engine's
// construction will: its bytecode, or nothing with the reason in *why. The backend's (host/packs/KindsTrial.hpp).
using KindsTrial = std::function<std::optional<KindsHalf>(const std::string& source, std::string* why)>;

}  // namespace rp::host::packs
```

`KindsTrial.{hpp,cpp}` follows `ProtocolTrial.{hpp,cpp}`. Add it to `retroplug-backend` beside `ProtocolTrial.cpp`.

```cpp
KindsTrial kindsTrial() {
    return [](const std::string& source, std::string* why) -> std::optional<KindsHalf> {
        const auto fail = [&](std::string reason) -> std::optional<KindsHalf> {
            if (why) *why = std::move(reason);
            return std::nullopt;
        };
        std::string error;
        auto bytecode = dsp::compileToBytecode(source, ScriptHost::kStackBytes, &error);
        if (!bytecode) return fail("its kinds half does not compile: " + error);
        ScriptHost host;
        const ScriptHost::Chunk chunks[] = {ScriptHost::embeddedBundle(), ScriptHost::Chunk{*bytecode}};
        if (!host.load(chunks, &error)) return fail("its kinds half did not run beside the built-in script nodes: " + error);
        return KindsHalf{std::move(*bytecode)};
    };
}
```

If `ScriptHost::load`'s error for a throwing chunk does not carry the exception's message (the tests look for "the
kinds fixture throws" and the registrar's text), find where it formats the throw and make sure the message reaches
`error`. The protocol trial's "did not load" path shows the shape.

`PackTable`:
- `setKindsTrial(KindsTrial)` and a `kindsTrial_` member, as for the protocol trial.
- `LoadedPack::kinds`, a `std::optional<KindsHalf>`.
- In `loadLocked`, after the protocol half and before the script half:

```cpp
    // The kinds half: read, compiled and run after the built-in script nodes by the backend's trial, which refuses a
    // runtime that throws, registers an id a built-in package holds or names a kind the product does not declare. An id
    // an earlier pack holds is the TS loader's to refuse (kindItemsRegistry), and every Engine's registrar refuses it
    // too.
    if (manifest->kinds) {
        if (!kindsTrial_) return refuseHere(id, pack + " has a kinds half, and this host loads none");
        std::string source;
        if (!readTextFile(candidate.dir / fs::path(manifest->halves.at("kinds")), source, &why))
            return refuseHere(id, pack + ": " + why);
        auto half = kindsTrial_(source, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        loaded->kinds = std::move(*half);
    }
```

`ProcessPacks.cpp` sets the kinds trial beside the protocol trial.

`PackLoaderCore.cpp`:
- The module loaders refuse `pack:<id>/kinds` as they refuse the protocol half.
- `packList` appends `,"kinds":<kindsJson>` when the pack has the member.

- [ ] **Step 5: Run the tests**

Run, with `echo $?` unpiped:
- `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc)`
- `build/bin/retroplug-packs-test "[packs-kinds]"`
- the whole binary
- `cmake --build build -j$(nproc)`

Expected: PASS, and the full build exits 0.

- [ ] **Step 6: Commit**

```bash
git add packages/native/src/host/packs/PackKinds.hpp packages/native/src/host/packs/KindsTrial.hpp \
  packages/native/src/host/packs/KindsTrial.cpp packages/native/src/host/packs/PackTable.hpp \
  packages/native/src/host/packs/PackTable.cpp packages/native/src/host/packs/ProcessPacks.cpp \
  packages/native/src/host/js/PackLoaderCore.cpp packages/native/CMakeLists.txt packages/native/cmake/tests.cmake \
  packages/native/test/packs/PackKinds.test.cpp packages/native/test/packs/kinds-fixture
git commit -m "packs: the table compiles and trial-loads a kinds half beside the built-in script nodes"
```

---

### Task 5: Every Engine runs the kinds halves from construction

**Files:**
- Modify: `packages/native/src/host/engine/Engine.{hpp,cpp}`, `packages/native/test/packs/PackKinds.test.cpp`

- [ ] **Step 1: Write the failing tests**

In `PackKinds.test.cpp`:

```cpp
namespace {

// The session, out of the host: host MIDI out is what its app's target sends. No surface is wired, and the session
// builds its app on its first block whatever the link says.
const char* kSessionDocument =
    R"({"nodes":[{"id":"s","type":"rp.controller.session","settings":{"app":"kindfix-app","target":"midiOut",)"
    R"("systemId":1}},{"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
    R"("wires":[{"from":"s.midiOut","to":"out.in1"}]})";

}  // namespace

TEST_CASE("an Engine built after the load runs the pack's kind item's runtime", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    Engine e(48000.0, table);
    REQUIRE(e.setGraph(rp::host::test::graphRequest(kSessionDocument, {})).ok);
    CHECK(runBlock(e, {}) == std::vector<MidiOut>{{1, 0, {0xB0, 0x7F, 0x2A}}});
    CHECK(e.refusedPacks().empty());
}

TEST_CASE("an Engine built before the load refuses the pack by name, and its session finds no app", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    Engine early(48000.0, table);
    REQUIRE(table.load(fixture("kindfix")));
    REQUIRE(early.setGraph(rp::host::test::graphRequest(kSessionDocument, {})).ok);
    CHECK(early.refusedPacks() == std::vector<std::string>{"kindfix"});
    CHECK(runBlock(early, {}).empty());
}

TEST_CASE("an Engine on another script bundle loads no kinds half and refuses the pack", "[packs-kinds]") {
    PackTable table;
    withTrials(table);
    REQUIRE(table.load(fixture("kindfix")));
    // The test bundle the engine-graph tests use (rp_script_test_bundle), as their Engines are built.
    Engine e(48000.0, testScriptBundle(), table);
    CHECK(e.refusedPacks() == std::vector<std::string>{"kindfix"});
}
```

- Use the tree's names for the refused-packs accessor (M2e's refused-here test reads one) and for the session's
  settings fields (`controllerSession.ts`'s settings).
- `testScriptBundle()` is whatever the engine-graph tests build their Engines with: find it.
- If the session does not build its app before a link is connected, set `controllerConnected` the way
  `controller-session.test.ts` does, and say how.

- [ ] **Step 2: Run them to make sure they fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-kinds]"`
Expected: the first case FAILS: the session finds no app, and host MIDI out is empty.

- [ ] **Step 3: The construction**

In `Engine.cpp`'s constructor, where the built-in bundle loads (`graph_->script.load(scriptBundle, &why)`):

```cpp
    // The kinds halves of the packs loaded so far, after the bundle, in the table's order: a pack's contribution-kind
    // items' runtimes run in the built-in pack's contexts, where the declaring package's node looks them up
    // (docs/downloadable-packs-m2f-plan.md), and only now, before any plan, can code join those contexts. Only on the
    // embedded bundle, the one the table's trial ran them beside; a pack loaded after this engine was built is refused
    // here at its sync (syncPacks). A load that fails with them is made again without them, so no pack's runtime can
    // take the built-in script nodes down.
    std::vector<rp::host::ScriptHost::Chunk> chunks(scriptBundle.begin(), scriptBundle.end());
    std::vector<std::string> kindPacks;
    const bool embedded = scriptBundle.data() == embeddedScripts().data();
    if (embedded) {
        for (const auto* pack : packs.loadedFrom(0)) {
            if (!pack->kinds) continue;
            chunks.push_back(rp::host::ScriptHost::Chunk{pack->kinds->bytecode});
            kindPacks.push_back(pack->manifest.id);
        }
    }
    bool loaded = graph_->script.load(chunks, &why);
    if (!loaded && !kindPacks.empty()) {
        std::fprintf(stderr, "[engine] the script-node bundle did not load with the packs' kinds halves (%s); loading it "
                             "alone\n", why.c_str());
        kindPacks.clear();
        loaded = graph_->script.load(scriptBundle, &why);
    }
    if (!loaded) std::fprintf(stderr, ...);  // the existing message
    kindPacksAtBuild_ = std::move(kindPacks);
    kindsOnEmbedded_ = embedded;
```

- Check whether `ScriptHost::load` may be called again after it failed. If it may not, rebuild `graph_` (a fresh
  `Graph`, before anything is registered on it) and load again, and say which you did.
- Read the constructors' delegation: the one taking `scriptBundle` and `packs` is where this goes, and `packs` is
  `*packs_`.

In `syncPacks`, before the required-pack loop:

```cpp
        if (pack->kinds && std::find(kindPacksAtBuild_.begin(), kindPacksAtBuild_.end(), pack->manifest.id) ==
                               kindPacksAtBuild_.end()) {
            why = kindsOnEmbedded_ ? "its kind items reach an Engine built after it loaded; they take effect at the next launch"
                                   : "its kind items run on the product's script bundle, and this engine runs another";
            fits = false;
        }
```

(Folded into the existing `fits` / refusal path, so the pack lands in the refused list with the message.)

Add `kindPacksAtBuild_` (`std::vector<std::string>`) and `kindsOnEmbedded_` (`bool`) to `Engine.hpp`, beside
`packsSeen_`, with a comment.

- [ ] **Step 4: Run the tests**

Run, with `echo $?` unpiped:
- `cmake --build build --target retroplug-packs-test -j$(nproc)`
- `build/bin/retroplug-packs-test "[packs-kinds]"`
- the whole binary
- `pnpm test:plugin packs-test`
- `cmake --build build -j$(nproc)`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/engine/Engine.hpp packages/native/src/host/engine/Engine.cpp \
  packages/native/test/packs/PackKinds.test.cpp
git commit -m "engine: an Engine's built-in pack runs the loaded packs' kinds halves from construction"
```

---

### Task 6: Every host loads its launch packs before it builds an Engine

**Files:**
- Modify: `packages/native/plugin/PluginDSP.cpp`, `packages/native/cli/main.cpp`, `packages/native/sdl/main.cpp`,
  `packages/native/test/ui/UiHarness.{hpp,cpp}`

- [ ] **Step 1: The plugin**

In `PluginDSP.cpp`, the first data member, declared above `svc_` (which holds the Engine), loads the launch packs:

```cpp
    // The launch packs, loaded before the Engine below is built: a pack's kind items' runtimes join an Engine only at its
    // construction (Engine's constructor, docs/downloadable-packs-m2f-plan.md). First, so it is constructed first.
    struct LaunchPacksFirst {
        LaunchPacksFirst() { rp::host::packs::ensureLaunchPacks(); }
    } launchPacks_;
```

Keep `bootControlPlane`'s call: `ensureLaunchPacks` is once per process, so the second call is a no-op, and its
comment says why it is there. Check that nothing declared above `svc_` today would move below it.

- [ ] **Step 2: The CLI**

In `cli/main.cpp`, call `ensureLaunchPacks()` before `HostServices svc;` when the process runs the dispatcher (not a
session file: `runFile`, which loads no pack). Move the existing call's comment with it. If `runFile` is computed after
`svc`, compute it first, or test the same condition `runFile` is computed from.

- [ ] **Step 3: The SDL standalone and the UI harness**

Find where each builds its Engine:
- the SDL host: `AppState` and its services, before `bootControlPlane(a)` calls `ensureLaunchPacks`;
- the UI harness: `UiHarness`'s engine services, before its constructor body calls it.

Make the call come first, in the shape that fits each: a first member as in the plugin, or a call before the object is
built. Keep the later calls: they are no-ops.

- [ ] **Step 4: Check**

Run, with `echo $?` unpiped:
- `cmake --build build -j$(nproc)`
- `pnpm test:ui packs-ts-fixture`
- `pnpm test:cli-ts`
- `pnpm test:plugin`
- `pnpm sdl:smoke`

Expected: every one exits 0.

Each change is held by reading: a host's own tests do not observe the order. Task 9's UI case shows the harness offers
a pack's app, which needs only the TS side. So say in the report, for each host, which object holds its Engine and what
now runs before it.

- [ ] **Step 5: Commit**

```bash
git add packages/native/plugin/PluginDSP.cpp packages/native/cli/main.cpp packages/native/sdl/main.cpp \
  packages/native/test/ui/UiHarness.hpp packages/native/test/ui/UiHarness.cpp
git commit -m "hosts: every host loads its launch packs before it builds an Engine"
```

---

### Task 7: The TS loader commits a pack's kind items and calls the kind's `add`

**Files:**
- Create: `packages/retroplug/src/packs/kindsHalf.ts`
- Modify: `packages/retroplug/src/packs/{loadPacks,packHost}.ts`, `packages/retroplug/test/packs/load-packs.test.ts`

- [ ] **Step 1: Write the failing tests**

Append to `load-packs.test.ts` (ids of its own; the registries only grow):

```ts
import { controllerApp } from "@retroplug/launchpad/registry";
import { controllerSyncOverride } from "@retroplug/launchpad/control/placer";
import { kindData } from "../../src/contributions/kindRegistries";

/** A pack contributing one controller app: its app half's control half, and its manifest's kinds entry. */
function kindPack(id: string, over: { appIds?: string[]; kindIds?: string[]; data?: Record<string, unknown>;
                                      kind?: string } = {}) {
  const kind = over.kind ?? "controllerApps";
  const appIds = over.appIds ?? [`${id}-app`];
  const kindIds = over.kindIds ?? [`${id}-app`];
  const info = pack(id, {
    halves: ["app", "kinds"],
    kinds: { [kind]: kindIds.map((i) => ({ id: i, ...(over.data ?? {}) })) },
  });
  const modules = {
    [id]: { app: { [kind]: appIds.map((i) => ({ id: i, label: `App ${i}`, config: { parse: () => ({}) } })) } },
  };
  return { info, modules };
}

test("a pack's controller app reaches the menu's table, its data the planner's, and nothing imports the kinds half", async () => {
  const { info, modules } = kindPack("lk-a", { data: { syncOverride: { node: "rp.lsdj.sync", role: "lsdj-sync",
    mode: "midiMap" } } });
  const { host, refused, imported } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(packStatus().loaded["lk-a"]).toEqual(["app", "kinds"]);
  expect(imported.includes("lk-a/kinds")).toBe(false);
  expect(controllerApp("lk-a-app")?.label).toBe("App lk-a-app");
  expect(kindData("controllerApps")["lk-a-app"]).toEqual({ syncOverride: { node: "rp.lsdj.sync", role: "lsdj-sync",
    mode: "midiMap" } });
  // The kind's add merged the data into the menu's copy: the override the menu reads for the app.
  expect(controllerSyncOverride([{ id: 1, pipeline: [{ kind: "lsdj-sync", node: "rp.lsdj.sync", config: {} }] }] as never,
    { app: "lk-a-app", target: "system", systemId: 1, cartSync: true } as never)?.mode).toBe("midiMap");
});

test("a pack's kind items are held to the kind's rules", async () => {
  const unpaired = kindPack("lk-b", { kindIds: ["lk-b-other"] });
  const taken = kindPack("lk-c", { appIds: ["lsdj-midimap"], kindIds: ["lsdj-midimap"] });
  const badKey = kindPack("lk-d", { data: { colour: "red" } });
  const undeclared = kindPack("lk-e", { kind: "nope" });
  const badRule = kindPack("lk-f", { data: { syncOverride: { node: "rp.nobody.sync", role: "x", mode: "m" } } });
  const all = [unpaired, taken, badKey, undeclared, badRule];
  const { host, refused } = fakeHost(all.map((p) => p.info), Object.assign({}, ...all.map((p) => p.modules)));
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lk-b"]).toBe("its app half contributes the controllerApps item lk-b-app, which its pack.json's " +
    "kinds member does not declare");
  expect(refused["lk-c"]).toBe("two packages contribute the controllerApps item lsdj-midimap: lsdj-launchpad and lk-c");
  expect(refused["lk-d"]).toBe("its controllerApps item lk-d-app has \"colour\", which nothing reads (the kind's data " +
    "keys are syncOverride)");
  expect(refused["lk-e"]).toBe("it contributes to nope, a contribution kind no loaded package declares");
  expect(refused["lk-f"]).toContain("names \"rp.nobody.sync\"");
  for (const id of ["lk-b-app", "lk-d-app", "lk-e-app", "lk-f-app"]) expect(controllerApp(id)).toBe(undefined);
});
```

- Adjust the `controllerSyncOverride` call to its real signature and the `OverrideSystem` shape in `placer.ts`. The
  point is that the menu's copy of the data now holds the pack's rule.
- `toContain` may not exist in this harness: if not, use `expect(refused["lk-f"].includes(...)).toBe(true)`.

- [ ] **Step 2: Run them to make sure they fail**

Run: `pnpm test packs/load-packs`
Expected: FAIL: `PackInfo` has no `kinds`, and a pack contributing to a kind is refused.

- [ ] **Step 3: The host's type**

`packHost.ts`: `HalfKind` gains `"kinds"`, and `PackInfo` gains:

```ts
  /** pack.json's kinds member: each kind's items with their data, as written (docs/downloadable-packs-m2f-plan.md). The
   *  kinds half itself is global code every Engine's built-in pack runs; the loader commits this data and the app half's
   *  items. */
  readonly kinds?: Readonly<Record<string, readonly Readonly<Record<string, unknown>>[]>>;
```

The `HalfKind` comment names `kinds` as a data half beside `protocol`.

- [ ] **Step 4: The kinds half's additions and checks**

`packages/retroplug/src/packs/kindsHalf.ts`:

```ts
// A pack's contribution-kind items (docs/downloadable-packs-m2f-plan.md): its app half's control halves paired with its
// pack.json's kinds entries, held to the declaring kind's rules as the generator holds a package's (the pairing, the
// data keys, the kind's own check), as the live kind tables' additions. The kind's `add` runs once the commit stands
// (kindsAdded).
import type { Addition } from "../contributions/registry";
import { declaredKind, kindDataRegistry, kindItemsRegistry } from "../contributions/kindRegistries";
import { protocolNodes, roleNodes } from "../graph/protocolNodes";
import { isWired, type ProtocolNodeEntry } from "../graph/protocolSpec";
import type { PackInfo } from "./packHost";

/** One kind's items as a pack contributes them: the app half's items and the manifest's data, by id. */
export interface PackKindItems {
  readonly kind: string;
  readonly items: readonly { readonly id: string }[];
  readonly data: Readonly<Record<string, Readonly<Record<string, unknown>>>>;
}

/** The kinds the pack contributes to, checked; throws the first refusal. `app` is its app half's default export. */
export function packKindItems(pack: PackInfo, app: Record<string, unknown> | undefined): PackKindItems[] {
  const manifest = pack.kinds ?? {};
  const kinds = new Set<string>(Object.keys(manifest));
  if (app) for (const key of Object.keys(app)) if (declaredKind(key) || Object.prototype.hasOwnProperty.call(manifest, key)) kinds.add(key);
  const out: PackKindItems[] = [];
  for (const kind of kinds) {
    const decl = declaredKind(kind);
    if (!decl) throw new Error(`it contributes to ${kind}, a contribution kind no loaded package declares`);
    if (!decl.contribution.add) throw new Error(`it contributes to ${kind}, which takes no items from a pack`);
    const items = (app && Array.isArray(app[kind]) ? app[kind] : []) as { id: string }[];
    const entries = Object.prototype.hasOwnProperty.call(manifest, kind) ? manifest[kind] : [];
    const entryIds = entries.map((e) => e.id as string);
    for (const item of items) {
      if (!entryIds.includes(item.id)) {
        throw new Error(`its app half contributes the ${kind} item ${item.id}, which its pack.json's kinds member does ` +
          "not declare");
      }
    }
    for (const id of entryIds) {
      if (!items.some((item) => item.id === id)) {
        throw new Error(`its pack.json's kinds member declares the ${kind} item ${id}, which its app half does not ` +
          "contribute");
      }
    }
    const data: Record<string, Readonly<Record<string, unknown>>> = {};
    for (const entry of entries) {
      const own: Record<string, unknown> = {};
      for (const key of Object.keys(entry)) {
        if (key === "id") continue;
        if (!decl.data.includes(key)) {
          throw new Error(`its ${kind} item ${entry.id as string} has ${JSON.stringify(key)}, which nothing reads ` +
            `(the kind's data keys are ${decl.data.join(", ")})`);
        }
        own[key] = entry[key];
      }
      if (Object.keys(own).length) data[entry.id as string] = own;
    }
    // The kind's own check, against the live protocol tables and the pack's own protocol half, which commits with it.
    const ownNodes = (pack.protocol?.nodes ?? []) as ProtocolNodeEntry[];
    decl.contribution.check?.(items, data, {
      nodeTypes: [...Object.keys(protocolNodes()), ...ownNodes.filter(isWired).map((n) => n.type)],
      roleNodes: { ...roleNodes(), ...(pack.protocol?.roles ?? {}) },
    });
    out.push({ kind, items, data });
  }
  return out;
}

/** The additions a pack's checked kind items make: their ids and their data. */
export function kindAdditions(pkg: string, kinds: readonly PackKindItems[]): Addition<any>[] {
  return [
    { registry: kindItemsRegistry, pkg, items: kinds.flatMap((k) => k.items.map((item) => ({ kind: k.kind, id: item.id }))) },
    { registry: kindDataRegistry, pkg, items: kinds.flatMap((k) =>
      Object.entries(k.data).map(([id, data]) => ({ kind: k.kind, id, data }))) },
  ];
}

/** After the commit stood: each kind's `add` with the pack's items. A throw is reported, never raised: the commit has
 *  happened (a kind's check is where a refusal belongs). */
export function kindsAdded(pkg: string, kinds: readonly PackKindItems[]): void {
  for (const k of kinds) {
    try {
      declaredKind(k.kind)?.contribution.add?.(k.items, k.data);
    } catch (e) {
      console.warn(`[packs] the ${k.kind} kind's add threw for pack ${pkg}: ${e instanceof Error ? e.message : String(e)}`);
    }
  }
}
```

- [ ] **Step 5: The loader**

In `loadPacks.ts`:
- `DATA_HALVES` gains `"kinds"`. The import loop puts `pack.kinds` there for the half, so nothing imports it.
- `checkManifest` drops the "a contribution kind a pack cannot add to yet" refusal. The members check accepts a
  declared kind's name.
- In `loadUnguarded`, for a pack whose `todo` holds `kinds` or whose app half names a declared kind, call
  `packKindItems(pack, appExport)` after `checkManifest`. This is the staging, so a throw refuses the pack. Then append
  `kindAdditions(pack.id, kinds)` to the pack's additions.
- `Staged` gains `kinds: PackKindItems[]`.
- `accept(s)` calls `kindsAdded(s.pack.id, s.kinds)` after it records the halves. That covers both the batch commit
  and the one-at-a-time fallback.
- `APP_BUILDERS.kinds` is a builder returning `[]`. Its additions come from `kindAdditions`, which needs the app half
  as well, so the staging adds them. The builder exists so the half is in the control plane's set. Comment it so.

A pack in a context whose builders lack `kinds` (none today: the UI and the CLI spread `APP_BUILDERS`) commits no items.

- [ ] **Step 6: Run the tests**

Run each, with `echo $?`:
- `pnpm test packs`
- `pnpm test graph`
- `pnpm test contributions`
- `pnpm test ui`
- `pnpm test cli`
- `pnpm typecheck`
- `pnpm check:boundaries`
- the whole `pnpm test`

Expected: every one exits 0.

- [ ] **Step 7: Commit**

```bash
git add packages/retroplug/src/packs/kindsHalf.ts packages/retroplug/src/packs/loadPacks.ts \
  packages/retroplug/src/packs/packHost.ts packages/retroplug/test/packs/load-packs.test.ts
git commit -m "packs: the loader commits a pack's kind items, held to the kind's rules, and calls the kind's add"
```

---

### Task 8: Building a package's kind items as a pack's kinds half

**Files:**
- Create: `packages/native/test/packs/ts-fixture/kinds/` (`package.json`, `pack.json`, `ts/app.ts`, `ts/appRuntime.ts`)
- Modify: `tools/build-pack-halves.mjs`, `packages/retroplug/test/script/build-pack-halves.test.ts`,
  `packages/native/test/packs/ts-fixture/tsconfig.json`

- [ ] **Step 1: The fixture package**

`packages/native/test/packs/ts-fixture/kinds/package.json`:

```json
{
  "name": "@retroplug-test/ts-fixture-kinds",
  "private": true,
  "retroplug": {
    "app": "ts/app.ts",
    "protocol": {
      "kinds": { "controllerApps": [{ "id": "tsfix-app", "module": "ts/appRuntime.ts" }] }
    }
  }
}
```

`pack.json` (the template):

```json
{
  "id": "tsfix-kind", "name": "TS fixture pack (kinds)", "version": "1.0.0", "apiLevel": 0,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture/kinds",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": [], "roles": [] },
  "halves": {}
}
```

`ts/app.ts`:

```ts
// The kinds fixture pack's app half (docs/downloadable-packs-m2f-plan.md): one controller app's control half, which the
// launchpad's registry offers in its menu as "TS Fixture App".
import { definePackage } from "@retroplug/core/definePackage";
import type {} from "@retroplug/launchpad/packageContributions";

export default definePackage({
  controllerApps: [{ id: "tsfix-app", label: "TS Fixture App", config: { parse: () => ({}) } }],
});
```

`ts/appRuntime.ts`:

```ts
// The kinds fixture pack's controller app runtime (docs/downloadable-packs-m2f-plan.md): an app that does nothing, whose
// target sends CC 127 = 42 once when the session builds it, so a test driving the session out of the host sees the
// pack's runtime ran in the built-in script contexts.
import type { ControllerAppRuntime, ControllerPlaybackModel } from "@retroplug/launchpad/apps";

const model: ControllerPlaybackModel = {
  // Every member the interface asks for, each doing nothing; the position is idle.
} as unknown as ControllerPlaybackModel;

const runtime: ControllerAppRuntime = {
  id: "tsfix-app",
  factory: () => () => {},
  playbackModel: () => model,
  target: (send) => {
    send([0xb0, 0x7f, 0x2a]);
    return { maxRow: 0, launch: () => {}, release: () => {} };
  },
};

export default runtime;
```

Write `model` out with every member `ControllerPlaybackModel` declares, rather than leaning on the cast. Add the
tsconfig paths the fixture needs (`@retroplug/launchpad/*`, as M2e added `@retroplug/tracker/*`), and add `kinds/ts` to
its `include`.

- [ ] **Step 2: Write the failing tests**

In `build-pack-halves.test.ts`:

```ts
test("a package's kind items are built as a pack's kinds half and kinds member", async () => {
  // Builds packages/native/test/packs/ts-fixture/kinds; runs js/kinds.js in a vm context whose
  // __rp_registerKindItems records its calls; prints { halves, kinds, calls }.
  // Expected:
  //   halves: { app: "js/app.js", kinds: "js/kinds.js" }
  //   kinds: { controllerApps: [{ id: "tsfix-app" }] }
  //   calls: [{ kind: "controllerApps", ids: ["tsfix-app"], itemIds: ["tsfix-app"] }]
  //   and js/kinds.js has no import statement
});

test("lsdj-launchpad's real kind items build as a pack's, with their data and their runtime", async () => {
  // Builds packages/lsdj-launchpad with only ["app", "kinds"] from a temp template; the kinds member equals its
  // manifest's protocol.kinds with each entry's module dropped; js/kinds.js registers ["lsdj-midimap"] through the
  // recording registrar.
});

test("a kinds half importing zod or react is refused", async () => {
  // A scratch package whose runtime module imports zod: refused with
  //   "<importer> imports zod: a kinds half runs in the script contexts, which carry no schema or UI library"
});
```

Write the three cases out in full in the style of the file's M2e cases: a node script spawned with `runNode`, its JSON
printed and asserted.

- [ ] **Step 3: Run them to make sure they fail**

Run: `pnpm test script/build-pack-halves`
Expected: the three new cases FAIL.

- [ ] **Step 4: The tool**

In `tools/build-pack-halves.mjs`:

- **The header** gains a paragraph on the kinds half. A package's `retroplug.protocol.kinds` becomes:
  - pack.json's `kinds` member (each entry without its `module`);
  - `js/kinds.js`, a global-code bundle registering the runtimes through the built-in bundle's registrar.

  The top-of-`buildPackHalves` refusal of `protocol.kinds` goes.
- **`scriptRealmImports({ refuse })`.** This generalises M2e's `protocolImports`:
  - It resolves any `@retroplug/<pkg>[/<sub>]` through `packages/<pkg>/package.json`'s exports map: one directory per
    package, the folder name being the package's short name. Read the repo's `packages/*/package.json` names to map a
    name to its folder.
  - It refuses the specifiers in `refuse`.
  - The protocol half keeps calling it with core alone allowed, so a non-core import is still refused there with M2e's
    message.
  - The kinds half calls it with every `@retroplug` package allowed and `["zod", "react", "react/jsx-runtime",
    "lvgljs", "lvgljs-ui"]` refused, with the message in the test.
- **`buildKindsHalf({ packageDir, outDir, pack, kinds })`.**
  - For each kind, for each entry: import its module's default.
  - Then call `globalThis.__rp_registerKindItems(<kind>, [<the defaults>], [<the ids>])`.
  - Bundle as the protocol half is bundled (IIFE, es2020, `tsOptions({ alwaysStrict: true })`, `define`,
    `nodePaths`) into `js/kinds.js`.
  - Set `pack.kinds = { <kind>: entries.map(({ module: _m, ...rest }) => rest) }`.
  - Refuse an entry with no `id` or no `module`.
- **The halves loop** handles `["kinds", block.protocol?.kinds]` as it handles protocol, and `--only kinds` works.
  `delete pack.kinds;` at the start, beside `delete pack.protocol`.

- [ ] **Step 5: Run the tests**

Run each, with `echo $?`:
- `pnpm test script/build-pack-halves`
- `pnpm test script`
- `pnpm typecheck`
- `pnpm check:boundaries`

Expected: every one exits 0.

- [ ] **Step 6: Commit**

```bash
git add tools/build-pack-halves.mjs packages/retroplug/test/script/build-pack-halves.test.ts \
  packages/native/test/packs/ts-fixture/kinds packages/native/test/packs/ts-fixture/tsconfig.json
git commit -m "packs: the pack build writes a package's kind items as its kinds half and kinds member"
```

---

### Task 9: The kinds fixture pack end to end

**Files:**
- Modify: `packages/native/test/packs/ts-fixture/CMakeLists.txt`, `packages/native/cmake/tests.cmake` (if needed),
  `packages/native/test/packs/PackKinds.test.cpp`, `packages/retroplug/scripts/run-ui-tests.mjs`,
  `packages/retroplug/test-ui/packs-ts-fixture.test.ts`, `tools/run-cli-ts-tests.sh`

- [ ] **Step 1: Stage the fixture**

In `ts-fixture/CMakeLists.txt`, build `tsfix-kind` from `kinds/` as `tsfix-proto` is built from `protocol/`:
- outputs `pack.json`, `js/app.js` and `js/kinds.js`;
- the same DEPENDS, plus `packages/launchpad/package.json`, since the kinds half resolves through its exports map;
- `_tsfix_claim(tsfix-kind)`;
- the outputs added to `retroplug-ts-fixture-packs`' DEPENDS.

- [ ] **Step 2: The built fixture's runtime runs in an Engine**

In `PackKinds.test.cpp`:

```cpp
TEST_CASE("the pack build's kinds half runs in an Engine built after the load", "[packs-kinds]") {
    // tsfix-kind, built from its package by tools/build-pack-halves.mjs: what a real pack's kind items are.
    PackTable table;
    withTrials(table);
    const auto load = table.load(fixture("tsfix-kind"));
    INFO(load.error);
    REQUIRE(load);
    Engine e(48000.0, table);
    const char* doc =
        R"({"nodes":[{"id":"s","type":"rp.controller.session","settings":{"app":"tsfix-app","target":"midiOut",)"
        R"("systemId":1}},{"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
        R"("wires":[{"from":"s.midiOut","to":"out.in1"}]})";
    REQUIRE(e.setGraph(rp::host::test::graphRequest(doc, {})).ok);
    CHECK(runBlock(e, {}) == std::vector<MidiOut>{{1, 0, {0xB0, 0x7F, 0x2A}}});
}
```

Run: `pnpm configure && cmake --build build --target retroplug-ts-fixture-packs retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-kinds]"`
Expected: PASS.

- [ ] **Step 3: The UI offers the pack's app**

- `run-ui-tests.mjs`'s `PACK_TESTS["packs-ts-fixture"]` gains `"tsfix-kind"`.
- In `packs-ts-fixture.test.ts`, extend the case or add one:
  - after `ui.boot()`, `status().loaded["tsfix-kind"]` is `["app", "kinds"]`;
  - the Launchpad (controller) menu's app cycler offers "TS Fixture App".

Find the row the menu cycles apps on in `packages/launchpad/ts`'s UI contribution, and follow the existing UI tests that
open that menu (grep `test-ui/` for the controller menu's label). If the menu is shown only with a device, assert
instead that the launchpad registry lists the app:
- `controllerApps.list()` through whatever the UI's module set exposes, or a `__rp_*` status hook if one exists;
- say which you used.

Run: `pnpm test:ui packs-ts-fixture`
Expected: PASS.

- [ ] **Step 4: The CLI leg**

In `tools/run-cli-ts-tests.sh`, section 8:
- `PACKPATH` gains `$PACKS/tsfix-kind/1.0.0` and `$PACKS/kindfix-taken/1.0.0`.
- Add:

```bash
contains "packs: a kinds half claiming a built-in app is refused natively" "$TMP/packs-help.err" \
	"the controllerApps item lsdj-midimap, which this product's lsdj-launchpad holds"
not_contains "packs: the kinds fixture pack is refused nowhere" "$TMP/packs-help.err" "pack tsfix-kind"
```

Use the script's existing negative helper (read it: there may be one, or follow how the M2e legs grep for a line that
must not appear).

Run: `pnpm test:cli-ts`
Expected: exit 0, with the new lines `ok`.

- [ ] **Step 5: Commit**

```bash
git add packages/native/test/packs/ts-fixture/CMakeLists.txt packages/native/test/packs/PackKinds.test.cpp \
  packages/retroplug/scripts/run-ui-tests.mjs packages/retroplug/test-ui/packs-ts-fixture.test.ts tools/run-cli-ts-tests.sh
git commit -m "packs: the kinds fixture pack built, its runtime run in an Engine and its app offered in the UI"
```

---

### Task 10: The docs

**Files:**
- Modify: `spec/02-native-host.md`, `spec/04-roles-protocol-nodes.md`, `spec/06-build-test.md`, `spec/07-remaining-work.md`,
  `docs/downloadable-packs-loading-design.md`, `docs/downloadable-packs-m2f-plan.md` (an "After the reviews" section)

- [ ] **Step 1: The specs**

- **spec/02**, where M2e describes the protocol half:
  - the kinds half is compiled and trial-loaded once by the table (the backend's `kindsTrial`);
  - every Engine built on the embedded bundle runs the loaded packs' kinds halves in its built-in pack, from
    construction;
  - an Engine built before such a pack refuses it, and so does one on another bundle;
  - every host loads its launch packs before it builds an Engine.
- **spec/04**, where contribution kinds are described:
  - the three halves of a pack's item;
  - the registrar;
  - `add`;
  - the live kind tables (`kindData`, `declaredKind`).
- **spec/06**:
  - the build tool's kinds half (what it bundles and refuses);
  - the fixtures (`kinds-fixture/`, `ts-fixture/kinds/`);
  - `[packs-kinds]`;
  - the UI and CLI legs.
- **spec/07**:
  - Delete M2f from the Downloadable packs entry. What is left of part 1 is M3, M4, M5 and the Windows allocator.
  - Add an "Open from M2f" sentence listing this plan's design questions.
  - Remove the "third-party controller apps ... the kinds are M2d's" sentence. A pack's controller app now loads.
    Replace it with what is still missing, if anything: a pack declaring a kind.
- **The loading design.** Section 1's manifest gains the `kinds` member and half. Section 3's "Where each half loads"
  gains the kinds half.

- [ ] **Step 2: Check and commit**

Run: `pnpm check:docs` (exit 0).

```bash
git add spec/02-native-host.md spec/04-roles-protocol-nodes.md spec/06-build-test.md spec/07-remaining-work.md \
  docs/downloadable-packs-loading-design.md docs/downloadable-packs-m2f-plan.md
git commit -m "docs: a pack contributes items to the built-in contribution kinds"
```

---

### Task 11: Verify, then the CI gate

- [ ] **Step 1: The local gate**

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 TVL=<repo>/.superpowers/verify/runs/m2f/tv .superpowers/verify/tip-verify-par.sh`.
Then run these:
- `pnpm sdl:controller` on its own;
- `pnpm typecheck && pnpm check:boundaries && pnpm check:docs`;
- `.superpowers/verify/compare.sh .superpowers/verify/runs/m2e/tv .superpowers/verify/runs/m2f/tv`.

Expected:
- Every leg exits 0, except the known GBA app-cores failure.
- The comparison shows only additions.
- A leg that fails where M2e's passed is rerun alone before it is judged: M2e's run met two timing flakes, the
  Launchpad swap case and the N8 jobs case under TSan.

- [ ] **Step 2: The CI gate (needs the user's go-ahead to push)**

Ask the user to push `feature/modularize`. Then dispatch `build.yml` on it and read every job: linux, linux-arm64, macos,
and windows `/W4 /WX`.

Expected: all four jobs succeed.

---

## Design questions this plan found

1. **A pack's kind items installed live wait for the next launch** (decided with the user). M4's live install must say
   so in Settings > Packs, and its TS side must not offer an app whose runtime no Engine of the process has. Today every
   pack is a launch pack.
2. **Only the trial and the registrar hold a kinds half's ids to its manifest.** The registrar refuses items whose ids
   are not the ones the chunk passes. The chunk is the build tool's, and it passes the manifest's. But a hand-made chunk
   could pass any ids. As M2c's question 9 says of the module guard, this catches a pack's mistakes, not a pack set on
   going around it.
3. **A kinds half carries its own copies of the modules its runtime imports.** For example, lsdj-launchpad's copy of
   `@retroplug/lsdj/playback/predict`. They run in the built-in contexts beside the product's own copies of the same
   modules, so a pack built against an older lsdj runs that lsdj's predictor. The API level does not cover that
   behaviour (M2e's question 13).
4. **A pack declaring a kind stays refused** (decided). When one is needed, its registrar and its runtime would be a
   pack's chunk too, and a kind declared by one pack and filled by another needs their chunks ordered.
5. **The host order is held by reading.** No test observes that a host loaded its launch packs before it built its
   Engine. The Engine's refusal message is what a regression would show.

## Self-review

**Scope** (the user's decisions, 2026-11-25): a pack contributing to built-in kinds, with its runtime joining Engines at
construction (Tasks 1 to 9). Declaring a kind and live installs are left as design questions 1 and 4.

**Spec coverage** (spec/07's M2f item and the loading design's section 3):
- `accept` runs once and a later commit's items come through `add` (Task 1).
- The items' runtime halves reach the declaring package's script context (Tasks 2, 4, 5), on every host from launch
  (Task 6).
- The control halves and the data commit with the pack, held to the kind's rules (Task 7).
- The build tool, and a real package's kind items (Task 8).
- End to end in an Engine, the UI and the CLI (Task 9).
- Docs (Task 10), CI (Task 11).

**Placeholder scan.** Four test bodies are described by their expected output rather than given whole:
- the manifest case in Task 3;
- the pack-list case in Task 4;
- the three build-tool cases in Task 8;
- the UI case in Task 9.

Each names its exact expected values and the existing case whose shape it copies. Where a step leans on the tree (the
refused-packs accessor, the test bundle's name, the session's settings, the UI menu's row), it names where to look.

**Type and name consistency:**
- Task 1, used in Tasks 2 and 7: `ContributionKind.add`, `addControllerSyncData`, `contributionKindsRegistry`,
  `kindItemsRegistry`, `kindDataRegistry`, `DeclaredKind`, `KindItemId`, `KindItemData`, `kindData()`, `declaredKind()`.
- Task 2, called in Tasks 4, 5 and 8: `__rp_registerKindItems(kind, items, ids)`.
- Task 3, read in Task 4: `KindsSpec`, `PackManifest::kinds`, the `kinds` half.
- Task 4, used in Task 5: `KindsHalf`, `KindsTrial`, `kindsTrial()`, `PackTable::setKindsTrial`, `LoadedPack::kinds`,
  `kindfix` and its three variants.
- Task 5: `kindPacksAtBuild_`, `kindsOnEmbedded_`.
- Task 7: `PackInfo.kinds`, `PackKindItems`, `packKindItems`, `kindAdditions`, `kindsAdded`.
- Task 8, used in Task 9: `scriptRealmImports`, `buildKindsHalf`, `tsfix-kind`, `tsfix-app`.
