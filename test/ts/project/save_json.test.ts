// Path-only JSON project save (the plugin/standalone default "Save Project").
// Proves a saved `.rplg` is plain JSON that embeds no binaries, yet reloads a
// working project by re-reading the ROM from `romPath` and the cartridge battery
// RAM from the sibling `<rom>.sav`. Run via: pnpm test:cli project/save_json
//
// Counterpart: emu.saveRplg writes the self-contained zip bundle ("Export Zip").

import { test, expect, emu } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const ROM  = "/tmp/rp_save_json.gb";    // staged ROM (romPath the save will reference)
const SAV  = "/tmp/rp_save_json.sav";   // sibling battery RAM, read on reload
const PROJ = "/tmp/rp_save_json.rplg";  // path-only JSON save

function decodeAscii(bytes: Uint8Array): string {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

test("path-only JSON save reloads ROM from path + sibling .sav", () => {
  // Stage a ROM and a distinctive sibling .sav on disk.
  const rom = emu.readFile(LSDJ);
  emu.writeFile(ROM, rom.buffer);
  const sav = emu.savFromJson(JSON.stringify({
    workingSong: {
      formatVersion: 22,
      rows:    [{ chains: [0] }],
      chains:  [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse" }],
    },
  }));
  emu.writeFile(SAV, sav);

  // Boot fresh (no in-memory sav) and write the default JSON save.
  emu.loadRom(ROM);
  emu.runMs(1000);
  emu.saveProjectFile(PROJ);

  // The file is plain JSON: starts with '{' (no PK zip magic), references the
  // ROM path, and embeds no binary blobs (they serialize as `[]`).
  const text = decodeAscii(emu.readFile(PROJ));
  expect(text.charCodeAt(0)).toBe(0x7b); // '{'
  // Match by basename: on Windows the harness redirects /tmp to %TEMP%\retroplug,
  // so the stored romPath is that resolved absolute path, not the literal /tmp one.
  expect(text.indexOf(ROM.split("/").pop()!) >= 0).toBeTruthy();
  expect(text.indexOf('"romBytes":[]') >= 0).toBeTruthy();
  expect(text.indexOf('"sram":[]') >= 0).toBeTruthy();

  // Reload: ROM is re-read from romPath, SRAM from the sibling .sav.
  const reSys = emu.loadRplg(PROJ);
  expect(reSys).toBeGreaterThan(0);

  const battery = emu.saveSram(reSys);
  const expected = new Uint8Array(sav);
  expect(battery.length).toBe(expected.length);
  let diffs = 0;
  for (let i = 0; i < expected.length; i++) if (battery[i] !== expected[i]) diffs++;
  expect(diffs).toBe(0);

  // The reloaded system is real and runs (ROM came back from disk).
  const audio = emu.runMsPerSystem(200);
  expect(audio.length).toBe(1);
});
