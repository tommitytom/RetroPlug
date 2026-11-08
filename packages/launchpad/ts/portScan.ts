// Find which MIDI port PAIR a device is on, by writing a probe out of each output in turn and listening on
// every input for what comes back.
//
// Device-agnostic: the probe is whatever bytes the caller hands in (a Universal Device Inquiry, for a
// Launchpad) and the replies come back raw, for the caller to recognise. Host-agnostic too: it runs over an
// injected `MidiPortApi` and a clock it is TOLD, never one it reads, so the whole thing is a deterministic
// state machine a test can drive a millisecond at a time. The standalone steps it from a frame poll.
//
// Why a probe at all, rather than watching for a device to announce itself: a Launchpad on TRS/DIN arrives
// through an ordinary MIDI interface, and DIN has no enumeration and no idle heartbeat - the OS sees the
// interface and nothing else. The inquiry is the only identification MIDI 1.0 has, and it is also the only
// way to learn the PAIRING, since the reply tells you which input belongs to the output you wrote to - which
// may be a different interface entirely. Hence user-initiated: never on a timer, never at startup.

/** Single-direction MIDI ports by handle - what a host provides (the standalone's facet calls). */
export interface MidiPortApi {
  /** Open every input at once, a handle each (0 for one that will not open - another application holds
   *  it). One call rather than one per port, because a host may have to step its own MIDI stream aside to
   *  let an input go, and that is a cost to pay once per scan, not once per refusing port. */
  openInputs(names: readonly string[]): number[];
  /** A handle > 0, or 0 when the port will not open. */
  openOutput(name: string): number;
  send(handle: number, bytes: readonly number[]): boolean;
  /** What an input received since the last read, oldest first. */
  read(handle: number): number[][];
  close(handle: number): void;
}

/** One message an input returned while a given output was being probed. */
export interface ScanReply {
  output: string; // the port the probe went out of
  input: string; // the port this came back on
  bytes: number[];
}

export interface ScanStatus {
  busy: boolean;
  done: boolean; // a scan finished (with or without replies)
  phase: string; // short label while running, e.g. "Probing MIDISPORT 2x2 Port A"
  error: string; // "" = ok
  skipped: number; // ports that would not open (in use elsewhere)
  version: number; // bumped on any change, so a poller re-renders only when it moves
  replies: ScanReply[];
}

export const IDLE_SCAN: ScanStatus = {
  busy: false, done: false, phase: "", error: "", skipped: 0, version: 0, replies: [],
};

/** Replies kept per scan. Only SysEx is kept (an inquiry answer is nothing else), so a keyboard or a fader
 *  riding through the scan cannot fill this; it only has to be deeper than the number of devices that answer
 *  a universal inquiry. */
export const MAX_REPLIES = 64;

/** The quiet between one output's answer window and the next probe. Anything arriving in it is dropped
 *  rather than credited to either side, so a device that answers late is lost instead of being blamed on the
 *  NEXT output - a reply credited to the wrong output records a pair whose LEDs go nowhere. */
export const GAP_MS = 50;

/** A question to ask on the SAME pair, the moment a reply arrives on it - while the ports are still open and
 *  the answer window is still running. How a scan confirms WHAT answered without a second stage: reopening
 *  the ports would cost a Windows host another round of stepping its MIDI stream aside, and a port another
 *  program grabs in between would make a real device look like it never confirmed. Return the messages to
 *  send (none for a reply that needs no follow-up); each distinct message goes out once per output. */
export type FollowUp = (reply: ScanReply) => readonly (readonly number[])[];

export interface PortScanOptions {
  followUp?: FollowUp;
}

export class PortScan {
  private readonly listeners: { name: string; handle: number }[] = [];
  private next = 0; // index into outputs of the next port to probe
  private probing: { name: string; handle: number; since: number } | null = null;
  private quietUntil = -Infinity; // the gap after a window closes: no probe goes out before this
  // The scan's OWN clock, advanced only by forward steps of the time it is told. A wall clock stepping
  // backwards (an NTP correction) would otherwise freeze every window open until it caught up - with every
  // input held and, on Windows, the host's MIDI stream stepped aside the whole time.
  private elapsed = 0;
  private lastNow: number | null = null;
  private readonly asked: { [key: string]: true } = {}; // follow-ups already sent, by output + bytes
  private readonly followUp: FollowUp | undefined;
  private st: ScanStatus;

