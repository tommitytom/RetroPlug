#include "system/mesen/MesenSmsSystem.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "system/InputTypes.hpp"
#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenGlobalInit.hpp"
#include "system/mesen/MesenVideoDevice.hpp"

#include "Core/SMS/Input/SmsController.h"
#include "Core/SMS/SmsConsole.h"
#include "Core/SMS/SmsControlManager.h"
#include "Core/SMS/SmsCpu.h"
#include "Core/SMS/SmsMemoryManager.h"
#include "Core/SMS/SmsPsg.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/CpuType.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/EventType.h"
#include "Core/Shared/MemoryType.h"
#include "Core/Shared/SaveStateManager.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Utilities/VirtualFile.h"

namespace {

float dbToLin(float dB) {
    return dB > -90.0f ? std::pow(10.0f, dB * 0.05f) : 0.0f;
}

constexpr const char* kMesenHomeFolder = "/tmp/retroplug-mesen";

// Coarse flush budget in Z80 T-states. ~256 output samples at 48 kHz NTSC.
// Bounded well under two independent limits: SmsPsg's own 20000-T auto-flush
// (so the cadence stays host-owned), and the ~149,000 T at which a single
// catch-up overruns blip_new(4000) - see the flush comment in
// stepIfBelowTarget.
constexpr std::uint64_t kCoarseCycles = 19088;

// Within this many samples of the block target, drop to flushing after EVERY
// instruction so the loop can stop close to the target instead of overshooting
// by a whole coarse window.
constexpr std::uint32_t kFineSamples = 64;

// Backstop for the audio thread. MesenGbaSystem's loop has no cap at all; a
// ROM that stops producing audio would spin it forever. ~8000 instructions
// covers a 512-frame block, so this is ~10x headroom.
constexpr std::uint64_t kInstructionBudget = 80000;

// Mesen's SmsConfig defaults are the .NET UI's problem, not the core's, so a
// headless host inherits several zero-values that are silently wrong. Every
// line here is load-bearing; see docs/sms-support.md section 2.6.
//
// MUST run before Emulator::LoadRom. Region resolution reads this config, and
// SmsConsole::UpdateRegion is private and only reachable from RunFrame - which
// the step loop below deliberately bypasses - so region is immutable for the
// life of the construct.
void configureSms(Emulator& emu, bool gameGear, bool enableFm) {
    EmuSettings* settings = emu.GetSettings();
    // ::SmsConfig here is Mesen's struct in SettingTypes.h; RetroPlug's
    // wrapping config is MesenSmsConfig (see MesenSmsConfig.hpp).
    SmsConfig cfg{};

    // Defaults to {0,0,0,0} and SmsPsg::Run multiplies every channel by
    // volumes[i]/100, so the stock config renders the PSG SILENT while still
    // producing a correct sample count. A boot smoke test passes; only an
    // amplitude assertion catches it.
    cfg.ChannelVolumes[0] = 100;
    cfg.ChannelVolumes[1] = 100;
    cfg.ChannelVolumes[2] = 100;
    cfg.ChannelVolumes[3] = 100;

    // The YM2413. Two costs when on, both real: SmsFmAudio is a separate
    // IAudioProvider that force-fits its OPLL stream into whatever the PSG
    // flush produced (so the cadence becomes an accuracy knob - see
    // stepIfBelowTarget), and Mesen models port $F2 as a MUX, so a ROM that
    // enables FM has its PSG output zeroed. See MesenSmsConfig::enableFm.
    cfg.EnableFmAudio  = enableFm;
    cfg.FmAudioVolume  = 100;

    // Defaults to None, which is worse than "no input": with no device
    // constructed, SmsControlManager::UpdateControlDevices' size() > 0 guard
    // never fires, so every frame takes a lock and allocates two shared_ptrs
    // ON THE AUDIO THREAD.
    cfg.Port1 = ControllerConfig{ .Type = ControllerType::SmsController };
    cfg.Port2 = ControllerConfig{ .Type = ControllerType::SmsController };

    // Defaults to Random, which noise-fills the unconditionally-allocated 32 KB
    // cart RAM. Two costs: the savestate stops compressing (measured 4.7x
    // larger), which feeds stateSnapshotSize() and every Duplicate; and two
    // runs of the same ROM diverge, so no byte-identity guard can be written.
    cfg.RamPowerOnState = RamState::AllZeros;

    // The VDP always emits 256x240 and BaseVideoFilter subtracts the overscan
    // below. Mesen defaults all three to {}, so without this SMS shows 24 black
    // rows top and bottom and Game Gear shows a 160x144 image floating in a
    // mostly-black 256x240 frame. Must agree with kSmsPixel* / kGgPixel*.
    cfg.NtscOverscan     = OverscanDimensions{ 0, 0, 24, 24 };   // -> 256x192
    cfg.PalOverscan      = OverscanDimensions{ 0, 0, 24, 24 };
    cfg.GameGearOverscan = OverscanDimensions{ 48, 48, 48, 48 }; // -> 160x144

    // Game Gear only, defaults true: blends each frame with the previous one to
    // emulate LCD ghosting. Any headless screenshot or deterministic render
    // would then depend on frame history.
    cfg.GgBlendFrames = false;
    (void)gameGear; // the overscan/blend fields are model-scoped by Mesen itself

    settings->SetSmsConfig(cfg);
}

// Mesen's native SmsController::Buttons ordering (SmsController.h:58) differs
// from the shared position-aligned wire bytes, so the remap is explicit.
// Returns -1 for a wire button the hardware does not have.
int toSmsButton(std::uint8_t wire) {
    switch (static_cast<SmsButton>(wire)) {
        case SmsButton::Right:  return SmsController::Buttons::Right;
        case SmsButton::Left:   return SmsController::Buttons::Left;
        case SmsButton::Up:     return SmsController::Buttons::Up;
        case SmsButton::Down:   return SmsController::Buttons::Down;
        case SmsButton::A:      return SmsController::Buttons::A;
        case SmsButton::B:      return SmsController::Buttons::B;
        // Pause is the console switch, not a pad button: it drives the Z80 NMI
        // on Master System and reads as Start on Game Gear.
        case SmsButton::Start:  return SmsController::Buttons::Pause;
        // No Select on either machine. Dropping it is deliberate - falling
        // through to a face button would make a Select tap fire button 2.
        case SmsButton::Select:
        default:                return -1;
    }
}

} // namespace

