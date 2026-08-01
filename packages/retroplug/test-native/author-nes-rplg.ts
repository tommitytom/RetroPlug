// Author a NES `.rplg.zip` fixture (export = PKZIP, ROM embedded) for the real-Reaper NES MIDI-timing
// render (the counterpart of author-mgb-rplg.ts / author-lsdj-rplg.ts). Runs on retroplug-host: compose a
// store over the real backend, load the n8-midi NES ROM (auto-attaches the `nes-n8-midi` host-MIDI role
// via romProviders), and export. The Reaper render boots it (~1 s) and drives it via a MIDI item.
//
// Paths injected at bundle time by tools/author-nes-rplg.js.
//   __NES_ROM__   absolute ROM path (resources/roms/n8-midi.nes)
//   __RPLG_OUT__  absolute output .rplg.zip path
import { createRealBackend } from "../src/realBackend";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

declare const __NES_ROM__: string;
declare const __RPLG_OUT__: string;

const be = createRealBackend();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());

const res = project.systems.loadRom(__NES_ROM__);
const id = res && "system" in res ? res.system : null;
if (id == null) throw new Error(`loadRom failed for ${__NES_ROM__}`);

const ok = project.export(__RPLG_OUT__);
console.log(`[author-nes-rplg] ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__}`);
(globalThis as { tjs?: { exit(code: number): void } }).tjs?.exit(ok ? 0 : 1);
