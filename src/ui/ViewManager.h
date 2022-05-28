#pragma once

#include <unordered_set>

#include <nanovg.h>
#include <spdlog/spdlog.h>

#include "ui/View.h"

namespace rp {
	class ViewManager : public View {
	private:
		struct MouseState {
			bool buttons[MouseButton::COUNT] = { false };
			Point position;
			Point dragDistance;
		};

		MouseState _mouseState;

		View::Shared _sharedData;

		std::vector<ViewPtr> _mouseOver;
		//std::unordered_set<ViewPtr> _mouseOver;

	public:
		ViewManager() : View({ 100, 100 }) {
			setType<ViewManager>();
			_shared = &_sharedData;
		}

		ViewManager(Dimension dimensions): View(dimensions) {
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

		bool onMouseScroll(PointT<f32> delta) {
			return propagateMouseScroll(delta, _mouseState.position, getChildren());
		}

		bool onMouseButton(MouseButton::Enum button, bool down) {
			return onMouseButton(button, down, _mouseState.position);
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) final override {
			_mouseState.buttons[button] = down;

			if (button == MouseButton::Left && !down) {
				DragContext& ctx = _shared->dragContext;
				
				if (ctx.isDragging) {
					ctx.isDragging = false;

					if (ctx.view) {
						ViewPtr target = propagateDrop(position, getChildren());
						if (target) {

						}

						spdlog::info("Finished dragging {} on to {}", ctx.view->getName(), target ? target->getName() : "nothing");

						propagateDragEnter(position, getChildren(), ctx);

						ctx.view->onDragFinish(ctx);

						propagateMouseEnter(position, getChildren());

						ctx.view = nullptr;
					} else {
						propagateClick(button, down, position, getChildren());
						propagateMouseEnter(position, getChildren());
					}
				}

				return true;
			} else {
				return propagateClick(button, down, position, getChildren());
			}
		}

		bool onMouseMove(Point pos) final override {
			bool handled = false;
			DragContext& ctx = _shared->dragContext;

			if (!ctx.isDragging) {
				if (!_mouseState.buttons[MouseButton::Left]) {
					handled = propagateMouseEnter(pos, getChildren());
					handled |= propagateMouseMove(pos, getChildren());
				} else {
					// Start drag
					ctx.isDragging = true;

					if (_shared->focused) {
						ctx.selected = _shared->focused->shared_from_this();

						if (_shared->focused->isDraggable()) {
							ctx.view = ctx.selected;
							spdlog::info("Started dragging {}", ctx.view->getName());
							ctx.view->onDragStart();
						}
					}
				}				
			} else {
				if (ctx.view) {
					handled = propagateDragEnter(pos, getChildren(), _shared->dragContext);
					handled |= propagateDragMove(pos, getChildren());
				} else {
					// lock focus on current view
					auto worldArea = ctx.selected->getWorldArea();
					bool mouseInView = worldArea.contains(pos);

					if (ctx.selected->_mouseOver) {
						if (!mouseInView) {
							spdlog::info("Mouse leaving {}", ctx.selected->getName());
							ctx.selected->onMouseLeave();
							ctx.selected->_mouseOver = false;
						} else {
							ctx.selected->onMouseMove(pos - worldArea.position);
						}
					} else {
						if (mouseInView) {
							spdlog::info("Mouse entering {}", ctx.selected->getName());
							ctx.selected->onMouseEnter(pos - worldArea.position);
							ctx.selected->_mouseOver = true;
						}
					}
				}				
			}

			_mouseState.position = pos;
			return handled;
		}

		bool propagateMouseEnter(Point position, std::vector<ViewPtr>& views) {
			bool handled = false;

			for (ViewPtr& view : views) {
				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

					if (!view->_mouseOver) {
						spdlog::info("Mouse entering {}", view->getName());
						view->onMouseEnter(childPosition);
						view->_mouseOver = true;
						handled = true;
					}

					handled |= propagateMouseEnter(childPosition, view->getChildren());
				} else if (view->_mouseOver) {
					// We have encountered a view that the mouse has left.  We need to call onMouseLeave()
					// on all dependencies, starting from the upper most child.
					propagateMouseLeave(view);
					handled = true;
				}
			}

			return handled;
		}

		void propagateMouseLeave(ViewPtr& view) {
			if (view->isVisible()) {
				for (int32 i = (int32)view->getChildren().size() - 1; i >= 0; --i) {
					ViewPtr child = view->getChild(i);

					if (child->_mouseOver) {
						propagateMouseLeave(child);
					}
				}

				spdlog::info("Mouse leaving {}", view->getName());
				view->onMouseLeave();
				view->_mouseOver = false;
			}
		}

