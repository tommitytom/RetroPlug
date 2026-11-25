# Downloadable packs M2e: a pack's protocol half. Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pack's `protocol` half brings QuickJS script nodes, their wiring and the role kinds placed as them. Its nodes
reach every Engine in the process, its wiring and roles join the live protocol tables `buildGraph` reads, and its
graph placer (a `placer` half) joins the live placer registry, so a role a pack adds is placed, planned and run as a
built-in package's is.

**Architecture:** The four frozen tables become two live registries (`protocolNodes`, `roleNodes`) that the generated
module seeds and the pack loader adds to. Both registries are checked at every commit by one shared checks module. The
native pack table compiles the protocol half's global-code bundle once, trial-loads it beside the built-in script nodes
to read its types and their descriptors, and every Engine loads it at `syncPacks` into its own script host as an
app-owned pack `rp.<id>.protocol`. The host numbers its types' span labels after the built-ins'. The TS loader commits
the manifest's protocol data (no import: the half is not a module) and the placer half into the control plane's
registries, the render worker's and the CLI's included, after holding each wired node's ports to its descriptor.

**Tech Stack:** C++20, QuickJS-ng (the script host's runtimes, `dsp::compileToBytecode`), M1's `retroplug-packs`,
M2a's live registries, M2c's pack loader and TS loader, M2d's render worker and bare loader, esbuild, Catch2 v3, the
shell's TS, CLI and generator suites.

---

## Where this sits

[docs/downloadable-packs-loading-design.md](downloadable-packs-loading-design.md) part 1 is built in milestones: M1
native packs, M2a live registries, M2b shared contract modules, M2c a pack's TS halves, M2d the device script half and
the render worker (each done, with its plan beside this one), then:

- **M2e (this plan, decided 2026-11-25):** the protocol half. Today `PROTOCOL_NODES`, `DECLARED_NODE_TYPES` and
  `ROLE_NODES` are frozen constants (`src/generated/protocol.ts`) that `buildGraph`, the stores and the kinds' checks
  read; a pack's QuickJS script nodes have no way into an Engine but `Engine::loadScriptPack`'s, which no host calls;
  and a pack's placer has no way into `placersRegistry`. `PackTable` refuses a `protocol` half by name.
- **M2f (next, decided with the user 2026-11-25):** the contribution kinds. `accept` runs once, and a kind item's
  runtime half (a controller app's) must run inside the built-in script contexts, where the session node runs; it can
  be added there only when an Engine is built, so a pack installed while the app runs gets its kind items at the next
  launch. Until M2f a pack contributing to a kind stays refused, by the loader as today and by the manifest's
  `protocol.kinds`.
- M3 placeholders, M4 archives, commands and Settings > Packs, M5 the real packages as packs, as before. Neither M5
  pack (Mesen, BlipToaster) has a protocol member; risa, smsggdj and lsdj do, which part 6 moves out.

**What exists.** A package's `retroplug.protocol` member (`packages/<pkg>/package.json`) names a module default-exporting
its node definitions (`DefinedNode[]`), each node's wiring (`midiIn`, `writes`, `launches`, `serialOut`, `midiOut`,
`capture`, `out`) and span label, the role kinds placed as its nodes, an optional `placer` module and the kind items it
contributes. `packages/retroplug/scripts/lib/packages.mjs` checks them at build time, writes the tables into
`src/generated/protocol.ts`, the placers into `src/generated/placers.ts` (seeding `placersRegistry`) and the node
modules into `src/generated/scriptNodes.ts`, which `product-bundles.cmake` bundles to a global-code IIFE every Engine's
`ScriptHost` runs in both of the built-in pack's contexts. `ScriptHost::loadPack` loads more such packs, each in a
runtime pair of its own, and gives each type a span label (`spanLabelFor`: the next free one, 16 up, per script host).
`test/graph/protocol-nodes.test.ts` holds each table entry's port names to its node module's descriptor.

**Execution model.** Sonnet implements each task from its text alone; Opus reviews each task's diff before the next one
starts. The code here was not compiled or run as written: where a step meets the tree differently (a helper's name, an
include, a type's exact field), the implementer follows the tree, keeps the step's intent, and reports the deviation.
Task 10 ends with a CI gate on Windows and macOS, which needs the user's go-ahead to push.

## Decisions this plan makes

- **The protocol half is global code, not a module.** `halves.protocol` names the script-node bundle: an IIFE that
  appends its definitions to `__rp_scriptNodes`, as the built-in bundle does. The script contexts have no module loader
  and are not the app's realm, so the bundle carries its own copy of what it imports, `@retroplug/core` alone; any other
  `@retroplug` import is refused when the pack is built. No context imports `pack:<id>/protocol`: both module loaders
  refuse it, as they refuse `native` and `script`.
- **The wiring is manifest data.** pack.json gains a `protocol` member, `{ "nodes": [...], "roles": {...} }`: the
  package manifest's `retroplug.protocol.nodes` (without `label`) and `.roles`, which `tools/build-pack-halves.mjs`
  copies. The native parser holds the shape it needs (each node's `type`, in `provides.nodeTypes`, once; each role's
  kind in `provides.roles` and its node among the nodes; no `label`; no `kinds`) and passes the nodes on as canonical
  JSON. The TS side holds the wiring itself. `protocol` without `halves.protocol`, or the reverse, is refused. The
  members are additive (a host before M2e refuses a protocol half anyway), so the API level does not move.
- **One compile, one trial, every Engine.** `PackTable` reads the bundle and hands it to a protocol trial the backend
  installs (`host/packs/ProtocolTrial.cpp`: `retroplug-packs` cannot reach the script host, which is the backend's).
  The trial compiles it to QuickJS bytecode, loads the built-in script nodes and then the pack into a scratch
  `ScriptHost`, registers both on a scratch `NodeRegistry` (so a clash with a built-in script node is refused), and
  returns the bytecode, the type ids in registration order and each type's descriptor. The table refuses a pack whose
  bundle defines a type its `protocol.nodes` does not declare, or declares one it does not define. Every Engine then
  loads the bytecode at `syncPacks` with `ScriptHost::loadAppPack` under `rp.<id>.protocol`. That is an id under
  `rp.`, which `loadScriptPack` refuses to a caller, so no RPC can reload or shadow it.
- **Span labels are the host's, given at load.** A pack's protocol nodes carry no `label`. Each script host numbers
  the types it is handed after the built-ins' (16 to 23 on the desktop), in the table's pack order, so an Engine that
  refuses no pack gives each type the same label as every other.
- **The tables are live registries.** `src/contributions/protocolRegistries.ts` holds `protocolNodesRegistry` (every
  declared node, wired or not: one `ProtocolNodeEntry` each) and `roleNodesRegistry` (`{ kind, node }`), seeded by the
  generated `protocol.ts` in span-label order. `graph/protocolNodes.ts` reads them through `protocolNodes()`,
  `roleNodes()`, `declaredNodeTypes()` and `protocolSpecOf(type)`, rebuilt after each commit (`derived`).
  `ProtocolNodeType` becomes `string`. `KIND_DATA` stays a generated constant: it is the kinds' data, M2f's.
- **One set of protocol checks, at every commit.** `src/graph/protocolSpec.ts` holds `ProtocolNodeSpec` (moved from
  `protocolNodes.ts`) and the rules each registry's check runs over its whole staged list: an entry's shape, one owner
  per node type and per role kind, the capture rules, an `out`'s rules, a role placed as a wired node. The product's
  own tables pass through them at the app's load as a pack's do at its commit. The generator keeps its build-time
  checks, as M2a kept the manifest checks there. `portsOf` moves there too: the loader holds each wired node a pack
  declares to its descriptor's ports, and `protocol-nodes.test.ts` holds the built-ins' through the same function.
- **The placer is a half of its own.** `halves.placer` is an ES module (built from `retroplug.protocol.placer`) whose
  default export is a `GraphPlacer`. The control plane's builders commit it to `placersRegistry`, so the render worker
  and the CLI, which spread those builders, place its nodes too. `appRegistries.ts`'s check already holds a placer's key
  to a project section's.
- **The protocol data commits with the pack.** `protocol` is a data half: the loader takes it from `PackInfo` rather
  than importing it, and `APP_BUILDERS.protocol` turns it into additions to the two registries, all or nothing with the
  pack's other halves.

## Commands used throughout

- The TS suite: `pnpm test` or `pnpm test <slug>` (a file's path under `test/` without `.test.ts`, or a directory; a
  slug that matches nothing exits 1 with "no tests match").
- The native suite: `pnpm test:native [slug]` (test-native/, on `retroplug-host`). The CLI: `pnpm test:cli-ts`. The
  plugin binaries: `pnpm test:plugin [binary]`, e.g. `pnpm test:plugin packs-test`.
- One binary: `cmake --build build --target <t> -j$(nproc)`, then e.g. `build/bin/retroplug-packs-test "[packs-protocol]"`.
  After a CMake change: `pnpm configure`. A UI change to the standalone needs the bare `cmake --build build -j$(nproc)`.
  Never `rm -rf build`.
- `pnpm typecheck`, `pnpm check:boundaries`, `pnpm check:docs`.
- `deps/sameboy` shows as modified after a configure: expected; never stage it. Generated modules and `build/` are
  gitignored outputs. Stage files by name.

## File map

Created:

| File | Responsibility |
|---|---|
| `packages/retroplug/src/graph/protocolSpec.ts` | `ProtocolNodeSpec`, `ProtocolNodeEntry`, `RoleNodeEntry`, the protocol checks, `portsOf`, `checkProtocolPorts` |
| `packages/retroplug/src/contributions/protocolRegistries.ts` | `protocolNodesRegistry`, `roleNodesRegistry` |
| `packages/retroplug/src/packs/protocolHalf.ts` | `protocolAdditions`: a pack's protocol data as the two registries' additions, its ports checked |
| `packages/retroplug/test/graph/protocol-spec.test.ts` | each check's refusal |
| `packages/retroplug/test/graph/protocol-live.test.ts` | a commit to the live tables, its readers, its refusals |
| `packages/native/src/host/packs/PackProtocol.hpp` | `ProtocolHalf`, `ProtocolTrial` |
| `packages/native/src/host/packs/ProtocolTrial.{hpp,cpp}` | `protocolTrial()`: the backend's compile and trial load |
| `packages/native/test/packs/PackProtocol.test.cpp` | `[packs-protocol]`: the table, the pack list, every Engine, the built fixture |
| `packages/native/test/packs/protocol-fixture/` | `protofix` and its variants, hand-written, with their CMake |
| `packages/native/test/packs/ts-fixture/protocol/` | `tsfix-proto`: a package with app, protocol and placer members, built by the tool |
| `packages/native/test/packs/ts-fixture/variants/tsfix-proto-ports/` | a protocol node whose ports are not its wiring's |

Modified: `packages/retroplug/src/graph/{protocolNodes,buildGraph,serialCapture}.ts`,
`packages/retroplug/scripts/lib/packages.mjs` (`renderProtocol`, `kindBlocks`, `renderPackages`),
`packages/retroplug/test/packages/fixtures/expected/{protocol,packages}.ts`,
`packages/retroplug/test/graph/protocol-nodes.test.ts`, `packages/retroplug/test/systems/store-graph.test.ts`,
`packages/retroplug/test/source-literals/no-cart-literals.test.ts`, `packages/retroplug/src/packs/{loadPacks,packHost}.ts`,
`packages/retroplug/test/packs/load-packs.test.ts`, `packages/native/src/host/packs/{PackManifest,PackTable,ProcessPacks}.{hpp,cpp}`,
`packages/native/src/host/script/ScriptHost.{hpp,cpp}` (`loadAppPack`), `packages/native/src/host/engine/Engine.cpp`
(`syncPacks`), `packages/native/src/host/js/PackLoaderCore.cpp`, `packages/native/CMakeLists.txt` (a backend source),
`packages/native/cmake/tests.cmake`, `packages/native/test/packs/{PackManifest,PackLoader}.test.cpp`,
`packages/native/test/packs/fixture/` (one manifest's comment), `packages/native/test/packs/ts-fixture/{CMakeLists.txt,tsconfig.json}`,
`tools/build-pack-halves.mjs`, `packages/retroplug/test/script/build-pack-halves.test.ts`, `tools/run-cli-ts-tests.sh`,
and the docs (spec/02, spec/03 or wherever the live registries are described, spec/06, spec/07, spec/08, the loading
design's sections 1 and 3).

---

### Task 1: The protocol checks module

**Files:**
- Create: `packages/retroplug/src/graph/protocolSpec.ts`, `packages/retroplug/test/graph/protocol-spec.test.ts`
- Modify: `packages/retroplug/src/graph/protocolNodes.ts` (the spec types move out), `packages/retroplug/test/graph/protocol-nodes.test.ts`

- [ ] **Step 1: Write the failing test**

`packages/retroplug/test/graph/protocol-spec.test.ts`:

```ts
// The protocol checks (docs/downloadable-packs-m2e-plan.md): what each live protocol table refuses, at every commit, so
// a pack's protocol half is held at its load to what the product's own packages are.
import { test, expect } from "@retroplug/testing";
import {
  checkProtocolEntry,
  checkProtocolPorts,
  checkProtocolTable,
  checkRoleNodes,
  portsOf,
  type ProtocolNodeEntry,
  type RoleNodeEntry,
} from "../../src/graph/protocolSpec";

const node = (pkg: string, item: ProtocolNodeEntry) => ({ pkg, item });
const role = (pkg: string, item: RoleNodeEntry) => ({ pkg, item });
const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};

const SYNC = { type: "rp.x.sync", midiIn: "midi", writes: ["bytes"] } as ProtocolNodeEntry;
const DECODE = { type: "rp.x.decode", midiIn: null, writes: [], serialOut: true, midiOut: true } as ProtocolNodeEntry;
const CAPTURER = { type: "rp.x.cap", midiIn: "midi", writes: ["serialIn"], capture: { drive: "drive" }, out: "rp.x.decode" } as ProtocolNodeEntry;
// The two without a member: a key present with the value undefined is a key set, which the shape check refuses.
const { out: _out, ...CAPTURER_NO_OUT } = CAPTURER;
const { serialOut: _serialOut, ...DECODE_NO_SERIAL_OUT } = DECODE;

test("an entry's shape: a type, the known keys, the wiring's types, both midiIn and writes or neither", () => {
  expect(refusal(() => checkProtocolEntry("p", { midiIn: null }))).toBe('p: every protocol node needs a "type", the node\'s id');
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", label: 16 }))).toBe(
    'p: the node rp.p.n has a "label": a span label is the host\'s to give, at load');
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", launchs: true }))).toBe(
    'p: the node rp.p.n has "launchs", which no protocol node reads (a node\'s keys are type, midiIn, writes, launches, ' +
      "serialOut, midiOut, capture, out)");
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiIn: "midi" }))).toBe(
    "p: the node rp.p.n declares midiIn without writes: a wired node declares both (an unwired one neither)");
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiIn: 3, writes: [] }))).toBe(
    "p: the node rp.p.n's midiIn must be an input's name or null, not 3");
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiIn: null, writes: ["audio"] }))).toBe(
    'p: the node rp.p.n writes "audio", which is no system input (serialIn, midi, bytes, buttons)');
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiIn: null, writes: [], midiOut: 1 }))).toBe(
    "p: the node rp.p.n's midiOut must be true when it is set, not 1");
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiIn: null, writes: [], capture: { a: "on" } }))).toBe(
    'p: the node rp.p.n\'s capture for "a" is "on", not "off", "listen" or "drive"');
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.n", midiOut: true }))).toBe(
    "p: the node rp.p.n has midiOut and is not wired (no midiIn and writes), so nothing reads it");
  expect(refusal(() => checkProtocolEntry("p", SYNC))).toBe("");
  expect(refusal(() => checkProtocolEntry("p", { type: "rp.p.unwired" }))).toBe("");
});

test("the table: one owner per type, captures decoded by a serial-out out, an out that only decodes", () => {
  expect(refusal(() => checkProtocolTable([node("a", SYNC), node("b", SYNC)]))).toBe(
    "two packages declare the script node rp.x.sync: a and b");
  expect(refusal(() => checkProtocolTable([node("a", SYNC), node("a", SYNC)]))).toBe(
    "the package a declares the script node rp.x.sync twice");
  expect(refusal(() => checkProtocolTable([node("a", CAPTURER_NO_OUT as ProtocolNodeEntry)]))).toBe(
    "a: the node rp.x.cap declares a capture and no out: a capture is for the serial-out node that decodes the bytes");
  expect(refusal(() => checkProtocolTable([node("a", CAPTURER)]))).toBe(
    "a: the node rp.x.cap declares a capture decoded at rp.x.decode, which no package declares");
  expect(refusal(() => checkProtocolTable([node("a", CAPTURER), node("a", DECODE_NO_SERIAL_OUT as ProtocolNodeEntry)]))).toBe(
    "a: the node rp.x.cap declares a capture decoded at rp.x.decode, which declares no serialOut");
  expect(refusal(() => checkProtocolTable([node("a", { ...SYNC, out: "rp.x.sync2" }),
    node("a", { type: "rp.x.sync2", midiIn: "midi", writes: [] })]))).toBe(
    "a: the node rp.x.sync's out rp.x.sync2 must be a wired node that writes no system input and reads neither " +
      "routed MIDI nor launches");
  expect(refusal(() => checkProtocolTable([node("a", DECODE)]))).toBe(
    "a: the node rp.x.decode reads its system's serialOut and is no node's out, so nothing would wire it");
  expect(refusal(() => checkProtocolTable([node("a", CAPTURER), node("a", DECODE), node("b", SYNC)]))).toBe("");
});

test("the roles: one owner per kind, each placed as a declared, wired node", () => {
  const nodes = [node("a", SYNC), node("a", { type: "rp.x.idle" })];
  expect(refusal(() => checkRoleNodes([role("a", { kind: "k", node: "rp.x.sync" }), role("b", { kind: "k", node: "rp.x.sync" })], nodes)))
    .toBe("two packages declare the role kind k: a and b");
  expect(refusal(() => checkRoleNodes([role("a", { kind: "k", node: "rp.x.gone" })], nodes))).toBe(
    "a: the role k names rp.x.gone, which no package declares");
  expect(refusal(() => checkRoleNodes([role("a", { kind: "k", node: "rp.x.idle" })], nodes))).toBe(
    "a: the role k names rp.x.idle, which declares no midiIn and writes to wire it");
  expect(refusal(() => checkRoleNodes([role("a", { kind: "k", node: "rp.x.sync" })], nodes))).toBe("");
});

test("a node's ports against its wiring", () => {
  expect(portsOf({ midiIn: "in", writes: ["bytes"], midiOut: true })).toEqual(
    ["bytes out bytes", "in in events", "midiOut out events"]);
  const spec = { midiIn: "in", writes: [], midiOut: true } as const;
  expect(refusal(() => checkProtocolPorts("rp.p.e", spec, [
    { id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }]))).toBe("");
  expect(refusal(() => checkProtocolPorts("rp.p.e", spec, [
    { id: "midi", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }]))).toBe(
    "its protocol node rp.p.e has the ports midi in events, midiOut out events, and its pack.json's protocol.nodes " +
      "wires in in events, midiOut out events");
});
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `pnpm test graph/protocol-spec`
Expected: FAIL, the module `../../src/graph/protocolSpec` cannot be resolved.

- [ ] **Step 3: Write the module**

`packages/retroplug/src/graph/protocolSpec.ts`. Move `SystemInput` and `ProtocolNodeSpec` (with their comments,
unchanged) here from `protocolNodes.ts`, and add the rest:

```ts
// The protocol node contract and its checks (docs/downloadable-packs-m2e-plan.md): how buildGraph wires a protocol node
// (ProtocolNodeSpec), and the rules the live protocol tables (src/contributions/protocolRegistries.ts) hold every commit
// to, so a pack's protocol half meets at its load what the product's own packages meet at the app's. The generator
// keeps its build-time checks of the manifests (scripts/lib/packages.mjs); these judge the tables as they stand. Imports
// nothing but types, so the planner's bundle carries it with no node code and no schema.
import type { PortDesc } from "@retroplug/core/descriptor";
import type { SerialOutCaptureMode } from "./serialCapture";

// SystemInput and ProtocolNodeSpec: moved here verbatim from protocolNodes.ts, comments included.

/** Every system input, in the order a refusal lists them. */
export const SYSTEM_INPUTS: readonly SystemInput[] = ["serialIn", "midi", "bytes", "buttons"];

/** One protocol node as a manifest declares it: its type and, when it is wired, how. What the live table holds. */
export type ProtocolNodeEntry = { readonly type: string } & Partial<ProtocolNodeSpec>;

/** A role kind and the node type it is placed as. */
export interface RoleNodeEntry {
  readonly kind: string;
  readonly node: string;
}

/** An entry with the package that contributed it, as a registry's check is handed it. */
interface Owned<T> {
  readonly pkg: string;
  readonly item: T;
}

const NODE_KEYS = ["type", "midiIn", "writes", "launches", "serialOut", "midiOut", "capture", "out"];
const FLAGS = ["launches", "serialOut", "midiOut"] as const;
const CAPTURES = ["off", "listen", "drive"];

const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

/** Whether buildGraph wires the node: a role may be placed as it. */
export function isWired(entry: ProtocolNodeEntry): boolean {
  return entry.midiIn !== undefined || entry.writes !== undefined;
}

/** One entry's shape, which throws naming the package and the node. */
export function checkProtocolEntry(pkg: string, raw: unknown): void {
  if (raw === null || typeof raw !== "object" || Array.isArray(raw) || typeof (raw as { type?: unknown }).type !== "string" ||
      (raw as { type: string }).type === "") {
    throw new Error(`${pkg}: every protocol node needs a "type", the node's id`);
  }
  const e = raw as Record<string, unknown> & { type: string };
  const at = `${pkg}: the node ${e.type}`;
  for (const key of Object.keys(e)) {
    if (key === "label") throw new Error(`${at} has a "label": a span label is the host's to give, at load`);
    if (!NODE_KEYS.includes(key)) {
      throw new Error(`${at} has ${JSON.stringify(key)}, which no protocol node reads (a node's keys are ` +
        `${NODE_KEYS.join(", ")})`);
    }
  }
  const midiIn = has(e, "midiIn");
  const writes = has(e, "writes");
  if (midiIn !== writes) {
    throw new Error(`${at} declares ${midiIn ? "midiIn" : "writes"} without ${midiIn ? "writes" : "midiIn"}: a wired ` +
      "node declares both (an unwired one neither)");
  }
  if (!midiIn) {
    for (const key of [...FLAGS, "out"]) {
      if (has(e, key)) throw new Error(`${at} has ${key} and is not wired (no midiIn and writes), so nothing reads it`);
    }
  } else {
    if (e.midiIn !== null && (typeof e.midiIn !== "string" || e.midiIn === "")) {
      throw new Error(`${at}'s midiIn must be an input's name or null, not ${JSON.stringify(e.midiIn)}`);
    }
    if (!Array.isArray(e.writes)) throw new Error(`${at}'s writes is not a list`);
    for (const input of e.writes) {
      if (!SYSTEM_INPUTS.includes(input as SystemInput)) {
        throw new Error(`${at} writes ${JSON.stringify(input)}, which is no system input (${SYSTEM_INPUTS.join(", ")})`);
      }
    }
    for (const flag of FLAGS) {
      if (has(e, flag) && e[flag] !== true) {
        throw new Error(`${at}'s ${flag} must be true when it is set, not ${JSON.stringify(e[flag])}`);
      }
    }
    if (has(e, "out") && (typeof e.out !== "string" || e.out === "")) {
      throw new Error(`${at}'s out must name a node type, not ${JSON.stringify(e.out)}`);
    }
  }
  if (has(e, "capture")) {
    // A capture on an unwired node keeps the generator's message: nothing would decode the bytes it clocked.
    if (!midiIn) {
      throw new Error(`${at} declares a capture and is not wired (no midiIn and writes), so nothing would decode ` +
        "the bytes it clocked");
    }
    const capture = e.capture;
    if (capture === null || typeof capture !== "object" || Array.isArray(capture)) {
      throw new Error(`${at}'s capture is not an object of mode: capture`);
    }
    for (const [mode, value] of Object.entries(capture)) {
      if (!CAPTURES.includes(value as string)) {
        throw new Error(`${at}'s capture for ${JSON.stringify(mode)} is ${JSON.stringify(value)}, not "off", "listen" ` +
          'or "drive"');
      }
    }
  }
}

/** The whole node table, as a registry's check is handed it: each entry's shape, one owner per type, each capture
 *  decoded by a serial-out node its entry names as `out`, and each `out` a node placed after its system that only
 *  decodes (it writes no system input and reads neither routed MIDI nor launches), as buildGraph wires one. */
export function checkProtocolTable(entries: readonly Owned<ProtocolNodeEntry>[]): void {
  const byType = new Map<string, Owned<ProtocolNodeEntry>>();
  for (const entry of entries) {
    checkProtocolEntry(entry.pkg, entry.item);
    const at = byType.get(entry.item.type);
    if (at) {
      throw new Error(at.pkg === entry.pkg
        ? `the package ${entry.pkg} declares the script node ${entry.item.type} twice`
        : `two packages declare the script node ${entry.item.type}: ${at.pkg} and ${entry.pkg}`);
    }
    byType.set(entry.item.type, entry);
  }
  const outs = new Set<string>();
  for (const { pkg, item } of entries) {
    if (item.out === undefined) continue;
    outs.add(item.out);
    const decoder = byType.get(item.out)?.item;
    if (item.capture !== undefined) {
      if (!decoder) {
        throw new Error(`${pkg}: the node ${item.type} declares a capture decoded at ${item.out}, which no package ` +
          "declares");
      }
      if (decoder.serialOut !== true) {
        throw new Error(`${pkg}: the node ${item.type} declares a capture decoded at ${item.out}, which declares no ` +
          "serialOut");
      }
    }
    if (!decoder || !isWired(decoder) || (decoder.writes ?? []).length > 0 || decoder.midiIn !== null ||
        decoder.launches !== undefined) {
      throw new Error(`${pkg}: the node ${item.type}'s out ${item.out} must be a wired node that writes no system ` +
        "input and reads neither routed MIDI nor launches");
    }
  }
  for (const { pkg, item } of entries) {
    if (item.capture !== undefined && item.out === undefined) {
      throw new Error(`${pkg}: the node ${item.type} declares a capture and no out: a capture is for the serial-out ` +
        "node that decodes the bytes");
    }
    if (item.serialOut === true && !outs.has(item.type)) {
      throw new Error(`${pkg}: the node ${item.type} reads its system's serialOut and is no node's out, so nothing ` +
        "would wire it");
    }
  }
}

/** The role table against the node table: one owner per kind, each placed as a declared node buildGraph wires. */
export function checkRoleNodes(roles: readonly Owned<RoleNodeEntry>[], nodes: readonly Owned<ProtocolNodeEntry>[]): void {
  const owners = new Map<string, string>();
  for (const { pkg, item } of roles) {
    if (typeof item.kind !== "string" || item.kind === "" || typeof item.node !== "string" || item.node === "") {
      throw new Error(`${pkg}: a role's kind and node must be non-empty strings`);
    }
    const at = owners.get(item.kind);
    if (at !== undefined) throw new Error(`two packages declare the role kind ${item.kind}: ${at} and ${pkg}`);
    owners.set(item.kind, pkg);
    const declared = nodes.find((n) => n.item.type === item.node)?.item;
    if (!declared) throw new Error(`${pkg}: the role ${item.kind} names ${item.node}, which no package declares`);
    if (!isWired(declared)) {
      throw new Error(`${pkg}: the role ${item.kind} names ${item.node}, which declares no midiIn and writes to wire it`);
    }
  }
}

/** A port as buildGraph wires it: "<id> <dir> <kind>". */
export const portKey = (p: Pick<PortDesc, "id" | "dir" | "kind">): string => `${p.id} ${p.dir} ${p.kind}`;

/** The ports a wired node's spec says it has, as buildGraph wires each, sorted. A system input a node writes is an
 *  output of the kind the system node takes it as: serial bytes into a Game Boy, core bytes into a NES or an SMS, the
 *  rest events. */
export function portsOf(spec: ProtocolNodeSpec): string[] {
  const out: string[] = [];
  if (spec.midiIn !== null) out.push(portKey({ id: spec.midiIn, dir: "in", kind: "events" }));
  if (spec.launches) out.push(portKey({ id: "launches", dir: "in", kind: "events" }));
  if (spec.serialOut) out.push(portKey({ id: "serialOut", dir: "in", kind: "bytes" }));
  for (const input of spec.writes)
    out.push(portKey({ id: input, dir: "out", kind: input === "serialIn" || input === "bytes" ? "bytes" : "events" }));
  if (spec.midiOut) out.push(portKey({ id: "midiOut", dir: "out", kind: "events" }));
  return out.sort();
}

/** A pack's wired node against the ports its descriptor declares (default settings: no protocol node's ports follow
 *  its settings). A port the spec names that the node lacks is a wire the planner refuses, and with it the whole
 *  document; one the node has that the spec does not name is never wired. */
export function checkProtocolPorts(type: string, spec: ProtocolNodeSpec,
                                   ports: readonly Pick<PortDesc, "id" | "dir" | "kind">[]): void {
  const actual = ports.map(portKey).sort();
  const wired = portsOf(spec);
  if (actual.join("\n") !== wired.join("\n")) {
    throw new Error(`its protocol node ${type} has the ports ${actual.join(", ")}, and its pack.json's protocol.nodes ` +
      `wires ${wired.join(", ")}`);
  }
}
```

In `protocolNodes.ts`, delete the two moved declarations and re-export them, so every importer still finds them there:
`export type { SystemInput, ProtocolNodeSpec } from "./protocolSpec";` (and `import type` them for the file's own use).
`serialCapture.ts` defines `SerialOutCaptureMode` and `protocolSpec.ts` imports it as a type only, so the cycle is
erased at build.

- [ ] **Step 4: The built-ins' port test reads the shared function**

In `protocol-nodes.test.ts`, delete the local `port` and `portsOf` and import `portKey` and `portsOf` from
`../../src/graph/protocolSpec`; the first test's `def.descriptor().ports.map(port)` becomes `.map(portKey)`.

- [ ] **Step 5: Run the tests**

Run: `pnpm test graph/protocol-spec graph/protocol-nodes && pnpm typecheck`
Expected: PASS, and the typecheck exits 0. If one of `checkProtocolTable`'s rules refuses the product's own table when
Task 2 seeds it, stop and report: the rules are meant to hold every built-in package as it is.

- [ ] **Step 6: Commit**

```bash
git add packages/retroplug/src/graph/protocolSpec.ts packages/retroplug/src/graph/protocolNodes.ts \
  packages/retroplug/test/graph/protocol-spec.test.ts packages/retroplug/test/graph/protocol-nodes.test.ts
git commit -m "protocol: the protocol node contract and its checks are a module of their own"
```

---

### Task 2: The live protocol tables

**Files:**
- Create: `packages/retroplug/src/contributions/protocolRegistries.ts`, `packages/retroplug/test/graph/protocol-live.test.ts`
- Modify: `packages/retroplug/scripts/lib/packages.mjs`, `packages/retroplug/src/graph/{protocolNodes,buildGraph,serialCapture}.ts`,
  `packages/retroplug/test/packages/fixtures/expected/{protocol,packages}.ts`, `packages/retroplug/test/graph/protocol-nodes.test.ts`,
  `packages/retroplug/test/systems/store-graph.test.ts`, `packages/retroplug/test/source-literals/no-cart-literals.test.ts`

- [ ] **Step 1: Write the failing test**

`packages/retroplug/test/graph/protocol-live.test.ts`:

```ts
// The live protocol tables (docs/downloadable-packs-m2e-plan.md): the generated module seeds them with the product's
// protocol nodes and roles, a commit adds a pack's, the readers see it after the commit, and a commit the checks refuse
// leaves both tables as they were.
import { test, expect } from "@retroplug/testing";
import { commitContributions, contributionsVersion } from "../../src/contributions/registry";
import { protocolNodesRegistry, roleNodesRegistry } from "../../src/contributions/protocolRegistries";
import { declaredNodeTypes, protocolNodeOf, protocolNodes, protocolSpecOf, roleNodes } from "../../src/graph/protocolNodes";

const refusal = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
  return "";
};

test("the seed is the product's tables", () => {
  expect(protocolNodeOf("mgb")).toBe("rp.mgb.midi");
  expect(protocolSpecOf("rp.mgb.midi")).toEqual({ midiIn: "midi", writes: ["serialIn"] });
  expect(declaredNodeTypes().includes("rp.midi.route")).toBe(true);
  // Unwired nodes are declared and not wired.
  expect(protocolSpecOf("rp.midi.route")).toBe(undefined);
});

test("a pack's node and role join the tables in one commit, and the readers see them", () => {
  const before = contributionsVersion();
  commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-a", items: [{ type: "rp.live-a.echo", midiIn: "in", writes: [], midiOut: true }] },
    { registry: roleNodesRegistry, pkg: "live-a", items: [{ kind: "live-a", node: "rp.live-a.echo" }] },
  ]);
  expect(contributionsVersion()).toBe(before + 1);
  expect(protocolNodeOf("live-a")).toBe("rp.live-a.echo");
  expect(protocolNodes()["rp.live-a.echo"]).toEqual({ midiIn: "in", writes: [], midiOut: true });
  expect(roleNodes()["live-a"]).toBe("rp.live-a.echo");
  const declared = declaredNodeTypes();
  expect(declared[declared.length - 1]).toBe("rp.live-a.echo");
});

test("a commit the checks refuse changes nothing", () => {
  const nodes = protocolNodesRegistry.items();
  const roles = roleNodesRegistry.items();
  expect(refusal(() => commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-b", items: [{ type: "rp.mgb.midi", midiIn: "midi", writes: ["serialIn"] }] },
  ]))).toBe("two packages declare the script node rp.mgb.midi: mgb and live-b");
  expect(refusal(() => commitContributions([
    { registry: roleNodesRegistry, pkg: "live-c", items: [{ kind: "mgb", node: "rp.mgb.midi" }] },
  ]))).toBe("two packages declare the role kind mgb: mgb and live-c");
  expect(refusal(() => commitContributions([
    { registry: protocolNodesRegistry, pkg: "live-d", items: [{ type: "rp.live-d.idle" }] },
    { registry: roleNodesRegistry, pkg: "live-d", items: [{ kind: "live-d", node: "rp.live-d.idle" }] },
  ]))).toBe("live-d: the role live-d names rp.live-d.idle, which declares no midiIn and writes to wire it");
  expect(protocolNodesRegistry.items()).toBe(nodes);
  expect(roleNodesRegistry.items()).toBe(roles);
});
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `pnpm test graph/protocol-live`
Expected: FAIL, `../../src/contributions/protocolRegistries` cannot be resolved.

