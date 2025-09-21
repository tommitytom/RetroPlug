#include "HexEditor.h"

#include <freetype-gl/texture-font.h>

#include "foundation/FsUtil.h"
#include "graphics/ftgl/FtglFont.h"

namespace rp {
	HexEditor::HexEditor() {
		auto& layout = getLayout();
		layout.setMinDimensions({ 1280, 720 });
		layout.setFlexDirection(fw::FlexDirection::Row);
	}

	void HexEditor::onInitialize() {
		_hexGrid = addChild<HexGrid>("Hex Grid");
		_scrollBar = addChild<ScrollBar>("Scroll Bar");
		_scrollBar->getLayout().setDimensions(fw::Dimension{ 20, 720 });

		if (_pendingData.size() > 0) {
			_hexGrid->setData(std::move(_pendingData));
			_pendingData = fw::Uint8Buffer();
		}

		_scrollBar->setSize(_hexGrid->getViewablePercent());

		subscribe<HexScrollEvent>(_hexGrid, [&](const HexScrollEvent& ev) {
			_scrollBar->setPosition(ev.position);
		});

		subscribe<ScrollBarEvent>(_scrollBar, [&](const ScrollBarEvent& ev) {
			_hexGrid->setScrollPosition(ev.position);
		});
	}

	void HexEditor::setData(fw::Uint8Buffer&& data) {
		if (_hexGrid) {
			_hexGrid->setData(std::move(data));
		} else {
			_pendingData = std::move(data);
		}
	}

	void HexEditor::onRender(fw::Canvas& canvas) {
		canvas.fillRect(getDimensionsF(), fw::Color4F::red);
	}
}
