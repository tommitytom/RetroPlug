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

/** Everything the host collected for one block. `midiIn` is GLOBAL (a project-scope routing
 *  behaviour fans it to systems); `buttons`/`keys` already carry a target system. `project` is the
 *  project-scope pipeline (e.g. midi-routing); `systems[i].pipeline` is that system's ordered
 *  DSP-thread behaviours. */
export interface Block extends BlockInfo {
  midiIn: MidiEvent[];
  buttons: ButtonEvent[];
  keys: KeyEvent[];
  systems: { id: number; pipeline: RoleInstance[] }[];
  project?: RoleInstance[];
}

/** The collected output the host applies to the emulators / host after the block. */
export interface Sinks {
  serialIn: { system: number; frame: number; byte: number }[];
  midiOut: { system: number; frame: number; data: number[] }[];
  buttons: { system: number; frame: number; button: number; down: boolean }[];
}

/** Per-system context handed to a system-scope behaviour: its inputs + sinks, scoped to the
 *  system so role code never carries a `sys` argument. `midi` is mutable — a future transform
 *  stage may rewrite it before a downstream translator reads it (the pipeline is ordered). */
export interface SystemCtx {
  config: Record<string, unknown>;
  midi: MidiEvent[];
  keys: KeyEvent[];
  buttons: ButtonEvent[];
  block: BlockInfo;
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

/** Runs a project's DSP-thread behaviours per block. Holds per-system `nextTick` state so
 *  `eachTick` stays drift-free across successive `processBlock` calls. Stateless otherwise — one
 *  instance per project; a fresh one per unit test. */
export class DspKernel {
  private readonly tick = new Map<number, number>();

  constructor(private readonly registry: RoleRegistry) {}

  processBlock(block: Block): Sinks {
    const out = emptySinks();
    const routed = new Map<number, MidiEvent[]>();

    // Project scope: routing fans the global midiIn into per-system inboxes.
    for (const stage of block.project ?? []) {
      const rt = this.registry.roleType(stage.kind);
      if (rt?.scope === "project" && rt.dsp) (rt.dsp as ProjectBehavior)(block, routed, stage.config);
    }

    // System scope: each system's ordered pipeline of translators/sources.
    for (const sys of block.systems) {
      const midi = routed.get(sys.id) ?? [];
      for (const stage of sys.pipeline) {
        const rt = this.registry.roleType(stage.kind);
        if (!rt || rt.scope === "project" || !rt.dsp) continue;
        (rt.dsp as SystemBehavior)(this.makeCtx(sys.id, stage.config, midi, block, out));
      }
    }
    return out;
  }

  private makeCtx(
    id: number,
    config: Record<string, unknown>,
    midi: MidiEvent[],
    block: Block,
    out: Sinks,
  ): SystemCtx {
    return {
      config,
      midi,
      keys: block.keys.filter((k) => k.system === id),
      buttons: block.buttons.filter((b) => b.system === id),
      block,
      pushSerialIn: (frame, byte) => out.serialIn.push({ system: id, frame, byte }),
      emitMidiOut: (frame, data) => out.midiOut.push({ system: id, frame, data }),
      pressButton: (button, down) => out.buttons.push({ system: id, frame: 0, button, down }),
      eachTick: (resolution, cb) => {
        const nt = this.tick.get(id) ?? 0;
        this.tick.set(id, walkTicks(block, resolution, nt, cb));
      },
    };
  }
}
