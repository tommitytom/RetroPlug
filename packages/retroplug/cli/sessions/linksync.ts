// The `linksync` CLI tool — RetroPlug as the host-side LSDj sync bridge for Chromatic hardware.
//
//   retroplug-cli linksync [--bpm n] [--divisor n] [--mode name] [--duration t]
//                          [--block-ms n] [--auto-start] [--out file]
//
// It runs the SAME lsdj-sync clock the plugin uses (LinkSyncBridge → walkTicks) and emits the resulting
// LSDj serial bytes as `rpsync <mode> <byte...>` command lines — the exact commands the Chromatic MCU
// firmware consumes to inject onto the Game Boy link (see the FPGA repo docs/retroplug-port + the MCU
// repo docs/retroplug-sync.md). Pipe the output to the console device to drive real hardware:
//
//   retroplug-cli linksync --bpm 120 --duration 4s --out sync.txt   # then: cat sync.txt > /dev/ttyACM0
//
// Tempo source: for this PoC the tempo is a fixed --bpm (deterministic, testable). A LIVE Ableton Link
// source that streams to /dev/ttyACM0 in real time needs native serial + Link-SDK adapters that the
// txiki CLI runtime doesn't have — documented as future work (docs/retroplug-port/ableton-link.md).

import type { CliTool } from "../tools";
import type { Session } from "../session";
import { LinkSyncBridge, LsdjSyncModeNum } from "./linksyncBridge.ts";

const MODE_BY_NAME: Record<string, number> = {
  off: LsdjSyncModeNum.Off,
  midisync: LsdjSyncModeNum.MidiSync,
  arduinoboy: LsdjSyncModeNum.MidiSyncArduinoboy,
  midisyncarduinoboy: LsdjSyncModeNum.MidiSyncArduinoboy,
};

export interface LinkSyncOpts {
  bpm: number;
  divisor: number;
  mode: number;
  durationMs: number;
  blockMs: number;
  autoStart: boolean;
  sampleRate: number;
  out?: string;
}

const DEFAULTS: LinkSyncOpts = {
  bpm: 120,
  divisor: 1,
  mode: LsdjSyncModeNum.MidiSync,
  durationMs: 4000,
  blockMs: 20,
  autoStart: false,
  sampleRate: 44100,
};

/** "1500ms" | "2s" | "500" → milliseconds. */
function parseTime(v: string): number {
  const m = /^(\d+(?:\.\d+)?)(ms|s)?$/.exec(v.trim());
  if (!m) throw new Error(`linksync: bad time '${v}'`);
  const n = parseFloat(m[1]);
  return m[2] === "s" ? Math.round(n * 1000) : Math.round(n);
}

export function parseLinkSyncArgs(args: string[]): LinkSyncOpts {
  const o: LinkSyncOpts = { ...DEFAULTS };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    const next = () => {
      if (i + 1 >= args.length) throw new Error(`linksync: ${a} needs a value`);
      return args[++i];
    };
    switch (a) {
      case "--bpm": o.bpm = parseFloat(next()); break;
      case "--divisor": o.divisor = parseInt(next(), 10); break;
      case "--mode": {
        const name = next().toLowerCase();
        if (!(name in MODE_BY_NAME)) throw new Error(`linksync: unknown --mode '${name}'`);
        o.mode = MODE_BY_NAME[name];
        break;
      }
      case "--duration": o.durationMs = parseTime(next()); break;
      case "--block-ms": o.blockMs = parseInt(next(), 10); break;
      case "--auto-start": o.autoStart = true; break;
      case "--sample-rate": o.sampleRate = parseInt(next(), 10); break;
      case "--out": o.out = next(); break;
      default: throw new Error(`linksync: unknown arg '${a}'`);
    }
  }
  if (o.bpm <= 0) throw new Error("linksync: --bpm must be > 0");
  return o;
}

/** Format one FPGA sync command line: `rpsync <mode_hex> <byte_hex ...>` (matches the MCU console command). */
export function formatRpsync(mode: number, bytes: number[]): string {
  const hex = (n: number) => n.toString(16);
  return `rpsync ${hex(mode)} ${bytes.map(hex).join(" ")}`;
}

/**
 * Generate the full `rpsync` command script for a fixed-tempo run — pure and deterministic, so it is the
 * golden vector for the hardware sync stream. Walks the timeline in `blockMs` blocks (as the DSP kernel
 * would), advancing PPQ by tempo, and emits one command line per block that produced bytes.
 */
export function generateSyncScript(o: LinkSyncOpts): string[] {
  const bridge = new LinkSyncBridge();
  const framesPerBlock = Math.max(1, Math.round((o.sampleRate * o.blockMs) / 1000));
  const beatsPerBlock = framesPerBlock / ((o.sampleRate * 60) / o.bpm);
  const totalBlocks = Math.ceil((o.durationMs / 1000) * (o.sampleRate / framesPerBlock));

  const lines: string[] = [];
  let ppq = 0;
  for (let i = 0; i < totalBlocks; i++) {
    const block = {
      frames: framesPerBlock,
      sampleRate: o.sampleRate,
      tempo: o.bpm,
      ppqStart: ppq,
      transport: true,
    };
    const { events, pressStart } = bridge.processBlock(block, {
      mode: o.mode,
      tempoDivisor: o.divisor,
      autoStart: o.autoStart,
    });
    if (pressStart) lines.push("poke 8"); // GB Start bit (kButton_Start) — the MCU 'poke' command
    if (events.length > 0) lines.push(formatRpsync(o.mode, events.map((e) => e.byte)));
    ppq += beatsPerBlock;
  }
  return lines;
}

function runLinkSync(s: Session, args: string[]): void {
  const o = parseLinkSyncArgs(args);
  const lines = generateSyncScript(o);
  const text = lines.join("\n") + (lines.length ? "\n" : "");

  if (o.out) {
    s.backend.writeFile(o.out, new TextEncoder().encode(text));
    console.log(`linksync: wrote ${lines.length} command lines → ${o.out}`);
    console.log(`  send to hardware: cat ${o.out} > /dev/ttyACM0`);
  } else {
    process.stdout ? process.stdout.write(text) : console.log(text);
  }
}

const LINKSYNC_HELP = `retroplug-cli linksync — generate an LSDj sync command stream for Chromatic hardware

usage: retroplug-cli linksync [options]

  --bpm <n>          tempo (default 120)
  --divisor <n>      LSDj clock divisor 1/2/4/8 (default 1)
  --mode <name>      midiSync | arduinoboy (default midiSync)
  --duration <t>     length, e.g. 4s / 2000ms (default 4s)
  --block-ms <n>     block size in ms (default 20)
  --auto-start       tap Start on the transport rise (emits a 'poke' line)
  --sample-rate <hz> timeline sample rate (default 44100)
  --out <file>       write command lines to a file (else stdout)

Emits 'rpsync <mode> <byte...>' lines (and 'poke' for Start), the exact commands the
Chromatic MCU firmware consumes. Send them to the device console, e.g.:

  retroplug-cli linksync --bpm 120 --duration 4s --out sync.txt
  cat sync.txt > /dev/ttyACM0

The clock is the same walkTicks the plugin's lsdj-sync role uses, so the hardware stream
matches an in-plugin render by construction. A live Ableton Link → serial daemon needs
native adapters (see docs/retroplug-port/ableton-link.md).`;

export const linksyncTool: CliTool = {
  name: "linksync",
  summary: "generate an LSDj sync command stream for Chromatic hardware",
  help: LINKSYNC_HELP,
  run: runLinkSync,
};
