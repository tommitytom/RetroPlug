// The renders the UI has started (src/renderJobs.ts): a job is listed rendering with the system it came from, its
// progress moves renderedMs, and its result settles it done, cancelled or error; a refusal or a throw at submission is
// listed as an error; cancel() is the job handle's; a dismissed job stays gone when it settles; and the engine's
// console lines reach this runtime's console. The jobs here are scripted promises: the engine side is
// test-native/engine-run and the C++ [engine-jobs] cases.
import { test, expect } from "../../testing/harness";
import { RenderJobs } from "../../src/renderJobs";
import { jobHandle, type EngineRunOptions, type EngineRunResult } from "../../src/offline";

interface Scripted {
  options: EngineRunOptions;
  spec: object;
  settle(result: EngineRunResult): void;
  fail(error: Error): void;
  cancelled: number;
}

/** A backend whose engineRun hands out controllable jobs; `jobs` lists them in submission order. */
function scripted(): { backend: { engineRun: RenderJobsBackend }; jobs: Scripted[] } {
  const jobs: Scripted[] = [];
  const engineRun: RenderJobsBackend = (_kind, spec, options = {}) => {
    let resolve!: (r: EngineRunResult) => void;
    let reject!: (e: Error) => void;
    const promise = new Promise<EngineRunResult>((res, rej) => ((resolve = res), (reject = rej)));
    const job: Scripted = { options, spec: spec as object, settle: resolve, fail: reject, cancelled: 0 };
    jobs.push(job);
    return jobHandle(promise, () => {
      job.cancelled++;
      return true;
    });
  };
  return { backend: { engineRun }, jobs };
}
type RenderJobsBackend = ConstructorParameters<typeof RenderJobs>[0]["engineRun"];

const done = (renderedMs: number): EngineRunResult =>
  ({ status: "done", message: "", outputs: ["/o.wav"], renderedMs });
const later = (): Promise<void> => new Promise((r) => void Promise.resolve().then(() => r()));

test("a started render is listed rendering for its system, and progress moves its renderedMs", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(3, { rom: "/a.nes" });
  expect(renders.list()).toEqual([{ id, systemId: 3, state: "rendering", renderedMs: 0, message: "" }]);
  expect(jobs[0].spec).toEqual({ rom: "/a.nes" });
  jobs[0].options.onProgress!(1500);
  expect(renders.list()[0].renderedMs).toBe(1500);
});

test("a result settles the job: done keeps the length written, cancelled and error say so", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  renders.start(2, {});
  renders.start(3, {});
  jobs[0].options.onProgress!(9000);
  jobs[0].settle(done(8100)); // a render that detects its end lowers its count to the length it wrote
  jobs[1].settle({ status: "cancelled", message: "", outputs: [], renderedMs: 200 });
  jobs[2].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
  await later();
  await later();
  const [a, b, c] = renders.list();
  expect([a.state, a.renderedMs]).toEqual(["done", 8100]);
  expect(b.state).toBe("cancelled");
  expect([c.state, c.message]).toEqual(["error", "boom"]);
});

test("a rejected or unsubmittable render is listed as an error", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  jobs[0].fail(new Error("the engine job was refused"));
  const thrower = new RenderJobs({
    engineRun: () => {
      throw new Error("no RPC surface");
    },
  });
  thrower.start(2, {});
  await later();
  await later();
  expect(renders.list()[0].state).toBe("error");
  expect(renders.list()[0].message).toBe("the engine job was refused");
  expect(thrower.list()[0].state).toBe("error");
  expect(thrower.list()[0].message).toBe("no RPC surface");
});

test("cancel() is the job handle's, and false for an id that is not listed", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(1, {});
  expect(renders.cancel(id)).toBe(true);
  expect(jobs[0].cancelled).toBe(1);
  expect(renders.cancel(id + 100)).toBe(false);
});

test("a dismissed job stays gone when it settles", async () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  const id = renders.start(1, {});
  renders.dismiss(id);
  jobs[0].options.onProgress!(100);
  jobs[0].settle(done(100));
  await later();
  await later();
  expect(renders.list().length).toBe(0);
});

test("the job's console lines go to this runtime's console by stream", () => {
  const { backend, jobs } = scripted();
  const renders = new RenderJobs(backend);
  renders.start(1, {});
  const out: string[] = [];
  const log = console.log;
  const err = console.error;
  console.log = (s: string) => void out.push(`out:${s}`);
  console.error = (s: string) => void out.push(`err:${s}`);
  try {
    jobs[0].options.onLog!({ stream: "stdout", text: "render: wrote x" });
    jobs[0].options.onLog!({ stream: "stderr", text: "warning" });
  } finally {
    console.log = log;
    console.error = err;
  }
  expect(out).toEqual(["out:render: wrote x", "err:warning"]);
});

test("a finished job whose system has no tile is dropped as it settles, and when the systems change", async () => {
  const { backend, jobs } = scripted();
  let live = [1, 2];
  const renders = new RenderJobs(backend, () => live);
  renders.start(1, {});
  renders.start(2, {});
  renders.start(3, {}); // a system that is already gone
  jobs[0].settle(done(100));
  jobs[2].settle(done(100));
  await later();
  await later();
  // System 3's finished job went as it settled; system 1's waits for its tile to dismiss it.
  expect(renders.list().map((j) => [j.systemId, j.state])).toEqual([[1, "done"], [2, "rendering"]]);
  jobs[1].settle({ status: "error", message: "boom", outputs: [], renderedMs: 0 });
  await later();
  await later();
  live = [2]; // system 1 is removed: its done job goes with it, system 2's error stays for its tile
  renders.prune();
  expect(renders.list().map((j) => [j.systemId, j.state])).toEqual([[2, "error"]]);
});
