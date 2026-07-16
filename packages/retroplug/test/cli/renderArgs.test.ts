// Guards the `retroplug-cli render` flag parser (renderArgs.ts) — the pure front of the compiled-in
// render subcommand. Covers defaults, every value/boolean flag, the --split whitelist, and bad usage.
import { test, expect } from "../../testing/harness";
import { parseRenderArgs } from "../../cli/renderArgs";

test("render args: bare <rom> yields the documented defaults", () => {
  const o = parseRenderArgs(["song.gb"]);
  expect(o.rom).toBe("song.gb");
  expect(o.durationMs).toBe(undefined); // unset → auto (LSDj length) / 8000 default applied in render.ts
  expect(o.maxDurationMs).toBe(300000);
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
    "--duration", "1200ms", "--max-duration", "60s", "--split", "channels", "--bpm", "128", "--transport", "--no-start",
  ]);
  expect(o.rom).toBe("r.nes");
  expect(o.sav).toBe("s.sav");
  expect(o.state).toBe("s.ss0");
  expect(o.out).toBe("o.wav");
  expect(o.durationMs).toBe(1200);
  expect(o.maxDurationMs).toBe(60000);
  expect(o.split).toBe("channels");
  expect(o.bpm).toBe(128);
  expect(o.transport).toBe(true);
  expect(o.start).toBe(false);
});

test("render args: positional rom is order-independent among flags", () => {
  const o = parseRenderArgs(["--split", "pins", "rom.nes", "--duration", "500ms"]);
  expect(o.rom).toBe("rom.nes");
  expect(o.split).toBe("pins");
  expect(o.durationMs).toBe(500);
});

test("render args: --split rejects an unknown mode (incl. the removed 'mono')", () => {
  expect(() => parseRenderArgs(["r.gb", "--split", "stereo"])).toThrow("--split");
  expect(() => parseRenderArgs(["r.nes", "--split", "mono"])).toThrow("--split");
});

test("render args: --duration accepts ms / s / m units + a bare number (= ms)", () => {
  expect(parseRenderArgs(["r.gb", "--duration", "3000ms"]).durationMs).toBe(3000);
  expect(parseRenderArgs(["r.gb", "--duration", "3s"]).durationMs).toBe(3000);
  expect(parseRenderArgs(["r.gb", "--duration", "3m"]).durationMs).toBe(180000);
  expect(parseRenderArgs(["r.gb", "--duration", "1.5s"]).durationMs).toBe(1500);
  expect(parseRenderArgs(["r.gb", "--duration", "500"]).durationMs).toBe(500); // bare = ms
});

test("render args: --duration rejects zero / non-numeric / unknown unit", () => {
  expect(() => parseRenderArgs(["r.gb", "--duration", "0"])).toThrow("--duration");
  expect(() => parseRenderArgs(["r.gb", "--duration", "x"])).toThrow("--duration");
  expect(() => parseRenderArgs(["r.gb", "--duration", "3h"])).toThrow("--duration");
});

test("render args: --max-duration parses a unit value + rejects a bad one", () => {
  expect(parseRenderArgs(["r.gb", "--max-duration", "2m"]).maxDurationMs).toBe(120000);
  expect(() => parseRenderArgs(["r.gb", "--max-duration", "0s"])).toThrow("--max-duration");
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
