#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "Shared/Emulator.h"
#include "Shared/SettingTypes.h"
#include "Shared/Video/VideoRenderer.h"
#include "Shared/Video/VideoDecoder.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/BaseControlManager.h"
#include "Shared/Interfaces/IConsole.h"
#include "Utilities/VirtualFile.h"

#include "system/mesen/MesenAudioDevice.hpp"
#include "system/mesen/MesenGlobalInit.hpp"
#include "system/mesen/MesenVideoDevice.hpp"
#include "transport/FrameBufferTriple.hpp"

/** What a booted Mesen core hands back. Empty `emu` means the ROM did not load and the caller must not
 *  mark itself activated. */
struct MesenCore {
    std::unique_ptr<Emulator>         emu;
    std::shared_ptr<MesenAudioDevice> audio;
    std::shared_ptr<MesenVideoDevice> video;

    explicit operator bool() const { return emu != nullptr; }
};

/** Stand up an Emulator around `romFile` and wire our audio + video devices to it.
 *
 *  This was the tail of all three Mesen systems' onActivate, identical to the character apart from the
 *  log tag and which `configure` ran. Each line of it encodes something learned the hard way, which is
 *  the real argument for having one copy:
 *
 *    * enableShortcuts=false keeps Mesen from spawning a per-instance ShortcutKeyHandler polling
 *      thread, which besides being pure overhead races the debugger pointer against LoadRom's
 *      ResetDebugger.
 *    * stopRom=false keeps it from spawning its internal _emuThread; the host drives the CPU itself,
 *      from the audio thread.
 *    * The sample rate has to be pushed into AudioConfig after LoadRom, or the SoundMixer resamples to
 *      whatever Mesen defaulted to.
 *
 *  `configure` runs after Initialize and BEFORE LoadRom - which matters for more than settings: it is
 *  where GBA installs its per-instance BIOS folder override, and LoadRom is what reads the boot ROM. */
inline MesenCore bootMesenCore(const char*                           tag,
                               const VirtualFile&                    romFile,
                               const std::string&                    romPathForLog,
                               double                                sampleRate,
                               FrameBufferTriple*                    frames,
                               std::uint32_t                         maxBlockFrames,
                               const std::function<void(Emulator&)>& configure) {
    // Mesen's home folder + message options are process-global; set them once, thread-safely, so
    // concurrent core construction on background render threads doesn't race (see MesenGlobalInit).
    mesenGlobalInit();

    MesenCore out;
    out.emu = std::make_unique<Emulator>();
    out.emu->Initialize(false);
    configure(*out.emu);

    if (!out.emu->LoadRom(romFile, VirtualFile(), /*stopRom=*/false)) {
        std::fprintf(stderr, "[%s] Mesen failed to load ROM '%s'\n", tag, romPathForLog.c_str());
        out.emu.reset();
        return out;
    }

    AudioConfig audioCfg = out.emu->GetSettings()->GetAudioConfig();
    audioCfg.SampleRate  = static_cast<uint32_t>(sampleRate);
    out.emu->GetSettings()->SetAudioConfig(audioCfg);

    out.audio = std::make_shared<MesenAudioDevice>(
        maxBlockFrames ? maxBlockFrames : MesenAudioDevice::kDefaultBlockFrames);
    out.emu->GetSoundMixer()->RegisterAudioDevice(out.audio.get());

    out.video = std::make_shared<MesenVideoDevice>();
    out.video->setFramebuffer(frames);
    out.emu->GetVideoRenderer()->RegisterRenderingDevice(out.video.get());

    return out;
}

/** Warm Mesen's per-frame video and input plumbing, off the block thread, at activation.
 *
 *  The first emulated frame is the core's first block, and it is what fills four persistent RenderedFrame input
 *  snapshots (the PPU's own, the decoder's two and the renderer's last-frame copy), the control manager's port
 *  snapshot and the video filter's output buffer (about 240 KiB for a NES). All of that is storage: filling it here,
 *  with the states the control manager already holds and the size the console's PPU emits, leaves every frame and
 *  every piece of emulation state exactly as it was, and takes the allocations off the block thread. `ppu` is the
 *  console's PPU (or VDP); null skips its own frame, which only a console whose PPU has no persistent frame would do.
 */
template <typename Ppu>
void warmMesenFrames(Emulator& emu, Ppu* ppu, std::uint32_t width, std::uint32_t height) {
    const std::vector<ControllerData>& ports = emu.GetConsole()->GetControlManager()->GetPortStates();
    if (ppu != nullptr) ppu->WarmUpFrame(ports);
    emu.GetVideoDecoder()->WarmUp(width, height, ports);
}
