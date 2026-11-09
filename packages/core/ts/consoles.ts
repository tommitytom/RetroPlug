// The console contract (docs/modular-graph-phase-f4a-design.md section 3): what a console package contributes so the
// shell can classify a ROM, place a system's node, wire its knobs and name its streams without knowing which console it
// is looking at. A descriptor is per graph NODE TYPE - the tables graph/buildGraph.ts keys by node type are the node's,
// and a Master System and a Game Gear share one - and lists the platforms that node serves.
//
// `Platform` and `Core` are the two axes a loaded system carries, and they are open string aliases: the registered
// descriptors are the authority for which ids exist (the shell validates a stored project's platform against them),
// and a closed union here would put the list of consoles in the contract, which is what the split removes. They stay
// declared in ./platform.ts, which the role contract and ConstructSpec import; this module re-exports them so a console
// package names them from one place.
import type { Core, Platform } from "./platform";

export type { Core, Platform } from "./platform";

/** A field's validator: whether the value is one the console's node takes. */
export type Knob = (v: unknown) => boolean;

/** A knob that takes one of `values` (a settings enum's labels, the position in the tuple the native ordinal). */
export const label = (values: readonly string[]): Knob => (v) => typeof v === "string" && values.includes(v);

/** A knob that takes a boolean. */
export const flag: Knob = (v) => typeof v === "boolean";

/** A knob that takes a number in `min`..`max` inclusive. */
export const within =
  (min: number, max: number): Knob =>
  (v) =>
    typeof v === "number" && v >= min && v <= max;

/** A knob that takes a whole number in `min`..`max`. Native reads a construct-time int with static_cast<int>, so a
 *  fraction is a value its node cannot carry faithfully: left out like any other value it would refuse, rather than
 *  silently truncated. */
export const intIn =
  (min: number, max: number): Knob =>
  (v) =>
    within(min, max)(v) && Number.isInteger(v);

/** One platform a console's node serves. */
export interface ConsolePlatform {
  readonly id: Platform;
  /** "Game Boy", "Game Gear": the names error text and menus use. */
  readonly label: string;
  /** Lowercase, dot included: [".gb", ".gbc"]. */
  readonly extensions: readonly string[];
  /** Settings the node gets for this platform alone, beside the knobs: a Game Gear's { gameGear: true }. */
  readonly nodeSettings?: Readonly<Record<string, unknown>>;
  /** True where the extension alone classifies a file with no recognised content (headerless SMS/GG homebrew). */
  readonly extensionOnly?: boolean;
  /** The core-config field that opts a system of this platform out of driving a registered real cart. */
  readonly realCartOptOut?: string;
}

/** How a console's content is recognised in a ROM's prefix. */
export interface ConsoleDetect {
  /** The bytes of a file's prefix this detector runs on. The shell reads each distinct length once, shortest first,
   *  and stops at the first answer. */
  readonly prefixBytes: number;
  /** Tiebreak among detectors that both match one prefix: lower first, so the order is data rather than the product
   *  list's. Two detectors of one rank run in their descriptors' id order, which makes the whole order total. */
  readonly rank: number;
  /** The platform the content names, or null. `ext` (lowercase, dot included) is passed on the deepest tier only, and
   *  only to a detector whose own `prefixBytes` is the deepest registered prefix, so classifyRom's tiered walk and
   *  detectPlatform's whole-buffer one show it to the same detectors; it may only break a tie the content cannot (a
   *  Sega header with an unfilled region nibble) and never overrides a match. */
  run(bytes: Uint8Array, ext: string | undefined): Platform | null;
}

/** One stream a core reports. */
export interface ConsoleStream {
  readonly port: string;
  readonly stereo: boolean;
}

/** A split of a console's output into one file per stream. */
export interface ConsoleSplit {
  /** Output file suffixes in stream order ("pulse1" ...), what a render names a split's files with. */
  readonly names: readonly string[];
  /** The core-config patch the split needs: { channelExportMode: "individualMono" } for a NES's channels. */
  readonly configPatch?: Readonly<Record<string, unknown>>;
  /** The core config's export mode this split builds a node in, as the native enum's ordinal: 1 pins, 3 channels.
   *  The engine arms it as the tap mode for a lone system under this split. */
  readonly tapMode?: number;
}

