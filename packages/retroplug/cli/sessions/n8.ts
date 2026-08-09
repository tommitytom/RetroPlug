// `retroplug-cli n8 <verb>` - drive a physical Everdrive N8 Pro over USB from the TS N8 stack (Edio framing
// + menu + ROM/save orchestration in src/n8, over the native serial byte-transport facet). The scriptable,
// mostly-TS twin of the native n8-load subcommand; shipped under the `n8` group because the launcher
// intercepts the bare `n8-load` string before the TS dispatcher (freed in a later phase). Verbs:
//   n8 ls        [path]            list an SD-card directory (read-only)
//   n8 load      <rom.nes>         upload + boot a ROM (--sd-path to boot an on-SD path; --srm to restore a save)
//   n8 dump-sram <out.srm>         read the cart battery SRAM to a file
//   n8 sram-only <save.srm>        write a save straight to cart SRAM (running game only; corrupts the menu)
//   n8 show-song [file.srm]        decode a risa/LSDj battery (a file, or the live cart) and print its songs
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { createSerialClient } from "../../src/realBackend";
import { createN8, baseName, type N8, type SerialPortInfo, type LoadOptions } from "../../src/n8";
import { isRisaSav, listSongs } from "../../src/risa";
import { isLsdjSav, listProjects } from "../../src/lsdj";

const VALUE_FLAGS = new Set(["--serial", "--srm", "--sd-path", "--out"]);
const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
function positionals(args: string[]): string[] {
  const out: string[] = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith("--")) {
      if (VALUE_FLAGS.has(args[i])) i++;
      continue;
    }
    out.push(args[i]);
  }
  return out;
}

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

// Open + connect the N8, run `fn`, then always close the port.
function withN8<T>(args: string[], fn: (n8: N8) => T): T {
  const serial = createSerialClient();
  const port = serial.open(pickPort(serial.listPorts(), flag(args, "--serial")));
  try {
    const n8 = createN8(port);
    n8.connect(); // throws if the N8 doesn't answer the handshake
    return fn(n8);
  } finally {
    port.close();
  }
}

function readOrThrow(s: Session, path: string, what: string): Uint8Array {
  const data = s.backend.readFile(path);
  if (!data) throw new Error(`cannot read ${what}: ${path}`);
  return data;
}

// --- verbs ----------------------------------------------------------------------------------------------

function ls(_s: Session, args: string[]): void {
  const path = positionals(args)[0] ?? "/";
  withN8(args, (n8) => {
    const entries = n8.listDir(path === "/" ? "" : path);
    console.log(`${path} (${entries.length} entr${entries.length === 1 ? "y" : "ies"}):`);
    for (const e of entries) {
      if (e.isDir) console.log(`  [DIR]  ${e.name}`);
      else console.log(`  ${String(e.size).padStart(8)}  ${e.name}`);
    }
  });
}

function load(s: Session, args: string[]): void {
  const romPath = positionals(args)[0];
  const sdPath = flag(args, "--sd-path");
  const srmPath = flag(args, "--srm");
  if (!romPath && !sdPath) throw new Error("usage: n8 load <rom.nes> [--sd-path <path>] [--srm <save.srm>]");

  const opts: LoadOptions = {};
  if (sdPath) opts.sdPath = sdPath;
  else if (romPath) {
    opts.romBytes = readOrThrow(s, romPath, "ROM");
    opts.romName = baseName(romPath);
  }
  if (srmPath) opts.srm = readOrThrow(s, srmPath, "save");

  const { bootPath, mapIndex } = withN8(args, (n8) => n8.load(opts));
  console.log(`booted '${bootPath}' (map index ${mapIndex})${srmPath ? ` with save ${srmPath}` : ""}`);
}

function dumpSram(s: Session, args: string[]): void {
  const out = positionals(args)[0];
  if (!out) throw new Error("usage: n8 dump-sram <out.srm> [--serial <port>]");
  const sram = withN8(args, (n8) => n8.dumpSram());
  if (!s.backend.writeFile(out, sram)) throw new Error(`write failed: ${out}`);
  console.log(`wrote ${sram.length} bytes of cart SRAM -> ${out}`);
}

function sramOnly(s: Session, args: string[]): void {
  const save = positionals(args)[0];
  if (!save) throw new Error("usage: n8 sram-only <save.srm> [--serial <port>]");
  const srm = readOrThrow(s, save, "save");
  const n = withN8(args, (n8) => n8.writeSramDirect(srm));
  console.log(`wrote + verified ${n} bytes straight to cart SRAM (no reboot)`);
  console.log("note: only valid for a RUNNING game - this corrupts the menu if run on the file browser");
}

function showSong(s: Session, args: string[]): void {
  const file = positionals(args)[0];
  const sram = file ? readOrThrow(s, file, "battery") : withN8(args, (n8) => n8.dumpSram());
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

const N8_HELP = [
  "usage: retroplug-cli n8 <verb> [args...]",
  "",
  "  Drive a physical Everdrive N8 Pro over USB. Auto-detects the N8 (VID:PID 38df:0017); pass",
  "  --serial <port> to any verb to target a specific port.",
  "",
  "  ls        [path]           list an SD-card directory (default: card root; read-only, no reboot)",
  "  load      <rom.nes>        upload the local ROM to usb-games/ and boot it",
  "            --sd-path <p>    instead, boot a ROM already on the SD card by its path",
  "            --srm <save>     restore this battery save (via EDN8/gamedata/<rom>/bram.srm) on boot",
  "  dump-sram <out.srm>        read the cart battery SRAM (64 KB game region) out to a file",
  "  sram-only <save.srm>       write a save STRAIGHT to cart SRAM (a running game only; no reboot).",
  "                             WARNING: corrupts the menu if run on the file browser",
  "  show-song [file.srm]       decode a risa/LSDj battery (a local file, or the live cart if omitted)",
  "                             and print its songs",
  "",
  "  Run from the N8 file-browser menu for load. If a load fails with 'out of memory' (a dirty menu",
  "  heap after a prior failed load), power-cycle the console to a fresh menu and retry.",
].join("\n");

export const n8Tool: CliTool = {
  name: "n8",
  summary: "drive a physical Everdrive N8 Pro over USB (ls / load / dump-sram / sram-only / show-song)",
  help: N8_HELP,
  run(s: Session, args: string[]): void {
    const sub = args[0];
    const rest = args.slice(1);
    if (sub === "ls") return ls(s, rest);
    if (sub === "load") return load(s, rest);
    if (sub === "dump-sram") return dumpSram(s, rest);
    if (sub === "sram-only") return sramOnly(s, rest);
    if (sub === "show-song") return showSong(s, rest);
    throw new Error(`unknown subcommand '${sub ?? ""}'\n\n${N8_HELP}`);
  },
};
