#include "LuaReact.h"

#include "ui/next/Document.h"
#include "ui/next/StyleComponentsMeta.h"
#include "ui/next/StyleUtil.h"

namespace fw {
	f64 getTime() {
		std::chrono::high_resolution_clock::duration time = std::chrono::high_resolution_clock::now().time_since_epoch();
		return std::chrono::duration_cast<std::chrono::duration<f64>>(time).count();
	}
	
	LuaReact::LuaReact(FontManager& fontManager, const std::filesystem::path& path) : _doc(fontManager), _path(path) {
		_listener.setCallback([&](FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			if (filename.ends_with(".css")) {
				_doc.loadStyle("E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\test.css");
			} else {
				reload();
			}
		});

		std::string root = std::filesystem::path(_path).remove_filename().string();
		_fileWatcher.addWatch(root, &_listener, true);

		reload();
	}
	
	LuaReact::~LuaReact() {
		if (_lua) {
			lua_close(_lua);
		}
	}

	void LuaReact::update(f32 dt) {
		_fileWatcher.update();
		_listener.update(dt);

		if (_lua) {
			luabridge::LuaRef cb = luabridge::getGlobal(_lua, "onFrame");
			luabridge::LuaResult result = cb(dt);

			if (!result) {
				spdlog::error(result.errorMessage());
			}
		}

		_doc.update(dt);
	}

	void LuaReact::reload() {
		if (_lua) {
			lua_close(_lua);
		}

		_doc.clear();

		_lua = luaL_newstate();
		luaL_openlibs(_lua);

		LuaUtil::reflectStyleComponents(_lua);

		luabridge::getGlobalNamespace(_lua)
			.beginNamespace("fw")
				.addFunction("getTime", &getTime)
				.beginClass<Color4F>("Color4F")
					.addConstructor<void(), void(f32, f32, f32, f32)>()
					.addProperty("r", &Color4F::r)
					.addProperty("g", &Color4F::g)
					.addProperty("b", &Color4F::b)
					.addProperty("a", &Color4F::a)
				.endClass()
				.beginClass<FlexValue>("FlexValue")
					.addConstructor<void(), void(f32), void(FlexUnit, f32), void(const FlexValue&)>()
					.addProperty("unit", &FlexValue::getUnit, &FlexValue::setUnit)
					.addProperty("value", &FlexValue::getValue, &FlexValue::setValue)
				.endClass()
				.beginClass<FlexRect>("FlexRect")
					.addConstructor<
						void(), 
						void(const FlexRect&), 
						void(FlexValue), 
						void(FlexValue, FlexValue, FlexValue, FlexValue),
						void(f32, f32, f32, f32)
					>()
					.addProperty("top", &FlexRect::top)
					.addProperty("left", &FlexRect::left)
					.addProperty("bottom", &FlexRect::bottom)
					.addProperty("right", &FlexRect::right)
				.endClass()
				.beginClass<FlexBorder>("FlexBorder")
					.addConstructor<void(), void(const FlexBorder&), void(f32), void(f32, f32, f32, f32)>()
					.addProperty("top", &FlexBorder::top)
					.addProperty("left", &FlexBorder::left)
					.addProperty("bottom", &FlexBorder::bottom)
					.addProperty("right", &FlexBorder::right)
				.endClass()
				.addVariable("document", &_doc)
			.endNamespace();

		DomStyle rootStyle = _doc.getStyle(_doc.getRootElement());
		rootStyle.setWidth(FlexValue(FlexUnit::Percent, 100.0f));
		rootStyle.setHeight(FlexValue(FlexUnit::Percent, 100.0f));
		rootStyle.setFlexPositionType(FlexPositionType::Absolute);
		rootStyle.setLayoutDirection(LayoutDirection::LTR);
		rootStyle.setFlexDirection(FlexDirection::Row);
		rootStyle.setJustifyContent(FlexJustify::FlexStart);
		rootStyle.setFlexAlignItems(FlexAlign::FlexStart);
		rootStyle.setFlexAlignContent(FlexAlign::Stretch);

		int ret = luaL_dofile(_lua, _path.string().c_str());
		if (ret != 0) {
			spdlog::error(lua_tostring(_lua, -1));
			
			lua_close(_lua);
			_lua = nullptr;
		}

		//update(0.3f);
		_doc.loadStyle("E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\test.css");
	}
	
