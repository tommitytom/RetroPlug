// The MIDI service and its rows (docs/modular-graph-phase-f5b-design.md section 6.5): a project's `midi` section is
// applied to the stream once per change (not once per poll), a name that resolves to nothing is kept and shown as
// missing, a project switch re-applies, and the clock tempo reaches the host. The device is a fake: no RtMidi, no
// hardware, nothing opened.
import { test, expect } from "@retroplug/testing";
import type { MidiDescription, MidiDeviceHost, MidiSelection } from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import type { MenuItem } from "@retroplug/ui/menu";
import midiUi from "../ui/index";
import { MIDI_SECTION_KEY, type MidiSection } from "../configSection";

const DESCRIPTION: MidiDescription = {
  inputs: ["Launchpad Pro MK3", "Arturia KeyStep 32"],
  outputs: ["Deluge"],
  openedInputs: ["Launchpad Pro MK3"],
  openedOutputs: [],
  supportsVirtual: true,
  reserved: { input: "", output: "" },
};

const SECTION: MidiSection = { inputs: [], allInputs: false, outputs: [], clockBpm: 120 };

interface Rig {
  host: UiHost;
  section: MidiSection;
  applied: { selection: MidiSelection; mode: string }[];
  clock: number[];
  transportSet: boolean[];
  setSection(patch: Record<string, unknown>): void;
  /** Adopt a whole project (a load): the section's bytes may not move, and the service must still re-resolve. */
  loadProject(patch?: Record<string, unknown>): void;
}

/** A host over a mutable section and a fake stream. Only the groups this package reaches are real. */
function rig(overrides: { describe?: () => MidiDescription; throwOnApply?: boolean } = {}): Rig {
  const mockSection: MidiSection = { ...SECTION };
  const applied: { selection: MidiSelection; mode: string }[] = [];
  const clock: number[] = [];
  const transportSet: boolean[] = [];
  let revision = 0;
  const device: MidiDeviceHost = {
    describe: overrides.describe ?? (() => DESCRIPTION),
    apply: (selection, mode = "diff") => {
      if (overrides.throwOnApply) throw new Error("the port refused to open");
      applied.push({ selection, mode });
    },
    transport: () => ({ playing: false, external: false, bpm: 120, localBpm: mockSection.clockBpm }),
    setTransport: (playing) => transportSet.push(playing),
    setClockBpm: (bpm) => clock.push(bpm),
  };
  const host = {
    midiDevice: device,
    project: {
      section: (key: string) => {
        if (key !== MIDI_SECTION_KEY) throw new Error(`no section ${key}`);
        return mockSection;
      },
      setSection: (key: string, patch: Record<string, unknown>) => {
        if (key !== MIDI_SECTION_KEY) throw new Error(`no section ${key}`);
        Object.assign(mockSection, patch);
      },
      revision: () => revision,
    },
  } as unknown as UiHost;
  return {
    host,
    section: mockSection,
    applied,
    clock,
    transportSet,
    setSection: (patch) => Object.assign(mockSection, patch),
    loadProject: (patch = {}) => {
      revision++;
      Object.assign(mockSection, patch);
    },
  };
}

const service = () => midiUi.services!.find((s) => s.id === "midi-ports")!;
const menu = (placement: "settings.midi" | "instance.clock") =>
  midiUi.menus!.find((m) => m.placement === placement)!;
const ids = (items: readonly MenuItem[]): string[] => items.map((i) => i.id);

/** A describe that counts its calls: what a frame costs the machine (a describe is a probe of it), which is the number
 *  the render loop must not multiply. */
let describes = 0;
function countingDescribe(): () => MidiDescription {
  return () => {
    describes++;
    return DESCRIPTION;
  };
}

test("a section change applies once, not once per poll", () => {
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }], clockBpm: 130 });
  expect(svc.poll!.run(r.host)).toBe(true); // the section moved: the checklists repaint
  expect(r.applied).toEqual([
    { selection: { inputs: ["Launchpad Pro MK3"], allInputs: false, outputs: [] }, mode: "diff" },
  ]);
  expect(svc.poll!.run(r.host)).toBe(false); // nothing moved
  expect(r.applied.length).toBe(1);
  // A second change applies again, still as a diff.
  r.setSection({ outputs: [{ name: "Deluge" }] });
  expect(svc.poll!.run(r.host)).toBe(true);
  expect(r.applied.at(-1)).toEqual({
    selection: { inputs: ["Launchpad Pro MK3"], allInputs: false, outputs: ["Deluge"] },
    mode: "diff",
  });
});

test("a project switch re-applies the new project's ports, in the diffing mode", () => {
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  svc.poll!.run(r.host); // the first project's (empty) selection
  // A project switch swaps the whole section: the old project's input is not in the new one.
  r.setSection({ inputs: [{ name: "Arturia KeyStep 32" }], allInputs: false, outputs: [] });
  expect(svc.poll!.run(r.host)).toBe(true);
  expect(r.applied.at(-1)!.selection.inputs).toEqual(["Arturia KeyStep 32"]);
});

