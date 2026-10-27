// TEMPORARY: deleted with the kernel (docs/modular-graph-phase-b3b-design.md section 7, item 6), once nothing is left
// to hold the per-role nodes to.
//
// The per-role script nodes (src/script/nodes/) against the DspKernel they replace, block for block. Each case builds
// one project two ways: the kernel's structure, as kernelProjection published it, run by a DspKernel over a
// CollectingSink; and the per-role graph as buildGraph is to place it (a routing node, a controller node when there
// is a controller, each system's protocol nodes in slot order), each node over the Block stand-in, its outputs wired
// into the next nodes' inputs within the block. Both are driven through the same blocks and every output is compared,
// port for port: each frame as the port's queue holds it (a frame sent behind a later one raised to it), every byte,
// every flag, in order. Serial in is compared byte by byte, since how its bytes are grouped into messages is free
// (design section 5); host MIDI out as rp.host.midi-out is to merge it, the controller's first, then each system's in
// slot order, each message under its system's id.
//
// What is deliberately not driven here, because the per-role graph does it differently on purpose (and a later stage
// must keep it in mind):
// - A role removed from a system that stays, then added back: the kernel kept the stage's scratch and the system's
//   clock (keyed by system id and stage index), where a new node starts fresh. The same keying gave a role the scratch
//   of whatever stood at its index before, when a role ahead of it came or went.
// - A controller whose system id is 0 (the first system) while the first system changes: the kernel resolved the id
//   when it built the session and compared the configured 0 for a rebuild, so it kept launching to the old first
//   system; the node takes the resolved id, so a new first system rebuilds the session (and re-takes the device).
// - A controller's app config that changes only in what the app's schema reads the same (`{}` against the defaults
//   spelled out): the kernel compared the raw config for a rebuild, the node compares it parsed.
// - Launches to a system with no lsdj-sync: the kernel put them in its inbox, which an mgb or nes-n8-midi stage
//   forwarded; only rp.lsdj.sync has a launches input.
import { test, expect } from "../../../testing/harness";
import type { ControlStream, DefinedNode } from "../../../src/graph/defineNode";
import { CollectingSink, DspKernel, type BlockInput, type KernelStructure } from "../../../src/dspKernel";
import { RoleRegistry, type RoleInstance } from "../../../src/systemRoles";
import { registerDspRoles } from "../../../src/dspRoles";
import { ControllerRegistry, registerControllerApps } from "../../../src/controller";
import { MIDI_ROUTING_VALUES, LSDJ_MODE_VALUES, type MidiRouting } from "../../../src/settingsEnums";
import { songRowTicks } from "../../../src/lsdj/playback";
import { SongSchema } from "../../../src/lsdj/model";
import { padIndex } from "../../../src/launchpad";
import { midiRouteNode, routedPort } from "../../../src/script/nodes/midiRoute";
import { mgbMidiNode } from "../../../src/script/nodes/mgbMidi";
import { n8MidiToFifoNode } from "../../../src/script/nodes/n8MidiToFifo";
import { risaSyncNode } from "../../../src/script/nodes/risaSync";
import { smsSyncNode } from "../../../src/script/nodes/smsSync";
import { lsdjSyncNode } from "../../../src/script/nodes/lsdjSync";
import { controllerSessionNode } from "../../../src/script/nodes/controllerSession";
import { NodeRig, type In, type Sent } from "./_rig";

// ---- the project ----------------------------------------------------------------------------------------------------

type RoleKind = "mgb" | "nes-n8-midi" | "risa-sync" | "sms-sync" | "lsdj-sync";
interface Role {
  kind: RoleKind;
  config?: Record<string, unknown>;
}
interface SystemSpec {
  id: number;
  roles: Role[];
  /** A Game Boy: its serial-out bytes reach the kernel, and its lsdj-sync node, a block late. */
  gameBoy?: boolean;
}
interface ControllerSpec {
  app?: string;
  target?: "system" | "midiOut";
  /** As the user chose it: 0 is the first system. */
  systemId?: number;
  /** Raw, as the project holds it. */
  appConfig?: Record<string, unknown>;
  songRowTicks: unknown;
  anchorRows?: (number | null)[];
  anchorSeq?: number;
}
interface Project {
  routing: MidiRouting;
  systems: SystemSpec[];
  controller?: ControllerSpec;
}

const roles = new RoleRegistry();
registerDspRoles(roles);
const apps = new ControllerRegistry();
registerControllerApps(apps);

/** A role's config as the stores hand it over: parsed by the role's own schema, its defaults filled. */
const roleConfig = (r: Role): Record<string, unknown> => roles.roleType(r.kind)!.schema.parse(r.config ?? {}) as Record<string, unknown>;

/** The kernel's structure, as kernelProjection built it: the routing stage, the launchpad stage with the
 *  controller's settings as given, and each system's pipeline. Fresh objects each time, as each publish's are. */
function structureOf(p: Project): KernelStructure {
  const project: RoleInstance[] = [{ kind: "midi-routing", config: { mode: p.routing } }];
  if (p.controller) {
    const c = p.controller;
    project.push({
      kind: "launchpad",
      config: {
        app: c.app ?? "lsdj-midimap",
        target: c.target ?? "system",
        systemId: c.systemId ?? 0,
        appConfig: c.appConfig ?? {},
        songRowTicks: c.songRowTicks,
        anchorRows: c.anchorRows ?? [],
        anchorSeq: c.anchorSeq ?? 0,
      },
    });
  }
  return { project, systems: p.systems.map((s) => ({ id: s.id, pipeline: s.roles.map((r) => ({ kind: r.kind, config: roleConfig(r) })) })) };
}