	bool LuaReact::handleMouseMove(PointF pos) {
		entt::registry& reg = _doc.getRegistry();
		bool changed = false;

		std::vector<DomElementHandle>& current = _mouseOver[_currentMouseOver];
		std::vector<DomElementHandle>& next = _mouseOver[1 - _currentMouseOver];
		assert(next.empty());

		getElementsAtPosition(_doc.getRootElement(), pos, next);

		for (auto it = current.rbegin(); it != current.rend(); ++it) {
			if (!StlUtil::vectorContains(next, *it)) {
				reg.remove<MouseEnteredTag>(*it);
				StyleUtil::markStyleDirty(reg, *it, true);
				changed |= emitEvent(*it, "onMouseLeave", MouseLeaveEvent{});
			}
		}

		for (DomElementHandle e : next) {
			if (!StlUtil::vectorContains(current, e)) {
				reg.emplace_or_replace<MouseEnteredTag>(e);
				StyleUtil::markStyleDirty(reg, e, true);
				changed |= emitRelativeEvent(e, "onMouseEnter", MouseEnterEvent{ Point(pos) });
			}
		}

		DomElementHandle currentTop = !current.empty() ? current.back() : entt::null;
		DomElementHandle nextTop = !next.empty() ? next.back() : entt::null;

		if (currentTop != nextTop) {
			if (currentTop != entt::null) {
				reg.remove<MouseOverTag>(currentTop);
				StyleUtil::markStyleDirty(reg, currentTop, true);
				changed |= emitEvent(currentTop, "onMouseOut", MouseLeaveEvent{});
			}

			if (nextTop != entt::null) {
				reg.emplace_or_replace<MouseOverTag>(nextTop);
				StyleUtil::markStyleDirty(reg, nextTop, true);
				changed |= emitRelativeEvent(nextTop, "onMouseOver", MouseEnterEvent{ Point(pos) });
			}
		}

		_cursor = CursorType::Arrow;

		// Mouse over whatever is at the top of the stack
		for (auto it = next.rbegin(); it != next.rend(); ++it) {
			DomElementHandle e = *it;

			const styles::Cursor* cursor = StyleUtil::findProperty<styles::Cursor>(reg, e, true);
			if (cursor) {
				_cursor = cursor->value;
			}

			bool fired = emitRelativeEvent(e, "onMouseMove", MouseMoveEvent{ Point(pos) });
			changed |= fired;

			if (fired) {
				break;
			}
		}

		current.clear();
		_currentMouseOver = 1 - _currentMouseOver;

		return changed;
	}
	
	bool LuaReact::handleMouseButton(const MouseButtonEvent& ev) {
		entt::registry& reg = _doc.getRegistry();
		bool fired = propagateMouseButton(_doc.getRootElement(), ev);

		if (_mouseFocus != entt::null && !ev.down) {
			reg.remove<MouseFocusTag>(_mouseFocus);
			StyleUtil::markStyleDirty(reg, _mouseFocus, true);
			fired |= emitEvent(_mouseFocus, "onMouseBlur", MouseBlurEvent{});
			_mouseFocus = entt::null;
		}

		return fired;
	}
	
	void LuaReact::getElementsAtPosition(const DomElementHandle e, const PointF pos, std::vector<DomElementHandle>& items) {
		entt::registry& reg = _doc.getRegistry();
		const RectF& area = reg.get<WorldAreaComponent>(e).area;

		if (area.contains(pos)) {
			items.push_back(e);

			_doc.each(e, [&](DomElementHandle child) {
				getElementsAtPosition(child, pos, items);
			});
		}
	}
	
	bool LuaReact::propagateMouseButton(DomElementHandle e, const MouseButtonEvent& ev) {
		entt::registry& reg = _doc.getRegistry();
		const RectF& area = reg.get<WorldAreaComponent>(e).area;

		if (area.contains(PointF(ev.position))) {
			MouseButtonEvent newEvent = ev;
			newEvent.position = ev.position - Point(area.position);

			if (emitEvent(e, "onMouseButton", std::move(newEvent))) {
				if (ev.down) {
					// NOTE: Maybe if 2 buttons are pressed something different should happen?
					// How do we track releases?
					reg.emplace_or_replace<MouseFocusTag>(e);
					StyleUtil::markStyleDirty(reg, e, true);
					emitEvent(e, "onMouseFocus", MouseFocusEvent{});
					_mouseFocus = e;
				}

				return true;
			}

			_doc.each(e, [&](DomElementHandle child) {
				propagateMouseButton(child, ev);
			});
		}

		return false;
	}
}
