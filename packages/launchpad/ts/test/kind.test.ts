// The controllerApps contribution kind's control-plane half (ts/control/kind.ts): the checks over a contributing
// manifest's `syncOverride`, and the two things `accept` fills. This is the real kind, not a fixture's copy: the
// generator's suite holds the generic mechanism over a fixture kind, and a package's own data rules are tested by the
// package that owns them (docs/modular-graph-phase-f5a-design.md section 6.5).
import { test, expect } from "@retroplug/testing";
import controllerAppsKind from "../control/kind";
import { controllerSyncOverride } from "../control/placer";
import { controllerApps, type ControllerAppContribution } from "../registry";

// The product's wired node types, as the generated registry hands them: the sync node an override names is a node the
// graph places, so a type no product wires can never be set.
const NODE_TYPES = ["rp.lsdj.sync", "rp.mgb.midi"];

const app = (id: string): ControllerAppContribution => ({
  id,
  label: id,
  config: { parse: (v: unknown) => (v ?? {}) as Record<string, unknown> },
});

/** The check over one app carrying `syncOverride`, with no other data. */
function refuse(syncOverride: unknown): void {
  controllerAppsKind.check!([app("a")], { a: { syncOverride } }, { nodeTypes: NODE_TYPES });
}

test("a syncOverride that is not an object is refused", () => {
  expect(() => refuse(null)).toThrow('the controller app a\'s syncOverride must be an object, not null');
  expect(() => refuse("midiMap")).toThrow('must be an object, not "midiMap"');
  expect(() => refuse([])).toThrow("must be an object, not []");
});

test("a syncOverride key nothing reads is refused, naming the keys there are", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", of: "off" }))
    .toThrow('has "of", which nothing reads (its keys are node, mode, listening, off)');
});

test("a syncOverride with no node or no mode is refused", () => {
  expect(() => refuse({ mode: "midiMap" }))
    .toThrow('needs a "node": the cart sync node type its mode is set on');
  expect(() => refuse({ node: "", mode: "midiMap" })).toThrow('needs a "node"');
  expect(() => refuse({ node: "rp.lsdj.sync" })).toThrow('needs a "mode": the mode that node is put in');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "" })).toThrow('needs a "mode"');
});

test("an off mode with no listening value is refused", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", off: "off" }))
    .toThrow('names an "off" mode and no "listening" value, so the mode it is for is never used');
});

test("a listening value and its off mode must both be strings, and off must differ from mode", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: 3, off: "off" }))
    .toThrow('"listening" must be the polled SYNC value that counts as listening, not 3');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "" , off: "off" }))
    .toThrow('"listening" must be the polled SYNC value');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap" }))
    .toThrow('needs an "off": the mode a cart polled at anything but "MidiMap" is put in');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "" }))
    .toThrow('needs an "off"');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "midiMap" }))
    .toThrow('names "midiMap" as both its mode and the mode a cart that is not listening is put in');
});

test("a node type the product wires no protocol node for is refused", () => {
  expect(() => refuse({ node: "rp.lsdj.syncs", mode: "midiMap" }))
    .toThrow('names "rp.lsdj.syncs", which no package declares a protocol node for');
});

test("a well-formed override passes, with or without a listening value", () => {
  // A bare call: the harness has no negated `toThrow`, and a throw here fails the case.
  refuse({ node: "rp.lsdj.sync", mode: "midiMap" });
  refuse({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off" });
});

test("accept fills the app registry and hands the menu the data it applies", () => {
  controllerAppsKind.accept([app("mine"), app("other")], { mine: { syncOverride: { node: "rp.lsdj.sync", mode: "lsdj" } } });
  expect(controllerApps.list().map((a) => a.id)).toEqual(["mine", "other"]);
  expect(controllerApps.get("mine")?.label).toBe("mine");
  // The menu's read: the same data, through the rule the placer applies to a cart.
  expect(controllerSyncOverride([{ id: 1 }], { app: "mine", target: "system", systemId: 1, cartSync: null }))
    .toEqual({ systemId: 1, node: "rp.lsdj.sync", mode: "lsdj", driving: true });
  // An app that declares no rule drives no mode.
  expect(controllerSyncOverride([{ id: 1 }], { app: "other", target: "system", systemId: 1, cartSync: null }))
    .toBe(null);
});
