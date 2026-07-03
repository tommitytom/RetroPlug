#include "system/sameboy/SameBoySystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "rfl/Variant.hpp"

#include "system/sameboy/RomSniffer.hpp"
#include "system/sameboy/roles/LsdjKitPatchRole.hpp"
#include "system/sameboy/roles/LsdjSyncRole.hpp"
#include "system/sameboy/roles/MgbPassthroughRole.hpp"

extern "C" {
#define GB_INTERNAL
#include <gb.h>
}

#include "system/sameboy/bootroms/agb_boot.h"
#include "system/sameboy/bootroms/cgb_boot.h"
#include "system/sameboy/bootroms/cgb_boot_fast.h"
#include "system/sameboy/bootroms/cgb0_boot.h"
#include "system/sameboy/bootroms/dmg_boot.h"
#include "system/sameboy/bootroms/mgb_boot.h"
#include "system/sameboy/bootroms/sgb_boot.h"
#include "system/sameboy/bootroms/sgb2_boot.h"

namespace {

GB_model_t toSameBoyModel(SameBoyModel model) {
    switch (model) {
        case SameBoyModel::DmgB:   return GB_MODEL_DMG_B;
        case SameBoyModel::Mgb:    return GB_MODEL_MGB;
        case SameBoyModel::Sgb:    return GB_MODEL_SGB_NTSC_NO_SFC;
        case SameBoyModel::SgbPal: return GB_MODEL_SGB_PAL_NO_SFC;
        case SameBoyModel::Sgb2:   return GB_MODEL_SGB2_NO_SFC;
        case SameBoyModel::Cgb0:   return GB_MODEL_CGB_0;
        case SameBoyModel::CgbA:   return GB_MODEL_CGB_A;
        case SameBoyModel::CgbB:   return GB_MODEL_CGB_B;
        case SameBoyModel::CgbC:   return GB_MODEL_CGB_C;
        case SameBoyModel::CgbD:   return GB_MODEL_CGB_D;
        case SameBoyModel::CgbE:   return GB_MODEL_CGB_E;
        case SameBoyModel::Agb:    return GB_MODEL_AGB;
        case SameBoyModel::Gbp:    return GB_MODEL_GBP;
        case SameBoyModel::Auto: // fallthrough
        default:                   return GB_MODEL_CGB_C;
    }
}

std::string_view findBootRom(GB_model_t model, bool fastBoot) {
    switch (model) {
        case GB_MODEL_DMG_B:           return std::string_view((const char*)dmg_boot, dmg_boot_len);
        case GB_MODEL_MGB:             return std::string_view((const char*)mgb_boot, mgb_boot_len);
        case GB_MODEL_SGB_NTSC_NO_SFC:
        case GB_MODEL_SGB_PAL_NO_SFC:  return std::string_view((const char*)sgb_boot, sgb_boot_len);
        case GB_MODEL_SGB2_NO_SFC:     return std::string_view((const char*)sgb2_boot, sgb2_boot_len);
        case GB_MODEL_CGB_0:           return std::string_view((const char*)cgb0_boot, cgb0_boot_len);
        case GB_MODEL_AGB:
        case GB_MODEL_GBP:             return std::string_view((const char*)agb_boot, agb_boot_len);
        default:
            // CGB-A/B/C/D/E share the stock CGB boot ROM.
            if (fastBoot)
                return std::string_view((const char*)cgb_boot_fast, cgb_boot_fast_len);
            return std::string_view((const char*)cgb_boot, cgb_boot_len);
    }
}

inline SameBoySystem& self(GB_gameboy_t* gb) {
    return *static_cast<SameBoySystem*>(GB_get_user_data(gb));
}

// Pack RGB into LVGL-native XRGB8888 (memory order B,G,R,X with LV_COLOR_DEPTH=32).
// Old codebase produced RGBA; LVGL wants BGRA in memory.
uint32_t rgbEncode(GB_gameboy_t*, uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(0xFFu) << 24)
         | (uint32_t(r)     << 16)
         | (uint32_t(g)     << 8)
         |  uint32_t(b);
}

void vblankHandler(GB_gameboy_t* gb, GB_vblank_type_t type) {
    if (type != GB_VBLANK_TYPE_NORMAL_FRAME) return;
    self(gb).onVblank();
}

void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
    self(gb).writeAudioSample(sample->left, sample->right);
}

void loadBootRomHandler(GB_gameboy_t* gb, GB_boot_rom_t /*type*/) {
    auto& s = self(gb);
    GB_model_t model = toSameBoyModel(s.config_.model);
    std::string_view boot = findBootRom(model, s.config_.fastBoot);
    GB_load_boot_rom_from_buffer(gb, (const unsigned char*)boot.data(), boot.size());
}

