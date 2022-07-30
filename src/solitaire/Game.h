#pragma once

#include <array>
#include <unordered_set>

#include <entt/entity/registry.hpp>

#include "platform/Types.h"
#include "RpMath.h"

#include "graphics/BgfxCanvas.h"
#include "graphics/BgfxTexture.h"
#include "Window.h"

namespace rp {
	enum class CardFace {
		Heart,
		Spade,
		Diamond,
		Club
	};

	enum class CardNumber {
		Two,
		Three,
		Four,
		Five,
		Six,
		Seven,
		Eight,
		Nine,
		Ten,
		Jack,
		Queen,
		King,
		Ace
	};

	const uint32 CARD_FACE_COUNT = 4;
	const uint32 CARDS_PER_FACE = 13; // 2-10, Jack, Queen, King, Ace
	const uint32 CARD_COUNT = CARDS_PER_FACE * 4;
	const uint32 TABLEAU_SPACING = 275;

	const uint32 TABLEAU_COUNT = 7;

	struct CardComponent {
		CardFace face;
		int32 value;
	};

	struct SpriteComponent {
		entt::id_type uriHash;
		PointF pivot;
	};

	struct SpriteRenderComponent {
		entt::id_type textureUriHash;
		rp::engine::Tile tile;
		RectF renderArea;
		PointF pivot;
		Color4F color = Color4F(1, 1, 1, 1);
	};

	struct TextComponent {
		
	};

	struct VisibleTag {};

	class Game : public rp::app::Window {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		entt::entity _rootEntity = entt::null;

		entt::resource<rp::engine::Texture> _cardsTex;
		entt::resource<rp::engine::Texture> _cardBackTex;
		entt::resource<rp::engine::Texture> _upTex;

		bool _physicsDebug = true;

		PointF _lastMousePos;

		std::array<entt::entity, CARD_COUNT> _cards = { entt::null };
		std::array<entt::entity, TABLEAU_COUNT> _tableau = { entt::null };
		std::array<entt::entity, CARD_FACE_COUNT> _foundation;
		entt::entity _stock;
		entt::entity _waste;

		uint32 _score = 0;
		uint32 _moves = 0;
		f64 _startTime = 0.0;

		f32 _zoom = 1.0f;

	public:
		Game() : Window("Solitaire", { 1366, 768 }) {}
		~Game() = default;

		void onInitialize() override;

		void onFrame(f32 delta) override;

		void onMouseButton(MouseButton::Enum button, bool down) override;

		void onMouseMove(rp::PointF position) override;

		void onMouseWheel(rp::PointF delta) override;

		void onKey(VirtualKey::Enum key, bool down) override;

	private:
		void startGame();

		void render(Dimension res);

		RectF calculateSpriteWorldRect(entt::registry& reg, entt::entity e);

		bool spriteContainsPoint(entt::registry& reg, entt::entity e, PointF point);

		void nextStock();
	};
}
