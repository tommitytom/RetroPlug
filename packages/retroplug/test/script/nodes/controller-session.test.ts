// rp.controller.session (@retroplug/launchpad/controllerSession): a control surface's session, as the DSP kernel's
// `launchpad` role ran it until B3b, whose cases these were: a pad press out as a launch (into an rp.lsdj.sync node's
// launches, through its real MI.MAP mode, or out of the host), LEDs on controllerOut, the device taken on its connect
// edge, and the re-push rules. Its default target ("0 is the first system") is buildGraph's
// (test/graph/build-graph.test.ts).
import { test, expect } from "@retroplug/testing";
import { CONTROLLER_SESSION_NODE_TYPE, controllerSessionNode } from "@retroplug/launchpad/controllerSession";
import { lsdjSyncNode } from "@retroplug/lsdj/lsdjSyncNode";
import { controllerApp } from "@retroplug/launchpad/registry";
// The side effect: the product's generated registry, whose load runs the launchpad package's contribution kind and
// fills its app table.
import "../../../src/generated/packages";
// The generated script bundle registers every app's runtime before the node can run (src/generated/scriptNodes.ts);
// a test process does the same, with the app the documents below name.
import { registerControllerApps } from "@retroplug/launchpad/apps";
import lsdjMidiMapRuntime from "@retroplug/lsdj-launchpad/appRuntime";
import { songRowTicks } from "@retroplug/lsdj/playback";
import { SongSchema } from "@retroplug/lsdj/model";
import { PRO_MK3, Palette, enterProgrammerMode, padIndex } from "@retroplug/launchpad";
import { NodeRig, bytesOn, type BlockSpec, type In, type Sent } from "./_rig";

// The control plane parses an app's config by the app's schema (the registry's, which the product's app modules fill);
// the node takes it parsed.
const parsed = (appConfig: Record<string, unknown> = {}) => controllerApp("lsdj-midimap")!.config.parse(appConfig);

registerControllerApps([lsdjMidiMapRuntime]);

/** Sixteen rows, each one one-phrase chain: every row has content and lasts 96 ticks. */
const table = () =>
  songRowTicks(SongSchema.parse({ formatVersion: 22, rows: Array.from({ length: 16 }, () => ({ chains: [0, 0, 0, 0] })), chains: [{ phrases: [0] }] }));

/** A 4 x 256 tick table with content on rows 0 and 1 only. */
const twoRows = (): (number | null)[][] => [0, 1, 2, 3].map(() => Array.from({ length: 256 }, (_, r) => (r < 2 ? 96 : null)));
/** The same song with a chain added to row 4, as an edit inside LSDj makes. */
const editedTwoRows = (): (number | null)[][] => {
  const t = twoRows();
  for (const ch of t) ch[4] = 96;
  return t;
};

const SYSTEM = 7;
const HELLO = enterProgrammerMode(PRO_MK3);

const settingsOf = (over: Record<string, unknown> = {}) => ({
  app: "lsdj-midimap",
  target: "system",
  systemId: SYSTEM,
  songRowTicks: table(),
  appConfig: parsed(),
  ...over,
});
const session = (over: Record<string, unknown> = {}) => new NodeRig(controllerSessionNode, settingsOf(over));

/** Press the pad showing (channel 0, row `row`) on page 0. Rows 0-7 are the left pane, 8-15 the right. */
const press = (row: number): In => ({ frame: 0, data: [0x90, padIndex({ x: row < 8 ? 0 : 4, y: row % 8 }), 100] });
const PRESS_ROW_0: In = { frame: 0, data: [0x90, 0x51, 0x7f] };

