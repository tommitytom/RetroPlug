# Audio & tuning verification tooling for retroplug-cli

Status (2026-09-21): **F1 built** (decoded-Hz reads on the expansion-audio channels - it found a live
N163 ROM tuning bug the RMS tests were blind to). **F2-F7 deferred**, not abandoned: nothing since has
needed spectral analysis in the CLI. Treat the rest of this document as a work spec, not a plan of
record.
Audience: an agent implementing these features inside `retroplug-cli`
Constraint: **everything stays in the CLI (TypeScript/Node). No Python, no external analysis step.**

---

## 1. Why this exists

`retroplug-cli` drives a real Mesen core and is our only way to verify that an evermidi ROM
actually *sounds* right. Recently two real tuning bugs slipped through and were only found by
hand-reasoning + web searches, not by the harness:

- **N163 shipped ~11 cents sharp** for months. The N163 test file only checks RMS/audibility and
  octave *ratios*, never absolute Hz, so a systematic detune was invisible.
- **VRC7 was up to 16 cents off** equal temperament. Same blind spot.

When we tried to *verify* the N163 fix, we hit a wall that this document is meant to remove. There
are two independent ways to check tuning/timbre, and the harness is weak at both for anything past
the 2A03:

1. **White-box** — read what the ROM actually programmed into the chip, and what pitch the emulator
   decodes from it. This is deterministic and precise; it is the right tool for *pitch/tuning*.
2. **Black-box** — analyse the rendered audio (FFT, pitch detection, spectrogram). This is the only
   tool for *timbre and dynamics* (FM patches, DMC quality, vibrato, mixing, aliasing), where there
   is no register to read.

The N163 investigation failed on **both**: `getExpansionAudioState` gave us the raw `period`
register but no decoded Hz (so we couldn't tell whether the emulator applies the wave-length term in
the N163 pitch formula), and every attempt at black-box spectral analysis was a fragile, hand-rolled
autocorrelation that returned garbage on N163's time-multiplexed output — and even mis-read a
known-good 2A03 pulse.

As evermidi grows (FM patch editing, DMC/PCM voices, filters, more expansion chips), both gaps get
more expensive. This spec closes them **inside the CLI**.

---

## 2. What already exists (do NOT rebuild these)

From `sdk/retroplug-cli.d.ts` — the curated public surface:

- **`backend.getApuState(id)`** → `ApuState`. 2A03 only. Note `ApuSquareState.frequency` is a
  **decoded Hz** value — the model to copy for expansion chips.
- **`backend.getExpansionAudioState(id)`** → `ExpansionAudioState` with per-voice `period`
  (chip-native pitch register), `block` (VRC7 octave), `volume`, `duty`, `instrument`,
  `constantOutput`. White-box register inspection for VRC6/VRC7/S5B/N163 **already works**
  (see `repro/expansion-state.ts`). **It has no `frequency` field** — that is gap F1 below.
