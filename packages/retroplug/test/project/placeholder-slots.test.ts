// A placeholder holds its slot in the graph and in the DAW's parameter pool (docs/downloadable-packs-m3-design.md
// section 1: the routing and layout settings that point at a system's slot keep working). The same file is opened
// twice, before and after a fixture pack provides the placeholder's platform, and the system after the placeholder is
// compared: its MIDI routing position (and so its channel), its host channels and its automation lanes are the same in
// both.
import { test, expect } from "@retroplug/testing";
import type { ConsoleDescriptor } from "@retroplug/core/consoles";
import type { RoleType } from "@retroplug/core/systemRoles";
import type { GraphDocument } from "@retroplug/core/graphDocument";
import { MidiRouting } from "@retroplug/core/midiRouting";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { commitContributions } from "../../src/contributions/registry";
import { consolesRegistry } from "../../src/contributions/consoleRegistries";
import { appAdditions } from "../../src/contributions/app";
import { CC_SLOTS_PER_SYSTEM, projectParameterMap } from "../../src/parameterMap";

const MAGIC = [0x50, 0x53, 0x4c, 0x21]; // "PSL!"

const DOC = {
  schemaVersion: "4",
  settings: { midiRouting: MidiRouting.MidiChannelToInstance, audioRouting: "twoPerInstance" },
  systems: [
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
    {
      platform: "pslot",
      core: "pslot-core",
      romPath: "/roms/song.pslot",
      roles: [{ kind: "pslot-core", config: {} }],
    },
    { platform: "gb", core: "sameboy", embeddedRom: "mgb" },
  ],
};

/** Open DOC in a fresh store on eight host channels, and read what slot 2's system has. */
function open() {
  const be = new MockBackend("/cfg");
  be.seed("/roms/song.pslot", new Uint8Array([...MAGIC, 0, 0, 0, 0]));
  be.seed("/proj/slots.rplg", JSON.stringify(DOC));
  const registry = buildAppRegistry();
  const p = new ProjectStore(be, new RecentStore(be), registry);
  expect(p.graph.setShape({ kind: "multi", channels: 8 })).toBe(true);
  expect(p.load("/proj/slots.rplg").kind).toBe("loaded");
  const slots = p.systems.slotView();
  const last = slots[2];
  const request = be.graphRequests[be.graphRequests.length - 1];
  const document = JSON.parse(request.document) as GraphDocument;
  const routing = document.nodes.find((n) => n.id === "routing")!.settings as { systems: number[] };
  const into = (port: string): string[] => document.wires.filter((w) => w.to === port).map((w) => w.from);
  return {
    held: slots.map((v) => v.placeholder !== undefined),
    nativeSlots: request.slots.length,
    routingIndex: routing.systems.indexOf(last.id),
    routingCount: routing.systems.length,
    // twoPerInstance on eight channels: slot 2's left and right on host channels 5 and 6.
    left: into("out.out.5").includes(`split-${last.id}.out1`),
    right: into("out.out.6").includes(`split-${last.id}.out2`),
    lanes: projectParameterMap(slots, p.settings().midiRouting, registry)
      .filter((s) => s.slot >= 2 * CC_SLOTS_PER_SYSTEM)
      .map((s) => [s.slot, s.channel]),
    placeholderLanes: projectParameterMap(slots, p.settings().midiRouting, registry).filter(
      (s) => s.slot >= CC_SLOTS_PER_SYSTEM && s.slot < 2 * CC_SLOTS_PER_SYSTEM,
    ).length,
  };
}

test("a system after a placeholder keeps its MIDI position, its host channels and its automation lanes", () => {
  const missing = open();
  expect(missing.held).toEqual([false, true, false]);
  expect(missing.nativeSlots).toBe(2); // the engine's system table names only what runs
  expect(missing.placeholderLanes).toBe(0); // the empty slot claims no lane, and keeps its pool range
  commitContributions([
    {
      registry: consolesRegistry,
      pkg: "pslot-pack",
      items: [{
        id: "pslot-console",
        core: "pslot-core",
        nodeType: "rp.pslot.system",
        platforms: [{ id: "pslot", label: "PSLOT", extensions: [".pslot"] }],
        detect: {
          prefixBytes: 4,
          rank: 950,
          run: (b: Uint8Array) => (MAGIC.every((m, i) => b[i] === m) ? "pslot" : null),
        },
      } as unknown as ConsoleDescriptor],
    },
    ...appAdditions("pslot-pack", {
      roles: [
        {
          kind: "pslot-core",
          category: "system",
          schema: { parse: (raw: unknown) => ({ ...(raw as object) }) },
        } as unknown as RoleType,
      ],
    }),
  ]);
  const loaded = open();
  expect(loaded.held).toEqual([false, false, false]);
  for (const build of [missing, loaded]) {
    expect(build.routingIndex).toBe(2);
    expect(build.routingCount).toBe(3);
    expect(build.left && build.right).toBe(true);
    expect(build.lanes.length > 0).toBe(true);
    expect(build.lanes.every(([slot, channel]) => slot < 3 * CC_SLOTS_PER_SYSTEM && channel === 2)).toBe(true);
  }
  expect(missing.lanes).toEqual(loaded.lanes);
});
