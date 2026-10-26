// The parity rig: the app's own control plane (bootSession, the boot path the CLI and the render worker
// share), reset to a known state before each case, and a render call that plays a segment through
// renderBlocks or, for a shape segment, through renderAudioPerSystem / renderAudioPerChannel. Cases drive
// it through the stores, as the UI does, so a case survives Phase B: B2 deletes the backend calls the
// stores make today, not the stores. With bootSession, this is the one file B2 and B3 edit (render-blocks.test.ts
// aside: it tests renderBlocks itself through direct backend calls).
import { bootSession, type Session } from "../../src/bootSession";
import type { RenderBlock } from "../../src/audioDriver";
import { laneDigest, type MidiOutEntry, type SegmentDigest } from "./digest";

/** EngineRpcService's kBlockSize: the block the shape render calls use. */
const SHAPE_BLOCK = 1024;

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
  /** Render through renderAudioPerSystem (`perSystem`) or renderAudioPerChannel (`perChannel`, the system in
   *  that project slot) instead of renderBlocks: `ms` of audio in those calls' own 1024-frame blocks, two
   *  lanes (L, R) per system or per stream. `blocks` must be empty, and the segment's MIDI is staged first,
   *  all of it at frame 0 through stageMidiIn, as the CLI timeline stages it between renderAudio calls. No
   *  controller traffic or transport. These are the output shapes renderBlocks does not reach. */
  shape?: { perSystem: true; ms: number } | { perChannel: number; ms: number };
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

export class Rig {
  readonly session: Session = bootSession();
  /** Blocks rendered since the last reset: the MIDI-out block index counts from the start of the case. */
  private blockBase = 0;
  /** system id -> project slot, for the whole case: updated by each render with the systems live then and
   *  never pruned, so MIDI out from a system an `after` hook removed or replaced keeps its last known slot. */
  private readonly slots = new Map<number, number>();
  /** The rate the last reset set: a shape segment turns its `ms` into frames as the render calls do. */
  private sampleRate = 48000;
  /** The engine's dropped-message count at the last reset: the count is cumulative over the file, so a case's own
   *  drops are what it has added since. */
  private droppedAtReset = 0;

  /** Where this process keeps its ROM copies: under its OWN config dir (the alone-run child gets another). */
  private romDir(): string {
    return `${this.session.backend.configDir()}/parity-roms`;
  }

  get project() {
    return this.session.project;
  }
  get audio() {
    return this.session.audio;
  }

  /** Back to a known state: no systems, default project settings, the transport stopped at ppq 0 and 120
   *  bpm, `sampleRate`, nothing pending, and no ROM copies left from the last case (a leftover "-2.sav" would
   *  be adopted by the next duplicate). The new project reshapes the kernel node, which buildGraph always
   *  places: the structure it publishes names no system (so every system's state is pruned) and only the
   *  default MIDI routing, which keeps no state, so a controller session's state goes with its stage. */
  reset(sampleRate: number): void {
    this.project.newProject();
    const romDir = this.romDir();
    for (const name of this.session.backend.listDir(romDir))
      if (!name.endsWith("/")) this.session.backend.deleteFile(`${romDir}/${name}`);
    const { audio } = this.session;
    audio.setTransport(false);
    audio.setPpq(0);
    audio.setBpm(120);
    if (!audio.setSampleRate(sampleRate)) throw new Error(`setSampleRate(${sampleRate}) refused: is a system still live?`);
    audio.drainMidiOut();
    if (audio.systemCount() !== 0) throw new Error(`${audio.systemCount()} system(s) survived the reset`);
    this.blockBase = 0;
    this.slots.clear();
    this.sampleRate = sampleRate;
    this.droppedAtReset = audio.droppedMessages();
  }

  /** A fresh copy of the ROM `src`, and of nothing else, in this file's temp dir, so a case never writes a
   *  committed .sav. No battery beside `src` is copied: a fixture tree can hold stray, untracked ones (a
   *  .sav an earlier session flushed beside the ROM), and the store prefers .sav over .srm, so a copied
   *  stray would shadow the battery a case writes and the baseline would depend on it. A case that needs a
   *  battery writes it next to the copy (smsggdj, lsdj and risa do). `prefix` names the copy apart from
   *  others of the same ROM: a second instance of one path gets the "-2.sav" battery slot, which a case
   *  cannot seed in advance, so two systems that each need a battery each need their own copy. The .sms and
   *  .gg smsggdj builds share one battery stem ("smsggdj_v0_45.sav"), so a case mixing them needs prefixes
   *  too. `prefix` is a file-name prefix, never a path: the reset wipes files only. */
  rom(src: string, prefix = ""): string {
    if (prefix.includes("/")) throw new Error(`rom prefix "${prefix}" must not contain "/"`);
    const dst = `${this.romDir()}/${prefix}${src.slice(src.lastIndexOf("/") + 1)}`;
    const bytes = this.session.backend.readFile(src);
    if (!bytes) throw new Error(`cannot read ${src}`);
    this.writeFile(dst, bytes);
    return dst;
  }