// Serial-link bit ferrying. When this system has no peers the callbacks are
// effectively no-ops (return true => idle high). With peers, every received
// bit is broadcast to all peers and the next-bit-to-read comes from the first
// peer. See LinkGroup.hpp for the lockstep stepping policy that pairs with
// these callbacks.
void serialStart(GB_gameboy_t* gb, bool bit_received) {
    SameBoySystem& s = self(gb);
    s.serialBitReceived(bit_received);
    // The SameBoy `bit_received` parameter is the bit this GB is SENDING
    // (the peer receives it). For standalone master-mode capture — where
    // no link peer exists to consume the bit — this is the outgoing bit
    // LSDJ is putting on the wire. Reading it in `serialEnd` instead would
    // be one-bit-off because by then SB has already shifted, so MSB(SB)
    // is the NEXT bit to send.
    if (s.linkPeers_.empty() && s.serialOutCaptureEnabled()) {
        s.captureSerialOutBit(bit_received);
    }
}
bool serialEnd(GB_gameboy_t* gb) {
    SameBoySystem& s = self(gb);
    if (s.linkPeers_.empty()) return s.nextSerialInBit();
    const bool ret = s.serialBitFromPeer();
    s.serialBroadcastBit();
    return ret;
}

constexpr float s16ToF32(int16_t v) {
    return v < 0 ? float(v) / 32768.0f : float(v) / 32767.0f;
}

} // namespace

namespace {
// dB → linear gain (kill below -90 dB so trim-to-mute is a hard zero).
inline float dbToLin(float dB) {
    return dB > -90.0f ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}
} // namespace

SameBoySystem::SameBoySystem(SystemId id,
                             SameBoyConfig config,
                             std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)) {
    linkPeers_.reserve(8);
    gainSmoother_.setTimeConstant(0.020f); // 20 ms — matches master gain
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
}

SameBoySystem::~SameBoySystem() {
    onDeactivate();
}

void SameBoySystem::onActivate(double sampleRate) {
    if (activated_) return;
    if (rom_.empty()) {
        std::fprintf(stderr, "[SameBoySystem] no ROM bytes; not activating\n");
        return;
    }

    sampleRate_ = sampleRate;
    // TODO: Investigate button spacing. Is it adding lag to keypresses? Is it needed?
    buttonSpacingSamples_ = static_cast<std::uint32_t>(sampleRate * 0.010); // 10 ms spacing
    audioFrameCount_ = 0;

    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();

    // Allocate via the lib's GB_alloc, NOT `new GB_gameboy_t()`. The SameBoy
    // Core compiles gb.h as C (/std:c11) while this TU compiles it as C++
    // (/std:c++20); under the MSVC ABI (clang-cl) the GB_gameboy_t layout — and
    // therefore its sizeof — diverges between the two (measured 54216 C++ vs
    // 54208 C; same GB_SECTION padding noted in stateSnapshotRegions). `new`
    // sizes the struct to the C++ layout, but GB_init's `memset(gb, 0,
    // sizeof_C(*gb))` and every later lib write use the C sizeof. It is safe
    // today only because the C++ size is the LARGER of the two; if a future
    // GB_DISABLE_* flag or struct change flips that, `new` becomes an instant
    // heap overflow on Windows. GB_alloc sizes it with the lib's C sizeof
    // unconditionally — SameBoy's documented "provided allocators" contract
    // (gb.h). No-op on Linux, where the C and C++ layouts are identical.
    gb_ = GB_alloc();
    GB_init(gb_, toSameBoyModel(config_.model));
    GB_set_user_data(gb_, this);

    GB_set_sample_rate(gb_, static_cast<unsigned>(sampleRate));
    GB_set_pixels_output(gb_, frames_.writeSlot());

    GB_set_boot_rom_load_callback(gb_, loadBootRomHandler);
    GB_set_rgb_encode_callback(gb_, rgbEncode);
    GB_set_vblank_callback(gb_, vblankHandler);
    GB_apu_set_sample_callback(gb_, audioHandler);
    GB_set_serial_transfer_bit_start_callback(gb_, serialStart);
    GB_set_serial_transfer_bit_end_callback(gb_, serialEnd);

    GB_set_background_rendering_disabled(gb_, false);
    GB_set_object_rendering_disabled(gb_, false);
    GB_set_color_correction_mode(gb_, GB_COLOR_CORRECTION_DISABLED);
    applyHighpassMode();

    GB_load_rom_from_buffer(gb_, rom_.data(), rom_.size());

    // Cartridge battery RAM. Apply BEFORE savestate so that, when both are
    // present, the savestate's embedded SRAM wins (same convention the
    // legacy build used in old/src/sameboy/SameBoyHooks.cpp).
    if (!config_.sram.empty()) {
        const int expected = GB_save_battery_size(gb_);
        if (expected > 0) {
            // SameBoy's load doesn't bounds-check the buffer it reads from.
            // Pad the slurped data to the expected size so a short .sav
            // can't make it read past the end. Truncate too-large.
            std::vector<std::uint8_t> sram = config_.sram;
            sram.resize(static_cast<std::size_t>(expected), 0);
            GB_load_battery_from_buffer(gb_, sram.data(), sram.size());
        }
    }

    if (!config_.savestate.empty()) {
        const auto& save = config_.savestate;
        if (GB_load_state_from_buffer(gb_, save.data(), save.size()) != 0) {
            std::fprintf(stderr, "[SameBoySystem] failed to load savestate\n");
        }
    }

    // Stored config is the source of truth. Empty roles → ask the sniffer
    // for a default; user edits later overwrite both the sniff and any role
    // already attached.
    if (config_.roles.empty()) {
        switch (detectRomKind(rom_)) {
            case RomKind::Mgb:
                config_.roles.emplace_back(MgbRoleConfig{});
                break;
            case RomKind::Lsdj:
                // Default to MidiSync mode so LSDJ syncs to host transport
                // out of the box. The UI's LSDJ-mode picker can flip this
                // to Off or to the Arduinoboy modes per project.
                config_.roles.emplace_back(LsdjSyncConfig{});
                // Kit-patching role attaches alongside sync: orthogonal
                // concerns, both wanted on every LSDJ ROM. Starts empty
                // (no kits patched); the rpc layer's `patchKit` mutates
                // `config_.roles[..]` via this role's runtime state.
                config_.roles.emplace_back(rp::lsdj::LsdjKitPatchConfig{});
                break;
            case RomKind::Generic:
            default:
                break;
        }
    }
    instantiateRoles();

    activated_ = true;
}

