// The MIDI routing decision — a pure port of native's Project::dispatchMidi
// (packages/native/src/project/Project.hpp). Given a block's MIDI events + the project's
// routing mode + system count, it decides which system(s) each event lands on and
// applies the MidiChannelToInstance channel rewrite. No emulator, no audio thread, no
// retained state — pure decision logic (doc-06 Tier-1). This is the canonical routing
// the app will eventually run; today it's the reference / conformance oracle
// (the live per-block execution waits on the doc-04 scriptable runtime + RT-safety).

import { MidiRouting, MIDI_ROUTING_VALUES } from "./settingsEnums";
export { MidiRouting, MIDI_ROUTING_VALUES };

/** Events with more than this many bytes are SysEx (native rides them on `dataExt`). */
export const MIDI_DATA_SIZE = 4;

// Routing mode is string-valued (see settingsEnums): SendToAll broadcasts every event to every system;
// FourChannelsPerInstance gives instance N channels 4N..4N+3; OneChannelPerInstance gives instance N
// only channel N; MidiChannelToInstance is OneChannelPerInstance with the channel rewritten to 0.

/** A block MIDI event. `data` is the raw MIDI byte sequence (status + payload); `frame`
 *  is the sample offset within the block. Native's `size` is `data.length` here (JS
 *  arrays are variable-length, so there is no separate `dataExt`); SysEx is
 *  `data.length > MIDI_DATA_SIZE`. */
export interface MidiEvent {
  frame: number;
  data: number[];
}

/** The routing decision for one event: the 0-based system indices it lands on, plus the
 *  (possibly channel-rewritten) bytes to deliver. Broadcast → every index. */
export function routeEvent(
  ev: MidiEvent,
  mode: MidiRouting,
  systemCount: number,
): { targets: number[]; data: number[] } {
  const data = ev.data;
  // Native returns early on empty systems and skips size-0 events.
  if (systemCount <= 0 || data.length === 0) return { targets: [], data };

  const status = data[0];
  const isSystemMsg = (status & 0xf0) === 0xf0;
  // System/realtime messages and SysEx have no channel nibble — broadcast unchanged,
  // regardless of routing mode.
  if (isSystemMsg || data.length > MIDI_DATA_SIZE) return { targets: allTargets(systemCount), data };

  const chan = status & 0x0f;
  switch (mode) {
    case MidiRouting.SendToAll:
      return { targets: allTargets(systemCount), data };
    case MidiRouting.FourChannelsPerInstance:
      return { targets: [Math.floor(chan / 4) % systemCount], data };
    case MidiRouting.OneChannelPerInstance:
      return { targets: [chan % systemCount], data };
    case MidiRouting.MidiChannelToInstance:
      // Rewrite to channel 1 (low nibble = 0); only the single target sees the copy.
      return { targets: [chan % systemCount], data: [status & 0xf0, ...data.slice(1)] };
    default:
      return { targets: [], data }; // modes are clamped 0..3 upstream; an unknown one drops
  }
}

/** Fan a whole block into per-system inboxes — the shape native produces by calling
 *  `sys->onMidi` per target. Returns an array of length `systemCount`; element `i` is the
 *  events system `i` receives (rewritten bytes for a MidiChannelToInstance target,
 *  unchanged otherwise). Size-0 events are skipped. */
export function routeBlock(events: MidiEvent[], mode: MidiRouting, systemCount: number): MidiEvent[][] {
  const inboxes: MidiEvent[][] = [];
  for (let i = 0; i < systemCount; i++) inboxes.push([]);
  for (const ev of events) {
    if (ev.data.length === 0) continue;
    const { targets, data } = routeEvent(ev, mode, systemCount);
    for (const t of targets) inboxes[t].push({ frame: ev.frame, data });
  }
  return inboxes;
}

function allTargets(n: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < n; i++) out.push(i);
  return out;
}

/** The allocation-free hot-path twin of `routeBlock`: fan `events` into caller-owned, PRE-CLEARED
 *  positional `inboxes` (element `i` = system `i`), mutating them in place. Used per audio block by
 *  the DSP kernel, so it inlines the routing arithmetic — no per-event `routeEvent` result object, no
 *  `allTargets` array. For every mode EXCEPT MidiChannelToInstance the delivered event is the ORIGINAL
 *  `ev` reference (zero allocation), which downstream translators must treat as read-only (true today;
 *  a future transform stage rewriting `ctx.midi` would need copy-on-write here). Only the mode-3
 *  channel rewrite allocates a fresh event. `routeBlock` stays the pure reference/conformance oracle. */
export function routeBlockInto(events: MidiEvent[], mode: MidiRouting, inboxes: MidiEvent[][]): void {
  const systemCount = inboxes.length;
  if (systemCount <= 0) return;
  for (let e = 0; e < events.length; e++) {
    const ev = events[e];
    const data = ev.data;
    if (data.length === 0) continue;

    const status = data[0];
    // System/realtime messages and SysEx have no channel nibble — broadcast unchanged.
    if ((status & 0xf0) === 0xf0 || data.length > MIDI_DATA_SIZE) {
      for (let i = 0; i < systemCount; i++) inboxes[i].push(ev);
      continue;
    }

    const chan = status & 0x0f;
    switch (mode) {
      case MidiRouting.SendToAll:
        for (let i = 0; i < systemCount; i++) inboxes[i].push(ev);
        break;
      case MidiRouting.FourChannelsPerInstance:
        inboxes[Math.floor(chan / 4) % systemCount].push(ev);
        break;
      case MidiRouting.OneChannelPerInstance:
        inboxes[chan % systemCount].push(ev);
        break;
      case MidiRouting.MidiChannelToInstance:
        // Rewrite the channel nibble to 0; only the single target sees the (freshly allocated) copy.
        inboxes[chan % systemCount].push({ frame: ev.frame, data: [status & 0xf0, ...data.slice(1)] });
        break;
      default:
        break; // modes are clamped 0..3 upstream; an unknown one drops
    }
  }
}
