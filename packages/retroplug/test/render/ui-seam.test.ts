// The UI render seam (ui/host/render.ts), pure-TS: startSystemRender snapshots the live system's state to files of
// the job's own and starts the render in the store graph's RenderJobs with the right spec; pickActiveRenderJob chooses
// the tile's badge job. The render itself (an engine job) is proven separately, in C++ and in test-native.

import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { startSystemRender, renderBaseName, pickActiveRenderJob, renderBadgeLabel, validSplits, formatDuration, type RenderJobStatus } from "../../ui/host/render";
import { outBase, type RenderOpts } from "../../src/render";
import { UserConfigStore } from "../../src/userConfigStore";
import { RENDER_MAX_DURATION_MAX_SEC, RENDER_MAX_DURATION_MIN_SEC } from "../../src/userConfig";
import { savFrom, type SavInput } from "@retroplug/lsdj";
import type { SystemView } from "../../src/systemsStore";
import { RenderJobs } from "../../src/renderJobs";
import { jobHandle, OFFLINE_MAX_JOBS, OfflineJobs, type EngineRunResult, type OfflinePort } from "../../src/offline";
import { liveJobCount } from "../../src/capability";

/** A minimal SystemView carrying only the fields startSystemRender reads. */
function sysView(over: Partial<SystemView>): SystemView {
  // `roles` is not optional on a real SystemView, and startSystemRender now reads it (to spot a cart whose
  // working song lives outside the battery) — the cast would otherwise hand it undefined.
  return { id: 1, romPath: "/roms/song.gb", battery: true, roles: [], settings: { gainDb: 0 }, ...over } as unknown as SystemView;
}

interface Captured {
  systemId: number;
  spec: {
    rom: string; out: string; split: string; sav?: string; state?: string; sampleRate?: number; maxDurationMs?: number;
    roles?: { kind: string; config: Record<string, unknown> }[];
    gainDb?: number;
    inputsMustExist?: boolean;
  };
}

/** A RenderJobs stand-in recording each start; `box.last` holds the most recent capture. */
function spyStartRender(returnId = 7): { box: { last: Captured | null }; renders: Pick<RenderJobs, "start"> } {
  const box: { last: Captured | null } = { last: null };
  return {
    box,
    renders: {
      start: (systemId, spec) => {
        const body = typeof spec === "function" ? spec("job-key") : spec;
        box.last = { systemId, spec: JSON.parse(JSON.stringify(body)) };
        return returnId;
      },
    },
  };
}

