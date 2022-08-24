#include "Solitaire.h"

#include <random>
#include <spdlog/spdlog.h>

#include "foundation/ResourceManager.h"
#include "engine/RelationshipComponent.h"
#include "engine/TransformComponent.h"
#include "engine/SceneGraphUtil.h"
#include "application/WindowManager.h"

using namespace fw;

struct FrontFacingTag {};
struct DropTargetTag {};
struct FoundationTag {};

const Dimension CARD_DIMENSIONS = Dimension(225, 315);
const f32 CARD_SPACING = 40;

const Color4F COLOR_WHITE = { 1, 1, 1, 1 };
const Color4F COLOR_MOUSE_OVER = { 1, 0.5f, 0.5f, 1 };
const Color4F COLOR_DRAGGING = { 0.5f, 1, 0.5f, 1 };
const Color4F COLOR_DRAG_OVER = { 0.5f, 0.5f, 1, 1 };

struct MouseState {
	bool mouseDown = false;
	entt::entity mouseOver = entt::null;
	entt::entity dragging = entt::null;
	entt::entity dragParent = entt::null;
	PointF dragParentOffset;
};

std::string_view getFaceName(CardFace face) {
	switch (face) {
	case CardFace::Club: return "Club";
	case CardFace::Diamond: return "Diamond";
	case CardFace::Heart: return "Heart";
	case CardFace::Spade: return "Spade";
	}

	return "UNKNOWN";
}

std::string_view getCardNumberName(uint32 idx) {
	switch (idx) {
	case 0: return "2";
	case 1: return "3";
	case 2: return "4";
	case 3: return "5";
	case 4: return "6";
	case 5: return "7";
	case 6: return "8";
	case 7: return "9";
	case 8: return "10";
	case 9: return "Jack";
	case 10: return "Queen";
	case 11: return "King";
	case 12: return "Ace";
	}

	return "UNKNOWN";
}

void shuffleCards(std::array<entt::entity, CARD_COUNT>& cards) {
	std::random_device rd;
	std::mt19937 g(rd());

	std::shuffle(cards.begin(), cards.end(), g);
}

entt::entity createEntity(entt::registry& reg, entt::entity parent, const TransformComponent& trans = TransformComponent()) {
	entt::entity e = reg.create();

	reg.emplace<TransformComponent>(e, trans);
	reg.emplace<WorldTransformComponent>(e);
	reg.emplace<RelationshipComponent>(e);
	reg.emplace<WorldAabbComponent>(e);

	if (parent != entt::null) {
		SceneGraphUtil::pushBack(reg, e, parent);
	}

	return e;
}

entt::entity createEntity(entt::registry& reg, const TransformComponent& trans = TransformComponent()) {
	return createEntity(reg, entt::null, trans);
}

entt::id_type getCardTileUriHash(const CardComponent& card) {
	std::string tileUri = fmt::format("cards/{}/{}", getFaceName((CardFace)card.face), getCardNumberName(card.value));
	return entt::hashed_string(tileUri.c_str(), tileUri.size());
}

std::string getCardTileUri(const CardComponent& card) {
	return fmt::format("cards/{}/{}", getFaceName((CardFace)card.face), getCardNumberName(card.value));
}

void setCardHighlight(entt::registry& reg, entt::entity e, const Color4F& color) {
	reg.get<SpriteRenderComponent>(e).color = color;
}

