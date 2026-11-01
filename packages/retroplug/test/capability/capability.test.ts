// Capability jobs as promises (src/capability.ts, docs/modular-graph-phase-c5-design.md section 6), over the mock
// backend's model of the native job queue: a read resolves at its submission, a write while the audio thread runs once
// the block boundary has passed and the tick pumps, a refusal rejects at once with native's reason, a failed job
// rejects with its error, a long job's progress reaches its listener, and closing the project rejects what is still
// pending. The native half (the queue, the threading, the agreement with readRam / readSram) is held by
// retroplug-engine-graph-test "[capability]" and test-native/capability.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { ProjectStore } from "../../src/projectStore";
import { RecentStore } from "../../src/recentStore";
import { buildAppRegistry } from "../../src/appHost";
import { CapabilityJobs, pumpAllJobs, setJobWait, type JobCompletion, type JobRequest, type JobSubmitResult } from "../../src/capability";
import { gbRom } from "../systems/fixtures";

// The message `promise` rejects with, held to contain `text`; a promise that resolves fails the case.
async function rejectsWith(promise: Promise<unknown>, text: string): Promise<void> {
  let message: string | null = null;
  try {
    await promise;
  } catch (e) {
    message = (e as Error).message;
  }
  if (message === null) throw new Error(`expected a rejection containing "${text}", got a resolution`);
  expect(message.includes(text) ? text : message).toBe(text);
}

function cart() {
  const be = new MockBackend("/cfg");
  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const rom = gbRom();
  rom.set([0x52, 0x50, 0x54, 0x45, 0x53, 0x54], 0x134); // "RPTEST"
  be.seed("/roms/cart.gb", rom);
  const id = project.systems.addSystem("/roms/cart.gb")!;
  be.setRam(id, new Uint8Array(64).map((_, i) => i));
  return { be, project, id, node: `sys-${id}`, rom };
}

test("a read resolves at its submission, with what the system-id read returns", async () => {
  const { be, node, id, rom } = cart();
  expect(await be.capability(node, "memory.read", { region: "ram" })).toEqual(be.readRam(id)!);
  expect(await be.capability(node, "memory.read", { region: "ram", offset: 4, length: 3 })).toEqual(new Uint8Array([4, 5, 6]));
  expect(await be.capability(node, "memory.regions")).toEqual([{ name: "ram", size: 64 }]);
  expect(await be.capability(node, "sram.dump")).toEqual(be.readSram(id)!);
  expect(await be.capability(node, "rom.header", { length: 0x150 })).toEqual(rom.slice(0, 0x150));
  expect(await be.capability(node, "rom.title")).toBe("RPTEST");
  expect(be.pumpJobs()).toBe(0);
});

test("a write while the audio thread runs resolves once the block boundary has passed and the tick pumps", async () => {
  const { be, node, id } = cart();
  be.audioRunning = true;
  let settled = false;
  const write = be.capability(node, "memory.write", { region: "ram", offset: 8, bytes: new Uint8Array([0xaa, 0xbb]) });
  void write.then(() => (settled = true));
  await Promise.resolve();
  expect(settled).toBe(false);
  expect(be.pumpJobs()).toBe(1); // nothing completed: the write is still in the ring
  be.runBlock();
  expect(be.pumpJobs()).toBe(0);
  expect(await write).toBe(undefined);
  expect(be.readRam(id)!.slice(8, 10)).toEqual(new Uint8Array([0xaa, 0xbb]));
});

test("a refused job rejects at once with native's reason", async () => {
  const { be, node } = cart();
  await rejectsWith(be.capability("sys-99", "memory.read", { region: "ram" }), "the running graph has no node sys-99");
  await rejectsWith(be.capability(node, "sram.restore", { bytes: new Uint8Array([1]) }), "restored by rebuilding it");
  await rejectsWith(be.capability(node, "memory.write", { region: "ram", offset: 63, bytes: new Uint8Array([1, 2]) }), "the write runs past region ram (64 bytes)");
  expect(be.pumpJobs()).toBe(0);
});

test("closing the project rejects what is still pending, and a late completion is dropped", async () => {
  const { be, project, node } = cart();
  be.audioRunning = true;
  const write = be.capability(node, "memory.write", { region: "ram", bytes: new Uint8Array([1]) });
  project.newProject();
  await rejectsWith(write, "the project closed");
  be.runBlock(); // the native job still completes; nobody is waiting for it
  expect(be.pumpJobs()).toBe(0);
});

test("a job that fails rejects with its error, and a long job's progress reaches its listener first", async () => {
  // A scripted port: what a device's job looks like from here (progress, then a completion).
  const queue: JobCompletion[] = [];
  const requests: JobRequest[] = [];
  const jobs = new CapabilityJobs({
    submitJob: (r: JobRequest): JobSubmitResult => {
      requests.push(r);
      return { id: requests.length };
    },
    drainJobs: () => queue.splice(0),
  });
  const seen: string[] = [];
  const dump = jobs.run("n8-1", "sram.dump", {}, (p) => seen.push(`${p.progress}/${p.total}`));
  const write = jobs.run("sys-1", "memory.write", { region: "ram", offset: 2, bytes: new Uint8Array([7]) });
  expect(requests).toEqual([
    { nodeId: "n8-1", capability: "rp.system.sram/1", op: "dump" },
    { nodeId: "sys-1", capability: "rp.system.memory/1", op: "write", region: "ram", offset: 2, bytes: new Uint8Array([7]) },
  ]);
  const empty = new Uint8Array(0);
  queue.push({ id: 1, done: false, ok: false, error: "", bytes: empty, progress: 4096, total: 65536 });
  queue.push({ id: 2, done: true, ok: false, error: "node sys-1 left the plan before the job completed", bytes: empty, progress: 0, total: 0 });
  expect(jobs.pump()).toBe(1);
  await rejectsWith(write, "node sys-1 left the plan before the job completed");
  queue.push({ id: 1, done: true, ok: true, error: "", bytes: new Uint8Array([9, 9]), progress: 65536, total: 65536 });
  expect(jobs.pump()).toBe(0);
  expect(await dump).toEqual(new Uint8Array([9, 9]));
  expect(seen).toEqual(["4096/65536"]);
  await rejectsWith(jobs.run("sys-1", "video.frame" as never), "unknown capability operation video.frame");
});

