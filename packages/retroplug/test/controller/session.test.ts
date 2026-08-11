// The session: does it hand an app the right inputs, drive the predictor honestly, and hand the device
// back in a usable state?
//
// The lifecycle half matters more than it looks. Entering Programmer mode blanks the device's surface, so
// a host whose diffing baseline says "already painted" would leave the grid dark; and leaving Programmer
// mode set locks the user out of their own hardware's Settings menu. The fake device models both, so
// these are real failures here rather than things discovered on a desk with a Launchpad on it.
import { test, expect } from "../../testing/harness";
import { FakeLaunchpad } from "./_fakeDevice";
import { ControllerSession, isPredictive, lsdjMidiMapTarget, type ControllerApp, type ControllerCtx } from "../../src/controller";
import { Palette, palette } from "../../src/launchpad";
import { SongSchema } from "../../src/lsdj/model";
import { PredictedLsdjModel } from "../../src/lsdj/playback";
import { idlePosition, type PlaybackModel } from "../../src/tracker/playbackModel";

const RED = { mode: "static", colour: palette(Palette.red) } as const;

/** A one-row, one-phrase song: 96 ticks per row, wrapping to itself. */
const song = () => SongSchema.parse({ formatVersion: 22, rows: [{ chains: [0, 0, 0, 0] }], chains: [{ phrases: [0] }] });

/** A read-only model that is never playing - the "observed" side of the seam, for tests about what the
 *  session does when it must NOT drive anything. */
const staticModel: PlaybackModel = {
  channelCount: 4,
  position: () => idlePosition(4),
  grid: () => ({ rowCount: 256, channelCount: 4, hasContent: () => false }),
};

interface Harness {
  device: FakeLaunchpad;
  session: ControllerSession;
  seen: ControllerCtx[];
  sent: number[][];
  connect(): void;
  run(opts?: { tick?: number; transport?: boolean; input?: number[][] }): void;
}

function harness(app: ControllerApp, playback: PlaybackModel = staticModel): Harness {
  const device = new FakeLaunchpad();
  const seen: ControllerCtx[] = [];
  const sent: number[][] = [];
  const session = new ControllerSession((c) => { seen.push({ ...c, events: [...c.events] }); app(c); }, {
    playback,
    target: lsdjMidiMapTarget((d) => sent.push(d)),
  });
  return {
    device, session, seen, sent,
    connect: () => device.write(session.connect()),
    run: (opts = {}) => device.write(session.update({
      input: opts.input ?? [],
      tick: opts.tick ?? 0,
      transport: opts.transport ?? false,
    })),
  };
}

const noop: ControllerApp = () => {};

// --- lifecycle -----------------------------------------------------------------------------------

test("connect puts the device into Programmer mode", () => {
  const h = harness(noop);
  expect(h.device.programmerMode).toBe(false); // a real device always boots into Live mode
  h.connect();
  expect(h.device.programmerMode).toBe(true);
});

test("the first update after connect repaints, even though Programmer mode blanked the surface", () => {
  // The trap this guards: the session's shadow buffer and the device agreed on "all off" before connect,
  // and entering Programmer mode blanked the device again - so a plain diff sends nothing and the grid
  // stays dark. connect() invalidates precisely so this cannot happen.
  const h = harness((c) => c.surface.setPad(0, 0, RED));
  h.connect();
  h.run();
  expect(h.device.pad(0, 0)).toEqual(RED);
});

test("an update that changes nothing writes nothing at all", () => {
  const h = harness((c) => c.surface.setPad(3, 4, RED));
  h.connect();
  h.run();
  h.device.clearWrites();

  h.run(); // the same declarative repaint, second time round
  expect(h.device.writes.length).toBe(0);
  expect(h.device.pad(3, 4)).toEqual(RED); // still lit - silence means "unchanged", not "cleared"
});