void Solitaire::onInitialize() {
	ResourceManager& rm = getResourceManager();
	//RectF cardPivotArea = RectF((f32)CARD_DIMENSIONS.w * -0.5, (f32)CARD_DIMENSIONS.h * -0.5, (f32)CARD_DIMENSIONS.w, (f32)CARD_DIMENSIONS.h);

	_cardsTex = rm.load<Texture>("cards.png");
	_cardBackTex = rm.load<Texture>("cardback.png");
	_upTex = rm.load<Texture>("up.png");

	TextureAtlasDesc atlasDesc = { .texture = _cardsTex };

	_registry.ctx().emplace<MouseState>();

	_rootEntity = createEntity(_registry, { .scale = { 0.5f, 0.5f } });

	// Generate card entities.  These will only be generated once for all games.

	for (uint32 cardFaceIdx = 0; cardFaceIdx < CARD_FACE_COUNT; ++cardFaceIdx) {
		CardFace face = (CardFace)cardFaceIdx;
		uint32 tileY = cardFaceIdx * CARD_DIMENSIONS.h;

		for (uint32 cardIdx = 0; cardIdx < CARDS_PER_FACE; ++cardIdx) {
			entt::entity card = createEntity(_registry);


			const CardComponent& cardComp = _registry.emplace<CardComponent>(card, CardComponent{
				.face = face,
				.value = (int32)cardIdx
			});

			std::string tileUri = getCardTileUri(cardComp);
			UriHash tileUriHash = ResourceUtil::hashUri(tileUri);
			Rect area;

			if (cardIdx == CARDS_PER_FACE - 1) {
				area = Rect(0, tileY, CARD_DIMENSIONS.w, CARD_DIMENSIONS.h);
			} else {
				area = Rect((cardIdx + 1) * CARD_DIMENSIONS.w, tileY, CARD_DIMENSIONS.w, CARD_DIMENSIONS.h);
			}

			atlasDesc.tiles.push_back({ tileUri, area });

			_cards[cardFaceIdx * CARDS_PER_FACE + cardIdx] = card;
		}
	}

	_cardAtlas = rm.create<TextureAtlas>("cards/atlas", atlasDesc);

	for (size_t i = 0; i < _cards.size(); ++i) {
		const CardComponent& card = _registry.get<CardComponent>(_cards[i]);
		std::string tileUri = getCardTileUri(card);
		addSprite(_cards[i], tileUri);
	}

	PointF offset(TABLEAU_SPACING, TABLEAU_SPACING);

	// Stock and waste
	_stock = createEntity(_registry, _rootEntity, { .position = offset });
	_waste = createEntity(_registry, _rootEntity, { .position = offset + PointF((f32)TABLEAU_SPACING, 0) });

	addSprite(_stock, _cardBackTex);

	/*_registry.emplace<SpriteRenderComponent>(_stock, SpriteRenderComponent{
		.texture = _cardBackTex,
		.renderArea = cardPivotArea,
		.color = { 1, 1, 1, 0.5f }
	});*/

	// Foundation
	for (uint32 i = 0; i < CARD_FACE_COUNT; ++i) {
		_foundation[i] = createEntity(_registry, _rootEntity, { .position = offset + PointF((f32)((i + 3) * TABLEAU_SPACING), 0) });

		const CardComponent& cardComp = _registry.emplace<CardComponent>(_foundation[i], CardComponent{
			.face = (CardFace)i,
			.value = CARDS_PER_FACE
		});

		addSprite(_foundation[i], _cardBackTex);

		/*_registry.emplace<SpriteRenderComponent>(_foundation[i], SpriteRenderComponent{
			.texture = _cardBackTex,
			.renderArea = cardPivotArea
		});*/
	}

	// Tableau
	for (uint32 i = 0; i < TABLEAU_COUNT; ++i) {
		_tableau[i] = createEntity(_registry, _rootEntity, { .position = offset + PointF((f32)(i * TABLEAU_SPACING), (f32)TABLEAU_SPACING + 100) });
	}

	startGame();
}

void Solitaire::startGame() {
	// Clear previous state

	for (size_t i = 0; i < _cards.size(); ++i) {
		SceneGraphUtil::remove(_registry, _cards[i]);
		setCardHighlight(_registry, _cards[i], COLOR_WHITE);
	}

	_registry.clear<FrontFacingTag>();
	_registry.clear<DropTargetTag>();
	_registry.clear<FoundationTag>();

	for (uint32 i = 0; i < CARD_FACE_COUNT; ++i) {
		_registry.emplace<DropTargetTag>(_foundation[i]);
		_registry.emplace<FoundationTag>(_foundation[i]);
	}

	shuffleCards(_cards);

	// Deal cards

	size_t cardIdx = 0;

	for (uint32 i = 0; i < TABLEAU_COUNT; ++i) {
		entt::entity lastEntity = _tableau[i];

		for (uint32 j = 0; j < i + 1; ++j) {
			entt::entity e = _cards[cardIdx++];
			SceneGraphUtil::pushBack(_registry, e, lastEntity);

			TransformComponent& transform = _registry.get<TransformComponent>(e);
			CardComponent& card = _registry.get<CardComponent>(e);
			SpriteRenderComponent& sprite = _registry.get<SpriteRenderComponent>(e);

			transform.position = PointF(0, CARD_SPACING);

			if (j == i) {
				updateSprite(e, getCardTileUri(card));
				_registry.emplace<FrontFacingTag>(e);
				_registry.emplace<DropTargetTag>(e);
			} else {
				updateSprite(e, _cardBackTex);
			}

			lastEntity = e;
		}
	}

	while (cardIdx < _cards.size()) {
		entt::entity e = _cards[cardIdx++];

		SceneGraphUtil::pushBack(_registry, e, _stock);
		_registry.get<TransformComponent>(e).position = { 0, 0 };
		updateSprite(e, _cardBackTex);
	}
}