interface Step {
  ppq?: number;
  playing?: boolean;
  tempo?: number;
  connected?: boolean;
  /** The link's generation (0 = none when `connected` is false): a swap inside one block changes it. */
  link?: number;
  /** The link's cumulative drop count. */
  drops?: number;
  surface?: In[];
}
const blockOf = (step: Step, frames = 1024): BlockSpec => ({
  frames,
  ppq: step.ppq ?? 0,
  tempo: step.tempo ?? 120,
  playing: step.playing ?? true,
  inputs: { controllerIn: step.surface ?? [] },
  controls: {
    controllerConnected: { start: step.connected ? (step.link ?? 1) : 0, changes: [] },
    controllerDrops: { start: step.drops ?? 0, changes: [] },
  },
});

const at = (sent: Sent[], port: string): number[][] => sent.filter((s) => s.port === port).map((s) => s.data);
const sameBytes = (a: readonly number[] | undefined, b: readonly number[]): boolean =>
  !!a && a.length === b.length && a.every((v, i) => v === b[i]);

/** A session wired to an LSDj node in MI.MAP mode, its launches into the node's launches, one block at a time. */
class Wired {
  readonly controller: NodeRig<unknown>;
  readonly lsdj = new NodeRig(lsdjSyncNode, { mode: "midiMap" });
  constructor(over: Record<string, unknown> = {}) {
    this.controller = session(over) as NodeRig<unknown>;
  }
  run(step: Step, midi: In[] = [], frames = 1024): { controller: Sent[]; lsdj: Sent[] } {
    const controller = this.controller.run(blockOf(step, frames));
    const launches = controller.filter((s) => s.port === "launches").map((s) => ({ frame: s.queued, data: s.data }));
    const lsdj = this.lsdj.run({ ...blockOf(step, frames), inputs: { midi, launches } });
    return { controller, lsdj };
  }
}
/** Row bytes only: the clock stream MI.MAP sends every tick would drown the signal. */
const rowBytes = (sent: Sent[]): number[] => bytesOn(sent, "serialIn").map(([, b]) => b).filter((b) => b !== 0xff && b !== 0xfe);

test("its ports: the surface in, its link generation and drop count; the surface out, the launches and host MIDI out", () => {
  expect(session().ports()).toEqual([
    "in events controllerIn",
    "in control controllerConnected",
    "in control controllerDrops",
    "out events controllerOut",
    "out events launches",
    "out events midiOut",
  ]);
  expect(controllerSessionNode.descriptor().id).toBe(CONTROLLER_SESSION_NODE_TYPE);
  // The three inputs (controllerIn, controllerConnected, controllerDrops) have no declared capacity; the three
  // outputs do.
  const caps = controllerSessionNode.layout(controllerSessionNode.parseSettings({})).ports.slice(3).map((p) => p.capacity);
  expect(caps).toEqual([{ messages: 256, bytes: 4096 }, { messages: 64, bytes: 256 }, { messages: 1024, bytes: 8192 }]);
  expect(controllerSessionNode.reshapes).toBe(true);
});

test("settings default as the role's did, and keep the parsed app config and the tables as given", () => {
  // No app by default: the node names no app, and the control plane's project settings name the one it starts with.
  expect(controllerSessionNode.parseSettings({})).toEqual({
    app: "",
    target: "system",
    systemId: 0,
    appConfig: {},
    songRowTicks: [],
    anchorRows: [],
    anchorSeq: 0,
    tickDivisor: 1,
  });
  const given = {
    app: "lsdj-midimap",
    target: "midiOut",
    systemId: SYSTEM,
    appConfig: parsed({ quantise: "beat" }),
    songRowTicks: [[96, null], [96]],
    anchorRows: [1, null, 3, 4],
    anchorSeq: 2,
    tickDivisor: 2,
  };
  expect(controllerSessionNode.parseSettings(given)).toEqual(given);
  expect(() => controllerSessionNode.parseSettings({ target: "elsewhere" })).toThrow("target: Invalid option");
});

test("a pad press reaches the cart's link port, through the real MI.MAP mode, in one block", () => {
  const w = new Wired({ appConfig: parsed({ quantise: "immediate" }) });
  expect(rowBytes(w.run({ ppq: 0 }).lsdj)).toEqual([]); // nothing pressed yet
  const out = w.run({ ppq: 0.1, surface: [press(5)] });
  expect(at(out.controller, "launches")).toEqual([[0x90, 5, 100]]);
  expect(rowBytes(out.lsdj)).toEqual([5]);
});

