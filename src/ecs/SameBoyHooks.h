#pragma once

#include "core/CoreComponents.h"
#include "core/SystemHook.h"
#include "sameboy/SameBoyComponents.h"

namespace rp {
	class SameboyHooks: public SystemHook<SameBoyComponent> {
		void onLoad(entt::registry& registry, entt::entity entity, SystemLoadComponent& load, SameBoyComponent& system) const override;
	};
}