test("a device job's name travels in the request, and its JSON result decodes as UTF-8", async () => {
  // A scripted port over a cart node: the name a battery or upload job is given (the ROM's file on the card) must reach
  // native, and the path native answers with is UTF-8 - the card's own encoding - not one byte a character.
  const queue: JobCompletion[] = [];
  const requests: JobRequest[] = [];
  const jobs = new CapabilityJobs({
    submitJob: (r: JobRequest): JobSubmitResult => {
      requests.push(r);
      return { id: requests.length };
    },
    drainJobs: () => queue.splice(0),
  });
  const upload = jobs.run("cart-1", "rom.upload", { bytes: new Uint8Array([0x4e, 0x45, 0x53]), name: "café.nes" });
  const restore = jobs.run("cart-1", "sram.restore", { bytes: new Uint8Array([1]), name: "café.nes" });
  expect(requests.map((r) => r.name)).toEqual(["café.nes", "café.nes"]);
  const uploadResult = { path: "usb-games/café.nes", mapIndex: 69, streaming: "stopped" as const };
  const restoreResult = { path: "EDN8/gamedata/café.nes/bram.srm", created: true };
  const encoded = (value: unknown) => new TextEncoder().encode(JSON.stringify(value));
  queue.push({ id: 1, done: true, ok: true, error: "", bytes: encoded(uploadResult), progress: 3, total: 3 });
  queue.push({ id: 2, done: true, ok: true, error: "", bytes: encoded(restoreResult), progress: 1, total: 1 });
  expect(jobs.pump()).toBe(0);
  expect(await upload).toEqual(uploadResult);
  expect(await restore).toEqual(restoreResult);
});

test("a close that leaves native's bound full of owed completions does not wedge the next capability()", async () => {
  const { be, project, node } = cart();
  be.audioRunning = true;
  const writes = Array.from({ length: 64 }, (_, k) => be.capability(node, "memory.write", { region: "ram", offset: k % 64, bytes: new Uint8Array([k]) }));
  await rejectsWith(be.capability(node, "rom.title"), "busy: 64 jobs are outstanding");
  project.newProject();
  for (const w of writes) await rejectsWith(w, "the project closed");
  be.runBlock(); // native finishes all 64; nobody is waiting for them, and they hold its bound
  const id = project.systems.addSystem("/roms/cart.gb")!;
  be.setRam(id, new Uint8Array(8));
  expect(await be.capability(`sys-${id}`, "memory.read", { region: "ram", length: 2 })).toEqual(new Uint8Array(2));
  expect(be.pumpJobs()).toBe(0);
});

test("a battery-less cart's dump refuses, and a console without a title field answers an empty one", async () => {
  const { be, node, id } = cart();
  be.setNoBattery(id);
  await rejectsWith(be.capability(node, "sram.dump"), `node ${node} publishes no battery`);
  const be2 = new MockBackend("/cfg");
  const project2 = new ProjectStore(be2, new RecentStore(be2), buildAppRegistry());
  be2.seed("/roms/cart.nes", new Uint8Array([0x4e, 0x45, 0x53, 0x1a, 2, 1, 0, 0, ...new Array(0x8000 + 0x2000).fill(0x41)]));
  const nes = project2.systems.addSystem("/roms/cart.nes");
  expect(nes).toBeTruthy();
  expect(await be2.capability(`sys-${nes}`, "rom.title")).toBe("");
});

test("with an audio thread no tick pumps for, a submission waits through the installed sleep until its job settles", async () => {
  // The CLI's case (src/audioDriver.ts installs the wait between startAudio and stopAudio): no timer, no frame tick.
  let blocks = 0;
  const queue: JobCompletion[] = [];
  const jobs = new CapabilityJobs({
    submitJob: (): JobSubmitResult => ({ id: 1 }),
    drainJobs: () => queue.splice(0),
  });
  setJobWait(() => {
    if (++blocks === 3) queue.push({ id: 1, done: true, ok: true, error: "", bytes: new Uint8Array([5]), progress: 0, total: 0 });
  });
  let settled: Uint8Array | null = null;
  try {
    void jobs.run("sys-1", "memory.read", { region: "ram" }).then((b) => (settled = b));
  } finally {
    setJobWait(null);
  }
  await Promise.resolve();
  await Promise.resolve();
  expect(blocks).toBe(3);
  expect(settled).toEqual(new Uint8Array([5]));
  // And with none installed the promise stays pending until a pump, which pumpAllJobs reaches.
  const later = jobs.run("sys-1", "memory.read", { region: "ram" });
  queue.push({ id: 1, done: true, ok: true, error: "", bytes: new Uint8Array([6]), progress: 0, total: 0 });
  pumpAllJobs();
  expect(await later).toEqual(new Uint8Array([6]));
});
