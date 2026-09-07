// The per-ROM DAW parameter projection (spec/12-dynamic-parameters.md). Locks the slot arithmetic, the
// routing-mode reachability rule (a lane that cannot reach the ROM must not be claimed), the per-system
// budget, the per-chip BlipToaster sets, and the invariants the native side depends on.
import { test, expect } from "../../testing/harness";
import { MidiRouting } from "../../src/settingsEnums";
import { blipToasterChip, inesMapper } from "../../src/bliptoaster/romDetect";
import {
  CC_SLOTS_PER_SYSTEM,
  MAX_PARAMETER_SYSTEMS,
  MGB_PARAMETERS,
  BLIPTOASTER_SPECS,
  expandRomSpec,
  parametersForRole,
  preRoutingChannel,
  projectParameterMap,
  type ParameterSystem,
} from "../../src/parameterMap";

const mgb: ParameterSystem = { roles: [{ kind: "mgb" }] };
const bt = (chip: string): ParameterSystem => ({ roles: [{ kind: "bliptoaster", config: { chip } }] });
const plain: ParameterSystem = { roles: [{ kind: "lsdj" }] };

// An iNES header prefix with the given mapper number.
const inesHeader = (mapper: number): Uint8Array => {
  const h = new Uint8Array(16);
  h[0] = 0x4e; h[1] = 0x45; h[2] = 0x53; h[3] = 0x1a;
  h[6] = (mapper & 0x0f) << 4;
  h[7] = mapper & 0xf0;
  return h;
};

test("a single mGB system claims slots from 0, in table order", () => {
  const map = projectParameterMap([mgb], MidiRouting.SendToAll);
  expect(map.length).toEqual(MGB_PARAMETERS.length);
  expect(map[0].slot).toEqual(0);
  expect(map[0].name).toEqual("PU1 Pulse Width");
  expect(map[0].cc).toEqual(1);
  expect(map[0].channel).toEqual(0);
  // CC1/CC2 mean different things on WAV than on the pulses, per mGB's implementation map
  const shape = map.find((s) => s.name === "WAV Shape");
  expect(shape?.cc).toEqual(1);
  expect(shape?.channel).toEqual(2);
  expect(map.find((s) => s.name === "WAV Shape Offset")?.cc).toEqual(2);
});

test("mGB exposes the controls it previously dropped", () => {
  const names = MGB_PARAMETERS.map((p) => p.name);
  for (const n of ["PU1 Bend Range", "PU1 Load Preset", "PU1 Sustain", "Noise Pan", "PU1 Pitch Sweep"])
    expect(names.includes(n)).toEqual(true);
  // CC3 pitch sweep is PU1-only on the pulses (mGB's README), so PU2 must not have it
  expect(names.includes("PU2 Pitch Sweep")).toEqual(false);
});

test("BlipToaster 2A03 exposes the full core CC set, not a curated pick", () => {
  const names = expandRomSpec(BLIPTOASTER_SPECS["2a03"]).map((p) => p.name);
  // the ones the first cut dropped
  for (const n of [
    "Pulse 1 MOD Hack", "Pulse 1 MOD Rate", "Pulse 1 Fine Bend", "Pulse 1 Envelope Mode",
    "Pulse 1 Gate Length", "Pulse 1 Sweep Direction", "Pulse 1 Velocity Curve", "Pulse 1 Sustain Pedal",
    "Triangle Linear Reload", "Noise Envelope Rate", "DMC Wave Traveler", "DMC Traveler Speed",
    "DMC Traveler Step", "DMC Address Override", "DMC Start Address", "DMC Direct PCM", "DMC DAC Value",
  ])
    expect(names.includes(n)).toEqual(true);
  // ...and the ones that must never become lanes: panic messages and the RPN handshake
  for (const cc of [120, 121, 123, 100, 101, 6, 38])
    expect(expandRomSpec(BLIPTOASTER_SPECS["2a03"]).some((p) => p.cc === cc)).toEqual(false);
});

test("triangle has no volume and pulses have no linear reload", () => {
  const core = expandRomSpec(BLIPTOASTER_SPECS["2a03"]);
  expect(core.some((p) => p.name === "Triangle Volume")).toEqual(false);
  expect(core.some((p) => p.name === "Pulse 1 Linear Reload")).toEqual(false);
  expect(core.some((p) => p.name === "Triangle Linear Reload")).toEqual(true);
});

