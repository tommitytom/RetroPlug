// Project-scope routing → per-system pipeline, end-to-end. The global midiIn is fanned to the
// right system by the midi-routing behavior, and that system's mgb stage turns its routed events
// into serial. Proves the two scopes compose (project runs first, then per-system).
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type Block } from "../../src/dspKernel";

function kernel(): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  return new DspKernel(reg);
}

test("OneChannelPerInstance routing sends each channel to its system, which mgb then serializes", () => {
  const b: Block = {
    frames: 1024,
    sampleRate: 44100,
    tempo: 120,
    ppqStart: 0,
    transport: false,
    buttons: [],
    keys: [],
    midiIn: [
      { frame: 0, data: [0x90, 60, 100] }, // channel 0 → system index 0 (id 10)
      { frame: 3, data: [0x91, 62, 80] }, //  channel 1 → system index 1 (id 20)
    ],
    project: [{ kind: "midi-routing", config: { mode: 2 } }], // OneChannelPerInstance
    systems: [
      { id: 10, pipeline: [{ kind: "mgb", config: {} }] },
      { id: 20, pipeline: [{ kind: "mgb", config: {} }] },
    ],
  };

  expect(kernel().processBlock(b).serialIn).toEqual([
    { system: 10, frame: 0, byte: 0x90 },
    { system: 10, frame: 0, byte: 60 },
    { system: 10, frame: 0, byte: 100 },
    { system: 20, frame: 3, byte: 0x91 },
    { system: 20, frame: 3, byte: 62 },
    { system: 20, frame: 3, byte: 80 },
  ]);
});

test("with no routing behavior, systems receive no MIDI (routing is a behavior, not implicit)", () => {
  const b: Block = {
    frames: 1024, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false,
    buttons: [], keys: [], project: [],
    midiIn: [{ frame: 0, data: [0x90, 60, 100] }],
    systems: [{ id: 1, pipeline: [{ kind: "mgb", config: {} }] }],
  };
  expect(kernel().processBlock(b).serialIn.length).toBe(0);
});
