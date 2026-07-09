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
  buttons: { system: number; frame: number; button: number; down: boolean }[];
}

/** Where a behaviour's sinks go. The kernel forwards each ctx sink call to the injected target,
 *  scoped by system id. In tests/txiki the default `CollectingSink` gathers them into a `Sinks`
 *  object the assertions read; natively the target's methods ARE the bound C thunks, so bytes cross
 *  as scalars with no intermediate JS arrays. `reset` (optional) clears per-block state. */
export interface SinkTarget {
  pushSerialIn(system: number, frame: number, byte: number): void;
  emitMidiOut(system: number, frame: number, data: number[]): void;
  pressButton(system: number, frame: number, button: number, down: boolean): void;
  reset?(): void;
}

/** The default `SinkTarget`: gathers sink calls into a `Sinks` object (what `processBlock` returns
 *  and the pure-TS tests assert on). */
export class CollectingSink implements SinkTarget {
  readonly sinks: Sinks = emptySinks();
  reset(): void {
    this.sinks.serialIn.length = 0;
    this.sinks.midiOut.length = 0;
    this.sinks.buttons.length = 0;
  }
  pushSerialIn(system: number, frame: number, byte: number): void {
    this.sinks.serialIn.push({ system, frame, byte });
  }
  emitMidiOut(system: number, frame: number, data: number[]): void {
    this.sinks.midiOut.push({ system, frame, data });
  }
  pressButton(system: number, frame: number, button: number, down: boolean): void {
    this.sinks.buttons.push({ system, frame, button, down });
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
  block: BlockInfo;
  state: Record<string, unknown>;
  pushSerialIn(frame: number, byte: number): void;
  emitMidiOut(frame: number, data: number[]): void;
  pressButton(button: number, down: boolean): void;
  eachTick(resolution: number, cb: (tick: number, off: number) => void): void;
}

export type SystemBehavior = (ctx: SystemCtx) => void;
export type ProjectBehavior = (
  block: Block,
  routed: Map<number, MidiEvent[]>,
  config: Record<string, unknown>,
) => void;

export function emptySinks(): Sinks {
  return { serialIn: [], midiOut: [], buttons: [] };
}

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

/** Runs a project's DSP-thread behaviours per block. Structure (systems + pipelines) is pushed once
 *  via `setSystems`; each block passes only dynamic input. Holds per-system `nextTick` state so
 *  `eachTick` stays drift-free across successive `processBlock` calls, and forwards every sink to an
 *  injected `SinkTarget` (default: a `CollectingSink` whose `Sinks` `processBlock` returns). One
 *  instance per project; a fresh one per unit test. */
export class DspKernel {
  private readonly tick = new Map<number, number>();
  // Persistent per-system, per-stage scratch bags (system id → stage index → bag). Backs ctx.state
  // so a stateful behaviour keeps its cross-block state; pruned alongside `tick` in setSystems.
  private readonly state = new Map<number, Map<number, Record<string, unknown>>>();
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
    systems: [],
    project: [],
  };
  // Returned from processBlock: the collecting sink's Sinks, else a stable empty (native ignores it).
  private readonly result: Sinks;

  constructor(
    private readonly registry: RoleRegistry,
    private readonly sink: SinkTarget = new CollectingSink(),
  ) {
    this.result = sink instanceof CollectingSink ? sink.sinks : emptySinks();
  }

  /** Push the (rarely-changing) system + pipeline structure. The stored ORDER is authoritative for
   *  positional routing. Also prunes per-system tick state for ids no longer present, so a
   *  removed-then-readded id starts a fresh clock instead of resuming mid-count. */
  setSystems(struct: KernelStructure): void {
    this.block.systems = struct.systems;
    this.block.project = struct.project ?? [];
    const live = new Set(struct.systems.map((s) => s.id));
    for (const id of this.tick.keys()) if (!live.has(id)) this.tick.delete(id);
    for (const id of this.state.keys()) if (!live.has(id)) this.state.delete(id);
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

    this.sink.reset?.();
    const routed = new Map<number, MidiEvent[]>();

    // Project scope: routing fans the global midiIn into per-system inboxes.
    for (const stage of b.project ?? []) {
      const rt = this.registry.roleType(stage.kind);
      if (rt?.scope === "project" && rt.dsp) (rt.dsp as ProjectBehavior)(b, routed, stage.config);
    }

    // System scope: each system's ordered pipeline of translators/sources. Filter this system's
    // keys/buttons ONCE (not per pipeline stage).
    for (const sys of b.systems) {
      const midi = routed.get(sys.id) ?? [];
      const keys = b.keys.filter((k) => k.system === sys.id);
      const buttons = b.buttons.filter((k) => k.system === sys.id);
      sys.pipeline.forEach((stage, stageIndex) => {
        const rt = this.registry.roleType(stage.kind);
        if (!rt || rt.scope === "project" || !rt.dsp) return;
        (rt.dsp as SystemBehavior)(this.makeCtx(sys.id, stageIndex, stage.config, midi, keys, buttons, b));
      });
    }
    return this.result;
  }

  private makeCtx(
    id: number,
    stageIndex: number,
    config: Record<string, unknown>,
    midi: MidiEvent[],
    keys: KeyEvent[],
    buttons: ButtonEvent[],
    block: BlockInfo,
  ): SystemCtx {
    return {
      config,
      midi,
      keys,
      buttons,
      block,
      state: this.stageState(id, stageIndex),
      pushSerialIn: (frame, byte) => this.sink.pushSerialIn(id, frame, byte),
      emitMidiOut: (frame, data) => this.sink.emitMidiOut(id, frame, data),
      pressButton: (button, down) => this.sink.pressButton(id, 0, button, down),
      eachTick: (resolution, cb) => {
        const nt = this.tick.get(id) ?? 0;
        this.tick.set(id, walkTicks(block, resolution, nt, cb));
      },
    };
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