test("the same launch stream goes to MIDI out instead, for a real Game Boy", () => {
  const w = new Wired({ target: "midiOut", appConfig: parsed({ quantise: "immediate" }) });
  w.run({});
  const out = w.run({ ppq: 0.1, surface: [press(5)] });
  expect(out.controller.filter((s) => s.port === "midiOut").map((s) => [s.frame, ...s.data])).toEqual([[0, 0x90, 5, 100]]);
  expect(at(out.controller, "launches")).toEqual([]); // and nothing went to the cart
  expect(rowBytes(out.lsdj)).toEqual([]);
});

test("control-surface traffic never reaches the cart as music", () => {
  // The same bytes on the cart's routed MIDI launch row 11 (the pad's note number), on the surface row 5.
  const w = new Wired({ appConfig: parsed({ quantise: "immediate" }) });
  w.run({});
  expect(rowBytes(w.run({ ppq: 0.1 }, [press(5)]).lsdj)).toEqual([padIndex({ x: 0, y: 5 })]);
  expect(rowBytes(w.run({ ppq: 0.2, surface: [press(5)] }).lsdj)).toEqual([5]);
});

test("quantisation is driven by the block clock, so a launch waits for the bar", () => {
  const w = new Wired(); // bar, the default: 96 ticks = 4 quarters
  w.run({ ppq: 4.0 });
  w.run({ ppq: 4.1, surface: [press(0)] }); // something playing, so the next press has a boundary to wait for
  expect(rowBytes(w.run({ ppq: 4.2, surface: [press(6)] }).lsdj)).toEqual([]); // cued, not launched
  expect(rowBytes(w.run({ ppq: 7.9 }).lsdj)).toEqual([]); // still short of the bar
  expect(rowBytes(w.run({ ppq: 8.0 }).lsdj)).toEqual([6]); // ppq 8 = tick 192
});

test("LED bytes come out on controllerOut at frame 0, not as launches or MIDI out", () => {
  const rig = session();
  const out = rig.run(blockOf({}));
  expect(at(out, "controllerOut").length > 0).toBeTruthy(); // the first paint
  expect(out.every((s) => s.port === "controllerOut" && s.frame === 0)).toBe(true);
  // A second identical block repaints the same grid, so the diffing surface sends nothing at all.
  expect(rig.run(blockOf({}))).toEqual([]);
});

test("the session survives across blocks, so cues and pages are not forgotten", () => {
  const w = new Wired();
  w.run({ ppq: 0 });
  w.run({ ppq: 0.1, surface: [press(0)] }); // immediate: nothing playing
  w.run({ ppq: 0.2, surface: [press(9)] }); // now cued
  expect(rowBytes(w.run({ ppq: 3.9 }).lsdj)).toEqual([]);
  expect(rowBytes(w.run({ ppq: 4.0 }).lsdj)).toEqual([9]);
});

test("an unknown app id runs nothing rather than throwing on the audio thread", () => {
  expect(session({ app: "does-not-exist" }).run(blockOf({ surface: [press(3)], connected: true }))).toEqual([]);
});

// --- taking the device -----------------------------------------------------------------------------------------------

const surface = (sent: Sent[]) => at(sent, "controllerOut");
const quiet = (over: Record<string, unknown> = {}) => session({ songRowTicks: twoRows(), ...over });
const still = (step: Step = {}): BlockSpec => blockOf({ playing: false, ...step }, 512);