MesenSmsSystem::MesenSmsSystem(SystemId id,
                               MesenSmsConfig config,
                               std::vector<std::uint8_t> romBytes)
    : SystemBase(id),
      config_(std::move(config)),
      rom_(std::move(romBytes)),
      frames_(config_.gameGear ? kGgPixelWidth  : kSmsPixelWidth,
              config_.gameGear ? kGgPixelHeight : kSmsPixelHeight) {
    gainSmoother_.setTimeConstant(0.020f);
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
}

MesenSmsSystem::~MesenSmsSystem() {
    onDeactivate();
}

SmsConsole* MesenSmsSystem::smsConsole() const {
    if (!emu_) return nullptr;
    return dynamic_cast<SmsConsole*>(emu_->GetConsole().get());
}

// Materialise the ROM bytes to a real file with a real name, because Mesen
// derives three unrelated things from that one string:
//
//   1. The MACHINE. SmsConsole::GetSupportedSignatures() is empty, so the model
//      comes purely from the file EXTENSION (SmsConsole.cpp:46-59). Boot .gg
//      bytes under a ".sms" name and the screen is black.
//   2. The BATTERY FILE STEM (Emulator.cpp:591 -> SmsMemoryManager LoadBattery,
//      which runs unconditionally). A constant name would collide every SMS
//      title in the process onto one .sav.
//   3. The SOURCE Reset() RE-READS (Emulator.cpp:347). A name that is not on
//      disk makes Reset a SILENT NO-OP - the console pointer does not even
//      change.
//
// So: real stem for 1 and 2, real file for 3. The per-system subdirectory keeps
// two systems whose ROMs happen to share a stem from overwriting each other's
// staged bytes (which Reset would then reload as the wrong game), while leaving
// the filename - and therefore the battery stem - the honest one.
std::string MesenSmsSystem::stageRom() {
    namespace fs = std::filesystem;
    std::error_code ec;

    std::string stem = "rom";
    if (!config_.romPath.empty()) {
        const fs::path p(config_.romPath);
        if (!p.stem().empty()) stem = p.stem().string();
    }
    const char* ext = config_.gameGear ? ".gg" : ".sms";

    const fs::path dir = fs::path(kMesenHomeFolder) / "staged" / std::to_string(id());
    fs::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "[MesenSmsSystem] cannot create staging dir '%s': %s\n",
                     dir.string().c_str(), ec.message().c_str());
        return {};
    }

    const fs::path dst = dir / (stem + ext);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "[MesenSmsSystem] cannot write staged ROM '%s'\n", dst.string().c_str());
        return {};
    }
    out.write(reinterpret_cast<const char*>(rom_.data()),
              static_cast<std::streamsize>(rom_.size()));
    out.close();
    if (!out) {
        std::fprintf(stderr, "[MesenSmsSystem] short write staging ROM '%s'\n", dst.string().c_str());
        return {};
    }
    return dst.string();
}

