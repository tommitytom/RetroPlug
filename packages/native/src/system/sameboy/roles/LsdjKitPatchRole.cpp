#include "system/sameboy/roles/LsdjKitPatchRole.hpp"

#include <cstdio>

#include "lsdj/KitUtil.hpp"
#include "lsdj/OffsetLookup.hpp"
#include "system/MemoryAccessor.hpp"
#include "system/MemoryType.hpp"
#include "system/sameboy/SameBoySystem.hpp"

namespace {

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

    // Live emulator ROM. The accessor wraps SameBoy's internal ROM buffer
    // (via GB_get_direct_access); writing here affects subsequent CPU
    // reads. Safe under the DSP thread because we're inside onProcessBlock
    // between GB_run steps. `system.rom_` is the immutable base ROM and is
    // left untouched — the patched bytes are persisted via
    // LsdjKitConfig::compiledBytes and re-applied on project reload.
    rp::MemoryAccessor rom = system.getMemory(rp::MemoryType::Rom, rp::AccessType::ReadWrite);
    if (!rom.valid()) return;
    rom.write(offset, bytes.data(), bytes.size());
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
        if (kit.compiledBytes.empty()) continue;
        pending_[kit.slot] = kit.compiledBytes;
    }
}