		bool propagateMouseMove(Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

					if (!propagateMouseMove(childPosition, view->getChildren())) {
						if (view->onMouseMove(childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateDragMove(Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

					if (!propagateDragMove(childPosition, view->getChildren())) {
						if (view->onDragMove(_shared->dragContext, childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateDragEnter(Point position, std::vector<ViewPtr>& views, DragContext& ctx) {
			bool handled = false;

			for (ViewPtr& view : views) {
				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;
					
					// TODO: Don't allow dragging a parent element on to a child element

					if (ctx.isDragging && view != ctx.view && !view->_dragOver) {
						spdlog::info("Drag entering {}", view->getName());

						_sharedData.dragContext.targets.push_back(view);

						view->onDragEnter(ctx, childPosition);
						view->_dragOver = true;
						handled = true;
					}

					handled |= propagateDragEnter(childPosition, view->getChildren(), ctx);
				} else if (view->_dragOver) {
					propagateDragLeave(view);
				}
			}

			return handled;
		}

		void propagateDragLeave(ViewPtr& view) {
			if (view->isVisible()) {
				for (int32 i = (int32)view->getChildren().size() - 1; i >= 0; --i) {
					ViewPtr child = view->getChild(i);

					if (child->_dragOver) {
						propagateDragLeave(child);
					}
				}

				spdlog::info("Drag leaving {}", view->getName());
				assert(view == _sharedData.dragContext.targets.back());

				_sharedData.dragContext.targets.pop_back();
				view->onDragLeave(_shared->dragContext);

				view->_dragOver = false;
			}
		}

		bool onDrop(const std::vector<std::string>& paths) final override { 
			return propagateDrop(paths, _mouseState.position, getChildren());
		}

		void onUpdate(f32 delta) final override {
			if (_shared->layoutDirty) {
				updateLayout();
			}
			
			propagateUpdate(getChildren(), delta);
			//handleRemovals();

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

		void updateLayout() {
			propagateSizingUpdate(getChildren());
			propagateLayoutChange(getChildren());
			
			_area = Rect();
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
					assert(view->getParent()->getSizingMode() != SizingMode::FitToContent);

					Rect area = { {0, 0}, view->getParent()->getDimensions() };

					if (view->getArea() != area) {
						view->setArea(area);
					}
				}

				propagateSizingUpdate(view->getChildren());

				if (view->_sizingMode == SizingMode::FitToContent) {
					Rect targetArea;
					calculateTotalArea(view->getChildren(), { 0, 0 }, targetArea);

					if (view->getArea() != targetArea) {
						view->setArea(targetArea);
					}
				}
			}
		}

		void calculateTotalArea(std::vector<ViewPtr>& views, Point offset, Rect& totalArea) {
			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					Rect viewWorldArea(offset + view->getPosition(), view->getDimensions());
					totalArea = totalArea.combine(viewWorldArea);

					calculateTotalArea(view->getChildren(), viewWorldArea.position, totalArea);
				}
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

		PointT<f32> pointToFloat(Point point) {
			return { (f32)point.x, (f32)point.y };
		}

		void propagateRender(std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					PointT<f32> pos = pointToFloat(view->getPosition());
					nvgTranslate(getVg(), pos.x, pos.y);
					view->onRender();
					nvgTranslate(getVg(), -pos.x, -pos.y);
				}
			}

			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					PointT<f32> pos = pointToFloat(view->getPosition());
					nvgTranslate(getVg(), pos.x, pos.y);
					propagateRender(view->getChildren());
					nvgTranslate(getVg(), -pos.x, -pos.y);
				}
			}
		}

		bool propagateMouseScroll(PointT<f32> delta, Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr& view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

					if (!propagateMouseScroll(delta, childPosition, view->getChildren())) {
						if (view->onMouseScroll(delta, childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool propagateClick(MouseButton::Enum button, bool down, Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr& view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					if (down) {
						_shared->focused = view.get();
					}

					Point childPosition = position - view->getArea().position;

					if (!propagateClick(button, down, childPosition, view->getChildren())) {
						if (view->onMouseButton(button, down, childPosition)) {	
							return true;
						}
					}
				}
			}

			return false;
		}

		ViewPtr propagateDrop(Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					_shared->focused = view.get();

					Point childPosition = position - view->getArea().position;
					ViewPtr childView = propagateDrop(childPosition, view->getChildren());

					if (childView) {
						return childView;
					}

					if (view->onDrop(_shared->dragContext, childPosition)) {
						return view;
					}
				}
			}

			return nullptr;
		}

		bool propagateAction(Point position, std::vector<ViewPtr>& views, const std::function<bool(ViewPtr&, Point)>& f) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

					if (!propagateAction(childPosition, view->getChildren(), f)) {
						if (f(view, childPosition)) {
							return true;
						}
					}
				}
			}

			return false;
		}

		// TODO: Make this a bit more generic
		bool propagateDrop(const std::vector<std::string>& paths, Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getArea().contains(position)) {
					Point childPosition = position - view->getArea().position;

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
