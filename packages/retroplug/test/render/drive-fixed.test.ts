// The fixed-duration driver's own guards. A render pulled in chunks that carry no frames would never reach its
// target, so a chunk with no frames (or no streams) is an error rather than a spin; RenderOpts.pullMs's own check
// refuses the pull that causes it before a render starts, so this is the guard behind that one.
import { test, expect } from "../../testing/harness";
import { driveFixed, type RenderSink } from "../../src/render/render";

const sinkOf = (chunks: Float32Array[][]): RenderSink & { emitted: number } => {
  let next = 0;
  const sink = {
    emitted: 0,
    renderChunk: () => chunks[Math.min(next++, chunks.length - 1)],
    emit: (_chunk: Float32Array[], take: number) => void (sink.emitted += take),
    finishAll: () => [],
    discard: () => {},
  };
  return sink;
};

test("driveFixed throws on a chunk with no frames instead of pulling for ever", () => {
  const sink = sinkOf([[new Float32Array(0)]]);
  expect(() => driveFixed(sink, 100, 44100, {}, 100)).toThrow("render: chunk render returned no frames");
  expect(sink.emitted).toBe(0);
});

test("driveFixed throws on a chunk with no streams", () => {
  const sink = sinkOf([[]]);
  expect(() => driveFixed(sink, 100, 44100, {}, 100)).toThrow("render: chunk render returned no streams");
});

test("driveFixed trims the last chunk to the target", () => {
  const sink = sinkOf([[new Float32Array(2 * 60)]]); // 60 stereo frames a pull
  driveFixed(sink, 100, 44100, {}, 100);
  expect(sink.emitted).toBe(100);
});
