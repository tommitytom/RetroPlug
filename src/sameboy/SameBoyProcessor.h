#pragma once

#include "core/SystemProcessor.h"

namespace rp {
	class SameBoyProcessor final : public SystemProcessor {
	public:
		void process(std::vector<SystemPtr>& systems, uint32 audioFrameCount) override;
	};
}
