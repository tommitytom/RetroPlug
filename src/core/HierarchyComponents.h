#pragma once

#include <entt/entity/entity.hpp>

namespace rp {
	struct HierarchyComponent {
		entt::entity first{ entt::null };
		entt::entity prev{ entt::null };
		entt::entity next{ entt::null };
		entt::entity parent{ entt::null };
	};
}
