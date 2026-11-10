// The N8 Pro's UI contribution (docs/modular-graph-phase-f4b-design.md sections 4.4 and 4.5): the submenu a project
// with a physical Everdrive drives, on the instance menu AND the start menu (the streaming and card ops need no loaded
// system, so a cart is configured before anything is opened), and the service that keeps its card row live.
//
// The submenu shows only when a physical N8 is actually detected (a serial port with the cart's USB VID:PID) - no
// hardware, no menu. Streaming: pick a serial port (auto-detecting the N8), Connect to open the link, tune the
// timed-release lookahead; the native host reconnects + persists (n8.cfg) on the spot. Card ops (Load ROM / back up and
// restore the battery): long capability jobs on the driven cart's rp.n8.nes node, which report progress on the
// read-only card row (the service's poll keeps it live). Those appear only once the link is up (see the gate below).
// One job at a time - every action is disabled while one runs.
import { action, browseThen, cycler, infoRow, separator, submenu, type MenuItem } from "@retroplug/ui/menu";
import {
  defineUi, type MenuBuildContext, type MenuBuilder, type UiHost, type UiService,
} from "@retroplug/ui/contribution";
import { connectN8, getN8Config, setN8Lookahead, setN8Port, subscribeN8, type N8Config } from "./n8Devices";
import {
  getN8CartJob, n8DumpSram, n8LoadRom, n8RestoreSram, onResetN8CartJobForTest, projectCart, type N8CartJob,
} from "./n8SdOps";

const N8_LOOKAHEADS = [0, 5, 10, 15, 20, 30, 50];

/** A physical N8 counts as "here" only when one is actually enumerated (a serial port flagged isN8 by its USB
 *  VID:PID). null cfg = the host lacks the seam (headless harness). Gates whether the whole submenu renders. */
function n8Detected(cfg: N8Config | null): boolean {
  return !!cfg && cfg.ports.some((p) => p.isN8);
}

/** The read-only card-operation row: progress while busy, else the last result / error, else Ready. */
function n8SdLabel(job: N8CartJob): string {
  if (job.busy) {
    const pct = job.bytesTotal > 0 ? ` ${Math.floor((job.bytesDone / job.bytesTotal) * 100)}%` : "";
    return `${job.op === "load" ? "Uploading" : job.op === "dump" ? "Reading" : "Writing"}${pct}`;
  }
  if (job.error) return `Error: ${job.error}`;
  if (job.result) return job.result;
  return "Ready";
}