- **`backend.drainEvents(id)`** → `DebugEvent[]`: the APU/PPU/**mapper register-write** log for the
  last frame (`{ type, operationType, address, value, programCounter, ... }`). Raw register-write
  capture already exists; it just needs a decoder (F6).
- **`audio.renderAudio(ms)`** → interleaved-stereo `Float32Array` @ 44100 (L,R,L,R…).
- **`audio.renderAudioPerSystem(ms)`** → per-system interleaved stereo.
- **`encodeWav(pcm, sampleRate?, channels?)`** → 16-bit RIFF WAV bytes. WAV export already exists.
- **`backend.writeFile / writeFileAtomic`** → persist bytes (e.g. a WAV or PNG).
- **`audio.screenshot(id, path)`** → writes an **RGB24 PNG** of the framebuffer. Proof that a PNG
  write path exists on the native side; a spectrogram is "just" a different RGB buffer (F4).
- Full **debugger/profiler** (breakpoints, stepping, trace, `readMemory`, `readCpu`, profiler).
- **TAP test harness** (`test` / `expect`) + `Timeline` / `renderTimeline`.

The hand-rolled DSP that should be **replaced/absorbed** by this work:
`tests/expansion-helpers.ts` (`acPitch`, `mono`, `monoN`, `rms`, `octave`) and
`repro/verify-pitch.ts` — both reimplement autocorrelation pitch detection inline.

---

## 3. Design principles

- **TypeScript only.** DSP (FFT, windows, pitch, spectrogram, PNG) lives in the CLI as
  dependency-free TS (Node's built-in `zlib` is allowed for PNG deflate). No Python, no native FFT.
- **Prefer white-box for pitch.** A decoded-Hz read (F1) or a register decode (F6) beats spectral
  estimation every time for tuning. Reserve black-box (F2–F5) for timbre/dynamics.
- **Emit images for timbre.** A spectrogram PNG that a human or an agent can *look at* is more robust
  than any single scalar threshold. Lean on it.
- **Deterministic & fast.** Analysis must be pure functions over `Float32Array` PCM: same input →
  same output, no wall-clock, suitable for CI assertions.
- **Match the SDK shape.** New TS helpers live in `cli/` next to `wav.ts`, are re-exported from the
  runtime barrel (`cli/sdk.ts`), and are hand-typed into `sdk/retroplug-cli.d.ts`. Native additions
  extend the reflect-cpp structs behind `backend`.

---

## 4. Features

Each feature lists: **Problem**, **Proposed API**, **Implementation notes**, **Acceptance**.

### F1 — Decoded frequency (Hz) on expansion-audio channels  *(native; highest leverage)*

**Problem.** `ExpansionAudioChannel` exposes `period` (raw register) but not the Hz the chip
produces. For N163 the register→Hz relationship depends on the wave length and the number of active
channels (`f = reg·clock / (15·65536·l·c)`), and we could not determine whether Mesen applies the
`l` term — so we could not confirm whether the ROM plays A4 at 440 Hz or an octave off. Mesen already
computes each expansion voice's frequency internally for mixing; we just don't surface it.

**Proposed API.** Add a decoded-Hz field to the native struct, mirroring `ApuSquareState.frequency`:

```ts
export interface ExpansionAudioChannel {
  // ...existing fields...
  frequency: number;   // decoded output frequency in Hz (0 when silent), from Mesen's own model
  waveLength?: number; // N163: active wave length in samples (the `l` in the pitch formula)
  activeChannels?: number; // N163: enabled channel count (the `c` term)
}
```

**Implementation notes.** Source `frequency` from the same value Mesen's expansion-audio mixer uses
per channel (VRC6/VRC7/S5B/N163). For N163 also surface `waveLength` and `activeChannels` so a test
can cross-check the formula and catch the wave-length-register bug (evermidi writes `0x80` → a
128-sample length while loading a 32-sample wave; see `rom/n163.c`). This is a reflect-cpp struct
change on the native side plus the binding, then the `.d.ts`.

**Acceptance.** `getExpansionAudioState(id).channels[0].frequency` for an N163 A4 note reads within a
few Hz of the true output pitch; a test can assert `|frequency - 440| < tolerance` and it fails if
the LUT, PAL scaling, or wave-length setup is wrong. **This single field would have caught N163.**

---

### F2 — DSP core: FFT + windows + magnitude spectrum  *(TS)*

**Problem.** There is no FFT in the CLI. Every analysis is bespoke autocorrelation. We need a shared,
correct spectral primitive.

**Proposed API** (`cli/dsp.ts`, re-exported + typed):

```ts
/** De-interleave one channel of stereo PCM into mono (default: average L+R). */
export function toMono(pcm: Float32Array, opts?: { channel?: "left" | "right" | "mix" }): Float32Array;

/** A mono window of `n` samples starting at `startMs` (handles the interleaved-stereo @44100 layout). */
export function window(pcm: Float32Array, startMs: number, n: number, sampleRate?: number): Float32Array;

/** In-place-friendly real FFT. Input length is zero-padded to the next power of two.
 *  Returns magnitude (and optionally phase) for bins 0..N/2. */
export function magnitudeSpectrum(
  x: Float32Array,
  opts?: { window?: "hann" | "hamming" | "blackman" | "rect"; sampleRate?: number },
): { freqs: Float32Array; mag: Float32Array; sampleRate: number; binHz: number };
```

**Implementation notes.** Dependency-free radix-2 Cooley–Tukey (~60 lines). Apply a Hann window by
default (leakage matters for tuning). Provide the window functions as pure helpers. `binHz =
sampleRate / fftSize`. Keep everything `Float32Array`.

**Acceptance.** A synthesized 440 Hz sine (generated in the test, not the emulator) has its magnitude
peak in the bin nearest 440 Hz; total energy matches Parseval within rounding.

---

### F3 — Robust pitch detection: FFT + Harmonic Product Spectrum  *(TS)*

**Problem.** Autocorrelation (`acPitch`, `verify-pitch.ts`) is octave-ambiguous and dies on
N163/FM/noisy signals; it returned 0 for us. We need a detector that survives strong harmonics and
inharmonic content, and reports confidence honestly.

**Proposed API** (`cli/pitch.ts`):

```ts
export interface PitchResult {
  hz: number;          // 0 when no confident pitch
  cents: number;       // signed cents vs `refHz` (see detectCents), NaN if hz==0
  confidence: number;  // 0..1
  harmonics: number;   // how many harmonics reinforced the estimate
}

