// The case matrix the menu equality test records and replays (docs/modular-graph-phase-f4b-design.md section 7.5,
// item 1). Every case builds a project over MockBackend, composes the app's stores and builds the tree the menu shows:
// each console, each tracker cart with a readable ROM, a project that drives an N8 and one with a Launchpad running
// MI.MAP, a two-system project (the Link Group row), the start menu and the settings menus. The record is taken from
// the menu code before F4b moved any of it (a `git archive 8a74339b9` tree) and this module is the same code that
// replays it, so a row that changes placement, label, gate, order or the value a step writes fails there.
//
// A tree is recorded flattened (id, kind, label, disabled, children, and an inline row's verb list). A cycler is
// recorded the way a user reads it: its label after each step, beside the project state that step changed and the node
// MIDI it staged. The state is a diff of the whole reachable config (each system's role configs, the project settings,
// the user config), so a step that writes a different VALUE fails even when it writes the label its table shows, and a
// step that writes nothing (a host-only picker) records `{}`. The staged MIDI is its own list: which node each step
// aimed at and which bytes it sent, which is how a hardware row (BlipToaster's SysEx) is pinned as well as its label.
import { MockBackend } from "../../testing/mockBackend";
import { composeAppStores, type AppStores } from "../../src/appStores";
import { buildInstanceMenu, buildStartMenu, type MenuContext } from "../../ui/screens/menu/menuDefs";
import type { MenuItem } from "@retroplug/ui/menu";
import { resetN8CartJob, setN8LinkClient } from "@retroplug/n8/ui";
import {
  resetLaunchpadScanForTest, setLaunchpadClient, setLaunchpadScanClock, startLaunchpadScan,
} from "@retroplug/launchpad/ui";
import type { LaunchpadClient } from "@retroplug/launchpad/client";
import { ROM_SIZE, BANK_SIZE, PALETTE_SIZE, PALETTE_CHECK } from "@retroplug/lsdj/rom";
import { loadSongToWorking, savFrom, injectSong } from "@retroplug/lsdj";
import { normalizeSaveContainer } from "@retroplug/risa/risaSav";
import * as R from "@retroplug/lsdj/codec/regions";
import { savBytes as risaSav } from "@retroplug/risa/testFixtures";
import {
  blipToasterRom,
  gbRom,
  gbRomBattery,
  gbaRom,
  lsdjRom,
  nesRom,
  risaRom,
  smsggdjRom,
} from "../systems/fixtures";

/** One case: the project it builds and the tree it shows. */
export interface RecordedCase {
  readonly name: string;
  /** The menu the case records: the instance menu of its first system, or the start menu (which holds the settings). */
  readonly menu: "instance" | "start";
  /** Whether the case has the two native facets' fakes mounted. Off is a machine with no N8 and no Launchpad, which is
   *  the state most systems are in: both submenus then have to be ABSENT, which the tree records like any other row. */
  readonly n8?: boolean;
  readonly launchpad?: boolean;
  /** The mock host reports itself as the standalone, which is the only host that draws Settings > MIDI, Settings > Audio
   *  and the Exit rows. `midi`/`audio` add the native seams those submenus read (a stub, so the lists are fixed). */
  readonly standalone?: boolean;
  readonly midi?: boolean;
  readonly audio?: boolean;
  /** Leave a control-surface scan in flight while the menu is read (Settings > MIDI's scan row, and the instance
   *  submenu's disabled port rows). Needs `launchpad`. */
  readonly scanning?: boolean;
  build(backend: MockBackend, stores: AppStores): void;
}

/** One step of a cycler: the label it shows afterwards, the state it wrote (empty for a host-only picker) and the node
 *  MIDI it staged (empty for a row that sends none). */
export interface RecordedStep {
  readonly label: string;
  readonly stored: Readonly<Record<string, unknown>>;
  readonly staged: readonly { readonly nodeId: string; readonly bytes: readonly number[] }[];
}