test("with no device attached the node never enters Programmer mode", () => {
  const rig = quiet();
  expect(surface(rig.run(still())).some((m) => sameBytes(m, HELLO))).toBe(false);
  expect(surface(rig.run(still())).some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("a device appearing mid-session is taken on that block, hello first", () => {
  const rig = quiet();
  rig.run(still());
  rig.run(still());
  const out = surface(rig.run(still({ connected: true })));
  expect(out.length > 0).toBe(true);
  expect(sameBytes(out[0], HELLO)).toBe(true);
});

test("a surface counts as attached when it is from frame 0", () => {
  const sentToSurface = (connected: { start: number; changes: { frame: number; value: number }[] }) =>
    session().run({ frames: 1024, controls: { controllerConnected: connected } }).length;
  const unattached = sentToSurface({ start: 0, changes: [] });
  expect(unattached).toBeGreaterThan(0);
  expect(sentToSurface({ start: 0, changes: [{ frame: 0, value: 1 }] })).toBeGreaterThan(unattached);
  expect(sentToSurface({ start: 1, changes: [] })).toBeGreaterThan(unattached);
  expect(sentToSurface({ start: 0, changes: [{ frame: 9, value: 1 }] })).toBe(unattached);
});

test("staying connected does not re-send hello every block", () => {
  const rig = quiet();
  rig.run(still({ connected: true }));
  expect(surface(rig.run(still({ connected: true }))).some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("a reconnect repaints the whole surface, not a diff against a device that was blanked", () => {
  const rig = quiet();
  const painted = surface(rig.run(still({ connected: true }))).filter((m) => !sameBytes(m, HELLO));
  expect(painted.length > 0).toBe(true);
  rig.run(still({ connected: true }));
  expect(rig.run(still({ connected: true })).length).toBe(0);
  rig.run(still({ connected: false }));
  const again = surface(rig.run(still({ connected: true })));
  expect(sameBytes(again[0], HELLO)).toBe(true);
  expect(again.length - 1).toBe(painted.length);
});

test("the predictor keeps running while nothing is attached", () => {
  const rig = quiet();
  rig.run(still({ playing: true, ppq: 0 }));
  const out = surface(rig.run(still({ playing: true, ppq: 8, connected: true }))); // 8 quarters = 192 ticks
  expect(sameBytes(out[0], HELLO)).toBe(true);
  expect(out.length > 1).toBe(true);
});

test("a pair swapped inside one block is taken as a NEW device, hello first, and repainted", () => {
  // A swap releases the old device and claims the new one inside a single UI-thread call, so no block ever sees
  // "detached": the generation alone has to re-take it, or the new device sits in Live mode while the shadow
  // buffer believes it is lit.
  const rig = quiet();
  const first = surface(rig.run(still({ connected: true, link: 1 })));
  const painted = first.filter((m) => !sameBytes(m, HELLO)).length;
  rig.run(still({ connected: true, link: 1 }));
  expect(rig.run(still({ connected: true, link: 1 })).length).toBe(0);
  const swapped = surface(rig.run(still({ connected: true, link: 2 })));
  expect(sameBytes(swapped[0], HELLO)).toBe(true);
  expect(swapped.filter((m) => !sameBytes(m, HELLO)).length).toBe(painted);
});

test("a forward seek is a relocate: the predictor does not race through the song", () => {
  // walkTicks sends the cart no clocks for a jump, so the session must not advance through one either - and
  // walking a thousand bars of rows in one block would cost milliseconds on the audio thread. Sixteen rows of 96
  // ticks, so walking the jump lands on row 2 rather than wrapping back to row 0.
  const rig = session({ songRowTicks: table() });
  const dev = new Device();
  dev.feed(rig.run(still({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] })));
  dev.feed(rig.run(still({ playing: true, ppq: 0.1, connected: true })));
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenBright);
  // +200 ticks: walking it would carry the playhead through row 1 and most of row 2, so the row it is on says
  // whether the jump was walked. The row 0 pad must stay the BRIGHT one (it is dim content when not played).
  dev.feed(rig.run(still({ playing: true, ppq: 0.1 + 200 / 24, connected: true })));
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenBright); // still on the row the cart is on
  expect(dev.leds.get(pad(0, 2)) === Palette.greenBright).toBe(false); // and the walk did not land it on row 2
});

test("a session that starts after a run with no app begins at the clock, not at the last count", () => {
  // The node hands the predictor the PREVIOUS block's count. While the app id names no app the node returns before the
  // walk, so that count used to sit there and the first block of the session that starts later was handed it: the fresh
  // predictor began a block ahead of the cart (row 2 where the cart is at row 1, here).
  const twoSeconds = 88200; // 96 ticks a block
  const clean = session({ songRowTicks: table() });
  const dev = new Device();
  dev.feed(clean.run(blockOf({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] }, twoSeconds)));
  dev.feed(clean.run(blockOf({ playing: true, ppq: 4, connected: true }, twoSeconds)));
  expect(dev.leds.get(pad(0, 1))).toBe(Palette.greenBright); // two blocks of 96 ticks: the prediction is on row 1
  // A block the node runs with no session, between two it runs with one.
  const rig = session({ songRowTicks: table() });
  const dev2 = new Device();
  dev2.feed(rig.run(blockOf({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] }, twoSeconds)));
  dev2.feed(rig.run(blockOf({ playing: true, ppq: 4, connected: true }, twoSeconds)));
  expect(rig.set({ ...settingsOf(), app: "does-not-exist" })).toBe(true);
  rig.run(blockOf({ playing: true, ppq: 8 }, twoSeconds)); // no session: the count it would have made is not kept
  expect(rig.set(settingsOf())).toBe(true);
  dev2.feed(rig.run(blockOf({ playing: true, ppq: 12, connected: true }, twoSeconds)));
  // Where the cart is, one block of ticks behind as always: the dangling count would have added the gap's block and
  // put this run a row ahead.
  expect(dev2.leds.get(pad(0, 1))).toBe(Palette.greenBright);
  expect(dev2.leds.get(pad(0, 2)) === Palette.greenBright).toBe(false);
});

test("a loop wrap keeps the block's ticks: the playhead does not fall a block further behind", () => {
  // A DAW loop (or an SPP back) moves the host's timeline backwards, and walkTicks goes on clocking the cart
  // through it: it merely resyncs its own counter to the block's start. A session that INFERRED the ticks from the
  // position read the wrap as no elapsed time and dropped the whole block, so the predicted playhead fell a block
  // behind on every pass and never caught up. Here three of the six playing blocks are wraps: counting the walk
  // keeps all 576 ticks (row 6), while inferring them would keep 288 (row 3).
  const rig = session({ songRowTicks: table() }); // sixteen rows of 96 ticks
  const dev = new Device();
  const twoSeconds = 88200; // 2 s at 44.1 kHz / 120 BPM = 96 ticks a block
  dev.feed(rig.run(blockOf({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] }, twoSeconds)));
  for (const ppq of [4, 0, 4, 0, 4, 0]) dev.feed(rig.run(blockOf({ playing: true, ppq, connected: true }, twoSeconds)));
  expect(dev.leds.get(pad(0, 6))).toBe(Palette.greenBright);
});

