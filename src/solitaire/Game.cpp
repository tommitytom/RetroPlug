#include "Game.h"

using namespace rp;

void shuffleCards(std::array<entt::entity, TOTAL_CARD_COUNT>& cards) {
	std::sort(cards);
}

void Game::init() {
	_upTex = _canvas.loadTexture("taco.png");

	// Generate card entities.  These will only be generated once for all games.

	for (uint32 cardFaceIdx = 0; cardFaceIdx < (uint32)CardFace::COUNT; ++cardFaceIdx) {
		CardFace face = (CardFace)cardFaceIdx;

		for (uint32 cardIdx = 0; cardIdx < CARDS_PER_FACE; ++cardIdx) {
			entt::entity card = _registry.create();
			_cards[cardFaceIdx * CARDS_PER_FACE + cardIdx] = card;

			_registry.emplace<CardComponent>(card, CardComponent {
				.face = face,
				.value = cardIdx
			});
		}
	}
}

void Game::update(f32 delta) {

}

void Game::render(Dimension res) {
	_canvas.beginRender(res, 1.0f);

	_canvas.fillRect({ 10, 10, 100, 100 }, { 0, 1, 0, 1.f });
	_canvas.fillRect({ 60, 60, 100, 100 }, { 1, 0, 0, 1.f });

	_canvas.text(100, 100, "Hello world!", Color4F(1, 1, 1, 1));

	_canvas.endRender();
}

void Game::onMouseMove(f32 x, f32 y) {
	_lastMousePos = { x, y };
}

void Game::onMouseButton(int button, int action, int mods) {

}