void MesenSmsSystem::onActivate(double sampleRate) {
    if (activated_) return;
    if (rom_.empty()) {
        std::fprintf(stderr, "[MesenSmsSystem] no ROM bytes; not activating\n");
        return;
    }

    sampleRate_ = sampleRate;

    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();

    // Shared with the other Mesen systems. The home folder is per-process; set
    // it once, thread-safely, so concurrent core construction on background
    // render threads doesn't race (see MesenGlobalInit).
    mesenGlobalInit();

    stagedRomPath_ = stageRom();
    if (stagedRomPath_.empty()) return;

    emu_ = std::make_unique<Emulator>();
    // enableShortcuts=false: the plugin drives input/transport itself and never
    // uses Mesen's keyboard-shortcut layer. Disabling it avoids a per-instance
    // background polling thread (ShortcutKeyHandler) that, besides being pure
    // overhead, races the debugger pointer against LoadRom's ResetDebugger.
    emu_->Initialize(false);
    configureSms(*emu_, config_.gameGear, config_.enableFm);

    VirtualFile romFile(stagedRomPath_);

    // stopRom=false: keep Mesen from spawning its internal _emuThread. We drive
    // cpu->Exec() ourselves from the audio thread.
    if (!emu_->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
        std::fprintf(stderr, "[MesenSmsSystem] Mesen failed to load ROM '%s'\n",
                     stagedRomPath_.c_str());
        emu_.reset();
        return;
    }

    // Tell Mesen to render audio at the host sample rate. The PSG emits at a
    // fixed 96 kHz internally; SoundMixer resamples to this target.
    AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
    audioCfg.SampleRate = static_cast<std::uint32_t>(sampleRate);
    emu_->GetSettings()->SetAudioConfig(audioCfg);

    audioDevice_ = std::make_shared<MesenAudioDevice>();
    emu_->GetSoundMixer()->RegisterAudioDevice(audioDevice_.get());

    videoDevice_ = std::make_shared<MesenVideoDevice>();
    videoDevice_->setFramebuffer(&frames_);
    emu_->GetVideoRenderer()->RegisterRenderingDevice(videoDevice_.get());

    if (SmsConsole* console = smsConsole()) {
        masterRate_ = console->GetMasterClockRate();
        // Cache once: setExternalInput is on the sync path and must not
        // dynamic_cast per event on the audio thread.
        controlManager_ = dynamic_cast<SmsControlManager*>(console->GetControlManager());
        if (controlManager_) syncRole_.onAttach(*controlManager_);
    }

    // Restore persisted battery RAM / savestate. Write directly into the
    // SmsCartRam region rather than going through Mesen's BatteryManager.
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

void MesenSmsSystem::onDeactivate() {
    if (!activated_) return;

    // NOT a bare emu_.reset() like the NES/GBA systems do. SMS is the first
    // console RetroPlug hosts that registers an audio provider unconditionally:
    // SmsFmAudio registers with the SoundMixer in its constructor and
    // UNregisters in its destructor - but Emulator declares _console before
    // _soundMixer, members destruct in reverse order, and ~Emulator is empty,
    // so the mixer is already gone by the time SmsFmAudio reaches for it.
    // Measured ~3 in 5 runs of 40 construct/destruct cycles segfault without
    // this; 5 in 5 clean with it.
    //
    // preventRecentGameSave=true is load-bearing, not tidiness: Stop() would
    // otherwise call SaveStateManager::SaveRecentGame, and SmsPsg::Serialize
    // calls Run() - replaying a long un-flushed gap into blip in one go, which
    // is the same buffer overrun the step loop's unconditional flush exists to
    // prevent.
    //
    // ASan does NOT catch the underlying bug: both frames live in the
    // uninstrumented libmesen.a. The construct/destruct loop in
    // test/audio/SmsAudio.test.cpp is what guards it.
    if (emu_) emu_->Stop(/*sendNotification=*/false, /*preventRecentGameSave=*/true,
                         /*saveBattery=*/false);
    syncRole_.onDetach();        // before the manager it points at goes away
    emu_.reset();
    audioDevice_.reset();
    videoDevice_.reset();
    controlManager_ = nullptr;   // owned by the console, which emu_ just took down
    activated_ = false;

    if (!stagedRomPath_.empty()) {
        std::error_code ec;
        std::filesystem::remove(stagedRomPath_, ec);
        stagedRomPath_.clear();
    }
}

void MesenSmsSystem::onSampleRateChanged(double sampleRate) {
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate));
    if (emu_) {
        AudioConfig audioCfg = emu_->GetSettings()->GetAudioConfig();
        audioCfg.SampleRate = static_cast<std::uint32_t>(sampleRate);
        emu_->GetSettings()->SetAudioConfig(audioCfg);
    }
}

