#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rfl/Literal.hpp"

// Plain-data, reflectcpp-friendly config for a Master System / Game Gear
// (Mesen2) system slot. Mirrors MesenGbaConfig's shape so the concrete configs
// are handled uniformly by the system backends.

// Named MesenSmsConfig (not SmsConfig) to avoid colliding with Mesen's own
// `struct SmsConfig` in deps/mesen/Core/Shared/SettingTypes.h:695 - both live
// at global scope and any TU that needs to call settings->SetSmsConfig() would
// otherwise see two definitions of the same name. Same reasoning as
// MesenGbaConfig; here it is not hypothetical, since MesenSmsSystem.cpp calls
// SetSmsConfig() directly.
struct MesenSmsConfig {
    // On-disk variant discriminator (`"kind":"sms"`). Vestigial in the same way
    // MesenGbaConfig's is: rfl::TaggedUnion appears once repo-wide and not on
    // this path. Kept for symmetry; don't build on it.
    using Tag = rfl::Literal<"sms">;

    // Which machine to boot. Not a user knob - it comes from the platform the
    // ROM was classified as ("sms" vs "gg"), and it reaches Mesen indirectly:
    // SmsConsole has no signature (SmsConsole.h:39) and picks its model purely
    // from the ROM file's EXTENSION (SmsConsole.cpp:46-59), so this field
    // decides the suffix of the staged file MesenSmsSystem hands to LoadRom.
    // Game Gear also needs its own overscan and blend settings, so this is not
    // cosmetic - booting .gg bytes under the SMS config renders a black screen.
    bool          gameGear        = false;

    // Emulate the YM2413 FM unit. On by default, matching Mesen and matching
    // real FM-equipped hardware.
    //
    // This is not just "more sound". Mesen models the Japanese SMS behaviour
    // where port $F2 MUXES rather than sums: a ROM that writes $F2=1 to turn FM
    // on gets its PSG output zeroed outright (SmsFmAudio::IsPsgAudioMuted ->
    // SmsPsg::PlayQueuedAudio's memset). smsggdj does exactly that at boot when
    // its FM option is enabled, and its own source notes that real hardware and
    // SMSPlus SUM the two while Emulicious muxes - so with FM on, Mesen loses
    // every PSG channel the tracker plays. Turning FM off here restores them.
    //
    // Also load-bearing for testing: with FM off the PSG output is byte
    // identical regardless of the step loop's flush cadence, which is what
    // makes a cadence-invariance guard possible at all.
    bool          enableFm        = true;

    bool          embedRom        = true;
    // Watch `romPath` on disk; the UI thread reloads the system when the
    // file's mtime advances. No-op when romPath is empty.
    bool          reloadOnRomChange = false;
    float         gainDb          = 0.0f;
    std::string   romPath;
    // See SameBoyConfig::savSuffix. 0 => owns `<rom>.sav`; N>=2 => `<rom>-N.sav`,
    // so duplicated / repeat-loaded instances don't clobber a shared sibling.
    std::uint32_t savSuffix = 0;
    // See SameBoyConfig::savPath. Empty => suffix-derived sibling; non-empty =>
    // a user-paired `.sav` file that all battery I/O targets.
    std::string   savPath;
    // Binary blobs live in the .rplg zip as raw entries - see ProjectBinaries.
    std::vector<std::uint8_t> romBytes;
    std::vector<std::uint8_t> sram;
    std::vector<std::uint8_t> savestate;
};