export interface RecordedRow {
  readonly default: string;
  readonly steps: readonly RecordedStep[];
}

export interface RecordedCaseResult {
  readonly tree: readonly unknown[];
  readonly rows: Readonly<Record<string, RecordedRow>>;
  readonly dialogs: Readonly<Record<string, readonly unknown[]>>;
}

/** What the recorder writes and the equality test reads: one result per case, keyed by name. */
export type RecordedMatrix = Readonly<Record<string, RecordedCaseResult>>;

/** The `MenuContext` App.tsx assembles from its hooks, minus React. */
export function ctxOf(stores: AppStores): MenuContext {
  return {
    stores,
    settings: stores.project.settings(),
    userConfig: stores.userConfig.config(),
    bindings: stores.bindings.resolvedBindings(),
    systems: stores.project.systems.view(),
    recent: stores.recent.view(),
    version: "",
    newProject: () => {},
    loadProject: () => {},
    loadRomAsProject: () => {},
    requestExit: () => {},
    beginSongImport: () => {},
  };
}

/** The N8 link the mock tier has no provider for: one attached, connected port - what the submenu needs to render. */
const N8_CONFIG = {
  ports: [{ port: "/dev/ttyACM0", isN8: true }],
  selectedPort: "/dev/ttyACM0",
  connected: true,
  enabled: true,
  lookaheadMs: 10,
  bytes: 0,
  error: "",
};

/** The Launchpad facet the mock tier has no provider for: a claimed Pro MK3 pair, which is what makes the submenu
 *  render and its port rows show a live link. */
const LP_INPUTS = ["Arturia KeyStep 32", "LPProMK3 MIDI"];
const LP_OUTPUTS = ["LPProMK3 MIDI"];

function fakeLaunchpad(connected = true): LaunchpadClient {
  const status = {
    connected,
    generation: 1,
    sent: 0,
    dropped: 0,
    input: "LPProMK3 MIDI",
    output: "LPProMK3 MIDI",
    error: "",
  };
  // The loose-port pool a scan opens. Handles rather than refusals, so a scan that starts actually probes its first
  // output and waits out that output's answer window (the clock is frozen), which is the "in flight" state.
  const open = new Map<number, { input: boolean; name: string }>();
  let next = 1;
  const openPort = (input: boolean, name: string): number => {
    const handle = next++;
    open.set(handle, { input, name });
    return handle;
  };
  return {
    ports: () => ({ inputs: [...LP_INPUTS], outputs: [...LP_OUTPUTS] }),
    status: () => ({ ...status }),
    claim: () => ({ ...status }),
    release: () => ({ ...status }),
    send: () => true,
    restored: () => true,
    openInputs: (names: string[]) => names.map((name) => openPort(true, name)),
    openOutput: (name: string) => openPort(false, name),
    sendOn: () => true,
    read: () => ({ messages: [], dropped: 0 }),
    close: (handle: number) => open.delete(handle),
    portError: () => "",
  } as unknown as LaunchpadClient;
}

/** A GG or SMS cart: the Sega header read from the first bank, its region nibble the console (an SMS is 4, a Game Gear
 *  is 6). Nothing else in the image is a ROM any core boots; the menu only classifies it. */
function segaRom(region: number): Uint8Array {
  const bytes = new Uint8Array(0x8000);
  bytes.set([0x54, 0x4d, 0x52, 0x20, 0x53, 0x45, 0x47, 0x41], 0x7ff0); // "TMR SEGA"
  bytes[0x7ff0 + 0xf] = region << 4;
  return bytes;
}

/** Add a system at `path` from the backend's seeded bytes and return its id. */
function add(backend: MockBackend, stores: AppStores, path: string, rom: Uint8Array): number {
  backend.seed(path, rom);
  return stores.project.systems.addSystem(path)!;
}

/** A project loaded from a document whose core-role config was written by hand: adopt keeps a core role's config as
 *  stored (SystemsStore.parsedForTheirNodes skips the core role), which is exactly the hand-edited case the menu's
 *  fallbacks exist for. */
