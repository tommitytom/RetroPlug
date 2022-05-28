#include "DockPanel.h"

#include <nanovg.h>
#include <spdlog/spdlog.h>

using namespace rp;

class DockWindow;

void DockPanel::onDragEnter(DragContext& ctx, Point position) {
	if (!ctx.view || ctx.view->isType<DockPanel>()) {
		_dragOver = true;
		_dragOverIdx = dropTargetUnderCursor(position);
	}
}

bool DockPanel::onDragMove(DragContext& ctx, Point position) {
	if (!ctx.view || ctx.view->isType<DockPanel>()) {
		_dragOverIdx = dropTargetUnderCursor(position);
		return true;
	}
}

void DockPanel::onDragLeave(DragContext& ctx) {
	_dragOverIdx = DropTargetType::None;
	_dragOver = false;
}

bool DockPanel::onDrop(DragContext& ctx, Point position) {
	if (!ctx.view || !ctx.view->isType<DockWindow>()) {
		return false;
	}

	ViewPtr sourceWindow = ctx.view;
	DockPanelPtr sourceWindowPanel = ctx.view->asShared<DockPanel>();
	DropTargetType targetType = dropTargetUnderCursor(position);

	spdlog::info("Dropped on {}", getName());
	spdlog::info("Target window has {} children", getChildren().size());

	switch (targetType) {
	case DropTargetType::Center:
		_displayMode = DisplayMode::Tab;
		addChild(sourceWindowPanel);	
		break;
	case DropTargetType::Left:
		// Add splitter

		/*if (targetWindowContent) {
			// Target already has a panel in place

			if (targetWindowContent->isType<DockSplitter>()) {
				DockSplitterPtr splitter = targetWindowContent->asShared<DockSplitter>();

				if (splitter->getSplitDirection() == SplitDirection::Vertical) {
					splitter->addItem(sourceWindowContent, 0);
				}
			} else if (targetWindowContent->isType<DockPanel>()) {
				// Add current panel in to a splitter with the dropped item
				targetWindowContent->remove();

				assert(targetWindow->getChildren().size() == 1);

				std::shared_ptr<DockSplitter> splitter = targetWindow->addChild<DockSplitter>("Vertical Splitter");
				splitter->addItem(sourceWindowContent, 0);
				splitter->addItem(targetWindowContent, 0);

				// Overlay needs to be in front
				splitter->pushToBack();
			}
		} else {
			spdlog::info("No panel was found. Setting directly");

			if (sourceWindowContent) {
				//sourceWindowContent->remove();
				targetWindow->addChild(sourceWindowContent);
				sourceWindowContent->pushToBack();
			}
		}*/

		sourceWindow->remove();

		break;
	}

	//spdlog::info("drop catch in {}", targetIdx);
	_dragOverIdx = DropTargetType::None;
	_dragOver = false;

	return true;
}

void DockPanel::onRender() {
	drawRect(getDimensions(), nvgRGBA(100, 100, 100, 255));

	if (_showHeader) {
		drawRect(_titleArea, (_mouseOverHeader) ? nvgRGBA(190, 190, 190, 255) : nvgRGBA(150, 150, 150, 255));
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

		drawText(0, 0, contentName, nvgRGBA(255, 255, 255, 255));
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

			if (_mouseOverTabIdx == i) {
				drawRect(_tabAreas[i], nvgRGBA(190, 190, 190, 255));
			}

			drawText(tabOffset, 0, tabName, nvgRGBA(255, 255, 255, 255));
			tabOffset += tabWidth;
		}
	}
}

void DockPanel::updateLayout() {
	if (getArea().w < DROP_TARGET_DISTANCE * 2 || getArea().h < DROP_TARGET_DISTANCE * 2) {
		return;
	}

	arrangePanels();

	Point mid = { getArea().w / 2, getArea().h / 2 };
	int32 x = mid.x - DROP_TARGET_SIZE;
	int32 y = mid.y - DROP_TARGET_SIZE;
	int32 w = DROP_TARGET_SIZE;
	int32 h = DROP_TARGET_SIZE;

	_dropTargets[0] = { x, y, w, h }; // Center
	_dropTargets[1] = { x, y - DROP_TARGET_DISTANCE, w, h }; // Top
	_dropTargets[2] = { x + DROP_TARGET_DISTANCE, y, w, h }; // Right
	_dropTargets[3] = { x, y + DROP_TARGET_DISTANCE, w, h }; // Bottom
	_dropTargets[4] = { x - DROP_TARGET_DISTANCE, y, w, h }; // Left

	//_titleArea.dimensions = { getDimensions().w, _titleAreaHeight };
	//_panelArea = { 0, _titleAreaHeight, getDimensions().w, getDimensions().h - _titleAreaHeight };

	//_overlay->setArea(_panelArea);

	const int32 TAB_WIDTH = 120;
	int32 tabOffset = 0;

	for (int32 i = 0; i < (int32)_panels.size(); ++i) {
		_panels[i]->setVisible(i == _panelIdx);
		_tabAreas[i] = Rect(tabOffset, 0, TAB_WIDTH, _titleAreaHeight);
		tabOffset += TAB_WIDTH;
	}
}

void DockPanel::arrangePanels() {
	switch (_displayMode) {
		case DisplayMode::Vertical: {
			f32 totalWidth = (f32)getDimensions().w;
			int32 h = getDimensions().h;

			int32 prevHandleEnd = 0;

			for (size_t i = 0; i < _handleOffsets.size(); ++i) {
				uint32 pixelOffset = (uint32)(_handleOffsets[i] * totalWidth);

				_handleAreas[i] = createHandleArea(pixelOffset);
				_panels[i]->setArea({ prevHandleEnd, 0, _handleAreas[i].x - prevHandleEnd, h });

				prevHandleEnd = _handleAreas[i].right();
			}

			if (_panels.size()) {
				_panels.back()->setArea({ prevHandleEnd, 0, getDimensions().w - prevHandleEnd, h });
			}

			break;
		}
		case DisplayMode::Horizontal: {
			f32 totalHeight = (f32)getDimensions().h;
			int32 w = getDimensions().w;

			int32 prevHandleEnd = 0;

			for (size_t i = 0; i < _handleOffsets.size(); ++i) {
				int32 pixelOffset = (int32)(_handleOffsets[i] * totalHeight);

				_handleAreas[i] = createHandleArea(pixelOffset);
				_panels[i]->setArea({ 0, prevHandleEnd, w, _handleAreas[i].y - prevHandleEnd });

				prevHandleEnd = _handleAreas[i].bottom();
			}

			if (_panels.size()) {
				_panels.back()->setArea({ 0, prevHandleEnd, w, getDimensions().h - prevHandleEnd });
			}

			break;
		}
		case DisplayMode::Single:
		case DisplayMode::Tab: {
			for (DockPanelPtr& panel : _panels) {
				panel->setDimensions(getDimensions());
			}

			break;
		}
	}
}

DropTargetType DockPanel::dropTargetUnderCursor(Point position) {
	for (size_t i = 0; i < _dropTargets.size(); ++i) {
		if (_dropTargets[i].contains(position)) {
			return (DropTargetType)i;
		}
	}

	return DropTargetType::None;
}

Rect DockPanel::createHandleArea(int32 pixelOffset) {
	int32 _handleSize = 20;

	if (_displayMode == DisplayMode::Vertical) {
		return Rect{ pixelOffset - _handleSize / 2, 0, _handleSize, getDimensions().h };
	} else {
		return Rect{ 0, pixelOffset - _handleSize / 2, getDimensions().w, _handleSize };
	}
}
