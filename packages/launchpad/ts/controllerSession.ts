// A control surface's session (what the DSP kernel's `launchpad` role ran until B3b, as a script node): a
// ControllerSession (ts/session.ts) fed the surface's traffic and the block's clock each block, its LED bytes back to
// the device on `controllerOut`, its row launches onward: into the target system's lsdj-sync node on `launches`, or
// out of the host to a real Game Boy's Arduinoboy on `midiOut`, each at frame 0.
//
// Its settings are what the control plane resolved: the app's id, where its launches go and the system id they go to
// (the first system's when the user chose none), the app's config ALREADY PARSED by the app's own schema (the shell
// registry's, which this node never imports, so it carries no zod), the song's per-row timing table, and the start
// anchor. It parses nothing: the node imports only the apps' runtime (ts/apps/index.ts).
//
// The role's re-push rules, kept: a settings change whose shape (the app, the target, the system, the app's config) is
// the same adopts the new timing table in place, so the playhead does not move; any other rebuilds the session around
// the same predictor, so the song does not restart, and re-takes the device; an anchor is applied once per sequence
// number. As the role did, the work happens in the first process after the change.
//
// A block allocates nothing here beyond what the session does (its surface diff, and a launch's message).
import { control, defineNode, events, startValue, type Block } from "@retroplug/core/defineNode";
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { LAUNCHES, MIDI_OUT } from "@retroplug/core/ports";
import { CONTROLLER_TARGET_VALUES } from "./enums";
import { CONTROLLER_OUT } from "./ports";
import { ControllerSession } from "./session";
import { lsdjMidiMapTarget } from "./trackerTarget";
import { controllerApp } from "./apps";
// The leaf, not the lsdj/playback barrel: that also exports the observed model and the sav-derived table builder,
// which would pull the WRAM reader and the whole sav codec into the bundle.
import { PredictedLsdjModel, normaliseRowTicks } from "@retroplug/lsdj/playback/predict";

export const CONTROLLER_SESSION_NODE_TYPE = "rp.controller.session";

/** 24 PPQN, the resolution every Arduinoboy-family sync mode counts in. The session's tick is derived from ppq rather
 *  than counted: Math.floor(ppq * 24) is the quantity walkTicks derives its ticks from, so the session's clock and the
 *  cart's cannot drift apart, and a stopped transport freezes both. */
const TICKS_PER_QUARTER = 24;

const settings = s.object({
  /** Which controller app runs (an id in apps/index.ts); an unknown one runs nothing. */
  app: s.string("lsdj-midimap"),
  target: s.enum(CONTROLLER_TARGET_VALUES, "system"),
  /** The system the launches are for, resolved: 0 is none. Where they go is the wiring's; the session is rebuilt
   *  when it changes, as it was for a new target. */
  systemId: s.int({ min: 0, max: 0xffffffff, default: 0 }),
  /** The app's own config, parsed by its schema on the control plane. */
  appConfig: s.json<Record<string, unknown>>({}),
  /** The per-row timing table (normaliseRowTicks reads it): the predictor's only input. */
  songRowTicks: s.json<unknown>([]),
  /** The start-edge re-anchor: where the cart said it was when the player started it themselves, and a sequence
   *  number so one anchor is applied once. Empty and 0 on the real-hardware path. */
  anchorRows: s.json<(number | null)[]>([]),
  anchorSeq: s.int({ min: 0, max: 0x7fffffff, default: 0 }),
});

export type ControllerSessionSettings = Infer<typeof settings>;

/** The settings that need a NEW session rather than an in-place update: which app runs, where its launches go, and
 *  the app's own knobs (an app reads some of those once into its state, so swapping the config underneath it would
 *  leave half the change applied). */
const sessionShape = (c: ControllerSessionSettings): string => JSON.stringify([c.app, c.target, c.systemId, c.appConfig]);

/** What the session keeps between blocks, as the role kept it in its scratch bag. */
interface SessionState {
  /** Undefined until the first block builds one; null when the settings name no app. */
  session?: ControllerSession | null;
  /** The link generation the previous block saw (0 = none): the edge that takes the device, and the swap that
   *  re-takes it. */
  attached?: number;
  /** The link's drop count as of the previous block: see the repaint below. */
  drops?: number;
  /** The predictor, held here as well as inside the session so a rebuild carries it over. */
  model?: PredictedLsdjModel;
  /** The timing table the model holds, by identity: settings arrive as a fresh object on every change. */
  table?: unknown;
  /** The shape the current session was built from. */
  shape?: string;
  /** The anchor sequence applied last. */
  anchorSeq?: number;
  /** The end of the previous block on the transport's tick, as a float: its start plus the ticks its own frames
   *  and tempo covered (see the relocate below). */
  nextTick?: number;
}

