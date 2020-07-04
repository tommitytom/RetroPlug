#pragma once

namespace micromsg {
	template <typename T>
	class UniquePtr {
	protected:
		ControlBlock* _controlBlock;

	private:
		UniquePtr(ControlBlock* controlBlock) : _controlBlock(controlBlock) {}

	public:
		UniquePtr(): _controlBlock(nullptr) {}
		UniquePtr(UniquePtr& other) { *this = other; }
		~UniquePtr() { release(); }

		UniquePtr& operator=(UniquePtr& other) {
			_controlBlock = other._controlBlock;
			other._controlBlock = nullptr;
			return *this;
		}

		UniquePtr& operator=(std::nullptr_t) {
			release();
			return *this;
		}

		T* get() const { 
			if (_controlBlock) {
				return reinterpret_cast<T*>(_controlBlock + 1);
			}
			
			return nullptr;
		}

		T* operator->() const { return get(); }

		size_t count() const { return _controlBlock->elementCount; }

		void release() {
			if (_controlBlock) {
				_controlBlock->destructor(get(), _controlBlock->elementCount);
				_controlBlock->destructor = nullptr;
				bool success = _controlBlock->queue->enqueue(_controlBlock);
				assert(success);
				_controlBlock = nullptr;
			}
		}

		friend class Allocator;
	};
}