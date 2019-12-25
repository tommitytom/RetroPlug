#include "File.h"

#include <fstream>
#include <sstream>

#include "DataBuffer.h"

size_t getFileSize(std::ifstream& stream) {
	stream.seekg(0, std::ios::end);
	size_t size = stream.tellg();
	stream.seekg(0, std::ios::beg);
	return size;
}

bool readFile(const tstring& path, std::vector<std::byte>& target) {
	std::ifstream f(path, std::ios::binary);
	target.resize(getFileSize(f));
	f.read((char*)target.data(), target.size());
	return true;
}

bool readFile(const tstring& path, DataBuffer* target) {
	std::ifstream f(path, std::ios::binary);
	target->resize(getFileSize(f));
	f.read((char*)target->data(), target->size());
	return true;
}

bool readFile(const tstring& path, std::byte* target, size_t size, bool binary) {
	std::ifstream f(path, binary ? std::ios::binary : 0);
	f.read((char*)target, size);
	return true;
}

bool readFile(const tstring& path, std::string& target) {
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	target = ss.str();
	return true;
}

bool writeFile(const tstring& path, const std::vector<std::byte>& data) {
	return writeFile(path, (std::byte*)data.data(), data.size());
}

bool writeFile(const tstring& path, const std::string& data) {
	return writeFile(path, (std::byte*)data.data(), data.size());
}

bool writeFile(const tstring& path, const std::byte* data, size_t size, bool binary) {
	std::ofstream f(path, binary ? std::ios::binary : 0);
	f.write((char*)data, size);
	return true;
}
