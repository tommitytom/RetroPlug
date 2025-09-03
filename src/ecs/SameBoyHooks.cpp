#include "SameBoyHooks.h"

#include "foundation/Replicator.h"
#include "sameboy/SameBoyUtil.h"
#include "ecs/EcsProjectSerializer.h"

#include <chrono>

namespace rp {
	void SameboyHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		SameBoyStateComponent state{std::make_unique<SameBoyState>()};
		SameBoyUtil::setup(system, *state.state, 11050, load);
		if (system.fastBoot) {
			std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
			SameBoyUtil::spinMs(state.state->gb, 400);// Skip bootloader
			std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			spdlog::info("Fast boot completed in {} ms", duration);
		}

		fw::Replicator::emplaceRemote(registry, entity, std::move(state));
	}

	void SameboyHooks::onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const {
		ProjectSerializer::serializeComponent<SameBoyComponent>(registry, entity, ctx);
	}

	void SameboyHooks::onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const {
		if (ProjectSerializer::deserializeComponent<SameBoyComponent>(registry, entity, ctx)) {
			registry.emplace<SystemComponent>(entity, getType());
		}
	}
}
