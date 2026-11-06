// defineNode and its helpers at work, line by line, as a device pack: the device-pack toolchain
// (scripts/build-device-pack.mjs) runs it under Node and as MicroQuickJS bytecode, and the two transcripts must match
// byte for byte (test/graph/device-pack.test.ts). What a device pack's definitions resolve to, refuse and say is then
// the same on the box's engine as here. Every line is the helpers' own work: nothing printed is an engine's message.
import {
  audio, bytes, channels, control, defineNode, events, layoutChannels, link, param, resource, startValue,
} from "../../../src/graph/defineNode";
import type { Block, Message, PortSpec } from "../../../src/graph/defineNode";
import { plainId, wellFormed } from "../../../src/graph/descriptor";
import { s } from "../../../src/graph/settingsSchema";
import { registerScriptNodes } from "../../../src/script/register";

// The device's runtime of a pack (the box's bundle, built with RP_DEVICE_RUNTIME: graph/defineNode.ts) keeps a
// definition's id, create and layout alone, so what the desktop's checks, settings parse and descriptor make of each
// definition is printed by the full bundle only (the transcript the two engines must agree on); the runtime defines the
// same nodes, which the toolchain then runs through the same blocks as the full bundle's.
declare const RP_DEVICE_RUNTIME: boolean | undefined;
const full = typeof RP_DEVICE_RUNTIME === "undefined";
const log = (line: string): void => {
  if (full) console.log(line);
};
const noop = () => ({ process() {} });

// What f does: "accepted", or the message it threw. Only the helpers' own Errors reach here.
function attempt(what: string, f: () => unknown): void {
  try {
    f();
    log(`${what}: accepted`);
  } catch (e) {
    log(`${what}: ${(e as Error).message}`);
  }
}

function ownKey<T>(key: string, value: T): Record<string, T> {
  const map: Record<string, T> = {};
  Object.defineProperty(map, key, { value, enumerable: true, writable: true, configurable: true });
  return map;
}

// ---- the descriptor's own rules ----

const layouts = ["mono", "stereo", "5.1", "7.1", "discrete(3)", "discrete(0)", "discrete(4294967296)", "any", "quad"];
for (const name of layouts)
  log(`layoutChannels(${name}) = ${String(layoutChannels(name))}`);
for (const id of ["a", "", "a b", "tab\t", "del\u007f", "lone\ud800", "pair😀", "rp.core.midi-filter"])
  log(`plainId(${JSON.stringify(id)}) = ${plainId(id)}, wellFormed = ${wellFormed(id)}`);

// ---- a node with every port kind a device pack may have, every param type, and an exclusive group ----

const everything = defineNode({
  id: "rp.test.portable.everything",
  version: "2.1.0",
  name: "Everything",
  category: "test",
  unique: true,
  ports: {
    out: events.out({ messages: 64, bytes: 1024 }),
    "out.alt": events.out({ messages: 8 }),
    midi: events.in(),
    serial: bytes.in(),
    serialOut: bytes.out({ match: "serial" }),
    gate: control.in(),
    level: control.out(),
    rom: resource.in("rp.rom"),
    sram: resource.write("rp.sram"),
    made: resource.out("rp.sample"),
    cart: link(),
  },
  params: {
    gain: param.float({ name: "Gain", min: -60, max: 12, default: 0, unit: "dB" }),
    steps: param.int({ name: "Steps", min: 1, max: 16, default: 4, unit: "" }),
    on: param.bool({ name: "On" }),
    mode: param.enum({ name: "Mode", values: ["a", "b", "c"], default: "b" }),
  },
  exclusive: [["out", "level"], ["out.*"]],
  create: noop,
});
if (full) log(`everything: ${JSON.stringify(everything.descriptor())}`);

// ---- settings, and ports that follow them ----

const fanIn = defineNode({
  id: "rp.test.portable.fan-in",
  settings: s.object({
    inputs: s.int({ min: 1, max: 8, default: 2 }),
    mode: s.enum(["merge", "first"], "merge"),
    thru: s.bool(true),
    scale: s.number(0.5),
    label: s.string("fan"),
    taps: s.array(s.object({ at: s.int({ default: 0 }), on: s.bool(false) }),
                  [{ at: 3 } as { at: number; on: boolean }]),
    blob: s.json<{ k: number }>({ k: 1 }),
  }),
  ports: (settings) => {
    const ports: Record<string, PortSpec> = {};
    for (let k = 1; k <= settings.inputs; ++k) ports[`in${k}`] = events.in();
    ports.out = events.out({ match: "in1" });
    return ports;
  },
  params: (settings): Record<string, ReturnType<typeof param.bool>> =>
    settings.thru ? { thru: param.bool({ name: "Thru", default: true }) } : {},
  reshapes: true,
  create: noop,
});
if (full) {
  log(`fan-in defaults: ${JSON.stringify(fanIn.parseSettings({}))}`);
  log(`fan-in, 4 inputs: ${JSON.stringify(fanIn.layout(fanIn.parseSettings({ inputs: 4, thru: false })))}`);
  log(`fan-in, taps: ${JSON.stringify(fanIn.parseSettings({ taps: [{ on: true }, { at: 7 }], toString: 1 }))}`);
}
const refusedSettings = [{ inputs: 99 }, { inputs: 1.5 }, { inputs: "2" }, { mode: "x" }, { taps: [1] },
                         { scale: "x" }];
