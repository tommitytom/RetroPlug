#pragma once

#include <memory>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/core/type_info.hpp>
#include <entt/core/any.hpp>

#include "core/Input.h"
#include "RpMath.h"

struct NVGcontext;
struct NVGcolor;

namespace rp {
	class Menu;
	class View;
	using ViewPtr = std::shared_ptr<View>;

	using ViewIndex = uint32;
	constexpr ViewIndex INVALID_VIEW_INDEX = -1;

	enum class SizingMode {
		None,
		FitToParent,
		FitToContent
	};

	class View : public std::enable_shared_from_this<View> {
	private:
		struct Shared {
			View* focused = nullptr;
			bool layoutDirty = true;
			f32 scale = 1.0f;
			std::vector<ViewPtr> removals;

			entt::registry userData;
		};

		Shared* _shared = nullptr;

		View* _parent = nullptr;
		std::vector<ViewPtr> _children;
		Rect<uint32> _area;
		f32 _alpha = 1.0f;
		NVGcontext* _vg = nullptr;

		SizingMode _sizingMode = SizingMode::None;

		std::string _name;
		entt::type_info _type;

	public:
		View(Dimension<uint32> dimensions = { 100, 100 }) : _area({}, dimensions) {
			_type = entt::type_id<View>();
		}

		View(Dimension<uint32> dimensions, entt::type_info type) : _type(type), _area({}, dimensions) {}

		~View() {
			if (_shared) {
				unfocus();
			}
			
			removeChildren(); 
		}

		virtual void onInitialized() {}

		virtual void onUpdate(f32 delta) {}

		virtual void onRender() {}

		virtual bool onButton(ButtonType::Enum button, bool down) { return false; }

		virtual bool onKey(VirtualKey::Enum key, bool down) { return false; }

		virtual bool onMouseButton(MouseButton::Enum button, bool down, Point<uint32> position) { return false; }

		virtual bool onMouseMove(Point<uint32> pos) { return false; }

		virtual bool onMouseScroll(Point<f32> delta, Point<uint32> position) { return false; }

		virtual void onChildRemoved(ViewPtr view) {}

		virtual void onChildAdded(ViewPtr view) {}

		virtual void onLayoutChanged() {}

		virtual void onResize() {}

		virtual bool onDrop(const std::vector<std::string>& paths) { return false; }

		virtual void onMenu(Menu& menu) {}

		virtual void onScaleChanged(f32 scale) {}

		f32 getScalingFactor() const {
			if (_shared) {
				return _shared->scale;
			}
			
			return 1.0f;
		}

		template <typename T>
		T* createShared(T&& item) {
			if (_shared && !getShared<T>()) {
				return &_shared->userData.set<T>(std::forward(item));
			}

			return nullptr;
		}

		template <typename T>
		T* createShared() {
			if (_shared && !getShared<T>()) {
				return &_shared->userData.set<T>();
			}

			return nullptr;
		}

		template <typename T>
		T* getShared() {
			if (_shared) {
				return _shared->userData.try_ctx<T>();
			}

			return nullptr;
		}

		View* getFocused() const {
			if (_shared) {
				return _shared->focused;
			}

			return nullptr;
		}

		void setSizingMode(SizingMode mode) {
			_sizingMode = mode;
			setLayoutDirty();
		}

		SizingMode getSizingMode() const {
			return _sizingMode;
		}

		entt::type_info getType() const {
			return _type;
		}

		void remove() {
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

		template <typename T>
		std::shared_ptr<T> addChildAt(std::string_view name, const Rect<uint32>& area) {
			std::shared_ptr<T> child = addChild<T>(name);
			child->setArea(area);
			return std::move(child);
		}

		template <typename T>
		std::shared_ptr<T> addChildAt(std::string_view name, const Point<uint32>& position) {
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
			view->_parent = this;
			view->setShared(_shared);
			view->setVg(_vg);

			_children.push_back(view);
			onChildAdded(view);
			view->onInitialized();

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
			if (_shared) {
				_shared->removals.push_back(view);
			} else {
				for (size_t i = 0; i < _children.size(); ++i) {
					if (_children[i] == view) {
						_children.erase(_children.begin() + i);
					}
				}
			}
		}

		void removeChildren() {
			for (int32 i = (int32)_children.size() - 1; i >= 0; --i) {
				removeChild(_children[i]);
			}
		}

		template <typename T>
		void forEach(bool recurse, T&& callback) {
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
		void findChildren(std::vector<T*>& target, bool recursive = false) {
			for (ViewPtr& child : _children) {
				if (child->isType<T>()) {
					target.push_back(child->asRaw<T>());
				}

				if (recursive) {
					child->findChildren<T>(target, true);
				}
			}
		}

		template <typename T>
		void findChildren(std::vector<std::shared_ptr<T>>& target, bool recursive = false) {
			for (ViewPtr& child : _children) {
				if (child->isType<T>()) {
					target.push_back(child->asShared<T>());
				}

				if (recursive) {
					child->findChildren<T>(target, true);
				}
			}
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

		void setPosition(uint32 x, uint32 y) {
			setPosition({ x, y });
		}

		void setPosition(Point<uint32> pos) {
			if (pos != _area.position) {
				_area.position = pos;
				setLayoutDirty();
			}
		}

		Point<uint32> getPosition() const {
			return _area.position;
		}
		
		void setDimensions(Dimension<uint32> dimensions) {
			if (dimensions != _area.dimensions) {
				_area.dimensions = dimensions;
				setLayoutDirty();
				onResize();
			}
		}
		
		Dimension<uint32> getDimensions() const {
			return _area.dimensions;
		}

		View* getParent() const {
			return _parent;
		}

		ViewPtr getChild(size_t idx) {
			return _children[idx];
		}

		const Rect<uint32>& getArea() const {
			return _area;
		}

		void setArea(const Rect<uint32>& area) {
			_area = area;
			setLayoutDirty();
		}

		void setAlpha(f32 alpha) {
			_alpha = alpha;
		}

		f32 getAlpha() const {
			return _alpha;
		}

		NVGcontext* getVg() {
			return _vg;
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

		void setVg(NVGcontext* vg) {
			_vg = vg;

			for (ViewPtr& view : _children) {
				view->setVg(vg);
			}
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


		friend class ViewManager;
	};
}
