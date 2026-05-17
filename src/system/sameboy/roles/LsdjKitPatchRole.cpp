#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

#include <cstdio>
#include <cstring>

#include "lsdj/KitUtil.hpp"
#include "lsdj/OffsetLookup.hpp"
#include "system/sameboy/SameBoySystem.hpp"

extern "C" {
#define GB_INTERNAL
#include <gb.h>
}

namespace {

// SameBoy stores each ROM bank at offset `bank * 0x4000` in its internal
// ROM buffer. Same layout as our `rom_` mirror, so the offsets align.
constexpr std::size_t kBankSize = rp::lsdj::Kit::kSize;

void applyOne(SameBoySystem& system,
              std::uint8_t   kitIndex,
              const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kBankSize) {
        std::fprintf(stderr,
            "[LsdjKitPatchRole] dropping patch slot %u: expected %zu bytes, got %zu\n",
            kitIndex, kBankSize, bytes.size());
        return;
    }
    const std::size_t bank   = rp::lsdj::OffsetLookup::kitBankForSlot(kitIndex);
    const std::size_t offset = bank * kBankSize;

    // Mirror copy so SameBoySystem::snapshotConfig sees the patched ROM
    // on project save.
    if (system.rom_.size() >= offset + kBankSize) {
        std::memcpy(system.rom_.data() + offset, bytes.data(), kBankSize);
    }

    // Live emulator: poke the byte under the running CPU. GB_get_direct_access
    // returns the ROM buffer SameBoy actually reads from, so a memcpy here
    // affects subsequent reads with no further coordination. Safe under the
    // DSP thread because we're inside `onProcessBlock` between GB_run steps.
    if (system.gb_) {
        std::size_t romSize = 0;
        std::uint16_t b = 0;
        void* rom = GB_get_direct_access(system.gb_, GB_DIRECT_ACCESS_ROM, &romSize, &b);
        if (rom && romSize >= offset + kBankSize) {
            std::memcpy(static_cast<std::uint8_t*>(rom) + offset,
                        bytes.data(), kBankSize);
        }
    }
}

} // namespace

LsdjKitPatchRole::LsdjKitPatchRole()  = default;
LsdjKitPatchRole::~LsdjKitPatchRole() = default;

void LsdjKitPatchRole::onAttach(SameBoySystem& /*system*/) {
    std::fprintf(stderr, "[RetroPlug] LSDJ kit-patch role attached\n");
}

void LsdjKitPatchRole::onProcessBlock(SameBoySystem& system,
                                      const AudioBlockInfo& /*info*/) {
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (!pending_[i].has_value()) continue;
        applyOne(system, static_cast<std::uint8_t>(i), *pending_[i]);
        pending_[i].reset();
    }
}

void LsdjKitPatchRole::queuePatch(std::uint8_t kitIndex,
                                  std::vector<std::uint8_t> kitBytes) {
    if (kitIndex >= kSlotCount) {
        std::fprintf(stderr,
            "[LsdjKitPatchRole] dropping out-of-range slot %u\n", kitIndex);
        return;
    }
    pending_[kitIndex] = std::move(kitBytes);
}

void LsdjKitPatchRole::queueAllFromConfig(const rp::lsdj::LsdjKitPatchConfig& config) {
    for (const auto& kit : config.kits) {
        if (kit.slot >= kSlotCount) continue;
        const auto& bytes = kit.compiledBytes.bytes();
        if (bytes.empty()) continue;
        pending_[kit.slot] = bytes;
    }
}