for (const raw of [...refusedSettings, [], null, "x", 3])
  attempt(`fan-in settings ${JSON.stringify(raw)}`, () => fanIn.parseSettings(raw));
if (full) log(`fan-in schema: ${JSON.stringify(fanIn.descriptor().settings)}`);

// ---- what a device pack refuses, and what every pack refuses ----

attempt("an audio input", () =>
  defineNode({ id: "rp.test.portable.audio", ports: { in: audio.in(), out: events.out() }, create: noop }));
attempt("an audio output", () =>
  defineNode({
    id: "rp.test.portable.audio-out",
    ports: { midi: events.in(), out: audio.out(channels.stereo()) },
    create: noop,
  }));
// Defined, since its default layout has none, and refused the moment a layout would.
const audioLater = defineNode({
  id: "rp.test.portable.audio-later",
  settings: s.object({ withAudio: s.bool(false) }),
  ports: (st): Record<string, PortSpec> => (st.withAudio ? { sound: audio.in() } : { midi: events.in() }),
  create: noop,
});
attempt("audio by settings", () => audioLater.layout(audioLater.parseSettings({ withAudio: true })));

// A definition with nothing but what each case gives: no ports, a create that does nothing.
const node = (given: Record<string, unknown>) =>
  defineNode({ id: "rp.t", ports: {}, create: noop, ...given } as never);

const refusals: [string, () => unknown][] = [
  ["a bad id", () => node({ id: "rp test" })],
  ["a bad dir", () => node({ ports: { x: { dir: "up", kind: "events" } as unknown as PortSpec } })],
  ["a bad kind", () => node({ ports: { x: { dir: "in", kind: "video" } as unknown as PortSpec } })],
  ["a link that is not", () => node({ ports: { x: { dir: "in", kind: "link" } as unknown as PortSpec } })],
  ["an input's capacity", () =>
    node({ ports: { x: { dir: "in", kind: "events", capacity: { messages: 2 } } as unknown as PortSpec } })],
  ["capacity match and size", () =>
    node({ ports: { i: events.in(), o: events.out({ match: "i", messages: 2 } as never) } })],
  ["capacity stray field", () => node({ ports: { o: events.out({ messages: 2, frames: 3 } as never) } })],
  ["capacity empty", () => node({ ports: { o: events.out({ messages: undefined }) } })],
  ["capacity zero", () => node({ ports: { o: events.out({ messages: 0 }) } })],
  ["capacity fraction", () => node({ ports: { o: bytes.out({ bytes: 1.5 }) } })],
  ["capacity too big", () => node({ ports: { o: events.out({ messages: 1048577 }) } })],
  ["capacity not an object", () => node({ ports: { o: events.out(7 as never) } })],
  ["capacity match a bytes input", () => node({ ports: { i: bytes.in(), o: events.out({ match: "i" }) } })],
  ["capacity match nothing", () => node({ ports: { o: bytes.out({ match: "nope" }) } })],
  ["a port and a param share an id", () =>
    node({ ports: { x: events.in() }, params: { x: param.bool({ name: "X" }) } })],
  // Own keys made with defineProperty: an assignment, or a computed key as the ES5 lowering spells it (one), sets the
  // prototype under Node and an own key under MicroQuickJS, which has no __proto__ accessor.
  ["__proto__ twice", () =>
    node({ ports: ownKey("__proto__", events.in()), params: ownKey("__proto__", param.bool({ name: "P" })) })],
  ["toString once", () => node({ ports: { toString: events.in(), constructor: events.out() } })],
  ["an integer-like id", () => node({ ports: { ["7"]: events.in() } })],
  ["a star in an id", () => node({ ports: { "a*": events.in() } })],
  ["a resource port's type", () => node({ ports: { r: resource.in("") } })],
  ["a written resource output", () =>
    node({ ports: { r: { dir: "out", kind: "resource", resourceType: "x", access: "write" } as never } })],
  ["an enum twice", () => node({ params: { m: param.enum({ name: "M", values: ["a", "b", "a"], default: "a" }) } })],
  ["an enum's default", () => node({ params: { m: param.enum({ name: "M", values: ["a"], default: "z" }) } })],
  ["an empty enum", () => node({ params: { m: param.enum({ name: "M", values: [], default: "" }) } })],
  ["a float's infinity", () => node({ params: { f: param.float({ name: "F", min: 0, max: 1 / 0, default: 0 }) } })],
  ["an int's fraction", () => node({ params: { i: param.int({ name: "I", min: 0, max: 9, default: 2.5 }) } })],
  ["min above max", () => node({ params: { f: param.float({ name: "F", min: 2, max: 1, default: 1 }) } })],
  ["default outside", () => node({ params: { f: param.float({ name: "F", min: 0, max: 1, default: 2 }) } })],
  ["a bool's default", () => node({ params: { b: { type: "bool", name: "B", default: 1 } as never } })],
  ["a param type", () => node({ params: { b: { type: "complex", name: "B" } as never } })],
  ["a unit", () => node({ params: { b: { type: "bool", name: "B", default: true, unit: 3 } as never } })],
  ["unique false", () => node({ unique: false as never })],
  ["reshapes false", () => node({ reshapes: false as never })],
  ["requires.heap", () => node({ requires: { heap: "yes" as never } })],
  ["a version", () => node({ version: 5 as never })],
  ["an empty exclusive group", () => node({ ports: { o: events.out() }, exclusive: [[]] })],
  ["an exclusive input", () => node({ ports: { i: events.in(), o: events.out() }, exclusive: [["i"]] })],
  ["exclusive overlap", () =>
    node({ ports: { "o.a": events.out(), "o.b": events.out() }, exclusive: [["o.*", "o.a"]] })],
  ["exclusive twice", () => node({ ports: { o: events.out() }, exclusive: [["o", "o"]] })],
  ["an integer-like settings field", () => node({ settings: s.object({ ["7"]: s.bool(false) }) })],
  ["one in an array's items", () =>
    node({ settings: s.object({ taps: s.array(s.object({ ["0"]: s.int({ default: 1 }) }), []) }) })],
  ["an int schema's bounds", () => s.int({ min: 3, max: 1, default: 2 })],
  ["an int schema's default", () => s.int({ min: 0, max: 9, default: 12 })],
  ["an enum schema twice", () => s.enum(["x", "y", "x"], "x")],
  ["an array schema's default", () => s.array(s.int({ default: 0 }), [1, "2" as never])],
];
for (const [what, f] of refusals) attempt(what, f);