test("a name that resolves to nothing opens nothing and stays saved", () => {
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  r.setSection({ inputs: [{ name: "Gone Synth" }] });
  svc.poll!.run(r.host);
  expect(r.applied.at(-1)!.selection.inputs).toEqual([]);
  expect(r.section.inputs).toEqual([{ name: "Gone Synth" }]); // the writer kept the entry
});

test("a saved name that is not present at all is shown as missing", () => {
  // A project saved on another machine: the port it named is not listed here. The entry stays in the section and the
  // row says so - it is not silently dropped.
  const r = rig();
  r.setSection({ inputs: [{ name: "Gone Synth" }] });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(rows.find((row) => row.id === "midi-missing-in-0")!.label).toBe("Gone Synth (missing)");
});

test("a saved port that resolved but did not open is unchecked, and tapping it retries the open", () => {
  // The saved port is present, but the stream has no such port open (it refused, exclusive on Windows): the row is
  // unchecked and it is NOT missing - it is here, and the tap's whole job is to try it again.
  const r = rig({ describe: () => ({ ...DESCRIPTION, openedInputs: [] }) });
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(rows.find((row) => row.id === "midi-input-0")!.label).toBe("[ ] Launchpad Pro MK3");
  expect(rows.some((row) => row.id === "midi-missing-in-0")).toBe(false);
  rows.find((row) => row.id === "midi-input-0")!.onSelect!();
  expect(r.section.inputs).toEqual([{ name: "Launchpad Pro MK3" }]); // the retry did not rewrite the project
  expect(r.applied.at(-1)!.mode).toBe("reopen"); // it asked the stream to open it again
});

test("a renumbered port reads checked, and one tap takes the saved name out", () => {
  // The case portMatch exists for: saved 24:0, present and OPEN 28:0. The row is the present port, checked, and a tap
  // removes the SAVED entry - the user never has to add 28:0 beside a stale 24:0 and cannot get rid of it.
  const present = ["LPProMK3 MIDI:LPProMK3 MIDI 28:0"];
  const r = rig({ describe: () => ({ ...DESCRIPTION, inputs: present, openedInputs: present }) });
  r.setSection({ inputs: [{ name: "LPProMK3 MIDI:LPProMK3 MIDI 24:0" }] });
  const build = () => menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(build().find((row) => row.id === "midi-input-0")!.label).toBe("[x] LPProMK3 MIDI:LPProMK3 MIDI 28:0");
  build().find((row) => row.id === "midi-input-0")!.onSelect!();
  expect(r.section.inputs).toEqual([]);
});

test("under All devices a tap on a checked row turns All devices off and takes the port out", () => {
  // The old rows did nothing visible here (the name was added while every input was already open).
  const r = rig();
  r.setSection({ allInputs: true });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(rows.find((row) => row.id === "midi-input-0")!.label).toBe("[x] Launchpad Pro MK3");
  rows.find((row) => row.id === "midi-input-0")!.onSelect!();
  expect(r.section.allInputs).toBe(false);
  expect(r.section.inputs).toEqual([]);
});

test("the control surface's port is shown held, never as a pick and never as missing", () => {
  // The stream cannot open a port the claim holds, so the row says who holds it - and a saved entry that resolves to it
  // is not "(missing)": the port IS here.
  const r = rig({
    describe: () => ({ ...DESCRIPTION, reserved: { input: "Launchpad Pro MK3", output: "" } }),
  });
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(rows.find((row) => row.id === "midi-reserved-in")!.label).toBe("[ ] Launchpad Pro MK3 (control surface)");
  // The held port is not offered as a pick under any index (the remaining input reindexes), and no missing row names
  // it.
  expect(rows.some((row) => row.id.startsWith("midi-input-") && row.label.includes("Launchpad Pro MK3")))
    .toBe(false);
  expect(rows.some((row) => row.id.startsWith("midi-missing-in-"))).toBe(false);
});

test("opening Settings > MIDI re-applies the saved section once, and a render asks for nothing", () => {
  // No section change: the list a project was resolved against may be older than the machine. The container's open hook
  // asks for one diff, the poll answers it, and building the rows - which App does on EVERY render - costs nothing.
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  svc.poll!.run(r.host); // the first apply
  const before = r.applied.length;
  const build = () => menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  build();
  expect(svc.poll!.run(r.host)).toBe(false); // a render is not a re-resolve
  expect(r.applied.length).toBe(before);
  menu("settings.midi").onOpen!(r.host); // the user expands it
  svc.poll!.run(r.host);
  expect(r.applied.length).toBe(before + 1);
  expect(svc.poll!.run(r.host)).toBe(false); // once, not every frame
});