void MesenSmsSystem::onReset() {
    if (emu_) emu_->Reset();
}

void MesenSmsSystem::setGainDb(float dB) {
    config_.gainDb = dB;
    gainSmoother_.setTargetValue(dbToLin(dB));
}

void MesenSmsSystem::pressButton(std::uint8_t button, bool down) {
    pendingButtons_.push_back({ button, down });
}

void MesenSmsSystem::pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size,
                                   bool flush) {
    // Host-transport sync levels, sample-offset scheduled. Released in the step loop against the
    // Z80 cycle position, so the ROM's next IN sees them at their true sample instant.
    syncRole_.pushBytes(frame, data, size, flush);
}

void MesenSmsSystem::prepareForBlock(const AudioBlockInfo& /*info*/) {
    if (!activated_ || !emu_) return;

    // Bind Mesen's emulation thread to whoever drives this block, so its
    // internal IsEmulationThread() checks pass during cpu->Exec(). Rebinds when
    // the driving thread changes (boot on the main thread, then an offline
    // parallel render on a worker, then back) - cheap compare.
    if (!emu_->IsEmulationThread()) {
        emu_->SetEmulationThreadId(std::this_thread::get_id());
    }

    SmsConsole* console = smsConsole();
    if (!console) return;

    if (!pendingButtons_.empty()) {
        if (auto controller = console->GetControlManager()->GetControlDeviceByIndex(0)) {
            for (const auto& pb : pendingButtons_) {
                const int native = toSmsButton(pb.button);
                if (native >= 0) controller->SetBitValue(static_cast<uint8_t>(native), pb.down);
            }
        }
        pendingButtons_.clear();
    }

    emu_->ProcessEvent(EventType::InputPolled, CpuType::Sms);

    blockStartCycle_ = console->GetCpu()->GetCycleCount();
    blockCarry_      = static_cast<std::uint32_t>(audioDevice_->availableFrames());
    pendingCycles_   = 0;
}

std::uint32_t MesenSmsSystem::availableFrames() const {
    return audioDevice_ ? static_cast<std::uint32_t>(audioDevice_->availableFrames()) : 0;
}

