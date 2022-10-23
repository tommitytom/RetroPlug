#pragma once

#include <stack>

#include "foundation/DataBuffer.h"
#include "foundation/Image.h"
#include "lsdj/Rom.h"
#include "lsdj/Sav.h"
#include "lsdj/Ram.h"

namespace rp::lsdj {
	const size_t PIXEL_WIDTH = 160;
	const size_t PIXEL_HEIGHT = 144;
	const size_t TILE_HEIGHT = 8;
	const size_t TILE_WIDTH = 8;
	const size_t COL_COUNT = PIXEL_WIDTH / TILE_WIDTH;
	const size_t ROW_COUNT = PIXEL_HEIGHT / TILE_HEIGHT;
	const uint32 FONT_TILE_PIXEL_COUNT = Font::TILE_WIDTH * Font::TILE_HEIGHT;
	const uint32 FONT_PIXEL_COUNT = Font::TILE_COUNT * FONT_TILE_PIXEL_COUNT;
	const uint32 TILE_BUFFER_SIZE = FONT_PIXEL_COUNT * 5 * 2; // Font size, 5 color sets, plus dimmed versions of those color sets

	enum class FontTiles {
		Note,
		ArrowRight,
		Space,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Dash,
		Hash,
		QuestionMark,
		ExclamationMark,
		Copyright,
		Special,
		Comma,
		Period,
		Colon,
		Equals,
		SawInv,
		Saw,
		PanL,
		PanR,
		OK,
		ArrowLeft,
		SineInv,
		Sine,
		Duty1,
		Duty2,
		Plus,
		BracketLeft,
		Duty3,
		Duty4,
		Duty5,
		Underscore,
		Duty6,
		Duty7,
		Percent,
		BracketRight,
		Semicolon,
		Slash
	};

	static FontTiles getCommandTile(lsdj_command_t command) {
		switch (command) {
		case LSDJ_COMMAND_NONE: return FontTiles::Dash;
		case LSDJ_COMMAND_A: return FontTiles::A;
		case LSDJ_COMMAND_C: return FontTiles::C;
		case LSDJ_COMMAND_D: return FontTiles::D;
		case LSDJ_COMMAND_E: return FontTiles::E;
		case LSDJ_COMMAND_F: return FontTiles::F;
		case LSDJ_COMMAND_G: return FontTiles::G;
		case LSDJ_COMMAND_H: return FontTiles::H;
		case LSDJ_COMMAND_K: return FontTiles::K;
		case LSDJ_COMMAND_L: return FontTiles::L;
		case LSDJ_COMMAND_M: return FontTiles::M;
		case LSDJ_COMMAND_O: return FontTiles::O;
		case LSDJ_COMMAND_P: return FontTiles::P;
		case LSDJ_COMMAND_R: return FontTiles::R;
		case LSDJ_COMMAND_S: return FontTiles::S;
		case LSDJ_COMMAND_T: return FontTiles::T;
		case LSDJ_COMMAND_V: return FontTiles::V;
		case LSDJ_COMMAND_W: return FontTiles::W;
		case LSDJ_COMMAND_Z: return FontTiles::Z;
		case LSDJ_COMMAND_ARDUINO_BOY_N: return FontTiles::N;
		case LSDJ_COMMAND_ARDUINO_BOY_X: return FontTiles::X;
		case LSDJ_COMMAND_ARDUINO_BOY_Q: return FontTiles::Q;
		case LSDJ_COMMAND_ARDUINO_BOY_Y: return FontTiles::Y;
		case LSDJ_COMMAND_B: return FontTiles::B;
		}

		return FontTiles::Dash;
	}

	static FontTiles findNumberTile(uint8 value) {
		return (FontTiles)((uint32)FontTiles::Num0 + value);
	}

	class Canvas {
	private:
		lsdj::Font _font;
		lsdj::Palette _palette;

		fw::Color4* _tileBuffer = nullptr;

		fw::Image _renderTarget;
		fw::DimensionT<uint32> _dimensions;
		fw::PointT<uint32> _translation = { 0, 0 };

		std::stack<fw::PointT<uint32>> _translationStack;

		int _textureHandle = -1;

	public:
		Canvas(fw::DimensionT<uint32> dimensions);
		Canvas(fw::DimensionT<uint32> dimensions, const lsdj::Font& font, const lsdj::Palette& palette);
		~Canvas();

		void translate(fw::PointT<uint32> translation) {
			_translationStack.push(_translation);
			_translation += translation;
		}

		void translate(uint32 x, uint32 y) {
			translate({ x, y });
		}

		void setTranslation(uint32 x, uint32 y) {
			assert(_translationStack.empty());
			_translation = { x, y };
		}

		void untranslate() {
			assert(!_translationStack.empty());
			_translation = _translationStack.top();
			_translationStack.pop();
		}

		void setPalette(lsdj::Palette palette) {
			_palette = palette;
			updateTileTexture();
		}

		void setFont(lsdj::Font font) {
			_font = font;
			updateTileTexture();
		}

		void fill(uint32 x, uint32 y, uint32 w, uint32 h, lsdj::ColorSets colorSetIdx, uint32 paletteIdx);

		void drawTile(uint32 x, uint32 y, lsdj::FontTiles tileIdx, lsdj::ColorSets colorSet, bool dimmed = false);

		void text(uint32 x, uint32 y, std::string_view text, lsdj::ColorSets colorSetIdx, bool dimmed = false);

		void hexNumber(uint32 x, uint32 y, uint8 value, lsdj::ColorSets colorSetIdx, bool pad = true, bool dimmed = false);

		void number(uint32 x, uint32 y, uint8 value, lsdj::ColorSets colorSetIdx, bool pad = true, bool dimmed = false);

		fw::Color4 getPixelColor(lsdj::ColorSets colorSetIdx, uint32 paletteIdx, uint8 alpha = 255);

		fw::Color4 getPixelColor(const lsdj::Palette::ColorSet& colorSet, uint32 pixel, uint8 alpha = 255);

		//void drawDimmedTile(uint32 x, uint32 y, const Font::Tile& tile, Palette::ColorSet colorSet);

		//void drawDimmedTile(uint32 x, uint32 y, FontTiles tileIdx, ColorSets colorSetIdx);

		fw::DimensionT<uint32> getDimensions() const {
			return _dimensions;
		}

		fw::Image& getRenderTarget() {
			return _renderTarget;
		}

		void clear() {
			_renderTarget.clear();
		}

	private:
		void updateTileTexture();
	};
}