- [ ] **Step 3: The registries**

`packages/retroplug/src/contributions/protocolRegistries.ts`:

```ts
// The protocol tables, as live registries (docs/downloadable-packs-m2e-plan.md): every protocol node the product's
// packages and the loaded packs declare, wired or not, and the role kinds placed as one. The generated
// src/generated/protocol.ts seeds them in span-label order; a pack's protocol half adds to them at its commit
// (src/packs/protocolHalf.ts). The checks are graph/protocolSpec.ts's, run over the whole staged table at every
// commit. Only registry.ts and that module are imported, so the planner's bundle carries no node code and no schema.
import { Registry } from "./registry";
import { checkProtocolTable, checkRoleNodes, type ProtocolNodeEntry, type RoleNodeEntry } from "../graph/protocolSpec";

export const protocolNodesRegistry = new Registry<ProtocolNodeEntry>("protocolNodes", (entries) =>
  checkProtocolTable(entries));

export const roleNodesRegistry = new Registry<RoleNodeEntry>("roleNodes", (entries, view) =>
  checkRoleNodes(entries, view(protocolNodesRegistry)));
```

- [ ] **Step 4: The generated seed**

In `packages.mjs`, replace `renderProtocol`'s output for the three tables with one commit. Its new body writes,
before `KIND_DATA` (which stays as it is, with its comment):