/** Fundamental via FFT + Harmonic Product Spectrum, with parabolic interpolation for sub-bin Hz. */
export function detectPitch(
  x: Float32Array,
  opts?: { sampleRate?: number; fmin?: number; fmax?: number; harmonics?: number },
): PitchResult;

/** Cents error of a detected pitch vs an expected equal-tempered frequency (octave-folded). */
export function centsError(measuredHz: number, expectedHz: number): number;
```

**Implementation notes.** HPS: downsample-multiply the magnitude spectrum `harmonics` times (default
5), take the argmax, then parabolic-interpolate the peak for sub-bin accuracy. Default `fmin` low
enough (e.g. 10 Hz) that an octave error is *detected*, not silently filtered — the ≥70 Hz floor in
the current `acPitch` is exactly why an octave-low N163 would read as "no pitch". Confidence = peak
prominence over the median spectrum. Deprecate `acPitch`/`verify-pitch.ts` in favour of this.

**Acceptance.** On emulator-rendered 2A03 pulse A2/A4/A6, `detectPitch` returns within ±5 cents of
55/440/1760 Hz with confidence > 0.5. On N163 A4 it returns a stable Hz (whatever the true value is)
across repeated renders — enough to assert the octave and, combined with F1, the exact tuning.

---

### F4 — Spectrogram PNG  *(TS)*

**Problem.** For timbre work (FM patch CCs, DMC playback, vibrato/tremolo, aliasing) there is no
register to read and no single scalar that captures "does it sound right". We need an **image** we
can inspect.

**Proposed API** (`cli/spectrogram.ts`):

```ts
export interface SpectrogramOpts {
  fftSize?: number;      // default 2048
  hopMs?: number;        // default ~10 ms
  sampleRate?: number;   // default 44100
  fmax?: number;         // top of the frequency axis, default 8000
  logFreq?: boolean;     // log frequency axis (default true — musically meaningful)
  db?: [number, number]; // dB range for the colormap, default [-90, 0]
  width?: number; height?: number;
}

/** Compute an STFT and encode it as a color-mapped PNG (magma-style). Returns PNG bytes. */
export function spectrogramPng(pcm: Float32Array, opts?: SpectrogramOpts): Uint8Array;
```

**Implementation notes.** STFT via F2, magnitude → dB → colormap → RGB buffer → PNG. Write the PNG in
pure TS using Node's built-in `zlib` for the IDAT deflate (a minimal PNG encoder is ~80 lines), OR
add a `backend.writePng(path, rgb, w, h)` that reuses the existing native RGB24 screenshot writer —
prefer the TS encoder to keep it dependency-light and platform-independent. Callers persist with
`backend.writeFile`. Label axes if cheap (optional). Also expose a raw
`stft(pcm, opts): { times, freqs, magDb }` for programmatic checks.

**Acceptance.** `spectrogramPng` of a VRC7 note produces a valid PNG showing a clear fundamental +
FM sidebands; sweeping a brightness CC visibly changes the harmonic content between two rendered
spectrograms.

---

### F5 — Timbre / quality metrics  *(TS)*

**Problem.** Some regressions are spectral but not pitch (a patch loses its harmonics, DMC gains
noise, a channel aliases). Need scalar metrics for assertions.

**Proposed API** (`cli/spectral-metrics.ts`):

```ts
export function spectralCentroid(x: Float32Array, sampleRate?: number): number;       // "brightness"
export function harmonicEnergy(x: Float32Array, f0: number, n?: number): number;      // sum of |H1..Hn|
export function thd(x: Float32Array, f0: number, n?: number): number;                 // total harmonic distortion
export function noiseFloorDb(x: Float32Array): number;                                // for DMC / hiss
export function bandEnergyDb(x: Float32Array, loHz: number, hiHz: number): number;
```

**Acceptance.** Raising VRC7 modulator level raises `harmonicEnergy`/`spectralCentroid`
monotonically; a clean DMC one-shot has a lower `noiseFloorDb` than a corrupted one.

---

### F6 — Register-write decode for expansion audio  *(TS on top of `drainEvents`)*

**Problem.** `drainEvents` already logs raw mapper register writes, but reconstructing "what
frequency/length/patch did the ROM program for this note" is manual. A decoder turns the raw log into
per-voice programmed values — the cleanest white-box tuning check, and the one that surfaces
write-path bugs the audio can hide (e.g. the N163 128-vs-32 wave-length byte).

**Proposed API** (`cli/reg-decode.ts`):

```ts
export interface N163Write { channel: number; freqReg: number; waveLen: number; waveAddr: number; volume: number; }
export interface Vrc7Write  { channel: number; fnum: number; block: number; key: boolean; inst: number; }
// ...VRC6 / S5B analogously...

