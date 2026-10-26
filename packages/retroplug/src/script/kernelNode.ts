// The DSP kernel as a script node (docs/modular-graph-phase-b3a-design.md section 4): today's DspKernel, with the
// registry the kernel bundle builds (dspKernelBundle.ts: the DSP roles, no core roles, no ROM providers), behind
// defineNode. Its settings are the kernel's structure plus one fact per system, whether it has a serialOut (a Game
// Boy); its ports follow from them and are named by system id, so a structure change is a reshape: the instance is
// kept, a surviving system's wires stay bound to its ports, and setSystems keeps today's rules (each surviving id's
// clock and role scratch, every project stage's state; a removed id pruned).
//
// Each block builds the BlockInput DspRuntime::processBlock builds today, field for field and afresh, and the
// kernel's sink calls go straight onto the output ports in emission order, where today's sink thunks append to
// vectors the engine fans out to the cores.
import { z } from "zod";
import { bytes, control, defineNode, events, startValue, type Block, type Message, type PortSpec } from "../graph/defineNode";
import type { QueueCapacity } from "../graph/descriptor";
import { DspKernel, type BlockInput, type DspTracer, type SinkTarget } from "../dspKernel";
import type { MidiEvent } from "../midiRouting";
import { RoleRegistry } from "../systemRoles";
import { registerDspRoles } from "../dspRoles";

/** The kernel node's type id. Its own is rp.dsp.kernel, which the native kernel stage holds until the engine
 *  switches over (a registry refuses a second type under one id); the switch deletes that node and this one takes
 *  the id, so a document that names rp.dsp.kernel with no settings plans as an empty kernel from then on. */
export const KERNEL_NODE_TYPE = "rp.dsp.kernel-script";

/** RP_MESSAGE_FLUSH (graph/rp_node.h): a bytes message the device takes only after dropping what it still holds. */
const FLUSH = 1;

// What each output carries in one block, at most (design section 2.6): today's sink vectors are unbounded, so these
// are sized from the most the suites send through them in one block. Measured over test:native with the fixtures
// (parity included, 2.2 million kernel blocks up to 8192 frames): per system, 18 serial-in bytes, 8 core-MIDI
// messages holding 4096 bytes (the largest message 512), 11 core-byte messages holding 11 bytes, 1 button press; 14
// host MIDI-out messages holding 70 bytes with their prefixes; 5 control-surface messages holding 206 bytes (in one).
// dsp-bench's densest profile, skipped there, sends 13 messages of 39 bytes a block. Each output has room for 16
// times the messages or more, and the bytes as well, but for core MIDI's (4 times the most measured, 32 of the largest
// message) and the MIDI out's (a decoded MI.OUT stream at its densest, a captured byte every 8 frames: 1024 in 8192).
// A queue costs 16 bytes a message plus its bytes, per plan.
export const KERNEL_SERIAL_IN: QueueCapacity = { messages: 1024, bytes: 1024 };
export const KERNEL_CORE_MIDI: QueueCapacity = { messages: 512, bytes: 16384 };
export const KERNEL_CORE_BYTES: QueueCapacity = { messages: 256, bytes: 1024 };
export const KERNEL_BUTTONS: QueueCapacity = { messages: 64, bytes: 128 };
export const KERNEL_MIDI_OUT: QueueCapacity = { messages: 1024, bytes: 8192 };
export const KERNEL_CONTROLLER_OUT: QueueCapacity = { messages: 256, bytes: 4096 };

const role = z.object({ kind: z.string(), config: z.record(z.string(), z.unknown()).default({}) });

const settings = z.object({
  /** The project-scope stages (MIDI routing, a controller session), run before every system. */
  project: z.array(role).default([]),
  /** Each system in slot order (positional routing reads it), its pipeline, and whether it has a serialOut. */
  systems: z
    .array(
      z.object({
        id: z.number().int().min(1).max(0xffffffff),
        pipeline: z.array(role).default([]),
        serialOut: z.boolean().default(false),
      }),
    )
    .refine((systems) => new Set(systems.map((s) => s.id)).size === systems.length, "two systems have the same id")
    .default([]),
});

export type KernelSettings = z.infer<typeof settings>;

/** One system's port ids, made once per settings so a sink call builds no string. */
interface SystemPorts {
  id: number;
  serialIn: string;
  midi: string;
  bytes: string;
  buttons: string;
  /** Its MI.OUT input, for a system with a serialOut. */
  serialOut: string | null;
}

const portsOf = (s: KernelSettings): SystemPorts[] =>
  s.systems.map(({ id, serialOut }) => ({
    id,
    serialIn: `sys-${id}.serialIn`,
    midi: `sys-${id}.midi`,
    bytes: `sys-${id}.bytes`,
    buttons: `sys-${id}.buttons`,
    serialOut: serialOut ? `sys-${id}.serialOut` : null,
  }));

// The per-role tracing thunks a profile build binds (spec/08-profiling.md Tier B; Engine.cpp, into the script host's
// audio context), the kernel bundle's three and traceArmed, the trace window's state, read each block in place of
// the bundle's __setTrace. `typeof` is safe on an unbound global, which is every other build.
declare function spanBegin(label: number): void;
declare function spanEnd(): void;
declare function traceName(label: number, kind: string): void;
declare function traceArmed(): boolean;
const traced = typeof spanBegin === "function" && typeof traceArmed === "function";

// The DSP roles alone, as the kernel bundle registers them: a behaviour keeps its state in the kernel's scratch
// bags, so every instance can share them.
const registry = new RoleRegistry();
registerDspRoles(registry);

