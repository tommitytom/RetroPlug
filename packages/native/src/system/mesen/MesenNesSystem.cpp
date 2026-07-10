#include "system/mesen/MesenNesSystem.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenVideoDevice.hpp"
#include "system/mesen/MesenNesDebugSession.hpp"
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
#include "Core/Shared/MemoryType.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/SaveStateManager.h"
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

void configureNes(Emulator& emu, std::uint32_t region, bool removeSpriteLimit) {
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
    // TS-owned "mesen" role knobs, seeded before LoadRom so region is correct from power-on.
    cfg.Region = static_cast<ConsoleRegion>(region);
    cfg.RemoveSpriteLimit = removeSpriteLimit;
    settings->SetNesConfig(cfg);
}

constexpr double kNesCpuHz = 1789773.0;

} // namespace

MesenNesSystem::MesenNesSystem(SystemId id,
                         MesenNesConfig config,
                         std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)) {
    gainSmoother_.setTimeConstant(0.020f);
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
}

MesenNesSystem::~MesenNesSystem() {
    onDeactivate();
}

void MesenNesSystem::onActivate(double sampleRate) {
    if (activated_) return;
    if (rom_.empty()) {
        std::fprintf(stderr, "[MesenNesSystem] no ROM bytes; not activating\n");
        return;
    }

    sampleRate_  = sampleRate;

    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();

    // Mesen reads/writes config files relative to a "home folder". We don't
    // need anything persistent right now; point it at /tmp so any incidental
    // writes don't pollute the user's HOME.
    FolderUtilities::SetHomeFolder("/tmp/retroplug-mesen");
    MessageManager::SetOptions(false, true);

    emu_ = std::make_unique<Emulator>();
    // enableShortcuts=false: the plugin drives input/transport itself and never
    // uses Mesen's keyboard-shortcut layer. Disabling it avoids a per-instance
    // background polling thread (ShortcutKeyHandler) that, besides being pure
    // overhead, races the debugger pointer against LoadRom's ResetDebugger.
    emu_->Initialize(false);
    configureNes(*emu_, config_.region, config_.removeSpriteLimit);

    VirtualFile romFile(rom_.data(), rom_.size(),
                        config_.romPath.empty() ? std::string("rom.nes") : config_.romPath);

    // stopRom=false: keep Mesen from spawning its internal _emuThread. We
    // drive cpu->Exec() ourselves from the audio thread.
    if (!emu_->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
        std::fprintf(stderr, "[MesenNesSystem] Mesen failed to load ROM '%s'\n", config_.romPath.c_str());
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

    // Restore persisted battery RAM / savestate. Mesen's BatteryManager will
    // have already called LoadBattery (no provider set → from disk under our
    // tmp home folder, almost always empty). Override by writing directly
    // into the live NesSaveRam region.
    if (!config_.sram.empty()) {
        auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
        if (accessor.valid() && accessor.size() > 0) {
            const std::size_t n = std::min(config_.sram.size(), accessor.size());
            if (n > 0) std::memcpy(accessor.data(), config_.sram.data(), n);
        }
    }
    if (!config_.savestate.empty()) {
        loadStateBytes(config_.savestate);
    }

    activated_ = true;
}

void MesenNesSystem::onDeactivate() {
    if (!activated_) return;
    // Order matters: the role holds a reference to the FIFO that was
    // registered with Mesen's NesMemoryManager. Drop the role (and thus the
    // memory-manager registration) before tearing down the emulator.
    n8Role_.reset();
    debugSession_.reset();  // holds emu_ (raw); drop before the emulator
    emu_.reset();
    audioDevice_.reset();
    videoDevice_.reset();
    activated_ = false;
}

rp::IDebugTarget* MesenNesSystem::debugTarget() {
    if (!activated_ || !emu_) return nullptr;
    if (!debugSession_)
        debugSession_ = std::make_unique<MesenNesDebugSession>(emu_.get());
    return debugSession_.get();
}

void MesenNesSystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    if (emu_) {
        AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
        audioCfg.SampleRate = static_cast<uint32_t>(sampleRate);
        emu_->GetSettings()->SetAudioConfig(audioCfg);
    }
}

void MesenNesSystem::onReset() {
    if (emu_) emu_->Reset();
}

void MesenNesSystem::setGainDb(float dB) {
    config_.gainDb = dB;
    gainSmoother_.setTargetValue(dbToLin(dB));
}

void MesenNesSystem::setRemoveSpriteLimit(bool on) {
    config_.removeSpriteLimit = on;
    // Live: the PPU re-reads this from the config reference every scanline.
    if (emu_) emu_->GetSettings()->GetNesConfig().RemoveSpriteLimit = on;
}

