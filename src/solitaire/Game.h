#pragma once

#include <array>

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "graphics/BgfxCanvas.h"

namespace rp {
	struct SpriteComponent {

	};

	enum class CardFace {
		Spade,
		Club,
		Heart,
		Diamond,
		COUNT
	};

	const uint32 CARDS_PER_FACE = 14; // 1-10, Jack, Queen, King, Ace
	const uint32 TOTAL_CARD_COUNT = CARDS_PER_FACE * 4;

	const uint32 COLUMN_COUNT = 7;
	const uint32 TOTAL_PER_COLUMN = TOTAL_CARD_COUNT;

	struct CardComponent {
		CardFace face;
		uint32 value;
	};

	class Game {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		rp::engine::BgfxCanvas _canvas;

		rp::engine::CanvasTextureHandle _upTex;

		bool _physicsDebug = true;

		PointF _lastMousePos;

		std::array<entt::entity, TOTAL_CARD_COUNT> _cards;
		std::array<entt::entity, COLUMN_COUNT * TOTAL_PER_COLUMN> _columns;

	public:
		Game() = default;
		~Game() = default;

		void init();

		void update(f32 delta);

		void render(Dimension res);

		void onMouseMove(f32 x, f32 y);

		void onMouseButton(int button, int action, int mods);
	};
}