  constructor(
    private readonly api: MidiPortApi,
    inputs: readonly string[],
    private readonly outputs: readonly string[],
    private readonly probe: readonly number[],
    private readonly windowMs: number,
    options: PortScanOptions = {},
  ) {
    this.followUp = options.followUp;
    this.st = { ...IDLE_SCAN, busy: true, phase: "Listening", version: 1, replies: [] };
    // Every input is opened ONCE and kept open for the whole scan, so a device is heard whichever output it
    // turns out to be paired with. A port that will not open is counted and stepped over: on Windows a MIDI
    // input is exclusive, so anything else already holding it shows up here rather than failing the scan.
    const handles = inputs.length > 0 ? api.openInputs(inputs) : [];
    for (let i = 0; i < inputs.length; i++) {
      const handle = handles[i] ?? 0;
      if (handle > 0) this.listeners.push({ name: inputs[i], handle });
      else this.st.skipped++;
    }
  }

  status(): ScanStatus {
    return this.st;
  }

  /** Advance to `nowMs`. Call often (each frame is plenty); returns whether the scan is still running. A
   *  throw from the port API ends the scan with the error recorded and every port closed, so a host never
   *  ends up holding inputs for a scan that can no longer move. */
  step(nowMs: number): boolean {
    if (!this.st.busy) return false;
    if (this.lastNow !== null && nowMs > this.lastNow) this.elapsed += nowMs - this.lastNow;
    this.lastNow = nowMs;
    try {
      this.advance(this.elapsed);
    } catch (e) {
      this.edit().error = e instanceof Error ? e.message : String(e);
      this.finish();
    }
    return this.st.busy;
  }

  /** Stop now, closing everything. Leaves the replies gathered so far. */
  cancel(): void {
    if (!this.st.busy) return;
    this.finish();
  }

  private advance(nowMs: number): void {
    // Read BEFORE moving on. Whatever has arrived so far came back while the current output was the only one
    // written to, so it is attributed to that output; arrivals outside a window are dropped.
    this.collect(nowMs);

    if (this.probing && nowMs - this.probing.since >= this.windowMs) {
      const handle = this.probing.handle;
      this.probing = null; // cleared first, so a close that throws is not closed again by finish()
      this.api.close(handle);
      this.quietUntil = nowMs + GAP_MS;
      this.edit();
    }
    if (!this.probing && nowMs >= this.quietUntil) this.probeNext(nowMs);
  }

  private collect(nowMs: number): void {
    for (const l of this.listeners) {
      const messages = this.api.read(l.handle);
      const probing = this.probing;
      if (!probing) continue; // drained either way, so a stale arrival cannot surface in a later window
      for (const bytes of messages) {
        // SysEx only. A Device Inquiry answer is nothing else, and everything else on a live rig - clock,
        // notes, a fader ride - would otherwise eat the reply cap before the device we are looking for has
        // been asked.
        if (bytes.length === 0 || bytes[0] !== 0xf0) continue;
        if (this.st.replies.length >= MAX_REPLIES) return;
        const reply = { output: probing.name, input: l.name, bytes };
        this.edit().replies.push(reply);
        this.askFollowUp(probing, reply, nowMs);
      }
    }
  }

  private askFollowUp(probing: { name: string; handle: number; since: number }, reply: ScanReply, nowMs: number): void {
    if (!this.followUp) return;
    for (const msg of this.followUp(reply)) {
      const key = probing.name + "|" + msg.join(",");
      if (this.asked[key]) continue; // the same device heard on several inputs is asked once
      this.asked[key] = true;
      this.api.send(probing.handle, msg);
      probing.since = nowMs; // the answer gets a full window of its own
    }
  }

  private probeNext(nowMs: number): void {
    while (this.next < this.outputs.length) {
      const name = this.outputs[this.next++];
      const handle = this.api.openOutput(name);
      if (handle <= 0) {
        this.edit().skipped++;
        continue;
      }
      this.probing = { name, handle, since: nowMs };
      this.edit().phase = `Probing ${name}`;
      if (this.probe.length > 0) this.api.send(handle, this.probe);
      return;
    }
    this.finish();
  }

  private finish(): void {
    const probing = this.probing;
    this.probing = null;
    const listeners = this.listeners.splice(0, this.listeners.length);
    const st = this.edit();
    st.busy = false;
    st.done = true;
    st.phase = "";
    // Closed last, and each on its own: one close that throws must not leave the rest open.
    if (probing) this.closeQuietly(probing.handle);
    for (const l of listeners) this.closeQuietly(l.handle);
  }

  private closeQuietly(handle: number): void {
    try {
      this.api.close(handle);
    } catch (e) {
      if (!this.st.error) this.edit().error = e instanceof Error ? e.message : String(e);
    }
  }

  /** A fresh status to write into. Copied BEFORE the change, so a consumer holding an earlier status (a React
   *  render) never sees it move underneath it. */
  private edit(): ScanStatus {
    this.st = { ...this.st, version: this.st.version + 1, replies: this.st.replies.slice() };
    return this.st;
  }
}
