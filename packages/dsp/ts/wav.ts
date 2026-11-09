// A tiny RIFF/PCM16 WAV codec's pure half (docs/modular-graph-phase-f4b-design.md section 5.2): the header, the
// int16 body, the whole-buffer encoder, the two stereo helpers and the decoder. A package's CLI tool imports these
// from here; the backend-bound streaming writer (`createWavWriter`) stays in the shell's src/render/wav.ts, which
// imports what it needs from this module.
function writeAscii(view: DataView, offset: number, s: string): void {
  for (let i = 0; i < s.length; i++) view.setUint8(offset + i, s.charCodeAt(i));
}

/** The fixed 44-byte canonical RIFF/PCM16 header. Only two fields depend on length: offset 4 (RIFF
 *  chunk size = 36 + dataSize) and offset 40 (dataSize), so a streaming writer emits this with
 *  dataSize=0, appends the PCM, then re-emits it at offset 0 with the final dataSize (createWavWriter). */
export function wavHeader(sampleRate: number, channels: number, dataSize: number): Uint8Array {
  const buf = new ArrayBuffer(44);
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
  return new Uint8Array(buf);
}

/** Convert interleaved float PCM in [-1,1] (clamped: the render pipeline isn't hard-clamped) to the
 *  little-endian int16 body a WAV carries. Iterates 0..length, so it honors a `subarray` view (non-zero
 *  byteOffset), which the streaming writer feeds per-chunk sub-slices with. */
export function pcm16(pcm: Float32Array): Uint8Array {
  const out = new Uint8Array(pcm.length * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < pcm.length; i++) {
    const v = pcm[i] > 1 ? 1 : pcm[i] < -1 ? -1 : pcm[i];
    view.setInt16(i * 2, (v * 32767) | 0, true);
  }
  return out;
}

/** Encode interleaved PCM (as renderAudio returns it) to a 16-bit WAV, whole-buffer. `pcm` is L,R,L,R…
 *  in [-1,1]; `channels` is how those samples interleave. (Streaming callers use createWavWriter.) */
export function encodeWav(pcm: Float32Array, sampleRate = 44100, channels = 2): Uint8Array {
  const header = wavHeader(sampleRate, channels, pcm.length * 2);
  const body = pcm16(pcm);
  const out = new Uint8Array(header.length + body.length);
  out.set(header);
  out.set(body, header.length);
  return out;
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

/** Decode a PCM WAV to { sampleRate, channels, interleaved pcm } in [-1,1]. Handles what encodeWav writes
 *  (16-bit, canonical 44-byte header) plus what an external recorder hands back from real hardware: 24/32-bit
 *  int and 32-bit float, WAVE_FORMAT_EXTENSIBLE, and extra chunks before `data` (so it walks chunks rather
 *  than assuming fixed offsets). */
export function decodeWav(bytes: Uint8Array): { sampleRate: number; channels: number; pcm: Float32Array } {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (o: number) => String.fromCharCode(view.getUint8(o), view.getUint8(o + 1), view.getUint8(o + 2), view.getUint8(o + 3));
  if (tag(0) !== "RIFF" || tag(8) !== "WAVE") throw new Error("decodeWav: not a RIFF/WAVE file");

  let format = 0, channels = 0, sampleRate = 0, bits = 0;
  let dataAt = -1, dataSize = 0;
  for (let o = 12; o + 8 <= bytes.byteLength; ) {
    const id = tag(o);
    const size = view.getUint32(o + 4, true);
    const body = o + 8;
    if (id === "fmt ") {
      format = view.getUint16(body, true);
      channels = view.getUint16(body + 2, true);
      sampleRate = view.getUint32(body + 4, true);
      bits = view.getUint16(body + 14, true);
      // WAVE_FORMAT_EXTENSIBLE: the real format is the first 2 bytes of the SubFormat GUID.
      if (format === 0xfffe && size >= 40) format = view.getUint16(body + 24, true);
    } else if (id === "data") {
      dataAt = body;
      dataSize = Math.min(size, bytes.byteLength - body);
    }
    o = body + size + (size & 1); // chunks are word-aligned
  }
  if (dataAt < 0 || !channels || !sampleRate) throw new Error("decodeWav: missing fmt/data chunk");

  const bytesPer = bits >> 3;
  if (!bytesPer) throw new Error("decodeWav: bad bits-per-sample");
  const count = Math.floor(dataSize / bytesPer);
  const pcm = new Float32Array(count);
  if (format === 3 && bits === 32) {
    for (let i = 0; i < count; i++) pcm[i] = view.getFloat32(dataAt + i * 4, true);
  } else if (format === 1 && bits === 16) {
    for (let i = 0; i < count; i++) pcm[i] = view.getInt16(dataAt + i * 2, true) / 32768;
  } else if (format === 1 && bits === 32) {
    for (let i = 0; i < count; i++) pcm[i] = view.getInt32(dataAt + i * 4, true) / 2147483648;
  } else if (format === 1 && bits === 24) {
    for (let i = 0; i < count; i++) {
      const p = dataAt + i * 3;
      const v = (view.getUint8(p) | (view.getUint8(p + 1) << 8) | (view.getInt8(p + 2) << 16));
      pcm[i] = v / 8388608;
    }
  } else if (format === 1 && bits === 8) {
    for (let i = 0; i < count; i++) pcm[i] = (view.getUint8(dataAt + i) - 128) / 128;
  } else {
    throw new Error(`decodeWav: unsupported format ${format} @ ${bits}-bit`);
  }
  return { sampleRate, channels, pcm };
}
