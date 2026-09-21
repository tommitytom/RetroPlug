#include "system/mesen/MesenGbaSystem.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

#include "system/InputTypes.hpp"
#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenVideoDevice.hpp"

#include "Core/GBA/APU/GbaApu.h"
#include "Core/GBA/GbaConsole.h"
#include "Core/GBA/GbaCpu.h"
#include "Core/GBA/GbaMemoryManager.h"
#include "Core/GBA/Input/GbaController.h"
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

#include "system/mesen/MesenStateCeiling.hpp"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Utilities/FolderUtilities.h"

#include "system/mesen/MesenGlobalInit.hpp"
#include "Utilities/VirtualFile.h"
#include "util/Gain.hpp"
#include "system/mesen/MesenBlockOps.hpp"

namespace {



void configureGba(Emulator& emu, bool skipBootScreen) {
    EmuSettings* settings = emu.GetSettings();
    // ::GbaConfig here refers to Mesen's struct in SettingTypes.h
    // (RetroPlug's wrapping config is MesenGbaConfig — see MesenGbaConfig.hpp).
    GbaConfig cfg{};
    cfg.Controller = ControllerConfig{ .Type = ControllerType::GbaController };
    cfg.SkipBootScreen = skipBootScreen;
    cfg.ChannelAVol = 100;
    cfg.ChannelBVol = 100;
    cfg.Square1Vol  = 100;
    cfg.Square2Vol  = 100;
    cfg.NoiseVol    = 100;
    cfg.WaveVol     = 100;
    settings->SetGbaConfig(cfg);
}

// Hands out the per-instance firmware directory number used below. Process-wide and monotonic,
// so it is unique among every GBA system this process ever builds - including ones constructed on
// the background render threads, hence the atomic.
std::uint64_t nextFirmwareSlot() {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

// Mesen's FirmwareHelper::LoadGbaBootRom hardcodes the filename `gba_bios.bin`, so honouring a
// user-provided biosPath means copying that file somewhere Mesen will look for it under that name.
//
// The folder is ours alone, and that matters: Mesen's own firmware folder is a process-global static
// (`FolderUtilities::GetFirmwareFolder()`), while a host runs several emulators at once - a DAW loads
// a plugin instance per track, and each background render spins its own. Sharing one folder makes the
// install and the load a two-step transaction over global state: whoever activates in between can
// replace the file, and the activating system then boots the other one's BIOS. Sequential activation
// happens to interleave safely, but render hosts construct cores on their own threads, so nothing
// orders those two steps. A directory per system removes the shared step rather than racing for it.
//
// So each system installs into `<home>/firmware/<slot>` and points its emulator at it through
// SetFirmwareFolderOverride (a RetroPlug addition to vendored Mesen, which consults it ahead of the
// shared folder). Returns the folder, or empty when there is nothing to install - failure is
// non-fatal either way, Mesen falls back to a zeroed boot ROM (HLE).
std::string installGbaBios(const std::string& biosPath, std::uint64_t slot) {
    if (biosPath.empty()) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path src(biosPath);
    if (!fs::exists(src, ec)) {
        std::fprintf(stderr, "[MesenGbaSystem] biosPath '%s' does not exist; falling back to HLE\n",
                     biosPath.c_str());
        return {};
    }
    fs::path dstDir = fs::path(mesenHomeFolder()) / "firmware" / std::to_string(slot);
    fs::create_directories(dstDir, ec);
    fs::path dst = dstDir / "gba_bios.bin";
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[MesenGbaSystem] failed to install BIOS '%s' -> '%s': %s\n",
                     biosPath.c_str(), dst.string().c_str(), ec.message().c_str());
        return {};
    }
    return dstDir.string();
}

} // namespace

MesenGbaSystem::MesenGbaSystem(SystemId id,
                     MesenGbaConfig config,
                     std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)),
      firmwareSlot_(nextFirmwareSlot()) {
    gainSmoother_.setTimeConstant(0.020f);
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
}

MesenGbaSystem::~MesenGbaSystem() {
    onDeactivate();
}

