#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "Shared/Emulator.h"
#include "Shared/SettingTypes.h"
#include "Shared/Video/VideoRenderer.h"
#include "Shared/Audio/SoundMixer.h"
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

    out.audio = std::make_shared<MesenAudioDevice>();
    out.emu->GetSoundMixer()->RegisterAudioDevice(out.audio.get());

    out.video = std::make_shared<MesenVideoDevice>();
    out.video->setFramebuffer(frames);
    out.emu->GetVideoRenderer()->RegisterRenderingDevice(out.video.get());

    return out;
}