function adoptHandEdited(
  backend: MockBackend,
  stores: AppStores,
  path: string,
  rom: Uint8Array,
  core: string,
  config: Record<string, unknown>,
): void {
  backend.seed(path, rom);
  stores.project.systems.adopt({ romPath: path, core: core as never, roles: [{ kind: core, config }] });
}

/** The Launchpad's own state (its selected pair and app) is project/user state, so it is set through the stores the
 *  way the shell's rows would leave it. `appConfig` is the project's stored config as written, which is what a
 *  hand-edited project (or an older build) leaves behind. */
function launchpadProject(stores: AppStores, app: string, appConfig: Record<string, unknown> = {}): void {
  stores.userConfig.setControlSurface({
    enabled: true,
    input: "LPProMK3 MIDI",
    output: "LPProMK3 MIDI",
    device: "Launchpad Pro [MK3]",
  });
  stores.project.setController({ app, appConfig });
}

/** A full 1 MiB LSDj ROM (the title makes the provider attach lsdj-sync). */
function lsdjRomFull(): Uint8Array {
  const b = new Uint8Array(ROM_SIZE);
  b.set(gbRomBattery(), 0);
  const title = "LSDJ-V9.4.2";
  for (let i = 0; i < title.length; i++) b[0x134 + i] = title.charCodeAt(i);
  const count = 2;
  b.set(PALETTE_CHECK, 1 * BANK_SIZE + 0x100 + count * PALETTE_SIZE);
  const nb = 27 * BANK_SIZE + 0x200;
  for (let i = 0; i < 3 + 2 * count; i++) for (let j = 0; j < 4; j++) b[nb + i * 5 + j] = 0x41 + j;
  b[nb + 15 + 2 * count * 5 + 4] = 0x01;
  return b;
}

/** A battery with two songs at spare slots 0 and 3. */
function twoSongSav(): Uint8Array {
  const song = savFrom({ workingSong: { settings: { tempo: 120 } } }).subarray(0, 0x8000).slice();
  let sav = savFrom({});
  sav = injectSong(sav, 0, "AAA", 1, song)!;
  sav = injectSong(sav, 3, "BBB", 2, song)!;
  return sav;
}