// An events or bytes input as today's marshalling hands it to the kernel: fresh {frame, data} objects, `data` a plain
// array of numbers (a role may keep a reference across blocks, and may rely on what an array is).
function midiEvents(messages: readonly Message[]): MidiEvent[] {
  const out: MidiEvent[] = [];
  for (let i = 0; i < messages.length; i++) {
    const m = messages[i];
    const data: number[] = [];
    for (let j = 0; j < m.data.length; j++) data.push(m.data[j]);
    out.push({ frame: m.frame, data });
  }
  return out;
}

// The block's input, built as DspRuntime::processBlock builds it: the transport, controllerConnected only when true,
// midiIn, empty buttons and keys (no host feeds them yet), every system's MI.OUT bytes from last block in slot order,
// and controllerIn only when a surface sent something. Fresh every block, as today.
function blockInput(b: Block, systems: readonly SystemPorts[]): BlockInput {
  const t = b.transport;
  const input = {
    frames: b.frames,
    sampleRate: t.sampleRate,
    tempo: t.tempo,
    ppqStart: t.ppq,
    transport: t.playing,
  } as BlockInput;
  if (startValue(b.control("controllerConnected")) !== 0) input.controllerConnected = true;
  input.midiIn = midiEvents(b.messages("midiIn"));
  input.buttons = [];
  input.keys = [];
  const serialOut: BlockInput["serialOut"] = [];
  for (let s = 0; s < systems.length; s++) {
    const port = systems[s].serialOut;
    if (port === null) continue;
    const messages = b.messages(port);
    for (let i = 0; i < messages.length; i++) {
      const data = messages[i].data;
      for (let j = 0; j < data.length; j++) serialOut.push({ system: systems[s].id, byte: data[j] });
    }
  }
  input.serialOut = serialOut;
  const controllerIn = b.messages("controllerIn");
  if (controllerIn.length > 0) input.controllerIn = midiEvents(controllerIn);
  return input;
}

export const kernelNode = defineNode<KernelSettings>({
  id: KERNEL_NODE_TYPE,
  name: "DSP Kernel",
  category: "dsp",
  settings,
  reshapes: true,
  ports: (s) => {
    const p: Record<string, PortSpec> = {
      midiIn: events.in(),
      controllerIn: events.in(),
      controllerConnected: control.in(),
    };
    const systems = portsOf(s);
    for (const sys of systems) if (sys.serialOut) p[sys.serialOut] = bytes.in();
    for (const sys of systems) {
      p[sys.serialIn] = bytes.out(KERNEL_SERIAL_IN);
      p[sys.midi] = events.out(KERNEL_CORE_MIDI);
      p[sys.bytes] = bytes.out(KERNEL_CORE_BYTES);
      p[sys.buttons] = events.out(KERNEL_BUTTONS);
    }
    p.midiOut = events.out(KERNEL_MIDI_OUT);
    p.controllerOut = events.out(KERNEL_CONTROLLER_OUT);
    return p;
  },
  create: ({ settings: initial }) => {
    let systems = portsOf(initial);
    let byId = new Map(systems.map((s) => [s.id, s]));
    let block: Block | null = null; // the block being processed, which every sink call sends on
    const one = new Uint8Array(1);
    const press = new Uint8Array(2);
    const tagged: number[] = [];

    // Each kernel sink call onto its port, in emission order; a call naming a system with no ports is dropped, as
    // today's fan-out skips an id it cannot find. Values convert as the thunks convert them (each byte & 0xff).
    const sink: SinkTarget = {
      pushSerialIn: (system, frame, byte) => {
        const p = byId.get(system);
        if (!p) return;
        one[0] = byte;
        block!.send(p.serialIn, frame, one);
      },
      emitCoreMidi: (system, frame, data) => {
        const p = byId.get(system);
        if (p) block!.send(p.midi, frame, data);
      },
      pushCoreBytes: (system, frame, data, flush) => {
        const p = byId.get(system);
        if (p) block!.send(p.bytes, frame, data, flush ? FLUSH : 0);
      },
      // At frame 0, whatever the call says: the core takes a role's press with no frame (today's fan-out).
      pressButton: (system, _frame, button, down) => {
        const p = byId.get(system);
        if (!p) return;
        press[0] = button;
        press[1] = down ? 1 : 0;
        block!.send(p.buttons, 0, press);
      },
      // One port for every system, in emission order, each message behind its system's id, 4 bytes little-endian
      // (design section 4.4), which rp.host.midi-out strips into the message's system tag.
      emitMidiOut: (system, frame, data) => {
        tagged.length = 0;
        tagged.push(system & 0xff, (system >>> 8) & 0xff, (system >>> 16) & 0xff, (system >>> 24) & 0xff);
        for (let i = 0; i < data.length; i++) tagged.push(data[i]);
        block!.send("midiOut", frame, tagged);
      },
      emitControllerOut: (data) => {
        block!.send("controllerOut", 0, data);
      },
    };
    const tracer: DspTracer | undefined = traced
      ? { name: (label, kind) => traceName(label, kind), begin: (label) => spanBegin(label), end: () => spanEnd() }
      : undefined;
    const kernel = new DspKernel(registry, sink, tracer);
    kernel.setSystems(initial);

    return {
      // A throw goes to the script host, which counts it and runs this again next block: what the kernel sent
      // before it stands, as today.
      process(b) {
        block = b;
        if (tracer) kernel.setTracing(traceArmed());
        kernel.processBlock(blockInput(b, systems));
      },
      onSettings(next) {
        systems = portsOf(next);
        byId = new Map(systems.map((s) => [s.id, s]));
        kernel.setSystems(next);
      },
    };
  },
});
