#include "ReactRoot.h"

#include <LuaBridge.h>

namespace fw {
	static constexpr std::string_view EVENT_POSTFIX = "Event";

	template <typename T>
	constexpr std::string_view formatEventName() {
		constexpr std::string_view name = entt::type_name<T>::value();
		static_assert(name.ends_with(EVENT_POSTFIX));
		constexpr std::string_view formattedName = name.substr(name.find_last_of(" :") + 1);
		return formattedName.substr(0, formattedName.size() - EVENT_POSTFIX.size());
	}

	template <typename T>
	void createEventHandler(lua_State* lua, std::unordered_map<std::string, ReactEventHandler>& eventHandlers) {
		std::string eventName = "on" + std::string(formatEventName<T>());

		eventHandlers[eventName] = ReactEventHandler{
			.type = entt::type_id<T>().index(),
			.name = eventName,
			.func = [lua](std::string_view name, uint32 nodeId, const entt::any& ev) {
				luabridge::LuaRef cb = luabridge::getGlobal(lua, "processHostEvent");
				luabridge::LuaResult result = cb(nodeId, name, entt::any_cast<const T&>(ev));

				if (!result) {
					spdlog::error(result.errorMessage());
					return false;
				}

				return result[0].cast<bool>().value();
			}
		};
	}

	ReactRoot::ReactRoot(lua_State* lua) : ReactElementView("body"), _lua(lua) {
		setName("ReactRoot");

		createEventHandler<MouseEnterEvent>(lua, _eventHandlers);
		createEventHandler<MouseLeaveEvent>(lua, _eventHandlers);
		createEventHandler<MouseLeaveEvent>(lua, _eventHandlers);
		createEventHandler<MouseMoveEvent>(lua, _eventHandlers);
		createEventHandler<MouseButtonEvent>(lua, _eventHandlers);
		createEventHandler<KeyEvent>(lua, _eventHandlers);
		createEventHandler<CharEvent>(lua, _eventHandlers);
		createEventHandler<MouseScrollEvent>(lua, _eventHandlers);
		createEventHandler<MouseFocusEvent>(lua, _eventHandlers);
		createEventHandler<MouseBlurEvent>(lua, _eventHandlers);
		createEventHandler<MouseDoubleClickEvent>(lua, _eventHandlers);
		createEventHandler<ButtonEvent>(lua, _eventHandlers);
	}
	
	void ReactRoot::addEventListener(ReactElementViewPtr node, const std::string& type) {
		auto found = _eventHandlers.find(type);
		if (found != _eventHandlers.end()) {
			// NOTE: Making a copy of the handler here, that might be bad if there are lots of events
			subscribe(found->second.type, node, [counterId = node->getCounterId(), handler = found->second](const entt::any& ev) -> bool {
				return handler.func(handler.name, counterId, ev);
			});
		} else {
			spdlog::error("Failed find event of type {}", type);
		}
	}
	
	void ReactRoot::removeEventListener(ReactElementViewPtr node, const std::string& type) {
		auto found = _eventHandlers.find(type);
		if (found != _eventHandlers.end()) {
			unsubscribe(found->second.type, node);
		} else {
			spdlog::error("Failed find event of type {}", type);
		}
	}
}
