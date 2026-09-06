// The UI render seam (ui/lvgl/render.ts), pure-TS: startSystemRender snapshots the live system's state to a
// temp file and calls the __rp_startRender hook with the right spec; pickActiveRenderJob chooses the tile's
// badge job. The native render itself (RenderHost/RenderJobRegistry) is proven separately in C++.

import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { startSystemRender, renderBaseName, pickActiveRenderJob, renderBadgeLabel, validSplits, formatDuration, type RenderJobStatus } from "../../ui/lvgl/render";
import { outBase, type RenderOpts } from "../../src/render";
import { UserConfigStore } from "../../src/userConfigStore";
import { RENDER_MAX_DURATION_MAX_SEC, RENDER_MAX_DURATION_MIN_SEC } from "../../src/userConfig";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import type { SystemView } from "../../src/systemsStore";

/** A minimal SystemView carrying only the fields startSystemRender reads. */
function sysView(over: Partial<SystemView>): SystemView {
  // `roles` is not optional on a real SystemView, and startSystemRender now reads it (to spot a cart whose
  // working song lives outside the battery) — the cast would otherwise hand it undefined.
  return { id: 1, romPath: "/roms/song.gb", battery: true, roles: [], ...over } as unknown as SystemView;
}

interface Captured {
  systemId: number;
  spec: { rom: string; out: string; split: string; sav?: string; state?: string; sampleRate?: number; maxDurationMs?: number };
}

/** Install a __rp_startRender spy; `box.last` holds the most recent capture. */
function spyStartRender(returnId = 7): { box: { last: Captured | null }; restore: () => void } {
  const g = globalThis as { __rp_startRender?: (id: number, spec: string) => number };
  const prev = g.__rp_startRender;
  const box: { last: Captured | null } = { last: null };
  g.__rp_startRender = (systemId, spec) => {
    box.last = { systemId, spec: JSON.parse(spec) };
    return returnId;
  };
  return { box, restore: () => { g.__rp_startRender = prev; } };
}

test("startSystemRender: battery cart → snapshots live SRAM to a temp file and calls the hook with a mix spec", () => {
  const backend = new MockBackend("/config");
  backend.setSram(1, new Uint8Array([1, 2, 3, 4]));
  const spy = spyStartRender(7);

  const jobId = startSystemRender(
    backend,
    sysView({ id: 1, romPath: "/roms/song.gb", battery: true }),
    { split: "mix", sampleRate: 48000, maxDurationMs: 90000 },
    "/out/song.wav",
  );

  expect(jobId).toBe(7);
  expect(backend.readSramCalls.includes(1)).toBeTruthy(); // read the LIVE core, not the on-disk .sav
  expect(backend.log.includes("writeFileAtomic")).toBeTruthy();
  const cap = spy.box.last!;
  expect(cap.systemId).toBe(1);
  expect(cap.spec.rom).toBe("/roms/song.gb");
  expect(cap.spec.out).toBe("/out/song.wav");
  expect(cap.spec.split).toBe("mix");
  expect(cap.spec.sampleRate).toBe(48000); // carried from the render request
  expect(cap.spec.maxDurationMs).toBe(90000);
  expect(cap.spec.sav).toBe("/config/.render-src-sys1.sav"); // the temp snapshot, under configDir
  expect(cap.spec.state).toBe(undefined);
  spy.restore();
});

test("startSystemRender: channels split threads the split mode through", () => {
  const backend = new MockBackend("/config");
  backend.setSram(2, new Uint8Array([9]));
  const spy = spyStartRender();
  startSystemRender(backend, sysView({ id: 2, battery: true }), { split: "channels" }, "/out/x");
  expect(spy.box.last!.spec.split).toBe("channels");
  expect(spy.box.last!.spec.sampleRate).toBe(undefined); // omitted → worker uses the engine default
  spy.restore();
});

test("startSystemRender: no on-disk ROM → no-op (returns null, hook not called)", () => {
  const backend = new MockBackend("/config");
  const spy = spyStartRender();
  const jobId = startSystemRender(backend, sysView({ id: 1, romPath: "", battery: true }), { split: "mix" }, "/out/x.wav");
  expect(jobId).toBe(null);
  expect(spy.box.last).toBe(null);
  expect(backend.readSramCalls.length).toBe(0);
  spy.restore();
});

test("startSystemRender: no hook bound (headless) → inert null", () => {
  const g = globalThis as { __rp_startRender?: unknown };
  const prev = g.__rp_startRender;
  delete g.__rp_startRender;
  const backend = new MockBackend("/config");
  backend.setSram(1, new Uint8Array([1]));
  expect(startSystemRender(backend, sysView({ id: 1 }), { split: "mix" }, "/out/x.wav")).toBe(null);
  g.__rp_startRender = prev;
});

