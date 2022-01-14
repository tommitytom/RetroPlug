#pragma once

#include "StartView.h"
#include "SystemView.h"
#include "core/Project.h"

namespace rp {
	enum class HighlightMode {
		Alpha,
		Outline
	};

	static bool viewIsFocused(ViewPtr view) {
		if (view->hasFocus()) {
			return true;
		}

		for (ViewPtr child : view->getChildren()) {
			if (viewIsFocused(child)) {
				return true;
			}
		}

		return false;
	}

	static void focusSystem(ViewPtr view) {
		if (view->getChildren().size()) {
			view->getChildren().back()->focus();
		} else {
			view->focus();
		}
	}

	class GridOverlay final : public View {
	private:
		ViewIndex _selected = INVALID_VIEW_INDEX;
		f32 _unselectedAlpha = 0.75f;

		HighlightMode _highlightMode = HighlightMode::Alpha;

		ViewPtr _grid;

		int32 _projectVersion = -1;

	public:
		GridOverlay() {
			setType<GridOverlay>(); 
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point<uint32> pos) final override {
			if (down) {
				std::vector<ViewPtr>& children = _grid->getChildren();

				for (int32 i = (int32)children.size() - 1; i >= 0; --i) {
					if (children[i]->getArea().contains(pos)) {
						setSelected((ViewIndex)i);
						break;
					}
				}
			}

			return false;
		}

		void onLayoutChanged() final override {
			std::vector<ViewPtr>& children = _grid->getChildren();

			for (size_t i = 0; i < children.size(); ++i) {
				ViewPtr view = children[i];

				if (viewIsFocused(view)) {
					_selected = (ViewIndex)i;
				}
			}

			updateLayout();
		}

		void setSelected(ViewIndex index) {
			_selected = index;
			updateLayout();
		}

		void incrementSelection() {
			_selected++;
			_selected %= _grid->getChildren().size();
			updateLayout();
		}

		void onUpdate(f32 delta) override;

		void onRender() override {
			if (_highlightMode == HighlightMode::Outline && _selected != INVALID_VIEW_INDEX && _grid->getChildren().size() > 1) {
				NVGcontext* vg = getVg();
				ViewPtr child = _grid->getChild(_selected);
				auto childArea = child->getArea();

				nvgBeginPath(vg);
				nvgRect(vg, (f32)childArea.x + 0.5f, (f32)childArea.y, (f32)childArea.w - 0.5f, (f32)childArea.h - 0.5f);
				nvgStrokeWidth(vg, 0.5f);
				nvgStrokeColor(vg, nvgRGBA(255, 0, 0, 220));
				nvgStroke(vg);
			}
		}

		void setUnselectedAlpha(f32 alpha) {
			_unselectedAlpha = alpha;
			updateLayout();
		}

		void setGrid(ViewPtr grid) {
			_grid = grid;
			updateLayout();
		}

	private:
		void updateLayout() {
			if (getArea() != _grid->getArea()) {
				setArea(_grid->getArea());
			}

			std::vector<ViewPtr>& children = _grid->getChildren();

			if (_selected == INVALID_VIEW_INDEX && children.size() > 0) {
				_selected = 0;
			}

			if (_selected >= children.size()) {
				if (children.size() > 0) {
					_selected = (ViewIndex)children.size() - 1;
				}
			}

			if (_selected != INVALID_VIEW_INDEX && !viewIsFocused(children[_selected])) {
				focusSystem(children[_selected]);
			}

			f32 unselectedAlpha = 1.0f;
			if (_highlightMode == HighlightMode::Alpha) {
				unselectedAlpha = _unselectedAlpha;
			}

			for (size_t i = 0; i < children.size(); ++i) {
				if (i == _selected) {
					children[i]->setAlpha(1.0f);
				} else {
					children[i]->setAlpha(unselectedAlpha);
				}
			}
		}
	};

	using GridOverlayPtr = std::shared_ptr<GridOverlay>;
}
