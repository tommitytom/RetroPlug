// The controller session: the thing that owns a Launchpad and runs a controller app against it.
//
// A controller app is a plain function over a context - not an object of callbacks. Decoding incoming
// bytes happens once here rather than in every app, LED flushing likewise, and an app's cross-update
// state lives in `ctx.state`. An app therefore reads as "given what the player pressed and where the
// song is, paint the grid".
//
// Three things the session owns so no app has to get them right:
//
//   1. It DRIVES the predictor. On the real-Game-Boy path nothing tells us where the song is, so the
//      model has to be fed the same clock we are generating (docs/launchpad-plan.md 4.2).
//   2. It MIRRORS launches into that predictor, so the prediction and the wire cannot disagree - the same
//      by-construction property buildGraph's fan-out gives the emulated cart and an N8 playing the same bytes.
//   3. It restores LIVE MODE on disconnect. The manual is emphatic: while Programmer mode is set over
//      SysEx the device's own Settings menu is locked out, so a host that skips this leaves the user's
//      hardware in a state they cannot escape from the front panel.
//
// Pure: no I/O, no native, no RetroPlug backend. It takes bytes in and hands bytes back, which is what
// lets the whole feature be tested against a fake device before any of it can reach real hardware.

import {
  PRO_MK3, Surface, decodeMessage, enterProgrammerMode, exitToLiveMode,
  type LaunchpadProfile, type SurfaceEvent,
} from "./index";
import type { PlaybackModel, PredictivePlaybackModel } from "@retroplug/tracker/playbackModel";
import type { TrackerTarget } from "./launchTarget";

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
  /** Persistent scratch, scoped to this session: the contract a DSP role's `ctx.state` had under the kernel. */
  readonly state: Record<string, unknown>;
}

export type ControllerApp = (ctx: ControllerCtx) => void;

/** The session's own view of the ctx: the same object an app sees, minus the readonly promise, so the
 *  fields that change per update can be written in place. */
type MutableCtx = { -readonly [K in keyof ControllerCtx]: ControllerCtx[K] };

/** One update's worth of host input.
 *
 *  `tick` is ABSOLUTE - `Math.floor(block.ppqStart * 24)`, the quantity `walkTicks` derives its ticks from - and is what
 *  the app paints the playhead at, so a seek moves it and a stopped transport freezes it.
 *
 *  `ticks` is the number of ticks the HOST clocked the cart in the PREVIOUS block, counted by the host's own walk of the
 *  transport (`packages/core/ts/ticks.ts`) rather than inferred from two positions. Inferred, a host loop wrap, a
 *  backward seek made while stopped, or a device taken again would drop a whole block's ticks - the session read a jump
 *  backwards as no elapsed time and a first block as no time at all - and the predicted playhead would sit behind the
 *  cart for the rest of the song. Counted, every tick the cart heard is one the predictor walks, exactly once. */
export interface SessionInput {
  /** Raw MIDI messages received from the device since the previous update: any arrays of bytes, none of them
   *  kept past the update. */
  input: readonly ArrayLike<number>[];
  tick: number;
  transport: boolean;
  /** Ticks the host clocked in the previous block; 0 while it was stopped, and 0 on the first update. */
  ticks: number;
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

  private prevTransport = false;

  // The ctx and the decoded-event list are built ONCE and mutated per update: this runs on the audio
  // thread (rp.controller.session), and a fresh nine-field object plus an array every block is exactly
  // the per-block allocation a script node is careful not to make.
  private readonly events: SurfaceEvent[] = [];
  private readonly ctx: MutableCtx;

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

    this.ctx = {
      surface: this.surface,
      events: this.events,
      playback: this.playback,
      target: this.target,
      tick: 0,
      ticks: 0,
      transport: false,
      config: this.config,
      state: this.state,
    };
  }

  /** Take the device: enter Programmer mode and force a full repaint.
   *
   *  Both halves matter. The device ALWAYS boots into Live mode, so this has to be sent on every connect
   *  rather than assumed; and entering Programmer mode blanks the surface, which makes our diffing
   *  baseline a lie - without the invalidate the next flush would send nothing and leave the grid dark. */
  connect(): number[][] {
    this.surface.invalidate();
    return [enterProgrammerMode(this.profile)];
  }

  /** Run one update and return the messages to write to the device. Empty when nothing changed, which is
   *  the steady state: an app repainting an unchanged grid produces no MIDI at all. */
  update(input: SessionInput): number[][] {
    const events = this.events;
    events.length = 0;
    for (let i = 0; i < input.input.length; i++) {
      const ev = decodeMessage(this.profile, input.input[i]);
      if (ev) events.push(ev);
    }

    // The host counted what it clocked, so there is nothing to infer here and nothing to gate: a stopped host passes
    // 0 because its walk fired nothing. The predictor must still be STOPPED when the transport falls, or the playhead
    // would stand still lit where the cart stopped.
    const ticks = input.ticks > 0 ? Math.floor(input.ticks) : 0;

    if (this.predictive) {
      // The ticks FIRST, then the stop: this block is the last one that was playing, so its ticks are the cart's last
      // ones, and a predictor stopped before them would stand where the cart was a block earlier.
      if (ticks > 0) this.predictive.advance(ticks);
      if (this.prevTransport && !input.transport) this.predictive.stop();
    }

    const ctx = this.ctx;
    ctx.tick = input.tick;
    ctx.ticks = ticks;
    ctx.transport = input.transport;
    this.app(ctx);

    this.prevTransport = input.transport;
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
