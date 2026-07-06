// The two roles we're migrating off C++, driven through the DSP kernel end-to-end (pure TS, no
// backend). mGB forwards routed MIDI bytes to serial; lsdj-sync's MidiSync mode emits a 24-PPQN
// 0xF8 clock and Off emits nothing — the doc-06 "translators are scripts" shape as plain TS.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { registerDspRoles } from "../../src/dspRoles";
import { DspKernel, type Block } from "../../src/dspKernel";

function kernel(): DspKernel {
  const reg = new RoleRegistry();
  registerDspRoles(reg);
  return new DspKernel(reg);
}

// 22050 frames @ 44100/120 = exactly 1 beat (24 ticks at 24 PPQN).
const baseBlock = (): Block => ({
  frames: 22050,
  sampleRate: 44100,
  tempo: 120,
  ppqStart: 0,
  transport: false,
  midiIn: [],
  buttons: [],
  keys: [],
  systems: [],
  project: [],
});

test("mgb forwards each routed MIDI byte to its system's serial", () => {
  const b: Block = {
    ...baseBlock(),
    midiIn: [{ frame: 0, data: [0x90, 60, 100] }],
    project: [{ kind: "midi-routing", config: { mode: 0 } }], // SendToAll
    systems: [{ id: 1, pipeline: [{ kind: "mgb", config: {} }] }],
  };
  expect(kernel().processBlock(b).serialIn).toEqual([
    { system: 1, frame: 0, byte: 0x90 },
    { system: 1, frame: 0, byte: 60 },
    { system: 1, frame: 0, byte: 100 },
  ]);
});

test("lsdj-sync MidiSync emits a 24-PPQN 0xF8 clock; Off emits nothing", () => {
  const withMode = (mode: number): Block => ({
    ...baseBlock(),
    transport: true,
    systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode } }] }],
  });

  const on = kernel().processBlock(withMode(1)); // MidiSync
  expect(on.serialIn.length).toBe(24);
  expect(on.serialIn.every((s) => s.system === 1 && s.byte === 0xf8)).toBeTruthy();

  expect(kernel().processBlock(withMode(0)).serialIn.length).toBe(0); // Off
});
