// defineNode: a TS node declares the same contract a C++ node does.
import { test, expect } from "../../testing/harness";
import { z } from "zod";
import { canonicalJson } from "../../src/graph/canonicalJson";
import { audio, bytes, channels, control, defineNode, events, param, resource } from "../../src/graph/defineNode";

const noop = () => ({ process() {} });

test("ports list inputs before outputs, each in declaration order", () => {
  const n = defineNode({
    id: "rp.test.order",
    ports: { out: audio.out(channels.stereo()), in: audio.in(), midi: events.in(), level: control.out() },
    create: noop,
  });
  expect(n.descriptor().ports.map((p) => p.id)).toEqual(["in", "midi", "out", "level"]);
});

test("ports and params can follow settings", () => {
  const add = defineNode({
    id: "rp.test.add",
    settings: z.object({ inputs: z.number().int().min(1).max(16).default(2) }),
    ports: (s) => {
      const ports: Record<string, ReturnType<typeof audio.in>> = {};
      for (let k = 1; k <= s.inputs; ++k) ports[`in${k}`] = audio.in(k === 1 ? channels.any() : channels.match("in1"));
      ports.out = audio.out(channels.match("in1"));
      return ports;
    },
    create: noop,
  });
  expect(add.descriptor().ports.length).toBe(3);
  expect(add.layout(add.parseSettings({ inputs: 4 })).ports.length).toBe(5);
  expect(() => add.parseSettings({ inputs: 99 })).toThrow();
});

test("a node without settings declares the same empty schema C++ does", () => {
  const n = defineNode({ id: "rp.test.bare", ports: {}, create: noop });
  expect(n.descriptor().settings).toEqual({ type: "object" });
});

test("a node with settings ships the JSON Schema of what a document may write", () => {
  const n = defineNode({
    id: "rp.test.schema",
    settings: z.object({ maxSamples: z.number().int().min(1).default(48000) }),
    ports: {},
    create: noop,
  });
  const schema = n.descriptor().settings as { properties?: Record<string, unknown>; required?: string[]; additionalProperties?: unknown };
  expect(Object.keys(schema.properties ?? {})).toEqual(["maxSamples"]);
  // A defaulted field is optional and extra keys are allowed, as parseSettings (and C++) treat them.
  expect(schema.required).toBe(undefined);
  expect(schema.additionalProperties).toBe(undefined);
  expect(n.parseSettings({}).maxSamples).toBe(48000);
  expect(n.parseSettings({ maxSamples: 5, extra: 1 }).maxSamples).toBe(5);
});

test("settings are a JSON object or nothing, and every field needs a default", () => {
  const n = defineNode({ id: "rp.test.bare", ports: {}, create: noop });
  expect(n.parseSettings(undefined)).toEqual({});
  expect(() => n.parseSettings("x")).toThrow("JSON object");
  expect(() => n.parseSettings(null)).toThrow("JSON object");
  expect(() => n.parseSettings([1])).toThrow("JSON object");
  expect(() => defineNode({ id: "rp.test.nodefault", settings: z.object({ rom: z.string() }), ports: {}, create: noop })).toThrow(
    "needs a default",
  );
});

test("refused settings say which field and why, in one line a planner error can carry", () => {
  const n = defineNode({
    id: "rp.test.refused",
    settings: z.object({ outs: z.number().int().min(1).max(4).default(2), name: z.string().default("a") }),
    ports: {},
    create: noop,
  });
  expect(() => n.parseSettings({ outs: 9 })).toThrow(`node type "rp.test.refused": settings: outs: Too big: expected number to be <=4`);
  expect(() => n.parseSettings({ name: 5 })).toThrow("settings: name: ");
  let message = "";
  try {
    n.parseSettings({ outs: 0, name: 5 });
  } catch (e) {
    message = (e as Error).message;
  }
  expect(message.includes("\n")).toBe(false);
});

test("duplicate ids and bad enum defaults are rejected", () => {
  const dup = defineNode({
    id: "rp.test.dup",
    ports: { gain: audio.in() },
    params: { gain: param.float({ name: "Gain", min: 0, max: 1, default: 0 }) },
    create: noop,
  });
  expect(() => dup.descriptor()).toThrow("more than one");
  const badEnum = defineNode({
    id: "rp.test.enum",
    ports: {},
    params: { mode: param.enum({ name: "Mode", values: ["a", "b"], default: "c" }) },
    create: noop,
  });
  expect(() => badEnum.descriptor()).toThrow("not one of its values");
});

