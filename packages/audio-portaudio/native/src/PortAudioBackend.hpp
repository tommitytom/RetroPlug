#pragma once

// The PortAudio backend of the host's audio (docs/modular-graph-phase-f5b-design.md section 5.3). Its factory is what
// the manifest's audioBackend names (rp_portaudio_backend, called from a product's generated
// rp_product_audio_backends()), so a product reaches this without naming a class of it.

#include <memory>

#include "audio/AudioBackend.hpp"

namespace rp::audio {

/** A PortAudio backend: its host APIs as drivers, its devices by name, the log quietening at first use, and a stream
 *  whose pull is the device callback. PortAudio is initialised on the first call that needs it, never by linking it. */
std::unique_ptr<AudioBackend> rp_portaudio_backend();

}  // namespace rp::audio
