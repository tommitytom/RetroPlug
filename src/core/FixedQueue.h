#pragma once

#include <array>

namespace rp {
	template <typename T, const size_t Count>
	class FixedQueue {
	private:
		std::array<T, Count> _items;
		size_t _back = 0;
		size_t _front = 0;

	public:
		bool tryPush(T&& item) {
			if (_back < _items.size()) {
				_items[_back++] = item;
				return true;
			}

			return false;
		}

		bool tryPush(const T& item) {
			if (_back < _items.size()) {
				_items[_back++] = item;
				return true;
			}

			return false;
		}

		bool tryPop(T& target) {
			if (_back > _front) {
				target = _items[_front++];
				return true;
			}

			return false;
		}

		void push(T&& item) {
			assert(_back < _items.size());
			_items[_back++] = item;
		}

		T& pop() {
			assert(_front < _back);
			return _items[_front++];
		}

		T& back() {
			assert(_back > _front);
			return _items[_back];
		}

		T& front() {
			assert(_front < _back);
			return _items[_front];
		}

		size_t count() const {
			return _back - _front;
		}

		void reset() {
			_back = 0;
			_front = 0;
		}
	};
}
