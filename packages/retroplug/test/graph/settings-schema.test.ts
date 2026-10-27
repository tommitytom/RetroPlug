// The zod-free settings schema (src/graph/settingsSchema.ts), held to zod: each builder against its zod twin (the same
// shape, each field with a .default), for the JSON Schema it writes and for what defineNode makes of a document's
// settings through it, defaults filled and refusals worded alike.
import { test, expect } from "../../testing/harness";
import { z } from "zod";
import { defineNode, events } from "../../src/graph/defineNode";
import { s, type Infer, type Schema, type SettingsParseResult, type SettingsSchema } from "../../src/graph/settingsSchema";

const noop = () => ({ process() {} });
const show = (v: unknown): string => (typeof v === "number" ? String(v) : (JSON.stringify(v) ?? String(v)));

// A refusal's first issue, where and why.
function first(result: SettingsParseResult<unknown>): { path: readonly PropertyKey[]; message: string } {
  if (result.success) throw new Error(`expected a refusal, got ${show(result.data)}`);
  const issue = result.error.issues[0];
  return { path: issue.path, message: issue.message };
}

// Each builder, and its zod twin. Every one is a field of a root object below, as a node's settings field is.
const twins: { name: string; mine: Schema<unknown>; zod: z.ZodType }[] = [
  { name: "int", mine: s.int({ min: 1, max: 4, default: 2 }), zod: z.number().int().min(1).max(4).default(2) },
  { name: "int with a min", mine: s.int({ min: 0, default: 0 }), zod: z.number().int().min(0).default(0) },
  { name: "int with a max", mine: s.int({ max: 0xffffffff, default: 1 }), zod: z.number().int().max(0xffffffff).default(1) },
  { name: "int with no bounds", mine: s.int({ default: 3 }), zod: z.number().int().default(3) },
  { name: "int past the safe range", mine: s.int({ min: -1e20, max: 1e20, default: 0 }), zod: z.number().int().min(-1e20).max(1e20).default(0) },
  { name: "number", mine: s.number(0.5), zod: z.number().default(0.5) },
  { name: "string", mine: s.string("x"), zod: z.string().default("x") },
  { name: "bool", mine: s.bool(true), zod: z.boolean().default(true) },
  {
    name: "enum",
    mine: s.enum(["off", "midiSync", "masterSync"], "midiSync"),
    zod: z.enum(["off", "midiSync", "masterSync"]).default("midiSync"),
  },
  { name: "enum of one", mine: s.enum(["only"], "only"), zod: z.enum(["only"]).default("only") },
  {
    name: "array",
    mine: s.array(s.int({ min: 0, max: 9, default: 0 }), [1, 2]),
    zod: z.array(z.number().int().min(0).max(9).default(0)).default([1, 2]),
  },
  {
    name: "array of objects",
    mine: s.array(s.object({ id: s.int({ min: 1, default: 1 }), on: s.bool(false) }), []),
    zod: z.array(z.object({ id: z.number().int().min(1).default(1), on: z.boolean().default(false) }).default({ id: 1, on: false })).default([]),
  },
  { name: "json", mine: s.json({ a: [1, { b: 2 }] }), zod: z.unknown().default({ a: [1, { b: 2 }] }) },
  { name: "json null", mine: s.json(null), zod: z.unknown().default(null) },
  {
    name: "object in an object",
    mine: s.object({ x: s.number(1), deep: s.object({ y: s.string(""), ids: s.array(s.int({ default: 0 }), [7]) }) }),
    zod: z
      .object({
        x: z.number().default(1),
        deep: z.object({ y: z.string().default(""), ids: z.array(z.number().int().default(0)).default([7]) }).default({ y: "", ids: [7] }),
      })
      .default({ x: 1, deep: { y: "", ids: [7] } }),
  },
];

const input = { io: "input" } as const;

test("each builder, as a field, writes the JSON Schema its zod twin writes, key for key", () => {
  for (const t of twins)
    expect(s.object({ f: t.mine }).toJSONSchema(input), t.name).toEqual(z.object({ f: t.zod }).toJSONSchema(input));
});

test("an object of every builder writes them in its shape's order, as zod does", () => {
  const mine: Record<string, Schema<unknown>> = {};
  const zod: Record<string, z.ZodType> = {};
  for (let i = 0; i < twins.length; ++i) {
    mine[`f${twins.length - i}`] = twins[i].mine;  // keys not in sorted order, so the order is the shape's
    zod[`f${twins.length - i}`] = twins[i].zod;
  }
  const written = s.object(mine).toJSONSchema(input);
  expect(written).toEqual(z.object(zod).toJSONSchema(input));
  // The input schema: no field is required, and no additionalProperties, so a key it does not name is allowed.
  expect(Object.keys(written)).toEqual(["$schema", "type", "properties"]);
});

