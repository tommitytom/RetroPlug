#pragma once

#include "foundation/FileWatcher.h"
#include "foundation/ResourceManager.h"
#include "foundation/ProcessUtil.h"

using namespace std::placeholders; // for _1, _2 etc.

namespace fw {
	class ResourceReloader {
	private:
		using ResourceReloadCallback = std::function<void(ResourceHandle)>;

		ResourceManager* _resourceManager = nullptr;

		std::unique_ptr<FileWatcher> _watcher;

	public:
		ResourceReloader(): _watcher(std::make_unique<DummyFileWatcher>()) {}
		~ResourceReloader() = default;

		WatchId startWatch(const std::filesystem::path& path, std::function<void(ResourceHandle)>&& func) {
			return _watcher->add(path.string(), [func = std::move(func)](const std::string& fullPath, WatchAction action) {
				if (action == WatchAction::Modified) {
					//ResourceHandle handle = ResourceManager::get().getResourceHandle(fullPath);
					//func(handle);
				}
			});
		}

		template <typename T>
		WatchId startWatch(const std::filesystem::path& path, std::function<void(TypedResourceHandle<T>)>&& func) {
			return startWatch(path, [func = std::move(func)](ResourceHandle handle) {
				func(handle.getResourceHandleAs<T>());
			});
		}

		WatchId startWatch(const std::filesystem::path& path) {
			return startWatch(path, nullptr);
		}

		void update() {
			_watcher->update();

			/*for (const auto& res : _resourceManager->getLoadedThisFrame()) {
				spdlog::info("{} has reloaded", res.getUri());
				std::string uriParent = std::filesystem::path(res.getUri()).parent_path().string();

				for (const Watch& watch : _reloaders) {
					for (const auto& callback : watch.callbacks) {
						if (callback.second && (callback.first == res.getUri() || callback.first == uriParent)) {
							callback.second(res);
						}
					}
				}
			}*/
		}

		void onReload(WatchId watchid, const std::string& path, WatchAction action) {
			if (!_resourceManager) {
				return;
			}

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
	};
}