/** The controller's resolved system: the chosen one, or the first system for 0. */
const targetOf = (p: Project): number => {
  const c = p.controller!;
  return (c.systemId ?? 0) > 0 ? c.systemId! : (p.systems[0]?.id ?? 0);
};

interface NodeSpec {
  id: string;
  def: DefinedNode<any>;
  settings: unknown;
}
const PROTOCOL: Record<RoleKind, DefinedNode<any>> = {
  mgb: mgbMidiNode,
  "nes-n8-midi": n8MidiToFifoNode,
  "risa-sync": risaSyncNode,
  "sms-sync": smsSyncNode,
  "lsdj-sync": lsdjSyncNode,
};
const protocolId = (system: number, kind: RoleKind) => `sys-${system}/${kind}`;

/** The per-role graph's script nodes, in the order buildGraph is to place them, each with the settings it is to write:
 *  a role's config as the stores parsed it, the controller's id resolved and its app config parsed by the app's
 *  schema. */
function graphOf(p: Project): NodeSpec[] {
  const nodes: NodeSpec[] = [{ id: "routing", def: midiRouteNode, settings: { mode: p.routing, systems: p.systems.map((s) => s.id) } }];
  if (p.controller) {
    const c = p.controller;
    const app = c.app ?? "lsdj-midimap";
    const type = apps.get(app);
    nodes.push({
      id: "controller",
      def: controllerSessionNode,
      settings: {
        app,
        target: c.target ?? "system",
        systemId: targetOf(p),
        appConfig: type ? type.schema.parse(c.appConfig ?? {}) : {},
        songRowTicks: c.songRowTicks,
        anchorRows: c.anchorRows ?? [],
        anchorSeq: c.anchorSeq ?? 0,
      },
    });
  }
  for (const s of p.systems) for (const r of s.roles) nodes.push({ id: protocolId(s.id, r.kind), def: PROTOCOL[r.kind], settings: roleConfig(r) });
  return nodes;
}

// ---- one block, and what both sides make of it ------------------------------------------------------------------------

interface Step {
  frames: number;
  sampleRate: number;
  tempo: number;
  ppq: number;
  playing: boolean;
  midiIn: In[];
  controllerIn: In[];
  connected: ControlStream;
  /** Each Game Boy's serial-out bytes from the last block, at their frames in it. */
  serialOut: Record<number, In[]>;
  /** A settings change, before this block. */
  project?: Project;
}

/** A message as compared: [frame as queued, flags, ...bytes]. */
type Msg = number[];
interface Outputs {
  serialIn: Record<number, [number, number][]>;
  coreMidi: Record<number, Msg[]>;
  coreBytes: Record<number, Msg[]>;
  buttons: Record<number, Msg[]>;
  /** [system, frame as queued, ...bytes], in host MIDI out's order. */
  midiOut: Msg[];
  controllerOut: Msg[];
}
const emptyOutputs = (): Outputs => ({ serialIn: {}, coreMidi: {}, coreBytes: {}, buttons: {}, midiOut: [], controllerOut: [] });
const push = <T>(into: Record<number, T[]>, id: number, v: T) => (into[id] ??= []).push(v);

/** A queue's frame rule: a frame earlier than the last one on the port is raised to it. */
class Raise {
  private readonly last = new Map<string, number>();
  at(port: string, frame: number): number {
    const queued = Math.max(frame >>> 0, this.last.get(port) ?? 0);
    this.last.set(port, queued);
    return queued;
  }
}

const startOf = (c: ControlStream): number => {
  let v = c.start;
  for (let i = 0; i < c.changes.length && c.changes[i].frame === 0; i++) v = c.changes[i].value;
  return v;
};

class KernelSide {
  private readonly sink = new CollectingSink();
  private readonly kernel = new DspKernel(roles, this.sink);
  private project: Project;
  constructor(project: Project) {
    this.project = project;
    this.kernel.setSystems(structureOf(project));
  }
  set(project: Project): void {
    this.project = project;
    this.kernel.setSystems(structureOf(project));
  }
  // The BlockInput the kernel node builds: the midiIn events afresh, controllerConnected only when true, the Game Boys'
  // serial-out bytes in slot order, controllerIn only when the surface sent something.
  run(step: Step): Outputs {
    const input = {
      frames: step.frames,
      sampleRate: step.sampleRate,
      tempo: step.tempo,
      ppqStart: step.ppq,
      transport: step.playing,
    } as BlockInput;
    if (startOf(step.connected) !== 0) input.controllerConnected = true;
    input.midiIn = step.midiIn.map((m) => ({ frame: m.frame, data: [...m.data] }));
    input.buttons = [];
    input.keys = [];
    input.serialOut = [];
    for (const s of this.project.systems) {
      if (!s.gameBoy) continue;
      for (const m of step.serialOut[s.id] ?? []) for (const b of m.data) input.serialOut.push({ system: s.id, frame: m.frame, byte: b });
    }
    if (step.controllerIn.length > 0) input.controllerIn = step.controllerIn.map((m) => ({ frame: m.frame, data: [...m.data] }));
    const sinks = this.kernel.processBlock(input);
    const out = emptyOutputs();
    const raise = new Raise();
    for (const e of sinks.serialIn) push(out.serialIn, e.system, [raise.at(`${e.system}.serialIn`, e.frame), e.byte & 0xff] as [number, number]);
    for (const e of sinks.coreMidi) push(out.coreMidi, e.system, [raise.at(`${e.system}.midi`, e.frame), 0, ...e.data]);
    for (const e of sinks.coreBytes) push(out.coreBytes, e.system, [raise.at(`${e.system}.bytes`, e.frame), e.flush ? 1 : 0, ...e.data]);
    for (const e of sinks.buttons) push(out.buttons, e.system, [raise.at(`${e.system}.buttons`, 0), 0, e.button, e.down ? 1 : 0]);
    for (const e of sinks.midiOut) out.midiOut.push([e.system >>> 0, raise.at("midiOut", e.frame), ...e.data]);
    for (const d of sinks.controllerOut) out.controllerOut.push([raise.at("controllerOut", 0), 0, ...d]);
    return out;
  }
}

