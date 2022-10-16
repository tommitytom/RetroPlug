#pragma once

#include <entt/fwd.hpp>

#include "foundation/ResourceManager.h"

namespace fw {
	struct WorldSingleton {
		entt::entity rootEntity = entt::null;
		ResourceManager* resourceManager = nullptr;
	};
}