void SameBoySystem::onDeactivate() {
    if (!activated_) return;
    if (gb_) {
        // Snapshot live state so the next onActivate (e.g. on Reaper's
        // transport-play or output-channel change) resumes from where
        // we left off instead of cold-rebooting from the original
        // .rplg savestate. Without this, hosts that toggle
        // deactivate/activate around transport edges (Reaper does)
        // visibly reset the GB on every play.
        const std::size_t saveSize = GB_get_save_state_size(gb_);
        if (saveSize > 0) {
            std::vector<std::uint8_t> save(saveSize);
            GB_save_state_to_buffer(gb_, save.data());
            config_.savestate = std::move(save);
        }
        const int sramSize = GB_save_battery_size(gb_);
        if (sramSize > 0) {
            std::vector<std::uint8_t> sram(static_cast<std::size_t>(sramSize));
            if (GB_save_battery_to_buffer(gb_, sram.data(), sram.size()) == 0) {
                config_.sram = std::move(sram);
            }
        }
        GB_free(gb_);       // tear down the lib's internal allocations
        GB_dealloc(gb_);    // free the struct itself (counterpart to GB_alloc)
        gb_ = nullptr;
    }
    activated_ = false;
}

void SameBoySystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    buttonSpacingSamples_ = static_cast<std::uint32_t>(sampleRate * 0.010); // 10 ms
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    if (gb_) {
        GB_set_sample_rate(gb_, static_cast<unsigned>(sampleRate));
    }
}

void SameBoySystem::setGainDb(float dB) {
    config_.gainDb = dB;
    gainSmoother_.setTargetValue(dbToLin(dB));
}

void SameBoySystem::serialBitReceived(bool bit) {
    bitToSend_ = bit;
}

bool SameBoySystem::serialBitFromPeer() const {
    if (linkPeers_.empty() || !linkPeers_.front()->gb_) return true;
    return GB_serial_get_data_bit(linkPeers_.front()->gb_);
}

void SameBoySystem::serialBroadcastBit() const {
    for (auto* peer : linkPeers_) {
        if (peer && peer->gb_) {
            GB_serial_set_data_bit(peer->gb_, bitToSend_);
        }
    }
}

void SameBoySystem::onReset() {
    if (gb_) {
        GB_reset(gb_);
        audioFrameCount_ = 0;
    }
    pendingButtons_.clear();
}