const asInputs = (sent: readonly Sent[], port: string): In[] =>
  sent.filter((s) => s.port === port).map((s) => ({ frame: s.queued, data: s.data, flags: s.flags }));
const asMsgs = (sent: readonly Sent[], port: string): Msg[] => sent.filter((s) => s.port === port).map((s) => [s.queued, s.flags, ...s.data]);

class GraphSide {
  private readonly nodes = new Map<string, NodeRig<any>>();
  private project!: Project;
  constructor(project: Project) {
    this.set(project);
  }
  /** A plan swap: a node kept where its id and type are, its settings handed over when they changed; a new one made
   *  fresh; one no longer placed dropped. */
  set(project: Project): void {
    this.project = project;
    const next = new Map<string, NodeRig<any>>();
    for (const spec of graphOf(project)) {
      const kept = this.nodes.get(spec.id);
      if (kept && kept.def === spec.def) {
        kept.set(spec.settings);
        next.set(spec.id, kept);
      } else {
        next.set(spec.id, new NodeRig(spec.def, spec.settings));
      }
    }
    this.nodes.clear();
    for (const [id, rig] of next) this.nodes.set(id, rig);
  }
  run(step: Step): Outputs {
    const p = this.project;
    const out = emptyOutputs();
    const block = { frames: step.frames, sampleRate: step.sampleRate, tempo: step.tempo, ppq: step.ppq, playing: step.playing };
    const routed = this.nodes.get("routing")!.run({ ...block, inputs: { midiIn: step.midiIn } });
    let launches: In[] = [];
    let launchTarget = -1;
    if (p.controller) {
      const sent = this.nodes.get("controller")!.run({
        ...block,
        inputs: { controllerIn: step.controllerIn },
        controls: { controllerConnected: step.connected },
      });
      out.controllerOut = asMsgs(sent, "controllerOut");
      for (const s of sent) if (s.port === "midiOut") out.midiOut.push([targetOf(p), s.queued, ...s.data]);
      if ((p.controller.target ?? "system") === "system") {
        launches = asInputs(sent, "launches");
        launchTarget = targetOf(p);
      }
      expect(sent.every((s) => ["controllerOut", "midiOut", "launches"].includes(s.port))).toBe(true);
    }
    for (const s of p.systems) {
      const midi = asInputs(routed, routedPort(s.id));
      for (const r of s.roles) {
        const rig = this.nodes.get(protocolId(s.id, r.kind))!;
        const inputs: Record<string, In[]> =
          r.kind === "nes-n8-midi"
            ? { midiIn: midi }
            : r.kind === "lsdj-sync"
              ? { midi, launches: launchTarget === s.id ? launches : [], serialOut: s.gameBoy ? (step.serialOut[s.id] ?? []) : [] }
              : r.kind === "mgb"
                ? { midi }
                : {};
        const sent = rig.run({ ...block, inputs });
        for (const m of sent) {
          if (m.port === "serialIn") for (const b of m.data) push(out.serialIn, s.id, [m.queued, b] as [number, number]);
          else if (m.port === "midi") push(out.coreMidi, s.id, [m.queued, m.flags, ...m.data]);
          else if (m.port === "bytes") push(out.coreBytes, s.id, [m.queued, m.flags, ...m.data]);
          else if (m.port === "buttons") push(out.buttons, s.id, [m.queued, m.flags, ...m.data]);
          else if (m.port === "midiOut") out.midiOut.push([s.id, m.queued, ...m.data]);
          else throw new Error(`${protocolId(s.id, r.kind)} sent on ${m.port}`);
        }
      }
    }
    return out;
  }
}

/** Drive both sides through `steps`, comparing every output of every block. Returns how much was compared, so a
 *  case can check it exercised what it meant to. */
function equivalent(name: string, project: Project, steps: Step[]): Outputs {
  const kernel = new KernelSide(project);
  const graph = new GraphSide(project);
  const total = emptyOutputs();
  steps.forEach((step, i) => {
    if (step.project) {
      kernel.set(step.project);
      graph.set(step.project);
    }
    const want = kernel.run(step);
    const got = graph.run(step);
    const where = `${name}, block ${i} (${step.frames} frames at ppq ${step.ppq}${step.playing ? ", playing" : ""})`;
    for (const key of ["serialIn", "coreMidi", "coreBytes", "buttons", "midiOut", "controllerOut"] as const) {
      expect(got[key], `${where}: ${key}`).toEqual(want[key]);
    }
    for (const [id, v] of Object.entries(want.serialIn)) (total.serialIn[+id] ??= []).push(...v);
    for (const [id, v] of Object.entries(want.coreMidi)) (total.coreMidi[+id] ??= []).push(...v);
    for (const [id, v] of Object.entries(want.coreBytes)) (total.coreBytes[+id] ??= []).push(...v);
    for (const [id, v] of Object.entries(want.buttons)) (total.buttons[+id] ??= []).push(...v);
    total.midiOut.push(...want.midiOut);
    total.controllerOut.push(...want.controllerOut);
  });
  return total;
}

