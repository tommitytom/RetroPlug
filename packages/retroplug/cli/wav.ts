// A tiny RIFF/PCM16 WAV encoder for the greenfield CLI. The greenfield backend has no WAV writer (every
// one in the tree is native/legacy-only), and createAudioDriver().renderAudio(ms) hands back raw
// interleaved-stereo Float32 PCM — so a session encodes the WAV itself and persists it via
// backend.writeFile(). Byte layout mirrors packages/native/cli/Wav.hpp (16-bit PCM, little-endian).

function writeAscii(view: DataView, offset: number, s: string): void {
  for (let i = 0; i < s.length; i++) view.setUint8(offset + i, s.charCodeAt(i));
}

/** Encode interleaved PCM (as renderAudio returns it) to a 16-bit WAV. `pcm` is L,R,L,R… in [-1,1]
 *  (clamped here — the render pipeline isn't hard-clamped); `channels` is how those samples interleave. */
export function encodeWav(pcm: Float32Array, sampleRate = 44100, channels = 2): Uint8Array {
  const dataSize = pcm.length * 2; // one int16 (2 bytes) per interleaved sample
  const buf = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buf);

  writeAscii(view, 0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  writeAscii(view, 8, "WAVE");
  writeAscii(view, 12, "fmt ");
  view.setUint32(16, 16, true); // fmt chunk size
  view.setUint16(20, 1, true); // PCM
  view.setUint16(22, channels, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * channels * 2, true); // byte rate
  view.setUint16(32, channels * 2, true); // block align
  view.setUint16(34, 16, true); // bits per sample
  writeAscii(view, 36, "data");
  view.setUint32(40, dataSize, true);

  let o = 44;
  for (let i = 0; i < pcm.length; i++) {
    const v = pcm[i] > 1 ? 1 : pcm[i] < -1 ? -1 : pcm[i];
    view.setInt16(o, (v * 32767) | 0, true);
    o += 2;
  }
  return new Uint8Array(buf);
}

/** Interleave K equal-length stereo streams (each L,R,L,R…) into one 2K-channel interleaved buffer,
 *  channel order [s0.L, s0.R, s1.L, s1.R, …]. Feeds encodeWav(_, sr, 2*K) for a single multichannel WAV
 *  (Game Boy: 4 stereo channel-streams → 8 channels). Streams must share a frame count. */
export function interleaveStereoStreams(streams: Float32Array[]): Float32Array {
  const k = streams.length;
  if (k === 0) return new Float32Array(0);
  const frames = streams[0].length / 2;
  for (const s of streams)
    if (s.length !== streams[0].length) throw new Error("interleaveStereoStreams: streams differ in length");
  const out = new Float32Array(frames * 2 * k);
  for (let f = 0; f < frames; f++)
    for (let s = 0; s < k; s++) {
      out[f * 2 * k + s * 2 + 0] = streams[s][f * 2 + 0];
      out[f * 2 * k + s * 2 + 1] = streams[s][f * 2 + 1];
    }
  return out;
}

/** Split one interleaved-stereo buffer (L,R,L,R…) into its two mono channels [L…], [R…]. Feeds
 *  encodeWav(_, sr, 1) for individual-mono export (Game Boy: 4 stereo streams → 8 mono files). */
export function deinterleaveStereo(pcm: Float32Array): [Float32Array, Float32Array] {
  const frames = pcm.length / 2;
  const l = new Float32Array(frames);
  const r = new Float32Array(frames);
  for (let f = 0; f < frames; f++) {
    l[f] = pcm[f * 2 + 0];
    r[f] = pcm[f * 2 + 1];
  }
  return [l, r];
}

/** Decode a 16-bit PCM WAV produced by encodeWav back to { sampleRate, channels, interleaved pcm } in
 *  [-1,1]. Reads the canonical 44-byte header this module writes (no chunk-walking); for the round-trip
 *  test that guards >2-channel output. */
export function decodeWav(bytes: Uint8Array): { sampleRate: number; channels: number; pcm: Float32Array } {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (o: number) => String.fromCharCode(view.getUint8(o), view.getUint8(o + 1), view.getUint8(o + 2), view.getUint8(o + 3));
  if (tag(0) !== "RIFF" || tag(8) !== "WAVE" || tag(36) !== "data")
    throw new Error("decodeWav: not a canonical RIFF/PCM16 WAV");
  const channels = view.getUint16(22, true);
  const sampleRate = view.getUint32(24, true);
  const dataSize = view.getUint32(40, true);
  const count = dataSize / 2; // int16 samples
  const pcm = new Float32Array(count);
  for (let i = 0; i < count; i++) pcm[i] = view.getInt16(44 + i * 2, true) / 32768;
  return { sampleRate, channels, pcm };
}
