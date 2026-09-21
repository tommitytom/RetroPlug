#!/usr/bin/env python3
"""Assert a rendered WAV carries real signal rather than silence.

The reaper render legs that have a timing analyzer get their proof from it. mgb-smoke has none, so
without this its whole assertion was "reaper exited 0" — which a silent render passes, and which the
notes record actually happening: renders went silent for weeks behind a stale VST3 class id in the
.rpp fixtures, and nothing failed.

Threshold is a FRACTION of full scale, not an absolute sample value. The previous inline version
compared a raw peak against 1000, which means 0.012% of full scale in a 24-bit file and 3% in a
16-bit one — a 250x difference in strictness depending on a render setting nobody was thinking about
when they picked the number. -80 dBFS says the same thing at every width.
"""
import sys
import wave

FLOOR = 1e-4  # -80 dBFS: comfortably above a dithered noise floor, far below any real music

def peak_fraction(path: str) -> tuple[float, int, int]:
    with wave.open(path, "rb") as w:
        width = w.getsampwidth()
        frames = w.getnframes()
        data = w.readframes(min(frames, 400_000))
    if width == 3:
        # audioop cannot do 24-bit, which is what these renders are.
        peak = max(
            (abs(int.from_bytes(data[i : i + 3], "little", signed=True)) for i in range(0, len(data) - 2, 3)),
            default=0,
        )
    else:
        import audioop  # noqa: PLC0415 - only needed for the widths audioop handles

        peak = audioop.max(data, width)
    full = float(1 << (8 * width - 1))
    return peak / full, frames, width

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: assert-nonsilent.py <file.wav>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    try:
        frac, frames, width = peak_fraction(path)
    except FileNotFoundError:
        print(f"error: {path} was not rendered", file=sys.stderr)
        return 1
    except wave.Error as e:
        print(f"error: {path} is not a readable WAV: {e}", file=sys.stderr)
        return 1
    db = 20 * __import__("math").log10(frac) if frac > 0 else float("-inf")
    print(f"{path}: frames={frames} sampwidth={width} peak={frac:.6f} ({db:.1f} dBFS)")
    if frac > FLOOR:
        return 0
    print(f"error: {path} is silent (peak {db:.1f} dBFS, floor {20 * __import__('math').log10(FLOOR):.0f} dBFS)", file=sys.stderr)
    return 1

if __name__ == "__main__":
    sys.exit(main())