void MesenGbaSystem::onActivate(double sampleRate) {
    if (activated_) return;
    if (rom_.empty()) {
        std::fprintf(stderr, "[MesenGbaSystem] no ROM bytes; not activating\n");
        return;
    }

    sampleRate_  = sampleRate;

    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();

    // Shared with MesenNesSystem (NES). The home folder is per-process; set it once, thread-safely, so
    // concurrent core construction on background render threads doesn't race (see MesenGlobalInit).
    mesenGlobalInit();

    firmwareDir_ = installGbaBios(config_.biosPath, firmwareSlot_);

    emu_ = std::make_unique<Emulator>();
    // enableShortcuts=false: the plugin drives input/transport itself and never
    // uses Mesen's keyboard-shortcut layer. Disabling it avoids a per-instance
    // background polling thread (ShortcutKeyHandler) that, besides being pure
    // overhead, races the debugger pointer against LoadRom's ResetDebugger.
    emu_->Initialize(false);
    // Before LoadRom, which is what reads the boot ROM - and is re-run by every Reset.
    if (!firmwareDir_.empty()) emu_->SetFirmwareFolderOverride(firmwareDir_);
    configureGba(*emu_, config_.skipBootScreen);

    VirtualFile romFile(rom_.data(), rom_.size(),
                        config_.romPath.empty() ? std::string("rom.gba") : config_.romPath);

    // stopRom=false: keep Mesen from spawning its internal _emuThread. We
    // drive cpu->Exec() ourselves from the audio thread.
    if (!emu_->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
        std::fprintf(stderr, "[MesenGbaSystem] Mesen failed to load ROM '%s'\n", config_.romPath.c_str());
        emu_.reset();
        return;
    }

    // Tell Mesen to render audio at the host sample rate. The GBA APU emits
    // ~32 kHz internally; SoundMixer resamples to this target.
    AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
    audioCfg.SampleRate = static_cast<uint32_t>(sampleRate);
    emu_->GetSettings()->SetAudioConfig(audioCfg);

    audioDevice_ = std::make_shared<MesenAudioDevice>();
    emu_->GetSoundMixer()->RegisterAudioDevice(audioDevice_.get());

    videoDevice_ = std::make_shared<MesenVideoDevice>();
    videoDevice_->setFramebuffer(&frames_);
    emu_->GetVideoRenderer()->RegisterRenderingDevice(videoDevice_.get());

    // Restore persisted battery RAM / savestate. Write directly into the
    // GbaSaveRam region rather than going through Mesen's BatteryManager.
    seedSramAndState(config_.sram, config_.savestate);

    activated_ = true;
}

void MesenGbaSystem::onDeactivate() {
    if (!activated_) return;
    // GBA registers no audio provider today, so this is a no-op for it — but "no-op because of a fact
    // about the vendored core" is a comment, and this is the same three lines the other two need.
    stopMesenEmulator(emu_.get());
    emu_.reset();
    audioDevice_.reset();
    videoDevice_.reset();
    activated_ = false;

    if (!firmwareDir_.empty()) {
        // Ours alone, so it goes with us rather than accumulating one copy of the BIOS per system
        // ever loaded. remove_all because the installed gba_bios.bin is still in it.
        std::error_code ec;
        std::filesystem::remove_all(firmwareDir_, ec);
        firmwareDir_.clear();
    }
}

void MesenGbaSystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    if (emu_) {
        AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
        audioCfg.SampleRate = static_cast<uint32_t>(sampleRate);
        emu_->GetSettings()->SetAudioConfig(audioCfg);
    }
}


void MesenGbaSystem::setGainDb(float dB) {
    config_.gainDb = dB;
    gainSmoother_.setTargetValue(dbToLin(dB));
}

void MesenGbaSystem::pressButton(std::uint8_t button, bool down) {
    pendingButtons_.push_back({ button, down });
}

namespace {
GbaController::Buttons toGbaButton(std::uint8_t wire) {
    // Wire byte is the GbaButton name-index (position-aligned with the
    // other system kinds for the shared 8 names). Mesen's native enum has
    // a different order; the switch is the explicit remap.
    switch (static_cast<GbaButton>(wire)) {
        case GbaButton::Right:  return GbaController::Buttons::Right;
        case GbaButton::Left:   return GbaController::Buttons::Left;
        case GbaButton::Up:     return GbaController::Buttons::Up;
        case GbaButton::Down:   return GbaController::Buttons::Down;
        case GbaButton::A:      return GbaController::Buttons::A;
        case GbaButton::B:      return GbaController::Buttons::B;
        case GbaButton::Select: return GbaController::Buttons::Select;
        case GbaButton::Start:  return GbaController::Buttons::Start;
        case GbaButton::L:      return GbaController::Buttons::L;
        case GbaButton::R:      return GbaController::Buttons::R;
        default:                return GbaController::Buttons::A;
    }
}
} // namespace

void MesenGbaSystem::prepareForBlock(const AudioBlockInfo& /*info*/) {
    if (!activated_ || !emu_) return;

    // Bind Mesen's emulation thread to whoever drives this block, so its
    // internal `IsEmulationThread()` checks pass during console->RunFrame().
    // Rebinds when the driving thread changes (e.g. boot on the main thread,
    // then an offline parallel render on a worker, then back) — cheap compare.
    if (!emu_->IsEmulationThread()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
    }

    auto* console = dynamic_cast<GbaConsole*>(emu_->GetConsole().get());
    if (!console) return;

    // Apply pending button transitions to GBA controller 0 before InputPolled.
    if (!pendingButtons_.empty()) {
        if (auto controller = console->GetControlManager()->GetControlDeviceByIndex(0)) {
            for (const auto& pb : pendingButtons_) {
                controller->SetBitValue(toGbaButton(pb.button), pb.down);
            }
        }
        pendingButtons_.clear();
    }

    emu_->ProcessEvent(EventType::InputPolled, CpuType::Gba);
}

