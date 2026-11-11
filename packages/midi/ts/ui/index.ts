// The midi package's UI contribution (docs/modular-graph-phase-f5b-design.md section 6.5): Settings > MIDI's rows, at
// `settings.midi` (beside the control surface's scan rows, which the Launchpad pack adds there), and the MIDI Clock
// submenu at `instance.clock`. The value they edit is the project's `midi` section through the host's `project` group,
// so the shell names no port, no list and no tempo.
//
// A selection APPLIES AT ONCE and marks the project dirty (a section write does both), unlike Settings > Audio's draft:
// there is nothing to stage, and a port pick is something you want to hear. The menu model has no checkbox kind, so the
// checklists are columns of action rows labelled "[x]" / "[ ]".
import { action, cycler, infoRow, separator, submenu, type MenuItem } from "@retroplug/ui/menu";
import { defineUi, type MenuBuilder, type UiHost, type UiService } from "@retroplug/ui/contribution";
import type { MidiDescription, MidiDeviceHost, MidiSelection, TransportState } from "@retroplug/core/host";
import {
  CLOCK_BPM_COARSE_STEP, CLOCK_BPM_MAX, CLOCK_BPM_MIN, CLOCK_BPM_STEP, DEFAULT_CLOCK_BPM, MIDI_SECTION_KEY,
  type MidiPortEntry, type MidiSection,
} from "../configSection";
import { resolvePort, resolvePorts } from "../portMatch";

export * from "../configSection";
export * from "../portMatch";

/** The saved entries' names, in the order they were saved. */
const savedNames = (entries: readonly MidiPortEntry[]): string[] => entries.map((e) => e.name);

/** The saved entries that resolve to one present port: a renumbered port can be named by more than one saved entry,
 *  and un-picking the row has to take every one of them out. */
function entriesFor(entries: readonly MidiPortEntry[], present: readonly string[], port: string): MidiPortEntry[] {
  return entries.filter((entry) => resolvePort(entry.name, present) === port);
}

/** What a project's section resolves to on this machine: EXACT port names for the host, and the saved entries that
 *  resolve to NO present port (the ones the rows show as missing). An entry that resolves to a port which did not open
 *  is not missing: the row is unchecked, and tapping it retries it. */
function resolveSelection(
  section: MidiSection,
  described: MidiDescription,
): { selection: MidiSelection; missingInputs: MidiPortEntry[]; missingOutputs: MidiPortEntry[] } {
  const inputs = resolvePorts(savedNames(section.inputs), described.inputs);
  const outputs = resolvePorts(savedNames(section.outputs), described.outputs);
  const missingInputs = section.inputs.filter((entry) => resolvePort(entry.name, described.inputs) === null);
  const missingOutputs = section.outputs.filter((entry) => resolvePort(entry.name, described.outputs) === null);
  return { selection: { inputs, allInputs: section.allInputs, outputs }, missingInputs, missingOutputs };
}

/** Apply a section to the device against a listing: the service's project-load path (a diff, so a load costs no
 *  connection it already had) and the Rescan row's (a reopen, the one remedy for a port that was unplugged). The listing
 *  is handed in rather than read here, because a describe is a probe of the machine and the caller knows whether this
 *  moment is one the design names for it. */
function applySection(
  dev: MidiDeviceHost,
  section: MidiSection,
  mode: "diff" | "reopen",
  described: MidiDescription,
): void {
  try {
    dev.apply(resolveSelection(section, described).selection, mode);
  } catch (e) {
    // A refused apply (an RPC failure in a facet, a port that would not open) must not escape into a frame's polling or
    // a row's click: the rows read what opened next frame.
    console.warn(`[midi] the selection was refused: ${e instanceof Error ? e.message : String(e)}`);
  }
}

/** Apply, then read the machine AGAIN, so the rows show what the apply actually opened. Every apply path needs it: the
 *  retry tap and the Rescan row change no section byte, and neither raises any of the edges the poll reacts to, so
 *  without this second read the row keeps printing "[ ]" over a port that is now open and the tap looks dead (it is the
 *  one remedy Windows has for an exclusive port, and the Rescan row's whole purpose). Read after the apply, not before:
 *  a listing taken first still says the port is closed. A click or a service edge is one of the moments a describe is
 *  allowed (a builder never probes), and the read is what makes the next poll report a repaint. */
