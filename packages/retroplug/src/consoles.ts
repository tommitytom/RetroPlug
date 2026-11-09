// The console registry: the shell's one read of the consoles its packages contribute
// (docs/modular-graph-phase-f4a-design.md section 3). The descriptors are the generated list in
// ./generated/consoles.ts, in product order, and this module turns them into the lookups the shell makes: which core
// runs a platform, which node type places it, what a ROM's bytes are, whether a cart has a battery, and which physical
// cart a platform's systems can drive.
//
// src/platform.ts stays the module the callers know (defaultCoreFor, systemNodeTypeFor, detectPlatform, romHasBattery,
// the two sniff lengths), with the same names over these lookups.
import { CONSOLES, REAL_CARTS } from "./generated/consoles";
import type { ConsoleDescriptor, ConsolePlatform, Core, Platform, RealCart } from "@retroplug/core/consoles";
import { extensionLower } from "./pathUtil";

export type {
  ConsoleDescriptor,
  ConsolePlatform,
  ConsoleSplit,
  Core,
  Platform,
  RealCart,
} from "@retroplug/core/consoles";

/** Every registered descriptor, in product order. */
export const CONSOLE_DESCRIPTORS: readonly ConsoleDescriptor[] = CONSOLES;

/** The node types the classifier and the node tables use, by platform and by node type: one map each, read when the
 *  module loads, so a classification never walks the list. */
const BY_PLATFORM = new Map<Platform, { platform: ConsolePlatform; descriptor: ConsoleDescriptor }>();
const BY_NODE_TYPE = new Map<string, ConsoleDescriptor>();
const PLATFORMS: ConsolePlatform[] = [];
const CORES: Core[] = [];
const DETECTORS: { descriptor: ConsoleDescriptor; detect: NonNullable<ConsoleDescriptor["detect"]> }[] = [];
const REAL_CART_BY_PLATFORM = new Map<Platform, RealCart>();

for (const descriptor of CONSOLES) {
  BY_NODE_TYPE.set(descriptor.nodeType, descriptor);
  if (!CORES.includes(descriptor.core)) CORES.push(descriptor.core);
  if (descriptor.detect) DETECTORS.push({ descriptor, detect: descriptor.detect });
  for (const platform of descriptor.platforms) {
    BY_PLATFORM.set(platform.id, { platform, descriptor });
    PLATFORMS.push(platform);
  }
}
for (const cart of REAL_CARTS) REAL_CART_BY_PLATFORM.set(cart.platform, cart);

/** The distinct prefix lengths the detectors run on, ascending: a file is read once per length, shortest first, and the
 *  read stops at the first answer (see classifyRom). */
const SNIFF_LENGTHS: readonly number[] = [...new Set(DETECTORS.map(({ detect }) => detect.prefixBytes))].sort(
  (a, b) => a - b,
);

/** A path's extension, lowercase and dot included, or undefined when it has none: the shell's own `path::extension`
 *  (a dotfile has none), which the registry's callers and the classification read the same way. */
function extensionOf(path: string): string | undefined {
  const ext = extensionLower(path);
  return ext === "" ? undefined : ext;
}

/** The descriptor that places a system of `nodeType`, or undefined for a type no console registers (a document's node
 *  `type` is a string, so a miss is expected rather than exceptional). */
export function consoleFor(nodeType: string): ConsoleDescriptor | undefined {
  return BY_NODE_TYPE.get(nodeType);
}

/** The descriptor whose node serves `platform`. */
export function consoleOfPlatform(platform: Platform): ConsoleDescriptor | undefined {
  return BY_PLATFORM.get(platform)?.descriptor;
}

/** One platform as its console declares it: its extensions, the settings a Game Gear needs, whether the extension alone
 *  may classify it, and the core-config field that opts it out of driving a real cart. */
export function platformEntry(platform: Platform): ConsolePlatform | undefined {
  return BY_PLATFORM.get(platform)?.platform;
}

/** Every registered platform, in product order. */
export function registeredPlatforms(): readonly ConsolePlatform[] {
  return PLATFORMS;
}

/** Every core a descriptor names, in product order, first registration first. */
export function registeredCores(): readonly Core[] {
  return CORES;
}

