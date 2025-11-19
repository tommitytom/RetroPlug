#include "SameBoyAudioHooks.h"

#include "SameBoyComponents.h"
#include "SameBoyUtil.h"

namespace rp {
	SameBoyAudioHooks::SameBoyAudioHooks() : AudioSystemHook(entt::type_id<SameBoyComponent>().index()) {}

	void SameBoyAudioHooks::onSaveState(entt::registry& registry, entt::entity entity, fw::Uint8Buffer& target) const {
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		SameBoyUtil::saveState(*state.state, target);
	}

	MemoryAccessor SameBoyAudioHooks::onGetMemory(entt::registry& registry, entt::entity entity, MemoryType type, AccessType access) const {
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		return SameBoyUtil::getMemory(*state.state, type, access);
	}

	void SameBoyAudioHooks::onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const {
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		MemoryAccessor accessor = SameBoyUtil::getMemory(*state.state, patch.type, AccessType::Write);

		std::visit(entt::overloaded{
			[&](uint8 val) { accessor.set(patch.offset, val); },
			[&](uint16 val) { accessor.write(patch.offset, val); },
			[&](uint32 val) { accessor.write(patch.offset, val); },
			[&](const fw::Uint8Buffer& val) { accessor.write(patch.offset, val); },
			}, patch.data);
	}

	void SameBoyAudioHooks::onReset(entt::registry& registry, entt::entity entity) const {
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		SameBoyUtil::reset(*state.state);
	}
}