void SameBoySystem::restartEmulator() {
    if (!gb_) return;
    const int sramSize = GB_save_battery_size(gb_);
    if (sramSize > 0) {
        std::vector<std::uint8_t> sram(static_cast<std::size_t>(sramSize));
        if (GB_save_battery_to_buffer(gb_, sram.data(), sram.size()) == 0) {
            config_.sram = std::move(sram);
        }
    }
    const double sr = sampleRate_;
    onDeactivate();
    // Discard the savestate onDeactivate just captured — a new GB
    // model can't restore a savestate from the old one.
    config_.savestate.clear();
    onActivate(sr);
}

void SameBoySystem::clearSram() {
    if (!gb_) return;
    const int sramSize = GB_save_battery_size(gb_);
    if (sramSize <= 0) return;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(sramSize), 0);
    GB_load_battery_from_buffer(gb_, zeros.data(), zeros.size());
    config_.sram.clear();
}

std::vector<std::uint8_t> SameBoySystem::saveSramBytes() const {
    if (!gb_) return {};
    const int sramSize = GB_save_battery_size(const_cast<GB_gameboy_t*>(gb_));
    if (sramSize <= 0) return {};
    std::vector<std::uint8_t> out(static_cast<std::size_t>(sramSize));
    if (GB_save_battery_to_buffer(const_cast<GB_gameboy_t*>(gb_),
                                  out.data(), out.size()) != 0) {
        return {};
    }
    return out;
}

bool SameBoySystem::loadSramBytes(const std::vector<std::uint8_t>& bytes) {
    if (!gb_ || bytes.empty()) return false;
    const int sramSize = GB_save_battery_size(gb_);
    if (sramSize <= 0) return false;
    // Pad/truncate to the cartridge's battery size, matching the load path
    // in onActivate (GB_load_battery_from_buffer wants the exact size).
    std::vector<std::uint8_t> sram = bytes;
    sram.resize(static_cast<std::size_t>(sramSize), 0);
    GB_load_battery_from_buffer(gb_, sram.data(), sram.size());
    config_.sram = std::move(sram);
    return true;
}

std::vector<std::uint8_t> SameBoySystem::saveStateBytes() const {
    if (!gb_) return {};
    const std::size_t size = GB_get_save_state_size(const_cast<GB_gameboy_t*>(gb_));
    if (size == 0) return {};
    std::vector<std::uint8_t> out(size);
    GB_save_state_to_buffer(const_cast<GB_gameboy_t*>(gb_), out.data());
    return out;
}

bool SameBoySystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    if (!gb_ || bytes.empty()) return false;
    return GB_load_state_from_buffer(gb_, bytes.data(), bytes.size()) == 0;
}

// -- Whole-savestate snapshot ------------------------------------------------

std::size_t SameBoySystem::stateSnapshotSize() const {
    return gb_ ? GB_get_save_state_size(const_cast<GB_gameboy_t*>(gb_)) : 0;
}

bool SameBoySystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    if (!gb_) return false;
    const std::size_t size = GB_get_save_state_size(gb_);
    if (size == 0) return false;
    dst.resize(size);                            // reused scratch — no alloc once warm
    GB_save_state_to_buffer(gb_, dst.data());
    return true;
}

// MBC-RAM (=SRAM), WRAM and VRAM sit as raw blobs at the tail of the no-BESS
// region of the savestate, in that order. Their offsets are therefore valid
// against a full (BESS) savestate too, since BESS data is appended after them.
// Ported from old/src/sameboy/SectionOffsetCollector.c.
SystemBase::StateRegionTable SameBoySystem::stateSnapshotRegions() const {
    StateRegionTable t{};
    if (!gb_) return t;
    auto* gb = const_cast<GB_gameboy_t*>(gb_);
    // Region sizes come from the public GB_get_direct_access, NOT raw gb->*_size
    // fields. SameBoySystem.cpp compiles gb.h as C++ while the SameBoy Core lib
    // compiles it as C; under the MSVC ABI (clang-cl) the GB_gameboy_t layout
    // diverges past the timing/APU sections, so gb->vram_size read garbage here
    // (it tested as 0, shifting the SRAM offset by a VRAM bank). The accessor is
    // computed inside the lib against the correct C layout, so it's robust.
    // (gb->io_registers lives before the divergence, so the synthetic-clock path
    // that reads it directly is unaffected.)
    auto regionSize = [&](GB_direct_access_t a) -> std::size_t {
        size_t sz = 0; uint16_t bank = 0;
        GB_get_direct_access(gb, a, &sz, &bank);
        return sz;
    };
    const std::size_t vramSize = regionSize(GB_DIRECT_ACCESS_VRAM);
    const std::size_t ramSize  = regionSize(GB_DIRECT_ACCESS_RAM);
    const std::size_t mbcSize  = regionSize(GB_DIRECT_ACCESS_CART_RAM);

    std::size_t off = GB_get_save_state_size_no_bess(gb);
    off -= vramSize;
    t[static_cast<std::size_t>(rp::MemoryType::Vram)] =
        { static_cast<std::uint32_t>(off), static_cast<std::uint32_t>(vramSize) };
    off -= ramSize;
    t[static_cast<std::size_t>(rp::MemoryType::Ram)] =
        { static_cast<std::uint32_t>(off), static_cast<std::uint32_t>(ramSize) };
    off -= mbcSize;
    t[static_cast<std::size_t>(rp::MemoryType::Sram)] =
        { static_cast<std::uint32_t>(off), static_cast<std::uint32_t>(mbcSize) };
    return t;
}

