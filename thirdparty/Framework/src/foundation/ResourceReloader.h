#pragma once

#include <FileWatcher/FileWatcher.h>

#include "foundation/ResourceManager.h"
#include "foundation/ProcessUtil.h"

using namespace std::placeholders; // for _1, _2 etc.

namespace fw {
	class ResourceReloader {
	private:
		using FileChangeCallback = std::function<void(FW::WatchID, const FW::String&, const FW::String&, FW::Action)>;
		using ResourceReloadCallback = std::function<void(ResourceHandle)>;
		
		class FileChangeListener final : public FW::FileWatchListener {
		private:
			FileChangeCallback _callback;

		public:
			FileChangeListener(FileChangeCallback&& callback) : _callback(std::move(callback)) {}
			~FileChangeListener() = default;

			void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override {
				_callback(watchid, dir, filename, action);
			}
		};
		
		struct Watch {
			FW::WatchID watchId;
			std::string path;

			std::vector<std::pair<std::string, ResourceReloadCallback>> callbacks;
		};

		ResourceManager* _resourceManager = nullptr;

		FW::FileWatcher _watcher;
		FileChangeListener _listener;
		
		std::vector<Watch> _reloaders;

	public:
		ResourceReloader(): _listener(std::bind(&ResourceReloader::onReload, this, _1, _2, _3, _4)) {}
		~ResourceReloader() = default;

		FW::WatchID startWatch(const std::filesystem::path& path, std::function<void(ResourceHandle)>&& func) {
			std::string watchPath;
			
			if (std::filesystem::is_directory(path)) {
				watchPath = path.string();
			} else {
				watchPath = path.parent_path().string();
			}

			Watch* existing = findWatch(watchPath);
			if (!existing) {
				FW::WatchID watchId = _watcher.addWatch(watchPath, &_listener);
				_reloaders.push_back({ watchId, watchPath });
				existing = &_reloaders.back();
			}

			existing->callbacks.push_back({ path.string(), std::move(func) });		

			return existing->watchId;
		}

		template <typename T>
		FW::WatchID startWatch(const std::filesystem::path& path, std::function<void(TypedResourceHandle<T>)>&& func) {
			return startWatch(path, [func = std::move(func)](ResourceHandle handle) {
				func(handle.getResourceHandleAs<T>());
			});
		}

		FW::WatchID startWatch(const std::filesystem::path& path) {
			return startWatch(path, nullptr);
		}

		void update() {
			_watcher.update();

			for (const auto& res : _resourceManager->getLoadedThisFrame()) {
				spdlog::info("{} has reloaded", res.getUri());
				std::string uriParent = std::filesystem::path(res.getUri()).parent_path().string();
				
				for (const Watch& watch : _reloaders) {
					for (const auto& callback : watch.callbacks) {
						if (callback.second && (callback.first == res.getUri() || callback.first == uriParent)) {
							callback.second(res);
						}
					}
				}
			}
		}

		void onReload(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) {
			if (!_resourceManager) {
				return;
			}

			std::string path = (std::filesystem::path(dir) / std::filesystem::path(filename)).string();

			if (_resourceManager->has(path)) {
				_resourceManager->reload(path);
				spdlog::info("{} has changed and will be reloaded", path);
			} else {
				spdlog::debug("{} has changed but was not reloaded", path);
			}
		}

		void setResourceManager(ResourceManager& resourceManager) {
			_resourceManager = &resourceManager;
		}

		ResourceManager* getResourceManager() {
			return _resourceManager;
		}

		Watch* findWatch(const std::string& path) {
			for (Watch& watch : _reloaders) {
				if (watch.path == path) {
					return &watch;
				}
			}

			return nullptr;
		}
	};
}