void MesenNesSystem::setRegion(std::uint32_t region) {
    if (config_.region == region) return;  // value-guarded — a reset is expensive
    config_.region = region;
    // Region reconfigures CPU/PPU/APU timing, which this integration only re-polls on reset (the NES
    // path drives cpu->Exec() directly and bypasses RunFrame). Mirror SameBoy model→restartEmulator.
    if (emu_) {
        emu_->GetSettings()->GetNesConfig().Region = static_cast<ConsoleRegion>(region);
        emu_->Reset();
    }
}

void MesenNesSystem::pressButton(std::uint8_t button, bool down) {
    pendingButtons_.push_back({ button, down });
}

void MesenNesSystem::onMidi(const ::MidiEvent* events, std::uint32_t count) {
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

void MesenNesSystem::prepareForBlock(const AudioBlockInfo& /*info*/) {
    if (!activated_ || !emu_) return;

    // Bind Mesen's emulation thread to whoever drives this block, so its
    // internal `IsEmulationThread()` checks pass during cpu->Exec(). Rebinds
    // when the driving thread changes (e.g. boot on the main thread, then an
    // offline parallel render on a worker, then back) — cheap thread-id compare.
    if (!emu_->IsEmulationThread()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
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
}

bool MesenNesSystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
    if (!activated_ || !emu_) return false;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return false;
    auto* cpu = console->GetCpu();

    // Degenerate 1-member unit: run the whole block here and report "done"
    // (false). Run the CPU one instruction at a time until the audio device
    // has accumulated enough samples for this block. The APU auto-flushes into
    // MesenAudioDevice every CycleLength APU cycles via NesApu::EndFrame().
    // ~227 samples per flush at 44.1 kHz, so blockSize samples are ready well
    // within one PPU frame.
    while (audioDevice_->availableFrames() < framesNeeded) {
        cpu->Exec();
    }
    return false;
}

void MesenNesSystem::finishBlock(const AudioBlockInfo& info, float* const* outs) {
    if (!activated_ || !emu_) return;

    const std::uint32_t blockSize = info.frames;
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

    // Tear-free memory snapshots for any UI subscriptions. End-of-block =
    // internally consistent state because the CPU isn't mid-instruction.
    publishMemorySnapshots();
    publishStateSnapshot(info.frames, sampleRate_);
}

std::size_t MesenNesSystem::stateSnapshotSize() const {
    // Mesen savestates are variable-size (SaveStateManager streams them), so
    // size the triple once with headroom; the publisher skips any capture that
    // would exceed it rather than reallocating mid-life.
    const std::size_t measured = saveStateBytes().size();
    if (measured == 0) return 0;
    return measured + measured / 2 + 8192;
}

bool MesenNesSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    // Goes through the streaming SaveState path, which allocates internally —
    // acceptable at the coarse snapshot interval, not per audio block.
    dst = saveStateBytes();
    return !dst.empty();
}

rp::MemoryAccessor MesenNesSystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!emu_) return rp::MemoryAccessor{};

    ::MemoryType native;
    switch (type) {
        case rp::MemoryType::Ram:          native = ::MemoryType::NesInternalRam;  break;
        case rp::MemoryType::Rom:          native = ::MemoryType::NesPrgRom;       break;
        case rp::MemoryType::Sram:         native = ::MemoryType::NesSaveRam;      break;
        case rp::MemoryType::OAM:          native = ::MemoryType::NesSpriteRam;    break;
        case rp::MemoryType::NametableRam: native = ::MemoryType::NesNametableRam; break;
        case rp::MemoryType::Vram: {
            // NES carts ship either CHR-ROM (fixed graphics) or CHR-RAM
            // (writable graphics). Prefer the RAM view; fall through to ROM
            // when the cart has no CHR-RAM region.
            ConsoleMemoryInfo info = emu_->GetMemory(::MemoryType::NesChrRam);
            if (info.Memory && info.Size > 0) {
                return rp::MemoryAccessor{type, access,
                                          static_cast<std::uint8_t*>(info.Memory),
                                          info.Size};
            }
            native = ::MemoryType::NesChrRom;
            break;
        }
        case rp::MemoryType::IORegisters:
        case rp::MemoryType::HRam:
        case rp::MemoryType::ExtWorkRam:
        default: return rp::MemoryAccessor{};
    }

    ConsoleMemoryInfo info = emu_->GetMemory(native);
    if (!info.Memory || info.Size == 0) return rp::MemoryAccessor{};
    return rp::MemoryAccessor{type, access,
                              static_cast<std::uint8_t*>(info.Memory),
                              info.Size};
}

// -- CPU state ---------------------------------------------------------------

namespace {
NesCpu* nesCpu(Emulator* emu) {
    if (!emu) return nullptr;
    auto* console = dynamic_cast<NesConsole*>(emu->GetConsole().get());
    return console ? console->GetCpu() : nullptr;
}
} // namespace

