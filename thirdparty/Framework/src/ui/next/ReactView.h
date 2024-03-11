#pragma once

#include <unordered_map>

#include <FileWatcher/FileWatcher.h>
#include <lua.hpp>
#include <LuaBridge.h>
#include <entt/core/any.hpp>

#include "ui/next/ReactElementView.h"
#include "ui/next/DelegateWatchListener.h"

namespace fw {
	class LuaReact;	

	class ReactRoot : public ReactElementView {
		RegisterObject();

	private:
		struct EventHandler {
			using Func = std::function<void(std::string_view, uint32, const entt::any&)>;
			EventType type;
			std::string name;
			Func func;
		};

		lua_State* _lua;
		std::unordered_map<std::string, EventHandler> _eventHandlers;

	public:
		ReactRoot(lua_State* lua) : ReactElementView("body"), _lua(lua) { 
			setName("ReactRoot");

			createEventHandler<MouseButtonEvent>();
			createEventHandler<MouseMoveEvent>();
		}
		~ReactRoot() = default;

		static constexpr std::string_view EVENT_POSTFIX = "Event";

		template <typename T>
		constexpr std::string_view formatEventName() {
			constexpr std::string_view name = entt::type_name<T>::value();
			static_assert(name.ends_with(EVENT_POSTFIX));
			constexpr std::string_view formattedName = name.substr(name.find_last_of(" :") + 1);
			return formattedName.substr(0, formattedName.size() - EVENT_POSTFIX.size());
		}

		template <typename T>
		void createEventHandler() {
			std::string eventName = "on" + std::string(formatEventName<T>());

			_eventHandlers[eventName] = EventHandler{
				.type = entt::type_id<T>().index(),
				.name = eventName,
				.func = [this](std::string_view name, uint32 nodeId, const entt::any& ev) {
					luabridge::LuaRef cb = luabridge::getGlobal(_lua, "processHostEvent");
					luabridge::LuaResult result = cb(nodeId, name, entt::any_cast<const T&>(ev));

					if (!result) {
						spdlog::error(result.errorMessage());
						return;
						//return false;
					}

					//return result[0];
				}
			};
		}

		void addEventListener(ReactElementViewPtr node, const std::string& type) {
			auto found = _eventHandlers.find(type);
			if (found != _eventHandlers.end()) {
				subscribe(found->second.type, node, [counterId = node->getCounterId(), handler = found->second](const entt::any& ev) {
					handler.func(handler.name, counterId, ev);
				});
			} else {
				spdlog::error("Failed find event of type {}", type);
			}
		}

		void removeEventListener(ReactElementViewPtr node, const std::string& type) {
			auto found = _eventHandlers.find(type);
			if (found != _eventHandlers.end()) {
				unsubscribe(found->second.type, node);
			} else {
				spdlog::error("Failed find event of type {}", type);
			}
		}
	};

	class ReactView : public View {
		RegisterObject()

	private:
		lua_State* _lua = nullptr;

		std::filesystem::path _path;
		FW::FileWatcher _fileWatcher;
		DelegateWatchListener _listener;

		std::shared_ptr<ReactRoot> _root;

	public:
		ReactView();
		ReactView(const std::filesystem::path& path);
		~ReactView();

		void setPath(const std::filesystem::path& path);

		void onUpdate(f32 dt) override;

		void reload();

		void onInitialize() override;

		void onHotReload() override;
	};
}
