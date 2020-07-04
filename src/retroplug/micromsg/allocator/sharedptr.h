#pragma once

#include "controlblock.h"

namespace micromsg {
	template <typename T>
	class SharedPtr {
	private:
		ControlBlock* _controlBlock = nullptr;

		SharedPtr(ControlBlock* block) : _controlBlock(block) {
			block->refCount.increment();
		}

	public:
		SharedPtr() {}
		SharedPtr(SharedPtr& other) { *this = other; }
		~SharedPtr() { tryDestroy(); }

		SharedPtr& operator=(SharedPtr& other) {
			if (_controlBlock) {
				tryDestroy();
			}

			if (other._controlBlock) {
				_controlBlock = other._controlBlock;
				_controlBlock->refCount.increment();
			}
		}

		const T* get() const { return static_cast<const T*>(_controlBlock + 1); }
		T* get() const { return static_cast<T*>(_controlBlock + 1); }

		T* operator->() { return get(); }
		const T* operator->() const { return get(); }

		void release() { tryDestroy(); }

	private:
		void tryDestroy() {
			if (_controlBlock && !_controlBlock->refCount.decrement()) {
				_controlBlock->queue->enqueue(static_cast<char*>(_controlBlock));
			}

			_controlBlock = nullptr;
		}

		friend class Allocator;
	};
}