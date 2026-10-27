// A script node driven as the script host drives one (packages/native/src/host/script/ScriptHost.cpp), over a stand-in
// for its Block: settings parsed on the control side and crossed as JSON, a plan swap's onSettings only when they
// changed, and a block that holds the node to its layout. What the stand-in models:
//
// - messages, send and control take only a port the layout has, of the method's kind, and throw a TypeError for
//   another, as the host does; every Block method throws once the process call it was handed to has returned.
// - send copies the data (each value & 0xff) and records the frame twice: as sent, and as the port's queue holds it,
//   raised to the last frame sent on that port this block.
// - The messages a block hands out are the node's for that call only: their bytes are scribbled over once it returns,
//   so a node that kept a view past the block reads garbage, as it would from the host's refilled buffers.
import type { Block, ControlStream, DefinedNode, Message, NodeInstance } from "../../../src/graph/defineNode";
import type { Layout } from "../../../src/graph/descriptor";

/** An input message as a test writes it. */
export interface In {
  frame: number;
  data: number[];
  flags?: number;
}

/** One send, in the order the node made it. */
export interface Sent {
  port: string;
  /** The frame the node sent. */
  frame: number;
  /** The frame the port's queue holds it at: raised to the last one sent on the port this block. */
  queued: number;
  data: number[];
  flags: number;
}

export interface BlockSpec {
  frames: number;
  playing?: boolean;
  ppq?: number;
  tempo?: number;
  sampleRate?: number;
  inputs?: Record<string, readonly In[]>;
  controls?: Record<string, ControlStream>;
}

const json = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

export class NodeRig<S> {
  node: NodeInstance<S>;
  settings: S;
  private layout: Layout;
  /** How many instances create made: 1, and one more for each settings change the definition does not reshape. */
  creates = 1;
  /** The last block's sends, kept when its process threw. */
  last: Sent[] = [];

  constructor(
    readonly def: DefinedNode<S>,
    raw: unknown = {},
    readonly sampleRate = 44100,
    readonly maxFrames = 8192,
  ) {
    this.settings = json(def.parseSettings(raw));
    this.layout = def.layout(this.settings);
    this.node = def.create({ settings: this.settings, sampleRate, maxFrames });
  }

  /** New settings at a plan swap. Unchanged ones (by their JSON) are no change, and reach no hook; a changed layout
   *  without `reshapes` is a new instance. True when the node saw the change. */
  set(raw: unknown): boolean {
    const next = json(this.def.parseSettings(raw));
    if (JSON.stringify(next) === JSON.stringify(this.settings)) return false;
    const layout = this.def.layout(next);
    const reshaped = JSON.stringify(layout) !== JSON.stringify(this.layout);
    this.settings = next;
    this.layout = layout;
    if (reshaped && this.def.reshapes !== true) {
      this.node = this.def.create({ settings: json(next), sampleRate: this.sampleRate, maxFrames: this.maxFrames });
      this.creates++;
    } else {
      this.node.onSettings?.(json(next));
    }
    return true;
  }

  /** The layout's port ids, as `dir kind id`. */
  ports(): string[] {
    return this.layout.ports.map((p) => `${p.dir} ${p.kind} ${p.id}`);
  }

  /** One block, and every send it made. A throw from process is the caller's, the sends before it kept in `last`. */
  run(spec: BlockSpec): Sent[] {
    const sent: Sent[] = [];
    this.last = sent;
    const ports = this.layout.ports;
    const port = (id: unknown, dir: "in" | "out", kinds: readonly string[], what: string) => {
      const p = ports.find((q) => q.id === id && q.dir === dir && kinds.includes(q.kind));
      if (!p) throw new TypeError(`${this.def.id} has no ${what} ${String(id)}`);
      return p;
    };
    let live = true;
    const alive = () => {
      if (!live) throw new TypeError("a block is used only during its node's process call");
    };
    const handedOut: Uint8Array[] = [];
    const lastFrame = new Map<string, number>();
    const block: Block = {
      frames: spec.frames,
      transport: { sampleRate: spec.sampleRate ?? 44100, tempo: spec.tempo ?? 120, ppq: spec.ppq ?? 0, playing: spec.playing ?? false },
      audio: () => {
        throw new TypeError("no audio ports here");
      },
      messages: (id) => {
        alive();
        port(id, "in", ["events", "bytes"], "events or bytes input");
        return (spec.inputs?.[id] ?? []).map((m): Message => {
          const data = Uint8Array.from(m.data);
          handedOut.push(data);
          return { frame: m.frame, data, flags: m.flags ?? 0 };
        });
      },
      send: (id, frame, data, flags = 0) => {
        alive();
        port(id, "out", ["events", "bytes"], "events or bytes output");
        const at = frame >>> 0;
        const queued = Math.max(at, lastFrame.get(id) ?? 0);
        lastFrame.set(id, queued);
        sent.push({ port: id, frame: at, queued, data: Array.from(data, (v) => v & 0xff), flags: flags >>> 0 });
        return true;
      },
      control: (id) => {
        alive();
        port(id, "in", ["control"], "control input or param");
        return spec.controls?.[id] ?? { start: 0, changes: [] };
      },
      emit: () => {
        throw new TypeError("no control outputs here");
      },
    };
    try {
      this.node.process(block);
    } finally {
      live = false;
      for (const d of handedOut) d.fill(0xee);
    }
    return sent;
  }
}

/** The sends on `port`, each as [queued frame, ...bytes]. */
export const on = (sent: readonly Sent[], port: string): number[][] => sent.filter((s) => s.port === port).map((s) => [s.queued, ...s.data]);

/** The bytes sent on `port`, each as [queued frame, byte]: how a serial-in port is compared, byte by byte. */
export const bytesOn = (sent: readonly Sent[], port: string): [number, number][] => {
  const out: [number, number][] = [];
  for (const s of sent) if (s.port === port) for (const b of s.data) out.push([s.queued, b]);
  return out;
};
