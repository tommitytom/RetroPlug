// The LSDj views over a fake host (docs/modular-graph-phase-f4c-design section 8.7): the HD session's cadence and the
// overlay's lines, with no display and no emulator. The pixel case needs the real ROM's fonts, so it reads the fixture
// and SKIPs without it.
import { test, expect, skip } from "@retroplug/testing";
import type { Host, SystemInfo } from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import { encodeSong, SongSchema } from "../index";
import { LsdjReader } from "../runtime";
import type { LsdjRom } from "../rom";
import { HdSession, hdView } from "../ui/hdView";
import { lsdjDebug, lsdjDebugOverlay } from "../ui/debugOverlay";
import { overlayView } from "../ui/overlayView";

/** The sibling resources tree, injected by the runner (scripts/run-tests.mjs) as the native suite's define is, so
 *  pointing it at a missing directory reproduces CI. */
declare const __RESOURCES_DIR__: string;
declare const tjs: { readFile(path: string): Promise<Uint8Array> };

/** A host member these views never call: the fake supplies every member of the groups it declares, so the type is the
 *  real contract and a view reaching for one more fails here rather than in production. */
function unused(): never {
  throw new Error("a view called a host member it does not use");
}

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/lsdj.gb",
    savPath: "/roms/lsdj.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [{ kind: "lsdj-sync", config: {} }],
    placed: ["lsdj-sync"],
    ...over,
  } as SystemInfo;
}

/** An mGB cart still storing the lsdj-sync it carried before a ROM swap: its graph places mgb and leaves lsdj-sync out,
 *  which is what the views ask. */
const STALE_SYNC = system({ roles: [{ kind: "mgb", config: {} }, { kind: "lsdj-sync", config: {} }], placed: ["mgb"] });

/** A host with the groups these views read. `state` is mutable so a case can change what the next read sees. */
function hostWith(state: {
  header?: Uint8Array | null;
  rom?: Uint8Array | null;
  ram?: Uint8Array | null;
  sram?: Uint8Array | null;
  systems?: readonly SystemInfo[];
  onSram?: () => void;
}): Host<"files" | "png" | "systems"> {
  return {
    files: {
      readPrefix: () => state.header ?? null,
      readFile: () => state.rom ?? null,
      writeFile: unused,
      writeFileAtomic: unused,
      fileExists: unused,
      listDir: unused,
    },
    png: { encode: unused, decode: unused },
    systems: {
      view: () => state.systems ?? [system()],
      readRam: () => state.ram ?? null,
      readSram: () => {
        state.onSram?.();
        return state.sram ?? null;
      },
      setRoleConfig: unused,
      reload: unused,
      loadSram: unused,
      savPathOf: unused,
      nodeIdOf: unused,
      currentId: unused,
      drivenCart: unused,
      stageNodeMidi: unused,
      capability: unused,
    },
  };
}

function header(title: string): Uint8Array {
  const h = new Uint8Array(0x150);
  for (let i = 0; i < title.length; i++) h[0x134 + i] = title.charCodeAt(i);
  return h;
}

/** The synthetic WRAM the recorded lines came from (real 9.4.2 offsets). */
function lsdjWram(): Uint8Array {
  const w = new Uint8Array(0x8000);
  w[0x0e0] = 1;
  w[0x0e1] = 1;
  w[0x168] = 0x10;
  w[0x169] = 0x11;
  w[0x16a] = 0xff;
  w[0x16b] = 0xff;
  w[0x16c] = 3;
  w[0x16d] = 4;
  w[0x16e] = 0xff;
  w[0x16f] = 0xff;
  w[0x200] = 2;
  w[0x201] = 5;
  w[0x202] = 0xff;
  w[0x203] = 0xff;
  w[0x401] = 4; // current screen: song
  w[0x41d] = 6; // song cursor col
  w[0x41e] = 1; // song cursor row
  w[0x529] = 128; // tempo
  return w;
}

test("the HD session reads SRAM on its 15-frame countdown and decodes only when the signature moved", () => {
  const reader = LsdjReader.fromHeader(header("LSDj-v9.4.2"))!;
  const sav = new Uint8Array(0x8000);
  sav.set(encodeSong(SongSchema.parse({})));
  let reads = 0;
  const host = hostWith({ sram: sav, onSram: () => (reads += 1), ram: lsdjWram() });
  const session = new HdSession(host, 1, reader, { fonts: () => [], palettes: () => [] } as unknown as LsdjRom);

  const at: number[] = [];
  const identities: string[] = [];
  let first: unknown = null;
  for (let frame = 0; frame < 33; frame++) {
    const before = reads;
    session.frame();
    if (reads > before) at.push(frame);
    if (frame === 0) first = session.song;
    identities.push(session.song === null ? "null" : session.song === first ? "first" : "other");
    if (frame === 16) sav[0] ^= 0x01; // an edit between polls
  }

  // The first frame, then every 16th, and a re-decode only on the edit.
  expect(at).toEqual([0, 16, 32]);
  expect(identities.slice(0, 32).every((id) => id === "first")).toBeTruthy();
  expect(identities[32]).toBe("other");
  // Nothing is dirty while the canvas has no font yet, so every frame draws nothing.
  expect(session.frame()).toBe(null);
});

