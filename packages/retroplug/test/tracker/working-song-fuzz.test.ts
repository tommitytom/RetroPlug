// A randomized model of the smsggdj cart, driven against everything on the host that touches its working
// song: the per-frame slot watcher, Save into the live battery, the carries across reset / reload / FM /
// Swap ROM, the Songs-menu rows that cold-boot the cart, Load SRAM / Load State / New SRAM, song requests
// (by index from the menu, by name from Recent), project export and import, and two instances side by side.
//
// The cart is modelled from its own source (smsggdj v0.45 rle.asm / engine.asm / editor.asm), including the
// parts that take more than one audio block: a FILES SAVE frees the old copy first (rss_free_old) and clears
// song_edited only once the entry is written; a FILES LOAD writes the name and echo, then the block, then
// clears the flag. The model keeps its OWN ground truth - which song the user has, whether it is unsaved,
// which battery row it came from - and after every step checks the host against it:
//
//   1. no silent loss - unsaved work in no slot is reported, and every host op that destroys it asked first
//   2. no clobber - a host Save overwrites only the row the song came from (or a copy, or the blank song)
//   3. a Save that says "saved" really is in the battery, and the cart's flag clears
//   4. reset / reload / FM / Swap ROM give the song back exactly, still unsaved if it was
//   5. an action aimed at one instance never touches the other's battery or working song
//   6. nothing throws, and nothing waits forever while the cart is up
//
// The fixed seeds below run in `pnpm test`. FUZZ_ITER=<n> runs n random sequences instead, shrinks every
// distinct violation to a minimal sequence and prints it (FUZZ_SEED, FUZZ_LEN, FUZZ_STALL and FUZZ_MS tune
// it; FUZZ_KNOWN=1 steps around the bugs already found, FUZZ_RACE=1 lets host Saves land inside the cart's
// own routines, FUZZ_STRICT_FLAG=1 fails on a saved song the cart still calls UNSAVED). FUZZ_REPLAY=<json>
// traces one sequence; FUZZ_SCAN=<n> reports which of the first n permanent seeds pass.
import { test } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { hasUnsavedChanges, unsavedChanges } from "../../src/unsavedChanges";
import { saveWorkingSong, savEditWouldDiscard, songLoadWouldDiscard, mutateLiveSav } from "../../src/tracker/liveSav";
import { smsggdjSongCatalog } from "../../src/tracker/smsggdjSongCatalog";
import { resolveSmsggdjLayout, commonConfigOffsets } from "../../src/smsggdj/runtime/layout";
import {
  addSong,
  buildSav,
  isSmsggdjSav,
  listSongs,
  packDirectory,
  readConfig,
  readSongBlock,
  readSongEcho,
  replaceSong,
  sanitizeEcho,
  unwrapSong,
  wrapSong,
  SMDJ4_BLOCK_LEN,
} from "../../src/smsggdj/codec/sav";
import type { WorkingSongImage } from "../../src/tracker/songCatalog";
import { decodeWorkingSong } from "../../src/tracker/workingSongBlob";
import type { ConstructSpec } from "../../src/backend";
import { smsggdjRom, smsggdjRam } from "../systems/fixtures";

// saveWorkingSong polls on the UI timer, which nothing pumps here: its first, synchronous look decides.
(globalThis as any).__rp_timersPumped = false;

// The host's clock is the model's: carries and exports key windows on Date.now, and a fuzzer that ran in
// real time would cross them at random. The real one stays for the wall-clock bounds.
const realNow = Date.now.bind(Date);
let vclock = 1_000_000;
Date.now = () => vclock;

const CART = 32 * 1024;
const L = resolveSmsggdjLayout("0.45")!;
const CFG = commonConfigOffsets()!; // pal, sync, vid, fm, cont, key delay, key speed
const CONT = 4;
const DEF_SETTINGS = [0, 0, 0, 0, 0, 0x0e, 3];
// Echo as the cart stores it: the editor's default (already in sanitizeEcho's range), one real setting,
// and the raw zeros a slot written with echo off carries (its FILES load copies them unsanitized).
const ECHO = [sanitizeEcho(new Uint8Array(8)), Uint8Array.of(2, 4, 8, 0, 0, 0, 0, 0), new Uint8Array(8)];
const NAMES = ["", "A", "B"];
const GARBAGE = 9; // a block caught half-unpacked by a FILES LOAD
const MAXDIR = 6;
const GROOVE = 0x1a00 + 3; // a groove tick playback's `T` command rewrites without setting song_edited
const ROMS = ["/roms/smsggdj.sms", "/roms/smsggdj-047.sms"];
const entryAt = (r: number): number => 0x20 + 32 * r;

// --- songs, as the model sees them ---------------------------------------------------------------------

interface Song {
  tag: number; // the block's pattern; 0 is song_new's blank block
  drift: boolean; // a groove tick rewritten by playback
  name: string;
  echo: number; // index into ECHO
}
interface Truth extends Song {
  edited: boolean; // the user has unsaved work in it
  cont: number;
  // CONT changed since the last save or load. The host counts that as unsaved (the cart sets song_edited
  // for it), but only until a boot: config_load always boots CONT to OFF, so no reboot can keep it.
  contEdited?: boolean;
}
const BLANK: Song = { tag: 0, drift: false, name: "", echo: 0 };
// Echo is compared sanitized everywhere on the host, so the default and the raw zeros are one value.
const key = (s: Song): string => `${s.tag}${s.drift ? "~" : ""}${s.name || "_"}${s.echo === 1 ? "x" : s.echo < 0 ? "?" : ""}`;
const same = (a: Song, b: Song): boolean => key(a) === key(b);
const contentOf = (s: Song): Song => ({ tag: s.tag, drift: s.drift, name: s.name, echo: s.echo });

