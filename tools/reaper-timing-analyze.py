#!/usr/bin/env python3
"""
Measure Arduinoboy startup-sync latency from a stereo WAV produced by
the reaper-lsdj-arduinoboy-metro test.

Left channel  = LSDj output (panned hard-left in the .RPP)
Right channel = ReaSynth click track, one note per quarter beat at
                Reaper's transport BPM

What the test measures:

  When Reaper's transport starts at t=0, a NoteOn 24 hits LsdjSyncRole
  ("Arduinoboy play-enable"). The role starts emitting 0xFA + 0xF8
  bytes from PpqUtil::eachTick() on the next audio block. LSDj
  receives them via the GB serial-in path and starts advancing rows.
  At the same t=0 the ReaSynth click fires its first note. Comparing
  *when* LSDj produces its first audible sample against *when* the
  click[0] fires gives us the round-trip latency:

      Reaper transport start -> Note 24 enqueue -> serial bytes
      -> LSDj's row advance -> first audible sample

  Reasonable tolerance: ~50 ms (one 24 PPQN tick at 120 BPM is 21 ms,
  plus a few ms of envelope attack, plus one block of plugin
  block-quantization at 1024 samples / 44.1 kHz = 23 ms).

What the test does NOT measure (and would need an envelope-edit in
the LSDj setup to capture):

  Ongoing per-beat sync drift. The default LSDj instrument 00 has
  LENGTH=UNLIM, so the first note sustains across all 16 rows of the
  phrase and we can't detect the subsequent retriggers. Configuring
  a short ENV release or a finite LENGTH would expose per-beat onsets;
  if you need that, edit the bootstrap script.
"""
import sys
import wave
import os

try:
    import numpy as np
except ImportError:
    venv_python = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), ".venv", "bin", "python3"
    )
    if os.path.exists(venv_python) and sys.executable != venv_python:
        os.execv(venv_python, [venv_python, *sys.argv])
    raise SystemExit("numpy not available (run tools/lsdj-manual-setup.sh "
                     "or `pip install numpy` first)")

TOLERANCE_MS    = 50.0   # see module docstring
MIN_SPACING_MS  = 100.0  # smaller than the smallest expected gap
THRESHOLD_FRAC  = 0.20
ENV_SMOOTH_MS   = 30.0


def load_stereo(path):
    with wave.open(path, "rb") as w:
        if w.getnchannels() != 2:
            raise SystemExit(f"{path}: expected stereo, got {w.getnchannels()} ch")
        sr = w.getframerate()
        sw = w.getsampwidth()
        n  = w.getnframes()
        raw = w.readframes(n)
    if sw == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        a = (b[:, 0].astype(np.int32)
             | (b[:, 1].astype(np.int32) << 8)
             | (b[:, 2].astype(np.int32) << 16))
        a = np.where(a & 0x800000, a - 0x1000000, a).astype(np.float32) / (2 ** 23)
    elif sw == 4:
        a = np.frombuffer(raw, dtype="<i4").astype(np.float32) / (2 ** 31)
    else:
        raise SystemExit(f"{path}: unsupported sample width {sw}")
    a = a.reshape(-1, 2)
    return sr, a[:, 0], a[:, 1]


def find_onsets(samples, sr):
    """Rising-edge crossings of a smoothed envelope above 20% of peak.
    Adjacent edges within MIN_SPACING_MS collapse to the first one."""
    env = np.abs(samples)
    window = max(1, int(sr * ENV_SMOOTH_MS / 1000.0))
    kernel = np.ones(window, dtype=np.float32) / window
    env = np.convolve(env, kernel, mode="same")

    if env.max() < 1e-6:
        return np.array([], dtype=np.int64)

    threshold = THRESHOLD_FRAC * env.max()
    above = env > threshold
    # Prepend False so a signal that *starts* above threshold registers
    # an edge at index 0 (e.g. ReaSynth click at t=0).
    diffs = np.diff(np.concatenate(([False], above)).astype(np.int8))
    edges = np.where(diffs == 1)[0]

    min_spacing = int(sr * MIN_SPACING_MS / 1000.0)
    if len(edges) == 0:
        return edges
    kept = [edges[0]]
    for e in edges[1:]:
        if e - kept[-1] >= min_spacing:
            kept.append(e)
    return np.array(kept, dtype=np.int64)


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} STEREO.wav", file=sys.stderr)
        return 2
    path = sys.argv[1]
    sr, left, right = load_stereo(path)
    n = len(left)
    print(f"file: {path}")
    print(f"sample rate: {sr} Hz, duration: {n/sr:.2f}s")

    lsdj_onsets  = find_onsets(left,  sr)
    click_onsets = find_onsets(right, sr)
    print(f"LSDj (L) onsets:  {len(lsdj_onsets)}")
    print(f"Click (R) onsets: {len(click_onsets)}")

    if len(click_onsets) == 0:
        print("ERROR: no click events detected — was the .RPP authored "
              "without the Click track / ReaSynth?", file=sys.stderr)
        return 1
    if len(lsdj_onsets) == 0:
        print("ERROR: no LSDj audio detected — check the .rplg autoload "
              "and the Note 24 (Arduinoboy play-enable) MIDI item",
              file=sys.stderr)
        return 1

    click_ms = click_onsets * 1000.0 / sr
    lsdj_ms  = lsdj_onsets  * 1000.0 / sr
    print()
    print(f"Click[0] @ {click_ms[0]:7.2f} ms   (host transport start)")
    print(f"Click[1] @ {click_ms[1]:7.2f} ms   (next quarter beat)")
    print(f"LSDj[0]  @ {lsdj_ms[0]:7.2f} ms   (first audible LSDj sample)")

    # Startup sync latency: LSDj first onset relative to host t=0
    # (which is where click[0] fires). Negative would mean LSDj
    # somehow led the host — should never happen.
    latency_ms = lsdj_ms[0] - click_ms[0]
    print()
    print(f"startup latency:  {latency_ms:+.2f} ms")
    print(f"tolerance window: +/- {TOLERANCE_MS:.0f} ms")
    if abs(latency_ms) > TOLERANCE_MS:
        print()
        print("FAIL: Arduinoboy slave is slipping its startup window")
        print("  expected:  LSDj's first row note within ~50 ms of host t=0")
        print("  observed:  {:+.0f} ms drift".format(latency_ms))
        print("  possible:  PpqUtil tick alignment, LsdjSyncRole start-byte"
              " timing, or LSDj's own boot-into-play latency")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