std::unique_ptr<SystemBase> SameBoySystem::clone(SystemId newId, double sampleRate) const {
    SameBoyConfig cfg = config_;
    cfg.linkGroupId   = 0;
    cfg.savSuffix     = 0;   // caller (duplicateSystem) assigns a non-colliding suffix
    cfg.savPath.clear();     // and its own sav file, not the source's paired one
    auto sramBytes  = saveSramBytes();
    if (!sramBytes.empty())  cfg.sram      = std::move(sramBytes);
    auto stateBytes = saveStateBytes();
    if (!stateBytes.empty()) cfg.savestate = std::move(stateBytes);
    std::vector<std::uint8_t> romCopy = rom_;
    auto out = std::make_unique<SameBoySystem>(newId, std::move(cfg), std::move(romCopy));
    out->onActivate(sampleRate);
    return out;
}

std::unique_ptr<SystemBase> SameBoySystem::cloneFromState(
        SystemId newId, double sampleRate,
        const std::vector<std::uint8_t>& savestate) const {
    if (savestate.empty()) return nullptr;
    SameBoyConfig cfg = config_;       // non-state config copy (the deferred config-race)
    cfg.linkGroupId   = 0;
    cfg.savSuffix     = 0;   // caller (duplicateSystem) assigns a non-colliding suffix
    cfg.savPath.clear();     // and its own sav file, not the source's paired one
    cfg.savestate     = savestate;
    // Slice SRAM out of the savestate (same offsets the snapshot uses) so the
    // clone's battery RAM round-trips, matching clone().
    const auto regions = stateSnapshotRegions();
    const auto& sram   = regions[static_cast<std::size_t>(rp::MemoryType::Sram)];
    if (sram.size > 0 &&
        static_cast<std::size_t>(sram.offset) + sram.size <= savestate.size()) {
        cfg.sram.assign(savestate.begin() + sram.offset,
                        savestate.begin() + sram.offset + sram.size);
    }
    std::vector<std::uint8_t> romCopy = rom_;
    auto out = std::make_unique<SameBoySystem>(newId, std::move(cfg), std::move(romCopy));
    out->onActivate(sampleRate);
    return out;
}

void SameBoySystem::instantiateRoles() {
    roles_.clear();
    serialIn_.clear();
    serialBitsRemaining_ = 0;
    serialOutByte_ = 0;
    serialOutBits_ = 0;

    // TODO: This could be nicer. Also does it belong in here? This stuff is gameboy specific, not necessarily SameBoy.
    for (const auto& rc : config_.roles) {
        if (rfl::get_if<MgbRoleConfig>(&rc.variant())) {
            auto role = std::make_unique<MgbPassthroughRole>();
            role->onAttach(*this);
            roles_.push_back(std::move(role));
            std::fprintf(stderr, "[RetroPlug] attached MGB passthrough role to system %u\n", id());
        } else if (const auto* lsdj = rfl::get_if<LsdjSyncConfig>(&rc.variant())) {
            auto role = std::make_unique<LsdjSyncRole>(*lsdj);
            role->onAttach(*this);
            roles_.push_back(std::move(role));
            std::fprintf(stderr, "[RetroPlug] attached LSDJ sync role to system %u\n", id());
        } else if (const auto* kitCfg = rfl::get_if<rp::lsdj::LsdjKitPatchConfig>(&rc.variant())) {
            auto role = std::make_unique<LsdjKitPatchRole>();
            role->onAttach(*this);
            // Replay any persisted kit bytes through the role so the
            // running emulator sees them after a project load. The role
            // applies pending patches at the top of the next process
            // block (sniffer-attached default with no kits is a no-op).
            role->queueAllFromConfig(*kitCfg);
            roles_.push_back(std::move(role));
            std::fprintf(stderr, "[RetroPlug] attached LSDJ kit-patch role to system %u (%zu kits)\n",
                         id(), kitCfg->kits.size());
        }
    }
    // Recompute the serial-out capture gate now that roles_ is settled.
    serialOutEnabled_ = false;
    for (const auto& role : roles_) {
        if (role && role->wantsSerialOut()) {
            serialOutEnabled_ = true;
            break;
        }
    }
}

