#pragma once

#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"
#include "core/RetroPlugComponents.h"

namespace rp {
	class LsdjHooks final : public SystemHook<SameBoyComponent> {
	public:
		void onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const override;

		void onBeforeLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;

		fw::ViewPtr onCreateOverlay(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const override;

		void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const override;

		void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const override;

		void onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const override;

		std::string onGetSystemName(const entt::registry& registry, entt::entity entity) const override;
	};
}
