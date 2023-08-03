#pragma once

#include <memory>

#include <FileWatcher/FileWatcher.h>
#include <lua.hpp>
#include <LuaBridge.h>

#include "foundation/Input.h"
#include "graphics/FontManager.h"
#include "ui/next/Document.h"
#include "ui/next/DocumentUtil.h"

struct lua_State;

namespace fw {
	class Document;

	class DelegateWatchListener : public FW::FileWatchListener {
	public:
		using CallbackFunc = std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)>;

	private:
		CallbackFunc _callback;

	public:
		DelegateWatchListener() {}
		DelegateWatchListener(CallbackFunc&& func) : _callback(std::move(func)) {}

		void setCallback(CallbackFunc&& func) {
			_callback = std::move(func);
		}

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override {
			if (_callback) {
				_callback(watchid, dir, filename, action);
			}
		}
	};

	class LuaReact {
	private:
		std::filesystem::path _path;
		Document _doc;

		lua_State* _lua = nullptr;

		FW::FileWatcher _fileWatcher;
		DelegateWatchListener _listener;

	public:
		LuaReact(FontManager& fontManager, const std::filesystem::path& path);
		~LuaReact();

		void update(f32 dt);

		void reload();

		Document& getDocument() {
			return _doc;
		}

		const Document& getDocument() const {
			return _doc;
		}

		bool handleMouseMove(PointF pos) {
			return propagateMouseMove(_doc.getRootElement(), pos);
		}

	private:
		bool propagateMouseMove(entt::entity e, PointF pos) {
			entt::registry& reg = _doc.getRegistry();
			const RectF& area = reg.get<WorldAreaComponent>(e).area;

			if (area.contains(pos)) {
				if (emitEvent(e, "onMouseMove", MouseMoveEvent{ Point(pos - area.position) })) {
					return true;
				}

				_doc.each(e, [&](entt::entity child) {
					propagateMouseMove(child, pos);
				});
			}

			return false;
		}


		template <typename T>
		bool emitEvent(entt::entity e, std::string_view name, T&& ev) {
			luabridge::LuaRef cb = luabridge::getGlobal(_lua, "processHostEvent");
			luabridge::LuaResult result = cb(e, name, ev);

			if (!result) {
				spdlog::error(result.errorMessage());
				return false;
			}

			return result[0];
		}
	};
}
