// rp.n8.midi-to-fifo (src/script/nodes/n8MidiToFifo.ts): every routed MIDI message, whole, to a NES core's MIDI input
// and its N8 FIFO. The nes-n8-midi case of test/dsp/roles.test.ts, which drives the role through the kernel, at the
// node.
import { test, expect } from "../../../testing/harness";
import { N8_MIDI_TO_FIFO_NODE_TYPE, n8MidiToFifoNode } from "../../../src/script/nodes/n8MidiToFifo";
import { NodeRig, on } from "./_rig";

test("its routed MIDI in as midiIn, out as midi, the system node's input of that name, sized as the kernel's", () => {
  const rig = new NodeRig(n8MidiToFifoNode);
  expect(rig.ports()).toEqual(["in events midiIn", "out events midi"]);
  expect(n8MidiToFifoNode.descriptor().id).toBe(N8_MIDI_TO_FIFO_NODE_TYPE);
  expect(n8MidiToFifoNode.layout({} as never).ports[1].capacity).toEqual({ messages: 4096, bytes: 73728 });
  expect(n8MidiToFifoNode.reshapes).toBe(true);
});

test("nes-n8-midi forwards each routed MIDI message to its system's core, whole", () => {
  const sent = new NodeRig(n8MidiToFifoNode).run({ frames: 22050, inputs: { midiIn: [{ frame: 0, data: [0x90, 60, 100] }] } });
  // The whole message crosses as one (the NES core fans the bytes into the N8 FIFO), and nothing goes anywhere else.
  expect(sent).toEqual([{ port: "midi", frame: 0, queued: 0, data: [0x90, 60, 100], flags: 0 }]);
});

test("every message goes on as it came, in order, SysEx whole", () => {
  const sysex = [0xf0, 0x7d, ...Array.from({ length: 512 }, (_, i) => i & 0x7f), 0xf7];
  const sent = new NodeRig(n8MidiToFifoNode).run({
    frames: 1024,
    inputs: { midiIn: [{ frame: 3, data: [0x91, 62, 80] }, { frame: 3, data: sysex }, { frame: 1023, data: [0x81, 62, 0] }] },
  });
  expect(on(sent, "midi")).toEqual([[3, 0x91, 62, 80], [3, ...sysex], [1023, 0x81, 62, 0]]);
});
