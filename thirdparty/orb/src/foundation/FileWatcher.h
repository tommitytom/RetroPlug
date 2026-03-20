#pragma once

#include <functional>

#ifndef FW_PLATFORM_WEB
#include <efsw/FileSystem.hpp>
#include <efsw/System.hpp>
#include <efsw/efsw.hpp>
#include <filesystem>
#endif

#include "foundation/Types.h"

namespace orb {
	enum class WatchAction {
		Added,
		Modified,
		Removed
	};

	using WatchId = int32;
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

		void remove(WatchId id) override {}

		void update() override {}
	};

	class UpdateListener : public efsw::FileWatchListener {
	private:
		std::vector<Watch>& _watches;
	public:
		UpdateListener(std::vector<Watch>& watches) : _watches(watches) {}
		~UpdateListener() {}

		void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename) override {
			spdlog::debug("File action: {} {} {}", dir, filename, (int)action);

			std::string fullPath = (std::filesystem::path(dir) / std::filesystem::path(filename)).string();
			for (Watch& watch : _watches) {
				if (watch.watchId == watchid) {
					for (const auto& callback : watch.callbacks) {
						if (callback.first == fullPath || callback.first == dir) {
							callback.second.func(fullPath, toWatchAction(action));
						}
					}
					return;
				}
			}
		}

	private:
		WatchAction toWatchAction(efsw::Action action) {
			switch (action) {
				case efsw::Actions::Add:
					return WatchAction::Added;
				case efsw::Actions::Modified:
					return WatchAction::Modified;
				case efsw::Actions::Delete:
					return WatchAction::Removed;
				default:
					return WatchAction::Modified;
			}
		}
	};

	class EfswFileWatcher : public orb::FileWatcher {
	private:
		std::vector<Watch> _watches;
		WatchId _nextWatchId = 1;
		efsw::FileWatcher _watcher;
		UpdateListener _listener{ _watches };

	public:
		EfswFileWatcher(): _watcher(false) {
			_watcher.watch();
		}

		void update() override {
			
		}

		WatchId add(const std::string& path, WatchCallbackFunc&& callback) override {
			std::string watchPath;

			if (std::filesystem::is_directory(path)) {
				watchPath = path;
			} else {
				watchPath = std::filesystem::path(path).parent_path().string();
			}

			Watch* existing = findWatch(watchPath);
			if (!existing) {
				WatchId watchId = _watcher.addWatch(watchPath, &_listener);
				_watches.push_back({ watchId, watchPath });
				existing = &_watches.back();
			}

			WatchId id = _nextWatchId++;
			existing->callbacks.insert({ path, Watch::Callback { .id = id, .func = std::move(callback) } });

			return id;
		}

		void remove(WatchId id) override {

		}

	private:
		Watch* findWatch(const std::string& path) {
			for (Watch& watch : _watches) {
				if (watch.path == path) {
					return &watch;
				}
			}

			return nullptr;
		}
	};
}