test("a device taken again keeps the block it lands in: a swap drops no ticks", () => {
  // A reconnect is not a reason to lose a block: the cart was clocked through it either way. The session used to drop
  // the ticks of the block a device was taken on, which lost a whole block per swap; two swaps over five blocks would
  // leave the playhead two rows behind (row 3 here) instead of where the cart is (row 5).
  const rig = session({ songRowTicks: table() });
  const dev = new Device();
  const twoSeconds = 88200;
  dev.feed(rig.run(blockOf({ playing: true, ppq: 0, connected: true, link: 1, surface: [PRESS_ROW_0] }, twoSeconds)));
  const links = [1, 2, 2, 3, 3]; // the pair is swapped before the second and the fourth playing block
  for (let i = 0; i < links.length; i++)
    dev.feed(rig.run(blockOf({ playing: true, ppq: 4 * (i + 1), connected: true, link: links[i] }, twoSeconds)));
  expect(dev.leds.get(pad(0, 5))).toBe(Palette.greenBright);
});

test("ragged blocks and a tempo change are elapsed time, not a seek: the playhead keeps every tick", () => {
  // A host under FL-style splits hands the audio thread uneven blocks, and a tempo change makes a long block hold
  // fewer ticks than the one before it. What a block covers comes from its OWN length and tempo, so neither is a
  // seek; a threshold measured against the PREVIOUS block's elapsed ticks would read both as one and drop the
  // block's ticks, leaving the lit playhead (and what the app quantises against) behind the cart.
  const quarters = (frames: number, tempo: number) => (frames / 44100) * (tempo / 60); // 44.1 kHz, 24 PPQN
  const patterns: Array<[string, ReadonlyArray<readonly [number, number]>]> = [
    ["a long block then a short one", [[4096, 240], [2048, 240]]],
    ["a tempo drop between two long blocks", [[4096, 240], [4096, 120]]],
  ];
  for (const [name, pattern] of patterns) {
    const rig = quiet(); // rows 0 and 1 of 96 ticks each
    const dev = new Device();
    let ppq = 0;
    const [firstFrames, firstTempo] = pattern[0];
    dev.feed(rig.run(blockOf({ playing: true, ppq, connected: true, tempo: firstTempo, surface: [PRESS_ROW_0] }, firstFrames)));
    ppq += quarters(firstFrames, firstTempo);
    for (let cycle = 1; cycle < 8; cycle++) {
      for (const [frames, tempo] of pattern) {
        dev.feed(rig.run(blockOf({ playing: true, ppq, connected: true, tempo }, frames)));
        ppq += quarters(frames, tempo);
      }
    }
    // The predictor walks the ticks the host clocked (the blocks up to the last one's start: the session is handed the
    // PREVIOUS block's count) plus the launch's own tick, and a full row is 96 of them. The row is not on an edge - the
    // run leaves a few ticks of margin either way - so a lost block, about one tick here, is not what decides it: what
    // does is that a ragged split or a tempo drop was read as a SEEK and the whole run dropped.
    const expected = Math.floor((1 + Math.floor(ppq * 24)) / 96);
    expect(expected).toBe(1); // the run is long enough to leave row 0, so a block of lost ticks shows as row 0
    expect(dev.leds.get(pad(0, expected)), name).toBe(Palette.greenBright);
  }
});

