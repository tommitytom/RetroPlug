#include "SameBoyAudioHooks.h"

#include "SameBoyComponents.h"
#include "SameBoyUtil.h"
#include "core/AudioEffect.h"
#include "core/AudioSettingsContext.h"
#include "core/HierarchyUtil.h"

namespace rp {
	SameBoyAudioHooks::SameBoyAudioHooks() : AudioSystemHook(entt::type_id<SameBoyComponent>().index()) {}

	void SameBoyAudioHooks::onSaveState(entt::registry& registry, entt::entity entity, orb::Uint8Buffer& target) const {
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
			[&](const orb::Uint8Buffer& val) { accessor.write(patch.offset, val); },
		}, patch.data);
	}

	void SameBoyAudioHooks::onReset(entt::registry& registry, entt::entity entity) const {
		SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
		SameBoyUtil::reset(*state.state);
	}

	void SameBoyAudioHooks::onProcess(entt::registry& registry, orb::AudioBuffer& out, const orb::AudioBuffer& in) const {
		// These should be hooks in the same vein as the system hooks, but this is easier for now
		onCreate<SameBoyStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
			SameBoyState& state = *registry.get<SameBoyStateComponent>(entity).state;
			SameBoyUtil::setSampleRate(state, (uint32)settings.sampleRate);
			state.io = std::make_shared<SystemIo>();
		});

		onDestroy<SameBoyStateComponent>(registry, [](entt::registry& registry, entt::entity entity) {
			SameBoyStateComponent& state = registry.get<SameBoyStateComponent>(entity);
			state.state = nullptr;
			HierarchyUtil::destroyHierarchy(registry, entity, false);
			registry.remove<SameBoyStateComponent>(entity);
		});

		auto view = registry.view<SameBoyStateComponent>();
		if (!view.size()) {
			return;
		}

		std::array<SameBoyStateComponent*, 4> comps;

		uint32 i = 0;
		for (const auto& [e, s] : view.each()) {
			s.state->io = registry.get<SystemIoComponent>(e).io;
			comps[i++] = &s;
		}

		const AudioSettingsContext& settings = registry.ctx().at<AudioSettingsContext>();
		SameBoyUtil::process(comps.data(), view.size(), settings.blockSize);
	}
}
