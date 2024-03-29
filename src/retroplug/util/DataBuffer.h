#pragma once

#include <assert.h>
#include <memory>
#include <string_view>

#include <xxhash.h>

template <typename T>
class DataBuffer {
private:
	T* _dataPtr = nullptr;
	size_t _reserved = 0;
	size_t _size = 0;
	bool _ownsData = false;

public:
	DataBuffer() {}
	DataBuffer(const DataBuffer& other) { *this = other; }
	DataBuffer(DataBuffer&& other) { *this = std::move(other); }
	DataBuffer(size_t size) { resize(size); }
	DataBuffer(T* data, size_t size, bool ownsData = false) : _dataPtr(data), _reserved(size), _ownsData(ownsData) {}
	~DataBuffer() { destroy(); }

	T get(size_t idx) { assert(idx < _reserved); return _dataPtr[idx]; }
	void set(size_t idx, T v) { assert(idx < _reserved); _dataPtr[idx] = v; }
	int find() {}
	
	std::string_view toString() {
		size_t len = strnlen(_dataPtr, _reserved);
		return std::string_view(_dataPtr, len);
	}

	uint64_t hash() {
		return XXH3_64bits((const void*)_dataPtr, _reserved * sizeof(T));
	}

	void write(const T* source, size_t size) {
		assert(size <= _size);
		memcpy(_dataPtr, source, size * sizeof(T));
	}

	T* data() { return _dataPtr; }
	const T* data() const { return _dataPtr; }

	const T* getData() const { return _dataPtr; }

	size_t size() const { return _reserved; }

	uint32_t readUint32(size_t pos) {
		return *((uint32_t*)(_dataPtr + pos));
	}

	int32_t readInt32(size_t pos) {
		return *((uint32_t*)(_dataPtr + pos));
	}

	DataBuffer<T> slice(size_t pos, size_t size) {
		assert(pos + size <= _reserved);
		return DataBuffer<T>(_dataPtr + pos, size);
	}

	void clear() {
		memset(_dataPtr, 0, _size * sizeof(T));
	}

	void reserve(size_t size) {
		assert(_reserved == 0 || _ownsData);
		if (size > _reserved) {
			T* data = new T[size];

			if (_reserved > 0) {
				memcpy(data, _dataPtr, size * sizeof(T));
				destroy();
			}

			_dataPtr = data;
			_reserved = size;
			_ownsData = true;
		} else if (_dataPtr) {
			_reserved = size;
		}
	}

	void resize(size_t size) {
		reserve(size);
		_size = size;
	}

	void destroy() {
		if (_ownsData && _dataPtr) {
			delete[] _dataPtr;
			_dataPtr = nullptr;
			_reserved = 0;
			_ownsData = false;
		}
	}

	size_t copyFrom(DataBuffer* other) {
		size_t writeAmount = std::min(_reserved, other->size());
		memcpy(_dataPtr, other->_dataPtr, writeAmount * sizeof(T));
		_size = std::max(_size, writeAmount);
		return writeAmount;
	}

	void copyTo(DataBuffer* target) const {
		target->resize(_reserved);
		target->write(_dataPtr, _reserved);
	}

	DataBuffer clone() const {
		DataBuffer ret;
		copyTo(&ret);
		return ret;
	}

	DataBuffer& operator=(const DataBuffer& other) {
		if (_ownsData && _dataPtr) {
			if (other.size() * 2 < _reserved) {
				destroy();
			}
		}

		other.copyTo(this);

		return *this;
	}

	DataBuffer& operator=(DataBuffer&& other) noexcept {
		destroy();

		_dataPtr = other._dataPtr;
		_ownsData = other._ownsData;
		_reserved = other._reserved;
		_size = other._size;
		
		other._dataPtr = nullptr;
		other._ownsData = false;
		other._reserved = 0;
		other._size = 0;

		return *this;
	}
};

template <typename T, const size_t Length>
class FixedDataBuffer : public DataBuffer<T> {
private:
	T _data[Length];

public:
    FixedDataBuffer() {}// : DataBuffer((T*)_data, Length, false) {}
};

using DataBufferPtr = std::shared_ptr<DataBuffer<char>>;
using FloatDataBufferPtr = std::shared_ptr<DataBuffer<float>>;
