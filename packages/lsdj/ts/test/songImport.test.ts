// LSDj's song importer (lsdjSongImport.ts), the batch behind the Songs menu's Add and a drag-and-drop: fold one or more
// .lsdsng/.lsdprj into the live SAV in ONE readSram -> edit -> write -> loadSram cycle, with a .lsdprj's kits recorded as
// lsdj-assets overrides. Driven over a fake host built from the three groups an import uses - files, png, systems - so
// every write, reload and role edit the import makes is recorded here, and no store is needed.
import { test, expect } from "@retroplug/testing";
import { sameBytes } from "@retroplug/testing/bytes";
import type { Host, SystemInfo } from "@retroplug/core/host";
import { LSDJ_SONG_IMPORT } from "../lsdjSongImport";
import { savFrom, encodeLsdsngRaw, encodeLsdprj, decompressSlot, listProjects } from "..";
import { applyOverridesToRom, readOverrides, type LsdjAssetOverride } from "../lsdjAssetsRole";
import { LsdjRom, buildKitBank, ROM_SIZE } from "../rom";

/** A 1 MiB LSDj image: a GB battery header and a version title, all kit slots free. */
function lsdjRom1M(title = "LSDJ-V9.1.C"): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set([0xce, 0xed, 0x66, 0x66], 0x104); // the Game Boy's logo; the importer only reads bytes here
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  return b;
}

const song = (tempo: number, sentinel: number): Uint8Array => {
  const raw = savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();
  raw[0x1730] = sentinel;
  return raw;
};
const rawSong = (tempo: number) => savFrom({ workingSong: { settings: { tempo } } }).subarray(0, 0x8000).slice();

/** The fake shell an import is handed: an in-memory disk, one live battery, one written .sav, and the role edits and
 *  reloads it made. `writeOk` is the disk's answer, so a failed write is a case. `host()` is the three groups the import
 *  uses, over this fake. */
class FakeHost {
  readonly files = new Map<string, Uint8Array>();
  readonly writes: { path: string; bytes: Uint8Array }[] = [];
  readonly loads: { id: number; path: string }[] = [];
  readonly roleEdits: { id: number; kind: string; config: Record<string, unknown> }[] = [];
  sram: Uint8Array;
  writeOk = true;
  private readonly roles = new Map<string, Record<string, unknown>>();

  /** The host the import calls: the disk's files, a PNG codec that decodes nothing, and the one fake system. */
  host(): Host<"files" | "png" | "systems"> {
    return {
      files: {
        readFile: (path) => this.files.get(path) ?? null,
        readPrefix: (path, bytes) => this.files.get(path)?.slice(0, bytes) ?? null,
        writeFile: (path, bytes) => this.write(path, bytes),
        writeFileAtomic: (path, bytes) => this.writeFileAtomic(path, bytes),
        fileExists: (path) => this.files.has(path),
      },
      png: { encode: () => null, decode: () => null },
      systems: {
        view: () => [],
        setRoleConfig: (id, kind, config) => this.setRoleConfig(id, kind, config),
        reload: () => null,
        readSram: () => this.readSram(),
        readRam: () => null,
        loadSram: (id, path) => this.loadSram(id, path),
        savPathOf: () => this.savPathOf(),
        nodeIdOf: (id) => `sys-${id}`,
        drivenCart: () => null,
        stageNodeMidi: () => false,
        capability: () => Promise.reject(new Error("a song import runs no capability job")),
      },
    };
  }

  private write(path: string, bytes: Uint8Array): boolean {
    if (!this.writeOk) return false;
    this.files.set(path, bytes);
    return true;
  }

  constructor(rom: Uint8Array, sram: Uint8Array) {
    this.files.set("/roms/song.gb", rom);
    this.sram = sram;
  }

  writeFileAtomic(path: string, bytes: Uint8Array): boolean {
    if (!this.write(path, bytes)) return false;
    this.writes.push({ path, bytes });
    return true;
  }

  readSram(): Uint8Array | null {
    return this.sram;
  }

  loadSram(id: number, path: string): boolean {
    this.loads.push({ id, path });
    return true;
  }

  setRoleConfig(_id: number, kind: string, config: Record<string, unknown>): boolean {
    this.roles.set(kind, config);
    this.roleEdits.push({ id: _id, kind, config });
    return true;
  }

  savPathOf(): string | null {
    return "/roms/song.sav";
  }
}

function target(): SystemInfo {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/song.gb",
    savPath: "",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [{ kind: "lsdj-assets", config: { overrides: [] } }],
  };
}