```js
  lines.push(
    `${generatedFrom(product)} and the`,
    "// packages' manifests. Do not edit; edit a manifest and rebuild.",
    "//",
    "// The protocol tables' seed: every protocol node the product's packages declare, wired or not, and the role kinds",
    "// placed as one, committed in span-label order into the live tables src/contributions/protocolRegistries.ts holds,",
    "// which a pack's protocol half adds to (docs/downloadable-packs-m2e-plan.md); then the kinds' data. Only the",
    "// registries and a type are imported, so a bundle that reads it carries no node's code, no role's schema and",
    "// nothing of the app.",
    'import type { KindData } from "@retroplug/core/defineKind";',
    `import { commitContributions } from "${specifier(outDir, join(shellSrc, "contributions/registry.ts"))}";`,
    `import { protocolNodesRegistry, roleNodesRegistry } from "${specifier(outDir, join(shellSrc, "contributions/protocolRegistries.ts"))}";`,
    "",
    "commitContributions([",
    '  { registry: protocolNodesRegistry, pkg: "retroplug", items: [] },',
    '  { registry: roleNodesRegistry, pkg: "retroplug", items: [] },',
  );
  const specs = [...byType.values()].sort((a, b) => a.node.label - b.node.label);
  for (const { node, pkg } of specs) {
    const fields = [`type: ${JSON.stringify(node.type)}`];
    for (const key of ["midiIn", "writes", "launches", "serialOut", "midiOut", "capture", "out"]) {
      if (node[key] === undefined) continue;
      fields.push(`${key}: ${literal(node[key])}`);
    }
    lines.push(`  { registry: protocolNodesRegistry, pkg: ${JSON.stringify(pkg)}, items: [{ ${fields.join(", ")} }] },`);
  }
  for (const { kind, type, pkg } of roles) {
    lines.push(`  { registry: roleNodesRegistry, pkg: ${JSON.stringify(pkg)}, items: [{ kind: ${JSON.stringify(kind)}, ` +
      `node: ${JSON.stringify(type)} }] },`);
  }
  lines.push("]);", "");
```

`byType`'s values carry `pkg` (the package's short name, as `renderPackages` writes it) and `roles` carries `pkg` too
(`roleTable`); if either carries the npm name instead, write what `renderPackages` writes for the same package. Keep
`specifier`'s use as the other renders do (`renderPlacers`'s `contributionsSpecifier` may be the helper meant for
`src/contributions/*`: use whichever the other seeds use).

In `kindBlocks`, the check's context reads the live tables:

```js
      `${local}.check?.(KIND_ITEMS_${decl.kind}, KIND_DATA_${decl.kind}, {`,
      "  nodeTypes: Object.keys(protocolNodes()),",
      "  roleNodes: roleNodes(),",
      "});",
```

and in `renderPackages` the import line becomes two: `import { KIND_DATA } from "./protocol";` and
`import { protocolNodes, roleNodes } from "<specifier of src/graph/protocolNodes.ts>";`, both written only when
`kindDecls.length`, as today's one is.

- [ ] **Step 5: The readers**

In `protocolNodes.ts`, replace the generated import and the re-exported constants (`export { PROTOCOL_NODES, ROLE_NODES }`
and `ProtocolNodeType = keyof typeof PROTOCOL_NODES`) with:

```ts
// The live tables, seeded by the generated module this imports first (docs/downloadable-packs-m2e-plan.md): a pack's
// protocol half adds to them, so they are read through these functions, each rebuilt after a commit.
import "../generated/protocol";
import { protocolNodesRegistry, roleNodesRegistry } from "../contributions/protocolRegistries";
import { derived, derivedList } from "../contributions/registry";
import { isWired, type ProtocolNodeSpec, type SystemInput } from "./protocolSpec";

/** A protocol node type: any id a package or a loaded pack declares. */
export type ProtocolNodeType = string;

/** Every wired protocol node's spec, by type: the product's and each loaded pack's. */
export const protocolNodes = derived((): Readonly<Record<string, ProtocolNodeSpec>> => {
  const out: Record<string, ProtocolNodeSpec> = Object.create(null);
  for (const entry of protocolNodesRegistry.items()) {
    if (!isWired(entry)) continue;
    const { type, ...spec } = entry;
    out[type] = spec as ProtocolNodeSpec;
  }
  return out;
});

/** The node type each role kind is placed as. */
export const roleNodes = derived((): Readonly<Record<string, string>> => {
  const out: Record<string, string> = Object.create(null);
  for (const { kind, node } of roleNodesRegistry.items()) out[kind] = node;
  return out;
});

/** Every protocol node type declared, wired or not, in commit order (the seed's span-label order, then the packs'). */
export const declaredNodeTypes = derivedList((): readonly string[] => protocolNodesRegistry.items().map((e) => e.type));

/** A wired node type's spec, or undefined for one that is not wired or not declared. */
export function protocolSpecOf(type: string): ProtocolNodeSpec | undefined {
  const table = protocolNodes();
  return Object.prototype.hasOwnProperty.call(table, type) ? table[type] : undefined;
}
```

and rewrite the readers below over them: `outNodeOf(node)` returns `protocolSpecOf(node)?.out`; `protocolNodeOf(kind)`
reads `roleNodes()` with the own-key test it has today; `protocolWrites(kind)` returns
`protocolSpecOf(node)?.writes ?? []`; `protocolPipeline` reads `protocolSpecOf(node)!.writes` (a node a role is placed as
is wired: the role check holds it). Update the file's header and the comment above the old re-export to say the tables
are live and where they are seeded.

In `buildGraph.ts`: drop `PROTOCOL_NODES` from the import and add `protocolSpecOf`; `PROTOCOL_NODES[role.node]`
becomes `protocolSpecOf(role.node)!` (three places: the role's spec, the `out` spec, the real cart's writes). In
`serialCapture.ts`: `PROTOCOL_NODES[r.node] as ProtocolNodeSpec` becomes `protocolSpecOf(r.node)` (with `?.capture`),
and its header's "it reads PROTOCOL_NODES, the generated table" becomes "it reads the protocol tables". Then
`pnpm typecheck` names every other reader of the old constants and of `keyof typeof PROTOCOL_NODES`: fix each by the
same substitution. The tests that read the constants: `protocol-nodes.test.ts` (`PROTOCOL_NODES` to `protocolNodes()`,
`ROLE_NODES` to `roleNodes()`), `store-graph.test.ts` (`ROLE_NODES` to `roleNodes()`), `no-cart-literals.test.ts`
(`DECLARED_NODE_TYPES` from the generated module to `declaredNodeTypes()` from `../../src/graph/protocolNodes`).
`KIND_DATA`'s importers (`buildGraph.ts`, `loadPacks.ts`, `build-graph.test.ts`) are unchanged.

- [ ] **Step 6: The generator's fixture**

Run: `pnpm test packages/packages-generator`
Expected: FAIL on `expected/protocol.ts` and `expected/packages.ts`, which still hold the old shapes. Regenerate them
as the test file says a fixture's expected modules are re-recorded (read its header; if it has no recording mode, write
the generated output over the two files by hand from the failure's diff), and read the new `expected/protocol.ts`
whole: one commit, the shell's two empty seeds first, then each node and each role in label order, each under its
package. Rerun: PASS. The generator's build-time refusals (`run-generator.mjs`) do not change.

- [ ] **Step 7: Run the tests**

Run: `pnpm test graph systems source-literals packages script && pnpm typecheck && pnpm check:boundaries`
Expected: PASS, every command exit 0. `test/script/bundle-protocol.test.ts` (the planner's bundle carries no schema)
must pass unchanged: `protocolRegistries.ts` imports only `registry.ts` and `protocolSpec.ts`.

- [ ] **Step 8: Commit**

```bash
git add packages/retroplug/src/contributions/protocolRegistries.ts packages/retroplug/src/graph/protocolNodes.ts \
  packages/retroplug/src/graph/buildGraph.ts packages/retroplug/src/graph/serialCapture.ts \
  packages/retroplug/scripts/lib/packages.mjs packages/retroplug/test/packages/fixtures/expected/protocol.ts \
  packages/retroplug/test/packages/fixtures/expected/packages.ts packages/retroplug/test/graph/protocol-live.test.ts \
  packages/retroplug/test/graph/protocol-nodes.test.ts packages/retroplug/test/systems/store-graph.test.ts \
  packages/retroplug/test/source-literals/no-cart-literals.test.ts
git commit -m "protocol: the protocol tables are live registries the generated module seeds"
```

(Add any other file the typecheck sent you to, by name.)

---

### Task 3: pack.json's protocol member and the placer half

**Files:**
- Modify: `packages/native/src/host/packs/PackManifest.{hpp,cpp}`, `packages/native/test/packs/PackManifest.test.cpp`

- [ ] **Step 1: Write the failing tests**

In `PackManifest.test.cpp`, beside the `script` member's cases (follow how they build a manifest from a base and
parse it with `RP_API_LEVEL`), add a `[packs-manifest]` case for the protocol member:

```cpp
TEST_CASE("pack.json's protocol member: its nodes and roles, held to provides", "[packs-manifest]") {
    // A base manifest with a protocol half: two node types, one role.
    const auto with = [](const std::string& protocol, const std::string& halves = R"("protocol":"js/protocol.js")",
                         const std::string& provides =
                             R"("nodeTypes":["rp.pp.echo","rp.pp.idle"],"roles":["pp"])") {
        return R"({"id":"pp","name":"P","version":"1.0.0","apiLevel":)" + std::to_string(RP_API_LEVEL) +
               R"(,"license":"MIT","source":"s","provides":{"platforms":[],)" + provides + R"(},"halves":{)" + halves +
               "}" + (protocol.empty() ? "" : R"(,"protocol":)" + protocol) + "}";
    };
    const std::string good =
        R"({"nodes":[{"type":"rp.pp.echo","midiIn":"in","writes":[],"midiOut":true},{"type":"rp.pp.idle"}],)"
        R"("roles":{"pp":"rp.pp.echo"}})";
    std::string why;
    const auto m = parsePackManifest(with(good), &why, RP_API_LEVEL);
    REQUIRE(m);
    REQUIRE(m->protocol);
    CHECK(m->protocol->nodeTypes == std::vector<std::string>{"rp.pp.echo", "rp.pp.idle"});
    CHECK(m->protocol->roles == std::map<std::string, std::string>{{"pp", "rp.pp.echo"}});
    // The nodes as canonical JSON: keys sorted, no space, what the TS loader reads.
    CHECK(m->protocol->nodesJson ==
          R"([{"midiIn":"in","midiOut":true,"type":"rp.pp.echo","writes":[]},{"type":"rp.pp.idle"}])");

    const auto refused = [&](const std::string& json, const std::string& reason) {
        std::string w;
        CHECK_FALSE(parsePackManifest(json, &w, RP_API_LEVEL));
        CHECK(w == "pack.json: " + reason);
    };
    refused(with(""), "halves.protocol needs the protocol member (its nodes and roles)");
    refused(with(good, R"("app":"js/app.js")"), "protocol is set, and halves names no protocol half");
    refused(with(R"({"nodes":[],"roles":{}})"), "protocol.nodes is empty: a protocol half registers node types");
    refused(with(R"({"nodes":[{"midiIn":null}],"roles":{}})"), R"(protocol.nodes[0] has no "type")");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo","label":16}],"roles":{}})"),
            "protocol.nodes gives rp.pp.echo a label: a pack's script nodes take their span labels at load");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo"},{"type":"rp.pp.echo"}],"roles":{}})"),
            "protocol.nodes names rp.pp.echo twice");
    refused(with(R"({"nodes":[{"type":"rp.pp.other"}],"roles":{}})"),
            "protocol.nodes names rp.pp.other, which provides.nodeTypes does not declare");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo"}],"roles":{"pp":"rp.pp.gone"}})"),
            "protocol.roles places pp as rp.pp.gone, which protocol.nodes does not declare");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo"}],"roles":{"qq":"rp.pp.echo"}})"),
            "protocol.roles names qq, which provides.roles does not declare");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo"}],"roles":{},"kinds":{"controllerApps":[]}})"),
            "protocol.kinds: a pack cannot contribute to a contribution kind yet");
    refused(with(R"({"nodes":[{"type":"rp.pp.echo"}],"roles":{},"placer":"js/p.js"})"),
            "protocol holds \"placer\", which is no member of it");
}

TEST_CASE("pack.json's placer half is a half like the others", "[packs-manifest]") {
    const std::string json = R"({"id":"pl","name":"P","version":"1.0.0","apiLevel":)" + std::to_string(RP_API_LEVEL) +
                             R"(,"license":"MIT","source":"s","halves":{"placer":"js/placer.js"}})";
    std::string why;
    const auto m = parsePackManifest(json, &why, RP_API_LEVEL);
    REQUIRE(m);
    CHECK(m->halves.at("placer") == "js/placer.js");
}
```

Also a node type named by both halves:

```cpp
TEST_CASE("a node type is one half's", "[packs-manifest]") {
    // script.nodeTypes and protocol.nodes naming one type: two halves would register it.
    // Build the manifest as the script member's cases do, with halves {"script","protocol"} and both members naming
    // rp.pp.echo; expected reason:
    //   "protocol.nodes names rp.pp.echo, which script.nodeTypes names too"
}
```

(Write that third case's manifest out in full the way the file's script-member cases spell theirs.)

- [ ] **Step 2: Run them to make sure they fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-manifest]"`
Expected: FAIL to compile (`PackManifest` has no `protocol`).

- [ ] **Step 3: The member**

`PackManifest.hpp`, beside `ScriptSpec`:

```cpp
// A protocol half's member (docs/downloadable-packs-m2e-plan.md): its QuickJS script nodes' wiring, which the TS loader
// commits to the live protocol tables, and the role kinds placed as them. The half itself is global code
// (halves.protocol) that every Engine loads.
struct ProtocolSpec {
    std::vector<std::string>           nodeTypes;  // each node's type, in the manifest's order; each in provides
    std::map<std::string, std::string> roles;      // role kind -> the node type it is placed as
    std::string                        nodesJson;  // protocol.nodes as canonical JSON, which the TS loader checks
};
```

and in `PackManifest`: `std::optional<ProtocolSpec> protocol;  // present exactly when halves names a protocol half`.
Update the header comment ("it refuses the protocol half, which M2e loads" becomes what the loader now does with it).

`PackManifest.cpp`: add `"placer"` to `kHalves`. Add the raw structs beside `RawScript`:

```cpp
// One protocol node: its type, and everything else kept for the TS loader, whose checks hold the wiring. Its label is
// refused here: the host gives a pack's script nodes their span labels at load.
struct RawProtocolNode {
    std::optional<std::string>     type;
    rfl::ExtraFields<rfl::Generic> extra;
};
// `protocol` is closed as `script` is, and names its one refused member, kinds, apart (M2f).
struct RawProtocol {
    std::optional<std::vector<rfl::Generic>>          nodes;
    std::optional<std::map<std::string, std::string>> roles;
    rfl::ExtraFields<rfl::Generic>                    extra;
};
```

with `std::optional<RawProtocol> protocol;` in `RawManifest`, and after the script member's block:

```cpp
    const bool protocolHalf = m.halves.count("protocol") > 0;
    if (const auto& p = raw->protocol) {
        if (!protocolHalf) {
            refuse(why, "protocol is set, and halves names no protocol half");
            return std::nullopt;
        }
        for (const auto& [key, value] : p->extra) {
            refuse(why, key == "kinds" ? std::string("protocol.kinds: a pack cannot contribute to a contribution kind yet")
                                       : "protocol holds \"" + echoed(key) + "\", which is no member of it");
            return std::nullopt;
        }
        ProtocolSpec spec;
        const std::vector<rfl::Generic> nodes = p->nodes.value_or(std::vector<rfl::Generic>{});
        if (nodes.empty()) {
            refuse(why, "protocol.nodes is empty: a protocol half registers node types");
            return std::nullopt;
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto node = rfl::from_generic<RawProtocolNode>(nodes[i]);
            if (!node || !node->type || node->type->empty()) {
                refuse(why, "protocol.nodes[" + std::to_string(i) + "] has no \"type\"");
                return std::nullopt;
            }
            const std::string& type = *node->type;
            for (const auto& [key, value] : node->extra) {
                if (key == "label") {
                    refuse(why, "protocol.nodes gives " + echoed(type) +
                                    " a label: a pack's script nodes take their span labels at load");
                    return std::nullopt;
                }
            }
            if (std::find(spec.nodeTypes.begin(), spec.nodeTypes.end(), type) != spec.nodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + " twice");
                return std::nullopt;
            }
            if (std::find(m.providesNodeTypes.begin(), m.providesNodeTypes.end(), type) == m.providesNodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + ", which provides.nodeTypes does not declare");
                return std::nullopt;
            }
            if (m.script && std::find(m.script->nodeTypes.begin(), m.script->nodeTypes.end(), type) !=
                                m.script->nodeTypes.end()) {
                refuse(why, "protocol.nodes names " + echoed(type) + ", which script.nodeTypes names too");
                return std::nullopt;
            }
            spec.nodeTypes.push_back(type);
        }
        for (const auto& [kind, type] : p->roles.value_or(std::map<std::string, std::string>{})) {
            if (std::find(m.providesRoles.begin(), m.providesRoles.end(), kind) == m.providesRoles.end()) {
                refuse(why, "protocol.roles names " + echoed(kind) + ", which provides.roles does not declare");
                return std::nullopt;
            }
            if (std::find(spec.nodeTypes.begin(), spec.nodeTypes.end(), type) == spec.nodeTypes.end()) {
                refuse(why, "protocol.roles places " + echoed(kind) + " as " + echoed(type) +
                                ", which protocol.nodes does not declare");
                return std::nullopt;
            }
            spec.roles.emplace(kind, type);
        }
        spec.nodesJson = rp::graph::canonicalJson(rfl::Generic(rfl::Generic::Array(nodes.begin(), nodes.end())));
        m.protocol = std::move(spec);
    } else if (protocolHalf) {
        refuse(why, "halves.protocol needs the protocol member (its nodes and roles)");
        return std::nullopt;
    }
```

Include `graph/CanonicalJson.hpp`. If `rfl::from_generic` with `ExtraFields` cannot read a non-object `Generic` (an
array entry that is a number), it returns an error, which the `!node` branch refuses: keep that. If the extra-fields
iteration has another spelling in this reflect-cpp, follow how the file already walks `s->extra`.

- [ ] **Step 4: Run them**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-manifest]"`
Expected: PASS. (`[packs-loader]`'s `packfixture-protocol` case now fails with the manifest's reason: Task 4 updates
it.)

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/packs/PackManifest.hpp packages/native/src/host/packs/PackManifest.cpp \
  packages/native/test/packs/PackManifest.test.cpp
