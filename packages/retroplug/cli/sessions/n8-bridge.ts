// `retroplug-cli n8-bridge` / `n8-sync` - stream live MIDI to a physical Everdrive N8 Pro over USB, entirely
// in TS (these replaced the native subcommands). `n8-bridge` forwards raw MIDI to the cart FIFO (for the
// EverMIDI ROM); `n8-sync` translates a MIDI clock/transport into risa host-sync bytes (via the pure-TS
// RisaSyncTranslator). Both are long-running: they open the serial (createN8) + MIDI (createMidiClient)
// facets, call keepAlive(), and run an event-driven poll loop (setInterval) until Ctrl-C - the native
// launcher pumps it (see cli/main.cpp).
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { keepAlive, exitProcess } from "../session";
import { createSerialClient, createMidiClient } from "../../src/realBackend";
import { createN8, RisaSyncTranslator, type SerialPortInfo } from "../../src/n8";

// txiki provides this libuv-backed global at runtime (deps/.../txiki timers); the TS lib config omits it.
declare function setInterval(handler: () => void, ms: number): number;

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

// The port to talk to: an explicit --serial, else the first attached N8 (USB VID:PID 38df:0017).
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

function printInputs(): void {
  const inputs = createMidiClient().listInputs();
  if (inputs.length === 0) {
    console.log("no MIDI inputs found");
    return;
  }
  console.log("MIDI inputs:");
  for (const n of inputs) console.log(`  ${n}`);
}

// The shared bridge loop: poll MIDI, transform each message to output bytes, forward to the cart FIFO with an
// optional constant-delay lookahead (arrival order == release order). `transform` is the ONLY difference
// between the raw bridge and the risa-sync bridge. Long-running: sets keepAlive() and returns; the native
// pump drives the setInterval poll until Ctrl-C.
function runBridge(args: string[], clientName: string, transform: (bytes: Uint8Array, out: number[]) => void): void {
  const lookaheadMs = Math.max(0, Number(flag(args, "--lookahead-ms") ?? 0) | 0);

  const serial = createSerialClient();
  const port = serial.open(pickPort(serial.listPorts(), flag(args, "--serial")));
  const n8 = createN8(port);
  n8.connect(); // throws if the N8 doesn't answer the handshake

  const midi = createMidiClient();
  const midiIn = flag(args, "--midi-in") ?? "";
  if (!midi.open(clientName, midiIn)) throw new Error("no MIDI system available");

  keepAlive(); // opt into the native run-until-Ctrl-C pump

  const queue: { due: number; bytes: number[] }[] = [];
  let msgCount = 0;
  let byteCount = 0;
  const forward = (bytes: number[]): void => {
    n8.edio.fifoWR(Uint8Array.from(bytes));
    msgCount++;
    byteCount += bytes.length;
  };

  setInterval(() => {
    const now = Date.now();
    for (const bytes of midi.poll()) {
      const out: number[] = [];
      transform(bytes, out);
      if (out.length === 0) continue; // non-transport MIDI (sync) yields nothing
      if (lookaheadMs > 0) queue.push({ due: now + lookaheadMs, bytes: out });
      else forward(out);
    }
    while (queue.length > 0 && queue[0].due <= now) forward(queue.shift()!.bytes);
  }, 1);

  setInterval(() => console.log(`forwarded ${msgCount} messages / ${byteCount} bytes`), 2000);

  console.log(
    `bridging ${midiIn || "all MIDI inputs"} -> Everdrive N8 on ${port.port}` +
      `${lookaheadMs ? ` (+${lookaheadMs}ms lookahead)` : ""}. Ctrl-C to stop.`,
  );
}

const COMMON_FLAGS = [
  "  --list             list the available MIDI input ports and exit",
  "  --midi-in <name>   forward from this MIDI input (default: all hardware inputs)",
  "  --serial <port>    use this serial port (default: auto-detect the N8, VID:PID 38df:0017)",
  "  --lookahead-ms <N> delay every message by N ms (constant-delay smoothing; default 0 = immediate)",
];

const BRIDGE_HELP = [
  "usage: retroplug-cli n8-bridge [--list] [--midi-in <name>] [--serial <port>] [--lookahead-ms <N>]",
  "",
  "  Stream live MIDI straight to a physical Everdrive N8 Pro's cart FIFO over USB, so a controller / DAW",
  "  plays the real NES (for the EverMIDI ROM). Runs until Ctrl-C.",
  "",
  ...COMMON_FLAGS,
].join("\n");

const SYNC_HELP = [
  "usage: retroplug-cli n8-sync [--list] [--midi-in <name>] [--serial <port>] [--lookahead-ms <N>]",
  "",
  "  Turn an incoming MIDI clock/transport (Start/Continue/Stop + 24-PPQN clock, optional Song Position)",
  "  into risa's host-sync protocol on a physical N8, so a risa cart plays locked to the DAW. Runs until",
  "  Ctrl-C. Load a risa ROM first (retroplug-cli n8-load).",
  "",
  ...COMMON_FLAGS,
].join("\n");

export const n8BridgeTool: CliTool = {
  name: "n8-bridge",
  summary: "stream live MIDI to a physical Everdrive N8 Pro over USB",
  help: BRIDGE_HELP,
  longRunning: true,
  run(_s: Session, args: string[]): void {
    if (args.includes("--list")) {
      printInputs();
      exitProcess(0);
      return;
    }
    runBridge(args, "RetroPlug N8", (bytes, out) => {
      for (const b of bytes) out.push(b); // raw forward
    });
  },
};

export const n8SyncTool: CliTool = {
  name: "n8-sync",
  summary: "drive risa host-sync on a physical Everdrive N8 Pro from a MIDI clock",
  help: SYNC_HELP,
  longRunning: true,
  run(_s: Session, args: string[]): void {
    if (args.includes("--list")) {
      printInputs();
      exitProcess(0);
      return;
    }
    const translator = new RisaSyncTranslator();
    runBridge(args, "RetroPlug N8 Sync", (bytes, out) => translator.onMessage(bytes, out));
  },
};
