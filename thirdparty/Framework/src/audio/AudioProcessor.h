#pragma once

#include "foundation/Types.h"

namespace fw {
	class AudioProcessor {
	public:
		virtual void onRender(f32* output, const f32* input, uint32 frameCount) = 0;
	};
}