export const CASES: readonly RecordedCase[] = [
  {
    name: "gb-default",
    menu: "instance",
    build: (be, s) => void add(be, s, "/roms/gb.gb", gbRom()),
  },
  {
    name: "gb-dmg",
    menu: "instance",
    build: (be, s) => {
      const id = add(be, s, "/roms/gb.gb", gbRom());
      s.project.systems.setRoleConfig(id, "sameboy", { model: "dmgB" });
    },
  },
  {
    name: "gb-sgb",
    menu: "instance",
    build: (be, s) => {
      const id = add(be, s, "/roms/gb.gb", gbRom());
      s.project.systems.setRoleConfig(id, "sameboy", { model: "sgb" });
    },
  },
  { name: "gba", menu: "instance", build: (be, s) => void add(be, s, "/roms/g.gba", gbaRom()) },
  { name: "nes", menu: "instance", build: (be, s) => void add(be, s, "/roms/g.nes", nesRom()) },
  { name: "sms", menu: "instance", build: (be, s) => void add(be, s, "/roms/g.sms", segaRom(4)) },
  { name: "gg", menu: "instance", build: (be, s) => void add(be, s, "/roms/g.gg", segaRom(6)) },
  {
    name: "nes-n8",
    menu: "instance",
    n8: true,
    build: (be, s) => void add(be, s, "/roms/g.nes", nesRom()),
  },
  {
    name: "nes-launchpad",
    menu: "instance",
    launchpad: true,
    build: (be, s) => {
      add(be, s, "/roms/g.nes", nesRom());
      launchpadProject(s, "lsdj-midimap");
    },
  },
  {
    name: "launchpad-cart-sync",
    menu: "instance",
    launchpad: true,
    build: (be, s) => {
      // An LSDj cart in SYNC MidiMap (8), its battery readable, and the project's controller on: the cart-sync row then
      // has an answer to show, which is the state MI.MAP is meant to drive.
      const id = add(be, s, "/roms/cart.gb", lsdjRom());
      const b = new Uint8Array(0x20000);
      b[0x813e] = 0x6a; // 'j'
      b[0x813f] = 0x6b; // 'k': a readable LSDj battery
      b[R.kModernRegions.syncMode] = 8;
      be.setSram(id, b);
      launchpadProject(s, "lsdj-midimap");
      s.project.setController({ enabled: true, target: "system" });
      s.project.refreshControllerSong();
    },
  },
  {
    name: "gb-multi-link",
    menu: "instance",
    build: (be, s) => {
      const a = add(be, s, "/roms/a.gb", gbRom());
      add(be, s, "/roms/b.gb", gbRom());
      s.project.systems.setRoleConfig(a, "sameboy", { linkGroupId: 2 });
    },
  },
  {
    name: "nes-multi",
    menu: "instance",
    build: (be, s) => {
      add(be, s, "/roms/a.nes", nesRom());
      add(be, s, "/roms/b.nes", nesRom());
    },
  },
  { name: "lsdj", menu: "instance", build: (be, s) => void add(be, s, "/roms/l.gb", lsdjRom()) },
  { name: "risa", menu: "instance", build: (be, s) => void add(be, s, "/roms/r.nes", risaRom()) },
  { name: "smsggdj", menu: "instance", build: (be, s) => void add(be, s, "/roms/s.sms", smsggdjRom()) },
  {
    name: "bliptoaster",
    menu: "instance",
    build: (be, s) => void add(be, s, "/roms/b.nes", blipToasterRom()),
  },
  {
    name: "hand-types",
    menu: "instance",
    build: (be, s) =>
      adoptHandEdited(be, s, "/roms/h.gb", gbRom(), "sameboy", {
        model: 42,
        highpass: false,
        fastBoot: "yes",
        colorCorrection: [],
        dmgPalette: null,
        lightTemperature: "warm",
        linkGroupId: "2",
      }),
  },
  {
    name: "hand-link",
    menu: "instance",
    build: (be, s) => {
      // The hand-edited system is adopted FIRST, so it is the one whose instance menu is recorded; the second system is
      // what makes the Link Group row a peer row at all.
      adoptHandEdited(be, s, "/roms/h.gb", gbRom(), "sameboy", { linkGroupId: "2" });
      add(be, s, "/roms/b.gb", gbRom());
    },
  },
  {
    name: "hand-values",
    menu: "instance",
    build: (be, s) =>
      adoptHandEdited(be, s, "/roms/h.gb", gbRom(), "sameboy", {
        model: "no-such-model",
        highpass: "no-such-filter",
        colorCorrection: "no-such-curve",
        dmgPalette: "no-such-palette",
        lightTemperature: 999,
        linkGroupId: 7,
      }),
  },
  {
    name: "hand-missing",
    menu: "instance",
    build: (be, s) => adoptHandEdited(be, s, "/roms/h.gb", gbRom(), "sameboy", {}),
  },
  {
    name: "hand-no-core-role",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/h.gb", gbRom());
      // A hand-edited project whose non-empty roles list holds no role of the system's core kind: the store never
      // re-adds it (withMissingFeatureRoles skips the core role), so the menu's core-config rows have no role to read.
      // Its record is the NEW menu's: base drew the six SameBoy rows anyway, and they were dead (setRoleConfig refuses
      // an absent role), which is design section 6's E13 edge.
      s.project.systems.adopt({ romPath: "/roms/h.gb", core: "sameboy", roles: [{ kind: "retro-role", config: {} }] });
    },
  },
  {
    name: "hand-nes",
    menu: "instance",
    build: (be, s) =>
      adoptHandEdited(be, s, "/roms/h.nes", nesRom(), "mesen", {
        region: "no-such-region",
        removeSpriteLimit: "yes",
        apuLatencyMs: 99,
        expansionVolume: -5,
        driveRealCart: 1,
        enableFm: "off",
      }),
  },
  {
    name: "lsdj-songs",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/songs.gb", lsdjRomFull());
      const id = s.project.systems.addSystem("/roms/songs.gb")!;
      // A linked, edited working song: the [working] row with Save Changes, which only a linked one gets.
      const edited = loadSongToWorking(twoSongSav(), 0)!;
      edited[0x100] ^= 0xff;
      be.setSram(id, edited);
    },
  },
  {
    name: "risa-songs",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/rsongs.nes", risaRom());
      const id = s.project.systems.addSystem("/roms/rsongs.nes")!;
      be.setSram(id, normalizeSaveContainer(risaSav("multi_legacy")).save);
    },
  },
  {
    name: "lsdj-override",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/rover.gb", lsdjRomFull());
      const id = s.project.systems.addSystem("/roms/rover.gb")!;
      s.project.systems.setRoleConfig(id, "lsdj-assets", {
        overrides: [{ type: "kit", slot: 0, name: "DRUMS", path: "/assets/drums.lsdk" }],
      });
    },
  },
  {
    name: "bliptoaster-pinned",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/bpin.nes", blipToasterRom());
      const id = s.project.systems.addSystem("/roms/bpin.nes")!;
      s.project.systems.setRoleConfig(id, "bliptoaster-assets", { settings: { theme: 2 } });
    },
  },
  {
    name: "lsdj-unreadable",
    menu: "instance",
    build: (be, s) => {
      be.seed("/roms/gone.gb", lsdjRomFull());
      s.project.systems.addSystem("/roms/gone.gb");
      // The file goes away after the system is up: a project whose ROM was moved or deleted still shows its submenu,
      // and every row that would read the image has to cope with not having it.
      be.unseed("/roms/gone.gb");
    },
  },
  {
    name: "nes-two-bliptoaster",
    menu: "instance",
    build: (be, s) => {
      add(be, s, "/roms/bt.nes", blipToasterRom());
      add(be, s, "/roms/other.nes", nesRom());
    },
  },
  { name: "start-empty", menu: "start", build: () => {} },
  {
    name: "start-one",
    menu: "start",
    build: (be, s) => void add(be, s, "/roms/gb.gb", gbRom()),
  },
  {
    name: "start-n8",
    menu: "start",
    n8: true,
    build: (be, s) => void add(be, s, "/roms/g.nes", nesRom()),
  },
  {
    name: "hand-controller",
    menu: "instance",
    launchpad: true,
    build: (be, s) => {
      add(be, s, "/roms/g.nes", nesRom());
      // A project whose controller config was written by hand (or by an older build): the rows show what they always
      // showed for it, which is not what the schema would default.
      launchpadProject(s, "lsdj-midimap", { quantise: 3, follow: "yes" });
    },
  },
  {
    name: "standalone-start",
    menu: "start",
    standalone: true,
    midi: true,
    audio: true,
    n8: true,
    build: (be, s) => void add(be, s, "/roms/g.nes", nesRom()),
  },
  {
    name: "standalone-instance",
    menu: "instance",
    standalone: true,
    midi: true,
    launchpad: true,
    build: (be, s) => {
      add(be, s, "/roms/g.nes", nesRom());
      launchpadProject(s, "lsdj-midimap");
    },
  },
  {
    name: "standalone-scan",
    menu: "instance",
    standalone: true,
    midi: true,
    launchpad: true,
    scanning: true,
    build: (be, s) => {
      add(be, s, "/roms/g.nes", nesRom());
      launchpadProject(s, "lsdj-midimap");
    },
  },
];

