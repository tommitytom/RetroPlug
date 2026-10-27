// The DSP kernel as a script node (docs/modular-graph-phase-b3a-design.md section 4): the DspKernel (src/dspKernel.ts),
// with a registry of the DSP roles alone (no core roles, no ROM providers), behind defineNode. Its settings are the
// kernel's structure plus one fact per system, whether it has a serialOut (a Game Boy); its ports follow from them and
// are named by system id, so a structure change is a reshape: the instance is kept, a surviving system's wires stay
// bound to its ports, and setSystems keeps its rules (each surviving id's clock and role scratch, every project
// stage's state; a removed id pruned).
//
// Each block builds the BlockInput the native kernel stage built before it (packages/native, deleted after the flip),
// field for field and afresh, and the kernel's sink calls go onto the output ports in emission order: each straight,
// but for a system's serial-in bytes, which go a run of same-frame bytes to a message.
import { z } from "zod";
import { bytes, control, defineNode, events, startValue, type Block, type Message, type PortSpec } from "../graph/defineNode";
import type { QueueCapacity } from "../graph/descriptor";
import { DspKernel, type BlockInput, type DspTracer, type SinkTarget } from "../dspKernel";
import type { MidiEvent } from "../midiRouting";
import { RoleRegistry } from "../systemRoles";
import { registerDspRoles } from "../dspRoles";
import { BUTTONS, CONTROLLER_OUT, CORE_BYTES, CORE_MIDI, FLUSH, MIDI_OUT, SERIAL_IN } from "./nodes/ports";

/** The kernel node's type id. A document that names it with no settings plans as an empty kernel. */
export const KERNEL_NODE_TYPE = "rp.dsp.kernel";

// What each output carries in one block, at most (design section 2.6): the kernel stage's sink vectors were unbounded,
// and a queue drops what does not fit. Sized, and explained, in nodes/ports.ts, which the protocol script nodes that
// replace this one take their capacities from, port for port. A queue costs 16 bytes a message plus its bytes, per
// plan: a system's four take 278 KiB, nearly all of it the two that pass host MIDI on, so 33 systems take 9.0 MiB of
// the plan's 64 MiB queue budget.
export const KERNEL_SERIAL_IN: QueueCapacity = SERIAL_IN;
export const KERNEL_CORE_MIDI: QueueCapacity = CORE_MIDI;
export const KERNEL_CORE_BYTES: QueueCapacity = CORE_BYTES;
export const KERNEL_BUTTONS: QueueCapacity = BUTTONS;
export const KERNEL_MIDI_OUT: QueueCapacity = MIDI_OUT;
export const KERNEL_CONTROLLER_OUT: QueueCapacity = CONTROLLER_OUT;

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

/** One system's port ids, made once per settings so a sink call builds no string, and its serial-in run. */
interface SystemPorts {
  id: number;
  serialIn: string;
  midi: string;
  bytes: string;
  buttons: string;
  /** Its MI.OUT input, for a system with a serialOut. */
  serialOut: string | null;
  /** The serial-in bytes not yet sent: consecutive pushSerialIn calls at runFrame, sent as one message. Empty
   *  from each block's end, or from the next process when the deadline stopped the call; reused, so it allocates
   *  nothing once grown. */
  run: number[];
  runFrame: number;
}

const portsOf = (s: KernelSettings): SystemPorts[] =>
  s.systems.map(({ id, serialOut }) => ({
    id,
    serialIn: `sys-${id}.serialIn`,
    midi: `sys-${id}.midi`,
    bytes: `sys-${id}.bytes`,
    buttons: `sys-${id}.buttons`,
    serialOut: serialOut ? `sys-${id}.serialOut` : null,
    run: [],
    runFrame: 0,
  }));

// The trace window's globals a profile build binds into the script host's audio context (spec/08-profiling.md Tier
// B; ScriptHost.cpp): a span per role, and traceArmed, the window's state, read each block. `typeof` is safe on an
// unbound global, which is every other build.
declare function spanBegin(label: number): void;
declare function spanEnd(): void;
declare function traceName(label: number, kind: string): void;
declare function traceArmed(): boolean;
const traced = typeof spanBegin === "function" && typeof traceArmed === "function";

