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