void MesenSmsSystem::setExternalInput(std::uint8_t port, std::uint8_t levels) {
    if (controlManager_) controlManager_->SetExternalInput(port, levels);
}

std::uint32_t MesenSmsSystem::coreFrameWidth() const {
    return videoDevice_ ? videoDevice_->lastFrameWidth() : 0;
}

std::uint32_t MesenSmsSystem::coreFrameHeight() const {
    return videoDevice_ ? videoDevice_->lastFrameHeight() : 0;
}

std::optional<std::uint8_t> MesenSmsSystem::readCpuByte(std::uint32_t addr) const {
    SmsConsole* console = smsConsole();
    if (!console) return std::nullopt;
    SmsMemoryManager* mm = console->GetMemoryManager();
    if (!mm) return std::nullopt;
    // Banking-aware, side-effect-free read of the Z80 address space.
    return mm->DebugRead(static_cast<std::uint16_t>(addr));
}

bool MesenSmsSystem::writeCpuByte(std::uint32_t addr, std::uint8_t value) {
    SmsConsole* console = smsConsole();
    if (!console) return false;
    SmsMemoryManager* mm = console->GetMemoryManager();
    if (!mm) return false;
    // Debugger-style write into the Z80 address space, the twin of readCpuByte's
    // DebugRead. Unlike the NES's DebugWrite there is no side-effect flag to
    // pass - SmsMemoryManager::DebugWrite is already the no-side-effects path.
    mm->DebugWrite(static_cast<std::uint16_t>(addr), value);
    return true;
}

std::uint32_t MesenSmsSystem::intraBlockSamplePos() const {
    SmsConsole* console = smsConsole();
    if (!console || masterRate_ == 0) return blockCarry_;
    const std::uint64_t elapsed = console->GetCpu()->GetCycleCount() - blockStartCycle_;
    return blockCarry_ + static_cast<std::uint32_t>(
        (elapsed * static_cast<std::uint64_t>(sampleRate_)) / masterRate_);
}

