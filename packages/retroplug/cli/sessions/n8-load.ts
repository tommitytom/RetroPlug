// `retroplug-cli n8-load` - load + boot a ROM on a physical Everdrive N8 Pro over USB by driving its
// on-device menu, plus the SD/SRAM helpers. The TS replacement for the native cli/N8Load.cpp (retired in
// Phase 4): same flag interface, so every documented `n8-load ...` invocation keeps working - now running
// on the TS N8 stack (Edio framing + menu + orchestration in src/n8) over the serial byte-transport facet.
// `--show-song` is new (decode the live cart battery via the pure-TS risa/LSDj codecs).
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { createSerialClient } from "../../src/realBackend";
import {
  createN8,
  baseName,
  assertGameRegion,
  ADDR_MENU_CHR,
  ADDR_SSR,
  ADDR_CHR,
  ADDR_PRG,
  type SerialPortInfo,
  type LoadOptions,
} from "../../src/n8";
import { menuScreenToRgba } from "../../src/n8/menuImage";
import { decodeSniffer, SNIFFER_REGION_SIZE, type SnifferSnapshot } from "../../src/n8/sniffer";
import { chrToPng, pngToChr } from "../../src/n8/chrImage";
import { decodeSysInfo, decodeVdc, fatDateStr, vdcStr } from "../../src/n8/sysInfo";
import { isRisaSav, listSongs } from "../../src/risa";
import { isLsdjSav, listProjects } from "../../src/lsdj";

const DEFAULT_ROM = "resources/roms/n8-midi.nes";
const VALUE_FLAGS = new Set([
  "--sd-path", "--srm", "--dump-sram", "--ls", "--get-file", "--screenshot", "--sniff-raw",
  "--dump-chr", "--patch-chr", "--patch-prg", "--mkdir", "--rm", "--serial",
]);

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};
const has = (args: string[], name: string): boolean => args.includes(name);
const isPng = (path: string): boolean => path.toLowerCase().endsWith(".png");
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

const DUTY = ["12.5%", "25%", "50%", "75%"];
function printSniffer(snap: SnifferSnapshot): void {
  if (!snap.magicOk) {
    console.log(
      "no live sniffer data (magic byte absent) - is a GAME running? The sniffer is off at the file-browser menu.",
    );
    return;
  }
  const a = snap.apu;
  const chan = (name: string, on: boolean, freq: number, vol: number, extra = ""): string =>
    `  ${name.padEnd(8)} ${on ? "on " : "off"}  ${String(freq).padStart(5)} Hz  vol ${String(vol).padStart(2)}${extra}`;
  console.log("APU ($4000-$401F, live write-mirror):");
  console.log(chan("pulse1", a.pulse1.enabled, a.pulse1.frequency, a.pulse1.volume, `  duty ${DUTY[a.pulse1.duty]}`));
  console.log(chan("pulse2", a.pulse2.enabled, a.pulse2.frequency, a.pulse2.volume, `  duty ${DUTY[a.pulse2.duty]}`));
  console.log(`  triangle ${a.triangle.enabled ? "on " : "off"}  ${String(a.triangle.frequency).padStart(5)} Hz`);
  console.log(`  noise    ${a.noise.enabled ? "on " : "off"}  period ${a.noise.periodIndex}  vol ${a.noise.volume}${a.noise.mode ? "  (short)" : ""}`);
  console.log(`  dmc      ${a.dmc.enabled ? "on " : "off"}  rate ${a.dmc.rateIndex}  level ${a.dmc.level}`);
  console.log(`  $4015=${a.enableReg.toString(16).padStart(2, "0")}  $4017 ${a.frameMode5Step ? "5-step" : "4-step"}`);
  console.log(
    `PPU: ctrl=${snap.ppu.ctrl.toString(16).padStart(2, "0")} mask=${snap.ppu.mask.toString(16).padStart(2, "0")}` +
      ` (bg ${snap.ppu.showBackground ? "on" : "off"}, sprites ${snap.ppu.showSprites ? "on" : "off"}) scroll=${snap.ppu.scrollX},${snap.ppu.scrollY}`,
  );
  const hex = (b: Uint8Array): string => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join(" ");
  console.log(`palette ($3F00-$3F1F): ${hex(snap.palette)}`);
  console.log(`OAM: ${snap.activeSprites} sprite(s) on-screen (of 64)`);
}

