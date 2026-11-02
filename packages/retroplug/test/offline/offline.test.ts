// Offline jobs as promises (src/offline.ts, docs/modular-graph-phase-d2-design.md section 3, and the engine jobs of
// docs/modular-graph-phase-d3-design.md section 3): the mock backend rejects every job of either kind, since the mock
// tier runs no graph and has no engine, and hands back a handle with nothing to cancel; an engine job settles with its
// status and length, its progress (a running maximum) and console lines delivered as the drains report them, a job
// one of its callbacks submits included, even when two jobs end in the drain it runs in; a cancel settles a held run
// at once and a submitted one through native's cancel, for both kinds; and the promise side over a virtual-time port:
// a refusal rejects at once with native's reason, a failure with an OfflineRunError carrying the inputs' reports,
// several jobs submitted in one run are all in flight at once and settle through the poll's native wait with no timer,
// a chain of jobs (direct, or through extra async layers) keeps moving while a long job runs, a fan-out past native's
// bound is held back rather than refused, a job native no longer holds rejects rather than being polled forever, a port
// call that throws fails its own run rather than the queue, the shared pump (pumpAllJobs, as the UI tick's pumpJobs)
// settles them with the wait off, and the wire request carries the handles and the default returns.
// The native half is held by retroplug-engine-graph-test "[offline]" and "[engine-jobs]", and test-native/offline-run
// and engine-run.
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
  setOfflineYield,
  type EngineWireCompletion,
  type EngineWireProgress,
  type EngineWireRequest,
  type OfflineRunRequest,
  type OfflineSubmitResult,
  type OfflineWireCompletion,
  type OfflineWireDrain,
  type OfflineWireRequest,
} from "../../src/offline";

