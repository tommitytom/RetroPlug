#pragma once

#include <memory>
#include <vector>
#include <spdlog/spdlog.h>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/core/type_info.hpp>
#include <entt/core/any.hpp>

#include "core/Input.h"
#include "RpMath.h"

struct NVGcontext;
struct NVGcolor;

namespace rp {
	class Canvas;
	class Menu;
	class View;
	using ViewPtr = std::shared_ptr<View>;

	using ViewIndex = uint32;
	constexpr ViewIndex INVALID_VIEW_INDEX = -1;

	enum class SizingPolicy {
		None,
		FitToParent,
		FitToContent
	};

	enum class FocusPolicy {
		None = 0x0,
		Tab = 0x1,
		Click = 0x2,
		Strong = Tab | Click | 0x8,
		Wheel = Strong | 0x4
	};

	struct DragContext {
		bool isDragging = false;
		ViewPtr source;
		ViewPtr attached;
		std::vector<ViewPtr> targets;
	};

	class View : public std::enable_shared_from_this<View> {
	private:
		struct Shared {
			View* focused = nullptr;
			bool layoutDirty = true;
			f32 scale = 1.0f;
			std::vector<ViewPtr> removals;

			DragContext dragContext;

			std::vector<ViewPtr> mouseOver;
			std::vector<ViewPtr> dragOver;

			entt::registry userData;
		};

		Shared* _shared = nullptr;

		View* _parent = nullptr;
		std::vector<ViewPtr> _children;
		Rect _area;
		f32 _alpha = 1.0f;
		bool _initialized = false;
		bool _visible = true;

		SizingPolicy _sizingMode = SizingPolicy::None;
		FocusPolicy _focusPolicy = FocusPolicy::None;

		std::string _name;
		entt::type_info _type;

	public:
		View(DimensionT<int32> dimensions = { 100, 100 }) : _area({}, dimensions), _type(entt::type_id<View>()) {}
		View(DimensionT<int32> dimensions, entt::type_info type) : _type(type), _area({}, dimensions) {}

		~View() {
			if (_shared) {
				unfocus();
			}
			
			removeChildren(); 
		}

		FocusPolicy getFocusPolicy() const {
			return _focusPolicy;
		}

		void setFocusPolicy(FocusPolicy focusPolicy) {
			_focusPolicy = focusPolicy;
		}

		const DragContext& getDragContext() const {
			assert(_shared);
			return _shared->dragContext;
		}

		void setVisible(bool visible) {
			_visible = visible;

			if (!visible) {
				// TODO: Remove mouse/drag state and call onMouseLeave/onDragLeave as necessary
				unfocus();
			}
		}

		bool isVisible() const {
			return _visible && getDimensions().w > 0 && getDimensions().h > 0;
		}

		virtual void onInitialized() {}

		virtual void onUpdate(f32 delta) {}

		virtual void onRender(Canvas& canvas) {}

		virtual bool onButton(ButtonType::Enum button, bool down) { return false; }

		virtual bool onKey(VirtualKey::Enum key, bool down) { return false; }

		virtual bool onMouseButton(MouseButton::Enum button, bool down, Point position) { return false; }

		virtual void onMouseEnter(Point pos) {}

		virtual bool onMouseMove(Point pos) { return false; }

		virtual void onMouseLeave() {}

		virtual bool onMouseScroll(PointF delta, Point position) { return false; }

		// Called on a view that is being dragged
		virtual void onDragStart() {}

		// Called on a view that is being dragged
		virtual void onDragFinish(DragContext& ctx) {}

		virtual void onDragEnter(DragContext& ctx, Point position) {}

		virtual bool onDragMove(DragContext& ctx, Point position) { return false; } 

		virtual void onDragLeave(DragContext& ctx) {}
		
		virtual bool onDrop(DragContext& ctx, Point position) { return false; }

		virtual void onChildRemoved(ViewPtr view) {}

		virtual void onChildAdded(ViewPtr view) {}

		virtual void onLayoutChanged() {}

		virtual void onResize(uint32 w, uint32 h) {}

		virtual bool onDrop(const std::vector<std::string>& paths) { return false; }

		virtual void onMenu(Menu& menu) {}

		virtual void onScaleChanged(f32 scale) {}

		virtual void onMount() {}

		virtual void onDismount() {}

