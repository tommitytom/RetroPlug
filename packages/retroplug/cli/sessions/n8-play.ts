// `retroplug-cli n8-play` - send a SCRIPTED MIDI sequence to a physical Everdrive N8 Pro's cart FIFO.
//
// The live twin of `n8-bridge`: where the bridge forwards a controller/DAW in real time, this plays a fixed
// sequence given on the command line, so a hardware check is one reproducible command with no MIDI hardware
// attached. The BlipToaster ROM reads these bytes at $40F0/$40F1. Pair it with a capture + `analyze-capture` to
// verify what the real chips actually did (see the nes-hardware-lab skill).
//
// Nothing here resets the cart between invocations: a note held by one call keeps sounding across the next,
// which is what lets a check be split across commands (hold a note with one call, push a SysEx with the
// next, capture the whole time).
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { createSerialClient } from "../../src/realBackend";
import { createN8, type SerialPortInfo } from "../../src/n8";
import { ADDR_EXP_VOL } from "../../src/n8/edio";

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

function pickPort(ports: SerialPortInfo[], explicit: string | undefined): string {
  if (explicit) return explicit;
  const n8 = ports.find((p) => p.isN8);
  if (!n8)
    throw new Error(
      "no Everdrive N8 Pro found (VID:PID 38df:0017). Plug it in, or pass --serial <port>.\n" +
        (ports.length ? `serial ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
    );
  return n8.port;
}

/** Blocking delay. The session runtime has no synchronous sleep, and a note hold has to span a real audio
 *  capture, so a hardware-test tool spins. Holds here are seconds at most. */
function spin(ms: number): void {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    /* wait */
  }
}

const num = (s: string, what: string, max: number): number => {
  const v = Number(s);
  if (!Number.isInteger(v) || v < 0 || v > max) throw new Error(`${what}: expected 0..${max}, got "${s}"`);
  return v;
};

/** A comma-separated list of hex bytes (`f0,7d,42` - a `0x` prefix and spaces are tolerated). */
export function parseHexBytes(list: string, what: string): number[] {
  const parts = list.split(/[,\s]+/).filter((p) => p.length > 0);
  if (parts.length === 0) throw new Error(`${what}: expected hex bytes, e.g. ${what}:f0,7d,42,02,f7`);
  return parts.map((p) => {
    const v = parseInt(p.replace(/^0x/i, ""), 16);
    if (!/^(0x)?[0-9a-fA-F]{1,2}$/.test(p) || Number.isNaN(v)) throw new Error(`${what}: bad hex byte "${p}"`);
    return v;
  });
}

/** One step of the sequence -> the bytes it emits (empty for `wait`, which only delays). `readFile` serves
 *  the `file:` step, whose bytes are read up front so a missing file is caught before the device is touched. */
export function stepBytes(step: string, readFile?: (path: string) => Uint8Array | null): { bytes: number[]; waitMs: number } {
  const colon = step.indexOf(":");
  const kind = colon < 0 ? step : step.slice(0, colon);
  const rest = colon < 0 ? "" : step.slice(colon + 1);
  const p = step.split(":");
  // MIDI channels are 1-based here (matching the BlipToaster monitor's CH column and BASE01).
  const chan = (i: number): number => num(p[i], `${kind}: channel`, 16) - 1;

  if (kind === "wait") {
    if (p.length !== 2) throw new Error('wait: expected "wait:<ms>"');
    return { bytes: [], waitMs: num(p[1], "wait: ms", 600000) };
  }
  if (kind === "on") {
    if (p.length < 3 || p.length > 4) throw new Error('on: expected "on:<ch>:<note>[:<vel>]"');
    const vel = p.length === 4 ? num(p[3], "on: velocity", 127) : 100;
    return { bytes: [0x90 | chan(1), num(p[2], "on: note", 127), vel], waitMs: 0 };
  }
  if (kind === "off") {
    if (p.length !== 3) throw new Error('off: expected "off:<ch>:<note>"');
    return { bytes: [0x80 | chan(1), num(p[2], "off: note", 127), 0], waitMs: 0 };
  }
  if (kind === "cc") {
    if (p.length !== 4) throw new Error('cc: expected "cc:<ch>:<num>:<val>"');
    return { bytes: [0xb0 | chan(1), num(p[2], "cc: number", 127), num(p[3], "cc: value", 127)], waitMs: 0 };
  }
  // Raw bytes, untouched - a SysEx, a 0xFF panic, anything the ROM's parser should see verbatim.
  if (kind === "raw") return { bytes: parseHexBytes(rest, "raw"), waitMs: 0 };
  // A SysEx payload (7-bit, manufacturer id first) wrapped in F0..F7.
  if (kind === "sysex") {
    const payload = parseHexBytes(rest, "sysex");
    const bad = payload.findIndex((b) => b > 0x7f);
    if (bad >= 0) throw new Error(`sysex: payload byte ${bad} is 0x${payload[bad].toString(16)}, expected 7-bit (00..7f)`);
    return { bytes: [0xf0, ...payload, 0xf7], waitMs: 0 };
  }
  // The bytes of a file, verbatim (what `retroplug-n8-hwtest fifowr <file>` sends).
  if (kind === "file") {
    if (!rest) throw new Error('file: expected "file:<path>"');
    if (!readFile) throw new Error("file: no file reader available");
    const bytes = readFile(rest);
    if (!bytes) throw new Error(`file: cannot read ${rest}`);
    if (bytes.length === 0) throw new Error(`file: ${rest} is empty`);
    return { bytes: Array.from(bytes), waitMs: 0 };
  }
  throw new Error(`unknown step "${step}" (expected on/off/cc/wait/raw/sysex/file)`);
}

/** iNES mappers whose cartridge carries expansion audio the N8 mixes through its FPGA master volume. */
export const EXPANSION_MAPPERS: Record<number, string> = {
  5: "MMC5", 19: "N163", 24: "VRC6", 26: "VRC6", 69: "Sunsoft 5B", 85: "VRC7",
};

/** The iNES mapper number from a ROM's header (null when the bytes are not an iNES image). */
export function inesMapper(rom: Uint8Array): number | null {
  if (rom.length < 16 || rom[0] !== 0x4e || rom[1] !== 0x45 || rom[2] !== 0x53 || rom[3] !== 0x1a) return null;
  return (rom[7] & 0xf0) | (rom[6] >> 4);
}

/** What to do about the FPGA expansion master volume: `value` to write (null = leave it alone) and the
 *  line to print. An explicit --exp-vol wins; else a --rom whose mapper carries expansion audio defaults
 *  to unity (128), a --rom without one leaves the register untouched, and no --rom at all is a warning -
 *  at 0 (a fresh boot) every expansion voice is silent no matter what the ROM does, and that reads
 *  exactly like a dead chip. */
export function expVolPlan(explicit: string | undefined, mapper: number | null | undefined): { value: number | null; note: string } {
  if (explicit !== undefined) {
    const v = num(explicit, "--exp-vol", 255);
    return { value: v, note: `expansion master volume <- ${v}${v === 0 ? " (MUTE)" : v === 128 ? " (unity)" : ""}` };
  }
  if (mapper === undefined) {
    return {
      value: null,
      note:
        "warning: --exp-vol not set; the FPGA expansion master volume is left as it was (0 after a power-cycle = " +
        "VRC6/VRC7/N163/S5B/MMC5 silent). Pass --exp-vol 128, or --rom <rom.nes> to default it from the mapper.",
    };
  }
  if (mapper === null) return { value: null, note: "--rom is not an iNES image; expansion master volume left as it was" };
  const chip = EXPANSION_MAPPERS[mapper];
  if (chip) return { value: 128, note: `expansion master volume <- 128 (unity; --rom is mapper ${mapper} = ${chip})` };
  return { value: null, note: `mapper ${mapper} has no expansion audio; expansion master volume left as it was` };
}

function run(s: Session, args: string[]): void {
  const seq = args.filter((a) => /^(on|off|cc|wait|raw|sysex|file):/.test(a));
  if (seq.length === 0) throw new Error("no steps given (e.g. cc:6:20:127 on:6:69 wait:2000 off:6:69)");

  // Parse EVERYTHING before touching the device, so a typo (or a missing file) can't leave a note hanging on
  // real hardware.
  const parsed = seq.map((step) => stepBytes(step, (path) => s.backend.readFile(path)));
  const romPath = flag(args, "--rom");
  let mapper: number | null | undefined;
  if (romPath !== undefined) {
    const rom = s.backend.readFile(romPath);
    if (!rom) throw new Error(`--rom: cannot read ${romPath}`);
    mapper = inesMapper(rom);
  }
  const plan = expVolPlan(flag(args, "--exp-vol"), mapper);

  const serial = createSerialClient();
  const port = serial.open(pickPort(serial.listPorts(), flag(args, "--serial")));
  const n8 = createN8(port);
  n8.connect();

  // Expansion audio (VRC6/VRC7/N163/S5B/MMC5) is mixed by the FPGA, not the 2A03 - if this is 0 the
  // expansion voices are silent no matter what the ROM does. Write-only: never read it back.
  if (plan.value !== null) n8.edio.memWR(ADDR_EXP_VOL, new Uint8Array([plan.value]));
  console.log(plan.note);

  if (flag(args, "--prime") !== "off") {
    // BlipToaster drops the first message after boot; burn one harmless CC so step 1 always lands.
    n8.edio.fifoWR(new Uint8Array([0xb0, 121, 0]));
    spin(50);
  }

  for (let i = 0; i < parsed.length; i++) {
    const { bytes, waitMs } = parsed[i];
    if (bytes.length) {
      n8.edio.fifoWR(new Uint8Array(bytes));
      const hex = bytes.map((b) => b.toString(16).padStart(2, "0"));
      console.log(`  ${seq[i]}  ->  ${hex.length > 24 ? `${hex.slice(0, 24).join(" ")} ... (${bytes.length} bytes)` : hex.join(" ")}`);
    } else {
      console.log(`  ${seq[i]}`);
    }
    if (waitMs) spin(waitMs);
  }
}

export const n8PlayTool: CliTool = {
  name: "n8-play",
  summary: "play a scripted MIDI sequence on a physical Everdrive N8 Pro (no MIDI hardware needed)",
  help: [
    "usage: retroplug-cli n8-play [--serial <port>] [--exp-vol <0-255> | --rom <rom.nes>] [--prime off] <step>...",
    "",
    "  Sends a fixed MIDI sequence to the N8's cart FIFO, which the BlipToaster ROM reads at $40F0/$40F1.",
    "  The scripted twin of n8-bridge: reproducible hardware checks with no controller attached.",
    "",
    "steps (MIDI channels are 1-based, matching the BlipToaster monitor's CH column):",
    "  on:<ch>:<note>[:<vel>]   note on  (velocity defaults to 100)",
    "  off:<ch>:<note>          note off",
    "  cc:<ch>:<num>:<val>      control change",
    "  wait:<ms>                hold for <ms> before the next step",
    "  raw:<hex,hex,...>        these bytes, verbatim (raw:ff is the panic byte; raw:f0,7d,42,02,f7 a SysEx)",
    "  sysex:<hex,hex,...>      a SysEx payload (7-bit, manufacturer id first) wrapped in F0..F7",
    "  file:<path>              the bytes of a file, verbatim (what retroplug-n8-hwtest fifowr sends)",
    "",
    "options:",
    "  --serial <port>    use this serial port (default: auto-detect the N8, VID:PID 38df:0017)",
    "  --exp-vol <0-255>  set the FPGA expansion-audio master volume first (0 mute, 128 unity, 255 2x).",
    "                     REQUIRED for expansion chips (VRC6/VRC7/N163/S5B/MMC5) - at 0 they are silent",
    "                     however correct the ROM is. Live-only: it applies to the running cart.",
    "  --rom <rom.nes>    the ROM that is running: defaults --exp-vol to 128 when its mapper carries",
    "                     expansion audio (5, 19, 24, 26, 69, 85) and leaves it alone otherwise. With",
    "                     neither flag the register is left as it was, and the run warns about it.",
    "  --prime off        skip the priming CC (BlipToaster drops its first message after boot)",
    "",
    "  Nothing is reset between invocations: a note held by one call keeps sounding through the next,",
    "  so a check can hold a note with one command and push a SysEx (or a capture) with another.",
    "",
    "example - hold a 2 s A4 on the S5B's Square A with the hardware envelope on:",
    "  retroplug-cli n8-play --exp-vol 128 \\",
    "      cc:6:29:80 cc:6:28:64 cc:6:20:127 on:6:69 wait:2000 off:6:69",
    "example - upload a flat wave to the N163 build while a note is held from an earlier call:",
    "  retroplug-cli n8-play --rom build/bliptoaster-n163.nes --prime off \\",
    "      sysex:7d,42,01,00,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08,08",
  ].join("\n"),
  run,
};
