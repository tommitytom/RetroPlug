#pragma once

#include <memory>
#include <queue>

#include <FileWatcher/FileWatcher.h>
#include <lua.hpp>
#include <LuaBridge.h>

#include "foundation/Input.h"
#include "foundation/StlUtil.h"
#include "graphics/FontManager.h"
#include "ui/next/Document.h"
#include "ui/next/DocumentUtil.h"
#include "ui/next/DelegateWatchListener.h"

struct lua_State;

namespace fw {
	class Document;

	class LuaReact {
	private:
		std::filesystem::path _path;
		Document _doc;

		lua_State* _lua = nullptr;

		FW::FileWatcher _fileWatcher;
		DelegateWatchListener _listener;

		std::vector<entt::entity> _mouseOver[2];
		size_t _currentMouseOver = 0;
		entt::entity _mouseFocus = entt::null;
		CursorType _cursor = CursorType::Arrow;

	public:
		LuaReact(FontManager& fontManager, const std::filesystem::path& path);
		~LuaReact();

		CursorType getCursor() const {
			return _cursor;
		}

		void update(f32 dt);

		void reload();

		Document& getDocument() {
			return _doc;
		}

		const Document& getDocument() const {
			return _doc;
		}

		bool handleMouseMove(PointF pos);

		bool handleMouseButton(const MouseButtonEvent& ev);

	private:
		void getElementsAtPosition(const entt::entity e, const PointF pos, std::vector<entt::entity>& items);

		bool propagateMouseButton(entt::entity e, const MouseButtonEvent& ev);

		template <typename T>
		bool emitRelativeEvent(entt::entity e, std::string_view name, T&& ev) {
			const RectF& area = _doc.getRegistry().get<WorldAreaComponent>(e).area;
			ev.position -= Point(area.position);
			return emitEvent(e, name, std::forward<T>(ev));
		}

		template <typename T>
		bool emitEvent(entt::entity e, std::string_view name, T&& ev) {
			luabridge::LuaRef cb = luabridge::getGlobal(_lua, "processHostEvent");
			luabridge::LuaResult result = cb(e, name, std::forward<T>(ev));

			if (!result) {
				spdlog::error(result.errorMessage());
				return false;
			}

			return result[0];
		}
	};
}
