#pragma once

#include <entt/entity/registry.hpp>

#include "core/RetroPlugComponents.h"
#include "core/TaskBase.h"
#include "lsdj/LsdjComponents.h"

namespace rp {
	class SampleCache;

	class PatchKitTask : public TaskBase {
	private:
		entt::entity _system;
		LsdjKitComponent _kitState;
		SampleCache& _sampleCache;

		orb::Uint8Buffer _kitData;

	public:
		PatchKitTask(entt::entity system, const LsdjKitComponent& kit, orb::Uint8Buffer&& kitData, SampleCache& sampleCache)
			: _system(system), _kitState(kit), _kitData(std::move(kitData)), _sampleCache(sampleCache) {
		}
		~PatchKitTask() = default;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};
}
