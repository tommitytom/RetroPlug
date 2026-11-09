// Mesen's app contribution: the core-config "system" role whose kind IS the core (platform.ts's `Core` value). It is
// keyed by the core, so this ONE role attaches to any Mesen system (NES, GBA, SMS/GG). The knobs are per-platform and
// the System submenu gates each group on `platform` (this package's ts/ui/index.ts): the first three are NES-only,
// enableFm is SMS/GG-only, and GBA has none of its own yet - it carries them all as inert bytes until it does, at which
// point the schema grows again.
//
// The system node reads the fields each platform reads (the shell's graph/buildGraph.ts CONSTRUCT_FIELDS and
// SETTING_FIELDS), so a field that means nothing on a platform is simply never placed in its settings. The native side
// is this package's: `rp_mesen_package` registers the NES, GBA and SMS nodes from packages/mesen/native.

import type { RoleType } from "@retroplug/core/systemRoles";
import { definePackage } from "@retroplug/core/definePackage";
import { z } from "zod";
import { clampedInt, clampedNumber, boolField, enumField, stringField } from "@retroplug/core/configFields";
import {
  REGION_VALUES,
  CARTRIDGE_ACCURACY_VALUES,
  CHANNEL_EXPORT_VALUES,
  FIFO_PROFILE_VALUES,
  N8_LINK_VALUES,
} from "./enums";

export const MESEN_ROLE: RoleType = {
  kind: "mesen",
  category: "system",
  schema: z.object({
    region: enumField(REGION_VALUES, "auto"), // ConsoleRegion: auto / ntsc / pal / dendy / ntscJapan
    removeSpriteLimit: boolField(false),
    // SMS/GG: route the YM2413 (FM). Applied at construct (configureSms runs before LoadRom). Not a
    // cosmetic toggle - Mesen models the $F2 audio-control port as a MUX whose PSG branch memsets the
    // buffer, so a tracker that writes $F2 = $01 at boot goes silent on the PSG with this on.
    enableFm: boolField(true),
    // APU flush window as a latency in ms (the worst-case NES audio latency the resampler batching adds).
    // Live knob; native converts ms->CPU cycles per region clock. ~1.4ms = the historical 2500-cycle window.
    apuLatencyMs: clampedNumber(0.25, 6.0, 1.4),
    // Cartridge-accuracy switches. Both default to "n8", NOT to the documented chip behaviour: RetroPlug
    // is music software, and NES music is overwhelmingly played back through an Everdrive N8 Pro, so the
    // useful default is the one where what you hear here is what the cartridge will do. Choose "chip" for
    // playback accuracy on a stock cartridge.
    //
    // The N8's FPGA cores measurably differ (both verified on hardware, see the shell's cartridge-accuracy test):
    // its 5B has no noise generator, so enabling noise MUTES the channel rather than rasping (the mixer
    // ANDs tone with noise); and its MMC5 pulse does not restart the duty sequencer on a $5003 write, so
    // a phase-reset hack holds a 50% duty and full level and only shifts pitch.
    s5bNoise: enumField(CARTRIDGE_ACCURACY_VALUES, "n8"),
    mmc5PhaseReset: enumField(CARTRIDGE_ACCURACY_VALUES, "n8"),
    // NES-only: the level of the CARTRIDGE's own sound chip (VRC6 / VRC7 / N163 / S5B / MMC5 / FDS), as a
    // percentage of unity - never the console's 2A03. Mesen mixes each expansion chip through a per-channel
    // volume; this sets all six together, since a cart has at most one. Live.
    //
    // It is also what a connected Everdrive N8 Pro is set to: the FPGA has the same control (`master_vol`,
    // 128 = unity) and the cart node rescales this value into it (`expansionVolumeRegister`), so one knob decides
    // how loud the chip is whether you are hearing the emulator or the console. 100 = the chip as the hardware
    // mixes it.
    expansionVolume: clampedInt(0, 200, 100),
    // CLI-only per-channel export mode (spec/10 §5/§5b): mix, stereoModPins (Pulse | TND + Expansion),
    // individualMono (5 core channels). Construct-time: a render's adopt sets it, an edit rebuilds the system, and
    // the settings menu does not surface it. Additive. (pinsPlusRef = pins + a mix reference, native/test-only.)
    channelExportMode: enumField(CHANNEL_EXPORT_VALUES, "mix"),
    // NES-only: the host directory the emulated EverDrive N8 SD card maps to ("/" on the card), for a
    // ROM that pulls its data off the cartridge rather than over MIDI. Empty means a per-process
    // scratch directory - deliberately NOT the working directory, which made the card depend on where
    // the process was started and let a file one test wrote become every later run's card.
    //
    // Construct-time, unlike `region` (which the NES node applies live: a region change resets the core): the FIFO
    // is built when the core activates, so an edit needs a rebuild, which setRoleConfig makes itself (the system's
    // node reads it once), carrying the battery.
    sdRoot: stringField(""),
    // NES-only: how faithfully the emulated cart FIFO behaves like the device's (see
    // FIFO_PROFILE_VALUES). Default "instant" - unbounded and immediate - because that is the
    // transport every existing test assumes, and because a lossy queue is something a test should
    // ASK for rather than inherit. "hardware" makes two hardware-only bug classes reachable in CI:
    // a dropped byte (so a ROM's recovery path can be exercised) and a wire that trickles (so a
    // copy loop can be starved mid-structure rather than only between chunks).
    //
    // The two overrides are 0 = "take the profile's value", so a rate sweep can vary one without
    // restating the other. Construct-time, like `sdRoot`: an edit rebuilds the system.
    fifo: enumField(FIFO_PROFILE_VALUES, "instant"),
    fifoDepth: clampedInt(0, 1 << 20, 0),
    fifoBytesPerSecond: clampedInt(0, 100_000_000, 0),
    // NES-only: BlipToaster's reliable link (see N8_LINK_VALUES) and the round trip the emulator models for a
    // re-send, in microseconds - the host->cart->host latency of a physical N8 over USB, so a hit the link recovers
    // lands as late in the plugin as on the cartridge: 2200 = a real N8 Pro's fastest round trip (the ROM's ack out,
    // the re-sent frame back), measured 2026-10-22. Construct-time: an edit rebuilds the system.
    n8Link: enumField(N8_LINK_VALUES, "auto"),
    n8LinkRttUs: clampedInt(0, 100_000, 2200),
    // NES-only: drive a real Everdrive N8 Pro from this system, so the same MIDI and generated bytes the emulated
    // core is played with reach the cart's FIFO too (buildGraph places its rp.n8.nes and fans them out, and the
    // engine's graph does the rest). Additive, and ON by default: it is what every project did before the setting
    // existed - the first NES's traffic reached a cart whatever its state - and the node is placed only for the
    // FIRST NES in slot order, so a second system's traffic reaches the cart only if this one lets go of it.
    driveRealCart: boolField(true),
  }),
};

export default definePackage({ roles: [MESEN_ROLE] });
