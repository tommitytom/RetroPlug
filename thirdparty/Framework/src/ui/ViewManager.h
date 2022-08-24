#pragma once

#include <unordered_set>
#include <spdlog/spdlog.h>

#include "graphics/Canvas.h"
#include "ui/View.h"

namespace fw {
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
		std::vector<ViewPtr> _dragOver;
		bool _mouseOverClickedItem = false;
		size_t _mouseOverClickIdx = 0;

	public:
		ViewManager() : View({ 100, 100 }) {
			setType<ViewManager>();
			_shared = &_sharedData;
		}

		ViewManager(Dimension dimensions): View(dimensions) {
			setType<ViewManager>();
			_shared = &_sharedData;
		}

		void setResourceManager(ResourceManager* resourceManager, FontManager* fontManager) {
			_shared->resourceManager = resourceManager;
			_shared->fontManager = fontManager;
		}

		void setPixelDensity(f32 pixelDensity) {
			if (pixelDensity != _sharedData.pixelDensity) {
				_sharedData.pixelDensity = pixelDensity;
				propagateScaleChange(getChildren(), pixelDensity);
			}
		}

		f32 getPixelDensity() const {
			return _sharedData.pixelDensity;
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

		bool onMouseScroll(PointF delta, Point position) override {
			return propagateMouseScroll(delta, position);
		}

		void handleDragEnd(Point position) {
			auto& ctx = _sharedData.dragContext;			
			assert(ctx.source);

			if (ctx.source) {
				ViewPtr target = propagateDrop(position);
				propagateDragLeave(position);

				if (target) {
					spdlog::info("Finished dragging {} on to {}", ctx.source->getName(), target ? target->getName() : "nothing");
				} else {
					spdlog::info("Finished dragging {} on to nothing (there was no target)");
				}

				ctx.source->onDragFinish(ctx);
				ctx.source = nullptr;
			} else {
				spdlog::info("Finished drop - there was no source");
			}

			ctx.isDragging = false;
			ctx.attached = nullptr;
			ctx.targets.clear();

			handleMouseEnterLeave(position);
		}

		bool handleMouseEnterLeave(Point position) {
			bool handled = false;
			std::vector<ViewPtr> mouseOver;

			handled |= propagateMouseLeave(position);
			handled |= propagateMouseEnter(position, getChildren(), mouseOver);
			_mouseOver = std::move(mouseOver);

			return handled;
		}

		bool handleDragEnterLeave(Point position) {
			bool handled = false;
			std::vector<ViewPtr> dragOver;

			handled |= propagateDragLeave(position);
			handled |= propagateDragEnter(position, getChildren(), _shared->dragContext, dragOver);
			_shared->dragContext.targets = std::move(dragOver);

			return handled;
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point position) final override {
			bool handled = false;
			
			_mouseState.buttons[button] = down;

			if (button == MouseButton::Left && !down) {
				DragContext& ctx = _shared->dragContext;
				
				if (ctx.isDragging) {
					handleDragEnd(position);
				} else {
					// Focus is locked to a single element
					// Process the mouse button release
					handled = propagateClick(button, down, position, false);
					handled |= handleMouseEnterLeave(position);
				}

				return true;
			}

			return propagateClick(button, down, position, true);
		}

		void onMouseLeave() final override {
			bool buttonHeld = false;

			for (size_t i = 0; i < MouseButton::COUNT; ++i) {
				buttonHeld |= _mouseState.buttons[i];
			}

			if (!buttonHeld) {
				for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
					ViewPtr& view = _mouseOver[i];

					spdlog::info("Mouse leaving {}", view->getName());
					view->onMouseLeave();
				}

				_mouseOver.clear();
			}
		}

