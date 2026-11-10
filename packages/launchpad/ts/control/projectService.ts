// The controller's control-plane half, as a project service (docs/modular-graph-phase-f5a-design.md section 6.2): the
// poll of the cart it drives, the model the graph places for it, and what the menu reads of the poll. Nothing about it
// is the shell's: the contract it implements is @retroplug/tracker/projectService's, and the values it reads come from
// the context that contract gives it.
//
// One INSTANCE per project store: the shell builds one from this factory per store it makes, because a plugin host runs
// one control plane per instance, so a service's state (the song signature, the anchor, the polled SYNC, the edge
// detector, the reader cache) is its store's and two stores share nothing.
import { ROLE_HEADER_LEN } from "@retroplug/core/systemRoles";
import type { ControllerAnchor, PlaybackReader, TrackerPlayback } from "@retroplug/tracker/trackerPlayback";
import type {
  ProjectService,
  ProjectServiceContext,
  ProjectServiceFactory,
} from "@retroplug/tracker/projectService";
import { parseAppConfig } from "../registry";
import { type ControllerSection, CONTROLLER_SECTION_KEY } from "./projectSection";
import type { GraphController } from "./placer";

/** One system as the service sees it (the context's own shape). */
type ServiceSystem = ReturnType<ProjectServiceContext["systems"]>[number];

/** What the UI may read of the controller: the cart's SYNC as last polled, and where it was seen starting on its own.
 *  Read through `host.project.serviceState("controller")`. */
export interface ControllerServiceState {
  readonly cartSync: string | null;
  readonly anchor: ControllerAnchor | null;
}

/** One store's controller state and behaviour. See ProjectStore's history for why each field is what it is; the poll's
 *  contract is the shell's song watch (every ~30 frames, editor-driven). */
class ControllerService implements ProjectService {
  /** The live song's signature at the last poll: a change re-publishes the graph, which is how a row edited inside the
   *  cart reaches the grid. */
  private songSig = 0;
  /** The cart's own SYNC setting at the last poll. */
  private sync: string | null = null;
  /** Where the cart last started playback on its own. */
  private anchor: ControllerAnchor | null = null;
  /** The playing edge detector the anchor is sampled on. */
  private wasPlaying = false;
  /** The ROM path the cached reader was built from, and the reader itself. */
  private readerPath = "";
  private reader: PlaybackReader | null = null;

  /** Notice an edit the player made INSIDE the tracker and hand the graph a new model, so a control surface's grid
   *  follows the song. Polled rather than pushed because a cart being edited on its own screen emits nothing. */
  poll(ctx: ProjectServiceContext): boolean {
    const section = ctx.section<ControllerSection>();
    if (!section.enabled) return false;
    const id = this.targetId(ctx, section);
    if (id <= 0) return false;
    // The cart's own playback answers it; a system whose integration has none reads "no sync mode, no signature", which
    // is what the poll did before for anything that was not a tracker cart.
    const sys = ctx.systems().find((s) => s.id === id);
    const playback = sys ? ctx.playbackOf(sys.roles) : null;
    const sram = ctx.readSram(id);
    // One read, two answers. The sync mode is recorded EVERY poll (the player can change it without touching the song),
    // while the re-push is gated on the song actually having moved.
    this.sync = playback?.syncMode(sram) ?? null;
    const anchored = this.sampleAnchor(ctx, sys, playback);
    const sig = playback?.songSignature(sram) ?? 0;
    const songMoved = sig !== this.songSig;
    this.songSig = sig;
    return songMoved || anchored;
  }

  /** The controller as the graph places it, its app config parsed by the app's schema and the derived song-timing table
   *  the predictor runs on, or null with no controller enabled.
   *
   *  The table is built HERE rather than in the node because decoding a battery is a control-plane job - the audio
   *  thread has neither the bytes nor any business running the sav codec. It is rebuilt on every publish and is never
   *  persisted.
   *
   *  KNOWN STALENESS: edits the player makes inside the cart itself publish nothing, so the table lags the cart until
   *  the poll notices the song moved, or something else publishes (docs/launchpad-plan.md risk 5). */
  graphModel(ctx: ProjectServiceContext): GraphController | null {
    const section = ctx.section<ControllerSection>();
    if (!section.enabled) return null;
    const id = this.targetId(ctx, section);
    const sys = ctx.systems().find((s) => s.id === id);
    // The battery is read only when a tracker actually predicts rows from it: a read is an RPC, and a system whose
    // roles run no playback has nothing to say from it.
    const playback = sys ? ctx.playbackOf(sys.roles) : null;
    const sram = playback && id > 0 ? ctx.readSram(id) : null;
    const table = playback ? (playback.rowTicks(sram) ?? null) : null;
    // No readable battery is not a failure: the app still launches rows, it just cannot shade the ones that hold
    // chains. An empty table reads as "no content anywhere".
    return {
      app: section.app,
      target: section.target,
      systemId: section.systemId,
      appConfig: parseAppConfig(section.app, section.appConfig),
      songRowTicks: table ?? [],
      anchor: this.anchor,
      cartSync: this.sync,
    };
  }

  /** The cart's SYNC as last polled, and the start-edge anchor: what the menu's cart-sync row reads. */
  state(): ControllerServiceState {
    return { cartSync: this.sync, anchor: this.anchor };
  }

  /** The system the section names: its own, or the first ("0 is the first system"). */
  private targetId(ctx: ProjectServiceContext, section: ControllerSection): number {
    return section.systemId > 0 ? section.systemId : (ctx.systems()[0]?.id ?? 0);
  }

  /** Watch for the cart starting playback on its OWN - the player pressing START on LSDj's song screen - and capture
   *  where it actually started, so the predictor can be corrected.
   *
   *  Only the not-playing -> playing EDGE, which is what makes this affordable on the structure the graph already
   *  carries: a start is rare and discrete, where streaming the position every block would not be. Returns whether a
   *  new anchor was captured. Emulated carts only - a real Game Boy has no WRAM to read, and dead reckoning stays all
   *  there is. */
  private sampleAnchor(
    ctx: ProjectServiceContext,
    sys: ServiceSystem | undefined,
    playback: TrackerPlayback | null,
  ): boolean {
    if (!sys) return false;
    const reader = this.readerFor(ctx, sys, playback);
    if (!reader || !playback) return false;
    const wram = ctx.readRam(sys.id);
    if (!wram) return false;
    const state = reader.read(wram);
    const playing = playback.anchor.playing(state);
    const started = playing && !this.wasPlaying;
    this.wasPlaying = playing;
    if (!started) return false;
    this.anchor = { rows: playback.anchor.rows(state), seq: (this.anchor?.seq ?? 0) + 1 };
    return true;
  }

  /** The WRAM reader for the cart, built once per ROM. Null for a system whose integration has no playback, an embedded
   *  ROM (nothing to read a header from), or a build its playback has no reader for. */
  private readerFor(
    ctx: ProjectServiceContext,
    sys: ServiceSystem,
    playback: TrackerPlayback | null,
  ): PlaybackReader | null {
    const romPath = sys.romPath ?? "";
    if (!playback || !romPath) return null;
    if (this.readerPath !== romPath) {
      this.readerPath = romPath;
      const header = ctx.readFilePrefix(romPath, ROLE_HEADER_LEN);
      this.reader = header ? playback.anchor.reader(header) : null;
    }
    return this.reader;
  }
}

/** The controller, as a project service the shell builds per store. */
export const controllerService: ProjectServiceFactory = {
  key: CONTROLLER_SECTION_KEY,
  create: () => new ControllerService(),
};