test("param ranges and channel specs are checked as the C++ parser checks them", () => {
  const bad = (params: Record<string, ReturnType<typeof param.float>>, ports = {}) =>
    () => defineNode({ id: "rp.test.bad", ports, params, create: noop }).descriptor();
  expect(bad({ g: param.float({ name: "G", min: 5, max: 1, default: 1 }) })).toThrow("min must not exceed max");
  expect(bad({ g: param.float({ name: "G", min: 0, max: 1, default: 2 }) })).toThrow("within");
  expect(bad({ n: param.int({ name: "N", min: 0.5, max: 3, default: 1 }) })).toThrow("whole-number");
  expect(bad({ m: param.enum({ name: "M", values: ["a", "a"], default: "a" }) })).toThrow("twice");
  expect(bad({}, { out: audio.out(channels.any() as never) })).toThrow('cannot be "any"');  // (a type error too)
  expect(bad({}, { out: audio.out(channels.match("nope")) })).toThrow("not another audio port");
  expect(bad({}, { in: audio.in(channels.fixed(2, "mono")) })).toThrow("is 1 channels");
  expect(bad({ g: param.float({ name: "G", min: 0, max: Infinity, default: 0 }) })).toThrow("finite");
  // an unknown layout name, not an Object.prototype key
  expect(bad({}, { in: audio.in(channels.fixed(2, "constructor")) })().ports[0].channels).toEqual({ count: 2, layout: "constructor" });
  expect(bad({}, { in: audio.in(channels.fixed(3, "discrete(03)")) })).toThrow("not a discrete(n)");
  expect(() =>
    defineNode({ id: "rp.test.ex", ports: { "pins.a": events.out() }, exclusive: [["pins.*", "pins.a"]], create: noop }).descriptor(),
  ).toThrow("overlap");
  expect(() => defineNode({ id: "rp.test\tx", ports: {}, create: noop })).toThrow("whitespace");
  expect(() => defineNode({ id: "rp.test\u0000x", ports: {}, create: noop })).toThrow("control characters");
  expect(() => defineNode({ id: "rp.test\u007f", ports: {}, create: noop })).toThrow("control characters");
  defineNode({ id: "rp.test.caf\u00e9\u00a0", ports: {}, create: noop });  // non-ASCII is fine, as in C++
  const unitless = defineNode({ id: "rp.test.unitless", ports: {}, params: { g: param.float({ name: "G", min: 0, max: 1, default: 0, unit: "" }) }, create: noop });
  expect("unit" in unitless.descriptor().params[0]).toBe(false);
});

test("resource ports carry their type and write access", () => {
  const rec = defineNode({
    id: "rp.test.rec",
    ports: { target: resource.write("audio-buffer"), buffer: resource.out("audio-buffer") },
    create: noop,
  });
  expect(rec.descriptor().ports).toEqual([
    { id: "target", dir: "in", kind: "resource", resourceType: "audio-buffer", access: "write" },
    { id: "buffer", dir: "out", kind: "resource", resourceType: "audio-buffer" },
  ]);
});

test("port shapes are checked as the C++ parser checks them", () => {
  const shaped = (spec: unknown) => () => defineNode({ id: "rp.test.shape", ports: { p: spec as never }, create: noop }).descriptor();
  expect(shaped({ dir: "in", kind: "audio" })).toThrow("needs channels");
  expect(shaped({ dir: "in", kind: "events", channels: { any: true } })).toThrow("only audio ports");
  expect(shaped({ dir: "in", kind: "link" })).toThrow("link ports");
  expect(shaped({ dir: "out", kind: "resource", resourceType: "t", access: "write" })).toThrow("only a resource input");
  expect(shaped({ dir: "in", kind: "events", resourceType: "t" })).toThrow("only resource ports");
  expect(shaped({ dir: "in", kind: "audio", channels: { any: true, count: 2, layout: "stereo" } })).toThrow("exactly one");
  expect(shaped({ dir: "in", kind: "audio", channels: { any: false } })).toThrow("must be true");
  expect(shaped({ dir: "in", kind: "audio", channels: { count: 4294967296, layout: "discrete(4294967296)" } })).toThrow("whole channel count");
  expect(shaped({ dir: "in", kind: "resource" })).toThrow("resourceType");
  expect(shaped({ dir: "sideways", kind: "events" })).toThrow("unknown dir");
});