test("startSystemRender: battery cart → snapshots live SRAM to a temp file and calls the hook with a mix spec", () => {
  const backend = new MockBackend("/config");
  backend.setSram(1, new Uint8Array([1, 2, 3, 4]));
  const spy = spyStartRender(7);

  const jobId = startSystemRender(
    backend,
    spy.renders,
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
  expect(cap.spec.sav).toBe("/config/.render-src-job-key.sav"); // the temp snapshot, under configDir, named by the job
  expect(cap.spec.state).toBe(undefined);
  // The spec's own inputs are these snapshots, so it says they must be there when the render boots
  // (RenderOpts.inputsMustExist): a snapshot the startup sweep took from under a queued live job then refuses the
  // render instead of booting the ROM blank (render-classify.test.ts).
  expect(cap.spec.inputsMustExist).toBe(true);
});

test("startSystemRender: channels split threads the split mode through", () => {
  const backend = new MockBackend("/config");
  backend.setSram(2, new Uint8Array([9]));
  const spy = spyStartRender();
  startSystemRender(backend, spy.renders, sysView({ id: 2, battery: true }), { split: "channels" }, "/out/x");
  expect(spy.box.last!.spec.split).toBe("channels");
  expect(spy.box.last!.spec.sampleRate).toBe(undefined); // omitted → worker uses the engine default
});

test("startSystemRender: the instance's configuration rides along, so the offline core is not built at defaults", () => {
  // A render boots a FRESH core from the ROM on disk. Its roles decide region, model, the cartridge sound
  // chip's level, which DMC kit is in the bank - all of which would otherwise come back as schema defaults
  // and render something the user is not hearing.
  const backend = new MockBackend("/config");
  backend.setSram(3, new Uint8Array([1]));
  const spy = spyStartRender();
  const roles = [
    { kind: "mesen", config: { region: "pal", expansionVolume: 50 } },
    { kind: "bliptoaster-assets", config: { overrides: [{ type: "kit", slot: 1, path: "/kits/mine.rkit" }] } },
  ];
  startSystemRender(backend, spy.renders, sysView({ id: 3, romPath: "/roms/a.nes", battery: true, roles, settings: { gainDb: -6, reloadOnRomChange: false } }), { split: "mix" }, "/out/x.wav");

  expect(spy.box.last!.spec.roles).toEqual(roles); // verbatim, configs and all
  expect(spy.box.last!.spec.gainDb).toBe(-6);
});

test("startSystemRender: a default-configured instance sends no roles/gain, so a render stays a plain boot", () => {
  // Thin spec, like every other field here: nothing pinned means nothing to say, and the worker's own
  // defaults (which are the same schema defaults) apply.
  const backend = new MockBackend("/config");
  backend.setSram(1, new Uint8Array([1]));
  const spy = spyStartRender();
  startSystemRender(backend, spy.renders, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/x.wav");
  expect(spy.box.last!.spec.roles).toBe(undefined);
  expect(spy.box.last!.spec.gainDb).toBe(undefined);
});

test("startSystemRender: no on-disk ROM → no-op (returns null, nothing started)", () => {
  const backend = new MockBackend("/config");
  const spy = spyStartRender();
  const jobId = startSystemRender(backend, spy.renders, sysView({ id: 1, romPath: "", battery: true }), { split: "mix" }, "/out/x.wav");
  expect(jobId).toBe(null);
  expect(spy.box.last).toBe(null);
  expect(backend.readSramCalls.length).toBe(0);
});

/** A RenderJobs over a backend whose engineRun holds every job until the test settles it, as an engine queues a render
 *  behind the worker budget: `specs` lists what each submission carried, `discards` the files each one handed the
 *  engine's pool (which deletes them), `settle(i)` ends job i. */
function queuedRenders(): {
  renders: RenderJobs;
  specs: Captured["spec"][];
  discards: (string[] | undefined)[];
  settle(i: number): void;
} {
  const specs: Captured["spec"][] = [];
  const discards: (string[] | undefined)[] = [];
  const resolvers: ((r: EngineRunResult) => void)[] = [];
  const renders = new RenderJobs({
    engineRun: (_kind, spec, options) => {
      specs.push(spec as Captured["spec"]);
      discards.push(options?.discardFiles);
      return jobHandle(new Promise<EngineRunResult>((res) => resolvers.push(res)), () => true);
    },
  });
  const settle = (i: number) => resolvers[i]({ status: "done", message: "", outputs: [], renderedMs: 1 });
  return { renders, specs, discards, settle };
}

const later = (): Promise<void> => new Promise((r) => void Promise.resolve().then(() => r()));

test("startSystemRender: two queued renders of one system boot their own snapshots, gone once settled", async () => {
  // A render reads its snapshot when a worker boots it, which can be minutes after the submission when renders queue
  // behind the engine's budget. A render, an edit and a second render must leave the first booting the state it was
  // started on: each snapshot is a file of its own, named by the job's key, and each job's own list of them rides its
  // submission (EngineRunOptions.discardFiles), which the engine's pool deletes however the job ends. This side deletes
  // them too, as the job settles: the second road, which is what a job that never reached the pool has (the C++
  // [engine-jobs] cases hold the pool's order, removal before the completion is queued).
  const backend = new MockBackend("/config");
  const q = queuedRenders();
  const sys = sysView({ id: 1, battery: true });
  backend.setSram(1, new Uint8Array([1, 1, 1]));
  startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/a.wav");
  backend.setSram(1, new Uint8Array([2, 2, 2])); // an edit while the first render is queued
  startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/b.wav");

  const [first, second] = q.specs;
  expect(first.sav !== undefined && second.sav !== undefined && first.sav !== second.sav).toBeTruthy();
  expect(Array.from(backend.readFile(first.sav!) ?? [])).toEqual([1, 1, 1]);
  expect(Array.from(backend.readFile(second.sav!) ?? [])).toEqual([2, 2, 2]);
  expect(q.discards).toEqual([[first.sav], [second.sav]]); // each render's own, and only its own

  q.settle(0);
  await later();
  expect(backend.fileExists(first.sav!)).toBe(false); // deleted by this side as the job settled
  expect(backend.fileExists(second.sav!)).toBe(true); // still queued, so still its job's
  q.settle(1);
  await later();
  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]);
});

