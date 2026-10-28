// Node settings without zod (docs/modular-graph-phase-b3b-design.md section 3). A bundle of script nodes is loaded
// into both of an Engine's QuickJS runtimes, the control side that checks settings and lays out ports and the audio
// side that runs process, and with zod in the bundle it was most of it (about 567 KB of 660 KB) though the audio side
// never checks a thing; MicroQuickJS (the Pico) cannot carry it at all. This is the part of zod a node's settings use,
// and no more:
//
//   const settings = s.object({
//     mode: s.enum(["off", "midiSync", "masterSync"], "off"),
//     tempoDivisor: s.int({ min: 1, max: 16, default: 1 }),
//     autoStart: s.bool(false),
//   });
//   type Settings = Infer<typeof settings>;  // { mode: "off" | "midiSync" | "masterSync"; tempoDivisor: number; ... }
//
// defineNode takes any SettingsSchema, so a zod schema serves as well; zod stays where the app uses it on the control
// plane (the role configs, the project files). Each builder does what its zod twin does with the same shape (z.object,
// z.enum, z.number().int().min().max(), z.boolean(), z.number(), z.string(), z.array(), z.unknown(), each field with a
// .default), held to zod's own output by test/graph/settings-schema.test.ts:
//
// - safeParse returns zod's result, each issue with zod's path and zod's English message, word for word (but for an
//   enum of integer-like strings, whose refusal zod lists reordered, "1"|"2"|"10" for ["2","10","1"]; this lists them
//   as declared), so defineNode's one-line `field: why` reads the same. Every field is checked, in shape order, and a
//   value refused gets one issue: the first zod reports for it.
// - Every schema has a default, which a field takes when it is missing or undefined; an object's is its fields'
//   defaults. The value safeParse is handed is never missing (parseSettings reads nothing as {}), so the root takes no
//   default and its JSON Schema writes none, as zod's twin has none at its root. An array item is read the same way, so
//   an undefined item takes the item schema's default (as it does in zod); the default is also written to the JSON
//   Schema, as zod writes an item's .default.
// - A default is read through its own schema when the schema is built: an array's default has each item normalized
//   (an object item gets its fields' defaults, which zod does not do: its .default() returns a default as given), and a
//   default that schema refuses throws. An int's bounds must be
//   finite numbers with min no greater than max, or the int throws.
// - An enum lists its values in declaration order in the JSON Schema (zod's may order integer-like strings such as
//   "1", "2" differently), and a value named twice throws where zod's enum collapses it.
// - An object drops a key its shape does not name, as z.object does: it strips, and refuses nothing. So a document may
//   carry a field this build does not read (one a later version adds) and still be read, and its input JSON Schema
//   says as much by writing no additionalProperties. A C++ node's settings (rfl::DefaultIfMissing) ignore one too.
// - A value is checked, never coerced: "2" is no int. An int is a safe integer, as zod's .int() requires.
// - toJSONSchema writes what zod's toJSONSchema({ io: "input" }) writes, key for key and in its order: the schema of
//   what a document may write, so no field is required. It is the only form there is.
// - A default is copied each time it is taken, and an object or array parsed is a new one, so what safeParse returns
//   is the caller's to edit. A json value given is kept as given, as z.unknown keeps it.
//
// It keeps to ES5's library (no Map, Set, Object.entries, includes, Number.isInteger and the like), the rule the script
// core follows so it can run on MicroQuickJS under check:portable (docs/modular-graph-design.md, the Pico row), so it
// is no obstacle there; defineNode itself is not yet.

/** A refusal, as a zod issue carries one: where (the field names and item indices from the root), and why. */
export interface SettingsIssue {
  readonly path: readonly PropertyKey[];
  readonly message: string;
}

export type SettingsParseResult<T> =
  | { success: true; data: T }
  | { success: false; error: { issues: readonly SettingsIssue[] } };

/** What defineNode takes as a node's settings: an `s` schema, or a zod one, which has the same two methods.
 *  safeParse fills defaults and refuses with issues; toJSONSchema({ io: "input" }) is the descriptor's settings. */
export interface SettingsSchema<T> {
  safeParse(raw: unknown): SettingsParseResult<T>;
  toJSONSchema(opts: { io: "input" }): unknown;
}

type Path = (string | number)[];
type Issues = { path: Path; message: string }[];
/** Checks a value that is present: the value to keep, or an issue pushed (and what it returns is not used). */
type Check<T> = (value: unknown, path: Path, issues: Issues) => T;
type Keywords = Record<string, unknown>;

