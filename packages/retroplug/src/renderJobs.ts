// The renders this instance has started from the UI (System > Render), as the tile badge shows them. A render is an
// engine job (backend.engineRun, docs/modular-graph-phase-d3-design.md): this list keeps what the job's promise and
// progress callbacks report - which system it came from, how much audio it has rendered, how it ended - plus the
// job's handle, so a tap on the badge cancels it. It belongs to the store graph, not to the editor: in a DAW the
// graph is the control plane's and outlives the editor, so a render started in one editor session is still listed
// (and its completion still settles through the graph's own queue) when the editor opens again.
//
// Nothing here pumps. A job settles when its queue drains it: at the submission's own pump, from the editor's frame
// tick (ui/app/useJobPump.ts) while one is open, or at the next submission. With the editor closed nothing drains, so a
// render that finishes then is still listed rendering until the editor opens again; its WAV is on disk either way.
//
// A finished job is dismissed by the tile of its system (ui/screens/grid/EmulatorTile.tsx). One whose system has no
// tile (removed, or the project replaced) would never be, so it is dropped as it settles and whenever the systems
// list changes (prune).

import type { Backend } from "./backend";
import { echoJobLog, type EngineRunHandle } from "./offline";

/** One render's status, as the tile badge reads it. */
export interface RenderJobStatus {
  id: number;
  systemId: number;
  state: "rendering" | "done" | "error" | "cancelled";
  renderedMs: number; // audio rendered so far (ms); on a finished job, the length written
  message: string; // error detail (state === "error")
}

interface Entry {
  status: RenderJobStatus;
  handle: EngineRunHandle | null; // null when the submission itself threw
}

export class RenderJobs {
  private readonly entries = new Map<number, Entry>();
  private nextId = 1;
  /** Drawn once per list, so a job's key (`<token>-<id>`) differs from every other list's: two plugin instances in one
   *  DAW each have a list of their own, both numbering from 1, and share the user's config directory. */
  private readonly token = `${Date.now().toString(36)}-${Math.floor(Math.random() * 2 ** 32).toString(36)}`;

  /** `liveSystems` names the systems that have a tile now; without it every job is kept until dismissed. */
  constructor(
    private readonly backend: Pick<Backend, "engineRun">,
    private readonly liveSystems?: () => number[],
  ) {}

  /** Start rendering `spec` (a render job spec) for system `systemId`, and return the job's id here. A render the
   *  engine refuses, or that cannot be submitted, is listed as an error. `spec` can be a function of the job's key, a
   *  name no other job of any list shares (the caller's files for this job carry it), and a throw from it is listed as
   *  the job's error with nothing submitted. `onSettled` runs once the job has ended however it ended (listed or
   *  dismissed, a throw from `spec` included): the engine has let go of the job's files by then, or can
   *  no longer report on the job (a broken link to it). */
  start(systemId: number, spec: object | ((key: string) => object), onSettled?: () => void): number {
    const status: RenderJobStatus = { id: this.nextId++, systemId, state: "rendering", renderedMs: 0, message: "" };
    const entry: Entry = { status, handle: null };
    this.entries.set(status.id, entry);
    const settled = (): void => {
      try {
        onSettled?.();
      } catch (e) {
        console.warn(`render job ${status.id}: ${(e as Error)?.message ?? String(e)}`);
      }
    };
    const update = (change: Partial<RenderJobStatus>): void => {
      // A job the badge dismissed is no longer listed, and what it reports after is dropped.
      if (this.entries.get(status.id) !== entry) return;
      entry.status = { ...entry.status, ...change };
      if (entry.status.state !== "rendering") this.prune();
    };
    try {
      const body = typeof spec === "function" ? spec(`${this.token}-${status.id}`) : spec;
      entry.handle = this.backend.engineRun("render", body, {
        onProgress: (renderedMs) => update({ renderedMs }),
        onLog: echoJobLog,
      });
    } catch (e) {
      update({ state: "error", message: (e as Error)?.message ?? String(e) });
      settled();
      return status.id;
    }
    entry.handle.then(
      (result) => {
        if (result.status === "done") update({ state: "done", renderedMs: result.renderedMs });
        else if (result.status === "cancelled") update({ state: "cancelled" });
        else update({ state: "error", message: result.message });
        settled();
      },
      (e: unknown) => {
        update({ state: "error", message: (e as Error)?.message ?? String(e) });
        settled();
      },
    );
    return status.id;
  }

  /** The renders listed, oldest first. A snapshot: a job's status object is replaced when it changes. */
  list(): RenderJobStatus[] {
    return [...this.entries.values()].map((e) => e.status);
  }

  /** Ask a running render to stop. It ends cancelled once the host drains it. False for a job that has finished. */
  cancel(id: number): boolean {
    return this.entries.get(id)?.handle?.cancel() ?? false;
  }

  /** Drop every finished job whose system has no tile any more. */
  prune(): void {
    if (!this.liveSystems) return;
    const live = new Set(this.liveSystems());
    for (const [id, e] of this.entries)
      if (e.status.state !== "rendering" && !live.has(e.status.systemId)) this.entries.delete(id);
  }

  /** Drop a job from the list (the badge dismissing a finished one). */
  dismiss(id: number): void {
    this.entries.delete(id);
  }
}