test("an events or bytes output declares its queue capacity, either part, as C++ writes it", () => {
  const n = defineNode({
    id: "rp.test.capacity",
    ports: {
      midi: events.in(),
      sysex: bytes.out({ messages: 4, bytes: 65536 }),
      serial: bytes.out({ bytes: 1 }),
      notes: events.out(),
    },
    create: noop,
  });
  expect(n.descriptor().ports).toEqual([
    { id: "midi", dir: "in", kind: "events" },
    { id: "sysex", dir: "out", kind: "bytes", capacity: { messages: 4, bytes: 65536 } },
    { id: "serial", dir: "out", kind: "bytes", capacity: { bytes: 1 } },
    { id: "notes", dir: "out", kind: "events" },
  ]);
  const port = (spec: unknown) => () => defineNode({ id: "rp.test.cap", ports: { q: spec as never }, create: noop }).descriptor();
  // the caps themselves; an undefined part is no part, as canonicalJson (and so C++) sees it
  expect(port(events.out({ messages: 1048576, bytes: 16777216 }))().ports[0].capacity).toEqual({ messages: 1048576, bytes: 16777216 });
  expect(port(events.out({ messages: 2, bytes: undefined }))().ports[0]).toEqual({ id: "q", dir: "out", kind: "events", capacity: { messages: 2 } });
});

test("a queue capacity is refused where Descriptor.cpp refuses it", () => {
  const port = (spec: unknown) => () => defineNode({ id: "rp.test.cap", ports: { q: spec as never }, create: noop }).descriptor();
  expect(port(events.out({ messages: 1048577 }))).toThrow('port "q": capacity.messages must be a whole number from 1 to 1048576');
  expect(port(bytes.out({ bytes: 16777217 }))).toThrow('port "q": capacity.bytes must be a whole number from 1 to 16777216');
  expect(port(events.out({ messages: 0 }))).toThrow("from 1 to 1048576");
  expect(port(events.out({ messages: 2.5 }))).toThrow("whole number");
  expect(port(events.out({ bytes: -1 }))).toThrow("from 1 to 16777216");
  expect(port({ dir: "out", kind: "events", capacity: { messages: "2" } })).toThrow("whole number");
  expect(port(events.out({}))).toThrow("capacity needs messages, bytes or both");
  expect(port(events.out({ messages: undefined }))).toThrow("capacity needs messages, bytes or both");
  expect(port({ dir: "out", kind: "events", capacity: 8 })).toThrow("capacity must be an object");
  expect(port({ dir: "out", kind: "events", capacity: [2] })).toThrow("capacity must be an object");
  expect(port({ dir: "out", kind: "events", capacity: { mesages: 2 } })).toThrow('capacity: unknown field "mesages"');
  expect(port({ dir: "out", kind: "control", capacity: { messages: 2 } })).toThrow("only events and bytes ports have capacity");
  expect(port({ dir: "in", kind: "audio", channels: { any: true }, capacity: { messages: 2 } })).toThrow("only events and bytes ports have capacity");
});

test("an input's capacity is refused: a wire carries its source's queue", () => {
  const port = (spec: unknown) => () => defineNode({ id: "rp.test.cap", ports: { q: spec as never }, create: noop }).descriptor();
  // Worded as Descriptor.cpp words it after the port.
  const why = `port "q": only an output declares a capacity: a wire carries its source's queue`;
  expect(port({ dir: "in", kind: "events", capacity: { messages: 512 } })).toThrow(why);
  expect(port({ dir: "in", kind: "bytes", capacity: { bytes: 8192 } })).toThrow(why);
  expect(port({ dir: "in", kind: "control", capacity: { messages: 2 } })).toThrow("only events and bytes ports have capacity");
  expect(port(events.in())().ports[0]).toEqual({ id: "q", dir: "in", kind: "events" });
  // @ts-expect-error an input takes no capacity, so naming one does not type-check
  expect(port(bytes.in({ bytes: 8192 }))().ports[0]).toEqual({ id: "q", dir: "in", kind: "bytes" });
});

test("a capacity a ports function computes from settings is checked as each layout is made", () => {
  const n = defineNode({
    id: "rp.test.sized",
    settings: z.object({ messages: z.number().int().default(256) }),
    ports: (s) => ({ out: events.out({ messages: s.messages }) }),
    create: noop,
  });
  expect(n.descriptor().ports[0].capacity).toEqual({ messages: 256 });
  expect(n.layout(n.parseSettings({ messages: 1048576 })).ports[0].capacity).toEqual({ messages: 1048576 });
  expect(() => n.layout(n.parseSettings({ messages: 1048577 }))).toThrow(
    'rp.test.sized: port "out": capacity.messages must be a whole number from 1 to 1048576',
  );
  expect(() => n.layout(n.parseSettings({ messages: 0 }))).toThrow("from 1 to 1048576");
});

