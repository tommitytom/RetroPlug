// "Stream to a physical Everdrive N8 Pro" config for the N8 Pro submenu (in the instance menu's tracker block, beside
// risa/LSDj), shown in every UI whose product starts the N8 pack's provider and mounts its facet (the SDL standalone
// and the DAW plugin: rp_register_package_rpc, packages/n8/native/src/n8/desktop/N8Rpc.hpp), reached through the
// pack's client (@retroplug/n8's createN8Client) over the app's RPC channel. Mirrors midiDevices.ts: the state lives
// in the native host (the serial connection + lookahead + persisted n8.cfg), not a TS store, so this is a subscribable,
// not a store. Picks/toggles apply immediately (the host (re)connects the serial port + persists on the spot); App
// subscribes (subscribeN8) so the labels track the new value at once. A host that did not mount the facet (the headless
// UI harness) answers no link, hasN8() is false and the submenu stays hidden.
//
// The one piece of N8 config that is NOT host state is the cartridge sound chip's level: that belongs to the
// system (its "Expansion Volume" knob, in the project), and the driven cart's node carries it as its own
// `expansionVolume` param, so nothing here reaches for it.
import { createN8Client, type N8LinkClient, type N8LinkConfig, type N8Port } from "@retroplug/n8";
import { makeCall } from "../../../src/rpcClient";
import { createSubscribable } from "./subscribable";

export type { N8Port };
export type N8Config = N8LinkConfig;

const notify = createSubscribable();
const emit = notify.emit;
export const subscribeN8 = notify.subscribe;

// The facet's link client, probed once: a product mounts its facets at boot and never changes them, so the first
// answer holds for the life of this bundle (one per control-plane context, so each plugin instance probes its own).
// undefined = not probed yet, null = no facet (no RPC channel, or a host that did not mount it).
let link: N8LinkClient | null | undefined;

function resolveLink(): N8LinkClient | null {
  if (link !== undefined) return link;
  try {
    const client = createN8Client(makeCall()).link;
    client.linkConfig(); // a host without the facet throws here (no such method)
    link = client;
  } catch {
    link = null;
  }
  return link;
}

/** Replace the facet's client: a fake for a test with no native host (the TS mock tier), null for none, undefined to
 *  probe the RPC channel again. */
export function setN8LinkClient(client: N8LinkClient | null | undefined): void {
  link = client;
}

/** Whether the host mounted the N8 facet (the SDL standalone, the DAW plugin). Necessary but not sufficient for the
 *  submenu: menuDefs only renders it when a physical N8 is actually detected (n8Detected over getN8Config). */
export function hasN8(): boolean {
  return resolveLink() !== null;
}

/** The live serial ports + current N8 link state, read fresh each render. */
export function getN8Config(): N8Config | null {
  return resolveLink()?.linkConfig() ?? null;
}

/** Choose the serial port by name. Applies + persists natively, then repaints the labels. */
export function setN8Port(name: string): void {
  resolveLink()?.setPort(name);
  emit();
}

/** Toggle streaming to the N8 (open/close the serial link). Applies + persists natively. */
export function connectN8(enabled: boolean): void {
  resolveLink()?.connect(enabled);
  emit();
}

/** Set the timed-release lookahead latency (ms). Applies + persists natively. */
export function setN8Lookahead(ms: number): void {
  resolveLink()?.setLookahead(ms);
  emit();
}