bool MesenSmsSystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
    if (!activated_ || !emu_) return false;
    SmsConsole* console = smsConsole();
    if (!console) return false;
    SmsCpu* cpu = console->GetCpu();
    SmsPsg* psg = console->GetPsg();
    if (!cpu || !psg) return false;

    // Degenerate 1-member unit: run the whole block and report done (false),
    // matching the other Mesen systems.
    //
    // This does NOT call SmsConsole::RunFrame, and that is the entire point of
    // this backend. RunFrame advances a whole video frame (~800 samples at
    // 48 kHz), which is what MesenGbaSystem does and why GBA sync jitters by
    // 0..803 samples per block. Stepping one Z80 instruction at a time bounds
    // that to ~30 T-states, under half an output sample.
    //
    // Nothing in RunFrame is load-bearing here: it is an Exec() loop plus a
    // region update plus one PSG flush. End-of-frame, video delivery and the
    // input latch all hang off SmsVdp::ProcessEndOfScanline, reached from
    // SmsCpu::ExecCycles, so they still fire at their true emulated instants.
    // The one casualty is SmsConsole::UpdateRegion, which is private and
    // RunFrame-only - hence region being construct-time immutable
    // (see configureSms).
    // Cap the coarse flush window at one block. Without this, a block SMALLER
    // than the coarse window takes a single ~256-sample flush and sails past its
    // target: measured residue 131 at a 128-frame block and 172 at 199, i.e. the
    // ring ends up holding MORE than a whole block. The next block is then
    // satisfied entirely from the ring without stepping the CPU at all, and a
    // block that never enters this loop never pumps scheduled events - so a sync
    // event would be silently dropped rather than delivered late. 128 and 192
    // are ordinary DAW buffer sizes.
    //
    // Derived from framesNeeded (a per-block constant), NOT from the shrinking
    // distance to the target. That distinction is the whole point: a
    // remainder-derived budget is the "predictive tail", which forces a fine
    // flush near the target on EVERY block and is what degrades FM. This clamp
    // is inert for any block at or above the coarse window (>= 256 frames at
    // 48 kHz), so the common path stays bit-identical.
    const std::uint64_t coarseCycles = std::min<std::uint64_t>(
        kCoarseCycles,
        (static_cast<std::uint64_t>(framesNeeded) * masterRate_) /
            static_cast<std::uint64_t>(sampleRate_ > 0.0 ? sampleRate_ : 1.0));

    std::uint64_t budget = kInstructionBudget;
    while (audioDevice_->availableFrames() < framesNeeded && budget-- > 0) {
        // Release any sync level whose offset the emulated clock has reached, BEFORE the instruction
        // retires, so a level scheduled for sample N is on the port for the very next IN.
        //
        // Gated on intraBlockSamplePos() - the Z80 cycle counter - and deliberately NOT on
        // availableFrames(), which only moves when the PSG flushes and so lags by up to a whole
        // coarse window. Gating on the ring here would quantise delivery to the flush cadence and
        // throw away the entire reason this backend steps per instruction.
        syncRole_.pumpUntil(intraBlockSamplePos());

        const std::uint64_t before = cpu->GetCycleCount();
        cpu->Exec();                                  // one Z80 instruction (+ any IRQ/NMI tail)
        pendingCycles_ += cpu->GetCycleCount() - before;

        // Flush cadence: coarse normally, per-instruction once we are within
        // kFineSamples of the target.
        //
        // The flush is unconditional on the CYCLE BUDGET, never conditional on
        // what the ROM does. SmsPsg::Run only self-triggers on a PSG port
        // write, so a ROM that goes quiet produces zero samples forever under a
        // bare Exec() loop - and worse, once ~149,000 T accumulate unflushed,
        // the next catch-up overruns blip_new(4000) with the assert compiled
        // out under NDEBUG. SmsPsg::Serialize also calls Run(), and
        // publishStateSnapshot takes a savestate every block, so that overrun
        // is reachable from three unrelated call sites.
        //
        // Coarse-then-fine rather than a predictive tail, deliberately: with FM
        // enabled (smsggdj needs it) SmsFmAudio force-fits its OPLL stream into
        // whatever the PSG flush produced, so the cadence is an accuracy knob
        // and the predictive variant measures ~6x worse for FM. The cost is a
        // small ring overshoot instead of landing exactly on the target;
        // measured at 3 samples for every block size at or above the coarse
        // window, and 0 below it.
        //
        // That trade is safe ONLY because event delivery gates on
        // intraBlockSamplePos() - the Z80 cycle counter - and never on
        // availableFrames(). The overshoot moves buffer determinism, not sync
        // precision. Do not "fix" this back to a predictive tail without
        // re-measuring the FM error.
        const std::uint32_t have   = static_cast<std::uint32_t>(audioDevice_->availableFrames());
        const std::uint32_t remain = framesNeeded > have ? framesNeeded - have : 0;
        if (pendingCycles_ >= (remain <= kFineSamples ? 0 : coarseCycles)) {
            psg->Run();                 // catch up to GetMasterClock()
            psg->PlayQueuedAudio();     // blip_end_frame -> SoundMixer -> MesenAudioDevice
            pendingCycles_ = 0;
        }
    }

    // Release anything due through the block end (offsets in [0, framesNeeded]). The loop above can
    // exit with the ring already past the target having never reached the last offset, and a level
    // that misses its block is worse than one delivered late: the ROM reads a LEVEL, so a skipped
    // counter value is a skipped clock. Offsets past the block end carry over via finishBlock's
    // rebase.
    syncRole_.pumpUntil(framesNeeded);
    return false;
}

void MesenSmsSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!activated_ || !emu_) return;

    assert(laneCount == 2); // mixed stereo only today (default single stereo stream)
    (void)laneCount;

    const std::uint32_t blockSize = info.frames;

    // Carry any sync level that did not come due this block into the next one, shifting its offset
    // back by the block length so it keeps its relative timing (mirrors SameBoy and the NES FIFO).
    syncRole_.rebase(blockSize);
    if (stereoAccum_.size() < std::size_t(blockSize) * 2) {
        stereoAccum_.assign(std::size_t(blockSize) * 2, 0.0f);
    }
    audioDevice_->drain(stereoAccum_.data(), blockSize);

    // Sum interleaved stereo into the planar L/R outputs with smoothed gain.
    // ONE gainSmoother_.next() per sample frame across all lanes (not per
    // lane), matching the other systems so multi-system mixes are uniform.
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