git commit -m "packs: pack.json's protocol member and the placer half"
```

---

### Task 4: The pack table loads a protocol half

**Files:**
- Create: `packages/native/src/host/packs/PackProtocol.hpp`, `packages/native/src/host/packs/ProtocolTrial.{hpp,cpp}`,
  `packages/native/test/packs/PackProtocol.test.cpp`, `packages/native/test/packs/protocol-fixture/` (CMakeLists.txt and six variant folders)
- Modify: `packages/native/src/host/script/ScriptHost.{hpp,cpp}`, `packages/native/src/host/packs/{PackTable,ProcessPacks}.{hpp,cpp}`,
  `packages/native/src/host/js/PackLoaderCore.cpp`, `packages/native/CMakeLists.txt`, `packages/native/cmake/tests.cmake`,
  `packages/native/test/packs/PackLoader.test.cpp`, `packages/native/test/packs/fixture/CMakeLists.txt` (a comment)

- [ ] **Step 1: The fixture packs**

`packages/native/test/packs/protocol-fixture/CMakeLists.txt`:

```cmake
# The protocol fixture packs (docs/downloadable-packs-m2e-plan.md): hand-written, each a pack.json and a global-code node
# bundle in the shape tools/build-pack-halves.mjs writes one, staged under build/packs/<id>/1.0.0/ as development packs
# at configure time. No product lists any of them.
#   protofix             rp.protofix.up (MIDI in, each message's second byte 12 up, out as host MIDI) and rp.protofix.idle
#   protofix-undeclared  its bundle defines a type its protocol.nodes does not declare
#   protofix-missing     its protocol.nodes declares a type its bundle does not define
#   protofix-clash       it defines rp.midi.route, a built-in script node
#   protofix-syntax      a bundle that does not compile
#   protofix-throws      a bundle that throws as it loads
foreach(_v protofix protofix-undeclared protofix-missing protofix-clash protofix-syntax protofix-throws)
    get_property(_staged GLOBAL PROPERTY RP_PACK_STAGED)
    if("${_v}/1.0.0" IN_LIST _staged)
        message(FATAL_ERROR "the protocol fixture pack ${_v}/1.0.0 is already staged by another target")
    endif()
    set_property(GLOBAL APPEND PROPERTY RP_PACK_STAGED "${_v}/1.0.0")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/pack.json.in" "${RP_PACKS_DIR}/${_v}/1.0.0/pack.json" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_v}/js/protocol.js" "${RP_PACKS_DIR}/${_v}/1.0.0/js/protocol.js"
                   COPYONLY)
endforeach()
```

and in `tests.cmake`, beside the other fixtures' `add_subdirectory` lines:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test/packs/protocol-fixture ${CMAKE_BINARY_DIR}/packs-protocol-fixture
                 EXCLUDE_FROM_ALL)
```

`protofix/pack.json.in`:

```json
{
  "id": "protofix", "name": "Protocol fixture", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/packs/protocol-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": ["rp.protofix.up", "rp.protofix.idle"], "roles": [] },
  "halves": { "protocol": "js/protocol.js" },
  "protocol": {
    "nodes": [{ "type": "rp.protofix.up", "midiIn": "in", "writes": [], "midiOut": true }, { "type": "rp.protofix.idle" }],
    "roles": {}
  }
}
```

`protofix/js/protocol.js`:

```js
// The protocol fixture's node bundle (docs/downloadable-packs-m2e-plan.md): global code appending definitions to the
// global the script host reads, in the shape defineNode makes, as tools/build-pack-halves.mjs writes a protocol half.
// rp.protofix.up sends each message on with its second byte 12 up; rp.protofix.idle has no ports.
(function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  function define(id, ports, create) {
    var layout = { ports: ports, params: [] };
    nodes.push({
      id: id,
      descriptor: function () { return { id: id, ports: ports, params: [], settings: { type: "object" } }; },
      parseSettings: function (raw) { return raw === undefined ? {} : raw; },
      layout: function () { return layout; },
      create: create,
      reshapes: false,
      faultOnThrow: false,
    });
  }
  define("rp.protofix.up", [{ id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }],
    function () {
      return { process: function (b) {
        var ms = b.messages("in");
        for (var i = 0; i < ms.length; i++) {
          var d = ms[i].data;
          b.send("midiOut", ms[i].frame, [d[0], d[1] + 12, d[2]]);
        }
      } };
    });
  define("rp.protofix.idle", [], function () { return { process: function () {} }; });
})();
```

The variants are the same files with their id in every place (`protofix-undeclared` and so on: the pack's id, its
types `rp.<id>.up`, its name). What differs:

- `protofix-undeclared`: `provides.nodeTypes` and `protocol.nodes` name `rp.protofix-undeclared.up` alone; the bundle
  also defines `rp.protofix-undeclared.extra` (with no ports).
- `protofix-missing`: `provides.nodeTypes` and `protocol.nodes` name `rp.protofix-missing.up` and
  `rp.protofix-missing.gone`; the bundle defines `.up` alone.
- `protofix-clash`: `provides.nodeTypes` and `protocol.nodes` name `rp.midi.route` alone (unwired); the bundle defines
  `rp.midi.route` with no ports.
- `protofix-syntax`: one node `rp.protofix-syntax.up`; `js/protocol.js` is `var x = ;`.
- `protofix-throws`: one node `rp.protofix-throws.up`; the bundle defines it and then runs
  `throw new Error("the protocol fixture throws");`.

- [ ] **Step 2: Write the failing tests**

`packages/native/test/packs/PackProtocol.test.cpp` (add it to `retroplug-packs-test`'s SOURCES in `tests.cmake` with
the comment `# the protocol half: the table's trial, the pack list, every Engine`):

```cpp
// A pack's protocol half (docs/downloadable-packs-m2e-plan.md): its global-code node bundle compiled and trial-loaded
// once by the table, beside the built-in script nodes, and refused for a type it does not declare, one it declares and
// does not define, a clash, a bundle that does not compile or throws; its data in the pack list the TS loader reads.
// Every Engine's own load is Task 5's.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "graph/rp_pack.h"
#include "host/packs/PackTable.hpp"
#include "host/packs/ProtocolTrial.hpp"

using rp::host::packs::PackTable;

namespace {

std::filesystem::path packDir(const std::string& id) {
    return std::filesystem::path(RP_PACKS_BUILD_DIR) / id / "1.0.0";
}

// A table with the backend's trial, as the process's has.
PackTable& trialTable(PackTable& table) {
    table.setProtocolTrial(rp::host::packs::protocolTrial());
    return table;
}

}  // namespace

TEST_CASE("a protocol half is compiled and trial-loaded once, its types and descriptors kept", "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    const auto load = table.load(packDir("protofix"));
    INFO(load.error);
    REQUIRE(load);
    REQUIRE(load.pack->protocol);
    const auto& half = *load.pack->protocol;
    CHECK(half.scriptId == "rp.protofix.protocol");
    CHECK_FALSE(half.bytecode.empty());
    CHECK(half.typeIds == std::vector<std::string>{"rp.protofix.up", "rp.protofix.idle"});
    REQUIRE(half.descriptors.size() == 2);
    CHECK(half.descriptors[0].find(R"("id":"rp.protofix.up")") != std::string::npos);
    CHECK(half.descriptors[0].find(R"("id":"midiOut")") != std::string::npos);
    CHECK(load.pack->packages.empty());  // no native half and no script half
}

TEST_CASE("a protocol half is refused for what it defines, what it misses, a clash, a compile and a throw",
          "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    const auto refused = [&](const std::string& id, const std::string& part) {
        const auto load = table.load(packDir(id));
        CHECK_FALSE(load);
        INFO(id << ": " << load.error);
        CHECK(load.error.find(part) != std::string::npos);
        CHECK(table.find(id) == nullptr);
    };
    refused("protofix-undeclared", "pack protofix-undeclared's protocol half defines node type "
                                   "rp.protofix-undeclared.extra, which its pack.json's protocol.nodes does not declare");
    refused("protofix-missing", "pack protofix-missing's pack.json's protocol.nodes declares rp.protofix-missing.gone, "
                                "which its protocol half does not define");
    refused("protofix-clash", "rp.midi.route");
    refused("protofix-syntax", "pack protofix-syntax: its protocol half does not compile");
    refused("protofix-throws", "the protocol fixture throws");
}

TEST_CASE("a table with no protocol trial refuses a protocol half", "[packs-protocol]") {
    PackTable table;
    const auto load = table.load(packDir("protofix"));
    CHECK_FALSE(load);
    CHECK(load.error == "pack protofix has a protocol half, and this host loads none");
}

TEST_CASE("a pack whose type an earlier pack's protocol half holds is refused", "[packs-protocol]") {
    // protofix loads, then a second copy of its types under another id: build the clash from protofix's own files by
    // loading protofix-missing? No: write the second pack as a scratch folder in a temporary directory, its pack.json
    // naming rp.protofix.up under the id protofix-again with protofix's bundle copied in, as PackLoader.test.cpp's
    // scratch cases write theirs. Expected: refused with
    //   "pack protofix-again registers node type rp.protofix.up, which package rp.protofix.protocol has registered already"
}
```

(Write the last case out in full with the scratch-folder helper `PackLoader.test.cpp` uses; if it has none, write the
folder with `std::ofstream` under `std::filesystem::temp_directory_path()` and remove it at the end.)

The pack list: in the same file, a case that installs the bare loader on a fresh QuickJS context over a table holding
`protofix` and reads `__rp_packList()`, as `PackModules.test.cpp`'s bare case drives `installBarePackLoader` (read it
for the runtime set-up and the eval helper; the table is passed the same way):

