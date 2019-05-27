#include "File.h"

#include <fstream>

bool readFile(const std::string& path, std::vector<char>& target) {
	std::ifstream f(path, std::ios::binary);
	target = std::vector<char>(std::istreambuf_iterator<char>(f), {});
	return true;
}

bool readFile(const std::string& path, char* target, size_t size) {
	std::ifstream f(path, std::ios::binary);
	f.read(target, size);
	return true;
}

bool writeFile(const std::string& path, const std::vector<char>& data) {
	return writeFile(path, data.data(), data.size());
}

bool writeFile(const std::string& path, const char* data, size_t size) {
	std::ofstream f(path, std::ios::binary);
	f.write(data, size);
	return true;
}

bool readFile(const std::wstring& path, std::vector<char>& target) {
	std::ifstream f(path, std::ios::binary);
	target = std::vector<char>(std::istreambuf_iterator<char>(f), {});
	return true;
}

bool readFile(const std::wstring& path, char* target, size_t size) {
	std::ifstream f(path, std::ios::binary);
	f.read(target, size);
	return true;
}

bool writeFile(const std::wstring& path, const std::vector<char>& data) {
	return writeFile(path, data.data(), data.size());
}

bool writeFile(const std::wstring& path, const char* data, size_t size) {
	std::ofstream f(path, std::ios::binary);
	f.write(data, size);
	return true;
}
