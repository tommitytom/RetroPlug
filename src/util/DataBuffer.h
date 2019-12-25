#pragma once

#include <assert.h>
#include <memory>
#include <string_view>
#include "crc32.h"

template <const size_t Length>
class FixedDataBuffer {
private:
	char _data[Length];

public:
	FixedDataBuffer() : DataBuffer(_data, Length) {}
};

class DataBuffer {
private:
	char* _dataPtr = nullptr;
	size_t _dataSize = 0;
	bool _ownsData = false;

public:
	DataBuffer() {}
	DataBuffer(size_t size) { resize(size); }
	DataBuffer(char* data, size_t size) : _dataPtr(data), _dataSize(size), _ownsData(false) {}
	~DataBuffer() { destroy(); }

	char get(size_t idx) { assert(idx < _dataSize); return _dataPtr[idx]; }
	void set(size_t idx, char v) { assert(idx < _dataSize); _dataPtr[idx] = v; }
	int find() {}
	
	std::string_view toString() {
		size_t len = strnlen(_dataPtr, _dataSize);
		return std::string_view(_dataPtr, len);
	}

	size_t hash(size_t initial = 0) {
		return crc32::update(_dataPtr, _dataSize, initial);
	}

	char* data() { return _dataPtr; }
	const char* data() const { return _dataPtr; }
	size_t size() const { return _dataSize; }

	DataBuffer slice(size_t pos, size_t size) {
		assert(pos + size <= _dataSize);
		return DataBuffer(_dataPtr + pos, size);
	}

	void resize(size_t size) {
		destroy();
		_dataPtr = new char[size];
		_dataSize = size;
		_ownsData = true;
	}

	void destroy() {
		if (_ownsData && _dataPtr) {
			delete[] _dataPtr;
			_dataPtr = nullptr;
			_dataSize = 0;
			_ownsData = false;
		}
	}
};

using DataBufferPtr = std::shared_ptr<DataBuffer>;
