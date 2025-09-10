#include "SameBoyHooks.h"

#include <chrono>

#include "foundation/Replicator.h"
#include "sameboy/SameBoyUtil.h"
#include "ecs/EcsProjectSerializer.h"

namespace rp {
	void SameboyHooks::onLoadRequest(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const {
		filterEntries(paths, entries, ".gb", "rom");
		filterEntries(paths, entries, ".gbc", "rom");
		filterEntries(paths, entries, ".sav", "sram");
		filterEntries(paths, entries, ".state", "state");
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

		SystemStateComponent& systemState = registry.get_or_emplace<SystemStateComponent>(entity);
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