```cpp
TEST_CASE("the pack list carries a protocol half's data, and pack:<id>/protocol is no module", "[packs-protocol]") {
    // Expected __rp_packList() entry for protofix contains, in order:
    //   "\"halves\":[\"protocol\"]"
    //   "\"protocol\":{\"nodes\":[{\"midiIn\":\"in\",\"midiOut\":true,\"type\":\"rp.protofix.up\",\"writes\":[]},{\"type\":\"rp.protofix.idle\"}],\"roles\":{},\"descriptors\":{\"rp.protofix.up\":{"
    // and import("pack:protofix/protocol") rejects (the loader does not serve it).
}
```

Write it out against the bare loader's real entry points. If `PackModules.test.cpp`'s bare-runtime helpers cannot be
reached from `retroplug-packs-test` without copying, copy the few lines needed (a runtime, a context, an eval of a
string to a std::string).

- [ ] **Step 3: Run them to make sure they fail**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc)`
Expected: FAIL to compile (`ProtocolTrial.hpp`, `LoadedPack::protocol`, `setProtocolTrial` do not exist).

- [ ] **Step 4: The script host loads an app pack**

`ScriptHost.hpp`, after `loadPack`:

```cpp
    // Load a pack the app owns (a downloaded pack's protocol half, docs/downloadable-packs-m2e-plan.md): what loadPack
    // does, under an id that is "rp." and more and never the built-in pack's, where loadPack refuses every "rp." id. So
    // no caller of loadScriptPack can reload or shadow it.
    const rp_package* loadAppPack(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit,
                                  std::string* error = nullptr);
```

`ScriptHost.cpp`: move `loadPack`'s body after its `canLoadPack` line into a private
`loadChecked(const std::string& id, std::span<const Chunk> chunks, std::size_t memoryLimit, std::string* error)`
(declared in the header's private section), so `loadPack` is the id check and that call, and:

```cpp
const rp_package* ScriptHost::loadAppPack(const std::string& id, std::span<const Chunk> chunks,
                                          std::size_t memoryLimit, std::string* error) {
    if (!graph::isPlainId(id) || id.rfind("rp.", 0) != 0 || id.size() <= 3 || id == kBuiltInPack) {
        if (error)
            *error = "an app pack's id is \"rp.\" and more, and never the built-in pack's: " + graph::jsonString(id);
        return nullptr;
    }
    return loadChecked(id, chunks, memoryLimit, error);
}
```

- [ ] **Step 5: The trial**

`packages/native/src/host/packs/PackProtocol.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// A pack's protocol half (docs/downloadable-packs-m2e-plan.md): its QuickJS script nodes, compiled once by the table and
// loaded into every Engine's script host as the app pack `scriptId`.
namespace rp::host::packs {

struct ProtocolHalf {
    std::string               scriptId;     // "rp.<pack id>.protocol": its package's id in every Engine
    std::vector<std::uint8_t> bytecode;     // the global-code bundle, compiled
    std::vector<std::string>  typeIds;      // its definitions' ids, in registration order
    std::vector<std::string>  descriptors;  // each type's descriptor as canonical JSON, index for index
};

// Compile a protocol half's source and load it beside the built-in script nodes, as an Engine will: its bytecode, its
// types and their descriptors, or nothing with the reason in *why. The backend's (host/packs/ProtocolTrial.hpp), which
// the table is handed (PackTable::setProtocolTrial): the script host is not this library's.
using ProtocolTrial =
    std::function<std::optional<ProtocolHalf>(const std::string& scriptId, const std::string& source, std::string* why)>;

}  // namespace rp::host::packs
```

`packages/native/src/host/packs/ProtocolTrial.hpp`:

```cpp
#pragma once

#include "host/packs/PackProtocol.hpp"

namespace rp::host::packs {

// The trial every host's table runs a protocol half through (docs/downloadable-packs-m2e-plan.md): the bundle compiled
// to global-code bytecode, then loaded into a scratch script host after the product's built-in script nodes and both
// registered on a scratch registry, so a type the built-ins hold, a descriptor the planner cannot use or a bundle that
// throws is refused before any Engine meets it. The scratch host is the call's alone, freed before it returns.
ProtocolTrial protocolTrial();

}  // namespace rp::host::packs
```

`packages/native/src/host/packs/ProtocolTrial.cpp` (add it to `retroplug-backend`'s sources in
`packages/native/CMakeLists.txt`, beside `ProcessPacks.cpp`, with the comment
`# a pack's protocol half compiled and trial-loaded beside the built-in script nodes`):

```cpp
#include "host/packs/ProtocolTrial.hpp"

#include "graph/Descriptor.hpp"
#include "graph/NodeRegistry.hpp"
#include "host/dsp/ScriptCompiler.hpp"
#include "host/script/ScriptHost.hpp"

namespace rp::host::packs {

ProtocolTrial protocolTrial() {
    return [](const std::string& scriptId, const std::string& source, std::string* why) -> std::optional<ProtocolHalf> {
        const auto fail = [&](std::string reason) -> std::optional<ProtocolHalf> {
            if (why) *why = std::move(reason);
            return std::nullopt;
        };
        std::string error;
        auto bytecode = dsp::compileToBytecode(source, ScriptHost::kStackBytes, &error);
        if (!bytecode) return fail("its protocol half does not compile: " + error);
        ScriptHost host;
        const ScriptHost::Chunk builtIn[] = {ScriptHost::embeddedBundle()};
        if (!host.load(builtIn, &error)) return fail("the built-in script nodes did not load beside it: " + error);
        const ScriptHost::Chunk chunk{*bytecode};
        const rp_package* package = host.loadAppPack(scriptId, {&chunk, 1}, 0, &error);
        if (!package) return fail("its protocol half did not load: " + error);
        rp::graph::NodeRegistry registry;
        registry.addPackage(host.package());
        std::string clash;
        const bool added = registry.addPackage(package, &clash);
        ProtocolHalf half{scriptId, std::move(*bytecode), {}, {}};
        if (added) {
            for (std::uint32_t k = 0; k < package->node_type_count; ++k) {
                const std::string id = package->node_types[k]->id;
                half.typeIds.push_back(id);
                half.descriptors.push_back(rp::graph::descriptorJson(registry.find(id)->descriptor));
            }
        }
        host.discardPack(package);
        if (!added) return fail("its protocol half's nodes were refused: " + clash);
        return half;
    };
}

}  // namespace rp::host::packs
```

Follow the tree for `ScriptHost`'s namespace (`rp::host`), its default construction (as `Engine::Graph` makes one),
`dsp::compileToBytecode`'s namespace, and `NodeRegistry::find`'s entry type; the `addPackage` of the built-in package
takes the same `why` argument shape as elsewhere.

- [ ] **Step 6: The table**

`PackTable.hpp`: include `host/packs/PackProtocol.hpp`; in `LoadedPack`, after `script`:

```cpp
    // Its protocol half, compiled and trial-loaded; every Engine loads it into its script host (Engine::syncPacks).
    std::optional<ProtocolHalf>    protocol;
```

and in `PackTable`'s public section:

```cpp
    // The trial a protocol half is run through (the backend's protocolTrial): a table without one refuses every pack
    // with a protocol half. Set before the first load; a test's table sets its own.
    void setProtocolTrial(ProtocolTrial trial);
```

with a `ProtocolTrial protocolTrial_;` member. `PackTable.cpp`:

- `setProtocolTrial` takes the lock and stores it.
- The halves loop drops its `kind == "protocol"` refusal (the file check below it now covers the protocol half and the
  placer half like the rest) and its comment's "The protocol half is M2e's" sentence.
- The `holder` map also holds every earlier pack's protocol types, under its `scriptId`:
  `for (const auto& p : packs_) if (p->protocol) for (const auto& t : p->protocol->typeIds) holder.emplace(t, p->protocol->scriptId);`
- After the native packages' `checkPackage` loop and before the script half:

```cpp
    // The protocol half: read, compiled and trial-loaded beside the built-in script nodes by the backend's trial, which
    // also refuses a clash with them; a type another package holds is refused first, as the script half's is.
    if (manifest->protocol) {
        if (!protocolTrial_) return refuseHere(id, pack + " has a protocol half, and this host loads none");
        for (const auto& typeId : manifest->protocol->nodeTypes) {
            const auto it = holder.find(typeId);
            std::string by = it != holder.end() ? it->second : std::string();
            for (const rp_package* p : loaded->packages)
                for (std::uint32_t k = 0; by.empty() && p && p->abi_version == RP_ABI_VERSION && p->node_types &&
                                          k < p->node_type_count; ++k)
                    if (p->node_types[k] && p->node_types[k]->id && typeId == p->node_types[k]->id) by = packageIdOf(p);
            if (!by.empty())
                return refuseHere(id, pack + " registers node type " + typeId + ", which package " + by +
                                          " has registered already");
        }
        std::string source;
        if (!readTextFile(candidate.dir / fs::path(manifest->halves.at("protocol")), source, &why))
            return refuseHere(id, pack + ": " + why);
        auto half = protocolTrial_("rp." + id + ".protocol", source, &why);
        if (!half) return refuseHere(id, pack + ": " + why);
        const auto& declared = manifest->protocol->nodeTypes;
        for (const auto& t : half->typeIds)
            if (std::find(declared.begin(), declared.end(), t) == declared.end())
                return refuseHere(id, pack + "'s protocol half defines node type " + t +
                                          ", which its pack.json's protocol.nodes does not declare");
        for (const auto& t : declared)
            if (std::find(half->typeIds.begin(), half->typeIds.end(), t) == half->typeIds.end())
                return refuseHere(id, pack + "'s pack.json's protocol.nodes declares " + t +
                                          ", which its protocol half does not define");
        for (const auto& t : half->typeIds) registered.insert(t);
        loaded->protocol = std::move(*half);
    }
```

`readTextFile` is whatever `PackScript.cpp` reads a bundle with: if it is file-local there, move it into a shared spot
both files reach (a small `host/packs/PackFiles.{hpp,cpp}` in `retroplug-packs`, or `PackDirs`) rather than copy it.
The clash pre-check's loop is the script half's: factor the two into one lambda (`heldBy(typeId)`) rather than leave
two copies.

`ProcessPacks.cpp`: the process's table gets the trial once, with the table itself:

```cpp
PackTable& processPacks() {
    // A function-local static: built once, under the guard the language gives it, by whichever thread asks first.
    // The data root is the user's, so a development folder inside it is held like an installed one. The protocol trial
    // is the backend's, which this library holds and the table's library cannot reach.
    static PackTable table(rp::graph::productPackages(), defaultPackRoots().data);
    static const bool trial = (table.setProtocolTrial(protocolTrial()), true);
    (void)trial;
    return table;
}
```

- [ ] **Step 7: The pack list and the module loaders**

`PackLoaderCore.cpp`: the module loader refuses `pack:<id>/protocol` as it refuses the native and script halves
(`half == "native" || half == "script" || half == "protocol"`), and `packList` appends the protocol data after
`requires` when the pack has a protocol half:

```cpp
            std::string protocol;
            if (m.protocol && pack->protocol) {
                std::string roles, descriptors;
                for (const auto& [kind, node] : m.protocol->roles)
                    roles += std::string(roles.empty() ? "" : ",") + jsonString(kind) + ":" + jsonString(node);
                for (std::size_t k = 0; k < pack->protocol->typeIds.size(); ++k)
                    descriptors += std::string(k == 0 ? "" : ",") + jsonString(pack->protocol->typeIds[k]) + ":" +
                                   pack->protocol->descriptors[k];
                protocol = ",\"protocol\":{\"nodes\":" + m.protocol->nodesJson + ",\"roles\":{" + roles +
                           "},\"descriptors\":{" + descriptors + "}}";
            }
```

inserted before the entry's closing brace. Update the file's comment that lists what the loader refuses.

- [ ] **Step 8: The old protocol refusal's tests**

`PackLoader.test.cpp`'s `packfixture-protocol` case: its manifest names a protocol half and no protocol member, so it
is now refused by the manifest. Change the expected reason to
`"pack.json: halves.protocol needs the protocol member (its nodes and roles)"` and its comment ("The protocol half is
M2e's") to say a protocol half needs its member. Update `test/packs/fixture/CMakeLists.txt`'s comment on
`packfixture-protocol` the same way.

- [ ] **Step 9: Run the tests**

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-protocol],[packs-loader],[packs-manifest],[packs-script]"`
Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add packages/native/src/host/packs/PackProtocol.hpp packages/native/src/host/packs/ProtocolTrial.hpp \
  packages/native/src/host/packs/ProtocolTrial.cpp packages/native/src/host/packs/PackTable.hpp \
  packages/native/src/host/packs/PackTable.cpp packages/native/src/host/packs/ProcessPacks.cpp \
  packages/native/src/host/script/ScriptHost.hpp packages/native/src/host/script/ScriptHost.cpp \
  packages/native/src/host/js/PackLoaderCore.cpp packages/native/CMakeLists.txt packages/native/cmake/tests.cmake \
  packages/native/test/packs/PackProtocol.test.cpp packages/native/test/packs/PackLoader.test.cpp \
  packages/native/test/packs/fixture/CMakeLists.txt packages/native/test/packs/protocol-fixture
git commit -m "packs: the table compiles and trial-loads a protocol half, and the pack list carries its data"
```

(Add `PackScript.cpp` and any shared file the `readTextFile` move touched, by name.)

---

### Task 5: Every Engine loads a pack's protocol nodes

**Files:**
- Modify: `packages/native/src/host/engine/Engine.cpp` (`syncPacks`), `packages/native/test/packs/PackProtocol.test.cpp`

- [ ] **Step 1: Write the failing tests**

In `PackProtocol.test.cpp`, the Engine cases. Copy `PackScript.test.cpp`'s `MidiOut` and `runBlock` helpers (one block
over staged messages, the host MIDI the plan sent out), or move them to a shared header both files include if that is
less code:

```cpp
#include "host/engine/Engine.hpp"

namespace {

// Host MIDI in -> rp.protofix.up -> host MIDI out, under system 1.
const char* kUpDocument =
    R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"up","type":"rp.protofix.up"},)"
    R"({"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
    R"("wires":[{"from":"in.midi","to":"up.in"},{"from":"up.midiOut","to":"out.in1"}]})";

}  // namespace

TEST_CASE("an Engine registers a pack's protocol nodes, and its node runs", "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    REQUIRE(table.load(packDir("protofix")));
    Engine e(48000.0, table);
    const auto r = e.setGraph({kUpDocument, {}});
    INFO((r.errors.empty() ? std::string() : r.errors.front().message));
    REQUIRE(r.ok);
    CHECK(runBlock(e, {{0x90, 60, 100}}) == std::vector<MidiOut>{{1, 0, {0x90, 72, 100}}});
    // Its pack is the engine's app pack, beside the built-in one.
    bool listed = false;
    for (const auto& p : e.scriptPacks()) listed = listed || p.id == "rp.protofix.protocol";
    CHECK(listed);
}

TEST_CASE("two Engines each load it, and one built before the load takes it at its next setGraph", "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    Engine early(48000.0, table);
    REQUIRE(table.load(packDir("protofix")));
    Engine late(48000.0, table);
    for (Engine* e : {&early, &late}) {
        REQUIRE(e->setGraph({kUpDocument, {}}).ok);
        CHECK(runBlock(*e, {{0x90, 64, 1}}) == std::vector<MidiOut>{{1, 0, {0x90, 76, 1}}});
    }
}

TEST_CASE("a pack's protocol nodes take the span labels after the built-ins'", "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    REQUIRE(table.load(packDir("protofix")));
    Engine e(48000.0, table);
    const auto names = e.dspTraceNames();
    if (names.empty()) SKIP("this build names no span labels (RETROPLUG_PROFILE is off)");
    const auto at = [&](const std::string& type) {
        return static_cast<std::size_t>(std::find(names.begin(), names.end(), type) - names.begin());
    };
    REQUIRE(at("rp.protofix.up") < names.size());
    CHECK(at("rp.protofix.idle") == at("rp.protofix.up") + 1);
    // Every built-in script node's label is below the pack's.
    CHECK(at("rp.midi.route") < at("rp.protofix.up"));
    CHECK(at("rp.lsdj.serial-out") < at("rp.protofix.up"));
}

