#include "BackendFacade.hpp"

#include <memory>

#include "GbaBackend.hpp"
#include "NesBackend.hpp"
#include "SameBoyBackend.hpp"

BackendFacade::BackendFacade() {
    // The one build path: a backend per emulator kind, keyed by the factory key toBuildSpec maps the
    // TS SystemKind onto. (The Engine pre-reserves its Project so the audio thread's adopt/swap never
    // reallocates.)
    factory_.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
    factory_.registerBackend("mesen-nes", std::make_unique<NesBackend>());
    factory_.registerBackend("mesen-gba", std::make_unique<GbaBackend>());
}
