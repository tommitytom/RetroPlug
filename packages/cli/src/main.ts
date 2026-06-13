// retroplug CLI — the txiki-hosted TypeScript end-user CLI (no Node at runtime).
//
// Bundled to QuickJS bytecode and embedded in retroplug-cli; the C++ host runs
// it by default (no --test), exposes argv via getArgv() and the exit code via
// exit(), and routes all emulator control + file I/O through the in-process RPC
// service (createEmu). This replaces the old C++ JSON `--script` render path.
//
//   retroplug-cli --script S.json [--rom R] [--out O.wav] [--duration MS]
//                 [--sample-rate SR] [--block-size BS]
//                 [--screenshot-dir D] [--final-screenshot] [--per-system-wav]
//                 [--event-logs DIR] [--save-rplg P] [--save-sav P]

import {
  createEmu, harnessRpcSend, hostArgv, hostExit, Button, Routing,
} from "@retroplug/retroplug";

const emu = createEmu(harnessRpcSend());

// -- tiny path helpers (txiki has no node:path) ------------------------------
const dirname = (p: string): string => {
  const i = p.lastIndexOf("/");
  return i < 0 ? "" : p.slice(0, i);
};
const basename = (p: string, ext = ""): string => {
  const b = p.slice(p.lastIndexOf("/") + 1);
  return ext && b.endsWith(ext) ? b.slice(0, -ext.length) : b;
};
const extname = (p: string): string => {
  const b = p.slice(p.lastIndexOf("/") + 1);
  const i = b.lastIndexOf(".");
  return i <= 0 ? "" : b.slice(i);
};
const join = (a: string, b: string): string => (a ? `${a}/${b}` : b);

// -- script model ------------------------------------------------------------
interface ScriptKitSample { path: string; name: string; }
interface ScriptKitPatch { slot: number; name: string; samples: ScriptKitSample[]; }
interface ScriptEvent {
  at_ms?: number;
  button?: string; down?: boolean;
  tap?: string; hold_ms?: number;
  chord?: string[]; stagger_ms?: number;
  midi?: number[];
  screenshot?: string;
  system?: number;
  set_transport?: boolean;
  set_bpm?: number;
  patch_kit?: ScriptKitPatch;
}
interface ScriptSystem {
  rom: string;
  link_group?: number;
  lsdj_sync_mode?: string;
  bios_path?: string;
}
interface Script {
  rom?: string;
  systems?: ScriptSystem[];
  midi_routing?: string;
  duration_ms?: number;
  sample_rate?: number;
  block_size?: number;
  out_wav?: string;
  bpm?: number;
  transport_running?: boolean;
  events?: ScriptEvent[];
}

// GameboyButton / NesButton / GbaButton share the position-aligned encoding
// (Right=0..Start=7); L/R are GBA-only wire bytes 8/9.
const BUTTONS: Record<string, number> = {
  right: Button.Right, left: Button.Left, up: Button.Up, down: Button.Down,
  a: Button.A, b: Button.B, select: Button.Select, start: Button.Start,
  l: 8, r: 9,
};
function parseButton(name: string): number {
  const b = BUTTONS[name.toLowerCase()];
  if (b === undefined) throw new Error(`unknown button name: ${name}`);
  return b;
}
const ROUTINGS: Record<string, number> = {
  SendToAll: Routing.SendToAll,
  FourChannelsPerInstance: Routing.FourChannelsPerInstance,
  OneChannelPerInstance: Routing.OneChannelPerInstance,
  MidiChannelToInstance: Routing.MidiChannelToInstance,
};

// -- argv parsing ------------------------------------------------------------
interface Args {
  script?: string; rom?: string; out?: string; duration?: number;
  sampleRate?: number; blockSize?: number;
  screenshotDir?: string; finalScreenshot: boolean; perSystemWav: boolean;
  eventLogs?: string; saveRplg?: string; saveSav?: string;
}
function parseArgs(argv: string[]): Args {
  const a: Args = { finalScreenshot: false, perSystemWav: false };
  const need = (i: number, flag: string): string => {
    if (i + 1 >= argv.length) throw new Error(`${flag} requires a value`);
    return argv[i + 1];
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    switch (arg) {
      case "--script":         a.script = need(i, arg); i++; break;
      case "--rom":            a.rom = need(i, arg); i++; break;
      case "--out":            a.out = need(i, arg); i++; break;
      case "--duration":       a.duration = Number(need(i, arg)); i++; break;
      case "--sample-rate":    a.sampleRate = Number(need(i, arg)); i++; break;
      case "--block-size":     a.blockSize = Number(need(i, arg)); i++; break;
      case "--screenshot-dir": a.screenshotDir = need(i, arg); i++; break;
      case "--final-screenshot": a.finalScreenshot = true; break;
      case "--per-system-wav":   a.perSystemWav = true; break;
      case "--event-logs":     a.eventLogs = need(i, arg); i++; break;
      case "--save-rplg":      a.saveRplg = need(i, arg); i++; break;
      case "--save-sav":       a.saveSav = need(i, arg); i++; break;
      default:
        throw new Error(`unknown argument: ${arg}`);
    }
  }
  return a;
}