test("each BlipToaster build adds its own voices and controls", () => {
  const named = (chip: string) => expandRomSpec(BLIPTOASTER_SPECS[chip]).map((p) => p.name);
  expect(named("vrc6").includes("VRC6 Saw Overdrive")).toEqual(true);
  expect(named("vrc7").includes("FM 3 Volume")).toEqual(true);
  expect(named("s5b").includes("Square C Volume")).toEqual(true);
  expect(named("n163").includes("Wave 3 Volume")).toEqual(true);
  expect(named("mmc5").includes("MMC5 Pulse 2 Envelope Mode")).toEqual(true);
  // MMC5's squares have no sweep, unlike the 2A03 pulses they mirror
  expect(named("mmc5").includes("MMC5 Pulse 1 Sweep Rate")).toEqual(false);
  // the base build has none of them
  expect(named("2a03").some((n) => /VRC6|FM |Square [ABC]|Wave \d|MMC5/.test(n))).toEqual(false);
});

test("chip-global controls get ONE lane, not one per voice", () => {
  // The VRC7 custom patch is a single shared user instrument (OPLL has only one), and the S5B
  // envelope + noise generator are chip-wide. Duplicating them per voice would be a lie.
  const vrc7 = expandRomSpec(BLIPTOASTER_SPECS.vrc7);
  expect(vrc7.filter((p) => p.name === "FM Attack").length).toEqual(1);
  expect(vrc7.filter((p) => p.cc === 73).length).toEqual(1);
  const s5b = expandRomSpec(BLIPTOASTER_SPECS.s5b);
  expect(s5b.filter((p) => p.cc === 29).length).toEqual(1);
  // ...while a genuinely per-voice control is repeated
  expect(vrc7.filter((p) => p.cc === 7 && p.name.startsWith("FM ")).length).toEqual(6);
});

test("the LFO is per-voice, and tremolo reaches the expansion voices too", () => {
  // lfo_tick() in the ROM walks st = 0..CH_COUNT-1 reading _lfoRate[st] / _lfoShape[st] / _lfoPhase[st],
  // so each expansion voice really does have its own LFO - "FM 3 LFO Shape" is a real control, not an
  // over-expansion. The same loop dips expansion volume through exp_trem(), which is why tremolo depth
  // belongs on them as well; it was missing from the first cut of these tables.
  const vrc7 = expandRomSpec(BLIPTOASTER_SPECS.vrc7);
  for (const n of ["FM 3 LFO Shape", "FM 3 LFO Rate", "FM 3 Vibrato Depth", "FM 3 Tremolo Depth"])
    expect(vrc7.some((p) => p.name === n)).toEqual(true);
  // per-voice, so every FM channel has its own
  expect(vrc7.filter((p) => p.cc === 79 && p.name.startsWith("FM ")).length).toEqual(6);
  expect(vrc7.filter((p) => p.cc === 78 && p.name.startsWith("FM ")).length).toEqual(6);
  // ...and every other expansion build carries tremolo on its voices, including the VRC6 saw, whose
  // exp_trem() dips the sawtooth accumulator base
  const has = (chip: string, n: string) => expandRomSpec(BLIPTOASTER_SPECS[chip]).some((p) => p.name === n);
  expect(has("vrc6", "VRC6 Saw Tremolo Depth")).toEqual(true);
  expect(has("s5b", "Square C Tremolo Depth")).toEqual(true);
  expect(has("n163", "Wave 3 Tremolo Depth")).toEqual(true);
  expect(has("mmc5", "MMC5 Pulse 2 Tremolo Depth")).toEqual(true);
  // the 2A03 triangle has no volume, so no tremolo - the one voice that must NOT have it
  expect(has("2a03", "Triangle Tremolo Depth")).toEqual(false);
});

test("VRC7 drops CC15, which that build does not implement", () => {
  expect(expandRomSpec(BLIPTOASTER_SPECS.vrc7).some((p) => p.cc === 15)).toEqual(false);
  expect(expandRomSpec(BLIPTOASTER_SPECS["2a03"]).some((p) => p.cc === 15)).toEqual(true);
});

test("no ROM table overflows its system's slot budget", () => {
  // The projection truncates, so an over-long table silently loses entries instead of failing.
  const sizes: Record<string, number> = { mgb: MGB_PARAMETERS.length };
  for (const chip of Object.keys(BLIPTOASTER_SPECS)) sizes[chip] = expandRomSpec(BLIPTOASTER_SPECS[chip]).length;
  for (const [name, n] of Object.entries(sizes)) {
    if (n > CC_SLOTS_PER_SYSTEM) throw new Error(`${name} needs ${n} lanes, budget is ${CC_SLOTS_PER_SYSTEM}`);
    expect(n > 0).toEqual(true);
  }
});

test("a system with no parameter-bearing role claims nothing", () => {
  expect(projectParameterMap([plain], MidiRouting.SendToAll)).toEqual([]);
  expect(projectParameterMap([], MidiRouting.SendToAll)).toEqual([]);
  expect(parametersForRole({ kind: "lsdj" })).toEqual(undefined);
});

