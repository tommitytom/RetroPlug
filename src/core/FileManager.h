#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <FileWatcher/FileWatcher.h>

namespace rp {
	struct RecentFilePath {
		std::string type;
		std::string name;
		std::filesystem::path path;
	};

	struct Watch {
		using Action = FW::Action;
		using Callback = std::function<void(const std::string&, Action)>;
		using Id = FW::WatchID;

		Id watchId;
		std::string path;

		std::vector<std::pair<std::string, Callback>> callbacks;
	};

	class FileManager : public FW::FileWatchListener {
	private:
		std::filesystem::path _rootPath;
		std::filesystem::path _recentPath;

		FW::FileWatcher _watcher;
		std::vector<Watch> _reloaders;

	public:
		static std::filesystem::path getContentPath();

		FileManager();
		~FileManager() {}

		Watch::Id startWatch(const std::filesystem::path& path, Watch::Callback&& func);

		void addRecent(RecentFilePath&& recent);

		void loadRecent(std::vector<RecentFilePath>& paths, const std::vector<std::string>& types = {});

		std::filesystem::path addHashedFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path addUniqueFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path createUniqueDirectory(const std::string& suggested);

		std::filesystem::path getUniqueDirectoryName(std::string suggested);

		std::filesystem::path getUniqueFilename(const std::filesystem::path& suggested);

		void handleFileAction(FW::WatchID watchid, const FW::String& dir, const FW::String& filename, FW::Action action) override;

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