test("a menu tree rebuilt per render applies nothing and reads the device once, not once per frame", () => {
  // App's cycle: a render rebuilds every visible tree (the start menu's Settings > MIDI included, menuDefs builds it
  // eagerly), a frame ticks the services, and a poll that answers true repaints. Sixty idle frames with the settings
  // rows on screen must cost the machine nothing: no apply, and no port listing beyond the one the rows render from.
  const r = rig({ describe: countingDescribe() });
  const svc = service();
  svc.start!(r.host);
  describes = 0;
  const build = () => menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  let repaints = 0;
  let pending = true; // the mount's first render
  for (let frame = 0; frame < 60; frame++) {
    if (pending) {
      build();
      pending = false;
    }
    if (svc.poll!.run(r.host)) {
      repaints++;
      pending = true;
    }
  }
  expect(r.applied.length).toBe(1); // the mount's first apply, and nothing after it
  expect(repaints).toBe(1); // one repaint: the first frame's rows
  expect(describes).toBeLessThanOrEqual(3); // the rows' first read, the poll's, and the one that reads what it opened
});

test("a project load re-resolves even when the new project saved the same ports", () => {
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  svc.poll!.run(r.host);
  const before = r.applied.length;
  r.loadProject(); // a different project, the same empty section
  svc.poll!.run(r.host);
  expect(r.applied.length).toBe(before + 1);
});

test("the clock tempo reaches the host, and a clock-only change applies no selection", () => {
  const r = rig();
  const svc = service();
  svc.start!(r.host);
  svc.poll!.run(r.host);
  r.setSection({ clockBpm: 174 });
  svc.poll!.run(r.host);
  expect(r.clock.at(-1)).toBe(174);
  expect(r.applied.length).toBe(1); // the section's ports did not change
});

test("a throw in apply is isolated to this service", () => {
  const r = rig({ throwOnApply: true });
  const svc = service();
  svc.start!(r.host);
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] });
  const warnings: string[] = [];
  const original = console.warn;
  console.warn = (...args: unknown[]) => void warnings.push(args.map(String).join(" "));
  try {
    expect(svc.poll!.run(r.host)).toBe(true); // the poll answered, and the frame continued
    expect(warnings.some((line) => line.includes("refused"))).toBe(true);
  } finally {
    console.warn = original;
  }
});

test("the Rescan row re-applies in the reopening mode", () => {
  const r = rig();
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  rows.find((row) => row.id === "midi-rescan")!.onSelect!();
  expect(r.applied.at(-1)!.mode).toBe("reopen");
});

test("the MIDI Clock submenu shows the transport and writes the tempo to the project", () => {
  const r = rig();
  const rows = menu("instance.clock").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  expect(ids(rows)).toEqual(["inst-sep-transport", "inst-clock"]);
  const clock = rows.find((row) => row.id === "inst-clock")!;
  expect(clock.label).toBe("MIDI Clock: Stopped");
  const tempo = clock.children!.find((row) => row.id === "inst-clock-tempo")!;
  expect(tempo.label).toBe("Tempo: 120 BPM");
  tempo.onCycle!(1);
  expect(r.section.clockBpm).toBe(121);
  const transport = clock.children!.find((row) => row.id === "inst-clock-transport")!;
  transport.onCycle!(1); // Stopped -> Playing
  expect(r.transportSet).toEqual([true]);
});

test("the rows are the project's: a pick writes the section and leaves the missing ones alone", () => {
  const r = rig();
  r.setSection({ inputs: [{ name: "Launchpad Pro MK3" }] });
  const rows = menu("settings.midi").build(r.host, null, { systems: [], assetRom: () => null, romPatterns: [] });
  // Both inputs are listed; the saved one is checked, the other not.
  expect(rows.find((row) => row.id === "midi-input-0")!.label).toBe("[x] Launchpad Pro MK3");
  expect(rows.find((row) => row.id === "midi-input-1")!.label).toBe("[ ] Arturia KeyStep 32");
  rows.find((row) => row.id === "midi-input-1")!.onSelect!();
  expect(r.section.inputs).toEqual([{ name: "Launchpad Pro MK3" }, { name: "Arturia KeyStep 32" }]);
  // Turning a row off removes its name; "All devices" is the explicit everything.
  rows.find((row) => row.id === "midi-input-0")!.onSelect!();
  expect(r.section.inputs).toEqual([{ name: "Arturia KeyStep 32" }]);
  rows.find((row) => row.id === "midi-all-inputs")!.onSelect!();
  expect(r.section.allInputs).toBe(true);
});

test("a host with no MIDI stream (a DAW's editor) contributes no row at all", () => {
  const host = { project: { section: () => SECTION, setSection: () => {} } } as unknown as UiHost;
  expect(menu("settings.midi").build(host, null, { systems: [], assetRom: () => null, romPatterns: [] })).toEqual([]);
  expect(menu("instance.clock").build(host, null, { systems: [], assetRom: () => null, romPatterns: [] })).toEqual([]);
});
