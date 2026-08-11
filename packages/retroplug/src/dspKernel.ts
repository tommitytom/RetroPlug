// The DSP-thread role kernel — pure TS, no backend. The host hands `processBlock` everything it
// collected for one audio block; project-scope behaviours (routing) fan the input to systems, then
// each system's pipeline of translators/sources reads its inputs and writes frame-tagged sinks.
// This is doc-06's "translators are scripts" model expressed as plain TS behaviours over a
// per-system context, so it runs and is fully testable under the mock (txiki) suite with ZERO C++.
// How the audio thread eventually executes this (a bare QuickJS context) is a separate, later
// concern that changes none of this code.

import type { MidiEvent } from "./midiRouting";
import type { RoleInstance, RoleRegistry } from "./systemRoles";

/** Per-block transport/timing context (mirrors native AudioBlockInfo / DspBlockInfo). */
export interface BlockInfo {
  frames: number;
  sampleRate: number;
  tempo: number;
  ppqStart: number; // PPQ position at block start
  transport: boolean;
}

/** A UI-mapped Game Boy button transition, targeted at a system. */
export interface ButtonEvent {
  system: number;
  frame: number;
  button: number;
  down: boolean;
}

/** A raw keyboard transition, targeted at a system (LSDj keyboard mode / future keyboard cores). */
export interface KeyEvent {
  system: number;
  frame: number;
  key: number;
  down: boolean;
}

/** One raw serial-out byte a core emitted on its serial port (LSDj MI.OUT), targeted at a system.
 *  Native gathers these AFTER rendering a block and feeds them to the NEXT block (one-block latency);
 *  the kernel fans them per-system into `ctx.serialOut` for the mode-7 decoder. */
export interface SerialOutEvent {
  system: number;
  byte: number;
}

/** The rarely-changing structure of a project: the project-scope pipeline (e.g. midi-routing) and
 *  each system's ordered DSP-thread pipeline. Pushed ONCE via `setSystems` (not re-marshalled per
 *  block). The system ORDER is authoritative — positional MIDI routing (midiRouting.ts) maps a MIDI
 *  channel to a system by its index here, so this list defines both the set and the order. */
export interface KernelStructure {
  project?: RoleInstance[];
  systems: { id: number; pipeline: RoleInstance[] }[];
}

/** The dynamic per-block input: transport/timing + the events the host collected this block.
 *  `midiIn` is GLOBAL (a project-scope routing behaviour fans it to systems); `buttons`/`keys`
 *  already carry a target system. This is all `processBlock` marshals each block. */
export interface BlockInput extends BlockInfo {
  midiIn: MidiEvent[];
  buttons: ButtonEvent[];
  keys: KeyEvent[];
  serialOut: SerialOutEvent[];
  /** Traffic from a CONTROL SURFACE (a Launchpad), deliberately separate from `midiIn`.
   *
   *  Not tidiness: a pad press is a NoteOn, and a translator like `midiMap` reads a NoteOn as a row
   *  launch - so a surface sharing the musical stream would fire every launch twice, once through the
   *  controller app's quantiser and once raw. Optional because only a host with a device link bound
   *  supplies it; absent reads as the kernel's stable empty array. */
  controllerIn?: MidiEvent[];
}

/** The full block view a behaviour sees: the dynamic input merged onto the stored structure. The
 *  kernel keeps ONE of these and overwrites its dynamic fields each block (no per-block alloc). */
export interface Block extends BlockInput {
  systems: { id: number; pipeline: RoleInstance[] }[];
  project?: RoleInstance[];
}

/** The collected output the host applies to the emulators / host after the block. */
export interface Sinks {
  serialIn: { system: number; frame: number; byte: number }[];
  midiOut: { system: number; frame: number; data: number[] }[];
  coreMidi: { system: number; frame: number; data: number[] }[];
  coreBytes: { system: number; frame: number; data: number[]; flush?: boolean }[];
  buttons: { system: number; frame: number; button: number; down: boolean }[];
  /** Raw messages bound for a control surface. Not system-addressed - the device is a project-scope
   *  peripheral, not something a system owns. */
  controllerOut: number[][];
}

/** Where a behaviour's sinks go. The kernel forwards each ctx sink call to the injected target,
 *  scoped by system id. In tests/txiki the default `CollectingSink` gathers them into a `Sinks`
 *  object the assertions read; natively the target's methods ARE the bound C thunks, so bytes cross
 *  as scalars with no intermediate JS arrays. `reset` (optional) clears per-block state. */