// ---- a node at work, on a block of the script host's shape ----

const counter = defineNode({
  id: "rp.test.portable.counter",
  settings: s.object({ step: s.int({ min: 1, max: 4, default: 1 }) }),
  ports: { in: events.in(), out: events.out({ match: "in" }), count: control.out(), rate: control.in() },
  create: ({ settings }) => {
    let total = 0;
    const out = new Uint8Array(2);
    return {
      process(b: Block) {
        const ms = b.messages("in");
        for (let i = 0; i < ms.length; i++) {
          total += settings.step;
          out[0] = ms[i].data[0];
          out[1] = (ms[i].data[1] + total) & 0x7f;
          b.send("out", ms[i].frame, out, ms[i].flags);
        }
        b.emit("count", b.frames - 1, total + startValue(b.control("rate")));
      },
    };
  },
});
const sent: string[] = [];
const message = (frame: number, data: number[]): Message => ({ frame, data: new Uint8Array(data), flags: 0 });
const block = {
  frames: 32,
  transport: { sampleRate: 48000, tempo: 120, ppq: 0, playing: false },
  audio: () => [],
  messages: () => [message(0, [0x90, 60]), message(5, [0x80, 126])],
  send: (port: string, frame: number, data: ArrayLike<number>, flags?: number) => {
    sent.push(`${port}@${frame}=${Array.prototype.join.call(data, ",")}/${flags ?? 0}`);
    return true;
  },
  forward: (port: string, m: Message): boolean => block.send(port, m.frame, m.data, m.flags),
  control: () => ({ start: 10, changes: [{ frame: 0, value: 20 }, { frame: 0, value: 30 }, { frame: 9, value: 40 }] }),
  emit: (port: string, frame: number, value: number) => {
    sent.push(`${port}@${frame}=${value}`);
    return true;
  },
} as Block;
if (full) {
  const instance = counter.create({ settings: counter.parseSettings({ step: 3 }), sampleRate: 48000, maxFrames: 64 });
  instance.process(block);
  instance.process(block);
  log(`counter sent: ${sent.join(" ")}`);
}

registerScriptNodes([everything, fanIn, audioLater, counter]);
