// The Mesen core's string-valued settings enums. As in every settings enum, each is an index-ordered value tuple whose
// position EQUALS the native enum integer the core's knob reads: the tuple feeds `z.enum(...)` (core's configFields'
// enumField) and indexes the menu cycler's display-name array, and the system node's settings carry the label
// graph/buildGraph.ts's systemKnobs hands to the C++ type. Their order and members are load-bearing: reordering one
// changes what a stored project means.
//
// They live here because Mesen's role schema reads them and this package owns that role. `n8Link` and `fifo` are the
// emulated EverDrive N8 cart inside the Mesen NES core (packages/native's src/transport/N8ReliableLink.hpp and the
// FIFO the role's config builds), so they are the core's settings; @retroplug/n8 owns the physical cart.

// NES console region (ConsoleRegion 0..4).
export const REGION_VALUES = ["auto", "ntsc", "pal", "dendy", "ntscJapan"] as const;
export type ConsoleRegion = (typeof REGION_VALUES)[number];

// NES per-channel export mode (spec/10 §5/§5b): mix=0, stereoModPins=1, pinsPlusRef=2, individualMono=3.
export const CHANNEL_EXPORT_VALUES = ["mix", "stereoModPins", "pinsPlusRef", "individualMono"] as const;
export type ChannelExportMode = (typeof CHANNEL_EXPORT_VALUES)[number];

// Cartridge-accuracy switches. "chip" is the documented hardware behaviour and the default; "n8" matches
// an Everdrive N8 Pro's FPGA cores, which measurably differ, so software developed against that cartridge
// (BlipToaster) sounds the same in the emulator as on the console. Both are per-system and live.
//   s5bNoise "n8"       - the 5B has no noise generator there, so tone-AND-noise mutes the channel
//   mmc5PhaseReset "n8" - a $5003/$5007 write does not restart the pulse duty sequencer there
export const CARTRIDGE_ACCURACY_VALUES = ["chip", "n8"] as const;
export type CartridgeAccuracy = (typeof CARTRIDGE_ACCURACY_VALUES)[number];

// How faithfully the emulated EverDrive N8 cart FIFO behaves like the real one.
//   "instant"  - unbounded and delivered immediately. The DEFAULT, and what every existing test was
//                written against: nothing can ever be lost and a pushed chunk is readable at once.
//   "hardware" - 2048 bytes deep, dropping when full, delivered at the wire's ~150 KB/s. Both of
//                those hide bug classes when they are absent: a ROM that cannot lose a byte never has
//                its recovery path exercised, and one handed a whole chunk atomically is never
//                starved mid-structure the way a real wire starves it.
// Construct-time (the FIFO is built when the core activates): setRoleConfig rebuilds the system for an edit.
export const FIFO_PROFILE_VALUES = ["instant", "hardware"] as const;
export type FifoProfile = (typeof FIFO_PROFILE_VALUES)[number];

// BlipToaster's reliable link (packages/native's src/transport/N8ReliableLink.hpp): MIDI to a ROM that advertises it
// (SIG capability bit 0) goes out framed and is re-sent when an NTSC DMC-DMA halt loses a byte, exactly as on a
// physical N8.
//   "auto" - on whenever the ROM advertises it. The DEFAULT: the emulator should lose and recover bytes the way
//            the cartridge does, not be quietly better than it.
//   "off"  - raw MIDI, unframed - for tests that drive the protocol (or a malformed stream) by hand.
// Construct-time (the FIFO role is built when the core activates): setRoleConfig rebuilds the system for an edit.
export const N8_LINK_VALUES = ["auto", "off"] as const;
export type N8LinkMode = (typeof N8_LINK_VALUES)[number];