bool MesenGbaSystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
    if (!activated_ || !emu_) return false;
    auto* console = dynamic_cast<GbaConsole*>(emu_->GetConsole().get());
    if (!console) return false;

    // Degenerate 1-member unit: run the whole block here and report "done"
    // (false). The GBA APU is driven per-frame, not by CPU instructions:
    // GbaConsole::RunFrame steps the CPU until the PPU advances FrameCount,
    // then calls _apu->Run() + PlayQueuedAudio() to generate and flush the
    // samples for that frame. (Contrast with NES, where the APU auto-flushes
    // during CPU execution.) Loop RunFrame until we have enough samples for
    // this host block. ~735 samples per frame @ 44.1 kHz / 60 fps, so usually
    // 2 frames per 1024-sample block.
    //
    // Bounded, but be clear about what the bound is worth. The loop unit is a whole VIDEO FRAME, so a
    // 1024-sample block needs 2 iterations and a cap of a few more is the only sane shape - SMS's
    // instruction budget of 80000 would be ~22 minutes of emulated time here, decoration rather than a
    // backstop. And the genuinely unbounded loop is not this one: GbaConsole::RunFrame is itself
    // `while (frameCount == newCount) _cpu->Exec()`, inside the vendored core, where nothing we write
    // can reach it.
    //
    // So this guards the case it can: the PPU advancing without the APU producing enough samples. It is
    // not expected to fire either - GbaApu::InternalRun appends a sample per tick OUTSIDE its
    // enabled-check, so even a silent ROM yields ~738 frames per call. (Which also corrects the premise
    // this bound was requested under: "a ROM that stops producing audio would spin it forever" is true
    // of a bare-Exec loop and not of this one.) If it does fire the ring comes up short and finishBlock
    // emits silence for the remainder, not a stale echo. Nothing is logged: this is the audio thread.
    const double  framesPerVideoFrame = (sampleRate_ > 0.0 ? sampleRate_ : 44100.0) / 59.7275;
    std::uint32_t budget = static_cast<std::uint32_t>(double(framesNeeded) / framesPerVideoFrame) + 4;
    while (audioDevice_->availableFrames() < framesNeeded && budget-- > 0) {
        console->RunFrame();
    }
    return false;
}

std::uint32_t MesenGbaSystem::availableFrames() const {
    return audioDevice_ ? static_cast<std::uint32_t>(audioDevice_->availableFrames()) : 0;
}

void MesenGbaSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!activated_ || !emu_) return;

    assert(laneCount == 2); // mixed stereo only today (default single stereo stream)
    (void)laneCount;

    const std::uint32_t blockSize = info.frames;
    sumStereoWithGain(*audioDevice_, stereoAccum_, outs, blockSize, gainSmoother_);

    // Tear-free memory snapshots for any UI subscriptions. End-of-block =
    // internally consistent state because the CPU isn't mid-instruction.
    publishMemorySnapshots();
    publishStateSnapshot(info.frames, sampleRate_);
}

std::size_t MesenGbaSystem::stateSnapshotSize() const {
    // A ceiling, measured from an UNCOMPRESSED serialize — see mesenStateCeiling. The triple is
    // allocated once from this and never resized, so it must cover the largest capture this core will
    // ever produce, not the small one a just-booted (mostly-zero, highly compressible) machine yields.
    return emu_ ? mesenStateCeiling(*emu_) : 0;
}

bool MesenGbaSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    // Streaming SaveState path allocates internally — acceptable at the coarse
    // snapshot interval, not per audio block.
    dst = saveStateBytes();
    return !dst.empty();
}

rp::MemoryAccessor MesenGbaSystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!emu_) return rp::MemoryAccessor{};

    ::MemoryType native;
    switch (type) {
        case rp::MemoryType::Ram:        native = ::MemoryType::GbaIntWorkRam; break;
        case rp::MemoryType::Rom:        native = ::MemoryType::GbaPrgRom;     break;
        case rp::MemoryType::Sram:       native = ::MemoryType::GbaSaveRam;    break;
        case rp::MemoryType::Vram:       native = ::MemoryType::GbaVideoRam;   break;
        case rp::MemoryType::OAM:        native = ::MemoryType::GbaSpriteRam;  break;
        case rp::MemoryType::ExtWorkRam: native = ::MemoryType::GbaExtWorkRam; break;
        case rp::MemoryType::IORegisters:
        case rp::MemoryType::HRam:
        case rp::MemoryType::NametableRam:
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
GbaCpu* gbaCpu(Emulator* emu) {
    if (!emu) return nullptr;
    auto* console = dynamic_cast<GbaConsole*>(emu->GetConsole().get());
    return console ? console->GetCpu() : nullptr;
}

// Parse "r0".."r15" -> 0..15, else -1.
int parseArmReg(std::string_view name) {
    if (name.size() < 2 || name[0] != 'r') return -1;
    int idx = 0;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return -1;
        idx = idx * 10 + (name[i] - '0');
    }
    return (idx >= 0 && idx <= 15) ? idx : -1;
}
} // namespace

