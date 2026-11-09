// The backend-bound half of the WAV codec: `createWavWriter` streams PCM to disk as the render pipeline produces it
// (a placeholder header, appended batches, then a header patch), so a long render never buffers the whole song. The
// pure functions it uses live in @retroplug/dsp/wav (docs/modular-graph-phase-f4b-design.md section 5.2), whose whole
// surface this module re-exports; cli/wav.ts re-exports both halves for the CLI's sessions.
import { pcm16, wavHeader } from "@retroplug/dsp/wav";

// Every importer of this module gets the whole codec, the pure half included.
export * from "@retroplug/dsp/wav";

/** The subset of Backend a streaming WAV writer needs (kept structural so wav.ts stays dependency-free). */
export interface WavBackend {
  writeFile(path: string, bytes: Uint8Array): boolean;
  appendFile(path: string, bytes: Uint8Array): boolean;
  writeFileAt(path: string, offset: number, bytes: Uint8Array): boolean;
}

export interface WavWriter {
  /** Append one interleaved-float chunk in this writer's channel layout (may be a subarray view). */
  append(pcm: Float32Array): void;
  /** Flush the remainder, patch the header with the final length, and return the frame count written. */
  finish(): number;
}

const WAV_FLUSH_BYTES = 1 << 20; // batch ~1 MiB of PCM per appendFile to bound memory + cut open/close churn

/** A streaming WAV writer: writes the header immediately (with a placeholder length), buffers PCM and
 *  flushes it to disk in ~1 MiB batches, then patches the header's length fields on finish(). Peak memory
 *  is O(one batch), independent of song length. `open` uses writeFile (truncating) so a re-render clobbers
 *  any stale file and creates parent dirs — append must never be a path's first touch. */
export function createWavWriter(
  backend: WavBackend,
  path: string,
  sampleRate: number,
  channels: number,
): WavWriter {
  if (!backend.writeFile(path, wavHeader(sampleRate, channels, 0)))
    throw new Error(`wav: could not open ${path}`);

  let pending: Uint8Array[] = [];
  let pendingBytes = 0;
  let dataSize = 0; // total PCM bytes appended (== frames * channels * 2)

  const flush = () => {
    if (pendingBytes === 0) return;
    const batch = pending.length === 1 ? pending[0] : concatBytes(pending, pendingBytes);
    if (!backend.appendFile(path, batch)) throw new Error(`wav: append failed: ${path}`);
    pending = [];
    pendingBytes = 0;
  };

  return {
    append(pcm) {
      const bytes = pcm16(pcm);
      pending.push(bytes);
      pendingBytes += bytes.length;
      dataSize += bytes.length;
      if (pendingBytes >= WAV_FLUSH_BYTES) flush();
    },
    finish() {
      flush();
      if (!backend.writeFileAt(path, 0, wavHeader(sampleRate, channels, dataSize)))
        throw new Error(`wav: header patch failed: ${path}`);
      return dataSize / 2 / channels;
    },
  };
}

function concatBytes(parts: Uint8Array[], total: number): Uint8Array {
  const out = new Uint8Array(total);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