// The DSP roles alone: a behaviour keeps its state in the kernel's scratch bags, so every instance can share them.
const registry = new RoleRegistry();
registerDspRoles(registry);

// An events or bytes input as the kernel stage's marshalling handed it to the kernel: fresh {frame, data} objects,
// `data` a plain array of numbers (a role may keep a reference across blocks, and may rely on what an array is).
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

// The block's input, built as the native kernel stage built it: the transport, controllerConnected only when true,
// midiIn, empty buttons and keys (no host feeds them yet), every system's serial-out bytes (what MI.OUT and master
// sync decode) from last block in slot order (each with its frame in that block), and controllerIn only when a surface
// sent something. Fresh every block, as the stage's was.
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
      const m = messages[i];
      for (let j = 0; j < m.data.length; j++) serialOut.push({ system: systems[s].id, frame: m.frame, byte: m.data[j] });
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
    // Relies on all host MIDI out being at frame 0 today, or later systems' are raised (B3b: a port per protocol).
    p.midiOut = events.out(KERNEL_MIDI_OUT);
    p.controllerOut = events.out(KERNEL_CONTROLLER_OUT);
    return p;
  },
  create: ({ settings: initial }) => {
    let systems = portsOf(initial);
    let byId = new Map(systems.map((s) => [s.id, s]));
    let block: Block | null = null; // the block being processed, which every sink call sends on
    const press = new Uint8Array(2);
    const tagged: number[] = [];
    // At least the number of non-empty runs, so a block that holds none skips the run bookkeeping: raised before a
    // byte starts a run, and set to 0 only once every run is empty. A run sent at a frame change is refilled by the
    // same call, so it stands; a call stopped or thrown part way can leave it too high, never too low.
    let held = 0;

    const sendRun = (p: SystemPorts) => {
      block!.send(p.serialIn, p.runFrame, p.run);
      p.run.length = 0;
    };
    const sendRuns = () => {
      for (let s = 0; s < systems.length; s++) if (systems[s].run.length > 0) sendRun(systems[s]);
      held = 0;
    };

    // Each kernel sink call onto its port, in emission order; a call naming a system with no ports is dropped, as
    // the kernel stage's fan-out skipped an id it could not find. Values convert as its thunks converted them (each
    // byte & 0xff).
    const sink: SinkTarget = {
      // A run of bytes at one frame goes as one message, which the system node applies a byte at a time at its frame,
      // as it applied a message a byte: a byte at another frame sends the run before it, and process sends the rest.
      pushSerialIn: (system, frame, byte) => {
        const p = byId.get(system);
        if (!p) return;
        if (p.run.length === 0) ++held;
        else if (frame !== p.runFrame) sendRun(p);
        p.runFrame = frame;
        p.run.push(byte & 0xff);
      },
      emitCoreMidi: (system, frame, data) => {
        const p = byId.get(system);
        if (p) block!.send(p.midi, frame, data);
      },
      pushCoreBytes: (system, frame, data, flush) => {
        const p = byId.get(system);
        if (p) block!.send(p.bytes, frame, data, flush ? FLUSH : 0);
      },
      // At frame 0, whatever the call says: the core takes a role's press with no frame (as the kernel stage's fan-out
      // gave it).
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
      // before it stands, as it stood under the kernel stage, its held runs included. A call the deadline stops skips
      // the finally, so a run it left (held is still above 0) is cleared here rather than sent in a block it was not
      // pushed in.
      process(b) {
        block = b;
        if (held > 0) {
          for (let s = 0; s < systems.length; s++) systems[s].run.length = 0;
          held = 0;
        }
        if (tracer) kernel.setTracing(traceArmed());
        try {
          kernel.processBlock(blockInput(b, systems));
        } finally {
          if (held > 0) sendRuns();
        }
      },
      // Between blocks. The new ports' runs start empty, so no byte crosses a reshape onto another port.
      onSettings(next) {
        systems = portsOf(next);
        held = 0;
        byId = new Map(systems.map((s) => [s.id, s]));
        kernel.setSystems(next);
      },
    };
  },
});
