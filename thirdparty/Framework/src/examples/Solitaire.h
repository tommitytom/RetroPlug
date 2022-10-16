#pragma once

#include <array>
#include <unordered_set>

#include <entt/entity/registry.hpp>

#include "foundation/Types.h"
#include "foundation/Math.h"

#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "graphics/TextureAtlas.h"
#include "application/Window.h"

namespace fw {
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
		std::string uri;
		PointF pivot;
	};

	struct SpriteRenderComponent {
		TextureAtlasTile tile;
		RectF renderArea;
		PointF pivot;
		Color4F color = Color4F(1, 1, 1, 1);
	};

	struct TextComponent {
		
	};

	struct VisibleTag {};

	class Solitaire : public View {
	private:
		entt::registry _registry;
		DimensionF _windowSize;
		RectF _viewPort;

		entt::entity _rootEntity = entt::null;

		TextureHandle _cardsTex;
		TextureHandle _cardBackTex;
		TextureHandle _upTex;

		TextureAtlasHandle _cardAtlas;

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
		Solitaire() : View({ 1366, 768 }) { setType<Solitaire>(); }
		~Solitaire() = default;

		void onInitialize() override;

		void onUpdate(f32 delta) override;

		void onRender(engine::Canvas& canvas) override;

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override;

		bool onMouseMove(Point position) override;

		bool onMouseScroll(PointF delta, Point position) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

	private:
		void startGame();

		RectF calculateSpriteWorldRect(entt::registry& reg, entt::entity e);

		bool spriteContainsPoint(entt::registry& reg, entt::entity e, PointF point);

		void nextStock();

		void flipCard(entt::registry& reg, entt::entity e);

		void addSprite(entt::entity e, const std::string& uri);

		void addSprite(entt::entity e, const TextureHandle& texture);

		void updateSprite(entt::entity e, const std::string& uri);

		void updateSprite(entt::entity e, const TextureHandle& texture);
	};
}
