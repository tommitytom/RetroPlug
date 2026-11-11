// The Settings > Audio draft: the rows edit this PENDING value and the Apply row commits it. A draft edit never
// touches the device, so stepping through rates does not open one per step, and a half-formed choice is never
// applied.
//
// Why a tiny subscribable and not a store: the draft lives beside the section the file carries, and nothing else in the
// UI reads it, so a cycler edit has nothing to notify App with directly - the menu would only repaint on the next
// unrelated render. The package's UI service subscribes (subscribeAudioDraft), and the shell's service ticker turns
// that into a repaint, so the labels track the pending value immediately.
import { createSubscribable } from "@retroplug/ui/menu";
import type {
  AudioBackendDescription, AudioDescription, AudioDeviceChoice, AudioDriverDescription,
} from "@retroplug/core/host";
import type { UiHost } from "@retroplug/ui/contribution";
import { AUDIO_SECTION_KEY, type AudioSection } from "../configSection";

/** The pending choice: the section's own fields, so an Apply writes exactly what the rows showed. */
export type AudioDraft = AudioDeviceChoice;

let draft: AudioDraft | null = null;
const notify = createSubscribable();
const emit = notify.emit;
export const subscribeAudioDraft = notify.subscribe;

/** The saved choice, as its contributor parses it (the defaults for a config.json that never set it). */
export function savedAudio(host: Pick<UiHost, "config">): AudioDraft {
  const section = host.config.section<AudioSection>(AUDIO_SECTION_KEY);
  return {
    backend: section.backend,
    driver: section.driver,
    device: section.device,
    sampleRate: section.sampleRate,
    blockSize: section.blockSize,
    outChannels: section.outChannels,
  };
}

/** The current draft, seeded from the saved section the first time it is read (and re-seeded after Apply). */
export function getAudioDraft(host: Pick<UiHost, "config">): AudioDraft {
  if (draft == null) draft = savedAudio(host);
  return draft;
}

/** Edit the draft (a cycler step). Notifies the UI service so the labels repaint at once. */
export function setAudioDraft(patch: Partial<AudioDraft>): void {
  if (draft == null) return;
  draft = { ...draft, ...patch };
  emit();
}

/** Whether the draft diverges from the saved choice - drives the Apply row's enabled state. */
export function audioDraftDirty(host: Pick<UiHost, "config">): boolean {
  return JSON.stringify(getAudioDraft(host)) !== JSON.stringify(savedAudio(host));
}

/** Drop any pending edits back to the saved value, and forget the sibling lists fetched for this visit. */
export function resetAudioDraft(): void {
  draft = null;
  siblingDrivers.clear();
  askedBackends.clear();
  cachedFor = "";
  emit();
}

/** Commit the draft: write the section (the file, through the contributor's parse) and hand the same choice to the
 *  device, then re-seed from what was saved. The status the host returns is what the muted row shows. */
export function applyAudioDraft(host: UiHost): void {
  if (!host.audioDevice) return;
  const choice = getAudioDraft(host);
  host.config.setSection(AUDIO_SECTION_KEY, { ...choice });
  const status = host.audioDevice.apply(choice);
  // The device may clamp or round (or fall back to the null device); the section keeps the CHOICE, which is what a
  // later launch opens, and the draft follows it rather than what the driver happened to obtain.
  draft = savedAudio(host);
  if (status?.error) console.warn(`[audio] ${status.error}`);
  emit();
}

/** The whole description, or null on a host with no audio device (a DAW-hosted editor). */
export function audioDescription(host: Pick<UiHost, "audioDevice">): AudioDescription | null {
  return host.audioDevice?.describe() ?? null;
}

/** One choice per driver the rows offer: "Auto" first, then every backend's drivers. The backend in FORCE arrives with
 *  its drivers in the description; a sibling's drivers are its library's first use, so they arrive only when the user
 *  asks for them ("More drivers...", below), which is why a builder never enumerates one. A saved choice that names a
 *  sibling this visit has not fetched still gets its row, labelled from its ids, so the row shows what is stored. */
export interface AudioDriverChoice {
  readonly label: string;
  readonly backend: string;
  readonly driver: string;
}

/** Sibling backends' drivers, fetched only by an explicit step on the Driver row, and for this visit: the cache is
 *  dropped whenever the backend in force moves (the user applied another one), so a later visit asks afresh. */
const siblingDrivers = new Map<string, readonly AudioDriverDescription[]>();
/** The siblings this visit has ASKED about, separately from what each answered: a backend with no usable driver (a
 *  pinned run, where the hook refuses; a library with nothing to list) answers `[]`, and that answer is final - counting
 *  it as "unasked" made the "More drivers..." row fetch the same backend again on every tap. */
const askedBackends = new Set<string>();
let cachedFor = "";

/** What one backend's drivers are right now: the description's own for the backend in force, the cache's for a sibling
 *  the user has already asked about, and nothing otherwise. Never calls the host. */
