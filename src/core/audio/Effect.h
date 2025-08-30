#pragma once

#include "audio/AudioBuffer.h"

namespace rp {
	class Effect {
	public:
		Effect() = default;
		virtual ~Effect() = default;

		virtual void process(fw::AudioBuffer& buffer) = 0;
	};
}