function applyAndRelist(
  dev: MidiDeviceHost,
  section: MidiSection,
  mode: "diff" | "reopen",
  described: MidiDescription,
): void {
  applySection(dev, section, mode, described);
  listing = { dev, value: dev.describe() ?? described };
}

/** The listing the rows render from, with the DEVICE it belongs to. The service refreshes its own; a builder only reads
 *  it, because a describe is a probe of the machine (on ALSA an RtMidi client created and destroyed, which every other
 *  client sees announced). Keyed by the device, so a host that swaps its stream (a test rig, a rebuilt session) reads
 *  the new one instead of showing the old one's ports. */
let listing: { dev: MidiDeviceHost; value: MidiDescription | null } | null = null;
function currentDescription(dev: MidiDeviceHost): MidiDescription | null {
  if (listing?.dev !== dev) listing = { dev, value: dev.describe() };
  return listing.value;
}

/** The parts of a listing that move when the MACHINE's ports move: what a project's saved names are resolved against. */
function listedPorts(d: MidiDescription | null): string {
  return d ? JSON.stringify({ inputs: d.inputs, outputs: d.outputs, reserved: d.reserved }) : "";
}

/** Everything the rows print, so the poll reports a repaint only when one of them moved - a section change that does not
 *  show is no reason to rebuild the whole UI. */
function rowsKey(d: MidiDescription | null, section: MidiSection, t: TransportState | null): string {
  return JSON.stringify({
    listed: listedPorts(d),
    opened: d ? { inputs: d.openedInputs, outputs: d.openedOutputs } : null,
    inputs: section.inputs,
    allInputs: section.allInputs,
    outputs: section.outputs,
    bpm: section.clockBpm,
    transport: t ? [t.playing, t.external, Math.round(t.bpm), Math.round(t.localBpm)] : null,
  });
}

/** One checklist row: "[x] Name" or "[ ] Name", staying open so several ports can be picked in a row. */
function portRow(id: string, name: string, checked: boolean, onSelect: () => void): MenuItem {
  return { ...action(id, `${checked ? "[x]" : "[ ]"} ${name}`, onSelect), keepOpen: true };
}

/** A port the control surface holds: never openable by the stream, so it is marked and never offered as a pick or as
 *  "(missing)" - it IS here, and it is held. */
function reservedRow(id: string, name: string): MenuItem {
  return infoRow(id, `[ ] ${name} (control surface)`);
}

