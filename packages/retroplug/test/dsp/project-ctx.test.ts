// The project-scope context the kernel hands a project behaviour.
//
// Before M5 a project stage got `(block, inboxes, config)` and nothing else - no state across blocks and
// nowhere to send bytes - which made a project role that OWNS something (a device session) impossible.
// These cover the added seams directly, with a throwaway behaviour rather than through the launchpad role,
// so a failure points at the kernel rather than at the app.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { z } from "../../src/configSchema";
import { DspKernel, CollectingSink, type BlockInput, type ProjectCtx, type ProjectBehavior } from "../../src/dspKernel";

const baseDyn = (): BlockInput => ({
  frames: 512, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: true,
  midiIn: [], buttons: [], keys: [], serialOut: [],
});

/** A kernel whose single project role runs `dsp`. */
function kernel(dsp: ProjectBehavior, systems: number[] = [1]): DspKernel {
  const reg = new RoleRegistry();
  reg.registerRole({ kind: "probe", category: "feature", scope: "project", schema: z.object({}), dsp });
  const k = new DspKernel(reg);
  k.setSystems({
    project: [{ kind: "probe", config: {} }],
    systems: systems.map((id) => ({ id, pipeline: [] })),
  });
  return k;
}

test("state persists across blocks, like a system stage's", () => {
  let last = 0;
  const k = kernel((c) => { c.state.n = ((c.state.n as number) ?? 0) + 1; last = c.state.n as number; });
  k.processBlock(baseDyn());
  k.processBlock(baseDyn());
  k.processBlock(baseDyn());
  expect(last).toBe(3);
});

test("state survives a structure push, so a session is not torn down when a system is added", () => {
  // setSystems fires on every structural edit - adding a system, changing a knob. A controller session
  // rebuilt each time would lose its shadow buffer and its pending cue, and would repaint the whole
  // device for no reason.
  let seen = 0;
  const dsp: ProjectBehavior = (c) => { c.state.n = ((c.state.n as number) ?? 0) + 1; seen = c.state.n as number; };
  const reg = new RoleRegistry();
  reg.registerRole({ kind: "probe", category: "feature", scope: "project", schema: z.object({}), dsp });
  const k = new DspKernel(reg);

  k.setSystems({ project: [{ kind: "probe", config: {} }], systems: [{ id: 1, pipeline: [] }] });
  k.processBlock(baseDyn());
  k.setSystems({ project: [{ kind: "probe", config: {} }], systems: [{ id: 1, pipeline: [] }, { id: 2, pipeline: [] }] });
  k.processBlock(baseDyn());
  expect(seen).toBe(2);
});

test("dropping the role drops its state, so re-adding it starts fresh", () => {
  let seen = 0;
  const dsp: ProjectBehavior = (c) => { c.state.n = ((c.state.n as number) ?? 0) + 1; seen = c.state.n as number; };
  const reg = new RoleRegistry();
  reg.registerRole({ kind: "probe", category: "feature", scope: "project", schema: z.object({}), dsp });
  const k = new DspKernel(reg);

  k.setSystems({ project: [{ kind: "probe", config: {} }], systems: [] });
  k.processBlock(baseDyn());
  k.setSystems({ project: [], systems: [] }); // role removed
  k.setSystems({ project: [{ kind: "probe", config: {} }], systems: [] }); // and back
  k.processBlock(baseDyn());
  expect(seen).toBe(1);
});

test("controllerIn is an empty array when the host does not supply one", () => {
  // Native builds the block-input object field by field and does not set this until a device link exists,
  // so the absent case is the NORMAL one and must not be undefined.
  let len = -1;
  const k = kernel((c) => { len = c.controllerIn.length; });
  k.processBlock(baseDyn());
  expect(len).toBe(0);

  k.processBlock({ ...baseDyn(), controllerIn: [{ frame: 0, data: [0x90, 11, 100] }] });
  expect(len).toBe(1);
});

test("controllerIn does not leak into the next block", () => {
  const seen: number[] = [];
  const k = kernel((c) => seen.push(c.controllerIn.length));
  k.processBlock({ ...baseDyn(), controllerIn: [{ frame: 0, data: [0x90, 11, 100] }] });
  k.processBlock(baseDyn());
  expect(seen).toEqual([1, 0]);
});

test("toSystem delivers into the right system's inbox, and ignores an unknown id", () => {
  const inboxSizes: number[] = [];
  const k = kernel((c) => {
    c.toSystem(20, { frame: 0, data: [0x90, 60, 100] });
    c.toSystem(999, { frame: 0, data: [0x90, 62, 100] }); // no such system
    for (const box of c.inboxes) inboxSizes.push(box.length);
  }, [10, 20]);
  k.processBlock(baseDyn());
  expect(inboxSizes).toEqual([0, 1]); // only system 20 got one, and the stray id landed nowhere
});

test("emitControllerOut collects into Sinks, and is safe when the host binds no sink", () => {
  const k = kernel((c) => c.emitControllerOut([0xf0, 0x01, 0xf7]));
  expect(k.processBlock(baseDyn()).controllerOut).toEqual([[0xf0, 0x01, 0xf7]]);
  expect(k.processBlock(baseDyn()).controllerOut.length).toBe(1); // cleared each block, not accumulated

  // A forwarding target with no emitControllerOut (every host before M4) must not throw.
  const reg = new RoleRegistry();
  reg.registerRole({
    kind: "probe", category: "feature", scope: "project", schema: z.object({}),
    dsp: (c: ProjectCtx) => c.emitControllerOut([1, 2, 3]),
  });
  const bare = new DspKernel(reg, {
    pushSerialIn: () => {}, emitMidiOut: () => {}, emitCoreMidi: () => {},
    pushCoreBytes: () => {}, pressButton: () => {},
  });
  bare.setSystems({ project: [{ kind: "probe", config: {} }], systems: [] });
  bare.processBlock(baseDyn()); // no throw is the assertion
  expect(true).toBe(true);
});

test("emitMidiOut from project scope reaches the sink", () => {
  const sink = new CollectingSink();
  const reg = new RoleRegistry();
  reg.registerRole({
    kind: "probe", category: "feature", scope: "project", schema: z.object({}),
    dsp: (c: ProjectCtx) => c.emitMidiOut(5, 0, [0x90, 40, 100]),
  });
  const k = new DspKernel(reg, sink);
  k.setSystems({ project: [{ kind: "probe", config: {} }], systems: [] });
  k.processBlock(baseDyn());
  expect(sink.sinks.midiOut).toEqual([{ system: 5, frame: 0, data: [0x90, 40, 100] }]);
});

test("a project role runs with NO systems at all", () => {
  // The real-Game-Boy case: there is no emulated cart to attach to, which is the whole reason the
  // controller runs at project scope.
  let ran = 0;
  const k = kernel(() => { ran++; }, []);
  k.processBlock(baseDyn());
  expect(ran).toBe(1);
});
