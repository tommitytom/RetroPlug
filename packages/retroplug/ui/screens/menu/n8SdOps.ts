// The Everdrive N8 Pro's card operations (Load ROM, back up and restore the battery), for the N8 Pro submenu (in the
// instance menu's tracker block). They are long USB transfers, so they run as the graph's own capability jobs
// (docs/modular-graph-phase-c5-design.md section 6): `backend.capability` submits an operation against the system's
// rp.n8.nes node and settles when the cart answers, reporting progress on the way. This module only picks the node,
// reads and writes the local file the user chose, and keeps the snapshot the menu row renders. The cart is at its menu
// throughout: a battery access on a running game wedges it, and a restore only lands when the game next boots (restore,
// then load).
//
// Available in BOTH the SDL standalone and the DAW plugin, wherever the project drives a cart (a NES whose "drive the
// real cart" setting is on places the node); with no such system there is no node to address, so the card rows are
// absent, and with no cart attached the node's job is refused and the reason shows in the row.

import type { ControlPlaneBackend } from "../../../src/backend";
import type { SystemView } from "../../../src/systemsStore";
import { cartNodeId, drivenCartSystem } from "../../../src/graph/buildGraph";
import { basename } from "../../../src/pathUtil";
import { roleConfig } from "../../../src/systemRoles";

/** One card operation, as the menu row renders it: what is running, how far it has got, and how the last one ended. */
export interface N8CartJob {
  busy: boolean;
  op: "load" | "dump" | "restore" | "";
  bytesDone: number;
  bytesTotal: number; // 0 = indeterminate
  error: string; // "" = ok
  result: string; // human summary on success
  version: number; // bumps on any change, so the UI re-renders only when it moves
}

// The snapshot the menu row renders and useN8SdWatch polls: it is replaced, never mutated, and its version moves on
// every change, so the poll re-renders exactly when something it shows did.
let job: N8CartJob = {
  busy: false,
  op: "",
  bytesDone: 0,
  bytesTotal: 0,
  error: "",
  result: "",
  version: 0,
};
// The file name of the ROM this process booted on the cart through `load`, and the name a dump or a restore after it
// reaches that ROM's battery file by without asking. The cart's state, not a node's: the ROM booted on the card
// outlives the drive moving to another NES, an editor close and reopen, and a project load (a fresh UI realm starts
// with none and falls back to the driven system's ROM file), so one name serves whichever node is the cart's now.
let bootedName: string | null = null;

/** The live snapshot, read fresh each poll (the menu and useN8SdWatch). */
export function getN8CartJob(): N8CartJob {
  return job;
}

function update(patch: Partial<N8CartJob>): void {
  job = { ...job, ...patch, version: job.version + 1 };
}

/** The cart the project drives, and the node that is it: the system buildGraph places an rp.n8.nes for (that rule,
 *  not a second one), which is the one whose core config drives the real cart. Null when no system drives one, which is
 *  the only way the card rows have nothing to act on. */
export function projectCart(systems: readonly SystemView[]): { system: SystemView; nodeId: string } | null {
  const cart = drivenCartSystem(
    systems.map((system) => ({ system, platform: system.platform, coreConfig: roleConfig(system.roles, "mesen") })),
  );
  return cart ? { system: cart.system, nodeId: cartNodeId(cart.system.id) } : null;
}

/** The ROM name a card job reaches the cart's files by: the ROM this process booted on the cart, else the driven
 *  system's own ROM file (the card's folder for a game is named after it). Empty when neither is known, which leaves
 *  the choice to the host: it resolves the ROM it last booted, or refuses the job. */
function romNameFor(cart: { system: SystemView }): string {
  return bootedName || basename(cart.system.romPath);
}

/** Start one job for the menu: no second job at a time, and every way it can end (refused, failed, done) leaves a
 *  snapshot the row can show. `start` answers the row's summary where the job's own result is worth showing (a restore
 *  says where it wrote), and nothing for the operation's plain one. */
