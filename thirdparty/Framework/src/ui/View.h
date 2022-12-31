#pragma once

#include <memory>
#include <vector>

#include <entt/entity/registry.hpp>
#include <spdlog/spdlog.h>

#include "foundation/Input.h"
#include "foundation/Math.h"
#include "foundation/StringUtil.h"
#include "foundation/TypeRegistry.h"

#include "graphics/Canvas.h"
#include "ui/TypeDataLookup.h"

namespace fw {
	namespace engine {
		class Canvas;
		class FontManager;
	}

	class ResourceManager;
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

	enum class CursorType {
		Arrow,
		Hand,
		IBeam,
		Crosshair,
		ResizeEW,
		ResizeNS,
		ResizeNWSE,
		ResizeNESW,
		NotAllowed,
		ResizeH = ResizeEW,
		ResizeV = ResizeNS,
	};

	using EventType = entt::id_type;

	enum class KeyAction {
		Release,
		Press,
		Repeat
	};

	struct KeyEvent {
		VirtualKey::Enum key;
		KeyAction action;
		bool down;
	};

	struct MouseScrollEvent {
		PointF delta;
		Point position;
	};

	struct MouseButtonEvent {
		MouseButton::Enum button;
		bool down;
		Point position;
	};

	struct MouseDoubleClickEvent {
		MouseButton::Enum button;
		Point position;
	};

	struct ButtonEvent {
		ButtonType::Enum button;
		bool down;
	};

	struct MouseEnterEvent {
		Point position;
	};

	struct MouseLeaveEvent {};

	struct MouseMoveEvent {
		Point position;
	};

	struct ResizeEvent {
		Dimension size;
		Dimension oldSize;
	};

	class View : public std::enable_shared_from_this<View> {
	private:
		struct Shared {
			std::weak_ptr<View> focused;
			bool layoutDirty = true;
			f32 pixelDensity = 1.0f;
			std::vector<ViewPtr> removals;

			DragContext dragContext;

			std::vector<ViewPtr> mouseOver;
			std::vector<ViewPtr> dragOver;

			TypeDataLookup state;
			TypeDataLookup themeLookup;

			FontManager* fontManager = nullptr;
			ResourceManager* resourceManager = nullptr;

			CursorType cursor = CursorType::Arrow;
			bool cursorChanged = true;
		};

		using SubscriptionHandler = std::function<void(const entt::any&)>;

		struct Subscription {
			std::weak_ptr<View> target;
			SubscriptionHandler handler;
		};

		Shared* _shared = nullptr;

		std::weak_ptr<View> _parent;
		std::vector<ViewPtr> _children;
		Rect _area;
		f32 _scale = 1.0f;
		f32 _alpha = 1.0f;
		//bool _initialized = false;
		bool _mounted = false;
		bool _visible = true;
		bool _clip = false;

		SizingPolicy _sizingMode = SizingPolicy::None;
		FocusPolicy _focusPolicy = FocusPolicy::None;

		std::string _name;
		entt::type_info _type;

		std::vector<std::pair<EventType, std::weak_ptr<View>>> _subscriptions;
		std::unordered_map<EventType, std::vector<Subscription>> _subscriptionTargets;

	public:
		View(Dimension dimensions = { 100, 100 }) : _area({}, dimensions), _type(entt::type_id<View>()) {}
		View(Dimension dimensions, entt::type_info type) : _type(type), _area({}, dimensions) {}
		~View() {
			unsubscribeAll();
		}

		void subscribe(EventType eventType, ViewPtr source, std::function<void(const entt::any&)>&& func) {
			assert(!source->hasSubscription(eventType, shared_from_this()));

			_subscriptions.push_back({ eventType, std::weak_ptr(source) });

			source->_subscriptionTargets[eventType].push_back(Subscription{
				.target = std::weak_ptr<View>(shared_from_this()),
				.handler = std::move(func)
			});
		}

