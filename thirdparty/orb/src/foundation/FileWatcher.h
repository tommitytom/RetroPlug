#pragma once

#include <functional>

//#ifndef FW_PLATFORM_WEB
//#include <FileWatcher/FileWatcher.h>
//#endif

#include "foundation/Types.h"

namespace orb {
	enum class WatchAction {
		Added,
		Modified,
		Removed
	};

	using WatchId = uint32;
	using WatchCallbackFunc = std::function<void(const std::string&, WatchAction)>;

	struct Watch {
		struct Callback {
			WatchId id;
			WatchCallbackFunc func;
		};

		WatchId watchId;
		std::string path;
		std::unordered_map<std::string, Callback> callbacks;
	};

	class FileWatcher {
	public:
		FileWatcher() {}
		virtual ~FileWatcher() {}

		virtual WatchId add(const std::string& path, WatchCallbackFunc&& callback) = 0;

		virtual void remove(WatchId id) = 0;

		virtual void update() = 0;
	};

	class DummyFileWatcher : public FileWatcher {
	public:
		WatchId add(const std::string& path, WatchCallbackFunc&& callback) override {
			return 0;
		}

		void remove(WatchId id) override {
		}

		void update() override {}
	};
/*
	class SimpleFileWatcher : public orb::FileWatchListener, public FileWatcher {
	private:
		std::vector<Watch> _watches;
		WatchId _nextWatchId = 1;
		orb::FileWatcher _watcher;

	public:
		WatchId add(const std::string& path, WatchCallbackFunc&& callback) {
			std::string watchPath;

			if (std::filesystem::is_directory(path)) {
				watchPath = path.string();
			} else {
				watchPath = path.parent_path().string();
			}

			Watch* existing = findWatch(watchPath);
			if (!existing) {
				orb::WatchID watchId = _watcher.addWatch(watchPath, this);
				_reloaders.push_back({ watchId, watchPath });
				existing = &_reloaders.back();
			}

			Watch::Id id = _nextWatchId++;
			existing->callbacks.insert({ path.string(), Watch::Callback { .id = id, .func = std::move(func) } });

			return id;
		}

		void remove(WatchId id) {
			std::string watchPath;

	if (std::filesystem::is_directory(path)) {
		watchPath = path.string();
	} else {
		watchPath = path.parent_path().string();
	}

	Watch* existing = findWatch(watchPath);
	if (!existing) {
		orb::WatchID watchId = _watcher.addWatch(watchPath, this);
		_reloaders.push_back({ watchId, watchPath });
		existing = &_reloaders.back();
	}

	Watch::Id id = _nextWatchId++;
	existing->callbacks.insert({ path.string(), Watch::Callback { .id = id, .func = std::move(func) } });

	return id;
		}

		#ifndef FW_PLATFORM_WEB
		void handleFileAction(orb::WatchID watchid, const orb::String& dir, const orb::String& filename, orb::Action action) override;
#endif

Watch* findWatch(const std::string& path) {
			for (Watch& watch : _reloaders) {
				if (watch.path == path) {
					return &watch;
				}
			}

			return nullptr;
		}
	};

	void FileManager::handleFileAction(orb::WatchID watchid, const orb::String& dir, const orb::String& filename, orb::Action action) {
	spdlog::debug("File action: {} {} {}", dir, filename, action);
	std::string fullPath = (std::filesystem::path(dir) / std::filesystem::path(filename)).string();
	for (Watch& watch : _reloaders) {
		if (watch.watchId == watchid) {
			for (const auto& callback : watch.callbacks) {
				if (callback.first == fullPath || callback.first == dir) {
					callback.second.func(fullPath, action);
				}
			}
			return;
		}
	}
}
	*/
}
