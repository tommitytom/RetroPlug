// Standalone-only pending audio-device config for the Settings > Audio submenu. The cyclers edit this
// DRAFT (never the live device); an explicit "Apply" row commits it via __rp_setAudioConfig, which re-opens
// the SDL audio device on the fly + persists (see packages/native/sdl/main.cpp). Kept out of any persisted
// store — it's ephemeral UI state mirroring the native audio.cfg.
//
// Why a tiny subscribable and not a store: the audio config lives in native (SDL host), not in a TS store,
// so a cycler edit here has nothing to notify App with — the menu would only repaint on the NEXT unrelated
// re-render. App subscribes (subscribeAudioDraft) so a draft edit forces a rebuild and the cycler labels
// track the pending value immediately. The seam is absent in a DAW / the headless harness (hasAudioConfig()
// is false → the submenu is hidden).

export interface AudioCfg {
  sampleRate: number;
  blockSize: number;
  outChannels: number; // 2 = stereo mix; 4/6/8 = wide stems for a multichannel device (per audioRouting)
  driver: string; // audio host API: "Auto" (PipeWire-preferred default) or a name from getAudioDrivers()
}

let draft: AudioCfg | null = null;
let version = 0;
const listeners = new Set<() => void>();
function emit(): void {
  version++;
  for (const l of listeners) l();
}

// The native config carries the available driver list too (drivers); it's an option set, not part of the
// draft value, so nativeGet folds only the selected `driver` into AudioCfg and getAudioDrivers() reads the list.
type NativeAudioCfg = Partial<AudioCfg> & { drivers?: string[] };
function nativeGet(): AudioCfg | null {
  const fn = (globalThis as { __rp_getAudioConfig?: () => NativeAudioCfg }).__rp_getAudioConfig;
  if (typeof fn !== "function") return null;
  const c = fn();
  if (!c) return null;
  return {
    sampleRate: c.sampleRate ?? 48000,
    blockSize: c.blockSize ?? 512,
    outChannels: c.outChannels ?? 2,
    driver: c.driver ?? "Auto",
  };
}
function nativeSet(sampleRate: number, blockSize: number, outChannels: number, driver: string): void {
  (
    globalThis as { __rp_setAudioConfig?: (r: number, b: number, ch: number, d: string) => void }
  ).__rp_setAudioConfig?.(sampleRate, blockSize, outChannels, driver);
}

/** The audio host APIs the SDL host offers ("Auto" + each compiled-in/available driver). Option set, not draft. */
export function getAudioDrivers(): string[] {
  const fn = (globalThis as { __rp_getAudioConfig?: () => NativeAudioCfg }).__rp_getAudioConfig;
  if (typeof fn !== "function") return ["Auto"];
  const list = fn()?.drivers;
  return Array.isArray(list) && list.length > 0 ? list : ["Auto"];
}

/** Whether the SDL host exposes the audio-config seam (standalone only). Gates the whole submenu. */
export function hasAudioConfig(): boolean {
  return nativeGet() != null;
}

/** The current draft, seeded from the live device the first time it's read (and re-seeded after Apply). */
export function getAudioDraft(): AudioCfg | null {
  if (draft == null) draft = nativeGet();
  return draft;
}

/** Whether the draft diverges from the live device — drives the Apply row's enabled state. */
export function audioDraftDirty(): boolean {
  const live = nativeGet();
  const d = getAudioDraft();
  return (
    !!live &&
    !!d &&
    (live.sampleRate !== d.sampleRate ||
      live.blockSize !== d.blockSize ||
      live.outChannels !== d.outChannels ||
      live.driver !== d.driver)
  );
}

/** Edit the draft (a cycler step). Notifies App so the labels repaint at once. */
export function setAudioDraft(next: Partial<AudioCfg>): void {
  const cur = getAudioDraft();
  if (!cur) return;
  draft = { ...cur, ...next };
  emit();
}

/** Commit the draft to the device (re-opens + persists natively), then re-seed from what it actually adopted. */
export function applyAudioDraft(): void {
  const d = getAudioDraft();
  if (!d) return;
  nativeSet(d.sampleRate, d.blockSize, d.outChannels, d.driver);
  draft = nativeGet(); // the device may clamp/round (or fall back to Auto); reflect what it took
  emit();
}

/** Drop any pending edits back to the live device value. */
export function resetAudioDraft(): void {
  draft = nativeGet();
  emit();
}

/** A monotonic version — a stable snapshot for App's forced re-render on draft change. */
export function audioDraftVersion(): number {
  return version;
}

export function subscribeAudioDraft(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
