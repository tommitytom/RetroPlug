// A control surface's session (what the DSP kernel's `launchpad` role ran until B3b, as a script node): a
// ControllerSession (ts/session.ts) fed the surface's traffic and the block's clock each block, its LED bytes back to
// the device on `controllerOut`, its row launches onward: into the target system's lsdj-sync node on `launches`, or
// out of the host to a real Game Boy's Arduinoboy on `midiOut`, each at frame 0.
//
// Its settings are what the control plane resolved: the app's id, where its launches go and the system id they go to
// (the first system's when the user chose none), the app's config ALREADY PARSED by the app's own schema (the shell's
// registry, which this node never imports, so it carries no zod), the song's per-row timing table, and the start
// anchor. It parses nothing: the app it runs is a runtime the generated bundle registered by id (ts/apps/index.ts), so
// a tracker's app lives with that tracker and this node names none.
//
// The role's re-push rules, kept: a settings change whose shape (the app, the target, the system, the app's config) is
// the same adopts the new timing table in place, so the playhead does not move; any other rebuilds the session around
// the same predictor, so the song does not restart, and re-takes the device; an anchor is applied once per sequence
// number. As the role did, the work happens in the first process after the change.
//
// A block allocates nothing here beyond what the session does (its surface diff, and a launch's message).
import { TickClock } from "@retroplug/core/clock";
import { control, defineNode, events, startValue, type Block } from "@retroplug/core/defineNode";
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { LAUNCHES, MIDI_OUT } from "@retroplug/core/ports";
import { CONTROLLER_TARGET_VALUES } from "./enums";
import { CONTROLLER_OUT } from "./ports";
import { ControllerSession } from "./session";
import { controllerApp, type ControllerAppRuntime, type ControllerPlaybackModel } from "./apps";

export const CONTROLLER_SESSION_NODE_TYPE = "rp.controller.session";

/** 24 PPQN, the resolution every Arduinoboy-family sync mode counts in. The position the app paints from is derived
 *  from ppq (Math.floor(ppq * 24), the quantity walkTicks derives its ticks from), while the ticks the predictor walks
 *  are COUNTED by the walk itself (the clock below): a position can only be differenced, and a loop wrap or a seek would
 *  lose the block it was differenced across. */
const TICKS_PER_QUARTER = 24;

const settings = s.object({
  /** Which controller app runs: an id a package's runtime registered (ts/apps/index.ts), which the control plane's
   *  project settings name (a project starts with the product's app). Empty or unknown runs nothing. */
  app: s.string(""),
  target: s.enum(CONTROLLER_TARGET_VALUES, "system"),
  /** The system the launches are for, resolved: 0 is none. Where they go is the wiring's; the session is rebuilt
   *  when it changes, as it was for a new target. */
  systemId: s.int({ min: 0, max: 0xffffffff, default: 0 }),
  /** The app's own config, parsed by its schema on the control plane. */
  appConfig: s.json<Record<string, unknown>>({}),
  /** The per-row timing table the app's playback model reads: the predictor's only input. */
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
  model?: ControllerPlaybackModel;
  /** The app the model was built for: a different app reads its own kind of model, so the model is rebuilt with it. */
  modelApp?: string;
  /** The timing table the model holds, by identity: settings arrive as a fresh object on every change. */
  table?: unknown;
  /** The shape the current session was built from. */
  shape?: string;
  /** The anchor sequence applied last. */
  anchorSeq?: number;
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
    const frame = { input, tick: 0, transport: false, ticks: 0 };
    // The clock the cart is sent, walked here so the predictor can be given the COUNT of ticks a block fired. It is the
    // walk the cart's own protocol node runs (packages/core/ts/ticks.ts), at 24 ticks a quarter. What it counts is what
    // MI.MAP would clock at divisor 1: an app's override sets the cart's sync MODE, never its tempoDivisor, and MI.MAP
    // clocks the cart at 24 / tempoDivisor, so at divisor 2 and above this walk runs that many times ahead of the cart -
    // the predicted playhead and the quantise with it. A cart in a mode that clocks only on a launch counts its own
    // either way. The callback only counts, and is made once so a block allocates nothing for it.
    const clock = new TickClock();
    let walked = 0;
    let counted = 0;
    const countTick = () => {
      counted++;
    };
    const toLaunches = (data: number[]) => {
      block!.send("launches", 0, data);
    };
    const toMidiOut = (data: number[]) => {
      block!.send("midiOut", 0, data);
    };

    // The session for these settings, or null when they name an app no package registered. The ONE difference between
    // driving an emulated cart and driving a real Game Boy is where `send` puts the bytes, so both carry the same
    // launch stream (the app's own target puts them there).
    const build = (app: ControllerAppRuntime, model: ControllerPlaybackModel): ControllerSession => {
      const send = config.target === "midiOut" ? toMidiOut : toLaunches;
      return new ControllerSession(app.factory(), {
        playback: model,
        target: app.target(send),
        config: config.appConfig,
      });
    };

    return {
      process(b) {
        block = b;
        // A re-push. A new timing table is adopted IN PLACE, so a chain added to row 12 lights up without the playhead
        // moving; anything structural rebuilds the session, carrying the predictor across so the song does not
        // restart.
        if (st.session === undefined || st.shape !== shape) {
          const app = controllerApp(config.app);
          // The model is carried over across a rebuild, so the song does not restart: it is the app's own, so a
          // different app builds its own (and the table may have moved too).
          if (!app) {
            st.session = null;
          } else if (st.model && st.modelApp === config.app) {
            st.model.setRowTicks(config.songRowTicks);
            st.session = build(app, st.model);
          } else {
            st.model = app.playbackModel(config.songRowTicks);
            st.modelApp = config.app;
            st.session = build(app, st.model);
          }
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
        // What the cart was CLOCKED in the previous block, not the distance between two positions: walkTicks fires no
        // clocks across a seek and resyncs from the block's own start, and a loop wrap moves ppq backwards - so a
        // difference would read each of those as no time (or lose a block at a device swap) and leave the predicted
        // playhead behind the cart for good. Counting the walk gives the predictor exactly the ticks the cart heard.
        clock.begin(b);
        counted = 0;
        clock.walk(TICKS_PER_QUARTER, countTick);
        const elapsed = walked;
        walked = counted;

        const messages = b.messages("controllerIn");
        input.length = 0;
        for (let i = 0; i < messages.length; i++) input.push(messages[i].data);
        frame.tick = Math.floor(b.transport.ppq * TICKS_PER_QUARTER);
        frame.transport = b.transport.playing;
        frame.ticks = elapsed;
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
