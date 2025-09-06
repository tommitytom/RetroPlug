#include "SameBoyHooks.h"

#include "foundation/Replicator.h"
#include "sameboy/SameBoyUtil.h"
#include "ecs/EcsProjectSerializer.h"

#include <chrono>

namespace rp {
	void SameboyHooks::onLoadRequset(entt::registry& registry, const std::vector<std::string>& paths, SystemLoadComponent& load) const {
		for (const std::string& path : paths) {
			if (path.ends_with(".gb") || path.ends_with(".gbc")) {
				load.entries["rom"].path = path;
			} else if (path.ends_with(".sav")) {
				load.entries["sram"].path = path;
			} else if (path.ends_with(".state")) {
				load.entries["state"].path = path;
			}
		}
	}

	void SameboyHooks::onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const {
		SameBoyStateComponent state;
		state.state.reset(new SameBoyState());

		if (!SameBoyUtil::setup(system, *state.state, 11050, load)) {
			spdlog::error("Failed to setup SameBoy instance");
			return;
		}

		if (system.fastBoot) {
			std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
			SameBoyUtil::spinMs(state.state->gb, 400);// Skip bootloader
			std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
			spdlog::info("Fast boot completed in {} ms", duration);
		}

		SystemStateComponent& systemState = registry.get<SystemStateComponent>(entity);
		systemState.stateOffsets = SameBoyUtil::getStateOffsets(*state.state);
		SameBoyUtil::saveState(*state.state, systemState.state);

		for (size_t i = 0; i < (size_t)MemoryType::MAX; i++) {
			const MemoryType type = (MemoryType)i;
			const MemoryAccessor accessor = SameBoyUtil::getMemory(*state.state, type, AccessType::Read);

			if (accessor.isValid()) {
				systemState.memory.push_back(VersionedMemory{
					.type = type,
					.data = accessor.getBuffer().clone(),
					.version = 1,
					.subscriberCount = 0
				});
			}
		}

		fw::Replicator::emplaceRemote(registry, entity, std::move(state));
	}

	void SameboyHooks::onReset(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const {

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
