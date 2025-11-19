#pragma once

#include "foundation/ClangClIntellisense.h"

#include <defs.h>
#include "foundation/Types.h"
#include "foundation/DataBuffer.h"
#include "core/CoreComponents.h"
#include "core/MemoryAccessor.h"
#include "sameboy/SameBoyComponents.h"

namespace rp::SameBoyUtil {
	void destroy(SameBoyState* state);
}

namespace rp {
	template <auto fn>
	struct deleter_from_fn {
		template <typename T>
		constexpr void operator()(T* arg) const {
			fn(arg);
		}
	};

	struct SameBoyStateComponent {
		std::unique_ptr<SameBoyState, deleter_from_fn<rp::SameBoyUtil::destroy>> state;
	};
}

namespace rp::SameBoyUtil {
	void spinMs(GB_gameboy_t* gb, f32 ms);

	void spinNs(GB_gameboy_t* gb, f32 ns);

	f32 cyclesToNs(GB_gameboy_t* gb, uint64 cycles);

	f32 cyclesToMs(GB_gameboy_t* gb, uint64 cycles);

	bool setup(const SameBoyComponent& comp, SameBoyState& state, uint32 sampleRate, const SystemLoadComponent& load);

	MemoryAccessor getMemory(SameBoyState& state, MemoryType type, AccessType access = AccessType::ReadWrite);

	void saveState(SameBoyState& state, fw::Uint8Buffer& target);

	void setSampleRate(SameBoyState& state, uint32 sampleRate);

	void setUserData(SameBoyState& state, void* userData);

	void setRenderingDisabled(SameBoyState& state, bool disabled);

	void process(SameBoyStateComponent** systems, size_t systemCount, uint32 sampleCount);

	SystemStateOffsets getStateOffsets(SameBoyState& state);

	void reset(SameBoyState& state);

	//void destroy(SameBoyState* state);
}