/** The native seams a standalone host installs, and what they answer here: fixed lists, so the device rows and their
 *  cyclers are recorded without a real audio or MIDI device. */
const AUDIO_SEAM = {
  sampleRate: 48000,
  blockSize: 512,
  outChannels: 2,
  driver: "Auto",
  device: "",
  drivers: ["Auto", "PipeWire"],
  devicesByDriver: { Auto: ["", "Built-in Audio"], PipeWire: ["", "Built-in Audio"] },
  defaultByDriver: { Auto: "Built-in Audio", PipeWire: "Built-in Audio" },
  autoDriver: "PipeWire",
};
const MIDI_SEAM = {
  inputs: ["Arturia KeyStep 32", "LPProMK3 MIDI"],
  outputs: ["LPProMK3 MIDI"],
  selectedInput: "",
  selectedOutput: "",
};

/** A tree with its children inline, which is what was recorded: every item's identity and gate, in order. */
export function flat(items: readonly MenuItem[]): unknown[] {
  return items.map((i) => ({
    id: i.id,
    kind: i.kind,
    label: i.label,
    ...(i.disabled === true ? { disabled: true } : {}),
    ...(i.children ? { children: flat(i.children) } : {}),
  }));
}

/** Every item of a tree, children first-consumed in document order. */
function walk(items: readonly MenuItem[], out: MenuItem[] = []): MenuItem[] {
  for (const i of items) {
    out.push(i);
    if (i.children) walk(i.children, out);
  }
  return out;
}

