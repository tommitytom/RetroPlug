#pragma once

#include <entt/entity/registry.hpp>

#include "foundation/Math.h"

namespace fw {
	struct SpriteComponent {
		std::string uri;
		PointF pivot;
	};

	struct SpriteRenderComponent {
		TextureAtlasTile tile;
		RectF renderArea;
		PointF pivot;
		Color4F color = Color4F(1, 1, 1, 1);
	};
}
