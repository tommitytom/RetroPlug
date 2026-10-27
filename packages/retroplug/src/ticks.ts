// The PPQ tick walk every clocking protocol counts its clocks with: LSDj's 24-PPQN serial clock, risa's FIFO clock and
// the smsggdj line counter. The protocol script nodes (script/nodes/) walk it, each keeping its counter on its
// instance (script/nodes/clock.ts). A leaf: it imports nothing.

/** What a tick walk reads of a block: its length and rate, the tempo, the PPQ position at its start, and whether the
 *  transport is playing. */
export interface TickBlock {
  frames: number;
  sampleRate: number;
  tempo: number;
  ppqStart: number; // PPQ position at block start
  transport: boolean;
}

/** Called for each tick a block holds: the tick's number and its frame in the block. */
export type TickCallback = (tick: number, off: number) => void;

// Walk the PPQ ticks that fall in this block at `resolution` ticks/quarter, calling cb(tick, off)
// for each; returns the advanced `nextTick`. A faithful TS twin of native PpqUtil::eachTick
// (packages/native/src/util/PpqUtil.hpp): the caller-owned `nextTick` persists across blocks so the
// clock is drift-free at block edges (each tick fires exactly once, no double/miss); a >1-tick
// transport jump (seek/loop/start) resyncs.
export function walkTicks(block: TickBlock, resolution: number, nextTick: number, cb: TickCallback): number {
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