void SameBoySystem::captureSerialOutBit(bool bit) {
    // MSB-first to match nextSerialInBit / standard GB serial shift order.
    serialOutByte_ = static_cast<std::uint8_t>(
        (serialOutByte_ << 1) | (bit ? 1u : 0u));
    if (++serialOutBits_ < 8) return;

    const std::uint8_t completed = serialOutByte_;
    serialOutByte_ = 0;
    serialOutBits_ = 0;
    // Diagnostic capture before fan-out so even decoder-internal exceptions
    // wouldn't drop the raw log entry.
    serialOutLog_.emplace_back(audioFrameCount_, completed);
    for (auto& role : roles_) {
        if (role) role->onSerialOutByte(*this, completed);
    }
}

bool SameBoySystem::nextSerialInBit() {
    if (serialIn_.empty()) return true; // idle high

    if (serialBitsRemaining_ == 0) {
        serialBitsRemaining_ = 8;
    }

    const std::uint8_t byte = serialIn_.front();
    const int bitIndex = serialBitsRemaining_ - 1; // MSB-first: 7..0
    const bool bit = ((byte >> bitIndex) & 1u) != 0;

    --serialBitsRemaining_;
    if (serialBitsRemaining_ == 0) {
        serialIn_.pop_front();
    }
    return bit;
}

void SameBoySystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
    if (events == nullptr || count == 0) return;
    pendingMidi_.insert(pendingMidi_.end(), events, events + count);
    for (auto& role : roles_) {
        if (role) role->onMidi(*this, events, count);
    }
}

// TODO: Remove references to old code in comments

void SameBoySystem::applyHighpassMode() {
    if (!gb_) return;
    GB_highpass_mode_t mode = GB_HIGHPASS_ACCURATE;
    switch (config_.highpass) {
        case SameBoyHighpass::Off:            mode = GB_HIGHPASS_OFF;             break;
        case SameBoyHighpass::Accurate:       mode = GB_HIGHPASS_ACCURATE;        break;
        case SameBoyHighpass::RemoveDcOffset: mode = GB_HIGHPASS_REMOVE_DC_OFFSET; break;
    }
    GB_set_highpass_filter_mode(gb_, mode);
}

void SameBoySystem::pressButton(std::uint8_t button, bool down) {
    // Append to the back of the queue, advancing the offset by buttonSpacing
    // from the previous entry. This stops a press+release pair sent in the
    // same UI tick from collapsing onto a single sample (which the joypad
    // debouncer would simply miss). Mirrors the old SameBoyUtil::processButtons
    // logic at old/src/sameboy/SameBoyUtil.cpp:149-163.
    std::uint32_t offset = 0;
    if (!pendingButtons_.empty())
        offset = pendingButtons_.back().offset + buttonSpacingSamples_;
    pendingButtons_.push_back(PendingButton{offset, static_cast<GameboyButton>(button), down});
}

void SameBoySystem::writeAudioSample(int16_t left, int16_t right) {
    const std::size_t writeIdx = static_cast<std::size_t>(audioFrameCount_) * 2;
    if (writeIdx + 1 < stereoAccum_.size()) {
        stereoAccum_[writeIdx + 0] = s16ToF32(left);
        stereoAccum_[writeIdx + 1] = s16ToF32(right);
    }
    // Overproduction beyond the block size is silently discarded (matches the
    // old behavior in old/src/sameboy/SameBoyUtil.cpp; ≤1 sample/block click).
    ++audioFrameCount_;

    // TODO: Does this belong here?

    // Synthetic Arduinoboy clock for master-mode roles. LSDJ in MI.OUT sets
    // SC=0x80 (transfer enable + external clock) and waits for a clock source
    // to shift its byte out. Real Arduinoboy hardware drives the clock; we
    // emulate that here by shifting one bit per audio sample whenever LSDJ
    // has a transfer pending and external clock is selected. The outgoing bit
    // is read from SB.bit7 BEFORE the shift; the incoming bit is idle-high
    // (1), matching the GB's pull-up behavior on the SIN line.
    // Bypassed when a link peer exists (LinkGroup handles that path).
    if (linkPeers_.empty() && serialOutEnabled_ && gb_) {
        const auto sc = gb_->io_registers[GB_IO_SC];
        if ((sc & 0x81) == 0x80) {
            const bool outBit = (gb_->io_registers[GB_IO_SB] & 0x80) != 0;
            captureSerialOutBit(outBit);
            GB_serial_set_data_bit(gb_, true);
        }
    }
}

