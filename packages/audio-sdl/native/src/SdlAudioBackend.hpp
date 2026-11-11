#pragma once

// The SDL2 backend of the host's audio (docs/modular-graph-phase-f5b-design.md section 5.3). Its factory is what the
// manifest's audioBackend names (rp_sdl_backend), so a product reaches this without naming a class of it.

#include <memory>

#include "audio/AudioBackend.hpp"

namespace rp::audio {

/** An SDL2 audio backend: SDL's audio drivers as drivers, one of them initialised at a time, float32 output with a pull
 *  callback at 2, 4, 6 or 8 channels. SDL's audio subsystem is initialised on the first call that needs it, never by
 *  linking it: the app's setupSdl initialises video and the game controller only. */
std::unique_ptr<AudioBackend> rp_sdl_backend();

}  // namespace rp::audio
