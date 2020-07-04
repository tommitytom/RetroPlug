#pragma once

#include "mpmcqueue.h"
#include "concurrentqueue.h"

namespace micromsg {
	struct ControlBlock;
	using DataQueue = moodycamel::ConcurrentQueue<ControlBlock*>;
}
