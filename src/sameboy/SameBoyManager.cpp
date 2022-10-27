#include "SameBoyManager.h"

#include <memory>
#include <iostream>
#include <entt/core/utility.hpp>

extern "C" {
#include <gb.h>
}

#include "util/GameboyUtil.h"

using namespace rp;

std::string SameBoyManager::getRomName(const fw::Uint8Buffer& romData) {
	return GameboyUtil::getRomName((const char*)romData.data());
}

void SameBoyManager::process(uint32 frameCount) {
	std::vector<SystemPtr>& baseSystems = getSystems();
	std::vector<SameBoySystem*> systems;

	for (size_t i = 0; i < baseSystems.size(); ++i) {
		std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(baseSystems[i]);

		if (system->getStream()) {
			system->acquireIo(system->getStream().get());
			systems.push_back(system.get());
		}
	}

	while (!systems.empty()) {
		for (auto it = systems.begin(); it != systems.end();) {
			SameBoySystem* system = *it;
			if (!system->processTick(frameCount)) {
				++it;
			} else {
				system->releaseIo();
				it = systems.erase(it);
			}
		}
	}
}
