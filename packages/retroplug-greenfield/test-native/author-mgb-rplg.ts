// Author a greenfield mGB `.rplg` on native-greenfield-host: compose a store over the real backend,
// load the embedded mGB, and export the project. The plugin's RETROPLUG_AUTOLOAD_PROJECT hook loads
// this at construct for the reaper render smoke — a greenfield-shaped .rplg (ProjectStore.export
// format), not a legacy chunk. Not a *.test.ts, so the test runner ignores it; driven by
// tools/author-greenfield-rplg.js which injects __RPLG_OUT__.
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __RPLG_OUT__: string;

const be = createRealBackend();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
const id = project.systems.loadMgb();
const ok = id != null && project.export(__RPLG_OUT__);
console.log(`[author-mgb-rplg] ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__}`);

(globalThis as { tjs: { exit(code: number): void } }).tjs.exit(ok ? 0 : 1);