function n8MenuChildren(host: UiHost, cfg: N8Config, ctx: MenuBuildContext): MenuItem[] {
  // Port cycler: index 0 = "(auto-detect)" (empty selection); the rest are the DETECTED N8 ports only. A plain
  // serial port can't be an Everdrive, so we don't offer it (the cart is identified by its USB VID:PID). A
  // persisted selection that isn't currently enumerated is still shown, tagged "(not detected)", so it never
  // silently vanishes.
  const rpc = host.devices.rpc;
  const n8Ports = cfg.ports.filter((p) => p.isN8);
  const portValues = n8Ports.map((p) => p.port);
  const names = ["(auto-detect)", ...portValues];
  let index = 0;
  if (cfg.selectedPort !== "") {
    const i = portValues.indexOf(cfg.selectedPort);
    if (i >= 0) index = i + 1;
    else { names.push(`${cfg.selectedPort} (not detected)`); index = names.length - 1; }
  }
  const portName = (n: number) => (n === 0 ? "" : portValues[n - 1] ?? cfg.selectedPort);
  const laIdx = Math.max(0, N8_LOOKAHEADS.indexOf(cfg.lookaheadMs));
  const status = cfg.enabled
    ? cfg.connected
      ? `Streaming (${cfg.bytes} bytes)`
      : cfg.error
        ? `Error: ${cfg.error}`
        : "Connecting..."
    : "Off";

  // A card job is one at a time and holds the cart, so every action here (Connect included, which would have to wait
  // for the job's current step) is disabled while one runs, and the status row shows its progress.
  const job = getN8CartJob();
  const busy = job.busy;
  const items: MenuItem[] = [
    // The port, the link and the lookahead are the host's link, which a card job holds while it runs (a disconnect
    // during one waits for its current step), so all three wait too.
    cycler("n8-port", "Port", names, index, (n) => setN8Port(portName(n), rpc), busy),
    action("n8-connect", cfg.enabled ? "Disconnect" : "Connect", () => connectN8(!cfg.enabled, rpc), busy),
    cycler("n8-lookahead", "Lookahead", N8_LOOKAHEADS.map((m) => `${m} ms`), laIdx,
      (n) => setN8Lookahead(N8_LOOKAHEADS[n], rpc), busy),
    separator("n8-sep-status"),
    infoRow("n8-status", `Status: ${status}`),  // read-only streaming status row
  ];

  // The card operations (Load ROM / back up and restore the battery) are the driven cart's node's capability jobs, so
  // they appear while the project drives a cart - or while one runs. They need the link up (a job reaches the cart
  // only through it), and a job reads disconnected while it runs, so `connected` blips false mid-job; `busy` keeps the
  // block put. After a ROM load streaming stays stopped (a new game booted), so the block hides until you reconnect.
  // The project's driven cart, whichever instance's menu is showing: the cart is the PROJECT's (the document places
  // one, for the first system that drives it), so a menu of another system still reaches it, as it always has.
  const cart = projectCart(host);
  if (cart && (cfg.connected || busy)) {
    // Load ROM needs the cart at its menu; the battery rows reach its SD card's save file for the driven ROM (see
    // n8SdOps): back up takes about a minute for 64 KB and shows its progress, and a restore only lands when the
    // game next boots - so restore first, then load.
    items.push(
      separator("n8-sep-sd"),
      // The dialog offers every console's ROM globs, as it always has: the filter is the product's ROM list (what a
      // project can load), which only the shell's console registry can name, and a package may not read it.
      action("n8-load-rom", "Load ROM to N8...", () =>
        browseThen(host, { title: "Load ROM to N8", patterns: [...ctx.romPatterns] },
          (p) => n8LoadRom(host, cart, p)), busy),
      action("n8-dump-sram", "Dump SRAM to file...", () =>
        browseThen(
          host,
          { title: "Dump N8 SRAM", patterns: ["*.srm", "*.sav"], saving: true, defaultName: "n8.srm" },
          (p) => n8DumpSram(host, cart, p)), busy),
      action("n8-restore-sram", "Restore SRAM from file...", () =>
        browseThen(host, { title: "Restore N8 SRAM", patterns: ["*.srm", "*.sav"] },
          (p) => n8RestoreSram(host, cart, p)), busy),
      infoRow("n8-sd-status", `SD: ${n8SdLabel(job)}`),  // read-only card-operation status row
    );
  }
  return items;
}

/** The instance menu's entry, in the tracker block (it drives a real NES). `[]` when no cart is attached, which hides
 *  the entry: the submenu exists only for hardware that is here. */
const n8InstanceMenu: MenuBuilder = {
  placement: "instance.devices",
  build(host, _sys, ctx) {
    const cfg = getN8Config(host.devices.rpc);
    if (!n8Detected(cfg)) return [];
    return [submenu("inst-n8", "N8 Pro", n8MenuChildren(host, cfg!, ctx))];
  },
};

/** The start menu's entry, in its own fenced block: streaming and the card operations need no loaded system, so the
 *  cart can be configured (or a ROM loaded onto it) before a project is opened. */
const n8StartMenu: MenuBuilder = {
  placement: "start",
  build(host, _sys, ctx) {
    const cfg = getN8Config(host.devices.rpc);
    if (!n8Detected(cfg)) return [];
    return [separator("start-sep-n8"), submenu("start-n8", "N8 Pro", n8MenuChildren(host, cfg!, ctx))];
  },
};

// ~10 Hz at 60 fps - smooth enough for the progress bar and to catch "done" promptly, cheap enough to run forever (one
// small read, no repaint unless the version changed).
const POLL_FRAMES = 6;

/** The card row's poll: the snapshot is replaced as a job moves, so the service repaints its host only when the
 *  version it last saw moves (a progress tick, done, or an error). The latch starts where a mount finds it (-1, so the
 *  first poll repaints once), and `start` puts it back on every mount. */
let seenVersion = -1;

onResetN8CartJobForTest(() => {
  seenVersion = -1;
});

const n8Service: UiService = {
  id: "n8",
  start: () => {
    seenVersion = -1;
  },
  subscribe: subscribeN8,
  poll: {
    frames: POLL_FRAMES,
    run() {
      const job = getN8CartJob();
      if (job.version === seenVersion) return false;
      seenVersion = job.version;
      return true;
    },
  },
};

// The device module is published through this entry, as the launchpad pack publishes its own, so a pack's contract is
// one module: its contribution plus the seam a test or another surface reaches it by.
export * from "./n8Devices";
export * from "./n8SdOps";

export default defineUi({ menus: [n8InstanceMenu, n8StartMenu], services: [n8Service] });
