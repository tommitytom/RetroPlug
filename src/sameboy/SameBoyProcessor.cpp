#include "SameBoyProcessor.h"

#include "sameboy/SameBoySystem.h"

namespace rp {
	void SameBoyProcessor::process(std::vector<SystemPtr>& systems, uint32 audioFrameCount) {
		std::vector<SameBoySystem*> systemPointers;

		for (size_t i = 0; i < systems.size(); ++i) {
			std::shared_ptr<SameBoySystem> system = std::static_pointer_cast<SameBoySystem>(systems[i]);
			assert(system->hasIo());

			system->beginProcess();
			systemPointers.push_back(system.get());
		}

		while (!systemPointers.empty()) [[likely]] {
			for (auto it = systemPointers.begin(); it != systemPointers.end();) [[likely]] {
				SameBoySystem* system = *it;

				if (!system->processTick(audioFrameCount)) {
					[[likely]]
					++it;
				} else {
					[[unlikely]]
					it = systemPointers.erase(it);
				}
			}
		}
	}
}
