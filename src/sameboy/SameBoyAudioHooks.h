#pragma once

#include "core/SystemHook.h"

namespace rp {
	class SameBoyAudioHooks : public AudioSystemHook {
	public:
		SameBoyAudioHooks();

		void onSaveState(entt::registry& registry, entt::entity entity, fw::Uint8Buffer& target) const override;

		MemoryAccessor onGetMemory(entt::registry& registry, entt::entity entity, MemoryType type, AccessType access) const override;

		void onPatchMemory(entt::registry& registry, entt::entity entity, const MemoryPatch& patch) const override;

		void onReset(entt::registry& registry, entt::entity entity) const override;
	};
}
