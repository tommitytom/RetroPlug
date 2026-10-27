// rp.mgb.midi (src/script/nodes/mgbMidi.ts): every routed MIDI byte into the Game Boy's serial input. The mgb case of
// test/dsp/roles.test.ts, which drives the role through the kernel, at the node.
import { test, expect } from "../../../testing/harness";
import { MGB_MIDI_NODE_TYPE, mgbMidiNode } from "../../../src/script/nodes/mgbMidi";
import { NodeRig, bytesOn, on } from "./_rig";

test("its routed MIDI in, the serial input out, sized as the kernel's serial-in port", () => {
  const rig = new NodeRig(mgbMidiNode);
  expect(rig.ports()).toEqual(["in events midi", "out bytes serialIn"]);
  expect(mgbMidiNode.descriptor().id).toBe(MGB_MIDI_NODE_TYPE);
  expect(mgbMidiNode.layout({} as never).ports[1].capacity).toEqual({ messages: 4096, bytes: 73728 });
  expect(mgbMidiNode.reshapes).toBe(true);
});

test("mgb forwards each routed MIDI byte to its system's serial, at its message's frame", () => {
  const sent = new NodeRig(mgbMidiNode).run({ frames: 22050, inputs: { midi: [{ frame: 0, data: [0x90, 60, 100] }] } });
  expect(bytesOn(sent, "serialIn")).toEqual([[0, 0x90], [0, 60], [0, 100]]);
});

test("a message goes as one serial-in message, at its frame, and an empty one as none", () => {
  const sent = new NodeRig(mgbMidiNode).run({
    frames: 1024,
    inputs: { midi: [{ frame: 9, data: [0x90, 60, 100] }, { frame: 9, data: [0xb0, 1, 64] }, { frame: 10, data: [] }, { frame: 200, data: [0xf0, 1, 2, 3, 0xf7] }] },
  });
  expect(on(sent, "serialIn")).toEqual([[9, 0x90, 60, 100], [9, 0xb0, 1, 64], [200, 0xf0, 1, 2, 3, 0xf7]]);
  expect(sent.every((s) => s.flags === 0)).toBe(true);
});

test("nothing in, nothing out, whatever the transport", () => {
  const rig = new NodeRig(mgbMidiNode);
  expect(rig.run({ frames: 1024, playing: true })).toEqual([]);
  expect(rig.run({ frames: 1024, playing: false })).toEqual([]);
});
