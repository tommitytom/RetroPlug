// The renders the render baseline records and the chunk test pulls (render-baseline.test.ts, render-chunks.test.ts),
// each through an engine job (backend.engineRun, docs/modular-graph-phase-d3-design.md sections 3 and 4): their specs,
// the scratch files they read, and how what they write is read back. Not a `.test.ts`, so the runner's walk skips it.
//
// What sounds. The repo's own ROMs (resources/roms) render digital silence through the render library: mGB and the
// BlipToaster carts play only what MIDI sends them, which a render does not do, and smsggdj boots to a blank song.
// So the cases that need no fixture are smsggdj's, playing songs this file authors into a battery (smsSyncSong's
// metronome, an octave up, and a three-channel chord) and loads by index or by name, one of them clocked by the host's
// running transport, and mGB booted as a Super Game Boy, whose boot jingle sounds with no MIDI. The jingle is the SGB
// BIOS's own (SameBoy's sgb.c), mixed after the APU, so it reaches the mix and no stem: a Game Boy split render and
// every NES render still need a fixture, or a render that plays MIDI (design section 8). The fixture tree adds LSDj
// (tripledipper's working song, fixed; telemelt's songs to their HFF stop, picked by name and by index) and risa
// (ecoli_soul to its HFF stop, at PAL and NTSC; blumarbl's split renders, fixed). The GBA fixture (nanoloop) renders
// silence without a BIOS, so no GBA case is here.
import { createRealBackend } from "../src/realBackend";
import { buildSav } from "@retroplug/smsggdj/codec/sav";
import { savBytes } from "@retroplug/risa/testFixtures";
import type { EngineRunResult } from "../src/offline";
import {
  buildMetronomeBlock,
  buildConfigBlock,
  SMS_SYNC_OFF,
  SMS_SYNC_IN24,
  CHAIN_ENTRIES,
  HIT_NOTE,
  P_CHAINS,
  P_PHRASES,
  P_SONG,
  STEPS_PER_PHRASE,
} from "./smsSyncSong";
import { fnv1a64Hex } from "./kit/sources";

declare const __REPO_RESOURCES_DIR__: string;
declare const __MGB_ROM__: string;
declare const __RESOURCES_DIR__: string;

export type Backend = ReturnType<typeof createRealBackend>;

/** Which tree a case's ROM comes from: the repo's own ("none"), or the fixture tree (RETROPLUG_RESOURCES_DIR). */
export type Fixture = "none" | "lsdj" | "risa";

export interface RenderCase {
  name: string;
  fixture: Fixture;
  /** The job spec without its `out`, given this run's scratch prefix (the files `writeScratch` wrote). */
  spec: (scratch: string) => Record<string, unknown>;
  /** The files it writes, by the suffix render.ts names them with: "" for a mix, "_<stream>" for a split. */
  outputs: string[];
  /** Outputs that may be silent (a stem its song never plays); every other output, and the case, must sound. */
  silent?: string[];
}

const MGB = __MGB_ROM__;
const SMS = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const GG = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg";
const TRIPLEDIPPER = __RESOURCES_DIR__ + "/roms/tripledipper942.gbc"; // its working song, in the sibling .sav
const TELEMELT = __RESOURCES_DIR__ + "/roms/lsdj942telemelt_5songs.gbc"; // five songs, in the sibling .sav
const RISA = __RESOURCES_DIR__ + "/roms/risa/risa-v2.2.1/risa-2.2.1-pal.nes";
const ECOLI = __RESOURCES_DIR__ + "/roms/risa/ecoli_soul.srm"; // a song whose last track HFFs (59 s at PAL)

const GB_STEMS = ["_pulse1", "_pulse2", "_wave", "_noise"];
const NES_CHANNELS = ["_square1", "_square2", "_triangle", "_noise", "_dmc"];
const NES_PINS = ["_pulse", "_tnd", "_expansion"];

const smsSav = (scratch: string) => `${scratch}-smsggdj.sav`;
const smsSyncSav = (scratch: string) => `${scratch}-smsggdj-sync.sav`;
const blumarblSav = (scratch: string) => `${scratch}-blumarbl.srm`;

