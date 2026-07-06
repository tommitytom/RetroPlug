#include "SystemFactory.hpp"

#include "system/SystemBase.hpp"  // complete type for unique_ptr<SystemBase> destruction

void SystemFactory::registerBackend(std::string kind, std::unique_ptr<SystemBackend> backend) {
    backends_[std::move(kind)] = std::move(backend);
}

std::unique_ptr<SystemBase> SystemFactory::build(SystemId id, const SystemBuildSpec& spec,
                                                 double sampleRate) const {
    const auto it = backends_.find(spec.backendKind);
    if (it == backends_.end()) return nullptr;  // unknown backend kind
    return it->second->build(id, spec, sampleRate);
}
