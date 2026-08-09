// Shared measurement helpers for the audio-levels-* files. Measurement harness, not a regression
// guard: prints peak / RMS in raw float32 (1.0 == int16 full scale) so the analytic ceilings can be
// checked against real core output.
export const peak = (a: Float32Array): number => {
  let m = 0;
  for (let i = 0; i < a.length; i++) { const v = Math.abs(a[i]); if (v > m) m = v; }
  return m;
};

export const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

export const db = (v: number): string => (v > 0 ? (20 * Math.log10(v)).toFixed(2) : "-inf");

export const report = (label: string, b: Float32Array): void => {
  const p = peak(b);
  console.log(
    `  ${label.padEnd(10)} peak=${p.toFixed(4)} ${("(" + db(p) + " dBFS)").padStart(16)}` +
    `  int16=${Math.round(p * 32768).toString().padStart(6)}   rms=${rms(b).toFixed(4)} (${db(rms(b))} dBFS)`,
  );
};

type Audio = { renderAudio(ms: number): Float32Array; renderAudioPerChannel(id: number, ms: number): Float32Array[]; stageMidiIn(b: number[]): boolean };

/** Render `ms` in slices, retriggering `notes` before each slice (so envelopes never decay out of the
 *  window), and keep the loudest slice. */
export const sustain = (audio: Audio, notes: number[][], ms: number, slices: number): Float32Array => {
  let best: Float32Array | null = null;
  for (let i = 0; i < slices; i++) {
    notes.forEach((m) => audio.stageMidiIn(m));
    const b = audio.renderAudio(ms / slices);
    if (!best || peak(b) > peak(best)) best = b;
  }
  return best ?? new Float32Array(0);
};

/** sustain(), but pulling the per-channel streams; keeps the slice whose loudest stem is loudest. */
export const sustainPerChannel = (audio: Audio, id: number, notes: number[][], ms: number, slices: number): Float32Array[] => {
  let best: Float32Array[] = [];
  for (let i = 0; i < slices; i++) {
    notes.forEach((m) => audio.stageMidiIn(m));
    const b = audio.renderAudioPerChannel(id, ms / slices);
    if (b.length && (!best.length || Math.max(...b.map(peak)) > Math.max(...best.map(peak)))) best = b;
  }
  return best;
};
