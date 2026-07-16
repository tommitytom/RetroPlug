#include "system/CoreBackends.hpp"

#include <memory>

#include "system/SystemFactory.hpp"
#include "system/mesen/MesenBackend.hpp"
#include "system/sameboy/SameBoyBackend.hpp"

void registerCoreBackends(SystemFactory& factory) {
    factory.registerBackend("sameboy", std::make_unique<SameBoyBackend>());
    factory.registerBackend("mesen", std::make_unique<MesenBackend>());
}