		bool onMouseMove(Point pos) final override {
			bool handled = false;
			DragContext& ctx = _shared->dragContext;

			if (_mouseState.buttons[MouseButton::Left]) {
				if (!ctx.isDragging) {
					// lock focus on current view
					ViewPtr view = _shared->focused ? _shared->focused->shared_from_this() : nullptr;
					if (view) {
						Rect worldArea = view->getWorldArea();

						if (_mouseOverClickedItem) {
							if (!worldArea.contains(pos)) {
								spdlog::info("Mouse leaving (held) {}", view->getName());
								view->onMouseLeave();
								_mouseOverClickedItem = false;
							}
						} else {
							if (worldArea.contains(pos)) {
								spdlog::info("Mouse entering (held) {}", view->getName());
								view->onMouseEnter(pos - worldArea.position);
								_mouseOverClickedItem = true;
							}
						}

						view->onMouseMove(pos - worldArea.position);

						handled = true;
					}
				} else {
					handled = handleDragEnterLeave(pos);
					handled |= propagateDragMove(pos, getChildren());
				}
			} else {
				handled |= handleMouseEnterLeave(pos);
				handled |= propagateMouseMove(pos);
			}

			_mouseState.position = pos;
			return handled;
		}

		bool propagateClick(MouseButton::Enum button, bool down, Point position, bool updateFocus) {
			if (updateFocus) {
				for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
					FocusPolicy policy = _mouseOver[i]->getFocusPolicy();

					if ((uint32)policy & (uint32)FocusPolicy::Click) {
						_shared->focused = _mouseOver[i].get();
						_mouseOverClickIdx = i;
						_mouseOverClickedItem = true;
						break;
					}
				}
			}
			
			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				Point childPosition = position - _mouseOver[i]->getWorldPosition();

