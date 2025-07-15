#include "MenuView.h"

#include "foundation/ResourceManager.h"

using namespace rp;

const f32 MARGIN = 10.0f;

const fw::Color4 COLOR_WHITE = fw::Color4(255, 255, 255, 255);
const fw::Color4 COLOR_GRAY = fw::Color4(190, 190, 190, 255);

MenuView::MenuView() : fw::View({ 160, 144 }) {
	setFocusPolicy(fw::FocusPolicy::Click);
	_menuArea = fw::RectF{ MARGIN, MARGIN, 160 - MARGIN * 2, 144 - MARGIN * 2 };
}

void MenuView::onUpdate(f32 delta) {
	if (_context.isClosing()) {
		this->remove();
	}
}

void MenuView::onInitialize() {
	//_font = getResourceManager().load<Font>("PlatNomor.ttf/16");
	//setArea({ 0, 0, 160, 144 });
	//getLayout().setFlexPositionType(fw::FlexPositionType::Absolute);
	//fitToParent();
}

bool isHighlightable(fw::MenuItemType type) {
	return type != fw::MenuItemType::Separator && type != fw::MenuItemType::Title;
}

bool MenuView::moveCursorUp() {
	for (int32 i = (int32)_selectedIdx - 1; i >= 0; --i) {
		const PositionedMenuItem& item = _flat[i];

		if (isHighlightable(item.menuItem->getType())) {
			_selectedIdx = i;
			updateScrollOffset(item);

			return true;
		}
	}

	return false;
}

bool MenuView::moveCursorDown() {
	for (size_t i = _selectedIdx + 1; i < _flat.size(); ++i) {
		const PositionedMenuItem& item = _flat[i];

		if (isHighlightable(item.menuItem->getType())) {
			_selectedIdx = (int32)i;
			updateScrollOffset(item);

			return true;
		}
	}

	return false;
}

void MenuView::updateScrollOffset(const PositionedMenuItem& item) {
	if (_menuBounds.h < _menuArea.h) {
		_drawOffset.y = 0;
	} else if (item.area.y < _scrollStartOffset) {
		_drawOffset.y = 0;
	} else if (item.area.y > _scrollEndOffset) {
		_drawOffset.y = -(_menuBounds.h - _menuArea.h);
	} else {
		_drawOffset.y = -(item.area.y - _scrollStartOffset);
	}
}

void MenuView::flattenHierarchy(fw::Menu& menu, fw::PointF& pos) {
	for (fw::MenuItemBase* item : menu.getItems()) {
		_flat.push_back({ fw::RectF(pos.x, pos.y, 160, 0), item });

		if (item->getType() == fw::MenuItemType::SubMenu && _openMenus.count(item) > 0) {
			_flat.back().area.h = _itemSpacing;

			pos.x += _indentSize;
			pos.y += _itemSpacing;
			flattenHierarchy(*item->as<fw::Menu>(), pos);
			pos.x -= _indentSize;
		} else if (item->getType() == fw::MenuItemType::Separator) {
			_flat.back().area.h = _separatorSpacing;
			pos.y += _separatorSpacing;
		} else {
			_flat.back().area.h = _itemSpacing;
			pos.y += _itemSpacing;
		}
	}
}

void MenuView::processButtons(const std::vector<fw::StreamButtonPress>& stream) {
	for (size_t i = 0; i < stream.size(); ++i) {
		const fw::StreamButtonPress& ev = stream[i];
		fw::ButtonType button = static_cast<fw::ButtonType>(ev.button);

		switch (button) {
		case fw::ButtonType::Left:
			if (ev.down) {
				fw::MenuItemBase* menuItem = getHighlighted();
				if (menuItem->getType() == fw::MenuItemType::MultiSelect) {
					fw::MultiSelect* multiSelect = menuItem->as<fw::MultiSelect>();
					multiSelect->prevItem();
					multiSelect->getFunction()(multiSelect->getValue());
				}
			}

			break;

		case fw::ButtonType::Right:
			if (ev.down) {
				fw::MenuItemBase* menuItem = getHighlighted();
				if (menuItem->getType() == fw::MenuItemType::MultiSelect) {
					fw::MultiSelect* multiSelect = menuItem->as<fw::MultiSelect>();
					multiSelect->nextItem();
					multiSelect->getFunction()(multiSelect->getValue());
				}
			}

			break;

		case fw::ButtonType::Down:
			if (ev.down) {
				moveCursorDown();
			}

			break;
		case fw::ButtonType::Up:
			if (ev.down) {
				moveCursorUp();
			}

			break;

		case fw::ButtonType::A:
			if (ev.down) {
				activateHighlighted();
			}

			break;

		case fw::ButtonType::Start:
			if (ev.down) {
				activateHighlighted();
			}

			break;

		case fw::ButtonType::B:
			if (_escCloses && ev.down) {
				this->remove();
			}

			break;
		}
	}
}

