// rp.controller.session (src/script/nodes/controllerSession.ts): a control surface's session, as the DSP kernel's
// `launchpad` role ran it until B3b, whose cases these were: a pad press out as a launch (into an rp.lsdj.sync node's
// launches, through its real MI.MAP mode, or out of the host), LEDs on controllerOut, the device taken on its connect
// edge, and the re-push rules. Its default target ("0 is the first system") is buildGraph's
// (test/graph/build-graph.test.ts).
import { test, expect } from "../../../testing/harness";
import { CONTROLLER_SESSION_NODE_TYPE, controllerSessionNode } from "../../../src/script/nodes/controllerSession";
import { lsdjSyncNode } from "../../../src/script/nodes/lsdjSync";
import { ControllerRegistry, registerControllerApps } from "../../../src/controller";
import { songRowTicks } from "../../../src/lsdj/playback";
import { SongSchema } from "../../../src/lsdj/model";
import { PRO_MK3, Palette, enterProgrammerMode, padIndex } from "../../../src/launchpad";
import { NodeRig, bytesOn, type BlockSpec, type In, type Sent } from "./_rig";

// The control plane parses an app's config by the app's schema (the registry's); the node takes it parsed.
const apps = new ControllerRegistry();
registerControllerApps(apps);
const parsed = (appConfig: Record<string, unknown> = {}) => apps.get("lsdj-midimap")!.schema.parse(appConfig);

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
  connected?: boolean;
  surface?: In[];
}
const blockOf = (step: Step, frames = 1024): BlockSpec => ({
  frames,
  ppq: step.ppq ?? 0,
  playing: step.playing ?? true,
  inputs: { controllerIn: step.surface ?? [] },
  controls: { controllerConnected: { start: step.connected ? 1 : 0, changes: [] } },
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
  run(step: Step, midi: In[] = []): { controller: Sent[]; lsdj: Sent[] } {
    const controller = this.controller.run(blockOf(step));
    const launches = controller.filter((s) => s.port === "launches").map((s) => ({ frame: s.queued, data: s.data }));
    const lsdj = this.lsdj.run({ ...blockOf(step), inputs: { midi, launches } });
    return { controller, lsdj };
  }
}
/** Row bytes only: the clock stream MI.MAP sends every tick would drown the signal. */
const rowBytes = (sent: Sent[]): number[] => bytesOn(sent, "serialIn").map(([, b]) => b).filter((b) => b !== 0xff && b !== 0xfe);

test("its ports: the surface in, its connected flag; the surface out, the launches and host MIDI out", () => {
  expect(session().ports()).toEqual([
    "in events controllerIn",
    "in control controllerConnected",
    "out events controllerOut",
    "out events launches",
    "out events midiOut",
  ]);
  expect(controllerSessionNode.descriptor().id).toBe(CONTROLLER_SESSION_NODE_TYPE);
  const caps = controllerSessionNode.layout(controllerSessionNode.parseSettings({})).ports.slice(2).map((p) => p.capacity);
  expect(caps).toEqual([{ messages: 256, bytes: 4096 }, { messages: 64, bytes: 256 }, { messages: 1024, bytes: 8192 }]);
  expect(controllerSessionNode.reshapes).toBe(true);
});

test("settings default as the role's did, and keep the parsed app config and the tables as given", () => {
  expect(controllerSessionNode.parseSettings({})).toEqual({
    app: "lsdj-midimap",
    target: "system",
    systemId: 0,
    appConfig: {},
    songRowTicks: [],
    anchorRows: [],
    anchorSeq: 0,
  });
  const given = {
    app: "lsdj-midimap",
    target: "midiOut",
    systemId: SYSTEM,
    appConfig: parsed({ quantise: "beat" }),
    songRowTicks: [[96, null], [96]],
    anchorRows: [1, null, 3, 4],
    anchorSeq: 2,
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

// --- reacting to a settings change -------------------------------------------------------------------------------------

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
  // it back.
  dev.feed(rig.run(still({ playing: true, ppq: 4.1, connected: true }))); // +96 ticks: row 1 -> 0
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