TEST_CASE("no caller of loadScriptPack can take a pack's protocol id", "[packs-protocol]") {
    PackTable table;
    trialTable(table);
    REQUIRE(table.load(packDir("protofix")));
    Engine e(48000.0, table);
    const auto r = e.loadScriptPack("rp.protofix.protocol", "var x = 1;");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("is the app's to give") != std::string::npos);
}
```

(If the packs-host product places no `rp.lsdj.serial-out`, drop that line: the point is that the built-ins come first.
Use the namespace `Engine` lives in, as `PackScript.test.cpp` does.)

- [ ] **Step 2: Run them to make sure they fail**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-protocol]"`
Expected: the Engine cases FAIL: the document is refused for an unknown type `rp.protofix.up`.

- [ ] **Step 3: `syncPacks` loads the protocol half**

In `Engine::syncPacks`, after the required-pack loop and before the `canAdd` loop:

```cpp
        // Its protocol half: the pack's QuickJS script nodes, loaded into this engine's script host as an app pack of its
        // own (rp.<id>.protocol), whose runtimes are this engine's as the built-in pack's are, and whose types take the
        // script host's next span labels. Loaded before the checks, so its package is checked with the pack's others, and
        // freed again when the pack is refused here.
        const rp_package* protocol = nullptr;
        if (fits && pack->protocol) {
            const rp::host::ScriptHost::Chunk chunk{pack->protocol->bytecode};
            protocol = graph_->script.loadAppPack(pack->protocol->scriptId, {&chunk, 1}, 0, &why);
            fits = protocol != nullptr;
        }
        for (const rp_package* p : pack->packages)
            fits = fits && registry.canAdd(p, &why);
        if (fits && protocol) fits = registry.canAdd(protocol, &why);
        if (!fits) {
            if (protocol) graph_->script.discardPack(protocol);
            std::fprintf(stderr, "[engine] pack %s was refused here: %s\n", pack->manifest.id.c_str(), why.c_str());
            packsRefused_.push_back(pack->manifest.id);
            continue;
        }
```

replacing the existing `canAdd` loop and refusal (keep their comments), and after the existing add loop:

```cpp
        if (protocol) {
            [[maybe_unused]] const bool added = graph_->registry.addPackage(protocol, &why);
            assert(added && "the protocol package canAdd passed, and the table refused a clash with the pack's own");
        }
```

Update `syncPacks`'s comment in `Engine.hpp` to say a pack's protocol half is loaded into the engine's script host too.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-protocol],[packs-engines],[packs-script]"`
Expected: PASS (the label case may SKIP on a build without `RETROPLUG_PROFILE`; say which in the report). Then
`pnpm test:plugin packs-test` exits 0.

- [ ] **Step 5: Commit**

```bash
git add packages/native/src/host/engine/Engine.cpp packages/native/src/host/engine/Engine.hpp \
  packages/native/test/packs/PackProtocol.test.cpp
git commit -m "engine: every Engine loads a pack's protocol nodes into its script host"
```

---

### Task 6: The TS loader commits a pack's protocol data and placer

**Files:**
- Create: `packages/retroplug/src/packs/protocolHalf.ts`
- Modify: `packages/retroplug/src/packs/{loadPacks,packHost}.ts`, `packages/retroplug/test/packs/load-packs.test.ts`

- [ ] **Step 1: Write the failing tests**

Append to `load-packs.test.ts` (each case uses ids of its own: the registries only grow):

```ts
import { protocolNodeOf } from "../../src/graph/protocolNodes";
import { protocolPipeline } from "../../src/graph/protocolNodes";
import { buildGraph, type GraphSystem } from "../../src/graph/buildGraph";
import { placersRegistry } from "../../src/contributions/placers";

/** A descriptor with these ports, as the host's trial hands one over. */
const descriptor = (id: string, ports: { id: string; dir: "in" | "out"; kind: string }[]) =>
  ({ id, name: id, ports, params: [], settings: { type: "object" } });

/** A pack with an app half (one role, one project section), a protocol half placing the role as an echo node, and a
 *  placer half. */
function protocolPack(id: string, over: { ports?: { id: string; dir: "in" | "out"; kind: string }[];
                                          node?: Record<string, unknown>; roles?: Record<string, string> } = {}) {
  const node = over.node ?? { type: `rp.${id}.echo`, midiIn: "in", writes: [], midiOut: true };
  const type = node.type as string;
  const info = pack(id, {
    halves: ["app", "protocol", "placer"],
    provides: { platforms: [], nodeTypes: [type], roles: [id] },
    protocol: {
      nodes: [node],
      roles: over.roles ?? { [id]: type },
      // The descriptor the host's trial would read for the node: its ports, `in` and `midiOut` unless given.
      descriptors: { [type]: descriptor(type, over.ports ?? [
        { id: "in", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }]) },
    },
  });
  const modules = {
    [id]: {
      app: { roles: [role(id)], projectSections: [{ key: `${id}-model`, schema: { parse: () => ({}) }, defaults: () => ({}) }] },
      placer: { key: `${id}-model`, place: () => ({}) },
    },
  };
  return { info, modules, type };
}

test("a pack's protocol data and placer commit with it, and buildGraph places its role's node", async () => {
  const { info, modules, type } = protocolPack("lp-proto");
  const { host, refused, imported } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused).toEqual({});
  expect(packStatus().loaded["lp-proto"]).toEqual(["app", "protocol", "placer"]);
  // The protocol half is data: nothing imported it.
  expect(imported.includes("lp-proto/protocol")).toBe(false);
  expect(protocolNodeOf("lp-proto")).toBe(type);
  expect(placersRegistry.items().some((p) => p.key === "lp-proto-model")).toBe(true);
  const gb: GraphSystem = {
    id: 1, platform: "gb", type: "rp.sameboy.gb", linkGroup: 0, exportMode: 0, resources: { rom: 1 }, romName: "rom1",
    pipeline: protocolPipeline([{ kind: "lp-proto", config: {} }]),
  };
  const { document } = buildGraph({ systems: [gb], routing: "stereo", shape: { kind: "multi", channels: 2 },
    midiRouting: "sendToAll", models: {} });
  expect(document.nodes.find((n) => n.id === "sys-1/lp-proto")?.type).toBe(type);
  expect(document.wires).toContainEqual({ from: "routing.sys-1", to: "sys-1/lp-proto.in" });
  expect(document.wires).toContainEqual({ from: "sys-1/lp-proto.midiOut", to: "midi-out.in1" });
});

test("a protocol node whose ports are not its wiring is refused, with nothing of the pack committed", async () => {
  const { info, modules } = protocolPack("lp-ports", { ports: [
    { id: "midi", dir: "in", kind: "events" }, { id: "midiOut", dir: "out", kind: "events" }] });
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-ports"]).toBe("its protocol node rp.lp-ports.echo has the ports midi in events, midiOut out " +
    "events, and its pack.json's protocol.nodes wires in in events, midiOut out events");
  expect(protocolNodeOf("lp-ports")).toBe(undefined);
  expect(placersRegistry.items().some((p) => p.key === "lp-ports-model")).toBe(false);
});

test("a protocol node another package declares, or one with a label, is refused", async () => {
  // A node with mGB's own type and ports, so its ports pass and the table's check is what refuses it.
  const dup = protocolPack("lp-dup", { node: { type: "rp.mgb.midi", midiIn: "midi", writes: ["serialIn"] }, roles: {},
    ports: [{ id: "midi", dir: "in", kind: "events" }, { id: "serialIn", dir: "out", kind: "bytes" }] });
  const labelled = protocolPack("lp-label", { node: { type: "rp.lp-label.echo", label: 30, midiIn: "in", writes: [], midiOut: true } });
  const { host, refused } = fakeHost([dup.info, labelled.info], { ...dup.modules, ...labelled.modules });
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-dup"]).toBe("two packages declare the script node rp.mgb.midi: mgb and lp-dup");
  expect(refused["lp-label"]).toBe('lp-label: the node rp.lp-label.echo has a "label": a span label is the host\'s ' +
    "to give, at load");
});

test("a placer half that is not a placer is refused", async () => {
  const { info, modules } = protocolPack("lp-noplacer");
  (modules["lp-noplacer"] as Record<string, unknown>).placer = { key: "lp-noplacer-model" };
  const { host, refused } = fakeHost([info], modules);
  await loadPacks(host, APP_BUILDERS);
  expect(refused["lp-noplacer"]).toBe("its placer half's default export is not a graph placer (a key and a place " +
    "function)");
});
```

(Adjust the `GraphSystem` literal to the tree's fields if they differ; `build-graph.test.ts`'s `gb()` helper is the
model.)

- [ ] **Step 2: Run them to make sure they fail**

Run: `pnpm test packs/load-packs`
Expected: FAIL to typecheck or run: `PackInfo` has no `protocol`, `HalfKind` has no `"protocol"` or `"placer"`.

- [ ] **Step 3: The host's types**

`packHost.ts`:

```ts
import type { TypeDescriptor } from "@retroplug/core/descriptor";

/**
 * The halves a context loads. `protocol` is data (pack.json's protocol member, carried in the pack list), committed
 * without an import: its code is global code every Engine loads (the native loader). The script half is MicroQuickJS
 * code the native loader registers, never a context's.
 */
export type HalfKind = "app" | "consoles" | "protocol" | "placer" | "ui" | "views" | "cli";

/** A pack's protocol half as the host lists it (docs/downloadable-packs-m2e-plan.md). */
export interface PackProtocol {
  /** pack.json's protocol.nodes as written: the live table's checks hold each entry (graph/protocolSpec.ts). */
  readonly nodes: readonly unknown[];
  /** Each role kind and the node type it is placed as. */
  readonly roles: Readonly<Record<string, string>>;
  /** Each node type's descriptor, as the host's trial load read it. */
  readonly descriptors: Readonly<Record<string, TypeDescriptor>>;
}
```

and in `PackInfo`: `readonly protocol?: PackProtocol;`. Check `TypeDescriptor`'s module with `contracts.mjs`: if
`@retroplug/core/descriptor` is a contract the shell may import (it is listed), import it there; else type the
descriptors as `{ readonly ports: readonly { id: string; dir: string; kind: string }[] }`.

- [ ] **Step 4: The protocol half's additions**

`packages/retroplug/src/packs/protocolHalf.ts`:

```ts
// A pack's protocol half as the live tables' additions (docs/downloadable-packs-m2e-plan.md): its nodes and roles, each
// wired node's ports first held to the descriptor the host's trial load read, so a wiring the planner would refuse is
// the pack's refusal and not every document's. The tables' own checks (graph/protocolSpec.ts) run at the commit.
import type { Addition } from "../contributions/registry";
import { protocolNodesRegistry, roleNodesRegistry } from "../contributions/protocolRegistries";
import { checkProtocolEntry, checkProtocolPorts, isWired, type ProtocolNodeEntry, type ProtocolNodeSpec,
  type RoleNodeEntry } from "../graph/protocolSpec";
import type { PackProtocol } from "./packHost";

export function protocolAdditions(pkg: string, protocol: PackProtocol | undefined): Addition<any>[] {
  if (!protocol) throw new Error("its pack.json names a protocol half, and the host listed no protocol member");
  for (const raw of protocol.nodes) {
    checkProtocolEntry(pkg, raw);
    const entry = raw as ProtocolNodeEntry;
    if (!isWired(entry)) continue;
    const descriptor = Object.prototype.hasOwnProperty.call(protocol.descriptors, entry.type)
      ? protocol.descriptors[entry.type] : undefined;
    if (!descriptor) throw new Error(`its protocol node ${entry.type} has no descriptor (the host did not load it)`);
    checkProtocolPorts(entry.type, entry as ProtocolNodeSpec, descriptor.ports);
  }
  const roles: RoleNodeEntry[] = Object.entries(protocol.roles).map(([kind, node]) => ({ kind, node }));
  return [
    { registry: protocolNodesRegistry, pkg, items: protocol.nodes as ProtocolNodeEntry[] },
    { registry: roleNodesRegistry, pkg, items: roles },
  ];
}
```

- [ ] **Step 5: The loader**

`loadPacks.ts`:

```ts
import { placersRegistry } from "../contributions/placers";
import type { GraphPlacer } from "@retroplug/core/placer";
import { protocolAdditions } from "./protocolHalf";
import type { PackProtocol } from "./packHost";

/** The halves whose content is the manifest's, not a module: committed from the pack list, never imported. */
const DATA_HALVES: ReadonlySet<HalfKind> = new Set<HalfKind>(["protocol"]);

/** A placer half's default export, held to the contract buildGraph calls. */
function placerExport(exported: unknown): GraphPlacer<unknown> {
  const p = objectExport("placer", exported);
  if (typeof p.key !== "string" || p.key === "" || typeof p.place !== "function") {
    throw new Error("its placer half's default export is not a graph placer (a key and a place function)");
  }
  return p as unknown as GraphPlacer<unknown>;
}
```

`APP_BUILDERS` gains, after `consoles`:

```ts
  protocol: (pkg, exported) => protocolAdditions(pkg, exported as PackProtocol | undefined),
  placer: (pkg, exported) => [{ registry: placersRegistry, pkg, items: [placerExport(exported)] }],
```

and its comment says the control plane's halves are the app's registries, the consoles', the protocol tables and the
placers. In `loadUnguarded`'s import loop:

```ts
      for (const kind of todo) {
        if (DATA_HALVES.has(kind)) {
          exported.set(kind, pack.protocol);
          continue;
        }
        const ns = (await importBefore(host, pack.id, kind, deadline)) as { default?: unknown };
        exported.set(kind, ns.default);
      }
```

The header's first lines name M2e's halves beside M2d's.

- [ ] **Step 6: Run the tests**

Run: `pnpm test packs graph && pnpm typecheck && pnpm check:boundaries`
Expected: PASS, every command exit 0. The UI's and the CLI's builders spread `APP_BUILDERS`, so they load the two
halves too; `pnpm test cli ui` must pass unchanged.

- [ ] **Step 7: Commit**

```bash
git add packages/retroplug/src/packs/protocolHalf.ts packages/retroplug/src/packs/loadPacks.ts \
  packages/retroplug/src/packs/packHost.ts packages/retroplug/test/packs/load-packs.test.ts
git commit -m "packs: the loader commits a pack's protocol data and placer half with the pack"
```

---

### Task 7: Building a package's protocol and placer halves as a pack's

**Files:**
- Create: `packages/native/test/packs/ts-fixture/protocol/` (`package.json`, `pack.json`, `ts/app.ts`, `ts/nodes.ts`, `ts/placer.ts`)
- Modify: `tools/build-pack-halves.mjs`, `packages/retroplug/test/script/build-pack-halves.test.ts`,
  `packages/native/test/packs/ts-fixture/tsconfig.json`

- [ ] **Step 1: The fixture package**

`packages/native/test/packs/ts-fixture/protocol/package.json`:

```json
{
  "name": "@retroplug-test/ts-fixture-protocol",
  "private": true,
  "retroplug": {
    "app": "ts/app.ts",
    "protocol": {
      "module": "ts/nodes.ts",
      "placer": "ts/placer.ts",
      "nodes": [{ "type": "rp.tsfix-proto.echo", "midiIn": "in", "writes": [], "midiOut": true }],
      "roles": { "tsfix-proto": "rp.tsfix-proto.echo" }
    }
  }
}
```

`pack.json` (the template):

```json
{
  "id": "tsfix-proto", "name": "TS fixture pack (protocol)", "version": "1.0.0", "apiLevel": 0,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture/protocol",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": ["rp.tsfix-proto.echo"], "roles": ["tsfix-proto"] },
  "halves": {}
}
```

`ts/nodes.ts`:

```ts
// The protocol fixture pack's node (docs/downloadable-packs-m2e-plan.md): each routed MIDI message sent on as host MIDI
// with its second byte 7 up, so a test that drives it sees the pack's code ran. Writes no system input, so it sits
// beside any system's own protocol node.
import { defineNode, events, type DefinedNode } from "@retroplug/core/defineNode";

export const echoNode = defineNode({
  id: "rp.tsfix-proto.echo",
  name: "TS fixture echo",
  category: "protocol",
  ports: { in: events.in(), midiOut: events.out() },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; i++) {
        const d = ms[i].data;
        b.send("midiOut", ms[i].frame, d.length === 3 ? [d[0], (d[1] + 7) & 0x7f, d[2]] : d);
      }
    },
  }),
});

const nodes: readonly DefinedNode<any>[] = [echoNode];
export default nodes;
```

