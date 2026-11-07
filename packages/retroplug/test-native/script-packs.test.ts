// Script node packs through the real host (docs/modular-graph-phase-c1-design.md section 7): Backend.loadScriptPack
// over the RPC surface every host mounts, into the Engine this host runs. A pack loads and lists its types with their
// descriptors; its node, placed in a document beside the built-in pack's MIDI routing and the host MIDI nodes and
// driven with host MIDI in, sends the MIDI out its code makes; reloaded, that node runs the new code while the
// routing's own output is as it was; and each refusal comes back in the result's shape, changing nothing. The toy packs
// are source strings whose nodes are plain objects in the shape defineNode makes, appended to the global the script
// host reads (packages/core/ts/register.ts), as the engine-graph tests' toys are
// (packages/native/test/engine-graph/ScriptPacks.test.cpp, which holds packs to their isolation, jobs, memory and
// freeing). No fixtures, so CI runs every case. One Engine serves the file and a pack stays loaded (there is no
// unload), so each case loads ids of its own.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { LoadScriptPackRequest, LoadScriptPackResult } from "../src/backend";
import type { TypeDescriptor } from "@retroplug/core/descriptor";
import { toGraphJson } from "@retroplug/core/graphDocument";

const be = createRealBackend();
// No graph of its own: it renders whatever this file last published, which places the host's stereo pair.
const audio = createAudioDriver();

// What every toy pack's source starts with: `toy.define(id, ports, create)` appends a definition whose layout is
// `ports` whatever its settings, and `toy.events(id, dir)` is an events port.
const TOY = `
var toy = (function () {
  var nodes = (globalThis.__rp_scriptNodes = globalThis.__rp_scriptNodes || []);
  return {
    define: function (id, ports, create) {
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
    },
    events: function (id, dir) { return { id: id, dir: dir, kind: "events" }; },
  };
})();
`;

/** A node `id` that sends each message it gets on, its second byte moved by `by`. */
const transposer = (id: string, by: number): string => `
toy.define(${JSON.stringify(id)}, [toy.events("in", "in"), toy.events("out", "out")], function () {
  return { process: function (b) {
    var ms = b.messages("in");
    for (var i = 0; i < ms.length; i++) {
      var d = ms[i].data;
      b.send("out", ms[i].frame, [d[0], d[1] + (${by}), d[2]]);
    }
  } };
});
`;

/** `request` loaded, required to load (its error said when it does not). */
function load(request: LoadScriptPackRequest): LoadScriptPackResult {
  const r = be.loadScriptPack(request);
  expect(r.ok, `loading ${request.id}: ${r.error}`).toBe(true);
  expect(r.error).toBe("");
  expect(r.errors).toEqual([]);
  return r;
}

/** `request` refused before any replan, `why` in its error, with no planner errors and no types. */
function refused(request: LoadScriptPackRequest, why: string): void {
  const r = be.loadScriptPack(request);
  expect(r.ok, request.id).toBe(false);
  expect(r.error.includes(why), `${request.id}: ${JSON.stringify(why)} in ${JSON.stringify(r.error)}`).toBe(true);
  expect(r.errors, request.id).toEqual([]);
  expect(r.types, request.id).toEqual([]);
}

/** Host MIDI through the built-in routing by channel, to systems 1 and 2 (no system is placed: the routing names its
 *  outputs by id), channel 0 through the node `shift` of type `type` and channel 1 straight on (its channel rewritten
 *  to 0, the routing's own work), both to the host MIDI out under their systems, beside the host's stereo pair. */
function place(type: string): void {
  const document = toGraphJson({
    nodes: [
      { id: "midi-in", type: "rp.host.midi-in" },
      { id: "routing", type: "rp.midi.route", settings: { mode: "midiChannelToInstance", systems: [1, 2] } },
      { id: "shift", type },
      { id: "midi-out", type: "rp.host.midi-out", settings: { systems: [1, 2] } },
      { id: "out", type: "rp.host.audio-out", settings: { buses: [1, 1] } },
    ],
    wires: [
      { from: "midi-in.midi", to: "routing.midiIn" },
      { from: "routing.sys-1", to: "shift.in" },
      { from: "shift.out", to: "midi-out.in1" },
      { from: "routing.sys-2", to: "midi-out.in2" },
    ],
  });
  const r = be.setGraph({ document, slots: [] });
  expect(r.ok, `placing ${type}: ${JSON.stringify(r.errors)}`).toBe(true);
}

