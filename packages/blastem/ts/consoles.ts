// The Mega Drive's descriptor (docs/megadrive-blastem-design.md section 1): what the shell needs to classify a Mega
// Drive ROM, place the node that runs it, and wire its knobs, without naming the console. Detection is the header at
// $100, never the extension: a Master System `.bin` has no "SEGA" there and is not this platform, and a `.md` file that
// is not one is refused rather than booted as noise.
import { MD_REGION_VALUES } from "./enums";
import { label, type ConsolesContribution, type ConsoleDescriptor } from "@retroplug/core/consoles";

// ONE rule, the native gate's: "SEGA" at $100, and not "SEGA 32X" (a different console this core does not run). A
// real header's next twelve bytes name the machine ("SEGA MEGA DRIVE ", "SEGA GENESIS    ", or a homebrew's own words),
// so matching the whole 16 refuses carts the native gate accepts; the four bytes are what both sides read.
const MAGIC = "SEGA";
const NOT_THIS_CONSOLE = "SEGA 32X";
const MAGIC_OFFSET = 0x100;

function matchesAt(bytes: Uint8Array, offset: number, text: string): boolean {
  if (bytes.length < offset + text.length) return false;
  for (let i = 0; i < text.length; i++) if (bytes[offset + i] !== text.charCodeAt(i)) return false;
  return true;
}

/** The Mega Drive's descriptor. One node type, one platform: the machine is a Mega Drive whether its header says
 *  MEGA DRIVE or GENESIS. */
export const MD: ConsoleDescriptor = {
  id: "blastem.md",
  core: "blastem",
  nodeType: "rp.blastem.md",
  platforms: [{ id: "md", label: "Mega Drive", extensions: [".md", ".bin", ".gen"] }],
  // Read at the DEEP tier even though the header is at $100: classifyRom reads in tiers, and the cheap tier runs every
  // detector that reads that far, so a detector here would answer "md" before the Sega 8-bit one (rank 3, at 0x8200) is
  // consulted, and a Sega 8-bit ROM or a crafted polyglot carrying "SEGA" at $100 would be a Mega Drive on the path the
  // app uses. At the deep tier rank 4 means LAST in classifyRom's tiered walk and in detectPlatform's whole-buffer pass
  // alike, which is the priority below; the cost is one deep read per Mega Drive ROM. The tiers stay [0x134, 0x8200]
  // (registry.test.ts pins them).
  detect: {
    prefixBytes: 0x8200,
    // Rank 4: LAST, so a crafted file carrying the GB logo or the Sega 8-bit header as well is that console and not
    // this one. Ranks are unique per detector (the same rank ties, and a tie is broken by descriptor id, which is not a
    // decision anyone wrote down), which is why this is not 2 (the Game Boy's).
    rank: 4,
    run: (bytes) =>
      matchesAt(bytes, MAGIC_OFFSET, MAGIC) && !matchesAt(bytes, MAGIC_OFFSET, NOT_THIS_CONSOLE) ? "md" : null,
  },
  // The whole prefix, not the short header: genmddj's product code is at $180 and its version string at $D1B1 in the
  // built ROM, and `hasBattery` reads $1B0. The store's detectBattery reads this same prefix (src/systemsStore.ts),
  // which is what makes the battery rule below see the byte it names.
  providerHeaderBytes: 0x10000,
  // The header's $1B0 RAM block: "RA" plus a RAM flag that is not the "no RAM" marker means a battery. $1B0 alone is
  // not enough (a cart may declare RAM it does not keep), so the flag's low nibble decides: romdb.h's three are
  // RAM_FLAG_ODD 0x18, RAM_FLAG_EVEN 0x10 and RAM_FLAG_BOTH 0x00 (nibbles 8, 0, 0), and a cart with no SRAM writes $0F
  // (nibble F). Only that one answer says no: any other byte declares something, and the policy below keeps the save.
  // A header TOO SHORT to hold the field answers TRUE, the policy romHasBattery documents in src/consoles.ts ("only ever
  // returns false when we're certain there's no save", so a working cart is never wrongly greyed): the store reads
  // providerHeaderBytes (0x10000), so this only triggers on a truncated read, and refusing a save the user has is worse
  // than offering one they do not.
  hasBattery: (header) =>
    header.length <= 0x1B2 ||
    (header[0x1B0] === 0x52 && // 'R'
     header[0x1B1] === 0x41 && // 'A'
     (header[0x1B2] & 0x0f) !== 0x0f),
  // Port 2 is the host's device only for a cart that claimed it: the shell writes `coreBytesIn` on the node from the
  // protocol nodes the system's roles place, and the core refuses a level until it says so (BlastemNode.cpp).
  capabilities: { claimedBytesIn: true },
  settingFields: { region: label(MD_REGION_VALUES) },
  // A region fixes the master clock and the frame height, so a change is a rebuild, not a live knob (the native
  // config is read once at boot).
  constructFields: { region: { takes: label(MD_REGION_VALUES), fallback: "auto" } },
  configParams: [],
};

const BLASTEM_CONSOLES: ConsolesContribution = { consoles: [MD] };
export default BLASTEM_CONSOLES;