test("renderBaseName: song name → SAV basename → ROM stem, in priority order", () => {
  const backend = new MockBackend("/config");
  // An LSDj sav whose active (working) project is named HELLO (same fixture shape as tracker/songCatalog).
  const song = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  const sav = savFrom({ activeProjectIndex: 0, projects: [{ name: "HELLO", version: 0, song }] } as SavInput);
  backend.setSram(1, sav);

  // lsdj-sync marker role + battery bytes → resolveSongCatalog → the working song name.
  expect(renderBaseName(backend, sysView({ id: 1, romPath: "/roms/lsdj.gb", roles: [{ kind: "lsdj-sync", config: {} }] }))).toBe("HELLO");
  // A non-tracker ROM whose sibling .sav name matches the ROM → the ROM stem.
  expect(renderBaseName(backend, sysView({ id: 2, romPath: "/roms/mGB.gb", roles: [] }))).toBe("mGB");
  // A tracker role but no battery bytes (readSram null) → SAV/ROM stem (sibling sav → same as ROM here).
  expect(renderBaseName(backend, sysView({ id: 3, romPath: "/roms/lsdj.gb", roles: [{ kind: "lsdj-sync", config: {} }] }))).toBe("lsdj");
  // A non-tracker cart with a CUSTOM loaded sav → the sav's basename (differs from the ROM stem).
  expect(renderBaseName(backend, sysView({ id: 4, romPath: "/roms/game.gb", roles: [], savPath: "/saves/MYSAVE.sav" }))).toBe("MYSAVE");
  // A battery-less cart → the ROM stem (no sav to name it after).
  expect(renderBaseName(backend, sysView({ id: 5, romPath: "/roms/demo.gb", roles: [], battery: false }))).toBe("demo");
});

test("outBase: song name without --out (sanitized), ROM-derived when null, --out always wins", () => {
  const opts = (over: Partial<RenderOpts>): RenderOpts => ({ rom: "/roms/song.gb", split: "mix", ...over } as unknown as RenderOpts);
  // No --out → the sanitized song name next to the ROM (spaces become _).
  expect(outBase(opts({}), "COOL SONG")).toBe("/roms/COOL_SONG.wav");
  // No --out, no song (non-tracker) → the old ROM-derived default, byte-identical.
  expect(outBase(opts({}), null)).toBe("/roms/song.wav");
  // Split, no --out → a prefix (per-channel files append _<channel>.wav downstream).
  expect(outBase(opts({ split: "channels" }), "COOL")).toBe("/roms/COOL");
  expect(outBase(opts({ split: "channels" }), null)).toBe("/roms/song");
  // --out always wins: mix keeps it verbatim; split trims a trailing .wav to a prefix.
  expect(outBase(opts({ out: "/x/o.wav" }), "COOL")).toBe("/x/o.wav");
  expect(outBase(opts({ split: "channels", out: "/x/o.wav" }), "COOL")).toBe("/x/o");
});

test("pickActiveRenderJob: prefers the in-progress render, ignores other systems + terminal jobs", () => {
  const jobs: RenderJobStatus[] = [
    { id: 1, systemId: 9, state: "rendering", renderedMs: 5000, message: "" }, // other system
    { id: 2, systemId: 3, state: "done", renderedMs: 12000, message: "" }, // terminal
    { id: 3, systemId: 3, state: "rendering", renderedMs: 2000, message: "" }, // the one
  ];
  const picked = pickActiveRenderJob(jobs, 3);
  expect(picked != null && picked.id === 3).toBeTruthy();
});

test("pickActiveRenderJob: surfaces a lingering error when nothing is rendering", () => {
  const jobs: RenderJobStatus[] = [
    { id: 5, systemId: 3, state: "error", renderedMs: 1000, message: "boom" },
  ];
  const picked = pickActiveRenderJob(jobs, 3);
  expect(picked != null && picked.state === "error" && picked.message === "boom").toBeTruthy();
});

test("pickActiveRenderJob: nothing for a system with only finished jobs", () => {
  const jobs: RenderJobStatus[] = [
    { id: 6, systemId: 3, state: "done", renderedMs: 12000, message: "" },
    { id: 7, systemId: 3, state: "cancelled", renderedMs: 3000, message: "" },
  ];
  expect(pickActiveRenderJob(jobs, 3)).toBe(null);
});

test("renderBadgeLabel: the elapsed rendered duration while rendering, the failure otherwise", () => {
  const job = (over: Partial<RenderJobStatus>): RenderJobStatus =>
    ({ id: 1, systemId: 3, state: "rendering", renderedMs: 0, message: "", ...over });
  // The badge counts the audio rendered so far - never a percentage of the max-duration cap, which for a
  // tracker render is only a safety bound and says nothing about how far along the song is.
  expect(renderBadgeLabel(job({ renderedMs: 0 }))).toBe(" rendering 0:00 (tap to cancel)");
  expect(renderBadgeLabel(job({ renderedMs: 42_000 }))).toBe(" rendering 0:42 (tap to cancel)");
  expect(renderBadgeLabel(job({ renderedMs: 65_400 }))).toBe(" rendering 1:05 (tap to cancel)"); // sub-second truncates
  expect(renderBadgeLabel(job({ state: "error", message: "boom" }))).toBe(" render failed (tap to dismiss)");
});

