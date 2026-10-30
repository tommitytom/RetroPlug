#include "system/SystemFactory.hpp"

#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase> destruction

void SystemFactory::registerBackend(std::string core, std::unique_ptr<SystemBackend> backend) {
    backends_[std::move(core)] = std::move(backend);
}

std::unique_ptr<SystemBase> SystemFactory::build(SystemId id, const SystemBuildRequest& request,
                                                 double sampleRate, std::string* why) const {
    // Which backend serves a request is the console its config names: one Mesen core builds three consoles, one
    // SameBoy core builds the Game Boy.
    const char* core = std::holds_alternative<SameBoyConfig>(request.config) ? "sameboy" : "mesen";
    const auto it = backends_.find(core);
    if (it == backends_.end()) {
        if (why) *why = std::string("the host has no ") + core + " core";
        return nullptr;
    }
    return it->second->build(id, request, sampleRate, why);
}