type Out = { system: number; frame: number; data: number[] };

/** One 256-frame block with `midi` as its host MIDI: the host MIDI out, by system and then frame. */
function drive(midi: { frame: number; bytes: number[] }[]): Out[] {
  const { midiOut } = audio.renderBlocks(2, [{ frames: 256, midi }]);
  return midiOut
    .map((m) => ({ system: m.system, frame: m.frame, data: Array.from(m.data) }))
    .sort((a, b) => a.system - b.system || a.frame - b.frame);
}

// A note on channel 0 (to the pack's node) and one on channel 1 (straight on).
const NOTES = [
  { frame: 3, bytes: [0x90, 60, 100] },
  { frame: 5, bytes: [0x91, 64, 90] },
];

test("a pack loads and lists its types, each with its descriptor", () => {
  const r = load({
    id: "test.list",
    source: TOY + transposer("test.list.up", 12) + transposer("test.list.down", -12),
    memoryLimitBytes: 32 * 1024 * 1024,
  });
  // In the order its source registered them, each descriptor the canonical JSON the registry holds (the name, never
  // given, is the id), so whatever builds documents for the pack knows its ports.
  expect(r.types.map((t) => t.id)).toEqual(["test.list.up", "test.list.down"]);
  for (const t of r.types) {
    const d = JSON.parse(t.descriptor) as TypeDescriptor;
    expect(d.id).toBe(t.id);
    expect(d.name).toBe(t.id);
    expect(d.ports.map((p) => `${p.dir} ${p.kind} ${p.id}`), t.id).toEqual(["in events in", "out events out"]);
  }
});

test("a pack's node runs beside the built-in routing, and the host MIDI out is what its code makes", () => {
  load({ id: "test.shift", source: TOY + transposer("test.shift.up", 12) });
  place("test.shift.up");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  expect(drive([])).toEqual([]);
  expect(drive([{ frame: 0, bytes: [0x80, 61, 0] }])).toEqual([{ system: 1, frame: 0, data: [0x80, 73, 0] }]);
});

test("a reload rebuilds the placed node on the new code, and the routing's output is as it was", () => {
  load({ id: "test.reload", source: TOY + transposer("test.reload.shift", 12) });
  place("test.reload.shift");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  // The same id loaded again is its reload: the engine replans the document it runs, with no setGraph from here, and
  // the new generation may add a type.
  const r = load({
    id: "test.reload",
    source: TOY + transposer("test.reload.shift", 24) + transposer("test.reload.more", 1),
  });
  expect(r.types.map((t) => t.id)).toEqual(["test.reload.shift", "test.reload.more"]);
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 84, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
  // And the type it added places.
  place("test.reload.more");
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 61, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
});

