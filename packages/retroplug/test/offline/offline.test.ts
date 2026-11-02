// Offline jobs as promises (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3): the mock backend rejects
// every job, since the mock tier runs no graph; and the promise side over a virtual-time port: a refusal rejects at
// once with native's reason, a failure with an OfflineRunError carrying the inputs' reports, several jobs submitted in
// one run are all in flight at once and settle through the poll's native wait with no timer, a chain of jobs (direct, or
// through extra async layers) keeps moving while a long job runs, a fan-out past native's bound is held back rather than
// refused, a job native no longer holds rejects rather than being polled forever, the shared pump (pumpAllJobs, as the
// UI tick's pumpJobs) settles them with the wait off, and the wire request carries the handles and the default returns.
// The native half is held by retroplug-engine-graph-test "[offline]" and test-native/offline-run.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { liveJobCount, pumpAllJobs } from "../../src/capability";
import {
  OFFLINE_MAX_JOBS,
  OfflineJobs,
  OfflineRunError,
  offlineInputRef,
  offlineOutputRef,
  setOfflineWait,
  type OfflineRunRequest,
  type OfflineSubmitResult,
  type OfflineWireCompletion,
  type OfflineWireDrain,
  type OfflineWireRequest,
} from "../../src/offline";

// A port over a virtual clock, as native behaves: a job runs for `frames` time units from its submission (0: done at
// once), a drain that waits advances the clock to the next job's end when none has finished, a drain that does not
// wait never moves it, at most OFFLINE_MAX_JOBS are outstanding (submitted and not yet drained) and one past that is
// refused busy. A job echoes its first input's samples, or fails for a document of "fail".
function clockPort() {
  let now = 0;
  let next = 1;
  const jobs: { id: number; request: OfflineWireRequest; end: number }[] = [];
  const log: string[] = [];
  const waits: number[] = [];
  const submitted: OfflineWireRequest[] = [];
  let most = 0;
  const port = {
    log,
    waits,
    submitted,
    now: () => now,
    advance: (t: number) => (now += t),
    mostInFlight: () => most,
    submitOfflineJob(request: OfflineWireRequest): OfflineSubmitResult {
      if (request.rate <= 0) return { id: null, error: "the job's sample rate must be a positive number" };
      if (jobs.length >= OFFLINE_MAX_JOBS) return { id: null, error: `busy: ${OFFLINE_MAX_JOBS} offline jobs are outstanding` };
      submitted.push(request);
      jobs.push({ id: next, request, end: now + request.frames });
      log.push(`t=${now} submit ${request.document ?? ""}`);
      most = Math.max(most, jobs.length);
      return { id: next++, error: null };
    },
    drainOfflineJobs(waitMs: number): OfflineWireDrain {
      waits.push(waitMs);
      if (waitMs > 0 && jobs.length && !jobs.some((j) => j.end <= now)) now = Math.min(...jobs.map((j) => j.end));
      const done: OfflineWireCompletion[] = [];
      for (const j of jobs.filter((x) => x.end <= now)) {
        const report = { handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true };
        if (j.request.document === "fail") {
          done.push({ id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] });
          continue;
        }
        const echoed = j.request.inputs[0]?.samples ?? new Uint8Array(0);
        log.push(`t=${now} done ${j.request.document ?? ""}`);
        done.push({
          id: j.id,
          ok: true,
          error: "",
          buffers: [{ handle: 1, channels: 1, frames: echoed.byteLength / 4, rate: 48000, samples: echoed }],
          inputs: [],
        });
      }
      for (const d of done) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
      return { completions: done, inFlight: jobs.length };
    },
  };
  return port;
}

const samples = (...v: number[]) => ({ samples: Float32Array.from(v), channels: 1, rate: 48000 });
const job = (document: string, frames: number, ...v: number[]): OfflineRunRequest => ({
  document,
  inputs: [samples(...v)],
  frames,
  rate: 48000,
});

// `body` with the poll's wait on, as a host with no UI tick runs it.
async function waiting<T>(body: () => Promise<T>): Promise<T> {
  setOfflineWait(7);
  try {
    return await body();
  } finally {
    setOfflineWait(0);
  }
}

test("the mock backend rejects every offline job: only a native host runs graphs", async () => {
  const be = new MockBackend("/cfg");
  let message = "";
  await be.offlineRun({ frames: 64, rate: 48000 }).catch((e: Error) => (message = e.message));
  expect(message).toBe("offline jobs need a native host: the mock backend cannot run graphs");
  expect(be.pumpJobs()).toBe(0);
});

test("a refusal rejects at once, and a failure rejects with its reason and the inputs' reports", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  let refused = "";
  await jobs.run({ frames: 64, rate: 0 }).catch((e: Error) => (refused = e.message));
  expect(refused).toBe("the job's sample rate must be a positive number");
  // Completed at the pump right after its submission: no wait, no tick.
  const failed = jobs.run({ document: "fail", inputs: [{ path: "/x.wav" }], frames: 0, rate: 48000 });
  let error: OfflineRunError | null = null;
  await failed.catch((e: OfflineRunError) => (error = e));
  expect(error!.message).toBe("a node faulted: f");
  expect(error! instanceof OfflineRunError).toBe(true);
  expect(error!.inputs).toEqual([{ handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true }]);
  expect(jobs.pendingCount()).toBe(0);
});

