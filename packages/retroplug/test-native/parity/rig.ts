// The parity rig: the app's own control plane (bootSession, the boot path the CLI and the render worker
// share), reset to a known state before each case, and a render call that plays a segment through
// renderBlocks. Cases drive it through the stores, as the UI does, so a case survives Phase B: B2 deletes
// the backend calls the stores make today, not the stores. This is the one file B2 and B3 edit.
import { bootSession, type Session } from "../../src/bootSession";
import type { RenderBlock } from "../../src/audioDriver";
import { laneDigest, type MidiOutEntry, type SegmentDigest } from "./digest";

declare const __CONFIG_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

/** One run of host blocks. Events are placed by ABSOLUTE frame within the segment and land in the block
 *  that contains them; transport changes apply at the start of the block containing their frame. */
export interface Segment {
  /** Block sizes, in order. */
  blocks: number[];
  midi?: { at: number; bytes: number[] }[];
  controller?: { at: number; bytes: number[] }[];
  transport?: { at: number; playing?: boolean; bpm?: number; ppq?: number }[];
  /** A store edit made after this segment renders and before the next one does. */
  after?: (rig: Rig) => void;
}

/** `total` frames in blocks of `size` (the last one short). */
export function even(total: number, size: number): number[] {
  const out: number[] = [];
  for (let at = 0; at < total; at += size) out.push(Math.min(size, total - at));
  return out;
}

/** `total` frames in blocks cycling through `sizes` (the last one short): a DAW's irregular blocks. */
export function cycle(total: number, sizes: number[]): number[] {
  const out: number[] = [];
  for (let at = 0, i = 0; at < total; i++) {
    const n = Math.min(sizes[i % sizes.length], total - at);
    out.push(n);
    at += n;
  }
  return out;
}

const ROM_DIR = `${__CONFIG_DIR__}/parity-roms`;
const swapExt = (path: string, ext: string): string => path.replace(/\.[^./]+$/, "") + ext;

export class Rig {
  readonly session: Session = bootSession();
  /** Blocks rendered since the last reset: the MIDI-out block index counts from the start of the case. */
  private blockBase = 0;

  get project() {
    return this.session.project;
  }
  get audio() {
    return this.session.audio;
  }

  /** Back to a known state: no systems, default project settings, a freshly loaded kernel, the transport
   *  stopped at ppq 0 and 120 bpm, `sampleRate`, nothing pending, and no ROM copies left from the last
   *  case (a leftover "-2.sav" would be adopted by the next duplicate). */
  reset(sampleRate: number): void {
    this.project.newProject();
    for (const name of this.session.backend.listDir(ROM_DIR))
      if (!name.endsWith("/")) this.session.backend.deleteFile(`${ROM_DIR}/${name}`);
    const { dsp, audio } = this.session;
    if (!dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)) throw new Error("the kernel did not reload");
    audio.setTransport(false);
    audio.setPpq(0);
    audio.setBpm(120);
    if (!audio.setSampleRate(sampleRate)) throw new Error(`setSampleRate(${sampleRate}) refused: is a system still live?`);
    audio.drainMidiOut();
    if (audio.systemCount() !== 0) throw new Error(`${audio.systemCount()} system(s) survived the reset`);
    this.blockBase = 0;
  }

  /** A fresh copy of `src` (and of the battery beside it, if any) in this file's temp dir, so a case
   *  never writes a committed .sav and always starts from the same battery. `prefix` names the copy apart
   *  from others of the same ROM: a second instance of one path gets the "-2.sav" battery slot, which a
   *  case cannot seed in advance, so two systems that each need a battery each need their own copy. */
  rom(src: string, prefix = ""): string {
    const backend = this.session.backend;
    const dst = `${ROM_DIR}/${prefix}${src.slice(src.lastIndexOf("/") + 1)}`;
    const bytes = backend.readFile(src);
    if (!bytes) throw new Error(`cannot read ${src}`);
    backend.writeFile(dst, bytes);
    for (const ext of [".sav", ".srm"]) {
      const battery = backend.readFile(swapExt(src, ext));
      if (battery) backend.writeFile(swapExt(dst, ext), battery);
    }
    return dst;
  }

  /** Add a ROM (copied first) as a new system; throws if it does not build. */
  add(src: string): number {
    const id = this.project.systems.addSystem(this.rom(src));
    if (id == null) throw new Error(`${src} did not build`);
    return id;
  }

  /** The id of the system in project slot `slot`. */
  id(slot: number): number {
    const v = this.project.systems.view()[slot];
    if (!v) throw new Error(`no system in slot ${slot}`);
    return v.id;
  }

  /** Render one segment at `outputs` lanes; returns its digest, its MIDI out, and the lanes themselves. */
  render(outputs: 2 | 4 | 6 | 8, seg: Segment): { digest: SegmentDigest; midiOut: MidiOutEntry[]; lanes: Float32Array[] } {
    const starts: number[] = [];
    let total = 0;
    const blocks: RenderBlock[] = seg.blocks.map((frames) => {
      starts.push(total);
      total += frames;
      return { frames };
    });
    const blockAt = (at: number): number => {
      if (at < 0 || at >= total) throw new Error(`an event at frame ${at} is outside a ${total}-frame segment`);
      let b = 0;
      while (b + 1 < starts.length && starts[b + 1] <= at) b++;
      return b;
    };
    for (const m of [...(seg.midi ?? [])].sort((x, y) => x.at - y.at)) {
      const b = blockAt(m.at);
      (blocks[b].midi ??= []).push({ frame: m.at - starts[b], bytes: m.bytes });
    }
    for (const c of seg.controller ?? []) (blocks[blockAt(c.at)].controller ??= []).push(c.bytes);
    for (const t of seg.transport ?? []) {
      const blk = blocks[blockAt(t.at)];
      if (t.playing !== undefined) blk.playing = t.playing;
      if (t.bpm !== undefined) blk.bpm = t.bpm;
      if (t.ppq !== undefined) blk.ppq = t.ppq;
    }

    const slotOf = new Map(this.project.systems.view().map((v, i) => [v.id, i]));
    const r = this.audio.renderBlocks(outputs, blocks);
    const midiOut = r.midiOut.map((m) => ({
      block: this.blockBase + m.block,
      slot: slotOf.get(m.system) ?? -1,
      frame: m.frame,
      data: m.data,
    }));
    this.blockBase += blocks.length;
    return { digest: { frames: total, lanes: r.lanes.map((l) => laneDigest(l, 0, total)) }, midiOut, lanes: r.lanes };
  }
}
