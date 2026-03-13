#pragma once

#include "foundation/ClangClIntellisense.h"
#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "mesen/MesenComponents.h"

namespace rp {
	class MesenHooks : public SystemHook<MesenComponent> {
		void onFilterEntries(entt::registry& registry, const PathVector& paths, NamedEntryVector& entries) const override;

		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, MesenComponent& system) const override;

		void onReset(entt::registry& registry, entt::entity entity, MesenComponent& system) const override;

		void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const override;

		void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const override;

		void onMoveComponents(entt::registry& sourceRegistry, entt::entity sourceEntity, entt::registry& targetRegistry, entt::entity targetEntity) const override;

		void onReplicate(entt::registry& registry) const override;
	};
}