// ---- writing the blocks ---------------------------------------------------------------------------------------------

/** A seeded generator (mulberry32), so every run drives the same blocks. */
function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** Block sizes: a fixed one, or a cycle (the FL-style splits the parity matrix runs). */
const FL_RAGGED = [33, 1, 222, 1, 7, 4096, 2];
const FL_BUFFER = [256, 33, 1, 222, 255, 1, 1, 255, 7, 249];

/** The blocks of a run, written as a host lives them: the transport's state carried from block to block (ppq advances
 *  while playing and holds while stopped), each Game Boy's serial-out bytes on an absolute timeline, delivered a block
 *  late at their frames in the block that produced them. */
class Script {
  readonly steps: Step[] = [];
  ppq = 0;
  playing = false;
  tempo = 120;
  sampleRate = 44100;
  connected: ControlStream = { start: 0, changes: [] };
  private at = 0; // the absolute frame the next block starts at
  private lastStart = 0;
  // Per system: [absolute frame, byte], ascending, and the next one not yet delivered.
  private readonly serial = new Map<number, { line: [number, number][]; next: number }>();

  /** Bytes a Game Boy clocks out, at absolute frames (ascending, from where the script is now on). */
  serialOut(system: number, bytes: [number, number][]): this {
    const s = this.serial.get(system) ?? { line: [], next: 0 };
    s.line.push(...bytes);
    this.serial.set(system, s);
    return this;
  }

  block(frames: number, extra: Partial<Pick<Step, "midiIn" | "controllerIn" | "project">> = {}): this {
    const serialOut: Record<number, In[]> = {};
    for (const [system, s] of this.serial) {
      const msgs: In[] = [];
      for (; s.next < s.line.length && s.line[s.next][0] < this.at; s.next++) {
        const [abs, byte] = s.line[s.next];
        if (abs >= this.lastStart) msgs.push({ frame: abs - this.lastStart, data: [byte] });
      }
      if (msgs.length > 0) serialOut[system] = msgs;
    }
    this.steps.push({
      frames,
      sampleRate: this.sampleRate,
      tempo: this.tempo,
      ppq: this.ppq,
      playing: this.playing,
      midiIn: extra.midiIn ?? [],
      controllerIn: extra.controllerIn ?? [],
      connected: this.connected,
      serialOut,
      ...(extra.project ? { project: extra.project } : {}),
    });
    if (this.playing) this.ppq += frames / ((this.sampleRate * 60) / this.tempo);
    this.lastStart = this.at;
    this.at += frames;
    return this;
  }

  /** `count` blocks sized by `sizes` (a number, or a cycle), each with whatever `each` adds. */
  blocks(count: number, sizes: number | number[], each?: (frames: number, i: number) => Partial<Pick<Step, "midiIn" | "controllerIn" | "project">>): this {
    for (let i = 0; i < count; i++) {
      const frames = typeof sizes === "number" ? sizes : sizes[i % sizes.length];
      this.block(frames, each?.(frames, i));
    }
    return this;
  }
}

/** Random MIDI for a block: `n` messages at ascending frames (as rp.host.midi-in's queue holds them), a block edge
 *  among them now and then, from a pool that exercises every mode: the Arduinoboy notes, MI.MAP's channels, the
 *  keyboard's ranges, SysEx and realtime, a 2-byte and a 4-byte channel message, a note cut short to its status or
 *  its status and note, and the odd empty one. */
function randomMidi(r: () => number, frames: number, n: number): In[] {
  const pick = <T>(xs: readonly T[]): T => xs[Math.floor(r() * xs.length)];
  const frameList: number[] = [];
  for (let i = 0; i < n; i++) {
    const edge = r();
    frameList.push(edge < 0.15 ? 0 : edge < 0.3 ? frames - 1 : Math.floor(r() * frames));
  }
  frameList.sort((a, b) => a - b);
  const channels = [0, 0, 0, 1, 1, 2, 3, 4, 5, 9, 15];
  const notes = [0, 24, 25, 26, 27, 28, 29, 30, 31, 40, 36, 38, 44, 47, 48, 55, 60, 61, 72, 95, 100, 107, 108, 120, 127];
  return frameList.map((frame): In => {
    const ch = pick(channels);
    const kind = r();
    let data: number[];
    if (kind < 0.35) data = [0x90 | ch, pick(notes), 1 + Math.floor(r() * 127)];
    else if (kind < 0.55) data = [0x80 | ch, pick(notes), 0];
    else if (kind < 0.6) data = [0x90 | ch, pick(notes), 0];
    else if (kind < 0.68) data = [0xb0 | ch, Math.floor(r() * 128), Math.floor(r() * 128)];
    else if (kind < 0.72) data = [0xc0 | ch, Math.floor(r() * 128)];
    else if (kind < 0.76) data = [0xe0 | ch, Math.floor(r() * 128), Math.floor(r() * 128)];
    else if (kind < 0.82) data = [pick([0xf8, 0xfa, 0xfc, 0xfe])];
    else if (kind < 0.86) data = [0xf0, ...Array.from({ length: 2 + Math.floor(r() * 30) }, () => Math.floor(r() * 128)), 0xf7];
    else if (kind < 0.9) data = [0x90 | ch, pick(notes), 64, 1]; // four bytes: still a channel message
    else if (kind < 0.94) data = [0x90 | ch, pick(notes), 64, 1, 2]; // five: SysEx's length, broadcast
    else if (kind < 0.95) data = [];
    else if (kind < 0.97) data = r() < 0.5 ? [pick([0x90, 0x80]) | ch] : [pick([0x90, 0x80]) | ch, pick(notes)]; // cut short
    else data = [0x90 | pick([0, 1]), pick(notes), 100];
    return { frame, data };
  });
}

