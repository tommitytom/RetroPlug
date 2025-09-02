#pragma once

#include "foundation/Types.h"

namespace rp {
	const size_t PIXEL_WIDTH = 160;
	const size_t PIXEL_HEIGHT = 144;
	const size_t PIXEL_COUNT = (PIXEL_WIDTH * PIXEL_HEIGHT);
	const size_t FRAME_BUFFER_SIZE = (PIXEL_COUNT * 4);
	const size_t AUDIO_SCRATCH_SIZE = 44100;
	constexpr uint32 SAMEBOY_GUID = 0x5A8EB011;
}
