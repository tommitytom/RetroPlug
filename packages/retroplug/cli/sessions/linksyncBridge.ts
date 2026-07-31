// LinkSyncBridge — the pure, headless core of the `linksync` host bridge.
//
// It turns transport (tempo + playing + PPQ position) into the exact LSDj serial byte stream that a
// SYNC=MIDI / SYNC=LSDj cart consumes, reusing walkTicks (the same drift-exact 24-PPQN clock the DSP
// kernel's `lsdj-sync` role uses — src/ppqClock.ts). Because the clock is the same code, the bytes this
// emits match RetroPlug's in-plugin sync by construction (that's the cross-repo golden vector).
//
// This core is I/O-free and deterministic: feed it blocks, get back the bytes to inject. The `linksync`
// CLI tool (linksync.ts) wraps it with a tempo source (Ableton Link / fixed BPM) and a serial sink that
// ships the bytes to Chromatic hardware over USB CDC.
//
// Mirrors packages/retroplug/src/dspRoles.ts `lsdj-sync` / `arduinoboy`.

import { walkTicks, type TickBlock } from "../../src/ppqClock.ts";

// LSDj serial bytes (== dspRoles.ts).
export const LSDJ_CLOCK = 0xf8; // 24-PPQN MIDI clock tick
export const LSDJ_START = 0xfa; // transport start (Arduinoboy bookend)
export const LSDJ_STOP = 0xfc; // transport stop

// Numeric LsdjSyncMode (0..8), matching settingsEnums.ts ordering. The `mode` byte sent to the FPGA.
export const LsdjSyncModeNum = {
  Off: 0,
  MidiSync: 1,
  MidiSyncArduinoboy: 2,
  MidiMap: 3,
  Keyboard: 4,
  KeyboardMidi: 5,
  MidiPassthrough: 6,
  MidiOut: 7,
  MasterSync: 8,
} as const;

export interface LinkSyncConfig {
  mode: number; // one of LsdjSyncModeNum
  tempoDivisor: number; // 1/2/4/8 — the LSDj clock is 24/divisor PPQN
  autoStart: boolean; // press Start on the transport rising edge (SYNC=MIDI auto-arm)
}

export interface SyncEvent {
  off: number; // sample offset within the block
  byte: number; // the LSDj/MIDI serial byte to inject
}

export interface BlockResult {
  events: SyncEvent[]; // serial bytes to inject this block, in order
  pressStart: boolean; // autoStart wants Start tapped this block (a GB button, not a serial byte)
}

/**
 * Stateful across blocks (holds the drift-exact nextTick + transport edge), exactly like the DSP role's
 * per-system state. One bridge per target console.
 */
export class LinkSyncBridge {
  private nextTick = 0;
  private prevTransport = false;

  reset(): void {
    this.nextTick = 0;
    this.prevTransport = false;
  }

  processBlock(block: TickBlock, cfg: LinkSyncConfig): BlockResult {
    const events: SyncEvent[] = [];
    let pressStart = false;
    const divisor = cfg.tempoDivisor > 0 ? cfg.tempoDivisor : 1;

    switch (cfg.mode) {
      case LsdjSyncModeNum.MidiSync: {
        // Bare 24/divisor-PPQN 0xF8 stream while the transport runs (no 0xFA/0xFC). autoStart taps Start
        // on the transport rise so a SYNC=MIDI cart auto-arms for a headless start.
        if (cfg.autoStart && block.transport && !this.prevTransport) pressStart = true;
        this.nextTick = walkTicks(block, 24 / divisor, this.nextTick, (_t, off) =>
          events.push({ off, byte: LSDJ_CLOCK }),
        );
        break;
      }
      case LsdjSyncModeNum.MidiSyncArduinoboy: {
        // 0xFA/0xFC bookend the transport edges; the 0xF8 clock flows while the transport runs.
        if (block.transport !== this.prevTransport) {
          events.push({ off: 0, byte: block.transport ? LSDJ_START : LSDJ_STOP });
        }
        this.nextTick = walkTicks(block, 24 / divisor, this.nextTick, (_t, off) =>
          events.push({ off, byte: LSDJ_CLOCK }),
        );
        break;
      }
      // MidiMap / MidiPassthrough / MidiOut / MasterSync / Keyboard: driven by MIDI/keys/serial-out, not by
      // Link transport — not produced by this bridge (Off emits nothing). See dspRoles.ts.
      default:
        break;
    }

    this.prevTransport = block.transport;
    return { events, pressStart };
  }
}
