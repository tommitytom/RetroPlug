// The DSP kernel as a script node (src/script/kernelNode.ts): its settings, the ports they give, and where each of the
// kernel's sink calls goes, over a stand-in for the script host's Block. What it sends, block for block, is held
// natively, to what it sent while the kernel stage it replaced ran beside it (packages/native/test/engine-graph/
// KernelNode.test.cpp).
import { test, expect } from "../../testing/harness";
import type { Block, ControlStream, Message } from "../../src/graph/defineNode";
import { DspKernel, type BlockInput } from "../../src/dspKernel";
import { KERNEL_NODE_TYPE, kernelNode, type KernelSettings } from "../../src/script/kernelNode";

const portsOf = (settings: unknown) => kernelNode.layout(kernelNode.parseSettings(settings)).ports.map((p) => `${p.dir} ${p.kind} ${p.id}`);

test("{} is an empty kernel: the host's ports and no system's", () => {
  expect(portsOf({})).toEqual([
    "in events midiIn",
    "in events controllerIn",
    "in control controllerConnected",
    "out events midiOut",
    "out events controllerOut",
  ]);
  expect(kernelNode.descriptor().id).toBe(KERNEL_NODE_TYPE);
  // A structure change is a reshape, and a throw is counted rather than latched (design section 4.5).
  expect(kernelNode.reshapes).toBe(true);
  expect(kernelNode.faultOnThrow).toBe(undefined);
});

test("each system has its four outputs, and one with a serialOut its MI.OUT input, named by its id", () => {
  expect(portsOf({ systems: [{ id: 3, pipeline: [], serialOut: true }, { id: 9 }] })).toEqual([
    "in events midiIn",
    "in events controllerIn",
    "in control controllerConnected",
    "in bytes sys-3.serialOut",
    "out bytes sys-3.serialIn",
    "out events sys-3.midi",
    "out bytes sys-3.bytes",
    "out events sys-3.buttons",
    "out bytes sys-9.serialIn",
    "out events sys-9.midi",
    "out bytes sys-9.bytes",
    "out events sys-9.buttons",
    "out events midiOut",
    "out events controllerOut",
  ]);
});

test("settings fill their defaults, keep each role's config as given, and refuse a bad or repeated id", () => {
  const config = { app: "lsdj-midimap", songRowTicks: [[96, null], [96]], appConfig: { quantise: "bar" } };
  expect(kernelNode.parseSettings({ project: [{ kind: "launchpad", config }], systems: [{ id: 2, pipeline: [{ kind: "mgb" }] }] })).toEqual({
    project: [{ kind: "launchpad", config }],
    systems: [{ id: 2, pipeline: [{ kind: "mgb", config: {} }], serialOut: false }],
  });
  for (const id of [0, 1.5, -1, 2 ** 32]) expect(() => kernelNode.parseSettings({ systems: [{ id }] }), `id ${id}`).toThrow("systems.0.id");
  expect(() => kernelNode.parseSettings({ systems: [{ id: 4 }, { id: 4 }] })).toThrow("systems: two systems have the same id");
});

// A Block as the script host hands one over, recording every send.
interface Sent {
  port: string;
  frame: number;
  data: number[];
  flags: number;
}
function blockOf(
  frames: number,
  playing: boolean,
  messages: Record<string, { frame: number; data: number[] }[]>,
  controls: Record<string, ControlStream> = {},
): { block: Block; sent: Sent[] } {
  const sent: Sent[] = [];
  const block: Block = {
    frames,
    transport: { sampleRate: 44100, tempo: 120, ppq: 0, playing },
    audio: () => [],
    messages: (port) => (messages[port] ?? []).map((m): Message => ({ frame: m.frame, data: Uint8Array.from(m.data), flags: 0 })),
    send: (port, frame, data, flags = 0) => (sent.push({ port, frame, data: Array.from(data), flags }), true),
    control: (id) => controls[id] ?? { start: 0, changes: [] },
    emit: () => true,
  };
  return { block, sent };
}

const settings = (s: unknown): KernelSettings => kernelNode.parseSettings(s);
// The largest block the file drives: a quarter at 120 bpm.
const MAX_FRAMES = 22050;
const create = (s: KernelSettings) => kernelNode.create({ settings: s, sampleRate: 44100, maxFrames: MAX_FRAMES });