test("jobs submitted together are all in flight at once, and settle through the poll's wait with no timer", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  await waiting(async () => {
    const all = Promise.all([jobs.run(job("a", 3, 1, 2)), jobs.run(job("b", 1, 3)), jobs.run(job("c", 2, 4, 5, 6))]);
    expect(port.mostInFlight()).toBe(3); // every submission made before any wait
    const results = await all;
    expect(results.map((r) => Array.from(r.buffers[0].samples))).toEqual([[1, 2], [3], [4, 5, 6]]);
    expect(port.waits.includes(7)).toBe(true); // the poll waited in native
    expect(port.now()).toBe(3); // and only as long as the longest job
    // A chain: each job submitted as the one before it settles.
    let value = 1;
    for (let i = 0; i < 4; i++) value = (await jobs.run(job("chain", 1, value + 1))).buffers[0].samples[0];
    expect(value).toBe(5);
  });
  expect(jobs.pendingCount()).toBe(0);
  expect(liveJobCount()).toBe(0);
});

test("a chain awaiting through extra async layers keeps moving while a long job runs", async () => {
  // A slot's chain as a compiler writes it: each step an async function around offlineRun, its result awaited
  // through a further layer, so the next submission comes several microtask turns after the completion. A long job runs
  // beside it the whole time. The short chain's three jobs must all run before the long job ends, rather than one per
  // completion of something else.
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const step = async (k: number) => {
    const r = await jobs.run(job(`short-${k}`, 1, k));
    await Promise.resolve();
    return r.buffers[0].samples[0];
  };
  const slot = async () => {
    for (let k = 0; k < 3; k++) await (async () => step(k))();
  };
  await waiting(async () => {
    const long = jobs.run(job("long", 50, 0));
    await slot();
    expect(port.now() <= 3).toBe(true);
    await long;
  });
  const order = port.log.filter((l) => l.includes("done")).map((l) => l.replace(/^t=\d+ done /, ""));
  expect(order).toEqual(["short-0", "short-1", "short-2", "long"]);
  expect(port.now()).toBe(50);
});

test("a fan-out past native's bound is held back, never refused busy", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const count = OFFLINE_MAX_JOBS * 2 + 7;
  const results = await waiting(() =>
    Promise.all(Array.from({ length: count }, (_, i) => jobs.run(job(`fan-${i}`, 1 + (i % 5), i)))),
  );
  expect(results.length).toBe(count);
  expect(results.every((r, i) => r.buffers[0].samples[0] === i)).toBe(true);
  expect(port.mostInFlight()).toBe(OFFLINE_MAX_JOBS);
  expect(port.submitted.length).toBe(count);
  expect(jobs.pendingCount()).toBe(0);
  // With the wait off, the pump submits what is held as room comes.
  const later = Array.from({ length: OFFLINE_MAX_JOBS + 3 }, (_, i) => jobs.run(job("later", 1, i)));
  expect(jobs.pendingCount()).toBe(OFFLINE_MAX_JOBS + 3);
  port.advance(1);
  pumpAllJobs();
  port.advance(1);
  pumpAllJobs();
  expect((await Promise.all(later)).length).toBe(OFFLINE_MAX_JOBS + 3);
  expect(jobs.pendingCount()).toBe(0);
});

test("a job native no longer holds rejects, rather than being polled forever", async () => {
  // A port that takes a job and then holds no record of it: nothing comes back and nothing is in flight.
  const jobs = new OfflineJobs({
    submitOfflineJob: () => ({ id: 1, error: null }),
    drainOfflineJobs: () => ({ completions: [], inFlight: 0 }),
  });
  let message = "";
  await waiting(() => jobs.run(job("lost", 1)).catch((e: Error) => (message = e.message)));
  expect(message).toBe("the engine holds no record of this offline job");
  expect(liveJobCount()).toBe(0);
});

test("with the wait off, the shared pump settles offline jobs as the UI tick's does", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  let settled = false;
  const p = jobs.run(job("tick", 2, 9)).then(() => (settled = true));
  expect(liveJobCount()).toBe(1);
  port.advance(1);
  pumpAllJobs();
  expect(jobs.pendingCount()).toBe(1);
  port.advance(1);
  pumpAllJobs();
  expect(port.waits.every((w) => w === 0)).toBe(true); // never a wait: this thread may be a UI's
  await p;
  expect(settled).toBe(true);
  expect(liveJobCount()).toBe(0);
});

test("a request names its inputs then its outputs, and returns every output by default", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const settled = jobs.run({
    document: "{}",
    inputs: [samples(1), { path: "/a.wav", offset: 3, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } }],
    outputs: [{ channels: 1, frames: 8, rate: 11468 }, { channels: 2, frames: 8, rate: 11468 }],
    frames: 8,
    rate: 11468,
  });
  const wire = port.submitted[0];
  port.advance(8);
  expect(jobs.pump()).toBe(0); // settled, so no queue is left live after the last case
  await settled;
  expect(wire.returns).toEqual([3, 4]);
  expect(wire.inputs[1]).toEqual({ path: "/a.wav", offset: 3, cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" } });
  expect(wire.inputs[0].channels).toBe(1);
  expect(offlineInputRef(1)).toBe("res:2");
  expect(offlineOutputRef(2, 1)).toBe("res:4");
});