test("ids are plain, unambiguous and keep their order", () => {
  const node = (ports: Record<string, unknown>) => () => defineNode({ id: "rp.test.ids", ports: ports as never, create: noop }).descriptor();
  expect(node({ "a b": events.in() })).toThrow("whitespace");
  expect(node({ ".*": events.out() })).toThrow("'*'");
  expect(node({ "1": events.in() })).toThrow("in order");
  expect(node({ "x\udc00": events.in() })).toThrow("control characters");  // a lone surrogate C++ cannot read
  expect(() => defineNode({ id: "rp.p.\ud800", ports: {}, create: noop })).toThrow("control characters");
  expect(defineNode({ id: "rp.p.\ud83c\udfb5", ports: {}, create: noop }).descriptor().id).toBe("rp.p.\ud83c\udfb5");  // a whole pair is fine
});

test("metadata, params and settings are typed and readable as C++ reads them", () => {
  const meta = (extra: Record<string, unknown>) => () => defineNode({ id: "rp.test.meta", ports: {}, create: noop, ...extra } as never).descriptor();
  expect(meta({ version: 5 })).toThrow("version must be a string");
  expect(meta({ name: "Gain\ud800" })).toThrow("name must be a string");
  expect(meta({ requires: { heap: "yes" } })).toThrow("true or false");
  expect(meta({ params: { b: { type: "bool", name: "B", default: false, unit: 5 } } })).toThrow("unit must be a string");
  // a stray field is not emitted (C++ would drop it when it spells the descriptor back), a missing name is the id
  const stray = meta({ params: { b: { type: "bool", default: true, min: 3, foo: 1 } } })();
  expect(stray.params[0]).toEqual({ id: "b", name: "b", type: "bool", default: true });
  expect(meta({ settings: z.object({ tag: z.string().default("\ud800") }) })).toThrow("lone surrogate");
  // an undefined member is no member, as canonicalJson (and so C++) sees it
  expect(meta({ ports: { in: { dir: "in", kind: "audio", channels: { any: true, count: undefined } } } })().ports[0].channels).toEqual({ any: true, count: undefined });
  expect(meta({ ports: { r: resource.in("audio buffer") } })).toThrow("resourceType");
  expect(() => defineNode({ id: "rp.test.rootless", settings: z.number() as never, ports: {}, create: noop })).toThrow("needs a default (");
});

test("how the script host runs a node is the definition's, never its descriptor's", () => {
  const settings = z.object({ outs: z.number().int().min(1).max(4).default(1) });
  const ports = (s: { outs: number }) => Object.fromEntries(Array.from({ length: s.outs }, (_, i) => [`out${i + 1}`, events.out()]));
  const kept = defineNode({
    id: "rp.test.kept",
    reshapes: true,
    faultOnThrow: true,
    settings,
    ports,
    create: ({ settings: first }) => {
      let outs = first.outs;
      return {
        process() {},
        // The new settings, already checked, typed as the definition's.
        onSettings(next) {
          outs = next.outs;
        },
        portsChanged() {
          void outs;
        },
      };
    },
  });
  expect(kept.reshapes).toBe(true);
  expect(kept.faultOnThrow).toBe(true);
  const plain = defineNode({ id: "rp.test.kept", settings, ports, create: noop });
  expect(plain.reshapes).toBe(undefined);
  expect(plain.faultOnThrow).toBe(undefined);
  expect(canonicalJson(kept.descriptor())).toBe(canonicalJson(plain.descriptor()));
  // Given, each is true: the host reads it as given, so a false would read as a promise it does not make.
  expect(() => defineNode({ id: "rp.test.x", ports: {}, create: noop, reshapes: false } as never)).toThrow("reshapes is true or absent");
  expect(() => defineNode({ id: "rp.test.x", ports: {}, create: noop, faultOnThrow: 1 } as never)).toThrow("faultOnThrow is true or absent");
});

test("a returned descriptor is the caller's own copy", () => {
  const n = defineNode({
    id: "rp.test.copy",
    ports: { "pins.a": events.out(), "pins.b": events.out() },
    params: { m: param.enum({ name: "M", values: ["a", "b"], default: "a" }) },
    exclusive: [["pins.a", "pins.b"]],
    create: noop,
  });
  const first = n.descriptor();
  first.exclusive![0].push("pins.a");
  (first.params[0] as { values: string[] }).values.push("a");
  (first.settings as Record<string, unknown>).type = "array";
  const second = n.descriptor();
  expect(second.exclusive).toEqual([["pins.a", "pins.b"]]);
  expect((second.params[0] as { values: string[] }).values).toEqual(["a", "b"]);
  expect(second.settings).toEqual({ type: "object" });
});
