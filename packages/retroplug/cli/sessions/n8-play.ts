// `retroplug-cli n8-play` - send a SCRIPTED MIDI sequence to a physical Everdrive N8 Pro's cart FIFO.
//
// The live twin of `n8-bridge`: where the bridge forwards a controller/DAW in real time, this plays a fixed
// sequence given on the command line, so a hardware check is one reproducible command with no MIDI hardware
// attached. The EverMIDI ROM reads these bytes at $40F0/$40F1. Pair it with a capture + `analyze-capture` to
// verify what the real chips actually did (see the nes-hardware-lab skill).
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

/** One step of the sequence -> the MIDI bytes it emits (empty for `wait`, which only delays). */
export function stepBytes(step: string): { bytes: number[]; waitMs: number } {
  const p = step.split(":");
  const kind = p[0];
  // MIDI channels are 1-based here (matching the EverMIDI monitor's CH column and BASE01).
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
  throw new Error(`unknown step "${step}" (expected on/off/cc/wait)`);
}

function run(_s: Session, args: string[]): void {
  const seq = args.filter((a) => /^(on|off|cc|wait):/.test(a));
  if (seq.length === 0) throw new Error("no steps given (e.g. cc:6:20:127 on:6:69 wait:2000 off:6:69)");

  // Parse EVERYTHING before touching the device, so a typo can't leave a note hanging on real hardware.
  const parsed = seq.map(stepBytes);

  const serial = createSerialClient();
  const port = serial.open(pickPort(serial.listPorts(), flag(args, "--serial")));
  const n8 = createN8(port);
  n8.connect();

  const vol = flag(args, "--exp-vol");
  if (vol !== undefined) {
    const v = num(vol, "--exp-vol", 255);
    // Expansion audio (VRC6/VRC7/N163/S5B/MMC5) is mixed by the FPGA, not the 2A03 - if this is 0 the
    // expansion voices are silent no matter what the ROM does. Write-only: never read it back.
    n8.edio.memWR(ADDR_EXP_VOL, new Uint8Array([v]));
    console.log(`expansion master volume <- ${v}${v === 0 ? " (MUTE)" : v === 128 ? " (unity)" : ""}`);
  }

  if (flag(args, "--prime") !== "off") {
    // EverMIDI drops the first message after boot; burn one harmless CC so step 1 always lands.
    n8.edio.fifoWR(new Uint8Array([0xb0, 121, 0]));
    spin(50);
  }

  for (let i = 0; i < parsed.length; i++) {
    const { bytes, waitMs } = parsed[i];
    if (bytes.length) {
      n8.edio.fifoWR(new Uint8Array(bytes));
      console.log(`  ${seq[i]}  ->  ${bytes.map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
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
    "usage: retroplug-cli n8-play [--serial <port>] [--exp-vol <0-255>] [--prime off] <step>...",
    "",
    "  Sends a fixed MIDI sequence to the N8's cart FIFO, which the EverMIDI ROM reads at $40F0/$40F1.",
    "  The scripted twin of n8-bridge: reproducible hardware checks with no controller attached.",
    "",
    "steps (MIDI channels are 1-based, matching the EverMIDI monitor's CH column):",
    "  on:<ch>:<note>[:<vel>]   note on  (velocity defaults to 100)",
    "  off:<ch>:<note>          note off",
    "  cc:<ch>:<num>:<val>      control change",
    "  wait:<ms>                hold for <ms> before the next step",
    "",
    "options:",
    "  --serial <port>    use this serial port (default: auto-detect the N8, VID:PID 38df:0017)",
    "  --exp-vol <0-255>  set the FPGA expansion-audio master volume first (0 mute, 128 unity, 255 2x).",
    "                     REQUIRED for expansion chips (VRC6/VRC7/N163/S5B/MMC5) - at 0 they are silent",
    "                     however correct the ROM is. Live-only: it applies to the running cart.",
    "  --prime off        skip the priming CC (EverMIDI drops its first message after boot)",
    "",
    "example - hold a 2 s A4 on the S5B's Square A with the hardware envelope on:",
    "  retroplug-cli n8-play --exp-vol 128 \\",
    "      cc:6:29:80 cc:6:28:64 cc:6:20:127 on:6:69 wait:2000 off:6:69",
  ].join("\n"),
  run,
};
