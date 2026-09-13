#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a Mesen-backed NES system slot.
// Mirrors SameBoyConfig's shape (embed / rom / sram / savestate) so the system
// backends handle the concrete configs uniformly.

struct MesenNesConfig {
    // On-disk variant discriminator (`"kind":"nes"`).
    using Tag = rfl::Literal<"nes">;

    bool          embedRom = true;
    // Watch `romPath` on disk; the UI thread reloads the system when the
    // file's mtime advances. No-op when romPath is empty.
    bool          reloadOnRomChange = false;
    float         gainDb   = 0.0f;
    // TS-owned "mesen" system-role knobs (coreRoles.ts). region = ConsoleRegion
    // (0=Auto,1=Ntsc,2=Pal,3=Dendy,4=NtscJapan) — applied at construct, live edit
    // needs a reset. removeSpriteLimit toggles the PPU 8-sprites/line cap (live).
    std::uint32_t region            = 0;
    bool          removeSpriteLimit = false;
    // Cartridge-accuracy switches (TS enums, 0 = "chip", 1 = "n8"). Default to "n8" to match coreRoles.ts:
    // this is music software and NES music is played back through an Everdrive N8 Pro, whose FPGA cores
    // measurably differ from the documented chips. Live knobs.
    // s5bNoise: the 5B's noise generator (the N8 has none, and its tone-AND-noise mixer then mutes).
    // mmc5PhaseReset: whether $5003/$5007 restarts the pulse duty sequencer (the N8's does not).
    std::uint32_t s5bNoise          = 1;
    std::uint32_t mmc5PhaseReset    = 1;
    // The cartridge sound chip's level as a percentage of unity (VRC6 / VRC7 / N163 / S5B / MMC5 / FDS -
    // never the console's own 2A03), applied to every expansion channel Mesen mixes. 100 = the chip as the
    // hardware mixes it. Live knob. The N8 Pro's FPGA has the same control (`master_vol`, 128 = unity), and
    // the UI drives both from this one value, so a cart sounds the same emulated and on the console.
    std::uint32_t expansionVolume   = 100;
    // Per-channel audio export mode (CLI-only; spec/10 §5/§5b). 0 = Mix (default, the mixed stereo
    // output). 1 = StereoModPins: the two 2A03 output pins (Pulse | TND) + a lumped Expansion stream, 3
    // mono channelLayout() streams. 3 = IndividualMono: the 5 core channels (Square1/Square2/Triangle/
    // Noise/DMC) as raw mono stems ("does not sum"). (2 = pins + a mix reference, native/test-only.) Set
    // at construct via the "mesen" role blob (capture engages in onActivate); there is no live toggle.
    std::uint32_t channelExportMode = 0;
    // APU flush window expressed as a latency in milliseconds — the worst-case NES audio latency the
    // resampler batching adds (window duration = cycleLength / cpuClock). Live-editable ("mesen" role knob);
    // converted to CPU cycles by NesSoundMixer::SetLatencyMs. ~1.4ms ≈ the historical 2500-cycle window (NTSC).
    double        apuLatencyMs      = 1.4;
    // Host directory the emulated EverDrive SD card maps to ("/" on the card), for a ROM that pulls
    // data off the cartridge's own storage. Empty => a per-process scratch dir (NesEverdriveFifo::
    // defaultSdRoot), NOT the working directory. Applied at activate; there is no live toggle, so a
    // change needs a rebuild (setRoleConfig + reset), same as `region`.
    std::string   sdRoot;
    // How faithfully the emulated cart FIFO behaves like the device's. 0 = instant (unbounded and
    // delivered immediately - the permissive behaviour every existing test was written against),
    // 1 = hardware (NesEverdriveFifo::FIFO_HW_*: 2048 deep, ~150 KB/s, dropping when full). The two
    // overrides are 0 = "take it from the profile", so a rate sweep can vary one without the other.
    // Construct-time (the FIFO is built in onActivate), same as sdRoot.
    std::uint32_t fifo               = 0;
    std::uint32_t fifoDepth          = 0;
    std::uint32_t fifoBytesPerSecond = 0;
    std::string   romPath;
    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
    std::uint32_t savSuffix = 0;
    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
    // a user-paired `.sav` file that all battery I/O targets.
    std::string   savPath;
    // Binary blobs live in the .rplg zip as raw entries — see ProjectBinaries.
    std::vector<std::uint8_t> romBytes;
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;
};