test("startSystemRender: a cart whose song is outside the battery snapshots its state too, of its own", async () => {
  class Live extends MockBackend {
    override readState(): Uint8Array | null {
      return new Uint8Array([9]);
    }
  }
  const backend = new Live("/config");
  const q = queuedRenders();
  const roles = [{ kind: "sms-sync", config: {} }];
  const sys = sysView({ id: 4, romPath: "/roms/smsggdj.sms", battery: true, roles });
  backend.setSram(4, new Uint8Array([7]));
  startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/a.wav");
  startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/b.wav");
  const [first, second] = q.specs;
  expect(first.state !== undefined && second.state !== undefined && first.state !== second.state).toBeTruthy();
  expect(first.sav !== second.sav).toBeTruthy();
  // Both files of each job are its submission's: the battery and the savestate that captures the working song.
  expect(q.discards).toEqual([[first.sav, first.state], [second.sav, second.state]]);
  q.settle(0);
  q.settle(1);
  await later();
  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]);
});

test("startSystemRender: a snapshot that cannot be written refuses the render, and the one it did write goes",
  async () => {
    // The savestate's write fails after the battery's succeeded: the spec function throws, so nothing is submitted and
    // no list reaches the pool, and the file it did write is this side's to delete (which `onSettled` does, as it does
    // for every road). The render is still listed as an error rather than booting the on-disk .sav.
    class HalfDisk extends MockBackend {
      override writeFileAtomic(path: string, bytes: Uint8Array): boolean {
        return path.endsWith(".ss0") ? false : super.writeFileAtomic(path, bytes);
      }
      override readState(): Uint8Array | null {
        return new Uint8Array([9]);
      }
    }
    const backend = new HalfDisk("/config");
    backend.setSram(4, new Uint8Array([1]));
    const q = queuedRenders();
    const sys = sysView({
      id: 4,
      romPath: "/roms/smsggdj.sms",
      battery: true,
      roles: [{ kind: "sms-sync", config: {} }],
    });
    const id = startSystemRender(backend, q.renders, sys, { split: "mix" }, "/out/a.wav");
    expect(q.specs.length).toBe(0); // nothing submitted
    expect(q.discards.length).toBe(0); // and so nothing handed over
    const job = q.renders.list().find((j) => j.id === id);
    expect(job?.state).toBe("error");
    expect(job!.message.includes("could not snapshot")).toBeTruthy();
    expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]); // the sav it wrote went with it
  });

/** A port whose side of the queue completes nothing by itself: every submission is accepted, `finish()` ends them all
 *  at the next drain, and a job past the queue's bound never reaches it at all. */
function heldPort(): { port: OfflinePort; submitted: string[]; finish(): void } {
  const done = new Set<number>();
  const ids: number[] = [];
  const submitted: string[] = [];
  let next = 1;
  const accept = (what: string): { id: number; error: null } => {
    submitted.push(what);
    ids.push(next);
    return { id: next++, error: null };
  };
  const port: OfflinePort = {
    submitOfflineJob: (request) => accept(request.document ?? ""),
    submitEngineJob: (request) => accept(request.spec),
    cancelOfflineJob: (id) => (done.add(id), true),
    drainOfflineJobs: () => ({
      completions: [],
      inFlight: ids.filter((id) => !done.has(id)).length,
      engineCompletions: ids
        .filter((id) => done.has(id))
        .map((id) => ({ id, status: "done", message: "", outputs: [], renderedMs: 0, lines: [], droppedLines: 0 })),
      engineProgress: [],
    }),
  };
  return { port, submitted, finish: () => void ids.forEach((id) => done.add(id)) };
}