export const CASES: RenderCase[] = [
  {
    name: "smsggdj/sms/index-0/mix",
    fixture: "none",
    spec: (d) => ({ rom: SMS, sav: smsSav(d), songIndex: 0, durationMs: 4000 }),
    outputs: [""],
  },
  {
    name: "smsggdj/sms/name-chord/mix-48000",
    fixture: "none",
    spec: (d) => ({ rom: SMS, sav: smsSav(d), song: "chord", durationMs: 3000, sampleRate: 48000 }),
    outputs: [""],
  },
  {
    name: "smsggdj/gg/index-1/mix",
    fixture: "none",
    spec: (d) => ({ rom: GG, sav: smsSav(d), songIndex: 1, durationMs: 3000 }),
    outputs: [""],
  },
  {
    // The cart clocked by the host: its sync node turns the running transport into the cart's clock, block by block.
    name: "smsggdj/sms/sync-in24/transport-120bpm",
    fixture: "none",
    spec: (d) => ({
      rom: SMS,
      sav: smsSyncSav(d),
      songIndex: 0,
      durationMs: 4000,
      transport: true,
      bpm: 120,
      roles: [{ kind: "sms-sync", config: { machine: "sms" } }],
    }),
    outputs: [""],
  },
  {
    // The Super Game Boy's boot jingle, unpressed: SameBoy's render path on CI.
    name: "sameboy/mgb-sgb/no-start/mix",
    fixture: "none",
    spec: () => ({ rom: MGB, start: false, durationMs: 4000, roles: [{ kind: "sameboy", config: { model: "sgb" } }] }),
    outputs: [""],
  },
  {
    name: "lsdj/tripledipper/fixed-5s/mix",
    fixture: "lsdj",
    spec: () => ({ rom: TRIPLEDIPPER, durationMs: 5000 }),
    outputs: [""],
  },
  {
    name: "lsdj/tripledipper/fixed-5s/channels",
    fixture: "lsdj",
    spec: () => ({ rom: TRIPLEDIPPER, durationMs: 5000, split: "channels" }),
    outputs: GB_STEMS,
  },
  {
    name: "lsdj/telemelt/hff/name-machete/mix",
    fixture: "lsdj",
    spec: () => ({ rom: TELEMELT, song: "machete" }),
    outputs: [""],
  },
  {
    name: "lsdj/telemelt/hff/index-3/channels",
    fixture: "lsdj",
    spec: () => ({ rom: TELEMELT, songIndex: 3, split: "channels" }),
    outputs: GB_STEMS,
  },
  {
    name: "risa/ecoli/hff/mix",
    fixture: "risa",
    spec: () => ({ rom: RISA, sav: ECOLI }),
    outputs: [""],
  },
  {
    name: "risa/ecoli/hff/region-ntsc/mix",
    fixture: "risa",
    spec: () => ({ rom: RISA, sav: ECOLI, roles: [{ kind: "mesen", config: { region: "ntsc" } }] }),
    outputs: [""],
  },
  {
    name: "risa/blumarbl/fixed-3s/channels",
    fixture: "risa",
    spec: (d) => ({ rom: RISA, sav: blumarblSav(d), songIndex: 0, durationMs: 3000, split: "channels" }),
    outputs: NES_CHANNELS,
    silent: ["_square1", "_triangle", "_dmc"], // its first three seconds play on the other two
  },
  {
    name: "risa/blumarbl/fixed-3s/pins",
    fixture: "risa",
    spec: (d) => ({ rom: RISA, sav: blumarblSav(d), songIndex: 0, durationMs: 3000, split: "pins" }),
    outputs: NES_PINS,
  },
];

export const caseNamed = (name: string): RenderCase => {
  const c = CASES.find((k) => k.name === name);
  if (!c) throw new Error(`no render case named ${name}`);
  return c;
};

/** The ROMs and batteries `fixture` needs, or why they are not all there. */
export function missingFixture(be: Backend, fixture: Fixture): string | null {
  const need = fixture === "lsdj" ? [TRIPLEDIPPER, TELEMELT] : fixture === "risa" ? [RISA, ECOLI] : [MGB, SMS, GG];
  const missing = need.filter((p) => !be.fileExists(p));
  return missing.length ? `missing ${missing.join(", ")}` : null;
}