function printInfo(sysBytes: Uint8Array, vdcBytes: Uint8Array): void {
  const s = decodeSysInfo(sysBytes);
  const v = decodeVdc(vdcBytes);
  const mb = s.flashSizeBytes >= 0x100000 ? `${s.flashSizeBytes / 0x100000} MB` : `${s.flashSizeBytes} B`;
  console.log(`device     : ${s.deviceName} (id 0x${s.deviceId.toString(16)})`);
  console.log(`serial     : ${s.serial}`);
  console.log(`form factor: ${s.formFactor}`);
  console.log(`bootloader : 0x${s.bootVer.toString(16).padStart(4, "0")}  (sw 0x${s.swVer.toString(16)}, hw 0x${s.hwVer.toString(16)})`);
  console.log(`build date : ${fatDateStr(s.buildDate)}   mcu core: ${fatDateStr(s.coreDate)}`);
  console.log(`flash size : ${mb}`);
  console.log(`counters   : ${s.gameCtr} games, ${s.bootCtr} boots`);
  console.log(`voltages   : 5.0=${vdcStr(v.v50)} 2.5=${vdcStr(v.v25)} 1.2=${vdcStr(v.v12)} bat=${vdcStr(v.bat)}`);
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
  "  --get-file <sd-path> <local-dest>  read an SD-card file over USB to a local file (no reboot)",
  "  --screenshot <out.png>  capture the N8 MENU screen over USB to a PNG (the menu must be showing)",
  "  --sniff            read a RUNNING game's live APU/PPU/OAM state over USB and print it (a game must",
  "                     be running - the sniffer is off at the menu)",
  "  --sniff-raw <file> dump the raw 512-byte sniffer region to a file (no decode)",
  "  --dump-chr <file>  read the running game's 8 KB visible CHR bank over USB. A .png dest renders an",
  "                     editable grayscale tile grid; else a raw 8 KB .chr",
  "  --patch-chr <hex-offset> <file>  live-patch the running game's CHR (graphics) from <file> at CHR+offset",
  "                     (verified; shows on-screen next frame). <file> = a .png tile grid or raw .chr bytes.",
  "                     Best on a CHR-ROM game (NROM etc.)",
  "  --patch-prg <hex-offset> <file>  live-patch the running game's PRG (code) from raw <file> at PRG+offset.",
  "                     WARNING: a bad code patch can crash the game (power-cycle to recover)",
  "  --info             print the N8's device info (serial, firmware/bootloader versions, NES/Famicom form",
  "                     factor, flash size, voltages) over USB",
  "  --df               print the SD card's free space",
  "  --mkdir <path>     create a directory on the SD card",
  "  --rm <path>        delete a file or empty directory on the SD card (PERMANENT)",
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
    const getFile = flag(args, "--get-file");
    const screenshot = flag(args, "--screenshot");
    const sniffRaw = flag(args, "--sniff-raw");
    const dumpChr = flag(args, "--dump-chr");
    const patchChr = flag(args, "--patch-chr"); // operand = hex offset into CHR
    const patchPrg = flag(args, "--patch-prg"); // operand = hex offset into PRG
    const doLs = has(args, "--ls");
    const sramOnly = has(args, "--sram-only");
    const showSong = has(args, "--show-song");
    const doSniff = has(args, "--sniff");
    const doInfo = has(args, "--info");
    const doDf = has(args, "--df");
    const mkdir = flag(args, "--mkdir");
    const rm = flag(args, "--rm");
    const isPatch = patchChr != null || patchPrg != null;

    if (sramOnly && !srmPath) throw new Error("--sram-only requires --srm <save.srm>");
    if (patchChr != null && patchPrg != null) throw new Error("pass only one of --patch-chr / --patch-prg");

    // Read local files up front (fail fast, before touching hardware).
    const readOnly =
      doLs || dumpPath != null || showSong || sramOnly || getFile != null || screenshot != null ||
      doSniff || sniffRaw != null || dumpChr != null || isPatch || doInfo || doDf || mkdir != null || rm != null;
    // --get-file <sd-path> <local-dest>: the SD source is the flag operand, the local destination the positional.
    const getFileDest = getFile != null ? positional(args) : undefined;
    if (getFile != null && !getFileDest)
      throw new Error("--get-file requires a local destination: --get-file <sd-path> <local-dest>");
    // --patch-chr/--patch-prg <hex-offset> <file>: the offset is the flag operand, the local file the positional.
    const patchFile = isPatch ? positional(args) : undefined;
    if (isPatch && !patchFile)
      throw new Error("--patch-chr/--patch-prg requires a data file: --patch-chr <hex-offset> <file>");
    const romPath =
      getFile != null || isPatch ? undefined : positional(args) ?? (sdPath || readOnly ? undefined : DEFAULT_ROM);
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

      if (doInfo) {
        // Device identity + voltages (CMD_SYS_INF + CMD_GET_VDC). Works at the menu or in a game.
        printInfo(n8.edio.sysInfo(), n8.edio.vdc());
        return;
      }
      if (doDf) {
        const free = n8.edio.freeSpace();
        // Our N8 firmware reports 0 for CMD_F_AVB (the FAT free-cluster count isn't populated) even though
        // mkdir/rm/ls work; be honest rather than print a bogus 0 GB. May report a real value on other units.
        if (free === 0) console.log("SD free space: 0 (this N8 firmware doesn't report free space via CMD_F_AVB)");
        else console.log(`SD free space: ${(free / 0x40000000).toFixed(2)} GB (${free} bytes)`);
        return;
      }
      if (mkdir != null) {
        n8.edio.dirMake(mkdir);
        console.log(`made directory: ${mkdir}`);
        return;
      }
      if (rm != null) {
        n8.edio.fileDelete(rm);
        console.log(`deleted: ${rm}`);
        return;
      }
      if (doLs) {
        const path = flag(args, "--ls") ?? "/";
        const entries = n8.listDir(path === "/" ? "" : path);
        console.log(`${path} (${entries.length} entr${entries.length === 1 ? "y" : "ies"}):`);
        for (const e of entries) console.log(e.isDir ? `  [DIR]  ${e.name}` : `  ${String(e.size).padStart(8)}  ${e.name}`);
        return;
      }
      if (getFile != null) {
        const bytes = n8.readFile(getFile);
        if (!s.backend.writeFile(getFileDest!, bytes)) throw new Error(`write failed: ${getFileDest}`);
        console.log(`read ${bytes.length} bytes of ${getFile} -> ${getFileDest}`);
        return;
      }
      if (screenshot != null) {
        n8.menu.test(); // clean "is the menu running?" error before the big raw VRAM read
        const { vram, palette } = n8.menu.vramDump();
        const chr = n8.edio.memRD(ADDR_MENU_CHR, 8192);
        const img = menuScreenToRgba(chr, vram, palette);
        const png = s.backend.pngEncode(img.width, img.height, img.rgba);
        if (!png) throw new Error("PNG encode failed");
        if (!s.backend.writeFile(screenshot, png)) throw new Error(`write failed: ${screenshot}`);
        console.log(`wrote ${img.width}x${img.height} menu screenshot -> ${screenshot}`);
        return;
      }
      if (doSniff || sniffRaw != null) {
        // Read the FPGA's live save-state sniffer of a RUNNING game (no menu.test - the sniffer is off at
        // the menu, exactly where '*t' answers, so requiring the menu would be backwards).
        const region = n8.edio.memRD(ADDR_SSR, SNIFFER_REGION_SIZE);
        if (sniffRaw != null) {
          if (!s.backend.writeFile(sniffRaw, region)) throw new Error(`write failed: ${sniffRaw}`);
          console.log(`wrote ${region.length} bytes of raw sniffer region -> ${sniffRaw}`);
        }
        if (doSniff) printSniffer(decodeSniffer(region));
        return;
      }
      if (dumpChr != null) {
        // Grab the 8 KB visible CHR bank (edit it, then --patch-chr it back). Read-only. A .png dest renders
        // the tiles as an editable grayscale grid; anything else writes the raw 8 KB.
        const chr = n8.edio.memRD(ADDR_CHR, 8192);
        if (isPng(dumpChr)) {
          const img = chrToPng(chr);
          const png = s.backend.pngEncode(img.width, img.height, img.rgba);
          if (!png) throw new Error("PNG encode failed");
          if (!s.backend.writeFile(dumpChr, png)) throw new Error(`write failed: ${dumpChr}`);
          console.log(`wrote ${img.width}x${img.height} grayscale CHR tile grid -> ${dumpChr}`);
        } else {
          if (!s.backend.writeFile(dumpChr, chr)) throw new Error(`write failed: ${dumpChr}`);
          console.log(`wrote ${chr.length} bytes of CHR (bank 0) -> ${dumpChr}`);
        }
        return;
      }
      if (isPatch) {
        // Live-patch a RUNNING game's CHR (graphics) or PRG (code) - the write-twin of --sniff. The console
        // fetches the same PSRAM, so the change shows on the next PPU/CPU fetch. assertGameRegion keeps it out
        // of the N8 OS region; writeMemDirect verifies the readback. For --patch-chr, a .png file is decoded
        // from the grayscale tile grid; else raw bytes. --patch-prg is raw only (PRG isn't tiles).
        const offset = parseInt((patchChr ?? patchPrg)!, 16);
        if (Number.isNaN(offset)) throw new Error(`bad patch offset (expected hex): ${patchChr ?? patchPrg}`);
        const raw = readOrThrow(s, patchFile!, "patch data");
        if (patchPrg != null && isPng(patchFile!))
          throw new Error("--patch-prg takes raw bytes, not a PNG (PRG is code, not tiles)");
        let bytes = raw;
        if (patchChr != null && isPng(patchFile!)) {
          const img = s.backend.pngDecode(raw);
          if (!img) throw new Error(`not a valid PNG: ${patchFile}`);
          bytes = pngToChr(img);
        }
        assertGameRegion(offset, bytes.length);
        const region = patchChr != null ? "CHR" : "PRG";
        const n = n8.writeMemDirect((patchChr != null ? ADDR_CHR : ADDR_PRG) + offset, bytes);
        console.log(`patched + verified ${n} bytes into ${region} at +0x${offset.toString(16)} (live, running game)`);
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