test("the root takes no default, so its JSON Schema writes none, as zod's twin with none at its root", () => {
  expect(s.object({}).toJSONSchema(input)).toEqual(z.object({}).toJSONSchema(input));
  expect(s.int({ min: 1, max: 4, default: 2 }).toJSONSchema(input)).toEqual(z.number().int().min(1).max(4).toJSONSchema(input));
  expect(s.enum(["a", "b"], "a").toJSONSchema(input)).toEqual(z.enum(["a", "b"]).toJSONSchema(input));
  const root = { mine: s.object({ n: s.int({ min: 1, default: 1 }) }), zod: z.object({ n: z.number().int().min(1).default(1) }) };
  expect(first(root.mine.safeParse(undefined))).toEqual(first(root.zod.safeParse(undefined)));
  expect(first(root.mine.safeParse(undefined))).toEqual({ path: [], message: "Invalid input: expected object, received undefined" });
});

// What defineNode makes of a document's settings through a schema: the settings, or the one line it throws.
function outcome(settings: SettingsSchema<unknown>, raw: unknown): string {
  const node = defineNode({ id: "rp.test.settings", settings, ports: {}, create: noop });
  try {
    return `ok ${JSON.stringify(node.parseSettings(raw))}`;
  } catch (e) {
    return `refused ${(e as Error).message}`;
  }
}

// Every value tried on every builder: the right type for some, the wrong one for the rest.
const values: unknown[] = [
  undefined, null, true, false, 0, 2, -1, 9, 2.5, 2 ** 53, -(2 ** 53), 1e300, NaN, Infinity, -Infinity, "", "x", "2",
  "midiSync", "only", [], [1, 2], [1, 10, "x"], [{}], [{ id: 2 }], [{ id: "x" }], [null], {}, { y: 5 }, { x: 2 },
  { x: 2, deep: { y: "z", ids: [1, 2.5] } }, { x: "y" }, { deep: [] }, { a: [1] },
];

test("defineNode makes the same of every value through each builder as through its zod twin", () => {
  for (const t of twins) {
    const mine = s.object({ f: t.mine });
    const zod = z.object({ f: t.zod });
    expect(outcome(mine, {}), `${t.name}: nothing`).toBe(outcome(zod, {}));
    for (const v of values) expect(outcome(mine, { f: v }), `${t.name}: ${show(v)}`).toBe(outcome(zod, { f: v }));
  }
});

test("a refusal's first issue has zod's path and message, in the shape's order", () => {
  const mine = s.object({ a: s.int({ min: 1, max: 4, default: 2 }), list: s.array(s.object({ id: s.int({ default: 1 }) }), []) });
  const zod = z.object({
    a: z.number().int().min(1).max(4).default(2),
    list: z.array(z.object({ id: z.number().int().default(1) })).default([]),
  });
  for (const raw of [{ list: [{ id: 1 }, { id: 0.5 }], a: 9 }, { list: [{}, { id: "1" }] }, { list: [{ id: 1 }, null] }, 5, [], null, "x"])
    expect(first(mine.safeParse(raw)), show(raw)).toEqual(first(zod.safeParse(raw)));
  expect(first(mine.safeParse({ list: [{}, { id: "1" }] }))).toEqual({ path: ["list", 1, "id"], message: "Invalid input: expected number, received string" });
  expect(outcome(mine, { list: [{}, { id: 2.5 }] })).toBe(`refused node type "rp.test.settings": settings: list.1.id: Invalid input: expected int, received number`);
  // Every field is checked, in shape order, whatever order the document writes them in.
  const two = mine.safeParse({ list: {}, a: "x" });
  expect(two.success ? [] : two.error.issues.map((i) => i.path.join("."))).toEqual(["a", "list"]);
});

test("the words for each refusal, as zod words them", () => {
  const n = s.object({
    i: s.int({ min: 1, max: 4, default: 2 }),
    e: s.enum(["a", "b"], "a"),
    one: s.enum(["only"], "only"),
    b: s.bool(false),
    list: s.array(s.string(""), []),
  });
  const why = (raw: Record<string, unknown>) => outcome(n, raw).replace(`refused node type "rp.test.settings": settings: `, "");
  expect(why({ i: 9 })).toBe("i: Too big: expected number to be <=4");
  expect(why({ i: 0 })).toBe("i: Too small: expected number to be >=1");
  expect(why({ i: 1.5 })).toBe("i: Invalid input: expected int, received number");
  expect(why({ i: 2 ** 53 })).toBe("i: Too big: expected int to be <=9007199254740991");
  expect(why({ i: "2" })).toBe("i: Invalid input: expected number, received string");
  expect(why({ i: NaN })).toBe("i: Invalid input: expected number, received NaN");
  expect(why({ e: "c" })).toBe(`e: Invalid option: expected one of "a"|"b"`);
  expect(why({ one: "two" })).toBe(`one: Invalid input: expected "only"`);
  expect(why({ b: "true" })).toBe("b: Invalid input: expected boolean, received string");
  expect(why({ list: [1] })).toBe("list.0: Invalid input: expected string, received number");
  expect(why({ list: null })).toBe("list: Invalid input: expected array, received null");
});