// A port over a virtual clock, as native behaves: a job runs for `frames` time units from its submission (0: done at
// once), a drain that waits advances the clock to the next job's end when none has finished, a drain that does not
// wait never moves it, at most OFFLINE_MAX_JOBS are outstanding (submitted and not yet drained) and one past that is
// refused busy. A job echoes its first input's samples, or fails for a document of "fail". An engine job's spec is
// {"ms": n, "out": path, "endMs"?: m, "drop"?: d}: it runs n time units, each drain reporting the units elapsed as its rendered
// milliseconds, writes one line as it starts and one as it ends, and completes "done" with `out` as its output (and
// `endMs` as its final length when given, as a render that detected its song's end lowers its count), or "error" for
// a spec with no out; its first report says `drop` console lines were dropped, as a record that overflowed would. A cancel ends a job at the next drain, a graph job's error and an engine job's status
// "cancelled".
function clockPort() {
  let now = 0;
  let next = 1;
  const jobs: {
    id: number;
    request: OfflineWireRequest;
    engine?: { ms: number; out?: string; endMs?: number; drop?: number; start: number; started: boolean };
    end: number;
    cancelled?: boolean;
  }[] = [];
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
      if (jobs.length >= OFFLINE_MAX_JOBS)
        return { id: null, error: `busy: ${OFFLINE_MAX_JOBS} offline jobs are outstanding` };
      submitted.push(request);
      jobs.push({ id: next, request, end: now + request.frames });
      log.push(`t=${now} submit ${request.document ?? ""}`);
      most = Math.max(most, jobs.length);
      return { id: next++, error: null };
    },
    submitEngineJob(request: EngineWireRequest): OfflineSubmitResult {
      if (request.kind !== "render") return { id: null, error: `unknown engine job kind "${request.kind}"` };
      if (jobs.length >= OFFLINE_MAX_JOBS)
        return { id: null, error: `busy: ${OFFLINE_MAX_JOBS} offline jobs are outstanding` };
      const spec = JSON.parse(request.spec) as { ms: number; out?: string; endMs?: number; drop?: number };
      const engine = { ms: spec.ms, out: spec.out, endMs: spec.endMs, drop: spec.drop, start: now, started: false };
      const request0: OfflineWireRequest = { inputs: [], outputs: [], returns: [], frames: 0, rate: 1 };
      jobs.push({ id: next, request: request0, engine, end: now + spec.ms });
      log.push(`t=${now} submit engine ${spec.out ?? ""}`);
      most = Math.max(most, jobs.length);
      return { id: next++, error: null };
    },
    cancelOfflineJob(id: number): boolean {
      const j = jobs.find((x) => x.id === id);
      if (!j || j.cancelled) return false;
      j.cancelled = true;
      j.end = now;
      return true;
    },
    drainOfflineJobs(waitMs: number): OfflineWireDrain {
      waits.push(waitMs);
      if (waitMs > 0 && jobs.length && !jobs.some((j) => j.end <= now)) now = Math.min(...jobs.map((j) => j.end));
      const done: OfflineWireCompletion[] = [];
      const engineDone: EngineWireCompletion[] = [];
      const engineProgress: EngineWireProgress[] = [];
      for (const j of jobs.filter((x) => x.engine)) {
        const e = j.engine!;
        const lines = e.started ? [] : [{ stream: "stdout" as const, text: `rendering ${e.out ?? ""}` }];
        const droppedLines = e.started ? 0 : (e.drop ?? 0); // the first report carries the count a bounded record dropped
        e.started = true;
        const renderedMs = Math.min(now, j.end) - e.start;
        if (j.end > now) {
          engineProgress.push({ id: j.id, renderedMs, lines, droppedLines });
          continue;
        }
        const status = j.cancelled ? "cancelled" : e.out ? "done" : "error";
        if (status === "done") lines.push({ stream: "stdout", text: e.out! });
        const message = status === "error" ? "the spec names no out" : "";
        const outputs = status === "done" ? [e.out!] : [];
        const finalMs = status === "done" && e.endMs !== undefined ? e.endMs : renderedMs;
        engineDone.push({ id: j.id, status, message, outputs, renderedMs: finalMs, lines, droppedLines });
      }
      for (const j of jobs.filter((x) => !x.engine && x.end <= now)) {
        if (j.cancelled) {
          const none = { buffers: [], inputs: [], guardsFired: 0, guardReason: "" };
          done.push({ id: j.id, ok: false, error: "cancelled", ...none });
          continue;
        }
        const report = { handle: 1, decodedRate: 22050, decodedFrames: 10, frames: 4, nonFinite: true };
        if (j.request.document === "fail") {
          const failed = { id: j.id, ok: false, error: "a node faulted: f", buffers: [], inputs: [report] };
          done.push({ ...failed, guardsFired: 0, guardReason: "" });
          continue;
        }
        const echoed = j.request.inputs[0]?.samples ?? new Uint8Array(0);
        log.push(`t=${now} done ${j.request.document ?? ""}`);
        done.push({
          id: j.id,
          ok: true,
          error: "",
          buffers: [
            { handle: 1, channels: 1, frames: echoed.byteLength / 4, rate: 48000, samples: echoed, nonFinite: false },
          ],
          inputs: [],
          guardsFired: 0,
          guardReason: "",
        });
      }
      for (const d of [...done, ...engineDone]) jobs.splice(jobs.findIndex((j) => j.id === d.id), 1);
      return { completions: done, inFlight: jobs.length, engineCompletions: engineDone, engineProgress };
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

test("the mock backend rejects every engine job, and its handles have nothing to cancel", async () => {
  const be = new MockBackend("/cfg");
  const handle = be.engineRun("render", { rom: "/x.gb", out: "/x.wav" });
  expect(typeof handle.cancel).toBe("function");
  expect(handle.cancel()).toBe(false);
  let message = "";
  await handle.catch((e: Error) => (message = e.message));
  expect(message).toBe("engine jobs need a native host: the mock backend has no engine to run them on");
  const graph = be.offlineRun({ frames: 64, rate: 48000 });
  expect(graph.cancel()).toBe(false);
  await graph.catch(() => {});
  expect(be.pumpJobs()).toBe(0);
});

test("an engine job settles with its status, its progress and console lines arriving as the drains report them",
  async () => {
    const port = clockPort();
    const jobs = new OfflineJobs(port);
    const progress: number[] = [];
    const lines: string[] = [];
    const onProgress = (ms: number) => progress.push(ms);
    const onLog = (l: { stream: string; text: string }) => lines.push(`${l.stream}: ${l.text}`);
    // With the wait off a pump drains; the clock moves only as the test moves it.
    const h = jobs.engine("render", { ms: 3, out: "/o.wav" }, { onProgress, onLog });
    expect(port.log).toEqual(["t=0 submit engine /o.wav"]);
    for (let t = 0; t < 3; t++) {
      port.advance(1);
      jobs.pump();
    }
    const result = await h;
    expect(result).toEqual({ status: "done", message: "", outputs: ["/o.wav"], renderedMs: 3 });
    expect(progress).toEqual([1, 2, 3]);
    expect(lines).toEqual(["stdout: rendering /o.wav", "stdout: /o.wav"]);
    // A failure resolves with its status and message rather than rejecting; a refusal rejects.
    const failed = await waiting(() => jobs.engine("render", JSON.stringify({ ms: 1 })));
    expect(failed).toEqual({ status: "error", message: "the spec names no out", outputs: [], renderedMs: 1 });
    // A render that detects its song's end lowers its count at the end: onProgress holds its maximum, and the length
    // it wrote comes with the result.
    const hff: number[] = [];
    const h2 = jobs.engine("render", { ms: 5, out: "/hff.wav", endMs: 3 }, { onProgress: (ms) => hff.push(ms) });
    for (let t = 0; t < 5; t++) {
      port.advance(1);
      jobs.pump();
    }
    expect((await h2).renderedMs).toBe(3);
    expect(hff).toEqual([1, 2, 3, 4]); // the completion's lower count is not a call
    let refused = "";
    await jobs.engine("bake" as "render", {}).catch((e: Error) => (refused = e.message));
    expect(refused).toBe('unknown engine job kind "bake"');
    expect(jobs.pendingCount()).toBe(0);
    expect(liveJobCount()).toBe(0);
  });

test("lines a bounded record dropped arrive as one stderr line saying how many, after the lines it kept", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const lines: string[] = [];
  const onLog = (l: { stream: string; text: string }) => lines.push(`${l.stream}: ${l.text}`);
  const h = jobs.engine("render", { ms: 2, out: "/o.wav", drop: 3 }, { onLog });
  port.advance(1);
  jobs.pump();
  port.advance(1);
  jobs.pump();
  await h;
  expect(lines).toEqual(["stdout: rendering /o.wav", "stderr: [3 console lines dropped]", "stdout: /o.wav"]);
  // One dropped line reads in the singular; a callback is not owed a line when nothing was dropped.
  lines.length = 0;
  const one = jobs.engine("render", { ms: 1, out: "/one.wav", drop: 1 }, { onLog });
  port.advance(1);
  jobs.pump();
  await one;
  expect(lines).toEqual(["stdout: rendering /one.wav", "stderr: [1 console line dropped]", "stdout: /one.wav"]);
  expect(liveJobCount()).toBe(0);
});

