#pragma once

#include "foundation/Types.h"

struct AudioSettingsContext {
	f32 sampleRate = 48000.0f;
	uint32 blockSize = 512;
};
