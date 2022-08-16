#pragma once

#include "platform/Types.h"

namespace rp {
	class AudioProcessor {
	public:
		virtual void onRender(f32* output, const f32* input, uint32 frameCount) = 0;
	};
}
