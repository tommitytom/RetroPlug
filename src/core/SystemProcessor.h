#pragma once

#include "core/System.h"

namespace rp {
	class SystemProcessor {
	public:
		virtual void process(std::vector<SystemPtr>& systems, uint32 audioFrameCount) = 0;
	};

	using SystemProcessorPtr = std::shared_ptr<SystemProcessor>;
}