test("an unknown or absent chip falls back to the 2A03 core", () => {
  const core = expandRomSpec(BLIPTOASTER_SPECS["2a03"]).length;
  expect(parametersForRole({ kind: "bliptoaster", config: { chip: "fictional" } })?.length).toEqual(core);
  expect(parametersForRole({ kind: "bliptoaster", config: {} })?.length).toEqual(core);
  expect(parametersForRole({ kind: "bliptoaster" })?.length).toEqual(core);
});

test("the chip comes off the iNES mapper, with 69 resolving to the base build", () => {
  expect(inesMapper(inesHeader(85))).toEqual(85);
  expect(blipToasterChip(inesHeader(5))).toEqual("mmc5");
  expect(blipToasterChip(inesHeader(19))).toEqual("n163");
  expect(blipToasterChip(inesHeader(24))).toEqual("vrc6");
  expect(blipToasterChip(inesHeader(85))).toEqual("vrc7");
  // FME-7 is BOTH the base build's kit-banking mapper and the Sunsoft 5B mapper, and the two ROMs are
  // otherwise header-identical, so it resolves to the subset that is never wrong.
  expect(blipToasterChip(inesHeader(69))).toEqual("2a03");
  expect(blipToasterChip(inesHeader(0))).toEqual("2a03");
});

test("system N's slots start at N * CC_SLOTS_PER_SYSTEM", () => {
  const map = projectParameterMap([plain, bt("2a03")], MidiRouting.SendToAll);
  const core = expandRomSpec(BLIPTOASTER_SPECS["2a03"]).length;
  expect(map.length).toEqual(core);
  expect(map[0].slot).toEqual(CC_SLOTS_PER_SYSTEM);
  expect(map[map.length - 1].slot).toEqual(CC_SLOTS_PER_SYSTEM + core - 1);
});

test("every claimed slot stays inside the pool and is claimed once", () => {
  const map = projectParameterMap([bt("vrc7"), bt("mmc5"), mgb, bt("n163")], MidiRouting.SendToAll);
  for (const s of map) {
    expect(s.slot >= 0 && s.slot < MAX_PARAMETER_SYSTEMS * CC_SLOTS_PER_SYSTEM).toEqual(true);
    expect(s.cc >= 0 && s.cc <= 127).toEqual(true);
    expect(s.channel >= 0 && s.channel <= 15).toEqual(true);
  }
  expect(new Set(map.map((s) => s.slot)).size).toEqual(map.length);
});

test("systems past the pool's reach claim nothing", () => {
  const map = projectParameterMap([mgb, mgb, mgb, mgb, mgb], MidiRouting.SendToAll);
  const systems = new Set(map.map((s) => Math.floor(s.slot / CC_SLOTS_PER_SYSTEM)));
  expect([...systems].sort()).toEqual([0, 1, 2, 3]);
});

test("preRoutingChannel: SendToAll passes the ROM channel through", () => {
  expect(preRoutingChannel(MidiRouting.SendToAll, 0, 1, 0)).toEqual(0);
  expect(preRoutingChannel(MidiRouting.SendToAll, 0, 1, 10)).toEqual(10);
  expect(preRoutingChannel(MidiRouting.SendToAll, 1, 2, 3)).toEqual(3);
});

test("preRoutingChannel: FourChannelsPerInstance offsets by 4 and drops voices past the fourth", () => {
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 0, 2, 0)).toEqual(0);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 0, 2, 3)).toEqual(3);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 1, 2, 0)).toEqual(4);
  expect(preRoutingChannel(MidiRouting.FourChannelsPerInstance, 1, 2, 2)).toEqual(6);
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
  // FourChannelsPerInstance cannot address a ROM's fifth voice onward, so DMC and every VRC7 FM lane go.
  const four = projectParameterMap([bt("vrc7"), bt("vrc7")], MidiRouting.FourChannelsPerInstance);
  expect(four.some((s) => s.name.startsWith("DMC "))).toEqual(false);
  expect(four.some((s) => s.name.startsWith("FM "))).toEqual(false);
  expect(four.some((s) => s.name === "Pulse 1 Duty")).toEqual(true);
  // OneChannelPerInstance reaches only voice 1, so nothing but Pulse 1 survives.
  const one = projectParameterMap([bt("vrc7"), bt("vrc7")], MidiRouting.OneChannelPerInstance);
  expect(one.every((s) => s.name.startsWith("Pulse 1 "))).toEqual(true);
  expect(one.length > 0).toEqual(true);
});

test("claimed slots pack from 0 with no gaps, so dropping one does not shift the rest apart", () => {
  const map = projectParameterMap([bt("vrc7")], MidiRouting.FourChannelsPerInstance);
  expect(map.map((s) => s.slot)).toEqual(map.map((_, i) => i));
});