export interface SinkTarget {
  pushSerialIn(system: number, frame: number, byte: number): void;
  emitMidiOut(system: number, frame: number, data: number[]): void;
  emitCoreMidi(system: number, frame: number, data: number[]): void;
  pushCoreBytes(system: number, frame: number, data: number[], flush?: boolean): void;
  pressButton(system: number, frame: number, button: number, down: boolean): void;
  /** Bytes back to a control surface (Launchpad LEDs). Optional: a host with no device link bound
   *  simply doesn't implement it, and the kernel drops the write rather than requiring every host to
   *  carry a stub. */
  emitControllerOut?(data: number[]): void;
  reset?(): void;
}

/** The default `SinkTarget`: gathers sink calls into a `Sinks` object (what `processBlock` returns
 *  and the pure-TS tests assert on). */
export class CollectingSink implements SinkTarget {
  readonly sinks: Sinks = emptySinks();
  reset(): void {
    this.sinks.serialIn.length = 0;
    this.sinks.midiOut.length = 0;
    this.sinks.coreMidi.length = 0;
    this.sinks.coreBytes.length = 0;
    this.sinks.buttons.length = 0;
    this.sinks.controllerOut.length = 0;
  }
  pushSerialIn(system: number, frame: number, byte: number): void {
    this.sinks.serialIn.push({ system, frame, byte });
  }
  emitMidiOut(system: number, frame: number, data: number[]): void {
    this.sinks.midiOut.push({ system, frame, data });
  }
  emitCoreMidi(system: number, frame: number, data: number[]): void {
    this.sinks.coreMidi.push({ system, frame, data });
  }
  pushCoreBytes(system: number, frame: number, data: number[], flush?: boolean): void {
    this.sinks.coreBytes.push(flush ? { system, frame, data, flush } : { system, frame, data });
  }
  pressButton(system: number, frame: number, button: number, down: boolean): void {
    this.sinks.buttons.push({ system, frame, button, down });
  }
  emitControllerOut(data: number[]): void {
    this.sinks.controllerOut.push(data);
  }
}

/** Per-system context handed to a system-scope behaviour: its inputs + sinks, scoped to the
 *  system so role code never carries a `sys` argument. `midi` is mutable — a future transform
 *  stage may rewrite it before a downstream translator reads it (the pipeline is ordered).
 *  `state` is a mutable scratch bag the kernel persists across blocks, scoped to this system +
 *  pipeline stage — where a stateful translator (LSDj Arduinoboy play flag, MidiMap last row,
 *  keyboard octave, transport edge) keeps what it needs between blocks. */
export interface SystemCtx {
  config: Record<string, unknown>;
  midi: MidiEvent[];
  keys: KeyEvent[];
  buttons: ButtonEvent[];
  /** The raw serial-out bytes this system emitted LAST block (LSDj MI.OUT), in order — the mode-7
   *  Arduinoboy decoder's input. Empty unless native has serial-out capture armed for this system. */
  serialOut: number[];
  block: BlockInfo;
  state: Record<string, unknown>;
  pushSerialIn(frame: number, byte: number): void;
  emitMidiOut(frame: number, data: number[]): void;
  emitCoreMidi(frame: number, data: number[]): void;
  /** Push raw bytes into the core's device byte-input (the NES N8 FIFO), no framing/length cap —
   *  for a byte protocol carried over the transport (a tracker's host-sync locate/clock stream).
   *  `flush` drops every byte the FIFO still holds — both not-yet-delivered and delivered-but-unread —
   *  before these are enqueued, so a re-locate isn't preceded by clocks queued for the old position. */
  pushCoreBytes(frame: number, data: number[], flush?: boolean): void;
  pressButton(button: number, down: boolean): void;
  eachTick(resolution: number, cb: (tick: number, off: number) => void): void;
  /** Force the tick this system's next `eachTick` starts from, overriding the drift-free counter it
   *  normally carries across blocks. For a role that has just repositioned the core itself (risa's
   *  host-sync arm names the clock it locates to), so the clock stream resumes from a known tick rather
   *  than wherever the previous block left off. `eachTick`'s own resync only triggers on a jump of more
   *  than a tick, so an exact loop back to the same position would otherwise emit no clocks at all. */
  setNextTick(tick: number): void;
}

