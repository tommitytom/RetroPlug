#pragma once

#include <nanovg.h>
#include <spdlog/spdlog.h>

#include "ui/View.h"

namespace rp {
	class ViewManager : public View {
	private:
		Point<uint32> _mousePosition;		
		View::Shared _sharedData;

	public:
		ViewManager() : View({ 100, 100 }) {
			setType<ViewManager>();
			_shared = &_sharedData;
		}

		ViewManager(Dimension<uint32> dimensions): View(dimensions) {
			setType<ViewManager>();
			_shared = &_sharedData;
		}

		void setScale(f32 scale) {
			if (scale != _sharedData.scale) {
				_sharedData.scale = scale;
				propagateScaleChange(getChildren(), scale);
			}
		}

		f32 getScale() const {
			return _sharedData.scale;
		}

		bool onKey(VirtualKey::Enum key, bool down) final override {
			View* current = _shared->focused;

			while (current && current != this) {
				if (!current->onKey(key, down)) {
					current = current->getParent();
				} else {
					return true;
				}
			}

			return false;
		}

		bool onButton(ButtonType::Enum button, bool down) final override {
			View* current = _shared->focused;

			while (current) {
				if (!current->onButton(button, down)) {
					current = current->getParent();
				} else {
					return true;
				}
			}

			return false;
		}

		bool onMouseScroll(Point<f32> delta) {
			return propagateMouseScroll(delta, _mousePosition, getChildren());
		}

		bool onMouseButton(MouseButton::Enum button, bool down) {
			return propagateClick(button, down, _mousePosition, getChildren());
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point<uint32> position) final override {
			return propagateClick(button, down, position, getChildren());
		}

		bool onMouseMove(Point<uint32> pos) final override {
			_mousePosition = pos;
			return propagateMouseMove(pos, getChildren());
		}

		bool onDrop(const std::vector<std::string>& paths) final override { 
			return propagateDrop(paths, _mousePosition, getChildren());
		}

		void onUpdate(f32 delta) final override {
			if (_shared->layoutDirty) {
				updateLayout();
			}
			
			propagateUpdate(getChildren(), delta);
			handleRemovals();

			if (_shared->layoutDirty) {
				updateLayout();
			}
		}

		void onRender() final override {
			if (getVg()) {
				propagateRender(getChildren());
			}
		}

		void printHierarchy() {
			propagatePrint(getChildren(), "");
		}

		void propagatePrint(std::vector<ViewPtr>& views, std::string indent) {
			for (ViewPtr view : views) {
				spdlog::info("{}- {}", indent, view->getName());
				propagatePrint(view->getChildren(), indent + '\t');
			}
		}

	private:
		bool childHasFocus(View* view, View* focused) {
			if (view == focused) {
				return true;
			}

			for (ViewPtr child : view->getChildren()) {
				if (childHasFocus(child.get(), focused)) {
					return true;
				}
			}

			return false;
		}

		void handleRemovals() {
			for (ViewPtr view : _shared->removals) {
				View* parent = view->getParent();
				//assert(parent);

				if (parent) {
					for (size_t i = 0; i < parent->_children.size(); ++i) {
						if (view == parent->_children[i]) {
							ViewPtr child = parent->_children[i];

							if (_shared->focused && childHasFocus(view.get(), _shared->focused)) {
								_shared->focused = parent;
							}

							view->_parent = nullptr;
							view->_shared = nullptr;

							parent->_children.erase(parent->_children.begin() + i);
							parent->onChildRemoved(child);

							setLayoutDirty();
						}
					}
				}
			}

			_shared->removals.clear();
			updateLayout();
		}

		void updateLayout() {
			propagateSizingUpdate(getChildren());
			propagateLayoutChange(getChildren());
			
			_area = Rect<uint32>();
			calculateTotalArea(getChildren(), { 0, 0 }, _area);

			propagateSizingUpdate(getChildren());
			propagateLayoutChange(getChildren());

			_shared->layoutDirty = false;
		}

		void propagateLayoutChange(std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				view->onLayoutChanged();
				propagateLayoutChange(view->getChildren());
			}
		}

		void propagateSizingUpdate(std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				if (view->_sizingMode == SizingMode::FitToParent) {
					assert(view->getParent());
					Rect<uint32> area = { {0, 0}, view->getDimensions() };

					if (view->getArea() != area) {
						view->setArea(area);
					}
				}

				propagateSizingUpdate(view->getChildren());

				if (view->_sizingMode == SizingMode::FitToContent) {
					Rect<uint32> targetArea;
					calculateTotalArea(view->getChildren(), { 0, 0 }, targetArea);

					if (view->getArea() != targetArea) {
						view->setArea(targetArea);
					}
				}
			}
		}

		void calculateTotalArea(std::vector<ViewPtr>& views, Point<uint32> offset, Rect<uint32>& totalArea) {
			for (ViewPtr& view : views) {
				Rect<uint32> viewWorldArea(offset + view->getPosition(), view->getDimensions());
				totalArea = totalArea.combine(viewWorldArea);

				calculateTotalArea(view->getChildren(), viewWorldArea.position, totalArea);
			}
		}

		void propagateUpdate(std::vector<ViewPtr>& views, f32 delta) {
			for (ViewPtr& view : views) {
				view->onUpdate(delta);
				propagateUpdate(view->getChildren(), delta);
			}
		}

		void propagateScaleChange(std::vector<ViewPtr>& views, f32 scale) {
			for (ViewPtr& view : views) {
				view->onScaleChanged(scale);
				propagateScaleChange(view->getChildren(), scale);
			}
		}

		Point<f32> pointToFloat(Point<uint32> point) {
			return { (f32)point.x, (f32)point.y };
		}

		void propagateRender(std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				Point<f32> pos = pointToFloat(view->getPosition());
				nvgTranslate(getVg(), pos.x, pos.y);
				view->onRender();
				nvgTranslate(getVg(), -pos.x, -pos.y);
			}

			for (ViewPtr& view : views) {
				Point<f32> pos = pointToFloat(view->getPosition());
				nvgTranslate(getVg(), pos.x, pos.y);
				propagateRender(view->getChildren());
				nvgTranslate(getVg(), -pos.x, -pos.y);
			}
		}

		bool propagateMouseScroll(Point<f32> delta, Point<uint32> position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr& view = views[i];

				if (view->getArea().contains(position)) {
					Point<uint32> childPosition = position - view->getArea().position;

					if (!propagateMouseScroll(delta, childPosition, view->getChildren())) {
						if (view->onMouseScroll(delta, childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateClick(MouseButton::Enum button, bool down, Point<uint32> position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr& view = views[i];

				if (view->getArea().contains(position)) {
					if (down) {
						_shared->focused = view.get();
					}

					Point<uint32> childPosition = position - view->getArea().position;

					if (!propagateClick(button, down, childPosition, view->getChildren())) {
						if (view->onMouseButton(button, down, childPosition)) {	
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateMouseMove(Point<uint32> position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->getArea().contains(position)) {
					Point<uint32> childPosition = position - view->getArea().position;

					if (!propagateMouseMove(childPosition, view->getChildren())) {
						if (view->onMouseMove(childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateDrop(const std::vector<std::string>& paths, Point<uint32> position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->getArea().contains(position)) {
					Point<uint32> childPosition = position - view->getArea().position;

					if (!propagateDrop(paths, childPosition, view->getChildren())) {
						if (view->onDrop(paths)) {
							return true;
						}
					}
				}
			}

			return false;
		}
	};
}