function midiSettingsChildren(host: UiHost): MenuItem[] {
  const dev = host.midiDevice;
  if (!dev) return []; // no MIDI stream on this host (a DAW routes MIDI): the container is empty
  const described = currentDescription(dev);
  if (!described) return []; // a stream that cannot describe itself shows no picker
  const section = host.project.section<MidiSection>(MIDI_SECTION_KEY);
  const { missingInputs, missingOutputs } = resolveSelection(section, described);
  const write = (patch: Record<string, unknown>) => host.project.setSection(MIDI_SECTION_KEY, patch);

  /** Toggle one port in one direction. A checked row is open (or every input is wanted): taking it out removes every
   *  entry that resolves to it and turns All devices off, so the tap always does something real. An unchecked row adds
   *  the name; one that is already saved but did not open is a RETRY (a reopen), which is what the exclusive-port case
   *  on Windows needs. */
  const toggle = (direction: "inputs" | "outputs", port: string, checked: boolean): void => {
    const entries = direction === "inputs" ? section.inputs : section.outputs;
    const named = entriesFor(entries, direction === "inputs" ? described.inputs : described.outputs, port);
    if (checked) {
      write({
        [direction]: entries.filter((entry) => !named.includes(entry)),
        ...(direction === "inputs" ? { allInputs: false } : {}),
      });
    } else if (named.length > 0) {
      applyAndRelist(dev, section, "reopen", described); // saved, resolved, refused: try it again
    } else {
      write({ [direction]: [...entries, { name: port }] });
    }
  };

  const reservedIn = described.reserved.input;
  const reservedOut = described.reserved.output;
  const inputRows = described.inputs
    .filter((name) => name !== reservedIn)
    .map((name, i) => {
      const saved = entriesFor(section.inputs, described.inputs, name).length > 0;
      const checked = described.openedInputs.includes(name) && (section.allInputs || saved);
      return portRow(`midi-input-${i}`, name, checked, () => toggle("inputs", name, checked));
    });
  const outputRows = described.outputs
    .filter((name) => name !== reservedOut)
    .map((name, i) => {
      const checked =
        described.openedOutputs.includes(name) &&
        entriesFor(section.outputs, described.outputs, name).length > 0;
      return portRow(`midi-output-${i}`, name, checked, () => toggle("outputs", name, checked));
    });
  return [
    infoRow("midi-project", "Saved with this project"),
    // Every hardware input at once, an explicit choice rather than the default: opening them all makes anything
    // plugged in a MIDI source, so a control surface's free-running clock would drive the host tempo.
    action("midi-all-inputs", `${section.allInputs ? "[x]" : "[ ]"} All devices`,
      () => write({ allInputs: !section.allInputs })),
    ...inputRows,
    ...(reservedIn ? [reservedRow("midi-reserved-in", reservedIn)] : []),
    ...missingInputs.map((entry, i) => infoRow(`midi-missing-in-${i}`, `${entry.name} (missing)`)),
    separator("midi-sep-outputs"),
    ...outputRows,
    ...(reservedOut ? [reservedRow("midi-reserved-out", reservedOut)] : []),
    ...missingOutputs.map((entry, i) => infoRow(`midi-missing-out-${i}`, `${entry.name} (missing)`)),
    separator("midi-sep-rescan"),
    // Re-resolve the saved names against a fresh listing and reopen every port: the one remedy for a device that was
    // unplugged and plugged back in (there is no hotplug watch). A click, so the fresh read is this row's own.
    {
      ...action("midi-rescan", "Rescan", () => {
        const fresh = dev.describe();
        if (!fresh) return;
        applyAndRelist(dev, section, "reopen", fresh);
      }),
      keepOpen: true,
    },
  ];
}

/** Settings > MIDI's rows, in the shell's MIDI container. Opening that container is one of the two moments the design
 *  names for a re-resolve (the other is a project load), and the shell's open hook is how a package hears it: the
 *  service re-reads the machine and diffs once, and no render touches a device. */
const midiMenu: MenuBuilder = {
  placement: "settings.midi",
  build: (host) => midiSettingsChildren(host),
  onOpen: () => requestReapply(),
};

/** The MIDI Clock submenu (Instance menu > MIDI Clock): a clock control, not a cosmetic one, because while the
 *  transport runs the LSDj / risa sync roles turn it into link-port clock for the cart. The tempo is the project's now;
 *  the transport starts stopped at every launch and is never saved. While an external master is running it owns both
 *  values, so the rows show ITS state and go inert. */
function clockChildren(host: UiHost, t: NonNullable<ReturnType<MidiDeviceHost["transport"]>>, bpm: number): MenuItem[] {
  if (t.external) {
    return [
      infoRow("inst-clock-source", "Source: External MIDI clock"),
      infoRow("inst-clock-transport", `Transport: ${t.playing ? "Playing" : "Stopped"}`),
      infoRow("inst-clock-tempo", `Tempo: ${Math.round(t.bpm)} BPM`),
    ];
  }
  const step = (delta: number) =>
    host.project.setSection(MIDI_SECTION_KEY, {
      clockBpm: Math.min(CLOCK_BPM_MAX, Math.max(CLOCK_BPM_MIN, Math.round(bpm + delta))),
    });
  return [
    cycler("inst-clock-transport", "Transport", ["Stopped", "Playing"], t.playing ? 1 : 0, (n) =>
      host.midiDevice?.setTransport(n === 1),
    ),
    {
      // Not a `cycler`: the tempo is a range, not a list of options. Same keys though - Left/Right step by one,
      // PageUp/PageDown by ten (onCoarseStep, as the render-duration row uses), Enter nudges up.
      id: "inst-clock-tempo",
      label: `Tempo: ${Math.round(t.localBpm)} BPM`,
      kind: "cycler",
      keepOpen: true,
      onSelect: () => step(CLOCK_BPM_STEP),
      onCycle: (dir) => step(dir * CLOCK_BPM_STEP),
      onCoarseStep: (dir) => step(dir * CLOCK_BPM_COARSE_STEP),
    },
  ];
}

