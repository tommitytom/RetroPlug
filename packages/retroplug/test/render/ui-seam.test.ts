// The UI render seam (ui/lvgl/render.ts), pure-TS: startSystemRender snapshots the live system's state to a
// temp file and calls the __rp_startRender hook with the right spec; pickActiveRenderJob chooses the tile's
// badge job. The native render itself (RenderHost/RenderJobRegistry) is proven separately in C++.

import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { startSystemRender, pickActiveRenderJob, type RenderJobStatus } from "../../ui/lvgl/render";
import type { SystemView } from "../../src/systemsStore";

/** A minimal SystemView carrying only the fields startSystemRender reads. */
function sysView(over: Partial<SystemView>): SystemView {
  return { id: 1, romPath: "/roms/song.gb", battery: true, ...over } as unknown as SystemView;
}

interface Captured {
  systemId: number;
  spec: { rom: string; out: string; split: string; sav?: string; state?: string };
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

  const jobId = startSystemRender(backend, sysView({ id: 1, romPath: "/roms/song.gb", battery: true }), "mix", "/out/song.wav");

  expect(jobId).toBe(7);
  expect(backend.readSramCalls.includes(1)).toBeTruthy(); // read the LIVE core, not the on-disk .sav
  expect(backend.log.includes("writeFileAtomic")).toBeTruthy();
  const cap = spy.box.last!;
  expect(cap.systemId).toBe(1);
  expect(cap.spec.rom).toBe("/roms/song.gb");
  expect(cap.spec.out).toBe("/out/song.wav");
  expect(cap.spec.split).toBe("mix");
  expect(cap.spec.sav).toBe("/config/.render-src-sys1.sav"); // the temp snapshot, under configDir
  expect(cap.spec.state).toBe(undefined);
  spy.restore();
});

test("startSystemRender: channels split threads the split mode through", () => {
  const backend = new MockBackend("/config");
  backend.setSram(2, new Uint8Array([9]));
  const spy = spyStartRender();
  startSystemRender(backend, sysView({ id: 2, battery: true }), "channels", "/out/x");
  expect(spy.box.last!.spec.split).toBe("channels");
  spy.restore();
});

test("startSystemRender: no on-disk ROM → no-op (returns null, hook not called)", () => {
  const backend = new MockBackend("/config");
  const spy = spyStartRender();
  const jobId = startSystemRender(backend, sysView({ id: 1, romPath: "", battery: true }), "mix", "/out/x.wav");
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
  expect(startSystemRender(backend, sysView({ id: 1 }), "mix", "/out/x.wav")).toBe(null);
  g.__rp_startRender = prev;
});

test("pickActiveRenderJob: prefers the in-progress render, ignores other systems + terminal jobs", () => {
  const jobs: RenderJobStatus[] = [
    { id: 1, systemId: 9, state: "rendering", progress: 0.5, message: "" }, // other system
    { id: 2, systemId: 3, state: "done", progress: 1, message: "" }, // terminal
    { id: 3, systemId: 3, state: "rendering", progress: 0.2, message: "" }, // the one
  ];
  const picked = pickActiveRenderJob(jobs, 3);
  expect(picked != null && picked.id === 3).toBeTruthy();
});

test("pickActiveRenderJob: surfaces a lingering error when nothing is rendering", () => {
  const jobs: RenderJobStatus[] = [
    { id: 5, systemId: 3, state: "error", progress: 0.1, message: "boom" },
  ];
  const picked = pickActiveRenderJob(jobs, 3);
  expect(picked != null && picked.state === "error" && picked.message === "boom").toBeTruthy();
});

test("pickActiveRenderJob: nothing for a system with only finished jobs", () => {
  const jobs: RenderJobStatus[] = [
    { id: 6, systemId: 3, state: "done", progress: 1, message: "" },
    { id: 7, systemId: 3, state: "cancelled", progress: 0.3, message: "" },
  ];
  expect(pickActiveRenderJob(jobs, 3)).toBe(null);
});