function knownDrivers(described: AudioDescription, backendId: string): readonly AudioDriverDescription[] {
  if (described.status.backend !== cachedFor) {
    siblingDrivers.clear();
    askedBackends.clear();
    cachedFor = described.status.backend;
  }
  const inDescription = described.backends.find((b) => b.id === backendId)?.drivers ?? [];
  if (inDescription.length > 0) return inDescription;
  return siblingDrivers.get(backendId) ?? [];
}

/** Whether a sibling backend is still one no step has asked about: what the "More drivers..." row is for. A run that
 *  PINS a backend (a harness's --audio-backend) makes every sibling unaskable - the hook refuses - so the row is not
 *  offered there; the "This run:" row already says why the saved choice is not the one running. */
export function moreDriversAvailable(described: AudioDescription): boolean {
  if (described.overrides.backend) return false;
  return described.backends.some((backend) => unaskedSibling(described, backend));
}

/** A backend the Driver row may still ask about: named by the product, not the fallback, not the one in force, and not
 *  one this visit has already asked. */
function unaskedSibling(described: AudioDescription, backend: AudioBackendDescription): boolean {
  return backend.id !== "null" && backend.id !== described.status.backend && !askedBackends.has(backend.id);
}

/** Fetch the next sibling backend's drivers. THE explicit user step the rules allow: a row's action, never a builder.
 *  Returns whether one was asked (and the rows should repaint). */
export function fetchMoreDrivers(host: Pick<UiHost, "audioDevice">, described: AudioDescription): boolean {
  if (described.overrides.backend) return false;
  const next = described.backends.find((backend) => unaskedSibling(described, backend));
  if (!next) return false;
  // Asked BEFORE the call: a backend that answers nothing must not be asked again on the next tap.
  askedBackends.add(next.id);
  siblingDrivers.set(next.id, host.audioDevice?.backendDrivers(next.id) ?? []);
  emit();
  return true;
}

export function audioDriverChoices(
  host: Pick<UiHost, "audioDevice" | "config">,
  described: AudioDescription,
): AudioDriverChoice[] {
  const cfg = getAudioDraft(host);
  const choices: AudioDriverChoice[] = [{ label: "Auto", backend: "auto", driver: "" }];
  const auto = autoChoice(described);
  if (auto) choices[0] = { label: `Auto (${auto.backend.label}: ${auto.driver.label})`, backend: "auto", driver: "" };
  for (const backend of described.backends) {
    if (backend.id === "null") continue; // the fallback the host takes by itself, never a choice
    const drivers = knownDrivers(described, backend.id);
    for (const driver of drivers) {
      choices.push({ label: `${backend.label}: ${driver.label}`, backend: backend.id, driver: driver.id });
    }
    // A stored pick this visit has not enumerated: name it from its ids rather than showing "Auto" over a saved
    // "SDL: pipewire". Picking it and applying makes that backend the one in force, whose real list then arrives.
    if (drivers.length === 0 && cfg.backend === backend.id) {
      choices.push({ label: `${backend.label}: ${cfg.driver || "default"}`, backend: backend.id, driver: cfg.driver });
    }
  }
  return choices;
}

/** What "Auto" resolves to right now: the first backend whose auto driver can open. */
export function autoChoice(
  described: AudioDescription,
): { backend: AudioBackendDescription; driver: AudioDriverDescription } | null {
  for (const backend of described.backends) {
    if (!backend.autoDriver) continue;
    const driver = backend.drivers.find((d) => d.id === backend.autoDriver);
    if (driver) return { backend, driver };
  }
  return null;
}

/** A backend's drivers as this visit knows them: never a host call (see knownDrivers). */
function driversOf(described: AudioDescription, backendId: string): readonly AudioDriverDescription[] {
  return knownDrivers(described, backendId);
}

/** The devices of the draft's choice, and what "" resolves to on it. Under "Auto" they are the auto-resolved driver's,
 *  so the row names where the sound is actually going. */
export function audioDevices(
  _host: Pick<UiHost, "audioDevice">,
  described: AudioDescription,
  choice: AudioDraft,
): { names: string[]; defaultName: string } {
  const resolved = choice.backend === "auto" ? autoChoice(described) : null;
  // "Auto" that resolves to nothing has nothing to enumerate: there is no backend by that name to ask.
  if (choice.backend === "auto" && !resolved) return { names: [], defaultName: "" };
  const backendId = resolved ? resolved.backend.id : choice.backend;
  const driverId = resolved ? resolved.driver.id : choice.driver;
  if (!backendId) return { names: [], defaultName: "" };
  // A driver the backend no longer lists (a saved pick from a machine with another interface) still names itself, so
  // the row shows what is saved rather than silently falling back.
  const driver =
    driversOf(described, backendId).find((d) => d.id === driverId) ??
    (choice.driver ? { id: choice.driver, label: choice.driver, devices: { names: [], defaultName: "" } } : undefined);
  return driver ? driver.devices : { names: [], defaultName: "" };
}

/** The saved section as the device would take it, with no rows in between (the mount's first apply). */
export function applySavedAudio(host: UiHost): void {
  if (!host.audioDevice) return;
  host.audioDevice.apply(savedAudio(host));
}