export const controllerSessionNode = defineNode<ControllerSessionSettings>({
  id: CONTROLLER_SESSION_NODE_TYPE,
  name: "Controller Session",
  category: "controller",
  settings,
  reshapes: true,
  ports: {
    controllerIn: events.in(),
    controllerConnected: control.in(),
    controllerDrops: control.in(),
    controllerOut: events.out(CONTROLLER_OUT),
    launches: events.out(LAUNCHES),
    midiOut: events.out(MIDI_OUT),
  },
  create: ({ settings: initial }) => {
    let config = initial;
    // The shape the current session was built from, computed once per push: a settings push builds a new object,
    // so stringifying per block would allocate every block, and scale with whatever appConfig a .rplg carries.
    let shape = sessionShape(initial);
    const st: SessionState = {};
    let block: Block | null = null; // the block being processed, which a launch is sent on
    // This block's surface messages, as the views the block handed over: the session decodes them and keeps none.
    // They are JS-owned copies the script host refills each block, so what this holds after process returns is
    // retained, not dangling; it is read only inside update, and emptied before the next update reads it.
    const input: ArrayLike<number>[] = [];
    // The session's whole input for a block, one object refilled each block so none is built per block.
    const frame = { input, tick: 0, transport: false, relocate: false };
    const toLaunches = (data: number[]) => {
      block!.send("launches", 0, data);
    };
    const toMidiOut = (data: number[]) => {
      block!.send("midiOut", 0, data);
    };

    // The session for these settings, or null when they name no app. The ONE difference between driving an emulated
    // cart and driving a real Game Boy is where `send` puts the bytes, so both carry the same launch stream.
    const build = (model: PredictedLsdjModel): ControllerSession | null => {
      const app = controllerApp(config.app);
      if (!app) return null;
      const send = config.target === "midiOut" ? toMidiOut : toLaunches;
      return new ControllerSession(app, { playback: model, target: lsdjMidiMapTarget(send), config: config.appConfig });
    };

    return {
      process(b) {
        block = b;
        // A re-push. A new timing table is adopted IN PLACE, so a chain added to row 12 lights up without the playhead
        // moving; anything structural rebuilds the session, carrying the predictor across so the song does not
        // restart.
        if (st.session === undefined || st.shape !== shape) {
          if (st.model) st.model.setRowTicks(config.songRowTicks); // carried over: the table may have moved too
          else st.model = PredictedLsdjModel.fromRowTicks(normaliseRowTicks(config.songRowTicks));
          st.session = build(st.model);
          st.shape = shape;
          st.table = config.songRowTicks;
          st.attached = 0; // a fresh surface has no idea what the device is showing: re-take it below
        } else if (st.table !== config.songRowTicks) {
          st.model?.setRowTicks(config.songRowTicks);
          st.table = config.songRowTicks;
        }

        // Re-anchor when the control plane saw the cart start playing on its own, once per sequence number.
        const seq = config.anchorSeq ?? 0;
        if (seq !== st.anchorSeq) {
          st.anchorSeq = seq;
          if (seq > 0 && Array.isArray(config.anchorRows)) st.model?.anchorTo(config.anchorRows);
        }

        const session = st.session;
        if (!session) return; // an unknown app id: nothing to run

        // Take the DEVICE on the block its link first reports one, and again whenever the link's generation changes:
        // a pair swapped while connected releases the old device and claims the new one inside a single UI-thread
        // call, so no block sees "detached" in between. A device boots into Live mode, so Programmer mode is entered
        // every time one appears, and connect() invalidates the shadow buffer so the surface repaints. A host that
        // sends only the flag (no link number) still works: it reads as generation 1.
        const attached = startValue(b.control("controllerConnected"));
        if (attached !== 0 && attached !== st.attached) {
          const hello = session.connect();
          for (let i = 0; i < hello.length; i++) b.send("controllerOut", 0, hello[i]);
        }
        st.attached = attached;

        // REPAINT after a drop. The link's output ring is small and is emptied once a frame, so a burst of LED
        // changes (or a stalled UI thread) can overflow it - and the surface's diff has already marked those LEDs
        // as sent, so they would stay wrong until each pad next changed. A full repaint is one bulk message.
        const drops = startValue(b.control("controllerDrops"));
        if (attached !== 0 && st.drops !== undefined && drops !== st.drops) session.surface.invalidate();
        st.drops = drops;

        // Run even with nothing attached: update drives the predictor, so the LEDs are right the moment a device
        // appears. A surface's timing is not musical (the app quantises a press), so the frames are dropped.
        //
        // A tick that jumps further than one block can cover is a RELOCATE (the DAW timeline clicked forward, an
        // SPP), not elapsed time: walkTicks sends the cart no clocks for a jump either, so advancing the predictor
        // through it would be wrong - and walking a thousand bars of rows in one block costs milliseconds.
        //
        // The yardstick is the PREVIOUS block's projected end, from its own frames and tempo, exactly as walkTicks
        // resyncs when its carried tick falls more than one tick behind the block's start. Comparing against THIS
        // block's length instead reads a ragged split (a long block then a short one) or a tempo drop between two
        // long blocks as a seek, and drops an ordinary block's ticks: the playhead then sits behind the cart for
        // the rest of the song. A backwards move is no relocate here; the session reads it as no elapsed time.
        const tick = Math.floor(b.transport.ppq * TICKS_PER_QUARTER);
        const perBlock = b.transport.sampleRate > 0
          ? (b.frames / b.transport.sampleRate) * (b.transport.tempo / 60) * TICKS_PER_QUARTER : 0;
        const relocate = st.nextTick !== undefined && b.transport.ppq * TICKS_PER_QUARTER > st.nextTick + 1;
        st.nextTick = b.transport.ppq * TICKS_PER_QUARTER + perBlock;

        const messages = b.messages("controllerIn");
        input.length = 0;
        for (let i = 0; i < messages.length; i++) input.push(messages[i].data);
        frame.tick = tick;
        frame.transport = b.transport.playing;
        frame.relocate = relocate;
        const out = session.update(frame);
        for (let i = 0; i < out.length; i++) b.send("controllerOut", 0, out[i]);
      },
      onSettings(next) {
        config = next;
        shape = sessionShape(next);
      },
    };
  },
});
