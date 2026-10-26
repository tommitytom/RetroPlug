// The DSP kernel as a script node (src/script/kernelNode.ts): its settings, the ports they give, and where each of the
// kernel's sink calls goes, over a stand-in for the script host's Block. That it does exactly what today's kernel
// does, block for block, is held natively, against DspRuntime (packages/native/test/engine-graph/KernelNode.test.cpp).
import { test, expect } from "../../testing/harness";
import type { Block, ControlStream, Message } from "../../src/graph/defineNode";
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
const create = (s: KernelSettings) => kernelNode.create({ settings: s, sampleRate: 44100, maxFrames: 1024 });

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
  expect(at("sys-1.serialIn")).toEqual([[4, 0x90, 0], [4, 60, 0], [4, 100, 0]]);
  expect(at("sys-2.midi")).toEqual([[4, 0x90, 60, 100, 0]]);
  const id = [4, 3, 2, 1]; // 0x01020304, little-endian
  expect(at("midiOut")).toEqual([[0, ...id, 0x90, 5, 0x7f, 0], [0, ...id, 0xfa, 0], [0, ...id, 0xf8, 0], [0, ...id, 0xf8, 0]]);
  // risa's arm flushes the FIFO; START and the first clock follow it.
  expect(at("sys-4.bytes").slice(0, 2)).toEqual([[0, 0xf9, 0x52, 0, 0, 0, 1], [0, 0xfa, 0]]);
  // Systems run in slot order, so their ports fill in that order.
  const order = sent.map((s) => s.port).filter((p, i, all) => all.indexOf(p) === i);
  expect(order).toEqual(["sys-1.serialIn", "sys-2.midi", "midiOut", "sys-4.bytes"]);
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
  expect(run().filter((s) => s.port === "sys-1.serialIn").length).toBe(24);
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
  expect(run().filter((s) => s.port === "sys-1.serialIn").length).toBe(0);
  node.onSettings!(settings({ systems: [] }));
  expect(run()).toEqual([]);
});
