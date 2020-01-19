#pragma once

#include <assert.h>
#include <memory>
#include <string_view>
#include "crc32.h"

template <typename T = char>
class DataBuffer {
private:
	T* _dataPtr = nullptr;
	size_t _dataSize = 0;
	bool _ownsData = false;

public:
	DataBuffer() {}
	DataBuffer(size_t size) { resize(size); }
	DataBuffer(T* data, size_t size) : _dataPtr(data), _dataSize(size), _ownsData(false) {}
	~DataBuffer() { destroy(); }

	T get(size_t idx) { assert(idx < _dataSize); return _dataPtr[idx]; }
	void set(size_t idx, T v) { assert(idx < _dataSize); _dataPtr[idx] = v; }
	int find() {}
	
	std::string_view toString() {
		size_t len = strnlen(_dataPtr, _dataSize);
		return std::string_view(_dataPtr, len);
	}

	size_t hash(size_t initial = 0) {
		return crc32::update((const void*)_dataPtr, _dataSize * sizeof(T), initial);
	}

	void write(const T* source, size_t size) {
		assert(size <= _dataSize);
		memcpy(_dataPtr, source, size * sizeof(T));
	}

	T* data() { return _dataPtr; }
	const T* data() const { return _dataPtr; }
	size_t size() const { return _dataSize; }

	DataBuffer<T> slice(size_t pos, size_t size) {
		assert(pos + size <= _dataSize);
		return DataBuffer<T>(_dataPtr + pos, size);
	}

	void resize(size_t size) {
		destroy();
		_dataPtr = new T[size];
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

template <typename T, const size_t Length>
class FixedDataBuffer : public DataBuffer<T> {
private:
	T _data[Length];

public:
	FixedDataBuffer() : DataBuffer(_data, Length) {}
};

using DataBufferPtr = std::shared_ptr<DataBuffer<char>>;
using FloatDataBufferPtr = std::shared_ptr<DataBuffer<float>>;
