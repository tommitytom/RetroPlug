#include "TabView.h"

#include "ui/Colors.h"

using namespace fw;

void TabView::onChildAdded(ViewPtr child) {
	_panels.push_back(child);
	_tabAreas.push_back(Rect());

	if (_panelIdx == -1) {
		_panelIdx = 0;
	}

	child->setSizingPolicy(SizingPolicy::None);
	child->setClip(true);

	updateLayout();
}

void TabView::onChildRemoved(ViewPtr child) {
	for (size_t i = 0; i < _panels.size(); ++i) {
		if (_panels[i] == child) {
			_panels.erase(_panels.begin() + i);
			_tabAreas.erase(_tabAreas.begin() + i);

			if (_panelIdx == (int32)i) {
				_panelIdx = std::min(_panelIdx, (int32)_panels.size() - 1);
			}

			updateLayout();

			break;
		}
	}
}

bool TabView::onMouseMove(Point pos) {
	_mouseOverHeader = _titleArea.contains(pos);
	_mouseOverTabIdx = -1;

	for (int32 i = 0; i < (int32)_tabAreas.size(); ++i) {
		if (_tabAreas[i].contains(pos)) {
			_mouseOverTabIdx = i;
		}
	}

	return _mouseOverHeader;
}

void TabView::setCurrentPanel(int32 panelIdx) {
	if (_panelIdx == panelIdx) {
		return;
	}

	if (_panelIdx != -1) {
		_panels[_panelIdx]->setVisible(false);
	}

	_panelIdx = panelIdx;

	if (_panelIdx != -1) {
		_panels[_panelIdx]->setVisible(true);
	}
}

bool TabView::onMouseButton(MouseButton::Enum button, bool down, Point position) {
	if (button == MouseButton::Left) {
		_draggingTabIdx = -1;

		if (down) {
			_dragStartPosition = position;

			for (int32 i = 0; i < (int32)_tabAreas.size(); ++i) {
				if (_tabAreas[i].contains(position)) {
					_draggingTabIdx = i;
					setCurrentPanel(i);
					return true;
				}
			}
		}
	}

	return false;
}

void TabView::onRender(Canvas& canvas) {
	canvas.fillRect(getDimensions(), RP_COLOR_BACKGROUND);

	if (_showHeader) {
		canvas.fillRect(_titleArea, RP_COLOR_FOREGROUND);
	}

	if (_panels.size() < 2) {
		std::string_view contentName = "NO CONTENT";

		if (_panels.size() == 1) {
			if (_panels[0]->getName().size()) {
				contentName = _panels[0]->getName();
			} else {
				contentName = "UNNAMED";
			}
		}

		canvas.setTextAlign(TextAlignFlags::Top | TextAlignFlags::Left);
		canvas.text(0, 0, contentName, RP_COLOR_WHITE);
	} else {
		const f32 tabWidth = 120;
		f32 tabOffset = 0;

		for (int32 i = 0; i < (int32)_panels.size(); ++i) {
			std::string_view tabName;

			if (_panels[i]->getName().size()) {
				tabName = _panels[i]->getName();
			} else {
				tabName = "UNNAMED";
			}

			if (_panelIdx == i) {
				canvas.fillRect(_tabAreas[i], RP_COLOR_HIGHLIGHT);
			}
			else if (_mouseOverTabIdx == i) {
				canvas.fillRect(_tabAreas[i], RP_COLOR_HIGHLIGHT2);
			}

			canvas.setTextAlign(TextAlignFlags::Top | TextAlignFlags::Left);
			canvas.text(tabOffset, 0, tabName, RP_COLOR_WHITE);
			tabOffset += tabWidth;
		}
	}
}

void TabView::updateLayout() {
	_titleArea.dimensions = { getDimensions().w, _titleAreaHeight };
	_panelArea = { 0, _titleAreaHeight, getDimensions().w, getDimensions().h - _titleAreaHeight };

	//_overlay->setArea(_panelArea);

	const int32 TAB_WIDTH = 120;
	int32 tabOffset = 0;

	for (int32 i = 0; i < (int32)_panels.size(); ++i) {
		_panels[i]->setVisible(i == _panelIdx);
		_panels[i]->setArea(_panelArea);
		_tabAreas[i] = Rect(tabOffset, 0, TAB_WIDTH, _titleAreaHeight);
		tabOffset += TAB_WIDTH;
	}
}
