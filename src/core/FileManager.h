#pragma once

#include <filesystem>

namespace rp {
	struct RecentFilePath {
		std::string type;
		std::string name;
		std::filesystem::path path;
	};

	class FileManager {
	private:
		std::filesystem::path _rootPath;
		std::filesystem::path _recentPath;
		std::vector<RecentFilePath> _recent;

	public:
		FileManager();
		~FileManager() {}

		void setRootPath(const std::filesystem::path& rootPath) {
			_rootPath = rootPath;
		}

		void addRecent(RecentFilePath&& recent);

		void loadRecent(std::vector<RecentFilePath>& paths, const std::vector<std::string>& types = {});

		std::filesystem::path addHashedFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path addUniqueFile(const std::filesystem::path& sourceFile, const std::filesystem::path& targetDir);

		std::filesystem::path getUniqueFilename(const std::filesystem::path& suggested);
	};
}