function clockRow(host: UiHost): MenuItem[] {
  const dev = host.midiDevice;
  const t = dev?.transport();
  if (!dev || !t) return [];
  const bpm = host.project.section<MidiSection>(MIDI_SECTION_KEY).clockBpm;
  const state = t.playing ? `${Math.round(t.bpm)} BPM` : "Stopped";
  return [separator("inst-sep-transport"), submenu("inst-clock", `MIDI Clock: ${state}`, clockChildren(host, t, bpm))];
}

/** The host-transport block, between the instance's own groups. */
const clockMenu: MenuBuilder = {
  placement: "instance.clock",
  build: (host) => clockRow(host),
};

/** The listing the rows render from, the section the last apply resolved, and what the rows printed last: the latches a
 *  mount puts back, and what makes the poll report a change once rather than every frame. */
let lastSection = "";
let lastClock = Number.NaN;
let lastRevision = Number.NaN;
let lastRows = "";
/** A re-resolve is due: the shell's Settings > MIDI container was opened, and the list a project was resolved against may
 *  be older than the machine (a device plugged in since). */
let reapplyRequested = false;

/** Ask the service to re-read and diff once on its next poll (the container opened). */
function requestReapply(): void {
  reapplyRequested = true;
}

/** The MIDI service: watches the project's `midi` section and applies it (a project load, a row's pick, a Rescan), drives
 *  the clock tempo, and repaints the rows when what they show moves.
 *
 *  It re-resolves on the edges only, never on a render: the mount, a section or project change, the container opening,
 *  and a listing that is not the one the last apply resolved against. That last one is what "a device plugged in since"
 *  means - there is no hotplug watch, so the machine is read when one of the other edges makes it worth reading.
 *
 *  The FIRST apply lands on the first poll, not on start: the services start in product order and the Launchpad's
 *  restore (a claim) comes after this package's, so a hardware port the claim holds is never opened first and then
 *  taken away - on Windows that would be a refused open, everywhere else a pad heard twice for a frame. The same
 *  ordering is why the AUDIO service applies on its first poll too. */
const midiService: UiService = {
  id: "midi-ports",
  start: () => {
    listing = null;
    lastSection = "";
    lastClock = Number.NaN;
    lastRevision = Number.NaN;
    lastRows = "";
    reapplyRequested = false;
  },
  poll: {
    frames: 1,
    run: (host) => {
      const dev = host.midiDevice;
      if (!dev) return false;
      const section = host.project.section<MidiSection>(MIDI_SECTION_KEY);
      // The tempo is applied on its own below: a clock-only change must not reopen the ports.
      const key = JSON.stringify({ inputs: section.inputs, allInputs: section.allInputs, outputs: section.outputs });
      // A project load re-resolves even when the new project saved the same names (another machine's ports may be the
      // ones that are here now).
      const revision = host.project.revision();
      if (listing?.dev !== dev || reapplyRequested || key !== lastSection || revision !== lastRevision) {
        // The fresh listing is what the saved names are resolved against, so a port that came or went since the last
        // apply (the machine's list changed, not the project's) is exactly what this read picks up.
        const fresh = dev.describe();
        if (fresh) {
          lastSection = key;
          lastRevision = revision;
          reapplyRequested = false;
          // What the apply opened, so the checkboxes follow a pick without waiting for another edge.
          applyAndRelist(dev, section, "diff", fresh);
        }
      }
      const bpm = section.clockBpm || DEFAULT_CLOCK_BPM;
      if (bpm !== lastClock) {
        lastClock = bpm;
        dev.setClockBpm(bpm);
      }
      // The rows follow the listing, the section and the transport: a poll repaints only when one of them moved.
      const rows = rowsKey(listing?.value ?? null, section, dev.transport());
      const moved = rows !== lastRows;
      lastRows = rows;
      return moved;
    },
  },
};

export default defineUi({
  menus: [midiMenu, clockMenu],
  services: [midiService],
});
