#pragma once

#include <map>
#include <string>
#include <memory>

#include "util/File.h"
#include "util/DataBuffer.h"
#include "util/crc32.h"
#include "util/fs.h"

const int MAX_ROM_SIZE = 1024 * 1024; // 1 mb
using RomBuffer = FixedDataBuffer<char, MAX_ROM_SIZE>;

struct RomData {
	char name[12];
	RomBuffer data;
};

class File {
public:
	std::string path;
	DataBufferPtr data;
	uint32_t checksum = 0;
};

class FileManager {
private:
	std::map<std::string, File> _files;

public:
	File* addFile(const std::string& path) {
		File* file = &_files[path];
		return file;
	}

	File* getFile(const std::string& path) {
		auto found = _files.find(path);
		if (found != _files.end()) {
			return &found->second;
		}

		return nullptr;
	}

	bool saveFile(const std::string& path, DataBuffer<char>* data) {
		return writeFile(tstr(path), data);
	}

	bool saveTextFile(const std::string& path, const std::string& data) {
		return writeFile(tstr(path), data);
	}

	File* loadFile(const std::string& path, bool reload = false) {
		File* file = getFile(path);
		if (file && reload == false) {
			return file;
		}

		if (!file) {
			file = addFile(path);
		}

		DataBufferPtr data = std::make_shared<DataBuffer<char>>();
		if (readFile(tstr(path), data.get())) {
			file->data = data;
		} else {
			return nullptr;
		}

		return file;
	}

	bool exists(const std::string& path) {
		return fs::exists(tstr(path));
	}

	void watchFolder(const std::string& path, bool recusrive = true) {

	}
};