const blocks = new Map<string, Uint8Array>();
function blockOf(tag: number, drift: boolean): Uint8Array {
  const k = `${tag}${drift}`;
  let b = blocks.get(k);
  if (!b) {
    b = new Uint8Array(SMDJ4_BLOCK_LEN);
    if (tag) for (let i = 0; i < SMDJ4_BLOCK_LEN; i += 4) b.set([tag, 0xff, 0, 0], i);
    if (drift) b[GROOVE] ^= 0x07;
    blocks.set(k, b);
  }
  return b;
}
function tagOf(b: Uint8Array): { tag: number; drift: boolean } {
  const t = b[0];
  for (let i = 0; i < SMDJ4_BLOCK_LEN; i++) {
    if (i === GROOVE) continue;
    const want = t === 0 ? 0 : (i & 3) === 0 ? t : (i & 3) === 1 ? 0xff : 0;
    if (b[i] !== want) return { tag: -1, drift: false };
  }
  return { tag: t, drift: b[GROOVE] !== 0 };
}
function echoIdx(e: Uint8Array): number {
  for (let k = 0; k < ECHO.length; k++) if (ECHO[k].every((x, i) => x === e[i])) return k;
  return -1;
}
const nameBytes = (s: string): Uint8Array => Uint8Array.from({ length: 8 }, (_, i) => (i < s.length ? s.charCodeAt(i) : 0x20));
function displayName(bytes: Uint8Array): string {
  let s = "";
  for (const c of bytes) {
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.replace(/\s+$/, "");
}
const songOfImage = (img: WorkingSongImage): Song => ({ ...tagOf(img.block), name: displayName(img.name), echo: echoIdx(img.echo) });

// --- work RAM and the battery ---------------------------------------------------------------------------

interface Ram {
  song: Song;
  edited: boolean;
  booted: boolean;
  settings: number[];
  playing: boolean; // play_state: playback drift happens only while it runs
}
function encodeRam(r: Ram): Uint8Array {
  const ram = smsggdjRam({ block: blockOf(r.song.tag, r.song.drift), edited: r.edited, booted: r.booted });
  ram.set(nameBytes(r.song.name), L.name);
  ram.set(ECHO[r.song.echo < 0 ? 0 : r.song.echo], L.echo);
  CFG.forEach((o, i) => (ram[o] = r.settings[i]));
  ram[L.playState] = r.playing ? 1 : 0;
  return ram;
}
function decodeRam(ram: Uint8Array): Ram {
  return {
    song: { ...tagOf(ram.subarray(L.song, L.song + SMDJ4_BLOCK_LEN)), name: displayName(ram.subarray(L.name, L.name + 8)), echo: echoIdx(ram.subarray(L.echo, L.echo + 8)) },
    edited: ram[L.edited] !== 0,
    booted: ram[L.booted] === 1,
    settings: CFG.map((o) => ram[o]),
    playing: ram[L.playState] !== 0,
  };
}
/** The cart a boot leaves: song_new's blank song, OPTIONS restored by config_load - except CONT, which
 *  config_load always boots to OFF ("a per-set performance choice, not persisted"). */
function bootRam(opts: number[] | null): Uint8Array {
  const settings = opts ? [...opts] : [...DEF_SETTINGS];
  settings[CONT] = 0;
  return encodeRam({ song: BLANK, edited: false, booted: true, settings, playing: false });
}

interface Dir {
  songs: Song[];
  opts: number[] | null;
}
function optsBlock(s: number[]): Uint8Array {
  const out = new Uint8Array(10);
  out[0] = 0x43;
  out[1] = 0x46;
  let sum = 0;
  s.forEach((b, i) => {
    out[2 + i] = b;
    sum = (sum + b) & 0xff;
  });
  out[9] = sum;
  return out;
}
function decodeDir(sram: Uint8Array): Dir {
  const cfg = readConfig(sram);
  const opts = cfg ? Array.from(cfg) : null;
  if (!isSmsggdjSav(sram)) return { songs: [], opts };
  const p = packDirectory(sram);
  const songs = listSongs(p).map((s) => {
    const b = readSongBlock(p, s.index);
    return { ...(b ? tagOf(b) : { tag: -2, drift: false }), name: s.name, echo: echoIdx(readSongEcho(p, s.index)!) };
  });
  return { songs, opts };
}
function encodeDir(songs: Song[], opts: number[] | null): Uint8Array {
  const sav = buildSav(
    songs.map((s) => ({ block: blockOf(s.tag, s.drift), name: s.name, echo: ECHO[s.echo < 0 ? 0 : s.echo] })),
    CART,
    opts ? optsBlock(opts) : undefined,
  );
  if (!sav) throw new ModelError("buildSav refused the model's battery");
  return sav;
}

// --- the PRNG -------------------------------------------------------------------------------------------

function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// --- steps ----------------------------------------------------------------------------------------------

const WEIGHTS: [string, number][] = [
  // the cart's own screens
  ["edit", 6], ["echo", 2], ["name", 2], ["undo", 2], ["cont", 2], ["opt", 1], ["drift", 2],
  ["csave", 4], ["cload", 4], ["cnew", 1], ["cclear", 1], ["crename", 1],
  // time passing: an audio block, a UI frame, or the user waiting for the cart to finish
  ["block", 10], ["frame", 10], ["settle", 2],
  // the host
  ["hsave", 4], ["hload", 3], ["hname", 2], ["hdel", 1], ["hmove", 1], ["hadd", 1], ["hrepl", 1],
  ["hlsram", 1], ["hlstate", 1], ["hsstate", 1], ["hnsram", 1], ["hreset", 2], ["hreload", 1], ["hfm", 1],
  ["hswap", 1], ["export", 2], ["focus", 2], ["prompt", 1],
];
const TOTAL = WEIGHTS.reduce((n, [, w]) => n + w, 0);
interface Step {
  k: string;
  i: number; // which instance
  a: number;
  b: number;
  c: number;
}
function gen(r: () => number, n: number): Step[] {
  const out: Step[] = [];
  for (let s = 0; s < n; s++) {
    let x = r() * TOTAL;
    let k = WEIGHTS[0][0];
    for (const [kind, w] of WEIGHTS) {
      if ((x -= w) < 0) {
        k = kind;
        break;
      }
    }
    out.push({ k, i: Math.floor(r() * 2), a: Math.floor(r() * 1000), b: Math.floor(r() * 1000), c: Math.floor(r() * 1000) });
  }
  return out;
}

class Violation extends Error {
  constructor(
    public inv: string,
    msg: string,
  ) {
    super(`${inv}: ${msg}`);
  }
}
class ModelError extends Error {}

// --- the world -------------------------------------------------------------------------------------------

/** One cart's micro-op: what the cart does on one audio block of a routine that spans several. `abort` is
 *  the bookkeeping when the core dies with it still pending. */
interface Op {
  label: string;
  run: () => void;
  abort?: () => void;
}
/** A published savestate: the machine at one moment, plus what the model knew then. */
interface Snap {
  ram: Uint8Array;
  sram: Uint8Array;
  uids: number[];
  origin: number | null;
  originContent: Song | null;
  lineage: number[];
  truth: Truth | null; // null: published inside a boot window, holding the blank cart
  lostWork: boolean; // the user's unsaved song was in a carry, not in this savestate
  booting: number;
}
interface Inst {
  k: number;
  id: number;
  rom: string;
  uids: number[]; // one per battery row, aligned with the packed directory
  origin: number | null; // the row the working song was loaded from or last saved into
  originContent: Song | null;
  // Every row this song was loaded from or saved into since it last became a different song: its own
  // earlier versions, which a Save writing back over loses nothing the user did not replace themselves.
  lineage: number[];
  truth: Truth;
  booting: number; // audio blocks until the cart's boot latch goes up
  ops: Op[];
  published: Snap | null;
  lastPublish: number;
  restoredFrom: Snap | null;
  revert: { truth: Truth; origin: number | null; originContent: Song | null; lineage: number[] } | null; // a host load not yet landed
  restoring: number;
  waiting: number;
  midSave: boolean; // between rss_free_old and the entry write
}
interface Req {
  index?: number;
  name?: string;
  systemId?: number;
  confirmed: boolean; // the user agreed to lose unsaved work (the request itself may say more)
  from: number; // the instance it was aimed at
}

/** Findings that are not invariant breaks, counted for the report. */
type Notes = Record<string, number>;

class World {
  be = new MockBackend("/cfg");
  project = new ProjectStore(this.be, new RecentStore(this.be), buildAppRegistry());
  systems = this.project.systems;
  insts: Inst[] = [];
  queue: [number, number, Uint8Array][] = []; // host writeRam, waiting for the audio thread
  ramWrites: number[] = [];
  sramVer = new Map<number, number>();
  dirCache = new Map<string, Dir>();
  registry: Snap[] = [];
  states: string[] = [];
  savFiles: string[] = [];
  smdjFiles: string[] = [];
  importQueue: number[] = [];
  req: Req | null = null;
  uidN = 0;
  fileN = 0;
  r: () => number;
  trace?: string[];
  notes: Notes;
  stall: number;

  constructor(seed: number, notes: Notes, stall: number, trace?: string[]) {
    this.r = rng(seed);
    this.notes = notes;
    this.stall = stall;
    this.trace = trace;
    vclock = 1_000_000;
    const be = this.be;
    // The audio thread: host writeRam lands only when a block runs (see block()).
    be.writeRam = (id, off, bytes) => {
      this.queue.push([id, off, bytes.slice()]);
      this.ramWrites.push(id);
      return true;
    };
    const realSetSram = be.setSram.bind(be);
    be.setSram = (id, bytes) => {
      realSetSram(id, bytes);
      this.sramVer.set(id, (this.sramVer.get(id) ?? 0) + 1);
    };
    // SRAM writes are never deferred: the mock applies the compare-and-swap synchronously.
    const realWriteSram = be.writeSram.bind(be);
    be.writeSram = (id, off, bytes, expect) => {
      const ok = realWriteSram(id, off, bytes, expect);
      this.sramVer.set(id, (this.sramVer.get(id) ?? 0) + 1);
      return ok;
    };
    const realConstruct = be.constructSystem.bind(be);
    be.constructSystem = (spec, id) => {
      const ok = realConstruct(spec, id);
      if (ok) this.onConstruct(spec, id);
      return ok;
    };
    // A savestate is the snapshot the core last PUBLISHED (every 0.5 s), frozen when it is read.
    be.readState = (id) => {
      const inst = this.insts.find((x) => x.id === id);
      if (!inst?.published) return null;
      this.registry.push({ ...inst.published, ram: inst.published.ram.slice(), sram: inst.published.sram.slice() });
      const n = this.registry.length - 1;
      return Uint8Array.of(0x46, 0x5a, n & 0xff, n >> 8);
    };
  }

  log(s: string): void {
    this.trace?.push(s);
  }
  note(k: string): void {
    this.notes[k] = (this.notes[k] ?? 0) + 1;
  }
  pick<T>(xs: T[], n: number): T {
    return xs[n % xs.length];
  }

  // --- setup ----------------------------------------------------------------------------------------------

  setup(a: number): void {
    const r0 = rng(a * 7 + 13);
    for (const rom of ROMS) this.be.seed(rom, smsggdjRom(rom === ROMS[0] ? "0.45" : "0.47"));
    const randomDir = (): Dir => {
      const n = 1 + Math.floor(r0() * 4);
      const songs: Song[] = [];
      for (let s = 0; s < n; s++) songs.push({ tag: 1 + Math.floor(r0() * 3), drift: false, name: NAMES[Math.floor(r0() * 3)], echo: Math.floor(r0() * 3) });
      return { songs, opts: r0() < 0.5 ? [...DEF_SETTINGS] : null };
    };
    for (let k = 0; k < 2; k++) {
      const id = this.systems.addSystem(ROMS[0])!;
      const d = randomDir();
      const sram = encodeDir(d.songs, d.opts);
      this.be.setSram(id, sram);
      this.be.seed(k === 0 ? "/roms/smsggdj.sav" : "/roms/smsggdj-2.sav", sram);
      const loaded = r0() < 0.7;
      const song = loaded ? d.songs[0] : BLANK;
      const uids = d.songs.map(() => ++this.uidN);
      const inst: Inst = {
        k,
        id,
        rom: ROMS[0],
        uids,
        origin: loaded ? uids[0] : null,
        lineage: loaded ? [uids[0]] : [],
        originContent: loaded ? { ...song } : null,
        truth: { ...song, edited: false, cont: 0 },
        booting: 0,
        ops: [],
        published: null,
        lastPublish: vclock,
        restoredFrom: null,
        revert: null,
        restoring: 0,
        waiting: 0,
        midSave: false,
      };
      const settings = d.opts ? [...d.opts] : [...DEF_SETTINGS];
      this.be.setRam(id, encodeRam({ song, edited: false, booted: true, settings, playing: false }));
      this.insts.push(inst);
      this.publish(inst);
    }
    // Files the host rows can pick: three batteries for Load SRAM, three songs for Add / Replace.
    for (let f = 0; f < 3; f++) {
      const d = randomDir();
      const p = `/savs/f${f}.sav`;
      this.be.seed(p, encodeDir(d.songs, d.opts));
      this.savFiles.push(p);
      const s = `/songs/${NAMES[f] || "S"}${f}.smdj4`;
      this.be.seed(s, wrapSong(blockOf(1 + f, false), ECHO[f])!);
      this.smdjFiles.push(s);
    }
    this.project.save("/proj/p.rplg");
  }

  // --- the model's view ------------------------------------------------------------------------------------

  dir(inst: Inst): Dir {
    const k = `${inst.id}:${this.sramVer.get(inst.id) ?? 0}`;
    let d = this.dirCache.get(k);
    if (!d) {
      d = decodeDir(this.be.readSram(inst.id)!);
      if (this.dirCache.size > 64) this.dirCache.clear();
      this.dirCache.set(k, d);
    }
    return d;
  }
  ram(inst: Inst): Ram {
    return decodeRam(this.be.readRam(inst.id)!);
  }
  /** files_enter (editor.asm): entering FILES stops the transport, unless CONT keeps it running. */
  enterFiles(inst: Inst): void {
    this.setRam(inst, (r) => {
      if (!r.settings[CONT]) r.playing = false;
    });
  }
  setRam(inst: Inst, f: (r: Ram) => void): void {
    const r = this.ram(inst);
    f(r);
    this.be.setRam(inst.id, encodeRam(r));
  }
  sys(inst: Inst) {
    const s = this.systems.systems().find((e) => e.id === inst.id);
    if (!s) throw new ModelError(`instance ${inst.k} (id ${inst.id}) is not in the store`);
    return s;
  }
  queued(inst: Inst): boolean {
    return this.queue.some((q) => q[0] === inst.id);
  }
  inBattery(d: Dir, s: Song): boolean {
    return d.songs.some((x) => same(x, s));
  }
  /** Unsaved work: edited, and either the song is in no slot or its CONT differs from the battery's. */
  atRisk(inst: Inst): boolean {
    const t = inst.truth;
    if (!t.edited) return false;
    const d = this.dir(inst);
    // song_new's blank song is no work, saved or not: only its CONT can be.
    if (!same(t, BLANK) && !this.inBattery(d, t)) return true;
    return !!t.contEdited && !!d.opts && d.opts[CONT] !== t.cont;
  }
  quiescent(inst: Inst): boolean {
    return inst.booting === 0 && inst.ops.length === 0 && !this.queued(inst) && !this.systems.isRestoringSong(inst.id);
  }
  desc(inst: Inst): string {
    const t = inst.truth;
    const rm = this.be.readRam(inst.id) ? this.ram(inst) : null;
    const carry = this.systems.pendingCarry(inst.id);
    return (
      `[${inst.k}] truth=${key(t)}${t.edited ? "*" : ""} ram=${rm ? key(rm.song) + (rm.edited ? "*" : "") + (rm.booted ? "" : "(boot)") : "-"}` +
      `${carry ? " carry=" + key(songOfImage(carry)) + (carry.edited ? "*" : "") : ""} dir=${this.dir(inst).songs.map(key).join(",")}` +
      ` origin=${inst.uids.indexOf(inst.origin ?? -1)} slot=${this.sys(inst).workingSlot?.index ?? "-"}${inst.ops.length ? " ops=" + inst.ops.map((o) => o.label).join("/") : ""}`
    );
  }

  // --- the core ------------------------------------------------------------------------------------------

  publish(inst: Inst): void {
    // Inside a boot window the savestate holds the booting cart, not the user's song (which is in a carry).
    const rm = inst.booting > 0 ? null : decodeRam(this.be.readRam(inst.id)!);
    const boot = !rm || !rm.booted || (this.systems.isRestoringSong(inst.id) && !same(rm.song, inst.truth));
    inst.published = {
      ram: this.be.readRam(inst.id)!,
      sram: this.be.readSram(inst.id)!,
      uids: [...inst.uids],
      origin: inst.origin,
      originContent: inst.originContent,
      lineage: [...inst.lineage],
      truth: boot ? null : { ...inst.truth },
      lostWork: boot && this.atRisk(inst),
      booting: inst.booting,
    };
    inst.lastPublish = vclock;
  }

  /** A core was built for one of the instances: the old one's pending routines die with it, and the new
   *  one starts from its seed - a savestate, or a battery and a boot. */
  onConstruct(spec: ConstructSpec, id: number): void {
    const inst =
      spec.replaceId !== undefined
        ? this.insts.find((x) => x.id === spec.replaceId)
        : this.importQueue.length
          ? this.insts[this.importQueue.shift()!]
          : undefined;
    if (!inst) return; // the initial addSystem: setup seeds it
    this.abortOps(inst);
    this.queue = this.queue.filter((q) => q[0] !== inst.id); // native drops writes for a dead core
    inst.id = id;
    inst.restoredFrom = null;
    inst.restoring = 0;
    inst.waiting = 0;
    if (spec.stateBytes) {
      const snap = this.registry[spec.stateBytes[2] | (spec.stateBytes[3] << 8)];
      if (!snap) throw new ModelError("savestate not in the registry");
      this.be.setSram(id, snap.sram.slice());
      this.be.setRam(id, snap.ram.slice());
      inst.booting = snap.booting;
      inst.restoredFrom = snap;
      inst.published = { ...snap };
      inst.lastPublish = vclock;
      return;
    } else {
      const s = new Uint8Array(CART);
      if (spec.sramBytes) s.set(spec.sramBytes.subarray(0, CART));
      this.be.setSram(id, s);
      this.be.setRam(id, smsggdjRam({ booted: false }));
      inst.booting = 1 + Math.floor(this.r() * 3);
    }
    this.publish(inst);
  }

  /** The core died: routines still running never finish, and what work RAM held is what there is. */
  abortOps(inst: Inst): void {
    const pendingLoad = inst.revert && this.queued(inst);
    if (!inst.ops.length && !pendingLoad) return;
    for (const op of inst.ops) op.abort?.();
    inst.midSave = false;
    if (inst.ops.length) {
      const rm = this.ram(inst);
      if (rm.booted) inst.truth = { ...rm.song, edited: rm.edited, cont: rm.settings[CONT], contEdited: inst.truth.contEdited };
      this.log(`    (aborted ${inst.ops.map((o) => o.label).join("/")})`);
      this.note("abort-mid-routine");
    }
    // A host load whose writes never reached the core is still the song: the host stands the LOADED song
    // in for the cart's until they land (SystemsStore.expectSong), so a reboot carries it and the user's
    // confirmed Load is what they get back. (The truth already says so; nothing to revert.)
    if (pendingLoad) this.note("abort-host-load");
    inst.ops = [];
    inst.revert = null;
  }

  /** One audio block: host writes land, then every cart runs one step of whatever it is doing. */
  block(): void {
    vclock += 3;
    const live = new Set(this.insts.map((x) => x.id));
    for (const [id, off, bytes] of this.queue.splice(0)) {
      if (!live.has(id)) continue;
      const ram = this.be.readRam(id)!;
      if (off + bytes.length <= ram.length) {
        ram.set(bytes, off);
        this.be.setRam(id, ram);
      }
    }
    for (const inst of this.insts) {
      inst.revert = null;
      if (inst.booting > 0) {
        if (--inst.booting === 0) this.be.setRam(inst.id, bootRam(this.dir(inst).opts));
      } else if (inst.ops.length) inst.ops.shift()!.run();
      if (vclock - inst.lastPublish >= 500 && inst.ops.length === 0) this.publish(inst);
    }
  }

  /** One UI frame, in the order the app runs it (useProjectModals). */
  frame(): void {
    vclock += 16;
    if (this.project.hasSongRequest()) this.settleRequest("frame");
    if (this.systems.hasCarries()) this.systems.settleCarries();
    this.systems.observeWorkingSongs();
    for (const inst of this.insts) {
      if (inst.booting === 0 && this.systems.isRestoringSong(inst.id)) {
        if (++inst.restoring > 400) throw new Violation("INV6-wedged", `instance ${inst.k} still restoring after ${inst.restoring} frames with its cart up`);
      } else inst.restoring = 0;
    }
  }

  /** The user waits for this cart to finish what it is doing (and to boot) before pressing anything - and a
   *  parked song request settles in the frames that pass meanwhile. */
  settleCart(inst: Inst): void {
    const busy = (): boolean =>
      inst.booting > 0 || inst.ops.length > 0 || this.queued(inst) || this.systems.isRestoringSong(inst.id) || this.project.hasSongRequest();
    for (let n = 0; busy(); n++) {
      if (n > 600) throw new Violation("INV6-wedged", `instance ${inst.k} never settled: ${this.desc(inst)}`);
      this.block();
      this.frame();
    }
    this.frame();
    this.block();
    vclock += 200; // a human between keypresses
  }

  hostTurn(s: Step): void {
    if (this.stall && s.c % this.stall === 0) {
      this.log("    (audio stalled)");
      return;
    }
    this.block();
  }

  // --- the parked song request -------------------------------------------------------------------------------

  settleRequest(where: string): void {
    const req = this.req;
    const before = this.ramWrites.length;
    const r = this.project.settleSong();
    const wrote = this.ramWrites.slice(before);
    if (r === "waiting") {
      const tgt = req ? this.requestTarget(req) : undefined;
      if (tgt && tgt.booting === 0 && !this.systems.isRestoringSong(tgt.id) && ++tgt.waiting > 400)
        throw new Violation("INV6-request-wedged", `a song request waited ${tgt.waiting} frames on a cart that is up`);
      return;
    }
    if (typeof r === "object") {
      // The discard prompt. Agree or decline at random.
      if (this.r() < 0.5) {
        this.project.confirmSong();
        if (req) req.confirmed = true;
        this.log(`    ${where}: settle asked to discard - confirmed`);
      } else {
        this.project.cancelSong();
        this.req = null;
        this.log(`    ${where}: settle asked to discard - cancelled`);
      }
      return;
    }
    this.req = null;
    if (!req || r !== "loaded" || !wrote.length) {
      this.log(`    ${where}: settle -> ${r}${r === "loaded" ? " (no-op)" : ""}`);
      return;
    }
    const inst = this.insts.find((x) => x.id === wrote[0]);
    if (!inst || wrote.some((id) => id !== wrote[0])) throw new Violation("INV5-isolation", `a song request wrote into ${wrote.join(",")}`);
    const want = this.requestTarget(req);
    if (want !== inst) throw new Violation("INV5-isolation", `a song request aimed at instance ${want?.k} loaded into instance ${inst.k}`);
    if (want.k !== req.from) this.note("name-request-followed-focus");
    if (this.atRisk(inst) && !req.confirmed)
      throw new Violation("INV1-unguarded", `${where}: a song request loaded over unsaved ${key(inst.truth)} without asking`);
    const d = this.dir(inst);
    const row = req.index !== undefined ? req.index : d.songs.findIndex((s) => s.name === req.name);
    const song = d.songs[row];
    if (!song) throw new ModelError(`loaded row ${row} is not in the battery`);
    inst.revert = { truth: inst.truth, origin: inst.origin, originContent: inst.originContent, lineage: inst.lineage };
    inst.truth = { ...song, edited: false, cont: inst.truth.cont };
    inst.origin = inst.uids[row];
    inst.originContent = { ...song };
    inst.lineage = [inst.uids[row]];
    this.log(`    ${where}: settle loaded row ${row} ${key(song)} into [${inst.k}]`);
  }
  requestTarget(req: Req): Inst | undefined {
    const id = req.systemId !== undefined ? this.systems.currentId(req.systemId) : this.systems.primary()?.id;
    return this.insts.find((x) => x.id === id);
  }

  // --- host Save ---------------------------------------------------------------------------------------------

  async hostSave(inst: Inst, why: string): Promise<string> {
    // A host Save landing inside one of the cart's own multi-block routines - a SAVE between freeing the old
    // copy and writing the entry, a LOAD mid-unpack - is a race the compare-and-swap cannot see (it is atomic
    // per block, not per routine). Found and reported; FUZZ_RACE=1 lets it through.
    if ((inst.midSave || inst.ops.some((o) => o.label === "unpack")) && !RACE) return `${why} -> (skipped: inside a cart routine)`;
    const pre = this.dir(inst).songs.map((s, i) => ({ ...s, uid: inst.uids[i] }));
    const img = this.systems.workingSongImage(inst.id);
    const saving = img ? songOfImage(img) : null;
    const risk = this.atRisk(inst);
    const t = inst.truth;
    const r = await saveWorkingSong(this.be, this.systems, this.sys(inst));
    const out = `${why} -> ${JSON.stringify(r)}`;
    const isTruth = !!saving && same(saving, t);
    if (r.kind === "clean") {
      if (risk && isTruth) throw new Violation("INV3-clean", `Save said clean, but ${key(t)} is unsaved and in no slot`);
      return out;
    }
    if (r.kind !== "saved") return out;
    if (!saving) throw new ModelError("saved with no image");
    const post = this.dir(inst).songs;
    if (!isTruth) this.note("save-of-non-truth");
    if (r.replaced) {
      if (post.length !== pre.length) throw new Violation("INV2-shape", `a write-back changed the song count ${pre.length} -> ${post.length}`);
      for (let i = 0; i < pre.length; i++) if (i !== r.index && !same(pre[i], post[i])) throw new Violation("INV2-other", `a write-back to ${r.index} changed slot ${i}`);
      const victim = pre[r.index];
      const ok =
        victim.uid === inst.origin ||
        inst.lineage.includes(victim.uid) ||
        // An identical twin of what the user loaded: the song's own previous version, wherever it sits.
        (!!inst.originContent && same(victim, inst.originContent)) ||
        same(victim, saving) ||
        post.some((p, i) => i !== r.index && same(p, victim)) ||
        // The empty default song, whatever playback did to its groove table: nothing in it to lose.
        victim.tag === BLANK.tag;
      if (!ok)
        throw new Violation(
          "INV2-clobber",
          `Save wrote ${key(saving)} over slot ${r.index} ${key(victim)}, which is not where the song came from (origin slot ${inst.uids.indexOf(inst.origin ?? -1)}) and exists nowhere else`,
        );
      if (saving.tag === GARBAGE) throw new Violation("INV2-garbage", `Save wrote a half-loaded block over slot ${r.index}`);
      if (!same(post[r.index], saving)) throw new Violation("INV3-content", `slot ${r.index} holds ${key(post[r.index])}, not ${key(saving)}`);
      if (!same(victim, saving)) inst.uids[r.index] = ++this.uidN;
    } else {
      if (post.length !== pre.length + 1) throw new Violation("INV2-shape", `an append changed the song count ${pre.length} -> ${post.length}`);
      for (let i = 0; i < pre.length; i++) if (!same(pre[i], post[i])) throw new Violation("INV2-other", `an append changed slot ${i}`);
      if (!same(post[pre.length], saving)) throw new Violation("INV3-content", `the appended slot holds ${key(post[pre.length])}, not ${key(saving)}`);
      inst.uids.push(++this.uidN);
    }
    if (isTruth) {
      inst.truth = { ...t, edited: false, contEdited: false };
      inst.origin = inst.uids[r.index];
      inst.originContent = contentOf(t);
      inst.lineage.push(inst.origin);
      if (this.atRisk(inst)) throw new Violation("INV3-content", `after Save the song is still unsaved`);
    }
    return out;
  }

  // --- the guard every destructive host row asks ------------------------------------------------------------

  /** True when the op should go ahead: nothing was at risk, or the guard asked and the user agreed. */
  guarded(inst: Inst, what: string, guard: boolean, s: Step): boolean {
    if (this.atRisk(inst) && !guard) throw new Violation("INV1-unguarded", `${what} would destroy unsaved ${key(inst.truth)} and did not ask`);
    if (guard && (s.c >> 4) % 2 === 0) {
      this.log(`    ${what}: guard asked - declined`);
      return false;
    }
    return true;
  }

  /** A cold boot from the host (a Songs-menu row, Load SRAM, New SRAM): the working song is the blank one
   *  the cart boots into, and nothing tracks it. */
  coldBooted(inst: Inst, uids: number[]): void {
    inst.truth = { ...BLANK, edited: false, cont: 0 };
    inst.origin = null;
    inst.originContent = null;
    inst.lineage = [];
    inst.uids = uids;
    this.publish(inst);
  }

  /** Reset, reload, FM toggle, Swap ROM: the song is carried; the reboot only drops CONT, as the cart's
   *  own config_load does. */
  rebooted(inst: Inst): void {
    if (inst.truth.contEdited && inst.truth.cont !== 0) this.note("cont-dropped-by-boot");
    inst.truth = { ...inst.truth, cont: 0, contEdited: false };
    // A song byte-identical to a saved one lands clean (an edited one is carried clean), and the watcher
    // then tracks that slot - by design: nothing tells an identical twin from the original. The model
    // follows it, as if the user had loaded the twin.
    const at = this.dir(inst).songs.findIndex((x) => same(x, inst.truth));
    if (at >= 0) {
      if (inst.truth.edited) this.note("carried-identical-as-clean");
      inst.truth = { ...inst.truth, edited: false };
      const held = inst.origin === null ? -1 : inst.uids.indexOf(inst.origin);
      if (held < 0 || !same(this.dir(inst).songs[held], inst.truth)) {
        inst.origin = inst.uids[at];
        inst.originContent = contentOf(inst.truth);
        inst.lineage.push(inst.origin);
      }
    }
  }

  // --- run one step ------------------------------------------------------------------------------------------

  async step(s: Step): Promise<string> {
    const inst = this.insts[s.i];
    const host = s.k.startsWith("h");
    // Between two UI-thread actions the audio thread normally runs; `stall` leaves it stopped.
    if (host && s.k !== "hsstate") this.hostTurn(s);
    // Known bug: a reboot inside an in-cart LOAD, before its flag clears. FUZZ_KNOWN lets the load finish.
    // And a reboot before a stalled host load lands (FUZZ_KNOWN runs the block).
    if (KNOWN && ["hreset", "hreload", "hfm", "hswap"].includes(s.k)) for (let n = 0; (inst.ops.length || this.queued(inst)) && n < 20; n++) this.block();
    // A Recent row is aimed at the primary instance, whichever that is; every other row at its own.
    const aimed = s.k === "hname" ? (this.insts.find((x) => x.id === this.systems.primary()?.id) ?? inst) : inst;
    const bystander = this.insts[1 - aimed.k];
    const before = host ? this.isolationSnapshot(bystander) : null;
    const d = await this.doStep(s, inst);
    if (before) this.isolationCheck(bystander, before, d);
    return d;
  }

  isolationSnapshot(o: Inst) {
    return {
      id: o.id,
      sram: this.be.readSram(o.id)!,
      ram: this.be.readRam(o.id)!,
      queued: this.queue.filter((q) => q[0] === o.id).length,
      slot: JSON.stringify(this.systems.systems().find((e) => e.id === o.id)?.workingSlot ?? null),
      carry: JSON.stringify(this.systems.pendingCarry(o.id)?.edited ?? null),
    };
  }
  isolationCheck(o: Inst, b: ReturnType<World["isolationSnapshot"]>, what: string): void {
    const eq = (x: Uint8Array, y: Uint8Array) => x.length === y.length && x.every((v, i) => v === y[i]);
    const a = this.isolationSnapshot(o);
    const diff = a.id !== b.id ? "its core" : !eq(a.sram, b.sram) ? "its battery" : !eq(a.ram, b.ram) ? "its work RAM" : a.queued !== b.queued ? "its queued writes" : a.slot !== b.slot ? "its tracked slot" : a.carry !== b.carry ? "its carry" : "";
    if (diff) throw new Violation("INV5-isolation", `${what} (aimed at the other instance) changed instance ${o.k}'s ${diff}`);
  }

  async doStep(s: Step, inst: Inst): Promise<string> {
    const { be, systems } = this;
    switch (s.k) {
      // --- the cart's own screens ---
      case "edit":
      case "echo":
      case "name":
      case "undo":
      case "cont":
      case "drift": {
        this.settleCart(inst);
        const t = inst.truth;
        let next: Song = contentOf(t);
        let edited = true;
        if (s.k === "edit") next = { ...next, tag: 1 + (s.a % 3), drift: false };
        else if (s.k === "echo") next = { ...next, echo: s.a % 2 }; // the editor keeps echo in range
        else if (s.k === "name") next = { ...next, name: this.pick(NAMES, s.a) };
        else if (s.k === "undo") {
          if (!inst.originContent) return "undo (nothing loaded)";
          next = { ...inst.originContent };
        } else if (s.k === "drift") {
          if (KNOWN && next.tag === 0) return "drift (skipped: known bug, a NEW read as drift)";
          next = { ...next, drift: !next.drift };
          edited = t.edited; // playback does not set the flag
        }
        const cont = s.k === "cont" ? s.a % 3 : t.cont;
        this.setRam(inst, (r) => {
          r.song = next;
          if (s.k !== "drift") r.edited = true;
          else r.playing = true; // `T` is a playback command: drift means the transport runs
          r.settings[CONT] = cont;
        });
        inst.truth = { ...next, edited, cont, contEdited: s.k === "cont" || t.contEdited };
        return `${s.k} [${inst.k}] -> ${key(next)}${s.k === "cont" ? " cont=" + cont : ""}`;
      }
      case "opt": {
        // COLR / FM on the OPTIONS screen: settings that do not set song_edited.
        this.settleCart(inst);
        this.setRam(inst, (r) => {
          if (s.a % 2) r.settings[0] = s.b % 3;
          else r.settings[3] = s.b % 2;
        });
        return `opt [${inst.k}]`;
      }
      case "csave": {
        this.settleCart(inst);
        this.enterFiles(inst);
        const d = this.dir(inst);
        const room = d.songs.length < MAXDIR;
        const r = s.a % (d.songs.length + (room ? 1 : 0));
        if (Number.isNaN(r)) return "csave (cart full)";
        const appending = r === d.songs.length;
        // FILES SAVE takes the row's (inline-edited) name into song_name before it saves.
        const name = !appending && s.b % 3 ? d.songs[r].name : this.pick(NAMES, s.b >> 2);
        const rm = this.ram(inst);
        const saved: Song = { ...rm.song, name };
        this.setRam(inst, (x) => (x.song = { ...x.song, name }));
        const prev = { truth: inst.truth, origin: inst.origin, originContent: inst.originContent };
        const uid = ++this.uidN;
        const settings = rm.settings;
        const write = (): void => {
          const cur = this.dir(inst);
          const songs = cur.songs.slice();
          // The entry goes in at directory index r, whatever is there by now. Normally that is the hole
          // rss_free_old left; if the host rewrote the battery in between, it is whichever song moved in.
          if (holed && be.readSram(inst.id)![entryAt(r)] === 0xa5 && r < songs.length && !same(songs[r], saved))
            throw new Violation("RACE-mid-save", `the host rewrote the battery inside the cart's SAVE; its entry write lands on slot ${r}, now ${key(songs[r])}`);
          if (holed && be.readSram(inst.id)![entryAt(r)] !== 0xa5) songs.splice(r, 0, saved);
          else if (appending || r >= songs.length) songs.push(saved);
          else songs[r] = saved;
          be.setSram(inst.id, encodeDir(songs, settings)); // rle_song_save, then config_save
          inst.midSave = false;
          if (holed && inst.uids.length < songs.length) inst.uids.splice(r, 0, uid);
          else if (appending || r >= inst.uids.length) inst.uids.push(uid);
          else inst.uids[r] = uid;
        };
        // An existing row is freed first (rss_free_old); when that lands in its own block, the battery
        // briefly has a hole where the row was.
        let holed = false;
        const ops: Op[] = [];
        if (!appending && s.c % 2) {
          ops.push({
            label: "free",
            run: () => {
              const sav = be.readSram(inst.id)!;
              sav[entryAt(r)] = 0;
              be.setSram(inst.id, sav);
              inst.uids.splice(r, 1);
              holed = true;
              inst.midSave = true;
            },
          });
          // rle_pack runs between the free and the entry write.
          for (let n = (s.c >> 3) % 3; n > 0; n--) ops.push({ label: "pack", run: () => {} });
        }
        ops.push({ label: "write", run: write, abort: () => {
          inst.origin = prev.origin !== null && inst.uids.includes(prev.origin) ? prev.origin : null;
          inst.originContent = inst.origin !== null ? prev.originContent : null;
        } });
        for (let n = (s.c >> 1) % 4; n > 0; n--) ops.push({ label: "busy", run: () => {} });
        ops.push({ label: "flag", run: () => this.setRam(inst, (x) => (x.edited = false)) });
        // The first step runs in the block the key is pressed in.
        ops.shift()!.run();
        inst.ops = ops;
        inst.truth = { ...saved, edited: false, cont: inst.truth.cont };
        inst.origin = uid;
        inst.originContent = { ...saved };
        inst.lineage.push(uid);
        return `csave [${inst.k}] row ${r}${appending ? " (append)" : ` over ${key(d.songs[r])}`} as ${key(saved)}${ops.length ? " ops=" + ops.map((o) => o.label).join("/") : ""}`;
      }
      case "cload": {
        this.settleCart(inst);
        this.enterFiles(inst);
        const d = this.dir(inst);
        if (!d.songs.length) return "cload (no songs)";
        const r = s.a % d.songs.length;
        const src = d.songs[r];
        const mode = s.c % 3;
        const full = (): void => this.setRam(inst, (x) => (x.song = { ...src }));
        const clear = (): void => this.setRam(inst, (x) => (x.edited = false));
        const ops: Op[] = [];
        if (mode === 0) {
          full();
          clear();
        } else if (mode === 1) {
          full();
          for (let n = (s.c >> 2) % 4; n > 0; n--) ops.push({ label: "busy", run: () => {} });
          ops.push({ label: "flag", run: clear });
        } else {
          // rle_song_load restores echo and name, then unpacks the block, then clears the flag.
          this.setRam(inst, (x) => (x.song = { ...src, tag: GARBAGE, drift: false }));
          ops.push({ label: "unpack", run: full });
          ops.push({ label: "flag", run: clear });
        }
        inst.ops = ops;
        inst.truth = { ...src, edited: false, cont: inst.truth.cont };
        inst.origin = inst.uids[r];
        inst.originContent = { ...src };
        inst.lineage = [inst.uids[r]];
        return `cload [${inst.k}] row ${r} ${key(src)} mode ${mode}`;
      }
      case "cnew": {
        this.settleCart(inst);
        this.enterFiles(inst);
        this.setRam(inst, (x) => {
          x.song = { ...BLANK };
          x.edited = false;
        });
        inst.truth = { ...BLANK, edited: false, cont: inst.truth.cont };
        inst.origin = null;
        inst.originContent = null;
        inst.lineage = [];
        return `cnew [${inst.k}]`;
      }
      case "cclear":
      case "crename": {
        this.settleCart(inst);
        const d = this.dir(inst);
        if (!d.songs.length) return `${s.k} (no songs)`;
        const r = s.a % d.songs.length;
        const songs = d.songs.slice();
        if (s.k === "cclear") {
          songs.splice(r, 1);
          if (inst.uids[r] === inst.origin) inst.origin = null;
          inst.uids.splice(r, 1);
        } else songs[r] = { ...songs[r], name: this.pick(NAMES, s.b) };
        be.setSram(inst.id, encodeDir(songs, d.opts));
        return `${s.k} [${inst.k}] row ${r}${s.k === "crename" ? ` -> "${songs[r].name}"` : ""}`;
      }

      // --- time ---
      case "block":
        this.block();
        return "block";
      case "frame":
        this.frame();
        return "frame";
      case "settle":
        for (const x of this.insts) this.settleCart(x);
        return "settle";

      // --- the host ---
      case "hsave": {
        return this.hostSave(inst, `hsave [${inst.k}]`);
      }
      case "hload": {
        const d = this.dir(inst);
        if (!d.songs.length) return "hload (no songs)";
        // No one drives the Songs menu inside the cart's own LOAD routine (milliseconds).
        if (inst.ops.length) return "hload (cart busy)";
        const idx = s.a % d.songs.length;
        const discards = songLoadWouldDiscard(systems, this.sys(inst));
        if (this.atRisk(inst) && !discards) throw new Violation("INV1-unguarded", `the Songs menu's Load would destroy unsaved ${key(inst.truth)} and did not ask`);
        let note = "";
        if (discards) {
          const choice = s.b % 3;
          if (choice === 0) return `hload [${inst.k}] row ${idx}: guard asked - declined`;
          if (choice === 2) {
            note = " " + (await this.hostSave(inst, "save-first"));
            // The menu loads after anything but a refusal (a skipped save stands in for one here).
            if (note.includes('"refused"') || note.includes("skipped")) return `hload [${inst.k}] row ${idx}:${note}, nothing loaded`;
          }
        }
        // `confirmed` here is the MODEL's: the user agreed to lose something. The request itself always
        // arrives confirmed, because the menu guarded it.
        this.req = { index: idx, systemId: inst.id, confirmed: discards, from: inst.k };
        this.project.requestSong({ index: idx, confirmed: true, systemId: inst.id });
        this.settleRequest("inline");
        return `hload [${inst.k}] row ${idx}${discards ? (s.b % 3 === 1 ? " (discard)" : " (save first)") : ""}${note}`;
      }
      case "hname": {
        const d = this.dir(inst);
        if (this.insts.some((x) => x.ops.length)) return "hname (cart busy)";
        const name = d.songs.length && s.a % 4 ? d.songs[s.a % d.songs.length].name : this.pick(NAMES, s.a);
        const primary = this.insts.find((x) => x.id === systems.primary()?.id);
        this.req = { name, confirmed: false, from: primary?.k ?? -1 };
        this.project.requestSong({ name, confirmed: false });
        this.settleRequest("inline");
        return `hname "${name}" (primary [${primary?.k}])`;
      }
      case "hdel":
      case "hmove":
      case "hadd":
      case "hrepl": {
        const d = this.dir(inst);
        const sys = this.sys(inst);
        const n = d.songs.length;
        let fn: (sav: Uint8Array) => Uint8Array | null;
        let songs = d.songs.slice();
        let uids = inst.uids.slice();
        let what: string = s.k;
        if (s.k === "hdel" || s.k === "hrepl" || s.k === "hmove") {
          if (!n) return `${s.k} (no songs)`;
        }
        const idx = n ? s.a % n : 0;
        if (s.k === "hdel") {
          fn = (sav) => smsggdjSongCatalog.delete(sav, idx);
          songs.splice(idx, 1);
          uids.splice(idx, 1);
          what = `delete row ${idx}`;
        } else if (s.k === "hmove") {
          const to = s.b % 2 ? idx + 1 : idx - 1;
          if (to < 0 || to >= n) return "hmove (edge)";
          fn = (sav) => smsggdjSongCatalog.reorder!(sav, idx, to);
          [songs[idx], songs[to]] = [songs[to], songs[idx]];
          [uids[idx], uids[to]] = [uids[to], uids[idx]];
          what = `move row ${idx} -> ${to}`;
        } else {
          const path = this.pick(this.smdjFiles, s.b);
          const song = unwrapSong(be.readFile(path)!)!;
          const file: Song = { ...tagOf(song.block), name: "", echo: echoIdx(song.echo) };
          if (s.k === "hadd") {
            if (n >= MAXDIR) return "hadd (full)";
            const name = path.replace(/^.*[\\/]/, "").replace(/\.smdj4$/i, "").slice(0, 8).toUpperCase();
            fn = (sav) => addSong(sav, song.block, name);
            songs.push({ ...file, name, echo: 2 }); // Add writes no echo: raw zeros
            uids.push(++this.uidN);
            what = `add ${name}`;
          } else {
            fn = (sav) => replaceSong(sav, idx, song.block, song.echo);
            songs[idx] = { ...file, name: songs[idx].name };
            uids[idx] = ++this.uidN;
            what = `replace row ${idx}`;
          }
        }
        if (!this.guarded(inst, `Songs menu ${what}`, savEditWouldDiscard(systems, sys), s)) return `${what}: declined`;
        const oldId = inst.id;
        if (!mutateLiveSav(be, systems, sys, fn)) {
          if (inst.id !== oldId) throw new ModelError("a refused edit rebooted the cart");
          return `${what}: refused`;
        }
        const got = this.dir(inst).songs;
        if (got.map(key).join(",") !== songs.map(key).join(",")) throw new ModelError(`${what}: battery ${got.map(key).join(",")}, model ${songs.map(key).join(",")}`);
        this.coldBooted(inst, uids);
        return `${what} [${inst.k}] (cold boot)`;
      }
      case "hlsram":
      case "hnsram": {
        const sys = this.sys(inst);
        const what = s.k === "hlsram" ? "Load SRAM" : "New SRAM";
        if (!this.guarded(inst, what, savEditWouldDiscard(systems, sys), s)) return `${what}: declined`;
        if (s.k === "hlsram") {
          const p = this.pick(this.savFiles, s.a);
          if (systems.loadSram(inst.id, p) === null) throw new ModelError("loadSram failed");
          this.coldBooted(inst, this.dir(inst).songs.map(() => ++this.uidN));
          return `Load SRAM ${p} [${inst.k}]`;
        }
        const p = `/savs/new${this.fileN++}.sav`;
        if (systems.newSramAs(inst.id, p) === null) throw new ModelError("newSramAs failed");
        this.savFiles.push(p);
        this.coldBooted(inst, []);
        return `New SRAM [${inst.k}]`;
      }
      case "hsstate": {
        const p = `/states/s${this.fileN++}.ss0`;
        if (systems.saveState(inst.id, p)) this.states.push(p);
        return `Save State [${inst.k}] -> ${p}`;
      }
      case "hlstate": {
        if (!this.states.length) return "Load State (no states)";
        if (KNOWN && this.project.hasSongRequest()) return "Load State (skipped: known bug, a parked request survives it)";
        const p = this.pick(this.states, s.a);
        if (!this.guarded(inst, "Load State", savEditWouldDiscard(systems, this.sys(inst)), s)) return "Load State: declined";
        if (systems.loadState(inst.id, p) === null) throw new ModelError("loadState failed");
        this.restored(inst, inst.restoredFrom!, null);
        return `Load State ${p} [${inst.k}]`;
      }
      case "hreset":
      case "hreload":
      case "hfm":
      case "hswap": {
        let what: string = s.k;
        if (s.k === "hreset") systems.reset(inst.id);
        else if (s.k === "hreload") systems.reloadSystem(inst.id);
        else if (s.k === "hfm") {
          const cfg = this.sys(inst).roles.find((r) => r.kind === "mesen")?.config as { enableFm?: boolean } | undefined;
          // The FM Audio row: store the role config, then reset so the core reads it.
          if (systems.setRoleConfig(inst.id, "mesen", { enableFm: cfg?.enableFm === false })) systems.reset(inst.id);
          what = "FM toggle";
        } else {
          inst.rom = inst.rom === ROMS[0] ? ROMS[1] : ROMS[0];
          if (systems.swapRom(inst.id, inst.rom) === null) throw new ModelError("swapRom failed");
          what = `swap ROM -> ${inst.rom}`;
        }
        this.rebooted(inst);
        return `${what} [${inst.k}]`;
      }
      case "export":
        return this.exportImport(s);
      case "focus":
        systems.setFocus(inst.id);
        return `focus [${inst.k}]`;
      case "prompt": {
        // The close prompt: its gate (asked of every instance here, rather than after every step - it is the
        // costliest check, and implied by the per-instance guard the step checks do ask), and its rows.
        if (this.insts.some((x) => this.atRisk(x)) && !hasUnsavedChanges(be, this.project))
          throw new Violation("INV1-gate", "an instance has unsaved work, and hasUnsavedChanges is false");
        const items = unsavedChanges(be, this.project);
        for (const x of this.insts) {
          const listed = items.some((i) => i.kind === "song" && i.id === x.id);
          if (listed !== savEditWouldDiscard(systems, this.sys(x))) throw new Violation("INV1-prompt", `the close prompt and the guard disagree about instance ${x.k}`);
        }
        return `prompt (${items.map((i) => i.kind).join(",")})`;
      }
    }
    throw new ModelError(`unknown step ${s.k}`);
  }

  /** The model after a core came back from a savestate (Load State, or a project import). `blob` is the
   *  working song the export shipped beside it, which lands over the savestate's. */
  restored(
    inst: Inst,
    snap: Snap,
    blob: { song: Song; live: Truth; liveUids: number[]; liveDir: Dir; liveOrigin: number | null; liveOriginContent: Song | null; liveLineage: number[] } | null,
  ): void {
    inst.uids = [...snap.uids];
    let d = decodeDir(snap.sram);
    const cont = decodeRam(snap.ram).settings[CONT];
    if (blob) {
      // With a song blob the archive is ONE moment: the live song and the live battery (written over the
      // savestate's lagging one) - so the reopened cart is exactly the one that was saved.
      inst.uids = [...blob.liveUids];
      d = blob.liveDir;
      inst.truth = { ...blob.song, edited: blob.live.edited, cont };
      inst.origin = blob.liveOrigin !== null && inst.uids.includes(blob.liveOrigin) ? blob.liveOrigin : null;
      inst.originContent = inst.origin !== null ? blob.liveOriginContent : null;
      // The row it came from may postdate the savestate; an identical row in it is the same song.
      const twin = d.songs.findIndex((x) => same(x, blob.song));
      if (inst.origin === null && twin >= 0) {
        inst.origin = inst.uids[twin];
        inst.originContent = { ...blob.song };
      }
      inst.lineage = [...blob.liveLineage.filter((u) => inst.uids.includes(u)), ...(inst.origin !== null ? [inst.origin] : [])];
      return;
    }
    if (snap.lostWork) this.note("savestate-taken-in-boot-window");
    if (snap.truth) inst.truth = { ...snap.truth };
    else inst.truth = { ...BLANK, edited: false, cont: 0 };
    inst.origin = snap.origin !== null && inst.uids.includes(snap.origin) ? snap.origin : null;
    inst.originContent = inst.origin !== null ? snap.originContent : null;
    inst.lineage = snap.lineage.filter((u) => inst.uids.includes(u));
    // A clean song identical to a saved one is tracked to it (the watcher cannot tell it from a load).
    const twin = d.songs.findIndex((x) => same(x, inst.truth));
    if (!inst.truth.edited && twin >= 0 && !(inst.originContent && same(inst.originContent, inst.truth))) {
      inst.origin = inst.uids[twin];
      inst.originContent = contentOf(inst.truth);
      inst.lineage.push(inst.origin);
    }
  }

  /** A DAW saving its session and reopening it: exportBytes, then loadBytes of the same bytes. */
  exportImport(s: Step): string {
    // A session save takes longer than a cart routine: let those finish (audio only, no UI).
    this.hostTurn(s);
    for (let n = 0; this.insts.some((x) => x.ops.length) && n < 20; n++) this.block();
    if (KNOWN && this.queue.length) this.block(); // known: a host load not yet landed (stalled audio)
    for (const x of this.insts) {
      // A host load still queued on a stalled audio thread never reached the core: the export ships the
      // song it was replacing.
      // A host load still queued is shipped as the song (see abortOps): the truth stands.
      if (x.revert && this.queued(x)) this.note("export-before-host-load-landed");
    }
    if (KNOWN) for (const x of this.insts) this.publish(x);
    const live = this.insts.map((x) => ({
      truth: { ...x.truth },
      uids: [...x.uids],
      dir: this.dir(x),
      origin: x.origin,
      originContent: x.originContent,
      lineage: [...x.lineage],
      risk: this.atRisk(x),
    }));
    const zip = this.project.exportBytes();
    if (!zip) throw new ModelError("export failed");
    const entries = this.be.unzip(zip)!;
    this.importQueue = this.insts.map((x) => x.k);
    const out = this.project.loadBytes(zip);
    if (out.kind !== "loaded") throw new ModelError(`loadBytes: ${out.kind}`);
    this.req = null;
    const notes: string[] = [];
    for (const inst of this.insts) {
      const blobBytes = entries.find((e) => e.name === `systems/${inst.k}/song`)?.bytes;
      const img = blobBytes ? decodeWorkingSong(blobBytes) : null;
      const lv = live[inst.k];
      if (img) {
        const song = songOfImage(img);
        if (!same(song, lv.truth)) throw new Violation("INV1-export", `the export shipped ${key(song)} for instance ${inst.k}, whose song is ${key(lv.truth)}`);
        this.restored(inst, inst.restoredFrom!, { song, live: lv.truth, liveUids: lv.uids, liveDir: lv.dir, liveOrigin: lv.origin, liveOriginContent: lv.originContent, liveLineage: lv.lineage });
        notes.push(`[${inst.k}] song blob ${key(song)}${img.edited ? "*" : ""}`);
      } else {
        this.restored(inst, inst.restoredFrom!, null);
        if (lv.risk && !same(inst.truth, lv.truth)) this.note("export-lost-to-savestate-lag");
        notes.push(`[${inst.k}] savestate`);
      }
    }
    return `export/import (${notes.join("; ")})`;
  }

  // --- the invariants, after every step ---------------------------------------------------------------------

  check(where: string): void {
    for (const inst of this.insts) {
      const risk = this.atRisk(inst);
      const sys = this.sys(inst);
      if (risk) {
        if (!savEditWouldDiscard(this.systems, sys))
          throw new Violation("INV1-silent", `${where}: instance ${inst.k}'s ${key(inst.truth)} is unsaved, and nothing says so`);
      }
      if (inst.ops.length || this.queued(inst)) continue;
      const img = this.systems.workingSongImage(inst.id);
      if (!img) {
        if (risk) throw new Violation("INV1-lost", `${where}: instance ${inst.k}'s unsaved ${key(inst.truth)} is nowhere - not in RAM, not carried`);
        continue;
      }
      const song = songOfImage(img);
      if (!same(song, inst.truth))
        throw new Violation("INV4-content", `${where}: instance ${inst.k} holds ${key(song)}, the user's song is ${key(inst.truth)}`);
      if (!this.quiescent(inst)) continue;
      const rm = this.ram(inst);
      if (risk && !rm.edited) throw new Violation("INV4-flag", `${where}: instance ${inst.k}'s unsaved song reads clean on the cart`);
      if (!inst.truth.edited && rm.edited) {
        this.note("flag-stuck-unsaved");
        if (STRICT_FLAG) throw new Violation("FLAG-stuck", `${where}: instance ${inst.k} says UNSAVED for a saved song`);
      }
    }
  }
}

let STRICT_FLAG = false;
let RACE = false;
// FUZZ_KNOWN=1 steps around the bugs already found, to look for others (see the report at the bottom).
let KNOWN = false;

// --- running sequences ----------------------------------------------------------------------------------------

interface Outcome {
  inv: string | null;
  msg: string;
  steps: number;
}

async function run(steps: Step[], notes: Notes, stall: number, trace?: string[]): Promise<Outcome> {
  const w = new World(steps.length ? steps[0].a * 31 + steps[0].b : 1, notes, stall, trace);
  let n = 0;
  try {
    w.setup(steps.length ? steps[0].a : 1);
    trace?.push(`init: ${w.insts.map((x) => w.desc(x)).join("  ")}`);
    for (; n < steps.length; n++) {
      const d = await w.step(steps[n]);
      if (trace) trace.push(`${n}: ${d}  | ${w.insts.map((x) => w.desc(x)).join("  ")}`);
      w.check(`step ${n} (${steps[n].k})`);
    }
    return { inv: null, msg: "", steps: n };
  } catch (e) {
    if (e instanceof Violation) return { inv: e.inv, msg: e.message, steps: n };
    if (e instanceof ModelError) return { inv: "MODEL", msg: e.message, steps: n };
    return { inv: "CRASH", msg: String((e as Error)?.stack ?? e).slice(0, 400), steps: n };
  }
}

async function shrink(steps: Step[], inv: string, stall: number): Promise<Step[]> {
  let cur = steps;
  const fails = async (cand: Step[]) => (await run(cand, {}, stall)).inv === inv;
  for (let changed = true; changed; ) {
    changed = false;
    for (let chunk = Math.max(1, cur.length >> 1); chunk >= 1; chunk >>= 1) {
      for (let i = 1; i + chunk <= cur.length; ) {
        const cand = [...cur.slice(0, i), ...cur.slice(i + chunk)];
        if (await fails(cand)) {
          cur = cand;
          changed = true;
        } else i++;
      }
    }
  }
  return cur;
}

const env = (globalThis as { tjs?: { env?: Record<string, string> } }).tjs?.env ?? {};
const ITER = Number(env.FUZZ_ITER ?? 0) || (env.FUZZ_REPLAY || env.FUZZ_SCAN || env.FUZZ_PERM ? 1 : 0);

// --- the permanent, bounded run ---------------------------------------------------------------------------------

// Fixed seeds, each run twice - audio running, and with the audio thread stalled before one host action in
// five - for 60 steps: deterministic, about 8 s. A seed that finds a real bug stays here commented out, with
// what it found, until the bug is fixed (see "What this found" at the bottom):
//   11 (its stalled run): F1, a reopened session's song saved nowhere and reading clean. (26, 30 and 31,
//   outside the range run here, hit F1 too.)
const SEEDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
const LEN = 60;
const STALLS = [0, 5];

if (!ITER) {
  test("the working song survives a randomized cart and host, across two instances", async () => {
    const notes: Notes = {};
    const started = realNow();
    let steps = 0;
    for (const seed of SEEDS) {
      for (const stall of STALLS) {
        const seq = gen(rng(seed + stall * 7777), LEN);
        const out = await run(seq, notes, stall);
        steps += out.steps;
        if (out.inv) {
          const trace: string[] = [];
          await run(seq, {}, stall, trace);
          throw new Error(`seed ${seed} stall ${stall}: ${out.msg}\n${trace.slice(-25).join("\n")}`);
        }
      }
    }
    console.log(`# ${steps} steps clean in ${realNow() - started} ms; notes ${JSON.stringify(notes)}`);
  });
}

// --- the long run ------------------------------------------------------------------------------------------------

if (ITER) {
  test("long fuzz: smsggdj working-song invariants", async () => {
    const seed0 = Number(env.FUZZ_SEED ?? 1);
    const len = Number(env.FUZZ_LEN ?? 120);
    const stall = Number(env.FUZZ_STALL ?? 0);
    const budget = Number(env.FUZZ_MS ?? 800_000);
    STRICT_FLAG = env.FUZZ_STRICT_FLAG === "1";
    RACE = env.FUZZ_RACE === "1";
    KNOWN = env.FUZZ_KNOWN === "1";
    if (env.FUZZ_SCAN) {
      // Which permanent-run seeds pass: for choosing SEEDS.
      const ok: number[] = [];
      for (let i = 0; i < Number(env.FUZZ_SCAN); i++) {
        const fails: string[] = [];
        for (const st of STALLS) {
          const out = await run(gen(rng(i + st * 7777), LEN), {}, st);
          if (out.inv) fails.push(`stall ${st}: ${out.msg.slice(0, 120)}`);
        }
        if (fails.length) console.log(`# seed ${i} FAILS ${fails.join(" | ")}`);
        else ok.push(i);
      }
      console.log(`# passing: ${JSON.stringify(ok)}`);
      return;
    }
    if (env.FUZZ_PERM) {
      const [sd, st] = env.FUZZ_PERM.split(",").map(Number);
      const trace: string[] = [];
      const out = await run(gen(rng(sd + st * 7777), LEN), {}, st, trace);
      for (const t of trace.slice(-14)) console.log(`#   ${t}`);
      console.log(`#   !! ${out.inv ? out.msg : "clean"}`);
      return;
    }
    if (env.FUZZ_REPLAY) {
      const trace: string[] = [];
      const out = await run(JSON.parse(env.FUZZ_REPLAY) as Step[], {}, stall, trace);
      for (const t of trace) console.log(`#   ${t}`);
      console.log(`#   !! ${out.inv ? out.msg : "clean"}`);
      return;
    }
    const notes: Notes = {};
    const found = new Map<string, { seed: number; steps: Step[]; count: number }>();
    const started = realNow();
    let steps = 0;
    let clean = 0;
    let it = 0;
    for (; it < ITER && realNow() - started < budget; it++) {
      const seed = seed0 * 1_000_003 + it;
      const seq = gen(rng(seed), len);
      const out = await run(seq, notes, stall);
      steps += out.steps;
      if (!out.inv) {
        clean++;
        continue;
      }
      const sig = `${out.inv}:${out.msg.replace(/\d+/g, "#").replace(/step # \([a-z]+\): /, "").slice(0, 70)}`;
      const prev = found.get(sig);
      if (!prev) found.set(sig, { seed, steps: seq.slice(0, out.steps + 1), count: 1 });
      else {
        prev.count++;
        if (out.steps + 1 < prev.steps.length) {
          prev.seed = seed;
          prev.steps = seq.slice(0, out.steps + 1);
        }
      }
    }
    console.log(`# ${it} sequences, ${steps} steps, ${clean} clean, ${found.size} distinct, ${realNow() - started} ms`);
    console.log(`# notes ${JSON.stringify(notes)}`);
    for (const [sig, { seed, steps: seq, count }] of found) {
      const inv = sig.slice(0, sig.indexOf(":"));
      const small = await shrink(seq, inv, stall);
      const trace: string[] = [];
      const out = await run(small, {}, stall, trace);
      console.log(`# ===== ${sig} x${count} (seed ${seed}, ${seq.length} -> ${small.length} steps)`);
      for (const t of trace) console.log(`#   ${t}`);
      console.log(`#   !! ${out.msg}`);
      console.log(`#   steps=${JSON.stringify(small)}`);
    }
  });
}

// --- What this found --------------------------------------------------------------------------------------------
//
// Each was confirmed against the store code directly, not only through the model, and all are fixed except
// where noted. Kept as the record of what this file guards.
//
// F1  An export shipped the live song beside a savestate whose battery predated a save made since: reopened,
//     the song was in no slot and read clean. Fixed: a work-RAM song always ships, and the live battery is
//     written over the savestate's on load, so the reopened cart is one moment.
// F2  The tracked slot was exported live beside that same lagging savestate, reopening an old song tracked
//     to a new slot. Fixed by the same change.
// F3  A reboot between the cart writing a song it LOADS and clearing its flag carried it edited when any
//     OPTIONS setting differed. Fixed: the carry's "identical" test compares content only.
// F4  An in-cart NEW over a tracked groove template read as playback drift. Fixed: drift needs the
//     transport running (`T` is a playback command).
// F5  A host Save inside one of the cart's multi-block routines. The free-before-write hole is waited out
//     (isBatteryBusy), and a save waits for the working song to hold still - which closes the mid-unpack
//     case too, EXCEPT with the audio thread stalled mid-unpack, where the half-loaded song holds perfectly
//     still. That one remains (FUZZ_RACE=1 with a stall reaches it); it needs the host to stop processing
//     at that exact moment.
// F6  A reboot or export before a host Load's writes landed carried the old song tracked to the new slot.
//     Fixed: the loaded song stands in until the writes land (SystemsStore.expectSong), and its slot is
//     tracked then.
// F7  A confirmed Songs-menu Load parked through a Load State landed unasked over the restored work. Fixed:
//     a rebuilt target drops the confirmation, and an index request is re-checked by index, not by name.
// Later campaigns on the fixed code found: a host Save's own directory change taken for the cart's
// (rebased now), playback drift onto an older version's exact bytes (drift keeps precedence while the
// directory is unmoved), and an explicit Load onto identical content left untracked (the Load's target is
// tracked when it lands).
//
// Also, not invariant breaks: after a reboot an edited song identical to its slot comes back UNSAVED (and
// prompts) whenever OPTIONS differ from the boot values; a Save State taken inside a boot window holds the
// blank booting cart, not the carried song; and a Recent row's request follows focus if it moves while the
// request is parked (by design - it means "the primary instance").
