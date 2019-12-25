#pragma once

#include <map>
#include <string>
#include <memory>

#include "util/File.h"
#include "util/DataBuffer.h"
#include <util/crc32.h>

const int MAX_ROM_SIZE = 1024 * 1024; // 1 mb
using RomBuffer = FixedDataBuffer<MAX_ROM_SIZE>;

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

	File* loadFile(const std::string& path, bool reload = false) {
		File* file = getFile(path);
		if (file && reload == false) {
			return file;
		}

		if (!file) {
			file = addFile(path);
		}

		DataBufferPtr data = std::make_shared<DataBuffer>();
		if (readFile(tstr(path), data.get())) {
			file->data = data;
		}

		return file;
	}

	void watchFolder(const std::string& path, bool recusrive = true) {

	}
};
