// The kit compilers' integer half (docs/modular-graph-phase-d2-design.md section 5): what TypeScript does to the
// buffers the offline jobs record, after native has done every float the bytes depend on. LSDj's levels become 4-bit
// codes, trimmed to whole wave frames and packed two to a byte (native KitUtil::compileSample's tail, SampleUtil's
// convertCodesToNibbles); risa's DPCM levels become the delta bits, packed and padded to a legal DMC length
// (RisaDmcCodec::encode's tail). Nothing here touches a float beyond reading which side of zero, or which level, a
// recorded sample is, so every step is exact by construction and the tests hold each to its native twin.
//
// The constants the chain needs to cut and cap a slot live here too, spelled as the native code spells them: the
// kit rates, the frame caps and the PAL DPCM rate table, whose literals are the native table's digit for digit.

/** LSDj's kit rate, the Game Boy's sample rate as the kit encoder resamples to (native kGameboySampleRate). */
export const LSDJ_KIT_RATE = 11468;

/** The 4-bit depth an LSDj kit sample is quantised to. */
export const LSDJ_KIT_BITS = 4;

/** The samples one wave frame holds, and the bytes it packs to (native SampleUtil). */
export const LSDJ_FRAME_SAMPLES = 32;
export const LSDJ_FRAME_BYTES = 16;

/** The most frames a kit can hold at the kit rate: its 0x3fa0 bytes of sample space, two 4-bit samples a byte
 *  (native Kit::kMaxSampleFrames). The window caps a slot's source to this many frames at the kit rate. */
export const LSDJ_MAX_SAMPLE_FRAMES = 0x3fa0 * 2;

/** risa's DMC byte limit: 4081 = 16 * 255 + 1, the longest legal $4013 length (native kDmcMaxBytes). */
export const DMC_MAX_BYTES = 4081;

/** A legal DMC length is 16k + 1 bytes (native kLengthStep). */
export const DMC_LENGTH_STEP = 16;

/** PAL DPCM playback rates (Hz), index 0..15, the literals native RisaDmcCodec.cpp's kPalRates spells (wav2dmc.py
 *  PAL_DPCM_RATES_HZ). A slot resamples to its index's rate, and its window's cap scales by it. */
export const PAL_DMC_RATES: readonly number[] = [
  4177.40, 4696.63, 5261.41, 5579.22, 6023.94, 7044.94, 7917.18, 8397.01,
  9446.63, 11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
];

/** The PAL DPCM rate a slot's index names: native masks the index to its low four bits (`rate & 0x0F`) at the RPC
 *  and again in the codec, so an index of 17 is rate 1, never an error. */
export function palDmcRate(index: number): number {
  return PAL_DMC_RATES[index & 0x0f];
}

/** A whole buffer's resampled length as the kit path cuts it (rp::dsp::resampledLength): `floor(frames * (outRate /
 *  inRate))`, the ratio taken first. JavaScript's numbers are the same doubles, so this is the native expression
 *  operation for operation: one division and one multiply, each correctly rounded, then a truncation (the operands
 *  are positive, so `Math.floor` is the cast). */
export function resampledLength(frames: number, inRate: number, outRate: number): number {
  return Math.floor(frames * (outRate / inRate));
}

/** `n` trimmed to whole 32-sample wave frames (native frameAlign): the packer writes whole frames only. */
export function trimToFrames(n: number): number {
  return n - (n % LSDJ_FRAME_SAMPLES);
}

/**
 * A 4-bit level back to its code. `rp.dsp.dither` outputs each code as the level the LSDj decoder gives it,
 * `float(code) / 15 * 2 - 1` in binary32 (rp::dsp::codeToLevel), so `(level + 1) * 0.5 * 15` lands within a few
 * float ulps of the code and `Math.round` recovers it: the rounding step is a whole unit and the error is about 1e-6,
 * so no level can round to its neighbour (the mock-tier case checks every one of the 16). A level the dither cannot
 * produce has no code; the clamp only keeps a stray one inside the nibble rather than wrapping it.
 */
export function levelToCode(level: number): number {
  const code = Math.round((level + 1) * 0.5 * 15);
  return code < 0 ? 0 : code > 15 ? 15 : code;
}

/**
 * Recorded 4-bit levels to the slot's packed nibbles: each level to its code, the run trimmed to whole wave frames,
 * then native convertCodesToNibbles - inverted (stored = 15 - code), and with `rotate` (LSDj 9.2.0+) each frame's
 * sample i at position (i + 1) % 32, so the frame's last sample opens it; then two samples a byte, high nibble first.
 */
export function packNibbles(levels: ArrayLike<number>, rotate: boolean): Uint8Array {
  const frames = trimToFrames(levels.length) / LSDJ_FRAME_SAMPLES;
  const out = new Uint8Array(frames * LSDJ_FRAME_BYTES);
  const frame = new Uint8Array(LSDJ_FRAME_SAMPLES);
  for (let f = 0; f < frames; f++) {
    const base = f * LSDJ_FRAME_SAMPLES;
    for (let i = 0; i < LSDJ_FRAME_SAMPLES; i++) {
      const at = rotate ? (i + 1) % LSDJ_FRAME_SAMPLES : i;
      frame[at] = 0xf - levelToCode(levels[base + i]);
    }
    for (let i = 0; i < LSDJ_FRAME_BYTES; i++) out[f * LSDJ_FRAME_BYTES + i] = (frame[i * 2] << 4) | frame[i * 2 + 1];
  }
  return out;
}

/**
 * Recorded DPCM levels to a slot's DMC bytes, as RisaDmcCodec::encode packs them: `rp.dsp.dpcm` writes each delta bit
 * as exactly +1 or -1, so `level > 0` is the bit; bits go least-significant first, eight to a byte; the stream is
 * padded with zero bytes to a legal 16k + 1 length (an empty one is the one-byte `{0}`, itself legal); and a stream
 * past the hardware's 4081 bytes is cut there, still a legal length.
 */
export function packDmc(levels: ArrayLike<number>): Uint8Array {
  const packedLength = Math.ceil(levels.length / 8);
  if (packedLength === 0) return Uint8Array.of(0);
  const rem = (packedLength - 1) % DMC_LENGTH_STEP;
  const padded = rem === 0 ? packedLength : packedLength + (DMC_LENGTH_STEP - rem);
  const out = new Uint8Array(Math.min(padded, DMC_MAX_BYTES));
  const bits = Math.min(levels.length, out.length * 8);
  for (let bit = 0; bit < bits; bit++) if (levels[bit] > 0) out[bit >> 3] |= 1 << (bit & 7);
  return out;
}