		void beginDrag(ViewPtr placeholder) {
			_shared->dragContext.isDragging = true;
			_shared->dragContext.source = shared_from_this();
			_shared->dragContext.attached = placeholder;
			spdlog::info("Beginning drag of {}", getName());
		}

		Point getWorldPosition() const {
			if (getParent()) {
				return getParent()->getWorldPosition() + getPosition();
			}

			return getPosition();
		}

		Rect getWorldArea() const {
			return { getWorldPosition(), getDimensions() };
		}

		f32 getScalingFactor() const {
			if (_shared) {
				return _shared->scale;
			}
			
			return 1.0f;
		}

		template <typename T>
		T* createShared(T&& item) {
			if (_shared && !getShared<T>()) {
				return &_shared->userData.ctx().emplace<T>(std::forward(item));
			}

			return nullptr;
		}

		template <typename T>
		T* createShared() {
			if (_shared && !getShared<T>()) {
				return &_shared->userData.ctx().emplace<T>();
			}

			return nullptr;
		}

		template <typename T>
		T* getShared() {
			if (_shared) {
				return _shared->userData.ctx().find<T>();
			}

			return nullptr;
		}

		View* getFocused() const {
			if (_shared) {
				return _shared->focused;
			}

			return nullptr;
		}

		void setSizingPolicy(SizingPolicy mode) {
			_sizingMode = mode;
			setLayoutDirty();
		}

		SizingPolicy getSizingPolicy() const {
			return _sizingMode;
		}

		entt::type_info getType() const {
			return _type;
		}

		void remove() {
			assert(getParent());
			getParent()->removeChild(shared_from_this());
		}
		
		void focus() {
			assert(_shared);
			if (_shared) {
				_shared->focused = this;
			}
		}

		void unfocus() {
			assert(_shared);
			if (_shared && hasFocus()) {
				_shared->focused = nullptr;
			}
		}

		bool hasFocus() const {
			return _shared && _shared->focused == this;
		}

		void bringToFront() {
			assert(getParent());
			std::vector<ViewPtr>& children = getParent()->getChildren();

			for (size_t i = 0; i < children.size(); ++i) {
				if (children[i].get() == this) {
					children.erase(children.begin() + i);
					children.push_back(this->shared_from_this());
					break;
				}
			}
		}

		void pushToBack() {
			assert(getParent());
			std::vector<ViewPtr>& children = getParent()->getChildren();

			for (size_t i = 0; i < children.size(); ++i) {
				if (children[i].get() == this) {
					children.erase(children.begin() + i);
					children.insert(children.begin(), this->shared_from_this());
					break;
				}
			}
		}

		template <typename T>
		std::shared_ptr<T> addChildAt(std::string_view name, const Rect& area) {
			std::shared_ptr<T> child = addChild<T>(name);
			child->setArea(area);
			return std::move(child);
		}

		template <typename T>
		std::shared_ptr<T> addChildAt(std::string_view name, const Point& position) {
			std::shared_ptr<T> child = addChild<T>(name);
			child->setPosition(position);
			return std::move(child);
		}

		template <typename T>
		std::shared_ptr<T> addChild(std::string_view name) {
			std::shared_ptr<T> view = std::make_shared<T>();
			view->setName(name);
			addChild(std::static_pointer_cast<View>(view));
			return view;
		}

		ViewPtr addChild(ViewPtr view) {
			if (view->_parent) {
				view->_parent->removeChild(view);
			}

			view->_parent = this;
			view->setShared(_shared);

			if (!view->_initialized) {
				view->onInitialized();
				view->_initialized = true;
			}

			_children.push_back(view);
			onChildAdded(view);

			view->onMount();

			setLayoutDirty();

			return view;
		}

		size_t getChildIndex(ViewPtr view) const {
			for (size_t i = 0; i < _children.size(); ++i) {
				if (_children[i] == view) {
					return i;
				}
			}

			return -1;
		}

		template <typename T>
		void removeChild() {
			ViewPtr found = findChild<T>();
			if (found) {
				removeChild(found);
			}
		}

		void removeChild(ViewPtr view) {
			for (size_t i = 0; i < _children.size(); ++i) {
				if (_children[i] == view) {
					if (_shared && _shared->focused && childHasFocus(view.get(), _shared->focused)) {
						_shared->focused = this;
					}

					onChildRemoved(view);
					view->onDismount();

					_children.erase(_children.begin() + i);

					view->_parent = nullptr;
					view->_shared = nullptr;

					setLayoutDirty();

					break;
				}
			}

			/*if (_shared) {
				_shared->removals.push_back(view);
			} else {
				
			}*/
		}