/** MI.OUT as LSDj clocks it: a byte every 8 frames (one bit an audio frame), each flag-framing one protocol byte, with
 *  idle bytes between and frames split across bytes now and then. From absolute frame `from`, `count` bytes. */
function miOutStream(r: () => number, from: number, count: number): [number, number][] {
  const out: [number, number][] = [];
  const protocol = [0x7d, 0x7f, 0x7f, 0x70, 60, 0x71, 0, 0x74, 64, 0x7a, 7, 0x7e, 0x7c, 42, 0x40, 0x73, 72];
  let k = 0;
  for (let i = 0; i < count; i++) {
    const x = r();
    const byte = x < 0.15 ? 0x00 : x < 0.25 ? Math.floor(r() * 256) : 0x80 | protocol[k++ % protocol.length];
    out.push([from + i * 8, byte]);
  }
  return out;
}

/** Master sync as a playing LSDj clocks it: one byte a 24-PPQN tick, `gap` frames apart (860 is tempo 128 at
 *  44.1 kHz), each LSDj's song row. */
const masterSyncRun = (from: number, count: number, gap: number, row: number): [number, number][] =>
  Array.from({ length: count }, (_, i): [number, number] => [from + i * gap, row]);

/** Sixteen rows, each one one-phrase chain: every row has content and lasts 96 ticks. */
const table = (rows = 16) =>
  songRowTicks(SongSchema.parse({ formatVersion: 22, rows: Array.from({ length: rows }, () => ({ chains: [0, 0, 0, 0] })), chains: [{ phrases: [0] }] }));

/** A Launchpad press (or release) of grid pad (x, y), or of an edge button by CC. */
const padPress = (x: number, y: number, down = true): In => ({ frame: 0, data: down ? [0x90, padIndex({ x, y }), 127] : [0x80, padIndex({ x, y }), 0] });
const button = (cc: number, down = true): In => ({ frame: 0, data: [0xb0, cc, down ? 127 : 0] });
const UP = 80;
const DOWN = 70;
const SESSION = 93;

const lsdjOf = (mode: string, extra: Record<string, unknown> = {}): Role => ({ kind: "lsdj-sync", config: { mode, ...extra } });

// ---- the cases --------------------------------------------------------------------------------------------------------

test("routing, in all four modes over 1 to 3 systems, each kind of MIDI reader behind it, in ragged blocks", () => {
  const r = rng(1);
  for (const mode of MIDI_ROUTING_VALUES) {
    for (let n = 1; n <= 3; n++) {
      const all: SystemSpec[] = [
        { id: 11, roles: [{ kind: "mgb" }], gameBoy: true },
        { id: 0x80000001, roles: [{ kind: "nes-n8-midi" }] },
        { id: 3, roles: [lsdjOf("midiPassthrough")], gameBoy: true },
      ];
      const project: Project = { routing: mode, systems: all.slice(0, n) };
      const s = new Script();
      s.blocks(60, [1024, ...FL_RAGGED, 64], (frames) => ({ midiIn: randomMidi(r, frames, Math.floor(r() * 6)) }));
      const total = equivalent(`${mode}, ${n} systems`, project, s.steps);
      expect((total.serialIn[11] ?? []).length > 0, `${mode} ${n}: mGB received MIDI`).toBe(true);
    }
  }
});

test("every LSDj mode, through transport starts, stops, seeks, loops and tempo changes, at 1, ragged and 1024 frames", () => {
  for (const mode of LSDJ_MODE_VALUES) {
    for (const [label, sizes] of [["1-frame", 1], ["ragged", FL_RAGGED], ["FL buffer", FL_BUFFER], ["1024", 1024], ["8192", 8192]] as const) {
      const r = rng(mode.length * 31 + label.length);
      const project: Project = {
        routing: "sendToAll",
        systems: [{ id: 5, roles: [lsdjOf(mode, { tempoDivisor: mode === "midiMap" ? 2 : 1, autoStart: true })], gameBoy: true }],
      };
      const s = new Script();
      const count = sizes === 1 ? 3000 : sizes === 8192 ? 30 : 120;
      const each = (frames: number) => ({ midiIn: r() < 0.5 ? randomMidi(r, frames, 1 + Math.floor(r() * 4)) : [] });
      // MI.OUT or master sync bytes over the whole run, whichever the mode reads (the other modes ignore them).
      const span = (typeof sizes === "number" ? sizes : sizes.reduce((a, b) => a + b, 0) / sizes.length) * count;
      if (mode === "masterSync") {
        s.serialOut(5, [...masterSyncRun(300, Math.floor(span / 4 / 860), 860, 9), ...masterSyncRun(Math.floor(span / 2), Math.floor(span / 4 / 700), 700, 3)]);
      } else {
        s.serialOut(5, miOutStream(r, 100, Math.floor(span / 8) - 20));
      }
      const quarter = Math.max(1, Math.floor(count / 8));
      // 1-frame blocks at a tempo that puts a clock every 240 frames or so, so a short run still holds a few dozen.
      if (sizes === 1) s.tempo = 460;
      s.blocks(quarter, sizes, each); // stopped
      s.playing = true;
      s.blocks(quarter, sizes, each); // started
      s.tempo = sizes === 1 ? 397.3 : 97.3;
      s.blocks(quarter, sizes, each); // a tempo change
      s.ppq = 64; // a seek while playing
      s.blocks(quarter, sizes, each);
      s.playing = false;
      s.blocks(quarter, sizes, each); // stopped
      s.ppq = 3.5; // a seek while stopped
      s.playing = true;
      s.tempo = sizes === 1 ? 574 : 174;
      s.blocks(quarter, sizes, each); // restarted somewhere else
      s.ppq = 3.5; // a loop back to where it started
      s.blocks(quarter, sizes, each);
      s.playing = false;
      s.blocks(quarter, sizes, each);
      equivalent(`${mode}, ${label}`, project, s.steps);
    }
  }
});

