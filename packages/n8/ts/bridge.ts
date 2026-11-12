// The bridge both N8 tools share (docs/modular-graph-phase-f4b-design.md section 5.2): the port, the MIDI facet, the
// N8 facet and the poll loop, with the transform each MIDI message to cart-FIFO bytes as the ONE argument that differs.
// It lives in the n8 package and is published as `@retroplug/n8/bridge` so risa's n8-sync can drive the same loop,
// which keeps n8 free of every cart (risa depends on n8, not the other way about).
import type { CliHost } from "@retroplug/core/cliTool";
import { createN8Client, pickN8Port } from "./index";

// txiki provides this libuv-backed global at runtime (deps/.../txiki timers); the TS lib config omits it.
declare function setInterval(handler: () => void, ms: number): number;

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

export function printInputs(host: CliHost): void {
  const inputs = host.devices.midi().listInputs();
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
export function runBridge(host: CliHost, args: string[], clientName: string,
  transform: (bytes: Uint8Array, out: number[]) => void): void {
  const lookaheadMs = Math.max(0, Number(flag(args, "--lookahead-ms") ?? 0) | 0);

  const client = createN8Client(host.devices.rpc);
  const n8 = client.openN8(pickN8Port(client.listPorts(), flag(args, "--serial")));
  n8.connect(); // throws if the N8 doesn't answer the handshake

  const midi = host.devices.midi();
  const midiIn = flag(args, "--midi-in") ?? "";
  if (!midi.open(clientName, midiIn)) throw new Error("no MIDI system available");

  host.process.keepAlive(); // opt into the native run-until-Ctrl-C pump

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
    `bridging ${midiIn || "all MIDI inputs"} -> Everdrive N8 on ${n8.port}` +
      `${lookaheadMs ? ` (+${lookaheadMs}ms lookahead)` : ""}. Ctrl-C to stop.`,
  );
}

/** The flags both tools accept, for their help text. */
export const COMMON_FLAGS = [
  "  --list             list the available MIDI input ports and exit",
  "  --midi-in <name>   forward from this MIDI input (default: all hardware inputs)",
  "  --serial <port>    use this serial port (default: auto-detect the N8, VID:PID 38df:0017)",
  "  --lookahead-ms <N> delay every message by N ms (constant-delay smoothing; default 0 = immediate)",
];

