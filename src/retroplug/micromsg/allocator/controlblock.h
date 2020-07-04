#pragma once

#include "atomicrefcount.h"
#include "types.h"

namespace micromsg {
	struct ControlBlock {
		AtomicRefCount refCount;
		DataQueue* queue;
		size_t elementCount;
		void (*destructor)(void*, size_t);
	};
}