/** The shortest and longest prefix a detector runs on: the two reads a classification makes. */
export function sniffLengths(): readonly number[] {
  return SNIFF_LENGTHS;
}

/** Every extension the registered consoles read, lowercase and dot included, in the order their platforms register in
 *  (the product's, then the descriptor's) and without repeats: the extension lists the file dialogs and the sibling-ROM
 *  probe use, so a platform's place there is behaviour. */
export function romExtensions(): readonly string[] {
  const out: string[] = [];
  for (const platform of PLATFORMS) {
    for (const ext of platform.extensions) if (!out.includes(ext)) out.push(ext);
  }
  return out;
}

/** The core that runs `platform`, or undefined for a platform no console registers. */
export function defaultCoreFor(platform: Platform): Core | undefined {
  return BY_PLATFORM.get(platform)?.descriptor.core;
}

/** The node type that places a system of `platform`, or undefined for a platform no console registers. */
export function systemNodeTypeFor(platform: Platform): string | undefined {
  return BY_PLATFORM.get(platform)?.descriptor.nodeType;
}

/** The capabilities a platform's console declares (a link cable, a serial-out capture, the split tap), or an empty set
 *  for a platform no descriptor registers: what a shell feature that needs one asks about the CONSOLE, rather than
 *  naming it. */
export function platformCapabilities(platform: Platform): NonNullable<ConsoleDescriptor["capabilities"]> {
  return BY_PLATFORM.get(platform)?.descriptor.capabilities ?? {};
}

/** Every registered physical cart, in product order. */
export function registeredRealCarts(): readonly RealCart[] {
  return REAL_CARTS;
}

/** The registered physical cart for `platform`, if one is: at most one, which the generated list refuses otherwise, so
 *  a caller resolving a platform's cart and a caller walking the list agree. */
export function realCartOf(platform: Platform): RealCart | undefined {
  return REAL_CART_BY_PLATFORM.get(platform);
}

/** `answer`, held to the platforms the descriptor that gave it lists: a detector that names a platform its own
 *  descriptor does not declare is a package's typo, and left to reach the shell it would be classified and then refused
 *  by systemNodeTypeFor, with nothing naming the console. Exported because the registry's list is the product's, so a
 *  console that misdeclares itself is shown here (test/consoles/registry.test.ts) rather than by editing a manifest. */
export function detectorAnswer(descriptor: ConsoleDescriptor, answer: Platform): Platform {
  if (descriptor.platforms.some((platform) => platform.id === answer)) return answer;
  throw new Error(
    `the console ${descriptor.id} detects ${JSON.stringify(answer)}, which its platforms do not list ` +
      `(${descriptor.platforms.map((platform) => platform.id).join(", ")})`,
  );
}

/** Classify `bytes` as a registered platform, or `"unknown"`.
 *
 *  `ext` (lowercase, dot included) is consulted ONLY where the content genuinely can't decide: which Sega machine a
 *  header with an unfilled region nibble means, and - as the last tier of all - a file with no recognised magic
 *  anywhere, which is how headerless SMS/GG homebrew loads. It can never override a positive content match, so a
 *  `.sms` file that is really a `.nes` still classifies `nes`. Omit it to classify on bytes alone.
 *
 *  Reading whole buffers, it runs every detector over the one buffer, in rank order (so the order of the answers is
 *  data, not the product list's); classifyRom is the reader that pays for each prefix once. */
export function detectPlatform(bytes: Uint8Array, ext?: string): Platform | "unknown" {
  const answer = detectAt(bytes, SNIFF_LENGTHS[SNIFF_LENGTHS.length - 1], true, ext);
  return answer ?? extensionOnly(ext) ?? "unknown";
}

/** Whether a cart has battery-backed save memory, read from its `header`. Only ever returns `false` when we're certain
 *  there's no save, so a real battery/LSDj cart is never wrongly greyed: a platform whose console declares no rule, and
 *  one no console registers, default to `true`. A Sega 8-bit cart carries NO battery bit in its header - the cart
 *  either wires up SRAM or it doesn't - and smsggdj IS a battery cart whose songs live in that SRAM, so `false` there
 *  would grey out the only rows that can save a user's work. */