		template <typename T, std::enable_if_t<std::is_empty_v<T>, bool> = true>
		EventType subscribe(ViewPtr source, std::function<void()>&& func) {
			EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, [func = std::move(func)](const entt::any& v) { func(); });
			return eventType;
		}

		template <typename T, std::enable_if_t<!std::is_empty_v<T>, bool> = true>
		EventType subscribe(ViewPtr source, std::function<void(const T&)>&& func) {
			EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, source, [func = std::move(func)](const entt::any& v) { func(entt::any_cast<const T&>(v)); });
			return eventType;
		}

		bool hasSubscription(EventType eventType, const ViewPtr& target) const {
			auto found = _subscriptionTargets.find(eventType);

			if (found != _subscriptionTargets.end()) {
				for (const Subscription& sub : found->second) {
					if (!sub.target.expired() && sub.target.lock() == target) {
						return true;
					}
				}
			}

			return false;
		}

		template <typename T>
		bool hasSubscription(const ViewPtr& target) const {
			EventType eventType = entt::type_id<T>().index();
			return hasSubscription(eventType, target);
		}

		template <typename T>
		bool unsubscribe(ViewPtr source) {
			EventType eventType = entt::type_id<T>().index();
			return unsubscribe(eventType, source);
		}

		void unsubscribeAll() {
			for (size_t i = 0; i < _subscriptions.size(); ++i) {
				auto& sub = _subscriptions[i];

				ViewPtr subPtr = sub.second.lock();
				if (subPtr) {
					auto found = subPtr->_subscriptionTargets.find(sub.first);

					if (found != subPtr->_subscriptionTargets.end()) {
						for (size_t i = 0; i < found->second.size(); ++i) {
							Subscription& sub = found->second[i];

							if (sub.target.expired() || sub.target.lock() == shared_from_this()) {
								found->second.erase(found->second.begin() + i);
								break;
							}
						}
					}
				}
			}

			_subscriptions.clear();
		}

		bool unsubscribe(EventType eventType, ViewPtr source) {
			for (size_t i = 0; i < _subscriptions.size(); ++i) {
				auto& sub = _subscriptions[i];

				if (sub.first == eventType && sub.second.lock() == source) {
					_subscriptions.erase(_subscriptions.begin() + i);
					break;
				}
			}

			auto found = source->_subscriptionTargets.find(eventType);

			if (found != source->_subscriptionTargets.end()) {
				for (size_t i = 0; i < found->second.size(); ++i) {
					Subscription& sub = found->second[i];

					if (!sub.target.expired() && sub.target.lock() == shared_from_this()) {
						found->second.erase(found->second.begin() + i);
						return true;
					}
				}
			}

			return false;
		}

		template <typename T>
		void emit(T&& ev) {
			EventType eventType = entt::type_id<T>().index();
			auto found = _subscriptionTargets.find(eventType);

			if (found != _subscriptionTargets.end() && found->second.size()) {
				entt::any v = ev;

				for (auto it = found->second.begin(); it != found->second.end();) {
					if (!it->target.expired()) {
						it->handler(v);
						++it;
					} else {
						it = found->second.erase(it);
					}
				}
			}
		}

		void setCursor(CursorType cursor) {
			_shared->cursor = cursor;
			_shared->cursorChanged = true;
		}

		void setClip(bool clip) {
			_clip = clip;
		}

		bool getClip() const {
			return _clip;
		}

		ResourceManager& getResourceManager() {
			assert(_shared);
			assert(_shared->resourceManager);
			return *_shared->resourceManager;
		}

		const ResourceManager& getResourceManager() const {
			assert(_shared);
			assert(_shared->resourceManager);
			return *_shared->resourceManager;
		}

		engine::FontManager& getFontManager() {
			assert(_shared);
			assert(_shared->fontManager);
			return *_shared->fontManager;
		}

		const engine::FontManager& getFontManager() const {
			assert(_shared);
			assert(_shared->fontManager);
			return *_shared->fontManager;
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

		virtual void onInitialize() {}

		virtual void onUpdate(f32 delta) {}

		virtual void onRender(engine::Canvas& canvas) {}

		virtual bool onButton(const ButtonEvent& ev) { return onButton(ev.button, ev.down); }

		virtual bool onButton(ButtonType::Enum button, bool down) { return false; }

		virtual bool onKey(const KeyEvent& ev) { return onKey(ev.key, ev.down); }

		virtual bool onKey(VirtualKey::Enum key, bool down) { return false; }

		virtual bool onMouseButton(const MouseButtonEvent& ev) { return onMouseButton(ev.button, ev.down, ev.position); }

		virtual bool onMouseDoubleClick(const MouseDoubleClickEvent& ev) { 
			return onMouseButton(MouseButtonEvent{
				.button = ev.button,
				.down = true,
				.position = ev.position
			}); 
		}

		virtual bool onMouseButton(MouseButton::Enum button, bool down, Point position) { return false; }

		virtual void onMouseEnter(Point pos) {}

		virtual bool onMouseMove(Point pos) { return false; }

		virtual void onMouseLeave() {}

		virtual bool onMouseScroll(const MouseScrollEvent& ev) { return onMouseScroll(ev.delta, ev.position); }

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

		virtual void onResize(const ResizeEvent& ev) {}

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
			ViewPtr parent = getParent();
			if (parent) {
				return parent->getWorldPosition() + (Point)(getPositionF() * parent->getWorldScale());
			}

			return getPosition();
		}

		f32 getWorldScale() const {
			ViewPtr parent = getParent();
			if (parent) {
				return parent->getWorldScale() * getScale();
			}

			return getScale();
		}

		Rect getWorldArea() const {
			return { getWorldPosition(), (Dimension)(getDimensionsF() * getWorldScale()) };
		}

		template <typename T>
		T* createState(T&& item) {
			spdlog::info("Creating state {}", entt::type_id<T>().name());
			if (_shared && !getState<T>()) {
				return &_shared->state.emplace<T>(std::forward<T>(item));
			}

			return nullptr;
		}

		template <typename T>
		T* createState(const T& item) {
			spdlog::info("Creating state {}", entt::type_id<T>().name());
			if (_shared && !getState<T>()) {
				return &_shared->state.emplace<T>(item);
			}

			return nullptr;
		}

		template <typename T>
		T* createState() {
			spdlog::info("Creating state {}", entt::type_id<T>().name());
			if (_shared && !getState<T>()) {
				return &_shared->state.emplace<T>();
			}

			return nullptr;
		}

		template <typename T>
		T* getState() {
			return _shared->state.tryGet<T>();
		}

		TypeDataLookup& getState() {
			return _shared->state;
		}

		const TypeDataLookup& getState() const {
			return _shared->state;
		}

		template <typename ThemeT>
		const ThemeT& getTheme() {
			assert(_shared);
			return _shared->themeLookup.getOrEmplace<ThemeT>();
		}

		ViewPtr getFocused() const {
			if (_shared) {
				return _shared->focused.lock();
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
			ViewPtr parent = getParent();
			if (parent) {
				parent->removeChild(shared_from_this());
			}
		}

		void focus() {
			assert(_shared);
			if (_shared) {
				_shared->focused = shared_from_this();
			}
		}

		void unfocus() {
			assert(_shared);
			if (_shared && hasFocus()) {
				_shared->focused.reset();
			}
		}

		bool hasFocus() const {
			return _shared && _shared->focused.lock() == shared_from_this();
		}

		void bringToFront() {
			ViewPtr parent = getParent();
			assert(parent);
			std::vector<ViewPtr>& children = parent->getChildren();

			for (size_t i = 0; i < children.size(); ++i) {
				if (children[i].get() == this) {
					children.erase(children.begin() + i);
					children.push_back(this->shared_from_this());
					break;
				}
			}
		}

		void pushToBack() {
			ViewPtr parent = getParent();
			assert(parent);
			std::vector<ViewPtr>& children = parent->getChildren();

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

		template <typename T>
		std::shared_ptr<T> addChild(std::shared_ptr<T> view) {
			addChild(std::static_pointer_cast<View>(view));
			return view;
		}

		ViewPtr addChild2(ViewPtr view) {
			return addChild(view);
		}

		ViewPtr addChild(ViewPtr view) {
			if (!view->_parent.expired()) {
				view->_parent.lock()->removeChild(view);
			}

			view->_parent = std::weak_ptr<View>(shared_from_this());
			_children.push_back(view);
			
			if (isInitialized()) {
				initializeRecursive(view);

				if (isMounted()) {
					mountRecursive(view);
				}
			}

			setLayoutDirty();

			return view;
		}

		bool isInitialized() const {
			return _shared != nullptr;
		}

		virtual bool isMounted() const {
			return _mounted;
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

		/*void removeChild(View* view) {

		}*/

		void removeChild(ViewPtr view) {
			for (size_t i = 0; i < _children.size(); ++i) {
				if (_children[i] == view) {
					if (_shared) {
						ViewPtr focused = _shared->focused.lock();

						if (focused && childHasFocus(view.get(), focused.get())) {
							focused = shared_from_this();
						}
					}

					_children.erase(_children.begin() + i);
					
					onChildRemoved(view);
					view->onDismount();

					view->_parent.reset();
					//view->_shared = nullptr;

					setLayoutDirty();

					break;
				}
			}

			if (_shared) {
				_shared->removals.push_back(view);
			}
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

		void setScale(f32 scale) {
			_scale = scale;

			for (ViewPtr& view : getChildren()) {
				view->onScaleChanged(scale);
			}

			setLayoutDirty();
		}

		Point getPosition() const {
			return _area.position;
		}

		PointF getPositionF() const {
			return (PointF)_area.position;
		}

		f32 getScale() const {
			return _scale;
		}

		void setDimensions(Dimension dimensions) {
			assert(dimensions.w >= 0 && dimensions.h >= 0);
			if (dimensions != _area.dimensions) {
				ResizeEvent ev = {
					.size = dimensions,
					.oldSize = _area.dimensions
				};

				onResize(ev);
				emit(ev);

				_area.dimensions = dimensions;
				setLayoutDirty();
			}
		}

		Dimension getDimensions() const {
			return _area.dimensions;
		}

		DimensionF getDimensionsF() const {
			return (DimensionF)_area.dimensions;
		}

		ViewPtr getParent() const {
			return _parent.lock();
		}

		ViewPtr getChild(size_t idx) {
			return _children[idx];
		}

		const Rect& getArea() const {
			return _area;
		}

		void setArea(const Rect& area) {
			assert(area.w >= 0 && area.h >= 0);

			if (area != _area) {
				ResizeEvent ev = {
					.size = area.dimensions,
					.oldSize = _area.dimensions
				};

				onResize(ev);
				emit(ev);

				_area = area;
				setLayoutDirty();
			}
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
			ViewPtr currentParent = getParent();

			assert(parent != shared_from_this());
			assert(currentParent != nullptr);

			while (currentParent != parent) {
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

			if (_name.empty()) {
				_name = StringUtil::formatClassName(_type.name());
			}
		}

	private:
		void initializeRecursive(ViewPtr view) {
			assert(isInitialized());

			if (!view->_shared) {
				view->_shared = _shared;
				view->onInitialize();
			}

			onChildAdded(view);

			for (ViewPtr& child : view->getChildren()) {
				if (!child->isInitialized()) {
					view->initializeRecursive(child);
				}
			}
		}

		void mountRecursive(ViewPtr view) {
			if (!view->isMounted()) {
				view->onMount();
			}

			for (ViewPtr& child : view->getChildren()) {
				if (!child->isMounted()) {
					view->mountRecursive(child);
				}
			}
		}

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