test("disconnect blanks the surface AND restores Live mode", () => {
  const h = harness((c) => { for (let x = 0; x < 8; x++) c.surface.setPad(x, 0, RED); });
  h.connect();
  h.run();
  expect(Object.keys(h.device.litPads()).length).toBe(8);

  h.device.write(h.session.disconnect());
  expect(h.device.litPads()).toEqual({}); // nothing left glowing
  expect(h.device.programmerMode).toBe(false); // or the user cannot reach their own Settings menu
});

// --- what the app is handed ----------------------------------------------------------------------

test("raw device bytes reach the app as decoded events", () => {
  const h = harness(noop);
  h.connect();
  h.run({ input: [h.device.press(2, 3), h.device.release(2, 3), [0xf8]] });

  const events = h.seen[0].events;
  expect(events.length).toBe(2); // the clock byte is not surface input
  expect(events[0].kind).toBe("down");
  expect(events[0].pad).toEqual({ x: 2, y: 3 });
  expect(events[1].kind).toBe("up");
});

test("events do not carry over into the next update", () => {
  const h = harness(noop);
  h.connect();
  h.run({ input: [h.device.press(0, 0)] });
  h.run();
  expect(h.seen[1].events.length).toBe(0);
});

// --- driving the predictor -----------------------------------------------------------------------

test("the predictor is advanced by the elapsed ticks, and by nothing while stopped", () => {
  const model = new PredictedLsdjModel(song());
  const h = harness(noop, model);
  expect(isPredictive(model)).toBe(true);

  h.connect();
  h.run({ tick: 1000, transport: true }); // first update establishes the baseline; no phantom 1000 ticks
  expect(h.seen[0].ticks).toBe(0);

  model.launch(0);
  h.run({ tick: 1050, transport: true });
  expect(h.seen[1].ticks).toBe(50);

  h.run({ tick: 1200, transport: false }); // a stopped host: ppq may move on a seek, the cart hears nothing
  expect(h.seen[2].ticks).toBe(0);
});

test("a transport fall stops the predictor rather than leaving a stale playhead lit", () => {
  const model = new PredictedLsdjModel(song());
  const h = harness(noop, model);
  h.connect();
  h.run({ tick: 0, transport: true });
  model.launch(0);
  expect(model.position().playing).toBe(true);

  h.run({ tick: 10, transport: false });
  expect(model.position().playing).toBe(false);
});

test("a launch through ctx.target reaches the cart and the predictor together", () => {
  // The by-construction property: there is no path that sends a row to the cart without telling the
  // model, so the LEDs cannot describe a position the cart was never sent to.
  const model = new PredictedLsdjModel(song());
  const h = harness((c) => { if (c.events.length) c.target.launch(0); }, model);
  h.connect();
  h.run({ input: [h.device.press(0, 0)], transport: true });

  expect(h.sent).toEqual([[0x90, 0, 100]]); // on the wire
  expect(model.position().channels[0].songRow).toBe(0); // and in the prediction
});

test("a read-only model is never driven, and the session does not care that it cannot be", () => {
  const h = harness((c) => { if (c.events.length) c.target.launch(4); }, staticModel);
  expect(isPredictive(staticModel)).toBe(false);
  h.connect();
  h.run({ input: [h.device.press(0, 0)], transport: true, tick: 500 });
  expect(h.sent).toEqual([[0x90, 4, 100]]); // the launch still goes out; only the mirroring is skipped
});

test("app state persists across updates", () => {
  const h = harness((c) => { c.state.n = ((c.state.n as number) ?? 0) + 1; });
  h.connect();
  h.run();
  h.run();
  h.run();
  expect(h.seen[2].state.n).toBe(3);
});

test("config reaches the app unchanged", () => {
  const device = new FakeLaunchpad();
  let seen: Record<string, unknown> | null = null;
  const session = new ControllerSession((c) => { seen = c.config; }, {
    playback: staticModel,
    target: lsdjMidiMapTarget(() => {}),
    config: { quantise: "beat", follow: false },
  });
  device.write(session.connect());
  device.write(session.update({ input: [], tick: 0, transport: false }));
  expect(seen).toEqual({ quantise: "beat", follow: false });
});