test("each sink call goes onto its system's port in emission order, host MIDI out behind its system's id", () => {
  const node = create(
    settings({
      project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
      systems: [
        { id: 1, pipeline: [{ kind: "mgb", config: {} }], serialOut: true },
        { id: 2, pipeline: [{ kind: "nes-n8-midi", config: {} }] },
        { id: 0x01020304, pipeline: [{ kind: "lsdj-sync", config: { mode: "masterSync" } }], serialOut: true },
        { id: 4, pipeline: [{ kind: "risa-sync", config: {} }] },
      ],
    }),
  );
  const { block, sent } = blockOf(1024, true, {
    midiIn: [{ frame: 4, data: [0x90, 60, 100] }],
    [`sys-${0x01020304}.serialOut`]: [{ frame: 0, data: [5] }, { frame: 0, data: [5] }],
  });
  node.process(block);
  const at = (port: string) => sent.filter((s) => s.port === port).map((s) => [s.frame, ...s.data, s.flags]);
  expect(at("sys-1.serialIn")).toEqual([[4, 0x90, 60, 100, 0]]); // a run of same-frame bytes as one message
  expect(at("sys-2.midi")).toEqual([[4, 0x90, 60, 100, 0]]);
  const id = [4, 3, 2, 1]; // 0x01020304, little-endian
  expect(at("midiOut")).toEqual([[0, ...id, 0x90, 5, 0x7f, 0], [0, ...id, 0xfa, 0], [0, ...id, 0xf8, 0], [0, ...id, 0xf8, 0]]);
  // risa's arm flushes the FIFO; START and the first clock follow it.
  expect(at("sys-4.bytes").slice(0, 2)).toEqual([[0, 0xf9, 0x52, 0, 0, 0, 1], [0, 0xfa, 0]]);
  // Systems run in slot order, so their ports fill in that order, but for a serial-in run, held until a byte at
  // another frame or the block's end (each port is its own queue, so no reader sees the order across them).
  const order = sent.map((s) => s.port).filter((p, i, all) => all.indexOf(p) === i);
  expect(order).toEqual(["sys-2.midi", "midiOut", "sys-4.bytes", "sys-1.serialIn"]);
});

test("a system's serial-in bytes go a run of same-frame bytes to a message, each run sent before the next frame's", () => {
  const node = create(
    settings({
      project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
      systems: [
        { id: 1, pipeline: [{ kind: "mgb", config: {} }], serialOut: true },
        { id: 2, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiSyncArduinoboy" } }], serialOut: true },
      ],
    }),
  );
  // mGB: two messages at one frame are one run, one at the next frame another. Arduinoboy, on the transport's rise:
  // each note's row byte at its frame, then START at frame 0, a run of its own, which the queue raises to the last
  // row's frame as it raised the byte alone.
  const { block, sent } = blockOf(1024, true, {
    midiIn: [{ frame: 9, data: [0x90, 60, 100] }, { frame: 9, data: [0xb0, 1, 64] }, { frame: 10, data: [0x80, 60, 0] }, { frame: 200, data: [0x90, 31, 100] }],
  });
  node.process(block);
  const at = (port: string) => sent.filter((s) => s.port === port).map((s) => [s.frame, ...s.data]);
  expect(at("sys-1.serialIn")).toEqual([[9, 0x90, 60, 100, 0xb0, 1, 64], [10, 0x80, 60, 0], [200, 0x90, 31, 100]]);
  expect(at("sys-2.serialIn")).toEqual([[9, 30], [200, 1], [0, 0xfa]]);
});

test("the runs a throw leaves are sent, as the kernel stage kept what the kernel pushed before one", () => {
  // No shipped role throws on what the kernel accepts, so Math.ceil throws for the one value the second system's
  // clock reaches when it first syncs, at ppq 10.5 (tick 252), after the first has forwarded its MIDI.
  const node = create(
    settings({
      project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
      systems: [
        { id: 1, pipeline: [{ kind: "mgb", config: {} }] },
        { id: 2, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiSync" } }] },
      ],
    }),
  );
  const { block, sent } = blockOf(1024, true, { midiIn: [{ frame: 4, data: [0x90, 60, 100] }] });
  block.transport.ppq = 10.5;
  const ceil = Math.ceil;
  Math.ceil = (x: number) => {
    if (x === 252) throw new Error("injected");
    return ceil(x);
  };
  try {
    expect(() => node.process(block)).toThrow("injected");
  } finally {
    Math.ceil = ceil;
  }
  expect(sent).toEqual([{ port: "sys-1.serialIn", frame: 4, data: [0x90, 60, 100], flags: 0 }]);
});

test("a run a stopped call left is never sent in a later block", () => {
  // The deadline's stop skips the finally that sends the runs. A send that throws stands in for it here: the run is
  // still held when the call ends.
  const node = create(
    settings({ project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }], systems: [{ id: 1, pipeline: [{ kind: "mgb", config: {} }] }] }),
  );
  const stopped = blockOf(1024, false, { midiIn: [{ frame: 4, data: [0x90, 60, 100] }] });
  stopped.block.send = () => {
    throw new Error("stopped");
  };
  expect(() => node.process(stopped.block)).toThrow("stopped");
  const next = blockOf(1024, false, {});
  node.process(next.block);
  expect(next.sent).toEqual([]);
});