function run(
  op: N8CartJob["op"],
  start: (onProgress: (done: number, total: number) => void) => Promise<string | void>,
): void {
  if (job.busy) return;
  update({ busy: true, op, bytesDone: 0, bytesTotal: 0, error: "", result: "" });
  start((done, total) => update({ bytesDone: done, bytesTotal: total }))
    .then((summary) => update({ busy: false, op: "", result: typeof summary === "string" ? summary : doneLabel(op) }))
    .catch((e: unknown) => update({ busy: false, op: "", error: e instanceof Error ? e.message : String(e) }));
}

function doneLabel(op: N8CartJob["op"]): string {
  if (op === "load") return "Booted the ROM on the cart";
  if (op === "dump") return "Backed up the cart's battery";
  if (op === "restore") return "Restored the battery (it loads at the next boot)";
  return "";
}

/** A file the user picked that could not be read: no job ran, so the row only gets its error - unless a job already
 *  holds it, which no row allows (they are all disabled while one runs) and a call that reaches here anyway must not
 *  clear. */
function unreadable(path: string): void {
  if (!job.busy) update({ busy: false, op: "", error: `cannot read ${path}` });
}

/** Upload a local ROM to the cart's SD card and boot it (the cart must be at its menu). The job's name is the file's
 *  own name, which is the folder its battery lives in. */
export function n8LoadRom(backend: ControlPlaneBackend, cart: { nodeId: string }, path: string): void {
  const bytes = backend.readFile(path);
  if (!bytes) {
    unreadable(path);
    return;
  }
  const name = basename(path);
  run("load", async (onProgress) => {
    await backend.capability(cart.nodeId, "rom.upload", { bytes, name }, (p) => onProgress(p.progress, p.total));
    bootedName = name;
  });
}

/** Back up the cart's battery (its SD card's bram.srm for the ROM) to the local file the user picked. About a minute
 *  for 64 KB over USB, so the row shows the block progress while it runs. */
export function n8DumpSram(
  backend: ControlPlaneBackend,
  cart: { system: SystemView; nodeId: string },
  path: string,
): void {
  const name = romNameFor(cart);
  run("dump", async (onProgress) => {
    const bytes = await backend.capability(
      cart.nodeId,
      "sram.dump",
      name ? { name } : {},
      (p) => onProgress(p.progress, p.total),
    );
    if (!backend.writeFile(path, bytes)) throw new Error(`cannot write ${path}`);
  });
}

/** Write a local .srm to the cart's SD card battery file. The game loads it at its next boot, so a restore goes before
 *  the ROM load: restore, then n8LoadRom. */
export function n8RestoreSram(
  backend: ControlPlaneBackend,
  cart: { system: SystemView; nodeId: string },
  path: string,
): void {
  const bytes = backend.readFile(path);
  if (!bytes) {
    unreadable(path);
    return;
  }
  const name = romNameFor(cart);
  run("restore", async (onProgress) => {
    const wrote = await backend.capability(cart.nodeId, "sram.restore", { bytes, ...(name ? { name } : {}) }, (p) =>
      onProgress(p.progress, p.total),
    );
    // The result names the file the cart wrote and whether the ROM's folder had to be made for it, which is how a name
    // the card has no folder for shows: a mistyped one as often as a game that never saved, and either way the bytes
    // may land in a folder the game never reads. So the row says where they went instead of that the restore worked.
    if (!wrote) return;
    return wrote.created
      ? `Warning: restored the battery to ${wrote.path}, and made that folder: is that the ROM's file name on the card?`
      : `Restored the battery to ${wrote.path} (it loads at the next boot)`;
  });
}

/** Forget the last job's snapshot and the ROM this process booted on the cart: a test seam, so one case's job does
 *  not colour the next one's rows. A project close does not need it (a rejected promise clears `busy`, and the booted
 *  ROM is read only as a name a dump or restore reaches by, so keeping it across projects is harmless). */
export function resetN8CartJob(): void {
  bootedName = null;
  update({ busy: false, op: "", bytesDone: 0, bytesTotal: 0, error: "", result: "" });
}