const MAX_SAFE = 9007199254740991;  // Number.MAX_SAFE_INTEGER, which ES5 has no name for
const DIALECT = "https://json-schema.org/draft/2020-12/schema";

const hasOwn = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

// A fresh copy of a JSON value, so a default taken is the caller's own.
function copy<T>(value: T): T {
  if (value === null || typeof value !== "object") return value;
  if (Array.isArray(value)) {
    const out: unknown[] = [];
    for (let i = 0; i < value.length; ++i) out.push(copy(value[i]));
    return out as T;
  }
  const from = value as Record<string, unknown>;
  const out: Record<string, unknown> = {};
  const keys = Object.keys(from);
  for (let i = 0; i < keys.length; ++i) out[keys[i]] = copy(from[keys[i]]);
  return out as T;
}

// zod's name for what it was given, in an invalid_type issue: the typeof, but for "NaN", "null" and "array". (zod also
// names a class instance by its constructor, which no JSON holds.)
function received(value: unknown): string {
  if (value === null) return "null";
  if (Array.isArray(value)) return "array";
  if (typeof value === "number" && value !== value) return "NaN";
  return typeof value;
}

const invalidType = (expected: string, value: unknown): string => `Invalid input: expected ${expected}, received ${received(value)}`;

/** One schema, made by the builders on `s`. */
export class Schema<T> implements SettingsSchema<T> {
  private readonly fallback: T;
  private readonly check: Check<T>;
  /** Its own JSON Schema keywords, in zod's order: all but the default, which the object or array holding it writes. */
  private readonly keywords: () => Keywords;

  /** A default its own check refuses is a mistake in the definition, refused here rather than at every parse. The
   *  default is copied, so the caller's own value is never the one taken. */
  constructor(fallback: T, check: Check<T>, keywords: () => Keywords) {
    this.fallback = copy(fallback);
    this.check = check;
    this.keywords = keywords;
    const issues: Issues = [];
    check(fallback, [], issues);
    if (issues.length > 0) throw new Error(`a settings default must pass its own schema: ${issues[0].message}`);
  }

  safeParse(raw: unknown): SettingsParseResult<T> {
    const issues: Issues = [];
    const data = this.check(raw, [], issues);
    return issues.length > 0 ? { success: false, error: { issues } } : { success: true, data };
  }

  /** What zod's toJSONSchema({ io: "input" }) writes, whatever is asked: the input form is the only one. */
  toJSONSchema(_opts?: { io?: "input" }): Keywords {
    return { $schema: DIALECT, ...this.keywords() };
  }

  /** A field's or an item's value: its default when it is missing, else the value checked. */
  read(value: unknown, path: Path, issues: Issues): T {
    return value === undefined ? copy(this.fallback) : this.check(value, path, issues);
  }

  /** A field's or an item's JSON Schema: its default first, where zod writes a .default, then its own keywords. */
  describe(): Keywords {
    return { default: copy(this.fallback), ...this.keywords() };
  }
}

/** The settings type a schema gives, as z.infer gives a zod schema's. */
export type Infer<S> = S extends Schema<infer T> ? T : never;

type Fields = Record<string, Schema<unknown>>;

function object<F extends Fields>(fields: F): Schema<{ [K in keyof F]: Infer<F[K]> }> {
  type Out = { [K in keyof F]: Infer<F[K]> };
  const keys = Object.keys(fields);
  const check: Check<Out> = (value, path, issues) => {
    const out: Record<string, unknown> = {};
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
      issues.push({ path, message: invalidType("object", value) });
      return out as Out;
    }
    const given = value as Record<string, unknown>;
    // Own keys only, where zod reads inherited ones too: a parsed JSON object inherits Object.prototype, so a field
    // named for one of its members (toString, constructor) given no value must read as missing, not as that member.
    for (let i = 0; i < keys.length; ++i) {
      const key = keys[i];
      out[key] = fields[key].read(hasOwn(given, key) ? given[key] : undefined, path.concat(key), issues);
    }
    return out as Out;
  };
  // Its default is what it reads from an empty object: each field's.
  return new Schema(check({}, [], []), check, () => {
    const properties: Keywords = {};
    for (let i = 0; i < keys.length; ++i) properties[keys[i]] = fields[keys[i]].describe();
    return { type: "object", properties };
  });
}

// A value of one JSON type, refused as zod refuses any other.
function primitive<T>(type: string, is: (value: unknown) => boolean, fallback: T): Schema<T> {
  const check: Check<T> = (value, path, issues) => {
    if (!is(value)) issues.push({ path, message: invalidType(type, value) });
    return value as T;
  };
  return new Schema(fallback, check, () => ({ type }));
}