void Solitaire::onUpdate(f32 delta) {
	_registry.sort<DropTargetTag>([&](const entt::entity lhs, const entt::entity rhs) {
		const auto& clhs = _registry.get<RelationshipComponent>(lhs);
		const auto& crhs = _registry.get<RelationshipComponent>(rhs);
		return crhs.parent == lhs || clhs.next == rhs
			|| (!(clhs.parent == rhs || crhs.next == lhs) && (clhs.parent < crhs.parent || (clhs.parent == crhs.parent && &clhs < &crhs)));
	});

	_registry.sort<FrontFacingTag>([&](const entt::entity lhs, const entt::entity rhs) {
		const auto& clhs = _registry.get<RelationshipComponent>(lhs);
		const auto& crhs = _registry.get<RelationshipComponent>(rhs);
		return crhs.parent == lhs || clhs.next == rhs
			|| (!(clhs.parent == rhs || crhs.next == lhs) && (clhs.parent < crhs.parent || (clhs.parent == crhs.parent && &clhs < &crhs)));
	});

	// Update
	SceneGraphUtil::updateWorldTransforms(_registry, _rootEntity);
}

void Solitaire::onRender(engine::Canvas& canvas) {
	SceneGraphUtil::eachRecursive(_registry, _rootEntity, [&](entt::entity e) {
		const SpriteRenderComponent* sprite = _registry.try_get<SpriteRenderComponent>(e);

		if (sprite) {
			const WorldTransformComponent& trans = _registry.get<WorldTransformComponent>(e);
			canvas.setTransform(trans.transform);			
			canvas.texture(sprite->tile.handle, sprite->renderArea, sprite->tile.uvArea, sprite->color);
		}
	});
}

bool isDragValid(entt::registry& reg, entt::entity source, entt::entity target) {
	if (source == entt::null || target == entt::null) {
		return false;
	}

	if (!reg.all_of<DropTargetTag>(target)) {
		return false;
	}

	const CardComponent& sourceComp = reg.get<CardComponent>(source);
	const CardComponent& targetComp = reg.get<CardComponent>(target);

	if (reg.all_of<FoundationTag>(target)) {
		if (sourceComp.face != targetComp.face) {
			return false;
		}
	} else {
		assert(reg.all_of<FrontFacingTag>(source));

		bool targetIsRed = targetComp.face == CardFace::Diamond || targetComp.face == CardFace::Heart;
		bool sourceIsRed = sourceComp.face == CardFace::Diamond || sourceComp.face == CardFace::Heart;

		if (sourceIsRed == targetIsRed) {
			return false;
		}
	}

	return sourceComp.value == targetComp.value - 1;
}