/** The project state a menu row can change, as one JSON-shaped object: each system's role configs (by position, so a
 *  run's allocated ids do not reach the record), the project settings and the user config. */
function snapshot(stores: AppStores): unknown {
  const systems: Record<string, unknown> = {};
  stores.project.systems.view().forEach((s, i) => {
    const roles: Record<string, unknown> = {};
    s.roles.forEach((r, j) => (roles[String(j)] = { kind: r.kind, config: r.config }));
    systems[`sys${i}`] = { roles };
  });
  return { systems, project: stores.project.settings(), user: stores.userConfig.config() };
}

/** A staging target by POSITION rather than by the id a run happened to allocate, so the record replays anywhere: a
 *  node a call aims at names its system ("sys-<id>", "cart-<id>"), and the record's system is its slot. */
function nodeKey(stores: AppStores, nodeId: string): string {
  const m = /^(sys|cart)-(\d+)$/.exec(nodeId);
  if (!m) return nodeId;
  const at = stores.project.systems.view().findIndex((s) => s.id === Number(m[2]));
  return at < 0 ? nodeId : `${m[1]}${at}`;
}

/** The paths at which `after` differs from `before`, with the value each holds after the step. */
function diff(before: unknown, after: unknown, path: string, out: Record<string, unknown>): void {
  if (JSON.stringify(before) === JSON.stringify(after)) return;
  const both =
    before !== null &&
    after !== null &&
    typeof before === "object" &&
    typeof after === "object" &&
    !Array.isArray(before) &&
    !Array.isArray(after);
  if (!both) {
    out[path] = after === undefined ? "<absent>" : after;
    return;
  }
  const a = before as Record<string, unknown>;
  const b = after as Record<string, unknown>;
  for (const key of new Set([...Object.keys(a), ...Object.keys(b)]))
    diff(a[key], b[key], path ? `${path}.${key}` : key, out);
}

/** The rows that open a file browser: a card operation's filter is part of the menu, so its dialog options are
 *  recorded beside the tree. */
const CARD_DIALOG_ROWS = new Set(["n8-load-rom", "n8-dump-sram", "n8-restore-sram"]);

