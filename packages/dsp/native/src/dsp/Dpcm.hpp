#pragma once

// risa's NES DMC encoder, as a whole-buffer kernel (docs/modular-graph-phase-d2-design.md section 4).
//
// wav2dmc.py's pipeline, which is what the risa kit compiler runs through rp.dsp.dpcm: the float scaled to the int16
// domain and rounded, a one-pole DC blocker in double with its state carried from sample to sample, the 7-bit map into
// 0..127 around 64 (peak-normalised to a 45 swing, or scaled by 63 / 32768), and the 1-bit delta that walks a counter
// between 0 and 127 two steps at a time. The delta bits are the encoder's output: one per sample, in stream order. The
// caller packs them least-significant bit first and pads the result to a legal DMC length, because those two are the
// risa bank's format rather than the encoder's arithmetic (packages/dsp/ts/pack.ts).
//
// One call over a whole buffer, with no scratch of its own: the DC blocker's two doubles and a running peak are the
// whole state, and the second pass recomputes the first pass's samples rather than keeping one int per sample, so a
// caller that sizes `bits` from the frame count gets an encoder that allocates nothing. rp.dsp.dpcm runs it at
// activate, on the control thread, into storage it owns for its lifetime; nothing here touches the heap.
//
// A different shape from the other kernels in src/dsp/: those stream a block at a time, because a plan feeds them
// blocks, where this one needs every sample before it writes its first bit (the peak normalises the whole buffer). A
// node that plays the bits out one block at a time runs the encoder once, ahead of the stream, which is the whole of
// why rp.dsp.dpcm declares `heap`.

#include <cstddef>
#include <cstdint>

namespace rp::dsp {

// Writes `frames` bytes, one per sample, into caller-owned `bits`: 1 where the delta stepped the counter up, 0 where
// it stepped it down. `in` and `bits` must not overlap. The arithmetic is risa's retired whole-buffer encoder
// function's, expression for expression, and `[dsp][dpcm]` holds it to a verbatim copy of that function, so the bits
// are the same on every host. The two-pass shape is Dpcm.cpp's note.
//
// `normalize` is risa's per-sample flag: on, the DC-blocked buffer's peak (never below 1) divides the 7-bit map's
// swing, so the loudest sample lands near full scale; off, the map is a plain 63 / 32768 scale, and the peak is never
// measured.
//
// A NaN sample is outside the contract: the kit compile refuses a source or pass that carries one, and a live node's
// buffer is not checked, so a NaN there is an unspecified bit stream rather than a defined clamp. An infinity is
// clamped like any value out of the int16 range, and encodes at a rail.
void encodeDpcm(const float* in, std::size_t frames, bool normalize, std::uint8_t* bits) noexcept;

} // namespace rp::dsp
