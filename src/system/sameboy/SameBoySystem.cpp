#include "system/sameboy/SameBoySystem.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

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

GB_model_t toSameBoyModel(GameboyModel model) {
    switch (model) {
        case GameboyModel::DmgB: return GB_MODEL_DMG_B;
        case GameboyModel::CgbC: return GB_MODEL_CGB_C;
        case GameboyModel::CgbE: return GB_MODEL_CGB_E;
        case GameboyModel::Agb:  return GB_MODEL_AGB;
        case GameboyModel::Auto: // fallthrough
        default:                  return GB_MODEL_CGB_C;
    }
}

std::string_view findBootRom(GB_model_t model, bool fastBoot) {
    switch (model) {
        case GB_MODEL_DMG_B: return std::string_view((const char*)dmg_boot, dmg_boot_len);
        case GB_MODEL_AGB:   return std::string_view((const char*)agb_boot, agb_boot_len);
        default:
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

// SameBoy requires serial-bit start/end callbacks even when we don't use the
// link cable; provide trivial no-ops (matches old behavior).
void serialStart(GB_gameboy_t*, bool) {}
bool serialEnd(GB_gameboy_t*) { return true; }

constexpr float s16ToF32(int16_t v) {
    return v < 0 ? float(v) / 32768.0f : float(v) / 32767.0f;
}

} // namespace

SameBoySystem::SameBoySystem(SystemId id,
                             SameBoyConfig config,
                             std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)) {}

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
    buttonSpacingSamples_ = static_cast<std::uint32_t>(sampleRate * 0.010); // 10 ms spacing
    audioFrameCount_ = 0;

    gb_ = new GB_gameboy_t();
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
    GB_set_highpass_filter_mode(gb_, GB_HIGHPASS_ACCURATE);

    GB_load_rom_from_buffer(gb_, rom_.data(), rom_.size());

    if (!config_.savestate.empty()) {
        const auto& save = config_.savestate.bytes();
        if (GB_load_state_from_buffer(gb_, save.data(), save.size()) != 0) {
            std::fprintf(stderr, "[SameBoySystem] failed to load savestate\n");
        }
    }

    activated_ = true;
}

void SameBoySystem::onDeactivate() {
    if (!activated_) return;
    if (gb_) {
        GB_free(gb_);
        delete gb_;
        gb_ = nullptr;
    }
    activated_ = false;
}

void SameBoySystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    buttonSpacingSamples_ = static_cast<std::uint32_t>(sampleRate * 0.010); // 10 ms
    if (gb_) {
        GB_set_sample_rate(gb_, static_cast<unsigned>(sampleRate));
    }
}

void SameBoySystem::onReset() {
    if (gb_) {
        GB_reset(gb_);
        audioFrameCount_ = 0;
    }
    pendingButtons_.clear();
}

void SameBoySystem::pressButton(GameboyButton button, bool down) {
    // Append to the back of the queue, advancing the offset by buttonSpacing
    // from the previous entry. This stops a press+release pair sent in the
    // same UI tick from collapsing onto a single sample (which the joypad
    // debouncer would simply miss). Mirrors the old SameBoyUtil::processButtons
    // logic at old/src/sameboy/SameBoyUtil.cpp:149-163.
    std::uint32_t offset = 0;
    if (!pendingButtons_.empty())
        offset = pendingButtons_.back().offset + buttonSpacingSamples_;
    pendingButtons_.push_back(PendingButton{offset, button, down});
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
}

void SameBoySystem::onVblank() {
    // SameBoy just finished writing the current write slot; rotate to the next
    // and re-point its pixel output target.
    frames_.publish();
    if (gb_) {
        GB_set_pixels_output(gb_, frames_.writeSlot());
    }
}

void SameBoySystem::onProcess(const AudioBlockInfo& info, float* const* outs) {
    if (!activated_ || !gb_) return;

    const std::uint32_t frames = info.frames;
    if (stereoAccum_.size() < std::size_t(frames) * 2 + 2) {
        // Pre-size with a little headroom for the ≤1-sample overproduction.
        stereoAccum_.assign(std::size_t(frames) * 2 + 16, 0.0f);
    } else {
        std::fill_n(stereoAccum_.data(), std::size_t(frames) * 2, 0.0f);
    }

    audioFrameCount_ = 0;

    // Drive the emulator until enough samples are produced, applying any
    // queued button transitions whose offset is reached before that sample.
    // Drive the emulator until enough samples are produced, applying any
    // queued button transitions whose offset is reached before that sample.
    while (audioFrameCount_ < frames) {
        while (!pendingButtons_.empty() &&
               pendingButtons_.front().offset <= audioFrameCount_) {
            const auto& pb = pendingButtons_.front();
            GB_set_key_state(gb_, static_cast<GB_key_t>(pb.button), pb.down);
            pendingButtons_.pop_front();
        }
        GB_run(gb_);
    }

    // Any button transitions that didn't land in this block stay queued; shift
    // their offsets back so the relative ordering (and timing) is preserved.
    for (auto& pb : pendingButtons_) {
        pb.offset = (pb.offset > frames) ? pb.offset - frames : 0;
    }

    // Sum interleaved stereo into the planar L/R outputs (DPF planar buffers).
    float* outL = outs[0];
    float* outR = outs[1];
    for (std::uint32_t i = 0; i < frames; ++i) {
        outL[i] += stereoAccum_[std::size_t(i) * 2 + 0];
        outR[i] += stereoAccum_[std::size_t(i) * 2 + 1];
    }

    audioFrameCount_ = 0;

    for (auto& role : roles_) {
        role->onProcessBlock(*this, info);
    }
}

SystemConfig SameBoySystem::snapshotConfig() const {
    SameBoyConfig out = config_;
    if (out.embedRom) {
        out.romBytes = Base64Bytes(rom_);
    } else {
        out.romBytes = Base64Bytes{};
    }
    if (gb_) {
        const std::size_t saveSize = GB_get_save_state_size((GB_gameboy_t*)gb_);
        std::vector<std::uint8_t> save(saveSize);
        GB_save_state_to_buffer((GB_gameboy_t*)gb_, save.data());
        out.savestate = Base64Bytes(std::move(save));
    } else {
        out.savestate = Base64Bytes{};
    }
    return out;
}
