// Author a THIN `.rplg` (raw JSON, paths only) with a single GB system loaded from an EXTERNAL ROM path
// and reloadOnRomChange ON — the fixture the file-watcher ROM-hot-reload verification autoloads. Unlike
// author-mgb-rplg (embedded mGB → zip), this points at an on-disk ROM so overwriting that file triggers
// the watcher. Driven by an esbuild wrapper that injects __ROM_PATH__ / __RPLG_OUT__.
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __ROM_PATH__: string;
declare const __RPLG_OUT__: string;

const be = createRealBackend();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
const id = project.systems.addSystem(__ROM_PATH__);
if (id == null) {
  console.log(`[author-ext-rom-rplg] FAILED to add system from ${__ROM_PATH__}`);
  (globalThis as { tjs?: { exit(code: number): void } }).tjs?.exit(1);
}
project.systems.setReloadOnRomChange(id as number, true);
const ok = project.save(__RPLG_OUT__);
console.log(`[author-ext-rom-rplg] ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__} (system ${id})`);

(globalThis as { tjs?: { exit(code: number): void } }).tjs?.exit(ok ? 0 : 1);