test("a refusal before any replan says why, with no planner errors, and changes nothing", () => {
  // A compile error, at its line.
  refused(
    { id: "test.syntax", source: "var a = 1;\nvar b = 2;\nvar c = ;\n" },
    "the source does not compile: line 3: SyntaxError: ",
  );
  // A type id outside the pack's namespace.
  refused(
    { id: "test.ns", source: TOY + transposer("test.ns.fine", 1) + transposer("test.other.stray", 2) },
    '(test.other.stray) is outside pack "test.ns": its type ids are "test.ns.<name>"',
  );
  // The built-in pack, and another id that is the app's to give.
  refused(
    { id: "rp.script", source: TOY + transposer("rp.script.up", 1) },
    `pack id "rp.script" is the built-in pack, which is not reloadable`,
  );
  refused({ id: "rp.mine", source: TOY + transposer("rp.mine.up", 1) }, `pack id "rp.mine" is the app's to give`);
  // A memory limit that is no whole number of bytes, refused before the id is looked at: a fraction, a negative, one
  // past 2^53 - 1 (past it a double no longer holds every whole number) and one that is not finite.
  const notWhole = "memoryLimitBytes must be a whole number of bytes";
  for (const memoryLimitBytes of [1.5, -1, 2 ** 53, Infinity, NaN]) {
    refused({ id: "test.limit", source: TOY, memoryLimitBytes }, notWhole);
  }
  // One too small for the load: the limit holds each of the pack's runtimes whole, its intrinsics included.
  refused(
    { id: "test.limit", source: TOY + transposer("test.limit.up", 1), memoryLimitBytes: 64 * 1024 },
    "(the load reached the pack's 65536-byte memory limit)",
  );
  // A top level that throws.
  refused({ id: "test.throws", source: TOY + "throw new Error('not today');" }, "chunk 0: Error: not today");

  // None of them loaded anything: a document naming a type one would have defined is refused as naming no type, and
  // each id loads cleanly once its source is good.
  const named = toGraphJson({ nodes: [{ id: "n", type: "test.ns.fine" }], wires: [] });
  const r = be.setGraph({ document: named, slots: [] });
  expect(r.ok).toBe(false);
  expect(r.errors.map((e) => `${e.code} at ${e.nodeId}`)).toEqual(["unknown-type at n"]);
  load({ id: "test.syntax", source: TOY + transposer("test.syntax.up", 1) });
  load({ id: "test.ns", source: TOY + transposer("test.ns.fine", 1) });
  load({ id: "test.limit", source: TOY, memoryLimitBytes: 0 });
  // 2^53 - 1 is the largest limit taken.
  load({ id: "test.limit.largest", source: TOY, memoryLimitBytes: 2 ** 53 - 1 });
});

/** A reload of `id` from `source` refused by its replan of the running document, which places it as node "shift":
 *  the planner's one error, `code` at that node, with `message` in it, summed up in `error`, and no types. */
function replanRefused(id: string, source: string, code: string, message: string): void {
  const r = be.loadScriptPack({ id, source });
  expect(r.ok, id).toBe(false);
  expect(r.types).toEqual([]);
  expect(r.errors.map((e) => `${e.code} at ${e.nodeId}`)).toEqual([`${code} at shift`]);
  expect(r.errors[0].message.includes(message), r.errors[0].message).toBe(true);
  const summary = `the running document does not plan with the reloaded pack: ${code} at shift: `;
  expect(r.error.startsWith(summary), r.error).toBe(true);
}

test("a reload whose replan is refused returns the planner's errors, and the old code runs on", () => {
  load({ id: "test.keep", source: TOY + transposer("test.keep.shift", 12) });
  place("test.keep.shift");
  const before = [
    { system: 1, frame: 3, data: [0x90, 72, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ];
  expect(drive(NOTES)).toEqual(before);

  // A new generation without the type the document places.
  replanRefused("test.keep", TOY + transposer("test.keep.other", 1), "unknown-type", "test.keep.shift");
  expect(drive(NOTES)).toEqual(before);

  // One whose parseSettings refuses the settings the document gives the node.
  const lenient = "parseSettings: function (raw) { return raw === undefined ? {} : raw; }";
  expect(TOY.includes(lenient)).toBe(true);
  const strict = TOY.replace(lenient, 'parseSettings: function () { throw new Error("no settings will do"); }');
  replanRefused("test.keep", strict + transposer("test.keep.shift", 24), "settings-invalid", "no settings will do");
  expect(drive(NOTES)).toEqual(before);

  // The pack still reloads once its code plans.
  load({ id: "test.keep", source: TOY + transposer("test.keep.shift", -12) });
  expect(drive(NOTES)).toEqual([
    { system: 1, frame: 3, data: [0x90, 48, 100] },
    { system: 2, frame: 5, data: [0x90, 64, 90] },
  ]);
});
