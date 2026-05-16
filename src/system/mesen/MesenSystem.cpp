#include "system/mesen/MesenSystem.hpp"

#include <cmath>
#include <cstdio>
#include <thread>

#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenVideoDevice.hpp"
#include "system/mesen/NesEverdriveFifo.hpp"
#include "system/mesen/roles/NesN8MidiRole.hpp"

#include "Core/NES/Input/NesController.h"
#include "Core/NES/NesConsole.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/NesMemoryManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/CpuType.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/EventType.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"

namespace {

float dbToLin(float dB) {
    return dB > -90.0f ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}

// NES NTSC palette (matches the legacy Mesen integration so colors don't
// drift between old and new builds).
constexpr uint32_t kNesPalette[64] = {
    0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00,
    0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00,
    0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32, 0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22,
    0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
    0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

void configureNes(Emulator& emu) {
    EmuSettings* settings = emu.GetSettings();
    NesConfig cfg{};
    cfg.Port1 = ControllerConfig{ .Type = ControllerType::NesController };
    cfg.Port2 = ControllerConfig{ .Type = ControllerType::NesController };
    for (int i = 0; i < 64; ++i) {
        cfg.UserPalette[i] = kNesPalette[i];
    }
    for (int i = 0; i < 11; ++i) {
        cfg.ChannelVolumes[i] = 100;
    }
    settings->SetNesConfig(cfg);
}

constexpr double kNesCpuHz = 1789773.0;

} // namespace

MesenSystem::MesenSystem(SystemId id,
                         MesenConfig config,
                         std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)) {
    gainSmoother_.setTimeConstant(0.020f);
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
}

MesenSystem::~MesenSystem() {
    onDeactivate();
}

void MesenSystem::onActivate(double sampleRate) {
    if (activated_) return;
    if (rom_.empty()) {
        std::fprintf(stderr, "[MesenSystem] no ROM bytes; not activating\n");
        return;
    }

    sampleRate_  = sampleRate;
    threadIdSet_ = false;

    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();

    // Mesen reads/writes config files relative to a "home folder". We don't
    // need anything persistent right now; point it at /tmp so any incidental
    // writes don't pollute the user's HOME.
    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen");
    MessageManager::SetOptions(false, true);

    emu_ = std::make_unique<Emulator>();
    emu_->Initialize();
    configureNes(*emu_);

    VirtualFile romFile(rom_.data(), rom_.size(),
                        config_.romPath.empty() ? std::string("rom.nes") : config_.romPath);

    // stopRom=false: keep Mesen from spawning its internal _emuThread. We
    // drive cpu->Exec() ourselves from the audio thread.
    if (!emu_->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
        std::fprintf(stderr, "[MesenSystem] Mesen failed to load ROM '%s'\n", config_.romPath.c_str());
        emu_.reset();
        return;
    }

    // Tell Mesen to render audio at the host sample rate.
    AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
    audioCfg.SampleRate = static_cast<uint32_t>(sampleRate);
    emu_->GetSettings()->SetAudioConfig(audioCfg);

    audioDevice_ = std::make_shared<MesenAudioDevice>();
    emu_->GetSoundMixer()->RegisterAudioDevice(audioDevice_.get());

    videoDevice_ = std::make_shared<MesenVideoDevice>();
    videoDevice_->setFramebuffer(&frames_);
    emu_->GetVideoRenderer()->RegisterRenderingDevice(videoDevice_.get());

    // Always-attach the N8 FIFO role on NES — see NesN8MidiRole.hpp's docs
    // for the rationale (FIFO is benign if the ROM doesn't touch $40F0/$40F1).
    if (auto* nesConsole = dynamic_cast<NesConsole*>(emu_->GetConsole().get())) {
        n8Role_ = std::make_unique<NesN8MidiRole>();
        n8Role_->onAttach(*nesConsole);
    }

    activated_ = true;
}

