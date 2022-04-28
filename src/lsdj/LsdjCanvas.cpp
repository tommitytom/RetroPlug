#include "LsdjCanvas.h"

using namespace rp;
using namespace rp::lsdj;

Canvas::Canvas(Dimension<uint32> dimensions) : _renderTarget(dimensions), _dimensions(dimensions) {
	_tileBuffer = new Color4[TILE_BUFFER_SIZE];
}

Canvas::Canvas(Dimension<uint32> dimensions, const lsdj::Font& font, const lsdj::Palette& palette) {
	_tileBuffer = new Color4[TILE_BUFFER_SIZE];
	_font = font;
	_palette = palette;
}

Canvas::~Canvas() {
	delete[] _tileBuffer;
}

Color3 blendColors(Color3 color1, Color3 color2) {
	return Color3{
		.r = (uint8)((color1.r + color2.r) / 2),
		.g = (uint8)((color1.g + color2.g) / 2),
		.b = (uint8)((color1.b + color2.b) / 2)
	};
}

Color3 getPixelColor(uint32 pixel, Palette::ColorSet colorSet) {
	switch (pixel) {
	case 0:
		return colorSet.first;
	case 1:
		return blendColors(colorSet.first, colorSet.second);
	case 2:
		return colorSet.second;
	default:
		return colorSet.first;
	}
}

void Canvas::updateTileTexture() {
	uint32 xOffset = 0;
	uint32 yOffset = 0;

	memset(_tileBuffer, 0, TILE_BUFFER_SIZE * sizeof(Color4));

	for (uint32 colorSetIdx = 0; colorSetIdx < 10; ++colorSetIdx) {
		const Palette::ColorSet& colorSet = _palette.sets[colorSetIdx % 5];
		uint32 colorSetOffset = colorSetIdx * FONT_PIXEL_COUNT;

		for (uint32 fontTileIdx = 0; fontTileIdx < Font::TILE_COUNT; ++fontTileIdx) {
			const Font::Tile& tile = _font.tiles[fontTileIdx];
			uint32 fontTileOffset = fontTileIdx * FONT_TILE_PIXEL_COUNT;

			for (uint32 tileY = 0; tileY < Font::TILE_HEIGHT; ++tileY) {
				for (uint32 tileX = 0; tileX < Font::TILE_WIDTH; ++tileX) {
					uint32 pixelOffset = tileY * Font::TILE_WIDTH + tileX;
					uint32 tileBufferOffset = colorSetOffset + fontTileOffset + tileY * Font::TILE_WIDTH + tileX;
					uint8 paletteOffset = tile.pixels[pixelOffset];

					if (colorSetIdx > 5 && paletteOffset == 2) {
						paletteOffset = 1;
					}

					_tileBuffer[tileBufferOffset] = getPixelColor(colorSet, paletteOffset);
				}
			}
		}
	}

	//stbi_write_png("c:\\temp\\tilebuffer.png", image.w(), image.h(), 4, image.getData(), image.w() * 4);

	//nvgUpdateImage(_vg, _tileTexture, (const unsigned char*)image.getData());
}

/*void Canvas::drawDimmedTile(uint32 x, uint32 y, const Font::Tile& tile, Palette::ColorSet colorSet) {
	x += _translation.x;
	y += _translation.y;

	x *= TILE_WIDTH;
	y *= TILE_HEIGHT;

	if (x < _renderTarget.w() && y < _renderTarget.h()) {
		for (uint32 tileY = 0; tileY < Font::TILE_HEIGHT; ++tileY) {
			for (uint32 tileX = 0; tileX < Font::TILE_WIDTH; ++tileX) {
				uint32 pixelOffset = tileY * Font::TILE_WIDTH + tileX;

				uint8 paletteOffset = tile.pixels[pixelOffset];
				if (paletteOffset == 2) {
					paletteOffset = 1;
				}

				Color3 pixel = getPixelColor(tile.pixels[pixelOffset], colorSet);
				_renderTarget.setPixel(x + tileX, y + tileY, pixel);
			}
		}
	} else {
		spdlog::warn("failed to write tile");
	}
}*/

FontTiles findTile(char code) {
	switch (code) {
	case '.': return FontTiles::Period;
	case '-': return FontTiles::Dash;
	case '/': return FontTiles::Slash;
	}

	if (code >= 48 && code <= 57) {
		return (FontTiles)((uint32)FontTiles::Num0 + (code - 48));
	}

	if (code >= 65 && code <= 90) {
		return (FontTiles)((uint32)FontTiles::A + (code - 65));
	}

	return FontTiles::Space;
}

void formatNote2(uint8 note, FontTiles* target) {
	if (note == 0) {
		target[0] = FontTiles::Dash;
		target[1] = FontTiles::Dash;
		target[2] = FontTiles::Dash;
		return;
	}

	note -= 1;

	uint8 octave = (note / 12);
	uint8 notePos = note - (octave * 12);

	switch (notePos) {
	case 0: target[0] = FontTiles::C; break;
	case 1: target[0] = FontTiles::C; target[1] = FontTiles::Hash; break;
	case 2: target[0] = FontTiles::D; break;
	case 3: target[0] = FontTiles::D; target[1] = FontTiles::Hash; break;
	case 4: target[0] = FontTiles::E; break;
	case 5: target[0] = FontTiles::F; break;
	case 6: target[0] = FontTiles::F; target[1] = FontTiles::Hash; break;
	case 7: target[0] = FontTiles::G; break;
	case 8: target[0] = FontTiles::G; target[1] = FontTiles::Hash; break;
	case 9: target[0] = FontTiles::A; break;
	case 10: target[0] = FontTiles::A; target[1] = FontTiles::Hash; break;
	case 11: target[0] = FontTiles::B; break;
	}

	target[2] = findNumberTile(octave + 3);
}