		void removeChildren() {
			for (int32 i = (int32)_children.size() - 1; i >= 0; --i) {
				removeChild(_children[i]);
			}
		}

		template <typename Func>
		void forEach(bool recurse, Func&& callback) {
			for (size_t i = 0; i < _children.size(); ++i) {
				callback(_children[i], (ViewIndex)i);

				if (recurse) {
					_children[i]->forEach(recurse, callback);
				}
			}
		}

		template <typename T>
		std::shared_ptr<T> findChild(bool recursive = false) {
			for (ViewPtr& child : _children) {
				if (child->isType<T>()) {
					return child->asShared<T>();
				}
			}

			if (recursive) {
				for (ViewPtr& child : _children) {
					std::shared_ptr<T> found = child->findChild<T>();

					if (found != nullptr) {
						return found;
					}
				}
			}

			return nullptr;
		}

		template <typename T>
		bool findChildren(std::vector<T*>& target, bool recursive = false) {
			bool found = false;

			for (ViewPtr& child : _children) {
				if (child->isType<T>()) {
					target.push_back(child->asRaw<T>());
					found = true;
				}

				if (recursive) {
					found |= child->findChildren<T>(target, true);
				}
			}

			return found;
		}

		template <typename T>
		bool findChildren(std::vector<std::shared_ptr<T>>& target, bool recursive = false) {
			bool found = false;

			for (ViewPtr& child : _children) {
				if (child->isType<T>()) {
					target.push_back(child->asShared<T>());
					found = true;
				}

				if (recursive) {
					found |= child->findChildren<T>(target, true);
				}
			}

			return found;
		}

		std::vector<ViewPtr>& getChildren() {
			return _children;
		}

		const std::vector<ViewPtr>& getChildren() const {
			return _children;
		}

		void setLayoutDirty() {
			if (_shared) {
				_shared->layoutDirty = true;
			}
		}

		void setPosition(int32 x, int32 y) {
			setPosition({ x, y });
		}

		void setPosition(Point pos) {
			if (pos != _area.position) {
				_area.position = pos;
				setLayoutDirty();
			}
		}

		Point getPosition() const {
			return _area.position;
		}
		
		void setDimensions(Dimension dimensions) {
			assert(dimensions.w >= 0 && dimensions.h >= 0);
			if (dimensions != _area.dimensions) {
				_area.dimensions = dimensions;
				setLayoutDirty();
				onResize(dimensions.w, dimensions.h);
			}
		}
		
		Dimension getDimensions() const {
			return _area.dimensions;
		}

		View* getParent() const {
			return _parent;
		}

		ViewPtr getChild(size_t idx) {
			return _children[idx];
		}

		const Rect& getArea() const {
			return _area;
		}

		void setArea(const Rect& area) {
			_area = area;
			setLayoutDirty();
		}

		void setAlpha(f32 alpha) {
			_alpha = alpha;
		}

		f32 getAlpha() const {
			return _alpha;
		}

		template <typename T>
		bool isType() const {
			return _type == entt::type_id<T>();
		}

		template <typename T>
		T* asRaw() {
			assert(isType<T>());
			return (T*)this;
		}

		template <typename T>
		std::shared_ptr<T> asShared() {
			assert(isType<T>());
			return std::static_pointer_cast<T>(shared_from_this());
		}

		void setName(std::string_view name) {
			_name = std::string(name);
		}

		const std::string& getName() const {
			return _name;
		}

		Point getReleativePosition(ViewPtr& parent, Point position) {
			assert(parent != shared_from_this());
			assert(getParent() != nullptr);

			View* currentParent = getParent();

			while (currentParent != parent.get()) {
				position += currentParent->getPosition();
				currentParent = currentParent->getParent();

				assert(currentParent);
			}

			return position;
		}

	protected:
		template <typename T>
		void setType() {
			_type = entt::type_id<T>();
		}

	private:
		void setShared(Shared* shared) {
			_shared = shared;

			for (ViewPtr& view : _children) {
				view->setShared(shared);
			}
		}

		bool childHasFocus(const View* view, const View* focused) {
			if (view == focused) {
				return true;
			}

			for (const ViewPtr& child : view->getChildren()) {
				if (childHasFocus(child.get(), focused)) {
					return true;
				}
			}

			return false;
		}

		friend class ViewManager;
	};
}
