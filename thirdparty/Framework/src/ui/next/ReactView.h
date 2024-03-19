#pragma once

#include <unordered_map>

#include <FileWatcher/FileWatcher.h>
#include <lua.hpp>
#include <LuaBridge.h>
#include <entt/core/any.hpp>

#include "ui/next/DelegateWatchListener.h"
#include "ui/next/ReactElementView.h"
#include "ui/next/ReactRoot.h"

namespace fw {
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
