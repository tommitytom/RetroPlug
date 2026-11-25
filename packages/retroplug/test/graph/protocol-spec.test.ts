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