				if (_mouseOver[i]->onMouseButton(button, down, childPosition)) {		
					return true;
				}
			}

			return false;
		}

		ViewPtr propagateDrop(Point position) {
			auto& targets = _shared->dragContext.targets;

			for (int32 i = (int32)targets.size() - 1; i >= 0; --i) {
				Point childPosition = position - targets[i]->getWorldPosition();

				if (targets[i]->onDrop(_shared->dragContext, childPosition)) {
					return targets[i];
				}
			}

			return nullptr;
		}

		template <typename T>
		bool vectorContains(const std::vector<T>& vec, const T& item) {
			return vectorIndex(vec, item) != -1;
		}

		template <typename T>
		int32 vectorIndex(const std::vector<T>& vec, const T& item) {
			for (size_t i = 0; i < vec.size(); ++i) {
				if (vec[i] == item) {
					return (int32)i;
				}
			}

			return -1;
		}

		bool propagateMouseLeave(Point position) {
			bool handled = false;

			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				ViewPtr& view = _mouseOver[i];

				if (!view->isVisible() || !view->getWorldArea().contains(position)) {
					spdlog::info("Mouse leaving {}", view->getName());
					view->onMouseLeave();
					_mouseOver.erase(_mouseOver.begin() + i);
					handled = true;
				}
			}

			return handled;
		}

		bool propagateMouseEnter(Point position, std::vector<ViewPtr>& views, std::vector<ViewPtr>& target) {
			bool handled = false;

			for (ViewPtr& view : views) {
				if (view->isVisible() && view->getWorldArea().contains(position)) {
					target.push_back(view);

					if (!vectorContains(_mouseOver, view)) {
						spdlog::info("Mouse entering {}", view->getName());
						view->onMouseEnter(position - view->getWorldPosition());
						handled = true;
					}

					handled |= propagateMouseEnter(position, view->getChildren(), target);
				}
			}

			return handled;
		}

		bool propagateDragEnter(Point position, std::vector<ViewPtr>& views, DragContext& ctx, std::vector<ViewPtr>& target) {
			bool handled = false;

			for (ViewPtr& view : views) {
				if (view != ctx.source && view->isVisible() && view->getWorldArea().contains(position)) {
					target.push_back(view);

					if (!vectorContains(ctx.targets, view)) {
						spdlog::info("Drag entering {}", view->getName());
						view->onDragEnter(ctx, position - view->getWorldPosition());
						handled = true;
					}

					handled |= propagateDragEnter(position, view->getChildren(), ctx, target);
				}
			}

			return handled;
		}

		bool propagateDragLeave(Point position) {
			bool handled = false;

			auto& ctx = _shared->dragContext;
			auto& dragOver = ctx.targets;

			for (int32 i = (int32)dragOver.size() - 1; i >= 0; --i) {
				ViewPtr& view = dragOver[i];

				if (!view->isVisible() || !view->getWorldArea().contains(position)) {
					spdlog::info("Drag leaving {}", view->getName());
					view->onDragLeave(ctx);
					dragOver.erase(dragOver.begin() + i);
					handled = true;
				}
			}

			return handled;
		}

		bool propagateMouseMove(Point position) {
			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				Point childPosition = position - _mouseOver[i]->getWorldPosition();

				if (_mouseOver[i]->onMouseMove(childPosition)) {
					return true;
				}
			}

			return false;
		}

		bool propagateDragMove(Point position, std::vector<ViewPtr>& views) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getWorldArea().contains(position)) {
					if (!propagateDragMove(position, view->getChildren())) {
						if (view->onDragMove(_shared->dragContext, position - view->getWorldPosition())) {
							return true;
						}
					}
				}
			}

			return false;
		}

		bool onDrop(const std::vector<std::string>& paths) final override { 
			return propagateDrop(paths, _mouseState.position, getChildren());
		}

		void onUpdate(f32 delta) final override {
			if (_shared->layoutDirty) {
				updateLayout();
			}
			
			propagateUpdate(getChildren(), delta);

			_shared->removals.clear();

			if (_shared->layoutDirty) {
				updateLayout();
			}
		}

		void onRender(Canvas& canvas) final override {
			propagateRender(canvas, getChildren());
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
				if (view->_sizingMode == SizingPolicy::FitToParent) {
					assert(view->getParent());
					assert(view->getParent()->getSizingPolicy() != SizingPolicy::FitToContent);

					Rect area = { {0, 0}, view->getParent()->getDimensions() };

					if (view->getArea() != area) {
						view->setArea(area);
					}
				}

				propagateSizingUpdate(view->getChildren());

				if (view->_sizingMode == SizingPolicy::FitToContent) {
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
					Rect viewWorldArea(offset + view->getPosition(), (Dimension)((DimensionF)view->getDimensions() * view->getScale()));
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

		void propagateRender(Canvas& canvas, std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					canvas.setTranslation((PointF)view->getWorldPosition());
					canvas.setScale({ view->getWorldScale(), view->getWorldScale() });

					bool clip = view->getClip();
					if (clip) {
						canvas.pushScissor((Rect)view->getWorldArea());
					}

					view->onRender(canvas);

					if (clip) {
						canvas.popScissor();
					}
				}
			}

			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					canvas.setTranslation((PointF)view->getWorldPosition());
					canvas.setScale({ view->getWorldScale(), view->getWorldScale() });

					bool clip = view->getClip();
					if (clip) {
						canvas.pushScissor((Rect)view->getWorldArea());
					}

					propagateRender(canvas, view->getChildren());

					if (clip) {
						canvas.popScissor();
					}
				}
			}
		}

		bool propagateMouseScroll(PointT<f32> delta, Point position) {
			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				Point childPosition = position - _mouseOver[i]->getWorldPosition();

				if (_mouseOver[i]->onMouseScroll(delta, childPosition)) {
					return true;
				}
			}

			return false;
		}

		bool propagateAction(Point position, std::vector<ViewPtr>& views, const std::function<bool(ViewPtr&, Point)>& f) {
			for (int32 i = (int32)views.size() - 1; i >= 0; --i) {
				ViewPtr view = views[i];

				if (view->isVisible() && view->getWorldArea().contains(position)) {
					if (!propagateAction(position, view->getChildren(), f)) {
						if (f(view, position - view->getWorldPosition())) {
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

				if (view->isVisible() && view->getWorldArea().contains(position)) {
					if (!propagateDrop(paths, position, view->getChildren())) {
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
