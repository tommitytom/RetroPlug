// `retroplug-cli n8-load` - load + boot a ROM on a physical Everdrive N8 Pro over USB by driving its
// on-device menu, plus the SD/SRAM helpers. The TS replacement for the native cli/N8Load.cpp (retired in
// Phase 4): same flag interface, so every documented `n8-load ...` invocation keeps working - now running
// on the TS N8 stack (Edio framing + menu + orchestration in src/n8) over the serial byte-transport facet.
// `--show-song` is new (decode the live cart battery via the pure-TS risa/LSDj codecs).
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { createSerialClient } from "../../src/realBackend";
import { createN8, baseName, type SerialPortInfo, type LoadOptions } from "../../src/n8";
import { isRisaSav, listSongs } from "../../src/risa";
import { isLsdjSav, listProjects } from "../../src/lsdj";

const DEFAULT_ROM = "resources/roms/n8-midi.nes";
const VALUE_FLAGS = new Set(["--sd-path", "--srm", "--dump-sram", "--ls", "--serial"]);

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
const has = (args: string[], name: string): boolean => args.includes(name);
function positional(args: string[]): string | undefined {
  for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith("--")) {
      if (VALUE_FLAGS.has(args[i])) i++;
      continue;
    }
    return args[i];
  }
  return undefined;
}

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

function readOrThrow(s: Session, path: string, what: string): Uint8Array {
  const data = s.backend.readFile(path);
  if (!data) throw new Error(`cannot read ${what}: ${path}`);
  return data;
}

function decodeAndPrint(sram: Uint8Array): void {
  if (isRisaSav(sram)) {
    const songs = listSongs(sram);
    console.log(`risa battery - ${songs.length} song(s):`);
    for (const song of songs)
      console.log(`  [${String(song.index).padStart(2)}] ${song.name.trim() || "(unnamed)"}  v${song.version}  ${song.length}B`);
  } else if (isLsdjSav(sram)) {
    const projects = listProjects(sram);
    console.log(`LSDj battery - ${projects.length} project(s):`);
    for (const p of projects) console.log(`  [${String(p.slot).padStart(2)}] ${p.name.trim() || "(unnamed)"}`);
  } else {
    console.log(`unrecognized battery image (${sram.length} bytes) - not a risa or LSDj save`);
  }
}

const N8_LOAD_HELP = [
  "usage: retroplug-cli n8-load [options] [<rom.nes>]",
  "",
  "  Load + boot a ROM on a physical Everdrive N8 Pro over USB by driving its on-device menu. The N8",
  "  firmware parses the ROM and sources the mapper core from its own SD, so no low-level FPGA/mapper",
  "  work happens here. Auto-detects the N8 (VID:PID 38df:0017).",
  "",
  "  <rom.nes>          upload this local ROM to usb-games/ and boot it (default: resources/roms/n8-midi.nes)",
  "  --sd-path <path>   instead, boot a ROM already on the N8 SD card by its SD path",
  "  --srm <save.srm>   restore this battery save on boot (written to EDN8/gamedata/<rom>/bram.srm)",
  "  --sram-only        with --srm: write the save STRAIGHT to cart SRAM (a running game; no reboot).",
  "                     WARNING: corrupts the menu if run on the file browser",
  "  --dump-sram <file> read the cart SRAM (64 KB game region) out to <file> (no ROM, no reboot)",
  "  --ls <path>        list an SD-card directory (use \"/\" for root) and exit",
  "  --show-song        decode the live cart battery (risa/LSDj) and print its songs",
  "  --serial <port>    use this serial port (default: auto-detect the N8)",
  "",
  "  Run load from the N8 file-browser menu. If a load fails with 'out of memory' (a dirty menu heap",
  "  after a prior failed load), power-cycle the console to a fresh menu and retry.",
].join("\n");

export const n8LoadTool: CliTool = {
  name: "n8-load",
  summary: "load + boot a ROM on a physical Everdrive N8 Pro over USB (menu-driven)",
  help: N8_LOAD_HELP,
  run(s: Session, args: string[]): void {
    const serialPort = flag(args, "--serial");
    const sdPath = flag(args, "--sd-path");
    const srmPath = flag(args, "--srm");
    const dumpPath = flag(args, "--dump-sram");
    const doLs = has(args, "--ls");
    const sramOnly = has(args, "--sram-only");
    const showSong = has(args, "--show-song");

    if (sramOnly && !srmPath) throw new Error("--sram-only requires --srm <save.srm>");

    // Read local files up front (fail fast, before touching hardware).
    const readOnly = doLs || dumpPath != null || showSong || sramOnly;
    const romPath = positional(args) ?? (sdPath || readOnly ? undefined : DEFAULT_ROM);
    let romBytes: Uint8Array | undefined;
    let romName: string | undefined;
    if (romPath && !sdPath) {
      romBytes = readOrThrow(s, romPath, "ROM");
      romName = baseName(romPath);
    }
    const srm = srmPath ? readOrThrow(s, srmPath, "save") : undefined;

    const serial = createSerialClient();
    const port = serial.open(pickPort(serial.listPorts(), serialPort));
    try {
      const n8 = createN8(port);
      n8.connect(); // throws if the N8 doesn't answer the handshake

      if (doLs) {
        const path = flag(args, "--ls") ?? "/";
        const entries = n8.listDir(path === "/" ? "" : path);
        console.log(`${path} (${entries.length} entr${entries.length === 1 ? "y" : "ies"}):`);
        for (const e of entries) console.log(e.isDir ? `  [DIR]  ${e.name}` : `  ${String(e.size).padStart(8)}  ${e.name}`);
        return;
      }
      if (dumpPath != null) {
        const sram = n8.dumpSram();
        if (!s.backend.writeFile(dumpPath, sram)) throw new Error(`write failed: ${dumpPath}`);
        console.log(`wrote ${sram.length} bytes of cart SRAM -> ${dumpPath}`);
        return;
      }
      if (showSong) {
        decodeAndPrint(n8.dumpSram());
        return;
      }
      if (sramOnly) {
        const n = n8.writeSramDirect(srm!);
        console.log(`wrote + verified ${n} bytes straight to cart SRAM (no reboot)`);
        return;
      }

      const opts: LoadOptions = {};
      if (sdPath) opts.sdPath = sdPath;
      else {
        opts.romBytes = romBytes;
        opts.romName = romName;
      }
      if (srm) opts.srm = srm;
      const { bootPath, mapIndex } = n8.load(opts);
      console.log(`booted '${bootPath}' (map index ${mapIndex})${srmPath ? ` with save ${srmPath}` : ""}`);
    } finally {
      port.close();
    }
  },
};