test("a .lsdsng + a .lsdprj land in ONE reload; kits ride overrides", () => {
  const host = new FakeHost(lsdjRom1M(), savFrom({})); // blank live battery
  const aSong = song(100, 0x11); // a plain .lsdsng: stays byte-exact
  // a .lsdprj whose song references kit 5, so its bundled bank imports as an override and the refs remap
  const bSong = savFrom({ workingSong: { settings: { tempo: 140 }, instruments: [{ type: "kit", kit1: 5, kit2: 5 }] } }).subarray(0, 0x8000);
  host.files.set("/s/a.lsdsng", encodeLsdsngRaw("AAA", 1, aSong));
  host.files.set(
    "/s/b.lsdprj",
    encodeLsdprj({ name: "BBB", version: 2, songBytes: bSong, kitBanks: [buildKitBank("DRUMS", [{ name: "BD", bytes: Uint8Array.of(1, 2, 3, 4) }])] }),
  );

  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/s/a.lsdsng", "/s/b.lsdprj"])).toBeTruthy();

  // Exactly one rebuild for the whole batch, from the one file written.
  expect(host.loads).toEqual([{ id: 1, path: "/roms/song.sav" }]);
  expect(host.writes.length).toBe(1);
  const sav = host.writes[0].bytes;
  expect(listProjects(sav).map((p) => p.slot)).toEqual([0, 1]); // both songs present
  expect(sameBytes(decompressSlot(sav, 0)!, aSong)).toBe(true); // .lsdsng byte-exact in slot 0
  expect(sameBytes(sav.subarray(0, 0x8000), decompressSlot(sav, 1)!)).toBe(true); // working memory = the LAST song

  // The .lsdprj's kit was recorded as an lsdj-assets override (linking the file by path), and the ROM an override
  // folds onto carries it.
  expect(host.roleEdits.length).toBe(1);
  expect(host.roleEdits[0].kind).toBe("lsdj-assets");
  const ov = readOverrides(host.roleEdits[0].config);
  expect(ov.some((o) => o.type === "kit" && o.path === "/s/b.lsdprj" && o.lsdprjKit === 0)).toBeTruthy();
  expect(LsdjRom.fromBytes(applyOverridesToRom(lsdjRom1M(), ov as LsdjAssetOverride[], host.host())).kit(0).name()).toBe("DRUMS");
});

test("an empty or all-bad list writes nothing and reloads nothing", () => {
  const host = new FakeHost(lsdjRom1M(), savFrom({}));
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), [])).toBe(false);
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/nope.lsdsng"])).toBe(false); // unreadable
  expect(host.loads.length).toBe(0);
  expect(host.writes.length).toBe(0);
});

test("a .lsdprj whose base ROM cannot be read is skipped (nothing written, no reload, no clobber)", () => {
  const host = new FakeHost(lsdjRom1M(), savFrom({}));
  host.files.delete("/roms/song.gb"); // the ROM becomes unreadable: .lsdprj needs it to patch its kits
  host.files.set("/s/b.lsdprj", encodeLsdprj({ name: "BBB", version: 2, songBytes: rawSong(120), kitBanks: [] }));
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/s/b.lsdprj"])).toBe(false);
  expect(host.loads.length).toBe(0);
  expect(host.writes.length).toBe(0);
});

test("the valid song lands and a malformed one in the same batch is skipped, in one reload", () => {
  const host = new FakeHost(lsdjRom1M(), savFrom({}));
  const good = rawSong(90);
  host.files.set("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, good));
  host.files.set("/s/bad.lsdprj", new Uint8Array([9, 9, 9])); // malformed
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/s/good.lsdsng", "/s/bad.lsdprj"])).toBe(true);
  expect(host.loads.length).toBe(1);
  const sav = host.writes[0].bytes;
  expect(listProjects(sav).map((p) => p.name)).toEqual(["GOOD"]); // only the good song; the bad file corrupted nothing
  expect(sameBytes(decompressSlot(sav, 0)!, good)).toBe(true);
});

test("a failed atomic write aborts cleanly: false, no reload, nothing on disk", () => {
  const host = new FakeHost(lsdjRom1M(), savFrom({}));
  host.files.set("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, rawSong(90)));
  host.writeOk = false; // the disk write fails
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/s/good.lsdsng"])).toBe(false);
  expect(host.loads.length).toBe(0); // no cold-boot from a failed write
  expect(host.files.has("/roms/song.sav")).toBe(false); // nothing was written
  expect(host.roleEdits.length).toBe(0); // the lsdj-assets role was never touched
});

test("a system that went away between the target and the write imports nothing", () => {
  // The host answers null for a missing system, and the import must not write its battery to an empty path.
  const host = new FakeHost(lsdjRom1M(), savFrom({}));
  host.files.set("/s/good.lsdsng", encodeLsdsngRaw("GOOD", 1, rawSong(90)));
  host.savPathOf = () => null;
  expect(LSDJ_SONG_IMPORT.import(host.host(), target(), ["/s/good.lsdsng"])).toBe(false);
  expect(host.writes.length).toBe(0);
  expect(host.loads.length).toBe(0);
});
