#pragma once

#include "foundation/ClangClIntellisense.h"
#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"

namespace rp {
	class SameboyHooks: public SystemHook<SameBoyComponent> {
		void onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const override;

		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;

		void onReset(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const override;

		void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const override;

		void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const override;

		void onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const override;

		void onReplicate(entt::registry& registry) const override;
	};
}
