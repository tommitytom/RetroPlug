#pragma once

#include <stack>
#include <unordered_set>

#include "ui/View.h"
#include "platform/Menu.h"

namespace rp {
	/*class Menu {
	public:
		struct DrawItem {
			PointT<f32> position;
			std::string text;
			bool selected;
		};

	private:
		uintptr_t _selected = 0;
		std::vector<DrawItem> _drawList;
		PointT<f32> _offset;

		f32 _indentSize = 10.0f;
		f32 _itemSpacing = 20.0f;

		Point<int32> _cursor;



	public:
		void beginFrame() {
			_offset = { 0, 0 };
			_drawList.clear();
		}

		void endFrame() {

		}

		void adjustCursor(Point<int32> pos) {
			_cursor += pos;
		}

		void cursorDown() {

		}

		bool action(std::string_view name) {
			addDrawItem(name);
			_offset.y += _itemSpacing;
			return false;
		}

		bool beginMenu(std::string_view name) {
			addDrawItem(name);
			_offset += { _indentSize, _itemSpacing };
		}

		void endMenu() {
			_offset.x -= _indentSize;
		}

		const std::vector<DrawItem>& getDrawList() const {
			return _drawList;
		}



	private:
		uintptr_t addDrawItem(std::string_view text, uintptr_t id = 0) {
			if (id == 0) {
				id = reinterpret_cast<uintptr_t>(text.data());
			}

			if (_selected == 0) {
				_selected = id;
			}

			_drawList.push_back(DrawItem{
				.position = _offset,
				.text = std::string(text),
				.selected = _selected == id
			});

			return id;
		}
	};*/

	struct PositionedMenuItem {
		RectT<f32> area;
		MenuItemBase* menuItem;
	};

	class MenuView : public View {
	private:
		f32 _fontSize = 12.0f;

		f32 _indentSize = 10.0f;
		f32 _itemSpacing = 20.0f;
		f32 _separatorSpacing = 20.0f;

		PointT<f32> _drawOffset;

		RectT<f32> _menuArea;
		DimensionT<f32> _menuBounds;

		f32 _scrollStartOffset = 0;
		f32 _scrollEndOffset = 0;
		f32 _scrollOffset = 0;

		MenuPtr _root;

		std::unordered_set<MenuItemBase*> _openMenus;

		std::vector<PositionedMenuItem> _flat;
		int32 _selectedIdx = 0;

		bool _autoClose = true;
		bool _escCloses = true;

		MenuContext _context;

	public:
		MenuView();

		void onUpdate(f32 delta) override;

		void onRender() override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) override { return true; }

		void setMenu(MenuPtr menu);

		void setAutoClose(bool autoClose) {
			_autoClose = autoClose;
		}

		void setEscCloses(bool escCloses) {
			_escCloses = escCloses;
		}

	private:
		void drawMenu(Menu& menu);

		void drawText(f32 x, f32 y, std::string_view text, NVGcolor color);

		bool moveCursorDown();

		bool moveCursorUp();

		void activateHighlighted();

		MenuItemBase* getHighlighted();

		void rebuildFlat();

		void flattenHierarchy(Menu& menu, PointT<f32>& pos);

		void updateScrollOffset(const PositionedMenuItem& item);
	};

	using MenuViewPtr = std::shared_ptr<MenuView>;
}