test("an object drops a key its shape does not name, at every depth, as z.object strips it", () => {
  const mine = s.object({ a: s.int({ default: 1 }), o: s.object({ b: s.bool(false) }), list: s.array(s.object({ c: s.string("") }), []) });
  const zod = z.object({
    a: z.number().int().default(1),
    o: z.object({ b: z.boolean().default(false) }).default({ b: false }),
    list: z.array(z.object({ c: z.string().default("") })).default([]),
  });
  const raw = { extra: 1, a: 2, o: { b: true, more: [1] }, list: [{ c: "x", d: 4 }, { e: null }] };
  const parsed = mine.safeParse(raw);
  expect(parsed).toEqual({ success: true, data: { a: 2, o: { b: true }, list: [{ c: "x" }, { c: "" }] } });
  expect(parsed.success && parsed.data).toEqual(zod.parse(raw));
});

test("parse fills each default, keeps each value given, and hands back objects of the caller's own", () => {
  const n = s.object({
    i: s.int({ default: 3 }),
    list: s.array(s.int({ default: 0 }), [1, 2]),
    deep: s.object({ tags: s.array(s.string(""), ["t"]) }),
    config: s.json<{ rows: number[] }>({ rows: [4] }),
  });
  const defaults = { success: true, data: { i: 3, list: [1, 2], deep: { tags: ["t"] }, config: { rows: [4] } } };
  const filled = n.safeParse({});
  expect(filled).toEqual(defaults);
  if (!filled.success) return;
  filled.data.list.push(9);
  filled.data.deep.tags.push("u");
  filled.data.config.rows.push(5);
  expect(n.safeParse({})).toEqual(defaults);
  // A value given is kept, and a json value as given, as z.unknown keeps it.
  const config = { rows: [1], other: "kept" };
  const given = n.safeParse({ i: -5, list: [], deep: { tags: [] }, config });
  expect(given).toEqual({ success: true, data: { i: -5, list: [], deep: { tags: [] }, config: { rows: [1], other: "kept" } } });
  expect(given.success && given.data.config).toBe(config);
});

test("a default its own schema refuses is refused when the schema is made", () => {
  expect(() => s.int({ min: 1, max: 4, default: 9 })).toThrow("a settings default must pass its own schema: Too big: expected number to be <=4");
  expect(() => s.int({ default: 0.5 })).toThrow("expected int");
  expect(() => s.enum(["a", "b"], "c" as never)).toThrow(`expected one of "a"|"b"`);
  expect(() => s.array(s.int({ min: 0, default: 0 }), [1, -1])).toThrow("Too small: expected number to be >=0");
  expect(() => s.number(Infinity)).toThrow("expected number");
});

// The settings type, as z.infer gives it: a compile-time check (a mismatch fails pnpm typecheck).
type Equal<A, B> = (<T>() => T extends A ? 1 : 2) extends <T>() => T extends B ? 1 : 2 ? true : false;
const settings = s.object({
  mode: s.enum(["off", "midiSync"], "off"),
  divisor: s.int({ min: 1, max: 16, default: 1 }),
  start: s.bool(false),
  gain: s.number(0),
  name: s.string(""),
  ids: s.array(s.int({ default: 1 }), []),
  inner: s.object({ on: s.bool(true) }),
  config: s.json<{ app: string }>({ app: "x" }),
});
const inferred: Equal<
  Infer<typeof settings>,
  { mode: "off" | "midiSync"; divisor: number; start: boolean; gain: number; name: string; ids: number[]; inner: { on: boolean }; config: { app: string } }
> = true;

test("the settings type is inferred, and a node's create and ports receive it", () => {
  expect(inferred).toBe(true);
  const node = defineNode({
    id: "rp.test.typed",
    settings,
    ports: (o) => ({ out: events.out({ messages: o.divisor }) }),
    create: ({ settings: o }) => ({ process: () => void o.inner.on }),
  });
  const typed: Equal<ReturnType<typeof node.parseSettings>, Infer<typeof settings>> = true;
  expect(typed).toBe(true);
});
