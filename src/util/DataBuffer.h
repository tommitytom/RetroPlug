#pragma once

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <string_view>

#include "RpMath.h"
#include "Crc32.h"
#include "platform/Types.h"

namespace rp {
	template <typename T>
	class DataBuffer {
	private:
		T* _data = nullptr;
		//size_t _size = 0;
		size_t _size = 0;
		bool _ownsData = false;

	public:
		DataBuffer() {}
		DataBuffer(const DataBuffer& other) { *this = other; }
		DataBuffer(DataBuffer&& other) noexcept { *this = std::move(other); }
		DataBuffer(size_t size) { resize(size); }
		DataBuffer(T* data, size_t size, bool ownsData = false) : _data(data), _size(size), _ownsData(ownsData) {}
		~DataBuffer() { destroy(); }

		bool isOwnerOfData() const {
			return _ownsData;
		}

		T& get(size_t idx) const {
			assert(idx < _size);
			return _data[idx];
		}

		void set(size_t idx, T v) {
			assert(idx < _size);
			_data[idx] = v;
		}

		T& operator[](size_t idx) {
			assert(idx < _size);
			return _data[idx];
		}

		T operator[](size_t idx) const {
			assert(idx < _size);
			return _data[idx];
		}

		std::string_view toString() {
			size_t len = strnlen(_data, _size);
			return std::string_view(_data, len);
		}

		size_t hash(size_t initial = 0) {
			// TODO: Use xxhash here?
			return crc32::update((const void*)_data, _size * sizeof(T), (uint32)initial);
		}

		T back() const {
			assert(_size > 0);
			return _data[_size - 1];
		}

		void write(const T* source, size_t size) {
			assert(size <= _size);
			memcpy(_data, source, size * sizeof(T));
		}

		void write(const DataBuffer<T>& other) {
			write(other.data(), other.size());
		}

		void append(const DataBuffer<T>& other) {
			size_t pos = _size;
			resize(_size + other.size());
			slice(pos, other.size()).write(other.data(), other.size());
		}

		T* data() { return _data; }

		const T* data() const { return _data; }

		size_t size() const { return _size; }

		uint32_t readUint32(size_t pos) {
			return *((uint32_t*)(_data + pos));
		}

		int32_t readInt32(size_t pos) {
			return *((int32_t*)(_data + pos));
		}

		DataBuffer<T> slice(size_t pos, size_t size) {
			assert(pos + size <= _size);
			return DataBuffer<T>(_data + pos, size);
		}

		const DataBuffer<T> slice(size_t pos, size_t size) const {
			assert(pos + size <= _size);
			return DataBuffer<T>(_data + pos, size);
		}

		void clear() {
			memset(_data, 0, _size * sizeof(T));
		}

		void clear(const T& value) {
			for (size_t i = 0; i < _size; ++i) {
				_data[i] = value;
			}
		}

		void reserve(size_t size) {
			// TODO: Needs to track reserved data!

			assert(_size == 0 || _ownsData);
			if (size > _size) {
				T* data = new T[size];

				if (_size > 0) {
					memcpy(data, _data, _size * sizeof(T));
					destroy();
				}

				_data = data;
				_size = size;
				_ownsData = true;
			} else if (_data) {
				_size = size;
			}
		}

		void resize(size_t size) {
			reserve(size);
			_size = size;
		}

		void destroy() {
			if (_ownsData && _data) {
				delete[] _data;
				_data = nullptr;
				_size = 0;
				_ownsData = false;
			}
		}

		size_t copyFrom(const DataBuffer& other) {
			size_t writeAmount = std::min(_size, other.size());
			memcpy(_data, other._data, writeAmount * sizeof(T));
			_size = std::max(_size, writeAmount);
			return writeAmount;
		}

		void copyTo(DataBuffer* target) const {
			target->resize(_size);
			target->write(_data, _size);
		}

		void copyTo(T* target) const {
			memcpy(target, _data, _size * sizeof(T));
		}

		DataBuffer clone() const {
			DataBuffer ret;
			copyTo(&ret);
			return ret;
		}

		bool operator==(const DataBuffer& other) const {
			if (other._size != _size) {
				return false;
			}

			return memcmp(_data, other._data, _size * sizeof(T)) == 0;
		}

		bool operator!=(const DataBuffer& other) const {
			if (other._size != _size) {
				return true;
			}

			return memcmp(_data, other._data, _size * sizeof(T)) != 0;
		}

		DataBuffer& operator=(const DataBuffer& other) {
			if (_ownsData && _data) {
				if (other.size() * 2 < _size) {
					destroy();
				}
			}

			if (other._ownsData) {
				other.copyTo(this);
			} else {
				_ownsData = false;
				_data = other._data;
				_size = other._size;
			}

			return *this;
		}

		DataBuffer& operator=(DataBuffer&& other) noexcept {
			destroy();

			_data = other._data;
			_ownsData = other._ownsData;
			_size = other._size;
			_size = other._size;

			other._data = nullptr;
			other._ownsData = false;
			other._size = 0;
			other._size = 0;

			return *this;
		}
	};

	template <typename T, const size_t Length>
	class FixedDataBuffer : public DataBuffer<T> {
	private:
		T _data[Length];

	public:
		FixedDataBuffer() : DataBuffer<T>((T*)_data, Length, false) {}
	};

	using Uint8Buffer = DataBuffer<uint8>;
	using Float32Buffer = DataBuffer<f32>;
	using Color4Buffer = DataBuffer<Color4>;

	using Uint8BufferPtr = std::shared_ptr<Uint8Buffer>;
	using Float32BufferPtr = std::shared_ptr<Float32Buffer>;
}
