#pragma once

#include <lua.hpp>
#include "ui/next/ReactElementView.h"

namespace fw {
	struct ReactEventHandler {
		using Func = std::function<bool(std::string_view, uint32, const entt::any&)>;
		EventType type;
		std::string name;
		Func func;
	};

	class ReactRoot : public ReactElementView {
		RegisterObject();

	private:
		lua_State* _lua;
		std::unordered_map<std::string, ReactEventHandler> _eventHandlers;

	public:
		ReactRoot(lua_State* lua);
		~ReactRoot() = default;

		void addEventListener(ReactElementViewPtr node, const std::string& type);

		void removeEventListener(ReactElementViewPtr node, const std::string& type);
	};
}
