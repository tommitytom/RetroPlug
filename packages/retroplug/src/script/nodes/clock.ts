// A protocol node's tick clock: the drift-free counter the DSP kernel kept for each system (its `tick` map), kept on
// the node's instance instead. No system has two clocking protocols, so one counter a node is the counter its system
// had. Made once in create, filled each block, so a block allocates nothing here.
import type { Block } from "../../graph/defineNode";
import { walkTicks, type TickBlock, type TickCallback } from "../../ticks";

export class TickClock {
  /** This block's transport, as a tick walk reads it (and as master sync reads the block's length and rate). */
  readonly block: TickBlock = { frames: 0, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false };
  /** The tick the next walk starts from: 0 on a fresh instance, as the kernel's counter was for a new system. */
  next = 0;

  /** Read this block's transport. Called once at the top of every process, before any walk. */
  begin(b: Block): void {
    const t = b.transport;
    const into = this.block;
    into.frames = b.frames;
    into.sampleRate = t.sampleRate;
    into.tempo = t.tempo;
    into.ppqStart = t.ppq;
    into.transport = t.playing;
  }

  /** Each tick this block holds at `resolution` ticks a quarter, through `cb`, and the counter advanced past them: the
   *  kernel's ctx.eachTick was. A walk `cb` throws out of leaves the counter where it was, as the kernel's did. */
  walk(resolution: number, cb: TickCallback): void {
    this.next = walkTicks(this.block, resolution, this.next, cb);
  }
}
