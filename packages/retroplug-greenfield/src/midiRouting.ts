// The MIDI routing decision — a pure port of native's Project::dispatchMidi
// (packages/native/src/project/Project.hpp). Given a block's MIDI events + the project's
// routing mode + system count, it decides which system(s) each event lands on and
// applies the MidiChannelToInstance channel rewrite. No emulator, no audio thread, no
// retained state — pure decision logic (doc-06 Tier-1). This is the canonical routing
// the greenfield app will eventually run; today it's the reference / conformance oracle
// (the live per-block execution waits on the doc-04 scriptable runtime + RT-safety).

/** Events with more than this many bytes are SysEx (native rides them on `dataExt`). */
export const MIDI_DATA_SIZE = 4;

/** Routing mode — mirrors native `MidiRouting` and `ProjectSettings.midiRouting` (0..3,
 *  clamped upstream). */
export enum MidiRouting {
  SendToAll = 0, // broadcast every event to every system; channel preserved
  FourChannelsPerInstance = 1, // instance N receives channels 4N..4N+3
  OneChannelPerInstance = 2, // instance N receives only channel N
  MidiChannelToInstance = 3, // like OneChannelPerInstance, but the channel is rewritten to 0
}

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