bool Solitaire::onMouseMove(Point position) {
	_lastMousePos = (PointF)position;

	entt::registry& reg = _registry;
	MouseState& mouseState = reg.ctx().at<MouseState>();

	if (mouseState.mouseOver != entt::null) {
		setCardHighlight(reg, mouseState.mouseOver, COLOR_WHITE);
	}

	mouseState.mouseOver = entt::null;

	if (mouseState.mouseDown) {
		if (mouseState.dragging != entt::null) {
			// Make the card follow the mouse pointer

			assert(SceneGraphUtil::hasParent(reg, mouseState.dragging));

			entt::entity parent = SceneGraphUtil::getParent(reg, mouseState.dragging);
			PointF parentPos = reg.get<WorldTransformComponent>(parent).transform.getTranslation();

			PointF scale = reg.get<TransformComponent>(_rootEntity).scale;
			scale.x = 1.0f / scale.x;
			scale.y = 1.0f / scale.y;

			// Mouse click pos relative to the transform
			PointF dist = (_lastMousePos - parentPos) * scale;

			reg.get<TransformComponent>(mouseState.dragging).position = dist;

			for (const auto& [e] : reg.view<DropTargetTag>().each()) {
				if (e == _foundation[0] && spriteContainsPoint(reg, e, _lastMousePos)) {
					spdlog::info("");
				}

				if (spriteContainsPoint(reg, e, _lastMousePos) && isDragValid(reg, mouseState.dragging, e)) {
					setCardHighlight(reg, e, COLOR_DRAG_OVER);
					mouseState.mouseOver = e;
				}
			}
		}
	} else {
		for (const auto& [e] : reg.view<FrontFacingTag>().each()) {
			if (spriteContainsPoint(reg, e, _lastMousePos)) {
				setCardHighlight(reg, e, COLOR_MOUSE_OVER);
				mouseState.mouseOver = e;
			}
		}
	}

	return true;
}

void Solitaire::nextStock() {
	entt::entity last = SceneGraphUtil::back(_registry, _stock);

	if (last != entt::null) {
		SceneGraphUtil::remove(_registry, last);
		SceneGraphUtil::pushBack(_registry, last, _waste);

		flipCard(_registry, last);
	}
}

void Solitaire::flipCard(entt::registry& reg, entt::entity e) {
	if (reg.all_of<FrontFacingTag>(e)) {
		reg.remove<FrontFacingTag>(e);
		updateSprite(e, _cardBackTex);
	} else {
		reg.emplace<FrontFacingTag>(e);
		updateSprite(e, getCardTileUri(reg.get<CardComponent>(e)));
	}
}

void Solitaire::addSprite(entt::entity e, const TextureHandle& texture) {
	DimensionF dimensions = (DimensionF)texture.getDesc().dimensions;

	_registry.emplace_or_replace<SpriteComponent>(e, SpriteComponent{ .uri = texture.getUri() });

	_registry.emplace_or_replace<SpriteRenderComponent>(e, SpriteRenderComponent{
		.tile = {
			.handle = texture,
			.dimensions = dimensions,
			.name = texture.getUri()
		},
		.renderArea = dimensions
	});
}

void Solitaire::addSprite(entt::entity e, const std::string& uri) {
	UriHash tileHash = ResourceUtil::hashUri(uri);

	TextureAtlas& atlas = _cardAtlas.getResource();
	const TextureAtlasTile* tile = atlas.findTile(tileHash);

	if (tile) {
		_registry.emplace<SpriteComponent>(e, SpriteComponent{ .uri = uri });

		_registry.emplace<SpriteRenderComponent>(e, SpriteRenderComponent{
			.tile = *tile,
			.renderArea = tile->dimensions
		});
	}
}

void Solitaire::updateSprite(entt::entity e, const TextureHandle& texture) {
	DimensionF dimensions = (DimensionF)texture.getDesc().dimensions;

	_registry.emplace_or_replace<SpriteComponent>(e, SpriteComponent{ .uri = texture.getUri() });

	_registry.emplace_or_replace<SpriteRenderComponent>(e, SpriteRenderComponent{
		.tile = {
			.handle = texture,
			.dimensions = dimensions,
			.name = texture.getUri()
		},
		.renderArea = dimensions
	});
}

void Solitaire::updateSprite(entt::entity e, const std::string& uri) {
	UriHash tileHash = ResourceUtil::hashUri(uri);

	TextureAtlas& atlas = _cardAtlas.getResource();
	const TextureAtlasTile* tile = atlas.findTile(tileHash);

	if (tile) {
		_registry.emplace_or_replace<SpriteComponent>(e, SpriteComponent{ .uri = uri });

		_registry.emplace_or_replace<SpriteRenderComponent>(e, SpriteRenderComponent{
			.tile = *tile,
			.renderArea = tile->dimensions
		});
	}
}

