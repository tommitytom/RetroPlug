#include "fs.h"

#include <fstream>
#include <string>
#include <spdlog/spdlog.h>

using namespace rp;

std::string fsutil::readTextFile(const fs::path& path) {
	std::ifstream file(path);
	if (file.is_open()) {
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());;
	}

	return std::string();
}

bool fsutil::writeTextFile(const fs::path& path, const std::string& data) {
	std::ofstream file(path);
	if (file.is_open()) {
		file.write(data.data(), data.size());
		return true;
	}

	return false;
}

bool fsutil::writeFile(const fs::path& path, const char* data, size_t size) {
	std::ofstream file(path, std::ios::binary);
	if (file.is_open()) {
		file.write(data, size);
		return true;
	}

	return false;
}

bool fsutil::exists(const fs::path& path) {
	return fs::exists(path);
}

size_t fsutil::fileSize(const fs::path& path) {
	std::ifstream f(path);
	f.seekg(std::ios::end);
	return (size_t)f.tellg();
}

std::vector<std::byte> fsutil::readFile(const fs::path& path) {
	std::vector<std::byte> target;
	std::ifstream f(path.lexically_normal(), std::ios::binary);

	//es_assert(f.is_open());
	if (!f.is_open()) {
		spdlog::warn("Failed to open {}", path.string());
		return target;
	}

	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	f.seekg(0, std::ios::beg);
	
	target.resize((size_t)size);
	f.read((char*)target.data(), target.size());

	if (size == 0) {
		spdlog::warn("File is empty {}", path.string());
	}

	return target;
}

size_t fsutil::readFile(const fs::path& path, Uint8Buffer* target) {
	std::ifstream f(path.lexically_normal(), std::ios::binary);

	//es_assert(f.is_open());
	if (!f.is_open()) {
		spdlog::warn("Failed to open {}", path.string());
		return 0;
	}

	f.seekg(0, std::ios::end);
	std::streamoff size = f.tellg();
	f.seekg(0, std::ios::beg);

	target->resize((size_t)size);
	f.read((char*)target->data(), target->size());

	if (size == 0) {
		spdlog::warn("File is empty {}", path.string());
	}

	return (size_t)size;
}

uint64 fsutil::lastWriteTime(const fs::path& path) {
	return (uint64)fs::last_write_time(path).time_since_epoch().count();
}
