// The plugin's control-plane bundle: the Phase-0 composition root (stores + DSP kernel), packaged for
// a host to eval once at construct. It runs in the plugin's txiki QuickJS context (the same
// __rpcSend-bound namespace the test host uses), composing ProjectStore/SystemsStore over the real
// Backend and loading the DSP kernel, then exposing a tiny C++→JS surface for project I/O:
//
//   __rp_loadProjectPath(path)  — autoload a `.rplg` from disk (reaper -renderproject seeds this)
//   __rp_loadProjectB64(b64)    — DPF setState: load an in-memory chunk (base64 of the .rplg zip)
//   __rp_saveProjectB64()       — DPF getState: export the project as a base64 .rplg chunk
//   __rp_newProject()           — DPF setState(""): reset to an empty project
//   __rp_ready                  — set true once composition + kernel load succeeded
//
// The C++ boundary stays string-only: base64 is done HERE (DPF state is NUL-terminated UTF-8; the
// .rplg is binary PKZIP). Every op is synchronous over the synchronous Backend RPC — the host pumps
// once after eval to run this top-level, then calls the globals directly with no further pumping.
import { createRealBackend } from "./realBackend";
import { RecentStore } from "./recentStore";
import { ProjectStore } from "./projectStore";
import { createDspRuntime } from "./dspRuntime";
import { buildAppRegistry, syncDspFromStore } from "./appHost";
import { dirname } from "./pathUtil";

declare const __DSP_KERNEL_BUNDLE__: string;

// --- base64 (Uint8Array <-> ASCII) — runtime-independent, no btoa/atob dependency ---
const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const B64REV = (() => {
  const m = new Int16Array(128).fill(-1);
  for (let i = 0; i < B64.length; i++) m[B64.charCodeAt(i)] = i;
  return m;
})();

function b64encode(bytes: Uint8Array): string {
  let out = "";
  for (let i = 0; i < bytes.length; i += 3) {
    const b0 = bytes[i];
    const b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
    out += B64[b0 >> 2];
    out += B64[((b0 & 3) << 4) | (b1 >> 4)];
    out += i + 1 < bytes.length ? B64[((b1 & 15) << 2) | (b2 >> 6)] : "=";
    out += i + 2 < bytes.length ? B64[b2 & 63] : "=";
  }
  return out;
}

function b64decode(s: string): Uint8Array {
  let count = 0;
  for (let i = 0; i < s.length; i++) if (B64REV[s.charCodeAt(i) & 127] >= 0) count++;
  const out = new Uint8Array(Math.floor((count * 3) / 4));
  let bits = 0;
  let nbits = 0;
  let oi = 0;
  for (let i = 0; i < s.length; i++) {
    const v = B64REV[s.charCodeAt(i) & 127];
    if (v < 0) continue;
    bits = (bits << 6) | v;
    nbits += 6;
    if (nbits >= 8) {
      nbits -= 8;
      out[oi++] = (bits >> nbits) & 0xff;
    }
  }
  return out;
}

// --- compose the app layer over the real backend + the DSP kernel ---
const be = createRealBackend();
const registry = buildAppRegistry();
const recent = new RecentStore(be);
recent.load();
const project = new ProjectStore(be, recent, registry);
const dsp = createDspRuntime();

const kernelOk = dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!);
// Any systems edit (load / new / role change) re-projects the store into the DSP kernel structure.
project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

// --- the C++→JS surface ---
const g = globalThis as Record<string, unknown>;

g.__rp_loadProjectPath = (path: string): boolean => {
  const bytes = be.readFile(path);
  if (!bytes) return false;
  return project.loadBytes(bytes, dirname(path)).kind === "loaded";
};

g.__rp_loadProjectB64 = (b64: string): boolean => {
  // Empty chunk = no-op (matches the legacy plugin): a host that saved an empty project, or the
  // autoload path having already seeded one, must not be wiped. Explicit reset goes via __rp_newProject.
  if (!b64) return true;
  return project.loadBytes(b64decode(b64), "").kind === "loaded";
};

g.__rp_saveProjectB64 = (): string => {
  const bytes = project.exportBytes();
  return bytes ? b64encode(bytes) : "";
};

g.__rp_newProject = (): void => project.newProject();

// LSDj in a host-clocked sync mode (MidiSync / MidiSyncArduinoboy) locks its playback a fixed distance
// behind the DAW clock — an emulator clock→sound latency, not a drift. Reporting it as plugin latency lets
// the host apply PDC so LSDj lands on the grid, for the DAW-timing renders and real users alike. Output
// modes (MI.OUT / Master Sync) and Off/Keyboard don't host-clock playback, so they add no latency. The
// plugin queries this (as ms) after each load and converts to frames at the live sample rate.
const LSDJ_HOST_SYNC_LATENCY_MS = 33;
g.__rp_syncLatencyMs = (): string => {
  let ms = 0;
  for (const s of project.systems.view()) {
    const mode = (s.roles.find((r) => r.kind === "lsdj-sync")?.config as { mode?: number } | undefined)?.mode;
    if (mode === 1 || mode === 2) ms = Math.max(ms, LSDJ_HOST_SYNC_LATENCY_MS);
  }
  return String(ms);
};

g.__rp_ready = kernelOk;