const USAGE = `Usage: retroplug-cli --script PATH [options]
  --script PATH         JSON file describing rom(s) + timed events (required)
  --rom PATH            override ROM (single-system scripts only)
  --out PATH            override output WAV path
  --duration MS         override duration_ms
  --sample-rate SR      override sample_rate (default 44100)
  --block-size BS       override block_size (default 1024)
  --screenshot-dir D    directory for screenshot PNGs
  --final-screenshot    dump every system's final frame at script end
  --per-system-wav      also write one WAV per system (SameBoy-only)
  --event-logs DIR      write per-system MIDI + serial-out logs
  --save-rplg PATH      snapshot the project to a .rplg at end
  --save-sav PATH       dump system 0's battery RAM at end`;

// -- unified event timeline --------------------------------------------------
type TimelineEvent =
  | { ms: number; kind: "press"; sys: number; button: number; down: boolean }
  | { ms: number; kind: "midi"; bytes: number[] }
  | { ms: number; kind: "transport"; setTransport?: boolean; setBpm?: number }
  | { ms: number; kind: "kit"; sys: number; patch: ScriptKitPatch }
  | { ms: number; kind: "screenshot"; sys: number; name: string };

function buildTimeline(events: ScriptEvent[], systemCount: number): TimelineEvent[] {
  const out: TimelineEvent[] = [];
  events.forEach((e, i) => {
    const at = e.at_ms ?? 0;
    const sys = e.system ?? 0;
    const forms = [e.button, e.tap, e.chord, e.midi, e.screenshot, e.patch_kit]
      .filter((x) => x !== undefined).length
      + (e.set_transport !== undefined || e.set_bpm !== undefined ? 1 : 0);
    if (forms === 0) throw new Error(`event #${i} has no input form`);
    if (forms > 1) throw new Error(`event #${i} mixes multiple input forms`);
    const checkSys = () => {
      if (sys >= systemCount) throw new Error(`event #${i} system ${sys} out of range`);
    };
    if (e.button !== undefined) {
      checkSys();
      if (e.down === undefined) throw new Error(`event #${i} 'button' requires 'down'`);
      out.push({ ms: at, kind: "press", sys, button: parseButton(e.button), down: e.down });
    } else if (e.tap !== undefined) {
      checkSys();
      const hold = e.hold_ms ?? 50;
      const btn = parseButton(e.tap);
      out.push({ ms: at, kind: "press", sys, button: btn, down: true });
      out.push({ ms: at + hold, kind: "press", sys, button: btn, down: false });
    } else if (e.chord !== undefined) {
      checkSys();
      if (e.chord.length !== 2) throw new Error(`event #${i} 'chord' needs 2 buttons`);
      const stagger = e.stagger_ms ?? 200;
      const hold = e.hold_ms ?? 200;
      const mod = parseButton(e.chord[0]);
      const key = parseButton(e.chord[1]);
      out.push({ ms: at, kind: "press", sys, button: mod, down: true });
      out.push({ ms: at + stagger, kind: "press", sys, button: key, down: true });
      out.push({ ms: at + stagger + hold, kind: "press", sys, button: key, down: false });
      out.push({ ms: at + 2 * stagger + hold, kind: "press", sys, button: mod, down: false });
    } else if (e.midi !== undefined) {
      out.push({ ms: at, kind: "midi", bytes: e.midi });
    } else if (e.screenshot !== undefined) {
      checkSys();
      if (!e.screenshot) throw new Error(`event #${i} 'screenshot' name must be non-empty`);
      out.push({ ms: at, kind: "screenshot", sys, name: e.screenshot });
    } else if (e.patch_kit !== undefined) {
      checkSys();
      if (e.patch_kit.slot >= 16) throw new Error(`event #${i} 'patch_kit.slot' must be 0..15`);
      out.push({ ms: at, kind: "kit", sys, patch: e.patch_kit });
    } else {
      out.push({ ms: at, kind: "transport", setTransport: e.set_transport, setBpm: e.set_bpm });
    }
  });
  // Stable sort by ms (Array.prototype.sort is stable in modern engines).
  return out.map((e, i) => ({ e, i }))
    .sort((x, y) => x.e.ms - y.e.ms || x.i - y.i)
    .map(({ e }) => e);
}