`ts/app.ts`:

```ts
// The protocol fixture pack's app half (docs/downloadable-packs-m2e-plan.md): the role its node is placed as, given to
// every ROM, and the project section its placer reads. The role writes no system input and changes nothing a render
// hears.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{ kind: "tsfix-proto", category: "feature", schema: { parse: () => ({}) } }],
  romProviders: [() => [{ kind: "tsfix-proto", config: {} }]],
  projectSections: [{ key: "tsfixProto", schema: { parse: () => ({}) }, defaults: () => ({}) }],
});
```

`ts/placer.ts`:

```ts
// The protocol fixture pack's placer (docs/downloadable-packs-m2e-plan.md): places nothing, and says on the console
// where buildGraph placed the pack's role, so a render's output shows the protocol data and the placer both committed
// in its worker.
import { definePlacer } from "@retroplug/core/placer";

export default definePlacer<unknown>({
  key: "tsfixProto",
  place(_model, ctx) {
    for (const s of ctx.systems)
      for (const r of s.pipeline) if (r.kind === "tsfix-proto") console.log(`tsfix-proto: placed ${r.node} on system ${s.id}`);
    return {};
  },
});
```

Add the folder to `ts-fixture/tsconfig.json`'s `include` the way `render/` is included (read the file; if it includes
`**/*.ts` already, nothing changes). The romProvider's exact signature follows `render/ts/app.ts`'s.

- [ ] **Step 2: Write the failing tests**

In `build-pack-halves.test.ts`, after the existing cases (each spawns node with a script, as they do):

```ts
test("a package's protocol member is built as a pack's protocol and placer halves", async () => {
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-protocol-"));',
    'const fixture = path.join(repo, "packages/native/test/packs/ts-fixture/protocol");',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const pack = await m.buildPackHalves({ packageDir: fixture, outDir: work, template: path.join(fixture, "pack.json") });',
    '  const bundle = fs.readFileSync(path.join(work, "js/protocol.js"), "utf8");',
    '  const ctx = vm.createContext({});',
    '  vm.runInContext(bundle, ctx);',
    '  console.log(JSON.stringify({ halves: pack.halves, protocol: pack.protocol,',
    '    registered: ctx.__rp_scriptNodes.map((n) => n.id), imports: /^\\s*import\\b/m.test(bundle) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.halves).toEqual({ app: "js/app.js", protocol: "js/protocol.js", placer: "js/placer.js" });
  expect(got.protocol).toEqual({
    nodes: [{ type: "rp.tsfix-proto.echo", midiIn: "in", writes: [], midiOut: true }],
    roles: { "tsfix-proto": "rp.tsfix-proto.echo" },
  });
  expect(got.registered).toEqual(["rp.tsfix-proto.echo"]);
  expect(got.imports).toBe(false);
});

test("risa's real protocol half builds, its labels dropped, and registers its node", async () => {
  // The real package (docs/downloadable-packs-m2e-plan.md): what part 6 will ship as a pack. Built alone (--only), from
  // a template that declares its node.
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url"), vm = require("vm");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-risa-"));',
    'const risa = path.join(repo, "packages/risa");',
    'const template = path.join(work, "template.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "risa", name: "risa", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] },',
    '  provides: { platforms: [], nodeTypes: ["rp.risa.sync"], roles: ["risa-sync"] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = path.join(work, "out");',
    '  const pack = await m.buildPackHalves({ packageDir: risa, outDir: out, template, only: ["protocol"] });',
    '  const ctx = vm.createContext({});',
    '  vm.runInContext(fs.readFileSync(path.join(out, "js/protocol.js"), "utf8"), ctx);',
    '  const manifest = JSON.parse(fs.readFileSync(path.join(risa, "package.json"), "utf8")).retroplug.protocol;',
    '  console.log(JSON.stringify({ protocol: pack.protocol, manifest, registered: ctx.__rp_scriptNodes.map((n) => n.id) }));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '}).catch((e) => { console.error(e.message); process.exit(1); });',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.protocol.nodes).toEqual(got.manifest.nodes.map(({ label: _label, ...rest }: Record<string, unknown>) => rest));
  expect(got.protocol.roles).toEqual(got.manifest.roles);
  expect(got.registered).toEqual(["rp.risa.sync"]);
});

test("a protocol module importing another package, or a kinds member, is refused", async () => {
  // A scratch package: its node module imports @retroplug/sameboy, which a protocol half may not carry; then the same
  // package with protocol.kinds.
  const script = [
    'const fs = require("fs"), os = require("os"), path = require("path"), url = require("url");',
    'const repo = path.resolve(process.cwd(), "../..");',
    'const work = fs.mkdtempSync(path.join(os.tmpdir(), "rp-pack-protocol-bad-"));',
    'const pkg = path.join(work, "pkg");',
    'fs.mkdirSync(path.join(pkg, "ts"), { recursive: true });',
    'fs.writeFileSync(path.join(pkg, "ts/nodes.ts"), "import { x } from \\"@retroplug/sameboy/cartridge\\";\\nexport default [x];\\n");',
    'const manifest = (extra) => ({ name: "bad", private: true, retroplug: { protocol: { module: "ts/nodes.ts",',
    '  nodes: [{ type: "rp.bad.n" }], roles: {}, ...extra } } });',
    'const template = path.join(work, "pack.json");',
    'fs.writeFileSync(template, JSON.stringify({ id: "bad", name: "b", version: "1.0.0", apiLevel: 0, license: "x",',
    '  source: "x", requires: { packs: [], platforms: [] }, provides: { platforms: [], nodeTypes: ["rp.bad.n"],',
    '  roles: [] }, halves: {} }));',
    'import(url.pathToFileURL(path.join(repo, "tools/build-pack-halves.mjs")).href).then(async (m) => {',
    '  const out = {};',
    '  for (const [name, extra] of [["foreign", {}], ["kinds", { kinds: { controllerApps: [] } }]]) {',
    '    fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify(manifest(extra)));',
    '    try { await m.buildPackHalves({ packageDir: pkg, outDir: path.join(work, name), template }); out[name] = "built"; }',
    '    catch (e) { out[name] = e.message; }',
    '  }',
    '  console.log(JSON.stringify(out));',
    '  fs.rmSync(work, { recursive: true, force: true });',
    '});',
  ].join("\n");
  const r = await runNode(script);
  expect(r.status, r.err).toBe(0);
  const got = JSON.parse(r.out);
  expect(got.foreign.includes("imports @retroplug/sameboy/cartridge: a protocol half bundles @retroplug/core alone"),
    got.foreign).toBe(true);
  expect(got.kinds).toBe("retroplug.protocol.kinds: a pack cannot contribute to a contribution kind yet");
});
```

(The existing cases' `runNode` and `text` helpers are the file's. If the test runner's working directory is not
`packages/retroplug`, take `repo` the way the existing cases do.)

- [ ] **Step 3: Run them to make sure they fail**

Run: `pnpm test script/build-pack-halves`
Expected: the three new cases FAIL (no protocol half is built).

- [ ] **Step 4: The tool**

In `tools/build-pack-halves.mjs`, extend the header with the protocol and placer halves (one paragraph: the protocol
member becomes the protocol half, a global-code script-node bundle carrying its own copy of `@retroplug/core`, and
pack.json's `protocol` member; `protocol.placer` becomes the placer half, an ES module like the others; a `kinds` member
is refused until M2f). Then:

```js
const { tsOptions } = createRequire(import.meta.url)("./ts-options.js");

/** Resolves a protocol half's @retroplug imports: its nodes run in an Engine's script contexts, which have no module
 *  loader and no RetroPlug module, so the bundle carries its own copy of what it imports, and of RetroPlug's that is
 *  @retroplug/core alone (the node SDK). Resolved through core's own exports map, so a package outside the workspace
 *  builds the same. */
function protocolImports() {
  const core = join(REPO, "packages/core");
  const exportsMap = JSON.parse(readFileSync(join(core, "package.json"), "utf8")).exports;
  return {
    name: "protocol-imports",
    setup(build) {
      build.onResolve({ filter: /^@retroplug\// }, (args) => {
        const m = /^@retroplug\/core(?:\/(.+))?$/.exec(args.path);
        const target = m ? exportsMap[m[1] ? `./${m[1]}` : "."] : undefined;
        if (typeof target !== "string") {
          return { errors: [{ text: `${args.importer} imports ${args.path}: a protocol half bundles @retroplug/core ` +
            "alone (its nodes run in the script contexts, which hold no other RetroPlug module)" }] };
        }
        return { path: join(core, target) };
      });
    },
  };
}

/** The package's protocol member as the pack's protocol half (see the header). Returns the bundle's file. */
async function buildProtocolHalf({ packageDir, outDir, pack, protocol }) {
  if (protocol.kinds !== undefined) {
    throw new Error("retroplug.protocol.kinds: a pack cannot contribute to a contribution kind yet");
  }
  if (typeof protocol.module !== "string" || protocol.module === "") {
    throw new Error("retroplug.protocol needs a module, the file that default-exports its nodes");
  }
  // The wiring as the manifest spells it, less the labels: a pack's script nodes take theirs at load.
  const nodes = (protocol.nodes ?? []).map(({ label: _label, ...rest }) => rest);
  const types = nodes.map((n) => n.type);
  const declared = pack.provides?.nodeTypes ?? [];
  for (const type of types) {
    if (!declared.includes(type)) {
      throw new Error(`the protocol node ${type} is not in the template's provides.nodeTypes`);
    }
  }
  // The entry registers the module's definitions in the manifest's order, and refuses a module that defines other
  // types than the manifest declares, as the product's generated entry does.
  const entry = [
    `import nodes from ${JSON.stringify(join(packageDir, protocol.module))};`,
    'import { registerScriptNodes } from "@retroplug/core/register";',
    `const ORDER = ${JSON.stringify(types)};`,
    "const ids = nodes.map((n) => n.id);",
    "if (ids.length !== ORDER.length || ids.some((id) => ORDER.indexOf(id) < 0)) {",
    '  throw new Error("the protocol module defines " + ids.join(", ") + ", and the manifest declares " + ORDER.join(", "));',
    "}",
    "registerScriptNodes(nodes.slice().sort((a, b) => ORDER.indexOf(a.id) - ORDER.indexOf(b.id)));",
  ].join("\n");
  await esbuild.build({
    stdin: { contents: entry, resolveDir: packageDir, sourcefile: "protocol-entry.js", loader: "js" },
    bundle: true,
    format: "iife",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    tsconfigRaw: tsOptions(),
    outfile: join(outDir, "js", "protocol.js"),
    plugins: [protocolImports()],
    absWorkingDir: packageDir,
    logLevel: "silent",
  });
  pack.protocol = { nodes, roles: protocol.roles ?? {} };
  return "js/protocol.js";
}
```

In `buildPackHalves`: `delete pack.protocol;` beside `delete pack.script`; the halves loop iterates
`[...HALVES.map((h) => [h, block[h]]), ["placer", block.protocol?.placer]]` (the placer half is built exactly as the
others, an ES module with the contracts external), with `--only` naming `placer` as it names the others; and after the
loop, before the script half:

```js
  if ((!only || only.includes("protocol")) && block.protocol?.module !== undefined) {
    halves.protocol = await buildProtocolHalf({ packageDir, outDir, pack, protocol: block.protocol });
  }
```

A package whose `protocol` member has kinds and no module (`lsdj-launchpad`'s) still reaches `buildProtocolHalf` only
when it has a module; so that a kinds member is refused for it too, test `block.protocol?.kinds !== undefined` there as
well and throw the same message. `esbuild` failures reach the caller as they do for the other halves; if esbuild
reports a plugin error inside an aggregate message, keep the plugin's text in what is thrown (the test looks for it).
If `tools/ts-options.js`'s export is spelt otherwise, follow `build-device-pack.mjs`'s use of it.

- [ ] **Step 5: Run the tests**

Run: `pnpm test script/build-pack-halves && pnpm typecheck`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/build-pack-halves.mjs packages/retroplug/test/script/build-pack-halves.test.ts \
  packages/native/test/packs/ts-fixture/protocol packages/native/test/packs/ts-fixture/tsconfig.json
git commit -m "packs: the pack build writes a package's protocol member as its protocol and placer halves"
```

---

### Task 8: The protocol fixture pack end to end

**Files:**
- Create: `packages/native/test/packs/ts-fixture/variants/tsfix-proto-ports/` (`pack.json.in`, `js/app.js`, `js/protocol.js`)
- Modify: `packages/native/test/packs/ts-fixture/CMakeLists.txt`, `packages/native/cmake/tests.cmake`,
  `packages/native/test/packs/PackProtocol.test.cpp`, `tools/run-cli-ts-tests.sh`, `package.json` (if `test:cli-ts`
  lists the fixture targets it builds)

- [ ] **Step 1: Stage the fixture**

In `ts-fixture/CMakeLists.txt`, build `tsfix-proto` from `protocol/` as `tsfix-render` is built from `render/`:

```cmake
# tsfix-proto: built as tsfix is, from protocol/ (docs/downloadable-packs-m2e-plan.md): an app, a protocol and a placer half.
set(_tsp_dir "${_tsfix_dir}/protocol")
set(_tsp_out "${RP_PACKS_DIR}/tsfix-proto/1.0.0")
file(GLOB _tsp_sources CONFIGURE_DEPENDS "${_tsp_dir}/ts/*.ts")
set(_tsp_halves "${_tsp_out}/js/app.js" "${_tsp_out}/js/protocol.js" "${_tsp_out}/js/placer.js")
add_custom_command(
    OUTPUT "${_tsp_out}/pack.json" ${_tsp_halves}
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs
            ${_tsp_dir} ${_tsp_out} ${_tsp_dir}/pack.json
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/build-pack-halves.mjs ${_tsp_dir}/package.json ${_tsp_dir}/pack.json
            ${_tsp_sources}
            ${CMAKE_SOURCE_DIR}/packages/node-sdk/native/src/graph/rp_pack.h
            ${CMAKE_SOURCE_DIR}/packages/retroplug/scripts/lib/contracts.mjs
            ${CMAKE_SOURCE_DIR}/packages/retroplug/host-modules.json
            # the protocol half bundles core's node SDK
            ${CMAKE_SOURCE_DIR}/packages/core/package.json
    COMMENT "Building the TS protocol fixture pack"
    VERBATIM)
_tsfix_claim(tsfix-proto)
```

add `"${_tsp_out}/pack.json" ${_tsp_halves}` to `retroplug-ts-fixture-packs`' DEPENDS, and `tsfix-proto-ports` to the
variants' `foreach` list. Core's own `ts/*.ts` are not in DEPENDS (the M2c fixture's DEPENDS covers only its own
sources; spec/07 records the DEPFILE question): say so in a comment beside the line.

`variants/tsfix-proto-ports/pack.json.in`:

```json
{
  "id": "tsfix-proto-ports", "name": "TS fixture variant tsfix-proto-ports", "version": "1.0.0", "apiLevel": @RP_API_LEVEL@,
  "license": "MIT", "source": "packages/native/test/packs/ts-fixture",
  "requires": { "packs": [], "platforms": [] },
  "provides": { "platforms": [], "nodeTypes": ["rp.tsfix-proto-ports.echo"], "roles": ["tsfix-proto-ports"] },
  "halves": { "app": "js/app.js", "protocol": "js/protocol.js" },
  "protocol": {
    "nodes": [{ "type": "rp.tsfix-proto-ports.echo", "midiIn": "in", "writes": [], "midiOut": true }],
    "roles": { "tsfix-proto-ports": "rp.tsfix-proto-ports.echo" }
  }
}
```

`variants/tsfix-proto-ports/js/app.js`:

```js
export default { roles: [{ kind: "tsfix-proto-ports", category: "feature", schema: { parse: () => ({}) } }] };
```

