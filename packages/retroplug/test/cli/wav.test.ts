// Guards the CLI WAV codec, especially the >2-channel path the Game Boy per-channel export relies on
// (spec/10 §5) — untested before this. Covers the round-trip through the 8-channel encoder/decoder plus
// the stereo-stream interleave/deinterleave helpers that derive the multichannel + mono export shapes.
import { test, expect } from "../../testing/harness";
import { encodeWav, decodeWav, interleaveStereoStreams, deinterleaveStereo } from "../../cli/wav";

test("wav: an 8-channel PCM buffer round-trips through encode/decode", () => {
  const frames = 6;
  const channels = 8;
  const pcm = new Float32Array(frames * channels);
  for (let f = 0; f < frames; f++)
    for (let c = 0; c < channels; c++)
      pcm[f * channels + c] = ((c + 1) / 16) * (f % 2 === 0 ? 1 : -1); // distinct per channel + sign

  const sr = 48000;
  const decoded = decodeWav(encodeWav(pcm, sr, channels));

  expect(decoded.channels).toBe(channels); // the header must carry 8, not clamp to 2
  expect(decoded.sampleRate).toBe(sr);
  expect(decoded.pcm.length).toBe(pcm.length);

  let maxDiff = 0;
  for (let i = 0; i < pcm.length; i++) maxDiff = Math.max(maxDiff, Math.abs(decoded.pcm[i] - pcm[i]));
  expect(maxDiff < 1e-3).toBeTruthy(); // only int16 quantization error survives
});

test("wav: decoded header fields (byte rate / block align) match the channel count", () => {
  const bytes = encodeWav(new Float32Array(16), 44100, 4);
  const view = new DataView(bytes.buffer);
  expect(view.getUint16(22, true)).toBe(4); // NumChannels
  expect(view.getUint32(28, true)).toBe(44100 * 4 * 2); // ByteRate = sr * channels * bytesPerSample
  expect(view.getUint16(32, true)).toBe(4 * 2); // BlockAlign = channels * bytesPerSample
});

test("wav: interleaveStereoStreams orders channels [s0.L,s0.R,s1.L,s1.R,…] per frame", () => {
  // Two 2-frame stereo streams; values are exact float32 so equality is safe.
  const s0 = new Float32Array([1, 2, 3, 4]); // frames: (1,2) (3,4)
  const s1 = new Float32Array([5, 6, 7, 8]); // frames: (5,6) (7,8)
  const out = interleaveStereoStreams([s0, s1]);
  expect(Array.from(out)).toEqual([1, 2, 5, 6, 3, 4, 7, 8]);
  expect(interleaveStereoStreams([]).length).toBe(0);
});

test("wav: interleave of 4 stereo streams yields an 8-channel buffer", () => {
  const streams = [0, 1, 2, 3].map((k) => new Float32Array([k, k, k, k])); // 2 frames each
  const out = interleaveStereoStreams(streams);
  expect(out.length).toBe(2 * 8); // 2 frames × (4 streams × 2)
  expect(Array.from(out.slice(0, 8))).toEqual([0, 0, 1, 1, 2, 2, 3, 3]); // first frame
});

test("wav: deinterleaveStereo splits L,R,L,R into the two mono channels", () => {
  const [l, r] = deinterleaveStereo(new Float32Array([1, 2, 3, 4, 5, 6]));
  expect(Array.from(l)).toEqual([1, 3, 5]);
  expect(Array.from(r)).toEqual([2, 4, 6]);
});

test("wav: interleaveStereoStreams rejects mismatched stream lengths", () => {
  expect(() => interleaveStereoStreams([new Float32Array(4), new Float32Array(2)])).toThrow();
});