test("a stopped call's runs are cleared before the next block starts its own, however many it sent first", () => {
  // The stand-in stop throws on the second system's serial in alone, so the call ends with the first's run sent and
  // the second's held. The next block pushes on both at that frame, where a held byte left uncleared would go out
  // ahead of its own; its runs at the first frame go out at the frame change, the rest at its end, and the blocks
  // after it start from none held.
  const mgb = { pipeline: [{ kind: "mgb", config: {} }] };
  const node = create(settings({ project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }], systems: [{ id: 1, ...mgb }, { id: 2, ...mgb }] }));
  const stopped = blockOf(1024, false, { midiIn: [{ frame: 4, data: [0x90, 60, 100] }] });
  const send = stopped.block.send;
  stopped.block.send = (port, frame, data, flags) => {
    if (port === "sys-2.serialIn") throw new Error("stopped");
    return send(port, frame, data, flags);
  };
  expect(() => node.process(stopped.block)).toThrow("stopped");
  expect(stopped.sent).toEqual([{ port: "sys-1.serialIn", frame: 4, data: [0x90, 60, 100], flags: 0 }]);
  const runs = (midiIn: { frame: number; data: number[] }[]) => {
    const b = blockOf(1024, false, { midiIn });
    node.process(b.block);
    return b.sent.map((s) => [s.port, s.frame, ...s.data]);
  };
  expect(runs([{ frame: 4, data: [0x80, 60, 0] }, { frame: 7, data: [0x90, 62, 100] }])).toEqual([
    ["sys-1.serialIn", 4, 0x80, 60, 0],
    ["sys-2.serialIn", 4, 0x80, 60, 0],
    ["sys-1.serialIn", 7, 0x90, 62, 100],
    ["sys-2.serialIn", 7, 0x90, 62, 100],
  ]);
  expect(runs([])).toEqual([]);
  expect(runs([{ frame: 2, data: [0x80, 62, 0] }])).toEqual([
    ["sys-1.serialIn", 2, 0x80, 62, 0],
    ["sys-2.serialIn", 2, 0x80, 62, 0],
  ]);
});

test("controllerIn is in the block's input only when the surface sent something, as the kernel stage's marshalling had it", () => {
  const inputs: BlockInput[] = [];
  const processBlock = DspKernel.prototype.processBlock;
  DspKernel.prototype.processBlock = function (this: DspKernel, input: BlockInput) {
    inputs.push(input);
    return processBlock.call(this, input);
  };
  try {
    const node = create(settings({}));
    node.process(blockOf(1024, false, {}).block);
    node.process(blockOf(1024, false, { controllerIn: [{ frame: 0, data: [0x90, 81, 127] }, { frame: 0, data: [0x80, 81, 0] }] }).block);
  } finally {
    DspKernel.prototype.processBlock = processBlock;
  }
  expect("controllerIn" in inputs[0]).toBe(false);
  expect(inputs[1].controllerIn).toEqual([{ frame: 0, data: [0x90, 81, 127] }, { frame: 0, data: [0x80, 81, 0] }]);
});

test("a role's press goes out at frame 0, and a surface counts as attached when it is from frame 0", () => {
  // autoStart taps START on the transport's rise.
  const press = create(settings({ systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiSync", autoStart: true } }] }] }));
  const started = blockOf(1024, true, {});
  press.process(started.block);
  expect(started.sent.filter((s) => s.port === "sys-1.buttons")).toEqual([{ port: "sys-1.buttons", frame: 0, data: [7, 1], flags: 0 }]);
  // A controller session paints its surface on its first block either way, and takes the surface (its hello) on the
  // block it is attached; one attached later in the block is attached from the next.
  const rows = [Array.from({ length: 16 }, () => 96)];
  const sentToSurface = (connected: ControlStream): number => {
    const node = create(settings({ project: [{ kind: "launchpad", config: { app: "lsdj-midimap", songRowTicks: rows } }] }));
    const b = blockOf(1024, false, {}, { controllerConnected: connected });
    node.process(b.block);
    expect(b.sent.every((s) => s.port === "controllerOut" && s.frame === 0)).toBe(true);
    return b.sent.length;
  };
  const unattached = sentToSurface({ start: 0, changes: [] });
  expect(unattached).toBeGreaterThan(0);
  expect(sentToSurface({ start: 0, changes: [{ frame: 0, value: 1 }] })).toBeGreaterThan(unattached);
  expect(sentToSurface({ start: 1, changes: [] })).toBeGreaterThan(unattached);
  expect(sentToSurface({ start: 0, changes: [{ frame: 9, value: 1 }] })).toBe(unattached);
});

test("a structure change keeps a survivor's clock as it moves, and a removed id sends nothing", () => {
  const one = settings({ systems: [{ id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiSync" } }] }] });
  const node = create(one);
  const run = () => {
    const b = blockOf(22050, true, {}); // one quarter at 120 bpm: 24 clocks
    node.process(b.block);
    return b.sent;
  };
  const clocks = (sent: Sent[]) => sent.filter((s) => s.port === "sys-1.serialIn").flatMap((s) => s.data).length;
  expect(clocks(run())).toBe(24);
  node.onSettings!(
    settings({
      systems: [
        { id: 2, pipeline: [{ kind: "mgb", config: {} }] },
        { id: 1, pipeline: [{ kind: "lsdj-sync", config: { mode: "midiSync" } }] },
      ],
    }),
  );
  // The clock goes on (ppq stays 0, so a fresh one would resync and send the same 24; one that kept its counter is
  // past this quarter and sends none).
  expect(clocks(run())).toBe(0);
  node.onSettings!(settings({ systems: [] }));
  expect(run()).toEqual([]);
});
