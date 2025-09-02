#pragma once

#include <defs.h>
#include "foundation/Types.h"
#include "foundation/DataBuffer.h"
#include "core/CoreComponents.h"

namespace rp {
	struct SameBoyComponent;
	struct SameBoyStateComponent;
}

namespace rp::SameBoyUtil {
	void spinMs(GB_gameboy_t* gb, f32 ms);

	void spinNs(GB_gameboy_t* gb, f32 ns);

	f32 cyclesToNs(GB_gameboy_t* gb, uint64 cycles);

	f32 cyclesToMs(GB_gameboy_t* gb, uint64 cycles);

	bool setup(const SameBoyComponent& comp, SameBoyStateComponent& state, uint32 sampleRate, const SystemLoadComponent& load);

	void setSampleRate(SameBoyStateComponent& state, uint32 sampleRate);

	void setUserData(SameBoyStateComponent& state, void* userData);

	void process(SameBoyStateComponent** systems, size_t systemCount, uint32 sampleCount);

	void destroy(SameBoyStateComponent& state);
}