test("a callback that throws is reported and does not strand the other jobs the same drain settles", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  const reported: string[] = [];
  const consoleError = console.error;
  console.error = (m: string) => void reported.push(m);
  try {
    const seen: number[] = [];
    const boom = jobs.engine("render", { ms: 1, out: "/boom.wav" }, {
      onProgress: () => {
        throw new Error("progress boom");
      },
      onLog: () => {
        throw new Error("log boom");
      },
    });
    const fine = jobs.engine("render", { ms: 1, out: "/fine.wav" }, { onProgress: (ms) => seen.push(ms) });
    port.advance(1);
    jobs.pump(); // both end in this one drain
    expect(await boom).toEqual({ status: "done", message: "", outputs: ["/boom.wav"], renderedMs: 1 });
    expect(await fine).toEqual({ status: "done", message: "", outputs: ["/fine.wav"], renderedMs: 1 });
    expect(seen).toEqual([1]);
    expect(reported.includes("an engine job's callback threw: progress boom")).toBe(true);
    expect(reported.includes("an engine job's callback threw: log boom")).toBe(true);
  } finally {
    console.error = consoleError;
  }
  expect(liveJobCount()).toBe(0);
});

test("a job an engine job's callback submits as the last job ends is submitted, and settles", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  await waiting(async () => {
    let next: Promise<number[]> | null = null;
    const onProgress = () => {
      // Inside the drain that ended the render, with nothing else in flight: not a job native lost.
      next ??= jobs.run(job("after", 2, 4)).then((r) => Array.from(r.buffers[0].samples));
    };
    expect((await jobs.engine("render", { ms: 1, out: "/a.wav" }, { onProgress })).status).toBe("done");
    expect(await next!).toEqual([4]);
  });
  expect(jobs.pendingCount()).toBe(0);
});

