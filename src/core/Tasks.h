#pragma once

#include <entt/entity/registry.hpp>

#include "ecs/RetroPlugComponents.h"
#include "ecs/TaskBase.h"

namespace rp {
	class SampleCache;

	struct LoadSystemTask : public TaskBase {
		entt::id_type systemType;
		entt::registry registry;
		entt::entity entity = entt::null;


		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};

	struct LoadProjectTask : public TaskBase {
		std::vector<std::filesystem::path> paths;
		entt::registry registry;


		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};

	class PatchKitTask : public TaskBase {
	private:
		entt::entity _system;
		LsdjKitComponent _kitState;
		SampleCache& _sampleCache;

		fw::Uint8Buffer _kitData;

	public:
		PatchKitTask(entt::entity system, const LsdjKitComponent& kit, fw::Uint8Buffer&& kitData, SampleCache& sampleCache)
			: _system(system), _kitState(kit), _kitData(std::move(kitData)), _sampleCache(sampleCache) {}
		~PatchKitTask() = default;

		void ExecuteRange(enki::TaskSetPartition range, uint32 threadnum) override;

		void finalize(entt::registry& targetRegistry) override;
	};
}