test("a link that dropped LED messages gets a full repaint, once", () => {
  const rig = quiet();
  const first = surface(rig.run(still({ connected: true, link: 1, drops: 0 })));
  const painted = first.filter((m) => !sameBytes(m, HELLO)).length;
  rig.run(still({ connected: true, link: 1, drops: 0 }));
  expect(rig.run(still({ connected: true, link: 1, drops: 0 })).length).toBe(0);
  const after = surface(rig.run(still({ connected: true, link: 1, drops: 3 })));
  expect(after.some((m) => sameBytes(m, HELLO))).toBe(false); // the same device: no mode change
  expect(after.length).toBe(painted); // but every LED again
  expect(rig.run(still({ connected: true, link: 1, drops: 3 })).length).toBe(0);
});

// --- reacting to a settings change
// -------------------------------------------------------------------------------------

// Grid pad (x, y) -> device index. The device counts rows from the BOTTOM.
const pad = (x: number, y: number) => (7 - y) * 10 + x + 11;

/** A running model of what the device is showing, fed block by block: the surface sends only CHANGES. */
class Device {
  readonly leds = new Map<number, number>();
  feed(sent: Sent[]): void {
    for (const m of surface(sent)) {
      if (m[0] === 0xf0) {
        for (let i = 7; i + 2 < m.length - 1; i += 3) this.leds.set(m[i + 1], m[i + 2]);
      } else if ((m[0] & 0xf0) === 0x90 || (m[0] & 0xf0) === 0xb0) {
        this.leds.set(m[1], m[2]);
      }
    }
  }
  lit(x: number, y: number): boolean {
    const c = this.leds.get(pad(x, y));
    return c !== undefined && c !== 0;
  }
}

