#include "BackendFacade.hpp"

#include <memory>

#include "SameBoyBackend.hpp"

BackendFacade::BackendFacade() {
    // The one build path. SameBoy-only for now; a Mesen backend registers here later. (The Engine
    // pre-reserves its Project so the audio thread's adopt/swap never reallocates.)
    factory_.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
}
