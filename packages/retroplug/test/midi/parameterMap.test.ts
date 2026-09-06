// The per-ROM DAW parameter projection (spec/12-dynamic-parameters.md). Locks the slot arithmetic, the
// routing-mode reachability rule (a lane that cannot reach the ROM must not be claimed), the per-system
// cap, and the invariants the native side depends on: slots are stable, and the CC tables are the ones
// the ROMs actually document.
import { test, expect } from "../../testing/harness";
import { MidiRouting } from "../../src/settingsEnums";
import {
  CC_SLOTS_PER_SYSTEM,
  MAX_PARAMETER_SYSTEMS,
  MGB_PARAMETERS,
  BLIPTOASTER_PARAMETERS,
  preRoutingChannel,
  projectParameterMap,
  type ParameterSystem,
} from "../../src/parameterMap";

const mgb: ParameterSystem = { roles: [{ kind: "mgb" }] };
const blip: ParameterSystem = { roles: [{ kind: "bliptoaster" }] };
const plain: ParameterSystem = { roles: [{ kind: "lsdj" }] };

test("a single mGB system claims slots from 0, in table order", () => {
  const map = projectParameterMap([mgb], MidiRouting.SendToAll);
  expect(map.length).toEqual(MGB_PARAMETERS.length);
  expect(map[0].slot).toEqual(0);
  expect(map[0].name).toEqual("PU1 Pulse Width");
  expect(map[0].cc).toEqual(1);
  expect(map[0].channel).toEqual(0);
  // WAV shape select is CC1 on MIDI channel 3 (index 2) per mGB's implementation map
  const wav = map.find((s) => s.name === "WAV Shape");
  expect(wav?.cc).toEqual(1);
  expect(wav?.channel).toEqual(2);
});

test("a system with no parameter-bearing role claims nothing", () => {
  expect(projectParameterMap([plain], MidiRouting.SendToAll)).toEqual([]);
  expect(projectParameterMap([], MidiRouting.SendToAll)).toEqual([]);
});

test("system N's slots start at N * CC_SLOTS_PER_SYSTEM", () => {
  const map = projectParameterMap([plain, blip], MidiRouting.SendToAll);
  expect(map.length).toEqual(BLIPTOASTER_PARAMETERS.length);
  expect(map[0].slot).toEqual(CC_SLOTS_PER_SYSTEM);
  expect(map[map.length - 1].slot).toEqual(CC_SLOTS_PER_SYSTEM + BLIPTOASTER_PARAMETERS.length - 1);
});

test("no ROM table overflows its system's slot budget", () => {
  // The projection truncates, so an over-long table would silently lose entries instead of failing.
  expect(MGB_PARAMETERS.length <= CC_SLOTS_PER_SYSTEM).toEqual(true);
  expect(BLIPTOASTER_PARAMETERS.length <= CC_SLOTS_PER_SYSTEM).toEqual(true);
});

test("every claimed slot stays inside the pool", () => {
  const map = projectParameterMap([mgb, blip, mgb, blip], MidiRouting.SendToAll);
  for (const s of map) {
    expect(s.slot >= 0 && s.slot < MAX_PARAMETER_SYSTEMS * CC_SLOTS_PER_SYSTEM).toEqual(true);
    expect(s.cc >= 0 && s.cc <= 127).toEqual(true);
    expect(s.channel >= 0 && s.channel <= 15).toEqual(true);
  }
  expect(new Set(map.map((s) => s.slot)).size).toEqual(map.length); // no slot claimed twice
});

test("systems past the pool's reach claim nothing", () => {
  const five = [mgb, mgb, mgb, mgb, mgb];
  const map = projectParameterMap(five, MidiRouting.SendToAll);
  const systems = new Set(map.map((s) => Math.floor(s.slot / CC_SLOTS_PER_SYSTEM)));
  expect([...systems].sort()).toEqual([0, 1, 2, 3]);
});

test("preRoutingChannel: SendToAll passes the ROM channel through", () => {
  expect(preRoutingChannel(MidiRouting.SendToAll, 0, 1, 0)).toEqual(0);
  expect(preRoutingChannel(MidiRouting.SendToAll, 0, 1, 4)).toEqual(4);
  expect(preRoutingChannel(MidiRouting.SendToAll, 1, 2, 3)).toEqual(3);
});

test("preRoutingChannel: FourChannelsPerInstance offsets by 4 and drops voices past the fourth", () => {
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 0, 2, 0)).toEqual(0);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 0, 2, 3)).toEqual(3);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 1, 2, 0)).toEqual(4);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 1, 2, 2)).toEqual(6);
  // a ROM's fifth voice (mGB POLY, BlipToaster DMC) has no channel in this mode
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 0, 2, 4)).toEqual(null);
});

test("preRoutingChannel: the one-channel modes reach only the ROM's first voice", () => {
  for (const mode of [MidiRouting.OneChannelPerInstance, MidiRouting.MidiChannelToInstance]) {
    expect(preRoutingChannel(mode, 0, 2, 0)).toEqual(0);
    expect(preRoutingChannel(mode, 1, 2, 0)).toEqual(1);
    expect(preRoutingChannel(mode, 1, 2, 1)).toEqual(null);
  }
});

test("an unreachable voice leaves its slot unclaimed rather than mis-routing", () => {
  // BlipToaster's DMC controls are on its fifth voice, which FourChannelsPerInstance cannot address.
  const map = projectParameterMap([blip, blip], MidiRouting.FourChannelsPerInstance);
  expect(map.some((s) => s.name.startsWith("Sample"))).toEqual(false);
  expect(map.some((s) => s.name === "Pulse 1 Duty")).toEqual(true);
  // ...and OneChannelPerInstance reaches only voice 1, so the triangle/noise/DMC lanes all go.
  const one = projectParameterMap([blip, blip], MidiRouting.OneChannelPerInstance);
  expect(one.every((s) => s.name.startsWith("Pulse 1") || s.name.endsWith("Depth") || s.name === "LFO Rate")).toEqual(
    true,
  );
});

test("claimed slots pack from 0 with no gaps, so dropping one does not shift the rest apart", () => {
  const map = projectParameterMap([blip], MidiRouting.FourChannelsPerInstance);
  expect(map.map((s) => s.slot)).toEqual(map.map((_, i) => i));
});
