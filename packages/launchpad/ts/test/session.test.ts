// The session: does it hand an app the right inputs, drive the predictor honestly, and hand the device
// back in a usable state?
//
// The lifecycle half matters more than it looks. Entering Programmer mode blanks the device's surface, so
// a host whose diffing baseline says "already painted" would leave the grid dark; and leaving Programmer
// mode set locks the user out of their own hardware's Settings menu. The fake device models both, so
// these are real failures here rather than things discovered on a desk with a Launchpad on it.
import { test, expect } from "@retroplug/testing";
import { FakeLaunchpad } from "../testSupport/fakeDevice";
import { ControllerSession, isPredictive, type ControllerApp, type ControllerCtx } from "../session";
import { Palette, palette } from "../index";
import { idlePosition, type PlaybackModel, type PredictivePlaybackModel } from "@retroplug/tracker/playbackModel";

const RED = { mode: "static", colour: palette(Palette.red) } as const;

/** A one-row song at 96 ticks a row: the session's predictive side, without a tracker's model - this package names no
 *  tracker, and the model a real app reads is its own package's (lsdj-launchpad's predictor for MI.MAP). */
class FakePredictive implements PredictivePlaybackModel {
  readonly channelCount = 4;
  private readonly pos = idlePosition(4);
  private row: number | null = null;
  private left = 0;
  private advanced = 0;

  position(): import("@retroplug/tracker/playbackModel").PlaybackPosition {
    return this.pos;
  }

  grid() {
    return { rowCount: 1, channelCount: 4, hasContent: () => true };
  }

  launch(row: number): void {
    this.row = row;
    this.left = 96;
    for (let i = 0; i < 4; i++) {
      this.pos.channels[i].playing = true;
      this.pos.channels[i].songRow = row;
    }
    this.pos.playing = true;
  }

  advance(ticks: number): void {
    if (this.row === null) return;
    this.advanced += ticks;
    this.left -= ticks;
    while (this.left <= 0) {
      this.row = (this.row + 1) % 1; // one row: it wraps to itself
      this.left += 96;
      for (let i = 0; i < 4; i++) this.pos.channels[i].songRow = this.row;
    }
  }

  stop(): void {
    this.row = null;
    for (let i = 0; i < 4; i++) this.pos.channels[i].playing = false;
    this.pos.playing = false;
  }

  reset(): void {
    this.stop();
  }

  ticksAdvanced(): number {
    return this.advanced;
  }
}

/** A one-row, one-phrase song: 96 ticks per row, wrapping to itself. */
const song = () => new FakePredictive();

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
  run(opts?: { tick?: number; transport?: boolean; ticks?: number; input?: number[][] }): void;
}

function harness(app: ControllerApp, playback: PlaybackModel = staticModel): Harness {
  const device = new FakeLaunchpad();
  const seen: ControllerCtx[] = [];
  const sent: number[][] = [];
  const session = new ControllerSession((c) => { seen.push({ ...c, events: [...c.events] }); app(c); }, {
    playback,
    target: { maxRow: 253, launch: (row) => sent.push([0x90, row, 100]), release: () => {} },
  });
  return {
    device, session, seen, sent,
    connect: () => device.write(session.connect()),
    run: (opts = {}) => device.write(session.update({
      input: opts.input ?? [],
      tick: opts.tick ?? 0,
      transport: opts.transport ?? false,
      ticks: opts.ticks ?? 0,
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

test("the predictor is advanced by exactly the ticks the host clocked, and by nothing while stopped", () => {
  const model = song();
  const h = harness(noop, model);
  expect(isPredictive(model)).toBe(true);

  h.connect();
  h.run({ tick: 1000, transport: true }); // the first block clocked nothing before it
  expect(h.seen[0].ticks).toBe(0);

  model.launch(0);
  h.run({ tick: 1050, transport: true, ticks: 50 });
  expect(h.seen[1].ticks).toBe(50);

  h.run({ tick: 1200, transport: false }); // a stopped host: ppq may move on a seek, the cart hears nothing
  expect(h.seen[2].ticks).toBe(0);
});

test("the count is the host's, so a seek is not walked through and a backwards jump is not lost", () => {
  // A host that seeks sends the cart no clocks across the jump, so its walk counts 0 for that block: the predictor
  // must not walk a thousand bars of rows, and must not read the jump as elapsed time. And a host whose timeline goes
  // BACKWARDS (a loop wrap) still clocked the block before it, so that block's ticks are still walked: reading the
  // difference between two positions would call a wrap no time at all and leave the playhead behind the cart.
  const model = song();
  const h = harness(noop, model);
  h.connect();
  h.run({ tick: 0, transport: true });
  model.launch(0);
  h.run({ tick: 50, transport: true, ticks: 50 });
  expect(model.position().channels[0].songRow).toBe(0);
  h.run({ tick: 5000, transport: true, ticks: 0 }); // a seek: nothing clocked
  expect(h.seen[2].ticks).toBe(0);
  expect(model.position().channels[0].songRow).toBe(0);
  // A loop wrap: the position went back to 12, but the block before the wrap was clocked 6 ticks.
  h.run({ tick: 12, transport: true, ticks: 6 });
  expect(h.seen[3].ticks).toBe(6);
  expect(model.ticksAdvanced()).toBe(56); // 50 + 6, in total: no tick was dropped
});

test("a transport fall stops the predictor rather than leaving a stale playhead lit", () => {
  const model = song();
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
  const model = song();
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
    target: { maxRow: 253, launch: () => {}, release: () => {} },
    config: { quantise: "beat", follow: false },
  });
  device.write(session.connect());
  device.write(session.update({ input: [], tick: 0, transport: false, ticks: 0 }));
  expect(seen).toEqual({ quantise: "beat", follow: false });
});