void SameBoySystem::onVblank() {
    // SameBoy just finished writing the current write slot; rotate to the next
    // and re-point its pixel output target.
    frames_.publish();
    if (gb_) {
        GB_set_pixels_output(gb_, frames_.writeSlot());
    }
}

void SameBoySystem::prepareForBlock(const AudioBlockInfo& info) {
    if (!activated_ || !gb_) return;

    const std::uint32_t frames = info.frames;
    if (stereoAccum_.size() < std::size_t(frames) * 2 + 2) {
        // Pre-size with a little headroom for the ≤1-sample overproduction.
        stereoAccum_.assign(std::size_t(frames) * 2 + 16, 0.0f);
    } else {
        std::fill_n(stereoAccum_.data(), std::size_t(frames) * 2, 0.0f);
    }

    audioFrameCount_ = 0;
    serialOutLog_.clear();
}

bool SameBoySystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
    if (!activated_ || !gb_) return false;
    if (audioFrameCount_ >= framesNeeded) return false;

    // Drain queued button transitions whose offset has been reached. Inside
    // the inner loop so a press+release pair sent in the same UI tick lands
    // at distinct sample offsets (joypad debouncer would miss zero-duration).
    while (!pendingButtons_.empty() &&
           pendingButtons_.front().offset <= audioFrameCount_) {
        const auto& pb = pendingButtons_.front();
        GB_set_key_state(gb_, static_cast<GB_key_t>(pb.button), pb.down);
        pendingButtons_.pop_front();
    }

    // Slave-mode serial pump. mGB and other "MIDI-listener" ROMs hold SC at
    // 0x80 (transfer enabled, external clock) waiting for a master to clock
    // bytes in. SameBoy's GB_serial_set_data_bit shifts one bit into SB and,
    // after the 8th call, clears SC bit 7 + raises the serial interrupt — so
    // 8 calls = one delivered byte. Only push when SC bit 7 is set (the GB is
    // ready) AND bit 0 is clear (external clock = slave). Linked GBs route
    // bits via serialBitFromPeer/Broadcast instead, so skip when peers exist.
    if (linkPeers_.empty() && !serialIn_.empty()) {
        const std::uint8_t sc = GB_safe_read_memory(gb_, 0xFF02);
        if ((sc & 0x80) && !(sc & 0x01)) {
            const std::uint8_t byte = serialIn_.front();
            serialIn_.pop_front();
            for (int b = 7; b >= 0; --b) {
                GB_serial_set_data_bit(gb_, ((byte >> b) & 1u) != 0);
            }
        }
    }

    GB_run(gb_);
    return audioFrameCount_ < framesNeeded;
}

void SameBoySystem::finishBlock(const AudioBlockInfo& info, float* const* outs) {
    if (!activated_ || !gb_) return;

    const std::uint32_t frames = info.frames;

    // Any button transitions that didn't land in this block stay queued; shift
    // their offsets back so the relative ordering (and timing) is preserved.
    for (auto& pb : pendingButtons_) {
        pb.offset = (pb.offset > frames) ? pb.offset - frames : 0;
    }

    // Sum interleaved stereo into the planar L/R outputs with smoothed gain.
    float* outL = outs[0];
    float* outR = outs[1];
    for (std::uint32_t i = 0; i < frames; ++i) {
        const float g = gainSmoother_.next();
        outL[i] += stereoAccum_[std::size_t(i) * 2 + 0] * g;
        outR[i] += stereoAccum_[std::size_t(i) * 2 + 1] * g;
    }

    audioFrameCount_ = 0;

    for (auto& role : roles_) {
        role->onProcessBlock(*this, info);
    }

    // Roles have had their chance to consume this block's MIDI; clear so the
    // next block starts empty. midiOut_ is drained by PluginDSP after
    // Project::onProcess returns.
    pendingMidi_.clear();

    // Tear-free memory snapshots for any UI subscriptions. Done AFTER role
    // processing so kit patches applied this block are visible immediately.
    publishMemorySnapshots();
    publishStateSnapshot(frames, sampleRate_);
}

