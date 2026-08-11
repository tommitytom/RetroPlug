// MIDI clock/transport -> risa host-sync bytes. A pure, I/O-free translator that turns an incoming MIDI
// transport stream (SPP / Start / Continue / Stop / Clock) into risa's arm/clock/stop protocol. It reuses
// the risaLocate/risaArmPacket primitives from ../risaSync (the single source of truth) - this replaces the
// C++ RisaSyncTranslator, which was a hand-port of the same logic. Feed one MIDI message at a time via
// onMessage(); it appends any risa output bytes. Cross-checked against the golden role test
// (test/dsp/risa-sync.test.ts) and the former native gtest.

import { RISA_START, RISA_CLOCK, RISA_STOP, risaLocate, risaArmPacket } from "../risaSync";

const MIDI_SPP = 0xf2;
const MIDI_CLOCK = 0xf8;
const MIDI_START = 0xfa;
const MIDI_CONTINUE = 0xfb;
const MIDI_STOP = 0xfc;

export class RisaSyncTranslator {
  private absoluteClock = 0; // 24-PPQN clock since song start; set by SPP, advanced per F8
  private playingFlag = false;
  private suppressNextClock = false; // skip one F8 after a start (risa primes the armed clock itself)

  /** The 5-byte arm/locate packet for a 24-PPQN clock: F9 52 songRow chainRow tick. */
  static armPacket(absoluteClock: number): number[] {
    return risaArmPacket(risaLocate(absoluteClock / 24)); // risaLocate re-multiplies by 24 -> the same clock
  }

  playing(): boolean {
    return this.playingFlag;
  }
  getAbsoluteClock(): number {
    return this.absoluteClock;
  }

  /** Consume one MIDI message, appending any risa bytes to `out` (does NOT clear it). */
  onMessage(bytes: ArrayLike<number>, out: number[]): void {
    if (bytes.length === 0) return;
    switch (bytes[0]) {
      case MIDI_SPP: {
        if (bytes.length < 3) return;
        const pos16th = ((bytes[2] & 0x7f) << 7) | (bytes[1] & 0x7f);
        this.absoluteClock = pos16th * 6; // six 24-PPQN clocks per sixteenth; emits nothing
        return;
      }
      case MIDI_START:
        this.absoluteClock = 0; // always from the top
        this.arm(out);
        return;
      case MIDI_CONTINUE:
        this.arm(out); // arm at the current position (last SPP, or where Stop left it)
        return;
      case MIDI_STOP:
        out.push(RISA_STOP);
        this.playingFlag = false;
        this.suppressNextClock = false; // keep absoluteClock so a later Continue resumes in place
        return;
      case MIDI_CLOCK:
        if (!this.playingFlag) return; // transport-gated
        this.absoluteClock++; // advance FIRST so a later re-arm stays aligned
        if (this.suppressNextClock) {
          this.suppressNextClock = false;
          return; // the armed clock is primed by risa - drop this one
        }
        out.push(RISA_CLOCK);
        return;
      default:
        return; // notes / CC / sysex / other real-time ignored
    }
  }

  private arm(out: number[]): void {
    for (const b of RisaSyncTranslator.armPacket(this.absoluteClock)) out.push(b);
    out.push(RISA_START);
    this.playingFlag = true;
    this.suppressNextClock = true;
  }
}