export type SystemBehavior = (ctx: SystemCtx) => void;

/** The context a project-scope behaviour runs against — the whole-project twin of `SystemCtx`, and
 *  built the same way: once per `setSystems`, pointing at things the kernel mutates in place, so a
 *  steady-state block allocates nothing here either.
 *
 *  Routing (the original project behaviour) needs only `block` + `inboxes` + `config`. The rest exists
 *  because a project-scope behaviour that OWNS something — a control surface, a device session — needs
 *  state across blocks and somewhere to send bytes, and had neither. */
export interface ProjectCtx {
  block: Block;
  /** Positional — `inboxes[i]` is the pre-cleared persistent inbox for `block.systems[i]`, in the same
   *  order — which a behaviour fills IN PLACE (the kernel then hands each system its inbox as
   *  `ctx.midi`). No per-block Map: the kernel owns the arrays. */
  inboxes: MidiEvent[][];
  config: Record<string, unknown>;
  /** Persistent scratch across blocks, same contract as `SystemCtx.state` (and pruned the same way). */
  state: Record<string, unknown>;
  /** This block's control-surface traffic. Empty unless a host bound a device link. */
  controllerIn: MidiEvent[];
  /** Send bytes to the control surface. Silently dropped when no host sink is bound. */
  emitControllerOut(data: number[]): void;
  /** Deliver one MIDI event into a system's inbox BY ID, so a behaviour can address a system without
   *  knowing its index. Ignored for an unknown id. Project stages run before every system pipeline, so
   *  an event pushed here is read by that system's translators in the SAME block. */
  toSystem(systemId: number, ev: MidiEvent): void;
  /** Host MIDI out (to a DAW, or to hardware like an Arduinoboy). */
  emitMidiOut(systemId: number, frame: number, data: number[]): void;
}

export type ProjectBehavior = (ctx: ProjectCtx) => void;

/** Per-role runtime-tracing hooks (spec/08-profiling.md Tier B), injected into the kernel only under a
 *  profile host (the bundle builds one iff the native `spanBegin` thunk is bound). `begin`/`end` bracket a
 *  stage's wall-time span (label = interned role id); `name` registers a label id → role kind once. */
export interface DspTracer {
  name(label: number, kind: string): void;
  begin(label: number): void;
  end(): void;
}

export function emptySinks(): Sinks {
  return { serialIn: [], midiOut: [], coreMidi: [], coreBytes: [], buttons: [], controllerOut: [] };
}

/** Stood in for an absent `BlockInput.controllerIn`. Shared and never written to, so the common case
 *  (no device link) costs no allocation. */
const NO_CONTROLLER_IN: MidiEvent[] = [];

// Walk the PPQ ticks that fall in this block at `resolution` ticks/quarter, calling cb(tick, off)
// for each; returns the advanced `nextTick`. A faithful TS twin of native PpqUtil::eachTick
// (packages/native/src/util/PpqUtil.hpp): the caller-owned `nextTick` persists across blocks so the
// clock is drift-free at block edges (each tick fires exactly once, no double/miss); a >1-tick
// transport jump (seek/loop/start) resyncs.
export function walkTicks(
  block: BlockInfo,
  resolution: number,
  nextTick: number,
  cb: (tick: number, off: number) => void,
): number {
  if (!block.transport || block.frames === 0 || resolution === 0 || block.tempo <= 0) return nextTick;

  const beatLenSamples = (block.sampleRate * 60) / block.tempo;
  const beatLenSamplesRes = beatLenSamples / resolution;
  const ppqRes = block.ppqStart * resolution;
  const framePpqLen = (block.frames / beatLenSamples) * resolution;
  const framePpqEnd = ppqRes + framePpqLen;

  if (nextTick < ppqRes - 1 || nextTick > framePpqEnd + 1) nextTick = Math.ceil(ppqRes);

  while (nextTick < framePpqEnd) {
    let offset = beatLenSamplesRes * (nextTick - ppqRes);
    if (offset < 0) offset = 0;
    if (offset >= block.frames) offset = block.frames - 1;
    cb(nextTick, Math.trunc(offset)); // native casts the offset to uint32 (truncates toward zero)
    nextTick++;
  }
  return nextTick;
}

/** One resolved system-scope pipeline stage: its behaviour + the PERSISTENT ctx it runs against
 *  (built once in `setSystems`; `processBlock` only mutates what the ctx's fields point at). */