/** What a console's node can be asked for beyond its knobs. */
export interface ConsoleCapabilities {
  /** The core config field naming a cable group, for a console whose core can be linked to another instance. */
  readonly linkCable?: { readonly field: string };
  /** The core can capture its serial out for a decoder node to read. */
  readonly serialOut?: true;
  /** The engine's split tap can arm on it (a NES's pins or its five channels). */
  readonly channelTap?: true;
}

/** Everything about how a console's output is rendered. */
export interface ConsoleRender {
  /** The lanes a "mix" render writes: a stereo console's two, or the left lane alone for a mono console. */
  readonly mixLanes: "stereo" | "left";
  /** The button a fixed-length render presses to start the cart's playback (a player's Start), for a console whose
   *  carts start that way. */
  readonly defaultPlay?: { readonly button: number };
}

/** A console's contribution: one per graph node type, listing the platforms that node serves. */
export interface ConsoleDescriptor {
  readonly id: string;
  readonly core: Core;
  readonly nodeType: string;
  readonly platforms: readonly ConsolePlatform[];
  /** Absent: found by extension alone, or never (none today). */
  readonly detect?: ConsoleDetect;
  /** The prefix the ROM providers are shown for this console (0x8200 for Sega, whose header is at a bank's end);
   *  absent: 0x150. */
  readonly providerHeaderBytes?: number;
  /** Absent: true, so a save is never wrongly greyed (GBA/SMS/GG carry no battery bit a ROM can be read for). */
  readonly hasBattery?: (header: Uint8Array, platform: Platform) => boolean;
  /** The setting fields the node takes from its core config, and what it takes for each. */
  readonly settingFields?: Readonly<Record<string, Knob>>;
  /** The fields the node reads once, at the build: whether it takes the stored value, and the value written when it does
   *  not (the node's own default). */
  readonly constructFields?: Readonly<Record<string, { readonly takes: Knob; readonly fallback: unknown }>>;
  /** The params the node takes from its core config, beside the gain every node takes from the system. */
  readonly configParams?: readonly string[];
  /** The streams a core reports in `mode` (an export mode; ignored by a console with one layout); absent: one stereo
   *  "mix". */
  readonly streams?: (mode: number) => readonly ConsoleStream[];
  /** The core config's export-mode setting for a console whose core can be built in more than one output layout: the
   *  field that names one, and its labels in the native enum's order, whose index is the `mode` `streams` is asked for
   *  (index 0, the mix, is what a config naming no readable label gives). A split's `tapMode` is the index of the label
   *  its `configPatch` names. */
  readonly exportModes?: { readonly field: string; readonly values: readonly string[] };
  readonly capabilities?: ConsoleCapabilities;
  readonly splits?: { readonly channels?: ConsoleSplit; readonly pins?: ConsoleSplit };
  /** How a render of this console shapes its mix, where a console declares it (the Game Boy stereo, the NES in the left
   *  lane). Absent (the GBA and the Sega consoles): the mix is stereo and the render presses no button; a cart that
   *  needs a gesture supplies it through its own render hooks. */
  readonly render?: ConsoleRender;
}

/** A physical cart a project can drive, and the node that is it: the console's platform is the one the cart receives
 *  what that platform's system sends. One cart per platform and one per node, which the generated list refuses
 *  otherwise. */
export interface RealCart {
  readonly platform: Platform;
  readonly cartNode: string;
}

/** A `retroplug.consoles` module's default export: the consoles a package's cores run and the physical carts one of
 *  them can drive. Each list is optional, and the module imports only the package's own enum modules and this contract,
 *  so the planner reads the descriptors without an app module or zod. */
export interface ConsolesContribution {
  readonly consoles?: readonly ConsoleDescriptor[];
  readonly realCarts?: readonly RealCart[];
}