void MesenSystem::onDeactivate() {
    if (!activated_) return;
    // Order matters: the role holds a reference to the FIFO that was
    // registered with Mesen's NesMemoryManager. Drop the role (and thus the
    // memory-manager registration) before tearing down the emulator.
    n8Role_.reset();
    emu_.reset();
    audioDevice_.reset();
    videoDevice_.reset();
    activated_ = false;
}

void MesenSystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    if (emu_) {
        AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
        audioCfg.SampleRate = static_cast<uint32_t>(sampleRate);
        emu_->GetSettings()->SetAudioConfig(audioCfg);
    }
}

void MesenSystem::onReset() {
    if (emu_) emu_->Reset();
}

void MesenSystem::setGainDb(float dB) {
    config_.gainDb = dB;
    gainSmoother_.setTargetValue(dbToLin(dB));
}

void MesenSystem::pressButton(std::uint8_t button, bool down) {
    pendingButtons_.push_back({ button, down });
}

void MesenSystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
    if (n8Role_) {
        n8Role_->onMidi(events, count);
    }
}

namespace {
NesController::Buttons toNesButton(std::uint8_t b) {
    switch (static_cast<NesButton>(b)) {
        case NesButton::Right:  return NesController::Buttons::Right;
        case NesButton::Left:   return NesController::Buttons::Left;
        case NesButton::Up:     return NesController::Buttons::Up;
        case NesButton::Down:   return NesController::Buttons::Down;
        case NesButton::A:      return NesController::Buttons::A;
        case NesButton::B:      return NesController::Buttons::B;
        case NesButton::Select: return NesController::Buttons::Select;
        case NesButton::Start:  return NesController::Buttons::Start;
        default:                return NesController::Buttons::A;
    }
}
} // namespace

void MesenSystem::onProcess(const AudioBlockInfo& info, float* const* outs) {
    if (!activated_ || !emu_) return;

    // First call from the audio thread: tell Mesen this is the emulation
    // thread so its internal `IsEmulationThread()` checks pass during
    // cpu->Exec().
    if (!threadIdSet_) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
        threadIdSet_ = true;
    }

    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return;

    // Apply pending button transitions to NES controller 0 before InputPolled.
    if (!pendingButtons_.empty()) {
        if (auto controller = console->GetControlManager()->GetControlDeviceByIndex(0)) {
            for (const auto& pb : pendingButtons_) {
                controller->SetBitValue(toNesButton(pb.button), pb.down);
            }
        }
        pendingButtons_.clear();
    }

    emu_->ProcessEvent(EventType::InputPolled, CpuType::Nes);

    auto* cpu = console->GetCpu();
    const std::uint32_t blockSize = info.frames;

    // Run the CPU one instruction at a time until the audio device has
    // accumulated enough samples for this block. The APU auto-flushes into
    // MesenAudioDevice every CycleLength APU cycles via NesApu::EndFrame().
    // ~227 samples per flush at 44.1 kHz, so blockSize samples are ready
    // well within one PPU frame.
    while (audioDevice_->availableFrames() < blockSize) {
        cpu->Exec();
    }

    if (stereoAccum_.size() < std::size_t(blockSize) * 2) {
        stereoAccum_.assign(std::size_t(blockSize) * 2, 0.0f);
    }
    audioDevice_->drain(stereoAccum_.data(), blockSize);

    // Sum interleaved stereo into the planar L/R outputs with smoothed gain
    // (matches SameBoySystem::finishBlock so multi-system mixes are uniform).
    float* outL = outs[0];
    float* outR = outs[1];
    for (std::uint32_t i = 0; i < blockSize; ++i) {
        const float g = gainSmoother_.next();
        outL[i] += stereoAccum_[std::size_t(i) * 2 + 0] * g;
        outR[i] += stereoAccum_[std::size_t(i) * 2 + 1] * g;
    }
}

SystemConfig MesenSystem::snapshotConfig() const {
    MesenConfig out = config_;
    if (out.embedRom) {
        out.romBytes = Base64Bytes(rom_);
    } else {
        out.romBytes = Base64Bytes{};
    }
    // Savestate/SRAM round-trip lands with step 16 (savestate slots). For
    // Phase A we just snapshot the static config + ROM bytes.
    return out;
}
