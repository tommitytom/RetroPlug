#include "File.h"

#include <fstream>
#include <sstream>

size_t getFileSize(std::ifstream& stream) {
	stream.seekg(0, std::ios::end);
	size_t size = stream.tellg();
	stream.seekg(0, std::ios::beg);
	return size;
}

bool readFile(const std::string& path, std::vector<char>& target) {
	std::ifstream f(path, std::ios::binary);
	target.resize(getFileSize(f));
	f.read(target.data(), target.size());
	return true;
}

bool readFile(const std::string& path, char* target, size_t size, bool binary) {
	std::ifstream f(path, binary ? std::ios::binary : 0);
	f.read(target, size);
	return true;
}

bool readFile(const std::string& path, std::string& target) {
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	target = ss.str();
	return true;
}

bool writeFile(const std::string& path, const std::vector<char>& data) {
	return writeFile(path, data.data(), data.size());
}

bool writeFile(const std::string& path, const std::string& data) {
	return writeFile(path, data.data(), data.size());
}

bool writeFile(const std::string& path, const char* data, size_t size, bool binary) {
	std::ofstream f(path, binary ? std::ios::binary : 0);
	f.write(data, size);
	return true;
}

bool readFile(const std::wstring& path, std::vector<char>& target) {
	std::ifstream f(path, std::ios::binary);
	target.resize(getFileSize(f));
	f.read(target.data(), target.size());
	return true;
}

bool readFile(const std::wstring& path, char* target, size_t size) {
	std::ifstream f(path, std::ios::binary);
	f.read(target, size);
	return true;
}

bool readFile(const std::wstring& path, std::string& target) {
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	target = ss.str();
	return true;
}

bool writeFile(const std::wstring& path, const std::vector<char>& data) {
	return writeFile(path, data.data(), data.size(), true);
}

bool writeFile(const std::wstring& path, const std::string& data) {
	return writeFile(path, data.data(), data.size(), false);
}

bool writeFile(const std::wstring& path, const char* data, size_t size, bool binary) {
	std::ofstream f(path, binary ? std::ios::binary : 0);
	f.write(data, size);
	return true;
}
