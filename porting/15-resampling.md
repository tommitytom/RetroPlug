# Step 15 — Resampling

**Status:** Pending.

## Goal

Run the SameBoy emulator at its native APU rate (or a known-good fixed rate)
and resample to the host rate. Replaces the current
`GB_set_sample_rate(host_rate)` shortcut, which produces small but cumulative
timing drift — enough to make LSDJ MIDI sync wander on long sessions and
unacceptable at non-44.1 kHz host rates.

## Depends on

Nothing strict. Slot wherever convenient — but worth landing before any
recording-grade use of LSDJ sync (so practically: before step 09 completes
production-quality, or right after).

## Architecture introduced

- **r8brain integration.** The vendored r8brain at
  [old/thirdparty/r8brain/](../old/thirdparty/r8brain/) is a small,
  well-tested SRC. Move to `deps/r8brain/`, add a CMake target, link into the
  plugin (DSP-side only).
- **Per-system resampler.** `SameBoySystem` gets a `r8brain::CDSPProcessor`
  pair (one per channel, or one stereo). The emulator runs at a fixed
  internal rate (see decision below) and produces samples that flow through
  the resampler before landing in the stereo accumulator.
- **Internal rate decision.** Two natural choices:
  1. **Native APU rate**: GB's APU clocks at 4194304 Hz / 96 = 43690 Hz.
     Closest to the chip's actual sample rate; resampler converts to host.
  2. **Highest sane fixed rate**: e.g. 96 kHz. Always upsample then
     downsample; r8brain handles either direction. Reduces aliasing, costs
     more CPU.
  Recommend native APU rate — matches the hardware, smallest CPU cost.
- **Buffer management.** The existing per-block stereo accumulator is sized
  for `frames * 2`; with resampling, the emulator generates *more* samples
  per block (block frames × 43690/host_rate, roughly). Pre-size with headroom.
- **Sample-accurate timing for buttons.** The `pendingButtons_` queue uses
  sample offsets; those need to be in the *resampled* domain (host samples)
  but applied to the emulator's *native* domain. Compute the conversion.

## Tasks

1. Move r8brain → `deps/r8brain/` with a CMake target. PIC + matching
   warning suppressions.
2. Add a `Resampler` wrapper at `src/system/Resampler.{hpp,cpp}` that
   handles the stereo + state lifecycle.
3. Update `SameBoySystem`:
   - In `onSampleRateChanged`, set `GB_set_sample_rate(gb, 43690)`. Configure
     the resampler ratio = host_rate / 43690.
   - In `onProcess`, drive the emulator until the resampler has buffered
     enough output samples for the requested block size.
   - Convert button offsets from host to native domain on push.
4. Update the audio callback to write into a *native-rate* accumulator that
   feeds the resampler.
5. Tests: load a known LSDJ song with MIDI sync; play it for 10 minutes
   alongside a host clock; confirm LSDJ doesn't drift.

## Verification

- A/B test pre/post: same LSDJ ROM, same MIDI clock, listen for tempo drift
  over ~5 minutes.
- A 1 kHz square wave (or LSDJ pulse channel at fixed pitch): FFT should
  show no spurious sidebands from the SRC.
- Switch host rate: 44.1 → 48 → 96 kHz. Audio quality stays consistent;
  emulator timing stays consistent.

## Risks / open questions

- **Latency.** r8brain adds a fixed delay (typically 1-4 ms). Document; users
  who want zero-latency emulation can fall back to the no-resampling path
  via a config flag.
- **CPU cost.** A few percent per system at 96 kHz host rates. Profile
  before merging.
- **Resampler state on reset.** When `SystemBase::onReset` fires, drop the
  resampler's internal buffer to avoid replaying stale samples.
- **Multi-instance scaling.** N resamplers cost N×ratio CPU. Mesen (step 17)
  has the same shape.