test("a job a callback submits while two engine jobs end in one drain leaves the second to settle as it ended",
  async () => {
    // Both renders end in the same drain. The first one's progress callback submits a job that completes at once,
    // which pumps; a drain nested inside the one still settling would find nothing in flight and take the second
    // render, whose completion the outer drain has yet to reach, for lost.
    const port = clockPort();
    const jobs = new OfflineJobs(port);
    await waiting(async () => {
      let next: Promise<number[]> | null = null;
      const onProgress = (ms: number) => {
        if (ms >= 2) next ??= jobs.run(job("now", 0, 6)).then((r) => Array.from(r.buffers[0].samples));
      };
      const first = jobs.engine("render", { ms: 2, out: "/1.wav" }, { onProgress });
      const second = jobs.engine("render", { ms: 2, out: "/2.wav" });
      expect((await first).status).toBe("done");
      expect(await second).toEqual({ status: "done", message: "", outputs: ["/2.wav"], renderedMs: 2 });
      expect(await next!).toEqual([6]);
    });
    expect(jobs.pendingCount()).toBe(0);
  });

test("a cancel settles a held run at once, and a submitted one of either kind through native's cancel", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  await waiting(async () => {
    // Submitted: the graph job rejects "cancelled", the engine job resolves with that status, both at a drain.
    const graph = jobs.run(job("long", 100, 1));
    const render = jobs.engine("render", { ms: 100, out: "/r.wav" });
    expect(graph.cancel()).toBe(true);
    expect(render.cancel()).toBe(true);
    let error: OfflineRunError | null = null;
    await graph.catch((e: OfflineRunError) => (error = e));
    expect(error! instanceof OfflineRunError).toBe(true);
    expect(error!.message).toBe("cancelled");
    expect(await render).toEqual({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
    expect(port.now()).toBe(0); // neither ran to its end
    // Settled: nothing left to cancel.
    expect(graph.cancel()).toBe(false);
    expect(render.cancel()).toBe(false);
    // Held past native's bound: settled here, never submitted.
    const fill = Array.from({ length: OFFLINE_MAX_JOBS }, (_, k) => jobs.run(job(`f${k}`, 5, k)));
    const heldGraph = jobs.run(job("held", 1, 7));
    const heldRender = jobs.engine("render", { ms: 1, out: "/h.wav" });
    expect(port.mostInFlight()).toBe(OFFLINE_MAX_JOBS);
    expect(heldGraph.cancel()).toBe(true);
    expect(heldRender.cancel()).toBe(true);
    let heldError = "";
    await heldGraph.catch((e: Error) => (heldError = e.message));
    expect(heldError).toBe("cancelled");
    expect(await heldRender).toEqual({ status: "cancelled", message: "", outputs: [], renderedMs: 0 });
    await Promise.all(fill);
    expect(port.submitted.some((r) => r.document === "held")).toBe(false);
    expect(port.log.some((l) => l.includes("/h.wav"))).toBe(false);
  });
  expect(jobs.pendingCount()).toBe(0);
  expect(liveJobCount()).toBe(0);
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

test("jobs submitted together are all in flight at once, and settle with no timer", async () => {
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
    submitEngineJob: () => ({ id: 1, error: null }),
    drainOfflineJobs: () => ({ completions: [], inFlight: 0 }),
    cancelOfflineJob: () => false,
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
  expect(wire.inputs[1]).toEqual({
    path: "/a.wav",
    offset: 3,
    cap: { frames: 32576, rate: 11468, arithmetic: "float-ratio" },
  });
  expect(wire.inputs[0].channels).toBe(1);
  expect(offlineInputRef(1)).toBe("res:2");
  expect(offlineOutputRef(2, 1)).toBe("res:4");
});

test("a submit that throws rejects its own run, held or not, and the rest still settle", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs({
    submitOfflineJob: (request) => {
      if (request.document === "boom") throw new Error("the port refused a field");
      return port.submitOfflineJob(request);
    },
    submitEngineJob: (request) => port.submitEngineJob(request),
    drainOfflineJobs: (waitMs) => port.drainOfflineJobs(waitMs),
    cancelOfflineJob: (id) => port.cancelOfflineJob(id),
  });
  let message = "";
  await waiting(async () => {
    // One throwing run held behind the whole bound, so it reaches the port from the poll's held-run loop rather than
    // from run(). The held runs are staggered (`frames: i + 1`), so most of them are still pending when it is
    // submitted: the throw must not strand them, nor the poll that is carrying them.
    const held = Array.from({ length: OFFLINE_MAX_JOBS }, (_, i) => jobs.run(job("hold", i + 1, i)));
    const boom = jobs.run(job("boom", 1, 0));
    // One more held behind the throwing one, so the loop it was in carries on and the queue still takes work, and a
    // thinned run still pending when everything else has settled.
    const behind = jobs.run(job("hold", 1, 0));
    expect(jobs.pendingCount()).toBe(OFFLINE_MAX_JOBS + 2);
    await boom.catch((e: Error) => (message = e.message));
    expect((await Promise.all(held)).length).toBe(OFFLINE_MAX_JOBS);
    await behind;
    // And the same throw straight from run(), which never reaches the queue's maps at all.
    await jobs.run(job("boom", 1, 0)).catch((e: Error) => (message += `; ${e.message}`));
  });
  expect(message).toBe("the port refused a field; the port refused a field");
  expect(jobs.pendingCount()).toBe(0);
  expect(liveJobCount()).toBe(0);
});

test("a held run native refuses for another reason rejects with that reason", async () => {
  const port = clockPort();
  const jobs = new OfflineJobs(port);
  let message = "";
  await waiting(async () => {
    // The refusal is reached from the poll's held-run loop, once a completion makes room, not from run().
    const held = Array.from({ length: OFFLINE_MAX_JOBS }, (_, i) => jobs.run(job("hold", 2, i)));
    const refused = jobs.run({ ...job("refused", 1, 0), rate: 0 });
    expect(jobs.pendingCount()).toBe(OFFLINE_MAX_JOBS + 1);
    await refused.catch((e: Error) => (message = e.message));
    expect((await Promise.all(held)).length).toBe(OFFLINE_MAX_JOBS);
  });
  expect(message).toBe("the job's sample rate must be a positive number");
  expect(jobs.pendingCount()).toBe(0);
});

test("a drain that throws rejects every pending run, not just the one that saw it", async () => {
  let next = 1;
  let broken = false;
  const jobs = new OfflineJobs({
    submitOfflineJob: () => ({ id: next++, error: null }),
    submitEngineJob: () => ({ id: next++, error: null }),
    drainOfflineJobs: () => {
      if (broken) throw new Error("the drain broke");
      return { completions: [], inFlight: next - 1 };
    },
    cancelOfflineJob: () => false,
  });
  const runs = [jobs.run(job("a", 1)), jobs.run(job("b", 1))];
  broken = true;
  runs.push(jobs.run(job("c", 1)));
  const messages: string[] = [];
  for (const run of runs) await run.catch((e: Error) => messages.push(e.message));
  expect(messages).toEqual(["the drain broke", "the drain broke", "the drain broke"]);
  expect(jobs.pendingCount()).toBe(0);
  expect(liveJobCount()).toBe(0);
});

test("a host with timers: the poll resumes through the yield after a waited step, never by itself", async () => {
  const port = clockPort();
  const queue = new OfflineJobs(port);
  const resumes: (() => void)[] = [];
  setOfflineWait(7);
  setOfflineYield((resume) => void resumes.push(resume));
  try {
    let result: { status: string } | null = null;
    // Two jobs, so the first waited drain settles one and leaves the other for a further step.
    void queue.engine("render", { ms: 5, out: "/a.wav" });
    void queue.engine("render", { ms: 50, out: "/o.wav" }).then((r) => (result = r));
    // The quiet steps drain without waiting, as microtasks; the first step that waits hands the poll to the yield.
    for (let i = 0; i < 40 && resumes.length === 0; i++) await Promise.resolve();
    expect(resumes.length, "the waited step handed the poll to the yield").toBe(1);
    expect(port.waits.some((w) => w > 0)).toBe(true);
    const drains = port.waits.length;
    // Left alone, the poll takes no further step: it resumes only when the host's loop calls the yield's resume.
    for (let i = 0; i < 40; i++) await Promise.resolve();
    expect(port.waits.length, "no drain until the host resumes the poll").toBe(drains);
    expect(result).toBe(null);
    for (let i = 0; i < 20 && result === null; i++) {
      resumes.splice(0).forEach((r) => r());
      for (let k = 0; k < 40; k++) await Promise.resolve();
    }
    expect(result!.status).toBe("done");
  } finally {
    setOfflineYield(null);
    setOfflineWait(0);
  }
});
