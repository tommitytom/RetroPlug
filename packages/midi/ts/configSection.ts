// The `midi` project section (docs/modular-graph-phase-f5b-design.md section 6.2): the ports a project listens to and
// mirrors to, and the clock tempo. A block under the .rplg's `settings`, distinct from the shell's own `midiRouting`
// (which is the graph's musical routing, unchanged).
//
// PROJECT state, not machine state (decision 5): a setup travels with the file, and the stored names are matched
// against what the machine lists when the project loads (portMatch.ts), so opening somebody else's project opens the
// ports it names where they exist. A fresh project has none - the virtual ports only - which is what a standalone that
// was never told otherwise played, and deliberate: opening every input by default makes anything plugged in a source,
// and a control surface's free-running clock then drives the host tempo.
//
// The entries are objects (`{ name }`) rather than bare strings so per-device routing can add a field later without a
// version bump. The section declares no `version`: everything about it is additive, so nothing is stamped and its bytes
// are what a first write makes them.
import type { ProjectSection } from "@retroplug/core/definePackage";

/** One named port. The entry is an OBJECT so per-device routing can add a field later without a version bump
 *  (decision 7), and every field this build does not read is carried through a load and a write: a project written by a
 *  newer build keeps its routing fields here. */
export type MidiPortEntry = { name: string } & Record<string, unknown>;

export interface MidiSection {
  inputs: MidiPortEntry[];
  /** Explicitly open every hardware input (the "All devices" row). A choice, not the default. */
  allInputs: boolean;
  outputs: MidiPortEntry[];
  /** The MIDI Clock submenu's local tempo. The transport's play state is not persisted: a tempo is a preference, a
   *  running clock is not. */
  clockBpm: number;
}

/** The section's key under `settings`, and the key its service watches. */
export const MIDI_SECTION_KEY = "midi";

/** The tempo range the host accepts, matching the window an external clock's estimate is trusted in
 *  (MidiClockSync::kMinBpm/kMaxBpm). */
export const CLOCK_BPM_MIN = 20;
export const CLOCK_BPM_MAX = 999;
export const CLOCK_BPM_STEP = 1;
export const CLOCK_BPM_COARSE_STEP = 10;
export const DEFAULT_CLOCK_BPM = 120;

const isObject = (v: unknown): v is Record<string, unknown> =>
  v !== null && typeof v === "object" && !Array.isArray(v);

/** One entry as an object: a bare name a first draft of the shape used reads as its object, and an entry keeps every
 *  field it carried beside `name`. */
function portEntry(entry: unknown): MidiPortEntry | null {
  if (typeof entry === "string") return entry ? { name: entry } : null;
  if (!isObject(entry)) return null;
  const name = typeof entry.name === "string" ? entry.name : "";
  return name ? ({ ...entry, name } as MidiPortEntry) : null;
}

/** Every entry as one port, in order, dropping the empty and the repeated names: two rows for one port would open it
 *  once and write the name twice. The FIRST entry for a name wins, with every field it carried. */
function portsOf(raw: unknown): MidiPortEntry[] {
  if (!Array.isArray(raw)) return [];
  const seen = new Set<string>();
  const out: MidiPortEntry[] = [];
  for (const entry of raw) {
    const port = portEntry(entry);
    if (!port || seen.has(port.name)) continue;
    seen.add(port.name);
    out.push(port);
  }
  return out;
}

function parseMidiSection(raw: unknown): MidiSection {
  const block = isObject(raw) ? raw : {};
  const bpm = block.clockBpm;
  return {
    inputs: portsOf(block.inputs),
    allInputs: block.allInputs === true,
    outputs: portsOf(block.outputs),
    clockBpm:
      typeof bpm === "number" && Number.isFinite(bpm)
        ? Math.min(CLOCK_BPM_MAX, Math.max(CLOCK_BPM_MIN, Math.round(bpm)))
        : DEFAULT_CLOCK_BPM,
  };
}

/** How a patch lands: a shallow merge, so a row writes one list or the tempo and leaves the rest as they are. Every
 *  value goes back through the parse, so a caller cannot save a shape a load would not read. */
function mergeMidiSection(current: MidiSection, patch: Readonly<Record<string, unknown>>): MidiSection {
  return parseMidiSection({ ...current, ...patch });
}

/** The section the generated registry hands the shell. */
export const midiSection: ProjectSection<MidiSection> = {
  key: MIDI_SECTION_KEY,
  schema: { parse: parseMidiSection },
  defaults: () => parseMidiSection({}),
  merge: mergeMidiSection,
};
