// A control surface's session (what the DSP kernel's `launchpad` role ran until B3b, as a script node): a
// ControllerSession (src/controller/session.ts) fed the surface's traffic and the block's clock each block, its LED
// bytes back to the device on `controllerOut`, its row launches onward: into the target system's lsdj-sync node on
// `launches`, or out of the host to a real Game Boy's Arduinoboy on `midiOut`, each at frame 0.
//
// Its settings are what the control plane resolved: the app's id, where its launches go and the system id they go
// to (the first system's when the user chose none), the app's config ALREADY PARSED by the app's own schema (the
// registry's, which this node never imports, so it carries no zod), the song's per-row timing table, and the start
// anchor. It parses nothing: the node imports only the apps' runtime (controller/apps/index.ts).
//
// The role's re-push rules, kept: a settings change whose shape (the app, the target, the system, the app's config)
// is the same adopts the new timing table in place, so the playhead does not move; any other rebuilds the session
// around the same predictor, so the song does not restart, and re-takes the device; an anchor is applied once per
// sequence number. As the role did, the work happens in the first process after the change.
//
// A block allocates nothing here beyond what the session does (its surface diff, and a launch's message).
import { control, defineNode, events, startValue, type Block } from "../../graph/defineNode";
import { s, type Infer } from "../../graph/settingsSchema";
import { CONTROLLER_TARGET_VALUES } from "../../settingsEnums";
import { ControllerSession } from "../../controller/session";
import { lsdjMidiMapTarget } from "../../controller/trackerTarget";
import { controllerApp } from "../../controller/apps";
// The leaf, not the lsdj/playback barrel: that also exports the observed model and the sav-derived table builder,
// which would pull the WRAM reader and the whole sav codec into the bundle.
import { PredictedLsdjModel, normaliseRowTicks } from "../../lsdj/playback/predict";
import { CONTROLLER_OUT, LAUNCHES, MIDI_OUT } from "./ports";

export const CONTROLLER_SESSION_NODE_TYPE = "rp.controller.session";

/** 24 PPQN, the resolution every Arduinoboy-family sync mode counts in. The session's tick is derived from ppq rather
 *  than counted: Math.floor(ppq * 24) is the quantity walkTicks derives its ticks from, so the session's clock and the
 *  cart's cannot drift apart, and a stopped transport freezes both. */
const TICKS_PER_QUARTER = 24;

const settings = s.object({
  /** Which controller app runs (an id in controller/apps/index.ts); an unknown one runs nothing. */
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
const shapeOf = (c: ControllerSessionSettings): string => JSON.stringify([c.app, c.target, c.systemId, c.appConfig]);

/** What the session keeps between blocks, as the role kept it in its scratch bag. */
interface SessionState {
  /** Undefined until the first block builds one; null when the settings name no app. */
  session?: ControllerSession | null;
  /** Whether a device was attached on the previous block: the edge that takes it. */
  attached?: boolean;
  /** The predictor, held here as well as inside the session so a rebuild carries it over. */
  model?: PredictedLsdjModel;
  /** The timing table the model holds, by identity: settings arrive as a fresh object on every change. */
  table?: unknown;
  shape?: string;
  /** The last anchor sequence applied. */
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
    controllerOut: events.out(CONTROLLER_OUT),
    launches: events.out(LAUNCHES),
    midiOut: events.out(MIDI_OUT),
  },
  create: ({ settings: initial }) => {
    let config = initial;
    let shape = shapeOf(initial);
    const st: SessionState = {};
    let block: Block | null = null; // the block being processed, which a launch is sent on
    // This block's surface messages, as the views the block handed over: the session decodes them and keeps none.
    // They are JS-owned copies the script host refills each block, so what this holds after process returns is
    // retained, not dangling; it is read only inside update, and emptied before the next update reads it.
    const input: Uint8Array[] = [];
    // The session's whole input for a block, one object refilled each block so none is built per block.
    const frame = { input, tick: 0, transport: false };
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
          st.attached = false; // a fresh surface has no idea what the device is showing: re-take it below
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

        // Take the device on the block a link first reports one: a device boots into Live mode, so Programmer mode
        // is entered every time one appears, and connect() invalidates the shadow buffer so the surface repaints.
        const attached = startValue(b.control("controllerConnected")) !== 0;
        if (attached && !st.attached) {
          const hello = session.connect();
          for (let i = 0; i < hello.length; i++) b.send("controllerOut", 0, hello[i]);
        }
        st.attached = attached;

        // Run even with nothing attached: update drives the predictor, so the LEDs are right the moment a device
        // appears. A surface's timing is not musical (the app quantises a press), so the frames are dropped.
        const messages = b.messages("controllerIn");
        input.length = 0;
        for (let i = 0; i < messages.length; i++) input.push(messages[i].data);
        frame.tick = Math.floor(b.transport.ppq * TICKS_PER_QUARTER);
        frame.transport = b.transport.playing;
        const out = session.update(frame);
        for (let i = 0; i < out.length; i++) b.send("controllerOut", 0, out[i]);
      },
      onSettings(next) {
        config = next;
        shape = shapeOf(next);
      },
    };
  },
});
