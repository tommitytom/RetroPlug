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
// The protocol node each role kind is placed as (the generated ROLE_NODES): the rule's role must be the kind of the
// role its node belongs to.
const ROLE_NODES = { "lsdj-sync": "rp.lsdj.sync", "mgb-midi": "rp.mgb.midi" };

const app = (id: string): ControllerAppContribution => ({
  id,
  label: id,
  config: { parse: (v: unknown) => (v ?? {}) as Record<string, unknown> },
});

/** The check over one app carrying `syncOverride`, with no other data. */
function refuse(syncOverride: unknown): void {
  controllerAppsKind.check!([app("a")], { a: { syncOverride } }, { nodeTypes: NODE_TYPES, roleNodes: ROLE_NODES });
}

/** The same, for a rule that is well formed but for what the case is about: its role is the one its node belongs to. */
function refuseWith(syncOverride: Record<string, unknown>): void {
  refuse({ role: "lsdj-sync", ...syncOverride });
}

test("a syncOverride that is not an object is refused", () => {
  expect(() => refuse(null)).toThrow('the controller app a\'s syncOverride must be an object, not null');
  expect(() => refuse("midiMap")).toThrow('must be an object, not "midiMap"');
  expect(() => refuse([])).toThrow("must be an object, not []");
});

test("a syncOverride key nothing reads is refused, naming the keys there are", () => {
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", of: "off" }))
    .toThrow('has "of", which nothing reads (its keys are node, role, mode, listening, off, divisor)');
});

test("a syncOverride with no node or no mode is refused", () => {
  expect(() => refuseWith({ mode: "midiMap" }))
    .toThrow('needs a "node": the cart sync node type its mode is set on');
  expect(() => refuseWith({ node: "", mode: "midiMap" })).toThrow('needs a "node"');
  expect(() => refuseWith({ node: "rp.lsdj.sync" })).toThrow('needs a "mode": the mode that node is put in');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "" })).toThrow('needs a "mode"');
});

test("an off mode with no listening value is refused", () => {
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", off: "off" }))
    .toThrow('names an "off" mode and no "listening" value, so the mode it is for is never used');
});

test("a listening value and its off mode must both be strings, and off must differ from mode", () => {
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: 3, off: "off" }))
    .toThrow('"listening" must be the polled SYNC value that counts as listening, not 3');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "" , off: "off" }))
    .toThrow('"listening" must be the polled SYNC value');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap" }))
    .toThrow('needs an "off": the mode a cart polled at anything but "MidiMap" is put in');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "" }))
    .toThrow('needs an "off"');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "midiMap" }))
    .toThrow('names "midiMap" as both its mode and the mode a cart that is not listening is put in');
});

test("a divisor must name the setting that divides the node's clock: a non-empty string", () => {
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", divisor: 2 }))
    .toThrow('\'s "divisor" must name the setting that divides that node\'s 24-tick clock, not 2');
  expect(() => refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", divisor: "" })).toThrow('\'s "divisor" must name');
});

test("a role must name the kind of the role the node belongs to: a non-empty string", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", role: 3 }))
    .toThrow('\'s "role" must be the kind of the role that node belongs to, not 3');
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap", role: "" })).toThrow('\'s "role" must be the kind');
});

test("a syncOverride with no role is refused: the menu could not tell a placed cart from a stored one", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", mode: "midiMap" }))
    .toThrow('needs a "role": the kind of the role that node belongs to');
});

test("a role whose protocol node is not the rule's node is refused, naming both", () => {
  expect(() => refuse({ node: "rp.lsdj.sync", role: "mgb-midi", mode: "midiMap" }))
    .toThrow('role "mgb-midi" is placed as "rp.mgb.midi", not the "rp.lsdj.sync" its node names');
  expect(() => refuse({ node: "rp.lsdj.sync", role: "no-such-role", mode: "midiMap" }))
    .toThrow('role "no-such-role" has no protocol node, so it is not the role of "rp.lsdj.sync"');
});

test("a node type the product wires no protocol node for is refused", () => {
  expect(() => refuseWith({ node: "rp.lsdj.syncs", mode: "midiMap" }))
    .toThrow('names "rp.lsdj.syncs", which no package declares a protocol node for');
});

test("a well-formed override passes, with or without a listening value", () => {
  // A bare call: the harness has no negated `toThrow`, and a throw here fails the case.
  refuseWith({ node: "rp.lsdj.sync", mode: "midiMap" });
  refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off" });
  refuseWith({ node: "rp.lsdj.sync", mode: "midiMap", listening: "MidiMap", off: "off", divisor: "tempoDivisor" });
});

test("accept fills the app registry and hands the menu the data it applies", () => {
  controllerAppsKind.accept([app("mine"), app("other")], {
    mine: { syncOverride: { node: "rp.lsdj.sync", role: "lsdj-sync", mode: "lsdj" } },
  });
  expect(controllerApps.list().map((a) => a.id)).toEqual(["mine", "other"]);
  expect(controllerApps.get("mine")?.label).toBe("mine");
  // The menu's read: the same data, through the rule the placer applies to a cart.
  expect(controllerSyncOverride([{ id: 1 }], { app: "mine", target: "system", systemId: 1, cartSync: null }))
    .toEqual({ systemId: 1, node: "rp.lsdj.sync", mode: "lsdj", driving: true });
  // An app that declares no rule drives no mode.
  expect(controllerSyncOverride([{ id: 1 }], { app: "other", target: "system", systemId: 1, cartSync: null }))
    .toBe(null);
});
