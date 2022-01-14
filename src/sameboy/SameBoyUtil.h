#pragma once

#include <gb_struct_def.h>
#include "platform/Types.h"

namespace rp::SameBoyUtil {
	void spinMs(GB_gameboy_t* gb, f32 ms);

	void spinNs(GB_gameboy_t* gb, f32 ns);

	f32 cyclesToNs(GB_gameboy_t* gb, uint64 cycles);

	f32 cyclesToMs(GB_gameboy_t* gb, uint64 cycles);
}