void MenuView::setMenu(fw::MenuPtr menu) {
	_selectedIdx = 0;
	_openMenus.clear();
	_root = menu;
	rebuildFlat();
}

void MenuView::rebuildFlat() {
	fw::MenuItemBase* highlighted = getHighlighted();

	_flat.clear();

	fw::PointF offset = { 0, 0 };
	flattenHierarchy(*_root, offset);

	_scrollStartOffset = 0;
	_scrollEndOffset = 0;
	_drawOffset = { 0, 0 };

	if (_flat.size()) {
		auto dim = getDimensions();
		_menuBounds = { (f32)dim.w, _flat.back().area.bottom() };

		if (_menuBounds.h > _menuArea.h) {
			_scrollStartOffset = dim.h / 2 - _itemSpacing / 2;
			_scrollEndOffset = _menuBounds.h - _scrollStartOffset;
		}
	}

	_selectedIdx = -1;

	for (size_t i = 0; i < _flat.size(); ++i) {
		if (_selectedIdx == -1 && isHighlightable(_flat[i].menuItem->getType())) {
			_selectedIdx = (int32)i;
		}

		if (_flat[i].menuItem == highlighted) {
			_selectedIdx = (int32)i;
			updateScrollOffset(_flat[i]);
			break;
		}
	}
}

void MenuView::activateHighlighted() {
	fw::MenuItemBase* item = getHighlighted();

	if (!item->isActive()) {
		return;
	}

	switch (item->getType()) {
	case fw::MenuItemType::Action:
	{
		fw::Action* action = (fw::Action*)item;
		fw::ActionContextFunction& func = action->getFunction();

		if (_autoClose) {
			_context.close();
		}

		func(_context);

		if (_context.isClosing()) {
			this->remove();
		} else if (_context.isDisablingCurrent()) {
			item->setActive(false);
			_context.disableCurrent(false);
		}

		break;
	}
	case fw::MenuItemType::Select:
	{
		fw::Select* select = (fw::Select*)item;
		select->toggleChecked();
		break;
	}
	case fw::MenuItemType::SubMenu:
		if (_openMenus.count(item) == 0) {
			_openMenus.insert(item);
		} else {
			_openMenus.erase(item);
		}

		rebuildFlat();

		break;
	}
}

fw::MenuItemBase* MenuView::getHighlighted() {
	if (_flat.size()) {
		return _flat[_selectedIdx].menuItem;
	}

	return nullptr;
}

void MenuView::drawText(fw::Canvas& canvas, fw::RectF area, std::string_view text, fw::Color4 color) {
	area.x += _menuArea.x + _drawOffset.x;
	area.y += _menuArea.y + _drawOffset.y;

	canvas.setTextAlign(fw::TextAlignFlags::Middle | fw::TextAlignFlags::Left);
	canvas.text(area, text, color);
}

void MenuView::drawArrow(fw::Canvas& canvas, fw::RectF area, ArrowDirection dir) {
	std::array<fw::PointF, 3> points;

	area.position += _menuArea.position + _drawOffset;

	switch (dir) {
	case ArrowDirection::Left:
		points[0] = area.topRight();
		points[1] = { area.x, area.getCenter().y };
		points[2] = area.bottomRight();
		break;
	case ArrowDirection::Right:
		points[0] = area.position;
		points[1] = { area.right(), area.getCenter().y };
		points[2] = area.bottomLeft();
		break;
	case ArrowDirection::Up:
		points[0] = area.bottomLeft();
		points[1] = { area.getCenter().x, area.y };
		points[2] = area.bottomRight();
		break;
	case ArrowDirection::Down:
		points[0] = area.position;
		points[1] = { area.getCenter().x, area.bottom() };
		points[2] = area.topRight();
		break;
	}

	canvas.lines(points, fw::Color4F(1, 1, 1, 1));
}

