// `retroplug-cli n8-ls` - list a directory on a physical Everdrive N8 Pro's SD card over USB. The read-only
// first consumer of the TS N8 stack: Edio protocol framing (src/n8/edio.ts) over the native serial byte-
// transport facet (createSerialClient). The TS twin of the C++ `n8-load --ls`; it changes no menu state and
// reboots nothing, so it's the safest proof the seam works end-to-end on real hardware.
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { createSerialClient } from "../../src/realBackend";
import { Edio, type SerialPortInfo } from "../../src/n8";

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
        (ports.length ? `ports: ${ports.map((p) => p.port).join(", ")}` : "no serial ports detected"),
    );
  return n8.port;
}

const N8_LS_HELP = [
  "usage: retroplug-cli n8-ls [path] [--serial <port>]",
  "",
  "  List an SD-card directory on a physical Everdrive N8 Pro over USB (read-only; no ROM, no reboot).",
  "  Talks to the N8 firmware, so it works whether or not a game is running.",
  "",
  "  path             SD directory to list (default: \"/\", the card root)",
  "  --serial <port>  use this serial port (default: auto-detect the N8, VID:PID 38df:0017)",
].join("\n");

export const n8LsTool: CliTool = {
  name: "n8-ls",
  summary: "list a directory on a physical Everdrive N8 Pro SD card over USB",
  help: N8_LS_HELP,
  run(_s: Session, args: string[]): void {
    const serialPort = flag(args, "--serial");
    const path = args.find((a) => !a.startsWith("--") && a !== serialPort) ?? "/";

    const serial = createSerialClient();
    const portName = pickPort(serial.listPorts(), serialPort);
    const port = serial.open(portName);
    try {
      const edio = new Edio(port);
      edio.connect(); // throws if the N8 doesn't answer the handshake
      const entries = edio.listDir(path === "/" ? "" : path);
      console.log(`${path} (${entries.length} entr${entries.length === 1 ? "y" : "ies"}):`);
      for (const e of entries) {
        if (e.isDir) console.log(`  [DIR]  ${e.name}`);
        else console.log(`  ${String(e.size).padStart(8)}  ${e.name}`);
      }
    } finally {
      port.close();
    }
  },
};
