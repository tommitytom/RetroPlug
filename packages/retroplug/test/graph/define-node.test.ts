// defineNode: a TS node declares the same contract a C++ node does.
import { test, expect } from "../../testing/harness";
import { z } from "zod";
import { audio, channels, control, defineNode, events, param, resource } from "../../src/graph/defineNode";

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

test("a node with settings ships zod's JSON Schema", () => {
  const n = defineNode({
    id: "rp.test.schema",
    settings: z.object({ maxSamples: z.number().int().min(1).default(48000) }),
    ports: {},
    create: noop,
  });
  const schema = n.descriptor().settings as { properties?: Record<string, unknown> };
  expect(Object.keys(schema.properties ?? {})).toEqual(["maxSamples"]);
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
  expect(bad({}, { out: audio.out(channels.any()) })).toThrow('cannot be "any"');
  expect(bad({}, { out: audio.out(channels.match("nope")) })).toThrow("not another audio port");
  expect(bad({}, { in: audio.in(channels.fixed(2, "mono")) })).toThrow("is 1 channels");
  expect(bad({ g: param.float({ name: "G", min: 0, max: Infinity, default: 0 }) })).toThrow("finite");
  bad({}, { in: audio.in(channels.fixed(2, "constructor")) })();  // an unknown layout name, not an Object.prototype key
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