test("master sync at 8, 48 and 96 kHz: a stop at exactly 200 ms and one frame past it, and runs closing on their own", () => {
  const cases: [number, (number | number[])[]][] = [
    [8000, [1, FL_RAGGED, 4096]],
    [48000, [FL_RAGGED, 4096]],
    [96000, [FL_BUFFER, 4096]],
  ];
  for (const [rate, splits] of cases) {
    for (const sizes of splits) {
      const label = `${rate} Hz, ${typeof sizes === "number" ? sizes : sizes.length === FL_RAGGED.length ? "ragged" : "FL buffer"}`;
      const project: Project = { routing: "sendToAll", systems: [{ id: 2, roles: [lsdjOf("masterSync")], gameBoy: true }] };
      const s = new Script();
      s.sampleRate = rate;
      // A run whose second byte is exactly 200 ms after its first (no stop) and whose third is a frame more after the
      // second (a stop, and a new run); a silence; a run of clocks; a stop before a lone byte; and its silence.
      const stop = Math.round(rate * 0.2);
      const gap = Math.round(stop / 10);
      const second = 100 + 4 * stop;
      const end = second + 39 * gap;
      s.serialOut(2, [[100, 1], [100 + stop, 1], [100 + 2 * stop + 1, 2], ...masterSyncRun(second, 40, gap, 4), [end + stop + 1000, 7]]);
      s.playing = true;
      const total = end + 3 * stop + 9000;
      const mean = typeof sizes === "number" ? sizes : sizes.reduce((a, b) => a + b, 0) / sizes.length;
      s.blocks(Math.ceil(total / mean), sizes);
      const out = equivalent(`master sync, ${label}`, project, s.steps);
      expect(out.midiOut.filter((m) => m[2] === 0xfc).length, `master sync, ${label}: stops`).toBe(4);
      expect(out.midiOut.filter((m) => m[2] === 0xfa).length, `master sync, ${label}: starts`).toBe(4);
    }
  }
});

test("a controller session: launches into its system's MI.MAP, LEDs, connects and disconnects, presses at block edges", () => {
  for (const [label, sizes] of [["ragged", FL_RAGGED], ["512", 512], ["1-frame", 1]] as const) {
    const r = rng(label.length * 7);
    const project: Project = {
      routing: "sendToAll",
      systems: [
        { id: 4, roles: [lsdjOf("midiMap")], gameBoy: true },
        { id: 9, roles: [{ kind: "mgb" }], gameBoy: true },
      ],
      controller: { target: "system", systemId: 4, appConfig: { quantise: "immediate" }, songRowTicks: table() },
    };
    const s = new Script();
    const count = sizes === 1 ? 2500 : 200;
    const each = (frames: number, i: number) => {
      const presses: In[] = [];
      if (r() < 0.08) presses.push(padPress(Math.floor(r() * 8), Math.floor(r() * 8)));
      if (r() < 0.05) presses.push(padPress(Math.floor(r() * 8), Math.floor(r() * 8), false));
      if (r() < 0.02) presses.push(button(pick3(r), r() < 0.7));
      if (i % 97 === 50) s.connected = { start: 0, changes: [{ frame: 0, value: 1 }] }; // attached from frame 0
      else if (i % 97 === 80) s.connected = { start: 1, changes: [{ frame: 5, value: 0 }] }; // detached later: still attached
      else if (i % 97 === 90) s.connected = { start: 0, changes: [] };
      else if (i % 97 === 60) s.connected = { start: 1, changes: [] };
      return { controllerIn: presses, midiIn: r() < 0.2 ? randomMidi(r, frames, 2) : [] };
    };
    s.blocks(Math.floor(count / 4), sizes, each);
    s.playing = true;
    s.blocks(Math.floor(count / 2), sizes, each);
    s.playing = false;
    s.blocks(Math.floor(count / 4), sizes, each);
    const total = equivalent(`controller to a system, ${label}`, project, s.steps);
    expect(total.controllerOut.length > 0, `${label}: LEDs`).toBe(true);
    expect((total.serialIn[4] ?? []).some(([, b]) => b !== 0xff && b !== 0xfe), `${label}: a launch reached the cart`).toBe(true);
  }
});

function pick3(r: () => number): number {
  return [UP, DOWN, SESSION][Math.floor(r() * 3)];
}

test("a controller session to MIDI out, beside a Game Boy's MI.OUT: host MIDI out in the kernel's order", () => {
  const r = rng(77);
  const project: Project = {
    routing: "oneChannelPerInstance",
    systems: [
      { id: 21, roles: [lsdjOf("midiOut")], gameBoy: true },
      { id: 22, roles: [lsdjOf("masterSync")], gameBoy: true },
    ],
    controller: { target: "midiOut", systemId: 22, appConfig: { quantise: "beat" }, songRowTicks: table() },
  };
  const s = new Script();
  s.serialOut(21, miOutStream(r, 0, 20000));
  s.serialOut(22, [...masterSyncRun(500, 100, 860, 6), ...masterSyncRun(120000, 50, 900, 2)]);
  s.connected = { start: 1, changes: [] };
  s.playing = true;
  s.blocks(300, FL_BUFFER, () => ({ controllerIn: r() < 0.1 ? [padPress(Math.floor(r() * 8), Math.floor(r() * 8))] : [] }));
  const total = equivalent("controller to MIDI out", project, s.steps);
  // The controller's launches went out under its system's id, as the kernel tagged them.
  expect(total.midiOut.some((m) => m[0] === 22 && (m[2] & 0xf0) === 0x90 && m.length === 5)).toBe(true);
  expect(total.midiOut.some((m) => m[0] === 21)).toBe(true);
});

