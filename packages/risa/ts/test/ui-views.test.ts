// risa's debug overlay over a fake host (docs/modular-graph-phase-f4c-design section 8.7): the lines the old overlay
// drew, asserted over a synthetic RAM image; no display, no emulator.
import { test, expect } from "@retroplug/testing";
import type { Host, SystemInfo } from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import { resolveRisaLayout } from "../runtime";
import { overlayView } from "../ui/overlayView";
import { risaDebug, risaDebugOverlay } from "../ui/debugOverlay";

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "nes",
    core: "mesen",
    romPath: "/roms/risa.nes",
    savPath: "/roms/risa.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [{ kind: "risa", config: {} }],
    ...over,
  } as SystemInfo;
}

/** A host member these views never call: the fake supplies every member of the groups it declares, so the type is the
 *  real contract and a view reaching for one more fails here rather than in production. */
function unused(): never {
  throw new Error("a view called a host member it does not use");
}

function hostWith(state: {
  rom?: Uint8Array | null;
  ram?: Uint8Array | null;
  systems?: readonly SystemInfo[];
}): Host<"files" | "systems"> {
  return {
    files: {
      readFile: () => state.rom ?? null,
      readPrefix: unused,
      writeFile: unused,
      writeFileAtomic: unused,
      fileExists: unused,
    },
    systems: {
      view: () => state.systems ?? [system()],
      readRam: () => state.ram ?? null,
      readSram: unused,
      setRoleConfig: unused,
      reload: unused,
      loadSram: unused,
      savPathOf: unused,
      nodeIdOf: unused,
      drivenCart: unused,
      stageNodeMidi: unused,
      capability: unused,
    },
  };
}

/** A ROM carrying risa's version marker where identifyRisaVersion scans for it. */
function risaRom(version = "2.2.1"): Uint8Array {
  const rom = new Uint8Array(0x400);
  rom.set([0x4e, 0x45, 0x53, 0x1a], 0); // iNES header
  const marker = `RISA V${version}`;
  for (let i = 0; i < marker.length; i++) rom[0x123 + i] = marker.charCodeAt(i);
  return rom;
}

/** The synthetic RAM the recorded lines came from (the old runtime test's builder). */
function risaRam(): Uint8Array {
  const layout = resolveRisaLayout("2.2.1")!;
  const ram = new Uint8Array(0x800);
  ram[layout.seqMode] = 1;
  ram[layout.seqActive] = 0b00101;
  ram[layout.bpm] = 128;
  ram[layout.currentScreen] = 2;
  ram[layout.cursorRow] = 3;
  ram[layout.cursorCol] = 4;
  ram[layout.songLastRow + 0] = 0xff;
  ram[layout.songRow + 0] = 7;
  ram[layout.phraseId + 0] = 10;
  ram[layout.phraseLastRow + 0] = 5;
  for (const base of [
    layout.songLastRow,
    layout.songRow,
    layout.chainId,
    layout.chainRow,
    layout.phraseId,
    layout.phraseLastRow,
    layout.note,
    layout.lastInst,
  ])
    ram[base + 1] = 0xff;
  ram[layout.phraseId + 2] = 0x20;
  ram[layout.phraseLastRow + 2] = 0x0a;
  return ram;
}

test("the risa overlay's lines are the old overlay's", () => {
  const state = { rom: risaRom() as Uint8Array | null, ram: risaRam() as Uint8Array | null };
  const host = hostWith(state);
  const sampler = overlayView.body.kind === "readout" ? overlayView.body.open(host, 1) : null;
  expect(sampler != null).toBeTruthy();
  expect(sampler!.sample()).toEqual({
    lines: ["SONG @4,3", "SONG  128bpm  v2.2.1", "1 0A.05  2 --  T 20.0A  N --  D --"],
    playing: true,
  });

  // Unreadable RAM keeps the strip (undefined).
  state.ram = null;
  expect(sampler!.sample()).toBe(undefined);

  // A RAM image too short for the layout is an unsupported state, so the strip shows nothing (null): the reader
  // degrades rather than reading past the snapshot's end.
  state.ram = new Uint8Array(4);
  expect(sampler!.sample()).toBe(null);

  // A ROM no version resolves opens nothing.
  const noVersion =
    overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ rom: new Uint8Array(4) }), 1) : null;
  expect(noVersion).toBe(null);
});

test("the risa overlay applies to risa systems only, and opens none without the role", () => {
  expect(overlayView.when?.(system())).toBeTruthy();
  expect(overlayView.when?.(system({ roles: [{ kind: "lsdj-sync", config: {} }] }))).toBeFalsy();
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({}), 1) : null).toBe(null);
  const noRole = system({ roles: [] });
  const sampler =
    overlayView.body.kind === "readout"
      ? overlayView.body.open(hostWith({ rom: risaRom(), systems: [noRole] }), noRole.id)
      : null;
  expect(sampler).toBe(null);
});

test("risa's debug flag is off at start, and its action flips it", () => {
  expect(risaDebug.get()).toBe(false);
  expect(overlayView.visible).toBe(risaDebug);
  risaDebugOverlay.run({} as UiHost, { systemId: null });
  expect(risaDebug.get()).toBe(true);
  expect(risaDebugOverlay.id).toBe("risa.debug-overlay");
  expect(risaDebugOverlay.keys).toEqual(["`"]);
  expect(risaDebugOverlay.label).toBe("risa Debug Overlay");
  risaDebugOverlay.run({} as UiHost, { systemId: null });
  expect(risaDebug.get()).toBe(false);
});
