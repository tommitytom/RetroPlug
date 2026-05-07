#pragma once

#include <string.h>

namespace micromsg {
	template <const int Size>
	class FixedString {
	private:
		char _data[Size];
		size_t _length = 0;

	public:
		FixedString() {
			clear();
		}

		FixedString(const char* str) {
			*this = str;
		}

		void clear() {
			memset(_data, '\0', Size);
			_length = 0;
		}

		char* data() { return _data; }

		size_t length() const { return _length; }

		size_t size() const { return _size; }

		FixedString& operator=(const FixedString& other) {
			clear();
			memcpy(_data, other._data, other.length());
			_length = other.length();
			return *this;
		}

		FixedString& operator=(const char* other) {
			clear();
			_length = strnlen(other, Size);
			memcpy(_data, other, _length);
			return *this;
		}

		FixedString& operator=(const std::string& other) {
			clear();
			memcpy(_data, other.c_str(), std::min(Size, other.length()));
			return *this;
		}

		std::string_view view() {
			return std::string_view(_data, _length);
		}

		std::string_view view() const {
			return std::string_view(_data, _length);
		}
	};
}
