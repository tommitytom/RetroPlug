#pragma once

#ifdef RP_MACOS
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <vector>
#include <string>
#include <string_view>

//#include "platform/Assert.h"
#include "platform/Types.h"
#include "util/DataBuffer.h"

namespace rp::fsutil {
	inline std::string_view getFilename(std::string_view path) {
		auto idx = path.find_last_of("/\\");
		if (idx != std::string::npos) {
			return path.substr(idx + 1);
		}

		return path;
	}

	inline std::string getFilename(const std::string& path) {
		auto idx = path.find_last_of("/\\");
		if (idx != std::string::npos) {
			return path.substr(idx + 1);
		}

		return path;
	}

	// Gets the file extension from a path, including the dot.  If `full` is set to false, only the
	// last part of an extension will be returned for a multi part extension.  Eg:
	// If `full` is true "myfile.multi.ext" will return ".multi.ext", else it will return ".ext"
	inline std::string_view getFileExt(std::string_view path, bool full = true) {
		std::string_view filename = getFilename(path);

		size_t idx = full ? filename.find_first_of('.') : filename.find_last_of('.');
		if (idx != std::string::npos) {
			return filename.substr(idx);
		}

		return "";
	}

	// Gets the file extension from a path, including the dot.  If `full` is set to false, only the
	// last part of an extension will be returned for a multi part extension.  Eg:
	// If `full` is true "myfile.multi.ext" will return ".multi.ext", else it will return ".ext"
	/*inline std::string getFileExt(const std::string& path, bool full = true) {
		std::string filename = getFilename(path);

		size_t idx = full ? filename.find_first_of('.') : filename.find_last_of('.');
		if (idx != std::string::npos) {
			return filename.substr(idx);
		}

		return "";
	}*/

	// Removes the file extension from a path, including the dot.  If `full` is set to false, only the
	// last part of an extension will be removed for a multi part extension.  Eg:
	// If `full` is true "myfile.multi.ext" will be changed to "myfile", else it will be changed 
	// to "myfile.multi"
	inline std::string removeFileExt(std::string_view path, bool full = true) {
		std::string_view filename = getFilename(path);

		size_t idx = full ? filename.find_first_of('.') : filename.find_last_of('.');
		if (idx != std::string::npos) {
			size_t off = path.size() - filename.size();
			return std::string(path.substr(0, off + idx));
		}

		return std::string(path);
	}

	// Replaces the file extension from a path, including the dot.  If `full` is set to false, only the
	// last part of an extension will be replaced for a multi part extension.  Eg:
	// If `full` is true "myfile.multi.ext" will be changed to "myfile.png", else it will be changed 
	// to "myfile.multi.png"
	inline std::string replaceFileExt(std::string_view path, const std::string& ext, bool full = true) {
		//es_assert_m(ext[0] == '.', "File extension must start with a '.'");
		return removeFileExt(path, full) + ext;
	}

	// Replaces the file extension from a path, including the dot.  If `full` is set to false, only the
	// last part of an extension will be replaced for a multi part extension.  Eg:
	// If `full` is true "myfile.multi.ext" will be changed to "myfile.png", else it will be changed 
	// to "myfile.multi.png"
	inline std::string replaceFileExt(const std::string& path, const std::string& ext, bool full = true) {
		//es_assert_m(ext[0] == '.', "File extension must start with a '.'");
		return removeFileExt(path, full) + ext;
	}

	inline std::string getDirectoryPath(const std::string& path) {
		auto idx = path.find_last_of("/\\");
		if (idx != std::string::npos) {
			return path.substr(0, idx);
		}

		return path;
	}

	inline std::string getDirectoryName(const std::string& path) {
		std::string dirPath = getDirectoryPath(path);
		return getFilename(dirPath);
	}

	std::string readTextFile(const fs::path& path);

	bool writeTextFile(const fs::path& path, const std::string& data);

	bool writeFile(const fs::path& path, const char* data, size_t size);

	bool exists(const fs::path& path);

	size_t fileSize(const fs::path& path);

	std::vector<std::byte> readFile(const fs::path& path);

	size_t readFile(const fs::path& path, Uint8Buffer* target);

	uint64 lastWriteTime(const fs::path& path);
}
