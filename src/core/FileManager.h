#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "foundation/FileWatcher.h"
#include "foundation/TypeRegistry.h"

namespace rp {
	struct RecentFilePath {
		std::string type;
		std::string name;
		std::string path;
	};

	class FileManager {
	private:
		std::filesystem::path _rootPath;
		std::filesystem::path _recentPath;

		const fw::TypeRegistry& _types;

	public:
		static std::filesystem::path getContentPath();

		FileManager(const fw::TypeRegistry& types);
		~FileManager() {}

		fw::WatchId startWatch(const std::filesystem::path& path, fw::WatchCallbackFunc&& func);

		void removeWatch(fw::WatchId watchId);

		void addRecent(RecentFilePath&& recent);

		bool loadRecent(std::vector<RecentFilePath>& paths, const std::vector<std::string>& types = {});

		std::filesystem::path addHashedFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path addUniqueFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path createUniqueDirectory(const std::string& suggested);

		std::filesystem::path getUniqueDirectoryName(std::string suggested);

		std::filesystem::path getUniqueFilename(const std::filesystem::path& suggested);

		void update();
	};
}