test("the HD view refuses what the old hook gated on: no placed LSDj role, no ROM path, an unreadable header", () => {
  expect(hdView.body.kind).toBe("pixels");
  if (hdView.body.kind !== "pixels") return;
  expect(hdView.body.unsupported).toBe("HD view needs a supported LSDj cartridge");
  expect(hdView.when?.(system())).toBeTruthy();
  expect(hdView.when?.(system({ roles: [], placed: [] }))).toBeFalsy();
  expect(hdView.when?.(STALE_SYNC)).toBeFalsy();

  const open = (state: Parameters<typeof hostWith>[0], sys = system()) =>
    hdView.body.kind === "pixels" ? hdView.body.open(hostWith({ ...state, systems: [sys] }), sys.id) : null;
  expect(open({ header: header("LSDj-v9.4.2"), rom: null })).toBe(null); // no ROM bytes
  expect(open({ header: null, rom: new Uint8Array(4) })).toBe(null); // no readable header
  expect(open({ header: header("MGB"), rom: new Uint8Array(4) })).toBe(null); // not an LSDj cart
  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ roles: [], placed: [] }))).toBe(null);
  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, STALE_SYNC)).toBe(null);
  expect(open({ header: header("LSDj-v9.4.2"), rom: new Uint8Array(4) }, system({ romPath: "" }))).toBe(null);
});

test("the LSDj overlay's lines are the old overlay's", () => {
  const state = { header: header("LSDj-v9.4.2"), ram: lsdjWram() as Uint8Array | null };
  const host = hostWith(state);
  const sampler = overlayView.body.kind === "readout" ? overlayView.body.open(host, 1) : null;
  expect(sampler != null).toBeTruthy();
  const frame = sampler!.sample();
  expect(frame).toEqual({
    lines: ["SONG @6,1", "PLAY S05  128bpm  v9.4.2", "1 10.03  2 11.04  W --  N --"],
    playing: true,
  });

  // An unreadable RAM frame keeps what shows (undefined).
  state.ram = null;
  expect(sampler!.sample()).toBe(undefined);

  // The overlay applies to a system whose graph places lsdj-sync and opens on nothing else: a system without the role
  // or storing it unplaced (an mGB cart after a ROM swap), a header that is not an LSDj cart (the reader's own
  // `supported` gate, which is how this view says "unsupported": a sampler it opened is always one whose reader
  // resolves a layout), and no header at all.
  const open = (sys = system()) =>
    overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("LSDj-v9.4.2"), systems: [sys] }), sys.id) : null;
  expect(overlayView.when?.(system())).toBeTruthy();
  expect(overlayView.when?.(system({ roles: [], placed: [] }))).toBeFalsy();
  expect(overlayView.when?.(STALE_SYNC)).toBeFalsy();
  expect(open(system({ roles: [], placed: [] }))).toBe(null);
  expect(open(STALE_SYNC)).toBe(null);
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: null }), 1) : null).toBe(null);
  expect(overlayView.body.kind === "readout" ? overlayView.body.open(hostWith({ header: header("MGB") }), 1) : null).toBe(null);
});

test("the debug flag is off at start, and the action flips it (and works on the start screen)", () => {
  expect(lsdjDebug.get()).toBe(false);
  expect(overlayView.visible).toBe(lsdjDebug);
  lsdjDebugOverlay.run({} as UiHost, { systemId: null });
  expect(lsdjDebug.get()).toBe(true);
  lsdjDebugOverlay.run({} as UiHost, { systemId: null });
  expect(lsdjDebug.get()).toBe(false);
  expect(lsdjDebugOverlay.id).toBe("lsdj.debug-overlay");
  expect(lsdjDebugOverlay.keys).toEqual(["`"]);
  expect(lsdjDebugOverlay.label).toBe("LSDj Debug Overlay");
});

test("the HD view paints the real cartridge and skips an unchanged frame (fixture)", async () => {
  const rom = await tjs.readFile(`${__RESOURCES_DIR__}/roms/lsdj/lsdj9_4_2.gb`).catch(() => null);
  if (!rom) skip("lsdj views: the LSDj fixture ROM is absent");
  const sav = await tjs.readFile("../lsdj/ts/test/savs/all.sav");
  const host = hostWith({ header: rom.subarray(0, 0x150), rom, sram: sav, ram: lsdjWram() });
  const session = hdView.body.kind === "pixels" ? hdView.body.open(host, 1) : null;
  expect(session != null).toBeTruthy();
  const first = session!.frame();
  expect(first != null).toBeTruthy();
  expect(first!.width).toBe(792);
  expect(first!.height).toBe(576);
  expect(first!.pixels.length).toBe(first!.width * first!.height);
  // The same live state again: the canvas flushed no tile, so there is nothing to push.
  expect(session!.frame()).toBe(null);
});
