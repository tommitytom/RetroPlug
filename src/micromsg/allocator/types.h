#pragma once

#include "mpmcqueue.h"

namespace micromsg {
	struct ControlBlock;
	using DataQueue = rigtorp::MPMCQueue<ControlBlock*>;
}