/** Decode the last frame's expansion-audio register writes into structured per-voice programmings. */
export function decodeExpansionWrites(
  events: DebugEvent[],
  chip: "vrc6" | "vrc7" | "s5b" | "n163",
): { n163?: N163Write[]; vrc7?: Vrc7Write[]; /* ... */ };
```

**Implementation notes.** Filter `DebugEvent` by the chip's port addresses (N163 `$F800/$4800`,
VRC7 `$9010/$9030`, VRC6 `$9000+`, S5B `$C000/$E000`), reassemble multi-byte registers, and emit the
final programmed value per voice. Pairs naturally with F1: assert both "the ROM wrote `freqReg=967`"
(F6) and "the chip decoded `440 Hz`" (F1).

**Acceptance.** Playing N163 A4 yields `n163[0].freqReg === <expected LUT value>` and
`n163[0].waveLen === <expected samples>`; a wrong length byte or LUT value fails the assertion.

---

### F7 — High-level tuning assertions + golden-audio regression  *(TS)*

**Problem.** Tests should not each reinvent pitch detection. Provide ergonomic assertions and a
regression mechanism, built on F1–F6.

**Proposed API** (`cli/audio-assert.ts`):

```ts
/** Render one note on `variant`/`ch` and assert it plays `expectedHz` within `tolCents`.
 *  Uses F1 (decoded Hz) when available, else F3 (detectPitch); records which path it used. */
export function assertInTune(
  variant: string, ch: number, note: number, expectedHz: number,
  opts?: { tolCents?: number; vel?: number },
): void;

/** A compact, stable spectral fingerprint of a render (e.g. quantised band-energy vector) for
 *  golden regression: store once, diff on change, fail on drift beyond `tol`. */
export function spectralFingerprint(pcm: Float32Array): number[];
export function assertFingerprint(pcm: Float32Array, golden: number[], tol?: number): void;

/** Convenience: render a note straight to a WAV and/or spectrogram PNG for inspection/artifacts. */
export function renderNoteWav(variant: string, ch: number, note: number, path: string): void;
export function renderNoteSpectrogram(variant: string, ch: number, note: number, path: string): void;
```

**Acceptance.** A new `tests/tuning.test.ts` asserts A4≈440 (±~10 cents) on **every** channel of
every chip — 2A03 pulse/tri, VRC6 ×2, VRC7, S5B, N163, MMC5 — and fails on a systematic detune. This
is the test that was missing when N163 shipped sharp.

---

## 5. Suggested build order

1. **F2 (FFT core)** — foundation for F3/F4/F5.
2. **F1 (decoded Hz)** — highest single-feature leverage; unblocks precise tuning tests and settles
   the N163 wave-length question. Native change, so schedule early.
3. **F3 (pitch)** and **F6 (register decode)** — the two white/black-box pitch paths.
4. **F7 (assertions + golden)** — wire F1/F3/F6 into ergonomic tests; add `tests/tuning.test.ts`.
5. **F4 (spectrogram)** and **F5 (metrics)** — timbre coverage for the FM/DMC features to come.

## 6. Worked example — how this catches the N163 bug

With the tooling in place, the regression that shipped would fail at authoring time:

```ts
// tests/tuning.test.ts
test("n163: A4 is in tune", () => {
  // White-box: the chip's own decoded pitch (F1)
  const st = playAndSnapshot("n163", /*ch*/ 6, /*note*/ 69);   // getExpansionAudioState mid-note
  expect(Math.abs(centsError(st.channels[0].frequency, 440)) < 10).toBeTruthy();

  // Cross-check the write path (F6): the programmed register matches the LUT/formula
  const w = decodeExpansionWrites(events, "n163").n163![0];
  expect(w.freqReg).toBe(967);          // round(440 * 15*65536*4 / 1789773)
  expect(w.waveLen).toBe(32);           // would have caught the 0x80 → 128-sample length byte
});
```

The old table (`freqReg ≈ 973`, ~11 cents sharp) fails the register assertion; a wave-length /
octave error fails the decoded-Hz assertion. Both blind spots that let N163 ship broken are now
covered — entirely inside the CLI.