interface StageRun {
  dsp: SystemBehavior;
  ctx: SystemCtx;
  label: number; // interned role-kind id for tracing (0 when no tracer)
}

/** Everything the kernel holds per live system, built once per `setSystems`. `inbox`/`keys`/`buttons`
 *  are reused every block (cleared, not reallocated); the stage ctxs point straight at them. */
interface SystemSlot {
  id: number;
  inbox: MidiEvent[];
  keys: KeyEvent[];
  buttons: ButtonEvent[];
  serialOut: number[];
  stages: StageRun[];
}

/** A resolved project-scope stage (e.g. routing): its behaviour + the PERSISTENT ctx it runs against
 *  (built once in `setSystems`, exactly like a system stage's). */
interface ProjectStage {
  dsp: ProjectBehavior;
  ctx: ProjectCtx;
  label: number; // interned role-kind id for tracing (0 when no tracer)
}

/** Runs a project's DSP-thread behaviours per block. Structure (systems + pipelines) is pushed once
 *  via `setSystems`; each block passes only dynamic input. Holds per-system `nextTick` state so
 *  `eachTick` stays drift-free across successive `processBlock` calls, and forwards every sink to an
 *  injected `SinkTarget` (default: a `CollectingSink` whose `Sinks` `processBlock` returns). One
 *  instance per project; a fresh one per unit test.
 *
 *  Per-block allocation is designed OUT: the per-system ctx + its sink closures, the routed inboxes,
 *  and the per-system key/button lists are all built once in `setSystems` and reused (mutated in
 *  place) every block, so a steady-state block allocates nothing on the kernel side. */
export class DspKernel {
  private readonly tick = new Map<number, number>();
  // Persistent per-system, per-stage scratch bags (system id → stage index → bag). Backs ctx.state
  // so a stateful behaviour keeps its cross-block state; pruned alongside `tick` in setSystems.
  private readonly state = new Map<number, Map<number, Record<string, unknown>>>();
  // The same, for PROJECT-scope stages (stage index → bag). Keyed by index alone since a project stage
  // has no system id; kept across setSystems so a role that owns something (a controller session) is not
  // torn down every time a system is added or a knob changes.
  private readonly projectState = new Map<number, Record<string, unknown>>();
  // Persistent per-system structure, rebuilt on each setSystems. `inboxes[i] === slots[i].inbox`
  // (positional, parallel to block.systems) — the array handed to project-scope routing.
  private slots: SystemSlot[] = [];
  private readonly slotById = new Map<number, SystemSlot>();
  private readonly inboxes: MidiEvent[][] = [];
  private projectStages: ProjectStage[] = [];
  // Per-role runtime tracing (spec/08-profiling.md Tier B). `tracer` exists only under a profile host;
  // `traceOn` is flipped by the host (__setTrace → setTracing) so per-stage span calls happen ONLY while
  // armed — the non-traced path stays byte-identical (deterministic alloc counts). Kinds are interned to
  // stable ids from ROLE_BASE (16; native pipeline stages own 0..3).
  private traceOn = false;
  private readonly traceLabels = new Map<string, number>();
  private nextLabel = 16;
  // The single persistent block view: setSystems writes its structure, processBlock its dynamics.
  private readonly block: Block = {
    frames: 0,
    sampleRate: 44100,
    tempo: 120,
    ppqStart: 0,
    transport: false,
    midiIn: [],
    buttons: [],
    keys: [],
    serialOut: [],
    systems: [],
    project: [],
  };
  // Returned from processBlock: the collecting sink's Sinks, else a stable empty (native ignores it).
  private readonly result: Sinks;

  constructor(
    private readonly registry: RoleRegistry,
    private readonly sink: SinkTarget = new CollectingSink(),
    private readonly tracer?: DspTracer,
  ) {
    this.result = sink instanceof CollectingSink ? sink.sinks : emptySinks();
  }

  /** Host hook (profile build only): arm/disarm per-role span emission for a trace window. */
  setTracing(on: boolean): void {
    this.traceOn = on;
  }

  // Intern a role kind → a stable label id (registering the name with the tracer on first sight). Only
  // called when a tracer exists, so the mock/production path never touches it.
  private internLabel(kind: string): number {
    let id = this.traceLabels.get(kind);
    if (id === undefined) {
      id = this.nextLabel++;
      this.traceLabels.set(kind, id);
      this.tracer?.name(id, kind);
    }
    return id;
  }