std::vector<rp::CpuRegister> MesenGbaSystem::getCpuRegisters() const {
    GbaCpu* cpu = gbaCpu(emu_.get());
    if (!cpu) return {};
    const GbaCpuState& s = cpu->GetState();
    std::vector<rp::CpuRegister> out;
    out.reserve(18);
    for (int i = 0; i < 16; ++i) {
        out.push_back({ "r" + std::to_string(i), s.R[i], 32 });
    }
    out.push_back({ "cpsr", const_cast<GbaCpuFlags&>(s.CPSR).ToInt32(), 32 });
    out.push_back({ "pc", s.R[15], 32 }); // alias of r15, per the "pc" contract
    return out;
}

bool MesenGbaSystem::setCpuRegister(std::string_view name, std::uint32_t value) {
    GbaCpu* cpu = gbaCpu(emu_.get());
    if (!cpu) return false;
    GbaCpuState& s = cpu->GetState();
    if (name == "pc" || name == "r15") {
        // PC writes must reload the pipeline; keep the current ARM/Thumb mode.
        cpu->SetProgramCounter(value, s.CPSR.Thumb);
        return true;
    }
    const int idx = parseArmReg(name);
    if (idx >= 0 && idx <= 14) {
        s.R[idx] = value;
        return true;
    }
    // cpsr write is deferred (no FromInt32 on GbaCpuFlags); unknown names fail.
    return false;
}

std::optional<std::uint32_t> MesenGbaSystem::getProgramCounter() const {
    GbaCpu* cpu = gbaCpu(emu_.get());
    if (!cpu) return std::nullopt;
    return cpu->GetProgramCounter();
}

std::optional<std::uint8_t> MesenGbaSystem::readCpuByte(std::uint32_t addr) const {
    if (!emu_) return std::nullopt;
    auto* console = dynamic_cast<GbaConsole*>(emu_->GetConsole().get());
    if (!console) return std::nullopt;
    // Banking-aware, side-effect-free read of the ARM7 address space.
    return console->GetMemoryManager()->DebugRead(addr);
}

std::uint64_t MesenGbaSystem::stepInstruction() {
    GbaCpu* cpu = gbaCpu(emu_.get());
    if (!cpu) return 0;
    // Exec() relies on Mesen's IsEmulationThread() check; bind it to the calling
    // thread (rebinds if that differs from the last driving thread).
    if (!emu_->IsEmulationThread()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
    }
    // One ARM/Thumb instruction, no debugger (the same Exec() RunFrame loops on
    // when IsDebugging() is false). Cycles via the CycleCount delta.
    const std::uint64_t before = cpu->GetState().CycleCount;
    cpu->Exec<false, false>();
    const std::uint64_t after = cpu->GetState().CycleCount;
    return after - before;
}

void MesenGbaSystem::setFastBoot(bool on) {
    if (config_.skipBootScreen == on) return;
    config_.skipBootScreen = on;
    if (emu_) configureGba(*emu_, on);
}

std::vector<std::uint8_t> MesenGbaSystem::saveSramBytes() const {
    if (!emu_) return {};
    auto* self = const_cast<MesenGbaSystem*>(this);
    auto accessor = self->getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    if (!accessor.valid() || accessor.size() == 0) return {};
    return std::vector<std::uint8_t>(accessor.data(),
                                     accessor.data() + accessor.size());
}

void MesenGbaSystem::clearSram() {
    if (!emu_) return;
    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    if (!accessor.valid() || accessor.size() == 0) return;
    std::memset(accessor.data(), 0, accessor.size());
    config_.sram.clear();
}

std::vector<std::uint8_t> MesenGbaSystem::saveStateBytes() const {
    if (!emu_) return {};
    std::stringstream ss(std::ios::out | std::ios::binary);
    emu_->GetSaveStateManager()->SaveState(ss);
    const std::string str = ss.str();
    return std::vector<std::uint8_t>(str.begin(), str.end());
}

bool MesenGbaSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    if (!emu_ || bytes.empty()) return false;
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    ss.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
    ss.seekg(0);
    return emu_->GetSaveStateManager()->LoadState(ss);
}