// smsggdj's battery: three songs. METRO is smsSyncSong's metronome (a note a beat on the first tone channel), HIGH
// the same an octave up, CHORD a major triad on the three tone channels (the metronome's phrase through chains 1 and
// 2, transposed 4 and 7 semitones).
function smsggdjSav(): Uint8Array {
  const metro = buildMetronomeBlock();
  const high = buildMetronomeBlock();
  for (let i = P_PHRASES; i < P_PHRASES + STEPS_PER_PHRASE * 4; i += 4) if (high[i] === HIT_NOTE) high[i] += 12;
  const chord = buildMetronomeBlock();
  for (const [chain, transpose] of [[1, 4], [2, 7]])
    for (let i = 0; i < CHAIN_ENTRIES; i++) chord.set([0x00, transpose], P_CHAINS + chain * 32 + i * 2);
  for (let row = 0; row < 8; row++) chord.set([0x00, 0x01, 0x02, 0xff], P_SONG + row * 4);
  const songs = [
    { block: metro, name: "METRO" },
    { block: high, name: "HIGH" },
    { block: chord, name: "CHORD" },
  ];
  return buildSav(songs, 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
}

/** Write the batteries the cases read under `scratch` (a path prefix of this run's own). */
export function writeScratch(be: Backend, scratch: string): void {
  if (!be.writeFile(smsSav(scratch), smsggdjSav())) throw new Error(`cannot write ${smsSav(scratch)}`);
  // The metronome alone, its OPTIONS block set to IN24: the cart waits for the host's clock, 24 a quarter note.
  const sync = buildSav([{ block: buildMetronomeBlock(), name: "METRO" }], 32 * 1024, buildConfigBlock(SMS_SYNC_IN24))!;
  if (!be.writeFile(smsSyncSav(scratch), sync)) throw new Error(`cannot write ${smsSyncSav(scratch)}`);
  if (!be.writeFile(blumarblSav(scratch), savBytes("v2_blumarbl")))
    throw new Error(`cannot write ${blumarblSav(scratch)}`);
}

/** One file a render wrote: its suffix, FNV-1a-64 and size, whether its samples are not all zero, and, when asked
 *  for, its bytes. */
export interface RenderedFile {
  suffix: string;
  hash: string;
  size: number;
  sounds: boolean;
  bytes?: Uint8Array;
}

export interface Rendered {
  result: EngineRunResult;
  files: RenderedFile[];
  /** The frames an auto-detect render ended at, from its `length:` line; null for a fixed render. */
  frames: number | null;
}

const WAV_HEADER = 44; // render/wav.ts writes a canonical 44-byte header, then 16-bit PCM

/** Render `c` as an engine job into `<scratch>-<tag>` (with `extra` in its spec), read back each file it wrote and
 *  delete it. */
export async function renderCase(
  be: Backend,
  c: RenderCase,
  scratch: string,
  tag: string,
  extra: Record<string, unknown> = {},
  keepBytes = false,
): Promise<Rendered> {
  const base = `${scratch}-${c.name.split("/").join("_")}-${tag}`;
  const split = c.outputs.length !== 1 || c.outputs[0] !== "";
  const out = split ? base : `${base}.wav`;
  let frames: number | null = null;
  const result = await be.engineRun(
    "render",
    { ...c.spec(scratch), ...extra, out },
    {
      onLog: (line) => {
        const m = /^length: \d+ ms \((\d+) frames/.exec(line.text);
        if (m) frames = Number(m[1]);
      },
    },
  );
  const files: RenderedFile[] = [];
  for (const suffix of c.outputs) {
    const path = split ? `${base}${suffix}.wav` : out;
    const bytes = be.readFile(path);
    if (!bytes) continue;
    let sounds = false;
    for (let i = WAV_HEADER; i < bytes.length && !sounds; i++) sounds = bytes[i] !== 0;
    files.push({ suffix, hash: fnv1a64Hex(bytes), size: bytes.length, sounds, bytes: keepBytes ? bytes : undefined });
    be.deleteFile(path);
  }
  return { result, files, frames };
}