  /** Push the (rarely-changing) system + pipeline structure. The stored ORDER is authoritative for
   *  positional routing. Also prunes per-system tick state for ids no longer present, so a
   *  removed-then-readded id starts a fresh clock instead of resuming mid-count. */
  setSystems(struct: KernelStructure): void {
    this.block.systems = struct.systems;
    this.block.project = struct.project ?? [];
    // Prune dead-id tick/state FIRST so a removed-then-re-added id gets a fresh scratch bag (a
    // survived id keeps its state across a config change). buildCtx below then binds the surviving bag.
    const live = new Set(struct.systems.map((s) => s.id));
    for (const id of this.tick.keys()) if (!live.has(id)) this.tick.delete(id);
    for (const id of this.state.keys()) if (!live.has(id)) this.state.delete(id);

    // Rebuild the persistent structure: RoleInstance/config identity changes on every setSystems, so
    // the ctxs (which capture config/id) and the role resolution must be rebuilt each time.
    this.projectStages = [];
    for (const stage of this.block.project) {
      const rt = this.registry.roleType(stage.kind);
      if (rt?.scope === "project" && rt.dsp) {
        this.projectStages.push({
          dsp: rt.dsp as ProjectBehavior,
          ctx: this.buildProjectCtx(this.projectStages.length, stage.config),
          label: this.tracer ? this.internLabel(stage.kind) : 0,
        });
      }
    }
    // Drop scratch for stages that no longer exist, so a removed-then-re-added project role starts fresh
    // rather than resuming against a structure it no longer matches.
    for (const index of this.projectState.keys()) {
      if (index >= this.projectStages.length) this.projectState.delete(index);
    }
    this.slots = [];
    this.slotById.clear();
    // Cleared in place, never reassigned: a project ctx built above captures this array, and swapping it
    // for a new one would leave that ctx routing into inboxes nothing reads.
    this.inboxes.length = 0;
    for (const sys of struct.systems) {
      const slot: SystemSlot = { id: sys.id, inbox: [], keys: [], buttons: [], serialOut: [], stages: [] };
      sys.pipeline.forEach((stage, stageIndex) => {
        const rt = this.registry.roleType(stage.kind);
        if (!rt || rt.scope === "project" || !rt.dsp) return;
        slot.stages.push({
          dsp: rt.dsp as SystemBehavior,
          ctx: this.buildCtx(sys.id, stageIndex, stage.config, slot),
          label: this.tracer ? this.internLabel(stage.kind) : 0,
        });
      });
      this.slots.push(slot);
      this.slotById.set(sys.id, slot);
      this.inboxes.push(slot.inbox);
    }
  }

  /** Run one block over the stored structure, marshalling only the dynamic input. Returns the
   *  collecting sink's `Sinks` (meaningful only under a `CollectingSink`; a forwarding target has
   *  already sent everything and the return is ignored). */
  processBlock(dyn: BlockInput): Sinks {
    const b = this.block;
    b.frames = dyn.frames;
    b.sampleRate = dyn.sampleRate;
    b.tempo = dyn.tempo;
    b.ppqStart = dyn.ppqStart;
    b.transport = dyn.transport;
    b.midiIn = dyn.midiIn;
    b.buttons = dyn.buttons;
    b.keys = dyn.keys;
    b.serialOut = dyn.serialOut;

    this.sink.reset?.();

    // Clear the persistent per-system inboxes/keys/buttons (length=0 keeps capacity — no realloc as
    // they refill to the prior high-water). Indexed loops throughout: `for...of` on an array would
    // allocate an iterator per call, defeating the point.
    const slots = this.slots;
    for (let i = 0; i < slots.length; i++) {
      const slot = slots[i];
      slot.inbox.length = 0;
      slot.keys.length = 0;
      slot.buttons.length = 0;
      slot.serialOut.length = 0;
    }

    // Fan this block's keys/buttons to their target system (shared refs — read-only downstream),
    // one pass each; replaces the old per-system `.filter()` + predicate closures.
    const keys = b.keys;
    for (let i = 0; i < keys.length; i++) {
      const slot = this.slotById.get(keys[i].system);
      if (slot) slot.keys.push(keys[i]);
    }
    const buttons = b.buttons;
    for (let i = 0; i < buttons.length; i++) {
      const slot = this.slotById.get(buttons[i].system);
      if (slot) slot.buttons.push(buttons[i]);
    }
    const serialOut = b.serialOut;
    for (let i = 0; i < serialOut.length; i++) {
      const slot = this.slotById.get(serialOut[i].system);
      if (slot) slot.serialOut.push(serialOut[i].byte);
    }

    // Project scope: routing fans the global midiIn into the positional persistent inboxes. The
    // `if (this.traceOn)` guard is false on the non-traced path (production/mock/alloc window) → the hot
    // path is byte-identical; only a trace window pays the span-thunk crossings (spec/08-profiling.md).
    const project = this.projectStages;
    const controllerIn = dyn.controllerIn ?? NO_CONTROLLER_IN;
    for (let i = 0; i < project.length; i++) {
      const ps = project[i];
      ps.ctx.controllerIn = controllerIn;
      if (this.traceOn) this.tracer!.begin(ps.label);
      ps.dsp(ps.ctx);
      if (this.traceOn) this.tracer!.end();
    }

    // System scope: run each system's ordered pipeline against its persistent ctx.
    for (let i = 0; i < slots.length; i++) {
      const stages = slots[i].stages;
      for (let s = 0; s < stages.length; s++) {
        const st = stages[s];
        if (this.traceOn) this.tracer!.begin(st.label);
        st.dsp(st.ctx);
        if (this.traceOn) this.tracer!.end();
      }
    }
    return this.result;
  }

