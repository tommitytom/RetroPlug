// Standalone-only MIDI device selection for the Settings > MIDI submenu. Mirrors audioDraft.ts, but a device
// pick APPLIES IMMEDIATELY (like the bindings "Profile" cycler) rather than staging a draft behind an Apply
// row - there is one value per direction, and the native host reconnects the RtMidi port + persists (midi.json)
// on the spot. See __rp_getMidiConfig / __rp_setMidiInput / __rp_setMidiOutput in packages/native/sdl/main.cpp.
//
// Why a subscribable and not a store: the selection lives in native (SDL host), not a TS store, so a pick here
// has nothing to notify App with - the menu would only repaint on the NEXT unrelated re-render. App subscribes
// (subscribeMidi) so a device pick forces a rebuild and the cycler label tracks the new value at once. The seam
// is absent in a DAW / JACK standalone / the headless harness (hasMidiConfig() is false → the submenu hidden).

/** The input selection meaning "every hardware port at once" - an explicit choice, not the default.
 *  Opening every device turns out to be a surprising thing to do by default: anything plugged in becomes a
 *  MIDI source, so a control surface's free-running clock ends up driving the host tempo and a controller's
 *  mixer ports send notes at the cart. Mirrors kAllInputs in MidiIo.hpp. */
export const ALL_INPUTS = "*";

export interface MidiConfig {
  inputs: string[]; // available hardware input port names
  outputs: string[]; // available hardware output port names
  selectedInput: string; // "" = None (virtual input only, the default); ALL_INPUTS = every device; else a name
  selectedOutput: string; // "" = None (virtual output only); else a device name
}

let version = 0;
const listeners = new Set<() => void>();
function emit(): void {
  version++;
  for (const l of listeners) l();
}

type MidiGlobals = {
  __rp_getMidiConfig?: () => Partial<MidiConfig>;
  __rp_setMidiInput?: (name: string) => void;
  __rp_setMidiOutput?: (name: string) => void;
};

/** Whether the SDL host exposes the MIDI-device seam (standalone only). Gates the whole submenu. */
export function hasMidiConfig(): boolean {
  return typeof (globalThis as MidiGlobals).__rp_getMidiConfig === "function";
}

/** The live device lists + current selection, read fresh each render (no draft; picks apply immediately). */
export function getMidiConfig(): MidiConfig | null {
  const fn = (globalThis as MidiGlobals).__rp_getMidiConfig;
  if (typeof fn !== "function") return null;
  const c = fn() ?? {};
  return {
    inputs: Array.isArray(c.inputs) ? c.inputs : [],
    outputs: Array.isArray(c.outputs) ? c.outputs : [],
    selectedInput: typeof c.selectedInput === "string" ? c.selectedInput : "",
    selectedOutput: typeof c.selectedOutput === "string" ? c.selectedOutput : "",
  };
}

/** Choose the input device by name ("" = All Devices). Applies + persists natively, then repaints the label. */
export function setMidiInput(name: string): void {
  (globalThis as MidiGlobals).__rp_setMidiInput?.(name);
  emit();
}

/** Choose the output device by name ("" = None). Applies + persists natively, then repaints the label. */
export function setMidiOutput(name: string): void {
  (globalThis as MidiGlobals).__rp_setMidiOutput?.(name);
  emit();
}

/** A monotonic version — a stable snapshot for App's forced re-render on a device pick. */
export function midiVersion(): number {
  return version;
}

export function subscribeMidi(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