bool Solitaire::onMouseButton(MouseButton::Enum button, bool down, Point position) {
	entt::registry& reg = _registry;
	MouseState& mouseState = reg.ctx().at<MouseState>();

	if (button == MouseButton::Right && down) {
		startGame();
	}

	if (button == MouseButton::Left) {
		if (down) {
			if (spriteContainsPoint(reg, _stock, _lastMousePos)) {
				nextStock();
				return true;
			}

			if (mouseState.mouseOver != entt::null) {
				// The card that the mouse is over will begin dragging

				setCardHighlight(reg, mouseState.mouseOver, COLOR_DRAGGING);

				mouseState.dragParent = SceneGraphUtil::getParent(reg, mouseState.mouseOver);
				mouseState.dragParentOffset = reg.get<TransformComponent>(mouseState.mouseOver).position;

				SceneGraphUtil::changeParent(reg, mouseState.mouseOver, _rootEntity);

				// Make sure we can't drag a card over itself or any of its children
				SceneGraphUtil::eachRecursive(reg, mouseState.mouseOver, [&](entt::entity e) {
					reg.remove<DropTargetTag>(e);
				}, true);
			}

			mouseState.dragging = mouseState.mouseOver;
			mouseState.mouseOver = entt::null;
			mouseState.mouseDown = true;
		} else {
			if (mouseState.dragging != entt::null) {
				if (isDragValid(reg, mouseState.dragging, mouseState.mouseOver)) {
					// A successful drop has happened

					if (reg.all_of<FoundationTag>(mouseState.mouseOver)) {
						CardFace face = reg.get<CardComponent>(mouseState.dragging).face;

						reg.remove<DropTargetTag>(mouseState.dragging);
						reg.emplace_or_replace<FoundationTag>(mouseState.dragging);

						SceneGraphUtil::remove(reg, mouseState.dragging);
						SceneGraphUtil::pushBack(reg, mouseState.dragging, _foundation[(uint32)face]);

						reg.get<TransformComponent>(mouseState.dragging).position = { 0, 0 };
					} else {
						if (mouseState.dragParent != entt::null && !reg.all_of<FrontFacingTag>(mouseState.dragParent) && reg.all_of<SpriteRenderComponent>(mouseState.dragParent)) {
							flipCard(reg, mouseState.dragParent);
						}

						reg.remove<FoundationTag>(mouseState.dragging);

						// I think this is already handled below...
						//reg.emplace_or_replace<DropTargetTag>(mouseState.dragging);

						SceneGraphUtil::remove(reg, mouseState.dragging);
						SceneGraphUtil::pushBack(reg, mouseState.dragging, mouseState.mouseOver);

						reg.get<TransformComponent>(mouseState.dragging).position = { 0, CARD_SPACING };
					}
				} else {
					SceneGraphUtil::remove(reg, mouseState.dragging);
					SceneGraphUtil::pushBack(reg, mouseState.dragging, mouseState.dragParent);
					reg.get<TransformComponent>(mouseState.dragging).position = mouseState.dragParentOffset;
				}

				setCardHighlight(reg, mouseState.dragging, COLOR_WHITE);

				SceneGraphUtil::eachRecursive(reg, mouseState.dragging, [&](entt::entity e) {
					if (SceneGraphUtil::countChildren(reg, e) == 0) {
						reg.emplace<DropTargetTag>(e);
					}
				}, true);
			}

			mouseState.mouseOver = mouseState.dragging;
			mouseState.dragging = entt::null;
			mouseState.dragParent = entt::null;
			mouseState.mouseDown = false;
		}
	}

	return true;
}

bool Solitaire::onMouseScroll(PointF delta, Point position) {
	if (delta.y > 0) {
		_zoom += 0.1f;
	} else {
		_zoom -= 0.1f;
	}

	return true;
}

bool Solitaire::onKey(VirtualKey::Enum key, bool down) {
	return true;
}

RectF Solitaire::calculateSpriteWorldRect(entt::registry& reg, entt::entity e) {
	assert(reg.all_of<SpriteRenderComponent>(e));

	const SpriteRenderComponent& sprite = reg.get<SpriteRenderComponent>(e);
	const Mat3x3& transform = reg.get<WorldTransformComponent>(e).transform;

	PointF topLeft = transform * sprite.renderArea.position;
	PointF bottomRight = transform * sprite.renderArea.bottomRight();

	return RectF(topLeft, { bottomRight.x - topLeft.x, bottomRight.y - topLeft.y });
}

bool Solitaire::spriteContainsPoint(entt::registry& reg, entt::entity e, PointF point) {
	return calculateSpriteWorldRect(reg, e).contains(point);
}
