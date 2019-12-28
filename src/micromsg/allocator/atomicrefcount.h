#pragma once

#include <atomic>

namespace micromsg {
	class AtomicRefCount {
	private:
		std::atomic<size_t> _refCount = 0;

	public:
		int increment() { return (int)_refCount.fetch_add(1, std::memory_order_relaxed); }

		bool decrement() { return _refCount.fetch_sub(1, std::memory_order_acq_rel) != 1; }

		bool isOne() const { return _refCount.load(std::memory_order_acquire) == 1; }
	};
}
