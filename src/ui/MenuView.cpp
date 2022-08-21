#include "MenuView.h"

#include "foundation/ResourceManager.h"

using namespace rp;

const f32 MARGIN = 10.0f;

const Color4 COLOR_WHITE = Color4(255, 255, 255, 255);
const Color4 COLOR_GRAY = Color4(190, 190, 190, 255);

MenuView::MenuView() : View({ 160, 144 }) {
	setType<MenuView>();
	_menuArea = RectF { MARGIN, MARGIN, 160 - MARGIN * 2, 144 - MARGIN * 2 };
}

void MenuView::onUpdate(f32 delta) {
	if (_context.isClosing()) {
		this->remove();
	}
}

void MenuView::onInitialize() {
	//_font = getResourceManager().load<Font>("PlatNomor.ttf/16");
}

bool isHighlightable(MenuItemType type) {
	return type != MenuItemType::Separator && type != MenuItemType::Title;
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

void MenuView::flattenHierarchy(Menu& menu, PointF& pos) {
	for (MenuItemBase* item : menu.getItems()) {
		_flat.push_back({ RectF(pos.x, pos.y, 160, 0), item });

		if (item->getType() == MenuItemType::SubMenu && _openMenus.count(item) > 0) {
			_flat.back().area.h = _itemSpacing;

			pos.x += _indentSize;
			pos.y += _itemSpacing;
			flattenHierarchy(*item->as<Menu>(), pos);
			pos.x -= _indentSize;
		} else if (item->getType() == MenuItemType::Separator) {
			_flat.back().area.h = _separatorSpacing;
			pos.y += _separatorSpacing;
		} else {
			_flat.back().area.h = _itemSpacing;
			pos.y += _itemSpacing;
		}
	}
}



bool MenuView::onKey(VirtualKey::Enum key, bool down) {
	switch (key) {
	case VirtualKey::LeftArrow:
		if (down) {
			MenuItemBase* menuItem = getHighlighted();
			if (menuItem->getType() == MenuItemType::MultiSelect) {
				MultiSelect* multiSelect = menuItem->as<MultiSelect>();
				multiSelect->prevItem();
				multiSelect->getFunction()(multiSelect->getValue());
			}
		}

		break;

	case VirtualKey::RightArrow:
		if (down) {
			MenuItemBase* menuItem = getHighlighted();
			if (menuItem->getType() == MenuItemType::MultiSelect) {
				MultiSelect* multiSelect = menuItem->as<MultiSelect>();
				multiSelect->nextItem();
				multiSelect->getFunction()(multiSelect->getValue());
			}
		}

		break;

	case VirtualKey::DownArrow:
		if (down) {
			moveCursorDown();
		}

		break;
	case VirtualKey::UpArrow:
		if (down) {
			moveCursorUp();
		}

		break;

	case VirtualKey::Enter:
		if (down) {
			activateHighlighted();
		}

		break;

	case VirtualKey::Space:
		if (down) {
			activateHighlighted();
		}

		break;

	case VirtualKey::Esc:
		if (_escCloses && down) {
			this->remove();
		}

		break;
	}

	return true;
}

void MenuView::setMenu(MenuPtr menu) {
	_selectedIdx = 0;
	_openMenus.clear();
	_root = menu;
	rebuildFlat();
}

void MenuView::rebuildFlat() {
	MenuItemBase* highlighted = getHighlighted();

	_flat.clear();

	PointF offset = { 0, 0 };
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
	MenuItemBase* item = getHighlighted();

	if (!item->isActive()) {
		return;
	}

	switch (item->getType()) {
	case MenuItemType::Action: {
		ActionContextFunction& func = ((Action*)item)->getFunction();

		if (_autoClose) {
			_context.close();
		}

		func(_context);

		if (_context.isClosing()) {
			this->remove();
		}

		break;
	}
	case MenuItemType::Select: {
		Select* select = (Select*)item;
		select->toggleChecked();
		break;
	}
	case MenuItemType::SubMenu:
		if (_openMenus.count(item) == 0) {
			_openMenus.insert(item);
		} else {
			_openMenus.erase(item);
		}

		rebuildFlat();

		break;
	}
}

MenuItemBase* MenuView::getHighlighted() {
	if (_flat.size()) {
		return _flat[_selectedIdx].menuItem;
	}

	return nullptr;
}

void MenuView::drawText(Canvas& canvas, f32 x, f32 y, std::string_view text, Color4 color) {
	x += _menuArea.x + _drawOffset.x;
	y += _menuArea.y + _drawOffset.y;

	canvas.text(x, y, text, color);

	/*nvgFontSize(vg, _fontSize);
	nvgFontFace(vg, "PlatNomor");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(vg, color);
	nvgStrokeColor(vg, color);
	nvgText(vg, x, y, text.data(), NULL);*/
}

enum class ArrowDirection {
	Left,
	Right,
	Up,
	Down
};

void drawArrow(Canvas& canvas, const RectF& area, ArrowDirection dir) {
	std::array<PointF, 3> points;

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

	canvas.lines(points, Color4F(1, 1, 1, 1));
}

void MenuView::drawMenu(Canvas& canvas, Menu& menu) {
	DimensionT<f32> dim = { (f32)getDimensions().w, (f32)getDimensions().h };
	PointF drawOffset = _drawOffset + _menuArea.position;

	//nvgScissor(vg, 0, 0, dim.w, dim.h);

	canvas.fillRect(getDimensions(), Color4F(0, 0, 0, 0.8f));

	for (size_t i = 0; i < _flat.size(); ++i) {
		auto& item = _flat[i];
		PointF itemOffset = item.area.position + drawOffset;

		// TODO: Only render this item if it is visible

		const f32 ARROW_SIZE = 2.0f;
		uint8 alpha = item.menuItem->isActive() ? 255 : 127;

		if (i == _selectedIdx) {
			RectF arrowArea(itemOffset.x - 6, itemOffset.y + 1.5f, ARROW_SIZE, ARROW_SIZE * 2);
			drawArrow(canvas, arrowArea, ArrowDirection::Right);
		}

		if (item.menuItem->getType() != MenuItemType::Separator) {
			drawText(canvas, item.area.x, item.area.y, item.menuItem->getName(), item.menuItem->isActive() ? COLOR_WHITE : COLOR_GRAY);
		} else {
			f32 yPos = item.area.y + (_separatorSpacing / 2) + drawOffset.y - 2.0f;

			canvas.line(
				{ item.area.x + _indentSize, yPos }, 
				{ (f32)getDimensions().w, yPos }, 
				Color4(255, 255, 255, 127)
			);
		}

		if (item.menuItem->getType() == MenuItemType::SubMenu) {
			const f32 ARROW_SIZE = 2.0f;
			RectF arrowArea(itemOffset.x - 6, itemOffset.y, ARROW_SIZE * 2, ARROW_SIZE);
			arrowArea.x += 50;
			arrowArea.y += 2;
			
			DimensionF bounds = getFontManager().measureText(item.menuItem->getName(), "PlatNomor.ttf", _fontSize);
			arrowArea.x = drawOffset.x + 5.0f + bounds.w;

			drawArrow(canvas, arrowArea, ArrowDirection::Down);
		}

		if (item.menuItem->getType() == MenuItemType::Select) {
			const f32 CHECK_BOX_SIZE = _itemSpacing * 0.6f;
			Select* select = item.menuItem->as<Select>();

			RectF checkboxArea(_menuArea.right() - CHECK_BOX_SIZE - 1.0f, itemOffset.y, CHECK_BOX_SIZE, CHECK_BOX_SIZE);

			/*nvgBeginPath(vg);
			nvgRect(vg, checkboxArea.x, checkboxArea.y, checkboxArea.w, checkboxArea.h);
			nvgStrokeWidth(vg, 0.5f);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
			nvgStroke(vg);

			if (select->getChecked()) {
				RectF checkArea = checkboxArea.shrink(1.5f);

				nvgBeginPath(vg);
				nvgMoveTo(vg, checkArea.x, checkArea.y);
				nvgLineTo(vg, checkArea.right(), checkArea.bottom());
				nvgMoveTo(vg, checkArea.right(), checkArea.y);
				nvgLineTo(vg, checkArea.x, checkArea.bottom());
				nvgStrokeWidth(vg, 1.0f);
				nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
				nvgStroke(vg);
			}*/
		}

		if (item.menuItem->getType() == MenuItemType::MultiSelect) {
			MultiSelect* multiSelect = item.menuItem->as<MultiSelect>();

			const f32 ARROW_SIZE = 2.0f;
			RectF arrowArea(itemOffset.x - 6, itemOffset.y, ARROW_SIZE, ARROW_SIZE * 2);
			arrowArea.x += 50;
			arrowArea.y += 1;

			DimensionF bounds = getFontManager().measureText(item.menuItem->getName(), "PlatNomor.ttf", _fontSize);
			arrowArea.x = drawOffset.x + 50.0f + bounds.w;

			drawArrow(canvas, arrowArea, ArrowDirection::Left);

			auto& selectItems = multiSelect->getItems();
			const std::string& selected = selectItems[multiSelect->getValue()];

			//f32 selectedBounds[4];
			//f32 selectedWidth = nvgTextBounds(vg, 0, 0, selected.c_str(), nullptr, selectedBounds);

			drawText(canvas, item.area.x + 80.0f, item.area.y, selected, COLOR_WHITE);

			arrowArea.x = dim.w - _itemSpacing;
			drawArrow(canvas, arrowArea, ArrowDirection::Right);
		}
	}

	// Top gradient
	/*nvgBeginPath(vg);
	nvgRect(vg, 0, 0, dim.w, _menuArea.y);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, 5, 0, 10, nvgRGBA(0, 0, 0, 255), nvgRGBA(0, 0, 0, 0)));
	nvgFill(vg);

	// Bottom gradient
	nvgBeginPath(vg);
	nvgRect(vg, 0, dim.h - 10, dim.w, _menuArea.y);
	nvgFillPaint(vg, nvgLinearGradient(vg, 0, dim.h - 5, 0, dim.h - 10, nvgRGBA(0, 0, 0, 255), nvgRGBA(0, 0, 0, 0)));
	nvgFill(vg);*/

	//nvgResetScissor(vg);
}

void MenuView::onRender(Canvas& canvas) {
	setClip(true);
	_fontSize = 9.0f;
	_itemSpacing = 12.0f;
	_separatorSpacing = 7.0f;
	
	canvas.setFont("PlatNomor.ttf", _fontSize);
	canvas.setTextAlign(TextAlignFlags::Top | TextAlignFlags::Left);

	if (_root) {
		rebuildFlat();
		drawMenu(canvas, *_root);
	}
}
