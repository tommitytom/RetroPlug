#include "HexGrid.h"

#include <freetype-gl/texture-font.h>

#include "foundation/FsUtil.h"
#include "graphics/ftgl/FtglFont.h"

namespace rp {
	HexGrid::HexGrid() {
		getLayout().setMinDimensions({ 1280, 720 });
	}

	void HexGrid::setData(orb::Uint8Buffer&& data) {
		_data = std::move(data);
		_totalRowCount = (_data.size() + _bytesPerRow - 1) / _bytesPerRow;
		_visibleRowCount = 32;
	}

	void HexGrid::onInitialize() {
		setName("Hex Editor");
		setFocusPolicy(orb::FocusPolicy::Click);
		_font = getFontManager().loadFont("Vera.ttf", 15.0f);
		//orb::FsUtil::readFile("C:\\Users\\Tom\\Downloads\\KIT.kit", _data);
	}

	char formatChar(uint8 val) {
		if (val == 0) {
			return '.';
		}

		return (char)val;
	}

	void HexGrid::onRender(orb::Canvas& canvas) {
		canvas.setFont(_font);

		std::string row[2];
		row[0].reserve(12 + _bytesPerRow * 5);
		row[1].reserve(12 + _bytesPerRow * 5);

		orb::FtglFontFace& font = _font.getResourceAs<orb::FtglFontFace>();
		ftgl::texture_font_t* textureFont = font.getTextureFont();
		orb::DimensionF tileDim = canvas.measureText(" ");
		tileDim.h = textureFont->height + 4;

		canvas.setTextAlign(orb::TextAlignFlags::Left | orb::TextAlignFlags::Top);
		canvas.fillRect(getDimensionsF(), orb::Color4F::black);

		row[0] += "Offset (h)  ";
		for (size_t i = 0; i < _bytesPerRow; ++i) row[0] += std::format("{:02X} ", i);
		row[0] += " Decoded text";

		canvas.text(tileDim.h, tileDim.h, row[0], orb::Color4F::green);

		const uint8* buffer = _data.data();

		const size_t startRow = _rowOffset;
		const size_t endRow = std::min(_rowOffset + _visibleRowCount, _totalRowCount);

		for (size_t i = startRow; i < endRow; ++i) {
			f32 rowPos = tileDim.h * (i - startRow + 3);
			if (rowPos > getDimensionsF().h) break;

			row[0].clear();
			row[1].clear();
			row[1] += "   ";

			const size_t offset = i * _bytesPerRow;

			canvas.text(tileDim.h, rowPos, std::format("{:010X}  ", offset), orb::Color4F::green);

			for (size_t j = 0; j < _bytesPerRow; ++j) row[j % 2] += std::format("{:02X}    ", buffer[offset + j]);
			row[0] += " ";
			for (size_t j = 0; j < _bytesPerRow; ++j) row[0] += formatChar(buffer[offset + j]);

			canvas.text(tileDim.h + tileDim.w * 11, rowPos, row[0], orb::Color4F::white);
			canvas.text(tileDim.h + tileDim.w * 11, rowPos, row[1], orb::Color4F::lightGrey);
		}
	}

	bool HexGrid::onMouseScroll(const orb::MouseScrollEvent& ev) {
		if (ev.delta.y > 0) {
			if (_rowOffset > 0) {
				_rowOffset--;
				emit(HexScrollEvent{ (f32)_rowOffset / (f32)_totalRowCount });
				return true;
			}
		} else if (ev.delta.y < 0) {
			if (_rowOffset + _visibleRowCount < _totalRowCount) {
				_rowOffset++;
				emit(HexScrollEvent{ (f32)_rowOffset / (f32)_totalRowCount });
				return true;
			}
		}

		return true;
	}
}
