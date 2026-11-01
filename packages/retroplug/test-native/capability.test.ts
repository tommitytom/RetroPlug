// Capability jobs over the real host (src/capability.ts, docs/modular-graph-phase-c5-design.md section 6): a running
// Master System answers backend.capability() with exactly what the system-id calls answer for it (readRam, readSram,
// and readFilePrefix on the ROM it was built from), and a write through it and one through writeRam land in the same
// region, each visible to the other's read. Rendering on the calling thread (no audio thread), every job completes at
// its submission; with the audio driver's thread running, a write completes at a block boundary, and the submission
// waits through the driver's native sleep until it does, since this host fires no timer and has no frame tick. The queue, the block-boundary completion and the threading are held by
// retroplug-engine-graph-test "[capability]".
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { directGraph } from "../src/graph/directGraph";

const graph = directGraph();

declare const __REPO_RESOURCES_DIR__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";

test("capability() and the system-id calls agree on a running system, reads and writes alike", async () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) skip(`capability: missing ${ROM}`);
  const audio = createAudioDriver();
  const id = 1;
  expect(
    graph.add(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, coreConfig: { enableFm: false } },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(500);
  const node = `sys-${id}`;

  const ram = be.readRam(id)!;
  expect(await be.capability(node, "memory.regions")).toEqual([{ name: "ram", size: ram.length }]);
  expect(await be.capability(node, "memory.read", { region: "ram" })).toEqual(ram);
  expect(await be.capability(node, "sram.dump")).toEqual(be.readSram(id)!);
  expect(await be.capability(node, "rom.header", { length: 0x8200 })).toEqual(be.readFilePrefix(ROM, 0x8200)!);
  expect(await be.capability(node, "rom.title")).toBe(""); // a Sega header has no title field

  // A write through the job lands where readRam reads, at once on this host; one through writeRam lands where the job
  // reads, at the next block as it always has.
  const offset = 0x1f00;
  expect(await be.capability(node, "memory.write", { region: "ram", offset, bytes: Uint8Array.from([0xde, 0xad]) })).toBe(undefined);
  expect(be.readRam(id)!.slice(offset, offset + 2)).toEqual(Uint8Array.from([0xde, 0xad]));
  expect(be.writeRam(id, offset + 2, Uint8Array.from([0xbe, 0xef]))).toBeTruthy();
  audio.renderAudio(20);
  expect(await be.capability(node, "memory.read", { region: "ram", offset, length: 4 })).toEqual(Uint8Array.from([0xde, 0xad, 0xbe, 0xef]));

  let refusal = "";
  await be.capability(node, "sram.restore", { bytes: new Uint8Array(1) }).catch((e: Error) => (refusal = e.message));
  expect(refusal.includes("restored by rebuilding it")).toBe(true);
  expect(be.pumpJobs()).toBe(0);
  expect(graph.remove(id)).toBeTruthy();
});

test("with the audio thread running, capability() settles a write with no timer and no frame tick", async () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) skip(`capability: missing ${ROM}`);
  const audio = createAudioDriver();
  const id = 2;
  expect(
    graph.add(
      { romPath: ROM, platform: "sms", core: "mesen", embeddedRom: "", savPath: null, statePath: null, coreConfig: { enableFm: false } },
      id,
    ),
  ).toBeTruthy();
  audio.renderAudio(200);
  expect(audio.startAudio()).toBeTruthy();
  const node = `sys-${id}`;
  const offset = 0x1f10;
  // Held without awaiting first: a promise that never settled would hang an await, so its settling is checked after
  // two microtask turns, which is all a settled promise needs (the submission waited for the block itself).
  let state = "pending";
  const write = be.capability(node, "memory.write", { region: "ram", offset, bytes: Uint8Array.from([0x12, 0x34]) });
  write.then(() => (state = "resolved"), (e: Error) => (state = `rejected: ${e.message}`));
  await Promise.resolve();
  await Promise.resolve();
  expect(state).toBe("resolved");
  expect(await be.capability(node, "memory.read", { region: "ram", offset, length: 2 })).toEqual(Uint8Array.from([0x12, 0x34]));
  expect(audio.stopAudio()).toBeTruthy();
  expect(be.pumpJobs()).toBe(0);
  expect(graph.remove(id)).toBeTruthy();
});