test("validSplits: gates channels/pins on platform", () => {
  expect(validSplits({ core: "sameboy", platform: "gb" })).toEqual(["mix", "channels"]); // GB: no pins
  expect(validSplits({ core: "mesen", platform: "nes" })).toEqual(["mix", "channels", "pins"]); // NES: all
  expect(validSplits({ core: "mesen", platform: "gba" })).toEqual(["mix"]); // GBA: mix only
});

test("formatDuration: seconds → M:SS", () => {
  expect(formatDuration(300)).toBe("5:00");
  expect(formatDuration(65)).toBe("1:05");
  expect(formatDuration(5)).toBe("0:05");
});

test("userConfig render setters: validate + clamp, persist through the store", () => {
  const backend = new MockBackend("/config");
  const store = new UserConfigStore(backend);
  store.load();

  expect(store.render().split).toBe("mix"); // default
  expect(store.setRenderSplit("channels")).toBe(true);
  expect(store.render().split).toBe("channels");
  expect(store.setRenderSplit("bogus" as never)).toBe(false); // unknown mode rejected

  expect(store.setRenderSampleRate(48000)).toBe(true);
  expect(store.render().sampleRate).toBe(48000);
  expect(store.setRenderSampleRate(192000)).toBe(true); // the high end the device picker also offers
  expect(store.render().sampleRate).toBe(192000);
  expect(store.setRenderSampleRate(48000)).toBe(true); // back, so the persistence check below is unchanged
  expect(store.setRenderSampleRate(22050)).toBe(false); // below the render floor: still not offered

  store.setRenderMaxDurationSec(90);
  expect(store.render().maxDurationSec).toBe(90);
  store.setRenderMaxDurationSec(999999); // over the cap → clamped
  expect(store.render().maxDurationSec).toBe(RENDER_MAX_DURATION_MAX_SEC);
  store.setRenderMaxDurationSec(0); // under the floor → clamped
  expect(store.render().maxDurationSec).toBe(RENDER_MAX_DURATION_MIN_SEC);

  expect(store.render().outputDir).toBe(""); // default: no chosen render folder yet
  expect(store.setRenderOutputDir("/music/out")).toBe(true);
  expect(store.render().outputDir).toBe("/music/out");
  expect(store.setRenderOutputDir("/music/out")).toBe(false); // unchanged → no-op

  expect(store.render().onExists).toBe("overwrite"); // default policy
  expect(store.setRenderOnExists("rename")).toBe(true);
  expect(store.render().onExists).toBe("rename");
  expect(store.setRenderOnExists("bogus" as never)).toBe(false); // unknown mode rejected

  // Persisted: a fresh store over the same backend reads the saved render block back.
  const reloaded = new UserConfigStore(backend);
  reloaded.load();
  expect(reloaded.render().split).toBe("channels");
  expect(reloaded.render().sampleRate).toBe(48000);
  expect(reloaded.render().outputDir).toBe("/music/out"); // round-trips through config.json
  expect(reloaded.render().onExists).toBe("rename");
});

test("userConfig render filename override: session-only, per-system, fires onChange, not persisted", () => {
  const backend = new MockBackend("/config");
  let changes = 0;
  const store = new UserConfigStore(backend, () => changes++);
  store.load();

  expect(store.renderFilename(1)).toBe(undefined); // no override → caller re-derives
  store.setRenderFilename(1, "MY_TAKE");
  expect(store.renderFilename(1)).toBe("MY_TAKE");
  expect(store.renderFilename(2)).toBe(undefined); // per-system
  expect(changes > 0).toBeTruthy(); // repaints the menu

  // NOT persisted: a fresh store over the same backend does not see it (config.json never carried it).
  const reloaded = new UserConfigStore(backend);
  reloaded.load();
  expect(reloaded.renderFilename(1)).toBe(undefined);
});

test("userConfig render Output-Dir override: session-only, per-system, fires onChange, never the config default", () => {
  const backend = new MockBackend("/config");
  let changes = 0;
  const store = new UserConfigStore(backend, () => changes++);
  store.load();

  expect(store.renderDir(1)).toBe(undefined); // no override → the menu falls back to the Settings default / sav dir
  store.setRenderDir(1, "/tmp/take-a");
  expect(store.renderDir(1)).toBe("/tmp/take-a");
  expect(store.renderDir(2)).toBe(undefined); // per-system
  expect(changes > 0).toBeTruthy(); // repaints the menu
  expect(store.render().outputDir).toBe(""); // the per-session Output Dir NEVER writes the Settings default

  // NOT persisted, and independent of the Settings default: reload sees neither the override nor a default.
  const reloaded = new UserConfigStore(backend);
  reloaded.load();
  expect(reloaded.renderDir(1)).toBe(undefined);
  expect(reloaded.render().outputDir).toBe("");
});