void Canvas::fill(uint32 x, uint32 y, uint32 w, uint32 h, ColorSets colorSetIdx, uint32 paletteIdx) {
	x += _translation.x;
	y += _translation.y;

	x *= TILE_WIDTH;
	y *= TILE_HEIGHT;

	w *= TILE_WIDTH;
	h *= TILE_HEIGHT;

	if (x < _renderTarget.w() && y < _renderTarget.h()) {
		if (x + w > _renderTarget.w()) {
			w = _renderTarget.w() - x;
			//spdlog::warn("overflow");
		}

		if (y + h > _renderTarget.h()) {
			y = _renderTarget.h() - y;
			//spdlog::warn("overflow");
		}

		if (w > 0 && h > 0) {
			Color4 source = getPixelColor(colorSetIdx, paletteIdx);
			Color4* target = _renderTarget.getData() + y * _renderTarget.w() + x;
			size_t rowBytes = w * sizeof(Color4);

			for (uint32 tileX = 0; tileX < w; ++tileX) {
				target[tileX] = source;
			}

			for (uint32 tileY = 1; tileY < h; ++tileY) {
				uint32 targetOffset = tileY * _renderTarget.w();
				memcpy(target + targetOffset, target, rowBytes);
			}
		}
	}
}

Color4 Canvas::getPixelColor(const Palette::ColorSet& colorSet, uint32 pixel, uint8 alpha) {
	Color3 val;
	switch (pixel) {
	case 0:
		val = colorSet.first;
		break;
	case 1:
		val = blendColors(colorSet.first, colorSet.second);
		break;
	case 2:
		val = colorSet.second;
		break;
	default:
		val = colorSet.first;
		break;
	}

	return val;
}

Color4 Canvas::getPixelColor(ColorSets colorSetIdx, uint32 pixel, uint8 alpha) {
	return getPixelColor(_palette.sets[(uint32)colorSetIdx], pixel, alpha);
}

void Canvas::drawTile(uint32 x, uint32 y, FontTiles tileIdx, ColorSets colorSetIdx, bool dimmed) {
	x += _translation.x;
	y += _translation.y;

	x *= TILE_WIDTH;
	y *= TILE_HEIGHT;

	const uint32 w = _renderTarget.w();
	const uint32 h = _renderTarget.h();

	if (dimmed) {
		colorSetIdx = (ColorSets)((uint32)colorSetIdx + 5);
	}

	if (x < w && y < h) {
		Color4* source = _tileBuffer + ((uint32)colorSetIdx * FONT_PIXEL_COUNT) + ((uint32)tileIdx * FONT_TILE_PIXEL_COUNT);
		Color4* target = _renderTarget.getData() + y * w + x;

		for (uint32 tileY = 0; tileY < Font::TILE_HEIGHT; ++tileY) {
			memcpy(target, source, Font::TILE_WIDTH * sizeof(Color4));
			target += w;
			source += Font::TILE_WIDTH;
		}
	} else {
		spdlog::warn("Failed to write tile, out of range - x: {}, y: {} | w: {}, h: {}", x, y, w, h);
	}
}

/*void Canvas::drawDimmedTile(uint32 x, uint32 y, FontTiles tileIdx, ColorSets colorSetIdx) {
	const Font::Tile& tile = _font.tiles[(uint32)tileIdx];
	const Palette::ColorSet& colorSet = _palette.sets[(uint32)colorSetIdx];
	//drawDimmedTile(x, y, tile, colorSet);
}*/

void Canvas::text(uint32 x, uint32 y, std::string_view text, ColorSets colorSetIdx, bool dimmed) {
	for (uint32 i = 0; i < (uint32)text.size(); i++) {
		FontTiles tile = findTile(toupper(text[i]));
		drawTile(x + i, y, tile, colorSetIdx, dimmed);
	}
}

static const char HEX_LOOKUP_TABLE[513] =
"000102030405060708090A0B0C0D0E0F"
"101112131415161718191A1B1C1D1E1F"
"202122232425262728292A2B2C2D2E2F"
"303132333435363738393A3B3C3D3E3F"
"404142434445464748494A4B4C4D4E4F"
"505152535455565758595A5B5C5D5E5F"
"606162636465666768696A6B6C6D6E6F"
"707172737475767778797A7B7C7D7E7F"
"808182838485868788898A8B8C8D8E8F"
"909192939495969798999A9B9C9D9E9F"
"A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"
"B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
"C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF"
"D0D1D2D3D4D5D6D7D8D9DADBDCDDDEDF"
"E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF"
"F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";

void Canvas::hexNumber(uint32 x, uint32 y, uint8 value, ColorSets colorSetIdx, bool pad, bool dimmed) {
	int pos = (value & 0xFF) * 2;

	if (value >= 15 || pad) {
		char target[2];
		target[0] = HEX_LOOKUP_TABLE[pos];
		target[1] = HEX_LOOKUP_TABLE[pos + 1];

		text(x, y, std::string_view(target, 2), colorSetIdx, dimmed);
	} else {
		text(x, y, std::string_view(HEX_LOOKUP_TABLE + pos + 1, 1), colorSetIdx, dimmed);
	}
}

void Canvas::number(uint32 x, uint32 y, uint8 value, ColorSets colorSetIdx, bool pad, bool dimmed) {
	if (pad) {
		text(x, y, fmt::format("{:03}", value), colorSetIdx, dimmed);
	} else {
		text(x, y, fmt::format("{}", value), colorSetIdx, dimmed);
	}
}