std::size_t MesenSmsSystem::stateSnapshotSize() const {
    // Variable-size streamed savestate; size the triple once with headroom and
    // skip (don't realloc) any later capture that would exceed it.
    const std::size_t measured = saveStateBytes().size();
    if (measured == 0) return 0;
    return measured + measured / 2 + 8192;
}

bool MesenSmsSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    dst = saveStateBytes();
    return !dst.empty();
}

rp::MemoryAccessor MesenSmsSystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!emu_) return rp::MemoryAccessor{};

    ::MemoryType native;
    switch (type) {
        case rp::MemoryType::Ram:   native = ::MemoryType::SmsWorkRam;  break;
        case rp::MemoryType::Rom:   native = ::MemoryType::SmsPrgRom;   break;
        case rp::MemoryType::Sram:  native = ::MemoryType::SmsCartRam;  break;
        case rp::MemoryType::Vram:  native = ::MemoryType::SmsVideoRam; break;
        // SMS sprites live in VRAM (no separate OAM); the rest are GB/NES/GBA
        // only. Colour RAM and the boot ROM have no rp::MemoryType tag yet.
        case rp::MemoryType::OAM:
        case rp::MemoryType::IORegisters:
        case rp::MemoryType::HRam:
        case rp::MemoryType::NametableRam:
        case rp::MemoryType::ExtWorkRam:
        default: return rp::MemoryAccessor{};
    }

    ConsoleMemoryInfo info = emu_->GetMemory(native);
    if (!info.Memory || info.Size == 0) return rp::MemoryAccessor{};
    return rp::MemoryAccessor{type, access,
                              static_cast<std::uint8_t*>(info.Memory),
                              info.Size};
}

std::vector<std::uint8_t> MesenSmsSystem::saveSramBytes() const {
    if (!emu_) return {};
    auto* self = const_cast<MesenSmsSystem*>(this);
    auto accessor = self->getMemory(rp::MemoryType::Sram, rp::AccessType::Read);
    if (!accessor.valid() || accessor.size() == 0) return {};
    return std::vector<std::uint8_t>(accessor.data(), accessor.data() + accessor.size());
}

void MesenSmsSystem::clearSram() {
    if (!emu_) return;
    auto accessor = getMemory(rp::MemoryType::Sram, rp::AccessType::ReadWrite);
    if (!accessor.valid() || accessor.size() == 0) return;
    std::memset(accessor.data(), 0, accessor.size());
    config_.sram.clear();
}

std::vector<std::uint8_t> MesenSmsSystem::saveStateBytes() const {
    if (!emu_) return {};
    std::stringstream ss(std::ios::out | std::ios::binary);
    emu_->GetSaveStateManager()->SaveState(ss);
    const std::string str = ss.str();
    return std::vector<std::uint8_t>(str.begin(), str.end());
}

bool MesenSmsSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    if (!emu_ || bytes.empty()) return false;
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    ss.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
    ss.seekg(0);
    return emu_->GetSaveStateManager()->LoadState(ss);
}

std::unique_ptr<SystemBase> MesenSmsSystem::clone(SystemId newId, double sampleRate) const {
    MesenSmsConfig cfg = config_;
    cfg.savSuffix = 0;   // caller (duplicateSystem) assigns a non-colliding suffix
    cfg.savPath.clear(); // and its own sav file, not the source's paired one
    auto sramBytes = saveSramBytes();
    if (!sramBytes.empty()) cfg.sram = std::move(sramBytes);
    auto stateBytes = saveStateBytes();
    if (!stateBytes.empty()) cfg.savestate = std::move(stateBytes);
    std::vector<std::uint8_t> romCopy = rom_;
    auto out = std::make_unique<MesenSmsSystem>(newId, std::move(cfg), std::move(romCopy));
    out->onActivate(sampleRate);
    return out;
}
