#pragma once

#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"

namespace rp {
	class SameboyHooks: public SystemHook<SameBoyComponent> {
		void onLoadRequset(entt::registry& registry, const std::vector<std::string>& paths, SystemLoadComponent& load) const override;

		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;

		void onReset(entt::registry& registry, entt::entity entity, SameBoyComponent& system) const override;

		void onSerialize(const entt::registry& registry, entt::entity entity, ProjectSerializerContext& ctx) const override;

		void onDeserialize(entt::registry& registry, entt::entity entity, ProjectDeserializerContext& ctx) const override;
	};
}