export function romHasBattery(header: Uint8Array, platform: Platform): boolean {
  const rule = BY_PLATFORM.get(platform)?.descriptor.hasBattery;
  return rule ? rule(header, platform) : true;
}

/** The platform whose console lists `ext` (lowercase, dot included), or undefined when none does: classification by a
 *  file's CONTENT is the rule, so this only stands in where there is no content to read (render.ts's edge E1). */
export function platformForExtension(ext: string): Platform | undefined {
  for (const platform of PLATFORMS) if (platform.extensions.includes(ext)) return platform.id;
  return undefined;
}

/** What a platform's ROM providers are shown: the console's own prefix where its header sits deep (Sega's does), and
 *  the short header everywhere else. */
export function providerHeaderBytes(platform: Platform): number | undefined {
  return BY_PLATFORM.get(platform)?.descriptor.providerHeaderBytes;
}

/** A reader of a file's first `length` bytes: the backend's readFilePrefix, narrowed to what a classification needs. */
export interface PrefixReader {
  readFilePrefix(path: string, length: number): Uint8Array | null;
}

/** Classify a ROM file's platform from its header only - the one place ROM bytes enter TS. Native never classifies.
 *
 *  The read is in TIERS, because the Nintendo headers sit at the START of the file while a Sega 8-bit header sits at the
 *  end of the first bank: reading that far on every classify would make GB/NES/GBA - the overwhelmingly common case - pay
 *  33 KB to find a logo in the first 0x134 bytes. So each distinct prefix length is read once, shortest first, and the
 *  deep read happens only for a file the cheap tier already declined. The reader truncates to the real file size, so
 *  asking for more than a file holds is safe. */
export function classifyRom(reader: PrefixReader, romPath: string): Platform | "unknown" {
  const ext = extensionOf(romPath);
  for (const length of SNIFF_LENGTHS) {
    // One read per tier and none after the first answer: the lengths ascend, so a tier is reached only when every
    // shorter one declined.
    const bytes = reader.readFilePrefix(romPath, length) ?? new Uint8Array();
    const answer = detectAt(bytes, length, length === SNIFF_LENGTHS[SNIFF_LENGTHS.length - 1], ext);
    if (answer !== null) return answer;
  }
  return extensionOnly(ext) ?? "unknown";
}

/** One tier of a classification: the detectors that read `length` bytes or fewer, in rank order, over one buffer. The
 *  extension is shown only at the deepest tier and only to the detectors that read that far (`ConsoleDetect.run`'s
 *  contract), so classifyRom, which walks the tiers, and detectPlatform, which hands over one whole buffer, show it to
 *  the same detectors and cannot disagree. */
function detectAt(bytes: Uint8Array, length: number, last: boolean, ext: string | undefined): Platform | null {
  const deepest = SNIFF_LENGTHS[SNIFF_LENGTHS.length - 1];
  for (const { descriptor, detect } of RANK_ORDERED) {
    if (detect.prefixBytes > length) continue;
    // The extension breaks a tie only at the tier whose header sits deepest (the Sega header's region nibble): a
    // detector reading a shorter prefix answers on content alone.
    const answer = detect.run(bytes, last && detect.prefixBytes === deepest ? ext : undefined);
    if (answer !== null) return detectorAnswer(descriptor, answer);
  }
  return null;
}

/** The detectors in rank order: the tiebreak among detectors that both match one prefix, so a crafted file carrying two
 *  consoles' signatures classifies the same whatever order the product lists the packages in. A rank two descriptors
 *  share is broken by their ids, which makes the order total (and the sort's answer independent of the engine's). */
const RANK_ORDERED = [...DETECTORS].sort((a, b) => {
  if (a.detect.rank !== b.detect.rank) return a.detect.rank - b.detect.rank;
  return a.descriptor.id < b.descriptor.id ? -1 : a.descriptor.id > b.descriptor.id ? 1 : 0;
});

/** The one extension-only platform `ext` names, if any: the last tier of all, for a file whose content says nothing
 *  (headerless SMS/GG homebrew). */
function extensionOnly(ext: string | undefined): Platform | null {
  if (ext === undefined) return null;
  for (const platform of PLATFORMS) {
    if (platform.extensionOnly && platform.extensions.includes(ext)) return platform.id;
  }
  return null;
}