  /** Write a file through the backend; throws when the write fails. */
  writeFile(path: string, bytes: Uint8Array): void {
    if (!this.session.backend.writeFile(path, bytes)) throw new Error(`cannot write ${path}`);
  }

  /** Press or release `button` on the system in project slot `slot`. */
  press(slot: number, button: number, down: boolean): void {
    this.audio.pressButton(this.id(slot), button, down);
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
    if (seg.shape) return this.renderShape(seg, seg.shape);
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
    for (const c of [...(seg.controller ?? [])].sort((x, y) => x.at - y.at)) (blocks[blockAt(c.at)].controller ??= []).push(c.bytes);
    // Transport applies at the start of the block holding its frame, so one block can only carry one value
    // per field: two different values (play then stop inside one big block) would silently keep the last.
    for (const t of [...(seg.transport ?? [])].sort((x, y) => x.at - y.at)) {
      const b = blockAt(t.at);
      const blk = blocks[b];
      for (const field of ["playing", "bpm", "ppq"] as const) {
        const v = t[field];
        if (v === undefined) continue;
        if (blk[field] !== undefined && blk[field] !== v)
          throw new Error(`block ${b} gets two different values for transport ${field} (${blk[field]} then ${v}): split the blocks`);
        Object.assign(blk, { [field]: v });
      }
    }

    this.project.systems.view().forEach((v, i) => this.slots.set(v.id, i));
    const r = this.audio.renderBlocks(outputs, blocks);
    this.noneDropped();
    const midiOut = r.midiOut.map((m) => ({
      block: this.blockBase + m.block,
      slot: this.slots.get(m.system) ?? -1,
      frame: m.frame,
      data: m.data,
    }));
    this.blockBase += blocks.length;
    return { digest: { frames: total, lanes: r.lanes.map((l) => laneDigest(l, 0, total)) }, midiOut, lanes: r.lanes };
  }

  /** Every message a case staged, and every one the kernel sent, reached the node it was wired to: a full queue on
   *  any wire of the graph counts a drop, and a case that dropped one renders what the unbounded staging the
   *  baseline was recorded with did not. The engine's count is cumulative over the file, so a case is held to the
   *  drops since its reset, and one case's drop fails that case alone. */
  private noneDropped(): void {
    const dropped = this.audio.droppedMessages() - this.droppedAtReset;
    if (dropped !== 0) throw new Error(`${dropped} message(s) dropped on a full queue of the graph`);
  }

  private renderShape(seg: Segment, shape: NonNullable<Segment["shape"]>): { digest: SegmentDigest; midiOut: MidiOutEntry[]; lanes: Float32Array[] } {
    if (seg.blocks.length || seg.controller?.length || seg.transport?.length)
      throw new Error("a shape segment renders its own blocks and takes no controller traffic or transport");
    if (!(shape.ms > 0)) throw new Error(`a shape segment needs ms > 0, got ${shape.ms}`);
    if (this.audio.systemCount() === 0) throw new Error("a shape segment needs at least one system");
    const perChannelId = "perChannel" in shape ? this.id(shape.perChannel) : -1;
    if ("perChannel" in shape && this.audio.systemCount() !== 1) throw new Error("perChannel needs exactly one system");
    const midi = [...(seg.midi ?? [])].sort((x, y) => x.at - y.at);
    for (const m of midi) if (m.at !== 0) throw new Error(`a shape segment stages all its MIDI at frame 0, got an event at ${m.at}`);
    for (const m of midi) if (!this.audio.stageMidiIn(m.bytes)) throw new Error(`stageMidiIn refused ${m.bytes}`);
    this.project.systems.view().forEach((v, i) => this.slots.set(v.id, i));
    // The frame count the render call computes, by the same double arithmetic (EngineRpcService.cpp).
    const total = Math.floor((shape.ms * this.sampleRate) / 1000);
    const interleaved = "perSystem" in shape ? this.audio.renderAudioPerSystem(shape.ms) : this.audio.renderAudioPerChannel(perChannelId, shape.ms);
    if (!interleaved.length) throw new Error("the render call returned nothing (the audio thread owns the engine)");
    this.noneDropped();
    const lanes: Float32Array[] = [];
    for (const buf of interleaved) {
      if (buf.length !== total * 2) throw new Error(`a render returned ${buf.length / 2} frames, expected ${total}`);
      const l = new Float32Array(total);
      const r = new Float32Array(total);
      for (let f = 0; f < total; f++) {
        l[f] = buf[2 * f];
        r[f] = buf[2 * f + 1];
      }
      lanes.push(l, r);
    }
    // These calls hand back their MIDI out without block indices: it is all tagged with the segment's first.
    // So a shape segment's MIDI out records ORDER only: a message that moved by a whole block at the same
    // in-block frame would hash the same.
    const midiOut = this.audio.drainMidiOut().map((m) => ({ block: this.blockBase, slot: this.slots.get(m.system) ?? -1, frame: m.frame, data: m.data }));
    this.blockBase += Math.ceil(total / SHAPE_BLOCK);
    return { digest: { frames: total, lanes: lanes.map((l) => laneDigest(l, 0, total)) }, midiOut, lanes };
  }
}
