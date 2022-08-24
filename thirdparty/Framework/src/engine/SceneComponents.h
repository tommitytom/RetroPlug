#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Math.h"

namespace fw::engine {
	struct Transform {
		PointF position = { 0, 0 };
		PointF scale = { 1, 1 };
		f32 rotation = 0;
	};

	struct WorldTransform {
		Mat3x3 transform;
	};

	struct Relationship {
		entt::entity first = entt::null;
		entt::entity last = entt::null;
		entt::entity prev = entt::null;
		entt::entity next = entt::null;
		entt::entity parent = entt::null;
	};
}
