// The controller session: the thing that owns a Launchpad and runs a controller app against it.
//
// A controller app is a plain function over a context, exactly like a DSP role's `SystemBehavior`
// (dspRoles.ts) - not an object of callbacks. Decoding incoming bytes happens once here rather than in
// every app, LED flushing likewise, and an app's cross-update state lives in `ctx.state` the way a role's
// does. An app therefore reads as "given what the player pressed and where the song is, paint the grid".
//
// Three things the session owns so no app has to get them right:
//
//   1. It DRIVES the predictor. On the real-Game-Boy path nothing tells us where the song is, so the
//      model has to be fed the same clock we are generating (docs/launchpad-plan.md 4.2).
//   2. It MIRRORS launches into that predictor, so the prediction and the wire cannot disagree - the same
//      by-construction property Engine::setCoreByteSink gives the risa/N8 mirror.
//   3. It restores LIVE MODE on disconnect. The manual is emphatic: while Programmer mode is set over
//      SysEx the device's own Settings menu is locked out, so a host that skips this leaves the user's
//      hardware in a state they cannot escape from the front panel.
//
// Pure: no I/O, no native, no RetroPlug backend. It takes bytes in and hands bytes back, which is what
// lets the whole feature be tested against a fake device before any of it can reach real hardware.

import {
  PRO_MK3, Surface, decodeMessages, enterProgrammerMode, exitToLiveMode,
  type LaunchpadProfile, type SurfaceEvent,
} from "../launchpad";
import type { PlaybackModel, PredictivePlaybackModel } from "../tracker/playbackModel";
import type { TrackerTarget } from "./trackerTarget";

/** What an app sees. Everything here is read-only except `surface` (paint it) and `state` (remember
 *  things in it) - an app cannot reach past this to the device, the model or the host. */
export interface ControllerCtx {
  /** Paint declaratively every update; the surface diffs, so repainting what did not change is free. */
  readonly surface: Surface;
  /** Decoded presses since the previous update, in order. */
  readonly events: readonly SurfaceEvent[];
  /** Where the song is. Observed or predicted - an app cannot tell, and must not care. */
  readonly playback: PlaybackModel;
  /** Where launches go. */
  readonly target: TrackerTarget;
  /** Absolute 24-PPQN tick at the start of this update. */
  readonly tick: number;
  /** Ticks elapsed since the previous update; 0 while stopped. */
  readonly ticks: number;
  readonly transport: boolean;
  readonly config: Record<string, unknown>;
  /** Persistent scratch, scoped to this session. Same contract as a DSP role's `ctx.state`. */
  readonly state: Record<string, unknown>;
}

export type ControllerApp = (ctx: ControllerCtx) => void;

/** One update's worth of host input.
 *
 *  `tick` is ABSOLUTE and the session diffs it, so elapsed ticks can be neither double-counted nor lost
 *  at a block edge - the same reason the DSP kernel's `eachTick` carries `nextTick` across blocks. A host
 *  supplies `Math.floor(block.ppqStart * 24)`, which is the quantity `walkTicks` derives its ticks from;
 *  a stopped transport freezes ppqStart, so `ticks` falls to 0 on its own. */
export interface SessionInput {
  /** Raw MIDI messages received from the device since the previous update. */
  input: readonly (readonly number[])[];
  tick: number;
  transport: boolean;
}

export interface SessionOptions {
  playback: PlaybackModel;
  target: TrackerTarget;
  profile?: LaunchpadProfile;
  config?: Record<string, unknown>;
}

/** True for a model that must be driven rather than read - the dead-reckoning side of the seam. A duck
 *  test rather than a flag, so `playbackModel.ts` needs no discriminator field that only exists for us. */
export function isPredictive(model: PlaybackModel): model is PredictivePlaybackModel {
  const m = model as Partial<PredictivePlaybackModel>;
  return typeof m.launch === "function" && typeof m.advance === "function";
}

export class ControllerSession {
  readonly surface: Surface;
  readonly profile: LaunchpadProfile;

  private readonly app: ControllerApp;
  private readonly playback: PlaybackModel;
  private readonly predictive: PredictivePlaybackModel | null;
  private readonly target: TrackerTarget;
  private readonly config: Record<string, unknown>;
  private readonly state: Record<string, unknown> = {};

  private lastTick = 0;
  private prevTransport = false;
  private started = false;

  constructor(app: ControllerApp, opts: SessionOptions) {
    this.app = app;
    this.profile = opts.profile ?? PRO_MK3;
    this.surface = new Surface(this.profile);
    this.playback = opts.playback;
    this.predictive = isPredictive(opts.playback) ? opts.playback : null;
    this.config = opts.config ?? {};

    // The app is handed a WRAPPED target: a launch reaches the cart and the predictor together, so the
    // two cannot drift apart through some path that forgot to tell the model.
    const real = opts.target;
    const predictive = this.predictive;
    this.target = {
      maxRow: real.maxRow,
      launch(row: number): void {
        real.launch(row);
        predictive?.launch(row); // release is not mirrored: 0xFE does nothing to playback (B5)
      },
      release(row: number): void {
        real.release(row);
      },
    };
  }

  /** Take the device: enter Programmer mode and force a full repaint.
   *
   *  Both halves matter. The device ALWAYS boots into Live mode, so this has to be sent on every connect
   *  rather than assumed; and entering Programmer mode blanks the surface, which makes our diffing
   *  baseline a lie - without the invalidate the next flush would send nothing and leave the grid dark. */
  connect(): number[][] {
    this.surface.invalidate();
    this.started = false;
    return [enterProgrammerMode(this.profile)];
  }

  /** Run one update and return the messages to write to the device. Empty when nothing changed, which is
   *  the steady state: an app repainting an unchanged grid produces no MIDI at all. */
  update(input: SessionInput): number[][] {
    const events = decodeMessages(this.profile, input.input);

    // Gate on transport rather than trusting the numbers: a host that seeks while stopped moves ppq
    // without any time passing, and the cart hears no clock for it either.
    const ticks = input.transport && this.started ? Math.max(0, input.tick - this.lastTick) : 0;

    if (this.predictive) {
      if (this.prevTransport && !input.transport) this.predictive.stop();
      if (ticks > 0) this.predictive.advance(ticks);
    }

    this.app({
      surface: this.surface,
      events,
      playback: this.playback,
      target: this.target,
      tick: input.tick,
      ticks,
      transport: input.transport,
      config: this.config,
      state: this.state,
    });

    this.lastTick = input.tick;
    this.prevTransport = input.transport;
    this.started = true;
    return this.surface.flush().messages;
  }

  /** Give the device back: blank the surface, then return it to Live mode so its front panel works
   *  again. Call this on disconnect AND on shutdown. */
  disconnect(): number[][] {
    this.surface.clear();
    const { messages } = this.surface.flush();
    return [...messages, exitToLiveMode(this.profile)];
  }
}
