// What a parity baseline records about a case, and how two records are compared. Pure: no host calls,
// so the comparator can be proven on its own (comparator.test.ts).

/** One run of blocks: its length, and one "fnv rms" string per lane. */
export interface SegmentDigest {
  frames: number;
  lanes: string[];
}

/** A whole case: its segments in order, the host MIDI out, and at the end each battery system's SRAM
 *  and every system's work RAM, keyed by project slot. Work RAM is stable only because the parity suite
 *  powers cores on in a fixed state (util/Determinism.hpp); it catches state a stimulus changed without
 *  making a sound. */
export interface CaseDigest {
  segments: SegmentDigest[];
  midiOut: { count: number; fnv: string };
  sram: Record<string, { length: number; fnv: string }>;
  ram: Record<string, { length: number; fnv: string }>;
}

/** One MIDI-out message as recorded: the block index counts from the start of the case, and the system
 *  is its project slot rather than its id (ids differ between a case run in its group and run alone). */
export interface MidiOutEntry {
  block: number;
  slot: number;
  frame: number;
  data: Uint8Array;
}

const FNV_OFFSET = 0x811c9dc5;
const FNV_PRIME = 0x01000193;

/** FNV-1a (32-bit) over `bytes`, continuing from `h`. */
export function fnv1a(bytes: Uint8Array, h = FNV_OFFSET): number {
  for (let i = 0; i < bytes.length; i++) h = Math.imul(h ^ bytes[i], FNV_PRIME) >>> 0;
  return h;
}

export const hex8 = (n: number): string => (n >>> 0).toString(16).padStart(8, "0");

/** RMS to 4 significant figures, "0" for exact silence. Computed from the very bits that are hashed, so
 *  it is as stable as the hash; it is there so a re-recorded baseline reads ("lane 3 went silent"). */
export function rms4(lane: Float32Array, from: number, to: number): string {
  let s = 0;
  for (let i = from; i < to; i++) s += lane[i] * lane[i];
  if (s === 0) return "0";
  return Math.sqrt(s / (to - from)).toPrecision(4);
}

/** "fnv rms" for frames [from, to) of one lane: the hash covers the float bit patterns. */
export function laneDigest(lane: Float32Array, from: number, to: number): string {
  const bytes = new Uint8Array(lane.buffer, lane.byteOffset + from * 4, (to - from) * 4);
  return `${hex8(fnv1a(bytes))} ${rms4(lane, from, to)}`;
}

/** Encoding: per message (block, slot, frame, data length) as little-endian u32, then the data bytes; FNV-1a over all. */
export function midiDigest(entries: MidiOutEntry[]): { count: number; fnv: string } {
  let h = FNV_OFFSET;
  const word = new Uint8Array(4);
  const view = new DataView(word.buffer);
  for (const e of entries) {
    for (const n of [e.block, e.slot, e.frame, e.data.length]) {
      view.setUint32(0, n, true);
      h = fnv1a(word, h);
    }
    h = fnv1a(e.data, h);
  }
  return { count: entries.length, fnv: hex8(h) };
}

/** Encoding: the length, plus FNV-1a over the bytes. */
export function bytesDigest(bytes: Uint8Array): { length: number; fnv: string } {
  return { length: bytes.length, fnv: hex8(fnv1a(bytes)) };
}

/** Is `lane` audible in the case after its first segment? The first is where systems boot, and a boot
 *  chime is not what a case means by "this lane sounds". Only the segments after it are judged, with no
 *  fallback: a case whose only segment is the boot has nothing to judge, so no lane sounds in it. */
export function laneSounds(d: CaseDigest, lane: number): boolean {
  const judged = d.segments.slice(1);
  return judged.some((s) => s.lanes[lane] !== undefined && !s.lanes[lane].endsWith(" 0"));
}

const describe = (lane: string): string => {
  const [fnv, rms] = lane.split(" ");
  return `${fnv} (rms ${rms})`;
};

/** Why `actual` is not `expected`, or [] when they match. Audio names the FIRST segment and lane that
 *  differ, which is where to look; `audio: false` compares only MIDI out, SRAM and work RAM (a platform
 *  whose float output cannot be held to the baseline's). */
export function compareDigests(expected: CaseDigest, actual: CaseDigest, opts: { audio: boolean }): string[] {
  const problems: string[] = [];
  if (opts.audio) problems.push(...compareAudio(expected, actual));
  if (expected.midiOut.count !== actual.midiOut.count || expected.midiOut.fnv !== actual.midiOut.fnv)
    problems.push(
      `MIDI out: expected ${expected.midiOut.count} message(s) hashing ${expected.midiOut.fnv}, ` +
        `got ${actual.midiOut.count} hashing ${actual.midiOut.fnv}`,
    );
  for (const [label, key] of [["SRAM", "sram"], ["work RAM", "ram"]] as const) {
    const slots = [...new Set([...Object.keys(expected[key]), ...Object.keys(actual[key])])].sort((a, b) => Number(a) - Number(b));
    for (const slot of slots) {
      const e = expected[key][slot];
      const a = actual[key][slot];
      if (!e || !a) problems.push(`${label} slot ${slot}: ${e ? "missing" : "unexpected"}`);
      else if (e.length !== a.length || e.fnv !== a.fnv)
        problems.push(`${label} slot ${slot}: expected ${e.length} bytes hashing ${e.fnv}, got ${a.length} hashing ${a.fnv}`);
    }
  }
  return problems;
}

/** The audio half of compareDigests: the first segment and lane that differ, or []. */
export function compareAudio(expected: CaseDigest, actual: CaseDigest): string[] {
  if (expected.segments.length !== actual.segments.length)
    return [`expected ${expected.segments.length} segment(s), got ${actual.segments.length}`];
  let start = 0;
  for (let s = 0; s < expected.segments.length; s++) {
    const e = expected.segments[s];
    const a = actual.segments[s];
    if (e.frames !== a.frames || e.lanes.length !== a.lanes.length)
      return [`segment ${s}: expected ${e.frames} frames x ${e.lanes.length} lanes, got ${a.frames} x ${a.lanes.length}`];
    for (let l = 0; l < e.lanes.length; l++)
      if (e.lanes[l].split(" ")[0] !== a.lanes[l].split(" ")[0]) {
        const silent = e.lanes[l].split(" ")[1] === "0" && a.lanes[l].split(" ")[1] === "0";
        return [
          `segment ${s} (frames ${start}..${start + e.frames}), lane ${l}: ` +
            `expected ${describe(e.lanes[l])}, got ${describe(a.lanes[l])}` +
            (silent ? " (both silent: only the sign of a zero differs)" : ""),
        ];
      }
    start += e.frames;
  }
  return [];
}

/** JSON with every object's keys sorted, one value per line: a re-record diffs line by line. Integer-like
 *  keys (the slot keys) come out in numeric order ahead of the rest, as the language orders them; the
 *  result is still deterministic. */
export function stableJson(v: unknown): string {
  return JSON.stringify(sortKeys(v), null, 1) + "\n";
}

/** The same sorted keys on ONE line: a case's actual digest in a failure message, so a CI log carries the
 *  run's output (the value of a baseline entry, ready to set beside it), not only the first difference. */
export function stableJsonLine(v: unknown): string {
  return JSON.stringify(sortKeys(v));
}

const sortKeys = (x: unknown): unknown =>
  Array.isArray(x)
    ? x.map(sortKeys)
    : x !== null && typeof x === "object"
      ? Object.fromEntries(Object.keys(x as object).sort().map((k) => [k, sortKeys((x as Record<string, unknown>)[k])]))
      : x;