// onProcess is no longer overridden here: the runner (runUnit) drives the triad
// directly for every unit, skipping linked systems via isLinked() and driving
// them through their LinkGroup, so no self-bail lives here anymore. The base
// SystemBase::onProcess remains as a fused convenience entry for direct callers.

rp::MemoryAccessor SameBoySystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!gb_) return rp::MemoryAccessor{};

    GB_direct_access_t native;
    switch (type) {
        case rp::MemoryType::Ram:         native = GB_DIRECT_ACCESS_RAM;       break;
        case rp::MemoryType::Rom:         native = GB_DIRECT_ACCESS_ROM;       break;
        case rp::MemoryType::Sram:        native = GB_DIRECT_ACCESS_CART_RAM;  break;
        case rp::MemoryType::Vram:        native = GB_DIRECT_ACCESS_VRAM;      break;
        case rp::MemoryType::IORegisters: native = GB_DIRECT_ACCESS_IO;        break;
        case rp::MemoryType::HRam:        native = GB_DIRECT_ACCESS_HRAM;      break;
        case rp::MemoryType::OAM:         native = GB_DIRECT_ACCESS_OAM;       break;
        case rp::MemoryType::NametableRam:
        case rp::MemoryType::ExtWorkRam:
        default:                          return rp::MemoryAccessor{};
    }

    std::size_t   size = 0;
    std::uint16_t bank = 0; // unused; full buffer is returned linearly
    void* data = GB_get_direct_access(gb_, native, &size, &bank);
    if (!data || size == 0) return rp::MemoryAccessor{};
    return rp::MemoryAccessor{type, access, static_cast<std::uint8_t*>(data), size};
}

// -- CPU state ---------------------------------------------------------------

std::vector<rp::CpuRegister> SameBoySystem::getCpuRegisters() const {
    if (!gb_) return {};
    const GB_registers_t* r = GB_get_registers(gb_);
    return {
        { "af", r->af, 16 },
        { "bc", r->bc, 16 },
        { "de", r->de, 16 },
        { "hl", r->hl, 16 },
        { "sp", r->sp, 16 },
        { "pc", r->pc, 16 },
    };
}

bool SameBoySystem::setCpuRegister(std::string_view name, std::uint32_t value) {
    if (!gb_) return false;
    GB_registers_t* r = GB_get_registers(gb_);
    const std::uint16_t v = static_cast<std::uint16_t>(value);
    if      (name == "af") r->af = v;
    else if (name == "bc") r->bc = v;
    else if (name == "de") r->de = v;
    else if (name == "hl") r->hl = v;
    else if (name == "sp") r->sp = v;
    else if (name == "pc") r->pc = v;
    else return false;
    return true;
}

std::optional<std::uint32_t> SameBoySystem::getProgramCounter() const {
    if (!gb_) return std::nullopt;
    return GB_get_registers(gb_)->pc;
}

std::optional<std::uint8_t> SameBoySystem::readCpuByte(std::uint32_t addr) const {
    if (!gb_) return std::nullopt;
    return GB_safe_read_memory(gb_, static_cast<std::uint16_t>(addr));
}

std::uint64_t SameBoySystem::stepInstruction() {
    if (!gb_) return 0;
    return static_cast<std::uint64_t>(GB_run(gb_));
}

SystemConfig SameBoySystem::snapshotConfig() const {
    SameBoyConfig out = config_;
    if (out.embedRom) {
        out.romBytes = rom_;
    } else {
        out.romBytes.clear();
    }
    if (gb_) {
        const std::size_t saveSize = GB_get_save_state_size((GB_gameboy_t*)gb_);
        std::vector<std::uint8_t> save(saveSize);
        GB_save_state_to_buffer((GB_gameboy_t*)gb_, save.data());
        out.savestate = std::move(save);

        // Capture cartridge battery RAM. Returns 0 for carts without a
        // battery (RTC-only or none) — those simply don't carry an `sram`.
        const int sramSize = GB_save_battery_size((GB_gameboy_t*)gb_);
        if (sramSize > 0) {
            std::vector<std::uint8_t> sram(static_cast<std::size_t>(sramSize));
            if (GB_save_battery_to_buffer((GB_gameboy_t*)gb_, sram.data(), sram.size()) == 0) {
                out.sram = std::move(sram);
            } else {
                out.sram.clear();
            }
        } else {
            out.sram.clear();
        }
    } else {
        out.savestate.clear();
        out.sram.clear();
    }
    return out;
}
