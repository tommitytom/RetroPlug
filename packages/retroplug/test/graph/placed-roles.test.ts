// The readers that decide what a system is ask what its graph places, not what its stored role list holds
// (docs/known-bugs-batch-2-design.md section 1). A stored list keeps a role the graph leaves out: an mGB cart can still
// carry lsdj-sync after a ROM swap, an LSDj cart mgb, and protocolPipeline places one writer per system input. So the
// tracker a system resolves, the config rows its menu shows, the DAW lanes it exposes and the cart views a package
// gates on `isPlaced` follow placement, while the stored role and its config stay, so a ROM swapped back brings them
// with it.
import { test, expect } from "@retroplug/testing";
import { MidiRouting } from "@retroplug/core/midiRouting";
import { isPlaced, type RoleInstance, type RoleType } from "@retroplug/core/systemRoles";
import type { RoleRow } from "@retroplug/ui/contribution";
import { MGB_PARAMETERS } from "@retroplug/mgb/parameters";
import { placedKinds, protocolNodeOf } from "../../src/graph/protocolNodes";
import { resolveTracker } from "../../src/trackerRegistry";
import { projectParameterMap } from "../../src/parameterMap";
import { buildAppRegistry } from "../../src/appHost";
import { SystemsStore } from "../../src/systemsStore";
import { MockBackend } from "../../testing/mockBackend";
import { roleRowItems } from "../../ui/screens/menu/configRows";
import { lsdjRom } from "../systems/fixtures";

const LSDJ_SYNC: RoleInstance = { kind: "lsdj-sync", config: { mode: "midiSync", tempoDivisor: 1, autoStart: false } };
const MGB: RoleInstance = { kind: "mgb", config: {} };
const SAMEBOY: RoleInstance = { kind: "sameboy", config: {} };
const ASSETS: RoleInstance = { kind: "lsdj-assets", config: { overrides: [] } };

/** An mGB cart that still stores the lsdj-sync it carried before a ROM swap: both nodes write serialIn, and a load puts
 *  the ROM's own role first, so mgb is placed and lsdj-sync left out. */
const MGB_CART: readonly RoleInstance[] = [SAMEBOY, MGB, LSDJ_SYNC];
/** The reverse: an LSDj cart that still stores mgb. */
const LSDJ_CART: readonly RoleInstance[] = [SAMEBOY, LSDJ_SYNC, MGB, ASSETS];

test("placedKinds lists each kind the graph places once, in role order", () => {
  expect(placedKinds(MGB_CART)).toEqual(["sameboy", "mgb"]);
  expect(placedKinds(LSDJ_CART)).toEqual(["sameboy", "lsdj-sync", "lsdj-assets"]);
  expect(placedKinds([MGB, MGB, ASSETS, ASSETS])).toEqual(["mgb", "lsdj-assets"]);
  // Nodes that write different inputs are all placed, beside a marker role with none.
  const nes = [{ kind: "mesen" }, { kind: "risa" }, { kind: "nes-n8-midi" }, { kind: "risa-sync" }];
  expect(placedKinds(nes)).toEqual(["mesen", "risa", "nes-n8-midi", "risa-sync"]);
  expect(placedKinds([])).toEqual([]);
});

test("isPlaced reads the kinds a system view says its graph places, not its stored roles", () => {
  expect(isPlaced({ placed: ["sameboy", "mgb"] }, "mgb")).toBe(true);
  expect(isPlaced({ placed: ["sameboy", "mgb"] }, "lsdj-sync")).toBe(false);
  expect(isPlaced({ placed: [] }, "sameboy")).toBe(false);
});

