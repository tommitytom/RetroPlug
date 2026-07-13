// Guards the CLI WAV codec, especially the >2-channel path the Game Boy per-channel export relies on
// (spec/10 §5) — untested before this. Covers the round-trip through the 8-channel encoder/decoder, the
// stereo-stream interleave/deinterleave helpers that derive the multichannel + mono export shapes, and the
// streaming createWavWriter (header placeholder → appended PCM batches → header patch) the render session
// uses to write WAVs without buffering the whole song.
import { test, expect } from "../../testing/harness";
import { encodeWav, decodeWav, interleaveStereoStreams, deinterleaveStereo, createWavWriter } from "../../cli/wav";
import { MockBackend } from "../../testing/mockBackend";

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

test("wav: createWavWriter streams PCM to disk and its final file equals encodeWav byte-for-byte", () => {
  const mock = new MockBackend();
  const sr = 44100, channels = 2, path = "/out/stream.wav";
  // Big enough to cross the ~1 MiB internal flush threshold (>524288 samples) at least once.
  const total = 600000;
  const pcm = new Float32Array(total);
  for (let i = 0; i < total; i++) pcm[i] = Math.sin(i * 0.013) * 0.5;

  const w = createWavWriter(mock, path, sr, channels);
  let o = 0;
  for (const n of [7, 300000, total - 7 - 300000]) { w.append(pcm.subarray(o, o + n)); o += n; } // irregular sub-slices
  const frames = w.finish();
  expect(frames).toBe(total / channels);
  const log = mock.log.slice(); // snapshot before the readFile below adds to it

  const file = mock.readFile(path)!;
  const whole = encodeWav(pcm, sr, channels);
  expect(file.length).toBe(whole.length);
  let diff = 0;
  for (let i = 0; i < whole.length; i++) if (file[i] !== whole[i]) diff++;
  expect(diff).toBe(0); // streamed output is byte-identical to the whole-buffer encoder

  // The file also decodes cleanly (header patched correctly).
  const decoded = decodeWav(file);
  expect(decoded.channels).toBe(channels);
  expect(decoded.pcm.length).toBe(total);

  // Streaming shape: open via writeFile → ≥1 appendFile flush → header patched via writeFileAt (last call).
  expect(log[0]).toBe("writeFile");
  expect(log.includes("appendFile")).toBeTruthy();
  expect(log[log.length - 1]).toBe("writeFileAt");
});

test("wav: createWavWriter clamps out-of-range samples identically to encodeWav (odd channel count)", () => {
  const mock = new MockBackend();
  const sr = 48000, channels = 3, path = "/out/id.wav";
  const pcm = new Float32Array(90); // 30 frames × 3ch
  for (let i = 0; i < pcm.length; i++) pcm[i] = i % 9 === 0 ? 2.0 : i % 9 === 4 ? -2.0 : (i % 7 - 3) / 4; // clamp edges

  const w = createWavWriter(mock, path, sr, channels);
  let o = 0;
  for (const n of [3, 0, 11, 76]) { w.append(pcm.subarray(o, o + n)); o += n; } // includes a zero-length append
  w.finish();

  const file = mock.readFile(path)!;
  const whole = encodeWav(pcm, sr, channels);
  expect(file.length).toBe(whole.length);
  let diff = 0;
  for (let i = 0; i < whole.length; i++) if (file[i] !== whole[i]) diff++;
  expect(diff).toBe(0);
});
