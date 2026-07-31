// The drift-exact PPQ tick walker — the TS twin of native PpqUtil::eachTick, and the single source of
// truth for the 24-PPQN LSDj sync clock. Extracted from dspKernel.ts (which re-exports it) so it has no
// runtime dependencies and can be reused headlessly (e.g. the retroplug-cli `linksync` host bridge, which
// streams the same clock to Chromatic hardware). Keep this file free of TS parameter properties / enums so
// it stays importable by plain Node type-stripping.

/** The minimal block shape walkTicks needs (structurally satisfied by dspKernel's BlockInfo). */
export interface TickBlock {
  frames: number;
  sampleRate: number;
  tempo: number;
  ppqStart: number; // PPQ position at block start
  transport: boolean;
}

/**
 * Emit every PPQ tick (at `resolution` ticks/quarter-note) that falls within this block, carrying a
 * PERSISTENT `nextTick` across blocks so the clock never drifts at block edges. Returns the next tick.
 * `cb(tick, off)` gets the absolute tick number and its sample offset within the block.
 */
export function walkTicks(
  block: TickBlock,
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
