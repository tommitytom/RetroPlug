// Guards the `retroplug-cli render` flag parser (renderArgs.ts) — the pure front of the compiled-in
// render subcommand. Covers defaults, every value/boolean flag, the --split whitelist, and bad usage.
import { test, expect } from "../../testing/harness";
import { parseRenderArgs } from "../../cli/renderArgs";

test("render args: bare <rom> yields the documented defaults", () => {
  const o = parseRenderArgs(["song.gb"]);
  expect(o.rom).toBe("song.gb");
  expect(o.ms).toBe(undefined); // unset → auto (LSDj length) / 8000 default applied in render.ts
  expect(o.maxMs).toBe(300000);
  expect(o.split).toBe("mix");
  expect(o.start).toBe(true); // auto-start playback by default
  expect(o.transport).toBe(false);
  expect(o.sav).toBe(undefined); // sibling <rom>.sav is resolved natively, not here
  expect(o.out).toBe(undefined);
  expect(o.song).toBe(undefined);
  expect(o.songIndex).toBe(undefined);
  expect(o.listSongs).toBe(false);
});

test("render args: LSDj song selection flags parse", () => {
  const byName = parseRenderArgs(["lsdj.gb", "--sav", "s.sav", "--song", "HAPPYBD"]);
  expect(byName.song).toBe("HAPPYBD");
  expect(byName.songIndex).toBe(undefined);

  const byIndex = parseRenderArgs(["lsdj.gb", "--sav", "s.sav", "--song-index", "0"]);
  expect(byIndex.songIndex).toBe(0); // slot 0 is valid (intValue would reject it)

  const list = parseRenderArgs(["lsdj.gb", "--sav", "s.sav", "--list-songs"]);
  expect(list.listSongs).toBe(true);
});

test("render args: --song and --song-index are mutually exclusive", () => {
  expect(() => parseRenderArgs(["r.gb", "--song", "A", "--song-index", "1"])).toThrow("mutually exclusive");
});

test("render args: --song-index rejects out-of-range / non-integer", () => {
  expect(() => parseRenderArgs(["r.gb", "--song-index", "32"])).toThrow("--song-index");
  expect(() => parseRenderArgs(["r.gb", "--song-index", "-1"])).toThrow("--song-index");
  expect(() => parseRenderArgs(["r.gb", "--song-index", "x"])).toThrow("--song-index");
});

test("render args: every value + boolean flag parses", () => {
  const o = parseRenderArgs([
    "r.nes", "--sav", "s.sav", "--state", "s.ss0", "--out", "o.wav",
    "--ms", "1200", "--max-ms", "60000", "--split", "mono", "--bpm", "128", "--transport", "--no-start",
  ]);
  expect(o.rom).toBe("r.nes");
  expect(o.sav).toBe("s.sav");
  expect(o.state).toBe("s.ss0");
  expect(o.out).toBe("o.wav");
  expect(o.ms).toBe(1200);
  expect(o.maxMs).toBe(60000);
  expect(o.split).toBe("mono");
  expect(o.bpm).toBe(128);
  expect(o.transport).toBe(true);
  expect(o.start).toBe(false);
});

test("render args: positional rom is order-independent among flags", () => {
  const o = parseRenderArgs(["--split", "pins", "rom.nes", "--ms", "500"]);
  expect(o.rom).toBe("rom.nes");
  expect(o.split).toBe("pins");
  expect(o.ms).toBe(500);
});

test("render args: --split rejects an unknown mode", () => {
  expect(() => parseRenderArgs(["r.gb", "--split", "stereo"])).toThrow("--split");
});

test("render args: --ms rejects a non-positive/non-integer value", () => {
  expect(() => parseRenderArgs(["r.gb", "--ms", "0"])).toThrow("--ms");
  expect(() => parseRenderArgs(["r.gb", "--ms", "1.5"])).toThrow("--ms");
  expect(() => parseRenderArgs(["r.gb", "--ms", "x"])).toThrow("--ms");
});

test("render args: --max-ms parses + rejects a bad value", () => {
  expect(parseRenderArgs(["r.gb", "--max-ms", "12000"]).maxMs).toBe(12000);
  expect(() => parseRenderArgs(["r.gb", "--max-ms", "0"])).toThrow("--max-ms");
});

test("render args: a missing rom throws usage", () => {
  expect(() => parseRenderArgs(["--split", "mix"])).toThrow("missing <rom>");
});

test("render args: an unknown flag throws", () => {
  expect(() => parseRenderArgs(["r.gb", "--loud"])).toThrow("unknown flag");
});

test("render args: a value flag with no value throws", () => {
  expect(() => parseRenderArgs(["r.gb", "--sav"])).toThrow("--sav");
});