test("the system view says which of its stored roles the graph places, and keeps the ones it leaves out", () => {
  const be = new MockBackend("/cfg");
  const store = new SystemsStore(be, undefined, buildAppRegistry());
  be.seed("/roms/mgb.gb", lsdjRom("MGB"));
  be.seed("/roms/lsdj.gb", lsdjRom("LSDJ"));
  // An mGB ROM at a path a project stored with LSDj's role, and the reverse: the load puts the ROM's own role first.
  const mgbId = store.adopt({ romPath: "/roms/mgb.gb", core: "sameboy", roles: [SAMEBOY, LSDJ_SYNC] })!;
  const lsdjId = store.adopt({ romPath: "/roms/lsdj.gb", core: "sameboy", roles: [SAMEBOY, MGB] })!;
  const view = (id: number) => store.view().find((v) => v.id === id)!;
  expect(view(mgbId).roles.map((r) => r.kind)).toEqual(["sameboy", "mgb", "lsdj-sync"]);
  expect(view(mgbId).placed).toEqual(["sameboy", "mgb"]);
  expect(view(lsdjId).roles.map((r) => r.kind)).toEqual(["sameboy", "lsdj-sync", "mgb", "lsdj-assets"]);
  expect(view(lsdjId).placed).toEqual(["sameboy", "lsdj-sync", "lsdj-assets"]);
  expect(isPlaced(view(mgbId), "lsdj-sync")).toBe(false);
  expect(isPlaced(view(lsdjId), "lsdj-sync")).toBe(true);
  // The view's answer is the document's: the kinds with a protocol node it lists are the pipeline the graph is built
  // from.
  for (const id of [mgbId, lsdjId]) {
    const system = store.graphSystems().find((s) => s.id === id)!;
    expect(view(id).placed.filter((kind) => protocolNodeOf(kind) !== undefined)).toEqual(
      system.pipeline.map((p) => p.kind),
    );
  }
});

test("a tracker is resolved by its placed marker role: an mGB cart storing a stale lsdj-sync resolves none", () => {
  expect(resolveTracker(MGB_CART)).toBe(undefined);
  expect(resolveTracker(LSDJ_CART)?.id).toBe("lsdj");
  // risa's marker has no node, so its presence is its placement.
  expect(resolveTracker([{ kind: "mesen", config: {} }, { kind: "risa", config: {} }])?.id).toBe("risa");
});

test("a role's config rows show only when the role is placed, and a role with no node's when it is present", () => {
  const defaults = { tempoDivisor: 1, model: "x" };
  const roleType = (kind: string): RoleType =>
    ({ kind, schema: { parse: (v: Record<string, unknown>) => ({ ...defaults, ...v }) } }) as unknown as RoleType;
  const divisor: RoleRow = {
    id: "lsdj-divisor",
    label: "Tempo Divisor",
    role: "lsdj-sync",
    key: "tempoDivisor",
    choices: [1, 2, 4, 8].map((value) => ({ value, label: String(value) })),
  };
  const model: RoleRow = {
    id: "sameboy-model",
    label: "Model",
    role: "sameboy",
    key: "model",
    choices: [{ value: "x", label: "X" }],
  };
  const ids = (roles: readonly RoleInstance[]) =>
    roleRowItems([divisor, model], { platform: "gb", roles }, roleType, () => {}).map((i) => i.id);
  expect(ids(MGB_CART)).toEqual(["sameboy-model"]);
  expect(ids(LSDJ_CART)).toEqual(["lsdj-divisor", "sameboy-model"]);
});

test("the DAW lanes are the first placed role's with a table: an LSDj cart storing a stale mgb exposes none", () => {
  const registry = buildAppRegistry();
  const lanes = (roles: readonly RoleInstance[]) =>
    projectParameterMap([{ roles: [...roles] }], MidiRouting.SendToAll, registry);
  // LSDj declares no lanes, so its cart claims no slot, where the stale mgb stored after its lsdj-sync claimed mGB's.
  expect(lanes(LSDJ_CART)).toEqual([]);
  expect(lanes(MGB_CART).length).toBe(MGB_PARAMETERS.length);
  // A role with no node keeps its table beside a placed node of another kind (BlipToaster's chip beside the N8's MIDI).
  const blip = lanes([
    { kind: "mesen", config: {} }, { kind: "nes-n8-midi", config: {} }, { kind: "bliptoaster", config: {} },
  ]);
  const blipTable = registry.roleType("bliptoaster")!.parameters!({});
  expect(blipTable.length).toBeGreaterThan(0);
  expect(blip.length).toBe(blipTable.length);
});