test("startSystemRender: a render the queue holds past the engine's bound deletes its snapshots when it settles",
  async () => {
    // Past OFFLINE_MAX_JOBS the queue keeps a submission in its own list, so it never reaches native and no JobFiles
    // ever holds its files. Cancelling it there settles the job here, and the settle is what deletes them: without this
    // road a snapshot nothing of the pool's knows about waits a day for the startup sweep.
    const backend = new MockBackend("/config");
    const held = heldPort();
    const jobs = new OfflineJobs(held.port);
    const renders = new RenderJobs({
      engineRun: (kind, spec, options) => jobs.engine(kind, spec, options),
    });
    backend.setSram(1, new Uint8Array([1, 2, 3]));
    const fill = Array.from({ length: OFFLINE_MAX_JOBS }, (_, k) =>
      jobs.engine("render", { rom: "/roms/x.gb", out: `/out/f${k}.wav` }),
    );
    expect(jobs.pendingCount()).toBe(OFFLINE_MAX_JOBS);
    const id = startSystemRender(backend, renders, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/a.wav");
    expect(id !== null).toBe(true);
    const snaps = backend.paths().filter((p) => p.startsWith("/config/"));
    expect(snaps.length).toBe(1); // written, and held: nothing of the pool's has the list
    expect(held.submitted.length).toBe(OFFLINE_MAX_JOBS); // the render itself never reached the port
    expect(renders.cancel(id!)).toBe(true);
    await later();
    expect(renders.list().find((j) => j.id === id)?.state).toBe("cancelled");
    expect(backend.fileExists(snaps[0])).toBe(false);
    renders.dismiss(id!);
    // The jobs it waited behind end at the next drain, so the shared queue goes quiet again.
    held.finish();
    jobs.pump();
    await Promise.all(fill);
    expect(jobs.pendingCount()).toBe(0);
    expect(liveJobCount()).toBe(0);
  });

test("startSystemRender: a submission the RPC throws on deletes the snapshots it wrote", async () => {
  // The request never reached native, so no JobFiles ever held these files either: the job fails here, and the settle
  // is again what deletes them. A link that broke is not a reason to leave a file in a directory every instance shares.
  const backend = new MockBackend("/config");
  const jobs = new OfflineJobs({
    submitOfflineJob: () => ({ id: null, error: "this case submits no graph job" }),
    submitEngineJob: () => {
      throw new Error("the RPC link went");
    },
    cancelOfflineJob: () => false,
    drainOfflineJobs: () => ({ completions: [], inFlight: 0 }),
  });
  const renders = new RenderJobs({ engineRun: (kind, spec, options) => jobs.engine(kind, spec, options) });
  backend.setSram(1, new Uint8Array([4, 5]));
  const id = startSystemRender(backend, renders, sysView({ id: 1, battery: true }), { split: "mix" }, "/out/a.wav");
  expect(backend.paths().filter((p) => p.startsWith("/config/")).length).toBe(1); // written before the submission
  await later();
  const job = renders.list().find((j) => j.id === id);
  expect(job?.state).toBe("error");
  expect(job!.message.includes("the RPC link went")).toBeTruthy();
  expect(backend.paths().filter((p) => p.startsWith("/config/"))).toEqual([]);
  renders.dismiss(id!);
  expect(liveJobCount()).toBe(0);
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
  // The console descriptor's own declaration decides: the Game Boy channels, the NES channels and pins, the GBA
  // neither, whatever the core.
  expect(validSplits({ platform: "gb" })).toEqual(["mix", "channels"]); // GB: no pins
  expect(validSplits({ platform: "nes" })).toEqual(["mix", "channels", "pins"]); // NES: all
  expect(validSplits({ platform: "gba" })).toEqual(["mix"]); // GBA: mix only
  expect(validSplits({ platform: "sms" })).toEqual(["mix"]); // SMS: mix only
  expect(validSplits({ platform: "gg" })).toEqual(["mix"]); // GG: mix only
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