`variants/tsfix-proto-ports/js/protocol.js`: `protofix/js/protocol.js`'s shape with one node, `rp.tsfix-proto-ports.echo`,
whose ports are `{ id: "midi", dir: "in", kind: "events" }` and `{ id: "midiOut", dir: "out", kind: "events" }` (the
manifest wires `in`), and whose `process` sends nothing.

In `tests.cmake`, `retroplug-packs-test` depends on `retroplug-ts-fixture-packs` too (beside its other fixture
dependencies), so the built `tsfix-proto` is there when it runs.

- [ ] **Step 2: The built fixture runs in an Engine**

In `PackProtocol.test.cpp`:

```cpp
TEST_CASE("the pack build's protocol half runs in an Engine", "[packs-protocol]") {
    // tsfix-proto, built from its package by tools/build-pack-halves.mjs: what a real pack's protocol half is.
    PackTable table;
    trialTable(table);
    const auto load = table.load(packDir("tsfix-proto"));
    INFO(load.error);
    REQUIRE(load);
    Engine e(48000.0, table);
    const char* doc =
        R"({"nodes":[{"id":"in","type":"rp.host.midi-in"},{"id":"echo","type":"rp.tsfix-proto.echo"},)"
        R"({"id":"out","type":"rp.host.midi-out","settings":{"systems":[1]}}],)"
        R"("wires":[{"from":"in.midi","to":"echo.in"},{"from":"echo.midiOut","to":"out.in1"}]})";
    REQUIRE(e.setGraph({doc, {}}).ok);
    CHECK(runBlock(e, {{0x90, 60, 100}}) == std::vector<MidiOut>{{1, 0, {0x90, 67, 100}}});
}
```

Run: `pnpm configure && cmake --build build --target retroplug-packs-test -j$(nproc) && build/bin/retroplug-packs-test "[packs-protocol]"`
Expected: PASS.

- [ ] **Step 3: The CLI legs**

In `tools/run-cli-ts-tests.sh`, section 8:

- `PACKPATH` gains `$PACKS/tsfix-proto/1.0.0` and `$PACKS/tsfix-proto-ports/1.0.0`.
- The protocol refusal's line changes: `tsfix-protocol` names a protocol half and no member now, so

```bash
contains "packs: a protocol half with no protocol member is refused" "$TMP/packs-help.err" \
	"halves.protocol needs the protocol member"
contains "packs: a protocol node whose ports are not its wiring is refused in TS" "$TMP/packs-help.err" \
	"[packs] pack tsfix-proto-ports is refused: its protocol node rp.tsfix-proto-ports.echo has the ports"
```

- After the render leg (M2d's), a protocol leg that needs no ROM fixture (mGB is in the repository):

```bash
# A render job's worker commits a pack's protocol data and placer, and its Engine plans and runs the pack's node
# (docs/downloadable-packs-m2e-plan.md): the fixture's placer says where its role's node was placed, and the WAV is the
# render's without the pack, since the node writes nothing a core hears.
RP="$TMP/proto-render"
mkdir -p "$RP"
cp "$REPO/packages/mgb/rom/mGB.gb" "$RP/rom.gb"
(cd "$RP" && RETROPLUG_DETERMINISTIC=1 "$CLI" render rom.gb --duration 1s --out plain.wav >plain.out 2>plain.err)
check "packs: a render of mGB with no pack exits 0" 0 $?
(cd "$RP" && RETROPLUG_PACK_PATH="$PACKS/tsfix-proto/1.0.0" RETROPLUG_DETERMINISTIC=1 \
	"$CLI" render rom.gb --duration 1s --out proto.wav >proto.out 2>proto.err)
check "packs: a render with the protocol fixture pack exits 0" 0 $?
contains "packs: the render's placer saw the pack's node placed" "$RP/proto.out" \
	"tsfix-proto: placed rp.tsfix-proto.echo on system"
if cmp -s "$RP/plain.wav" "$RP/proto.wav"; then
	echo "ok   - packs: the protocol fixture leaves the render's bytes as they were"
else
	echo "FAIL - packs: the protocol fixture changed the render's bytes"
	fails=$((fails + 1))
fi
if grep -q "refused" "$RP/proto.err"; then
	echo "FAIL - packs: the render refused something with the protocol fixture: $(grep refused "$RP/proto.err" | head -1)"
	fails=$((fails + 1))
else
	echo "ok   - packs: nothing was refused in the protocol render"
fi
```

Read the M2d render leg first: follow its `contains` stream (the job's console may print to stderr, not stdout: use
whichever file M2d's `tsfix-render: constructed` check reads) and its variable names. If the render's command line
takes the ROM differently, follow it. If `test:cli-ts` in `package.json` builds a list of targets, make sure
`retroplug-ts-fixture-packs` is in it (M2d added it for `tsfix-render`).

- [ ] **Step 4: Run the CLI suite**

Run: `pnpm test:cli-ts`
Expected: exit 0, the new lines `ok`.

- [ ] **Step 5: Commit**

```bash
git add packages/native/test/packs/ts-fixture/CMakeLists.txt packages/native/test/packs/ts-fixture/variants/tsfix-proto-ports \
  packages/native/cmake/tests.cmake packages/native/test/packs/PackProtocol.test.cpp tools/run-cli-ts-tests.sh
git commit -m "packs: the protocol fixture pack built, run in an Engine and placed in a render"
```

(Add `package.json` by name if Step 3 changed it.)

---

### Task 9: The docs

**Files:**
- Modify: `spec/02-native-host.md`, the spec doc that describes the live registries (find it: `grep -rn "placersRegistry\|live registr" spec/`),
  `spec/06-build-test.md`, `spec/07-remaining-work.md`, `spec/08-profiling.md`, `docs/downloadable-packs-loading-design.md`,
  `docs/downloadable-packs-m2e-plan.md` (an "After the reviews" section)

- [ ] **Step 1: The specs**

- spec/02, where M1/M2d describe packs: a pack's protocol half is compiled and trial-loaded once by the table (the
  backend's `protocolTrial`, beside the built-in script nodes), and every Engine loads it at `syncPacks` with
  `ScriptHost::loadAppPack` as `rp.<id>.protocol`, an id no `loadScriptPack` caller may take; its types take the script
  host's next span labels.
- The live registries' doc: the protocol tables are two registries, `protocolNodes` and `roleNodes`, read through
  `protocolNodes()`, `roleNodes()`, `declaredNodeTypes()`, `protocolSpecOf()`; their checks are `protocolSpec.ts`'s,
  run at every commit; `KIND_DATA` stays generated until M2f.
- spec/06: `build-pack-halves.mjs`'s protocol and placer halves (the protocol half bundles `@retroplug/core` alone); the
  fixtures (`protocol-fixture/`'s `protofix` and variants, `ts-fixture/protocol/`'s `tsfix-proto`,
  `tsfix-proto-ports`), and `PackProtocol.test.cpp`'s `[packs-protocol]` in the packs-test description.
- spec/08: a pack's script nodes take the span labels after the built-ins', at load, in the table's pack order.
- spec/07's Downloadable packs entry: what is left of part 1 is M2f (the contribution kinds: `accept` runs once; a kind
  item's runtime half must run in the built-in script contexts, added only when an Engine is built, so a pack installed
  live gets its kind items at the next launch; until then a pack contributing to a kind is refused), M3, M4, M5. Add an
  "Open from M2e" sentence listing this plan's design questions (below). In "The app loads no script pack", the
  parenthesis "a pack's protocol nodes are M2e's" becomes that a pack's protocol nodes reach every Engine through
  `syncPacks`, render jobs' included, and `backend.loadScriptPack` still has no app caller.
- The loading design: section 1's manifest example and bullets gain the `protocol` member and the `placer` half;
  section 3's "Where each half loads" says the protocol half is global code every Engine loads and its data commits in
  the control plane, the render worker and the CLI, with the placer half; "Script-node span labels" is unchanged in
  substance (it already says labels are given at load).

- [ ] **Step 2: Check and commit**

Run: `pnpm check:docs`
Expected: exit 0.

```bash
git add spec/02-native-host.md spec/06-build-test.md spec/07-remaining-work.md spec/08-profiling.md \
  docs/downloadable-packs-loading-design.md docs/downloadable-packs-m2e-plan.md
git commit -m "docs: a pack's protocol half reaches every Engine, and its tables are live"
```

(Add the live registries' spec doc by name.)

---

### Task 10: Verify, then the CI gate

- [ ] **Step 1: The local gate**

Run: `env -u RETROPLUG_RESOURCES_DIR TV_SKIP_SDLCTL=1 .superpowers/verify/tip-verify-par.sh` (the parallel tip verify),
then `pnpm sdl:controller` on its own (the parallel verify skips it), then `pnpm typecheck && pnpm check:boundaries &&
pnpm check:docs`. Compare the run with M2d's (`.superpowers/verify/compare.sh`).
Expected: every leg exit 0 but the known GBA app-cores failure M2c and M2d carry; the comparison shows only additions.

- [ ] **Step 2: The CI gate (needs the user's go-ahead to push)**

Ask the user to push `feature/modularize`; then watch `build.yml`'s run on it and read every job (linux, linux-arm64,
macos, windows `/W4 /WX`). The Windows job is the first MSVC build of `ProtocolTrial.cpp`, `loadAppPack` and the
manifest's protocol member; a warning is fixed, not suppressed.
Expected: all four jobs succeed.

---

## Design questions this plan found

1. **Each Engine loads each pack's protocol bundle into a runtime pair of its own,** as it does the built-in bundle:
   a plugin with many instances pays a pack's script memory once per instance.
2. **A pack refused in TS keeps its protocol node types in every Engine,** as its native types are kept (M2c's
   question 6): nothing places them, and Settings > Packs (M4) shows the pack refused.
3. **The trial loads the built-in script nodes once per protocol pack at launch,** in a scratch script host; a launch
   with many protocol packs could keep one scratch host across a `loadAll`.
4. **A CLI test or run session loads no pack.** Found while planning: `retroplug-cli test` and `run` spawn a child that
   boots the SDK's bundle, which has registries of its own, apart from the dispatcher's module set that
   `loadCliPacks` commits into; so a consumer's test file cannot use a pack's role, console or protocol node. M4's
   `--pack` should decide how a session's SDK gets the process's packs.
5. **Span labels can differ between Engines** if one refuses a pack another accepted (its later packs' types shift);
   the trace names stay right per Engine.
6. **The protocol checks exist twice:** the generator's build-time ones over package manifests and `protocolSpec.ts`'s
   over the live tables, as M2a left the manifest checks in the generator. The messages agree where the rules overlap.
7. **A protocol half's memory limit is the script host's default;** pack.json has no knob for it.
8. **The protocol half's `@retroplug/core` copy is the pack's build's,** not the host's: a pack built against an older
   core runs that core's node helpers in its script contexts. The API level covers the node ABI, not those helpers'
   behaviour.
9. **A pack's placer runs only with a project service publishing its model.** `buildGraph` skips a placer whose key has no
   non-null model, and no pack doc said so; the pack docs now state it (spec/06, the loading design's section 1).
10. **The shell's placer-key check exempts a package with no project sections;** the loader now holds a pack's placer to
    one of its own app half's project sections, a built-in package is not held to it.
11. **The checks do not refuse an `out` naming itself, an `out` with its own `out`, or a role placed as an `out`
    decoder,** and the generator does not either.

## Self-review

**Scope** (the user's split, 2026-11-25): the protocol half, its tables and its placer (Tasks 1 to 8); the contribution
kinds are M2f's and are refused as before.

**Spec coverage** (spec/07's M2e item and the loading design's section 3): the frozen tables become live registries
(Task 2) with the generator's protocol checks run at every commit (Task 1); a pack's protocol nodes reach every Engine
through `loadScriptPack`'s path, `ScriptHost::loadPack` under an app-owned id (Tasks 4, 5), render jobs' Engines
included (they read the same table); its placer reaches the live placer registry (Task 6); span labels are given at load
(Task 5's label case); the build tool and a real package (Task 7); end to end in an Engine and a render (Task 8); docs
(Task 9); CI (Task 10).

**Placeholder scan:** the new modules, functions and fixtures are given whole, but for three tests that lean on the
tree: the earlier-pack clash case and the pack-list case in Task 4 (each names its expected message, and the helpers it
should copy from `PackLoader.test.cpp` and `PackModules.test.cpp`), and the one-type-two-halves manifest case in Task 3
(named by its message, spelt as the file's script cases are). Where a step leans on the tree (`readTextFile`, the
generator's `specifier` helpers, the render leg's output stream, `tsOptions`), it names the file to read and what to
follow.

**Type and name consistency:** `SystemInput`, `ProtocolNodeSpec`, `ProtocolNodeEntry`, `RoleNodeEntry`,
`SYSTEM_INPUTS`, `isWired`, `checkProtocolEntry`, `checkProtocolTable`, `checkRoleNodes`, `portKey`, `portsOf`,
`checkProtocolPorts` (Task 1, used in Tasks 2 and 6); `protocolNodesRegistry`, `roleNodesRegistry`, `protocolNodes()`,
`roleNodes()`, `declaredNodeTypes()`, `protocolSpecOf()`, `ProtocolNodeType = string` (Task 2, used in Task 6);
`ProtocolSpec`, `PackManifest::protocol`, the `placer` half (Task 3, read in Task 4); `ProtocolHalf`, `ProtocolTrial`,
`protocolTrial()`, `PackTable::setProtocolTrial`, `LoadedPack::protocol`, `ScriptHost::loadAppPack`,
`rp.<id>.protocol`, `protofix` and its five variants (Task 4, used in Task 5); `PackProtocol`, `PackInfo::protocol`,
`HalfKind` with `protocol` and `placer`, `DATA_HALVES`, `protocolAdditions`, `placerExport` (Task 6);
`buildProtocolHalf`, `protocolImports`, `tsfix-proto`, `rp.tsfix-proto.echo`, `tsfixProto` (Task 7, used in Task 8);
`tsfix-proto-ports` (Task 8).

## After the reviews

M2e was built as `3de7fe7ec..cf59cf47b` (Tasks 1 to 8) and the docs commit after it (Task 9); Task 10's CI gate waits for
the user's go-ahead to push. Where the task blocks above and the tree differ, the tree wins. What changed from the plan
while it ran, and why:

- The planner bundle's module counts moved (2 to 5, the ceiling 26 to 28), since the readers now import the registries
  and the checks (d330d7d96).
- `checkRoleNodes` refuses one package declaring a kind twice, by name (a761f950a).
- The manifest's `nodesJson` is re-read with `parseJson`, so an integer above INT64_MAX is not wrapped (95aaec484).
- `ScriptHost` requires a loaded pack's types to sit under its id; an app pack (`loadAppPack`) is exempt, since a pack's
  protocol nodes are named by the pack (`rp.risa.sync`, `rp.sms.sync`), and the table and the registry hold them.
  `loadAppPack` refuses an id already current, so an app pack is never reloaded, and the trial names whether the
  built-ins or the pack's nodes failed to register (cb5e713df, e2f80b39a).
- `readTextFile` moved to `PackDirs`, and one `heldBy` pre-check serves the script and protocol halves (cb5e713df).
- `runBlock` and `MidiOut` moved to `PackTestSupport.hpp`; the Engine refused-here path and a live-audio-thread load
  are tested; the span-label case runs only in a `RETROPLUG_PROFILE` build (it passed in one during review) (3b4437bed,
  3cd4c2f70).
- The loader guards malformed host data by name, and a pack's placer half must place one of its own app half's project
  sections (5826248a6).
- The protocol half's bundle starts `"use strict"` like the product's script bundle; `zod` and core subpaths core does
  not export are refused by name, and a module defining an id twice is refused (9b0f59de8, aa5a098ae).
- A placer runs only when a project service publishes a non-null model under its key (`buildGraph` skips one with none),
  so the fixture's app half has a project service, and it imports `@retroplug/tracker/packageContributions` for the
  `projectServices` member's type; the CLI's protocol legs render mGB and smsggdj (the latter where audio is real) with and
  without the pack, byte-identical, and fail on any refused, faulted or `[script]` line (21b225b71, cf59cf47b).

The design questions above gained 9 to 11 while it ran. Task 9 also updated the control plane's and the render worker's
comments that still said the packs' "app and consoles halves".