void MenuView::drawMenu(fw::Canvas& canvas, fw::Menu& menu) {
	const fw::DimensionT<f32> dim = { (f32)getDimensions().w, (f32)getDimensions().h };
	const fw::PointF drawOffset = _drawOffset + _menuArea.position;

	canvas.fillRect(getDimensions(), fw::Color4F(0, 0, 0, 0.8f));

	for (size_t i = 0; i < _flat.size(); ++i) {
		auto& item = _flat[i];

		// TODO: Only render this item if it is visible

		const f32 ARROW_SIZE = 2.0f;
		uint8 alpha = item.menuItem->isActive() ? 255 : 127;

		if (i == _selectedIdx) {
			fw::RectF arrowArea(item.area.x - 6, item.area.y + 1.5f, ARROW_SIZE, ARROW_SIZE * 2);
			drawArrow(canvas, arrowArea, ArrowDirection::Right);
		}

		fw::DimensionF textDimensions;
		if (item.menuItem->getType() != fw::MenuItemType::Separator) {
			textDimensions = canvas.measureText(item.menuItem->getName());
			drawText(canvas, item.area, item.menuItem->getName(), item.menuItem->isActive() ? COLOR_WHITE : COLOR_GRAY);
		} else {
			f32 yPos = item.area.y + (_separatorSpacing / 2) + drawOffset.y - 2.0f;

			canvas.line(
				fw::PointF{ item.area.x + _indentSize, yPos },
				fw::PointF{ (f32)getDimensions().w, yPos },
				fw::Color4(255, 255, 255, 127)
			);
		}

		if (item.menuItem->getType() == fw::MenuItemType::SubMenu) {
			const f32 ARROW_SIZE = 2.0f;

			fw::DimensionF bounds = getFontManager().measureText(item.menuItem->getName(), _fontName, _fontSize);

			f32 offset = (item.area.h - bounds.h) * 0.5f;

			fw::RectF arrowArea(item.area.x + bounds.w, item.area.y + offset, ARROW_SIZE * 2, ARROW_SIZE);
			arrowArea.x += 3;
			arrowArea.y += 1;

			drawArrow(canvas, arrowArea, ArrowDirection::Down);
		}

		if (item.menuItem->getType() == fw::MenuItemType::Select) {
			const f32 CHECK_BOX_SIZE = _itemSpacing * 0.6f;
			fw::Select* select = item.menuItem->as<fw::Select>();

			fw::PointF pos = item.area.position + _menuArea.position + _drawOffset;
			fw::RectF checkboxArea(_menuArea.right() - CHECK_BOX_SIZE - 1.0f, pos.y, CHECK_BOX_SIZE, CHECK_BOX_SIZE);

			canvas.strokeRect(checkboxArea, fw::Color4F::white);

			if (select->getChecked()) {
				fw::RectF checkArea = checkboxArea.shrink(1.f);

				canvas.line(checkArea.position, checkArea.bottomRight());
				canvas.line(checkArea.bottomLeft(), checkArea.topRight());
			}
		}

		if (item.menuItem->getType() == fw::MenuItemType::MultiSelect) {
			fw::MultiSelect* multiSelect = item.menuItem->as<fw::MultiSelect>();

			const f32 ARROW_SIZE = 2.0f;
			const f32 arrowHeightDiff = floor((item.area.h - (ARROW_SIZE * 2)) * 0.5f);

			auto& selectItems = multiSelect->getItems();
			int value = multiSelect->getValue();

			const std::string& selected = value < selectItems.size() ? selectItems[multiSelect->getValue()] : "";

			fw::DimensionF bounds = getFontManager().measureText(selected, _fontName, _fontSize);

			const f32 textStart = _menuArea.x + _drawOffset.x + item.area.position.x + textDimensions.w;
			const f32 textWidth = dim.w - textStart;

			fw::RectF availableArea = fw::RectF(item.area.position.x + textDimensions.w, item.area.position.y, textWidth, item.area.h);
			availableArea = availableArea.shrink(4.0f, 0.0f);

			fw::RectF leftArrowArea(availableArea.x, availableArea.y + arrowHeightDiff, ARROW_SIZE, ARROW_SIZE * 2);
			fw::RectF rightArrowArea(availableArea.right() - ARROW_SIZE, leftArrowArea.y, ARROW_SIZE, ARROW_SIZE * 2);

			drawArrow(canvas, leftArrowArea, ArrowDirection::Left);
			canvas.setTextAlign(fw::TextAlignFlags::Middle | fw::TextAlignFlags::Center);
			availableArea.position += _menuArea.position + _drawOffset;
			canvas.text(availableArea, selected, fw::Color4F::white);
			drawArrow(canvas, rightArrowArea, ArrowDirection::Right);
		}
	}
}

void MenuView::onRender(fw::Canvas& canvas) {
	setClip(true);

	_fontSize = 7.0f;
	_itemSpacing = 10.0f;
	_separatorSpacing = 5.0f;

	canvas.setFont(_fontName, _fontSize);
	canvas.setTextAlign(fw::TextAlignFlags::Middle | fw::TextAlignFlags::Left);

	if (_root) {
		rebuildFlat();
		drawMenu(canvas, *_root);
	}
}
