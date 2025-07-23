#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <FileWatcher/FileWatcher.h>

#include "foundation/TypeRegistry.h"

namespace rp {
	struct RecentFilePath {
		std::string type;
		std::string name;
		std::string path;
	};

	struct Watch {
		using Action = FW::Action;
		using CallbackFunc = std::function<void(const std::string&, Action)>;
		using Id = FW::WatchID;

		struct Callback {
			Id id;
			CallbackFunc func;
		};

		Id watchId;
		std::string path;

		std::unordered_map<std::string, Callback> callbacks;
	};

	class FileManager : public FW::FileWatchListener {
	private:
		std::filesystem::path _rootPath;
		std::filesystem::path _recentPath;

		FW::FileWatcher _watcher;
		std::vector<Watch> _reloaders;

		Watch::Id _nextWatchId = 1;

		const fw::TypeRegistry& _types;

	public:
		static std::filesystem::path getContentPath();

		FileManager(const fw::TypeRegistry& types);
		~FileManager() {}

		Watch::Id startWatch(const std::filesystem::path& path, Watch::CallbackFunc&& func);

		void removeWatch(Watch::Id watchId);

		void addRecent(RecentFilePath&& recent);

		bool loadRecent(std::vector<RecentFilePath>& paths, const std::vector<std::string>& types = {});

		std::filesystem::path addHashedFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path addUniqueFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path createUniqueDirectory(const std::string& suggested);

		std::filesystem::path getUniqueDirectoryName(std::string suggested);

		std::filesystem::path getUniqueFilename(const std::filesystem::path& suggested);

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override;

		void update();

	private:
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