std::vector<rp::CpuRegister> MesenNesSystem::getCpuRegisters() const {
    NesCpu* cpu = nesCpu(emu_.get());
    if (!cpu) return {};
    const NesCpuState& s = cpu->GetState();
    return {
        { "a",  s.A,  8 },
        { "x",  s.X,  8 },
        { "y",  s.Y,  8 },
        { "sp", s.SP, 8 },
        { "ps", s.PS, 8 },
        { "pc", s.PC, 16 },
    };
}

bool MesenNesSystem::setCpuRegister(std::string_view name, std::uint32_t value) {
    NesCpu* cpu = nesCpu(emu_.get());
    if (!cpu) return false;
    NesCpuState s = cpu->GetState();
    if      (name == "a")  s.A  = static_cast<std::uint8_t>(value);
    else if (name == "x")  s.X  = static_cast<std::uint8_t>(value);
    else if (name == "y")  s.Y  = static_cast<std::uint8_t>(value);
    else if (name == "sp") s.SP = static_cast<std::uint8_t>(value);
    else if (name == "ps") s.PS = static_cast<std::uint8_t>(value);
    else if (name == "pc") s.PC = static_cast<std::uint16_t>(value);
    else return false;
    cpu->SetState(s);
    return true;
}

std::optional<std::uint32_t> MesenNesSystem::getProgramCounter() const {
    NesCpu* cpu = nesCpu(emu_.get());
    if (!cpu) return std::nullopt;
    return cpu->GetState().PC;
}

std::optional<std::uint8_t> MesenNesSystem::readCpuByte(std::uint32_t addr) const {
    if (!emu_) return std::nullopt;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return std::nullopt;
    // Banking-aware, side-effect-free read of the 6502 address space.
    return console->GetMemoryManager()->DebugRead(static_cast<std::uint16_t>(addr));
}

bool MesenNesSystem::writeCpuByte(std::uint32_t addr, std::uint8_t value) {
    if (!emu_) return false;
    auto* console = dynamic_cast<NesConsole*>(emu_->GetConsole().get());
    if (!console) return false;
    // Debugger-style write into the 6502 address space, bypassing side effects
    // (the memory-edit path — mirrors readCpuByte's DebugRead).
    console->GetMemoryManager()->DebugWrite(static_cast<std::uint16_t>(addr), value, /*disableSideEffects*/ true);
    return true;
}

std::uint64_t MesenNesSystem::stepInstruction() {
    NesCpu* cpu = nesCpu(emu_.get());
    if (!cpu) return 0;
    // Exec() relies on Mesen's IsEmulationThread() check; bind it to the calling
    // thread (rebinds if that differs from the last driving thread).
    if (!emu_->IsEmulationThread()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
    }
    const std::uint64_t before = cpu->GetState().CycleCount;
    cpu->Exec();
    const std::uint64_t after = cpu->GetState().CycleCount;
    return after - before;
}

std::vector<std::uint8_t> MesenNesSystem::saveSramBytes() const {
    if (!emu_) return {};
    auto* self = const_cast<MesenNesSystem*>(this);
    auto accessor = self->getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    if (!accessor.valid() || accessor.size() == 0) return {};
    return std::vector<std::uint8_t>(accessor.data(),
                                     accessor.data() + accessor.size());
}

void MesenNesSystem::clearSram() {
    if (!emu_) return;
    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    if (!accessor.valid() || accessor.size() == 0) return;
    std::memset(accessor.data(), 0, accessor.size());
    config_.sram.clear();
}

std::vector<std::uint8_t> MesenNesSystem::saveStateBytes() const {
    if (!emu_) return {};
    std::stringstream ss(std::ios::out | std::ios::binary);
    emu_->GetSaveStateManager()->SaveState(ss);
    const std::string str = ss.str();
    return std::vector<std::uint8_t>(str.begin(), str.end());
}

bool MesenNesSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    if (!emu_ || bytes.empty()) return false;
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    ss.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
    ss.seekg(0);
    return emu_->GetSaveStateManager()->LoadState(ss);
}

std::unique_ptr<SystemBase> MesenNesSystem::clone(SystemId newId, double sampleRate) const {
    MesenNesConfig cfg = config_;
    cfg.savSuffix = 0;   // caller (duplicateSystem) assigns a non-colliding suffix
    cfg.savPath.clear(); // and its own sav file, not the source's paired one
    auto sramBytes = saveSramBytes();
    if (!sramBytes.empty()) cfg.sram = std::move(sramBytes);
    auto stateBytes = saveStateBytes();
    if (!stateBytes.empty()) cfg.savestate = std::move(stateBytes);
    std::vector<std::uint8_t> romCopy = rom_;
    auto out = std::make_unique<MesenNesSystem>(newId, std::move(cfg), std::move(romCopy));
    out->onActivate(sampleRate);
    return out;
}
