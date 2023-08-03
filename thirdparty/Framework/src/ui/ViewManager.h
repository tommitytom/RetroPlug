#pragma once

#include <unordered_set>
#include <spdlog/spdlog.h>

#include "foundation/StlUtil.h"
#include "graphics/Canvas.h"
#include "ui/View.h"

namespace fw {
	class ViewManager final : public View {
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
			calculateLayout();
		}

		ViewManager(Dimension dimensions): View(dimensions) {
			setType<ViewManager>();
			_shared = &_sharedData;
			calculateLayout();
		}

		bool isMounted() const override {
			return true;
		}

		void setResourceManager(ResourceManager* resourceManager, FontManager* fontManager) {
			_shared->resourceManager = resourceManager;
			_shared->fontManager = fontManager;
		}

		View::Shared& getShared() {
			return _sharedData;
		}

		const View::Shared& getShared() const {
			return _sharedData;
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

		bool onChar(const CharEvent& ev) override {
			ViewPtr current = _shared->focused.lock();

			while (current && current.get() != this) {
				if (!current->onChar(ev)) {
					current = current->getParent();
				} else {
					current->emit(ev);
					return true;
				}
			}

			return false;
		}

		void addGlobalKeyHandler() {
			
		}

		bool onKey(const KeyEvent& ev) override {
			auto it = _shared->globalKeyHandlers.begin();

			while (it != _shared->globalKeyHandlers.end()) {
				if (it->view.expired()) {
					it = _shared->globalKeyHandlers.erase(it);
				} else {
					if (it->func(ev)) {
						return true;
					}
					
					++it;
				}
			}

			ViewPtr current = _shared->focused.lock();

			while (current && current.get() != this) {
				if (!current->onKey(ev)) {
					current = current->getParent();
				} else {
					current->emit(ev);
					return true;
				}
			}

			return false;
		}

		bool onButton(const ButtonEvent& ev) override {
			ViewPtr current = _shared->focused.lock();

			while (current) {
				if (!current->onButton(ev)) {
					current = current->getParent();
				} else {
					current->emit(ev);
					return true;
				}
			}

			return false;
		}

		bool onMouseScroll(const MouseScrollEvent& ev) override {
			return propagateMouseScroll(ev);
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			bool handled = false;

			_mouseState.buttons[ev.button] = ev.down;

			if (ev.button == MouseButton::Left && !ev.down) {
				DragContext& ctx = _shared->dragContext;

				if (ctx.isDragging) {
					handleDragEnd(ev.position);
				} else {
					// Focus is locked to a single element
					// Process the mouse button release
					handled = propagateClick(ev, false);
					handled |= handleMouseEnterLeave(ev.position);
				}

				return true;
			}

			return propagateClick(ev, true);
		}

		void onMouseLeave() override {
			bool buttonHeld = false;

			for (size_t i = 0; i < MouseButton::COUNT; ++i) {
				buttonHeld |= _mouseState.buttons[i];
			}

			if (!buttonHeld) {
				for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
					ViewPtr& view = _mouseOver[i];

					spdlog::info("Mouse leaving {}", view->getName());
					view->onMouseLeave();
					view->emit(MouseLeaveEvent{});
				}

				_mouseOver.clear();
			}
		}