/** One case: its tree, then each cycler it shows, stepped one full cycle and read the way a user reads it. */
function recordCase(c: RecordedCase): RecordedCaseResult {
  // The mock tier has no native host, so the two facets and the standalone seams the submenus gate on are stubbed here
  // and cleared after; a case that does not build a system still gets them (the start menu shows the N8 block from the
  // same seam).
  resetN8CartJob();
  resetLaunchpadScanForTest();
  setLaunchpadScanClock(() => 0);
  const globals = globalThis as Record<string, unknown>;
  const seams = { standalone: globals.__rp_isStandalone, midi: globals.__rp_getMidiConfig, audio: globals.__rp_getAudioConfig };
  if (c.standalone === true) globals.__rp_isStandalone = true;
  if (c.midi === true) globals.__rp_getMidiConfig = () => ({ ...MIDI_SEAM });
  if (c.audio === true) globals.__rp_getAudioConfig = () => ({ ...AUDIO_SEAM });
  if (c.n8 === true)
    setN8LinkClient({
      linkConfig: () => ({ ...N8_CONFIG }),
      setPort: () => {},
      connect: () => {},
      setLookahead: () => {},
    });
  // A scan is refused while the link holds a pair (those ports cannot be reopened), so the scanning case's device is
  // enumerated by its port name and not yet claimed, which is the state Settings > MIDI's scan exists for.
  if (c.launchpad === true) setLaunchpadClient(fakeLaunchpad(c.scanning !== true));
  if (c.scanning === true) startLaunchpadScan();
  try {
    const be = new MockBackend("/cfg");
    const stores = composeAppStores({ backend: be });
    c.build(be, stores);
    const tree = (): MenuItem[] => {
      const ctx = ctxOf(stores);
      if (c.menu === "start") return buildStartMenu(ctx).items;
      return buildInstanceMenu({ ...ctx, system: stores.project.systems.view()[0] }).items;
    };

    const recorded = flat(tree());
    const rows: Record<string, RecordedRow> = {};
    // Every cycler the tree shows, in document order. A row that a step gates away ends its own cycle there: the row
    // is gone, so there is no next label to read.
    for (const id of walk(tree())
      .filter((i) => i.kind === "cycler")
      .map((i) => i.id)) {
      if (rows[id]) continue;
      const first = walk(tree()).find((i) => i.id === id);
      if (!first) continue;
      const start = first.label;
      const steps: RecordedStep[] = [];
      for (let n = 0; n < 60; n++) {
        const row = walk(tree()).find((i) => i.id === id);
        if (!row) break;
        const before = snapshot(stores);
        const stagedFrom = be.stageNodeMidiCalls.length;
        row.onCycle!(1);
        const stored: Record<string, unknown> = {};
        diff(before, snapshot(stores), "", stored);
        const staged = be.stageNodeMidiCalls.slice(stagedFrom).map((call) => ({
          nodeId: nodeKey(stores, call.nodeId),
          bytes: call.bytes,
        }));
        const next = walk(tree()).find((i) => i.id === id);
        if (!next) {
          steps.push({ label: "<row gone>", stored, staged });
          break;
        }
        steps.push({ label: next.label, stored, staged });
        if (next.label === start) break;
      }
      rows[id] = { default: start, steps };
    }
    // The card rows' file dialogs: the options each opens with (title, filter, save flag, default name). A filter is
    // how a hardware operation names what it may upload, so it is part of what the menu must not change; selecting a
    // row is enough, because the mock browser answers null and the operation behind the dialog never runs.
    const dialogs: Record<string, readonly unknown[]> = {};
    for (const row of walk(tree()).filter((i) => i.kind === "action" && CARD_DIALOG_ROWS.has(i.id))) {
      const from = be.fileBrowserCalls.length;
      row.onSelect!();
      if (be.fileBrowserCalls.length > from) dialogs[row.id] = be.fileBrowserCalls.slice(from);
    }
    return { tree: recorded, rows, dialogs };
  } finally {
    setN8LinkClient(undefined);
    setLaunchpadClient(undefined);
    resetN8CartJob();
    resetLaunchpadScanForTest();
    globals.__rp_isStandalone = seams.standalone;
    globals.__rp_getMidiConfig = seams.midi;
    globals.__rp_getAudioConfig = seams.audio;
  }
}

/** The whole matrix, in case order. */
export function collect(): RecordedMatrix {
  const out: Record<string, RecordedCaseResult> = {};
  for (const c of CASES) out[c.name] = recordCase(c);
  return out;
}