// -- main --------------------------------------------------------------------
function run(): number {
  const args = parseArgs(hostArgv());
  if (!args.script) {
    console.error("--script is required\n" + USAGE);
    return 2;
  }

  let script: Script;
  try {
    script = JSON.parse(new TextDecoder().decode(emu.readFile(args.script))) as Script;
  } catch (e: any) {
    console.error(`failed to parse script ${args.script}: ${e?.message ?? e}`);
    return 1;
  }

  // CLI overrides win over script fields.
  const out = args.out ?? script.out_wav;
  const durationMs = args.duration ?? script.duration_ms ?? 0;
  const sampleRate = args.sampleRate ?? script.sample_rate ?? 44100;
  const routing = ROUTINGS[script.midi_routing ?? "SendToAll"];
  if (routing === undefined) throw new Error(`unknown midi_routing: ${script.midi_routing}`);

  // Normalize systems: legacy `rom` -> one-element array; --rom overrides it.
  let systems: ScriptSystem[];
  if (script.systems && script.systems.length) {
    systems = script.systems;
  } else if (args.rom || script.rom) {
    systems = [{ rom: (args.rom ?? script.rom)!, lsdj_sync_mode: undefined }];
  } else {
    console.error("script has no 'rom' or 'systems'");
    return 2;
  }
  if (args.rom && systems.length === 1) systems[0].rom = args.rom;

  // Load each system (C++ side detects the ROM format).
  const ids = systems.map((s) =>
    emu.loadRom(s.rom, undefined, s.lsdj_sync_mode, s.link_group ?? 0));

  // Initial simulated host transport.
  emu.setBpm(script.bpm ?? 120);
  emu.setTransport(script.transport_running ?? false);

  const timeline = buildTimeline(script.events ?? [], systems.length);

  // Screenshot dir + filename: <scriptStem>_<name>_sys<idx>.png.
  const scriptStem = basename(args.script, ".json");
  const shotDir = args.screenshotDir ?? ((out ? dirname(out) : "") || ".");
  const shotPath = (name: string, idx: number) =>
    join(shotDir, `${scriptStem}_${name}_sys${idx}.png`);

  // Per-system WAV paths derived from `out`: <stem>_sys<i><ext>.
  let perSysPaths: string[] = [];
  if (args.perSystemWav) {
    if (!out) { console.error("--per-system-wav requires an out WAV"); return 1; }
    const stem = basename(out, extname(out));
    const ext = extname(out) || ".wav";
    const d = dirname(out) || ".";
    perSysPaths = ids.map((_, i) => join(d, `${stem}_sys${i}${ext}`));
  }

  const wav = !!out;
  if (wav) emu.renderBegin(out!, perSysPaths, sampleRate);
  const advance = (toMs: number) => {
    const d = toMs - cur;
    if (d <= 0) return;
    if (wav) emu.renderChunk(d); else emu.runMs(d);
    cur = toMs;
  };
  let cur = 0;

  for (const ev of timeline) {
    advance(ev.ms);
    switch (ev.kind) {
      case "press": emu.press(ids[ev.sys], ev.button, ev.down); break;
      case "midi": emu.dispatchMidi(ev.bytes, routing); break;
      case "transport":
        if (ev.setBpm !== undefined) emu.setBpm(ev.setBpm);
        if (ev.setTransport !== undefined) emu.setTransport(ev.setTransport);
        break;
      case "kit": emu.patchKit(ids[ev.sys], ev.patch.slot, ev.patch.name, ev.patch.samples); break;
      case "screenshot": emu.screenshot(ids[ev.sys], shotPath(ev.name, ev.sys)); break;
    }
  }
  advance(durationMs); // render the tail
  if (wav) emu.renderEnd();

  // Final screenshots.
  if (args.finalScreenshot)
    ids.forEach((id, i) => emu.screenshot(id, shotPath("final", i)));

  // Event logs: <stem>_midi_sys<N>.txt / <stem>_serial_sys<N>.txt.
  if (args.eventLogs) {
    const enc = new TextEncoder();
    ids.forEach((id, i) => {
      const midi = emu.drainMidi(id);
      const lines = midi.map((m) =>
        `${m.sample} ${m.bytes.map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);
      emu.writeFile(join(args.eventLogs!, `${scriptStem}_midi_sys${i}.txt`),
        enc.encode(lines.join("\n") + (lines.length ? "\n" : "")).buffer as ArrayBuffer);
      const serial = emu.drainSerial(id);
      const slines = serial.map((s) => `${s.sample} ${s.byte.toString(16).padStart(2, "0")}`);
      emu.writeFile(join(args.eventLogs!, `${scriptStem}_serial_sys${i}.txt`),
        enc.encode(slines.join("\n") + (slines.length ? "\n" : "")).buffer as ArrayBuffer);
    });
  }

  if (args.saveRplg) emu.saveRplg(args.saveRplg);
  if (args.saveSav) {
    const sram = emu.saveSram(ids[0]);
    emu.writeFile(args.saveSav, sram.buffer as ArrayBuffer);
  }

  return 0;
}

try {
  hostExit(run());
} catch (e: any) {
  console.error(`error: ${e?.stack ?? e?.message ?? e}`);
  hostExit(1);
}
