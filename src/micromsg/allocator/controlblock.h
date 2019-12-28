#pragma once

#include "atomicrefcount.h"
#include "types.h"

namespace micromsg {
	struct ControlBlock {
		AtomicRefCount refCount;
		DataQueue* queue;
		void (*destructor)(void*);
	};
}
