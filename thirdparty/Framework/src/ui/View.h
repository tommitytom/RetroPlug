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
#include "ui/ViewLayout.h"

using namespace fw::literals;

namespace fw {
	namespace engine {
		class Canvas;
		class FontManager;
	}

	class ResourceManager;
	class Menu;
	class View;
	using ViewPtr = std::shared_ptr<View>;
	using ConstViewPtr = std::shared_ptr<const View>;

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
		Point sourcePoint;
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

	struct ResizeEvent {
		Dimension size;
		Dimension oldSize;
	};

	class Object : public std::enable_shared_from_this<Object> {
	public:
		virtual uint32 getTypeId() const = 0;

		virtual std::string_view getTypeName() const = 0;

		template <typename T = Object>
		std::shared_ptr<T> sharedFromThis() {
			return std::static_pointer_cast<T>(shared_from_this());
		}

		template <typename T = Object>
		std::shared_ptr<const T> sharedFromThis() const {
			return std::static_pointer_cast<const T>(shared_from_this());
		}
	};
	
#define RegisterObject() \
	uint32 getTypeId() const override { return entt::type_hash<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); } \
	std::string_view getTypeName() const override { return entt::type_name<std::remove_const_t<std::remove_pointer_t<std::decay_t<decltype(this)>>>>::value(); }

	class View : public Object {
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

			fw::FontManager* fontManager = nullptr;
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
		f32 _scale = 1.0f;
		f32 _alpha = 1.0f;
		//bool _initialized = false;
		bool _mounted = false;
		bool _visible = true;
		bool _clip = false;

		FocusPolicy _focusPolicy = FocusPolicy::None;

		std::string _name;
		entt::type_info _type;

		std::vector<std::pair<EventType, std::weak_ptr<View>>> _subscriptions;
		std::unordered_map<EventType, std::vector<Subscription>> _subscriptionTargets;

		ViewLayout _layout;

	public:
		RegisterObject()

		View(Dimension dimensions = { 100, 100 }) : _type(entt::type_id<View>()), _layout(dimensions) {}
		View(Dimension dimensions, entt::type_info type) : _layout(dimensions), _type(type) {}
		~View() { unsubscribeAll(); }

		void subscribe(EventType eventType, ViewPtr source, std::function<void(const entt::any&)>&& func) {
			assert(!source->hasSubscription(eventType, sharedFromThis<View>()));

			_subscriptions.push_back({ eventType, std::weak_ptr(source) });

			source->_subscriptionTargets[eventType].push_back(Subscription{
				.target = std::weak_ptr<View>(sharedFromThis<View>()),
				.handler = std::move(func)
			});
		}

		template <typename T, std::enable_if_t<std::is_empty_v<T>, bool> = true>
		EventType subscribe(ViewPtr source, std::function<void()>&& func) {
			EventType eventType = entt::type_id<T>().index();
			subscribe(eventType, source, [func = std::move(func)](const entt::any& v) { func(); });
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

							if (sub.target.expired() || sub.target.lock() == sharedFromThis<View>()) {
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

					if (!sub.target.expired() && sub.target.lock() == sharedFromThis<View>()) {
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

		fw::FontManager& getFontManager() {
			assert(_shared);
			assert(_shared->fontManager);
			return *_shared->fontManager;
		}

		const fw::FontManager& getFontManager() const {
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
			return _visible && _layout.getCalculatedDimensions().w > 0 && _layout.getCalculatedDimensions().h > 0;
		}

		virtual void onInitialize() {}

		virtual void onUpdate(f32 delta) {}

		virtual void onRender(fw::Canvas& canvas) {}

		virtual bool onButton(const ButtonEvent& ev) { return onButton(ev.button, ev.down); }

		virtual bool onButton(ButtonType::Enum button, bool down) { return false; }

		virtual bool onChar(const CharEvent& ev) { return false; }

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

		virtual void onFocus() {}
		
		virtual void onLostFocus() {}

		virtual void onHotReload() {}

		void beginDrag(ViewPtr placeholder, Point sourcePos = Point()) {
			_shared->dragContext.isDragging = true;
			_shared->dragContext.source = sharedFromThis<View>();
			_shared->dragContext.attached = placeholder;
			_shared->dragContext.sourcePoint = sourcePos;
			spdlog::info("Beginning drag of {}", getName());
		}

		Point getWorldPosition() const {
			ViewPtr parent = getParent();
			if (parent) {
				return parent->getWorldPosition() + (Point)(getPositionF() * parent->getWorldScale());
			}

			return getPosition();
		}

		PointF getWorldPositionF() const {
			ViewPtr parent = getParent();
			if (parent) {
				return parent->getWorldPositionF() + (getPositionF() * parent->getWorldScale());
			}

			return getPositionF();
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
		T* tryGetState() {
			assert(_shared);
			return _shared->state.tryGet<T>();
		}

		template <typename T>
		const T* tryGetState() const {
			assert(_shared);
			return _shared->state.tryGet<T>();
		}

		entt::any* tryGetState(entt::id_type type) {
			assert(_shared);
			return _shared->state.tryGet(type);
		}

		template <typename T>
		T& createState() {
			assert(_shared && !tryGetState<T>());
			spdlog::debug("Creating state {}", entt::type_id<T>().name());
			return _shared->state.emplace<T>();
		}

		template <typename T>
		T& createState(T&& item) {
			assert(_shared && !tryGetState<T>());
			spdlog::debug("Creating state {}", entt::type_id<T>().name());
			return _shared->state.emplace<T>(std::forward<T>(item));
		}

		template <typename T>
		T& createState(const T& item) {
			assert(_shared && !tryGetState<T>());
			spdlog::debug("Creating state {}", entt::type_id<T>().name());
			return _shared->state.emplace<T>(item);
		}

		entt::any& createState(entt::any&& item) {
			assert(_shared && !tryGetState(item.type().index()));
			spdlog::debug("Creating state {}", item.type().name());
			return _shared->state.emplace(std::forward<entt::any>(item));
		}

		template <typename T>
		T& getState() {
			assert(_shared);
			return _shared->state.get<T>();
		}

		template <typename T>
		const T& getState() const {
			assert(_shared);
			return _shared->state.get<T>();
		}

		TypeDataLookup& getStateLookup() {
			return _shared->state;
		}

		const TypeDataLookup& getStateLookup() const {
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

		entt::type_info getType() const {
			return _type;
		}

		void remove() {
			ViewPtr parent = getParent();
			if (parent) {
				parent->removeChild(sharedFromThis<View>());
			}
		}

		void focus() {
			assert(_shared);
			if (_shared && !hasFocus()) {
				ViewPtr currentFocus = _shared->focused.lock();

				if (currentFocus) {
					currentFocus->unfocus();
				}

				_shared->focused = sharedFromThis<View>();
				onFocus();
			}
		}

		void unfocus() {
			assert(_shared);
			if (_shared && hasFocus()) {
				_shared->focused.reset();
				onLostFocus();
			}
		}

		bool hasFocus() const {
			return _shared && _shared->focused.lock() == sharedFromThis<View>();
		}

		void bringToFront() {
			ViewPtr parent = getParent();
			assert(parent);
			std::vector<ViewPtr>& children = parent->getChildren();

			for (size_t i = 0; i < children.size(); ++i) {
				if (children[i].get() == this) {
					children.erase(children.begin() + i);
					children.push_back(this->sharedFromThis<View>());
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
					children.insert(children.begin(), this->sharedFromThis<View>());
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
			child->getLayout().setPositionEdge(FlexEdge::Left, (f32)position.x);
			child->getLayout().setPositionEdge(FlexEdge::Top, (f32)position.y);
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

			uint32 idx = (uint32)_children.size();
			
			YGNodeInsertChild(_layout.getNode(), view->getLayout().getNode(), idx);

			view->_parent = std::weak_ptr<View>(sharedFromThis<View>());
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
							focused = sharedFromThis<View>();
						}
					}

					YGNodeRemoveChild(_layout.getNode(), view->getLayout().getNode());

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

		void setLayout(ViewLayout&& layout) {
			_layout = std::move(layout);
		}

		ViewLayout& getLayout() {
			return _layout;
		}

		const ViewLayout& getLayout() const {
			return _layout;
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

		void setScale(f32 scale) {
			_scale = scale;

			for (ViewPtr& view : getChildren()) {
				view->onScaleChanged(scale);
			}

			setLayoutDirty();
		}

		f32 getScale() const {
			return _scale;
		}

		ViewPtr getParent() const {
			return _parent.lock();
		}

		ViewPtr getChild(size_t idx) const {
			return _children[idx];
		}

		void setArea(const Rect& area) {
			assert(area.w >= 0 && area.h >= 0);
			
			getLayout().setFlexPositionType(FlexPositionType::Absolute);
			getLayout().setPositionEdge(FlexEdge::Left, (f32)area.x);
			getLayout().setPositionEdge(FlexEdge::Top, (f32)area.y);
			getLayout().setWidth((f32)area.w);
			getLayout().setHeight((f32)area.x);

			/*if (area != _area) {
				ResizeEvent ev = {
					.size = area.dimensions,
					.oldSize = _area.dimensions
				};

				onResize(ev);
				emit(ev);

				_area = area;
				setLayoutDirty();
			}*/
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
			return std::static_pointer_cast<T>(sharedFromThis<View>());
		}

		void setName(std::string_view name) {
			_name = std::string(name);
		}

		const std::string& getName() const {
			return _name;
		}

		Point getRelativePosition(ViewPtr& parent, Point position) {
			ViewPtr currentParent = getParent();

			assert(parent != sharedFromThis<View>());
			assert(currentParent != nullptr);

			while (currentParent != parent) {
				position += currentParent->getPosition();
				currentParent = currentParent->getParent();
				assert(currentParent);
			}

			return position;
		}

		PointF getPositionF() const {
			return _layout.getCalculatedPosition();
		}

		Point getPosition() const {
			return Point(_layout.getCalculatedPosition());
		}

		DimensionF getDimensionsF() const {
			return _layout.getCalculatedDimensions();
		}

		Dimension getDimensions() const {
			return Dimension(_layout.getCalculatedDimensions());
		}

		Rect getArea() const {
			return Rect(_layout.getCalculatedArea());
		}

		RectF getAreaF() const {
			return _layout.getCalculatedArea();
		}

	protected:
		template <typename T>
		void setType() {
			_type = entt::type_id<T>();

			if (_name.empty()) {
				_name = StringUtil::formatClassName(_type.name());
			}
		}

		const Shared* getShared() const {
			return _shared;
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
				view->_mounted = true;
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

REFL_AUTO(
	type(fw::View),
	func(getName, property("name")), func(setName, property("name")),
	func(getAlpha, property("alpha")), func(setAlpha, property("alpha")),
	func(getLayout, property("layout")), func(setLayout, property("layout"))
)
