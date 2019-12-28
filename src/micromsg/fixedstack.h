#pragma once

#include "mmassert.h"

namespace micromsg {
	template <typename T>
	class Stack {
	private:
		T* _data = nullptr;
		size_t _size = 0;
		size_t _count = 0;

	public:
		Stack() {}
		Stack(size_t size) { resize(size); }
		~Stack() { delete[] _data; }

		void push(const T& value) {
			mm_assert(_count < _size);
			_data[_count++] = value;
		}

		T pop() {
			mm_assert(_count > 0);
			return _data[--_count];
		}

		bool empty() const {
			return _count == 0;
		}

		void resize(size_t size) {
			_size = size;
			_data = new T[size];
		}
	};
}
