// risa's host sync (the `risa-sync` role's DSP behaviour, dspRoles.ts, as a script node): the DAW transport into
// risa's N8-FIFO receive path, a raw byte protocol that reuses MIDI status values (risaSync.ts). On a transport rise,
// or a ppqStart discontinuity while playing (a seek or a loop), a fresh 5-byte arm+locate computed from ppqStart, which
// flushes the FIFO, then START; on a fall, STOP; while playing, 24-PPQN clocks at their own frames (the FIFO has no
// serial gate). The arm points the clock at the clock after the one it names, which risa primes itself.
//
// The role's glue, line for line, with its state and its system's tick counter on the instance. A block allocates
// nothing but an arm's packet, as the role's did.
import { bytes, defineNode, type Block } from "../../graph/defineNode";
import {
  RISA_CLOCK,
  RISA_PPQN,
  RISA_SEEK_TOLERANCE,
  RISA_START,
  RISA_STOP,
  risaArmPacket,
  risaLocate,
} from "../../risaSync";
import { TickClock } from "./clock";
import { CORE_BYTES, FLUSH } from "./ports";

export const RISA_SYNC_NODE_TYPE = "rp.risa.sync";

const START = new Uint8Array([RISA_START]);
const STOP = new Uint8Array([RISA_STOP]);
const CLOCK = new Uint8Array([RISA_CLOCK]);

export const risaSyncNode = defineNode({
  id: RISA_SYNC_NODE_TYPE,
  name: "risa Host Sync",
  category: "protocol",
  reshapes: true,
  ports: { bytes: bytes.out(CORE_BYTES) },
  create: () => {
    const clock = new TickClock();
    let prevT = false;
    let ppqEnd: number | undefined; // where this block should start if it follows the last: undefined while stopped
    let block: Block | null = null;
    const tick = (_t: number, off: number) => {
      block!.send("bytes", off, CLOCK);
    };
    return {
      process(b) {
        block = b;
        clock.begin(b);
        const t = clock.block;
        const playing = t.transport;

        // Re-arm on a fresh start, or on a ppqStart discontinuity while playing: a seek or loop needs a new locate,
        // and the arm gates risa's playback and discards the old position's queued clocks.
        const seek = ppqEnd !== undefined && Math.abs(t.ppqStart - ppqEnd) > RISA_SEEK_TOLERANCE;
        if (playing && (!prevT || seek)) {
          const loc = risaLocate(t.ppqStart);
          b.send("bytes", 0, risaArmPacket(loc), FLUSH); // F9 52 songRow chainRow tickOffset, flushing the FIFO
          b.send("bytes", 0, START);
          clock.next = loc.absoluteClock + 1; // risa primes the armed clock itself; we resume after it
        } else if (!playing && prevT) {
          b.send("bytes", 0, STOP);
        }

        if (playing) clock.walk(RISA_PPQN, tick); // F8 at 24 PPQN

        // Predict the next contiguous block's ppqStart for seek detection (the quarters this block spans).
        prevT = playing;
        if (playing) {
          const samplesPerQuarter = (t.sampleRate * 60) / t.tempo;
          ppqEnd = t.ppqStart + (samplesPerQuarter > 0 ? t.frames / samplesPerQuarter : 0);
        } else {
          ppqEnd = undefined;
        }
      },
    };
  },
});
