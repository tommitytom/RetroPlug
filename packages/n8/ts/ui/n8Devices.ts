// "Stream to a physical Everdrive N8 Pro" config for the N8 Pro submenu (in the instance menu's tracker block, beside
// risa/LSDj), shown in every UI whose product starts the N8 pack's provider and mounts its facet (the SDL standalone
// and the DAW plugin: rp_register_package_rpc, packages/n8/native/src/n8/desktop/N8Rpc.hpp), reached through the
// pack's client (createN8Client) over the surface's RPC channel, as the shell's other device seams are. The state
// lives in the native host (the serial connection + lookahead + persisted n8.cfg), not a TS store, so this is a
// subscribable, not a store. Picks/toggles apply immediately (the host (re)connects the serial port + persists on the
// spot); the n8 service subscribes (subscribeN8) so the labels track the new value at once. A host that did not mount
// the facet (the headless UI harness) answers no link, hasN8() is false and the submenu stays hidden.
//
// The one piece of N8 config that is NOT host state is the cartridge sound chip's level: that belongs to the
// system (its "Expansion Volume" knob, in the project), and the driven cart's node carries it as its own
// `expansionVolume` param, so nothing here reaches for it.
import type { Call } from "@retroplug/core/host";
import { createN8Client, type N8LinkClient, type N8LinkConfig, type N8Port } from "../index";
import { createSubscribable } from "@retroplug/ui/menu";

export type { N8Port };
export type N8Config = N8LinkConfig;

const notify = createSubscribable();
const emit = notify.emit;
export const subscribeN8 = notify.subscribe;

// The facet's link client, probed once: a product mounts its facets at boot and never changes them, so the first
// answer holds for the life of this bundle (one per control-plane context, so each plugin instance probes its own).
// undefined = not probed yet, null = no facet (no RPC channel, or a host that did not mount it). A probe needs the
// surface's caller, so a call with none (a test that set the client itself, a host with no channel) reads nothing
// rather than latching "no facet" for ever.
let link: N8LinkClient | null | undefined;

function resolveLink(rpc?: Call): N8LinkClient | null {
  // A test that drives the App's services injects its fake through a global: the App has its own copy of this module,
  // so `setN8LinkClient` cannot reach it. Nothing in production sets one.
  const injected = (globalThis as { __rp_n8LinkClient?: N8LinkClient | null }).__rp_n8LinkClient;
  if (injected !== undefined) return injected;
  if (link !== undefined) return link;
  if (!rpc) return null;
  try {
    const client = createN8Client(rpc).link;
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
 *  submenu: the builder only renders it when a physical N8 is actually detected (n8Detected over getN8Config). */
export function hasN8(rpc?: Call): boolean {
  return resolveLink(rpc) !== null;
}

/** The live serial ports + current N8 link state, read fresh each render. */
export function getN8Config(rpc?: Call): N8Config | null {
  return resolveLink(rpc)?.linkConfig() ?? null;
}

/** Choose the serial port by name. Applies + persists natively, then repaints the labels. */
export function setN8Port(name: string, rpc?: Call): void {
  resolveLink(rpc)?.setPort(name);
  emit();
}

/** Toggle streaming to the N8 (open/close the serial link). Applies + persists natively. */
export function connectN8(enabled: boolean, rpc?: Call): void {
  resolveLink(rpc)?.connect(enabled);
  emit();
}

/** Set the timed-release lookahead latency (ms). Applies + persists natively. */
export function setN8Lookahead(ms: number, rpc?: Call): void {
  resolveLink(rpc)?.setLookahead(ms);
  emit();
}