test("a song edited on the cart lights up in place, with no hello", () => {
  const rig = quiet();
  const dev = new Device();
  dev.feed(rig.run(still({ connected: true })));
  expect(rig.run(still({ connected: true })).length).toBe(0); // settled
  expect(dev.lit(0, 4)).toBe(false);
  expect(rig.set(settingsOf({ songRowTicks: editedTwoRows() }))).toBe(true);
  const out = rig.run(still({ connected: true }));
  dev.feed(out);
  expect(dev.lit(0, 4)).toBe(true);
  expect(surface(out).some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("adopting a new song table does not restart the song", () => {
  const rig = quiet();
  const dev = new Device();
  dev.feed(rig.run(still({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] })));
  dev.feed(rig.run(still({ playing: true, ppq: 4, connected: true }))); // 96 ticks: row 0 -> row 1
  const playheadBefore = dev.leds.get(pad(0, 1));
  expect(playheadBefore !== undefined && playheadBefore !== 0).toBe(true);
  rig.set(settingsOf({ songRowTicks: editedTwoRows() }));
  dev.feed(rig.run(still({ playing: true, ppq: 4, connected: true })));
  expect(dev.leds.get(pad(0, 1))).toBe(playheadBefore);
  expect(dev.lit(0, 4)).toBe(true);
});

test("at tickDivisor 2 the predictor walks half the ticks a block, as the cart clocked at 24 / 2 hears them", () => {
  // MI.MAP clocks its cart at 24 / tempoDivisor ticks a quarter (rp.lsdj.sync), so at divisor 2 a row of 96 ticks
  // lasts eight quarters. The session walks the same division: four blocks of 96 host ticks after the launch are 192 of
  // the cart's, row 2, where walking every host tick would put the playhead on row 4, two rows ahead of the cart.
  const twoSeconds = 88200; // 96 ticks a block at divisor 1
  const lit = (tickDivisor: number): number => {
    const rig = session({ songRowTicks: table(), tickDivisor });
    const dev = new Device();
    dev.feed(rig.run(blockOf({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] }, twoSeconds)));
    for (let i = 1; i <= 4; i++) dev.feed(rig.run(blockOf({ playing: true, ppq: 4 * i, connected: true }, twoSeconds)));
    return [0, 1, 2, 3, 4, 5].findIndex((row) => dev.leds.get(pad(0, row)) === Palette.greenBright);
  };
  expect(lit(2)).toBe(2);
  expect(lit(1)).toBe(4);
});

test("a divisor changed while a song plays reaches the running session at its next block, its cue kept", () => {
  // tickDivisor is read every block and is not part of the session's shape, so the change rebuilds nothing: no hello,
  // and the launch cued for the end of row 0 still waiting. The session walks what its cart is clocked, the block the
  // walk changes resolution in included, so the cued row reaches the cart once the cart has been sent row 0's 96 ticks.
  const w = new Wired({ appConfig: parsed({ quantise: "rowEnd" }) });
  const second = 44100; // 48 host ticks a block at 120 BPM
  let clocks = 0;
  const run = (step: Step): { controller: Sent[]; rows: number[] } => {
    const out = w.run(step, [], second);
    const rows: number[] = [];
    for (const [, byte] of bytesOn(out.lsdj, "serialIn")) {
      if (byte === 0xff) clocks++;
      else if (byte !== 0xfe) rows.push(clocks);
    }
    return { controller: out.controller, rows };
  };
  expect(run({ ppq: 0, surface: [PRESS_ROW_0] }).rows).toEqual([0]); // nothing was playing: launched at once
  expect(w.controller.set(settingsOf({ appConfig: parsed({ quantise: "rowEnd" }), tickDivisor: 2 }))).toBe(true);
  expect(w.lsdj.set({ mode: "midiMap", tempoDivisor: 2 })).toBe(true);
  const changed = run({ ppq: 2, surface: [press(9)] });
  expect(surface(changed.controller).some((m) => sameBytes(m, HELLO))).toBe(false); // the same session, not a new one
  expect(changed.rows).toEqual([]); // cued for the end of row 0
  const reached: number[] = [];
  for (let block = 2; block < 8; block++) reached.push(...run({ ppq: 2 * block }).rows);
  expect(reached).toEqual([96]);
});

test("an app knob changes behaviour on the next block", () => {
  const secondLaunchFires = (appConfig: Record<string, unknown>): boolean => {
    const rig = quiet();
    rig.set(settingsOf({ target: "midiOut", songRowTicks: twoRows(), appConfig: parsed(appConfig) }));
    rig.run(still({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] })); // starts it playing
    rig.run(still({ playing: true, ppq: 1, connected: true }));
    // Now something IS playing, and tick 26 is nowhere near a bar line (96).
    return at(rig.run(still({ playing: true, ppq: 1.1, connected: true, surface: [PRESS_ROW_0] })), "midiOut").length > 0;
  };
  expect(secondLaunchFires({})).toBe(false); // bar (the default): the second press waits for the bar line
  expect(secondLaunchFires({ quantise: "immediate" })).toBe(true);
});

