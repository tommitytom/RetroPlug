#pragma once

namespace micromsg {
	template <typename T>
	class UniquePtr {
	private:
		ControlBlock* _controlBlock;

		UniquePtr(ControlBlock* controlBlock) : _controlBlock(controlBlock) {}

	public:
		UniquePtr() {}
		UniquePtr(UniquePtr& other) { *this = other; }
		~UniquePtr() { destroy(); }

		UniquePtr& operator=(UniquePtr& other) {
			_controlBlock = other._controlBlock;
			other._controlBlock = nullptr;
			return *this;
		}

		T* get() const { return reinterpret_cast<T*>(_controlBlock + 1); }

		T* operator->() const { return get(); }

	private:
		void destroy() {
			if (_controlBlock) {
				_controlBlock->destructor(get());
				_controlBlock->destructor = nullptr;
				bool success = _controlBlock->queue->try_push(_controlBlock);
				assert(success);
			}
		}

		friend class Allocator;
	};
}