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

struct lua_State;

namespace fw {
	class Document;

	class DelegateWatchListener : public FW::FileWatchListener {
	public:
		using CallbackFunc = std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)>;
		struct Change {
			FW::WatchID watchid;
			FW::String dir;
			FW::String filename;
			FW::Action action;
		};

	private:
		const f32 _defaultDelay = 0.1f;
		CallbackFunc _callback;
		std::vector<Change> _pending;
		f32 _delay = 0.0f;
		
	public:
		DelegateWatchListener() {}
		DelegateWatchListener(CallbackFunc&& func) : _callback(std::move(func)) {}

		void setCallback(CallbackFunc&& func) {
			_callback = std::move(func);
		}

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override {
			for (auto it = _pending.begin(); it != _pending.end();) {
				if (it->watchid == watchid && it->dir == dir && it->filename == filename && it->action == action) {
					it = _pending.erase(it);
				} else {
					++it;
				}
			}

			_pending.push_back(Change{ watchid, dir, filename, action });
			_delay = _defaultDelay;
		}

		void update(f32 delta) {
			if (!_pending.empty() && _callback) {
				_delay -= delta;

				if (_delay <= 0.0f) {
					for (const auto& change : _pending) {
						_callback(change.watchid, change.dir, change.filename, change.action);
					}

					_pending.clear();
					_delay = 0.0f;
				}
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