test("a rebuilt session re-takes the device rather than diffing against a surface it never painted", () => {
  const rig = quiet();
  const first = surface(rig.run(still({ connected: true })));
  expect(sameBytes(first[0], HELLO)).toBe(true);
  rig.set(settingsOf({ songRowTicks: twoRows(), appConfig: parsed({ follow: false }) }));
  const out = surface(rig.run(still({ connected: true })));
  expect(sameBytes(out[0], HELLO)).toBe(true);
  expect(out.length).toBe(first.length);
});

test("a new target system rebuilds the session, as a new target did", () => {
  const rig = quiet();
  rig.run(still({ connected: true }));
  rig.set(settingsOf({ songRowTicks: twoRows(), systemId: SYSTEM + 1 }));
  expect(sameBytes(surface(rig.run(still({ connected: true })))[0], HELLO)).toBe(true);
});

test("an anchor moves the playhead to where the cart actually started, once per sequence number", () => {
  const rig = quiet();
  const dev = new Device();
  dev.feed(rig.run(still({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] })));
  expect(dev.lit(0, 0)).toBe(true); // predicted: row 0
  rig.set(settingsOf({ songRowTicks: twoRows(), anchorRows: [1, 1, 1, 1], anchorSeq: 1 }));
  dev.feed(rig.run(still({ playing: true, ppq: 0.1, connected: true })));
  expect(dev.leds.get(pad(0, 1))).toBe(Palette.greenBright);
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenDim);
  // The playhead advances a row on its own; an unrelated change arrives carrying the same anchor, which must not drag
  // it back. +96 ticks, PLAYED at the block rate a host would (512 frames at 44.1 kHz, 120 BPM is about half a tick a
  // block): one jump of four quarters would be a relocate - a seek - which advances nothing.
  for (let i = 1; i <= 200; i++) dev.feed(rig.run(still({ playing: true, ppq: 0.1 + i * 0.02, connected: true })));
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenBright);
  rig.set(settingsOf({ songRowTicks: editedTwoRows(), anchorRows: [1, 1, 1, 1], anchorSeq: 1 }));
  dev.feed(rig.run(still({ playing: true, ppq: 4.2, connected: true })));
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenBright);
});

test("no anchor on the hardware path leaves dead reckoning exactly as it was", () => {
  const rig = quiet();
  const dev = new Device();
  dev.feed(rig.run(still({ playing: true, ppq: 0, connected: true, surface: [PRESS_ROW_0] })));
  // Settings with no anchor are what the node already holds, so the push is no change at all (the rig sees none).
  expect(rig.set(settingsOf({ songRowTicks: twoRows(), anchorRows: [], anchorSeq: 0 }))).toBe(false);
  dev.feed(rig.run(still({ playing: true, ppq: 0.1, connected: true })));
  expect(dev.leds.get(pad(0, 0))).toBe(Palette.greenBright);
});