test("risa and smsggdj sync, and NES MIDI, through starts, stops, seeks and loops, in every block size", () => {
  for (const [label, sizes] of [["1-frame", 1], ["ragged", FL_RAGGED], ["FL buffer", FL_BUFFER], ["1024", 1024], ["8192", 8192]] as const) {
    const r = rng(label.length * 13);
    const project: Project = {
      routing: "fourChannelsPerInstance",
      systems: [
        { id: 1, roles: [{ kind: "nes-n8-midi" }, { kind: "risa-sync" }] },
        { id: 2, roles: [{ kind: "sms-sync", config: { machine: "sms" } }] },
        { id: 3, roles: [{ kind: "sms-sync", config: { machine: "gg" } }] },
      ],
    };
    const s = new Script();
    const count = sizes === 1 ? 3000 : sizes === 8192 ? 24 : 100;
    const each = (frames: number) => ({ midiIn: r() < 0.4 ? randomMidi(r, frames, 3) : [] });
    const step = Math.max(1, Math.floor(count / 6));
    s.blocks(step, sizes, each);
    s.playing = true;
    s.blocks(step, sizes, each);
    s.ppq = 5;
    s.tempo = 140;
    s.blocks(step, sizes, each);
    s.ppq = 5;
    s.blocks(step, sizes, each);
    s.playing = false;
    s.blocks(step, sizes, each);
    s.playing = true;
    s.blocks(step, sizes, each);
    const total = equivalent(`risa / sms / NES, ${label}`, project, s.steps);
    expect((total.coreBytes[1] ?? []).some((m) => m[1] === 1), `${label}: risa armed`).toBe(true);
    expect((total.coreBytes[3] ?? []).length > 0, `${label}: gg clocked`).toBe(true);
  }
});

test("settings changes mid-run: modes, divisors and auto-arm, the routing, machines, systems added and removed", () => {
  const r = rng(5150);
  const lsdj = (mode: string, extra: Record<string, unknown> = {}): SystemSpec => ({ id: 7, roles: [lsdjOf(mode, extra)], gameBoy: true });
  const mgb: SystemSpec = { id: 8, roles: [{ kind: "mgb" }], gameBoy: true };
  const sms: SystemSpec = { id: 9, roles: [{ kind: "sms-sync", config: { machine: "sms" } }] };
  const gg: SystemSpec = { id: 9, roles: [{ kind: "sms-sync", config: { machine: "gg" } }] };
  const at = (routing: MidiRouting, systems: SystemSpec[]): Project => ({ routing, systems });
  const edits: Project[] = [
    at("sendToAll", [lsdj("midiSync", { autoStart: true }), mgb, sms]),
    at("sendToAll", [lsdj("midiSync", { tempoDivisor: 4 }), mgb, sms]),
    at("oneChannelPerInstance", [lsdj("midiSyncArduinoboy", { autoStart: true }), mgb, sms]),
    at("oneChannelPerInstance", [lsdj("midiSyncArduinoboy", { tempoDivisor: 8 }), mgb, sms]),
    at("midiChannelToInstance", [lsdj("masterSync"), mgb, sms]),
    at("midiChannelToInstance", [lsdj("midiMap"), sms, mgb]), // reordered
    at("fourChannelsPerInstance", [lsdj("masterSync"), gg, mgb]), // back to master sync: a fresh run; a new machine
    at("fourChannelsPerInstance", [lsdj("keyboardMidi"), gg]), // mGB removed
    at("sendToAll", [lsdj("midiOut"), gg, mgb]), // and back, fresh
    at("sendToAll", [lsdj("masterSync"), sms, mgb]),
    at("sendToAll", [lsdj("off"), sms, mgb]),
    at("sendToAll", [mgb]), // the LSDj and SMS systems removed
    at("sendToAll", [mgb, lsdj("masterSync")]), // LSDj back, fresh, in another slot
    at("sendToAll", [mgb, lsdj("midiPassthrough"), gg]),
  ];
  for (const [label, sizes] of [["ragged", FL_RAGGED], ["1024", 1024], ["1-frame", 1]] as const) {
    const s = new Script();
    s.serialOut(7, [...masterSyncRun(200, 150, 860, 5), ...masterSyncRun(160000, 200, 860, 6)]);
    s.playing = true;
    const per = sizes === 1 ? 150 : 25;
    edits.forEach((project, k) => {
      s.blocks(per, sizes, (frames, i) => ({
        midiIn: randomMidi(r, frames, r() < 0.4 ? 2 : 0),
        ...(i === 0 && k > 0 ? { project } : {}),
      }));
      if (k === 6) s.ppq = 12; // a seek among the edits
      if (k === 9) s.tempo = 133;
    });
    equivalent(`edits, ${label}`, edits[0], s.steps);
  }
});

