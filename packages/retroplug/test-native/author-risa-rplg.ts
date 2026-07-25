// Author a risa `.rplg.zip` fixture (export = PKZIP, carrying the risa .srm battery) for the real-Reaper
// host-sync render, the NES counterpart of author-lsdj-rplg.ts. Runs on retroplug-host: compose a store
// over the real backend, adopt the risa 2.3.0 ROM with an authored save, and export. The provider
// auto-attaches `risa-sync` off the ROM's RISAxyz marker, so the render needs no role configuration -
// the DAW transport alone drives it.
//
// The song is the shared one-hit-per-beat metronome (risaSyncSong), so every beat is a distinct
// transient the drift analyzer can pair to a click.
//
// Paths injected at bundle time by tools/author-risa-rplg.js.
//   __RISA_ROM__  absolute ROM path (risa-2.3.0-pal.nes)
//   __RPLG_OUT__  absolute output .rplg.zip path
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";
import { buildRisaMetronomeSav } from "./risaSyncSong";

declare const __RISA_ROM__: string;
declare const __RPLG_OUT__: string;

const be = createRealBackend();
const audio = createAudioDriver();
const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());

const id = project.systems.adopt({ romPath: __RISA_ROM__ }, { sramBytes: buildRisaMetronomeSav() });
if (id == null) throw new Error(`adopt failed for ${__RISA_ROM__}`);

audio.renderAudio(3000); // boot: risa materializes the authored working song from the battery

const ok = project.export(__RPLG_OUT__);
console.log(`[author-risa-rplg] ${ok ? "wrote" : "FAILED"} ${__RPLG_OUT__}`);
(globalThis as { tjs?: { exit(code: number): void } }).tjs?.exit(ok ? 0 : 1);