		bool onMouseMove(Point pos) override {
			bool handled = false;
			DragContext& ctx = _shared->dragContext;

			if (_mouseState.buttons[MouseButton::Left]) {
				if (!ctx.isDragging) {
					// lock focus on current view
					ViewPtr view = _shared->focused.lock();
					if (view) {
						Rect worldArea = view->getWorldArea();

						if (_mouseOverClickedItem) {
							if (!worldArea.contains(pos)) {
								spdlog::info("Mouse leaving (held) {}", view->getName());
								view->onMouseLeave();
								view->emit(MouseLeaveEvent{});
								_mouseOverClickedItem = false;
							}
						} else {
							if (worldArea.contains(pos)) {
								spdlog::info("Mouse entering (held) {}", view->getName());
								view->onMouseEnter(pos - worldArea.position);
								view->emit(MouseEnterEvent{ pos - worldArea.position });
								_mouseOverClickedItem = true;
							}
						}

						view->onMouseMove(pos - worldArea.position);
						view->emit(MouseMoveEvent{ pos - worldArea.position });

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



		bool onDrop(const std::vector<std::string>& paths) override {
			return propagateDrop(paths, _mouseState.position, getChildren());
		}

		void onHotReload() override {
			propagateHotReload(getChildren());
		}

		void onResize(const ResizeEvent& ev) override {
			/*switch (getSizingPolicy()) {
			case fw::SizingPolicy::None:
				break;
			case fw::SizingPolicy::FitToContent:
				break;
			case fw::SizingPolicy::FitToParent:
				_area.dimensions = ev.size;
				setLayoutDirty();
				calculateLayout();
				break;
			}*/
		}

		void onUpdate(f32 delta) override {
			if (_shared->layoutDirty) {
				calculateLayout();
			}

			propagateUpdate(getChildren(), delta);

			_shared->removals.clear();

			if (_shared->layoutDirty) {
				calculateLayout();
			}
		}

		void onRender(fw::Canvas& canvas) override {
			propagateRender(canvas, getChildren());
		}

		void printHierarchy() {
			propagatePrint(getChildren(), "");
		}

		void calculateLayout() {
			YGNodeCalculateLayout(_layout.getNode(), YGUndefined, YGUndefined, YGDirectionInherit);
			_shared->layoutDirty = true; // Forcing constant layout updates for now...
		}

	private:
		void propagatePrint(std::vector<ViewPtr>& views, std::string indent) {
			for (ViewPtr view : views) {
				spdlog::info("{}- {}", indent, view->getName());
				propagatePrint(view->getChildren(), indent + '\t');
			}
		}

		void propagateHotReload(std::vector<ViewPtr>& views) {
			for (ViewPtr view : views) {
				view->onHotReload();
				propagateHotReload(view->getChildren());
			}
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

		bool propagateClick(const MouseButtonEvent& ev, bool updateFocus) {
			if (updateFocus) {
				for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
					FocusPolicy policy = _mouseOver[i]->getFocusPolicy();

					if ((uint32)policy & (uint32)FocusPolicy::Click) {
						_mouseOver[i]->focus();
						
						_mouseOverClickIdx = i;
						_mouseOverClickedItem = true;
						
						break;
					}
				}
			}

			MouseButtonEvent childEvent = ev;

			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				childEvent.position = ev.position - _mouseOver[i]->getWorldPosition();

				if (_mouseOver[i]->onMouseButton(childEvent)) {
					_mouseOver[i]->emit(childEvent);
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

		bool propagateMouseLeave(Point position) {
			bool handled = false;

			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				ViewPtr& view = _mouseOver[i];

				if (!view->isVisible() || !view->getWorldArea().contains(position)) {
					spdlog::info("Mouse leaving {}", view->getName());

					view->onMouseLeave();
					view->emit(MouseLeaveEvent{});

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

					if (!StlUtil::vectorContains(_mouseOver, view)) {
						spdlog::info("Mouse entering {}", view->getName());

						view->onMouseEnter(position - view->getWorldPosition());
						view->emit(MouseEnterEvent{ position - view->getWorldPosition() });

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

					if (!StlUtil::vectorContains(ctx.targets, view)) {
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
					_mouseOver[i]->emit(MouseMoveEvent{ childPosition });
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

		void propagateLayoutChange(std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				view->onLayoutChanged();
				propagateLayoutChange(view->getChildren());
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

		void propagateRender(fw::Canvas& canvas, std::vector<ViewPtr>& views) {
			for (ViewPtr& view : views) {
				if (view->isVisible()) {
					FlexOverflow overflow = view->getLayout().getOverflow();
					
					canvas.setTranslation((PointF)view->getWorldPosition());
					canvas.setScale({ view->getWorldScale(), view->getWorldScale() });
					
					if (overflow != FlexOverflow::Visible) {
						canvas.pushScissor(view->getWorldArea());
					}

					view->onRender(canvas);
					propagateRender(canvas, view->getChildren());

					if (overflow != FlexOverflow::Visible) {
						canvas.popScissor();
					}
				}
			}
		}

		bool propagateMouseScroll(const MouseScrollEvent& ev) {
			MouseScrollEvent childEvent = ev;

			for (int32 i = (int32)_mouseOver.size() - 1; i >= 0; --i) {
				childEvent.position = ev.position - _mouseOver[i]->getWorldPosition();

				if (_mouseOver[i]->onMouseScroll(childEvent)) {
					_mouseOver[i]->emit(childEvent);
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

	using ViewManagerPtr = std::shared_ptr<ViewManager>;
}