// z.number() refuses NaN and both infinities (an infinity as "received number", zod's words).
const isNumber = (value: unknown): value is number => typeof value === "number" && isFinite(value);

/** A whole number, a safe integer, from min to max when they are given. */
function int(o: { min?: number; max?: number; default: number }): Schema<number> {
  const { min, max } = o;
  if ((min !== undefined && !isNumber(min)) || (max !== undefined && !isNumber(max))) {
    throw new Error(`an int's bounds must be finite numbers, got min ${String(min)} and max ${String(max)}`);
  }
  if (min !== undefined && max !== undefined && min > max) throw new Error(`an int's min ${min} is above its max ${max}`);
  // In zod's order: the type, then .int() (whole, then safe), then .min(), then .max().
  const check: Check<number> = (value, path, issues) => {
    const why = !isNumber(value) ? invalidType("number", value)
      : Math.floor(value) !== value ? invalidType("int", value)
      : value > MAX_SAFE ? `Too big: expected int to be <=${MAX_SAFE}`
      : value < -MAX_SAFE ? `Too small: expected int to be >=${-MAX_SAFE}`
      : min !== undefined && value < min ? `Too small: expected number to be >=${min}`
      : max !== undefined && value > max ? `Too big: expected number to be <=${max}`
      : null;
    if (why !== null) issues.push({ path, message: why });
    return value as number;
  };
  // zod writes both bounds for an int, the safe range's where none is given or the safe range is tighter.
  return new Schema(o.default, check, () => ({
    type: "integer",
    minimum: min !== undefined && min > -MAX_SAFE ? min : -MAX_SAFE,
    maximum: max !== undefined && max < MAX_SAFE ? max : MAX_SAFE,
  }));
}

/** One of a list of strings, listed in the JSON Schema in the order given. */
function oneOf<const V extends readonly [string, ...string[]]>(values: V, fallback: NoInfer<V[number]>): Schema<V[number]> {
  const list: string[] = values.slice();
  for (let i = 0; i < list.length; ++i) {
    if (list.indexOf(list[i]) !== i) throw new Error(`an enum names "${list[i]}" twice (${list.map((v) => `"${v}"`).join(", ")})`);
  }
  // zod's words: one value alone is expected outright, more are listed quoted and joined by "|".
  const why = list.length === 1
    ? `Invalid input: expected "${list[0]}"`
    : `Invalid option: expected one of ${list.map((v) => `"${v}"`).join("|")}`;
  const check: Check<V[number]> = (value, path, issues) => {
    if (typeof value !== "string" || list.indexOf(value) < 0) issues.push({ path, message: why });
    return value as V[number];
  };
  return new Schema(fallback, check, () => ({ type: "string", enum: list.slice() }));
}

/** A list of items of one schema. */
function array<T>(item: Schema<T>, fallback: readonly T[]): Schema<T[]> {
  const check: Check<T[]> = (value, path, issues) => {
    const out: T[] = [];
    if (!Array.isArray(value)) {
      issues.push({ path, message: invalidType("array", value) });
      return out;
    }
    for (let i = 0; i < value.length; ++i) out.push(item.read(value[i], path.concat(i), issues));
    return out;
  };
  // The default is read through the item schema, so an item of it takes its own defaults as one parsed would.
  // What it refuses is a mistake in the definition, thrown here: a container check returns an empty one for a value it
  // refuses, which the constructor's re-check of the result would pass.
  const normalized: T[] = [];
  const refused: Issues = [];
  for (let i = 0; i < fallback.length; ++i) normalized.push(item.read(fallback[i], [i], refused));
  if (refused.length > 0) throw new Error(`a settings default must pass its own schema: ${refused[0].message}`);
  return new Schema(normalized, check, () => ({ type: "array", items: item.describe() }));
}

/** An opaque JSON value, already checked where it was made (a controller's parsed app config, say): any value is
 *  kept, and its JSON Schema says nothing but its default. Its type is the caller's to name: s.json<AppConfig>(...). */
function json<T = unknown>(fallback: NoInfer<T>): Schema<T> {
  return new Schema<T>(fallback, (value) => value as T, () => ({}));
}

/** The builders. Each takes its default; s.object's is its fields'. */
export const s = {
  object,
  enum: oneOf,
  int,
  bool: (fallback: boolean): Schema<boolean> => primitive("boolean", (v) => typeof v === "boolean", fallback),
  number: (fallback: number): Schema<number> => primitive("number", isNumber, fallback),
  string: (fallback: string): Schema<string> => primitive("string", (v) => typeof v === "string", fallback),
  array,
  json,
};
