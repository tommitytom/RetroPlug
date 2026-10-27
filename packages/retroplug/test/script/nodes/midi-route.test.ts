// rp.midi.route (src/script/nodes/midiRoute.ts): the host's MIDI fanned to one output a system, by the routing mode.
// The expectations of test/dsp/routing.test.ts, which drives the midi-routing role through the kernel, at the node,
// and each mode's decision held to routeBlock, the pure oracle.
import { test, expect } from "../../../testing/harness";
import { MIDI_ROUTE_NODE_TYPE, midiRouteNode } from "../../../src/script/nodes/midiRoute";
import { routeBlock, MIDI_ROUTING_VALUES, type MidiEvent } from "../../../src/midiRouting";
import { NodeRig, on, type In } from "./_rig";

const route = (mode: string, systems: number[]) => new NodeRig(midiRouteNode, { mode, systems });

test("one output a system, named by its id, in slot order, after the one input", () => {
  expect(new NodeRig(midiRouteNode).ports()).toEqual(["in events midiIn"]);
  expect(route("sendToAll", [7, 3, 0xffffffff]).ports()).toEqual([
    "in events midiIn",
    "out events sys-7",
    "out events sys-3",
    "out events sys-4294967295",
  ]);
  const d = midiRouteNode.descriptor();
  expect(d.id).toBe(MIDI_ROUTE_NODE_TYPE);
  // Each output holds what rp.host.midi-in's does, since under send-to-all it carries all of it.
  expect(midiRouteNode.layout(midiRouteNode.parseSettings({ systems: [1] })).ports[1].capacity).toEqual({ messages: 2048, bytes: 65536 });
  expect(midiRouteNode.reshapes).toBe(true);
});

test("settings default to send-to-all and no systems, and refuse a bad mode, a bad id or one id twice", () => {
  expect(midiRouteNode.parseSettings({})).toEqual({ mode: "sendToAll", systems: [] });
  expect(() => midiRouteNode.parseSettings({ mode: "everywhere" })).toThrow("mode: Invalid option");
  for (const id of [0, 1.5, -1, 2 ** 32]) expect(() => midiRouteNode.parseSettings({ systems: [id] }), `id ${id}`).toThrow("systems.0");
  expect(() => midiRouteNode.layout(midiRouteNode.parseSettings({ systems: [4, 4] }))).toThrow("rp.midi.route: two systems have the id 4");
});

test("OneChannelPerInstance sends each channel to its system, at its frame", () => {
  const sent = route("oneChannelPerInstance", [10, 20]).run({
    frames: 1024,
    inputs: {
      midiIn: [
        { frame: 0, data: [0x90, 60, 100] }, // channel 0 → system index 0 (id 10)
        { frame: 3, data: [0x91, 62, 80] }, //  channel 1 → system index 1 (id 20)
      ],
    },
  });
  expect(on(sent, "sys-10")).toEqual([[0, 0x90, 60, 100]]);
  expect(on(sent, "sys-20")).toEqual([[3, 0x91, 62, 80]]);
});

test("with no systems, nothing is sent", () => {
  expect(route("sendToAll", []).run({ frames: 1024, inputs: { midiIn: [{ frame: 0, data: [0x90, 60, 100] }] } })).toEqual([]);
});

test("SysEx and system messages reach every system unchanged, in every mode; an empty message reaches none", () => {
  const sysex = [0xf0, 0x7d, 1, 2, 3, 0xf7];
  for (const mode of MIDI_ROUTING_VALUES) {
    const sent = route(mode, [1, 2]).run({
      frames: 64,
      inputs: { midiIn: [{ frame: 1, data: sysex }, { frame: 2, data: [0xf8] }, { frame: 3, data: [] }, { frame: 4, data: [0x93, 1, 2, 3, 4] }] },
    });
    // Five bytes is SysEx's length, whatever its status says (native rides it on dataExt), so it is broadcast too.
    for (const port of ["sys-1", "sys-2"]) expect(on(sent, port), `${mode} ${port}`).toEqual([[1, ...sysex], [2, 0xf8], [4, 0x93, 1, 2, 3, 4]]);
  }
});

test("MidiChannelToInstance's target takes the message with its channel rewritten to 0, and no other system does", () => {
  const sent = route("midiChannelToInstance", [1, 2, 3]).run({
    frames: 64,
    inputs: { midiIn: [{ frame: 0, data: [0x94, 60, 100] }, { frame: 1, data: [0xc2, 7] }, { frame: 2, data: [0xb5, 1] }] },
  });
  expect(on(sent, "sys-1")).toEqual([]);
  expect(on(sent, "sys-2")).toEqual([[0, 0x90, 60, 100]]); // channel 4 % 3 = 1
  expect(on(sent, "sys-3")).toEqual([[1, 0xc0, 7], [2, 0xb0, 1]]); // channels 2 and 5
});

test("every mode routes as routeBlock does, over 1 to 3 systems and every channel", () => {
  const events: In[] = [];
  for (let ch = 0; ch < 16; ch++) events.push({ frame: ch * 3, data: [0x90 | ch, 40 + ch, 100] }, { frame: ch * 3 + 1, data: [0xe0 | ch, 0, 64] });
  events.push({ frame: 60, data: [0xfa] }, { frame: 61, data: [0xf0, 1, 2, 3, 0xf7] });
  for (const mode of MIDI_ROUTING_VALUES) {
    for (let n = 1; n <= 3; n++) {
      const ids = [5, 9, 2].slice(0, n);
      const sent = route(mode, ids).run({ frames: 128, inputs: { midiIn: events } });
      const expected = routeBlock(events as MidiEvent[], mode, n);
      ids.forEach((id, i) => expect(on(sent, `sys-${id}`), `${mode} ${n} systems, #${i}`).toEqual(expected[i].map((e) => [e.frame, ...e.data])));
    }
  }
});

test("a settings change moves the routing on the next block: a new mode, and the outputs reordered", () => {
  const rig = route("oneChannelPerInstance", [1, 2]);
  const block = { frames: 64, inputs: { midiIn: [{ frame: 0, data: [0x91, 60, 100] }] } };
  expect(on(rig.run(block), "sys-2")).toEqual([[0, 0x91, 60, 100]]);
  expect(rig.set({ mode: "oneChannelPerInstance", systems: [2, 1] })).toBe(true); // channel 1 is now system 1's
  expect(on(rig.run(block), "sys-1")).toEqual([[0, 0x91, 60, 100]]);
  rig.set({ mode: "midiChannelToInstance", systems: [2, 1] });
  expect(on(rig.run(block), "sys-1")).toEqual([[0, 0x90, 60, 100]]);
  expect(rig.creates).toBe(1); // reshaped, never made again
});
