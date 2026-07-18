#!/usr/bin/env python3
"""
Measure LSDj sync timing against a reference click from a stereo WAV.

Left channel  = LSDj output (panned hard-left in the .RPP)
Right channel = ReaSynth click track, one note per quarter beat at
                Reaper's transport BPM

Two modes:

  (default)  startup latency  — reaper-lsdj-{arduinoboy,midi}-metro
             Compares LSDj's first onset to the click's first onset
             (a single number). See the long note below.

  --drift    per-beat drift over time — reaper-lsdj-midi-drift
             Pairs every LSDj noise click to its reference beat and
             reports how the offset evolves over a long (e.g. 1 h) render:
             mean / median / max-abs / stddev, a per-minute trend table,
             and a linear accumulation slope (ms drift per minute). Fails
             if max-abs drift exceeds tolerance or too many beats go
             unmatched. Reads the (large) WAV in chunks and works on a
             decimated envelope so memory stays modest.

What the startup-latency mode measures:

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

# --drift mode tuning
DRIFT_TOLERANCE_MS    = 50.0    # max abs per-beat drift before FAIL
DRIFT_ENV_RATE_HZ     = 4000    # decimated-envelope rate (0.25 ms resolution)
DRIFT_MAX_UNMATCHED   = 0.01    # >1% unmatched beats -> FAIL (rate mismatch / dropouts)
DRIFT_READ_CHUNK      = 1 << 20 # frames per WAV read (~4 MB/ch at 16-bit)

# --midi-timing mode defaults (overridable on the CLI). Proves host MIDI-in keeps its intra-block
# sample offset: two mGB notes in one large render block should land ~MT_GAP_MS apart (collapse to
# frame 0 would merge them), and the late one should align with a coincident ReaSynth click.
MT_FROM_MS = 2000.0   # ignore everything before this (the mGB DMG-boot burst); notes are placed ~3 s in
MT_GAP_MS  = 136.05   # authored spacing of the two mGB notes (6000 samples @ 44.1 kHz)
MT_TOL_MS  = 25.0     # pass window: << the ~136/158 ms collapse signal, >> the ~2 ms detector error


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


def decimated_envelopes(path):
    """Stream the stereo WAV once and return (env_rate, env_left, env_right):
    abs-peak envelopes block-reduced to ~DRIFT_ENV_RATE_HZ. Peak-hold over each
    block preserves transient onsets while keeping memory tiny (a 1 h file
    collapses to a few tens of MB instead of gigabytes)."""
    with wave.open(path, "rb") as w:
        if w.getnchannels() != 2:
            raise SystemExit(f"{path}: expected stereo, got {w.getnchannels()} ch")
        sr = w.getframerate()
        sw = w.getsampwidth()
        nframes = w.getnframes()
        dec = max(1, sr // DRIFT_ENV_RATE_HZ)
        env_rate = sr / dec

        def to_float(raw):
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
            return a.reshape(-1, 2)

        env_l, env_r = [], []
        carry = np.empty((0, 2), dtype=np.float32)
        while True:
            raw = w.readframes(DRIFT_READ_CHUNK)
            if not raw:
                break
            block = np.abs(to_float(raw))
            block = np.concatenate([carry, block]) if carry.size else block
            usable = (len(block) // dec) * dec
            carry = block[usable:]
            if usable:
                red = block[:usable].reshape(-1, dec, 2).max(axis=1)
                env_l.append(red[:, 0].copy())
                env_r.append(red[:, 1].copy())
        if carry.size:  # final partial block -> one more envelope sample
            env_l.append(np.array([carry[:, 0].max()], dtype=np.float32))
            env_r.append(np.array([carry[:, 1].max()], dtype=np.float32))

    el = np.concatenate(env_l) if env_l else np.zeros(0, np.float32)
    er = np.concatenate(env_r) if env_r else np.zeros(0, np.float32)
    return env_rate, el, er


def onsets_from_envelope(env, env_rate):
    """Rising-edge onsets on an already-computed abs envelope (see find_onsets)."""
    window = max(1, int(env_rate * ENV_SMOOTH_MS / 1000.0))
    kernel = np.ones(window, dtype=np.float32) / window
    sm = np.convolve(env, kernel, mode="same")
    if sm.max() < 1e-6:
        return np.array([], dtype=np.float64)
    above = sm > THRESHOLD_FRAC * sm.max()
    diffs = np.diff(np.concatenate(([False], above)).astype(np.int8))
    edges = np.where(diffs == 1)[0]
    if len(edges) == 0:
        return np.array([], dtype=np.float64)
    min_spacing = int(env_rate * MIN_SPACING_MS / 1000.0)
    kept = [edges[0]]
    for e in edges[1:]:
        if e - kept[-1] >= min_spacing:
            kept.append(e)
    return np.array(kept, dtype=np.float64) / env_rate  # seconds


def analyze_drift(path):
    print(f"file: {path}")
    env_rate, env_l, env_r = decimated_envelopes(path)
    dur = len(env_l) / env_rate if env_rate else 0.0
    print(f"envelope rate: {env_rate:.1f} Hz, duration: {dur:.1f}s")

    lsdj  = onsets_from_envelope(env_l, env_rate)  # seconds
    click = onsets_from_envelope(env_r, env_rate)
    print(f"LSDj (L) onsets:  {len(lsdj)}")
    print(f"Click (R) onsets: {len(click)}")
    if len(click) < 2:
        print("ERROR: no/too-few click events — was the .RPP authored with the "
              "Click track / ReaSynth?", file=sys.stderr)
        return 1
    if len(lsdj) == 0:
        print("ERROR: no LSDj audio detected — check the .rplg autoload (SYNC=MIDI "
              "armed) and that the host transport plays from t=0", file=sys.stderr)
        return 1

    beat = float(np.median(np.diff(click)))   # reference beat interval (s)
    window = 0.5 * beat
    print(f"beat interval: {beat*1000:.2f} ms ({60.0/beat:.2f} BPM)")

    # Pair each click beat to the nearest LSDj onset within +/- half a beat.
    idx = np.searchsorted(lsdj, click)
    drift, matched_t, missed = [], [], 0
    for c, i in zip(click, idx):
        cands = []
        if i < len(lsdj):       cands.append(lsdj[i])
        if i > 0:               cands.append(lsdj[i - 1])
        best = min(cands, key=lambda x: abs(x - c)) if cands else None
        if best is None or abs(best - c) > window:
            missed += 1
        else:
            drift.append((best - c) * 1000.0)  # ms
            matched_t.append(c)
    drift = np.array(drift)
    matched_t = np.array(matched_t)
    # LSDj onsets that never paired to a beat (extra/spurious hits).
    extra = max(0, len(lsdj) - len(drift))

    unmatched_frac = missed / len(click)
    print()
    print(f"matched beats:    {len(drift)} / {len(click)}")
    print(f"missed beats:     {missed}  ({unmatched_frac*100:.2f}%)")
    print(f"extra LSDj hits:  {extra}")
    if len(drift) == 0:
        print("ERROR: no LSDj onset paired to a click beat — LSDj and the click "
              "fire at incompatible rates (check groove / step spacing in "
              "lsdj_midi_drift.test.ts)", file=sys.stderr)
        return 1

    print()
    print(f"drift  first:  {drift[0]:+.2f} ms")
    print(f"drift  mean:   {drift.mean():+.2f} ms")
    print(f"drift  median: {np.median(drift):+.2f} ms")
    print(f"drift  stddev: {drift.std():.2f} ms")
    print(f"drift  max|.|: {np.abs(drift).max():.2f} ms")

    # Accumulation: linear fit of drift vs time (ms per minute).
    if len(drift) >= 2:
        slope_per_s = np.polyfit(matched_t, drift, 1)[0]
        print(f"drift  slope:  {slope_per_s*60.0:+.3f} ms/min (accumulation)")

    # Per-minute mean-drift trend.
    print()
    print("per-minute mean drift (ms):")
    minute = (matched_t // 60).astype(int)
    for m in range(minute.max() + 1):
        sel = drift[minute == m]
        if len(sel):
            print(f"  min {m:>3}: {sel.mean():+7.2f}  (max|.| {np.abs(sel).max():6.2f}, n={len(sel)})")

    print()
    print(f"tolerance: max|drift| <= {DRIFT_TOLERANCE_MS:.0f} ms, "
          f"unmatched <= {DRIFT_MAX_UNMATCHED*100:.0f}%")
    fail = False
    if np.abs(drift).max() > DRIFT_TOLERANCE_MS:
        print(f"FAIL: peak drift {np.abs(drift).max():.2f} ms exceeds "
              f"{DRIFT_TOLERANCE_MS:.0f} ms")
        fail = True
    if unmatched_frac > DRIFT_MAX_UNMATCHED:
        print(f"FAIL: {unmatched_frac*100:.2f}% of beats unmatched — LSDj is "
              f"dropping clocks or running at the wrong rate")
        fail = True
    if fail:
        return 1
    print("PASS")
    return 0


def analyze_midi_timing(path, from_ms, gap_ms, tol_ms):
    """Intra-block MIDI-offset accuracy (reaper:mgb-midi-timing).

    L = mGB (two notes authored into ONE large render block, near its start and end).
    R = ReaSynth click, one note coincident with the LATE mGB note.

    Honoured  → L has two onsets ~gap_ms apart, and the late one aligns with the click.
    Collapsed → both mGB notes fire at the block start: one merged L onset, ~gap_ms BEFORE the click.
    """
    sr, left, right = load_stereo(path)
    # Blank the pre-window (the mGB boot burst) so only the two authored notes survive on L.
    guard = int(from_ms / 1000.0 * sr)
    left  = left.copy();  left[:guard]  = 0.0
    right = right.copy(); right[:guard] = 0.0

    mgb   = find_onsets(left,  sr)
    click = find_onsets(right, sr)
    mgb_ms   = mgb   * 1000.0 / sr
    click_ms = click * 1000.0 / sr
    print(f"file: {path}")
    print(f"sample rate: {sr} Hz, duration: {len(left)/sr:.2f}s, window: >{from_ms:.0f} ms")
    print(f"mGB (L) onsets:   {len(mgb)}  {[round(x, 1) for x in mgb_ms.tolist()]}")
    print(f"Click (R) onsets: {len(click)}  {[round(x, 1) for x in click_ms.tolist()]}")

    ok = True
    if len(mgb) != 2:
        print()
        print(f"FAIL: expected exactly 2 mGB onsets, saw {len(mgb)}")
        print("  a single merged onset is the frame-0-collapse signature (both notes at the block start);"
              " 0 onsets means mGB never sounded (autoload / boot-window placement).")
        return 1
    if len(click) < 1:
        print("\nFAIL: no ReaSynth click detected (Click track / ReaSynth missing?)", file=sys.stderr)
        return 1

    gap = float(mgb_ms[1] - mgb_ms[0])
    print()
    print(f"note spacing:     {gap:7.2f} ms   (authored {gap_ms:.2f} ms)")
    if abs(gap - gap_ms) > tol_ms:
        print(f"FAIL: mGB note spacing off by {gap - gap_ms:+.1f} ms (tol +/- {tol_ms:.0f})")
        print("  the two intra-block events did not land at their authored offsets.")
        ok = False

    ref = float(mgb_ms[1] - click_ms[-1])   # late mGB note vs the coincident click
    print(f"late note vs click: {ref:+6.2f} ms   (authored coincident)")
    if abs(ref) > tol_ms:
        print(f"FAIL: late mGB note is {ref:+.1f} ms from the click (tol +/- {tol_ms:.0f})")
        print("  its absolute position drifted from an independent plugin at the same instant.")
        ok = False

    print()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    flags = {"--drift", "--midi-timing"}
    argv = sys.argv[1:]
    from_ms, gap_ms, tol_ms = MT_FROM_MS, MT_GAP_MS, MT_TOL_MS
    positional = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--from-ms", "--gap-ms", "--tol-ms"):
            val = float(argv[i + 1]); i += 2
            if a == "--from-ms": from_ms = val
            elif a == "--gap-ms": gap_ms = val
            else: tol_ms = val
            continue
        if a not in flags:
            positional.append(a)
        i += 1
    if len(positional) != 1:
        print(f"usage: {sys.argv[0]} [--drift | --midi-timing [--from-ms N --gap-ms N --tol-ms N]] STEREO.wav",
              file=sys.stderr)
        return 2
    path = positional[0]
    if "--midi-timing" in argv:
        return analyze_midi_timing(path, from_ms, gap_ms, tol_ms)
    if "--drift" in argv:
        return analyze_drift(path)
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