test("settings changes that keep a mode's state: the Arduinoboy's play flag and divisor, an auto-arm turned on while playing", () => {
  const sys = (mode: string, extra: Record<string, unknown> = {}): Project => ({
    routing: "sendToAll",
    systems: [{ id: 6, roles: [lsdjOf(mode, extra)], gameBoy: true }],
  });
  for (const [label, sizes] of [["ragged", FL_RAGGED], ["1024", 1024]] as const) {
    const s = new Script();
    s.playing = true;
    const note = (n: number): In[] => [{ frame: 0, data: [0x90, n, 100] }];
    // Arduinoboy: played (note 24) at divisor 4 (note 28); the configured divisor moves twice, and neither edit may
    // reset the divisor the notes set, nor the play flag.
    s.block(256, { midiIn: [...note(24), ...note(28)] });
    s.blocks(20, sizes);
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSyncArduinoboy", { tempoDivisor: 8 }) } : {}));
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSyncArduinoboy", { tempoDivisor: 2, autoStart: true }) } : {}));
    // MidiSync, its auto-arm off, then on while the transport plays: the arm reads the edge it last saw with it on.
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSync") } : {}));
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSync", { autoStart: true }) } : {}));
    s.playing = false;
    s.blocks(5, sizes);
    s.playing = true;
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSync", { autoStart: false }) } : {}));
    s.playing = false;
    s.blocks(5, sizes);
    s.blocks(20, sizes, (_f, i) => (i === 0 ? { project: sys("midiSync", { autoStart: true }) } : {}));
    s.playing = true;
    s.blocks(20, sizes);
    const total = equivalent(`mode state, ${label}`, sys("midiSyncArduinoboy", { tempoDivisor: 1 }), s.steps);
    expect((total.buttons[6] ?? []).length > 0, `${label}: an arm pressed START`).toBe(true);
  }
});

test("controller settings changes: a new song table in place, an anchor, an app knob, a new target, removed and back", () => {
  const systems: SystemSpec[] = [
    { id: 31, roles: [lsdjOf("midiMap")], gameBoy: true },
    { id: 32, roles: [lsdjOf("midiMap")], gameBoy: true },
  ];
  const ctl = (over: Partial<ControllerSpec>): Project => ({
    routing: "sendToAll",
    systems,
    controller: { target: "system", systemId: 31, appConfig: { quantise: "bar" }, songRowTicks: table(), ...over },
  });
  const edits: Project[] = [
    ctl({}),
    ctl({ songRowTicks: table(12) }), // the song edited on the cart: adopted in place
    ctl({ songRowTicks: table(12), anchorRows: [3, 3, null, 3], anchorSeq: 1 }), // the cart started on its own
    ctl({ songRowTicks: table(14), anchorRows: [3, 3, null, 3], anchorSeq: 1 }), // the same anchor again: once
    ctl({ songRowTicks: table(14), appConfig: { quantise: "immediate", follow: false } }), // a knob: rebuilt
    ctl({ songRowTicks: table(14), appConfig: { quantise: "immediate", follow: false }, systemId: 32 }), // a new target
    ctl({ songRowTicks: table(14), appConfig: { quantise: "rowEnd" }, systemId: 32, target: "midiOut" }),
    { routing: "sendToAll", systems }, // the controller turned off
    ctl({ systemId: 0 }), // and on again, fresh, launching to the first system
    ctl({ systemId: 0, app: "does-not-exist" }),
    ctl({ systemId: 0, anchorRows: [1, 2, 3, 4], anchorSeq: 2 }),
  ];
  for (const [label, sizes] of [["ragged", FL_RAGGED], ["1024", 1024], ["1-frame", 1]] as const) {
    const r = rng(label.length * 3 + 1);
    const s = new Script();
    s.connected = { start: 1, changes: [] };
    s.playing = true;
    const per = sizes === 1 ? 300 : 30;
    if (sizes === 1) s.tempo = 600;
    edits.forEach((project, k) => {
      s.blocks(per, sizes, (frames, i) => ({
        controllerIn: r() < (sizes === 1 ? 0.01 : 0.15) ? [padPress(Math.floor(r() * 8), Math.floor(r() * 8)), ...(r() < 0.3 ? [button(pick3(r))] : [])] : [],
        midiIn: r() < 0.1 ? randomMidi(r, frames, 1) : [],
        ...(i === 0 && k > 0 ? { project } : {}),
      }));
      if (k === 3) {
        s.playing = false;
        s.block(64);
        s.playing = true;
      }
      if (k === 5) s.connected = { start: 0, changes: [] };
      if (k === 6) s.connected = { start: 0, changes: [{ frame: 0, value: 1 }] };
    });
    const total = equivalent(`controller edits, ${label}`, edits[0], s.steps);
    expect(total.controllerOut.length > 0).toBe(true);
  }
});

test("a 1-frame stretch inside a run of long blocks keeps every clock, row and bookend in step", () => {
  const r = rng(99);
  const project: Project = {
    routing: "sendToAll",
    systems: [
      { id: 1, roles: [lsdjOf("midiSyncArduinoboy", { autoStart: true })], gameBoy: true },
      { id: 2, roles: [{ kind: "risa-sync" }, { kind: "nes-n8-midi" }] },
      { id: 3, roles: [lsdjOf("midiMap", { tempoDivisor: 3 })], gameBoy: true },
    ],
  };
  const s = new Script();
  s.playing = true;
  const notes = (frames: number) => ({ midiIn: r() < 0.3 ? [{ frame: frames - 1, data: [0x90, 24, 100] }, ...randomMidi(r, frames, 2)].sort((a, b) => a.frame - b.frame) : [] });
  s.blocks(20, 4096, notes);
  s.blocks(4000, 1, notes);
  s.blocks(20, 4096, notes);
  s.ppq = 0;
  s.blocks(2000, [1, 2, 3], notes);
  equivalent("1-frame stretch", project, s.steps);
});