  // Build the PERSISTENT per-(system, stage) context. Every field points at something the kernel
  // mutates in place each block — `slot.inbox/keys/buttons` (refilled), `this.block` (overwritten),
  // the persistent `state` bag — so `processBlock` never rebuilds a ctx or its sink closures. The
  // sink closures are bound once here (capture `this`/`id`), eliminating the per-block closure
  // clusters that formerly formed the reference cycles the collector had to sweep.
  private buildCtx(id: number, stageIndex: number, config: Record<string, unknown>, slot: SystemSlot): SystemCtx {
    return {
      config,
      midi: slot.inbox,
      keys: slot.keys,
      buttons: slot.buttons,
      serialOut: slot.serialOut,
      block: this.block,
      state: this.stageState(id, stageIndex),
      pushSerialIn: (frame, byte) => this.sink.pushSerialIn(id, frame, byte),
      emitMidiOut: (frame, data) => this.sink.emitMidiOut(id, frame, data),
      emitCoreMidi: (frame, data) => this.sink.emitCoreMidi(id, frame, data),
      pushCoreBytes: (frame, data, flush) => this.sink.pushCoreBytes(id, frame, data, flush),
      pressButton: (button, down) => this.sink.pressButton(id, 0, button, down),
      eachTick: (resolution, cb) => {
        const nt = this.tick.get(id) ?? 0;
        this.tick.set(id, walkTicks(this.block, resolution, nt, cb));
      },
      setNextTick: (tick) => this.tick.set(id, tick),
    };
  }

  // The PERSISTENT project-scope context, built once per setSystems. `block` and `inboxes` are the
  // kernel's own long-lived objects (overwritten / cleared in place each block), and `controllerIn` is
  // repointed by processBlock, so nothing here is rebuilt per block.
  private buildProjectCtx(stageIndex: number, config: Record<string, unknown>): ProjectCtx {
    return {
      block: this.block,
      inboxes: this.inboxes,
      config,
      state: this.projectStageState(stageIndex),
      controllerIn: NO_CONTROLLER_IN,
      emitControllerOut: (data) => this.sink.emitControllerOut?.(data),
      toSystem: (systemId, ev) => {
        const slot = this.slotById.get(systemId);
        if (slot) slot.inbox.push(ev);
      },
      emitMidiOut: (systemId, frame, data) => this.sink.emitMidiOut(systemId, frame, data),
    };
  }

  // The persistent scratch bag for a project-scope stage, created on first use.
  private projectStageState(stageIndex: number): Record<string, unknown> {
    let bag = this.projectState.get(stageIndex);
    if (!bag) this.projectState.set(stageIndex, (bag = {}));
    return bag;
  }

  // The persistent scratch bag for one system's pipeline stage, created on first use.
  private stageState(id: number, stageIndex: number): Record<string, unknown> {
    let byStage = this.state.get(id);
    if (!byStage) this.state.set(id, (byStage = new Map()));
    let bag = byStage.get(stageIndex);
    if (!bag) byStage.set(stageIndex, (bag = {}));
    return bag;
  }
}
