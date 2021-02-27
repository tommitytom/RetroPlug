#pragma once

#include "../concurrentqueue.h"

namespace micromsg {
	struct ControlBlock;
	using DataQueue = moodycamel::ConcurrentQueue<ControlBlock*>;
}
